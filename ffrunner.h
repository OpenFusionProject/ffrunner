#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <assert.h>

#include <windows.h>
#include <wininet.h>

#include "npapi/npapi.h"
#include "npapi/npfunctions.h"
#include "npapi/npruntime.h"
#include "npapi/nptypes.h"

#define LOG_FILE_PATH "ffrunner.log"
#define USERAGENT "ffrunner"
#define CLASS_NAME "FFWINDOW"
#define IO_MSG_NAME "FFRunnerIoReady"
#define REQUEST_BUFFER_SIZE 0x10000
#define POST_DATA_SIZE 0x1000
#define MAX_URL_LENGTH 256
#define WIDTH 1280
#define HEIGHT 720
#define FALLBACK_SRC_URL "http://cdn.dexlabs.systems/ff/big/beta-20100104/main.unity3d"

#define ARRLEN(x) (sizeof(x)/sizeof(*x))
#define MIN(a, b) (a > b ? b : a)

extern DWORD mainThreadId;
extern NPP_t npp;
extern NPPluginFuncs pluginFuncs;
extern NPNetscapeFuncs netscapeFuncs;
extern NPWindow npWin;

extern PTP_POOL threadpool;
extern UINT ioMsg;

typedef NPError     (OSCALL *NP_GetEntryPointsFuncOS)(NPPluginFuncs*);
typedef NPError     (OSCALL *NP_InitializeFuncOS)(NPNetscapeFuncs*);
typedef NPError     (OSCALL *NP_ShutdownFuncOS)(void);

#define REQ_SRC_UNSET 0
#define REQ_SRC_FILE 1
#define REQ_SRC_HTTP 2
#define REQ_SRC_CACHE 3
typedef uint8_t RequestSource;

typedef struct _Request {
    /* params */
    void *notifyData;
    bool doNotify;
    char url[MAX_URL_LENGTH];
    bool isPost;
    uint32_t postDataLen;
    char postData[POST_DATA_SIZE];
    /* state */
    HANDLE readyEvent;
    char *mimeType;
    RequestSource source;
    NPStream *stream;
    uint16_t streamType;
    size_t sizeHint;
    size_t writeSize;
    size_t writePtr;
    uint32_t bytesWritten;
    uint8_t buf[REQUEST_BUFFER_SIZE];
    bool done;
    NPReason doneReason;
    bool failed;
    union {
        HANDLE hFile;
        struct _NetHandles {
            HINTERNET hConn;
            HINTERNET hReq;
        } http;
    } handles;
} Request;

void register_get_request(const char *url, bool doNotify, void *notifyData);
void register_post_request(const char *url, bool doNotify, void *notifyData, uint32_t postDataLen, const char *postData);
bool handle_io_progress(Request *req);
void submit_request(Request *req);
void init_network(char *mainSrcUrl);

HWND prepare_window(void);
void message_loop(void);

void init_logging(const char *logPath);
void log(const char *fmt, ...);
