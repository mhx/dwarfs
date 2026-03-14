#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2025 Alexis Girault. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# List Docker buildx cache mounts with human-readable names.
# Extends the output of `docker buildx du` by adding a NAME column that shows
# the cache mount ID instead of just the UID.

set -euo pipefail

# Parse command-line arguments.
SHOW_FS_SIZE=false
if [[ "${1:-}" == "--fs-size" ]] || [[ "${1:-}" == "-fs" ]]; then
    SHOW_FS_SIZE=true
elif [[ "${1:-}" ]]; then
    echo "Usage: $0 [--fs-size|-fs]"
    echo "  --fs-size, -fs    Show actual filesystem size (requires read access to Docker storage)"
    exit 0
fi

# Get Docker overlay path for filesystem size calculation.
OVERLAY_PATH=""
if $SHOW_FS_SIZE; then
    OVERLAY_PATH=$(docker info --format '{{.DockerRootDir}}/{{.Driver}}')

    # Check if we have read access to the overlay directory.
    if [[ ! -r "$OVERLAY_PATH" ]]; then
        echo "Error: Cannot read $OVERLAY_PATH" >&2
        echo "Please run as root (sudo $0 --fs-size) or ensure your user has read access to Docker's storage directory." >&2
        exit 1
    fi
fi

# Separator for pairing UIDs with names.
readonly sep=' '

# Build ID-to-name mapping from verbose output.
# Pipeline: verbose output → extract patterns → pair consecutive lines
# Result: "uid1 name1\nuid2 name2\n..."
id_to_name=$(
    docker buildx du --filter 'type=exec.cachemount' --verbose | \
    sed -n \
        -e 's|^ID:[[:space:]]*\([a-z0-9]\+\).*|\1|p' \
        -e 's|.*with id "/\([^"]\+\).*|\1|p' | \
    paste -d"$sep" - -
)

# Prepend NAME column (and optionally FS_SIZE) to standard output.
docker buildx du --filter 'type=exec.cachemount' | \
    awk -v id_to_name="$id_to_name" \
        -v sep="$sep" \
        -v show_fs_size="$SHOW_FS_SIZE" \
        -v overlay_path="$OVERLAY_PATH" '
        BEGIN {
            # Build hash table for fast name lookups.
            # Input: "uid1 name1\nuid2 name2\n..."
            # Output: names[uid1] = name1, names[uid2] = name2, ...
            split(id_to_name, lines, "\n")
            for (i in lines) {
                split(lines[i], pair, sep)
                names[pair[1]] = pair[2]
            }
        }

        NR == 1 {
            # Print headers
            if (show_fs_size == "true") {
                printf "%-40s %-15s %-30s %-15s %s\n", "NAME", "FS SIZE*", "ID", "DOCKER SIZE", "LAST ACCESSED"
            } else {
                printf "%-40s %-30s %-15s %s\n", "NAME", "ID", "SIZE", "LAST ACCESSED"
            }
            next
        }

        {
            # Extract columns from docker buildx du output.
            id = $1
            size = $3
            last_accessed = ""      # could have multiple values i.e. columns after the 4th.
            for (i = 4; i <= NF; i++) {
                last_accessed = last_accessed (i > 4 ? " " : "") $i
            }

            # Find associated name.
            sub(/\*$/, "", id)                           # Remove trailing asterisk from ID.
            name = (id in names) ? names[id] : "unknown" # Lookup name in hash table.

            if (show_fs_size == "true") {
                # Compute actual filesystem size.
                cmd = "du -sh " overlay_path "/" id " 2>/dev/null | awk '\''{print $1}'\''"
                cmd | getline fs_size
                close(cmd)
                if (fs_size == "") fs_size = "N/A"
                printf "%-40s %-15s %-30s %-15s %s\n", name, fs_size, id, size, last_accessed
            } else {
                printf "%-40s %-30s %-15s %s\n", name, id, size, last_accessed
            }
        }
        END {
            # Print footnote if showing filesystem size.
            if (show_fs_size == "true" && NR > 1) {
                print ""
                print "* Filesystem size measured under " overlay_path "/<ID>"
            }
        }
    '
