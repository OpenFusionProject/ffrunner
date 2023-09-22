#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <map>
#include <string>
#include <iterator>
#include <fstream>

#include <windows.h>

#include "npapi/npapi.h"
#include "npapi/npfunctions.h"
#include "npapi/npruntime.h"
#include "npapi/nptypes.h"

#include "ffrunner.h"

std::map<void*, Request*> requests;

void
file_handler(Request *req, NPReason *res)
{

}

void
direct_handler(Request *req, NPReason *res)
{

}

void
fail_handler(Request *req, NPReason *res)
{
    *res = NPRES_NETWORK_ERR;
}

struct {
    std::string pattern;
    std::string mimeType;
    void (*handler)(Request*, NPReason*);
} request_handlers[] = {
    "revisions.plist", "UNUSED",                   fail_handler,
    ".png",            "image/png",                file_handler,
    "main.unity3d",    "application/octet-stream", file_handler,
    ".php",            "text/plain",               direct_handler,
};

void
handle_requests(void)
{
    int i;
    const char *mimeType;
    NPReason res;

    for (auto iter = requests.begin(); iter != requests.end(); iter++) {
        auto req = iter->second;

        res = NPRES_DONE;
        mimeType = "application/octet-stream";

        for (i = 0; i < ARRLEN(request_handlers); i++) {
            if (req->url.find(request_handlers[i].pattern) != std::string::npos) {
                mimeType = request_handlers[i].mimeType.c_str();
                request_handlers[i].handler(req, &res);
                break;
            }
        }
        // TODO: else default handler

        NPStream npstream = {
            .url = req->url.c_str(),
            .notifyData = req->notifyData,
        };

        /* TODO: implement this
        pluginFuncs.newstream(&npp "TODO/MIMETYPE", &npstream, 0, NP_NORMAL);
        pluginFuncs.writeready(&npp, &npstream);
        pluginFuncs.write(&npp, &npstream, ...);
        pluginFuncs.destroystream(&npp, NPRES_DONE);
        */

        printf("> NPP_URLNotify %s\n", req->url.c_str());
        pluginFuncs.urlnotify(&npp, req->url.c_str(), res, req->notifyData);
    }

    requests.clear();
}

void
register_request(const char *url, void *notifyData)
{
    requests[notifyData] = new Request(std::string(url), notifyData);
}
