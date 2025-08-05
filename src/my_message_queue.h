#pragma once

#include "my_thread.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct Message {
    void* data;
    struct Message* next;
} Message;

typedef struct MessageQueue {
    Message* head;
    Message* tail;
    thd_mutex lock;
    thd_condition not_empty;
    bool closed;
} MessageQueue;

typedef void (*_mq_free_func)(void*);


void mq_init(MessageQueue* mq);
void mq_close(MessageQueue* mq);
void mq_destroy(MessageQueue* mq, _mq_free_func free_func);
int mq_push(MessageQueue* mq, void* data);
void* mq_pop(MessageQueue* mq);
void* mq_pop_timeout(MessageQueue* mq, size_t timeoutMs);
