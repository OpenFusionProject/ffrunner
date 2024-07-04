#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <windows.h>

#include "npapi/npapi.h"
#include "npapi/npfunctions.h"
#include "npapi/npruntime.h"
#include "npapi/nptypes.h"

#include "ffrunner.h"

NPP_t npp;
NPPluginFuncs pluginFuncs;
NPNetscapeFuncs netscapeFuncs;
NPSavedData saved;
NPObject browserObject;
NPClass browserClass;
NPWindow npWin;

#define NPIDENTIFIERCOUNT 32
#define NPSTRINGMAXSIZE 128

char npidentifiers[NPIDENTIFIERCOUNT][NPSTRINGMAXSIZE];

NPIdentifier
getNPIdentifier(const char *s)
{
    int i;

    assert(*s != '\0');
    assert(strlen(s) < NPSTRINGMAXSIZE);

    for (i = 0; i < NPIDENTIFIERCOUNT; i++) {
        if (strncmp(s, npidentifiers[i], NPSTRINGMAXSIZE) == 0)
            return (NPIdentifier)&npidentifiers[i];

        if (npidentifiers[i][0] == '\0')
            break;
    }
    /*
     * Make sure there's still room.
     * i would have already been incremented to NPIDENTIFIERCOUNT if
     * the loop went through all iterations.
     */
    assert(i < NPIDENTIFIERCOUNT);

    assert(npidentifiers[i][0] == '\0');
    strncpy(npidentifiers[i], s, NPSTRINGMAXSIZE);
    return &npidentifiers[i];
}

NPError
NPN_GetURLProc(NPP instance, const char* url, const char* window)
{
    printf("< NPN_GetURLProc:%p, url: %s, window: %s\n", instance, url, window);

    register_get_request(url, false, NULL);

    return NPERR_NO_ERROR;
}

NPError
NPN_GetURLNotifyProc(NPP instance, const char* url, const char* window, void* notifyData)
{
    printf("< NPN_GetURLNotifyProc:%p, url: %s, window: %s, notifyData: %p\n", instance, url, window, notifyData);

    register_get_request(url, true, notifyData);

    return NPERR_NO_ERROR;
}

NPError
NPN_PostURLProc(NPP instance, const char* url, const char* window, uint32_t len, const char* buf, NPBool file)
{
    printf("< NPN_PostURLProc:%p, url: %s, window: %s, len: %d, buf: %s, file: %d\n",
            instance, url, window, len, buf, file);

    register_post_request(url, false, NULL, len, buf);

    return NPERR_NO_ERROR;
}

NPError
NPN_PostURLNotifyProc(NPP instance, const char* url, const char* window, uint32_t len, const char* buf, NPBool file, void* notifyData)
{
    printf("< NPN_PostURLNotifyProc:%p, url: %s, window: %s, len: %d, buf: %s, file: %d, notifyData: %p\n",
            instance, url, window, len, buf, file, notifyData);

    register_post_request(url, true, notifyData, len, buf);

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
    printf("< NPN_InvokeProc:%p, obj: %p, methodName: %p, argCount:%d\n", npp, obj, methodName, argCount);
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
    NPObject *npobj;

    printf("< NPN_CreateObjectProc\n");
    assert(aClass);

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
        /* should never ask to deallocate the (statically allocated) browser window object */
        assert(obj != &browserObject);

        if (obj->_class && obj->_class->deallocate)
            obj->_class->deallocate(obj);
        else
            free(obj);
    }
}

NPError
NPN_GetValueProc(NPP instance, NPNVariable variable, void *ret_value)
{
    NPObject **retPtr;

    printf("< NPN_GetValueProc %d\n", variable);

    browserObject.referenceCount++;

    retPtr = (NPObject**)ret_value;
    *retPtr = &browserObject;

    return NPERR_NO_ERROR;
}

#define AUTH_CALLBACK_SCRIPT "authDoCallback(\"UnityEngine.GameObject\");"
#define EXIT_CALLBACK_SCRIPT "HomePage(\"UnityEngine.GameObject\");"

bool
NPN_EvaluateProc(NPP npp, NPObject *obj, NPString *script, NPVariant *result)
{
    printf("< NPN_EvaluateProc %s\n", script->UTF8Characters);

    /* Evaluates JS calls, like MarkProgress(1), most of which doesn't need to do anything. */
    if (strncmp(script->UTF8Characters, EXIT_CALLBACK_SCRIPT, sizeof(EXIT_CALLBACK_SCRIPT)) == 0) {
        /* Gracefully exit game. */
        PostQuitMessage(0);
    }

    *result = (NPVariant){
        .type = NPVariantType_Void
    };

    return true;
}

