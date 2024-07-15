#include "ffrunner.h"

#include <stdio.h>

PTP_POOL threadpool;
HINTERNET hInternet;
UINT ioMsg;
char *srcUrl;
size_t nRequests;
bool mainLoaded;

char *
get_post_payload(const char *buf)
{
    const char *term = "\r\n\r\n";
    return strstr(buf, term) + sizeof(term);
}

struct {
    char *pattern;
    char *mimeType;
} MIME_TYPES[] = {
    "unity-dexlabs.png",       "image/png",
    "unity-loadingbar.png",    "image/png",
    "unity-loadingframe.png",  "image/png",
    "main.unity3d",            "application/octet-stream",
    ".php",                    "text/plain",
    ".txt",                    "text/plain",
    ".png",                    "image/png",
};

char *
get_mime_type(char *fileName)
{
    char *pattern;
    for (int i = 0; i < ARRLEN(MIME_TYPES); i++) {
        pattern = MIME_TYPES[i].pattern;
        if (strstr(fileName, pattern) != NULL) {
            return MIME_TYPES[i].mimeType;
        }
    }
    return "application/octet-stream";
}

char *
get_file_name(char *url)
{
    char *pos;
    int len;

    len = strlen(url);
    pos = url + len;
    while (pos > url) {
        pos--;
        if (*pos == '/') {
            return pos + 1;
        }
    }
    return url;
}

void
on_load_ready()
{
    /* load the actual content */
    register_get_request(srcUrl, true, NULL);
}

void cancel_request(Request *req)
{
    req->writePtr = 0;
    req->writeSize = 0;
    req->doneReason = NPRES_NETWORK_ERR;
    req->done = true;
    req->failed = true;
}

bool
handle_io_progress(Request *req)
{
    NPError err;
    uint32_t writeSize;
    int32_t bytesConsumed;
    uint8_t *dataPtr;

    assert(req->source != REQ_SRC_UNSET);

    if (req->stream == NULL && !req->failed) {
        /* start streaming */
        req->stream = (NPStream*)malloc(sizeof(NPStream));
        req->stream->url = req->url;
        req->stream->end = req->sizeHint;
        req->stream->notifyData = req->notifyData;
        log("> NPP_NewStream %s\n", req->url);
        err = pluginFuncs.newstream(&npp, req->mimeType, req->stream, false, &req->streamType);
        log("returned %d\n", err);
        if (err != NPERR_NO_ERROR) {
            cancel_request(req);
        }
    }

    if (req->stream && req->writeSize > 0) {
        /* streaming in progress AND data available */
        log("> NPP_WriteReady %s\n", req->url);
        writeSize = pluginFuncs.writeready(&npp, req->stream);
        log("returned %d\n", writeSize);
        writeSize = MIN(writeSize, req->writeSize);
        
        log("> NPP_Write %s %d %p\n", req->url, writeSize, req->writePtr);
        dataPtr = req->buf + req->writePtr;
        bytesConsumed = pluginFuncs.write(&npp, req->stream, req->bytesWritten, writeSize, dataPtr);
        if (bytesConsumed < 0) {
            log("error %d\n", bytesConsumed);
            cancel_request(req);
        } else {
            req->bytesWritten += bytesConsumed;
            req->writePtr += bytesConsumed;
        }
    }

    if (req->done && req->writePtr == req->writeSize) {
        /* request is cancelled, or complete and all available data has been consumed */
        if (req->stream) {
            log("> NPP_DestroyStream %s %d\n", req->url, req->doneReason);
            err = pluginFuncs.destroystream(&npp, req->stream, req->doneReason);
            log("returned %d\n", err);
            if (err != NPERR_NO_ERROR) {
                return false;
            }
            free(req->stream);
        }

        if (req->doNotify) {
            log("> NPP_UrlNotify %s %d %p\n", req->url, req->doneReason, req->notifyData);
            pluginFuncs.urlnotify(&npp, req->url, req->doneReason, req->notifyData);
        }

        if (req->source == REQ_SRC_FILE && req->handles.hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(req->handles.hFile);
        } 

        if (req->source == REQ_SRC_HTTP) {
            if (req->handles.http.hReq != NULL) {
                InternetCloseHandle(req->handles.http.hReq);
            }
            if (req->handles.http.hConn != NULL) {
                InternetCloseHandle(req->handles.http.hConn);
            }
        }

        nRequests--;
        if (!mainLoaded && nRequests == 0) {
            log("Ready to load main\n");
            on_load_ready();
            mainLoaded = true;
        }

        return false;
    }

    return true;
}

