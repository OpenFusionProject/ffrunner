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
    FILE *f;
    int ret;
    int32_t wantbufsize;
    size_t bytesRead, bytesWritten, offset;
    uint16_t streamtype;
    NPError npErr;

    NPStream npstream = {
        .url = req->url.c_str(),
        .notifyData = req->notifyData,
    };

    f = fopen(req->url.c_str(), "r");
    if (!f) {
        perror("fopen");
        *res = NPRES_NETWORK_ERR;
        return;
    }

    // seek for ftell()
    ret = fseek(f, 0, SEEK_END);
    if (ret < 0) {
        perror("fseek");
        *res = NPRES_NETWORK_ERR;
        return;
    }
    npstream.end = ftell(f);

    // seek back to start
    ret = fseek(f, 0, SEEK_SET);
    if (ret < 0) {
        perror("fseek");
        *res = NPRES_NETWORK_ERR;
        return;
    }

    printf("> NPP_NewStream %s\n", req->url.c_str());
    npErr = pluginFuncs.newstream(&npp, (char*)req->mimeType.c_str(), &npstream, 0, &streamtype); // TODO: set seekable?
    printf("returned %d\n", npErr);
    assert(streamtype == NP_NORMAL);

    if (npErr != NPERR_NO_ERROR) {
        *res = NPRES_NETWORK_ERR;
        return;
    }

    for (offset = 0; offset < npstream.end; offset += bytesRead) {
        printf("> NPP_WriteReady\n");
        wantbufsize = pluginFuncs.writeready(&npp, &npstream);
        printf("returned %d\n", wantbufsize);

        //
        wantbufsize = MIN(MIN(wantbufsize, npstream.end - offset), REQUEST_BUFFER_SIZE);

        printf("* fread %d bytes at offset %d\n", wantbufsize, offset);
        bytesRead = fread(req->data, wantbufsize, 1, f);
        printf("returned %d\n", bytesRead);
        if (bytesRead < wantbufsize) {
            if (ferror(f)) {
                perror("fread");
                *res = NPRES_NETWORK_ERR;
                goto failInStream;
            }
            if (feof(f))
                printf("IS EOF\n");
        }

        // do not write empty buffers
        if (bytesRead == 0)
            break;

        printf("> NPP_Write %d bytes at offset %d\n", bytesRead, offset);
        bytesWritten = pluginFuncs.write(&npp, &npstream, offset, bytesRead, req->data);
        printf("returned %d\n", bytesWritten);

        if (bytesWritten < 0 || bytesWritten < bytesRead) {
            *res = NPRES_NETWORK_ERR;
            goto failInStream;
        }
    }

    printf("NPP_DestroyStream %s\n", req->url.c_str());
    pluginFuncs.destroystream(&npp, &npstream, NPRES_DONE);

    return;

failInStream:
    printf("NPP_DestroyStream FAIL %s\n", req->url.c_str());
    pluginFuncs.destroystream(&npp, &npstream, NPRES_NETWORK_ERR);
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
    ".php",            "text/plain",               file_handler,
};

void
handle_requests(void)
{
    int i;
    NPReason res;

    for (auto iter = requests.begin(); iter != requests.end(); iter++) {
        auto req = iter->second;

        res = NPRES_DONE;

        for (i = 0; i < ARRLEN(request_handlers); i++) {
            if (req->url.find(request_handlers[i].pattern) != std::string::npos) {
                req->mimeType = request_handlers[i].mimeType;
                request_handlers[i].handler(req, &res);
                break;
            }
        }
        // TODO: else default handler

        printf("> NPP_URLNotify %d %s\n", res, req->url.c_str());
        pluginFuncs.urlnotify(&npp, req->url.c_str(), res, req->notifyData);
    }

    requests.clear();
}

void
register_request(const char *url, void *notifyData)
{
    requests[notifyData] = new Request(std::string(url), notifyData);
}
