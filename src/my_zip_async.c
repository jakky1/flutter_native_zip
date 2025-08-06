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

#include "my_zip.h"
#include "my_zip_utils.h"
#include "my_task_notify.h"

#include <zip.h>

#include <stdlib.h>
#include <stdbool.h>

typedef struct _zip_func_params {
    zip_t *zip;
    void *task;
    int taskId;
    const char *s1;
    const char *s2;
    const char **sArr1;
    int entriesCount;
    int threadCount;
    int skipTopLevel;
} _zip_func_params;

typedef struct _my_zip_close_task {
    zip_t* zip;
    int taskId;
} _my_zip_close_task;

#define _AsyncFinalize(threadFunc, returnValue) \
    thd_thread thread; \
    thd_thread_create(&thread, threadFunc, (void*)params); \
    thd_thread_detatch(&thread); \
    return returnValue;

#define _AsyncThreadFinalize(toCloseZip) \
    if (err) { \
        if (toCloseZip) zip_discard(params->zip); \
        notifyDartTaskError(params->taskId, err, NULL); \
    } else { \
        if (toCloseZip) { \
            err = my_zip_close(params->zip); \
            if (err) notifyDartTaskError(params->taskId, err, NULL); \
            else notifyDartTaskFinish(params->taskId); \
        } else { \
            notifyDartTaskFinish(params->taskId); \
        } \
    } \
    free(params);

// --------------------------------------------------------------------------
// zipDir async
// --------------------------------------------------------------------------

void _zipDirAsync_thread(void *_params) {
    _zip_func_params *params = (_zip_func_params*)_params;
    _my_zip_task* task = (_my_zip_task*) params->task;
    int err = zipDir(task, params->zip, params->sArr1, params->entriesCount, params->s1, params->skipTopLevel, params->threadCount);
    if (err == 0) err = task->errCode;
    task->progress.now_processing_filePath = (char*)"";

    bool toCloseZip = !task->isZipClosed;
    _AsyncThreadFinalize(toCloseZip); // don't zip_close() here, because zipDir() already call it
    //free(task); // TODO: should free by dart code, but... how? if dart code ignore the 'task' variable?
}

// NOTE: will call zip_close() or zip_discard()
FFI_PLUGIN_EXPORT void* zipDirAsync(void* _zip, bool hasPassword, const char** dirPathList, int dirPathListCount, const char* entryDirPathBase, int compressLevel, int skipTopLevel, int threadCount) {
    zip_t *zip = (zip_t*)_zip;
    _my_zip_task* task = (_my_zip_task*) calloc(1, sizeof(_my_zip_task));
    task->taskId = generateTaskId();
    task->progress.now_processing_filePath = (char*) "";
    task->hasPassword = hasPassword;
    task->compressLevel = compressLevel;

    _zip_func_params *params = (_zip_func_params*) malloc(sizeof(_zip_func_params));
    params->task = task;
    params->taskId = task->taskId;
    params->zip = zip;
    params->sArr1 = dirPathList;
    params->entriesCount = dirPathListCount;
    params->s1 = entryDirPathBase;
    params->skipTopLevel = skipTopLevel;
    params->threadCount = threadCount;
    
    _AsyncFinalize(_zipDirAsync_thread, (void*)task);
}

// --------------------------------------------------------------------------
// unzipToDir async
// --------------------------------------------------------------------------

void _unzipToDirAsync_thread(void *_params) {
    _zip_func_params *params = (_zip_func_params*)_params;
    _my_unzip_task* task = (_my_unzip_task*) params->task;
    int err = unzipToDir(task, params->zip, params->s1, (char**)params->sArr1, params->entriesCount, params->s2, params->threadCount);
    if (err == 0) err = task->errCode;
    task->progress.now_processing_filePath = (char*)"";

    //free(params->task); // should free by dart
    //_AsyncThreadFinalize(true);

    if (err) {
        notifyDartTaskError(params->taskId, err, NULL);
    } else {
        notifyDartTaskFinish(params->taskId);
    }
    free(params);
}

