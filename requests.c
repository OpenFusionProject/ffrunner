#include "ffrunner.h"

#include <stdio.h>

PTP_POOL threadpool;
HINTERNET hInternet;
UINT ioMsg;

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
    return NULL;
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

bool
handle_io_progress(Request *req)
{
    NPError err;
    uint32_t writeSize;
    int32_t bytesConsumed;
    uint8_t *dataPtr;

    assert(req->source != REQ_SRC_UNSET);

    if (req->stream == NULL) {
        /* start streaming */
        req->stream = (NPStream*)malloc(sizeof(NPStream));
        req->stream->url = req->url;
        req->stream->end = req->sizeHint;
        req->stream->notifyData = req->notifyData;
        log("> NPP_NewStream %s\n", req->url);
        err = pluginFuncs.newstream(&npp, req->mimeType, req->stream, false, &req->streamType);
        log("returned %d\n", err);
        if (err != NPERR_NO_ERROR) {
            exit(1);
        }
    }

    if (req->writeSize > 0) {
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
            req->writePtr = req->writeSize;
            req->doneReason = NPRES_NETWORK_ERR;
            req->done = true;
        } else {
            req->writePtr += bytesConsumed;
        }
    }

    if (req->done && req->writePtr == req->writeSize) {
        /* request is complete and all available data has been consumed */
        assert(req->stream != NULL);
        log("> NPP_DestroyStream %s %d\n", req->url, req->doneReason);
        err = pluginFuncs.destroystream(&npp, req->stream, req->doneReason);
        log("returned %d\n", err);
        if (err != NPERR_NO_ERROR) {
            return false;
        }
        free(req->stream);

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

        return false;
    }

    return true;
}

void
init_request_file(Request *req)
{
    DWORD fileSize;

    req->handles.hFile = CreateFileA(req->url, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (req->handles.hFile == INVALID_HANDLE_VALUE) {
        req->doneReason = NPRES_NETWORK_ERR;
        req->done = true;
    }

    fileSize = GetFileSize(req->handles.hFile, NULL);
    if (fileSize != INVALID_FILE_SIZE) {
        req->sizeHint = fileSize;
    }
}

void
init_request_http(Request *req, LPURL_COMPONENTSA urlComponents)
{
    if (hInternet == NULL) {
        goto fail;
    }

    // TODO

fail:
    req->doneReason = NPRES_NETWORK_ERR;
    req->done = true;
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
        init_request_http(req, &urlComponents);
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
            req->writeSize = 0; /* just in case */
            req->doneReason = NPRES_NETWORK_ERR;
        } else {
            req->doneReason = NPRES_DONE;
        }
        req->done = true;
        break;
    case REQ_SRC_HTTP:
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
    if (req->source == REQ_SRC_UNSET) {
        init_request(req);
    }
    progress_request(req);
    PostThreadMessage(mainThreadId, ioMsg, (WPARAM)NULL, (LPARAM)req);
}

void
submit_request(Request *req)
{
    PTP_WORK work;

    work = CreateThreadpoolWork(step_request, req, NULL);
    SubmitThreadpoolWork(work);
}

void
register_get_request(const char *url, bool doNotify, void *notifyData)
{
    Request *req;

    assert(strlen(url) < MAX_URL_LENGTH);

    req = (Request*)malloc(sizeof(Request));

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

    req = (Request*)malloc(sizeof(Request));

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
init_network()
{
    ioMsg = RegisterWindowMessageA(IO_MSG_NAME);
    if (!ioMsg) {
        log("Failed to register io msg: %d\n", GetLastError());
        exit(1);
    }
    threadpool = CreateThreadpool(NULL);
    hInternet = InternetOpenA(USERAGENT, INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
}