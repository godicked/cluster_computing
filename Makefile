all:
	mpicc -o main main.c
run1:
	mpirun -nodes tusci08 main bmatrix1 bmatrix2 bmatrix3
run2:
	mpirun -nodes tusci08,tusci12 main bmatrix1 bmatrix2 bmatrix3
run3:
	mpirun -nodes tusci08,tusci12,tusci04 main bmatrix1 bmatrix2 bmatrix3
run4:
	mpirun -nodes tusci08,tusci12,tusci04,tusci00 main bmatrix1 bmatrix2 bmatrix3
