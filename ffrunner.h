#define USERAGENT "ffrunner"
#define REQUEST_BUFFER_SIZE 0x8000
#define REVISIONS_PLIST "http://webplayer.unity3d.com/autodownload_webplugin_beta/revisions.plist"

#define ARRLEN(x) (sizeof(x)/sizeof(*x))

struct Request {
    void *notifyData;
    std::string url;
    uint8_t data[REQUEST_BUFFER_SIZE] = {};

    Request(std::string u, void *nd) : url(u), notifyData(nd) {}
};

extern NPP_t npp;
extern NPPluginFuncs pluginFuncs;
extern NPNetscapeFuncs netscapeFuncs;

void handle_requests(void);
void register_request(const char *url, void *notifyData);
