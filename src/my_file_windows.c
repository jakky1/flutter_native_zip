#ifdef _WIN32

#include "native_zip.h"
#include "my_file.h"
#include "my_thread.h"
#include "my_utils.h"

#include <windows.h>
#include <wchar.h>
#include <stdio.h> // snprintf
#include <io.h>
#include <stdbool.h>
#include <stdint.h>

// *** NOTE: in windows,
//     mkdir(), stat(), fopen() doesn't work with non-english utf8 path,
//     so we use _wmkdir(), _wstat(), _wfopen_s() instead

thd_mutex __mutex_mkdir;
int __mutex_mkdir_inited = 0;
int _my_dir_mkdir(const char* path) {
    WCHAR buf[MAX_PATH_CHAR_COUNT];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, buf, sizeof(buf) / sizeof(WCHAR));

    if (!__mutex_mkdir_inited) {
        __mutex_mkdir_inited = 1;
        thd_mutex_init(&__mutex_mkdir);
    }

    thd_mutex_lock(&__mutex_mkdir);
    int err = _wmkdir(buf); // NOTE: maybe not thread-safe...
    thd_mutex_unlock(&__mutex_mkdir);
    return err;
}

int my_file_stat(const char* path, NATIVE_FILE_STAT *st) {
    WCHAR buf[MAX_PATH_CHAR_COUNT];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, buf, sizeof(buf) / sizeof(WCHAR));
    return _wstat(buf, st);
}

void _my_file_unix_time_to_FILETIME(time_t t, FILETIME* ft) {
    uint64_t time = ((uint64_t)t * 10000000ULL) + 116444736000000000ULL;
    ft->dwLowDateTime = (DWORD)(time & 0xFFFFFFFF);
    ft->dwHighDateTime = (DWORD)(time >> 32);
}

int my_file_set_lastWriteTime(const char* path, bool isDir, time_t mtime) {
    WCHAR buf[MAX_PATH_CHAR_COUNT];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, buf, sizeof(buf) / sizeof(WCHAR));

    HANDLE hFile = CreateFileW(buf, FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 
        isDir ? FILE_FLAG_BACKUP_SEMANTICS : 0,
        NULL);
    if (hFile == INVALID_HANDLE_VALUE) return -1;

    FILETIME mt;
    _my_file_unix_time_to_FILETIME(mtime, &mt);
    bool ret = SetFileTime(hFile, &mt, &mt, &mt);
    CloseHandle(hFile);
    return ret ? 0 : -1;
}

int _my_file_fopen_windows(FILE** fp, const char* path, WCHAR* mode) {
    WCHAR buf[MAX_PATH_CHAR_COUNT];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, buf, sizeof(buf) / sizeof(WCHAR));
    return _wfopen_s(fp, buf, mode);
}

char* _my_dir_current_file_name(MyDir* pDir) {
    if (pDir->_utf8Filename[0] == 0) {
        WideCharToMultiByte(CP_UTF8, 0, pDir->info.cFileName, -1, pDir->_utf8Filename, sizeof(pDir->_utf8Filename), NULL, NULL);
    }
    return pDir->_utf8Filename;
}

// --------------------------------------------------------------------------

void _my_dir_findNext_copyStat(MyDir* pDir) {
    pDir->st.st_mode = _S_IFDIR;   
    if ((pDir->info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        pDir->st.st_mode = _S_IFREG;
        pDir->st.st_size = ((size_t)pDir->info.nFileSizeHigh << 32) | pDir->info.nFileSizeLow;
    }

    if (pDir->info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        if (pDir->info.dwReserved0 == IO_REPARSE_TAG_SYMLINK) {
            // is symbolic link file or directory
            pDir->st.st_mode |= _S_IFLNK;
        }
    }

    ULARGE_INTEGER ull;
    ull.LowPart = pDir->info.ftLastWriteTime.dwLowDateTime;
    ull.HighPart = pDir->info.ftLastWriteTime.dwHighDateTime;
    ull.QuadPart -= 116444736000000000ULL;
    pDir->st.st_mtime = (ull.QuadPart / 10000000ULL);
}

int _my_dir_findFirst(MyDir* pDir, const char* dirPath) {
    WCHAR *buf = (WCHAR*) malloc(MAX_PATH_CHAR_COUNT * sizeof(WCHAR));
    // TODO: is this safe enough?
    int len = MultiByteToWideChar(CP_UTF8, 0, dirPath, -1, buf, MAX_PATH_CHAR_COUNT - 3) - 1;
    wcscpy_s(buf + len, 5,  L"\\*"); // append string "\\*" to end of dirPath

    pDir->dir = FindFirstFileW(buf, &pDir->info);
    //pDir->dir = FindFirstFileEx(buf, FindExInfoBasic, &pDir->info, FindExSearchNameMatch, NULL, FIND_FIRST_EX_CASE_SENSITIVE | FIND_FIRST_EX_LARGE_FETCH);
    free(buf);
    if (pDir->dir == INVALID_HANDLE_VALUE) return ERR_NZ_DIR_TRAVERSAL_FILE_NOTFOUND;

    pDir->_utf8Filename[0] = 0; // clear current filename, windows-only
    pDir->_dirPathLen = strlen(dirPath); // don't use 'len' here, 'len' is 
    my_strncpy(pDir->_nowFilepath, dirPath, sizeof(pDir->_nowFilepath)); // set _nowFilepath = _dirPath

    char* name = _my_dir_current_file_name(pDir);
    if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) return _my_dir_findNext(pDir);

    _my_dir_findNext_copyStat(pDir);
    return 0;
}

int _my_dir_findNext(MyDir* pDir) {
    pDir->_nowFilepath[pDir->_dirPathLen] = 0; // set _nowFilepath = _dirPath
    pDir->_utf8Filename[0] = 0; // clear current filename

    int err = FindNextFileW(pDir->dir, &pDir->info);
    if (err == 0) return ERR_NZ_DIR_TRAVERSAL_NO_MORE_FILE;

    char* name = _my_dir_current_file_name(pDir);
    if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) return _my_dir_findNext(pDir);

    _my_dir_findNext_copyStat(pDir);
    return 0;
}

int _my_dir_close(MyDir* pDir) {
    FindClose(pDir->dir);
    return 0;
}

#endif