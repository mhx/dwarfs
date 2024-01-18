import mistletoe
import sys


class RenderContext:
    def __init__(self):
        self.line = 0
        self.level = 0
        self.indent = 4
        self.section = None

    def get_indent(self, add=0):
        return self.indent * (max(0, self.level + add))


class Element:
    def __init__(self, tags, content=None, comment=None):
        self.tags = tags
        self.content = content
        self.comment = comment

    def __repr__(self):
        return f"Element({self.tags}, {self.content}, {self.comment})"

    def tags_to_style(self):
        style = []
        if "b" in self.tags:
            style.append("fmt::emphasis::bold")
        if "i" in self.tags:
            style.append("fmt::emphasis::italic")
        if "head" in self.tags:
            style.append(
                "fmt::fg(fmt::terminal_color::bright_green) | fmt::emphasis::bold"
            )
        if "code" in self.tags:
            style.append(
                "fmt::fg(fmt::terminal_color::bright_blue) | fmt::emphasis::bold"
            )
        if "block" in self.tags:
            style.append("fmt::emphasis::faint")
        if len(style) == 0:
            return "{}"
        return " | ".join(style)

    def render(self, context):
        return f'{{{self.tags_to_style()} /* {self.tags} */, R"({self.content})"}} /* {self.comment} */'


def apply(elements, *tags):
    return [Element({*tags, *e.tags}, e.content, e.comment) for e in elements]


class Line:
    def __init__(self, elements=None, indent_first=0, indent=None, comment=None):
        self.elements = [] if elements is None else elements
        self.indent_first = indent_first
        self.indent = indent_first if indent is None else indent
        self.comment = comment

    def split(self):
        rv = []
        cur = []
        indent_first = self.indent_first
        for e in self.elements:
            if e.comment == "line break":
                rv.append(Line(cur, indent_first, self.indent, comment=self.comment))
                indent_first = self.indent
                cur = []
            else:
                cur.append(e)
        rv.append(Line(cur, indent_first, self.indent, comment=self.comment))
        return rv

    def join(self):
        rv = []
        for e in self.elements:
            if e.comment == "line break":
                rv.append(Element(set(), " ", "whitespace"))
            else:
                rv.append(e)
        return Line(rv, self.indent_first, self.indent, comment=self.comment)

    def render(self, context):
        template = (
            "constexpr uint32_t const line{0}_indent_first{{{2}}};\n"
            "constexpr uint32_t const line{0}_indent_next{{{3}}};\n"
            "constexpr std::array<element, {1}> const line{0}_elements{{{{\n"
            "{4}"
            "}}}};\n\n"
        )
        rv = ""
        if self.comment is not None:
            rv += "// " + self.comment + "\n"
        rv += template.format(
            context.line,
            len(self.elements),
            self.indent_first,
            self.indent,
            "".join([f"    {e.render(context)},\n" for e in self.elements]),
        )
        context.line += 1
        return rv


class Paragraph:
    def __init__(self, elements, comment=None):
        self.elements = elements
        self.comment = comment

    def render(self, context, indent_first=None, indent=None):
        if indent_first is None:
            indent_first = context.get_indent()
        if indent is None:
            indent = indent_first
        line = Line(self.elements, indent_first, indent, comment=self.comment)
        if context.section is not None and context.section == "SYNOPSIS":
            lines = line.split()
        else:
            lines = [line.join()]
        return "".join([l.render(context) for l in lines]) + Line().render(context)


class Heading:
    def __init__(self, elements, level, section=None, comment=None):
        self.elements = elements
        self.level = level
        self.section = section
        self.comment = comment

    def __repr__(self):
        return f"Heading({self.level}, {self.section}, {self.comment})"

    def render(self, context):
        context.level = 2
        indent = context.get_indent(-2 if self.level <= 2 else -1)
        context.section = self.section
        rv = Line(apply(self.elements, "head"), indent, comment=self.comment).render(
            context
        )
        if self.level == 1:
            rv += Line().render(context)
        return rv


