#include <stdio.h>
#include <omp.h>

#define N 10

int main() 
{
    int a[N] = {1,2,3,4,5,6,7,8,9,2};
    int b[N] = {1,2,3,4,5,6,7,8,9,2};
    int i;

/* // INCORRECT
    float lsum = 0.0;
    float sum = 0.0;
    #pragma omp parallel for private(lsum)
    for(i=0; i<N; i++) {
        lsum += (a[i] * b[i]);
        #pragma omp critical
        {
            sum += lsum; // reduction
        }
    }
*/

    float sum = 0.0;
    
    #pragma omp parallel // N Threads
    {
        float lsum = 0.0;
        int threadID = omp_get_thread_num();
        int numThreads = omp_get_num_threads();
        
        #pragma omp for
        for(i=0; i<N; i++) {
            lsum += (a[i] * b[i]);
        }
        #pragma omp critical
        {
            printf("Thread: %d of %d. Local Sum: %f\n", threadID, numThreads, lsum);
        
            sum += lsum; // reduction
        }
    }
    
    
    /*
    #pragma omp parallel for reduction(+:sum) schedule(dynamic, 1)
    for(i=0; i<N; i++) {
        sum += a[i] * b[i];
    }
    */

    
    
    
    printf("sum value: %f\n", sum);
    return 0;

}

