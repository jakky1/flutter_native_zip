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
#include "my_file.h"
#include "my_utils.h"
#include "native_zip.h"

#include <zip.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// --------------------------------------------------------------------------
// zip file operation
// --------------------------------------------------------------------------

bool _verifyZipPassword(zip_t *zip) {
    // use default password to open an zip entry, and return true if success
    struct zip_stat st;
    zip_int64_t cnt = zip_get_num_entries(zip, 0);
    for (zip_int64_t i = 0; i < cnt; i++) {
        int err = zip_stat_index(zip, i, 0, &st);
        if (err) return false;

        if (st.name[0] != '\0' && st.name[strlen(st.name) - 1] != '/') {
            zip_file_t* zf = zip_fopen_index(zip, st.index, 0);
            if (zf == NULL) return false;
            zip_fclose(zf);
            return true;
        }
    }
    return true;
}

FFI_PLUGIN_EXPORT void* openZip(const char* filename, const char* password) {
    zip_t *zip = zip_open(filename, ZIP_CREATE, NULL);
    if (zip && password) {
        int err = zip_set_default_password(zip, password);
        if (err || !_verifyZipPassword(zip)) {
            zip_close(zip);
            return NULL;
        }
    }
    return (void*) zip;
}

FFI_PLUGIN_EXPORT int closeZip(void* zip) {
    return my_zip_close((zip_t*)zip);
}

FFI_PLUGIN_EXPORT void discardZip(void* zip) {
    zip_discard((zip_t*)zip);
}

FFI_PLUGIN_EXPORT void nativeFree(void* p) {
    free(p);
}

FFI_PLUGIN_EXPORT NativeZipEntry* getZipEntries(void* zip, int* outCount, const char* path, int isRecursive) {
    // NOTE: dart code should free the returned pointer
    // [path] == "" means root directory
    // [path] cannot be NULL
    // return NativeZipEntry*
    zip_t* _zip = (zip_t*)zip;
    struct zip_stat st;
    NativeZipEntry* ret;
    size_t lenPath = strlen(path);
    bool isPathEndsWithSeparator = lenPath > 0 && path[lenPath-1] == ZIP_PATH_SEPARATOR;

    zip_int64_t totalCount = zip_get_num_entries(_zip, 0);
    ret = (NativeZipEntry*)malloc(sizeof(NativeZipEntry) * totalCount);

    bool dontFilter = (lenPath == 0) && isRecursive;
    int cnt = 0;
    for (zip_int64_t i = 0; i < totalCount; i++) {
        if (zip_stat_index(_zip, i, 0, &st) != 0) continue;

        if (!dontFilter) { // filter entries
            if (strncmp(st.name, path, lenPath) != 0) continue; // if not the same prefix, ignored
            char nextCh = st.name[lenPath];
            if (nextCh == '\0' && !isPathEndsWithSeparator) { // entry is a file
                i = totalCount; // ignore the rest of entries
            } else if (nextCh != '\0') { // entry maybe a file or sub-directory in [path]
                bool isChild = lenPath == 0 || (isPathEndsWithSeparator || nextCh == ZIP_PATH_SEPARATOR);
                if (!isChild) continue; // if not a child in [path], ignored
                if (!isRecursive) {
                    const char* pFirstSeparator = strchr(st.name + lenPath + 1, ZIP_PATH_SEPARATOR);
                    if (pFirstSeparator != NULL && pFirstSeparator[1] != '\0') continue;
                }
            }
        }

        NativeZipEntry* entry = &ret[cnt++];
        entry->index = st.index;
        entry->path = st.name;
        entry->originalSize = st.size;
        entry->compressedSize = st.comp_size;
        entry->modifiedTime = st.mtime;
    }
    *outCount = cnt;
    return ret;
}

// --

FFI_PLUGIN_EXPORT void* readZipFileEntryOpenByIndex(void* zip, int index) {
    zip_file_t* file = zip_fopen_index((zip_t*)zip, index, 0);
    return file;
}

FFI_PLUGIN_EXPORT void* readZipFileEntryOpen(void* zip, const char* entryPath) {
    zip_file_t* file = zip_fopen((zip_t*)zip, entryPath, 0);
    return file;
}

FFI_PLUGIN_EXPORT int readZipFileEntry(void* zipEntryFile, int8_t* buf, int len) {
    // return bytes count read
    zip_file_t* _file = (zip_file_t*)zipEntryFile;
    return (int)zip_fread(_file, buf, len);
}

FFI_PLUGIN_EXPORT int readZipFileEntryClose(void* zipEntryFile) {
    // return 0 if success
    zip_file_t* _file = (zip_file_t*)zipEntryFile;
    return zip_fclose(_file);
}


// --------------------------------------------------------------------------
// zip open / close / discard logging
// --------------------------------------------------------------------------

int my_zip_get_error(zip_t* zip) {
    zip_error_t* error = zip_get_error(zip);
    return zip_error_code_zip(error);
}

#undef zip_open
#undef zip_discard

int my_zip_close(zip_t* zip) {
    // if zip_close() fails, get the real error code, and call zip_discard()

    //notifyDartLog("######## zip_close() called");
    int err = zip_close(zip);
    if (err) {
        zip_error_t* error = zip_get_error(zip);
        err = zip_error_code_zip(error);
        zip_discard(zip);
    }
    return err;
}

zip_t* __zip_open(const char* path, int flags, int* errorp) {
    //notifyDartLog("######## zip_open() called");
    return zip_open(path, flags, errorp);
}
void __zip_discard(zip_t* zip) {
    //notifyDartLog("######## zip_discard() called");
    zip_discard(zip);
}
