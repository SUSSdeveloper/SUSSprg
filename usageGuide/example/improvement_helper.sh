#!/bin/bash
input_file=$1
# Get the last value of time to use as max_t
max_t=$(tail -n 1 "$input_file" | awk '{print $4}')
max_t=$(($max_t / 1000))



# Create or empty the output file
> temp.temp

# Loop from 0 to max_t
for ((i=0; i<=max_t; i++)); do
    # Multiply i by 1000 to find the corresponding t value in input file
    target_t=$((i * 1000))

    # Use awk to find the closest t value to target_t and get its corresponding td
    h=$(awk -v target_t="$target_t" 'BEGIN{min_diff=1e9; h=""} 
    {
        diff=target_t-$4; 
        if (diff<0) diff=-diff; 
        if (diff<min_diff) {min_diff=diff; h=$20}
    } 
    END{print h}' "$input_file")

    # Append the result to the output file
    echo "$i $h" >> temp.temp
done
suffix=${input_file##*.}
mv temp.temp temp."$suffix"
