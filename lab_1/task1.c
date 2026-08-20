/*
Task 1 – Serial Code - Finding Prime Numbers

Write a serial C program to search for prime numbers that are strictly less
than an integer n, provided by the user. The program will output a sorted
list of all prime numbers found.

Function parameters:
  - argc: Number of command-line arguments passed to the program.
    The program expects one argument in addition to the program name:
    the maximum number n to search for prime numbers.
  - argv: Array containing the command-line arguments.
    argv[0] is the program name, while argv[1] contains the value of n.
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