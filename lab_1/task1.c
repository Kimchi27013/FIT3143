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

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char *argv[])
{	
    if (argc < 2) {
        printf("Error: Please provide at least one argument.\n");
        printf("Usage: %s max_number\n", argv[0]);
        return 1; // Return non-zero to indicate an error
    }
    
    const char *filename = "prime-out.txt";

    FILE *fp = fopen(filename, "w");
    
    if (fp == NULL) {
        printf("Error opening file!\n");
        return 1;
    }

    clock_t start = clock();
    int n = atoi(argv[1]);

    // Decide ONCE, before the loop, where output goes.
    int writeToTxt = (n > 100) ? 1 : 0;

    for (int i = 2; i < n; i++) {
        int prime = 1;

        for (int j = 2; j < i; j++) {
            if (i % j == 0) {
                prime = 0;
                break;
            }
        }

        if (prime == 1) {
            if (writeToTxt) {
                fprintf(fp, "%d\n", i);
            } else {
                printf("%d\n", i);
            }
        }
    }

    // Stop tracking CPU time
    clock_t end = clock();

    // Calculate elapsed time in seconds
    double cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;

    printf("CPU time used: %f seconds\n", cpu_time_used);

    fclose(fp);

    return 0;
}