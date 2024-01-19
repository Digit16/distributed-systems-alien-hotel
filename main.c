#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>

#include "request_queue.h"
#include "utils.h"

#define NUMBER_OF_ROUNDS 2

// number of processes
extern int num_of_purple_aliens, num_of_blue_aliens, num_of_cleaners;

// number of resources
extern int num_of_hotels, hotels_capacity, num_of_guides;

// current state of the process
extern ProcessType process_type;

// process id and number of processes
extern int rank;
extern int size;

extern int picked_hotel;

// clocks of a current process
extern int scalar_ts;

// array of last received timestamps for every process
extern int* last_received_scalar_ts;

// flags for entering critical sections
extern bool can_enter_hotel_section;
extern bool can_enter_guide_section;


pthread_mutex_t clock_guard = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t state_guard = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t signal_guard = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t signal_cond = PTHREAD_COND_INITIALIZER;

Queue** hotel_requests;
Queue* guide_requests;


ProcessType get_process_type(int process_rank) {
    if (process_rank < num_of_purple_aliens) {
        return PROCESS_PURPLE_ALIEN;
    } else if (process_rank < num_of_purple_aliens + num_of_blue_aliens) {
        return PROCESS_BLUE_ALIEN;
    } else if (process_rank < num_of_purple_aliens + num_of_blue_aliens + num_of_cleaners) {
        return PROCESS_CLEANER;
    } else {
        return PROCESS_NONE;
    }
}


int get_min_timestamp(int range) {
    pthread_mutex_lock(&clock_guard);
    int min_ts = last_received_scalar_ts[0];
    for (size_t i = 1; i < range; ++i) {
        min_ts = MIN(min_ts, last_received_scalar_ts[i]);
    }
    pthread_mutex_unlock(&clock_guard);

    return min_ts;
}


// send packet to processed with rank in range
void send_packet_range(int from_rank, int to_rank, Tag tag, int resource_idx) {
    pthread_mutex_lock(&clock_guard);
    ++scalar_ts;
    int position = 0;

    PayloadData payload;
    payload.hotel_idx = resource_idx;
    payload.ts = scalar_ts;

    // MPI_Pack(&scalar_ts, 1, MPI_INT, send_buffer, buffer_size, &position, MPI_COMM_WORLD);
    // MPI_Pack(&resource_idx, 1, MPI_INT, send_buffer, buffer_size, &position, MPI_COMM_WORLD);

    // TODO: add debug message
    for (int i = from_rank; i < to_rank; i++)
        MPI_Send(&payload, sizeof(payload), MPI_BYTE, i, (int)tag, MPI_COMM_WORLD);
    pthread_mutex_unlock(&clock_guard);
}


// send packet to single process
void send_packet(int dest, Tag tag, int resource_idx) {
    send_packet_range(dest, dest+1, tag, resource_idx);
}


