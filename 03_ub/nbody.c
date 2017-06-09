#include <stdio.h>
#include <mpi.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define MIN(a,b) (((a)<(b))?(a):(b))

#define MASTER_ID 0

#define DIST_THRESHOLD 5


typedef struct
{
    float x;
    float y;
    float weight;
} point;


typedef struct
{
    float x;
    float y;
} vector;


//  compute the acceleration defining the movement from a to b
vector acceleration(point *a, point *b)
{
    float dx    = b->x - a->x;
    float dy    = b->y - a->y;
    float dist  = sqrt(dx*dx + dy*dy);

    //  If two points are close the bigger weight absorbs the other.
    //  The pointer address is used in case of a tie to decide which point absorbs the other.
    //  This way we are sure that every node will chose the same point.
    if(dist < DIST_THRESHOLD)
    {
        //printf("THRESHOLD\n");
        if(a->weight > b->weight || (a->weight == b->weight && a < b))
        {
            a->weight *= b->weight;
            b->weight = 0;
        }
        else
        {
            b->weight *= a->weight;
            a->weight = 0;
        }

        vector direction = {0, 0};
        return direction;
    }

    vector direction;
    direction.x = dx / dist;
    direction.y = dy / dist;

    float acc = b->weight / (dist * dist);

    direction.x *= acc;
    direction.y *= acc;

    //printf("weight: %.1f, dist: %.1f, computed acc: %.2f\n", b->weight, dist, acc);

    return direction;
}

void actualise_vel(vector *vel, vector acc)
{
    //printf("acc: %.1f:%.1f\n", acc.x, acc.y);
    vel->x += acc.x;
    vel->y += acc.y;

    //printf("vel: %.1f:%.1f\n", vel->x, vel->y);
}

//  Update point position
//  Absorbed points ( weight == 0) are ignored
void compute_movement(  point *points, vector *point_vel, unsigned int offset,
                        unsigned int compute_size, unsigned int point_size) 
{
    unsigned int i, j;

    //compute new point_vel for each point between offset and offset + compute_size
    for(i = offset; i < offset + compute_size; i++)
    {
        point *p1 = &points[i];
        if(p1->weight == 0) continue;
        
        vector acc = {0, 0};
        for(j = 0; j < point_size; j++)
        {
            point *p2 = &points[j];
            if (p2->weight == 0 || p1 == p2) continue;

            actualise_vel(&acc, acceleration(p1, p2));
        }
        actualise_vel(&point_vel[i - offset], acc);
    }

    // compute new point position with the new point_vel
    for(i = offset; i < offset + compute_size; i++)
    {
        point *p = &points[i];
        
        if(p->weight == 0) continue;

        p->x += point_vel[i - offset].x;
        p->y += point_vel[i - offset].y;
    }
}


void read_point(char *filename, point **points, int *full_size)
{
    FILE *fp;
    long size;
    fp = fopen(filename, "r");

    fseek(fp, 0, SEEK_END);
    size = ftell(fp); // get current file pointer
    rewind(fp);

    char *buffer = (char *) malloc(size * sizeof(float));
    *points = (point *) malloc(size * sizeof(point));

    int result = fread(buffer, 1, size, fp);
    if(result != size) { perror("read error\n"); }

    char *pos = buffer;
    int index = 0;

    //printf("read: %s\n", buffer);

    while (pos < buffer + size)
    {
        char *end;
        float x = strtod(pos, &end);
        pos = end;
        float y = strtod(pos, &end);
        pos = end;
        float weight = strtod(pos, &end);

        if (end == pos)
            break;
        
        point p = { x, y, weight };
	//printf("x: %.1f, y: %.1f, weight: %.1f\n", x, y, weight);
        (*points)[index] = p;

        pos = end;
        index++;
    }

    *full_size = index;

    free(buffer);
    fclose(fp);

    //printf("end read points\n");
}

