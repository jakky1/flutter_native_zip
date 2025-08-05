#include "my_atomic_int_max.h"

void atomic_int_max_init(atomic_int_max_t* ai, size_t init, size_t max_value) {
    ai->isInvalid = 0;
    ai->value = init;
    ai->max_value = max_value;
    thd_mutex_init(&ai->mutex);
    thd_condition_init(&ai->cond);
}

void atomic_int_max_invalid(atomic_int_max_t* ai) {
    ai->isInvalid = 1;
    thd_condition_signal_all(&ai->cond);
}

void atomic_int_max_destroy(atomic_int_max_t* ai) {
    thd_mutex_destroy(&ai->mutex);
    thd_condition_destroy(&ai->cond);
}

void atomic_int_max_add(atomic_int_max_t* ai, size_t n) {
    thd_mutex_lock(&ai->mutex);
    while (!ai->isInvalid && ai->value + n > ai->max_value) {
        thd_condition_wait(&ai->cond, &ai->mutex);
    }
    ai->value += n;
    thd_mutex_unlock(&ai->mutex);
}

void atomic_int_max_sub(atomic_int_max_t* ai, size_t n) {
    thd_mutex_lock(&ai->mutex);
    ai->value -= n;
    thd_condition_signal_all(&ai->cond);
    thd_mutex_unlock(&ai->mutex);
}

size_t atomic_int_max_get(atomic_int_max_t* ai) {
    thd_mutex_lock(&ai->mutex);
    size_t val = ai->value;
    thd_mutex_unlock(&ai->mutex);
    return val;
}

void atomic_int_max_set(atomic_int_max_t* ai, size_t new_value) {
    thd_mutex_lock(&ai->mutex);
    ai->value = new_value;
    thd_condition_signal_all(&ai->cond);
    thd_mutex_unlock(&ai->mutex);
}