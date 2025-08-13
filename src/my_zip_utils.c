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

#include "my_zip_utils.h"
#include "native_zip.h"

#include "my_zip.h"
#include "my_file.h"
#include "my_thread.h"
#include "my_threadpool.h"
#include "my_atomic_int_max.h"
#include "my_message_queue.h"
#include "my_compress.h"
#include "my_common.h"

#include <zip.h>
#include <zlib.h>
#include <zconf.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>

#define min(a,b) (((a) < (b)) ? (a) : (b))

bool _my_zip_is_malicious_path(const char* path) {
    if (path[0] == '/' || strstr(path, "../")) return true; // malicious path, exit
    return false;
}

struct _my_zip_callback_data;
typedef struct _my_zip_block { // each file is divided into several blocks, and compressed by thread  
    struct _my_zip_block* nextBlock; // next block of this file
    _my_zip_task* task;
    struct _my_zip_callback_data* cbData;
    size_t fileSize;

    size_t blockSize;   // size of this block, default size is '_my_zip_task->defaultBlockSize'
    size_t blockOffset; // bytes offset in the file
    uLong crc; // crc of the original (uncompressed) data in the block
    char* compressedData; // data compressd by threads
    size_t compressedDataSize;
    bool isCompressDone; // is this block is totally compressed into 'compressedData'
} _my_zip_block;

typedef struct _my_zip_callback_data { // userdata of zip_source callback function
    _my_zip_task* task;
    _my_zip_block* nowBlock; // the first block of the file that not written into zip yet
    char* filePath;
    time_t mtime; // modified time
    size_t fileSize; // file size of current file
    size_t bufOffset; // in 'nowBlock->compressedData', bytes written into zip
    size_t compressedFileSize; // compressed file size
    uLong crc; // crc of the original (uncompressed) file content
    bool isEOF; // is all compressed data written into zip
} _my_zip_callback_data;

void _my_zip_block_free(_my_zip_block* block, bool toFreeAllNextBlocks) {
    if (!block) return;
    if (block->compressedData) {
        free(block->compressedData);
        block->compressedData = NULL;
        atomic_int_max_sub(&block->task->allocatedBlocksTracker, 1);
    }
    if (toFreeAllNextBlocks && block->nextBlock) {
        _my_zip_block_free(block->nextBlock, true);
        block->nextBlock = NULL;
    }
    free(block);
}

void _my_zip_callback_data_free(_my_zip_callback_data* data) {
    _my_zip_block_free(data->nowBlock, true);
    free(data->filePath);
    free(data);
}

/// compress file block by thread
int _zip_thread_compress_block(_my_zip_task *task, _my_zip_block *block) {
    if (task->isCancelled) return 0;
    block->crc = 0;
    block->isCompressDone = false;

    FILE* fp = NULL;
    _my_file_fopen(&fp, block->cbData->filePath, "rb");
    if (!fp) {
        task->isCancelled = true;
        return ZIP_ER_READ;
    }

    void* pStream = _my_zlib_compress_init(task->compressLevel);
    block->compressedData = (char*)malloc(compressBound((uLong)block->blockSize));
    atomic_int_max_add(&task->allocatedBlocksTracker, 1);

    MY_FLUSH_TYPE flushType = MY_FLUSH_NO;
    bool isEndOfFile = block->nextBlock == NULL;

    char inBuf[1024 * 16];
    size_t totalReadLen = 0;
    size_t totalOutputLen = 0;
    int err = fseek(fp, (long) block->blockOffset, SEEK_SET);
    while (err == 0) {
        if (task->isCancelled) break;
        
        size_t count = min(sizeof(inBuf), block->blockSize - totalReadLen);
        size_t readLen = fread(inBuf, 1, count, fp);
        if (readLen < 0) {
            err = ZIP_ER_READ;
            break; // fread() error
        }
        else if (readLen == 0) {            
            //printf("block %X size: %d, crc: %X\n", block, totalReadLen, block->crc);
            break; // EOF for this block
        }

        totalReadLen += readLen;
        int isEndOfBlock = totalReadLen == block->blockSize;           
        if (isEndOfBlock) flushType = isEndOfFile ? MY_FLUSH_FINISH : MY_FLUSH_BLOCK;

        block->crc = crc32(block->crc, (Bytef*) inBuf, (uInt)readLen);

        const size_t len = INT_MAX; // we assume `block->compressedData` is big enough
        size_t outputLen = _my_zlib_compress_next(pStream, inBuf, readLen, block->compressedData + totalOutputLen, len, flushType);
        if (outputLen < 0) {
            err = ZIP_ER_COMPRESSED_DATA;
            task->isCancelled = true;
            break;
        }
        totalOutputLen += outputLen;
    }    

    block->compressedDataSize = totalOutputLen;
    block->isCompressDone = true;
    fclose(fp);
    _my_zlib_compress_destroy(pStream);
    if (err != 0) {
        task->isCancelled = true;
    }
    return err;
}

