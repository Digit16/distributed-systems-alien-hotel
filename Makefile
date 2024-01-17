CC = mpicc

SRC = main.c request_queue.c

main: $(SRC)
	$(CC) $(SRC) -o main

clean:
	rm -f main