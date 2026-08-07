////////////////////////////////////////////////////////////////////////////
// Vector_Cell_Product_Parallel.c
// -------------------------------------------------------------------------
//
// Performs a cell by cell product between two vectors using POSIX threads
//
//////////////////////////////////////////////////////////////////////////
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <time.h>
#include <pthread.h> 

#define NUM_THREADS 24

int *pArrayNum1 = NULL;
int *pArrayNum2 = NULL;
int *pArrayNum3 = NULL;
int row1, col1, row2, col2;

// Function prototype
int* ReadFromFile(char *pFilename, int *pOutRow, int *pOutCol);
void WriteToFile(char *pFilename, int *pMatrix, int inRow, int inCol);
void *ThreadFunc(void *pArg); // POSIX thread function format

int main()
{
	int i;
	struct timespec start, end, startComp, endComp; 
	double time_taken; 

	pthread_t tid[NUM_THREADS];
	int threadNum[NUM_THREADS];

    	// Get current clock time.
	clock_gettime(CLOCK_MONOTONIC, &start); 

	printf("Cell Product!\n\n");
	printf("Commence Reading\n");

    	// Call the read from file function
	pArrayNum1 = ReadFromFile("VA.txt", &row1, &col1);
	
    	if(pArrayNum1 == 0)
	{
	    printf("Read failed.\n");
	    return 0;
	}
	

    	// Call the read from file function
	pArrayNum2 = ReadFromFile("VB.txt", &row2, &col2);
	
    	if(pArrayNum2 == 0)
	{
	    printf("Read failed.\n");
	    free(pArrayNum1);
	    return 0;
	}
	
	if(row1 != row2 || col1 != col2)
	{
	    printf("Not matching row and column values between the arrays.\n");
	    free(pArrayNum1);
	    free(pArrayNum2);
	    return 0;
	}
	printf("Read complete\n");
	// Perform the cell by cell product
	
	printf("Compute\n");
	
	pArrayNum3 = (int*)malloc(row1 * sizeof(int));
	
	// Get current clock time.
	clock_gettime(CLOCK_MONOTONIC, &startComp); 
	
    	// Fork		
	for (i = 0; i < NUM_THREADS; i++)
	{
	    	threadNum[i] = i;
		pthread_create(&tid[i], 0, ThreadFunc, &threadNum[i]);
	}
	
	// Join
	for(i = 0; i < NUM_THREADS; i++)
	{
	    	pthread_join(tid[i], NULL);
	}


	// Get the clock current time again
	// Subtract end from start to get the CPU time used.
	clock_gettime(CLOCK_MONOTONIC, &endComp); 
	time_taken = (endComp.tv_sec - startComp.tv_sec) * 1e9; 
    	time_taken = (time_taken + (endComp.tv_nsec - startComp.tv_nsec)) * 1e-9; 
	printf("Cell product complete - Computational time only(s): %lf\n", time_taken);


	// Write to file
	printf("Commence Writing\n");
	WriteToFile("VC.txt", pArrayNum3, row1, col1);
	printf("Write complete\n");
	free(pArrayNum1);
	free(pArrayNum2);
	free(pArrayNum3);
	
	// Get the clock current time again
	// Subtract end from start to get the CPU time used.
	clock_gettime(CLOCK_MONOTONIC, &end); 
	time_taken = (end.tv_sec - start.tv_sec) * 1e9; 
    	time_taken = (time_taken + (end.tv_nsec - start.tv_nsec)) * 1e-9; 
	printf("Overall time (Including read, product and write)(s): %lf\n", time_taken);	
	
	return 0;
}


// Function definition
void *ThreadFunc(void *pArg)
{
	int i, j;
	int my_rank = *((int*)pArg);
	printf("Thread %d\n", my_rank);
	
	int rpt = row1 / NUM_THREADS; // rpt = rows per thread (in this e.g, row1 = 20M, rpt = 20M/6 ~ 3.33M
	int rptr = row1 % NUM_THREADS; // rpt = rows per thread remainder
	
	int sp = my_rank * rpt; // e.g., my_rank = 1, sp = 3.3M,
	int ep = sp + rpt; // ep = 6.6M
	if(my_rank == NUM_THREADS-1)
		ep += rptr;
	
	for(i = sp; i< ep; i++){ // Shared memory parallelism - Row segmentation
		for(j = 0; j< 500; j++){
	    		pArrayNum3[i] = pArrayNum1[i] * pArrayNum2[i];
		}
	}

	return NULL;
}



int* ReadFromFile(char *pFilename, int *pOutRow, int *pOutCol)
{
	int i, j;
	int row, col;
	FILE *pFile = fopen(pFilename, "r");
	if(pFile == NULL)
	{
		printf("Error: Cannot open file\n");
		return 0;
	}

	fscanf(pFile, "%d%d", &row, &col);
	int *pMatrix = (int*)malloc(row * col * sizeof(int)); // Heap array

	// Reading a 2D matrix into a 1D heap array
	for(i = 0; i < row; i++){
	    for(j = 0; j < col; j++){
		fscanf(pFile, "%d", &pMatrix[(i * col) + j]);
		}
	}
	fclose(pFile);

	*pOutRow = row; // Dereferencing the pointer
	*pOutCol = col; // Dereferencing the pointer
	return pMatrix;
}

void WriteToFile(char *pFilename, int *pMatrix, int inRow, int inCol)
{
	int i, j;
	FILE *pFile = fopen(pFilename, "w");
	fprintf(pFile, "%d\t%d\n", inRow, inCol);
	for(i = 0; i < inRow; i++){
	    for(j = 0; j < inCol; j++){
		fprintf(pFile, "%d\t", pMatrix[(i * inCol) + j]);
		}
		fprintf(pFile, "\n");
	}
	fclose(pFile);
}