_my_zip_block* _zip_thread_get_next_block(_my_zip_task *task) {
    if (task->isCancelled) return NULL;
    thd_mutex_lock(&task->mq_blocksMutex);

    _my_zip_block* block = (_my_zip_block*)mq_pop(&task->mq_blocks);
    if (block) {
        atomic_int_max_add(&task->nowMemoryUsage, block->blockSize);
        //printf("+++ add memory : %d\n", (int)atomic_int_max_get(&task->nowMemoryUsage));
    }

    thd_mutex_unlock(&task->mq_blocksMutex);
    return block;
}

void _zip_thread_compress_block_proc(void* param) {
    _my_zip_task* task = (_my_zip_task*)param;

    while (1) {
        _my_zip_block* block = _zip_thread_get_next_block(task);
        if (task->isCancelled) return;
        if (block == NULL) return; // no more blocks, exit
        if (task->isCancelled) return;
        int err = _zip_thread_compress_block(task, block);
        if (err) {
            if (!task->errCode) task->errCode = err;
            return;
        }
    }
}

zip_int64_t _my_zip_source_callback(void* userdata, void* data, zip_uint64_t len, zip_source_cmd_t cmd) {
    _my_zip_callback_data* ud = (_my_zip_callback_data*)userdata;

    switch (cmd) {
    case ZIP_SOURCE_OPEN:
        if (ud->task->isCancelled) return -1;
        while (ud->nowBlock->isCompressDone == false) {
            if (ud->task->isCancelled) return -1;
            Sleep(30); // TODO: performance...
        }
        ud->task->progress.now_processing_filePath = ud->filePath;
        ud->isEOF = 0;
        ud->bufOffset = 0;
        ud->crc = ud->nowBlock->crc;
        //printf("source open: first block crc : %X\n", ud->crc);
        return 0;

    case ZIP_SOURCE_READ: {
        if (ud->task->isCancelled) return -1;
        if (ud->isEOF) return 0;
        size_t outputLen = 0;
        while (len > 0) {
            zip_uint64_t compressedBufLeftLen = ud->nowBlock->compressedDataSize - ud->bufOffset;
            if (compressedBufLeftLen == 0) {
                _my_zip_block *oldBlock = ud->nowBlock;
                ud->nowBlock = oldBlock->nextBlock;
                
                // update processing info
                ud->task->progress.processed_fileSize += oldBlock->blockSize;
                ud->task->progress.processed_compressSize += ud->bufOffset;

                atomic_int_max_sub(&oldBlock->task->nowMemoryUsage, oldBlock->blockSize); // update 'nowMemoryUsage', and wake-up thread that waiting for memory usage decrease
                //printf("--- free memory : %d\n", (int) atomic_int_max_get(&oldBlock->task->nowMemoryUsage));
                _my_zip_block_free(oldBlock, false);

                ud->bufOffset = 0;
                if (ud->nowBlock == NULL) {
                    ud->isEOF = 1;
                    break; // no more data
                }

                while (ud->nowBlock->isCompressDone == false) {
                    if (ud->task->isCancelled) return -1;
                    Sleep(30); // TODO: performance...
                }
                ud->crc = crc32_combine(ud->crc, ud->nowBlock->crc, (long) ud->nowBlock->blockSize);
                continue;
            }

            if (ud->task->isCancelled) return -1;
            zip_uint64_t count = min(len, compressedBufLeftLen);
            memcpy((char*)data + outputLen, ud->nowBlock->compressedData + ud->bufOffset, count);
            len -= count;
            ud->bufOffset += count;
            outputLen += count;
        }

        ud->compressedFileSize += outputLen;
        return outputLen;
    }

    case ZIP_SOURCE_STAT: {
        if (ud->task->isCancelled) return -1;
        zip_stat_t* st = (zip_stat_t*)data;
        if (ud->isEOF) {
            st->comp_size = ud->compressedFileSize;
            st->size = ud->fileSize;
            st->crc = ud->crc;
            st->valid |= ZIP_STAT_COMP_SIZE | ZIP_STAT_CRC | ZIP_STAT_SIZE;
        }
        st->comp_method = ZIP_CM_DEFLATE;
        st->mtime = ud->mtime;
        st->valid |= ZIP_STAT_COMP_METHOD | ZIP_STAT_MTIME;
        return 0;
    }

    case ZIP_SOURCE_CLOSE:
        return 0;

    case ZIP_SOURCE_FREE:
        ud->task->progress.now_processing_filePath = (char*)"";
        queue_pop(&ud->task->queue_cb_data);
        _my_zip_callback_data_free(ud);
        return 0;

    case ZIP_SOURCE_ERROR: {
        int* err_data = (int*)data;
        err_data[0] = ZIP_ER_NOENT; // libzip error code
        err_data[1] = ZIP_ER_NOENT; // system error code
        return sizeof(int) * 2;
    }

    case ZIP_SOURCE_SUPPORTS:
        return ZIP_SOURCE_SUPPORTS_READABLE;

    default:
        // unsupport command
        return -1;
    }
}

