#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>

#include <windows.h>

#include "npapi/npapi.h"
#include "npapi/npfunctions.h"
#include "npapi/npruntime.h"
#include "npapi/nptypes.h"

#define USERAGENT "ffrunner"

NPP_t npp;
NPPluginFuncs pluginFuncs;
NPSavedData saved;

void
printNPClass(NPClass *npclass)
{
    printf("structVersion: %d\n"
    "construct: %p\n"
    "allocate: %p\n"
    "...\n",
    npclass->structVersion, npclass->construct, npclass->allocate);
}

NPError
NPN_GetURLProc(NPP instance, const char* url, const char* window)
{
    printf("[D] NPN_GetURLProcPtr:%p, url: %s, window: %s\n", instance, url, window);
    return NPERR_NO_ERROR;
}

const char *
NPN_UserAgentProc(NPP instance)
{
    printf("[D] NPN_UserAgentProc, NPP:%p\n", instance);
    return USERAGENT;
}

bool
NPN_GetPropertyProc(NPP npp, NPObject *obj, NPIdentifier propertyName, NPVariant *result)
{
    printf("[D] NPN_GetPropertyProc\n");
    return false;
}

bool
NPN_InvokeProc(NPP npp, NPObject* obj, NPIdentifier methodName, const NPVariant *args, uint32_t argCount, NPVariant *result)
{
    printf("[D] NPN_InvokeProc:%p, obj: %s, methodName: %p, argCount:%d\n", npp, obj, methodName, argCount);
    return false;
}

void
NPN_ReleaseVariantValueProc(NPVariant *variant)
{
    printf("[D] NPN_ReleaseVariantValueProc\n");
}

NPObject *
NPN_CreateObjectProc(NPP npp, NPClass *aClass)
{
    printf("[D] NPN_CreateObjectProc\n");
    assert(aClass);
    printNPClass(aClass);

    NPObject *npobj;

    if (aClass->allocate)
        npobj = aClass->allocate(npp, aClass);
    else
        npobj = malloc(sizeof(*npobj));

    if (npobj) {
        npobj->_class = aClass;
        npobj->referenceCount = 1;
    }

    return npobj;
}

NPObject *
NPN_RetainObjectProc(NPObject *obj)
{
    printf("[D] NPN_RetainObjectProc\n");
    assert(obj);
    obj->referenceCount++;
    return obj;
}

void
NPN_ReleaseObjectProc(NPObject *obj)
{
    printf("[D] NPN_ReleaseObjectProc\n");
    assert(obj);

    obj->referenceCount--;

    if (obj->referenceCount == 0) {
        if (obj->_class && obj->_class->deallocate)
            obj->_class->deallocate(obj);
        else
            free(obj);
    }
}

NPError
NPN_GetURLNotifyProc(NPP instance, const char* url, const char* window, void* notifyData)
{
    printf("[D] NPN_GetURLNotifyProc:%p, url: %s, window: %s, notifyData: %p\n", instance, url, window, notifyData);
    return 0;
}

NPNetscapeFuncs netscapeFuncs = {
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

int
main(void)
{
    char *cwd = getcwd(NULL, 0);
    printf("setenv(\"%s\")\n", cwd);
    SetEnvironmentVariable("UNITY_HOME_DIR", cwd);

    printf("LoadLibraryA\n");
    HMODULE loader = LoadLibraryA("npUnity3D32.dll");

    printf("GetProcAddress\n");
    NP_GetEntryPointsFunc NP_GetEntryPoints = (NP_GetEntryPointsFunc)GetProcAddress(loader, "NP_GetEntryPoints");
    FARPROC NP_Initialize = GetProcAddress(loader, "NP_Initialize");
    FARPROC NP_Shutdown = GetProcAddress(loader, "NP_Shutdown");

    printf("NP_GetEntryPoints\n");
    NPError ret = NP_GetEntryPoints(&pluginFuncs);
    printf("returned %d\n", ret);

    printf("NP_Initialize\n");
    ret = NP_Initialize(&netscapeFuncs);
    printf("returned %d\n", ret);

    printf("print members\n");
    printf("size: %d, version: %d\n", pluginFuncs.size, pluginFuncs.version);
    printf("%#p\n", pluginFuncs.newp);

    printf("run NPP_NewProc\n");
    ret = pluginFuncs.newp("application/vnd.ffuwp", &npp, 1, 0, NULL, NULL, &saved);
    printf("returned %d\n", ret);

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

    printf("run NPP_SetWindowProc\n");
    ret = pluginFuncs.setwindow(&npp, &npWin);
    printf("returned %d\n", ret);

    NPObject *scriptableObject = NULL;

    printf("run NPP_GetValueProc\n");
    ret = pluginFuncs.getvalue(&npp, NPPVpluginScriptableNPObject, &scriptableObject);
    printf("returned %d and NPObject %p\n", ret, scriptableObject);

    assert(scriptableObject->_class);
    printNPClass(scriptableObject->_class);

    NPIdentifier *items;
    int itemcount;

    // enumerate() isn't implented. invoke() looks to be the way to go.'
#if 0
    printf("run NPEnumerationFunction\n");
    scriptableObject->_class->enumerate(scriptableObject, &items, &itemcount);
    printf("returned %d items\n", itemcount);
#endif

    return 0;
}
