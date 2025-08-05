#pragma once

#include <stdint.h>
#include <sys/stat.h>


#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif


// --------------------------------------------------------------------------
// dart notify
// --------------------------------------------------------------------------

typedef enum DartNotifyAction {
    TASK_FINISH = 0,
    TASK_WARNING,
    TASK_ERROR,
    TASK_LOG, // print log
} DartNotifyAction;

typedef struct NativeNotifyMessage {
    int taskId;
    int action; // values in [DartNotifyAction]
    int errCode;
    char* errMsg;
} NativeNotifyMessage;

FFI_PLUGIN_EXPORT NativeNotifyMessage* getDartMessage();

// --------------------------------------------------------------------------
// zlib stream
// --------------------------------------------------------------------------

FFI_PLUGIN_EXPORT void* openZipStream(int windowBits, int compressLevel);
FFI_PLUGIN_EXPORT int writeZipStream(void* pStream, int isZipping, int8_t* inBuf, int inBufSize, int8_t* outBuf, int outBufSize, int isEOF);
FFI_PLUGIN_EXPORT int writeZipStream_readNext(void* _pStream, int isZipping, int8_t* outBuf, int outBufSize, int isEOF);
FFI_PLUGIN_EXPORT void closeZipStream(void* pStream);

FFI_PLUGIN_EXPORT void* openUnzipStream(int windowBits);
FFI_PLUGIN_EXPORT void closeUnzipStream(void* pStream);


// --------------------------------------------------------------------------
// zip utils
// --------------------------------------------------------------------------

typedef struct NativeZipTaskProgressInfo {
    char* now_processing_filePath;
    size_t total_fileSize;
    size_t processed_fileSize;
    size_t processed_compressSize;
} NativeZipTaskProgressInfo;

#include <stdbool.h>
#define STRUCT_NativeZipTaskInfo \
    int taskId; \
    int errCode; \
    bool isCancelled;    \
    bool isDone; \
    NativeZipTaskProgressInfo progress;   

typedef struct NativeZipTaskInfo {
    STRUCT_NativeZipTaskInfo
} NativeZipTaskInfo;

FFI_PLUGIN_EXPORT void* zipDirAsync(void* _zip, bool hasPassword, const char** dirPathList, int dirPathListCount, const char* entryDirPathBase, int compressLevel, int skipTopLevel, int threadCount);
FFI_PLUGIN_EXPORT void* unzipToDirAsync(void* _zip, const char *password, const char *zipFilePath, const char **entryPathsArr, int entriesCount, const char *toDirPath, int threadCount);

FFI_PLUGIN_EXPORT int zipRenameEntryAsync(void* _zip, const char* entryPath, const char* newEntryPath);
FFI_PLUGIN_EXPORT int zipMoveEntriesAsync(void* _zip, const char** entryPaths, int entriesCount, const char* newEntryBasePath);
FFI_PLUGIN_EXPORT int zipRemoveEntriesAsync(void* _zip, const char** entryPaths, int entriesCount);

// --------------------------------------------------------------------------
// zip 
// --------------------------------------------------------------------------

typedef struct {
    uint64_t  index;
    const char* path;
    uint64_t  originalSize;
    uint64_t  compressedSize;
    time_t    modifiedTime;
} NativeZipEntry;


FFI_PLUGIN_EXPORT void* openZip(const char* filename, const char* password);
FFI_PLUGIN_EXPORT int closeZip(void* zip);
FFI_PLUGIN_EXPORT void discardZip(void* zip);

FFI_PLUGIN_EXPORT NativeZipEntry* getZipEntries(void* zip, int* outCount, const char* path, int isRecursive);
FFI_PLUGIN_EXPORT void nativeFree(void* p);

FFI_PLUGIN_EXPORT void* readZipFileEntryOpenByIndex(void* zip, int index);
FFI_PLUGIN_EXPORT void* readZipFileEntryOpen(void* zip, const char* entryPath);
FFI_PLUGIN_EXPORT int readZipFileEntry(void* zipEntryFile, int8_t* buf, int len);
FFI_PLUGIN_EXPORT int readZipFileEntryClose(void* zipEntryFile);


// --------------------------------------------------------------------------
// error codes
// --------------------------------------------------------------------------

#define MIN_TASK_ID 888

typedef enum NativeZipErrors {
    ERR_NZ_MIN = -10000,
    ERR_NZ_CANCELLED,
    ERR_NZ_DIR_TRAVERSAL_FILE_NOTFOUND,
    ERR_NZ_DIR_TRAVERSAL_NO_MORE_FILE,
    ERR_NZ_DIR_TRAVERSAL_PATH_TOO_LONG,
    ERR_NZ_INVALID_ARGUMENT,
    ERR_NZ_INVALID_PATH, // malicious path, or entry path not ends with '/' for a dir
    ERR_NZ_ZIP_HAS_MALICIOUS_PATH,
    ERR_NZ_INTERNAL_ERROR,
    ERR_NZ_MKDIR,
    ERR_NZ_ZIP_ENTRY_NOT_FOUND,
    ERR_NZ_ZIP_ENTRY_ALREADY_EXISTS, // try to add / move / rename to an entry which is already exists
    ERR_NZ_FILE_ALREADY_EXISTS,
    ERR_NZ_MAX,
} NativeZipErrors;