int _zipDir_traversal_onFileFound(const char* filePath, const char* relativePath, NATIVE_FILE_STAT* st, void* param) {
    _my_zip_task *task = (_my_zip_task*) param;
    zip_t *zip = task->zip;
    if (task->isCancelled) return ERR_NZ_CANCELLED;

    if (S_ISDIR(st->st_mode)) {
        zip_int64_t index = zip_dir_add(zip, relativePath, ZIP_FL_ENC_UTF_8);
        if (index < 0) return ERR_NZ_ZIP_ENTRY_ALREADY_EXISTS;
        zip_file_set_mtime(zip, index, st->st_mtime, 0); // set last modified time
        return 0;
    }

    size_t fileSize = st->st_size;
    _my_zip_callback_data* ud = (_my_zip_callback_data*)calloc(1, sizeof(_my_zip_callback_data));
    ud->task = task;
    ud->filePath = strdup(filePath);
    ud->fileSize = fileSize;
    ud->mtime = st->st_mtime;

    // divide each file to multiple blocks
    // push fileA:block1, fileA:block2, fileA:block3, ..., fileB:block1, fileB:block2, ...., fileC:block1, ...
    // and compress all the blocks by thread
    _my_zip_block* first_block = NULL;
    _my_zip_block* prev_block = NULL;
    for (size_t offset = 0; offset < fileSize; offset += task->maxBlockSize) {
        _my_zip_block* block = (_my_zip_block*)calloc(1, sizeof(_my_zip_block));
        size_t blockSize = min(task->maxBlockSize, fileSize - offset);
        block->task = task;
        block->cbData = ud;
        block->fileSize = fileSize;
        block->blockOffset = offset;
        block->blockSize = blockSize;
        block->compressedData = NULL;
        block->isCompressDone = false;
        block->crc = 0;
        mq_push(&task->mq_blocks, block);

        if (prev_block == NULL) first_block = block;
        else prev_block->nextBlock = block;
        prev_block = block;
    }
    ud->nowBlock = first_block;
    task->progress.total_fileSize += fileSize;

    // 'filePath' and 'relativePath' must be utf-8 string
    zip_source_t* source = NULL;
    if (fileSize == 0) {
        source = zip_source_file(zip, filePath, 0, 0);
        free(ud->filePath);
        free(ud);
    }
    else {
        // pass the first block of each file into callback
        source = zip_source_function(zip, _my_zip_source_callback, ud);
        queue_push(&task->queue_cb_data, ud);
    }
    if (source == NULL) {
        //free(ud);
        return ZIP_ER_WRITE;
    }

    zip_int64_t index = zip_file_add(zip, relativePath, source, ZIP_FL_OVERWRITE);
    if (index < 0) {
        //free(ud);
        zip_source_free(source);
        return ERR_NZ_ZIP_ENTRY_ALREADY_EXISTS;
    }

    if (task->hasPassword) {
        int err = zip_file_set_encryption(zip, index, ZIP_EM_AES_256, NULL); // use default5 password set into zip_set_default_password()
        if (err) {
            err = my_zip_get_error(zip);
            task->errCode = err;
            task->isCancelled = true;
        }
    }

    //ud->entryIndex = index;

    //printf("file added: %s\n", filePath);
    return 0;
}


