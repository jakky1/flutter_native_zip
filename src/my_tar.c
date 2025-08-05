#include "my_file.h"
#include "my_hashmap.h"
#include "my_common.h"
#include "my_utils.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>

#define BLOCKSIZE 512
#define NAME_SIZE 100
#define PREFIX_SIZE 155

#define _round_up_to_512(v) ((v & (512 - 1)) == 0 ? v : (v | (512-1)) + 1)

// in https://www.gnu.org/software/tar/manual/html_node/Standard.html :
//   => The name, linkname, magic, uname, and gname are null-terminated character strings. All other fields are zero-filled octal numbers in ASCII.

// ref: https://www.ibm.com/docs/sl/aix/7.2.0?topic=files-tarh-file
typedef struct { // POSIX ustar header
    char name[NAME_SIZE]; // maybe not end with '\0' if just the strlen(path) == 100
    char mode[8];   // permission
    char uid[8];    // max: 2097151
    char gid[8];    // max: 2097151
    char size[12];  // max size: 8GB
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[NAME_SIZE]; // maybe not end with '\0' if just the strlen(linkname) == 100
    char magic[6];  // "ustar\0"
    char version[2];// "00"
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[PREFIX_SIZE]; // maybe not end with '\0' if just the strlen(path) == 155
    char padding[12]; // fill to 512-bytes
} _tar_header;
const char _null_block[BLOCKSIZE] = { 0 };

size_t _tar_checksum(const _tar_header* hdr) {
    unsigned char* p = (unsigned char*) hdr;
    size_t sum = 0;
    for (int i = 0; i < 148; i++) {
        sum += *p++;
    }
    sum += ' ' * (156 - 148);
    p = (unsigned char*) & hdr->typeflag;
    for (int i = 156; i < BLOCKSIZE; i++) {
        sum += *p++;
    }
    return sum;
}

// --------------------------------------------------------------------------
// tar dir
// --------------------------------------------------------------------------

bool _int2OctalStr(size_t value, char* str, int bufSize) {
    char* p = str + bufSize - 1;
    *p-- = '\0';
    for (;; p--) {
        *p = (value & 0x07) | '0';
        value >>= 3;
        if (value == 0) break;
        else if (p == str) return false; // value is too large, buf size is too small
    }
    for (p--; p >= str; p--) *p = '0';
    return true;
}

void _tarDir_writeNormalHeader(FILE *fp, _tar_header* hdr) {
    memcpy(hdr->magic, "ustar", sizeof(hdr->magic));
    memcpy(hdr->version, "00", sizeof(hdr->version));
    size_t checkSum = _tar_checksum(hdr);
    _int2OctalStr(checkSum, hdr->chksum, sizeof(hdr->chksum));
    fwrite(hdr, 1, BLOCKSIZE, fp); // write normal header
}

void _tarDir_writeBlockPadding(FILE* fp, size_t dataSize) {
    // write 0 values to fill the rest of the block
    size_t totalSize = _round_up_to_512(dataSize);
    size_t nullSize = totalSize - dataSize;
    if (nullSize > 0) fwrite(_null_block, 1, nullSize, fp); // fill 0 to align up to 512-bytes
}

int _tarDir_writeGnuTarHeader(FILE* fp, char typeflag, size_t strSize, const char* str) {
    strSize++; // including '\0'

    // write GNU tar header
    _tar_header hdr = { 0 };
    hdr.typeflag = typeflag;

    _int2OctalStr(strSize, hdr.size, sizeof(hdr.size));
    _tarDir_writeNormalHeader(fp, &hdr);

    // write string blocks
    fwrite(str, 1, strSize, fp); // also write '\0' in the end
    _tarDir_writeBlockPadding(fp, strSize);
    return 0;
}

void _tarDir_writePaxHeader_writeLine(FILE *fp, const char *key, const char *value, size_t lineSize) {
    // write line content: "<lineSize> <key>=<value>\n"
    size_t valueLen = strlen(value); // TODO: performance: this calc twice...
    char buf[128];
    int bufLen = snprintf(buf, sizeof(buf), "%d %s=", (int)lineSize, key);
    fwrite(buf, 1, bufLen, fp);
    fwrite(value, 1, valueLen, fp);
    fwrite("\n", 1, 1, fp);
}

