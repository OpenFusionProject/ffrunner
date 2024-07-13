#include <string.h>
#include <assert.h>

#include <windows.h>

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
    log("< NPN_GetURLProc:%p, url: %s, window: %s\n", instance, url, window);

    register_get_request(url, false, NULL);

    return NPERR_NO_ERROR;
}

NPError
NPN_GetURLNotifyProc(NPP instance, const char* url, const char* window, void* notifyData)
{
    log("< NPN_GetURLNotifyProc:%p, url: %s, window: %s, notifyData: %p\n", instance, url, window, notifyData);

    register_get_request(url, true, notifyData);

    return NPERR_NO_ERROR;
}

NPError
NPN_PostURLProc(NPP instance, const char* url, const char* window, uint32_t len, const char* buf, NPBool file)
{
    log("< NPN_PostURLProc:%p, url: %s, window: %s, len: %d, buf: %s, file: %d\n",
            instance, url, window, len, buf, file);

    register_post_request(url, false, NULL, len, buf);

    return NPERR_NO_ERROR;
}

NPError
NPN_PostURLNotifyProc(NPP instance, const char* url, const char* window, uint32_t len, const char* buf, NPBool file, void* notifyData)
{
    log("< NPN_PostURLNotifyProc:%p, url: %s, window: %s, len: %d, buf: %s, file: %d, notifyData: %p\n",
            instance, url, window, len, buf, file, notifyData);

    register_post_request(url, true, notifyData, len, buf);

    return NPERR_NO_ERROR;
}

const char *
NPN_UserAgentProc(NPP instance)
{
    log("< NPN_UserAgentProc, NPP:%p\n", instance);
    return USERAGENT;
}

bool
NPN_GetPropertyProc(NPP npp, NPObject *obj, NPIdentifier propertyName, NPVariant *result)
{
    log("< NPN_GetPropertyProc\n");
    return false;
}

bool
NPN_InvokeProc(NPP npp, NPObject* obj, NPIdentifier methodName, const NPVariant *args, uint32_t argCount, NPVariant *result)
{
    log("< NPN_InvokeProc:%p, obj: %p, methodName: %p, argCount:%d\n", npp, obj, methodName, argCount);
    return false;
}

void
NPN_ReleaseVariantValueProc(NPVariant *variant)
{
    log("< NPN_ReleaseVariantValueProc\n");
}

NPObject *
NPN_CreateObjectProc(NPP npp, NPClass *aClass)
{
    NPObject *npobj;

    log("< NPN_CreateObjectProc\n");
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
    log("< NPN_RetainObjectProc\n");
    assert(obj);
    obj->referenceCount++;
    return obj;
}

void
NPN_ReleaseObjectProc(NPObject *obj)
{
    log("< NPN_ReleaseObjectProc\n");
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

    log("< NPN_GetValueProc %d\n", variable);

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
    log("< NPN_EvaluateProc %s\n", script->UTF8Characters);

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
    log("< NPN_GetStringIdentifierProc %s\n", name);

    return getNPIdentifier(name);
}

void
NPN_GetStringIdentifiersProc(const NPUTF8** names, int32_t nameCount, NPIdentifier* identifiers)
{
    int i;

    log("< NPN_GetStringIdentifiersProc %d\n", nameCount);

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

    log("< NPAllocateFunction %p\n", aClass);
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
    log("< NPDeallocateFunction %p\n", npobj);
    assert(npobj != &browserObject);

    free(npobj);
}

void
NPInvalidateFunction(NPObject *npobj)
{
    log("< NPInvalidateFunction %p\n", npobj);
}

bool
NPHasMethodFunction(NPObject *npobj, NPIdentifier name)
{
    log("< NPHasMethodFunction %p %p\n", npobj, name);

    return 0;
}

bool
NPInvokeFunction(NPObject *npobj, NPIdentifier name, const NPVariant *args, uint32_t argCount, NPVariant *result)
{
    log("< NPInvokeFunction %p %p\n", npobj, name);

    return 0;
}

bool
NPInvokeDefaultFunction(NPObject *npobj, const NPVariant *args, uint32_t argCount, NPVariant *result)
{
    log("< NPInvokeDefaultFunction %p\n", npobj);

    return 0;
}

bool
NPHasPropertyFunction(NPObject *npobj, NPIdentifier name)
{
    log("< NPHasPropertyFunction %p\n", npobj);

    return 0;
}