// NOTE: zipDir()will call zip_close() in the end
int zipDir(_my_zip_task *task, void *_zip, const char** dirPathList, int dirPathListCount, const char *entryDirPathBase, bool skipTopLevel, int threadCount) {
    // [entryDirPathBase] must be "", or ends with '/', and cannot starts with '/', it must be a directory path

    zip_t* zip = (zip_t*)_zip;
    const int maxBlockSize = 1024 * 1024 * 8;
    const size_t maxMemoryUsage = 1024 * 1024 * 128;

    if (dirPathListCount < 1) return ERR_NZ_INVALID_ARGUMENT;
    if (threadCount < 1) return ERR_NZ_INVALID_ARGUMENT;
    if (_my_zip_is_malicious_path(entryDirPathBase)) return ERR_NZ_INVALID_PATH; // malicious path, exit
    if (entryDirPathBase[0] != '\0' && (entryDirPathBase[0] == '/' || entryDirPathBase[strlen(entryDirPathBase) - 1] != '/')) {
        // [entryDirPathBase] must be "", or ends with '/', and cannot starts with '/'
        return ERR_NZ_INVALID_PATH;
    }


    // NOTE: we divide each file to blocks (max size is 'maxBlockSize'), and compress these blocks in threads, then write into zip file

    int err = 0;
    //memset(task, 0, sizeof(_my_zip_task));
    task->zip = zip;
    task->isCancelled = false;
    task->maxBlockSize = maxBlockSize;
    task->entryDirPathBase = entryDirPathBase;
    task->progress.now_processing_filePath = (char*)"";
    mq_init(&task->mq_blocks);
    queue_create(&task->queue_cb_data);

    for (int i = 0; i < dirPathListCount; i++) {
        const char* path = dirPathList[i];
        if (*path == '\0' || path[strlen(path) - 1] == DIR_SEPARATOR) return ERR_NZ_INVALID_PATH;
        err = my_dir_traversal(path, entryDirPathBase, skipTopLevel, _zipDir_traversal_onFileFound, task);
        if (err != 0 || task->isCancelled) {
            // cleanup
            mq_destroy(&task->mq_blocks, free);
            queue_destroy(&task->queue_cb_data, (void (*)(void*))_my_zip_callback_data_free);
            return err;
        }
    }
    for (int i = 0; i <= threadCount; i++) mq_push(&task->mq_blocks, NULL); // notify threads that no more blocks

    //_zip_thread_compress_block
    SimpleThreadPool pool;
    thd_mutex_init(&task->mq_blocksMutex);
    atomic_int_max_init(&task->nowMemoryUsage, 0, maxMemoryUsage);
    atomic_int_max_init(&task->allocatedBlocksTracker, 0, maxMemoryUsage);
    simple_thread_pool_create(&pool, threadCount, _zip_thread_compress_block_proc, task);
    
    // libzip write all changes into .zip file
    err = zip_close(zip); // NOTE: don't use my_zip_close() here, and don't call zip_discard() immediately
    task->isZipClosed = true;
    if (err) task->isCancelled = true;
    //printf("zip error: %s", zip_error_strerror(zip_get_error(zip)));

    atomic_int_max_invalid(&task->nowMemoryUsage); // wake-up all threads if thread is waiting for 'nowMemoryUsage' value down
    simple_thread_pool_destroy(&pool); // wait for all thread finish
    
    if (err) {
        // NOTE:
        //   if zip_close() failed, call zip_discard() after all thread finished,
        //   because zip_discard() cause all zip_source freed, 
        //   which may cause 'case ZIP_SOURCE_FREE:' in '_my_zip_source_callback' called, 
        //   which will free all '_my_zip_block', and app crash if thread access them
        err = my_zip_get_error(zip);

        zip_discard(zip);
    }

    // NOTE: if user cancelled or error occurs during saving zip file, that is to say, during zip_close(),
    //       libzip won't call ZIP_SOURCE_FREE for all remain 'zip_source_function' in this case,
    //       so we cleanup all '_my_zip_callback_data' here
    queue_destroy(&task->queue_cb_data, (void (*)(void*))_my_zip_callback_data_free);

    mq_destroy(&task->mq_blocks, NULL); // all blocks already freed by _my_zip_callback_data_free above
    thd_mutex_destroy(&task->mq_blocksMutex);
    
    if (atomic_int_max_get(&task->nowMemoryUsage) != 0
        || atomic_int_max_get(&task->allocatedBlocksTracker) != 0) {
        printf("################# zipDir() memory leak detected #################\n");
        notifyDartLog("################# zipDir() memory leak detected  #################");
    }

    atomic_int_max_destroy(&task->nowMemoryUsage);
    atomic_int_max_destroy(&task->allocatedBlocksTracker);

    return err;
}


