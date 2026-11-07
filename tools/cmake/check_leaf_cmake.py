#!/usr/bin/env python3
"""
check_leaf_cmake.py - Keep leaf CMake files declarative.

Leaf files are the CMakeLists.txt and *.cmake files under src/ and tools/
(LEAF_DIRS), outside hidden directories such as a virtual environment. They
declare sources, dependencies and options through the project helpers in
cmake/seeker/, which construct the targets. The check prints each leaf file's
commands and fails when a leaf file:

  - calls a command that constructs or changes a target or the build settings
    outside the helpers (add_library, add_executable, target_*, and the
    property, custom-target and directory-level equivalents),
  - calls an internal helper (_seeker_*),
  - includes anything other than a seeker/ helper module,
  - has an escaped parenthesis that the lexer reads as structural (\\( or \\)
    at the start of an unquoted argument, for example): a form this check does
    not support; quote the argument instead ("(" or ")"),
  - has single quotes around a parenthesis, which the lexer reads as one quoted
    argument while CMake reads ' as an ordinary character: a form this check
    does not support; write the argument in double quotes instead.

It also fails when the helper signatures in .cmake-format.yaml differ from the
output of cmake-genparsers for cmake/seeker/*.cmake, when a helper's comment
block does not name one of the keywords it parses, and when the helper
reference in cmake/seeker/README.md is not what gen_helper_reference.py renders
from the comment blocks. Leaf files are read through the lexer of cmakelang,
the package that provides cmake-format and cmake-genparsers, so comments,
quoted and bracket arguments are not commands; a file its tokens do not read as
commands (an unterminated bracket argument, for example) fails with its line.
Requires cmakelang.

Usage:
    python3 tools/cmake/check_leaf_cmake.py [--root DIR]
"""

import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Tuple

try:
    from cmakelang import lex
    from cmakelang.common import UserError
except ImportError:
    sys.exit("check_leaf_cmake: cmakelang not found (install cmakelang)")

# =============================== Configuration ================================

# The native libraries under src/ and the tool trees under tools/.
LEAF_DIRS = ("src", "tools")
HELPER_DIR = Path("cmake") / "seeker"
FORMAT_CONFIG = Path(".cmake-format.yaml")

# Commands that construct or change targets or directory build settings: in a
# leaf file each one bypasses a helper that should own it.
TARGET_COMMANDS = re.compile(
    r"^(add_library|add_executable|add_custom_target|add_custom_command"
    r"|add_dependencies|add_test|target_\w+|set_target_properties|set_property"
    r"|set_source_files_properties|set_tests_properties|set_directory_properties"
    r"|include_directories|link_directories|link_libraries|add_compile_options"
    r"|add_compile_definitions|add_definitions|remove_definitions|add_link_options"
    r"|cmake_language)$"
)
INTERNAL_HELPER = re.compile(r"^_seeker_")
HELPER_MODULE = re.compile(r"^seeker/[A-Za-z]+$")

Command = Tuple[str, int, List[str]]

# =============================== Command Reader ===============================

TOKEN = lex.TokenType
# Tokens between arguments and between commands.
SEPARATORS = {
    TOKEN.WHITESPACE,
    TOKEN.NEWLINE,
    TOKEN.COMMENT,
    TOKEN.BRACKET_COMMENT,
    TOKEN.FORMAT_OFF,
    TOKEN.FORMAT_ON,
    TOKEN.BYTEORDER_MARK,
}


class ListFileError(Exception):
    """(line, problem): a leaf file whose tokens do not read as commands."""


def _tokens(text: str) -> List[lex.Token]:
    """cmakelang's tokens for text. ListFileError for a lexer error; for the
    unquoted literal or line comment the lexer returns where CMake finds a
    bracket argument, quoted argument or bracket comment that does not end; and
    for two forms the check does not support: a parenthesis right after a token
    that ends in an odd number of backslashes, which CMake reads as escaped and
    the lexer as structural (it splits "\\(" at the start of an argument into
    "\\" and "("), and a single-quoted literal holding a parenthesis, which the
    lexer reads as one quoted argument while CMake reads ' as an ordinary
    character and the parenthesis as structural."""
    try:
        tokens = lex.tokenize(text)
    except UserError as error:
        message = str(error.msg).splitlines()[0]
        at = re.search(r"(\d+):\d+", message)
        raise ListFileError(int(at.group(1)) if at else 1, message)
    previous = None
    for token in tokens:
        spelling = token.spelling
        if token.type == TOKEN.UNQUOTED_LITERAL and re.match(r'\[=*\[|"', spelling):
            kind = "quoted" if spelling[0] == '"' else "bracket"
            raise ListFileError(token.begin.line, "unterminated %s argument" % kind)
        if token.type == TOKEN.COMMENT and re.match(r"#\[=*\[", spelling):
            raise ListFileError(token.begin.line, "unterminated bracket comment")
        if (
            token.type in (TOKEN.LEFT_PAREN, TOKEN.RIGHT_PAREN)
            and previous is not None
            and previous.end.offset == token.begin.offset
            and re.search(r"(?<!\\)(\\\\)*\\$", previous.spelling)
        ):
            raise ListFileError(
                token.begin.line,
                "escaped parenthesis \\%s in an unquoted argument: not supported by "
                'this check; write the argument quoted, "%s"' % (spelling, spelling),
            )
        if (
            token.type == TOKEN.QUOTED_LITERAL
            and spelling[0] == "'"
            and re.search(r"[()]", spelling)
        ):
            raise ListFileError(
                token.begin.line,
                "single quotes around a parenthesis: not supported by this check "
                "(CMake reads ' as an ordinary character); write the argument in "
                "double quotes",
            )
        previous = token
    return tokens


