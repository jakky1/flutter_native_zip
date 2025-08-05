#pragma once

#include <stddef.h> //size_t

typedef enum {
    MY_FLUSH_NO = 0,      // has more data
    MY_FLUSH_BLOCK = 1,   // block boundary reached
    MY_FLUSH_FINISH = 2,  // end of file
} MY_FLUSH_TYPE;

void* _my_zlib_compress_init(int level);
size_t _my_zlib_compress_next(void* stream, char* inBuf, size_t inBufLen, char* outBuf, size_t outBufSize, MY_FLUSH_TYPE flushState);
void _my_zlib_compress_destroy(void* stream);