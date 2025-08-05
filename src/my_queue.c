#include "my_queue.h"

#include <stdlib.h>
#include <stddef.h> // NULL
#include <stdbool.h> // bool

void queue_create(Queue *q) {
    q->front = q->rear = NULL;
    q->size = 0;
}

void queue_destroy(Queue* q, void (*free_data)(void*)) {
    _queue_node* current = q->front;
    while (current) {
        _queue_node* tmp = current;
        current = current->next;
        if (free_data) free_data(tmp->data);
        free(tmp);
    }
}

bool queue_is_empty(Queue* q) {
    return q->size == 0;
}

int queue_size(Queue* q) {
    return q->size;
}

bool queue_push(Queue* q, void* data) {
    _queue_node* node = (_queue_node*)malloc(sizeof(_queue_node));
    if (!node) return false;
    node->data = data;
    node->next = NULL;

    if (q->rear) {
        q->rear->next = node;
    }
    else {
        q->front = node;
    }
    q->rear = node;
    q->size++;
    return true;
}

void* queue_pop(Queue* q) {
    if (queue_is_empty(q)) return NULL;
    _queue_node* node = q->front;
    void* data = node->data;
    q->front = node->next;
    if (q->front == NULL)
        q->rear = NULL;
    free(node);
    q->size--;
    return data;
}

void* queue_peek(Queue* q) {
    if (queue_is_empty(q)) return NULL;
    return q->front->data;
}