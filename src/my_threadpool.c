#include "my_threadpool.h"
#include "my_thread.h"

#include <stdlib.h>

void simple_thread_pool_destroy(SimpleThreadPool* pool) {
    for (int i = 0; i < pool->num_threads; i++) {
        thd_thread_join(&pool->threads[i]); // wait for all thread finished
    }
    free(pool->threads);
    pool->threads = NULL;
}

int simple_thread_pool_create(SimpleThreadPool *pool, int threadCount, void (*func)(void*), void *param) {
    pool->num_threads = threadCount;
    pool->threads = (thd_thread*)calloc(threadCount, sizeof(thd_thread));
    if (pool->threads == NULL) return -1;

    for (int i = 0; i < threadCount; i++) {
        int err = thd_thread_create(&pool->threads[i], func, param);
        if (err != 0) {
            pool->num_threads = i;
            simple_thread_pool_destroy(pool);
            return -1;
        }
    }
    return 0;
}

// --------------------------------------------------------------------------

static void thread_func(void *param) {
    ThreadPool* pool = (ThreadPool*)param;
    for (;;) {
        thd_mutex_lock(&pool->lock);
        while (!pool->shutdown && pool->job_head == NULL) {
            thd_condition_wait(&pool->job_available, &pool->lock);
        }
        if (pool->shutdown && pool->job_head == NULL) {
            thd_mutex_unlock(&pool->lock);
            break;
        }
        _Job* job = pool->job_head;
        if (job) {
            pool->job_head = job->next;
            if (!pool->job_head) pool->job_tail = NULL;
            pool->job_count--;
            pool->working_count++;
        }
        thd_mutex_unlock(&pool->lock);

        if (job) {
            job->func(job->arg);
            free(job);

            thd_mutex_lock(&pool->lock);
            pool->working_count--;
            if (pool->job_count == 0 && pool->working_count == 0) {
                thd_condition_signal_all(&pool->job_done);
            }
            thd_mutex_unlock(&pool->lock);
        }
    }
}

void thread_pool_destroy(ThreadPool* pool) {
    if (!pool) return;
    thd_mutex_lock(&pool->lock);
    pool->shutdown = 1;
    thd_condition_signal_all(&pool->job_available);
    thd_mutex_unlock(&pool->lock);

    for (int i = 0; i < pool->num_threads; ++i) {
        thd_thread_join(&pool->threads[i]);
    }
    free(pool->threads);

    _Job* job = pool->job_head;
    while (job) {
        _Job* next = job->next;
        free(job);
        job = next;
    }
    thd_mutex_destroy(&pool->lock);
    free(pool);
}

ThreadPool* thread_pool_create(int num_threads, int max_queue_size) {
    if (num_threads < 1) num_threads = 1;
    ThreadPool* pool = (ThreadPool*)calloc(1, sizeof(ThreadPool));
    if (!pool) return NULL;

    pool->num_threads = num_threads;
    pool->threads = (thd_thread*)calloc(num_threads, sizeof(thd_thread));
    pool->max_queue_size = max_queue_size;
    thd_mutex_init(&pool->lock);
    thd_condition_init(&pool->job_available);
    thd_condition_init(&pool->job_done);

    for (int i = 0; i < num_threads; ++i) {
        int ret = thd_thread_create(&(pool->threads[i]), thread_func, pool);
        if (ret != 0) {
            thread_pool_destroy(pool);
            return NULL;
        }
    }
    return pool;
}

int thread_pool_submit(ThreadPool* pool, void (*func)(void*), void* arg) {
    if (!pool || !func) return 0;
    _Job* job = (_Job*)malloc(sizeof(_Job));
    if (!job) return 0;
    job->func = func;
    job->arg = arg;
    job->next = NULL;

    thd_mutex_lock(&pool->lock);
    if (pool->shutdown || (pool->max_queue_size > 0 && pool->job_count >= pool->max_queue_size)) {
        thd_mutex_unlock(&pool->lock);
        free(job);
        return 0;
    }
    if (pool->job_tail) {
        pool->job_tail->next = job;
    } 
    else {
        pool->job_head = job;
    }
    pool->job_tail = job;
    pool->job_count++;
    thd_condition_signal(&pool->job_available);
    thd_mutex_unlock(&pool->lock);
    return 1;
}

void thread_pool_wait_all(ThreadPool* pool) {
    thd_mutex_lock(&pool->lock);
    while (pool->job_count > 0 || pool->working_count > 0) {
        thd_condition_wait(&pool->job_done, &pool->lock);
    }
    thd_mutex_unlock(&pool->lock);
}