def _argument(token: lex.Token) -> str:
    """An argument's text: the content of a quoted or bracket argument, the
    spelling of any other token."""
    if token.type == TOKEN.BRACKET_ARGUMENT:
        return lex.parse_bracket_argument(token.spelling)[1]
    if token.type == TOKEN.QUOTED_LITERAL and token.spelling[0] == '"':
        return token.spelling[1:-1]
    return token.spelling


def commands(text: str) -> List[Command]:
    """Return (name, line, arguments) for every command invocation in text.

    A command is a word followed, after nothing but spaces or tabs, by "(";
    parentheses in its arguments nest. Argument tokens with nothing between
    them are one argument: cmakelang splits an unquoted argument at an escape
    it does not list, such as "\\.". Anything else at the top level, or an
    argument list still open at the end, raises ListFileError."""
    found: List[Command] = []
    name = None  # the word of the command being read
    args: List[str] = []
    depth = 0  # its open parentheses; 0 until its "("
    last = None  # its last argument token
    for token in _tokens(text):
        kind = token.type
        if name is None:
            if kind in SEPARATORS:
                continue
            if kind != TOKEN.WORD:
                raise ListFileError(
                    token.begin.line, "expected a command, found %r" % token.spelling
                )
            name, args, depth, last = token, [], 0, None
        elif depth == 0:
            if kind == TOKEN.LEFT_PAREN:
                depth = 1
            elif kind != TOKEN.WHITESPACE:
                break  # the word is not followed by "("
        elif kind == TOKEN.LEFT_PAREN:
            depth += 1
        elif kind == TOKEN.RIGHT_PAREN:
            depth -= 1
            if depth == 0:
                found.append((name.spelling, name.begin.line, args))
                name = None
        elif kind in SEPARATORS:
            continue
        elif last is not None and last.end.offset == token.begin.offset:
            args[-1] += _argument(token)
            last = token
        else:
            args.append(_argument(token))
            last = token
    if name is not None:
        problem = "%s has no closing )" if depth else "expected ( after %s"
        raise ListFileError(name.begin.line, problem % name.spelling)
    return found


# ================================ Leaf Check ==================================


def leaf_files(root: Path) -> List[Path]:
    """Every CMakeLists.txt and *.cmake file under the leaf directories, except
    in hidden directories (a virtual environment, a tool cache)."""
    files: List[Path] = []
    for top in LEAF_DIRS:
        base = root / top
        if base.is_dir():
            files += base.rglob("CMakeLists.txt")
            files += base.rglob("*.cmake")
    visible = [
        p for p in files if not any(part.startswith(".") for part in p.relative_to(root).parts)
    ]
    return sorted(set(visible))


def check_leaf(path: Path, rel: str) -> Tuple[List[str], List[str]]:
    """Return the file's command summary and its violations."""
    try:
        found = commands(path.read_text())
    except ListFileError as error:
        return [], ["%s:%d: %s" % (rel, *error.args)]
    counts: Dict[str, int] = {}
    violations: List[str] = []
    for name, line, args in found:
        lowered = name.lower()
        counts[lowered] = counts.get(lowered, 0) + 1
        where = "%s:%d: %s" % (rel, line, name)
        if TARGET_COMMANDS.match(lowered):
            violations.append(
                "%s: constructs or changes a target outside the helpers; give the "
                "seeker_* call that creates the target an argument for it" % where
            )
        elif INTERNAL_HELPER.match(lowered):
            violations.append("%s: internal helper; call the public seeker_* helper" % where)
        elif lowered == "include" and not (args and HELPER_MODULE.match(args[0])):
            target = args[0] if args else ""
            violations.append(
                "%s(%s): leaf files include only seeker/ helper modules" % (where, target)
            )
    summary = [name if n == 1 else "%s(%d)" % (name, n) for name, n in counts.items()]
    return summary, violations


# ============================ Helper Contract Check ===========================


