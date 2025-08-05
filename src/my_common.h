#pragma once

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#define strdup _strdup
#else
#define FFI_PLUGIN_EXPORT
#endif

#define FREEIF(p) { if (p) {free(p); p = NULL;} }
