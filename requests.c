#include "ffrunner.h"

#define POST_PAYLOAD_DIVIDER "\r\n\r\n"

PTP_POOL threadpool;
HINTERNET hInternet;
UINT ioMsg;
char *srcUrl;
size_t nRequests;
bool mainLoaded;

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
        snprintf(newUrl, MAX_URL_LENGTH, "https://%s/launcher/loading%s", args.endpointHost, rest);
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
    req->doneReason = NPRES_NETWORK_ERR;
    InterlockedExchange(&req->failed, 1);
}

static void
post_io(Request *req, bool final)
{
    if (final) {
        PostMessageW(hwnd, ioMsg, (WPARAM)IO_MSG_FINAL, (LPARAM)req);
    } else if (InterlockedExchange(&req->msgPending, 1) == 0) {
        PostMessageW(hwnd, ioMsg, (WPARAM)IO_MSG_PROGRESS, (LPARAM)req);
    }
}

static RequestChunk *
peek_chunk(Request *req)
{
    RequestChunk *chunk;

    EnterCriticalSection(&req->queueLock);
    chunk = req->queueHead;
    LeaveCriticalSection(&req->queueLock);
    return chunk;
}

static void
pop_chunk(Request *req)
{
    RequestChunk *chunk;

    EnterCriticalSection(&req->queueLock);
    chunk = req->queueHead;
    req->queueHead = chunk->next;
    if (req->queueHead == NULL) {
        req->queueTail = NULL;
    }
    req->queuedBytes -= chunk->size;
    LeaveCriticalSection(&req->queueLock);
    free(chunk);
    SetEvent(req->spaceEvent);
}

bool
handle_io_progress(Request *req, bool final)
{
    NPError err;
    RequestChunk *chunk;
    int32_t bytesReady;
    int32_t bytesConsumed;
    DWORD bytesAvailable;

    assert(req->source != REQ_SRC_UNSET);

    if (req->finished) {
        /* stale message; the worker may re-post the final message */
        return false;
    }
    if (!final) {
        InterlockedExchange(&req->msgPending, 0);
    }

    if (req->stream == NULL
        && !InterlockedCompareExchange(&req->failed, 0, 0)
        && !InterlockedCompareExchange(&req->aborted, 0, 0)) {
        /* start streaming */
        req->stream = malloc(sizeof(*req->stream));
        memset(req->stream, 0, sizeof(*req->stream));
        req->stream->url = req->originalUrl;
        req->stream->end = req->sizeHint;
        req->stream->notifyData = req->notifyData;
        dbglogmsg("> NPP_NewStream %s\n", req->originalUrl);
        err = pluginFuncs.newstream(&npp, req->mimeType, req->stream, false, &req->streamType);
        dbglogmsg("  returned %d\n", err);
        if (err != NPERR_NO_ERROR) {
            InterlockedExchange(&req->aborted, 1);
            SetEvent(req->spaceEvent);
        }
    }

    while ((chunk = peek_chunk(req)) != NULL) {
        if (InterlockedCompareExchange(&req->aborted, 0, 0) || req->stream == NULL) {
            pop_chunk(req);
            continue;
        }
        bytesAvailable = chunk->size - chunk->consumed;
        dbglogmsg("> NPP_WriteReady %s %d\n", req->originalUrl, bytesAvailable);
        bytesReady = pluginFuncs.writeready(&npp, req->stream);
        if (bytesReady <= 0) {
            /* plugin isn't ready; the worker re-posts shortly */
            SetEvent(req->spaceEvent);
            return false;
        }
        bytesReady = MIN((DWORD)bytesReady, bytesAvailable);
        dbglogmsg("> NPP_Write %s %d\n", req->originalUrl, bytesReady);
        bytesConsumed = pluginFuncs.write(&npp, req->stream, req->bytesWritten, bytesReady, chunk->data + chunk->consumed);
        if (bytesConsumed < 0) {
            logmsg("write error %d\n", bytesConsumed);
            InterlockedExchange(&req->aborted, 1);
            SetEvent(req->spaceEvent);
            continue;
        } else if (bytesConsumed < bytesReady) {
            /* plugin promised it would consume offerSize via writeready */
            logmsg("not enough bytes consumed %d < %d\n", bytesConsumed, bytesReady);
            InterlockedExchange(&req->aborted, 1);
            SetEvent(req->spaceEvent);
            continue;
        }
        req->bytesWritten += bytesConsumed;
        chunk->consumed += bytesConsumed;
        if (chunk->consumed == chunk->size) {
            pop_chunk(req);
        }
    }

    if (!final) {
        return false;
    }

    assert(InterlockedCompareExchange(&req->readerDone, 0, 0));
    if (InterlockedCompareExchange(&req->aborted, 0, 0)) {
        cancel_request(req);
    }

    if (req->stream) {
        dbglogmsg("> NPP_DestroyStream %s %d\n", req->originalUrl, req->doneReason);
        err = pluginFuncs.destroystream(&npp, req->stream, req->doneReason);
        if (err != NPERR_NO_ERROR) {
            logmsg("destroystream error %d\n", err);
        }
        free(req->stream);
        req->stream = NULL;
    }

    if (req->doNotify) {
        dbglogmsg("> NPP_UrlNotify %s %d %p\n", req->originalUrl, req->doneReason, req->notifyData);
        pluginFuncs.urlnotify(&npp, req->originalUrl, req->doneReason, req->notifyData);
    }

    complete_request();
    req->finished = true;
    return true;
}