def _run(args: List[str], root: Path) -> str:
    try:
        done = subprocess.run(
            args, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
    except FileNotFoundError:
        sys.exit("check_leaf_cmake: %s not found (install cmakelang)" % args[0])
    if done.returncode != 0:
        sys.exit("check_leaf_cmake: %s failed:\n%s" % (" ".join(args), done.stderr))
    return done.stdout


def generated_signatures(root: Path, helper_dir: Path = HELPER_DIR) -> Dict[str, dict]:
    """cmake-genparsers' description of every public helper's arguments."""
    modules = [str(p.relative_to(root)) for p in sorted((root / helper_dir).glob("*.cmake"))]
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "signatures.json"
        _run(["cmake-genparsers", "-f", "json", "-o", str(out)] + modules, root)
        return json.loads(out.read_text())


def configured_signatures(root: Path) -> Dict[str, dict]:
    """The helper signatures the formatter configuration carries."""
    dumped = _run(["cmake-format", "-c", str(FORMAT_CONFIG), "--dump-config", "json"], root)
    return json.loads(dumped).get("parse", {}).get("additional_commands", {})


def helper_blocks(root: Path, helper_dir: Path = HELPER_DIR) -> List[Tuple[Path, str, str]]:
    """(module, function, comment block) for each function definition in the
    helpers, in file order and then definition order; the block is the run of
    comment lines directly above the definition."""
    found: List[Tuple[Path, str, str]] = []
    for module in sorted((root / helper_dir).glob("*.cmake")):
        lines = module.read_text().splitlines()
        for index, line in enumerate(lines):
            match = re.match(r"\s*function\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)", line)
            if not match:
                continue
            start = index
            while start > 0 and lines[start - 1].lstrip().startswith("#"):
                start -= 1
            found.append((module, match.group(1), "\n".join(lines[start:index])))
    return found


def comment_blocks(root: Path) -> Dict[str, str]:
    """The comment block directly above each function definition in the helpers."""
    return {name: block for _, name, block in helper_blocks(root)}


def check_helpers(root: Path, generated: Dict[str, dict]) -> Tuple[List[str], List[str]]:
    """Return the helper check summary and its violations; a signature mismatch
    ends with the command that regenerates the signatures."""
    configured = configured_signatures(root)
    violations: List[str] = []
    for name in sorted(set(generated) | set(configured)):
        if name not in configured:
            violations.append("%s: %s has no signature in it" % (FORMAT_CONFIG, name))
        elif name not in generated:
            violations.append("%s: %s is not a helper in %s" % (FORMAT_CONFIG, name, HELPER_DIR))
        elif generated[name] != configured[name]:
            violations.append(
                "%s: the signature of %s differs from the helper" % (FORMAT_CONFIG, name)
            )
    if violations:
        violations.append("regenerate with: python3 tools/cmake/gen_signatures.py")

    blocks = comment_blocks(root)
    for name, spec in sorted(generated.items()):
        keywords = list(spec.get("pargs", {}).get("flags", [])) + list(spec.get("kwargs", {}))
        block = blocks.get(name, "")
        for keyword in keywords:
            if not re.search(r"\b%s\b" % re.escape(keyword), block):
                violations.append(
                    "%s: the comment block of %s does not name %s" % (HELPER_DIR, name, keyword)
                )
    summary = [
        "helper signatures: %s matches cmake-genparsers for %d helpers"
        % (FORMAT_CONFIG, len(generated)),
        "helper comments: every keyword named for %d helpers" % len(generated),
    ]
    return summary, violations


def check_reference(root: Path, generated: Dict[str, dict]) -> Tuple[List[str], List[str]]:
    """Return the helper reference summary and its violations: the guide's
    section between the reference markers is the rendering of the helpers'
    comment blocks."""
    # Imported here: the generator reads the blocks through this module.
    import gen_helper_reference as reference

    try:
        directory = reference.helper_dir(root)
        if directory != HELPER_DIR:
            raise reference.BlockError(
                "the generator's helper directory %s is not %s" % (directory, HELPER_DIR)
            )
        guide = reference.guide(root)
        rendered = reference.render(root, generated)
        current = reference.section(guide.read_text())
    except reference.BlockError as error:
        return [], [str(error)]
    if current != rendered:
        return [], [
            "%s: the helper reference differs from the helpers' comment blocks; regenerate "
            "with: python3 tools/cmake/gen_helper_reference.py" % guide.relative_to(root)
        ]
    summary = "helper reference: %s renders the comment blocks of %d helpers"
    return [summary % (guide.relative_to(root), len(generated))], []


# ================================== Main CLI ==================================


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0].strip())
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="repository root (default: the checkout containing this script)",
    )
    root = parser.parse_args().root.resolve()

    leaf_violations: List[str] = []
    files = leaf_files(root)
    print("leaf CMake files (%d):" % len(files))
    for path in files:
        rel = str(path.relative_to(root))
        summary, found = check_leaf(path, rel)
        print("  %s: %s" % (rel, " ".join(summary)))
        leaf_violations += found

    generated = generated_signatures(root)
    helper_summary, helper_violations = check_helpers(root, generated)
    reference_summary, reference_violations = check_reference(root, generated)
    helper_summary += reference_summary
    helper_violations += reference_violations

    for line in leaf_violations + helper_violations:
        print(line)
    if leaf_violations or helper_violations:
        print("check_leaf_cmake: FAILED")
        sys.exit(1)
    for line in helper_summary:
        print(line)
    print("check_leaf_cmake: OK")


if __name__ == "__main__":
    main()
