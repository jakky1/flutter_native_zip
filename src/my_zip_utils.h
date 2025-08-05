#pragma once

#include "native_zip.h"
#include "my_queue.h"
#include "my_message_queue.h"
#include "my_atomic_int_max.h"
#include "my_threadpool.h"

#include <zip.h>

typedef struct {
    char* now_processing_filePath; // DO NOT free this
    size_t total_fileSize;
    size_t processed_fileSize;
    size_t processed_compressSize;
} _my_zip_task_progress_info;

typedef struct {
    STRUCT_NativeZipTaskInfo // dart accessible part

    zip_t* zip;
    int compressLevel;
    bool hasPassword;
    bool isZipClosed; // is zip_close() called in zipDir()
    long maxBlockSize; // max file block size to compress

    const char* entryDirPathBase; // zip files to which dir path in .zip file, DON't free()
    Queue queue_cb_data;
    MessageQueue mq_blocks; // all '_my_zip_block' need to compress by threads
    thd_mutex mq_blocksMutex;
    atomic_int_max_t nowMemoryUsage; // current memory used by all '_my_zip_block->compressedData'

    atomic_int_max_t allocatedBlocksTracker;
} _my_zip_task;

typedef struct _my_unzip_task {
    STRUCT_NativeZipTaskInfo // dart accessible part
    thd_mutex progress_mutex;

    zip_t* zip;
    const char* password;
    const char* zipFilePath;
    const char* dirPath;
    MessageQueue mq;
    SimpleThreadPool pool;
} _my_unzip_task;


int zipDir(_my_zip_task* task, void* _zip, const char** dirPathList, int dirPathListCount, const char* entryDirPathBase, bool skipTopLevel, int threadCount);
int unzipToDir(_my_unzip_task* task, void* _zip, const char* zipFilePath, char** entryPathsArr, int entriesCount, const char* toDirPath, int threadCount);
int zipRemoveEntries(zip_t* zip, const char** entryPaths, int entriesCount);
int zipRenameEntry(zip_t* zip, const char* entryPath, const char* newEntryPath);
int zipMoveEntries(zip_t* zip, const char** entryPaths, int entriesCount, const char* newEntryBasePath);


// --------------------------------------------------------------------------
// ez sync functions
// --------------------------------------------------------------------------

int zipDir_ez(const char* dirPath, const char* zipFilepath, bool skipTopLevel, int threadCount);
int unzipToDir_ez(const char* zipFilepath, const char* toDirPath, int threadCount);
