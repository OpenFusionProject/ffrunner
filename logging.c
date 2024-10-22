#include "ffrunner.h"

CRITICAL_SECTION cs;
HANDLE logFile;
bool verbose;
bool initialized = false;

void
init_logging(const char *logPath, bool verbose)
{
    InitializeCriticalSection(&cs);
    if (logPath != NULL) {
        logFile = CreateFileA(logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    } else {
        logFile = INVALID_HANDLE_VALUE;
    }
    initialized = true;
    verbose = verbose;
}

void
dbglogmsg(const char *fmt, ...)
{
    va_list args;
    char buf[4028];
    int len;
    DWORD written;

    if (!initialized) {
        printf("Log called before initialization\n");
        exit(1);
    }

    if (!verbose) {
        return;
    }

    va_start(args, fmt);
    len = vsnprintf(buf, ARRLEN(buf), fmt, args);
    va_end(args);

    EnterCriticalSection(&cs);
    if (logFile != INVALID_HANDLE_VALUE) {
        WriteFile(logFile, buf, len, &written, NULL);
    }
    LeaveCriticalSection(&cs);
}

void
logmsg(const char *fmt, ...)
{
    va_list args;
    char buf[4028];
    int len;
    DWORD written;

    if (!initialized) {
        printf("Log called before initialization\n");
        exit(1);
    }

    va_start(args, fmt);
    len = vsnprintf(buf, ARRLEN(buf), fmt, args);
    va_end(args);

    EnterCriticalSection(&cs);
    if (logFile != INVALID_HANDLE_VALUE) {
        WriteFile(logFile, buf, len, &written, NULL);
    }
    LeaveCriticalSection(&cs);
}
