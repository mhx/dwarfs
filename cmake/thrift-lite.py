#!/usr/bin/env python3

# vim:set ts=2 sw=2 sts=2 et: */
#
# \author     Marcus Holland-Moritz (github@mhxnet.de)
# \copyright  Copyright (c) Marcus Holland-Moritz
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

from __future__ import annotations

import argparse
import dataclasses
import sys
import textwrap
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple


class ParseError(Exception):
    pass


@dataclasses.dataclass(frozen=True)
class TypeRef:
    # kind: "base" | "id" | "list" | "set" | "map"
    kind: str
    base: Optional[str] = None  # for base
    type_id: Optional[str] = None  # for id
    elem: Optional["TypeRef"] = None  # for list/set
    key: Optional["TypeRef"] = None  # for map
    val: Optional["TypeRef"] = None  # for map


@dataclasses.dataclass(frozen=True)
class TypedefDef:
    name: str
    true_type: TypeRef
    annotations: Dict[str, str]


@dataclasses.dataclass(frozen=True)
class EnumMember:
    name: str
    value: int
    annotations: Dict[str, str]


@dataclasses.dataclass(frozen=True)
class EnumDef:
    name: str
    members: List[EnumMember]
    annotations: Dict[str, str]


@dataclasses.dataclass(frozen=True)
class FieldDef:
    name: str
    field_id: int
    required: str  # "" | "optional"
    type_ref: TypeRef
    annotations: Dict[str, str]


@dataclasses.dataclass(frozen=True)
class StructDef:
    name: str
    fields: List[FieldDef]
    annotations: Dict[str, str]


@dataclasses.dataclass(frozen=True)
class ParsedIDL:
    cpp_namespace: str  # "a::b::c" or ""
    cpp_includes: List[str]  # from cpp_include statements
    typedefs: List[TypedefDef]
    enums: List[EnumDef]
    structs: List[StructDef]
    typedefs_by_name: Dict[str, TypedefDef]
    enums_by_name: Dict[str, EnumDef]
    structs_by_name: Dict[str, StructDef]


# -----------------------------
# Lexer
# -----------------------------


@dataclasses.dataclass(frozen=True)
class Token:
    kind: str  # "ident" | "int" | "string" | "sym" | "eof"
    value: str
    line: int
    col: int


def _is_ident_start(ch: str) -> bool:
    return ch.isalpha() or ch == "_"


def _is_ident_part(ch: str) -> bool:
    return ch.isalnum() or ch == "_"


def _lex(s: str) -> List[Token]:
    tokens: List[Token] = []
    i = 0
    line = 1
    col = 1

    def err(msg: str) -> None:
        raise ParseError(f"{msg} at {line}:{col}")

    def advance(n: int = 1) -> None:
        nonlocal i, line, col
        for _ in range(n):
            if i >= len(s):
                return
            if s[i] == "\n":
                line += 1
                col = 1
            else:
                col += 1
            i += 1

    def peek(n: int = 0) -> str:
        if i + n >= len(s):
            return ""
        return s[i + n]

    while i < len(s):
        ch = peek()

        # whitespace
        if ch.isspace():
            advance()
            continue

        # line comments: // ... or # ...
        if ch == "/" and peek(1) == "/":
            while i < len(s) and peek() != "\n":
                advance()
            continue
        if ch == "#":
            while i < len(s) and peek() != "\n":
                advance()
            continue

        # block comment: /* ... */
        if ch == "/" and peek(1) == "*":
            advance(2)
            while i < len(s):
                if peek() == "*" and peek(1) == "/":
                    advance(2)
                    break
                advance()
            else:
                err("unterminated block comment")
            continue

        # string literal "..."
        if ch == '"':
            start_line, start_col = line, col
            advance()  # consume "
            out_chars: List[str] = []
            while i < len(s):
                c = peek()
                if c == '"':
                    advance()
                    break
                if c == "\\":
                    advance()
                    esc = peek()
                    if not esc:
                        err("unterminated string escape")
                    if esc == "n":
                        out_chars.append("\n")
                        advance()
                    elif esc == "r":
                        out_chars.append("\r")
                        advance()
                    elif esc == "t":
                        out_chars.append("\t")
                        advance()
                    elif esc == "\\":
                        out_chars.append("\\")
                        advance()
                    elif esc == '"':
                        out_chars.append('"')
                        advance()
                    elif esc == "x":
                        advance()
                        h1, h2 = peek(), peek(1)
                        if not (
                            h1
                            and h2
                            and all(c in "0123456789abcdefABCDEF" for c in (h1, h2))
                        ):
                            err("invalid \\x escape")
                        out_chars.append(chr(int(h1 + h2, 16)))
                        advance(2)
                    else:
                        # keep unknown escapes as literal char (thrift is permissive in practice)
                        out_chars.append(esc)
                        advance()
                else:
                    out_chars.append(c)
                    advance()
            else:
                raise ParseError(f"unterminated string at {start_line}:{start_col}")

            tokens.append(Token("string", "".join(out_chars), start_line, start_col))
            continue

        # integer literal (decimal or hex), optional leading -
        if ch == "-" or ch.isdigit():
            start_line, start_col = line, col
            j = i
            if ch == "-":
                j += 1
                if j >= len(s) or not s[j].isdigit():
                    # '-' as symbol handled below
                    j = i
                else:
                    # consume sign
                    advance()
                    ch = peek()

            if peek() == "0" and peek(1) in ("x", "X"):
                advance(2)
                digits: List[str] = []
                while peek() and peek() in "0123456789abcdefABCDEF":
                    digits.append(peek())
                    advance()
                if not digits:
                    err("invalid hex literal")
                val = "0x" + "".join(digits)
                if s[i - len(val) : i].startswith("-"):
                    val = "-" + val
                tokens.append(Token("int", val, start_line, start_col))
                continue

            if peek().isdigit():
                digits: List[str] = []
                while peek() and peek().isdigit():
                    digits.append(peek())
                    advance()
                val = "".join(digits)
                if s[i - len(val) - 1 : i - len(val)] == "-":
                    val = "-" + val
                tokens.append(Token("int", val, start_line, start_col))
                continue

            # fallthrough to symbol handling (for lone '-')

            # reset if we consumed '-' incorrectly
            # (handled by logic above)
        # identifier
        if _is_ident_start(ch):
            start_line, start_col = line, col
            ident: List[str] = []
            while peek() and _is_ident_part(peek()):
                ident.append(peek())
                advance()
            tokens.append(Token("ident", "".join(ident), start_line, start_col))
            continue

        # single-char symbols (plus '.' for namespaces)
        if ch in "{}()<>:;=,.":
            tokens.append(Token("sym", ch, line, col))
            advance()
            continue

        raise ParseError(f"unexpected character {ch!r} at {line}:{col}")

    tokens.append(Token("eof", "", line, col))
    return tokens


