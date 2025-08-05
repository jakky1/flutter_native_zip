#include <zlib.h>
#include "my_compress.h"

#include <assert.h>
#include <stdlib.h>

void* _my_zlib_compress_init(int level) {
	z_stream *pStream = (z_stream*) malloc(sizeof(z_stream));
	if (pStream == NULL) return NULL;

	pStream->zalloc = NULL;
	pStream->zfree = NULL;
	pStream->opaque = NULL;

	const int memLevel = 8;
	const int windowBits = -MAX_WBITS;
	int err = deflateInit2(pStream, level, Z_DEFLATED, windowBits, memLevel, Z_DEFAULT_STRATEGY);
	if (err != Z_OK) {
		free(pStream);
		return NULL;
	}

	return pStream;
}

size_t _my_zlib_compress_next(void* stream, char *inBuf, size_t inBufLen, char *outBuf, size_t outBufSize, MY_FLUSH_TYPE flushState) {
    // return output size
    // NOTE: we assume the input buffer size is small, and output buffer size is large enough
    //       so all the input buffer will be consumed before exit the function

	z_stream* pStream = (z_stream*) stream;

    int flushMode = Z_NO_FLUSH;
    switch (flushState) {
    case MY_FLUSH_BLOCK:
        flushMode = Z_SYNC_FLUSH;
        break;
    case MY_FLUSH_FINISH:
        flushMode = Z_FINISH;
        break;
    default:
        flushMode = Z_NO_FLUSH;
        break;
    }

    pStream->next_out = (Bytef*) outBuf;
    pStream->avail_out = (uInt) outBufSize;
    pStream->next_in = (Bytef*) inBuf;
    pStream->avail_in = (uInt) inBufLen;

    int err = deflate(pStream, flushMode);
    size_t outputLen = outBufSize - pStream->avail_out;
    switch (err) {
    case Z_OK:
    case Z_STREAM_END:
        assert(pStream->avail_in == 0);
        return outputLen;
    }
    return err;
}

void _my_zlib_compress_destroy(void* stream) {
	z_stream* pStream = (z_stream*) stream;
	deflateEnd(pStream);
	free(pStream);
}