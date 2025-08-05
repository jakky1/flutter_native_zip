#pragma once

#include "my_task_notify.h"
#include <zip.h>

int my_zip_get_error(zip_t* zip);

int my_zip_close(zip_t* zip);
zip_t* __zip_open(const char* path, int flags, int* errorp);
void __zip_discard(zip_t* zip);

#define LOG_ZIP_OPEN_CLOSE
#define zip_open __zip_open
#define zip_discard __zip_discard
