#
# Copyright (c) Marcus Holland-Moritz
#
# This file is part of dwarfs.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the “Software”), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# SPDX-License-Identifier: MIT
#

"""Render markdown manual pages to C++ documents or to roff.

render_manpage.py --cpp --name mkdwarfs mkdwarfs.md mkdwarfs_manpage.cpp
render_manpage.py --man mkdwarfs.md mkdwarfs.1
"""

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone

from markdown_it import MarkdownIt
from markdown_it.tree import SyntaxTreeNode

BOLD = "b"
ITALIC = "i"
CODE = "code"
HEAD = "head"
BLOCK = "block"


class _Break:
    def __init__(self, hard):
        self.hard = hard


SOFT_BREAK = _Break(False)
HARD_BREAK = _Break(True)


@dataclass(frozen=True)
class Span:
    text: str
    style: frozenset = frozenset()

    def restyled(self, *tags):
        return Span(self.text, self.style | frozenset(tags))


@dataclass
class Heading:
    level: int
    spans: list


@dataclass
class Paragraph:
    # One entry per source line, because the "term:" / "description"
    # convention used for option lists depends on that boundary.
    lines: list
    # hard[i] is True if the break after lines[i] was a hard break.
    hard: list = field(default_factory=list)

    def rendered(self):
        """Source lines joined at soft breaks, split at hard breaks."""
        out = [list(self.lines[0])]
        for i, spans in enumerate(self.lines[1:]):
            if self.hard[i]:
                out.append(list(spans))
            else:
                out[-1] += [Span(" ")] + list(spans)
        return [merge_spans(l) for l in out]

    def tail(self):
        """This paragraph without its first source line."""
        return Paragraph(self.lines[1:], self.hard[1:])


@dataclass
class Pre:
    lines: list  # list of str


@dataclass
class Table:
    align: list  # "l", "c" or "r" per column
    header: list  # list of cells, each a list of spans
    rows: list  # list of rows, each a list of cells


@dataclass
class ListItem:
    blocks: list


@dataclass
class BulletList:
    items: list
    ordered: bool = False
    start: int = 1


@dataclass
class DefItem:
    term: list  # spans
    blocks: list


@dataclass
class DefList:
    items: list


@dataclass
class Document:
    name: str = ""
    section: str = ""
    tagline: str = ""
    blocks: list = field(default_factory=list)


