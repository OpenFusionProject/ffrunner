#define USERAGENT "ffrunner"
#define REVISIONS_PLIST "http://webplayer.unity3d.com/autodownload_webplugin_beta/revisions.plist"

#define ARRLEN(x) (sizeof(x)/sizeof(*x))

struct Request {
    bool completed;
    void *notifyData;
    char url[256];
};

extern Request requests[16];
extern int nrequests;

extern NPP_t npp;
extern NPPluginFuncs pluginFuncs;
extern NPNetscapeFuncs netscapeFuncs;

void handle_requests(void);
