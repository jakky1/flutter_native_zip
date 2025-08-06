/*
BSD 3-Clause License

Copyright 2025, jakky1 (jakky1@gmail.com)
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of Google Inc. nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "native_zip.h"
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

FFI_PLUGIN_EXPORT void* openZipStream(int windowBits, int compressLevel) {
    z_stream* pStream = (z_stream*)malloc(sizeof(z_stream));
    pStream->zalloc = NULL;
    pStream->zfree = NULL;
    pStream->opaque = NULL;

    // TODO: use deflateInit2() instead to set compression level
    //int windowBits = 9;
    const int memLevel = 8;
    //if (deflateInit2(pStream, compressLevel, Z_DEFLATED, windowBits, memLevel, Z_DEFAULT_STRATEGY) != Z_OK) {
    if (deflateInit(pStream, compressLevel) != Z_OK) {
        free(pStream);
        // TODO: report error code to dart ?
        return NULL;
    }

    return pStream;
}

FFI_PLUGIN_EXPORT int writeZipStream(void* _pStream, int isZipping, int8_t* inBuf, int inBufSize, int8_t* outBuf, int outBufSize, int isEOF) {
    // ref: https://zlib.net/zlib_how.html
    // isZipping=1: zip
    // isZipping=0: unzip
    // outBuf: size must be 1024*16 bytes
    // return > 0: output data len
    // return = 0: no more data
    // return < 0: error code
    // NOTE: Dart should free the inBuf returned by 'callback'

    z_stream* pStream = (z_stream*) _pStream;
    if (!isEOF) {
        pStream->next_in = (Bytef*)inBuf;
        pStream->avail_in = inBufSize;
    }
    return writeZipStream_readNext(pStream, isZipping, outBuf, outBufSize, isEOF);
}

FFI_PLUGIN_EXPORT int writeZipStream_readNext(void* _pStream, int isZipping, int8_t* outBuf, int outBufSize, int isEOF) {

    z_stream* pStream = (z_stream*) _pStream;
    pStream->next_out = (Bytef*)outBuf;
    pStream->avail_out = outBufSize;

    int ret;
    if (isZipping) {
        //ret = deflate(pStream, pStream->avail_in ? Z_NO_FLUSH : Z_FINISH); // compress data
        ret = deflate(pStream, isEOF ? Z_FINISH : Z_NO_FLUSH); // compress data
    }
    else {
        //ret = inflate(pStream, Z_NO_FLUSH); // decompress data
        ret = inflate(pStream, isEOF ? Z_FINISH : Z_NO_FLUSH); // decompress data
    }
    switch (ret) {
    case Z_OK: //0
    case Z_STREAM_END: //1, ignore this
        break;
    case Z_NEED_DICT: //2
        return Z_ERRNO;
    default:
        return ret; // error occurs
    }

    if (pStream->avail_out != 0) assert(pStream->avail_in == 0);
    int outputLen = outBufSize - pStream->avail_out;
    return outputLen; // if outputLen==outBufSize, dart should call writeZipStream_readNext() for more data
}

FFI_PLUGIN_EXPORT void closeZipStream(void* pStream) {
    deflateEnd((z_stream*)pStream);
    free(pStream);
}

// --------------------------------------------------------------------------
// unzip stream
// --------------------------------------------------------------------------

FFI_PLUGIN_EXPORT void* openUnzipStream(int windowBits) {
    z_stream* pStream = (z_stream*)malloc(sizeof(z_stream));
    pStream->zalloc = NULL;
    pStream->zfree = NULL;
    pStream->opaque = NULL;

    //if (inflateInit(pStream) != Z_OK) {
    if (inflateInit2(pStream, windowBits) != Z_OK) {
        free(pStream);
        // TODO: report error code to dart ?
        return NULL;
    }

    return pStream;
}

FFI_PLUGIN_EXPORT void closeUnzipStream(void* pStream) {
    inflateEnd((z_stream*)pStream);
    free(pStream);
}
