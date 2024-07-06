#!/bin/bash

# Check if exactly one argument is provided
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <filename>"
    exit 1
fi

# Get the filename from the argument
input_file=$1

# Check if the input_file matches the pattern raw.suss1 or raw.suss0
if ! [[ "$input_file" == "raw.suss1" || "$input_file" == "raw.suss0" ]]; then
    echo "Filename does not match the pattern (raw.suss0 or raw.suss1)."
    exit 1
fi

# Define the pattern to match
pattern="SUSSmsg cubic starts sending data. Follow id=[0-9]* for Sport=20480"

# Count the number of matching lines
matching_lines=$(grep -E "$pattern$" "$input_file" | wc -l)

if [ "$matching_lines" -ne 1 ]; then
    echo "There are $matching_lines number of download in $input_file"
    echo "This script only supports a single download test case."
    exit 1
fi

suffix=${input_file##*.}
base_file="base.$suffix"
data_file="data.$suffix"

# Extract the id from the specific line
id=$(grep -Eo "SUSSmsg cubic starts sending data. Follow id=[0-9]+ for Sport=20480" "$input_file" | grep -Eo "id=[0-9]+" | cut -d '=' -f 2)

# Check if the id was found
if [ -z "$id" ]; then
    echo "Pattern not found in the input file."
    exit 1
fi

# Use grep to extract lines containing the id and save them to the output file
grep -E " id=$id " "$input_file" > "$base_file"


grep "@ id=" "$base_file" | sed 's/.*@ id=/id=/' |sed 's/=/ /g'> temp.temp




# Read the base time from the first line
base_time=$(awk 'NR==1 {print $4}' "temp.temp")

# Process the file and write to the output file
awk -v base_time="$base_time" '
{
    if (NR == 1) {
        $4 = 0
    } else {
        $4 = $4 - base_time
    }
    print
}' "temp.temp" > "$data_file"

rm temp.temp

echo "$data_file was created successfully."
