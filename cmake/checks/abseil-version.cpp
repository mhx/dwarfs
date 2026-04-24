// SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
// SPDX-License-Identifier: MIT

#define DWARFS_HAVE_ABSEIL

#include <dwarfs/internal/btree.h>
#include <dwarfs/internal/phmap.h>

// If this template instantiation fails, it means the available Abseil version
// doesn't have commit 4ab53949759d which we rely on.
// Abseil LTS 20260107 and rolling builds newer than Dec 15, 2025 have it.
static_assert(sizeof(dwarfs::internal::flat_hash_map<int, int>) > 0);

int main() { return 0; }
