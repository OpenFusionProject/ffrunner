#include "ffrunner.h"

#ifdef DEBUG
    #define DEBUG_LOG(fmt, ...) log(fmt, __VA_ARGS__)
#else
    #define DEBUG_LOG(fmt, ...) ;
#endif

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

void
cancel_request(Request *req)
{
    req->writeSize = 0;
    req->doneReason = NPRES_NETWORK_ERR;
    req->done = true;
    req->failed = true;
}

bool
handle_io_progress(Request *req)
{
    NPError err;
    size_t bytesAvailable;
    uint32_t writeSize;
    int32_t bytesConsumed;
    uint8_t *dataPtr;

    assert(req->source != REQ_SRC_UNSET);

    if (req->stream == NULL && !req->failed) {
        /* start streaming */
        req->stream = malloc(sizeof(*req->stream));
        req->stream->url = req->url;
        req->stream->end = req->sizeHint;
        req->stream->notifyData = req->notifyData;
        logmsg("> NPP_NewStream %s\n", req->url);
        err = pluginFuncs.newstream(&npp, req->mimeType, req->stream, false, &req->streamType);
        logmsg("returned %d\n", err);
        if (err != NPERR_NO_ERROR) {
            cancel_request(req);
        }
    }

    bytesAvailable = req->writeSize - req->writePtr;
    if (req->stream && bytesAvailable > 0) {
        /* streaming in progress AND data available */
        DEBUG_LOG("> NPP_WriteReady %s %d\n", req->url, bytesAvailable);
        writeSize = pluginFuncs.writeready(&npp, req->stream);
        writeSize = MIN(writeSize, bytesAvailable);

        DEBUG_LOG("> NPP_Write %s %d\n", req->url, writeSize);
        dataPtr = req->buf + req->writePtr;
        bytesConsumed = pluginFuncs.write(&npp, req->stream, req->bytesWritten, writeSize, dataPtr);
        if (bytesConsumed < 0) {
            logmsg("write error %d\n", bytesConsumed);
            cancel_request(req);
        } else if ((uint32_t)bytesConsumed < writeSize) {
            logmsg("not enough bytes consumed %d < %d\n", bytesConsumed, writeSize);
            cancel_request(req);
        } else {
            req->bytesWritten += bytesConsumed;
            req->writePtr += bytesConsumed;
        }
    }

    if (req->failed || (req->done && bytesAvailable == 0)) {
        /* request is cancelled or complete */
        if (req->stream) {
            logmsg("> NPP_DestroyStream %s %d\n", req->url, req->doneReason);
            err = pluginFuncs.destroystream(&npp, req->stream, req->doneReason);
            if (err != NPERR_NO_ERROR) {
                logmsg("destroystream error %d\n", err);
            }
            free(req->stream);
        }

        if (req->doNotify) {
            logmsg("> NPP_UrlNotify %s %d %p\n", req->url, req->doneReason, req->notifyData);
            pluginFuncs.urlnotify(&npp, req->url, req->doneReason, req->notifyData);
        }

        if ((req->source == REQ_SRC_FILE || req->source == REQ_SRC_CACHE) && req->handles.hFile != INVALID_HANDLE_VALUE) {
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
            logmsg("Ready to load main\n");
            on_load_ready();
            mainLoaded = true;
        }

        return true;
    }

    return false;
}

void
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

    return;

fail:
    cancel_request(req);
}

bool
try_init_from_cache(Request *req)
{
    DWORD lenlen;
    DWORD err;
    HANDLE hFile;
    INTERNET_CACHE_ENTRY_INFOA *cacheData;
    bool success;

    success = false;
    lenlen = sizeof(INTERNET_CACHE_ENTRY_INFOA) + MAX_URL_LENGTH + MAX_PATH;
    cacheData = malloc(lenlen);
retry:
    if (!RetrieveUrlCacheEntryFileA(req->url, cacheData, &lenlen, 0)) {
        err = GetLastError();
        if (err == ERROR_INSUFFICIENT_BUFFER) {
            cacheData = realloc(cacheData, lenlen);
            goto retry;
        }
        assert(err == ERROR_FILE_NOT_FOUND);
        goto cleanup;
    }

    hFile = CreateFileA(cacheData->lpszLocalFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        goto cleanup;
    }

    req->handles.hFile = hFile;
    req->source = REQ_SRC_CACHE;
    req->sizeHint = cacheData->dwSizeLow;
    success = true;

cleanup:
    free(cacheData);
    return success;
}