// --------------------------------------------------------------------------
// unzipDir
// --------------------------------------------------------------------------

typedef struct _my_unzip_file_info {
    zip_int64_t index;
    size_t basePathLen;
} _my_unzip_file_info;

int _unzipToDir_unzipEntry(_my_unzip_task* task, zip_t* zip, _my_unzip_file_info* info) {
    struct zip_stat st;
    int err = 0;

    err = zip_stat_index(zip, info->index, 0, &st);
    if (err != 0) return ERR_NZ_ZIP_ENTRY_NOT_FOUND;

    if (_my_zip_is_malicious_path(st.name)) return ERR_NZ_ZIP_HAS_MALICIOUS_PATH; // malicious path, exit

    char newFilePath[MAX_PATH_CHAR_COUNT];
    snprintf(newFilePath, sizeof(newFilePath), "%s%c%s", task->dirPath, DIR_SEPARATOR, st.name + info->basePathLen);
    _my_file_path_separator_fix(newFilePath);
    task->progress.now_processing_filePath = newFilePath;

    // NOTE: a directory relativePath in libzip always ends with '/' or '\\' (by OS)
    //       if not, it is a file
    if (st.name[strlen(st.name) - 1] == ZIP_PATH_SEPARATOR) {
        // is a directory
        _my_dir_mkdirs(newFilePath);
        if (st.valid & ZIP_STAT_MTIME) {
            //my_file_set_lastWriteTime(newFilePath, true, st.mtime);
        }
        return 0;
    }

    if (task->isCancelled) return 0;
    zip_file_t* zf = zip_fopen_index(zip, info->index, 0);
    if (!zf) return ZIP_ER_OPEN;

    FILE* fout;
    _my_file_fopen(&fout, newFilePath, "wb");
    if (!fout) {
        // NOTE: zip file format allow a file entry path like "a/b/c.txt"
        //       without directory entry "a" and "a/b"
        //       so when fopen() fails, call mkdirs() and try again
        char* p = strrchr(newFilePath, DIR_SEPARATOR);
        if (p != NULL) {
            p++; // make path ends with separator
            char ch = *p;
            *p = 0;
            err = _my_dir_mkdirs(newFilePath);
            if (err != 0) {
                err = -1;
            }
            *p = ch;
        }

        _my_file_fopen(&fout, newFilePath, "wb");
        if (!fout) {
            zip_fclose(zf);
            return ZIP_ER_WRITE;
        }
    }

    // write file
    char buf[1024 * 16];
    zip_uint64_t sum = 0;
    while (sum < st.size) {
        if (task->isCancelled) break;
        zip_int64_t len = zip_fread(zf, buf, sizeof(buf));
        if (len < 0) {
            err = ZIP_ER_READ;
            break;
        }
        fwrite(buf, 1, len, fout);
        sum += len;
    }

    // cleanup
    fclose(fout);
    zip_fclose(zf);
    if (st.valid & ZIP_STAT_MTIME) my_file_set_lastWriteTime(newFilePath, false, st.mtime);


    // update process info
    thd_mutex_lock(&task->progress_mutex);
    if (task->progress.now_processing_filePath == newFilePath) {
        task->progress.now_processing_filePath = (char*)"";
    }
    if (st.valid & ZIP_STAT_SIZE) task->progress.processed_fileSize += st.size;
    if (st.valid & ZIP_STAT_COMP_SIZE) task->progress.processed_compressSize += st.comp_size;
    thd_mutex_unlock(&task->progress_mutex);

    return err;
}

int _unzipToDir_consume_queue(_my_unzip_task* task, zip_t *zip) {
    int err = 0;
    _my_unzip_file_info* info = NULL;
    while (1) {
        FREEIF(info);
        info = (_my_unzip_file_info*)mq_pop(&task->mq);
        if (info == NULL) break; // end of queue

        if (task->isCancelled) break;
        err = _unzipToDir_unzipEntry(task, zip, info);
        if (err != 0) { 
            task->errCode = err;
            task->isCancelled = true;
            break;
        }
    }
    FREEIF(info);
    return err;
}

