#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "npapi/npapi.h"
#include "npapi/npfunctions.h"
#include "npapi/npruntime.h"
#include "npapi/nptypes.h"

#define USERAGENT "ffrunner"
#define CLASS_NAME "FFWINDOW"
#define REQUEST_BUFFER_SIZE 0x8000
#define MAX_URL_LENGTH 256
#define WIDTH 1280
#define HEIGHT 720
#define SRC_URL "http://cdn.dexlabs.systems/ff/big/beta-20100104/main.unity3d"

#define ARRLEN(x) (sizeof(x)/sizeof(*x))
#define MIN(a, b) (a > b ? b : a)

typedef NPError     (OSCALL *NP_GetEntryPointsFuncOS)(NPPluginFuncs*);
typedef NPError     (OSCALL *NP_InitializeFuncOS)(NPNetscapeFuncs*);
typedef NPError     (OSCALL *NP_ShutdownFuncOS)(void);

extern NPP_t npp;
extern NPPluginFuncs pluginFuncs;
extern NPNetscapeFuncs netscapeFuncs;
extern NPWindow npWin;

extern HANDLE ioReadySig;
extern HANDLE ioProcessedSig;

void handle_requests(void);
void handle_io_event(void);
void register_get_request(const char *url, bool doNotify, void *notifyData);
void register_post_request(const char *url, bool doNotify, void *notifyData, uint32_t postDataLen, const char *postData);
void init_requests(void);
DWORD WINAPI request_loop(LPVOID param);

HWND prepare_window(void);
bool handle_messages(void);

void init_logging();
void log(char *fmt, ...);
