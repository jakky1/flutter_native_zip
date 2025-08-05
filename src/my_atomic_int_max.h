#pragma once

#include <stddef.h> // size_t
#include "my_thread.h"

typedef struct {
    size_t value;
    size_t max_value;
    thd_mutex mutex;
    thd_condition cond;
    int isInvalid;
} atomic_int_max_t;


void atomic_int_max_init(atomic_int_max_t* ai, size_t init, size_t max_value);
void atomic_int_max_invalid(atomic_int_max_t* ai);
void atomic_int_max_destroy(atomic_int_max_t* ai);
void atomic_int_max_add(atomic_int_max_t* ai, size_t n);
void atomic_int_max_sub(atomic_int_max_t* ai, size_t n);
size_t atomic_int_max_get(atomic_int_max_t* ai);
void atomic_int_max_set(atomic_int_max_t* ai, size_t new_value);