void _unzipToDir_copy_thread(void* _task) {
    _my_unzip_task* task = (_my_unzip_task*)_task;

    int err;
    zip_t* zip = zip_open(task->zipFilePath, ZIP_RDONLY, &err);
    if (!zip) {
        task->errCode = ZIP_ER_OPEN;
        task->isCancelled = true;
        return;
    }
    
    if (task->password) {
        err = zip_set_default_password(zip, task->password);
        if (err) {
            task->errCode = ZIP_ER_WRONGPASSWD;
            task->isCancelled = true;
            my_zip_close(zip);
            return;
        }
    }

    err = _unzipToDir_consume_queue(task, zip);
    if (err) {
        task->errCode = err;
    }
    my_zip_close(zip);
}

int _unzipToDir_dirs_set_mtime(_my_unzip_task* task, zip_t* zip) {
    int err = 0;
    char newFilePath[MAX_PATH_CHAR_COUNT];

    struct zip_stat st;
    zip_int64_t cnt = zip_get_num_entries(zip, 0);
    for (zip_int64_t i = 0; i < cnt; i++) {
        err = zip_stat_index(zip, i, 0, &st);
        if (err != 0) continue; //return ERR_NZ_INTERNAL_ERROR;
        if (st.name[strlen(st.name) - 1] != ZIP_PATH_SEPARATOR) {
            continue; // if not a directory
        }
        //if ((st.valid & ZIP_STAT_MTIME) == 0) continue;

        snprintf(newFilePath, sizeof(newFilePath), "%s%c%s", task->dirPath, DIR_SEPARATOR, st.name);

        // TODO: not all directory has corresponding entry in .zip ...
        my_file_set_lastWriteTime(newFilePath, true, st.mtime);
    }
    return 0;
}

const char* _unzipDir_find_path_last_separator(const char *path) {
    const char *lastSeparator = NULL;
    for (; *path != '\0'; path++) {
        if (*path == ZIP_PATH_SEPARATOR) {
            if (path[1] == '\0') break;
            lastSeparator = path;
        }
    }
    return lastSeparator;
}

int _unzipDir_get_base_directory_len_from_path(const char *path) {
    // if path is "a/bc/d.txt", then return length of "a/bc/"
    const char *p = _unzipDir_find_path_last_separator(path);
    if (p) return (int)(p - path - 1);
    return 0;
}

const bool _unzipDir_path_is_direactory(const char *path) {
    return path[strlen(path) - 1] == ZIP_PATH_SEPARATOR;
}

void _unzipDir_add_file_into_queue(_my_unzip_task* task, zip_int64_t index, size_t basePathLen) {
    _my_unzip_file_info* info = (_my_unzip_file_info*)malloc(sizeof(_my_unzip_file_info));
    info->index = index;
    info->basePathLen = basePathLen;
    mq_push(&task->mq, (void*)info);
}

