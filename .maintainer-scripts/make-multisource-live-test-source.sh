#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

# $ bash make-multisource-live-test-source.sh /tmp/dwarfs-fixture
# $ mkdwarfs -i /tmp/dwarfs-fixture -o /tmp/dwarfs-fixture.dwarfs

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 SOURCE_DIRECTORY" >&2
  exit 2
fi

root=$1
mkdir -p -- "$root/alpha/shared" "$root/beta/shared" "$root/outside/subdir"

printf '%s\n' 'first source wins' > "$root/alpha/overlap.txt"
printf '%s\n' 'second source loses' > "$root/beta/overlap.txt"
printf '%s\n' 'from alpha' > "$root/alpha/shared/one.txt"
printf '%s\n' 'from beta' > "$root/beta/shared/two.txt"
printf '%s\n' 'dereferenced file' > "$root/outside/linked.txt"
printf '%s\n' 'dereferenced directory' > "$root/outside/subdir/child.txt"

ln -s ../outside/linked.txt "$root/alpha/linked-file"
ln -s ../outside/subdir "$root/alpha/linked-directory"
ln -s alpha "$root/alias-alpha"