NPIdentifier
NPN_GetStringIdentifierProc(const NPUTF8* name)
{
    printf("< NPN_GetStringIdentifierProc %s\n", name);

    return getNPIdentifier(name);
}

void
NPN_GetStringIdentifiersProc(const NPUTF8** names, int32_t nameCount, NPIdentifier* identifiers)
{
    int i;

    printf("< NPN_GetStringIdentifiersProc %d\n", nameCount);

    for (i = 0; i < nameCount; i++) {
        identifiers[i] = getNPIdentifier(names[i]);
    }
}

/*
 * Browser Object methods below here.
 */

NPObject *
NPAllocateFunction(NPP instance, NPClass *aClass)
{
    NPObject *npobj;

    printf("< NPAllocateFunction %p\n", aClass);
    assert(aClass);

    if (aClass->allocate)
        npobj = aClass->allocate(instance, aClass);
    else
        npobj = (NPObject*)malloc(sizeof(*npobj));

    if (npobj) {
        npobj->_class = aClass;
        npobj->referenceCount = 1;
    }

    return npobj;
}

void
NPDeallocateFunction(NPObject *npobj)
{
    printf("< NPDeallocateFunction %p\n", npobj);
    assert(npobj != &browserObject);

    free(npobj);
}

void
NPInvalidateFunction(NPObject *npobj)
{
    printf("< NPInvalidateFunction %p\n", npobj);
}

bool
NPHasMethodFunction(NPObject *npobj, NPIdentifier name)
{
    printf("< NPHasMethodFunction %p %p\n", npobj, name);

    return 0;
}

bool
NPInvokeFunction(NPObject *npobj, NPIdentifier name, const NPVariant *args, uint32_t argCount, NPVariant *result)
{
    printf("< NPInvokeFunction %p %p\n", npobj, name);

    return 0;
}

bool
NPInvokeDefaultFunction(NPObject *npobj, const NPVariant *args, uint32_t argCount, NPVariant *result)
{
    printf("< NPInvokeDefaultFunction %p\n", npobj);

    return 0;
}

bool
NPHasPropertyFunction(NPObject *npobj, NPIdentifier name)
{
    printf("< NPHasPropertyFunction %p\n", npobj);

    return 0;
}

bool
NPGetPropertyFunction(NPObject *npobj, NPIdentifier name, NPVariant *result)
{
    printf("< NPGetPropertyFunction %p\n", npobj);

    return 0;
}

bool
NPSetPropertyFunction(NPObject *npobj, NPIdentifier name, const NPVariant *value)
{
    printf("< NPSetPropertyFunction %p\n", npobj);

    return 0;
}

bool
NPRemovePropertyFunction(NPObject *npobj, NPIdentifier name)
{
    printf("< NPRemovePropertyFunction %p\n", npobj);

    return 0;
}

bool
NPEnumerationFunction(NPObject *npobj, NPIdentifier **value, uint32_t *count)
{
    printf("< NPEnumerationFunction %p\n", npobj);

    return 0;
}

bool
NPConstructFunction(NPObject *npobj, const NPVariant *args, uint32_t argCount, NPVariant *result)
{
    printf("< NPConstructFunction %p\n", npobj);

    return 0;
}

void
initBrowserObject(void)
{
    browserObject._class = &browserClass;
    browserObject.referenceCount = 1;

    browserClass.structVersion = 3;
    browserClass.allocate = NPAllocateFunction;
    browserClass.deallocate = NPDeallocateFunction;
    browserClass.invalidate = NPInvalidateFunction;
    browserClass.hasMethod = NPHasMethodFunction;
    browserClass.invoke = NPInvokeFunction;
    browserClass.invokeDefault = NPInvokeDefaultFunction;
    browserClass.hasProperty = NPHasPropertyFunction;
    browserClass.getProperty = NPGetPropertyFunction;
    browserClass.setProperty = NPSetPropertyFunction;
    browserClass.removeProperty = NPRemovePropertyFunction;
    browserClass.enumerate = NPEnumerationFunction;
    browserClass.construct = NPConstructFunction;
}

