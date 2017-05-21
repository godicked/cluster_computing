#include <sisci_api.h>
#include <sisci_error.h>
#include <stdio.h>
#include <mpi.h>

#define NO_FLAGS 0
#define NO_CALLBACK 0
#define NO_ARG 0

#define SEGMENT_ID 4 << 16 
#define ADAPTER_NO 0

int main(int argc, char **argv)
{
   int node;
   
   MPI_Init(&argc,&argv);
   MPI_Comm_rank(MPI_COMM_WORLD, &node);
   MPI_Status status;

   sci_desc_t	v_dev;
    sci_error_t error;


    SCIInitialize(NO_FLAGS, &error);

    if(error != SCI_ERR_OK)
	printf("error Init\n");

    SCIOpen(&v_dev, NO_FLAGS, &error);

    if(error != SCI_ERR_OK) {
	printf("Error Open\n");
	return 1;
    }

    unsigned int local_node_id;
    sci_query_adapter_t query;
    query.subcommand = SCI_Q_ADAPTER_NODEID;
    query.localAdapterNo = ADAPTER_NO;
    query.data = &local_node_id;
    SCIQuery(SCI_Q_ADAPTER, &query, NO_FLAGS, &error);
  
    printf("Node id: %d\n", local_node_id);

    unsigned int SEGMENT_SIZE = 4097;

    if(local_node_id == 4)
    {
	sci_local_segment_t local_segment;
	int *local_address;
	sci_map_t local_map;

	printf("Master segment size: %d\n", SEGMENT_SIZE);

	SCICreateSegment(v_dev, &local_segment, SEGMENT_ID, SEGMENT_SIZE, NO_CALLBACK,
				NO_ARG, NO_FLAGS, &error);
	if(error != SCI_ERR_OK)
	    printf("Master error! %d\n", error);

	printf("prepare segment \n");

	SCIPrepareSegment(local_segment, ADAPTER_NO, NO_FLAGS, &error);

	local_address = (int *) SCIMapLocalSegment(local_segment, &local_map, 0, 
		SEGMENT_SIZE, 0, NO_FLAGS, &error);

	local_address[0] = 1;
	local_address[1] = 2;
	
	SCISetSegmentAvailable(local_segment, ADAPTER_NO, NO_FLAGS, &error);

	MPI_Barrier(MPI_COMM_WORLD);
    }
    else
    {

	MPI_Barrier(MPI_COMM_WORLD);

	sci_remote_segment_t remote_segment;
	sci_map_t remote_map;
	unsigned int segment_size;
	volatile int *remote_address;

	//do {
	SCIConnectSegment(v_dev, &remote_segment, 4, SEGMENT_ID, ADAPTER_NO,
		    NO_CALLBACK, NO_ARG, SCI_INFINITE_TIMEOUT,
		    NO_FLAGS, &error);
	//}while(error != SCI_ERR_OK);

	if(error != SCI_ERR_OK)
	    printf("error: %d !!! \n", error);

	segment_size = SCIGetRemoteSegmentSize(remote_segment);
	//segment_size = 8;
	printf("segment size: %d\n", segment_size);

	remote_address = (volatile int *) SCIMapRemoteSegment(remote_segment, 
		&remote_map, 0, segment_size, 0, NO_FLAGS, &error);

	printf("Node: %d, first two array value: %d, %d\n", local_node_id,
		remote_address[0], remote_address[1]);

    }

    MPI_Barrier(MPI_COMM_WORLD);

 
    SCIClose(v_dev, NO_FLAGS, &error);
    SCITerminate();
    MPI_Finalize();
    return 0;
}