# -----------------------------
# Parser
# -----------------------------


class _Parser:
    def __init__(self, toks: Sequence[Token]) -> None:
        self.toks = toks
        self.pos = 0

    def _cur(self) -> Token:
        return self.toks[self.pos]

    def _at(self, kind: str, value: Optional[str] = None) -> bool:
        t = self._cur()
        if t.kind != kind:
            return False
        if value is not None and t.value != value:
            return False
        return True

    def _eat(self, kind: str, value: Optional[str] = None) -> Token:
        t = self._cur()
        if not self._at(kind, value):
            want = f"{kind}({value})" if value is not None else kind
            raise ParseError(
                f"expected {want}, got {t.kind}({t.value}) at {t.line}:{t.col}"
            )
        self.pos += 1
        return t

    def _maybe(self, kind: str, value: Optional[str] = None) -> Optional[Token]:
        if self._at(kind, value):
            return self._eat(kind, value)
        return None

    def _ident(self) -> str:
        return self._eat("ident").value

    def _dotted_ident(self) -> str:
        parts = [self._ident()]
        while self._maybe("sym", "."):
            parts.append(self._ident())
        return ".".join(parts)

    def _int(self) -> int:
        tok = self._eat("int")
        v = tok.value
        neg = v.startswith("-")
        if neg:
            v = v[1:]
        if v.startswith(("0x", "0X")):
            n = int(v, 16)
        else:
            n = int(v, 10)
        return -n if neg else n

    def _string(self) -> str:
        return self._eat("string").value

    def parse_annotations(self) -> Dict[str, str]:
        # (k="v", cpp.include="<foo>", ...)
        ann: Dict[str, str] = {}

        if not self._maybe("sym", "("):
            return ann

        first = True
        while True:
            if self._maybe("sym", ")"):
                break
            if not first:
                self._maybe("sym", ",")
            first = False

            key = self._dotted_ident()
            self._eat("sym", "=")

            # allow string/int/ident values; store as string
            if self._at("string"):
                val = self._string()
            elif self._at("int"):
                val = str(self._int())
            elif self._at("ident"):
                val = self._ident()
            else:
                t = self._cur()
                raise ParseError(
                    f"invalid annotation value {t.kind}({t.value}) at {t.line}:{t.col}"
                )

            ann[key] = val

            # optional trailing comma before ')'
            self._maybe("sym", ",")

        return ann

    def parse_type(self) -> TypeRef:
        # base | id | list<...> | set<...> | map<...,...>
        if self._at("ident", "list") or self._at("ident", "set"):
            kind = self._ident()
            self._eat("sym", "<")
            elem = self.parse_type()
            self._eat("sym", ">")
            return TypeRef(kind=kind, elem=elem)

        if self._at("ident", "map"):
            self._eat("ident", "map")
            self._eat("sym", "<")
            k = self.parse_type()
            self._eat("sym", ",")
            v = self.parse_type()
            self._eat("sym", ">")
            return TypeRef(kind="map", key=k, val=v)

        if not self._at("ident"):
            t = self._cur()
            raise ParseError(
                f"expected type, got {t.kind}({t.value}) at {t.line}:{t.col}"
            )

        name = self._ident()
        if name in ("bool", "byte", "i16", "i32", "i64", "string", "binary"):
            return TypeRef(kind="base", base=name)

        if name in ("double", "uuid"):
            raise ParseError(f"type {name!r} not supported")

        return TypeRef(kind="id", type_id=name)

    def parse_namespace_cpp(self) -> str:
        # namespace cpp a.b.c
        self._eat("ident", "namespace")
        lang = self._ident()
        parts: List[str] = []
        # accept either ident or ident '.' ident ...
        parts.append(self._ident())
        while self._maybe("sym", "."):
            parts.append(self._ident())

        if lang != "cpp":
            # ignore other namespaces
            return ""

        return "::".join(parts)

    def parse_cpp_include(self) -> str:
        # cpp_include "..."
        self._eat("ident", "cpp_include")
        return self._string()

    def parse_typedef(self) -> TypedefDef:
        self._eat("ident", "typedef")
        t = self.parse_type()
        name = self._ident()
        ann = self.parse_annotations()
        self._maybe("sym", ";")
        return TypedefDef(name=name, true_type=t, annotations=ann)

    def parse_enum(self) -> EnumDef:
        self._eat("ident", "enum")
        name = self._ident()
        ann = self.parse_annotations()
        self._eat("sym", "{")

        members: List[EnumMember] = []
        next_val = 0
        while not self._at("sym", "}"):
            mn = self._ident()
            mv = next_val
            if self._maybe("sym", "="):
                mv = self._int()
            m_ann = self.parse_annotations()
            # member separators are optional, accept ',' or ';'
            self._maybe("sym", ",")
            self._maybe("sym", ";")
            members.append(EnumMember(name=mn, value=mv, annotations=m_ann))
            next_val = mv + 1

        self._eat("sym", "}")
        self._maybe("sym", ";")
        return EnumDef(name=name, members=members, annotations=ann)

    def parse_struct(self) -> StructDef:
        self._eat("ident", "struct")
        name = self._ident()
        ann = self.parse_annotations()
        self._eat("sym", "{")

        fields: List[FieldDef] = []
        while not self._at("sym", "}"):
            # field-id must be present
            if not self._at("int"):
                t = self._cur()
                raise ParseError(
                    f"field-id required, got {t.kind}({t.value}) at {t.line}:{t.col}"
                )
            field_id = self._int()
            self._eat("sym", ":")

            # optional qualifier
            required = ""
            if self._at("ident", "optional"):
                self._eat("ident", "optional")
                required = "optional"
            elif self._at("ident", "required") or self._at("ident", "req_out"):
                q = self._ident()
                raise ParseError(f"{q} fields are not supported")

            t = self.parse_type()
            fn = self._ident()

            f_ann = self.parse_annotations()

            # default values are not supported; detect "= ..." for early error
            if self._maybe("sym", "="):
                raise ParseError("default values are not supported")

            self._maybe("sym", ",")
            self._maybe("sym", ";")

            fields.append(
                FieldDef(
                    name=fn,
                    field_id=field_id,
                    required=required,
                    type_ref=t,
                    annotations=f_ann,
                )
            )

        self._eat("sym", "}")
        self._maybe("sym", ";")
        return StructDef(name=name, fields=fields, annotations=ann)

    def parse(self) -> ParsedIDL:
        cpp_ns = ""
        cpp_includes: List[str] = []
        typedefs: List[TypedefDef] = []
        enums: List[EnumDef] = []
        structs: List[StructDef] = []

        while not self._at("eof"):
            if self._at("ident", "namespace"):
                ns = self.parse_namespace_cpp()
                if ns:
                    cpp_ns = ns
                continue

            if self._at("ident", "cpp_include"):
                cpp_includes.append(self.parse_cpp_include())
                continue

            if self._at("ident", "include"):
                raise ParseError("IDL includes are not supported")

            if self._at("ident", "typedef"):
                typedefs.append(self.parse_typedef())
                continue

            if self._at("ident", "enum"):
                enums.append(self.parse_enum())
                continue

            if self._at("ident", "struct"):
                structs.append(self.parse_struct())
                continue

            if self._at("ident", "union"):
                raise ParseError("unions are not supported")
            if self._at("ident", "exception"):
                raise ParseError("exceptions are not supported")
            if self._at("ident", "service"):
                raise ParseError("services are not supported")
            if self._at("ident", "const"):
                raise ParseError("constants are not supported")

            t = self._cur()
            raise ParseError(
                f"unexpected token {t.kind}({t.value}) at {t.line}:{t.col}"
            )

        return ParsedIDL(
            cpp_namespace=cpp_ns,
            cpp_includes=cpp_includes,
            typedefs=typedefs,
            enums=enums,
            structs=structs,
            typedefs_by_name={td.name: td for td in typedefs},
            enums_by_name={e.name: e for e in enums},
            structs_by_name={s.name: s for s in structs},
        )