class Parser:
    def __init__(self, path):
        self.path = path
        self.warnings = []

    def warn(self, node, msg):
        while node is not None and not node.map:
            node = node.parent
        line = node.map[0] + 1 if node is not None else 0
        self.warnings.append(f"{self.path}:{line}: {msg}")

    def inline(self, node, style=frozenset()):
        """Return a list of spans interleaved with break markers."""
        out = []
        for child in node.children or []:
            t = child.type
            if t == "text":
                out.append(Span(child.content, style))
            elif t == "code_inline":
                out.append(Span(child.content, style | {CODE}))
            elif t == "strong":
                out.extend(self.inline(child, style | {BOLD}))
            elif t == "em":
                out.extend(self.inline(child, style | {ITALIC}))
            elif t == "link":
                out.extend(self.link(child, style))
            elif t == "softbreak":
                out.append(SOFT_BREAK)
            elif t == "hardbreak":
                out.append(HARD_BREAK)
            elif t == "html_inline":
                if not child.content.lstrip().startswith("<!--"):
                    self.warn(
                        child,
                        f"inline HTML {child.content.strip()!r} dropped; "
                        "use markdown markup instead",
                    )
            elif t in ("s", "image"):
                self.warn(child, f"unsupported inline element '{t}', ignored")
                out.extend(self.inline(child, style))
            else:
                self.warn(child, f"unhandled inline element '{t}', ignored")
        return out

    def link(self, node, style):
        """Links can't be followed from a manual page, so:

        - an in-document anchor becomes emphasised text,
        - a link to another page of ours, or to a file in the repository,
          keeps its text and drops the target,
        - anything with a URL scheme keeps the URL, since that is the only
          way a reader can get at it.
        """
        href = node.attrs.get("href", "")
        if href.startswith("#"):
            return self.inline(node, style | {ITALIC})
        text = self.inline(node, style)
        if re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", href):
            return text + [Span(" ", style), Span(href, style | {ITALIC})]
        return text

    def paragraph(self, node):
        lines = [[]]
        hard = []
        for s in self.inline(node.children[0]):
            if s in (SOFT_BREAK, HARD_BREAK):
                lines.append([])
                hard.append(s is HARD_BREAK)
            else:
                lines[-1].append(s)
        return Paragraph([merge_spans(l) for l in lines], hard)

    def blocks(self, nodes):
        out = []
        for node in nodes:
            t = node.type
            if t == "heading":
                spans = merge_spans(self.inline(node.children[0]))
                out.append(Heading(int(node.tag[1:]), spans))
            elif t == "paragraph":
                out.append(self.paragraph(node))
            elif t in ("fence", "code_block"):
                out.append(Pre(node.content.rstrip("\n").split("\n")))
            elif t in ("bullet_list", "ordered_list"):
                out.append(self.list(node))
            elif t == "table":
                out.append(self.table(node))
            elif t == "html_block":
                pass  # comments
            elif t == "hr":
                pass
            else:
                self.warn(node, f"unhandled block element '{t}', ignored")
        return out

    ALIGN = {"text-align:center": "c", "text-align:right": "r"}

    def table(self, node):
        def cells(row):
            return [
                merge_spans(self.inline(c.children[0])) if c.children else []
                for c in row.children
            ]

        header, rows, align = [], [], []
        for section in node.children:
            for row in section.children:
                if section.type == "thead":
                    header = cells(row)
                    align = [
                        self.ALIGN.get(c.attrs.get("style", ""), "l")
                        for c in row.children
                    ]
                else:
                    rows.append(cells(row))
        return Table(align, header, rows)

    def list(self, node):
        items = [ListItem(self.blocks(child.children)) for child in node.children]

        if node.type == "ordered_list":
            start = node.attrs.get("start", 1)
            return BulletList(items, ordered=True, start=int(start))

        deflist = self.maybe_deflist(node, items)
        return deflist if deflist is not None else BulletList(items)

    @staticmethod
    def is_definition_shaped(item):
        """A definition item starts with a code span and its first line ends
        with a colon, e.g.

            - `-i`, `--input=`*file*:
              Path to the filesystem image.
        """
        if not item.blocks or not isinstance(item.blocks[0], Paragraph):
            return False
        para = item.blocks[0]
        first = para.lines[0]
        if not first or CODE not in first[0].style:
            return False
        if not first[-1].text.rstrip().endswith(":"):
            return False
        return len(para.lines) > 1 or len(item.blocks) > 1

    def maybe_deflist(self, node, items):
        shaped = [self.is_definition_shaped(i) for i in items]
        if 2 * sum(shaped) < len(items):
            return None

        for child, ok in zip(node.children, shaped):
            if not ok:
                self.warn(
                    child,
                    "list item does not look like a definition, but the "
                    "surrounding list does; check for a missing ':'",
                )

        out = []
        for item in items:
            para = item.blocks[0]
            term = list(para.lines[0])
            # Drop the trailing colon, it's just markdown convention
            if term[-1].text.rstrip().endswith(":"):
                stripped = term[-1].text.rstrip()[:-1]
                if stripped:
                    term[-1] = Span(stripped, term[-1].style)
                else:
                    term.pop()
            tail = para.tail()
            blocks = ([tail] if tail.lines else []) + item.blocks[1:]
            out.append(DefItem(merge_spans(term), blocks))
        return DefList(out)

    def parse(self, text):
        md = MarkdownIt("commonmark").enable("table")
        tree = SyntaxTreeNode(md.parse(text))
        blocks = self.blocks(tree.children)

        doc = Document()
        if blocks and isinstance(blocks[0], Heading) and blocks[0].level == 1:
            title = spans_to_text(blocks[0].spans)
            m = re.match(r"^\s*(\S+)\((\w+)\)\s*--\s*(.*?)\s*$", title)
            if m:
                doc.name, doc.section, doc.tagline = m.groups()
            else:
                doc.name = title
        doc.blocks = blocks
        return doc


def merge_spans(spans):
    out = []
    for s in spans:
        if out and out[-1].style == s.style:
            out[-1] = Span(out[-1].text + s.text, s.style)
        else:
            out.append(s)
    return [s for s in out if s.text]


def shift_spaces(spans):
    out = [Span(s.text, s.style) for s in spans]
    for i in range(len(out) - 1):
        text = out[i].text
        if len(text) > 1 and text.endswith(" ") and not text.endswith("  "):
            out[i] = Span(text[:-1], out[i].style)
            out[i + 1] = Span(" " + out[i + 1].text, out[i + 1].style)
    return [s for s in out if s.text]


def spans_to_text(spans):
    return "".join(s.text for s in spans)


INDENT = 4
BODY = 2 * INDENT
DEF_BODY = 2 * INDENT
CODE_INDENT = INDENT


@dataclass
class Line:
    indent_first: int
    indent_next: int
    spans: list
    no_wrap: bool = False


