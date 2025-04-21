#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <assert.h>

#include <windows.h>
#include <wininet.h>
#include <shellscalingapi.h>
#include <dxgi.h>

#include "npapi/npapi.h"
#include "npapi/npfunctions.h"
#include "npapi/npruntime.h"
#include "npapi/nptypes.h"

#define LOG_FILE_PATH "ffrunner.log"
#define USERAGENT "ffrunner"
#define CLASS_NAME L"FFWINDOW"
#define IO_MSG_NAME L"FFRunnerIoReady"
#define REQUEST_BUFFER_SIZE 0x8000
#define POST_DATA_SIZE 0x1000
#define MAX_URL_LENGTH 256
#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720

// we default to local server with 104 assets
#define FALLBACK_SRC_URL "http://cdn.dexlabs.systems/ff/big/beta-20100104/main.unity3d"
#define FALLBACK_ASSET_URL "http://cdn.dexlabs.systems/ff/big/beta-20100104/"
#define FALLBACK_SERVER_ADDRESS "127.0.0.1:23000"

#define ARRLEN(x) (sizeof(x)/sizeof(*x))
#define MIN(a, b) (a > b ? b : a)

typedef HRESULT (WINAPI *SetProcessDpiAwarenessFunc)(PROCESS_DPI_AWARENESS awareness);

typedef NPError     (OSCALL *NP_GetEntryPointsFuncOS)(NPPluginFuncs*);
typedef NPError     (OSCALL *NP_InitializeFuncOS)(NPNetscapeFuncs*);
typedef NPError     (OSCALL *NP_ShutdownFuncOS)(void);

enum {
    REQ_SRC_UNSET,
    REQ_SRC_FILE,
    REQ_SRC_HTTP,
    REQ_SRC_CACHE,
    REQ_SRC_MEMORY
};

typedef uint8_t RequestSource;
typedef struct Request Request;
typedef struct Arguments Arguments;

struct Request {
    /* params */
    void *notifyData;
    bool doNotify;
    char originalUrl[MAX_URL_LENGTH];
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
    DWORD sizeHint;
    DWORD writeSize;
    DWORD writePtr;
    DWORD bytesWritten;
    uint8_t buf[REQUEST_BUFFER_SIZE];
    bool done;
    NPReason doneReason;
    bool failed;

    /* handle */
    union {
        HANDLE hFile;
        struct {
            HINTERNET hConn;
            HINTERNET hReq;
        } http;
        char *hData;
    } handles;
};

struct Arguments {
    char *mainPathOrAddress;
    char *logPath;
    char *serverAddress;
    char *assetUrl;
    char *endpointHost;
    char *tegId;
    char *authId;
    uint32_t windowWidth;
    uint32_t windowHeight;
    bool useEndpointLoadingScreen;
    bool verboseLogging;
    bool forceVulkan;
    bool forceOpenGl;
};

extern Arguments args;
extern NPP_t npp;
extern NPPluginFuncs pluginFuncs;
extern NPNetscapeFuncs netscapeFuncs;
extern NPWindow npWin;

extern HWND hwnd;

extern PTP_POOL threadpool;
extern UINT ioMsg;

void register_get_request(const char *url, bool doNotify, void *notifyData);
void register_post_request(const char *url, bool doNotify, void *notifyData, uint32_t postDataLen, const char *postData);
bool handle_io_progress(Request *req);
void submit_request(Request *req);
void init_network(char *mainSrcUrl);

void prepare_window(uint32_t width, uint32_t height);
void show_error_dialog(char *msg);
void open_link(char *url);
void message_loop(void);
void apply_vram_fix(void);

void init_logging(const char *logPath, bool verbose);
void dbglogmsg(const char *fmt, ...);
void logmsg(const char *fmt, ...);