void _tarDir_writePaxHeader_writeLine_IntValue(FILE* fp, const char* key, size_t value, size_t lineSize) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%d", (int)value);
    _tarDir_writePaxHeader_writeLine(fp, key, buf, lineSize);
}

size_t _tarDir_writePaxHeader_writeLine_getLineSize(const char* key, const char* value, size_t keyLen) {
    // write line content: "<lineSize> <key>=<value>\n"
    size_t valueLen = strlen(value);
    size_t lineSize = keyLen + valueLen + 2; // size including " key=value\n", excluding "<lineSIze>

    if (lineSize < 10) {
        if (lineSize >= 9) lineSize++;
        lineSize += 1;
    }
    else if (lineSize < 100) {
        if (lineSize >= 98) lineSize++;
        lineSize += 2;
    }
    else if (lineSize < 1000) {
        if (lineSize >= 997) lineSize++;
        lineSize += 3;
    }
    else if (lineSize < 10000) {
        if (lineSize >= 9996) lineSize++;
        lineSize += 4;
    }
    else if (lineSize < 100000) {
        if (lineSize >= 99995) lineSize++;
        lineSize += 5;
    }
    return lineSize;
}

size_t _tarDir_writePaxHeader_writeLine_IntValue_getLineSize(const char* key, size_t value, size_t keyLen) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%d", (int)value);
    return _tarDir_writePaxHeader_writeLine_getLineSize(key, buf, keyLen);
}

void _tarDir_writePaxHeader(FILE* fp, const char* path, size_t size) {
    // TODO: maybe buggy.............
    // <lineSize> <key>=<value>\n
    size_t lineSize_path = 0;
    size_t lineSize_size = 0;
    size_t paxSize = 0;

    // calc pax content size first
    if (path) {
        lineSize_path = _tarDir_writePaxHeader_writeLine_getLineSize("path", path, sizeof("path"));
        paxSize += lineSize_path;
    }
    if (size > 0) {
        lineSize_size = _tarDir_writePaxHeader_writeLine_IntValue_getLineSize("size", size, sizeof("size"));
        paxSize += lineSize_size;
    }
 
    // write pax header
    _tar_header hdr = { 0 };
    hdr.typeflag = 'x';
    _int2OctalStr(paxSize, hdr.size, sizeof(hdr.size));
    _tarDir_writeNormalHeader(fp, &hdr);

    // write pax content
    if (path) {
        _tarDir_writePaxHeader_writeLine(fp, "path", path, lineSize_path);
    }
    if (size > 0) {
        _tarDir_writePaxHeader_writeLine_IntValue(fp, "size", size, lineSize_size);
    }

    _tarDir_writeBlockPadding(fp, paxSize);
}

bool _tarDir_hdr_writePath(FILE *fp, _tar_header* hdr, const char* relPath, bool usePaxHdr) {
    char* pLastSeparator = NULL;
    char* pEnd = (char*)relPath;
    for (; *pEnd != '\0' ; pEnd++) {
        if (*pEnd == ZIP_PATH_SEPARATOR) pLastSeparator = pEnd;
    }
    size_t lenFullpath = pEnd - relPath;
    size_t lenPrefix = pLastSeparator ? pLastSeparator - relPath : 0;
    size_t lenFilename = pLastSeparator ? pEnd - (pLastSeparator) - 1 : lenFullpath;
    if (lenFullpath < sizeof(hdr->name)) {
        // full path is short enough to save into hdr->name
        memcpy(hdr->name, relPath, lenFullpath + 1);
        hdr->prefix[0] = '\0';
    } else if (pLastSeparator != NULL && lenPrefix < sizeof(hdr->prefix) && lenFilename < sizeof(hdr->name)) {
        // prefix & filename is short enough to save into hdr->prefix & hdr->name
        *pLastSeparator = '\0';
        memcpy(hdr->prefix, relPath, lenPrefix + 1);
        *pLastSeparator = ZIP_PATH_SEPARATOR;
        memcpy(hdr->name, pLastSeparator + 1, lenFilename + 1);
    }
    else {
        // 'relPath' is too long to save into legacy header '_tar_header'
        hdr->prefix[0] = '\0';
        hdr->name[0] = '\0';

        // save long file path into extended header
        if (usePaxHdr) {
            return false; // not handled
        } else {
            _tarDir_writeGnuTarHeader(fp, 'L', lenFullpath, relPath);
        } 
    }

    return true; // handled
}

