#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

import xml.etree.ElementTree as ET

tests = []
for tc in ET.parse("ctest.xml").iterfind(".//testcase"):
    name = tc.get("name", "")
    classname = tc.get("classname", "")
    time_s = float(tc.get("time", "0") or 0)
    full_name = f"{classname}.{name}" if classname else name
    tests.append((time_s, full_name))

for time_s, name in sorted(tests, reverse=True)[:50]:
    print(f"{time_s:9.3f}s  {name}")
