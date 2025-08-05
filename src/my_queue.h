#pragma once

#include <stdbool.h>

typedef struct _queue_node {
    void* data;
    struct _queue_node* next;
} _queue_node;

typedef struct Queue {
    _queue_node* front;
    _queue_node* rear;
    int size;
} Queue;


void queue_create(Queue* q);
void queue_destroy(Queue* q, void (*free_data)(void*));
bool queue_is_empty(Queue* q);
int queue_size(Queue* q);
bool queue_push(Queue* q, void* data);
void* queue_pop(Queue* q);
void* queue_peek(Queue* q);