void
release_request(Request *req)
{
    DeleteCriticalSection(&req->queueLock);
    free(req);
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
try_init_from_cache(Request *req, char *hostname, char *filePath, INTERNET_SCHEME scheme, INTERNET_PORT port)
{
    DWORD lenlen;
    DWORD err;
    HANDLE hFile;
    INTERNET_CACHE_ENTRY_INFOA *cacheData;
    HINTERNET hConn;
    HINTERNET hReq;
    SYSTEMTIME modifiedTimeSys;
    FILETIME modifiedTimeFile;
    DWORD contentLength;
    DWORD flags;
    bool success;
    bool hasCacheEntry;
    size_t lockRetryCount;

    success = false;
    hasCacheEntry = false;
    hFile = INVALID_HANDLE_VALUE;
    hConn = NULL;
    hReq = NULL;
    lenlen = sizeof(INTERNET_CACHE_ENTRY_INFOA) + MAX_URL_LENGTH + MAX_PATH;
    cacheData = malloc(lenlen);

    logmsg("Checking cache for %s\n", req->url);

retry:
    if (!RetrieveUrlCacheEntryFileA(req->url, cacheData, &lenlen, 0)) {
        err = GetLastError();
        if (err == ERROR_INSUFFICIENT_BUFFER) {
            cacheData = realloc(cacheData, lenlen);
            goto retry;
        }
        if (err != ERROR_FILE_NOT_FOUND) {
            logmsg("RetrieveUrlCacheEntryFileA returned unexpected err %d\n", err);
        }
        goto cleanup;
    }
    hasCacheEntry = true;

    hFile = CreateFileA(cacheData->lpszLocalFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        goto cleanup;
    }

    /* Send a HEAD request to validate the cached entry without downloading. */
    if (hInternet != NULL) {
        hConn = InternetConnectA(hInternet, hostname, port, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    }
    if (hConn != NULL) {
        flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
        if (scheme == INTERNET_SCHEME_HTTPS) {
            flags |= INTERNET_FLAG_SECURE;
        }
        hReq = HttpOpenRequestA(hConn, "HEAD", filePath, NULL, NULL, NULL, flags, 0);
    }
    if (hReq != NULL && HttpSendRequestA(hReq, NULL, 0, NULL, 0)) {
        /* If the remote file is newer than what we have cached, invalidate. */
        lenlen = sizeof(modifiedTimeSys);
        if (HttpQueryInfoA(hReq, HTTP_QUERY_FLAG_SYSTEMTIME | HTTP_QUERY_LAST_MODIFIED, &modifiedTimeSys, &lenlen, 0)
            && SystemTimeToFileTime(&modifiedTimeSys, &modifiedTimeFile)
            && CompareFileTime(&modifiedTimeFile, &cacheData->LastModifiedTime) == 1) {
            goto cleanup;
        }
    }

    /* Compare actual file size on disk against Content-Length to catch truncation. */
    if (hReq != NULL) {
        DWORD fileSize = GetFileSize(hFile, NULL);
        lenlen = sizeof(contentLength);
        if (fileSize != INVALID_FILE_SIZE
            && HttpQueryInfoA(hReq, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_CONTENT_LENGTH, &contentLength, &lenlen, 0)
            && fileSize != contentLength) {
            logmsg("Cache file truncated for %s: on disk %d, expected %d\n",
                req->url, fileSize, contentLength);
            goto cleanup;
        }
    }

    req->handles.hFile = hFile;
    req->source = REQ_SRC_CACHE;
    req->sizeHint = cacheData->dwSizeLow;
    success = true;

    logmsg("Initialized from cache: %s\n", req->url);
    logmsg("  Cache file: %s\n", cacheData->lpszLocalFileName);

cleanup:
    if (hReq != NULL) {
        InternetCloseHandle(hReq);
    }
    if (hConn != NULL) {
        InternetCloseHandle(hConn);
    }
    if (!success) {
        if (hasCacheEntry) {
            /*
             * The lock count on disk may be polluted, so try to drain a
             * reasonable amount of them here. If we still ultimately fail to
             * unlock, that's fine; it just means the bad cache entry will be checked
             * again on the next request, which will drain the locks even more.
             */
            lockRetryCount = 64;
            while (UnlockUrlCacheEntryFileA(req->url, 0))
            {
                lockRetryCount--;
                if (lockRetryCount == 0) {
                    logmsg("Cache entry for %s is hard locked\n", req->url);
                    break;
                }
            }
            if (DeleteUrlCacheEntryA(req->url)) {
                logmsg("Deleted cache entry for %s\n", req->url);
            } else {
                logmsg("Failed to delete cache entry for %s: %d\n", req->url, GetLastError());
            }
        }

        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            /* deleting the cache entry doesn't delete the file */
            DeleteFileA(cacheData->lpszLocalFileName);
        }
    }
    free(cacheData);
    return success;
}

void
init_request_http(Request *req, char *hostname, char *filePath, INTERNET_SCHEME scheme, INTERNET_PORT port)
{
    DWORD lenlen;
    DWORD err;
    DWORD status;

    PCTSTR verb = req->isPost ? "POST" : "GET";
    PCTSTR acceptedTypes[2] = { req->mimeType, NULL };
    DWORD flags = INTERNET_FLAG_RESYNCHRONIZE;

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
        if (try_init_from_cache(req, hostname, filePath, scheme, port)) {
            return;
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
            snprintf(endpointUrl, MAX_URL_LENGTH, "https://%s/%s", args.endpointHost, fileName);
            /* need to crack the URL again post-fmt */
            if (InternetCrackUrlA(endpointUrl, strlen(endpointUrl), 0, &urlComponents)) {
                logmsg("checking endpoint for %s (%s)\n", fileName, endpointUrl);
                req->source = REQ_SRC_HTTP;
                init_request_http(req, hostname, filePath, urlComponents.nScheme, urlComponents.nPort);
                return;
            }
        }
    }

    req->source = REQ_SRC_FILE;
    init_request_file(req);
}

