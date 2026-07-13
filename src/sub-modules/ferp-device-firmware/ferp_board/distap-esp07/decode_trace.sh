#!/bin/bash


# Define the ELF file path
ELF_FILE="build/rtos_dis_tap_esp07.elf"

# Check if the input file is provided
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <input_file>"
    exit 1
fi

INPUT_FILE="$1"

# Check if the input file exists
if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: File '$INPUT_FILE' not found!"
    exit 1
fi

# Read the content of the file into a variable
input=$(cat "$INPUT_FILE")
# Initialize an array
addresses=()

# Extract each address and add it to the array
while read -r address; do
    addresses+=("$address")
done < <(echo "$input" | tr ' ' '\n')

# Display the addresses
echo "Extracted addresses:"
for addr in "${addresses[@]}"; do
    echo "$addr"
    "C:\msys32\opt\xtensa-lx106-elf\bin\xtensa-lx106-elf-addr2line.exe" -pfiaC -e "$ELF_FILE" "$addr"
done