int _tarDir_writeFileContent(FILE* fpOut, const char* path) {
    FILE* fpSrc = NULL;
    _my_file_fopen(&fpSrc, path, "rb");
    if (!fpSrc) return -1;

    char buf[1024 * 16];
    size_t writtenLen = 0;
    while (1) {
        size_t len = fread(buf, 1, sizeof(buf), fpSrc);
        if (len < 0) {
            fclose(fpSrc);
            return -1;
        }
        if (len == 0) break;
        fwrite(buf, 1, len, fpOut);
        writtenLen += len;
    }

    fclose(fpSrc);
    _tarDir_writeBlockPadding(fpOut, writtenLen); // align up to 512-bytes
    printf("+++ add file: %s\n", path);
    return 0;
}

int _my_tarDir_onFileFound(const char* path, const char* relPath, NATIVE_FILE_STAT* st, void* param) {
    FILE* fp = (FILE*)param;
    int err = 0;

    _tar_header hdr = { 0 };
    memcpy(hdr.mode, "0000644\0", 8);
    size_t fileSize = 0;

    bool bForcePaxHeader = true;
    bool isNormalFile = false;
    bool saveSizeIntoPax = false;
    if (S_ISDIR(st->st_mode)) {
        hdr.typeflag = '5';
        hdr.size[0] = '\0';
        // fileSize = 0;
    }
    else if (S_ISREG(st->st_mode)) {        
        hdr.typeflag = '0';
        isNormalFile = true;
        fileSize = st->st_size;
        saveSizeIntoPax = !_int2OctalStr(fileSize, hdr.size, sizeof(hdr.size));
    }
    else {
        return 0; // ignore others
    }
    bool toWritePathIntoPax = !_tarDir_hdr_writePath(fp, &hdr, relPath, bForcePaxHeader | saveSizeIntoPax);

    _int2OctalStr(st->st_mtime, hdr.mtime, sizeof(hdr.mtime));
    // prefix, name, mtime, size, chksum, uid, gid, permission

    // write pax header if need
    if (saveSizeIntoPax || toWritePathIntoPax) {
        _tarDir_writePaxHeader(fp,
            toWritePathIntoPax ? relPath : NULL,
            saveSizeIntoPax ? fileSize : 0);
    }

    _tarDir_writeNormalHeader(fp, &hdr);
    if (isNormalFile) err = _tarDir_writeFileContent(fp, path);
    if (err != 0) {
        err = -1;
    }
    return err;
}

int tarDir(char* dirPath, char* tarPath, bool bSkipTopLevel) {
    FILE* fp = NULL;
    _my_file_fopen(&fp, tarPath, "wb");
    if (!fp)  return -1;
    
    int err = my_dir_traversal(dirPath, "", bSkipTopLevel, _my_tarDir_onFileFound, fp);
    if (err != 0) {
        fclose(fp);
        return err;
    }
    
    // write 2 null blocks to indicate no more file
    for (int i=0; i<2; i++) fwrite(_null_block, 1, BLOCKSIZE, fp);

    fclose(fp);
    return err;
}

// --------------------------------------------------------------------------
// untar to dir
// --------------------------------------------------------------------------