int unzipToDir(_my_unzip_task *task, void* _zip, const char *zipFilePath, char **entryPathsArr, int entriesCount, const char *toDirPath, int threadCount) {
    // [entryPathsArr] : all the entry paths must be in the same directory !
    // [zipFilePath] : must be the path of [_zip], used to open zip file in each thread
    // return '_my_unzip_task' object
    if (threadCount < 1) threadCount = 1;
    if (entriesCount < 1) return ERR_NZ_INVALID_ARGUMENT;

    zip_t* zip = (zip_t*)_zip;
    task->zip = zip;
    task->isCancelled = false;
    task->progress.now_processing_filePath = (char*)"";

    /*
    // check if all the entry path are in the same directory
    char *firstEntryPath = NULL;
    int baseDirectoryPathLen = 0;
    for (int i=0; i<entriesCount; i++) {
        char *entryPath = entryPathsArr[i];
        if (_my_zip_is_malicious_path(entryPath)) return ERR_NZ_INVALID_PATH; // malicious path, exit

        if (i==0) {
            firstEntryPath = entryPath;
            baseDirectoryPathLen = _unzipDir_get_base_directory_len_from_path(entryPath);
        } else {
            // check if all the entry path are in the same directory
            if (_unzipDir_get_base_directory_len_from_path(entryPath) != baseDirectoryPathLen) return ERR_NZ_INVALID_PATH;
            if (strncmp(firstEntryPath, entryPath, baseDirectoryPathLen) != 0) return ERR_NZ_INVALID_PATH;
        }
    }
    // TODO: check if there are two identical paths ?
    */


    // add all entry index that need to be copied into message queue
    mq_init(&task->mq); // TODO: call mq_destroy() before return in some cases...
    struct zip_stat st;
    zip_stat_init(&st);
    for (int k=0; k<entriesCount; k++) {
        char *entryPath = entryPathsArr[k];
        bool isDir = _unzipDir_path_is_direactory(entryPath);

        if (!isDir && entryPath[0] != '\0') { // entryPath is a file
            zip_int64_t entryIndex = zip_name_locate(zip, entryPath, ZIP_FL_ENC_UTF_8);
            if (entryIndex < 0) return ERR_NZ_ZIP_ENTRY_NOT_FOUND;
            
            size_t entryPathLen = 0;
            char* lastSeparator = strrchr(entryPath, '/');
            if (lastSeparator) entryPathLen = lastSeparator - entryPath + 1;
            else entryPathLen = 0;
            _unzipDir_add_file_into_queue(task, entryIndex, entryPathLen);
            continue;
        }

        size_t entryPathLen = strlen(entryPath);
        bool isFound = false;
        zip_int64_t cnt = zip_get_num_entries(zip, 0);
        for (zip_int64_t i = 0; i < cnt; i++) {
            if (zip_stat_index(zip, i, 0, &st) != 0) return ERR_NZ_ZIP_ENTRY_NOT_FOUND;
            if (k==0 && _my_zip_is_malicious_path(st.name)) return ERR_NZ_ZIP_HAS_MALICIOUS_PATH; // malicious path, exit
            if (strncmp(st.name, entryPath, entryPathLen) != 0) continue;

            if (st.valid & ZIP_STAT_SIZE) task->progress.total_fileSize += st.size;
            _unzipDir_add_file_into_queue(task, st.index, entryPathLen);
            isFound = true;
        }
        if (!isFound) return ERR_NZ_ZIP_ENTRY_NOT_FOUND;
    }


    task->zipFilePath = zipFilePath;
    task->dirPath = toDirPath;

    // start to copy files in threads
    thd_mutex_init(&task->progress_mutex);
    int err = 0;
    do {
        if (task->isCancelled) break;
        err = simple_thread_pool_create(&task->pool, threadCount - 1, _unzipToDir_copy_thread, task);
        if (err != 0) {
            err = ERR_NZ_INTERNAL_ERROR;
            break;
        }

        for (int i = 0; i < threadCount; i++) {
            mq_push(&task->mq, (void*)NULL); // finish signal for each thread
        }

        if (task->isCancelled) break;
        _unzipToDir_consume_queue(task, zip);
    } while (0);

    simple_thread_pool_destroy(&task->pool); // wait for all thread finish
    thd_mutex_destroy(&task->progress_mutex);
    mq_destroy(&task->mq, free);

    if (task->errCode) err = task->errCode;
    if (task->isCancelled) return err;
    _unzipToDir_dirs_set_mtime(task, zip); // set last modified time for all directories. this must be done after all files saved to avoid updating directory time during saving file
    //my_zip_close(zip);
    return err;
}

// --------------------------------------------------------------------------
// rename / move / delete entry
// --------------------------------------------------------------------------

// remove entries in zip. If entry is a directory, remove all children recursively
int zipRemoveEntries(zip_t *zip, const char **entryPaths, int entriesCount) {
    struct zip_stat st;
    int err = 0;
    zip_int64_t cnt = zip_get_num_entries(zip, 0);

    for (int j=0; j<entriesCount; j++) {
        const char *path = entryPaths[j];
        size_t pathLen = strlen(path);
        bool isDir = path[pathLen-1] == '/';

        if (!isDir) { // is a file
            zip_uint64_t index = zip_name_locate(zip, path, 0);
            if (index < 0) return ERR_NZ_ZIP_ENTRY_NOT_FOUND;
            
            err = zip_delete(zip, index);
            if (err) return my_zip_get_error(zip);
            continue;
        }

        bool isFound = false;
        for (zip_int64_t i = 0; i < cnt; i++) {            
            if (zip_stat_index(zip, i, 0, &st) != 0) continue;
            if (strncmp(st.name, path, pathLen) != 0) continue;
            
            err = zip_delete(zip, st.index);
            if (err) return my_zip_get_error(zip);
            isFound = true;
        }
        if (!isFound) return ERR_NZ_ZIP_ENTRY_NOT_FOUND;
    }
    return 0;
}

//

