#include <stdio.h>
#include <mpi.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define MIN(a,b) (((a)<(b))?(a):(b))

#define MASTER_ID 0

#define DIST_THRESHOLD 0.1


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
        printf("THRESHOLD\n");
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

    return direction;
}

void actualise_vel(vector *vel, vector acc)
{
    printf("acc: %.1f:%.1f\n", acc.x, acc.y);
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
        actualise_vel(&point_vel[i], acc);
    }

    // compute new point position with the new point_vel
    for(i = offset; i < offset + compute_size; i++)
    {
        point *p = &points[i];
        
        if(p->weight == 0) continue;

        p->x += point_vel[i].x;
        p->y += point_vel[i].y;
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

    while (pos < buffer + size)
    {
        char *end;
        float x = strtof(pos, &end);
        pos = end;
        float y = strtof(pos, &end);
        pos = end;
        float weight = strtof(pos, &end);

        if (end == pos)
            break;
        
        point p = {(float) x, (float) y, (float) weight};
        (*points)[index] = p;

        pos = end;
        index++;
    }

    *full_size = index;

    free(buffer);
    fclose(fp);
}

void send_point(point *points, int size)
{
    MPI_Bcast(&size, 1, MPI_INT, MASTER_ID, MPI_COMM_WORLD);
    MPI_Bcast(points, size * 3, MPI_FLOAT, MASTER_ID, MPI_COMM_WORLD);   
}

//  Initialize velocity vector
void init_vel(vector **point_vel, int size)
{
    *point_vel = malloc(size * sizeof(vector));

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
}

void update_points(int comm_size, point *points, int size)
{
    int chunk = ceil((float)size / comm_size) * 3;      // Each point consist of 3 float
    int node_id;
    for(node_id = 0; node_id < comm_size; node_id++)
    {
        int start   = chunk * node_id;
        int count   = MIN(start + chunk, size) - start;

        MPI_Bcast(points + start, count, MPI_FLOAT, node_id, MPI_COMM_WORLD);
    }
}

void work(int node_id, int comm_size, point *points, int full_size, int iteration)
{
    vector *point_vel;
    int compute_size;
    int offset;
    
    compute_size = ceil((float)full_size / comm_size);
    offset = node_id * compute_size;
    if(node_id == comm_size -1)
        compute_size = floor((float) full_size / comm_size);
    
    printf("full_size: %d, compute_size: %d\n", full_size, compute_size);

    init_vel(&point_vel, compute_size);

    int i;
    for(i = 0; i < iteration; i++)
    {
        compute_movement(points, point_vel, offset, compute_size, full_size);
        update_points(comm_size, points, full_size);
    }

    free(point_vel);

}


int main(int argc, char **argv)
{
    int node_id;            // MPI rank from 0 to n nodes
    int comm_size;          // number of nodes
    point *points;          // array holding all the points
    int full_size;          // number of points


    //  MPI initialisation
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &node_id);
    MPI_Status status;
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    
    int iteration = 10;

    if(node_id == MASTER_ID)
    {
        // string input    = "points";
        // string output   = "point_res";
        read_point("points", &points, &full_size);
        send_point(points, full_size);
        work(node_id, comm_size, points, full_size, iteration);
        // write_point(output, points, full_size);    
    }
    else
    {
        receive_points(&points, &full_size);
        work(node_id, comm_size, points, full_size, iteration);
    }

    free(points);

    MPI_Finalize();
}