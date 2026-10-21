Our current design had some flaws so here are the changes made to the design that was implemented in the code:

1. Polling neighbours
Issue: Our slides gives pollNeighbours() to base station but it should be the EVNodes running the check. 
Fix: The EVNodes only poll neighbours when they are congested and alerts base station when every enighbour is also congested.

2. Neighbour polling deadlocks
Issue: two adjacent nodes are both full. Each sends a request and then sits in MPI_Waitall waiting for replies.
Fix: Use Testall which checks that all requests have been sent before 
    if (polling) MPI_Testall(nnb, poll_reqs, &done, ...);   // not Waitall
   
3. Use less blocking messages
The blocking MPI_Send "for urgent alerts" is not faster. Using MPI_Isend to complete it later. The base station also shouldn't block on MPI_Recv. Changed it to loop on MPI_Iprobe.

4. Alerts don't hold enough
Now sending alerts with a lot more information built with MPI_Type_create_struct. This also reduces number of MPI calls.

5. Added shutdown
Base station now sends a TERMINATE to every node, then writes and closes the log. Each node then stops new neighbour polls, continues to answer existing requests, joins the threads and frees memory.


Current structure:
`#pragma omp parallel num_threads(k+1)`
Thread 0 handles all MPI communication.
Threads 1..k each update status[port] once per cycle.
Locking. Each thread writes only its own slot, so writes don't need atomic. 