def parse_document_from_idl(idl_text: str) -> ParsedIDL:
    toks = _lex(idl_text)
    p = _Parser(toks)
    return p.parse()


def die(msg: str) -> None:
    raise SystemExit(f"thrift-lite.py: error: {msg}")


class Emitter:
    def __init__(self) -> None:
        self._parts: List[str] = []

    def emit(self, s: str, indent: int = 0, strip: bool = True) -> None:
        s = textwrap.dedent(s)
        if strip:
            s = s.lstrip("\n")
        if indent > 0:
            s = textwrap.indent(s, " " * indent)
        self._parts.append(s)

    def text(self) -> str:
        return "".join(self._parts)


def classify_id(
    type_id: str,
    idl: ParsedIDL,
) -> str:
    if type_id in idl.typedefs_by_name:
        return "typedef"
    if type_id in idl.enums_by_name:
        return "enum"
    if type_id in idl.structs_by_name:
        return "struct"
    die(f"unknown id type {type_id!r} (dependency between IDL files is not supported)")


def resolve_wire_type(
    t: TypeRef,
    idl: ParsedIDL,
) -> TypeRef:
    # Resolve typedefs, and map enums to i32 on the wire.
    if t.kind == "base":
        return t
    if t.kind in ("list", "set"):
        assert t.elem is not None
        return TypeRef(
            kind=t.kind,
            elem=resolve_wire_type(t.elem, idl),
        )
    if t.kind == "map":
        assert t.key is not None and t.val is not None
        return TypeRef(
            kind="map",
            key=resolve_wire_type(t.key, idl),
            val=resolve_wire_type(t.val, idl),
        )
    if t.kind == "id":
        assert t.type_id is not None
        k = classify_id(t.type_id, idl)
        if k == "typedef":
            return resolve_wire_type(
                idl.typedefs_by_name[t.type_id].true_type,
                idl,
            )
        if k == "enum":
            return TypeRef(kind="base", base="i32")
        if k == "struct":
            return TypeRef(kind="id", type_id=t.type_id)
    die(f"cannot resolve wire type for {t!r}")


