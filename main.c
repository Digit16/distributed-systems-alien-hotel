#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <pthread.h>


#define MAX(a,b) \
    ({ __typeof__ (a) _a = (a); \
        __typeof__ (b) _b = (b); \
        _a > _b ? _a : _b; })


typedef enum Tag {
    REQ_HOTEL,
    REQ_GUIDE,
    ACK_HOTEL,
    ACK_GUIDE,
    RELEASE_HOTEL,
    RELEASE_GUIDE,
    FINISHED,
} Tag;


typedef enum State {
    REST,                // Waiting
    WAIT_HOTEL,          // Requesting Hotel
    INSECTION_HOTEL,     // In section Hotel
    WAIT_GUIDE,          // Requesting Guide
    INSECTION_GUIDE,     // In section Hotel and in section Guide
} State;


// process id and number of processes
int rank;
int size;

// clocks of a current process
int scalar_ts = 0;
int* vector_ts;

// array of last received timestamps for every process
int* last_received_scalar_ts;

// buffers for MPI_Send and MPI_Recv
char* send_buffer;
char* recv_buffer;
size_t buffer_size;

// number of received ACK after REQ was sent
int ack_hotel_counter = 0;
int ack_guide_counter = 0;


pthread_mutex_t clock_guard = PTHREAD_MUTEX_INITIALIZER;

// send packet scalar and vector clocks
void send_packet(int dest, Tag tag) {
    pthread_mutex_lock(&clock_guard);
    ++scalar_ts;
    ++vector_ts[rank];
    int position = 0;
    MPI_Pack(&scalar_ts, 1, MPI_INT, send_buffer, buffer_size, &position, MPI_COMM_WORLD);
    MPI_Pack(vector_ts, size, MPI_INT, send_buffer, buffer_size, &position, MPI_COMM_WORLD);
    MPI_Send(send_buffer, position, MPI_PACKED, dest, (int)tag, MPI_COMM_WORLD);
    pthread_mutex_unlock(&clock_guard);
}

// recieve packet with scalar and vector clocks
void recv_packet(MPI_Status* status) {
    MPI_Recv(recv_buffer, buffer_size, MPI_PACKED, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, status);
    pthread_mutex_lock(&clock_guard);
    int position = 0;
    int ts;
    MPI_Unpack(recv_buffer, buffer_size, &position, &ts, 1, MPI_INT, MPI_COMM_WORLD);
    scalar_ts = MAX(scalar_ts, ts) + 1;
    last_received_scalar_ts[status->MPI_SOURCE] = scalar_ts;
    for (int i = 0; i < size; ++i) {
        MPI_Unpack(recv_buffer, buffer_size, &position, &ts, 1, MPI_INT, MPI_COMM_WORLD);
        vector_ts[i] = MAX(vector_ts[i], ts) + 1;
    }
    pthread_mutex_unlock(&clock_guard);
}


void* listener_loop(void* arg) {
    MPI_Status status;

    int finished_counter = 0;

    while(finished_counter < size) {
        recv_packet(&status);

        switch (status.MPI_TAG) {
            case REQ_HOTEL: {
            // TODO: add request to list
            send_packet(status.MPI_SOURCE, ACK_HOTEL);
            } break;
            case REQ_GUIDE: {
            // TODO: add request to list
            send_packet(status.MPI_SOURCE, ACK_GUIDE);
            } break;
            case ACK_HOTEL: {
            ++ack_hotel_counter;
            } break;
            case ACK_GUIDE: {
            ++ack_guide_counter;
            } break;
            case RELEASE_HOTEL: {
            // TODO: remove from requests list
            } break;
            case RELEASE_GUIDE: {
            // TODO: remove from requests list
            } break;
            case FINISHED: {
            ++finished_counter;
            } break;

            // TODO: check ack counter conditions
        }
    }
}


int main(int argc, char** argv) {

   MPI_Init(&argc, &argv);
   MPI_Comm_size(MPI_COMM_WORLD, &size);
   MPI_Comm_rank(MPI_COMM_WORLD, &rank);

   vector_ts = calloc(size * sizeof(int), 0);
   last_received_scalar_ts = calloc(size * sizeof(int), 0);

   buffer_size = (1 + size) * sizeof(int);
   send_buffer = malloc(buffer_size);
   recv_buffer = malloc(buffer_size);

   
   send_packet((rank + 1) % size, ACK_HOTEL);
   MPI_Status status;
   recv_packet(&status);
   printf("%d - received from %d\n", rank, status.MPI_SOURCE);


   MPI_Finalize();
   free(vector_ts);
   free(send_buffer);
   free(recv_buffer);

   return EXIT_SUCCESS;
}