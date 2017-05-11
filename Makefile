all:
	mpicc -o main main.c

run:
	mpicc -o main main.c
	mpirun -nodes tusci08,tusci12,tusci04 main matrix1 matrix2