def ttype_expr(wire: TypeRef, no_string: bool = False) -> str:
    if wire.kind == "base":
        b = wire.base
        if b == "bool":
            return "::dtl::ttype::bool_t"
        if b == "byte":
            return "::dtl::ttype::byte_t"
        if b == "i16":
            return "::dtl::ttype::i16_t"
        if b == "i32":
            return "::dtl::ttype::i32_t"
        if b == "i64":
            return "::dtl::ttype::i64_t"
        if not no_string and b == "string":
            return "::dtl::ttype::string_t"
        if b == "binary" or b == "string":
            return "::dtl::ttype::binary_t"
        die(f"unsupported base wire type {b!r}")
    if wire.kind == "id":
        return "::dtl::ttype::struct_t"
    if wire.kind == "list":
        return "::dtl::ttype::list_t"
    if wire.kind == "set":
        return "::dtl::ttype::set_t"
    if wire.kind == "map":
        return "::dtl::ttype::map_t"
    die(f"unsupported wire kind {wire.kind!r}")


def cpp_type_for(
    t: TypeRef,
    idl: ParsedIDL,
    annotations: Optional[Dict[str, str]] = None,
    fully_qualified: bool = False,
) -> str:
    fq = "::" if fully_qualified else ""
    if t.kind == "base":
        b = t.base
        if b == "bool":
            return "bool"
        if b == "byte":
            return fq + "std::int8_t"
        if b == "i16":
            return fq + "std::int16_t"
        if b == "i32":
            return fq + "std::int32_t"
        if b == "i64":
            return fq + "std::int64_t"
        if b == "string":
            return fq + "std::string"
        if b == "binary":
            return f"{fq}std::vector<{fq}std::byte>"
        die(f"unsupported base type {b!r}")

    if t.kind == "id":
        assert t.type_id is not None
        k = classify_id(t.type_id, idl)
        # Preserve typedef/enums/struct names in the C++ API.
        fqcpp = (
            f"::{idl.cpp_namespace}::" if fully_qualified and idl.cpp_namespace else ""
        )
        return fqcpp + t.type_id

    if t.kind == "list":
        assert t.elem is not None
        return f"{fq}std::vector<{cpp_type_for(t.elem, idl, fully_qualified=fully_qualified)}>"
    if t.kind == "set":
        assert t.elem is not None
        set_template = fq + "std::set"
        if annotations is not None:
            set_template = annotations.get("cpp.template", set_template)
        return f"{set_template}<{cpp_type_for(t.elem, idl, fully_qualified=fully_qualified)}>"
    if t.kind == "map":
        assert t.key is not None and t.val is not None
        map_template = fq + "std::map"
        if annotations is not None:
            map_template = annotations.get("cpp.template", map_template)
        return (
            f"{map_template}<"
            f"{cpp_type_for(t.key, idl, fully_qualified=fully_qualified)}, "
            f"{cpp_type_for(t.val, idl, fully_qualified=fully_qualified)}>"
        )

    die(f"unsupported type kind {t.kind!r}")