int _pax_tar_header_read_values(FILE *tar, size_t size, HashMap *pMap) {
    if (size == 0) return 0;
    size = _round_up_to_512(size);
    char* content = (char*)malloc(size);
    fread(content, 1, size, tar);

    // read key-value pairs
    char* p = content;
    char* key = NULL;
    while (1) { 
        // get line size
        size_t lineSize = strtol(p, &p, 10);
        for (; *p == ' '; p++); // skip all space chars
        if (*p == '\0') break; // end of content
        if (lineSize == 0) break; // wrong content
        
        // get key
        char* keyBegin = p;
        for (; isalpha(*p); p++); // skip all alpha chars
        if (*p == '\0') break; // end of content
        if (*p != '=') break; // wrong content
        char* keyEnd = p;
        *p = '\0';
        key = strdup(keyBegin);
        for (p++; *p == ' '; p++); // skip all space chars
        if (*p == '\0') break; // end of content

        char** ppValue = (char**) hashmap_find(pMap, key);
        if (ppValue) {
            // we are interested in this key-value
            char* value = (char*)malloc(lineSize);
            
            char* valueEnd = value + lineSize - 1;
            char* v = value;
            for (; *p != '\n' && *p != '\0' && p != valueEnd;) { // copy until line end
                *v++ = *p++;
            }
            *v = '\0';
            *ppValue = value;

            for (; *p != '\n' && *p != '\0';); // skip to line end
        }
        else {
            // ignore this key-value
            for (; *p != '\n' && *p != '\0'; p++); // skip to line end
        }
        FREEIF(key);

        if (*p == '\0') break; // end of content
        p++; // read next line
    }

    if (key != NULL) free(key);
    free(content);
    return 0;
}

char* _gnu_tar_header_read_long_string(FILE* tar, size_t size) {
    if (size == 0) return NULL;
    size = _round_up_to_512(size);

    char* str = (char*)malloc(size);
    if (str == NULL) return NULL;
    fread(str, 1, size, tar);
    return str;
}
int _octal2int(char* str, int bufSize) {
    int v = 0;
    char ch = *str;
    for (int i = 0; i < bufSize && (ch & 0xF8) == 0x30; str++, ch = *str) { // (ch & 0xF8) == 0x30  equals  ch >= '0' && ch < '8'
        v = (v << 3) | (ch & 0x07); // v = (v << 3) | (ch - '0');
    }
    return v;
}

typedef struct {
    char* path;
    char* linkPath;
} _my_tar_ext_fields;

int _my_tar_write_file(FILE* tar, char* path, size_t size) {
    _my_dir_mkdirs_for_file(path);

    char buf[1024 * 8];
    size_t paddingSize = _round_up_to_512(size) - size;
    FILE* fp;
    _my_file_fopen(&fp, path, "wb");
    if (fp == NULL) return -1;
    while (size > 0) {
        size_t tryToReadLen = min(size, sizeof(buf));
        size_t len = fread(buf, 1, min(size, sizeof(buf)), tar);
        if (len < 0) {
            fclose(fp);
            return -1;
        }

        fwrite(buf, 1, len, fp);
        size -= len;
    }

    fclose(fp);
    fseek(tar, (long)paddingSize, SEEK_CUR);
    return 0;
}

int _my_tar_validate_header(_tar_header* hdr) {
    size_t chksum = _tar_checksum(hdr);
    return chksum == _octal2int(hdr->chksum, sizeof(hdr->chksum)) ? 0 : -1;
}

