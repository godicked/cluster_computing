#include <stdio.h>
#include <mpi.h>
#include <stdlib.h>
#include <math.h>

#define MAX(a,b) (((a)>(b))?(a):(b))
#define INFO_BUFFER 4
#define MATRIX_INFO 0
#define MATRIX_A    1
#define MATRIX_B    2
#define MATRIX_C    3

typedef struct matrix_struct
{
    int *matrix;
    int rows;
    int columns;
} matrix;

int read_matrix(char *filename, matrix *A)
{
   printf("\nRead matrix...");
   MPI_File fh;
   MPI_Status status;
   MPI_Offset size;
   MPI_Offset position = 0;

   MPI_File_open(MPI_COMM_SELF, filename, MPI_MODE_RDONLY, MPI_INFO_NULL, &fh);
   MPI_File_get_size(fh, &size);
 
   printf("file size: %d\n", size);

   const int buffer_size = 1000;
   char buffer[buffer_size];
   int i;
   A->rows = 0;
   A->columns = 0;
   int max_byte_length = 0;
   int byte_length = 0;


   while(position < size)
   {
       MPI_File_seek(fh, position, MPI_SEEK_SET);
       MPI_File_read(fh, buffer, buffer_size, MPI_CHAR, &status); 
       
       
       for(i = 0; i < buffer_size && position + i < size; i++)
       {
	  byte_length++;
          if(A->rows == 0 && buffer[i] == ' ')
	  {
	    A->columns++;
	  }
	  if(buffer[i] == '\n')
	  {
	     A->rows++;
	     max_byte_length = MAX(byte_length, max_byte_length);
	     byte_length = 0;
	  }
       }

       position += buffer_size;
  
   }
   A->columns++;
   int matrix_size = A->rows * A->columns;
   
   A->matrix = (int *) malloc(matrix_size * sizeof(int));
   //printf(", max byte length: %d\n", max_byte_length);

   int row = 0;
   char *line_buffer = (char *) malloc(max_byte_length * sizeof(char));
   position = 0;
   int index = 0;

   while(position < size)
   {
       MPI_File_seek(fh, position, MPI_SEEK_SET);
       MPI_File_read(fh, line_buffer, max_byte_length, MPI_CHAR, &status); 

       int col = 0;
       char *pos = line_buffer;
       
       while(pos < line_buffer + max_byte_length)
       {
	   char *end;
	   long int value = strtol(pos, &end, 10);
	   if( value == 0L && end == pos)
	       break;

	   A->matrix[index] = (int) value; 
	   //printf("%d:%ld ", index, value);
       	   pos = end;
	   index++;

	   if(*pos == '\n')
	   {
	       pos++;
	       break;
	   }
       
       }
       position += pos - line_buffer;
   }

   free(line_buffer);
 
   //printf("Matrix size: %d x %d\n", A->rows, A->columns);
   MPI_File_close(&fh);
}

void print_matrix(matrix A)
{
   int i,j;
   printf("\nPrint matrix %d x %d\n", A.rows, A.columns);
   for(i = 0; i < A.rows; i++)
   {
       for(j = 0; j < A.columns; j++)
       {
	   printf("%d ", A.matrix[i * A.columns + j]);
       }
       printf("\n");
   }
   printf("\n");
}

void free_matrix(matrix *A)
{
    free(A->matrix);
}


void send_matrix_parts(matrix A, matrix B, matrix *A_rest)
{
    int comm_size;
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    int chunk_size = ceil(A.rows / comm_size);

    MPI_Request request;
    int buffer[INFO_BUFFER];
    buffer[0] = chunk_size;
    buffer[1] = A.columns;
    buffer[2] = B.rows;
    buffer[3] = B.columns;

    int worker;
    int *pos_A = A.matrix;
    for(worker = 1; worker < comm_size; worker++)
    {
	MPI_Isend(buffer, INFO_BUFFER, MPI_INT, worker, MATRIX_INFO, MPI_COMM_WORLD, 
		&request);

        MPI_Isend(pos_A, chunk_size * A.columns, MPI_INT, worker, MATRIX_A, 
		MPI_COMM_WORLD, &request);

	MPI_Isend(B.matrix, B.rows * B.columns, MPI_INT, worker, MATRIX_B,
		MPI_COMM_WORLD, &request);

	pos_A += chunk_size * A.columns;
    }

    int A_rest_size = (A.matrix + (A.rows * A.columns)) - pos_A;
    //printf("rest size : %d", A_rest_size);
    A_rest->columns = A.columns;
    A_rest->rows = A_rest_size / A_rest->columns;
    A_rest->matrix = pos_A;
    
}

void recv_matrix_parts(matrix *A, matrix *B)
{
   int buffer[INFO_BUFFER];
   MPI_Status status;
   MPI_Recv(buffer, INFO_BUFFER, MPI_INT, 0, MATRIX_INFO, MPI_COMM_WORLD, &status);
       
   //printf("A: %d x %d, B: %d x %d\n", buffer[0], buffer[1], buffer[2], buffer[3]);

   A->rows 	= buffer[0];
   A->columns 	= buffer[1];
   int A_size	= A->rows * A->columns;
   A->matrix	= (int *) malloc(A_size * sizeof(int));
   B->rows 	= buffer[2];
   B->columns 	= buffer[3];
   int B_size	= B->rows * B->columns;
   B->matrix	= (int *) malloc(B_size * sizeof(int));

   MPI_Recv(A->matrix, A_size , MPI_INT, 0, MATRIX_A, MPI_COMM_WORLD, &status);
   MPI_Recv(B->matrix, B_size , MPI_INT, 0, MATRIX_B, MPI_COMM_WORLD, &status);

}

void multiply_matrix(matrix A, matrix B, matrix *C)
{
    C->rows 	= A.rows;
    C->columns 	= B.columns;
    int C_size 	= C->rows * C->columns;
    C->matrix	= (int *) malloc(C_size * sizeof(int));

    //print_matrix(A);
    //print_matrix(B);

    int C_index;
    for(C_index = 0; C_index < C_size; C_index++)
    {
	int i;
	int value 	= 0;
	int row 	= C_index / C->columns;
	int column 	= C_index - (row * C->columns);

	//printf("row: %d, column: %d\n", row, column);
	for(i = 0; i < A.columns; i++)
	{
	    
	    int A_index = row * A.columns + i;
	    int B_index = i * B.columns + column;
	    //printf("%d: %d,%d ", i,  A_index, B_index);

	    value += A.matrix[A_index] * B.matrix[B_index];
	}

	C->matrix[C_index] = value;
	//printf("\n");
    }
}

int main(int argc, char **argv)
{
   int node;
   
   MPI_Init(&argc,&argv);
   MPI_Comm_rank(MPI_COMM_WORLD, &node);
   MPI_Status status;

   printf("\nHello World from Node %d\n",node);

   matrix A,B;
   
   if(node == 0)
   {
       read_matrix(argv[1], &A);
       //print_matrix(A);
       read_matrix(argv[2], &B);
       //print_matrix(B);
       
       matrix A_rest;
       send_matrix_parts(A, B, &A_rest);
       //print_matrix(A_rest);
   }    
   else
   {
       matrix C;
       recv_matrix_parts(&A, &B);
       multiply_matrix(A, B, &C);
       if(node == 1)
       {
	   print_matrix(C);
       }

   }

   //printf("node: %d finalize\n", node);

   free_matrix(&A);
   free_matrix(&B);
   
   MPI_Finalize();
   return 0;
}












