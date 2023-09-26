#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <windows.h>

#include "npapi/npapi.h"
#include "npapi/npfunctions.h"
#include "npapi/npruntime.h"
#include "npapi/nptypes.h"

#include "ffrunner.h"

typedef struct Request Request;
struct Request {
    void *notifyData;
    char *mimeType;
    bool doNotify;
    char url[MAX_URL_LENGTH];
    uint8_t data[REQUEST_BUFFER_SIZE]; // TODO: move to stack
};

Request requests[MAX_CONCURRENT_REQUESTS];
int nrequests;

char *rewrite_mappings[][2] = { // TODO: align
    "http://cdn.dexlabs.systems/ff/big/beta-20100104/main.unity3d", "assets/main.unity3d",
    "rankurl.txt", "assets/rankurl.txt",
    "assetInfo.php", "assets/assetInfo.php",
    "loginInfo.php", "assets/loginInfo.php",
    "images.php", "assets/images.php",
    "sponsor.php", "assets/sponsor.php",
};

char *
rewrite_url(char *input)
{
    int i;

    for (i = 0; i < ARRLEN(rewrite_mappings); i++)
        if (strcmp(input, rewrite_mappings[i][0]) == 0)
            return rewrite_mappings[i][1];

    return input;
}

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
        .url = req->url,
        .notifyData = req->notifyData,
    };

    char *path = rewrite_url(req->url);

    f = fopen(path, "rb");
    if (!f) {
        perror(path);
        goto failEarly;
    }

    // seek for ftell()
    ret = fseek(f, 0, SEEK_END);
    if (ret < 0) {
        perror("fseek");
        goto failWithOpenFile;
    }
    npstream.end = ftell(f);

    // seek back to start
    rewind(f);

    printf("> NPP_NewStream %s\n", req->url);
    npErr = pluginFuncs.newstream(&npp, (char*)req->mimeType, &npstream, 0, &streamtype); // TODO: set seekable?
    printf("returned %d\n", npErr);
    if (npErr != NPERR_NO_ERROR) {
        goto failWithOpenFile;
    }
    assert(streamtype == NP_NORMAL);

    for (offset = 0; offset < npstream.end; offset += bytesRead) {
        //printf("> NPP_WriteReady\n");
        wantbufsize = pluginFuncs.writeready(&npp, &npstream);
        //printf("returned %d\n", wantbufsize);

        // pick the smallest buffer size out of hardcoded, plugin-desired and bytes left in file
        wantbufsize = MIN(MIN(wantbufsize, npstream.end - offset), REQUEST_BUFFER_SIZE);

        //printf("* fread %d bytes at offset %d\n", wantbufsize, offset);
        bytesRead = fread(req->data, 1, wantbufsize, f);
        //printf("returned %d\n", bytesRead);
        if (bytesRead < wantbufsize) {
            if (ferror(f)) {
                perror("fread");
                goto failInStream;
            }
            if (feof(f))
                printf("IS EOF\n");
        }

        // do not write empty buffers
        if (bytesRead == 0)
            break;

        //printf("> NPP_Write %d bytes at offset %d\n", bytesRead, offset);
        bytesWritten = pluginFuncs.write(&npp, &npstream, offset, bytesRead, req->data);
        //printf("returned %d\n", bytesWritten);

        if (bytesWritten < 0 || bytesWritten < bytesRead) {
            goto failInStream;
        }
    }

    printf("* done processing file of size %d\n", offset);

    printf("NPP_DestroyStream %s\n", path);
    pluginFuncs.destroystream(&npp, &npstream, NPRES_DONE);

    fclose(f);

    return;

failInStream:
    printf("NPP_DestroyStream FAIL %s\n", path);
    pluginFuncs.destroystream(&npp, &npstream, NPRES_NETWORK_ERR);

failWithOpenFile:
    fclose(f);

failEarly:
    *res = NPRES_NETWORK_ERR;
}

void
fail_handler(Request *req, NPReason *res)
{
    *res = NPRES_NETWORK_ERR;
}

struct {
    char *pattern;
    char *mimeType;
    void (*handler)(Request*, NPReason*);
} request_handlers[] = {
    "revisions.plist", "UNUSED",                   fail_handler,
    ".png",            "image/png",                file_handler,
    "main.unity3d",    "application/octet-stream", file_handler,
    ".php",            "text/plain",               file_handler,
    ".txt",            "text/plain",               file_handler,
};

void
handle_requests(void)
{
    int i, j;
    NPReason res;

    for (i = 0; i < nrequests; i++) {
        Request *req = &requests[i];

        res = NPRES_DONE;

        for (j = 0; j < ARRLEN(request_handlers); j++) {
            if (strstr(req->url, request_handlers[j].pattern) != NULL) {
                req->mimeType = request_handlers[j].mimeType;
                request_handlers[j].handler(req, &res);
                break;
            }
        }
        // TODO: else default handler

        if (req->doNotify) {
            printf("> NPP_URLNotify %d %s\n", res, req->url);
            pluginFuncs.urlnotify(&npp, req->url, res, req->notifyData);
        }
    }

    // clear all requests
    memset(&requests, 0, sizeof(requests));
    nrequests = 0;
}

void
register_request(const char *url, void *notifyData, bool doNotify)
{
    assert(nrequests - 1 < MAX_CONCURRENT_REQUESTS);

    requests[nrequests] = (Request){
        notifyData,
        "application/octet-stream",
        doNotify
    };

    strncpy(requests[nrequests].url, url, MAX_URL_LENGTH);

    nrequests++;
}
