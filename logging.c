#include <stdio.h>

#include <windows.h>

#include "ffrunner.h"

CRITICAL_SECTION cs;
HANDLE logFile;

void init_logging(char *logPath) {
    InitializeCriticalSection(&cs);
    logFile = CreateFileA(logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

void log(char *fmt, ...) {
    char buf[256];
    int len;
    DWORD written;
    va_list args;

    va_start(args, fmt);
    len = vsnprintf(buf, ARRLEN(buf), fmt, args);
    va_end(args);

    EnterCriticalSection(&cs);
    if (!logFile) {
        printf("Log called before logging init\n");
        exit(1);
    }
    printf(buf);
    WriteFile(logFile, buf, len, &written, NULL);
    LeaveCriticalSection(&cs);
}