static bool
read_chunk(Request *req, RequestChunk *chunk)
{
    DWORD size;

    chunk->size = 0;
    switch (req->source) {
    case REQ_SRC_FILE:
    case REQ_SRC_CACHE:
        return ReadFile(req->handles.hFile, chunk->data, REQUEST_CHUNK_SIZE, &chunk->size, NULL);
    case REQ_SRC_HTTP:
        /* InternetReadFile may return less than asked; fill the chunk unless the stream ended */
        while (chunk->size < REQUEST_CHUNK_SIZE) {
            if (!InternetReadFile(req->handles.http.hReq, chunk->data + chunk->size, REQUEST_CHUNK_SIZE - chunk->size, &size)) {
                return false;
            }
            if (size == 0) {
                break;
            }
            chunk->size += size;
        }
        return true;
    case REQ_SRC_MEMORY:
        size = MIN(req->sizeHint - req->bytesRead, REQUEST_CHUNK_SIZE);
        memcpy(chunk->data, req->handles.hData + req->bytesRead, size);
        chunk->size = size;
        return true;
    default:
        logmsg("Bad req src %d\n", req->source);
        exit(1);
    }
}

VOID CALLBACK
handle_request(PTP_CALLBACK_INSTANCE inst, void *reqArg, PTP_WORK work)
{
    Request *req;

    req = (Request *)reqArg;
    init_request(req);

    while (!InterlockedCompareExchange(&req->failed, 0, 0)
           && !InterlockedCompareExchange(&req->aborted, 0, 0)) {
        RequestChunk *chunk;

        chunk = malloc(sizeof(*chunk));
        chunk->next = NULL;
        chunk->consumed = 0;
        if (!read_chunk(req, chunk)) {
            free(chunk);
            cancel_request(req);
            break;
        }
        if (chunk->size == 0) {
            /* EOF */
            free(chunk);
            req->doneReason = NPRES_DONE;
            break;
        }
        req->bytesRead += chunk->size;

        if (req->hOutFile != INVALID_HANDLE_VALUE) {
            /* write directly to output file */
            DWORD written;
            WriteFile(req->hOutFile, chunk->data, chunk->size, &written, NULL);
            free(chunk);
            continue;
        }

        /* queue for the main thread, which pipes it to the plugin */
        EnterCriticalSection(&req->queueLock);
        if (req->queueTail) {
            req->queueTail->next = chunk;
        } else {
            req->queueHead = chunk;
        }
        req->queueTail = chunk;
        req->queuedBytes += chunk->size;
        LeaveCriticalSection(&req->queueLock);
        post_io(req, false);

        /* bound memory use if the plugin falls behind */
        while (!InterlockedCompareExchange(&req->aborted, 0, 0)) {
            size_t queued;
            EnterCriticalSection(&req->queueLock);
            queued = req->queuedBytes;
            LeaveCriticalSection(&req->queueLock);
            if (queued < REQUEST_MAX_QUEUED) {
                break;
            }
            if (WaitForSingleObject(req->spaceEvent, 50) == WAIT_TIMEOUT) {
                post_io(req, false);
            }
        }
    }

    if (req->hOutFile == INVALID_HANDLE_VALUE) {
        /*
         * Hand over the rest and let the main thread finish the stream. Only the
         * worker posts messages, so the final one is always handled last; it is
         * re-posted if the plugin wasn't ready to take everything yet.
         */
        InterlockedExchange(&req->readerDone, 1);
        post_io(req, true);
        while (WaitForSingleObject(req->readyEvent, 50) == WAIT_TIMEOUT) {
            if (WaitForSingleObject(req->spaceEvent, 0) == WAIT_OBJECT_0 && peek_chunk(req) != NULL) {
                post_io(req, true);
            }
        }
    }

    /* cleanup I/O handles */
    if ((req->source == REQ_SRC_FILE || req->source == REQ_SRC_CACHE)
        && req->handles.hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(req->handles.hFile);
    }
    if (req->source == REQ_SRC_CACHE) {
        UnlockUrlCacheEntryFileA(req->originalUrl, 0);
    }
    if (req->source == REQ_SRC_HTTP) {
        if (req->handles.http.hReq != NULL) {
            InternetCloseHandle(req->handles.http.hReq);
        }
        if (req->handles.http.hConn != NULL) {
            InternetCloseHandle(req->handles.http.hConn);
        }
    }

    /* cleanup signals */
    if (req->doneEvent != INVALID_HANDLE_VALUE) {
        SetEvent(req->doneEvent);
        CloseHandle(req->doneEvent);
    }
    CloseHandle(req->readyEvent);
    CloseHandle(req->spaceEvent);
    if (req->hOutFile != INVALID_HANDLE_VALUE) {
        DeleteCriticalSection(&req->queueLock);
        free(req);
    } else {
        /* messages for this request may still be queued; the main thread frees it after them */
        PostMessageW(hwnd, ioMsg, (WPARAM)IO_MSG_RELEASE, (LPARAM)req);
    }

    CloseThreadpoolWork(work);
}

