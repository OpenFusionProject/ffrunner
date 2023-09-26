#define USERAGENT "ffrunner"
#define CLASS_NAME "FFWINDOW"
#define REQUEST_BUFFER_SIZE 0x8000
#define MAX_CONCURRENT_REQUESTS 64
#define MAX_URL_LENGTH 256
#define REVISIONS_PLIST "http://webplayer.unity3d.com/autodownload_webplugin_beta/revisions.plist"
#define WIDTH 1280
#define HEIGHT 720

#define ARRLEN(x) (sizeof(x)/sizeof(*x))
#define MIN(a, b) (a > b ? b : a)

extern NPP_t npp;
extern NPPluginFuncs pluginFuncs;
extern NPNetscapeFuncs netscapeFuncs;
extern NPWindow npWin;

void handle_requests(void);
void register_request(const char *url, void *notifyData, bool doNotify);

HWND prepare_window(void);
void message_loop(void);
