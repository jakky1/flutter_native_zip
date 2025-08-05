#include "my_file.h"
#include "my_utils.h"
#include "native_zip.h"

#include <stdio.h>
#include <stdlib.h>

char* _my_dir_current_file_name(MyDir* pDir);
char* _my_dir_current_file_path(MyDir* pDir) {
    if (pDir->_nowFilepath[pDir->_dirPathLen] == 0) {
        char* nowFilename = _my_dir_current_file_name(pDir);
        pDir->_nowFilepath[pDir->_dirPathLen] = DIR_SEPARATOR;
        size_t maxCopyLen = sizeof(pDir->_nowFilepath) - pDir->_dirPathLen - 1;
        my_strncpy(pDir->_nowFilepath + pDir->_dirPathLen + 1, nowFilename, maxCopyLen);
    }
    return pDir->_nowFilepath;
}

const char* _my_file_get_filename_from_path(const char* path) {
    const char* p = strrchr(path, DIR_SEPARATOR);
    if (p != NULL) return p + 1;
    else return path;
}

int _my_dir_mkdirs(const char* dirPath) {
    int separatorIndex[1024]; // position of all path separator chars
    int separatorCount = 0;
    int i = 1; // skip first char for path "/xxx"
    while (1) {
        char ch = dirPath[i];
        if (ch == 0) break;
        if (ch == '/' || ch == '\\') {
            separatorIndex[separatorCount++] = i;
        }
        i++;
    }

    NATIVE_FILE_STAT st;
    char* _path = (char*)dirPath;
    for (i = separatorCount - 1; i >= 0; i--) {
        int separatorPos = separatorIndex[i] + 1;
        char ch = _path[separatorPos];
        _path[separatorPos] = 0;
        int err = my_file_stat(dirPath, &st);
        _path[separatorPos] = ch;
        if (err != 0) continue; // file not exists

        if (S_ISDIR(st.st_mode)) { // is a directory
            i++;
            break;
        }
        if (S_ISREG(st.st_mode)) { // is a file
            return -1;
        }
        return -1; // unknown error
    }

    for (; i < separatorCount; i++) {
        int separatorPos = separatorIndex[i];
        _path[separatorPos] = 0;
        int err = _my_dir_mkdir(dirPath);
        if (err != 0) {
            int ret = my_file_stat(dirPath, &st);
            if (ret != 0 || !S_ISDIR(st.st_mode)) { // if not exists, or if not a directory
                return err;
            }
        }
        _path[separatorPos] = DIR_SEPARATOR;
    }

    return 0;
}

void my_string_replace(char* path, char from, char to) {
    while (1) {
        char ch = *path;
        if (ch == 0) break;
        if (ch == from) *path = to;
        path++;
    }
}

void _my_file_path_separator_fix(char* path) {
#ifdef _WIN32
    my_string_replace(path, '/', '\\');
#else
    my_string_replace(path, '\\', '/');
#endif
}

int _my_dir_mkdirs_for_file(const char* filePath) {
    char* p = (char*) strrchr(filePath, DIR_SEPARATOR);
    if (p == NULL) return -1;
    *p = '\0';
    int err = _my_dir_mkdirs(filePath);
    *p = DIR_SEPARATOR;
    return err;
}

// --------------------------------------------------------------------------

int _my_dir_traversal_dir(const char* dirPath, const char* relativePath, size_t relPathLen, NATIVE_FILE_STAT* st, my_dir_traversal_cb cb, void* param) {
    int findResult = 0;
    int err = 0;

    if (relPathLen >= MAX_PATH_CHAR_COUNT - 1) return ERR_NZ_DIR_TRAVERSAL_PATH_TOO_LONG;

    err = cb(dirPath, relativePath, st, param); // callback for a directory found
    if (err != 0) return err;

    MyDir *dir = (MyDir*)malloc(sizeof(MyDir));
    findResult = _my_dir_findFirst(dir, dirPath);
    if (findResult == ERR_NZ_DIR_TRAVERSAL_FILE_NOTFOUND) err = ERR_NZ_DIR_TRAVERSAL_FILE_NOTFOUND;
    while (findResult == 0) {
        char* childName = _my_dir_current_file_name(dir);
        char* childPath = _my_dir_current_file_path(dir);
        if (S_ISDIR(dir->st.st_mode)) {
            snprintf((char*)relativePath + relPathLen, MAX_PATH_CHAR_COUNT - relPathLen, "%s%c", childName, ZIP_PATH_SEPARATOR);
            err = _my_dir_traversal_dir(childPath, relativePath, relPathLen + strlen(childName) + 1, &dir->st, cb, param);
        }
        else if (S_ISREG(dir->st.st_mode)) {
            my_strncpy((char*)relativePath + relPathLen, childName, MAX_PATH_CHAR_COUNT - relPathLen);
            err = cb(childPath, relativePath, &dir->st, param);
        }
        else if (S_ISLNK(dir->st.st_mode)) {
            // TODO: handle symbolic link files in linux
            //err = ERR_NZ_DIR_TRAVERSAL_FILE_NOTFOUND;
            //break;
        }
        else {
            // ignore all other cases
        }

        if (err != 0) break;
        findResult = _my_dir_findNext(dir);
    }

    _my_dir_close(dir);
    free(dir);
    return err;
}

int my_dir_traversal(const char* path, const char *relPath, bool skipTopLevel, my_dir_traversal_cb cb, void* param) {
    NATIVE_FILE_STAT st;
    int err = my_file_stat(path, &st);
    if (err != 0) return ERR_NZ_DIR_TRAVERSAL_FILE_NOTFOUND;

    char *relativePath = (char*) malloc(MAX_PATH_CHAR_COUNT);
    const char* filename = _my_file_get_filename_from_path(path);
    if (S_ISDIR(st.st_mode)) { // is directory
        if (skipTopLevel) {
            my_strncpy(relativePath, relPath, MAX_PATH_CHAR_COUNT);
            err = _my_dir_traversal_dir(path, relativePath, strlen(relPath), &st, cb, param);
        }
        else {
            snprintf(relativePath, MAX_PATH_CHAR_COUNT, "%s%s%c", relPath, filename, ZIP_PATH_SEPARATOR);
            err = _my_dir_traversal_dir(path, relativePath, strlen(relativePath), &st, cb, param);
        }
    }
    else if (S_ISREG(st.st_mode)) { // is normal file, or hard-link file in unix
        snprintf(relativePath, MAX_PATH_CHAR_COUNT, "%s%s", relPath, filename);
        err = cb(path, relativePath, &st, param);
    }
    else if (S_ISLNK(st.st_mode)) { // is symbolic-link file in unix
        // TODO: handle symbolic link files in linux
        err = ERR_NZ_DIR_TRAVERSAL_FILE_NOTFOUND;
    }
    else {
        err = 0; // ignore all other case
    }

    free(relativePath);
    return err;
}