def typedef_cpp_underlying(
    td: TypedefDef,
    idl: ParsedIDL,
) -> str:
    # The typedef name itself becomes the alias in C++.
    # Its underlying C++ type is either cpp.type annotation or the mapped type of the true type.
    cpp = td.annotations.get("cpp.type")
    if cpp:
        return cpp
    return cpp_type_for(
        td.true_type,
        idl,
        annotations=td.annotations,
    )


def gen_type_class(
    t: TypeRef,
    idl: ParsedIDL,
) -> str:
    ns = "::dtli::type_class"

    if t.kind == "base":
        b = t.base
        if b in ("bool", "byte", "i16", "i32", "i64"):
            return f"{ns}::integral"
        if b in ("binary", "string"):
            return f"{ns}::{b}"
        die(f"unsupported base read type {b!r}")

    if t.kind == "id":
        assert t.type_id is not None
        kind = classify_id(t.type_id, idl)

        if kind == "struct":
            return f"{ns}::structure"

        if kind == "enum":
            return f"{ns}::enumeration"

        td = idl.typedefs_by_name[t.type_id]
        true_t = td.true_type
        wire_true = resolve_wire_type(true_t, idl)

        if wire_true.kind == "base" and wire_true.base in ("i16", "i32", "i64", "byte"):
            true_t = wire_true

        return gen_type_class(true_t, idl)

    if t.kind in ("list", "set"):
        assert t.elem is not None
        elem_type_class = gen_type_class(t.elem, idl)
        return f"{ns}::{t.kind}<{elem_type_class}>"

    if t.kind == "map":
        assert t.key is not None and t.val is not None
        key_type_class = gen_type_class(t.key, idl)
        val_type_class = gen_type_class(t.val, idl)
        return f"{ns}::map<{key_type_class}, {val_type_class}>"

    die(f"unsupported read kind {t.kind!r}")


