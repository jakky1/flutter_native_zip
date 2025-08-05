#pragma once

#include "native_zip.h"

#include <sys/stat.h> // stat()
#include <string.h> // strcpy_s()
#include <stdbool.h> // bool

#define ZIP_PATH_SEPARATOR '/'
#define MAX_PATH_CHAR_COUNT 1024*32

#ifdef _WIN32
#include <stdio.h>
#include <windows.h>
int _my_file_fopen_windows(FILE** fp, const char* path, WCHAR* mode); // windows-only
#define _my_file_fopen(fp, path, mode) _my_file_fopen_windows(fp, path, (WCHAR*) L##mode)
#else
#define _my_file_fopen(fp, path, mode) *fp = fopen(path, mode)
#endif

#ifdef _WIN32
#include <windows.h>
#include <direct.h> // mkdir()
#include <io.h> // _findfirst(), ...
typedef HANDLE _NATIVE_DIR;
typedef WIN32_FIND_DATA _NATIVE_FILE_INFO;
typedef struct _stat NATIVE_FILE_STAT;
#define DIR_SEPARATOR '\\'

#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#define	_S_IFLNK	0x0400	/* Symbolic link.  */
#define S_ISLNK(mode) (((mode) & _S_IFLNK) == _S_IFLNK)

#define mkdir(path, mode) _mkdir(path)

#else

#include <dirent.h> // opendir()
//#include <sys/types.h>
typedef DIR* _NATIVE_DIR;
typedef struct dirent* _NATIVE_FILE_INFO;
typedef struct stat NATIVE_FILE_STAT;

#define DIR_SEPARATOR '/'
#define strcat_s(dest, size, src) strcat(dest, src)
#endif

typedef struct _MyDir {
    _NATIVE_DIR dir;
    NATIVE_FILE_STAT st;
    _NATIVE_FILE_INFO info;
    size_t _dirPathLen;
    char _nowFilepath[MAX_PATH_CHAR_COUNT]; // full path of current file entry, utf8
#ifdef _WIN32
    char _utf8Filename[MAX_PATH_CHAR_COUNT]; // only used in windows, to convert WCHAR to char
#endif
} MyDir;

int _my_dir_mkdir(const char* path);
int _my_dir_mkdirs_for_file(const char* filePath);
int my_file_stat(const char* path, NATIVE_FILE_STAT* st);

void _my_file_path_separator_fix(char* path);

char* _my_dir_current_file_name(MyDir* pDir);
char* _my_dir_current_file_path(MyDir* pDir);
int _my_dir_mkdirs(const char* path);
const char* _my_file_get_filename_from_path(const char* path);
char* _my_dir_current_file_name(MyDir* pDir);
int _my_dir_findFirst(MyDir* pDir, const char* dirPath);
int _my_dir_findNext(MyDir* pDir);
int _my_dir_close(MyDir* pDir);

typedef int (*my_dir_traversal_cb)(const char* path, const char* relPath, NATIVE_FILE_STAT* st, void* param);
int my_dir_traversal(const char* path, const char* relPath, bool skipTopLevel, my_dir_traversal_cb cb, void* param);

int my_file_set_lastWriteTime(const char* path, bool isDir, time_t mtime);

