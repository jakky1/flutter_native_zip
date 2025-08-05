#pragma once

#include "my_thread.h"

typedef struct SimpleThreadPool {
    thd_thread *threads;
    int num_threads;
} SimpleThreadPool;


void simple_thread_pool_destroy(SimpleThreadPool* pool);
int simple_thread_pool_create(SimpleThreadPool* pool, int threadCount, void (*func)(void*), void* param);

// --------------------------------------------------------------------------

typedef struct _Job {
    void (*func)(void*);
    void* arg;
    struct _Job* next;
} _Job;

typedef struct ThreadPool {
    thd_thread* threads;
    int num_threads;

    _Job* job_head;
    _Job* job_tail;
    int job_count;
    int max_queue_size;

    thd_mutex lock;
    thd_condition job_available;
    thd_condition job_done;

    int shutdown;
    int working_count;
} ThreadPool;


void thread_pool_destroy(ThreadPool* pool);
ThreadPool* thread_pool_create(int num_threads, int max_queue_size);
int thread_pool_submit(ThreadPool* pool, void (*func)(void*), void* arg);
void thread_pool_wait_all(ThreadPool* pool);
