#!/usr/bin/env python3
"""
gen_helper_reference.py - Render the helper reference of the CMake guide.

The helper directory is the leaf check's HELPER_DIR, the one child of cmake/
that holds All.cmake. Section 5 of the README.md there, between its two marker
lines, is the rendering of the
comment block above each public helper in the directory's *.cmake files, read
with the leaf check's reader: per helper its call form, the first paragraph of
its description and its argument table, grouped by helper file; the rest of the
description stays in the block. A block reads when it has the shape the helpers
share:

    # -------------------------------------------------------------------------
    # <prefix>_add_example(...)         the call form, continued on
    #                                   indented lines
    #
    # The description: prose paragraphs; lines indented further are a
    # literal block.
    #
    # Arguments:
    #   NAME     <target>  required
    #   FLAG               optional flag: notes, continued on lines
    #                      indented to the notes
    # -------------------------------------------------------------------------

The argument rows must name exactly the keywords cmake-genparsers parses from
the helper, a flag without a value and a keyword with one. A block that does
not read, or rendered prose the guide would read as markup, stops the script
with nothing written; so does a cmake/ with no child holding All.cmake or more
than one, which stops the import of the leaf check. The leaf check fails when
the section is not this rendering; run the script after changing a comment
block. A second run changes nothing.

Usage:
    python3 tools/cmake/gen_helper_reference.py [--root DIR]
"""

import argparse
import re
import sys
from pathlib import Path
from typing import Dict, List, NamedTuple, Optional

import check_leaf_cmake as leaf

# =============================== Configuration ================================

BEGIN = "<!-- helper reference: rendered by tools/cmake/gen_helper_reference.py -->"
END = "<!-- end of helper reference -->"
PREAMBLE = [
    "Rendered from the comment block above each public helper by",
    "`tools/cmake/gen_helper_reference.py`, grouped by helper file: the call form,",
    "the first paragraph of the description and the arguments; the rest of the",
    "description is in the block. Edit the comment block and rerun the script; the",
    "leaf check fails while this section and the blocks differ.",
]

RULE = re.compile(r"^#\s*-{10,}$")
ROW = re.compile(r"^  ([A-Z][A-Z0-9_]*)(?: +(\S+))? +((?:required|optional)\b.*)$")
# A word is shown as code when it holds one of these: identifiers, placeholders,
# variables, paths, globs, alternatives, assignments, namespaces, flags, file
# names and extensions, calls, and upper-case keywords and options.
CODE = re.compile(r"[_<>${}/*\\|=#\[\]]|::|^-+\w|^\.\w|\w\.\w|\(\)$|^[A-Z][A-Z0-9]+$")
OPENERS = {")": "(", "]": "[", "}": "{"}
# Prose lines the guide would read as a list item, quote, heading or rule.
MARKUP = re.compile(r"^(?:[-+*] |> ?|#{1,6}(?: |$)|\d+[.)] |[-=_*]{3,}$)")


class BlockError(Exception):
    """A helper comment block, or the guide, that the script cannot render."""


class Argument(NamedTuple):
    keyword: str
    value: str
    notes: str


class Helper(NamedTuple):
    module: str
    name: str
    call: List[str]
    description: List[str]
    arguments: List[Argument]


# =============================== Block Reading ================================


def guide(root: Path) -> Path:
    """The guide whose section 5 is rendered: README.md in the helper directory."""
    return root / leaf.HELPER_DIR / "README.md"


def _text(where: str, line: str) -> str:
    """A comment line's text: the line without "#" and the space after it."""
    if "\t" in line or "```" in line:
        raise BlockError("%s: a tab or a code fence in %r" % (where, line))
    if line == "#":
        return ""
    if line.startswith("# "):
        return line[2:]
    raise BlockError("%s: a block line that is not a '# ' comment: %r" % (where, line))


