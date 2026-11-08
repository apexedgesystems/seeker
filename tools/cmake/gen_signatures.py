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
with nothing written. The leaf check fails when the signatures drift from the
helpers; run this after changing a helper's arguments.
"""
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, List, NoReturn, Optional, Tuple

import check_leaf_cmake as leaf
import yaml  # type: ignore[import-untyped]  # cmakelang's yaml extra (PyYAML); no type stubs

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
    gen = subprocess.run(
        ["cmake-genparsers", "-f", "yaml"] + [str(p) for p in HELPERS],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
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


def trim_back(lines: List[str], start: int, end: int) -> int:
    while end > start and (not lines[end - 1].strip() or lines[end - 1].lstrip().startswith("#")):
        end -= 1
    return end


def main() -> None:
    text = CONFIG.read_text() if CONFIG.exists() else ""
    lines = text.splitlines(keepends=True)
    if lines and not lines[-1].endswith("\n"):
        lines[-1] += "\n"
    generated, count = generated_lines()
    block = yaml.safe_load("\n".join(generated) + "\n")

    root = yaml.compose(text) if text.strip() else None
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
    expected = yaml.safe_load(text) if text.strip() else None
    expected = dict(expected or {})
    expected[SECTION] = dict(expected.get(SECTION) or {})
    expected[SECTION][KEY] = block
    try:
        candidate = yaml.safe_load(new_text)
    except yaml.YAMLError as error:
        fail("the candidate is not valid YAML (%s)" % str(error).splitlines()[0])
    if candidate != expected:
        fail("the candidate would change something outside %s.%s" % (SECTION, KEY))

    if new_text != text:
        CONFIG.write_text(new_text)
    print("signatures: %d helpers from %d files" % (count, len(HELPERS)))


if __name__ == "__main__":
    main()
