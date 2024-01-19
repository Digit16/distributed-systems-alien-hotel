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

// number of processes
int num_of_purple_aliens, num_of_blue_aliens, num_of_cleaners;

// number of resources
int num_of_hotels, hotels_capacity, num_of_guides;

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

// flags for entering critical sections
bool can_enter_hotel_section = false;
bool can_enter_guide_section = false;


pthread_mutex_t clock_guard = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t state_guard = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t signal_guard = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t signal_cond = PTHREAD_COND_INITIALIZER;

Queue* hotel_requests;
Queue* guide_requests;

HotelInfo* hotel_info;
int* hotel_bookings;

ProcessType get_process_type(int process_rank) {
    if (rank < num_of_purple_aliens) {
        return PROCESS_PURPLE_ALIEN;
    } else if (rank < num_of_purple_aliens + num_of_blue_aliens) {
        return PROCESS_BLUE_ALIEN;
    } else if (rank < num_of_purple_aliens + num_of_blue_aliens + num_of_cleaners) {
        return PROCESS_CLEANER;
    } else {
        return PROCESS_NONE;
    }
}


// send packet to processed with rank in range
void send_packet_range(int from_rank, int to_rank, Tag tag) {
    pthread_mutex_lock(&clock_guard);
    ++scalar_ts;
    ++vector_ts[rank];
    int position = 0;
    MPI_Pack(&scalar_ts, 1, MPI_INT, send_buffer, buffer_size, &position, MPI_COMM_WORLD);
    MPI_Pack(vector_ts, size, MPI_INT, send_buffer, buffer_size, &position, MPI_COMM_WORLD);

    // TODO: add debug message
    for (int i = from_rank; i < to_rank; i++)
        MPI_Send(send_buffer, position, MPI_PACKED, i, (int)tag, MPI_COMM_WORLD);
    pthread_mutex_unlock(&clock_guard);
}


// send packet to single process
void send_packet(int dest, Tag tag) {
    send_packet_range(dest, dest+1, tag);
}