void
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

    if (!req->isPost) {
        /*
         * HACK: Wine's implementation of wininet doesn't support
         * transparently reading from the cache, so we attempt to
         * explicitly read from it before falling back to HTTP.
         */
        if (try_init_from_cache(req)) {
            return;
        }
    }

    if (hInternet == NULL) {
        goto fail;
    }

    req->handles.http.hConn = InternetConnectA(hInternet, hostname, urlComponents->nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!req->handles.http.hConn) {
        logmsg("Failed internetconnect: %d\n", GetLastError());
        goto fail;
    }

    if (urlComponents->nScheme == INTERNET_SCHEME_HTTPS) {
        flags |= INTERNET_FLAG_SECURE;
    }
    logmsg("Verb: %s\nHost: %s\nPort: %d\nObject: %s\n", verb, hostname, urlComponents->nPort, filePath);
    req->handles.http.hReq = HttpOpenRequestA(req->handles.http.hConn, verb, filePath, NULL, NULL, acceptedTypes, flags, 0);
    if (!req->handles.http.hReq) {
        logmsg("Failed httpopen: %d\n", GetLastError());
        goto fail;
    }

    if (req->isPost) {
        headers = req->postData;
        payload = get_post_payload(req->postData);
        headersSz = payload - headers;
        payloadSz = req->postDataLen - headersSz;
    }

    if (!HttpSendRequestA(req->handles.http.hReq, headers, headersSz, payload, payloadSz)) {
        logmsg("Failed httpsend: %d\n", GetLastError());
        goto fail;
    }

    /* Make sure we don't get a 404 */
    lenlen = sizeof(status);
    if (!HttpQueryInfoA(req->handles.http.hReq, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_STATUS_CODE, &status, &lenlen, 0)) {
        logmsg("Failed httpquery (status code): %d\n", GetLastError());
        goto fail;
    }
    if (status != HTTP_STATUS_OK) {
        logmsg("HTTP not OK (%d)\n", status);
        goto fail;
    }

    /*
     * Attempt to get the length from the Content-Length header.
     * If we don't know the length, the plugin asks for 0.
     */
    lenlen = sizeof(req->sizeHint);
    if (!HttpQueryInfoA(req->handles.http.hReq, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_CONTENT_LENGTH, &req->sizeHint, &lenlen, 0)) {
        err = GetLastError();
        if (err != ERROR_HTTP_HEADER_NOT_FOUND) {
            logmsg("Failed httpquery: %d\n", GetLastError());
            goto fail;
        }
        req->sizeHint = 0;
    }

    return;

fail:
    cancel_request(req);
}

void
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
        init_request_file(req);
    } else {
        /* remote */
        req->source = REQ_SRC_HTTP;
        init_request_http(req, hostname, filePath, &urlComponents);
    }
}

void
progress_request(Request *req)
{
    assert(req->source != REQ_SRC_UNSET);

    if (req->writePtr != req->writeSize) {
        /* waiting for plugin to consume bytes */
        return;
    }
    req->writePtr = 0;
    req->writeSize = 0;

    switch (req->source) {
    case REQ_SRC_FILE:
    case REQ_SRC_CACHE:
        if (!ReadFile(req->handles.hFile, req->buf, REQUEST_BUFFER_SIZE, &req->writeSize, NULL)) {
            cancel_request(req);
            return;
        }
        break;
    case REQ_SRC_HTTP:
        if (!InternetReadFile(req->handles.http.hReq, req->buf, REQUEST_BUFFER_SIZE, &req->writeSize)) {
            cancel_request(req);
            return;
        }
        break;
    default:
        logmsg("Bad req src %d\n", req->source);
        exit(1);
    }

    if (req->writeSize == 0) {
        /* EOF */
        req->done = true;
        req->doneReason = NPRES_DONE;
    }
}

VOID CALLBACK
handle_request(PTP_CALLBACK_INSTANCE inst, Request *req, PTP_WORK work)
{
    while (!req->done) {
        if (req->source == REQ_SRC_UNSET) {
            init_request(req);
        }
        if (!req->failed) {
            progress_request(req);
        }
        PostThreadMessage(mainThreadId, ioMsg, (WPARAM)NULL, (LPARAM)req);
        WaitForSingleObject(req->readyEvent, INFINITE);
    }
}

void
submit_request(Request *req)
{
    PTP_WORK work;

    req->readyEvent = CreateEventA(NULL, false, false, NULL);
    assert(req->readyEvent);

    work = CreateThreadpoolWork(handle_request, req, NULL);
    assert(work);

    SubmitThreadpoolWork(work);
    nRequests++;
}

void
register_get_request(const char *url, bool doNotify, void *notifyData)
{
    Request *req;

    assert(strlen(url) < MAX_URL_LENGTH);

    req = malloc(sizeof(*req));

    *req = (Request){
        .notifyData = notifyData,
        .doNotify = doNotify,
        .isPost = false,
        .postDataLen = 0
    };
    strncpy(req->url, url, MAX_URL_LENGTH);

    submit_request(req);
}

void
register_post_request(const char *url, bool doNotify, void *notifyData, uint32_t postDataLen, const char *postData)
{
    Request *req;

    assert(strlen(url) < MAX_URL_LENGTH);
    assert(postDataLen < POST_DATA_SIZE);

    req = malloc(sizeof(*req));

    *req = (Request){
        .notifyData = notifyData,
        .doNotify = doNotify,
        .isPost = true,
        .postDataLen = postDataLen
    };
    strncpy(req->url, url, MAX_URL_LENGTH);
    memcpy(req->postData, postData, postDataLen);
    
    submit_request(req);
}

void
init_network(char *mainSrcUrl)
{
    srcUrl = mainSrcUrl;
    ioMsg = RegisterWindowMessageA(IO_MSG_NAME);
    if (!ioMsg) {
        logmsg("Failed to register io msg: %d\n", GetLastError());
        exit(1);
    }
    threadpool = CreateThreadpool(NULL);
    hInternet = InternetOpenA(USERAGENT, INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    nRequests = 0;
    mainLoaded = false;
}
