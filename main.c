#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <pthread.h>
#include <string.h>


#define MAX(a,b) \
    ({ __typeof__ (a) _a = (a); \
        __typeof__ (b) _b = (b); \
        _a > _b ? _a : _b; })

#define MAIN_ERROR(...) if (rank == 0) fprintf(stderr, __VA_ARGS__)
#define ERROR(...) fprintf(stderr, __VA_ARGS__)
#define DEBUG(...) ({ printf("DEBUG: "); printf(__VA_ARGS__); })


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


typedef struct RequestInfo {
    int source;
    int ts;
} RequestInfo;

typedef struct RequestQueue {
    RequestInfo* requests;
    size_t size;

} RequestQueue;


RequestQueue hotel_requests;
RequestQueue guide_requests;


void add_request_to_queue(RequestQueue* queue, RequestInfo request) {
    size_t i;
    for (i = 0; i < queue->size; ++i) {
        if ((queue->requests[i].ts == request.ts && queue->requests[i].source > request.source) || 
            queue->requests[i].ts > request.ts) {
            break;
        }
    }

    if (i < queue->size)
        memmove(queue->requests + i, queue->requests + i + 1, queue->size - i);
    queue->requests[i] = request;
    queue->size++;
}

void remove_request_from_queue(RequestQueue* queue, int source) {
    for (size_t i = 0; i < queue->size; ++i) {
        if (queue->requests[i].source == source) {
            if (i < queue->size - 1)
                memmove(queue->requests + i + 1, queue->requests + i, queue->size - i - 1);
            queue->size--;
            return;
        }
    }
}


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
int recv_packet(MPI_Status* status) {

    MPI_Recv(recv_buffer, buffer_size, MPI_PACKED, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, status);
    pthread_mutex_lock(&clock_guard);

    // TODO: add debug message

    int position = 0;
    int s_ts;
    int v_ts;
    MPI_Unpack(recv_buffer, buffer_size, &position, &s_ts, 1, MPI_INT, MPI_COMM_WORLD);
    scalar_ts = MAX(scalar_ts, s_ts) + 1;
    last_received_scalar_ts[status->MPI_SOURCE] = scalar_ts;

    ++vector_ts[rank];
    for (int i = 0; i < size; ++i) {
        MPI_Unpack(recv_buffer, buffer_size, &position, &v_ts, 1, MPI_INT, MPI_COMM_WORLD);
        vector_ts[i] = MAX(vector_ts[i], v_ts);
    }
    pthread_mutex_unlock(&clock_guard);
    
    return s_ts;
}



void change_state(State new_state) {
    pthread_mutex_lock(&state_guard);
    // TODO: add debug message
    state = new_state;
    pthread_mutex_unlock(&state_guard);
}



void* listener_loop(void* arg) {
    MPI_Status status;
    int finished_counter = 0;

    while(finished_counter < size) {
        int ts = recv_packet(&status);

        switch (status.MPI_TAG) {
            case REQ_HOTEL: {
                add_request_to_queue(&hotel_requests, (RequestInfo){status.MPI_SOURCE, ts});
                send_packet(status.MPI_SOURCE, ACK_HOTEL);
            } break;
            case REQ_GUIDE: {
                add_request_to_queue(&guide_requests, (RequestInfo){status.MPI_SOURCE, ts});
                send_packet(status.MPI_SOURCE, ACK_GUIDE);
            } break;
            case ACK_HOTEL: {
                ++ack_hotel_counter;
            } break;
            case ACK_GUIDE: {
                ++ack_guide_counter;
            } break;
            case RELEASE_HOTEL: {
                remove_request_from_queue(&hotel_requests, status.MPI_SOURCE);
            } break;
            case RELEASE_GUIDE: {
                remove_request_from_queue(&hotel_requests, status.MPI_SOURCE);
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

    if (argc != 6) {
        MAIN_ERROR("Usage: %s <purple_aliens> <blue_aliens> <cleaners> <hotels> <hotel_capacity>\n", argv[0]);
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    int purple_aliens, blue_aliens, cleaners, hotels, hotel_capacity;
    if (sscanf(argv[1], "%d", &purple_aliens)  != 1 ||
        sscanf(argv[2], "%d", &blue_aliens)    != 1 ||
        sscanf(argv[3], "%d", &cleaners)       != 1 ||
        sscanf(argv[4], "%d", &hotels)         != 1 ||
        sscanf(argv[5], "%d", &hotel_capacity) != 1) {
        MAIN_ERROR("Error: Please provide valid integers.\n");
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    if (purple_aliens < 0 || blue_aliens < 0 || cleaners < 0 || hotels < 0 || hotel_capacity < 0) {
        MAIN_ERROR("Error: Please provide non-negative integers.\n");
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    const int num_of_processes = purple_aliens + blue_aliens + cleaners;

    if (num_of_processes > size) {
        MAIN_ERROR("Error: The number of processes available (%d) is insufficient, expecting %d processes.\n", size, num_of_processes);
        MPI_Finalize();
        return EXIT_FAILURE;
    } else if (num_of_processes < size) {
        MAIN_ERROR("Warning: Excess processes detected. Currently %d processes, but only %d processes are expected. %d processes will be inactive.\n",
                    size, num_of_processes, size - num_of_processes);
    }

    // Ensure that initialisation Errors/Warning are displayed on top
    MPI_Barrier(MPI_COMM_WORLD);

    if (rank < purple_aliens) {
        process_type = ALIEN_PURPLE;
        DEBUG("Process %d becomes a purple alien.\n", rank);
    } else if (rank < purple_aliens + blue_aliens) {
        process_type = ALIEN_BLUE;
        DEBUG("Process %d becomes a blue alien.\n", rank);
    } else if (rank < num_of_processes) {
        process_type = CLEANER;
        DEBUG("Process %d becomes a cleaner.\n", rank);
    } else {
        DEBUG("Process %d finishes due to being inactive.\n", rank);
        MPI_Finalize();
        return EXIT_SUCCESS;
    }

    // Overwrite size with number of active processes
    size = num_of_processes;

    vector_ts = calloc(size * sizeof(int), 0);
    last_received_scalar_ts = calloc(size * sizeof(int), 0);

    buffer_size = (1 + size) * sizeof(int);
    send_buffer = malloc(buffer_size);
    recv_buffer = malloc(buffer_size);

    hotel_requests = (RequestQueue){malloc(size * sizeof(RequestInfo)), 0};
    guide_requests = (RequestQueue){malloc(size * sizeof(RequestInfo)), 0};

    // TODO: create a function to determine process type
    process_type = ALIEN_BLUE;

    pthread_t main_thread, listener_thread;

    // if (process_type == ALIEN_BLUE || process_type == ALIEN_PURPLE) {
    //     pthread_create(&main_thread, NULL, alien_loop, NULL);
    // } else {
    //     pthread_create(&main_thread, NULL, cleaner_loop, NULL);
    // }
    // pthread_create(&listener_thread, NULL, listener_loop, NULL);


    // pthread_join(main_thread, NULL);
    // pthread_join(listener_thread, NULL);


    MPI_Finalize();

    free(vector_ts);
    free(last_received_scalar_ts);
    free(send_buffer);
    free(recv_buffer);
    free(hotel_requests.requests);
    free(guide_requests.requests);

    return EXIT_SUCCESS;
}