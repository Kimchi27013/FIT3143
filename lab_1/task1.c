/*
Task 1 – Serial Code - Finding Prime Numbers
Write a serial C program to search for prime numbers that are strictly less than an integer n,
provided by the user. The program will output a sorted list of all prime numbers found.
Example: For instance, if the user inputs n as 10 on the terminal, the prime numbers being
printed are: 2, 3, 5, 7 (sorted in ascending order).
Your program is required to have the capability to print the sorted list of prime numbers to:
a) the standard output (for small n values, e.g., n < 100), and
b) a text file (for larger n values, e.g., n > 100).
*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char *argv[])
{
    // Check for command-line argument
    if (argc < 2) {
        printf("Error: Please provide at least one argument.\n");
        printf("Usage: %s max_number\n", argv[0]);
        return 1;
    }

    const char *filename = "prime-out.txt";
    FILE *fp = fopen(filename, "w");

    if (fp == NULL) {
        printf("Error opening file!\n");
        return 1;
    }

    clock_t start = clock();

    // Convert command-line argument to an integer
    int n = atoi(argv[1]);

    // Use file output for large values of n
    int writeToTxt = (n > 100) ? 1 : 0;

    // Check every number from 2 up to n - 1
    for (int i = 2; i < n; i++) {
        int prime = 1;

        // Check if i has any divisors
        for (int j = 2; j*j < i; j++) {
            if (i % j == 0) {
                prime = 0;
                break;
            }
        }

        // Output the number if it is prime
        if (prime == 1) {
            if (writeToTxt) {
                fprintf(fp, "%d\n", i);
            } else {
                printf("%d\n", i);
            }
        }
    }

    clock_t end = clock();

    // Calculate and display CPU time
    double cpu_time_used =
        ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("CPU time used: %f seconds\n", cpu_time_used);

    fclose(fp);

    return 0;
}