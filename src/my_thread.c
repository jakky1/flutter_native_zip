// reference: https://github.com/pierreguillot/thread/blob/master/src/thd.c

#include "my_thread.h"


#ifdef _WIN32

#include <errhandlingapi.h>

typedef struct ___t_internal_parameters
{
    thd_thread_method i_method;
    void* i_data;
} __t_internal_parameters;

static DWORD WINAPI __internal_method_ptr(LPVOID arg)
{
    __t_internal_parameters* params = (__t_internal_parameters*)arg;
    params->i_method(params->i_data);
    free(params);
    return 0;
}

int thd_thread_create(thd_thread* thread, thd_thread_method method, void* data)
{
    __t_internal_parameters* params = (__t_internal_parameters*)malloc(sizeof(__t_internal_parameters));
    if (params)
    {
        params->i_method = method;
        params->i_data = data;
        *thread = CreateThread(NULL, 0, __internal_method_ptr, params, 0, NULL);
        if (*thread == NULL)
        {
            free(params);
            return -1;
        }
        return 0;
    }
    return -1;
}

int thd_thread_join(thd_thread* thread)
{
    if (WaitForSingleObject(*thread, INFINITE) != WAIT_FAILED)
    {
        if (CloseHandle(*thread))
        {
            return 0;
        }
    }
    return 1;
}

int thd_thread_detatch(thd_thread* thread)
{
    return CloseHandle(*thread) ? 0 : 1;
}

int thd_mutex_init(thd_mutex* mutex)
{
    InitializeCriticalSection(mutex);
    return 0;
}

int thd_mutex_lock(thd_mutex* mutex)
{
    EnterCriticalSection(mutex);
    return 0;
}

int thd_mutex_trylock(thd_mutex* mutex)
{
    return TryEnterCriticalSection(mutex) ? 0 : -1;
}

int thd_mutex_unlock(thd_mutex* mutex)
{
    LeaveCriticalSection(mutex);
    return 0;
}

int thd_mutex_destroy(thd_mutex* mutex)
{
    DeleteCriticalSection(mutex);
    return 0;
}

int thd_condition_init(thd_condition* cond)
{
    InitializeConditionVariable(cond);
    return 0;
}

int thd_condition_signal(thd_condition* cond)
{
    WakeConditionVariable(cond);
    return 0;
}

int thd_condition_signal_all(thd_condition* cond)
{
    WakeAllConditionVariable(cond);
    return 0;
}

int thd_condition_wait(thd_condition* cond, thd_mutex* mutex)
{
    return SleepConditionVariableCS(cond, mutex, INFINITE) ? 0 : -1;
}

TimedOpResult thd_condition_timedwait(thd_condition* cond, thd_mutex* mutex, size_t timeoutMs)
{   // SleepConditionVariableCS() return 0: timeout or failed
    // return 0: success, others: fail or timeout
    int ret = SleepConditionVariableCS(cond, mutex, (DWORD) timeoutMs);

    if (ret != 0) return COND_OK;
    else if (GetLastError() == ERROR_TIMEOUT) return COND_TIMEOUT;
    else return COND_FAIL;
}

int thd_condition_destroy(thd_condition* cond)
{
    return 0;
}

#else

#include <errno.h>

int thd_thread_create(thd_thread* thread, thd_thread_method method, void* data)
{
    return pthread_create(thread, 0, (void*)method, data);
}

int thd_thread_join(thd_thread* thread)
{
    return pthread_join(*thread, NULL);
}

int thd_thread_detatch(thd_thread* thread)
{
    return pthread_detach(*thread);
}

int thd_mutex_init(thd_mutex* mutex)
{
    return pthread_mutex_init(mutex, NULL);
}

int thd_mutex_lock(thd_mutex* mutex)
{
    return pthread_mutex_lock(mutex);
}

int thd_mutex_trylock(thd_mutex* mutex)
{
    return pthread_mutex_trylock(mutex);
}

int thd_mutex_unlock(thd_mutex* mutex)
{
    return pthread_mutex_unlock(mutex);
}

int thd_mutex_destroy(thd_mutex* mutex)
{
    return pthread_mutex_destroy(mutex);
}

int thd_condition_init(thd_condition* cond)
{
    return pthread_cond_init(cond, NULL);
}

int thd_condition_signal(thd_condition* cond)
{
    return pthread_cond_signal(cond);
}

int thd_condition_signal_all(thd_condition* cond)
{
    return pthread_cond_broadcast(cond);
}

int thd_condition_wait(thd_condition* cond, thd_mutex* mutex)
{
    return pthread_cond_wait(cond, mutex);
}

TimedOpResult thd_condition_timedwait(thd_condition* cond, thd_mutex* mutex, size_t timeoutMs)
{   // return 0: success, others: fail or timeout
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += timeoutMs / 1000;
    timeout.tv_nsec += (timeoutMs % 1000) * 1000000;
    
    if (timeout.tv_nsec >= 1000000000) {
        timeout.tv_sec += 1;
        timeout.tv_nsec -= 1000000000;
    }    
    int ret = pthread_cond_timedwait(cond, mutex, &timeout);    
    
    if (ret == 0) return COND_OK;
    else if (ret == ETIMEDOUT) return COND_TIMEOUT;
    else return COND_FAIL;
}

int thd_condition_destroy(thd_condition* cond)
{
    return pthread_cond_destroy(cond);
}

#endif