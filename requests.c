#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <windows.h>
#include <wininet.h>
//#pragma comment(lib, "Wininet.lib")

#include "npapi/npapi.h"
#include "npapi/npfunctions.h"
#include "npapi/npruntime.h"
#include "npapi/nptypes.h"

#include "ffrunner.h"

typedef struct Request Request;
struct Request {
    void *notifyData;
    bool doNotify;
    char url[MAX_URL_LENGTH];
};

int nrequests;
Request requests[MAX_CONCURRENT_REQUESTS];
uint8_t request_data[REQUEST_BUFFER_SIZE];

HINTERNET hinternet;

char *rewrite_mappings[][2] = {
    SRC_URL,            "assets/main.unity3d",
    "rankurl.txt",      "assets/rankurl.txt",
    "assetInfo.php",    "assets/assetInfo.php",
    "loginInfo.php",    "assets/loginInfo.php",
    "images.php",       "assets/images.php",
    "sponsor.php",      "assets/sponsor.php",
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
file_handler(Request *req, char *mimeType, NPReason *res)
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

    /* seek for ftell() */
    ret = fseek(f, 0, SEEK_END);
    if (ret < 0) {
        perror("fseek");
        goto failWithOpenFile;
    }
    npstream.end = ftell(f);

    /* seek back to start */
    rewind(f);

    printf("> NPP_NewStream %s\n", req->url);
    npErr = pluginFuncs.newstream(&npp, mimeType, &npstream, 0, &streamtype);
    printf("returned %d\n", npErr);
    if (npErr != NPERR_NO_ERROR) {
        goto failWithOpenFile;
    }
    assert(streamtype == NP_NORMAL);

    for (offset = 0; offset < npstream.end; offset += bytesRead) {
        wantbufsize = pluginFuncs.writeready(&npp, &npstream);

        /* Pick the smallest buffer size out of hardcoded, plugin-desired and bytes left in file. */
        wantbufsize = MIN(MIN(wantbufsize, npstream.end - offset), REQUEST_BUFFER_SIZE);

        bytesRead = fread(request_data, 1, wantbufsize, f);
        if (bytesRead < wantbufsize) {
            if (ferror(f)) {
                perror("fread");
                goto failInStream;
            }
            if (feof(f))
                printf("IS EOF\n");
        }

        /* Do not write empty buffers. */
        if (bytesRead == 0)
            break;

        bytesWritten = pluginFuncs.write(&npp, &npstream, offset, bytesRead, request_data);

        if (bytesWritten < 0 || bytesWritten < bytesRead) {
            goto failInStream;
        }
    }

    printf("* done processing file of size %d\n", offset);

    printf("NPP_DestroyStream %s\n", path);
    pluginFuncs.destroystream(&npp, &npstream, NPRES_DONE);

    fclose(f);
    *res = NPRES_DONE;

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
http_handler(Request *req, char *mimeType, NPReason *res)
{
    int ret;
    uint64_t wantbufsize;
    long unsigned int lenlen, bytesRead;
    size_t bytesWritten, offset;
    uint16_t streamtype;
    NPError npErr;
    HINTERNET urlHandle;

    NPStream npstream = {
        .url = req->url,
        .notifyData = req->notifyData,
    };

    urlHandle = InternetOpenUrlA(hinternet, req->url, NULL, 0, 0, 0);
    if (!urlHandle) {
        goto failEarly;
    }

    /* get file size */
    lenlen = sizeof(npstream.end);
    if (!HttpQueryInfoA(urlHandle, HTTP_QUERY_FLAG_NUMBER|HTTP_QUERY_CONTENT_LENGTH, &npstream.end, &lenlen, 0)) {
        goto failWithInternetHandle;
    }
    assert(lenlen == sizeof(npstream.end));

    printf("> NPP_NewStream %s\n", req->url);
    npErr = pluginFuncs.newstream(&npp, mimeType, &npstream, 0, &streamtype);
    printf("returned %d\n", npErr);
    if (npErr != NPERR_NO_ERROR) {
        goto failWithInternetHandle;
    }
    assert(streamtype == NP_NORMAL);

    for (offset = 0; offset < npstream.end; offset += bytesRead) {
        wantbufsize = pluginFuncs.writeready(&npp, &npstream);

        /* Pick the smallest buffer size out of hardcoded, plugin-desired and bytes left in file. */
        wantbufsize = MIN(MIN(wantbufsize, npstream.end - offset), REQUEST_BUFFER_SIZE);

        if (!InternetReadFile(urlHandle, request_data, wantbufsize, &bytesRead)) {
            goto failInStream;
        }

        /* Do not write empty buffers. */
        if (bytesRead == 0)
            break;

        bytesWritten = pluginFuncs.write(&npp, &npstream, offset, bytesRead, request_data);

        if (bytesWritten < 0 || bytesWritten < bytesRead) {
            goto failInStream;
        }
    }

    printf("* done processing file of size %d\n", offset);

    printf("NPP_DestroyStream %s\n", req->url);
    pluginFuncs.destroystream(&npp, &npstream, NPRES_DONE);

    InternetCloseHandle(urlHandle);
    *res = NPRES_DONE;

    return;

failInStream:
    printf("NPP_DestroyStream FAIL %s\n", req->url);
    pluginFuncs.destroystream(&npp, &npstream, NPRES_NETWORK_ERR);

failWithInternetHandle:
    InternetCloseHandle(urlHandle);

failEarly:
    *res = NPRES_NETWORK_ERR;
}

void
fail_handler(Request *req, char *mimeType, NPReason *res)
{
    *res = NPRES_NETWORK_ERR;
}

struct {
    char *pattern;
    char *mimeType;
    void (*handler)(Request*, char*, NPReason*);
} request_handlers[] = {
    "revisions.plist",         "UNUSED",                   fail_handler,
    "turner.com",              "UNUSED",                   fail_handler,
    "unity-dexlabs.png",       "image/png",                file_handler,
    "unity-loadingbar.png",    "image/png",                file_handler,
    "unity-loadingframe.png",  "image/png",                file_handler,
    "main.unity3d",            "application/octet-stream", http_handler,
    ".php",                    "text/plain",               file_handler,
    ".txt",                    "text/plain",               file_handler,
    ".png",                    "image/png",                http_handler,
};

void
handle_requests(void)
{
    int i, j;
    bool hit;
    char *mimeType;
    NPReason res;

    for (i = 0; i < nrequests; i++) {
        Request *req = &requests[i];

        /* defaults */
        res = NPRES_NETWORK_ERR;
        mimeType = "application/octet-stream";
        hit = false;

        for (j = 0; j < ARRLEN(request_handlers); j++) {
            if (strstr(req->url, request_handlers[j].pattern) != NULL) {
                hit = true;
                request_handlers[j].handler(req, request_handlers[j].mimeType, &res);
                break;
            }
        }
        if (!hit)
            http_handler(req, mimeType, &res);

        if (req->doNotify) {
            printf("> NPP_URLNotify %d %s\n", res, req->url);
            pluginFuncs.urlnotify(&npp, req->url, res, req->notifyData);
        }
    }

    /* clear all requests */
    memset(&requests, 0, sizeof(requests));
    nrequests = 0;
}

void
register_request(const char *url, bool doNotify, void *notifyData)
{
    assert(nrequests < MAX_CONCURRENT_REQUESTS);
    assert(strlen(url) < MAX_URL_LENGTH);

    requests[nrequests] = (Request){
        .notifyData = notifyData,
        .doNotify = doNotify
    };
    strncpy(requests[nrequests].url, url, MAX_URL_LENGTH);
    nrequests++;
}

void
init_network()
{
    hinternet = InternetOpenA(USERAGENT, INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    assert(hinternet);
}