def parse(module: Path, name: str, block: str) -> Helper:
    """Read one helper's comment block into its call form, description and
    argument rows."""
    where = "%s: the comment block of %s" % (module.name, name)
    lines = [line.strip() for line in block.splitlines()]
    if len(lines) < 3 or not RULE.match(lines[0]) or not RULE.match(lines[-1]):
        raise BlockError("%s is not between two rule lines" % where)
    text = [_text(where, line) for line in lines[1:-1]]
    if not text or not text[0].startswith(name + "("):
        raise BlockError("%s does not open with %s(" % (where, name))

    end = 1
    while end < len(text) and text[end].startswith(" "):
        end += 1
    call = text[:end]
    if end < len(text) and text[end]:
        raise BlockError("%s has no blank line after the call form" % where)
    body = text[end + 1 :]

    table: List[str] = []
    if "Arguments:" in body:
        at = body.index("Arguments:")
        body, table = body[:at], body[at + 1 :]
    while body and not body[-1]:
        body.pop()
    if not body:
        raise BlockError("%s has no description" % where)

    arguments: List[Argument] = []
    for row in table:
        match = ROW.match(row)
        if match:
            arguments.append(Argument(match.group(1), match.group(2) or "", match.group(3)))
        elif arguments and row.startswith("   ") and row.strip():
            last = arguments[-1]
            arguments[-1] = last._replace(notes=last.notes + " " + row.strip())
        else:
            raise BlockError("%s has an argument row that does not read: %r" % (where, row))
    return Helper(module.name, name, call, body, arguments)


def check_keywords(helper: Helper, spec: dict) -> None:
    """The argument rows name exactly the keywords cmake-genparsers parses: a
    flag without a value, a keyword with one."""
    where = "%s: the comment block of %s" % (helper.module, helper.name)
    flags = set(spec.get("pargs", {}).get("flags", None) or [])
    keywords = set(spec.get("kwargs", None) or {})
    rows = [argument.keyword for argument in helper.arguments]
    if len(rows) != len(set(rows)):
        raise BlockError("%s lists a keyword twice" % where)
    missing = sorted((flags | keywords) - set(rows))
    extra = sorted(set(rows) - flags - keywords)
    if missing:
        raise BlockError("%s has no argument row for %s" % (where, " ".join(missing)))
    if extra:
        raise BlockError("%s lists %s, which the helper does not parse" % (where, " ".join(extra)))
    for argument in helper.arguments:
        if argument.keyword in flags and argument.value:
            raise BlockError("%s gives the flag %s a value" % (where, argument.keyword))
        if argument.keyword in keywords and not argument.value:
            raise BlockError("%s gives %s no value" % (where, argument.keyword))


def helpers(root: Path, signatures: Optional[Dict[str, dict]] = None) -> List[Helper]:
    """Every public helper with a block that reads, checked against the
    signatures cmake-genparsers gives (run here when not passed)."""
    if signatures is None:
        signatures = leaf.generated_signatures(root)
    found: List[Helper] = []
    for module, name, block in leaf.helper_blocks(root):
        if name.startswith("_"):
            continue
        helper = parse(module, name, block)
        if name not in signatures:
            raise BlockError(
                "%s: %s is not in the output of cmake-genparsers" % (module.name, name)
            )
        check_keywords(helper, signatures[name])
        found.append(helper)
    unread = sorted(set(signatures) - {helper.name for helper in found})
    if unread:
        raise BlockError("no comment block found for %s" % " ".join(unread))
    return found


# ================================= Rendering ==================================


def code(text: str, in_table: bool = False) -> str:
    """text as an inline code span; a pipe is escaped inside a table."""
    if "`" in text:
        raise BlockError("a backtick in %r: write the block without one" % text)
    return "`%s`" % (text.replace("|", "\\|") if in_table else text)


def word(token: str, in_table: bool) -> str:
    """A token of prose, its code part as a code span and its surrounding
    punctuation as text."""
    match = re.match(r"^([(\[{\"']*)(.*?)([.,;:!?)\]}\"']*)$", token)
    assert match is not None
    lead, core, trail = match.groups()
    while trail and trail[0] in OPENERS and core.count(OPENERS[trail[0]]) > core.count(trail[0]):
        core, trail = core + trail[0], trail[1:]
    if trail.startswith("::"):
        core, trail = core + "::", trail[2:]
    if core and CODE.search(core):
        core = code(core, in_table)
    return lead + core + trail