// recieve packet with scalar and vector clocks
PacketData recv_packet() {

    MPI_Status status;

    MPI_Recv(recv_buffer, buffer_size, MPI_PACKED, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
    pthread_mutex_lock(&clock_guard);

    // TODO: add debug message

    int position = 0;
    int s_ts;
    int v_ts;
    MPI_Unpack(recv_buffer, buffer_size, &position, &s_ts, 1, MPI_INT, MPI_COMM_WORLD);
    scalar_ts = MAX(scalar_ts, s_ts) + 1;
    // DEBUG("%d, %d: received message from %d with ts: %d, [%d, %d, %d, %d]", scalar_ts,rank, status.MPI_SOURCE, s_ts,
    // last_received_scalar_ts[0], last_received_scalar_ts[1], last_received_scalar_ts[2], last_received_scalar_ts[3]);

    last_received_scalar_ts[status.MPI_SOURCE] = s_ts;


    ++vector_ts[rank];
    for (int i = 0; i < size; ++i) {
        MPI_Unpack(recv_buffer, buffer_size, &position, &v_ts, 1, MPI_INT, MPI_COMM_WORLD);
        vector_ts[i] = MAX(vector_ts[i], v_ts);
    }

    pthread_mutex_unlock(&clock_guard);
    
    return (PacketData){
        .source = status.MPI_SOURCE,
        .process_type = get_process_type(status.MPI_SOURCE),
        .tag = status.MPI_TAG,
        .ts = s_ts,
    };
}



void change_state(State new_state) {
    pthread_mutex_lock(&state_guard);
    // TODO: add debug message
    state = new_state;
    pthread_mutex_unlock(&state_guard);
}


bool can_enter_hotel(HotelInfo hotel) {
    if (hotel.rooms_occupied >= hotels_capacity) {
        return false;
    }
    if (hotel.last_process_type == PROCESS_NONE) {
        return true;
    }

    switch (process_type) {
        case PROCESS_CLEANER:
            return hotel.last_process_type != PROCESS_CLEANER;
        case PROCESS_BLUE_ALIEN:
            return hotel.last_process_type == PROCESS_BLUE_ALIEN;
        case PROCESS_PURPLE_ALIEN:
            return hotel.last_process_type == PROCESS_PURPLE_ALIEN;
        default:
            // TODO: add error handling for invalid process type
    }
}

void mark_hotel_entry(int process_rank, int hotel_idx) {

    // DEBUG("%d: process %d, enters hotel %d", rank, process_rank, hotel_idx);

    hotel_info[hotel_idx].rooms_occupied++;
    hotel_info[hotel_idx].last_process_type = process_type;

    hotel_bookings[process_rank] = hotel_idx;
}

void mark_hotel_departure(int process_rank) {

    
    // check in which hotel is the process
    int hotel_idx = hotel_bookings[process_rank];

    // DEBUG("%d: process %d, leaves hotel %d", rank, process_rank, hotel_idx);

    hotel_bookings[process_rank] = -1;
    hotel_info[hotel_idx].rooms_occupied--;
    if (hotel_info[hotel_idx].rooms_occupied == 0) {
        hotel_info[hotel_idx].last_process_type = PROCESS_NONE;
    }
    // todo add error handling
}


void manage_hotel() {
    

    int min_ts = last_received_scalar_ts[0];
    for (size_t i = 1; i < size; ++i) {
        min_ts = MIN(min_ts, last_received_scalar_ts[i]);
    }

    Queue* skip_start = NULL;
    Queue* head = hotel_requests;

    // DEBUG("%d, %d: manage hotel start with min_ts %d [%d, %d, %d, %d]", scalar_ts, rank, min_ts, last_received_scalar_ts[0], last_received_scalar_ts[1], last_received_scalar_ts[2], last_received_scalar_ts[3]);
    // DEBUG("%d: head ts is %d", rank, head != NULL ? ((PacketData*)head->data)->ts : -1);

    Queue* test_head = hotel_requests;

    while (test_head != NULL) {
        DEBUG("%d, %d: HOTELS_QUEUE %d %d %d", scalar_ts, rank, ((PacketData*)test_head->data)->source, ((PacketData*)test_head->data)->tag, ((PacketData*)test_head->data)->ts);

        test_head = test_head->next;
    }

    bool skipped = false;

    while (head != NULL && ((PacketData*)head->data)->ts <= min_ts) {
        // DEBUG("%d, %d: hotel tag %d", scalar_ts, rank, ((PacketData*)head->data)->tag);

        if (((PacketData*)head->data)->tag == TAG_REQ_HOTEL) {
            // DEBUG("%d: manage hotel request", rank);
            bool entered_hotel = false;

            // loop over all hotels
            for (size_t hotel_idx = 0; hotel_idx < hotels_capacity; ++hotel_idx) {
                // DEBUG("%d: hotel %lu has %d guests", rank, hotel_idx, hotel_info[hotel_idx].rooms_occupied);
                // check if entry condition was met
                if (can_enter_hotel(hotel_info[hotel_idx])) {
                    // DEBUG("%d: process %d enters hotel %lu", rank, ((PacketData*)head->data)->source, hotel_idx);

                    // mark entry to hotel in data structures
                    mark_hotel_entry(((PacketData*)head->data)->source, hotel_idx);
                    entered_hotel = true;

                    // if source == rank allow entry in main loop
                    if (((PacketData*)head->data)->source == rank) {
                        pthread_mutex_lock(&signal_guard);
                        can_enter_hotel_section = true;
                        pthread_cond_signal(&signal_cond);
                        pthread_mutex_unlock(&signal_guard);
                    }

                    // remove from packet queue
                    dequeue(&head);

                    
                }
            }

            if (!entered_hotel) {
                skipped = true;
                skip_start = head;
                head = head->next;
            }
            
        } else if (((PacketData*)head->data)->tag == TAG_RELEASE_HOTEL) {
            // DEBUG("%d: manage hotel release", rank);

            // mark departure from hotel in data structures
            mark_hotel_departure(((PacketData*)head->data)->source);
            
            // TODO: if any requests were skipped check if they can enter now

            dequeue(&head);
            // DEBUG("%d: manage hotel release [removed from queue]", rank);
            
            if (skipped) {
                skipped = false;
                head = skip_start;
            }
        } else {
            // TODO: add error message
        }
    }

    // Queue* head = hotel_requests;
    hotel_requests = head;
}


void manage_guide() {

    // DEBUG("%d: manage guide", rank);

    int min_ts = last_received_scalar_ts[0];
    for (size_t i = 1; i < size; ++i) {
        min_ts = MIN(min_ts, last_received_scalar_ts[i]);
    }


    int guide_queue_position = 0;
    Queue* head = guide_requests;


    while (head != NULL && ((PacketData*)head->data)->ts <= min_ts && guide_queue_position < num_of_guides) {
        
        // check if our process (source == rank) is in queue
        if ( ((PacketData*)head->data)->source == rank ) {
            // DEBUG("[%d, %d]: my requests in on %d position with %d ts while min_ts is %d", scalar_ts, rank, guide_queue_position, ((PacketData*)head->data)->ts, min_ts);
            pthread_mutex_lock(&signal_guard);
            can_enter_guide_section = true;
            pthread_cond_signal(&signal_cond);
            pthread_mutex_unlock(&signal_guard);

            return;
        }

        guide_queue_position++;
        head = head->next;
    }

    // DEBUG("%d: finished manage guide", rank);
}


void* alien_listener_loop(void* arg) {
    int finished_counter = 0;

    while(finished_counter < size) {
        PacketData received_packet_data = recv_packet();

        switch (received_packet_data.tag) {
            case TAG_REQ_HOTEL:
                // DEBUG("%d, %d: Received TAG_REQ_HOTEL", scalar_ts, rank);
                enqueue_packet(&hotel_requests, received_packet_data);
                send_packet(received_packet_data.source, TAG_ACK_HOTEL);
                break;
            case TAG_REQ_GUIDE:
                // DEBUG("%d, %d: Received TAG_REQ_GUIDE", scalar_ts, rank);
                enqueue_packet(&guide_requests, received_packet_data);
                send_packet(received_packet_data.source, TAG_ACK_GUIDE);
                break;
            case TAG_RELEASE_HOTEL:
                // DEBUG("%d, %d: Received TAG_RELEASE_HOTEL", scalar_ts, rank);
                enqueue_packet(&hotel_requests, received_packet_data);
                send_packet(received_packet_data.source, TAG_ACK_HOTEL);
                break;
            case TAG_RELEASE_GUIDE:
                // DEBUG("%d, %d: Received TAG_RELEASE_GUIDE", scalar_ts, rank);
                dequeue_matching(&guide_requests, &received_packet_data.source, (Comparison)match_packet_source);
                break;
            case TAG_FINISHED:
                DEBUG("%d, %d: Received TAG_FINISHED", scalar_ts, rank);
                ++finished_counter;
                break;
            case TAG_ACK_HOTEL:
            case TAG_ACK_GUIDE:
                // DEBUG("%d, %d: Received TAG_ACK...", scalar_ts, rank);
                break;
            default:
                ERROR("%d, %d: RECEIVED INVALID PACKET", scalar_ts, rank);
        }


        manage_hotel();
        manage_guide();
    }

    return NULL;
}


void* cleaner_listener_loop(void* arg) {
    int finished_counter = 0;

    while(finished_counter < size) {
        PacketData received_packet_data = recv_packet();

        switch (received_packet_data.tag) {
            case TAG_REQ_HOTEL:
                enqueue_packet(&hotel_requests, received_packet_data);
                send_packet(received_packet_data.source, TAG_ACK_HOTEL);
                break;
            case TAG_RELEASE_HOTEL:
                enqueue_packet(&hotel_requests, received_packet_data);
                break;
            case TAG_FINISHED:
                ++finished_counter;
                break;
            case TAG_ACK_HOTEL:
                break;
            default:
                // TODO: error
        }

        manage_hotel();
    }

    return NULL;
}



void* guide_test_loop(void* arg) {

    for (int i = 0; i < 10; i++) {
        
        INFO("[%d, %d]: requesting guide!", scalar_ts, rank);
        can_enter_guide_section = false;
        send_packet_range(0, num_of_purple_aliens + num_of_blue_aliens, TAG_REQ_GUIDE);

        change_state(WAIT_GUIDE);
        pthread_mutex_lock(&signal_guard);
        while (!can_enter_guide_section) pthread_cond_wait(&signal_cond, &signal_guard);
        pthread_mutex_unlock(&signal_guard);

        change_state(INSECTION_GUIDE);
        INFO("[%d, %d]: having guide!", scalar_ts, rank);
        sleep(40);
        INFO("[%d, %d]: releasing guide!", scalar_ts, rank);
        send_packet_range(0, num_of_purple_aliens + num_of_blue_aliens, TAG_RELEASE_GUIDE);
        sleep(20);
    }

}

void* alien_loop(void* arg) {

    // each process repeats process n times
    for (int i = 0; i < 10; i++) {

        // Rest idle
        change_state(REST);
        sleep(1);

        // Request hotel
        can_enter_hotel_section = false;
        DEBUG("%d, %d: Sending TAG_REQ_HOTEL", scalar_ts, rank);
        send_packet_range(0, size, TAG_REQ_HOTEL);
        change_state(WAIT_HOTEL);

        // Wait for acceptance and enter hotel
        pthread_mutex_lock(&signal_guard);
        while (!can_enter_hotel_section) pthread_cond_wait(&signal_cond, &signal_guard);
        pthread_mutex_unlock(&signal_guard);
        
        // Wait inside of hotel
        change_state(INSECTION_HOTEL);
        sleep(1);

        // INFO("%d: requesting guide!", rank);
        // Request guide
        can_enter_guide_section = false;
        send_packet_range(0, num_of_purple_aliens + num_of_blue_aliens, TAG_REQ_GUIDE);
        change_state(WAIT_GUIDE);
        // Wait for acceptance and enter hotel
        pthread_mutex_lock(&signal_guard);
        while (!can_enter_guide_section) pthread_cond_wait(&signal_cond, &signal_guard);
        pthread_mutex_unlock(&signal_guard);

        // Wait with guide (sightseeing)
        change_state(INSECTION_GUIDE);

        INFO("%d: having guide!", rank);

        sleep(1);


        // Release resources
        send_packet_range(0, num_of_purple_aliens + num_of_blue_aliens, TAG_RELEASE_GUIDE);
        send_packet_range(0, size, TAG_RELEASE_HOTEL);
        
        change_state(REST);

    }

    send_packet_range(0, size, TAG_FINISHED);

    return NULL;
}



void* cleaner_loop(void* arg) {

    // each process repeats process n times
    for (int i = 0; i < 10; i++) {

        // Rest idle
        change_state(REST);
        sleep(1);

        // Request hotel
        can_enter_hotel_section = false;
        send_packet_range(0, size, TAG_REQ_HOTEL);
        change_state(WAIT_HOTEL);

        // Wait for acceptance and enter hotel
        pthread_mutex_lock(&signal_guard);
        while (!can_enter_hotel_section) pthread_cond_wait(&signal_cond, &signal_guard);
        pthread_mutex_unlock(&signal_guard);
        
        // Wait inside of hotel (cleaning)
        change_state(INSECTION_HOTEL);
        sleep(1);

        // Release resources
        send_packet_range(0, size, TAG_RELEASE_HOTEL);
        change_state(REST);

    }

    send_packet_range(0, size, TAG_FINISHED);

    return NULL;
}


bool match_int(void* a, void* b) {
    return *(int*)a == *(int*)b;
}

bool comp_int(int* a, int* b) {
    return *a >= *b;
}


int main_test(void) {
    Queue* items = NULL;

    int a = 1, b = 2, c = 3, d = 4;
    enqueue(&items, &d, (Comparison)comp_int);
    enqueue(&items, &c, (Comparison)comp_int);
    enqueue(&items, &a, (Comparison)comp_int);
    enqueue(&items, &b, (Comparison)comp_int);
    
    
    
    
    // printf("%d\n", *(int*)dequeue(&items));
    // dequeue(&items);

    // dequeue_matching(&items, &c, &match_int);

    Queue* reqs = items;
    while (reqs != NULL) {
        printf("%d\n", *(int*)reqs->data);
        reqs = reqs->next;
    }
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
        MAIN_ERROR("Warning: Excess processes detected. Currently %d processes, but only %d processes are expected. %d processes will be inactive.",
                    size, num_of_processes, size - num_of_processes);
    }

    // Ensure that initialisation Errors/Warning are displayed on top
    MPI_Barrier(MPI_COMM_WORLD);

    process_type = get_process_type(rank);
    switch(process_type) {
    case PROCESS_PURPLE_ALIEN:
        INFO("Process %d becomes a pnurple alien.", rank); break;
    case PROCESS_BLUE_ALIEN:
        INFO("Process %d becomes a blue alien.", rank); break;
    case PROCESS_CLEANER:
        INFO("Process %d becomes a cleaner.", rank); break;
    default:
        INFO("Process %d finishes due to being inactive.", rank);
        MPI_Finalize();
        return EXIT_SUCCESS;
    }
    
    MPI_Barrier(MPI_COMM_WORLD);
    
    // Overwrite size with number of active processes
    size = num_of_processes;

    vector_ts = calloc(size, sizeof(int));
    last_received_scalar_ts = calloc(size, sizeof(int));

    buffer_size = (1 + size) * sizeof(int) + sizeof(PacketData);
    send_buffer = malloc(buffer_size);
    recv_buffer = malloc(buffer_size);

    hotel_info = malloc(sizeof(HotelInfo) * num_of_hotels);
    for (size_t i = 0; i < num_of_hotels; ++i) {
        hotel_info[i].rooms_occupied = 0;
        hotel_info[i].last_process_type = PROCESS_NONE;
    }

    hotel_bookings = malloc(sizeof(int)*size);
    for (size_t i = 0; i < size; ++i) {
        hotel_bookings[i] = -1;
    }

    pthread_t main_thread, listener_thread;

    DEBUG("starting threads");

    if (process_type == PROCESS_BLUE_ALIEN || process_type == PROCESS_PURPLE_ALIEN) {
        pthread_create(&main_thread, NULL, guide_test_loop, NULL);
        pthread_create(&listener_thread, NULL, alien_listener_loop, NULL);
    } else {
        pthread_create(&main_thread, NULL, cleaner_loop, NULL);
        pthread_create(&listener_thread, NULL, cleaner_listener_loop, NULL);
    }


    pthread_join(main_thread, NULL);
    pthread_join(listener_thread, NULL);

    MPI_Barrier(MPI_COMM_WORLD);


    MPI_Finalize();

    free(vector_ts);
    free(last_received_scalar_ts);
    free(send_buffer);
    free(recv_buffer);
    free(hotel_info);
    // free(hotel_requests.requests);
    // free(guide_requests.requests);

    return EXIT_SUCCESS;
}