// rename / move entry in zip
// If [entryPath] is a directory, rename / move all children recursively
int zipRenameEntry(zip_t *zip, const char* entryPath, const char* newEntryPath) {
    // return < 0 : operation failed
    // return >= 0 : taskId

    size_t pathLen = strlen(entryPath);
    bool isDir = entryPath[0] == '\0' || entryPath[pathLen - 1] == '/';
    int err = 0;

    if (!isDir) { // is a file
        zip_uint64_t index = zip_name_locate(zip, entryPath, 0);
        if (index < 0) err = ERR_NZ_ZIP_ENTRY_NOT_FOUND;
        else err = zip_file_rename(zip, index, newEntryPath, ZIP_FL_ENC_UTF_8);
        if (err) {
            err = my_zip_get_error(zip);
            if (err == ZIP_ER_INVAL || err == ZIP_ER_DELETED) {
                err = ERR_NZ_ZIP_ENTRY_NOT_FOUND;
            } else if (err == ZIP_ER_EXISTS) {
                err = ERR_NZ_ZIP_ENTRY_ALREADY_EXISTS;
            }
            return err;
        }
    }
    else { // is a directory, rename all its children recursively
        if (newEntryPath[0] != '\0' && newEntryPath[strlen(newEntryPath) - 1] != '/') {
            return ERR_NZ_INVALID_PATH;
        }

        struct zip_stat st;
        zip_int64_t cnt = zip_get_num_entries(zip, 0);

        bool isFound = false;
        char buf[MAX_PATH_CHAR_COUNT];
        for (zip_int64_t i = 0; i < cnt; i++) {
            if (zip_stat_index(zip, i, 0, &st) != 0) continue;
            if (strncmp(st.name, entryPath, pathLen) != 0) continue;
            
            snprintf(buf, sizeof(buf), "%s%s", newEntryPath, st.name + pathLen);
            if (buf[0] == '\0') continue; // e.g., moving 'dirA/dirB/' to '' (root), ignore the 'dirA/dirB/' entry

            err = zip_file_rename(zip, st.index, buf, ZIP_FL_ENC_UTF_8);
            if (err) {
                err = my_zip_get_error(zip);
                if (err == ZIP_ER_INVAL || err == ZIP_ER_DELETED) {
                    err = ERR_NZ_ZIP_ENTRY_NOT_FOUND;
                } else if (err == ZIP_ER_EXISTS) {
                    err = ERR_NZ_ZIP_ENTRY_ALREADY_EXISTS;
                }
                return err;
            }
            isFound = true;
        }
        if (!isFound) return ERR_NZ_ZIP_ENTRY_NOT_FOUND;
    }

    return 0;
}

// move entry in zip
// If [entryPath] is a directory, move all children recursively
int zipMoveEntries(zip_t *zip, const char** entryPaths, int entriesCount, const char* newEntryBasePath) {
    // [newEntryBasePath] must ends with '/'
    // all the entries in [entryPaths] will be moved to [newEntryBasePath]/*/*

    if (newEntryBasePath[0] != '\0' && !_unzipDir_path_is_direactory(newEntryBasePath)) {
        return ERR_NZ_INVALID_PATH;
    }

    char newPath[MAX_PATH_CHAR_COUNT];
    for (int i = 0; i < entriesCount; i++) {
        const char* oldPath = entryPaths[i];
        const char *filename = _unzipDir_find_path_last_separator(oldPath);
        if (filename == NULL) filename = oldPath;
        else filename++;
        snprintf(newPath, sizeof(newPath), "%s%s", newEntryBasePath, filename);
        int err = zipRenameEntry(zip, oldPath, newPath);
        if (err) return err;
    }
    return 0;
}



// --------------------------------------------------------------------------
// ez sync functions
// --------------------------------------------------------------------------

int zipDir_ez(const char* dirPath, const char* zipFilepath, bool skipTopLevel, int threadCount) {
    zip_t* zip = zip_open(zipFilepath, ZIP_CREATE, NULL);
    if (zip == NULL) return ZIP_ER_EXISTS;

    const char* entryPathsArr[] = { dirPath };
    _my_zip_task task = { .compressLevel = 5 };
    int err = zipDir(&task, zip, entryPathsArr, 1, "", skipTopLevel, threadCount);
    if (!task.isZipClosed) {
        if (err) zip_discard(zip);
        else my_zip_close(zip);
    }
    return err;
}

int unzipToDir_ez(const char* zipFilepath, const char* toDirPath, int threadCount) {
    zip_t* zip = zip_open(zipFilepath, 0, NULL);
    if (zip == NULL) return ZIP_ER_OPEN;

    const char* entryPathsArr[] = { "" };

    _my_unzip_task task = { 0 };
    int err = unzipToDir(&task, zip, zipFilepath, (char**)entryPathsArr, 1, toDirPath, threadCount);
    my_zip_close(zip);
    return err;
}
