#/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

from pathlib import Path
import re
from collections import Counter

p = Path("libcxx/src/locale.cpp")
s = p.read_text()

remove = {
    # Unused locale categories.
    "moneypunct",
    "money_get",
    "money_put",
    "messages",

    # Unused wide-character time facets.
    "collate<wchar_t",
    "num_get<wchar_t",
    "num_put<wchar_t",
    "numpunct<wchar_t",
    "time_get<wchar_t",
    "time_put<wchar_t",
}

pattern = re.compile(
    r"^\s*install(?:_from)?\s*[(<]"
    r".*?(?:\n|$)",
    re.MULTILINE,
)

removed = Counter()

def replace(m):
    line = m[0]

    normalized = line.replace("_byname", "")

    for facet in sorted(remove):
        if facet in normalized:
            removed[facet] += 1
            return ""

    return line

s = pattern.sub(replace, s)

if not removed:
    raise RuntimeError("No locale facet registrations matched")

for facet, count in sorted(removed.items()):
    print(f"Removed {count:2d} registrations of {facet}")

p.write_text(s)
