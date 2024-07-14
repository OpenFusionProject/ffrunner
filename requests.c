#include "ffrunner.h"

#include <stdio.h>
#include <wininet.h>

typedef struct Request Request;
struct Request {
    void *notifyData;
    bool doNotify;
    char url[MAX_URL_LENGTH];
    bool isPost;
    uint32_t postDataLen;
    char postData[REQUEST_BUFFER_SIZE];
};

CRITICAL_SECTION requestsCrit;
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
get_post_payload(const char *buf)
{
    const char *term = "\r\n\r\n";
    return strstr(buf, term) + sizeof(term);
}

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
    uint32_t wantbufsize;
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

    log("> NPP_NewStream %s\n", req->url);
    npErr = pluginFuncs.newstream(&npp, mimeType, &npstream, 0, &streamtype);
    log("returned %d\n", npErr);
    if (npErr != NPERR_NO_ERROR) {
        goto failWithOpenFile;
    }
    assert(streamtype == NP_NORMAL);

    for (offset = 0; offset < npstream.end; offset += bytesRead) {
        wantbufsize = pluginFuncs.writeready(&npp, &npstream);

        /* Pick the smallest buffer size out of hardcoded, plugin-desired and bytes left in file. */
        wantbufsize = MIN(MIN(wantbufsize, npstream.end - offset), REQUEST_BUFFER_SIZE);

        assert(wantbufsize <= REQUEST_BUFFER_SIZE);

        bytesRead = fread(request_data, 1, wantbufsize, f);
        if (bytesRead < wantbufsize) {
            if (ferror(f)) {
                perror("fread");
                goto failInStream;
            }
            if (feof(f))
                log("IS EOF\n");
        }

        /* Do not write empty buffers. */
        if (bytesRead == 0)
            break;

        bytesWritten = pluginFuncs.write(&npp, &npstream, offset, bytesRead, request_data);

        if (bytesWritten < 0 || bytesWritten < bytesRead) {
            goto failInStream;
        }
    }

    log("* done processing file of size %d\n", offset);

    log("NPP_DestroyStream %s\n", path);
    pluginFuncs.destroystream(&npp, &npstream, NPRES_DONE);

    fclose(f);
    *res = NPRES_DONE;

    return;

failInStream:
    log("NPP_DestroyStream FAIL %s\n", path);
    pluginFuncs.destroystream(&npp, &npstream, NPRES_NETWORK_ERR);

failWithOpenFile:
    fclose(f);

failEarly:
    *res = NPRES_NETWORK_ERR;
}