def blank():
    return Line(0, 0, [])


class Layout:
    """Turns the block tree into a flat list of lines."""

    def __init__(self):
        self.lines = []

    def emit(self, line):
        self.lines.append(line)

    def document(self, doc):
        for block in doc.blocks:
            self.block(block, BODY)
        return self.lines

    def block(self, block, margin):
        if isinstance(block, Heading):
            indent = 0 if block.level <= 2 else INDENT
            self.emit(Line(indent, indent, [s.restyled(HEAD) for s in block.spans]))
            if block.level == 1:
                self.emit(blank())
        elif isinstance(block, Paragraph):
            self.paragraph(block, margin)
            self.emit(blank())
        elif isinstance(block, Pre):
            for text in block.lines:
                self.emit(
                    Line(
                        margin + CODE_INDENT,
                        margin + CODE_INDENT,
                        [Span(text, frozenset({BLOCK}))] if text else [],
                        no_wrap=True,
                    )
                )
            self.emit(blank())
        elif isinstance(block, Table):
            self.table(block, margin)
        elif isinstance(block, DefList):
            for item in block.items:
                self.def_item(item, margin)
        elif isinstance(block, BulletList):
            self.bullet_list(block, margin)
        else:
            raise AssertionError(f"unexpected block {block!r}")

    def table(self, table, margin):
        rows = ([table.header] if table.header else []) + table.rows
        columns = max(len(r) for r in rows)
        text = [[spans_to_text(c) for c in r] + [""] * (columns - len(r)) for r in rows]
        width = [max(len(r[i]) for r in text) for i in range(columns)]
        align = list(table.align) + ["l"] * (columns - len(table.align))

        def justify(cell, i):
            if align[i] == "r":
                return cell.rjust(width[i])
            if align[i] == "c":
                return cell.center(width[i])
            return cell.ljust(width[i])

        def row(cells, style):
            line = "  ".join(justify(c, i) for i, c in enumerate(cells)).rstrip()
            self.emit(
                Line(margin, margin, [Span(line, style)] if line else [], no_wrap=True)
            )

        for i, cells in enumerate(text):
            head = bool(table.header) and i == 0
            row(cells, frozenset({BOLD}) if head else frozenset())
            if head:
                row(["-" * w for w in width], frozenset({BLOCK}))
        self.emit(blank())

    def paragraph(self, para, margin, indent_first=None):
        for i, spans in enumerate(para.rendered()):
            first = margin if i or indent_first is None else indent_first
            self.emit(Line(first, margin, spans))

    def def_item(self, item, margin):
        body = margin + DEF_BODY
        term_len = len(spans_to_text(item.term))
        first = item.blocks[0] if item.blocks else None

        # A short term shares its line with the start of the description.
        if term_len < DEF_BODY - 1 and isinstance(first, Paragraph) and first.lines:
            pad = Span(" " * (DEF_BODY - term_len))
            head = first.rendered()
            self.emit(Line(margin, body, merge_spans(item.term + [pad] + head[0])))
            for spans in head[1:]:
                self.emit(Line(body, body, spans))
            self.emit(blank())
            rest = item.blocks[1:]
        else:
            self.emit(Line(margin, body, item.term))
            rest = item.blocks

        for block in rest:
            self.block(block, body)

    def bullet_list(self, lst, margin):
        markers = self.markers(lst)
        width = max(len(m) for m in markers)
        body = margin + width
        for marker, item in zip(markers, lst.items):
            self.item_with_marker(marker.ljust(width), item, margin, body)

    @staticmethod
    def markers(lst):
        if lst.ordered:
            return [f"{lst.start + i}. " for i in range(len(lst.items))]
        return ["- "] * len(lst.items)

    def item_with_marker(self, marker, item, margin, body):
        start = len(self.lines)
        for block in item.blocks:
            self.block(block, body)

        if len(self.lines) == start:
            self.emit(Line(margin, body, [Span(marker.rstrip())]))
            self.emit(blank())
            return

        head = self.lines[start]
        if head.no_wrap:
            self.lines.insert(start, Line(margin, body, [Span(marker.rstrip())]))
        else:
            head.indent_first = margin
            head.spans = merge_spans([Span(marker)] + head.spans)


STYLES = [
    (BOLD, "fmt::emphasis::bold"),
    (ITALIC, "fmt::emphasis::italic"),
    (HEAD, "fmt::fg(fmt::terminal_color::bright_green) | fmt::emphasis::bold"),
    (CODE, "fmt::fg(fmt::terminal_color::bright_blue) | fmt::emphasis::bold"),
    (BLOCK, "fmt::emphasis::faint"),
]