void
initNetscapeFuncs(void)
{
    netscapeFuncs.size = 224;
    netscapeFuncs.version = 27;
    netscapeFuncs.geturl = NPN_GetURLProc;
    netscapeFuncs.posturl = NPN_PostURLProc;
    netscapeFuncs.uagent = NPN_UserAgentProc;
    netscapeFuncs.geturlnotify = NPN_GetURLNotifyProc;
    netscapeFuncs.posturlnotify = NPN_PostURLNotifyProc;
    netscapeFuncs.releaseobject = NPN_ReleaseObjectProc;
    netscapeFuncs.invoke = NPN_InvokeProc;
    netscapeFuncs.getproperty = NPN_GetPropertyProc;
    netscapeFuncs.createobject = NPN_CreateObjectProc;
    netscapeFuncs.retainobject = NPN_RetainObjectProc;
    netscapeFuncs.releasevariantvalue = NPN_ReleaseVariantValueProc;
    netscapeFuncs.getvalue = NPN_GetValueProc;
    netscapeFuncs.evaluate = NPN_EvaluateProc;
    netscapeFuncs.getstringidentifier = NPN_GetStringIdentifierProc;
    netscapeFuncs.getstringidentifiers = NPN_GetStringIdentifiersProc;
}

void
signal_loop(void)
{
    while (true) {
        if (message_loop())
            break;
        handle_requests();
    }
}

int
main(void)
{
    DWORD err;
    char cwd[MAX_PATH];
    NPError ret;
    NPObject *scriptableObject;
    HMODULE loader;
    HWND hwnd;
    RECT winRect;

    if (GetCurrentDirectory(MAX_PATH, cwd)) {
        printf("setenv(\"%s\")\n", cwd);
        SetEnvironmentVariable("UNITY_HOME_DIR", cwd);
    }
    SetEnvironmentVariable("UNITY_DISABLE_PLUGIN_UPDATES", "yes");

    initNetscapeFuncs();
    initBrowserObject();

    printf("LoadLibraryA\n");
    loader = LoadLibraryA("npUnity3D32.dll");
    if (!loader) {
        err = GetLastError();
        printf("Failed to load plugin DLL: 0x%x\n", err);
        exit(1);
    }

    printf("GetProcAddress\n");
    NP_GetEntryPointsFuncOS NP_GetEntryPoints = (NP_GetEntryPointsFuncOS)GetProcAddress(loader, "NP_GetEntryPoints");
    NP_InitializeFuncOS NP_Initialize = (NP_InitializeFuncOS)GetProcAddress(loader, "NP_Initialize");
    NP_ShutdownFuncOS NP_Shutdown = (NP_ShutdownFuncOS)GetProcAddress(loader, "NP_Shutdown");

    if (!NP_GetEntryPoints || !NP_Initialize || !NP_Shutdown) {
        printf("Failed to find one or more plugin symbols. Invalid plugin DLL?\n");
        exit(1);
    }

    init_network();

    printf("> NP_GetEntryPoints\n");
    ret = NP_GetEntryPoints(&pluginFuncs);
    printf("returned %d\n", ret);

    printf("> NP_Initialize\n");
    ret = NP_Initialize(&netscapeFuncs);
    printf("returned %d\n", ret);

    char *argn[] = {
        "src",
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
        SRC_URL,
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

    hwnd = prepare_window();
    assert(hwnd);

    npWin = (NPWindow){
        .window = hwnd,
        .x = 0, .y = 0,
        .width = WIDTH, .height = HEIGHT,
        .clipRect = {
            0, 0, HEIGHT, WIDTH
        },
        .type = NPWindowTypeWindow
    };

    /* adjust plugin rect to the real inner size of the window */
    if (GetClientRect(hwnd, &winRect)) {
        npWin.clipRect.top = winRect.top;
        npWin.clipRect.bottom = winRect.bottom;
        npWin.clipRect.left = winRect.left;
        npWin.clipRect.right = winRect.right;

        npWin.height = winRect.bottom - winRect.top;
        npWin.width = winRect.right - winRect.left;
    }

    printf("> NPP_SetWindowProc\n");
    ret = pluginFuncs.setwindow(&npp, &npWin);
    printf("returned %d\n", ret);


    printf("> NPP_GetValueProc\n");
    ret = pluginFuncs.getvalue(&npp, NPPVpluginScriptableNPObject, &scriptableObject);
    printf("returned %d and NPObject %p\n", ret, scriptableObject);
    assert(scriptableObject->_class);

    printf("> scriptableObject.hasMethod style\n");
    ret = scriptableObject->_class->hasMethod(scriptableObject, getNPIdentifier("style"));
    printf("returned %d\n", ret);

    handle_requests();

    /* load the actual content */
    register_get_request(SRC_URL, true, NULL);

    signal_loop();

    return 0;
}
