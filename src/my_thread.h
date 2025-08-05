#pragma once

#ifdef _WIN32
	#include <windows.h>
	typedef HANDLE thd_thread;
	typedef CRITICAL_SECTION thd_mutex;
	typedef CONDITION_VARIABLE thd_condition;
#else
	#include <pthread.h>
    #include <unistd.h> // sleep()
    #define Sleep(ms) usleep((ms)*1000)
	typedef pthread_t thd_thread;
	typedef pthread_mutex_t thd_mutex;
	typedef pthread_cond_t thd_condition;
#endif

typedef void (*thd_thread_method)(void*);

int thd_thread_create(thd_thread* thread, thd_thread_method method, void* data);
int thd_thread_join(thd_thread* thread);
int thd_thread_detatch(thd_thread* thread);

int thd_mutex_init(thd_mutex* mutex);
int thd_mutex_lock(thd_mutex* mutex);
int thd_mutex_trylock(thd_mutex* mutex);
int thd_mutex_unlock(thd_mutex* mutex);
int thd_mutex_destroy(thd_mutex* mutex);

typedef enum {
	COND_OK,
	COND_TIMEOUT,
	COND_FAIL,
} TimedOpResult;

int thd_condition_init(thd_condition* cond);
int thd_condition_signal(thd_condition* cond);
int thd_condition_signal_all(thd_condition* cond);
int thd_condition_wait(thd_condition* cond, thd_mutex* mutex);
TimedOpResult thd_condition_timedwait(thd_condition* cond, thd_mutex* mutex, size_t timeoutMs);
int thd_condition_destroy(thd_condition* cond);
