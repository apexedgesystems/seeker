#!/usr/bin/env python3
"""
gen_signatures.py - Regenerate the helpers' formatter signatures.

Replaces the parse.additional_commands section of .cmake-format.yaml with the
output of cmake-genparsers (cmakelang) for the helper files in the leaf check's
HELPER_DIR (the child directory of cmake/ that holds All.cmake) and leaves
everything else in the file as it is. The file's structure is read
with PyYAML, the parser cmake-format itself uses, so the section's boundaries
are the parser's: quoted keys, comments and blank lines are what the parser
says they are. The candidate text is loaded again and compared with the
original, section for section, before the file is written; a difference
anywhere but parse.additional_commands, or a form the script does not edit
(no block mapping at the root, a flow-style parse section, parse or
additional_commands given twice, additional_commands outside parse), stops it
with nothing written, as do a configuration that cannot be read, that is not
valid YAML or that cmake-format's loader cannot construct (an unknown tag),
a cmake-genparsers that is missing or fails, generator output that is not
YAML or does not describe exactly the public helpers the helper directory
defines (the leaf check's own list), and a candidate cmake-format itself
cannot load or cannot use (its configuration constructed in-process from the
candidate, then one call of every helper formatted with it), each with a
one-line message. Past that the values are trusted: a signature the
formatter loads and uses but that is wrong for its helper cannot be told from
a changed helper here, and the format gate over every leaf is that check. The
leaf check fails when the signatures drift from the helpers; run this after
changing a helper's arguments.
"""
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, List, NoReturn, Optional, Tuple

import check_leaf_cmake as leaf
import yaml  # type: ignore[import-untyped]  # cmakelang's yaml extra (PyYAML); no type stubs
from cmakelang import configuration  # type: ignore[import-untyped]  # the formatter's own loader
from cmakelang.format import __main__ as formatter  # type: ignore[import-untyped]  # and its use

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / ".cmake-format.yaml"
HELPERS = sorted((ROOT / leaf.HELPER_DIR).glob("*.cmake"))
SECTION = "parse"
KEY = "additional_commands"


def fail(message: str) -> NoReturn:
    sys.exit("gen_signatures: %s; nothing written" % message)


