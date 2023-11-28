CC = mpicc

main: main.c
	$(CC) main.c -o main

clean:
	rm -f main