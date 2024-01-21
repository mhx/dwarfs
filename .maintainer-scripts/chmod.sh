#!/bin/bash

set -eu

# Function to generate a random octal permission mode, including sticky and suid bits
generate_random_octal_mode() {
    echo "$((RANDOM % 8))$((RANDOM % 8))$((RANDOM % 8))"
}

# Function to generate a random octal permission mode, including sticky and suid bits
generate_random_octal_mode_special() {
    local special_bits=("4" "2" "1" "0") # 4 for SUID, 2 for SGID, 1 for sticky bit, 0 for none
    local operations=("+" "-" "=")
    local special_bit=""
    local op=""
    if [ $((RANDOM % 5)) -eq 0 ]; then
        special_bit=${special_bits[$RANDOM % ${#special_bits[@]}]}
    fi
    if [ $((RANDOM % 5)) -eq 0 ]; then
        op=${operations[$RANDOM % ${#operations[@]}]}
    fi
    echo "$op$special_bit$(generate_random_octal_mode)"
}

# Function to generate a valid complex random symbolic permission mode
generate_random_complex_symbolic_mode() {
    local symbols=("u" "g" "o" "a")
    local operations=("+" "-" "=")
    local permissions=("r" "w" "x" "X" "s" "t") # Including 'X' for conditional execute, 's' for SUID/SGID, 't' for sticky bit
    local copyfrom=("u" "g" "o")

    # Generating symbol groups and permissions
    local result=""
    local num_symbols=$((RANDOM % 3 + 1))
    for (( i=0; i<$num_symbols; i++ )); do
        local symbol=${symbols[$RANDOM % ${#symbols[@]}]}
        # Avoid duplicating symbols
        if [[ $result != *"$symbol"* ]]; then
            result+="$symbol"
        fi
    done

    if [ $((RANDOM % 5)) -eq 0 ]; then
        local operation=${operations[$RANDOM % ${#operations[@]}]}
        local source=${copyfrom[$RANDOM % ${#copyfrom[@]}]}
        result+="$operation$source"
    else
        local num_groups=$((RANDOM % 3 + 1))
        local ops=""
        for (( i=0; i<$num_groups; i++ )); do
            local operation=${operations[$RANDOM % ${#operations[@]}]}
            if [[ $ops != *"$operation"* ]]; then
                ops+="$operation"
                local permission_group=""
                local num_permissions=$((RANDOM % 3 + 1))
                for (( i=0; i<$num_permissions; i++ )); do
                    local permission=${permissions[$RANDOM % ${#permissions[@]}]}
                    # Avoid duplicating permissions
                    if [[ $permission_group != *"$permission"* ]]; then
                        permission_group+="$permission"
                    fi
                done
                result+="$operation$permission_group"
            fi
        done
    fi

    echo "$result"
}

# Main loop
for i in {1..5000}; do
    # Generate random chmod mode string
    if [ $((RANDOM % 10)) -eq 0 ]; then
        mode=$(generate_random_octal_mode_special)
    else
        mode=$(generate_random_complex_symbolic_mode)
    fi

    # Set random umask
    umask_value=$(generate_random_octal_mode)
    umask $umask_value

    # Create a dummy file with random permissions and get its initial permissions
    dummy_file="dummy_$i.txt"
    touch $dummy_file
    chmod $(generate_random_octal_mode_special) $dummy_file
    original_permissions=$(stat -c "%a" $dummy_file)

    # Run chmod with the generated mode on the dummy file
    chmod $mode $dummy_file

    # Read the new permissions from the dummy file
    new_permissions=$(stat -c "%a" $dummy_file)

    # Print umask, chmod string, original and new file permissions
    cat <<EOF
    {"$mode", 0$umask_value, 0$original_permissions, 0$new_permissions},
EOF

    # Clean up
    rm -f $dummy_file
done
