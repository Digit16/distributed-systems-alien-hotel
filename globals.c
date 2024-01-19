#include <stdbool.h>
#include <pthread.h>

#include "utils.h"
#include "request_queue.h"

// number of processes
int num_of_purple_aliens, num_of_blue_aliens, num_of_cleaners;

// number of resources
int num_of_hotels, hotels_capacity, num_of_guides;

// current state of the process
ProcessType process_type;

// process id and number of processes
int rank;
int size;

int picked_hotel = 0;

// clocks of a current process
int scalar_ts = 0;

// array of last received timestamps for every process
int* last_received_scalar_ts;

// flags for entering critical sections
bool can_enter_hotel_section = false;
bool can_enter_guide_section = false;