static void
submit_request_work(Request *req)
{
    PTP_WORK work;

    req->readyEvent = CreateEventW(NULL, false, false, NULL);
    assert(req->readyEvent);
    req->spaceEvent = CreateEventW(NULL, false, false, NULL);
    assert(req->spaceEvent);
    InitializeCriticalSection(&req->queueLock);

    work = CreateThreadpoolWork(handle_request, req, NULL);
    assert(work);

    SubmitThreadpoolWork(work);
}

void
submit_request(Request *req)
{
    submit_request_work(req);
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
        .postDataLen = 0,
        .hOutFile = INVALID_HANDLE_VALUE,
        .doneEvent = INVALID_HANDLE_VALUE
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
        .postDataLen = postDataLen,
        .hOutFile = INVALID_HANDLE_VALUE,
        .doneEvent = INVALID_HANDLE_VALUE
    };
    strncpy(req->originalUrl, url, MAX_URL_LENGTH);
    memcpy(req->postData, postData, postDataLen);

    submit_request(req);
}

void
register_temp_request(const char *url, HANDLE outFile, HANDLE onDone)
{
    Request *req;

    assert(strlen(url) < MAX_URL_LENGTH);

    req = malloc(sizeof(*req));

    *req = (Request){
        .doNotify = false,
        .isPost = false,
        .postDataLen = 0,
        .hOutFile = outFile,
        .doneEvent = onDone
    };
    strncpy(req->originalUrl, url, MAX_URL_LENGTH);

    submit_request_work(req);
}

void complete_request()
{
    nRequests--;
    if (!mainLoaded && nRequests == 0) {
        logmsg("Ready to load main\n");
        on_load_ready();
        mainLoaded = true;
    }
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