int untarToDir(char* tarPath, char *dirPath) {
    FILE* tar;
    _my_file_fopen(&tar, tarPath, "rb");
    if (!tar) {
        perror("fopen");
        return 1;
    }

    size_t lenDirPath = strlen(dirPath);
    int cntFile = 0;
    int cntDir = 0;

    HashMap* pMap = hashmap_create(100);
    HashMap* pGlobalMap = hashmap_create(100);
    _my_tar_ext_fields extHeaderFields = { 0 };
    _my_tar_ext_fields extGlobalHeaderFields = { 0 };
    hashmap_insert(pMap, strdup("path"), (void*)&extHeaderFields.path);
    hashmap_insert(pGlobalMap, strdup("path"), (void*)&extGlobalHeaderFields.path);

    char buf[BLOCKSIZE];
    char shortPath[NAME_SIZE + PREFIX_SIZE + 3];
    char* pLongname = NULL;
    char* pLongLinkname = NULL;
    bool hasExtendedHeader = false;

    while (fread(buf, 1, BLOCKSIZE, tar) == BLOCKSIZE) {
        _tar_header* hdr = (_tar_header*)buf;

        if (hdr->size[0] == 0 && hdr->name[0] == 0 && hdr->typeflag == 0) {
            printf("tar file end\n");
            break;
        }

        if (_my_tar_validate_header(hdr) != 0) {
            printf("tar header checksum wrong\n");
            fclose(tar);
            return -1;
        }

        // parse headers
        size_t size = _octal2int(hdr->size, sizeof(hdr->size));
        switch (hdr->typeflag) {
        case 0:
        case '0':
        case '5':
            break;
        case 'L': // GNU long filename header
            pLongname = _gnu_tar_header_read_long_string(tar, size);
            hasExtendedHeader = true;
            continue; // read next header
        case 'K': // GNU long linkname header
            pLongLinkname = _gnu_tar_header_read_long_string(tar, size);
            hasExtendedHeader = true;
            continue;
        case 'x': // PAX header
            _pax_tar_header_read_values(tar, size, pMap);
            hasExtendedHeader = true;
            continue; // read next header
        case 'g': // global PAX header
            _pax_tar_header_read_values(tar, size, pGlobalMap);
            //hasExtendedHeader = true;
            continue; // read next header
        default:
            hasExtendedHeader = false;
            break;
        //case '0': // normal file
        //case 0: // normal file
        //case '5': // directory
        }


        // get full path
        char *relPath = NULL;
        if (hasExtendedHeader) {
            if (pLongname) {
                relPath = pLongname;
            }
            else if (extHeaderFields.path) {
                relPath = extHeaderFields.path;
            }
            else {
                printf("not found long name");
                break;
            }
        }
        else {
            char chm = hdr->mode[0];
            hdr->mode[0] = '\0'; // ensure 'name' ends with '\0'
            if (hdr->prefix[0] == '\0') {
                my_strncpy(shortPath, hdr->name, sizeof(shortPath));
                relPath = shortPath;
            }
            else {
                hdr->padding[0] = '\0'; // ensure 'prefix' ends with '\0'
                snprintf(shortPath, sizeof(shortPath), "%s/%s", hdr->prefix, hdr->name);
                relPath = shortPath;
            }
            hdr->mode[0] = chm;
        }


        char* pFullpath = NULL;
        switch (hdr->typeflag) {
        case 0:
        case '0':
        case '5':
            if (strcmp(relPath, "..") == 0
                || strcmp(relPath, "../") == 0
                || strstr(relPath, "/../") != NULL) {
                // ".." dir name is disallowed, to prevent writing files out of 'dirPath'
                break;
            }

#ifdef _WIN32
            // tar path separator is '/', windows path separator is '\\'
            // fix path separator in windows ( change '/' to '\\' )
            for (char* p = relPath; *p != '\0'; p++) {
                if (*p == '/') *p = '\\';
            }
#endif

            size_t lenRelPath = strlen(relPath);
            size_t lenFullpath = lenDirPath + 3 + lenRelPath;
            pFullpath = (char*)malloc(lenFullpath);
            snprintf(pFullpath, lenFullpath, "%s%c%s", dirPath, DIR_SEPARATOR, relPath);

            if (hdr->typeflag == '5') {
                _my_dir_mkdirs(pFullpath);
            }
            else {
                printf("saving file: %s\n", pFullpath);
                _my_tar_write_file(tar, pFullpath, size); // write file
            }
            // TODO: save file uid / pid / permission / etc...

            break;
        }
        FREEIF(pFullpath);

        // cleanup
        hasExtendedHeader = false;
        FREEIF(pLongname);
        FREEIF(pLongLinkname);
        FREEIF(extHeaderFields.path);
        FREEIF(extHeaderFields.linkPath);
        FREEIF(extGlobalHeaderFields.path);
        FREEIF(extGlobalHeaderFields.linkPath);
    }

    printf("total dir count : %d, file count : %d", cntDir, cntFile);

    fclose(tar);
    hashmap_free(pMap, NULL); // TODO: set free func
    hashmap_free(pGlobalMap, NULL); // TODO: set free func
    return 0;
}
