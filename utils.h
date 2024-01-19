#ifndef UTILS_H
#define UTILS_H


#define MAX(a,b) \
    ({ __typeof__ (a) _a = (a); \
        __typeof__ (b) _b = (b); \
        _a > _b ? _a : _b; })

#define MIN(a,b) \
    ({ __typeof__ (a) _a = (a); \
        __typeof__ (b) _b = (b); \
        _a < _b ? _a : _b; })


#define MAIN_ERROR(...) if (rank == 0) ({fprintf(stderr, __VA_ARGS__); printf("\n"); })
#define ERROR(...) ({ printf("ERROR: "); printf(__VA_ARGS__); printf("\n"); })
#define DEBUG(...) ({ printf("DEBUG: "); printf(__VA_ARGS__); printf("\n"); })
#define INFO(...) ({ printf("INFO: "); printf(__VA_ARGS__); printf("\n"); })


typedef enum Tag {
    TAG_REQ_HOTEL = 1,
    TAG_REQ_GUIDE = 2,
    TAG_ACK_HOTEL = 4,
    TAG_ACK_GUIDE = 8,
    TAG_RELEASE_HOTEL = 16,
    TAG_RELEASE_GUIDE = 32,
    TAG_FINISHED = 64,
} Tag;


typedef enum State {
    REST,                // Waiting
    WAIT_HOTEL,          // Requesting Hotel
    INSECTION_HOTEL,     // In section Hotel
    WAIT_GUIDE,          // Requesting Guide
    INSECTION_GUIDE,     // In section Hotel and in section Guide
} State;


typedef enum ProcessType {
    PROCESS_NONE         = 0,
    PROCESS_PURPLE_ALIEN = 1,
    PROCESS_BLUE_ALIEN   = 2,
    PROCESS_CLEANER      = 4,
} ProcessType;

char* process_to_text(ProcessType pt);

typedef struct HotelInfo {
    int rooms_occupied;
    ProcessType last_process_type;
} HotelInfo;


typedef struct PacketData {
    int source;
    int tag;
    int ts;
    ProcessType process_type;
    int resource_idx;
} PacketData;

typedef struct PayloadData {
    int ts;
    int hotel_idx;
} PayloadData;


#endif