def cxx_style(style):
    parts = [expr for tag, expr in STYLES if tag in style]
    return " | ".join(parts) if parts else "{}"


def cxx_string(text):
    if ')"' not in text and "\\" not in text:
        return f'R"({text})"'
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def render_cpp(doc, name):
    lines = Layout().document(doc)

    elements = []  # flat list of (style, text)
    spans = []  # (offset, count) per line
    for line in lines:
        line_spans = shift_spaces(line.spans)
        spans.append((len(elements), len(line_spans)))
        elements.extend((cxx_style(s.style), s.text) for s in line_spans)

    out = []
    out.append("// Generated from markdown by render_manpage.py -- do not edit.\n")
    out.append("\n")
    out.append("#include <array>\n")
    out.append("\n")
    out.append("#include <dwarfs/tool/manpage.h>\n")
    out.append("\n")
    out.append("namespace dwarfs::tool::manpage {\n")
    out.append("\n")
    out.append("// NOLINTBEGIN(readability-*)\n")
    out.append("\n")
    out.append("namespace {\n")
    out.append("\n")

    out.append(f"constexpr std::array<element, {len(elements)}> const elements{{{{\n")
    for style, text in elements:
        out.append(f"    {{{style}, {cxx_string(text)}}},\n")
    out.append("}};\n\n")

    out.append(f"constexpr std::array<line, {len(lines)}> const document_array{{{{\n")
    for line, (offset, count) in zip(lines, spans):
        span = f"{{elements.data() + {offset}, {count}}}" if count else "{}"
        flag = ", true" if line.no_wrap else ""
        out.append(f"    {{{line.indent_first}, {line.indent_next}, {span}{flag}}},\n")
    out.append("}};\n\n")

    out.append("} // namespace\n")
    out.append("\n")
    out.append("// NOLINTEND(readability-*)\n")
    out.append("\n")
    out.append(f"document get_{name}_manpage() {{ return document_array; }}\n")
    out.append("\n")
    out.append("} // namespace dwarfs::tool::manpage\n")

    return "".join(out)


def walk(blocks):
    """Yield every block, descending into list items."""
    for block in blocks:
        yield block
        if isinstance(block, (BulletList, DefList)):
            for item in block.items:
                yield from walk(item.blocks)


ROFF_FONT = {CODE: "B", BOLD: "B", ITALIC: "I"}

ROFF_CHARS = {
    "\u2014": "\\(em",
    "\u2013": "\\(en",
    "\u201c": "\\(lq",
    "\u201d": "\\(rq",
    "\u2018": "\\(oq",
    "\u2019": "\\(cq",
    "\u2026": "\\|.\\|.\\|.",
}


def roff_escape(text):
    text = text.replace("\\", "\\e").replace("-", "\\-")
    for char, esc in ROFF_CHARS.items():
        text = text.replace(char, esc)
    return text


def roff_arg(text):
    """Quote a macro argument.

    Embedded double quotes are written as \\(dq rather than by doubling
    them, which is unambiguous for both groff and mandoc.
    """
    return '"' + text.replace('"', "\\(dq") + '"'


ROFF_MAX_LINE = 76


def roff_wrap(text):
    out, cur = [], ""
    for word in text.split(" "):
        if cur and len(cur) + 1 + len(word) > ROFF_MAX_LINE:
            out.append(cur)
            cur = word
        else:
            cur = f"{cur} {word}" if cur else word
    if cur:
        out.append(cur)
    return out


def roff_text_line(text):
    return "\\&" + text if text[:1] in (".", "'", " ") else text


def roff_spans(spans):
    out = []
    for s in spans:
        text = roff_escape(s.text)
        font = next((ROFF_FONT[t] for t in (CODE, BOLD, ITALIC) if t in s.style), None)
        out.append(f"\\f{font}{text}\\fR" if font else text)
    return "".join(out)


