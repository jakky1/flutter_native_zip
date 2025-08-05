#include "native_zip.h"
#include "my_thread.h"
#include "my_message_queue.h"
#include "my_common.h"

#include <stdarg.h>
#include <stdio.h>

MessageQueue _dartQueue;
void _initDartMQ() {
    // TODO: use mutex ?
    static int isInited = 0;
    if (!isInited) {
        mq_init(&_dartQueue);
        isInited = 1;
    }
}

void _notifyDartEvent(int taskId, DartNotifyAction action, int errCode, char *errMsg) {
    _initDartMQ();

    NativeNotifyMessage *msg = (NativeNotifyMessage*) malloc(sizeof(NativeNotifyMessage));
    msg->taskId = taskId;
    msg->action = action;
    msg->errCode = errCode;
    msg->errMsg = errMsg ? strdup(errMsg) : NULL;
    //_dart_mq_enqueue(&_dartMQueue, &msg);
    mq_push(&_dartQueue, msg);
}

// --------------------------------------------------------------------------

void notifyDartTaskFinish(int taskId) {
    _notifyDartEvent(taskId, TASK_FINISH, 0, NULL);
}

void notifyDartTaskWarning(int taskId, int errCode, char *errMsg) {
    _notifyDartEvent(taskId, TASK_WARNING, errCode, errMsg);
}

void notifyDartTaskError(int taskId, int errCode, char* errMsg) {
    _notifyDartEvent(taskId, TASK_ERROR, errCode, errMsg);
}

void notifyDartLog(const char* msg) {
    // [msg] cannot be in heap, because dart isolate cannot access heap created in other thread
    _notifyDartEvent(-1, TASK_LOG, 0, (char*)msg);
}

void notifyDartPrintf(const char* format, ...) {
    va_list args;
    va_start(args, format);

    char buf[1024];
    vsnprintf(buf, sizeof(buf), format, args);
    notifyDartLog(buf);

    va_end(args);
}

// --------------------------------------------------------------------------

FFI_PLUGIN_EXPORT NativeNotifyMessage* getDartMessage() {
    _initDartMQ();
    static NativeNotifyMessage *msg = NULL;

    // clear the previous data
    if (msg) {
        if (msg->errMsg) free(msg->errMsg);
        free(msg);
        msg = NULL;
    }

    // set timeout 1000 ms to avoid dart isolate blocking, which cause flutter hot-reload disabled
    msg = (NativeNotifyMessage*) mq_pop_timeout(&_dartQueue, 1000);
    if (msg == NULL) return NULL;
    return msg;
}

static int _last_task_id = MIN_TASK_ID;
int generateTaskId() {
    return _last_task_id++;
}