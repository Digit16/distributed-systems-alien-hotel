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


#define MAIN_ERROR(...) if (rank == 0) fprintf(stderr, __VA_ARGS__)
#define ERROR(...) ({ printf("ERROR: "); printf(__VA_ARGS__); })
#define DEBUG(...) ({ printf("DEBUG: "); printf(__VA_ARGS__); })
#define INFO(...) ({ printf("INFO: "); printf(__VA_ARGS__); })


typedef enum Tag {
    TAG_REQ_HOTEL,
    TAG_REQ_GUIDE,
    TAG_ACK_HOTEL,
    TAG_ACK_GUIDE,
    TAG_RELEASE_HOTEL,
    TAG_RELEASE_GUIDE,
    TAG_FINISHED,
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


typedef struct HotelInfo {
    int rooms_occupied;
    ProcessType last_process_type;
} HotelInfo;


typedef struct PacketData {
    int source;
    int tag;
    int ts;
    ProcessType process_type;
} PacketData;


// typedef struct HotelRecord {
//     int source;                 // process requesting or releasing
//     int ts;                     // received timestamp
//     Tag tag;                    // REQ_HOTEL or RELEASE_HOTEL
//     ProcessType process_type;   // type of the process
// } HotelRecord;

// typedef struct GuideRecord {
//     int source;                 // process requesting or releasing
//     int ts;                     // received timestamp
// } GuideRecord;



#endif