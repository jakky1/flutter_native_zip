#pragma once

int generateTaskId();

void notifyDartTaskFinish(int taskId);
void notifyDartTaskWarning(int taskId, int errCode, char* errMsg);
void notifyDartTaskError(int taskId, int errCode, char* errMsg);
void notifyDartLog(const char* msg);
void notifyDartPrintf(const char* format, ...);