// NOTE: won't call zip_close() or zip_discard()
FFI_PLUGIN_EXPORT void* unzipToDirAsync(void* _zip, const char *password, const char *zipFilePath, const char **entryPathsArr, int entriesCount, const char *toDirPath, int threadCount) {
    zip_t *zip = (zip_t*)_zip;
    _my_unzip_task* task = (_my_unzip_task*) calloc(1, sizeof(_my_unzip_task));
    task->taskId = generateTaskId();
    task->progress.now_processing_filePath = (char*) "";
    task->password = password;

    _zip_func_params *params = (_zip_func_params*) malloc(sizeof(_zip_func_params));
    params->task = task;
    params->taskId = task->taskId;
    params->zip = zip;
    params->s1 = zipFilePath;
    params->sArr1 = entryPathsArr;
    params->entriesCount = entriesCount;
    params->s2 = toDirPath;
    params->threadCount = threadCount;
    
    _AsyncFinalize(_unzipToDirAsync_thread, (void*)task);
}


// --------------------------------------------------------------------------
// rename async
// --------------------------------------------------------------------------

void _zipRenameEntryAsync_thread(void *_params) {
    _zip_func_params *params = (_zip_func_params*)_params;
    int err = zipRenameEntry(params->zip, params->s1, params->s2);
    _AsyncThreadFinalize(true);
}

// NOTE: will call zip_close() or zip_discard()
FFI_PLUGIN_EXPORT int zipRenameEntryAsync(void* _zip, const char* entryPath, const char* newEntryPath) {
    zip_t *zip = (zip_t*)_zip;
    _zip_func_params *params = (_zip_func_params*) malloc(sizeof(_zip_func_params));
    params->taskId = generateTaskId();
    params->zip = zip;
    params->s1 = entryPath;
    params->s2 = newEntryPath;
    
    _AsyncFinalize(_zipRenameEntryAsync_thread, params->taskId);
}

// --------------------------------------------------------------------------
// move entries async
// --------------------------------------------------------------------------

void _zipMoveEntriesAsync_thread(void *_params) {
    _zip_func_params *params = (_zip_func_params*)_params;
    int err = zipMoveEntries(params->zip, params->sArr1, params->entriesCount, params->s1);
    _AsyncThreadFinalize(true);
}

// NOTE: will call zip_close() or zip_discard()
FFI_PLUGIN_EXPORT int zipMoveEntriesAsync(void* _zip, const char** entryPaths, int entriesCount, const char* newEntryBasePath) {
    zip_t *zip = (zip_t*)_zip;
    _zip_func_params *params = (_zip_func_params*) malloc(sizeof(_zip_func_params));
    params->taskId = generateTaskId();
    params->zip = zip;
    params->sArr1 = entryPaths;
    params->entriesCount = entriesCount;
    params->s1 = newEntryBasePath;

    _AsyncFinalize(_zipMoveEntriesAsync_thread, params->taskId);
}

// --------------------------------------------------------------------------
// remove entries async
// --------------------------------------------------------------------------

void _zipRemoveEntriesAsync_thread(void *_params) {
    _zip_func_params *params = (_zip_func_params*)_params;
    int err = zipRemoveEntries(params->zip, params->sArr1, params->entriesCount);
    _AsyncThreadFinalize(true);
}

// NOTE: will call zip_close() or zip_discard()
FFI_PLUGIN_EXPORT int zipRemoveEntriesAsync(void *_zip, const char **entryPaths, int entriesCount) {
    zip_t *zip = (zip_t*)_zip;
    _zip_func_params *params = (_zip_func_params*) malloc(sizeof(_zip_func_params));
    params->taskId = generateTaskId();
    params->zip = zip;
    params->sArr1 = entryPaths;
    params->entriesCount = entriesCount;

    _AsyncFinalize(_zipRemoveEntriesAsync_thread, params->taskId);
}