class Roff:
    def __init__(self):
        self.out = []

    def line(self, text):
        self.out.append(text + "\n")

    def text(self, spans):
        for chunk in roff_wrap(roff_spans(spans)):
            self.line(roff_text_line(chunk))

    def document(self, doc, date, source, manual):
        if any(isinstance(b, Table) for b in walk(doc.blocks)):
            self.line("'\\\" t")

        fields = [doc.name.upper(), doc.section, date, source, manual]
        while len(fields) > 2 and not fields[-1]:
            fields.pop()
        self.line(".TH " + " ".join(roff_arg(f) for f in fields))

        if doc.tagline:
            self.line(".SH " + roff_arg("NAME"))
            self.line(f"\\fB{roff_escape(doc.name)}\\fR \\- {roff_escape(doc.tagline)}")

        previous = None
        for block in doc.blocks:
            if isinstance(block, Heading) and block.level == 1:
                continue
            self.block(block, sep=None if isinstance(previous, Heading) else ".P")
            previous = block
        return "".join(self.out)

    def block(self, block, sep=".P"):
        """`sep` is the macro that separates this block from the previous
        one: ".P", ".sp", or None where the context already provides the
        break."""
        if isinstance(block, Heading):
            macro = ".SH" if block.level == 2 else ".SS"
            self.line(f"{macro} {roff_arg(roff_spans(block.spans))}")
        elif isinstance(block, Paragraph):
            if sep:
                self.line(sep)
            for i, spans in enumerate(block.lines):
                if i and block.hard[i - 1]:
                    self.line(".br")
                if spans:
                    self.text(spans)
        elif isinstance(block, Pre):
            self.line(".sp")
            self.line(".RS 4")
            self.line(".nf")
            for text in block.lines:
                self.line(roff_text_line(roff_escape(text)) if text else "")
            self.line(".fi")
            self.line(".RE")
        elif isinstance(block, Table):
            self.table(block)
        elif isinstance(block, DefList):
            for item in block.items:
                self.line(".TP")
                self.text(item.term)
                self.blocks_in_item(item.blocks)
        elif isinstance(block, BulletList):
            markers = Layout.markers(block)
            for marker, item in zip(markers, block.items):
                tag = "\\(bu" if not block.ordered else roff_escape(marker.strip())
                self.line(f".IP {roff_arg(tag)} 4")
                self.blocks_in_item(item.blocks)
        else:
            raise AssertionError(f"unexpected block {block!r}")

    def table(self, table):
        rows = ([table.header] if table.header else []) + table.rows
        columns = max(len(r) for r in rows)
        align = list(table.align) + ["l"] * (columns - len(table.align))

        self.line(".TS")
        self.line("allbox;")
        specs = []
        if table.header:
            specs.append(" ".join(a + "b" for a in align))
        specs.append(" ".join(align))
        for i, spec in enumerate(specs):
            self.line(spec + ("." if i == len(specs) - 1 else ""))
        for row in rows:
            cells = [roff_spans(c) for c in row]
            cells += [""] * (columns - len(cells))
            self.line("\t".join(cells))
        self.line(".TE")

    def blocks_in_item(self, blocks):
        head = blocks[:1] if blocks and isinstance(blocks[0], Paragraph) else []
        for block in head:
            self.block(block, sep=None)

        tail = blocks[len(head) :]
        if tail:
            self.line(".RS")
            for i, block in enumerate(tail):
                self.block(block, sep=".sp" if i == 0 else ".P")
            self.line(".RE")


def input_date(path):
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    stamp = int(epoch) if epoch else os.stat(path).st_mtime
    date = datetime.fromtimestamp(stamp, timezone.utc)
    return f"{date:%B} {date.day}, {date.year}"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    fmt = ap.add_mutually_exclusive_group(required=True)
    fmt.add_argument("--cpp", action="store_true", help="emit a C++ document")
    fmt.add_argument("--man", action="store_true", help="emit roff")
    ap.add_argument("--name", help="document name (C++ symbol); default: from title")
    ap.add_argument(
        "--date",
        help="date for the .TH line; defaults to the modification time of "
        "the input (or SOURCE_DATE_EPOCH, if set)",
    )
    ap.add_argument("--source", default="", help="source for the .TH line")
    ap.add_argument("--manual", default="", help="manual for the .TH line")
    ap.add_argument("--strict", action="store_true", help="turn warnings into errors")
    ap.add_argument("input")
    ap.add_argument("output", help="output file, or '-' for stdout")
    args = ap.parse_args()

    with open(args.input, "r", encoding="utf-8") as fin:
        parser = Parser(args.input)
        doc = parser.parse(fin.read())

    for w in parser.warnings:
        print(f"warning: {w}", file=sys.stderr)
    if args.strict and parser.warnings:
        return 1

    if args.cpp:
        name = args.name or doc.name
        if not name:
            print("error: cannot determine document name", file=sys.stderr)
            return 1
        text = render_cpp(doc, re.sub(r"\W", "_", name))
    else:
        date = args.date if args.date is not None else input_date(args.input)
        text = Roff().document(doc, date, args.source, args.manual)

    if args.output == "-":
        sys.stdout.write(text)
    else:
        with open(args.output, "w", encoding="utf-8") as fout:
            fout.write(text)

    return 0


if __name__ == "__main__":
    sys.exit(main())
