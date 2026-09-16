#!/bin/bash

# 1. Compile the code with high optimization
echo "Compiling task1.c..."
mpicc -O3 -o task1 task1.c -lm

# 2. Set the number of MPI processes (Match this to your POSIX thread count)
CORES=4

# 3. Create the CSV file and write the header
OUTPUT_FILE="task1_scaling_results.csv"
echo "n,processes,total_time" > $OUTPUT_FILE

echo "Starting benchmark for n = 10,000,000 to 200,000,000..."

# 4. Loop from 10M to 200M in increments of 5M
for ((n=140000000; n<=200000000; n+=5000000))
do
    echo "Testing n = $n..."
    
    # Run the program, find the line with "Total Execution", and extract the number
    EXEC_TIME=$(mpirun -np $CORES ./task1 $n | grep "Total Execution" | awk '{print $4}')
    
    # Append the result to the CSV
    echo "$n,$CORES,$EXEC_TIME" >> $OUTPUT_FILE
done

echo "Benchmarking complete! All results safely stored in $OUTPUT_FILE."