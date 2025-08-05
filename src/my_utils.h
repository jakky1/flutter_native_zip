#pragma once

#include <stddef.h>

char* my_strncpy(char* dest, const char* src, size_t bufSize);
int _string_startsWith(const char* src, const char* prefix);
int _string_endWith(const char* src, char ch);