def generated_lines() -> Tuple[List[str], int]:
    """The additional_commands block at column 0, one helper per top-level
    key, in the shape the format gate's YAML formatter settles on (double
    quotes, list items two spaces under their key)."""
    try:
        gen = subprocess.run(
            ["cmake-genparsers", "-f", "yaml"] + [str(p) for p in HELPERS],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    except FileNotFoundError:
        fail("cmake-genparsers is not on PATH (install cmakelang with its YAML extra)")
    except subprocess.CalledProcessError as error:
        detail = (error.stderr or "").strip().splitlines()
        fail(
            "cmake-genparsers exited %d%s"
            % (error.returncode, (": " + detail[-1]) if detail else "")
        )
    lines = gen.splitlines()
    if "" in lines:  # the processed file names follow a blank line
        lines = lines[: lines.index("")]
    lines = [
        re.sub(r"^(\s*)- ", r"\1  - ", line.replace("nargs: '*'", 'nargs: "*"')) for line in lines
    ]
    count = sum(1 for line in lines if line and not line.startswith(" "))
    return lines, count


def scalar(node: Any) -> Optional[str]:
    return node.value if isinstance(node, yaml.ScalarNode) else None


def is_null(node: Any) -> bool:
    return isinstance(node, yaml.ScalarNode) and node.tag == "tag:yaml.org,2002:null"


def entries(node: Any, what: str) -> list:
    """The (key, value) node pairs of a block mapping; a null node is empty."""
    if node is None or is_null(node):
        return []
    if not isinstance(node, yaml.MappingNode):
        fail("%s must be a mapping" % what)
    if node.flow_style:
        fail("%s must be a block mapping, not flow style" % what)
    return node.value


def lookup(pairs: list, key: str, where: str) -> Optional[Tuple[Any, Any]]:
    hits = [(k, v) for k, v in pairs if scalar(k) == key]
    if len(hits) > 1:
        fail("%s is given %d times under %s" % (key, len(hits), where))
    return hits[0] if hits else None


def end_line(lines: List[str], node: Any) -> int:
    """First line after a value node. The parser's end mark is where the next
    token starts: at the start of a following key (only indentation before
    it) that line is not part of the node; after a flow value on the same
    line, it is."""
    mark = node.end_mark
    if mark.line >= len(lines) or not lines[mark.line][: mark.column].strip():
        return mark.line
    return mark.line + 1


def call_of(name: str, spec: Any) -> str:
    """A representative call of the helper for the formatter to exercise its
    signature: one positional word (the declared count when it is a number),
    each top-level keyword with a value, each top-level flag."""
    pargs = spec.get("pargs", 0) if isinstance(spec, dict) else 0
    words = ["x"] * (pargs if isinstance(pargs, int) and pargs > 0 else 1)
    kwargs = spec.get("kwargs", {}) if isinstance(spec, dict) else {}
    if isinstance(kwargs, dict):
        words += ["%s v" % key for key in kwargs]
    flags = spec.get("flags", []) if isinstance(spec, dict) else []
    if isinstance(flags, list):
        words += [str(flag) for flag in flags]
    return "%s(%s)\n" % (name, " ".join(words))


def trim_back(lines: List[str], start: int, end: int) -> int:
    while end > start and (not lines[end - 1].strip() or lines[end - 1].lstrip().startswith("#")):
        end -= 1
    return end


def main() -> None:
    try:
        text = CONFIG.read_text() if CONFIG.exists() else ""
    except (OSError, UnicodeDecodeError) as error:
        fail("cannot read %s (%s)" % (CONFIG.name, error))
    lines = text.splitlines(keepends=True)
    if lines and not lines[-1].endswith("\n"):
        lines[-1] += "\n"
    generated, count = generated_lines()
    try:
        block = yaml.safe_load("\n".join(generated) + "\n")
    except yaml.YAMLError as error:
        fail("cmake-genparsers output is not valid YAML (%s)" % str(error).splitlines()[0])
    # The output must describe exactly the public helpers the layer defines (the
    # check's own list); anything else would be written over the signatures
    helpers = sorted(name for _, name, _ in leaf.helper_blocks(ROOT) if not name.startswith("_"))
    if not isinstance(block, dict):
        fail("cmake-genparsers output is not a mapping of helper signatures")
    if not all(isinstance(name, str) for name in block):
        fail("cmake-genparsers output has a key that is not a helper name")
    if sorted(block) != helpers:
        missing = " ".join(name for name in helpers if name not in block)
        unexpected = " ".join(name for name in block if name not in helpers)
        fail(
            "cmake-genparsers described %d helpers where %s defines %d%s%s"
            % (
                len(block),
                leaf.HELPER_DIR,
                len(helpers),
                ("; missing: " + missing) if missing else "",
                ("; unexpected: " + unexpected) if unexpected else "",
            )
        )

    try:
        root = yaml.compose(text) if text.strip() else None
    except yaml.YAMLError as error:
        fail("%s is not valid YAML (%s)" % (CONFIG.name, str(error).splitlines()[0]))
    top = entries(root, "the configuration")
    if lookup(top, KEY, "the root"):
        fail("%s must sit under %s:" % (KEY, SECTION))
    section = lookup(top, SECTION, "the root")

    if section is None:
        indent = 2
        body = ["%s:\n" % SECTION, " " * indent + "%s:\n" % KEY]
        body += [" " * (indent + 2) + line + "\n" for line in generated]
        new_lines = lines + body
    else:
        skey, sval = section
        pairs = entries(sval, SECTION)
        indent = pairs[0][0].start_mark.column if pairs else skey.start_mark.column + 2
        body = [" " * indent + "%s:\n" % KEY]
        body += [" " * (indent + 2) + line + "\n" for line in generated]
        existing = lookup(pairs, KEY, SECTION)
        if existing is not None:
            akey, aval = existing
            start = akey.start_mark.line
            end = trim_back(lines, start + 1, max(end_line(lines, aval), start + 1))
        elif pairs:
            start = end = trim_back(
                lines, pairs[-1][0].start_mark.line + 1, end_line(lines, pairs[-1][1])
            )
        else:
            start = end = skey.start_mark.line + 1
        new_lines = lines[:start] + body + lines[end:]

    new_text = "".join(new_lines)
    try:
        expected = yaml.safe_load(text) if text.strip() else None
    except yaml.YAMLError as error:
        fail(
            "%s cannot be loaded as cmake-format loads it (%s)"
            % (CONFIG.name, str(error).splitlines()[0])
        )
    expected = dict(expected or {})
    expected[SECTION] = dict(expected.get(SECTION) or {})
    expected[SECTION][KEY] = block
    try:
        candidate = yaml.safe_load(new_text)
    except yaml.YAMLError as error:
        fail("the candidate is not valid YAML (%s)" % str(error).splitlines()[0])
    if candidate != expected:
        fail("the candidate would change something outside %s.%s" % (SECTION, KEY))
    # The consumer decides what a valid signature is: construct the formatter's
    # configuration from the candidate exactly as cmake-format does when it
    # loads the file, then format one call of every helper with it, in-process;
    # whatever either raises is a rejection of the candidate. A signature the
    # formatter loads and uses but that is wrong for its helper cannot be told
    # from a changed helper here; the format gate over every leaf is that check.
    try:
        config = configuration.Configuration(**candidate)
    except Exception as error:  # the constructor's own exception types are not a contract
        fail(
            "cmake-format rejects the candidate configuration (%s: %s)"
            % (type(error).__name__, str(error).splitlines()[0] if str(error) else "")
        )
    for name in helpers:
        try:
            formatter.process_file(config, call_of(name, block[name]))
        except Exception as error:  # the formatter's own exception types are not a contract
            fail(
                "cmake-format cannot format a call of %s with the candidate signatures (%s: %s)"
                % (name, type(error).__name__, str(error).splitlines()[0] if str(error) else "")
            )

    if new_text != text:
        CONFIG.write_text(new_text)
    print("signatures: %d helpers from %d files" % (count, len(HELPERS)))


if __name__ == "__main__":
    main()