def get_types(out_stem: str, idl: ParsedIDL) -> Tuple[str, str]:
    def type_class_for(ref: TypeRef) -> str:
        return gen_type_class(
            ref,
            idl,
        )

    # Header
    h = Emitter()
    h.emit(
        """
        // @generated by thrift-lite.py; do not edit.
        #pragma once

        #include <bitset>
        #include <cstddef>
        #include <cstdint>
        #include <map>
        #include <set>
        #include <span>
        #include <string>
        #include <string_view>
        #include <vector>

        #include <dwarfs/thrift_lite/enum_traits.h>
        #include <dwarfs/thrift_lite/field_ref.h>
        #include <dwarfs/thrift_lite/protocol_fwd.h>
        #include <dwarfs/thrift_lite/types.h>

        """
    )

    extra_includes = idl.cpp_includes
    if extra_includes:
        for inc in extra_includes:
            header = inc if inc.startswith("<") and inc.endswith(">") else f'"{inc}"'
            h.emit(f"#include {header}\n")
        h.emit("\n", strip=False)

    if idl.cpp_namespace:
        h.emit(f"namespace {idl.cpp_namespace} {{\n\n")

    # Enums
    for e in idl.enums:
        h.emit(f"enum class {e.name} {{\n")
        for m in e.members:
            h.emit(f"{m.name} = {m.value},\n", 2)
        h.emit("};\n\n")

    # Typedefs as aliases
    for td in idl.typedefs:
        underlying = typedef_cpp_underlying(td, idl)
        h.emit(f"using {td.name} = {underlying};\n")
    if idl.typedefs:
        h.emit("\n", strip=False)

    # Structs
    for s in idl.structs:
        optional_fields = [f for f in s.fields if f.required == "optional"]
        has_optional = bool(optional_fields)

        h.emit(
            f"""
            class {s.name} final {{
             public:
              {s.name}();
              {s.name}({s.name} const&);
              {s.name}({s.name}&&) noexcept;
              auto operator=({s.name} const&) -> {s.name}&;
              auto operator=({s.name}&&) noexcept -> {s.name}&;

              bool operator==({s.name} const&) const = default;

              void read(::dwarfs::thrift_lite::protocol_reader& r);
              void write(::dwarfs::thrift_lite::protocol_writer& w) const;

              [[nodiscard]] auto has_any_fields_for_write(::dwarfs::thrift_lite::writer_options const& opts) const noexcept -> bool;

            """
        )

        # Field refs only
        opt_index = {f.name: i for i, f in enumerate(optional_fields)}
        for f in s.fields:
            cpp_t = cpp_type_for(
                f.type_ref,
                idl,
                annotations=f.annotations,
            )
            if f.required == "optional":
                idx_c = f"{f.name}_isset_index"
                h.emit(
                    f"""
                    auto {f.name}() & noexcept {{
                      return ::dwarfs::thrift_lite::optional_field_ref<{cpp_t}&, isset_ref>{{{f.name}_, isset_[{idx_c}]}};
                    }}
                    auto {f.name}_ref() & noexcept {{
                      return ::dwarfs::thrift_lite::optional_field_ref<{cpp_t}&, isset_ref>{{{f.name}_, isset_[{idx_c}]}};
                    }}
                    auto {f.name}() const& noexcept {{
                      return ::dwarfs::thrift_lite::optional_field_ref<{cpp_t} const&, bool>{{{f.name}_, isset_.test({idx_c})}};
                    }}
                    auto {f.name}_ref() const& noexcept {{
                      return ::dwarfs::thrift_lite::optional_field_ref<{cpp_t} const&, bool>{{{f.name}_, isset_.test({idx_c})}};
                    }}

                    """,
                    indent=2,
                )
            else:
                h.emit(
                    f"""
                    auto {f.name}() & noexcept {{
                      return ::dwarfs::thrift_lite::field_ref<{cpp_t}&>{{{f.name}_}};
                    }}
                    auto {f.name}_ref() & noexcept {{
                      return ::dwarfs::thrift_lite::field_ref<{cpp_t}&>{{{f.name}_}};
                    }}
                    auto {f.name}() const& noexcept {{
                      return ::dwarfs::thrift_lite::field_ref<{cpp_t} const&>{{{f.name}_}};
                    }}
                    auto {f.name}_ref() const& noexcept {{
                      return ::dwarfs::thrift_lite::field_ref<{cpp_t} const&>{{{f.name}_}};
                    }}

                    """,
                    indent=2,
                )

        h.emit("private:\n", indent=1)
        if has_optional:
            for i, f in enumerate(optional_fields):
                h.emit(
                    f"static constexpr auto {f.name}_isset_index = std::size_t{{{i}}};\n",
                    indent=2,
                )
            h.emit(
                f"""
                using isset_type = std::bitset<{len(optional_fields)}>;
                using isset_ref = isset_type::reference;
                isset_type isset_{{}};
                """,
                indent=2,
            )

        for f in s.fields:
            cpp_t = cpp_type_for(
                f.type_ref,
                idl,
                annotations=f.annotations,
            )
            h.emit(f"{cpp_t} {f.name}_{{}};\n", indent=2)
        h.emit("};\n\n")

    if idl.cpp_namespace:
        h.emit(f"}} // namespace {idl.cpp_namespace}\n\n")

    # enum_traits specializations (decl only)
    h.emit("namespace dwarfs::thrift_lite {\n\n")
    for e in idl.enums:
        fq = f"::{idl.cpp_namespace}::{e.name}" if idl.cpp_namespace else f"::{e.name}"
        h.emit(
            f"""
            template <>
            struct enum_traits<{fq}> {{
              using type = {fq};

              static auto values() noexcept -> std::span<type const>;
              static auto names() noexcept -> std::span<std::string_view const>;
            }};

            """
        )
    h.emit("} // namespace dwarfs::thrift_lite\n")

    # Source
    c = Emitter()
    c.emit(
        f"""
        // @generated by thrift-lite.py; do not edit.

        #include <array>
        #include <cstddef>
        #include <cstdint>
        #include <map>
        #include <set>
        #include <span>
        #include <string>
        #include <string_view>
        #include <type_traits>
        #include <utility>
        #include <vector>

        #include <dwarfs/thrift_lite/protocol_reader.h>
        #include <dwarfs/thrift_lite/protocol_writer.h>
        #include <dwarfs/thrift_lite/writer_options.h>

        #include <dwarfs/thrift_lite/internal/protocol_helpers.h>
        #include <dwarfs/thrift_lite/internal/protocol_methods.h>

        #include "{out_stem}_types.h"

        using namespace std::string_view_literals;
        namespace dtl = ::dwarfs::thrift_lite;
        namespace dtli = ::dwarfs::thrift_lite::internal;

        """
    )

    if idl.cpp_namespace:
        c.emit(f"namespace {idl.cpp_namespace} {{\n\n")

    for s in idl.structs:
        optional_fields = [f for f in s.fields if f.required == "optional"]
        has_optional = bool(optional_fields)

        # --- emit ctors/dtor/assignment operators ---

        c.emit(
            f"""
            {s.name}::{s.name}() = default;
            {s.name}::{s.name}({s.name} const&) = default;
            {s.name}::{s.name}({s.name}&&) noexcept = default;
            auto {s.name}::operator=({s.name} const&) -> {s.name}& = default;
            auto {s.name}::operator=({s.name}&&) noexcept -> {s.name}& = default;

            """
        )

        # --- emit has_any_fields_for_write() before write() ---

        c.emit(
            f"[[nodiscard]] auto {s.name}::has_any_fields_for_write(::dtl::writer_options const& opts) const noexcept -> bool {{\n"
        )

        any_fields_checks = []

        # Optionals: if any isset bit is set, we will write something.
        optional_fields = [f for f in s.fields if f.required == "optional"]
        if optional_fields:
            any_fields_checks.append("isset_.any()")

        for f in s.fields:
            if f.required == "optional":
                continue

            # If the field is a struct type (possibly via typedef), recurse.
            def resolves_to_struct(tr: TypeRef) -> Optional[str]:
                if tr.kind == "id" and tr.type_id is not None:
                    k = classify_id(tr.type_id, idl)
                    if k == "struct":
                        return tr.type_id
                    if k == "typedef":
                        return resolves_to_struct(
                            idl.typedefs_by_name[tr.type_id].true_type
                        )
                if tr.kind in ("list", "set", "map"):
                    return None
                return None

            struct_name = resolves_to_struct(f.type_ref)
            if struct_name is not None:
                any_fields_checks.append(f"{f.name}_.has_any_fields_for_write(opts)")
            else:
                any_fields_checks.append(f"::dtli::should_write_regular(opts, {f.name}_)")

        checks_str = " ||\n                     ".join(any_fields_checks)

        c.emit(
            f"""
              return {checks_str};
            }}

            """
        )

        # write(): sort by field id (ascending)
        fields_sorted = sorted(s.fields, key=lambda f: f.field_id)

        c.emit(
            f"""
            void {s.name}::write(::dtl::protocol_writer& w) const {{
              auto const& opts = w.options();

              w.write_struct_begin("{s.name}");

            """
        )

        for f in fields_sorted:
            wire = resolve_wire_type(f.type_ref, idl)
            texpr = ttype_expr(wire)

            should_write = (
                f"isset_.test({f.name}_isset_index)"
                if f.required == "optional"
                else f"::dtli::should_write_regular(opts, {f.name}_)"
            )
            type_class = type_class_for(f.type_ref)

            c.emit(
                f"""
                if ({should_write}) {{
                  w.write_field_begin("{f.name}", {texpr}, {f.field_id});
                  ::dtli::protocol_methods<{type_class}, std::remove_cvref_t<decltype({f.name}_)>>::write(w, {f.name}_);
                  w.write_field_end();
                }}

                """,
                indent=2,
            )

        c.emit(
            """
              w.write_field_stop();
              w.write_struct_end();
            }

            """
        )

        # read()
        c.emit(f"void {s.name}::read(::dtl::protocol_reader& r) {{\n")
        if has_optional:
            c.emit("isset_.reset();\n", indent=2)
        for f in s.fields:
            c.emit(f"{f.name}_ = {{}};\n", indent=2)
        c.emit(
            """
            r.read_struct_begin();

            for (;;) {
              auto field_type = ::dtl::ttype{};
              auto field_id = std::int16_t{};

              r.read_field_begin(field_type, field_id);

              if (field_type == ::dtl::ttype::stop_t) {
                break;
              }

              bool skip = true;

              switch (field_id) {
            """,
            indent=2,
            strip=False,
        )

        fields_by_id = {f.field_id: f for f in s.fields}
        for fid in sorted(fields_by_id.keys()):
            f = fields_by_id[fid]
            wire = resolve_wire_type(f.type_ref, idl)
            expected = ttype_expr(wire, no_string=True)

            type_class = type_class_for(f.type_ref)

            c.emit(
                f"""
                case {fid}:
                  if (field_type == {expected}) {{
                    ::dtli::protocol_methods<{type_class}, std::remove_cvref_t<decltype({f.name}_)>>::read(r, {f.name}_);
                """,
                indent=6,
            )

            if f.required == "optional":
                c.emit(f"isset_.set({f.name}_isset_index);\n", indent=10)

            c.emit(
                f"""
                    skip = false;
                  }}
                  break;

                """,
                indent=8,
            )

        c.emit(
            """
                  default:
                    break;
                }

                if (skip) {
                  r.skip(field_type);
                }

                r.read_field_end();
              }

              r.read_struct_end();
            }

            """
        )

    if idl.cpp_namespace:
        c.emit(f"}} // namespace {idl.cpp_namespace}\n\n")

    # enum_traits definitions
    c.emit("namespace dwarfs::thrift_lite {\n\n")
    for e in idl.enums:
        fq = f"::{idl.cpp_namespace}::{e.name}" if idl.cpp_namespace else f"::{e.name}"
        n = len(e.members)

        c.emit(
            f"auto enum_traits<{fq}>::values() noexcept -> std::span<type const> {{\n"
        )
        c.emit(
            f"""
            using enum {fq};
            static constexpr auto vals = std::array<type, {n}>{{
            """,
            indent=2,
        )
        for m in e.members:
            c.emit(f"{m.name},\n", indent=4)
        c.emit(
            """
              };
              return vals;
            }

            """
        )

        c.emit(
            f"auto enum_traits<{fq}>::names() noexcept -> std::span<std::string_view const> {{\n"
        )
        c.emit(f"static constexpr auto names = std::array{{\n", indent=2)
        for m in e.members:
            c.emit(f'"{m.name}"sv,\n', indent=4)
        c.emit(
            """
              };
              return names;
            }

            """
        )

    c.emit("} // namespace dwarfs::thrift_lite\n")

    return h.text(), c.text()


