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

#define USERAGENT "ffrunner"
#define REVISIONS_PLIST "http://webplayer.unity3d.com/autodownload_webplugin_beta/revisions.plist"

#define ARRLEN(x) (sizeof(x)/sizeof(*x))

struct {
    bool completed;
    void *notifyData;
    char url[256];
} requests[16];

int nrequests = 0;

NPP_t npp;
NPPluginFuncs pluginFuncs;
NPSavedData saved;

void
printNPClass(NPClass *npclass)
{
#if 0
    printf("structVersion: %d\n"
    "construct: %p\n"
    "allocate: %p\n"
    "...\n",
    npclass->structVersion, npclass->construct, npclass->allocate);
#endif
}

void
dispatch_requests(void)
{
    int i;
    NPReason res;

    for (i = 0; i < nrequests; i++) {
        if (requests[i].completed)
            continue;

        res = NPRES_DONE;

        if (strncmp(requests[i].url, REVISIONS_PLIST, strlen(REVISIONS_PLIST)) == 0)
            res = NPRES_NETWORK_ERR;

        NPStream npstream = {
            .url = requests[i].url,
            .notifyData = requests[i].notifyData,
        };

        /* TODO: implement this
        pluginFuncs.newstream(&npp "TODO/MIMETYPE", &npstream, 0, NP_NORMAL);
        pluginFuncs.writeready(&npp, &npstream);
        pluginFuncs.write(&npp, &npstream, ...);
        pluginFuncs.destroystream(&npp, NPRES_DONE);
        */

        printf("> NPP_URLNotify %s\n", requests[i].url);
        pluginFuncs.urlnotify(&npp, requests[i].url, NPRES_NETWORK_ERR, requests[i].notifyData);
    }
}

NPError
NPN_GetURLProc(NPP instance, const char* url, const char* window)
{
    printf("< NPN_GetURLProcPtr:%p, url: %s, window: %s\n", instance, url, window);
    return NPERR_NO_ERROR;
}

NPError
NPN_GetURLNotifyProc(NPP instance, const char* url, const char* window, void* notifyData)
{
    printf("< NPN_GetURLNotifyProc:%p, url: %s, window: %s, notifyData: %p\n", instance, url, window, notifyData);

    strncpy(requests[nrequests].url, url, sizeof(requests[nrequests].url));
    requests[nrequests].notifyData = notifyData;
    requests[nrequests].completed = false;
    nrequests++;

    return NPERR_NO_ERROR;
}

const char *
NPN_UserAgentProc(NPP instance)
{
    printf("< NPN_UserAgentProc, NPP:%p\n", instance);
    return USERAGENT;
}

bool
NPN_GetPropertyProc(NPP npp, NPObject *obj, NPIdentifier propertyName, NPVariant *result)
{
    printf("< NPN_GetPropertyProc\n");
    return false;
}

bool
NPN_InvokeProc(NPP npp, NPObject* obj, NPIdentifier methodName, const NPVariant *args, uint32_t argCount, NPVariant *result)
{
    printf("< NPN_InvokeProc:%p, obj: %s, methodName: %p, argCount:%d\n", npp, obj, methodName, argCount);
    return false;
}

void
NPN_ReleaseVariantValueProc(NPVariant *variant)
{
    printf("< NPN_ReleaseVariantValueProc\n");
}

NPObject *
NPN_CreateObjectProc(NPP npp, NPClass *aClass)
{
    printf("< NPN_CreateObjectProc\n");
    assert(aClass);
    printNPClass(aClass);

    NPObject *npobj;

    if (aClass->allocate)
        npobj = aClass->allocate(npp, aClass);
    else
        npobj = (NPObject*)malloc(sizeof(*npobj));

    if (npobj) {
        npobj->_class = aClass;
        npobj->referenceCount = 1;
    }

    return npobj;
}

NPObject *
NPN_RetainObjectProc(NPObject *obj)
{
    printf("< NPN_RetainObjectProc\n");
    assert(obj);
    obj->referenceCount++;
    return obj;
}

void
NPN_ReleaseObjectProc(NPObject *obj)
{
    printf("< NPN_ReleaseObjectProc\n");
    assert(obj);

    obj->referenceCount--;

    if (obj->referenceCount == 0) {
        if (obj->_class && obj->_class->deallocate)
            obj->_class->deallocate(obj);
        else
            free(obj);
    }
}

NPNetscapeFuncs netscapeFuncs;
#if 0
= {
    .size = 224,
    .version = 27,
    .geturl = NPN_GetURLProc,
    /* ... */
    .uagent = NPN_UserAgentProc,
    /* ... */
    .geturlnotify = NPN_GetURLNotifyProc,
    /* ... */
    .releaseobject = NPN_ReleaseObjectProc,
    /* ... */
    .invoke = NPN_InvokeProc,
    /* ... */
    .getproperty = NPN_GetPropertyProc,
    /* ... */
    .createobject = NPN_CreateObjectProc,
    .retainobject = NPN_RetainObjectProc,
    /* ... */
    .releasevariantvalue = NPN_ReleaseVariantValueProc,
};
#endif

