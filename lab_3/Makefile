CC = mpicc
CFLAGS = -O2 -Wall -Wextra -std=c11 -fopenmp
TARGET = wsn
SOURCES = wsn_sim.c ev_node.c base_station.c

$(TARGET): $(SOURCES) wsn.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

run: $(TARGET)
	mpirun --oversubscribe -np 10 ./$(TARGET) 3 3 6 30 1000 2 0.75 10

clean:
	rm -f $(TARGET) *.log

.PHONY: run clean