def get_layouts(out_stem: str, idl: ParsedIDL) -> Tuple[str, str]:
    def field_cpp_type(f: FieldDef) -> str:
        return cpp_type_for(
            f.type_ref,
            idl,
            annotations=f.annotations,
            fully_qualified=True,
        )

    h = Emitter()
    c = Emitter()

    h.emit(
        f"""
        // @generated by thrift-lite.py; do not edit.

        #pragma once

        #include <thrift/lib/cpp2/frozen/Frozen.h>
        #include "{out_stem}_types.h"

        namespace apache::thrift::frozen {{

        """
    )

    c.emit(
        f"""
        // @generated by thrift-lite.py; do not edit.

        #include "{out_stem}_layouts.h"

        namespace apache::thrift::frozen {{

        """
    )

    for s in idl.structs:
        fq_struct = (
            f"::{idl.cpp_namespace}::{s.name}" if idl.cpp_namespace else f"::{s.name}"
        )

        # ---- header ----

        h.emit(f"FROZEN_TYPE({fq_struct},\n")

        for f in s.fields:
            opt = "_OPT" if f.required == "optional" else ""
            h.emit(
                f"FROZEN_FIELD{opt}({f.name}, {f.field_id}, {field_cpp_type(f)})\n",
                indent=2,
            )

        first = True

        for name, field, needs_opt, gen in (
            ("VIEW", "VIEW", True, lambda f: field_cpp_type(f)),
            ("SAVE_INLINE", "SAVE", False, None),
            ("LOAD_INLINE", "LOAD", False, lambda f: str(f.field_id)),
        ):
            if first:
                first = False
            else:
                h.emit("\n", strip=False)
            h.emit(f"FROZEN_{name}(", indent=2)
            for f in s.fields:
                args = [f.name]
                if gen is not None:
                    args.append(gen(f))
                opt = "_OPT" if needs_opt and f.required == "optional" else ""
                h.emit(
                    f"\nFROZEN_{field}_FIELD{opt}({', '.join(args)})",
                    indent=4,
                    strip=False,
                )
            h.emit(")")

        h.emit(");\n\n")

        # ---- source ----

        def emit_cpp(
            name: str,
            needs_opt: bool = False,
            with_id: bool = False,
            with_semicolon: bool = False,
        ) -> None:
            semi = ";" if with_semicolon else ""
            c.emit(f"FROZEN_{name}({fq_struct},")
            for f in s.fields:
                args = [f.name]
                if with_id:
                    args.append(str(f.field_id))
                opt = "_OPT" if needs_opt and f.required == "optional" else ""
                c.emit(
                    f"\nFROZEN_{name}_FIELD{opt}({', '.join(args)}){semi}",
                    indent=2,
                    strip=False,
                )
            c.emit("\n)\n" if with_semicolon else ")\n", strip=False)

        emit_cpp("CTOR", needs_opt=True, with_id=True)
        emit_cpp("MAXIMIZE", with_semicolon=True)
        emit_cpp("LAYOUT", needs_opt=True)
        emit_cpp("FREEZE", needs_opt=True)
        emit_cpp("THAW", needs_opt=True)
        emit_cpp("DEBUG")
        emit_cpp("CLEAR")

        c.emit("\n", strip=False)

    h.emit(
        """
        } // apache::thrift::frozen
        """
    )

    c.emit(
        """
        } // apache::thrift::frozen
        """
    )

    return h.text(), c.text()


def main(argv: Sequence[str]) -> int:
    ap = argparse.ArgumentParser(prog="thrift-lite.py")
    ap.add_argument(
        "--input",
        required=True,
        help="Input Thrift IDL",
    )
    ap.add_argument(
        "--output-dir", required=True, help="Output directory for generated code"
    )
    ap.add_argument(
        "--frozen",
        action="store_true",
        help="Also emit frozen layout code",
    )
    args = ap.parse_args(list(argv))

    in_path = Path(args.input)
    output_dir = Path(args.output_dir)

    with open(args.input, "r", encoding="utf-8") as f:
        idl = parse_document_from_idl(f.read())

    output_dir.mkdir(parents=True, exist_ok=True)

    out_stem = in_path.stem

    outputs = [("types", get_types)]

    if args.frozen:
        outputs.append(("layouts", get_layouts))

    for suffix, generator in outputs:
        header_name = f"{out_stem}_{suffix}.h"
        source_name = f"{out_stem}_{suffix}.cpp"

        header_text, source_text = generator(out_stem=out_stem, idl=idl)

        (output_dir / header_name).write_text(header_text, encoding="utf-8")
        (output_dir / source_name).write_text(source_text, encoding="utf-8")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