class ListItem:
    def __init__(self, paragraphs, comment=None):
        self.paragraphs = paragraphs
        self.comment = comment

    def __repr__(self):
        return f"ListItem({self.paragraphs}, {self.comment})"

    def render(self, context):
        rv = ""
        for i, p in enumerate(self.paragraphs):
            handled = False
            if i == 0:
                content_len = 0
                for i in range(len(p.elements) - 1):
                    cur = p.elements[i]
                    nxt = p.elements[i + 1]
                    content_len += len(cur.content)
                    if cur.content.endswith(":") and nxt.comment == "line break":
                        cur.content = cur.content[:-1]
                        content_len -= 1
                        if content_len < (2 * context.indent - 1):
                            cur.content += " " * (2 * context.indent - content_len)
                            p.elements = p.elements[: i + 1] + p.elements[i + 2 :]
                            rv += p.render(
                                context,
                                indent_first=context.get_indent(),
                                indent=context.get_indent(2),
                            )
                        else:
                            rv += Line(
                                p.elements[:i + 1], context.get_indent(), comment=p.comment
                            ).render(context)
                            p.elements = p.elements[i + 2 :]
                            rv += p.render(context, indent_first=context.get_indent(2))
                        handled = True
                        break
            if not handled:
                rv += p.render(context)
        return rv


class BlockCode:
    def __init__(self, element):
        self.element = element

    def render(self, context):
        lines = self.element.content.split("\n")
        rv = ""
        for line in lines:
            rv += Line(
                [Element({"block"}, line)],
                context.get_indent(1),
                comment=self.element.comment,
            ).render(context)
        return rv


class ManpageRenderer(mistletoe.base_renderer.BaseRenderer):
    def __init__(self, document_name, *extras, **kwargs):
        self.__document_name = document_name
        super().__init__(*extras, **kwargs)

    def render_inner(self, token, tags=None):
        rv = []
        for child in token.children:
            c = self.render(child)
            if isinstance(c, list):
                rv.extend(c)
            else:
                rv.append(c)
        if tags is not None:
            for child in rv:
                child.tags.update(tags)
        return rv

    @staticmethod
    def render_thematic_break(token):
        raise NotImplementedError

    @staticmethod
    def render_line_break(token):
        return Element(set(), "", "line break")

    def render_inline_code(self, token):
        assert len(token.children) == 1
        return Element({"code"}, token.children[0].content, "inline code")

    def render_raw_text(self, token, escape=True):
        return Element(set(), token.content, "raw text")

    def render_strikethrough(self, token):
        raise NotImplementedError

    def render_escape_sequence(self, token):
        return self.render_inner(token)

    def render_strong(self, token):
        return self.render_inner(token, {"b"})

    def render_emphasis(self, token):
        return self.render_inner(token, {"i"})

    def render_image(self, token):
        raise NotImplementedError

    def render_heading(self, token):
        inner = self.render_inner(token, {"h{}".format(token.level)})
        section = None
        if len(inner) == 1:
            assert isinstance(inner[0], Element)
            section = inner[0].content
        return Heading(inner, token.level, section)

    def render_paragraph(self, token):
        inner = self.render_inner(token)
        return Paragraph(inner, "paragraph")

    def render_block_code(self, token):
        inner = self.render_inner(token)
        assert len(inner) == 1
        assert isinstance(inner[0], Element)
        return BlockCode(inner[0])

    def render_list(self, token):
        return [self.render(child) for child in token.children]

    def render_list_item(self, token):
        inner = self.render_inner(token)
        assert isinstance(inner, list)
        assert isinstance(inner[0], Paragraph)
        return ListItem(inner, "list item")

    def render_table(self, token):
        raise NotImplementedError

    def render_table_row(self, token):
        raise NotImplementedError

    def render_math(self, token):
        raise NotImplementedError

    def render_table_cell(self, token):
        raise NotImplementedError

    def render_document(self, token):
        rv = """#include <array>
#include "dwarfs/manpage.h"

namespace dwarfs::manpage {

namespace {
"""
        ctx = RenderContext()
        for child in token.children:
            r = self.render(child)
            rv += f"#if 0\n{r}\n#endif\n"
            if isinstance(r, list):
                for e in r:
                    rv += e.render(ctx)
            else:
                rv += r.render(ctx)
        rv += f"constexpr std::array<line, {ctx.line}> const document_array{{{{\n"
        for i in range(ctx.line):
            rv += f"    {{line{i}_indent_first, line{i}_indent_next, line{i}_elements}},\n"
        rv += f"""}}}};
}} // namespace

document get_{self.__document_name}_manpage() {{ return document_array; }}

}} // namespace dwarfs::manpage
"""
        return rv


doc_name = sys.argv[1]
input_file = sys.argv[2]
output_file = sys.argv[3]

with open(input_file, "r") as fin:
    with ManpageRenderer(doc_name) as renderer:
        doc = renderer.render(mistletoe.Document(fin))
        with open(output_file, "w") as fout:
            fout.write(doc)