void
http_handler(Request *req, char *mimeType, NPReason *res)
{
    uint64_t wantbufsize;
    long unsigned int lenlen, bytesRead;
    size_t bytesWritten, offset;
    uint32_t lengthHint;
    uint16_t streamtype;
    NPError npErr;
    HINTERNET connHandle;
    char hostname[MAX_URL_LENGTH];
    char filepath[MAX_URL_LENGTH];
    HINTERNET reqHandle;

    NPStream npstream = {
        .url = req->url,
        .notifyData = req->notifyData,
    };

    URL_COMPONENTSA urlComponents = {
        .dwStructSize = sizeof(URL_COMPONENTSA),
        .lpszHostName = hostname,
        .dwHostNameLength = MAX_URL_LENGTH,
        .lpszUrlPath = filepath,
        .dwUrlPathLength = MAX_URL_LENGTH
    };
    lenlen = strlen(req->url);
    if (!InternetCrackUrlA(req->url, lenlen, 0, &urlComponents)) {
        goto failEarly;
    }

    connHandle = InternetConnectA(hinternet, hostname, urlComponents.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!connHandle) {
        log("Failed internetconnect: %d\n", GetLastError());
        goto failEarly;
    }

    /* Create and send the request */
    PCTSTR verb = req->isPost ? "POST" : "GET";
    PCTSTR acceptedTypes[] = { mimeType, NULL };
    DWORD flags = 0;
    if (urlComponents.nScheme == INTERNET_SCHEME_HTTPS) {
        flags |= INTERNET_FLAG_SECURE;
    }
    log("Verb: %s\nHost: %s\nPort: %d\nObject: %s\n", verb, hostname, urlComponents.nPort, filepath);
    reqHandle = HttpOpenRequestA(connHandle, verb, filepath, NULL, NULL, acceptedTypes, flags, 0);
    if (!reqHandle) {
        log("Failed httpopen: %d\n", GetLastError());
        goto failWithConnHandle;
    }

    LPSTR headers = NULL;
    DWORD headersSz = 0;
    LPSTR payload = NULL;
    DWORD payloadSz = 0;
    if (req->isPost) {
        headers = req->postData;
        payload = get_post_payload(req->postData);
        headersSz = payload - headers;
        payloadSz = req->postDataLen - headersSz;
    }
    if (!HttpSendRequestA(reqHandle, headers, headersSz, payload, payloadSz)) {
        log("Failed httpsend: %d\n", GetLastError());
        goto failWithReqHandle;
    }

    /* Attempt to get the length from the Content-Length header. */
    /* If we don't know the length, the plugin asks for 0. */
    lengthHint = 0;
    lenlen = sizeof(lengthHint);
    if (!HttpQueryInfoA(reqHandle, HTTP_QUERY_FLAG_NUMBER|HTTP_QUERY_CONTENT_LENGTH, &lengthHint, &lenlen, 0)
        && GetLastError() != ERROR_HTTP_HEADER_NOT_FOUND) {
        log("Failed httpquery: %d\n", GetLastError());
        goto failWithReqHandle;
    }
    npstream.end = lengthHint;

    log("> NPP_NewStream %s\n", req->url);
    npErr = pluginFuncs.newstream(&npp, mimeType, &npstream, 0, &streamtype);
    log("returned %d\n", npErr);
    if (npErr != NPERR_NO_ERROR) {
        goto failWithReqHandle;
    }
    assert(streamtype == NP_NORMAL);

    for (offset = 0; ; offset += bytesRead) {
        wantbufsize = pluginFuncs.writeready(&npp, &npstream);

        /* Pick the smallest buffer size out of hardcoded, plugin-desired and bytes left in file. */
        wantbufsize = MIN(wantbufsize, REQUEST_BUFFER_SIZE);
        if (npstream.end > 0) {
            wantbufsize = MIN(wantbufsize, npstream.end - offset);
        }

        if (!InternetReadFile(reqHandle, request_data, wantbufsize, &bytesRead)) {
            goto failInStream;
        }

        /* EOF */
        if (bytesRead == 0)
            break;

        bytesWritten = pluginFuncs.write(&npp, &npstream, offset, bytesRead, request_data);

        if (bytesWritten < 0 || bytesWritten < bytesRead) {
            goto failInStream;
        }
    }

    log("* done processing file of size %d\n", offset);

    log("NPP_DestroyStream %s\n", req->url);
    pluginFuncs.destroystream(&npp, &npstream, NPRES_DONE);

    InternetCloseHandle(reqHandle);
    InternetCloseHandle(connHandle);
    *res = NPRES_DONE;

    return;

failInStream:
    log("NPP_DestroyStream FAIL %s\n", req->url);
    pluginFuncs.destroystream(&npp, &npstream, NPRES_NETWORK_ERR);

failWithReqHandle:
    InternetCloseHandle(reqHandle);

failWithConnHandle:
    InternetCloseHandle(connHandle);

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

    EnterCriticalSection(&requestsCrit);

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
            log("> NPP_URLNotify %d %s\n", res, req->url);
            pluginFuncs.urlnotify(&npp, req->url, res, req->notifyData);
        }
    }

    /* clear all requests */
    memset(&requests, 0, sizeof(requests));
    nrequests = 0;

    LeaveCriticalSection(&requestsCrit);
}

void
register_get_request(const char *url, bool doNotify, void *notifyData)
{
    EnterCriticalSection(&requestsCrit);

    assert(nrequests < MAX_CONCURRENT_REQUESTS);
    assert(strlen(url) < MAX_URL_LENGTH);

    requests[nrequests] = (Request){
        .notifyData = notifyData,
        .doNotify = doNotify,
        .isPost = false,
        .postDataLen = 0
    };
    strncpy(requests[nrequests].url, url, MAX_URL_LENGTH);
    nrequests++;

    LeaveCriticalSection(&requestsCrit);
}

void
register_post_request(const char *url, bool doNotify, void *notifyData, uint32_t postDataLen, const char *postData)
{
    assert(nrequests < MAX_CONCURRENT_REQUESTS);
    assert(strlen(url) < MAX_URL_LENGTH);

    requests[nrequests] = (Request){
        .notifyData = notifyData,
        .doNotify = doNotify,
        .isPost = true,
        .postDataLen = postDataLen
    };
    strncpy(requests[nrequests].url, url, MAX_URL_LENGTH);
    strncpy(requests[nrequests].postData, postData, postDataLen);
    requests[nrequests].postData[postDataLen] = '\0';
    nrequests++;
}

void
init_network()
{
    InitializeCriticalSection(&requestsCrit);
    hinternet = InternetOpenA(USERAGENT, INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    assert(hinternet);
}
