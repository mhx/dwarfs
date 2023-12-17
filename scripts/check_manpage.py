#!/bin/env python3

import os
import re
import subprocess
import sys
import termcolor


def extract_options(text):
    options = set()

    for line in text.splitlines():
        match = re.search(r"--(\w[\w-]*)", line)
        if match:
            options.add(match.group(1))

    return options


if __name__ == "__main__":
    if len(sys.argv) < 3 or len(sys.argv) > 4:
        print("Usage: check_manpage.py <manpage> <program> [<help-arg>]")
        sys.exit(1)

    manpage = sys.argv[1]
    program = sys.argv[2]
    help_arg = sys.argv[3] if len(sys.argv) == 4 else "--help"

    manpage_basename = os.path.basename(manpage)

    result = subprocess.run([program, help_arg], capture_output=True)
    program_help = (result.stdout + result.stderr).decode("utf-8")
    manpage_text = open(manpage).read()

    program_options = extract_options(program_help)
    manpage_options = extract_options(manpage_text)

    # print(f'Program options: {program_options}')
    # print(f'Manpage options: {manpage_options}')

    # subset of options that are in the manpage but not in the program
    obsolete_options = manpage_options - program_options

    # subset of options that are in the program but not in the manpage
    missing_options = program_options - manpage_options

    exit_code = 0

    if obsolete_options:
        print(termcolor.colored(f"{manpage_basename}: obsolete options:", "red"))
        for option in obsolete_options:
            print(f"  --{option}")
        exit_code = 1

    if missing_options:
        print(termcolor.colored(f"{manpage_basename}: missing options:", "red"))
        for option in missing_options:
            print(f"  --{option}")
        exit_code = 1

    if exit_code == 0:
        print(
            termcolor.colored(f"{manpage_basename}: OK", "green")
            + f" (checked {len(program_options)} options)"
        )

    sys.exit(exit_code)
