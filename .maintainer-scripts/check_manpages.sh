#!/bin/bash

script_dir="$(cd $(dirname $0); pwd)"
manpage_dir="$script_dir/../doc"
check_manpage_script="$script_dir/check_manpage.py"

# function to check tool help vs manpage

function check_manpage() {
    local tool="./$1"
    local tool_help=$2
    local manpage="$manpage_dir/$1.md"

    # check that the manpage exists
    if [ ! -f "$manpage" ]; then
        echo "ERROR: '$manpage' does not exist"
        exit 1
    fi

    # check that the tool exists
    if [ ! -f "$tool" ]; then
        echo "ERROR: '$tool' does not exist"
        exit 1
    fi

    $check_manpage_script "$manpage" "$tool" "$tool_help"
}

check_manpage mkdwarfs --long-help
check_manpage dwarfsck --help
check_manpage dwarfsextract --help
# cannot easily check `dwarfs` at the moment