void send_point(point *points, int size)
{
    //printf("start send point\n");
    MPI_Bcast(&size, 1, MPI_INT, MASTER_ID, MPI_COMM_WORLD);
    MPI_Bcast(points, size * 3, MPI_FLOAT, MASTER_ID, MPI_COMM_WORLD);   
}

//  Initialize velocity vector
void init_vel(vector **point_vel, int size)
{
    *point_vel = (vector *) malloc(size * sizeof(vector));

    int i;
    for(i = 0; i < size; i++)
    {
        vector v = {0, 0};      // null velocity at start
        (*point_vel)[i] = v;
    }
}

void receive_points(point **points, int *full_size)
{
    int size;
    MPI_Bcast(&size, 1, MPI_INT, MASTER_ID, MPI_COMM_WORLD);

    *points = (point *) malloc(size * sizeof(point));
    MPI_Bcast(*points, size * 3, MPI_FLOAT, MASTER_ID, MPI_COMM_WORLD); // Each point consist of 3 float

    *full_size = size;

    //printf("worker received points\n");
}

void update_points(int comm_size, point *points, int size)
{

    size *= 3;	// Each point consist of 3 float
    int chunk =  (size / comm_size);
    int node_id;
    for(node_id = 0; node_id < comm_size; node_id++)
    {
        int start   = chunk * node_id;
        
	if(node_id == comm_size -1)
	    chunk = size - start;

	//printf("node_id: %d, start: %d, size: %d, count: %d\n", node_id, start, size, chunk);
        MPI_Bcast(points + (start/3), chunk, MPI_FLOAT, node_id, MPI_COMM_WORLD);
    }
}

void work(int node_id, int comm_size, point *points, int full_size, int iteration)
{
    // overhead counter
    double overhead = 0;

    vector *point_vel;
    int compute_size;
    int offset;
    
    compute_size = full_size / comm_size;
    offset = node_id * compute_size;

    if(node_id == comm_size -1)
    	compute_size = (full_size + comm_size -1 ) / comm_size ;
    
    //printf("full_size: %d, comm_size: %d, node_id: %d, compute_size: %d\n", full_size, comm_size, node_id, compute_size);

    init_vel(&point_vel, compute_size);

    int i;
    for(i = 0; i < iteration; i++)
    {
        compute_movement(points, point_vel, offset, compute_size, full_size);

	double time = MPI_Wtime();
        update_points(comm_size, points, full_size);
	overhead += MPI_Wtime() - time;
    }
    

    free(point_vel);
    //printf("%d end work\n", node_id);
    if(node_id == 0)
    	printf("communication overhead: %.1f sec\n", overhead);
    
}

void write_point( char *filename, point *points, int size) 
{
	
    FILE *fp;
    fp = fopen(filename, "w");

    int i;
    for(i = 0; i < size; i++)
    {
	point p = points[i];
	fprintf(fp, "%.1f %.1f %.1f\n", p.x, p.y, p.weight);
    }

    fclose(fp);
}


int main(int argc, char **argv)
{
    //printf("START MAIN\n");
    int node_id;            // MPI rank from 0 to n nodes
    int comm_size;          // number of nodes
    point *points;          // array holding all the points
    int full_size;          // number of points


    //  MPI initialisation
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &node_id);
    MPI_Status status;
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    
    int iteration = (int) strtol(argv[1], NULL, 10);

    if(node_id == MASTER_ID)
    {
	//printf("Master started\n");
        read_point(argv[2], &points, &full_size);

	// Take time
	double time = MPI_Wtime();

	// Main work
        send_point(points, full_size);
        work(node_id, comm_size, points, full_size, iteration);

	// Final time
	double final_time = MPI_Wtime() - time;
	printf("Simulation took: %.1f sec, for: %d iterations with: %d nodes\n", final_time, iteration, comm_size);
        write_point(argv[3], points, full_size);    
    }
    else
    {
	//printf("Worker started\n");
        receive_points(&points, &full_size);
        work(node_id, comm_size, points, full_size, iteration);
    }
    
    //printf("finalize\n");
    free(points);

    MPI_Finalize();
}
