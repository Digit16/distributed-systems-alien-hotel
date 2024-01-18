#include "request_queue.h"
#include <stdlib.h>


void enqueue(Queue** head_ref, void* data, Comparison priority_comp) {
    Queue* new_node = malloc(sizeof(Queue));
    new_node->data = data;
    new_node->next = NULL;

    if (*head_ref == NULL || priority_comp((*head_ref)->data, data)) {
        new_node->next = *head_ref;
        *head_ref = new_node;
    } else {
        Queue* current = *head_ref;
        while (current->next != NULL && !priority_comp(current->next->data, data)) {
            current = current->next;
        }
        new_node->next = current->next;
        current->next = new_node;
    }
}

void dequeue(Queue** head_ref) {
    if (*head_ref == NULL) return;

    Queue* removed_head = *head_ref;
    *head_ref = (*head_ref)->next;
    
    free(removed_head->data);
    free(removed_head);
}

void dequeue_matching(Queue** head_ref, void* to_find, Comparison match_func) {
    Queue* temp = *head_ref;
    Queue* prev;  

    if (temp != NULL && match_func(to_find, temp->data)) {
        *head_ref = temp->next;
        free(temp->data);
        free(temp);
        return;
    }
    while (temp != NULL && !match_func(to_find, temp->data)) {
        prev = temp;
        temp = temp->next;
    }
    if (temp == NULL) return;

    prev->next = temp->next;

    free(temp->data);
    free(temp);
}




bool compare_packet(PacketData* rec_a, PacketData* rec_b) {
    if (rec_a->ts > rec_b->ts)
        return true;
    if (rec_a->ts == rec_b->ts && rec_a->source > rec_b->source)
        return true;
    return false;
}

void enqueue_packet(Queue** head_ref, PacketData packet) {
    PacketData* packet_ref = malloc(sizeof(PacketData));
    *packet_ref = packet;
    enqueue(head_ref, packet_ref, (Comparison)compare_packet);
}

bool match_packet_source(int* source, PacketData* packet) {
    return *source == packet->source;
}