def prose(text: str, in_table: bool = False) -> str:
    """A line of block prose as guide text: single spaces, code words as code."""
    line = " ".join(word(token, in_table) for token in text.split())
    if not in_table and MARKUP.match(line):
        raise BlockError("prose that the guide would read as markup: %r" % text)
    return line


def first_paragraph(lines: List[str]) -> List[str]:
    """The description up to its first blank line."""
    return lines[: lines.index("")] if "" in lines else lines


def description(lines: List[str]) -> List[str]:
    """Paragraphs of prose and, for lines indented further, literal blocks."""
    out: List[str] = []
    paragraph: List[str] = []
    literal: List[str] = []

    def close() -> None:
        if paragraph:
            out.extend(paragraph + [""])
            paragraph.clear()
        if literal:
            indent = min(len(line) - len(line.lstrip()) for line in literal)
            out.extend(["```text"] + [line[indent:] for line in literal] + ["```", ""])
            literal.clear()

    for line in lines:
        if not line:
            close()
        elif line.startswith(" "):
            if paragraph:
                close()
            literal.append(line.rstrip())
        else:
            if literal:
                close()
            paragraph.append(prose(line))
    close()
    return out


def table(arguments: List[Argument]) -> List[str]:
    """The argument table, aligned as the format gate's markdown formatter
    leaves it."""
    rows = [["Argument", "Value", "Notes"]]
    for argument in arguments:
        value = code(argument.value, in_table=True) if argument.value else ""
        rows.append([code(argument.keyword), value, prose(argument.notes, in_table=True)])
    widths = [max([3] + [len(row[column]) for row in rows]) for column in range(3)]

    def line(cells: List[str]) -> str:
        return "| " + " | ".join(cell.ljust(width) for cell, width in zip(cells, widths)) + " |"

    out = [line(rows[0]), line(["-" * width for width in widths])]
    return out + [line(row) for row in rows[1:]] + [""]


def render(root: Path, signatures: Optional[Dict[str, dict]] = None) -> str:
    """The section between the markers, the markers included."""
    out = [BEGIN, ""] + PREAMBLE + [""]
    module = None
    for helper in helpers(root, signatures):
        if helper.module != module:
            module = helper.module
            out += ["### %s" % code(module), ""]
        call = " ".join(" ".join(helper.call).split())
        try:
            out += ["#### %s" % code(helper.name), "", code(call), ""]
            out += description(first_paragraph(helper.description))
            if helper.arguments:
                out += table(helper.arguments)
        except BlockError as error:
            raise BlockError("%s: the comment block of %s: %s" % (module, helper.name, error))
    return "\n".join(out + [END]) + "\n"


# ================================ Guide Update ================================


def _markers(text: str) -> List[int]:
    """The lines of the two markers in the guide."""
    lines = text.splitlines()
    begins = [i for i, line in enumerate(lines) if line == BEGIN]
    ends = [i for i, line in enumerate(lines) if line == END]
    if len(begins) != 1 or len(ends) != 1 or ends[0] < begins[0]:
        raise BlockError("the guide needs one %r line and, after it, one %r line" % (BEGIN, END))
    return [begins[0], ends[0]]


def section(text: str) -> str:
    """The guide's current section between the markers, the markers included."""
    begin, end = _markers(text)
    return "".join(text.splitlines(keepends=True)[begin : end + 1])


def splice(text: str, rendered: str) -> str:
    """The guide with the section between the markers replaced."""
    begin, end = _markers(text)
    lines = text.splitlines(keepends=True)
    return "".join(lines[:begin]) + rendered + "".join(lines[end + 1 :])


# ================================== Main CLI ==================================


def main() -> None:
    """Render the section and write the guide when it changed."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0].strip())
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="root of a copy of this repository (default: the checkout containing this script)",
    )
    root = parser.parse_args().root.resolve()
    try:
        path = guide(root)
        rendered = render(root)
        text = path.read_text()
        updated = splice(text, rendered)
    except BlockError as error:
        sys.exit("gen_helper_reference: %s; nothing written" % error)
    if updated != text:
        path.write_text(updated)
    print("helper reference: %d lines in %s" % (rendered.count("\n"), path.relative_to(root)))


if __name__ == "__main__":
    main()
