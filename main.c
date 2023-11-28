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

typedef enum ProcessType {
    ALIEN_PURPLE,
    ALIEN_BLUE,
    CLEANER,
} ProcessType;

// current state of the process
State state = REST;
ProcessType process_type;

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
pthread_mutex_t state_guard = PTHREAD_MUTEX_INITIALIZER;


// send packet scalar and vector clocks
void send_packet(int dest, Tag tag) {
    pthread_mutex_lock(&clock_guard);
    ++scalar_ts;
    ++vector_ts[rank];
    int position = 0;
    MPI_Pack(&scalar_ts, 1, MPI_INT, send_buffer, buffer_size, &position, MPI_COMM_WORLD);
    MPI_Pack(vector_ts, size, MPI_INT, send_buffer, buffer_size, &position, MPI_COMM_WORLD);

    // TODO: add debug message

    MPI_Send(send_buffer, position, MPI_PACKED, dest, (int)tag, MPI_COMM_WORLD);
    pthread_mutex_unlock(&clock_guard);
}


// recieve packet with scalar and vector clocks
MPI_Status recv_packet(void) {
    MPI_Status status;

    MPI_Recv(recv_buffer, buffer_size, MPI_PACKED, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
    pthread_mutex_lock(&clock_guard);

    // TODO: add debug message

    int position = 0;
    int ts;
    MPI_Unpack(recv_buffer, buffer_size, &position, &ts, 1, MPI_INT, MPI_COMM_WORLD);
    scalar_ts = MAX(scalar_ts, ts) + 1;
    last_received_scalar_ts[status.MPI_SOURCE] = scalar_ts;

    ++vector_ts[rank];
    for (int i = 0; i < size; ++i) {
        MPI_Unpack(recv_buffer, buffer_size, &position, &ts, 1, MPI_INT, MPI_COMM_WORLD);
        vector_ts[i] = MAX(vector_ts[i], ts);
    }
    pthread_mutex_unlock(&clock_guard);
    
    return status;
}



void change_state(State new_state) {
    pthread_mutex_lock(&state_guard);
    // TODO: add debug message
    state = new_state;
    pthread_mutex_unlock(&state_guard);
}



void* listener_loop(void* arg) {
    int finished_counter = 0;

    while(finished_counter < size) {
        MPI_Status status = recv_packet();

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

            
        }

        // TODO: check ack counter conditions
    }
}



void* alien_loop(void* arg) {

}



void* cleaner_loop(void* arg) {

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

    // TODO: create a function to determine process type
    process_type = ALIEN_BLUE;

    pthread_t main_thread, listener_thread;


    if (process_type == ALIEN_BLUE || process_type == ALIEN_PURPLE) {
        pthread_create(&main_thread, (void*)0, alien_loop, (void*)0);
    } else {
        pthread_create(&main_thread, (void*)0, cleaner_loop, (void*)0);
    }

    pthread_create(&listener_thread, (void*)0, listener_loop, (void*)0);


    pthread_join(main_thread, (void*)0);
    pthread_join(listener_thread, (void*)0);


    MPI_Finalize();
    free(vector_ts);
    free(send_buffer);
    free(recv_buffer);

    return EXIT_SUCCESS;
}