#ifndef _WIN32

#include "my_file.h"
#include "my_utils.h"
#include <utime.h>

int _my_dir_mkdir(const char* path) {
    return mkdir(path, 0755);
}

int my_file_stat(const char* path, NATIVE_FILE_STAT* st) {
    return lstat(path, st); // NOTE: don't use stat()
}

char* _my_dir_current_file_name(MyDir* pDir) {
    return pDir->info->d_name;
}

int my_file_set_lastWriteTime(const char* path, bool isDir, time_t mtime) {
    struct utimbuf t;
    t.actime = mtime;
    t.modtime = mtime;
    return utime(path, &t);
}

// --------------------------------------------------------------------------

int _my_dir_findNext(MyDir* pDir) {
    pDir->_nowFilepath[pDir->_dirPathLen] = 0; // set _nowFilepath = _dirPath

    pDir->info = readdir(pDir->dir);
    if (pDir->info == NULL) return ERR_NZ_DIR_TRAVERSAL_NO_MORE_FILE;

    char* name = pDir->info->d_name;
    if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) return _my_dir_findNext(pDir);

    pDir->_nowFilepath[pDir->_dirPathLen] = '/';
    strcpy(pDir->_nowFilepath + pDir->_dirPathLen + 1, pDir->info->d_name);

    lstat(pDir->_nowFilepath, &pDir->st); // NOTE: don't use stat()
    pDir->_nowFilepath[pDir->_dirPathLen] = 0; // set _nowFilepath = _dirPath

    return 0;
}

int _my_dir_findFirst(MyDir* pDir, const char* dirPath) {
    pDir->dir = opendir(dirPath);
    if (pDir->dir == NULL) return ERR_NZ_DIR_TRAVERSAL_FILE_NOTFOUND;

    pDir->_dirPathLen = strlen(dirPath); // don't use 'len' here, 'len' is 
    my_strncpy(pDir->_nowFilepath, dirPath, sizeof(pDir->_nowFilepath)); // set _nowFilepath = _dirPath

    return _my_dir_findNext(pDir);
}

int _my_dir_close(MyDir* pDir) {
    closedir(pDir->dir);
    pDir->dir = NULL;
    return 0;
}

#endif