#include "my_message_queue.h"
#include "my_thread.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>


void mq_init(MessageQueue* mq) {
    mq->head = mq->tail = NULL;
    mq->closed = false;
    thd_mutex_init(&mq->lock);
    thd_condition_init(&mq->not_empty);
}

void mq_close(MessageQueue* mq) {
    thd_mutex_lock(&mq->lock);
    mq->closed = true;
    thd_condition_signal_all(&mq->not_empty);
    thd_mutex_unlock(&mq->lock);
}

typedef void (*_mq_free_func)(void*);
void mq_destroy(MessageQueue* mq, _mq_free_func free_func) {
    mq_close(mq);
    thd_mutex_lock(&mq->lock);
    Message* msg = mq->head;
    while (msg) {
        Message* next = msg->next;
        if (free_func) free_func(msg->data);
        free(msg);
        msg = next;
    }
    mq->head = mq->tail = NULL;
    thd_mutex_unlock(&mq->lock);
    thd_mutex_destroy(&mq->lock);
}

int mq_push(MessageQueue* mq, void* data) {
    int ret = 0;
    Message* msg = (Message*)malloc(sizeof(Message));
    if (!msg) return -1;
    msg->data = data;
    msg->next = NULL;

    thd_mutex_lock(&mq->lock);
    if (mq->closed) {
        ret = -1;
        free(msg);
    }
    else {
        if (mq->tail) {
            mq->tail->next = msg;
        }
        else {
            mq->head = msg;
        }
        mq->tail = msg;
        thd_condition_signal(&mq->not_empty);
    }
    thd_mutex_unlock(&mq->lock);

    return ret;
}

void* mq_pop_timeout(MessageQueue* mq, size_t timeoutMs) {
    // [timeoutMs] < 0 means no timeout
    void *ret = NULL;
    thd_mutex_lock(&mq->lock);
    while (!mq->head && !mq->closed) {
        if (timeoutMs > 0) {
            TimedOpResult ret = thd_condition_timedwait(&mq->not_empty, &mq->lock, timeoutMs);
            if (ret == COND_TIMEOUT) {
                thd_mutex_unlock(&mq->lock);
                return NULL;
            }
        } else {
            thd_condition_wait(&mq->not_empty, &mq->lock);
        }
    }
    if (mq->head) {
        Message* msg = mq->head;
        mq->head = msg->next;
        if (!mq->head)
            mq->tail = NULL;
        ret = msg->data;
        free(msg);
    } else if (mq->closed) {
        ret = NULL;
    }
    thd_mutex_unlock(&mq->lock);
    return ret;
}

void* mq_pop(MessageQueue* mq) {
    return mq_pop_timeout(mq, -1);
}