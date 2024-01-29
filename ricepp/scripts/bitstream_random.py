#!/bin/env python3
# -*- coding: utf-8 -*-

import random
import sys

oper = ['single'] + ['sequence']*2 + ['multi']*5

for i in range(0, 1000):
    op = random.choice(oper)
    if op == 'single':
      print(f"  {{ oper::{op}, 0, {random.randint(0, 1)} }},")
    elif op == 'sequence':
      print(f"  {{ oper::{op}, {random.randint(0, 100)}, 0 }},")
    else:
      nbits = random.randint(1, 64)
      print(f"  {{ oper::{op}, {nbits}, UINT64_C(0x{random.randint(0, 2**nbits - 1):x}) }},")