// recieve packet with scalar and vector clocks
PacketData recv_packet() {

    MPI_Status status;
    PayloadData payload;

    MPI_Recv(&payload, sizeof(payload), MPI_BYTE, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
    pthread_mutex_lock(&clock_guard);

    // TODO: add debug message


    // MPI_Unpack(recv_buffer, buffer_size, &position, &s_ts, 1, MPI_INT, MPI_COMM_WORLD);
    // MPI_Unpack(recv_buffer, buffer_size, &position, &resource_idx, 1, MPI_INT, MPI_COMM_WORLD);

    scalar_ts = MAX(scalar_ts, payload.ts) + 1;

    last_received_scalar_ts[status.MPI_SOURCE] = payload.ts;

    pthread_mutex_unlock(&clock_guard);
    
    return (PacketData){
        .source = status.MPI_SOURCE,
        .process_type = get_process_type(status.MPI_SOURCE),
        .tag = status.MPI_TAG,
        .ts = payload.ts,
        .resource_idx = payload.hotel_idx
    };
}

void signal_enter_hotel() {
    pthread_mutex_lock(&signal_guard);
    can_enter_hotel_section = true;
    pthread_cond_signal(&signal_cond);
    pthread_mutex_unlock(&signal_guard);
}

void signal_enter_guide() {
    pthread_mutex_lock(&signal_guard);
    can_enter_guide_section = true;
    pthread_cond_signal(&signal_cond);
    pthread_mutex_unlock(&signal_guard);
}

void wait_hotel() {
    pthread_mutex_lock(&signal_guard);
    while (!can_enter_hotel_section) pthread_cond_wait(&signal_cond, &signal_guard);
    pthread_mutex_unlock(&signal_guard);
}

void wait_guide() {
    pthread_mutex_lock(&signal_guard);
    while (!can_enter_guide_section) pthread_cond_wait(&signal_cond, &signal_guard);
    pthread_mutex_unlock(&signal_guard);
}

void check_for_hotel(int hotel_idx) {
    int min_ts = get_min_timestamp(size);

    int hotel_queue_position = 0;
    Queue* head = hotel_requests[hotel_idx];

    DEBUG("Hotel queue state:");
    while(head != NULL) {
        DEBUG("source: %d, ts: %d", ((PacketData*)head->data)->source, ((PacketData*)head->data)->ts);
        head = head->next;
    }

    head = hotel_requests[hotel_idx];

    while (head != NULL && ((PacketData*)head->data)->ts <= min_ts && hotel_queue_position < hotels_capacity) {

        PacketData* packet = ((PacketData*)head->data);

        if (process_type == PROCESS_CLEANER) {
            if (rank == packet->source && hotel_queue_position == 0) {
                signal_enter_hotel();
            }
            return;
        }

        if (process_type == packet->process_type) {
            if (rank == packet->source) {
                signal_enter_hotel();
                return;
            }
        } else {
            return; 
        }

        hotel_queue_position++;
        head = head->next;
    }

}

void check_for_quide() {
    int min_ts = get_min_timestamp(num_of_blue_aliens + num_of_purple_aliens);

    int guide_queue_position = 0;
    Queue* head = guide_requests;

    while (head != NULL && ((PacketData*)head->data)->ts <= min_ts && guide_queue_position < num_of_guides) {
        
        // check if our process (source == rank) is in queue
        if ( ((PacketData*)head->data)->source == rank ) {
            // DEBUG("[%d, %d]: my requests in on %d position with %d ts while min_ts is %d", scalar_ts, rank, guide_queue_position, ((PacketData*)head->data)->ts, min_ts);
            signal_enter_guide();
            return;
        }

        guide_queue_position++;
        head = head->next;
    }
}


void* alien_listener_loop(void* arg) {
    int finished_counter = 0;

    while(finished_counter < size) {
        PacketData received_packet_data = recv_packet();

        switch (received_packet_data.tag) {
            case TAG_REQ_HOTEL:
                DEBUG("received TAG_REQ_HOTEL from %d", received_packet_data.source);
                enqueue_packet(&hotel_requests[received_packet_data.resource_idx], received_packet_data);
                DEBUG("sending TAG_ACK_HOTEL to %d", received_packet_data.source);
                send_packet(received_packet_data.source, TAG_ACK_HOTEL, -1);
                break;
            case TAG_REQ_GUIDE:
                DEBUG("received TAG_REQ_GUIDE from %d", received_packet_data.source);
                enqueue_packet(&guide_requests, received_packet_data);
                DEBUG("sending TAG_ACK_GUIDE to %d", received_packet_data.source);
                send_packet(received_packet_data.source, TAG_ACK_GUIDE, -1);
                break;
            case TAG_RELEASE_HOTEL:
                DEBUG("received TAG_RELEASE_HOTEL from %d", received_packet_data.source);
                dequeue_matching(&hotel_requests[received_packet_data.resource_idx], &received_packet_data.source, (Comparison)match_packet_source);
                break;
            case TAG_RELEASE_GUIDE:
                DEBUG("received TAG_RELEASE_GUIDE from %d", received_packet_data.source);
                dequeue_matching(&guide_requests, &received_packet_data.source, (Comparison)match_packet_source);
                break;
            case TAG_FINISHED:
                DEBUG("received TAG_FINISHED from %d", received_packet_data.source);
                ++finished_counter;
                break;
            case TAG_ACK_HOTEL:
                DEBUG("received TAG_ACK_HOTEL from %d", received_packet_data.source);
                break;
            case TAG_ACK_GUIDE:
                DEBUG("received TAG_ACK_GUIDE from %d", received_packet_data.source);
                break;
            default:
                ERROR("RECEIVED INVALID PACKET from %d", received_packet_data.source);
        }

        check_for_hotel(picked_hotel);
        check_for_quide();
    }

    return NULL;
}


void* cleaner_listener_loop(void* arg) {
    int finished_counter = 0;

    while(finished_counter < size) {
        PacketData received_packet_data = recv_packet();

        switch (received_packet_data.tag) {
            case TAG_REQ_HOTEL:
                DEBUG("received TAG_REQ_HOTEL from %d", received_packet_data.source);
                enqueue_packet(&hotel_requests[received_packet_data.resource_idx], received_packet_data);
                DEBUG("sending TAG_ACK_HOTEL to %d", received_packet_data.source);
                send_packet(received_packet_data.source, TAG_ACK_HOTEL, -1);
                break;
            case TAG_RELEASE_HOTEL:
                DEBUG("received TAG_RELEASE_HOTEL from %d", received_packet_data.source);
                dequeue_matching(&hotel_requests[received_packet_data.resource_idx], &received_packet_data.source, (Comparison)match_packet_source);
                break;
            case TAG_FINISHED:
                DEBUG("received TAG_FINISHED from %d", received_packet_data.source);
                ++finished_counter;
                break;
            case TAG_ACK_HOTEL:
                DEBUG("received TAG_ACK_HOTEL from %d", received_packet_data.source);
                break;
            default:
                ERROR("RECEIVED INVALID PACKET from %d", received_packet_data.source);
        }

        check_for_hotel(picked_hotel);
    }

    return NULL;
}


void* alien_loop(void* arg) {

    for (int i = 0; i < NUMBER_OF_ROUNDS; i++) {
        
        picked_hotel = rand() % num_of_hotels;

        // requesting hotel
        INFO("requesting hotel %d!", picked_hotel);
        can_enter_hotel_section = false;
        send_packet_range(0, size, TAG_REQ_HOTEL, picked_hotel);
        wait_hotel();

        // being in hotel
        INFO("entering hotel %d!", picked_hotel);
        sleep(4);

        // requesting guide
        INFO("requesting guide!");
        can_enter_guide_section = false;
        send_packet_range(0, num_of_purple_aliens + num_of_blue_aliens, TAG_REQ_GUIDE, -1);
        wait_guide();

        // having a guide
        INFO("having guide!");
        sleep(4);

        // realising guide
        INFO("releasing guide!");
        send_packet_range(0, num_of_purple_aliens + num_of_blue_aliens, TAG_RELEASE_GUIDE, -1);
        sleep(2);

        // realising hotel
        INFO("leaving hotel %d!", picked_hotel);
        send_packet_range(0, size, TAG_RELEASE_HOTEL, picked_hotel);
        sleep(2);
    }

    send_packet_range(0, size, TAG_FINISHED, -1);

}


void* cleaner_loop(void* arg) {

    for (int i = 0; i < NUMBER_OF_ROUNDS; i++) {
        
        picked_hotel = rand() % num_of_hotels;

        // requesting hotel
        INFO("requesting hotel %d!", picked_hotel);
        can_enter_hotel_section = false;
        send_packet_range(0, size, TAG_REQ_HOTEL, picked_hotel);
        wait_hotel();

        // being in hotel
        INFO("entering hotel %d!", picked_hotel);
        sleep(4);

        // realising hotel
        INFO("leaving hotel %d!", picked_hotel);
        send_packet_range(0, size, TAG_RELEASE_HOTEL, picked_hotel);
        sleep(2);
    }

    send_packet_range(0, size, TAG_FINISHED, -1);

}


int main(int argc, char** argv) {

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (argc != 7) {
        MAIN_ERROR("Usage: %s <purple_aliens> <blue_aliens> <cleaners> <hotels> <hotel_capacity> <guides>", argv[0]);
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    if (sscanf(argv[1], "%d", &num_of_purple_aliens)  != 1 ||
        sscanf(argv[2], "%d", &num_of_blue_aliens)    != 1 ||
        sscanf(argv[3], "%d", &num_of_cleaners)       != 1 ||
        sscanf(argv[4], "%d", &num_of_hotels)         != 1 ||
        sscanf(argv[5], "%d", &hotels_capacity) != 1 ||
        sscanf(argv[6], "%d", &num_of_guides) != 1) {
        MAIN_ERROR("Error: Please provide valid integers.");
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    if (num_of_purple_aliens < 0 || num_of_blue_aliens < 0 || num_of_cleaners < 0 || num_of_hotels < 0 || hotels_capacity < 0 || num_of_guides < 0) {
        MAIN_ERROR("Error: Please provide non-negative integers.");
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    const int num_of_processes = num_of_purple_aliens + num_of_blue_aliens + num_of_cleaners;

    if (num_of_processes > size) {
        MAIN_ERROR("Error: The number of processes available (%d) is insufficient, expecting %d processes.", size, num_of_processes);
        MPI_Finalize();
        return EXIT_FAILURE;
    } else if (num_of_processes < size) {
        MAIN_ERROR("Error: Excess processes detected. %d Processes detected, but only %d processes are expected.", size, num_of_processes);
        MPI_Finalize();
        return EXIT_FAILURE;     
    }

    // Ensure that initialisation Errors/Warning are displayed on top
    MPI_Barrier(MPI_COMM_WORLD);

    process_type = get_process_type(rank);
    switch(process_type) {
    case PROCESS_PURPLE_ALIEN:
        INFO("Process %d becomes a purple alien.", rank); break;
    case PROCESS_BLUE_ALIEN:
        INFO("Process %d becomes a blue alien.", rank); break;
    case PROCESS_CLEANER:
        INFO("Process %d becomes a cleaner.", rank); break;
    default:
        ERROR("Process %d finishes due to being inactive.", rank);
        MPI_Finalize();
        return EXIT_SUCCESS;
    }
    
    last_received_scalar_ts = calloc(size, sizeof(int));

    hotel_requests = calloc(num_of_hotels, sizeof(Queue*));

    pthread_t main_thread, listener_thread;
    
    srand(rank*time(NULL));

    MPI_Barrier(MPI_COMM_WORLD);

    if (process_type == PROCESS_BLUE_ALIEN || process_type == PROCESS_PURPLE_ALIEN) {
        pthread_create(&main_thread, NULL, alien_loop, NULL);
        pthread_create(&listener_thread, NULL, alien_listener_loop, NULL);
    } else {
        pthread_create(&main_thread, NULL, cleaner_loop, NULL);
        pthread_create(&listener_thread, NULL, cleaner_listener_loop, NULL);
    }

    pthread_join(main_thread, NULL);
    pthread_join(listener_thread, NULL);

    MPI_Finalize();

    free(last_received_scalar_ts);
    free(hotel_requests);

    return EXIT_SUCCESS;
}