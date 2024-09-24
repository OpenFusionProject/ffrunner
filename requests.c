#include "ffrunner.h"

#define POST_PAYLOAD_DIVIDER "\r\n\r\n"

PTP_POOL threadpool;
HINTERNET hInternet;
UINT ioMsg;
char *srcUrl;
size_t nRequests;
bool mainLoaded;

bool
bytes_to_hex(char *buf, int bufSz, const char *src)
{
    int i, sz;
    char *target;

    sz = strlen(src);
    if (sz * 2 + 1 >= bufSz) {
        logmsg("bytes_to_hex: buffer not big enough\n");
        return false;
    }

    for (i = 0; i < sz; i ++) {
        target = buf + 2 * i;
        sprintf(target, "%02x", src[i]);
    }

    return true;
}

char *
get_post_payload(const char *buf)
{
    return strstr(buf, POST_PAYLOAD_DIVIDER) + sizeof(POST_PAYLOAD_DIVIDER) - 1;
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

struct {
    char *url;
    char **data;
} IN_MEMORY_SOURCES[] = {
    { "loginInfo.php", &args.serverAddress },
    { "assetInfo.php", &args.assetUrl },
};

char *
get_in_memory_data(char *url)
{
    char *data;
    for (int i = 0; i < ARRLEN(IN_MEMORY_SOURCES); i++) {
        if (strcmp(url, IN_MEMORY_SOURCES[i].url) == 0) {
            data = *IN_MEMORY_SOURCES[i].data;
            if (data != NULL) {
                return data;
            }
        }
    }
    return NULL;
}

void
get_redirected(char *url, char* newUrl)
{
    assert(newUrl != NULL);

    /* redirect loading images if set */
    if (args.useEndpointLoadingScreen && strstr(url, "assets/img") == url) {
        char *rest = url + strlen("assets/img");
        snprintf(newUrl, MAX_URL_LENGTH, "http://%s/launcher/loading%s", args.endpointHost, rest);
        return;
    }

    strcpy(newUrl, url);
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
        memset(req->stream, 0, sizeof(*req->stream));
        req->stream->url = req->originalUrl;
        req->stream->end = req->sizeHint;
        req->stream->notifyData = req->notifyData;
        logmsg("> NPP_NewStream %s\n", req->originalUrl);
        err = pluginFuncs.newstream(&npp, req->mimeType, req->stream, false, &req->streamType);
        logmsg("returned %d\n", err);
        if (err != NPERR_NO_ERROR) {
            cancel_request(req);
        }
    }

    bytesAvailable = req->writeSize - req->writePtr;
    if (req->stream && bytesAvailable > 0) {
        /* streaming in progress AND data available */
        dbglogmsg("> NPP_WriteReady %s %d\n", req->originalUrl, bytesAvailable);
        writeSize = pluginFuncs.writeready(&npp, req->stream);
        writeSize = MIN(writeSize, bytesAvailable);

        dbglogmsg("> NPP_Write %s %d\n", req->originalUrl, writeSize);
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
            logmsg("> NPP_DestroyStream %s %d\n", req->originalUrl, req->doneReason);
            err = pluginFuncs.destroystream(&npp, req->stream, req->doneReason);
            if (err != NPERR_NO_ERROR) {
                logmsg("destroystream error %d\n", err);
            }
            free(req->stream);
        }

        if (req->doNotify) {
            logmsg("> NPP_UrlNotify %s %d %p\n", req->originalUrl, req->doneReason, req->notifyData);
            pluginFuncs.urlnotify(&npp, req->originalUrl, req->doneReason, req->notifyData);
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
            if (req->handles.http.hCache != INVALID_HANDLE_VALUE) {
                CloseHandle(req->handles.http.hCache);
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

    req->handles.hFile = CreateFileA(req->url, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
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
get_cache_file_path_for_url(char *buf, const char *url)
{
    char *fileName;
    int pathLen = sizeof(HTTP_CACHE_PATH) - 1;

    /* prefix with cache path */
    strcpy(buf, HTTP_CACHE_PATH);

    fileName = buf + pathLen;
    return bytes_to_hex(fileName, MAX_PATH - pathLen, url);
}

bool
try_init_from_cache(Request *req)
{
    DWORD lenlen;
    DWORD err;
    char cacheFilePath[MAX_PATH];
    DWORD cacheFileSz, remoteFileSz;
    HANDLE hFile;
    HINTERNET hUrl;
    SYSTEMTIME modifiedTimeSys;
    FILETIME modifiedTimeRemote, modifiedTimeLocal;
    bool success;

    success = false;
    hUrl = NULL;
    hFile = INVALID_HANDLE_VALUE;

    /* create the cache dir if it doesn't exist yet */
    CreateDirectoryA(HTTP_CACHE_PATH, NULL);

    if (!get_cache_file_path_for_url(cacheFilePath, req->url)) {
        goto cleanup;
    }
    //logmsg("For URL:\n%s\nchecking cache for:\n%s\n", req->url, cacheFilePath);

    /* open the cache file */
    hFile = CreateFileA(cacheFilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND) {
            logmsg("Error reading %s from cache: %d\n", req->url, err);
        }
        goto cleanup;
    }

    /* read size for size hint */
    lenlen = GetFileSize(hFile, NULL);
    if (lenlen == INVALID_FILE_SIZE) {
        logmsg("Couldn't read size of cache file for %s\n", req->url);
        goto cleanup;
    }
    cacheFileSz = lenlen;

    /* read last modified time for comparison with remote */
    if (!GetFileTime(hFile, NULL, NULL, &modifiedTimeLocal)) {
        err = GetLastError();
        logmsg("Couldn't get last modified time of cache file for %s: %d\n", req->url, err);
        goto cleanup;
    }

    /* If we have internet... */
    hUrl = InternetOpenUrlA(hInternet, req->url, NULL, 0, 0, (DWORD_PTR)NULL);
    if (hUrl != NULL) {
        /* Check the size of the remote resource. If it's not the same as the cached file size, bail. */
        lenlen = sizeof(remoteFileSz);
        if (HttpQueryInfoA(hUrl, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_CONTENT_LENGTH, &remoteFileSz, &lenlen, 0)
            && remoteFileSz != cacheFileSz) {
            goto cleanup;
        }

        /* Check the last modified time of the resource. If it's newer than what we have cached, bail. */
        lenlen = sizeof(modifiedTimeSys);
        if (HttpQueryInfoA(hUrl, HTTP_QUERY_FLAG_SYSTEMTIME | HTTP_QUERY_LAST_MODIFIED, &modifiedTimeSys, &lenlen, 0)
            && SystemTimeToFileTime(&modifiedTimeSys, &modifiedTimeRemote)
            && CompareFileTime(&modifiedTimeRemote, &modifiedTimeLocal) == 1) {
            goto cleanup;
        }
    }

    req->handles.hFile = hFile;
    req->source = REQ_SRC_CACHE;
    req->sizeHint = cacheFileSz;
    success = true;

cleanup:
    if (hUrl != NULL) {
        InternetCloseHandle(hUrl);
    }

    // we use the file handle for streaming on success,
    // so only close if we're failing out
    if (hFile != INVALID_HANDLE_VALUE && !success) {
        CloseHandle(hFile);
    }

    return success;
}

void
init_request_http(Request *req, char *hostname, char *filePath, INTERNET_SCHEME scheme, INTERNET_PORT port)
{
    DWORD lenlen;
    DWORD err;
    DWORD status;

    char cacheFilePath[MAX_PATH];
    HFILE hCacheFile;

    PCTSTR verb = req->isPost ? "POST" : "GET";
    PCTSTR acceptedTypes[2] = { req->mimeType, NULL };
    DWORD flags = INTERNET_FLAG_RELOAD;

    /* for post */
    LPSTR headers = NULL;
    DWORD headersSz = 0;
    LPSTR payload = NULL;
    DWORD payloadSz = 0;

    if (!req->isPost) {
        if (try_init_from_cache(req)) {
            return;
        }

        /* we want to cache the result of this, so open a handle to the cache */
        if (get_cache_file_path_for_url(cacheFilePath, req->url)) {
            req->handles.http.hCache = CreateFileA(cacheFilePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        }
    }

    if (hInternet == NULL) {
        goto fail;
    }

    req->handles.http.hConn = InternetConnectA(hInternet, hostname, port, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!req->handles.http.hConn) {
        logmsg("Failed internetconnect: %d\n", GetLastError());
        goto fail;
    }

    if (scheme == INTERNET_SCHEME_HTTPS) {
        flags |= INTERNET_FLAG_SECURE;
    }
    logmsg("Verb: %s\nHost: %s\nPort: %d\nObject: %s\n", verb, hostname, port, filePath);
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
    char redirectedUrl[MAX_URL_LENGTH];
    char endpointUrl[MAX_URL_LENGTH];
    URL_COMPONENTSA urlComponents;
    char hostname[MAX_URL_LENGTH];
    char filePath[MAX_URL_LENGTH];
    char *fileName;
    bool fileExists;
    char *inMemoryData;

    assert(req->source == REQ_SRC_UNSET);
    assert(req->originalUrl != NULL);

    /* setup state */
    fileName = get_file_name(req->originalUrl);
    req->mimeType = get_mime_type(fileName);

    /* check for redirects */
    get_redirected(req->originalUrl, redirectedUrl);
    strncpy(req->url, redirectedUrl, MAX_URL_LENGTH);

    /* check for in-memory source first */
    inMemoryData = get_in_memory_data(req->url);
    if (inMemoryData != NULL) {
        dbglogmsg("in-memory data\nurl: %s\ndata: %s\n", req->url, inMemoryData);
        req->source = REQ_SRC_MEMORY;
        req->handles.hData = inMemoryData;
        req->sizeHint = strlen(inMemoryData);
        return;
    }

    /* determine whether the target is local or remote by the url */
    urlComponents = (URL_COMPONENTSA){
        .dwStructSize = sizeof(URL_COMPONENTSA),
        .lpszHostName = hostname,
        .dwHostNameLength = MAX_URL_LENGTH,
        .lpszUrlPath = filePath,
        .dwUrlPathLength = MAX_URL_LENGTH
    };

    if (InternetCrackUrlA(req->url, strlen(req->url), 0, &urlComponents)) {
        if (urlComponents.nScheme == INTERNET_SCHEME_FILE) {
            /* file:/// scheme; translate to file src to avoid wininet overhead */
            strncpy(req->url, filePath, MAX_URL_LENGTH);
            goto init_as_file;
        }

        if (urlComponents.nScheme == INTERNET_SCHEME_HTTP || urlComponents.nScheme == INTERNET_SCHEME_HTTPS) {
            /* remote */
            req->source = REQ_SRC_HTTP;
            init_request_http(req, hostname, filePath, urlComponents.nScheme, urlComponents.nPort);
            return;
        }
    }

     /* assume file path */

init_as_file:

    /* if the file doesn't exist on disk, try getting it from the endpoint */
    if (args.endpointHost != NULL) {
        fileExists = GetFileAttributesA(req->url) != INVALID_FILE_ATTRIBUTES;
        if (!fileExists) {
            snprintf(endpointUrl, MAX_URL_LENGTH, "http://%s/%s", args.endpointHost, fileName);
            /* need to crack the URL again post-fmt */
            if (InternetCrackUrlA(endpointUrl, strlen(endpointUrl), 0, &urlComponents)) {
                logmsg("checking endpoint for %s (http://%s/%s)\n", fileName, args.endpointHost, fileName);
                req->source = REQ_SRC_HTTP;
                init_request_http(req, hostname, filePath, urlComponents.nScheme, urlComponents.nPort);
                return;
            }
        }
    }

    req->source = REQ_SRC_FILE;
    init_request_file(req);
}

void
progress_request(Request *req)
{
    assert(req->source != REQ_SRC_UNSET);

    DWORD cacheBytesWritten;

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

        /* also write to cache */
        if (!WriteFile(req->handles.http.hCache, req->buf, req->writeSize, &cacheBytesWritten, NULL)) {
            /* give up on caching. the size mismatch will invalidate it. */
            logmsg("Failed to write bytes to cache for %s\n", req->url);
            CloseHandle(req->handles.http.hCache);
            req->handles.http.hCache = INVALID_HANDLE_VALUE;
        }
        break;
    case REQ_SRC_MEMORY:
        if (req->bytesWritten == 0) {
            memcpy(req->buf, req->handles.hData, req->sizeHint);
            req->writeSize = req->sizeHint;
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
handle_request(PTP_CALLBACK_INSTANCE inst, void *reqArg, PTP_WORK work)
{
    Request *req;

    req = reqArg;
    while (!req->done) {
        if (req->source == REQ_SRC_UNSET) {
            init_request(req);
        }
        if (!req->failed) {
            progress_request(req);
        }
        PostMessageW(hwnd, ioMsg, (WPARAM)NULL, (LPARAM)req);
        WaitForSingleObject(req->readyEvent, INFINITE);
    }
}

void
submit_request(Request *req)
{
    PTP_WORK work;

    req->readyEvent = CreateEventW(NULL, false, false, NULL);
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
    strncpy(req->originalUrl, url, MAX_URL_LENGTH);

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
    strncpy(req->originalUrl, url, MAX_URL_LENGTH);
    memcpy(req->postData, postData, postDataLen);

    submit_request(req);
}

void
init_network(char *mainSrcUrl)
{
    srcUrl = mainSrcUrl;
    ioMsg = RegisterWindowMessageW(IO_MSG_NAME);
    if (!ioMsg) {
        logmsg("Failed to register io msg: %d\n", GetLastError());
        exit(1);
    }
    threadpool = CreateThreadpool(NULL);
    hInternet = InternetOpenA(USERAGENT, INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    nRequests = 0;
    mainLoaded = false;
}
