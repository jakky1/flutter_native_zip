#include "native_zip.h"
#include "my_task_notify.h"

/*
void (*_dartPrint)(const char *const) = NULL;
void dartPrint(const char *format, ...) {
  if (_dartPrint == NULL) return;
  va_list args;
  va_start(args, format);

  char buf[300];
  snprintf(buf, sizeof(buf), format, args);
  _dartPrint(buf);

  va_end(args);
}
FFI_PLUGIN_EXPORT void initDartPrint(void (*printCallback)(const char *const)) {
  _dartPrint = printCallback;
  dartPrint("initDartPrint() OK");
}
*/