bool
NPGetPropertyFunction(NPObject *npobj, NPIdentifier name, NPVariant *result)
{
    log("< NPGetPropertyFunction %p\n", npobj);

    return 0;
}

bool
NPSetPropertyFunction(NPObject *npobj, NPIdentifier name, const NPVariant *value)
{
    log("< NPSetPropertyFunction %p\n", npobj);

    return 0;
}

bool
NPRemovePropertyFunction(NPObject *npobj, NPIdentifier name)
{
    log("< NPRemovePropertyFunction %p\n", npobj);

    return 0;
}

bool
NPEnumerationFunction(NPObject *npobj, NPIdentifier **value, uint32_t *count)
{
    log("< NPEnumerationFunction %p\n", npobj);

    return 0;
}

bool
NPConstructFunction(NPObject *npobj, const NPVariant *args, uint32_t argCount, NPVariant *result)
{
    log("< NPConstructFunction %p\n", npobj);

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
    const HANDLE signals[] = { ioReadySig };

    DWORD waitResult;
    bool initialLoadDone = false;

    while (true) {
        waitResult = MsgWaitForMultipleObjects(ARRLEN(signals), signals, false, INFINITE, QS_ALLEVENTS);
        switch (waitResult)
        {
        case WAIT_OBJECT_0:
            /* I/O posted and needs to be processed */
            handle_io_event();
            break;
        case WAIT_OBJECT_0 + ARRLEN(signals):
            /* Message(s) posted and need to be processed */
            if (handle_messages())
                /* Quit message received */
                return;
            break;
        default:
            log("Signal loop error: 0x%x\n", waitResult);
            exit(1);
        }
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
    HANDLE reqThread;

    init_logging("log.txt");

    if (GetCurrentDirectory(MAX_PATH, cwd)) {
        log("setenv(\"%s\")\n", cwd);
        SetEnvironmentVariable("UNITY_HOME_DIR", cwd);
    }
    SetEnvironmentVariable("UNITY_DISABLE_PLUGIN_UPDATES", "yes");

    initNetscapeFuncs();
    initBrowserObject();

    log("LoadLibraryA\n");
    loader = LoadLibraryA("npUnity3D32.dll");
    if (!loader) {
        err = GetLastError();
        log("Failed to load plugin DLL: 0x%x\n", err);
        exit(1);
    }

    log("GetProcAddress\n");
    NP_GetEntryPointsFuncOS NP_GetEntryPoints = (NP_GetEntryPointsFuncOS)GetProcAddress(loader, "NP_GetEntryPoints");
    NP_InitializeFuncOS NP_Initialize = (NP_InitializeFuncOS)GetProcAddress(loader, "NP_Initialize");
    NP_ShutdownFuncOS NP_Shutdown = (NP_ShutdownFuncOS)GetProcAddress(loader, "NP_Shutdown");

    if (!NP_GetEntryPoints || !NP_Initialize || !NP_Shutdown) {
        log("Failed to find one or more plugin symbols. Invalid plugin DLL?\n");
        exit(1);
    }

    init_requests();

    log("> NP_GetEntryPoints\n");
    ret = NP_GetEntryPoints(&pluginFuncs);
    log("returned %d\n", ret);

    log("> NP_Initialize\n");
    ret = NP_Initialize(&netscapeFuncs);
    log("returned %d\n", ret);

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

    log("> NPP_NewProc\n");
    ret = pluginFuncs.newp("application/vnd.ffuwp", &npp, 1, ARRLEN(argn), argn, argv, &saved);
    log("returned %d\n", ret);

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

    log("> NPP_SetWindowProc\n");
    ret = pluginFuncs.setwindow(&npp, &npWin);
    log("returned %d\n", ret);


    log("> NPP_GetValueProc\n");
    ret = pluginFuncs.getvalue(&npp, NPPVpluginScriptableNPObject, &scriptableObject);
    log("returned %d and NPObject %p\n", ret, scriptableObject);
    assert(scriptableObject->_class);

    log("> scriptableObject.hasMethod style\n");
    ret = scriptableObject->_class->hasMethod(scriptableObject, getNPIdentifier("style"));
    log("returned %d\n", ret);

    reqThread = CreateThread(NULL, 0, request_loop, NULL, 0, NULL);
    assert(reqThread);

    signal_loop();

    return 0;
}
