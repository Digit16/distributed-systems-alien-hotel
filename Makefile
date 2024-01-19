CC = mpicc

SRC = main.c request_queue.c utils.c globals.c

main: $(SRC)
	$(CC) $(SRC) -o main

clean:
	rm -f main