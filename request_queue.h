#ifndef REQUEST_QUEUE_H
#define REQUEST_QUEUE_H

#include <stdbool.h>
#include "utils.h"

typedef struct Queue {
    void* data;
    struct Queue* next;
} Queue;


// typedef struct PacketQueue {
//     PacketData packet;
//     struct PacketQueue* next;
// } PacketQueue;



typedef bool (*Comparison)(void*, void*);

// priority_comp should return true if first argument has higher or equal priority than the second argument
void enqueue(Queue** head_ref, void* data, Comparison priority_comp);

void dequeue(Queue** head_ref);

void dequeue_matching(Queue** head_ref, void* to_find, Comparison match_func);



bool compare_packet(PacketData* rec_a, PacketData* rec_b);

void enqueue_packet(Queue** head_ref, PacketData packet);

bool match_packet_source(int* source, PacketData* packet);


#endif