int
main(void)
{
    netscapeFuncs.size = 224;
    netscapeFuncs.version = 27;
    netscapeFuncs.geturl = NPN_GetURLProc;
    /* ... */
    netscapeFuncs.uagent = NPN_UserAgentProc;
    /* ... */
    netscapeFuncs.geturlnotify = NPN_GetURLNotifyProc;
    /* ... */
    netscapeFuncs.releaseobject = NPN_ReleaseObjectProc;
    /* ... */
    netscapeFuncs.invoke = NPN_InvokeProc;
    /* ... */
    netscapeFuncs.getproperty = NPN_GetPropertyProc;
    /* ... */
    netscapeFuncs.createobject = NPN_CreateObjectProc;
    netscapeFuncs.retainobject = NPN_RetainObjectProc;
    /* ... */
    netscapeFuncs.releasevariantvalue = NPN_ReleaseVariantValueProc;


    char *cwd = getcwd(NULL, 0);
    printf("setenv(\"%s\")\n", cwd);
    SetEnvironmentVariable("UNITY_HOME_DIR", cwd);

    printf("LoadLibraryA\n");
    HMODULE loader = LoadLibraryA("npUnity3D32.dll");

    printf("GetProcAddress\n");
    NP_GetEntryPointsFunc NP_GetEntryPoints = (NP_GetEntryPointsFunc)GetProcAddress(loader, "NP_GetEntryPoints");
    NP_InitializeFunc NP_Initialize = (NP_InitializeFunc)GetProcAddress(loader, "NP_Initialize");
    NP_ShutdownFunc NP_Shutdown = (NP_ShutdownFunc)GetProcAddress(loader, "NP_Shutdown");

    printf("> NP_GetEntryPoints\n");
    NPError ret = NP_GetEntryPoints(&pluginFuncs);
    printf("returned %d\n", ret);

    printf("> NP_Initialize\n");
    ret = NP_Initialize(&netscapeFuncs);
    printf("returned %d\n", ret);

    printf("print members\n");
    printf("size: %d, version: %d\n", pluginFuncs.size, pluginFuncs.version);
    printf("%#p\n", pluginFuncs.newp);

    char *argn[] = {
        "src",
        "type",
        "pluginspage",
        "width",
        "height",
        "bordercolor",
        "backgroundcolor",
        "disableContextMenu",
        "textcolor",
        "logoimage",
        "progressbarimage",
        "progressframeimage",
    };
    char *argv[] = {
        "http://cdn.dexlabs.systems/ff/big/beta-20100104/main.unity3d",
        "application/vnd.ffuwp",
        "http://www.unity3d.com/unity-web-player-2.x",
        "1280",
        "680",
        "000000",
        "000000",
        "true",
        "ccffff",
        "assets/img/unity-dexlabs.png",
        "assets/img/unity-loadingbar.png",
        "assets/img/unity-loadingframe.png",
    };
    assert(ARRLEN(argn) == ARRLEN(argv));

    printf("> NPP_NewProc\n");
    ret = pluginFuncs.newp("application/vnd.ffuwp", &npp, 1, ARRLEN(argn), argn, argv, &saved);
    printf("returned %d\n", ret);

    dispatch_requests();

    HWND hwnd = CreateWindowA("STATIC", "FusionFall", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, 0, 0, GetModuleHandleA(0), 0);
    assert(hwnd);

    //ShowWindow(hwnd, SW_SHOWDEFAULT);
    //UpdateWindow(hwnd);

    NPWindow npWin = {
        .window = hwnd,
        .x = 0, .y = 0,
        .width = 1280, .height = 720,
        .clipRect = {
            0, 0, 1280, 720
        },
        .type = NPWindowTypeWindow
    };

    printf("> NPP_SetWindowProc\n");
    ret = pluginFuncs.setwindow(&npp, &npWin);
    printf("returned %d\n", ret);

    NPObject *scriptableObject = NULL;

    printf("> NPP_GetValueProc\n");
    ret = pluginFuncs.getvalue(&npp, NPPVpluginScriptableNPObject, &scriptableObject);
    printf("returned %d and NPObject %p\n", ret, scriptableObject);

    assert(scriptableObject->_class);
    printNPClass(scriptableObject->_class);

    NPIdentifier *items;
    int itemcount;

    // enumerate() isn't implented. invoke() looks to be the way to go.
#if 0
    printf("run NPEnumerationFunction\n");
    scriptableObject->_class->enumerate(scriptableObject, &items, &itemcount);
    printf("returned %d items\n", itemcount);
#endif

    return 0;
}
