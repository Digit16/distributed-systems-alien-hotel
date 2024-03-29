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



#define GET_COLOR() (process_type == PROCESS_CLEANER ? "[0;37m" : (process_type == PROCESS_BLUE_ALIEN ? "[0;34m" : (process_type == PROCESS_PURPLE_ALIEN ? "[0;35m" : "[0;31m")))

#define MAIN_ERROR(...) if (rank == 0) ({fprintf(stderr, __VA_ARGS__); printf("\n"); })
#define ERROR(...) ({ printf("%c%sERROR: [%d, %d]: ", 27, GET_COLOR(), scalar_ts, rank); printf(__VA_ARGS__); printf("\n"); })
// #define DEBUG(...) ({ printf("DEBUG: [%d, %d]: ", scalar_ts, rank); printf(__VA_ARGS__); printf("\n"); })
#define INFO(...) ({ printf("%c%sINFO: [%d, %d]: ", 27, GET_COLOR(), scalar_ts, rank); printf(__VA_ARGS__); printf("\n"); })


// #define ERROR(...)
#define DEBUG(...)
// #define INFO(...)



typedef enum Tag {
    TAG_REQ_HOTEL = 1,
    TAG_REQ_GUIDE = 2,
    TAG_ACK_HOTEL = 4,
    TAG_ACK_GUIDE = 8,
    TAG_RELEASE_HOTEL = 16,
    TAG_RELEASE_GUIDE = 32,
    TAG_FINISHED = 64,
} Tag;

typedef enum ProcessType {
    PROCESS_NONE         = 0,
    PROCESS_PURPLE_ALIEN = 1,
    PROCESS_BLUE_ALIEN   = 2,
    PROCESS_CLEANER      = 4,
} ProcessType;


typedef struct PacketData {
    int source;
    int tag;
    int ts;
    ProcessType process_type;
    int resource_idx;
} PacketData;

typedef struct PayloadData {
    int ts;
    int resource_idx;
} PayloadData;


#endif