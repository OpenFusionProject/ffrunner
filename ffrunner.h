#define USERAGENT "ffrunner"
#define CLASS_NAME "FFWINDOW"
#define REQUEST_BUFFER_SIZE 0x8000
#define REVISIONS_PLIST "http://webplayer.unity3d.com/autodownload_webplugin_beta/revisions.plist"
#define WIDTH 1280
#define HEIGHT 720

#define ARRLEN(x) (sizeof(x)/sizeof(*x))
#define MIN(a, b) (a > b ? b : a)

struct Request {
    void *notifyData;
    std::string url;
    std::string mimeType = "application/octet-stream";
    uint8_t data[REQUEST_BUFFER_SIZE] = {};
    bool doNotify;

    Request(std::string u, void *nd, bool notify) : url(u), notifyData(nd), doNotify(notify) {}
};

extern NPP_t npp;
extern NPPluginFuncs pluginFuncs;
extern NPNetscapeFuncs netscapeFuncs;

void handle_requests(void);
void register_request(const char *url, void *notifyData, bool doNotify);

HWND prepare_window(void);
void message_loop(void);