bool
init_request_file(Request *req)
{
    DWORD fileSize;
    char redirectPath[MAX_PATH];

    req->handles.hFile = CreateFileA(req->url, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (req->handles.hFile == INVALID_HANDLE_VALUE) {
        /* Check the assets folder */
        snprintf(redirectPath, MAX_PATH, "assets/%s", req->url);
        req->handles.hFile = CreateFileA(redirectPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    if (req->handles.hFile == INVALID_HANDLE_VALUE) {
        goto fail;
    }

    fileSize = GetFileSize(req->handles.hFile, NULL);
    if (fileSize != INVALID_FILE_SIZE) {
        req->sizeHint = fileSize;
    }

    return true;

fail:
    cancel_request(req);
    return false;
}

bool
init_request_http(Request *req, char *hostname, char *filePath, LPURL_COMPONENTSA urlComponents)
{
    DWORD lenlen;
    DWORD err;
    DWORD status;

    PCTSTR verb = req->isPost ? "POST" : "GET";
    PCTSTR acceptedTypes[2] = { req->mimeType, NULL };
    DWORD flags = 0;

    /* for post */
    LPSTR headers = NULL;
    DWORD headersSz = 0;
    LPSTR payload = NULL;
    DWORD payloadSz = 0;

    if (hInternet == NULL) {
        goto fail;
    }

    req->handles.http.hConn = InternetConnectA(hInternet, hostname, urlComponents->nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!req->handles.http.hConn) {
        log("Failed internetconnect: %d\n", GetLastError());
        goto fail;
    }

    if (urlComponents->nScheme == INTERNET_SCHEME_HTTPS) {
        flags |= INTERNET_FLAG_SECURE;
    }
    log("Verb: %s\nHost: %s\nPort: %d\nObject: %s\n", verb, hostname, urlComponents->nPort, filePath);
    req->handles.http.hReq = HttpOpenRequestA(req->handles.http.hConn, verb, filePath, NULL, NULL, acceptedTypes, flags, 0);
    if (!req->handles.http.hReq) {
        log("Failed httpopen: %d\n", GetLastError());
        goto fail;
    }

    if (req->isPost) {
        headers = req->postData;
        payload = get_post_payload(req->postData);
        headersSz = payload - headers;
        payloadSz = req->postDataLen - headersSz;
    }

    if (!HttpSendRequestA(req->handles.http.hReq, headers, headersSz, payload, payloadSz)) {
        log("Failed httpsend: %d\n", GetLastError());
        goto fail;
    }

    /* Make sure we don't get a 404 */
    lenlen = sizeof(status);
    if (!HttpQueryInfoA(req->handles.http.hReq, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_STATUS_CODE, &status, &lenlen, 0)) {
        log("Failed httpquery (status code): %d\n", GetLastError());
        goto fail;
    }
    if (status != HTTP_STATUS_OK) {
        log("HTTP not OK (%d)\n", status);
        goto fail;
    }

    /* Attempt to get the length from the Content-Length header. */
    /* If we don't know the length, the plugin asks for 0. */
    lenlen = sizeof(req->sizeHint);
    if (!HttpQueryInfoA(req->handles.http.hReq, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_CONTENT_LENGTH, &req->sizeHint, &lenlen, 0)) {
        err = GetLastError();
        if (err != ERROR_HTTP_HEADER_NOT_FOUND) {
            log("Failed httpquery: %d\n", GetLastError());
            goto fail;
        }
        req->sizeHint = 0;
    }

    return true;

fail:
    cancel_request(req);
    return false;
}

bool
init_request(Request *req)
{
    URL_COMPONENTSA urlComponents;
    char hostname[MAX_URL_LENGTH];
    char filePath[MAX_URL_LENGTH];
    char *fileName;

    assert(req->source == REQ_SRC_UNSET);
    assert(req->url != NULL);

    /* setup state */
    fileName = get_file_name(req->url);
    req->mimeType = get_mime_type(fileName);

    /* determine whether the target is local or remote by the url */
    urlComponents = (URL_COMPONENTSA){
        .dwStructSize = sizeof(URL_COMPONENTSA),
        .lpszHostName = hostname,
        .dwHostNameLength = MAX_URL_LENGTH,
        .lpszUrlPath = filePath,
        .dwUrlPathLength = MAX_URL_LENGTH
    };
    if (!InternetCrackUrlA(req->url, strlen(req->url), 0, &urlComponents)) {
        /* local */
        req->source = REQ_SRC_FILE;
        return init_request_file(req);
    } else {
        /* remote */
        req->source = REQ_SRC_HTTP;
        return init_request_http(req, hostname, filePath, &urlComponents);
    }
}

void
progress_request(Request *req)
{
    assert(req->source != REQ_SRC_UNSET);

    if (req->writePtr < req->writeSize) {
        /* waiting for plugin to consume all data */
        return;
    }

    req->writePtr = 0;
    switch (req->source)
    {
    case REQ_SRC_FILE:
        if (!ReadFile(req->handles.hFile, req->buf, REQUEST_BUFFER_SIZE, &req->writeSize, NULL)) {
            cancel_request(req);
        } else {
            req->doneReason = NPRES_DONE;
        }
        req->done = true;
        break;
    case REQ_SRC_HTTP:
        if (!InternetReadFile(req->handles.http.hReq, req->buf, REQUEST_BUFFER_SIZE, &req->writeSize)) {
            cancel_request(req);
        } else if (req->writeSize == 0) {
            /* EOF */
            req->done = true;
            req->doneReason = NPRES_DONE;
        }
        break;
    default:
        log("Bad req src %d\n", req->source);
        exit(1);
    }
}

VOID
CALLBACK
step_request(PTP_CALLBACK_INSTANCE inst, Request *req, PTP_WORK work)
{
    bool running;

    running = true;
    if (req->source == REQ_SRC_UNSET) {
        running = init_request(req);
    }
    if (running) {
        progress_request(req);
    }
    PostThreadMessage(mainThreadId, ioMsg, (WPARAM)NULL, (LPARAM)req);
}

void
submit_request(Request *req)
{
    assert(req->work);
    SubmitThreadpoolWork(req->work);
}

void
register_get_request(const char *url, bool doNotify, void *notifyData)
{
    Request *req;
    PTP_WORK work;

    assert(strlen(url) < MAX_URL_LENGTH);

    req = (Request*)malloc(sizeof(Request));
    work = CreateThreadpoolWork(step_request, req, NULL);
    if (!work) {
        log("Failed to create threadpool work for %s: %d\n", url, GetLastError());
        free(req);
        exit(1);
    }

    *req = (Request){
        .notifyData = notifyData,
        .doNotify = doNotify,
        .isPost = false,
        .postDataLen = 0,
        .work = work
    };
    strncpy(req->url, url, MAX_URL_LENGTH);

    submit_request(req);
    nRequests++;
}

void
register_post_request(const char *url, bool doNotify, void *notifyData, uint32_t postDataLen, const char *postData)
{
    Request *req;
    PTP_WORK work;

    assert(strlen(url) < MAX_URL_LENGTH);

    req = (Request*)malloc(sizeof(Request));
    if (!work) {
        log("Failed to create threadpool work for %s: %d\n", url, GetLastError());
        free(req);
        exit(1);
    }

    *req = (Request){
        .notifyData = notifyData,
        .doNotify = doNotify,
        .isPost = true,
        .postDataLen = postDataLen,
        .work = work
    };
    strncpy(req->url, url, MAX_URL_LENGTH);
    memcpy(req->postData, postData, postDataLen);
    
    submit_request(req);
    nRequests++;
}

void
init_network(char *mainSrcUrl)
{
    srcUrl = mainSrcUrl;
    ioMsg = RegisterWindowMessageA(IO_MSG_NAME);
    if (!ioMsg) {
        log("Failed to register io msg: %d\n", GetLastError());
        exit(1);
    }
    threadpool = CreateThreadpool(NULL);
    hInternet = InternetOpenA(USERAGENT, INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    nRequests = 0;
    mainLoaded = false;
}