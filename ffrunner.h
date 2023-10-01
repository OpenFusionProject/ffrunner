#define USERAGENT "ffrunner"
#define CLASS_NAME "FFWINDOW"
#define REQUEST_BUFFER_SIZE 0x8000
#define MAX_CONCURRENT_REQUESTS 64
#define MAX_URL_LENGTH 256
#define WIDTH 1280
#define HEIGHT 720
#define SRC_URL "http://cdn.dexlabs.systems/ff/big/beta-20100104/main.unity3d"

#define ARRLEN(x) (sizeof(x)/sizeof(*x))
#define MIN(a, b) (a > b ? b : a)

extern NPP_t npp;
extern NPPluginFuncs pluginFuncs;
extern NPNetscapeFuncs netscapeFuncs;
extern NPWindow npWin;

extern bool windowChanged;
extern CRITICAL_SECTION graphicsCrit;

void handle_requests(void);
void register_request(const char *url, bool doNotify, void *notifyData);
void init_network(void);

void message_loop(void);
DWORD WINAPI graphics_thread(void *);
