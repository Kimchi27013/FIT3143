#!/bin/bash

# 1. Compile the hybrid code with OpenMP and math library linked
echo "Compiling task2.c..."
mpicc -O3 -fopenmp -o task2 task2.c -lm

# 2. Set the hybrid hardware configuration 
# Total computing units = MPI_PROCS * OMP_THREADS
MPI_PROCS=2
OMP_THREADS=2

# 3. Create the CSV file and write the header
OUTPUT_FILE="task2_scaling_results.csv"
echo "n,mpi_processes,omp_threads,total_time" > $OUTPUT_FILE

echo "Starting hybrid benchmark for n = 10,000,000 to 200,000,000..."
echo "Configuration: $MPI_PROCS MPI Processes x $OMP_THREADS OpenMP Threads"

# 4. Loop from 10M to 200M in increments of 5M
for ((n=10000000; n<=200000000; n+=5000000))
do
    echo "Testing n = $n..."
    
    # Run the program, find the line with "Total Execution", and extract the number
    EXEC_TIME=$(mpirun -np $MPI_PROCS ./task2 $n $OMP_THREADS | grep "Total Execution" | awk '{print $4}')
    
    # Append the result to the CSV
    echo "$n,$MPI_PROCS,$OMP_THREADS,$EXEC_TIME" >> $OUTPUT_FILE
done

echo "Benchmarking complete! All results safely stored in $OUTPUT_FILE."