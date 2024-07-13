#!/bin/bash

# Check if both data.suss0 and data.suss1 exist
if [ ! -f "data.suss0" ] || [ ! -f "data.suss1" ]; then
    echo "One or both files (data.suss0 and data.suss1) do not exist."
    exit 1  # Exit with a status code indicating an error.
fi


bash improvement_helper.sh data.suss0
bash improvement_helper.sh data.suss1
paste temp.suss0 temp.suss1 | awk '{print $1, $2, $4}' > improvement.dat

rm temp.suss?

