#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <string>

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

#define NPIDENTIFIERCOUNT 16
#define NPSTRINGMAXSIZE 128

char npidentifiers[NPIDENTIFIERCOUNT][NPSTRINGMAXSIZE];

NPIdentifier
getNPIdentifier(const char *s)
{
    int i;

    assert(*s != '\0');
    assert(strlen(s) <= NPSTRINGMAXSIZE);

    for (i = 0; i < NPIDENTIFIERCOUNT; i++) {
        if (strncmp(s, npidentifiers[i], NPSTRINGMAXSIZE) == 0)
            return (NPIdentifier)&npidentifiers[i];

        if (npidentifiers[i][0] == '\0')
            break;
    }
    // make sure there's still room
    assert(i < NPIDENTIFIERCOUNT-1);

    assert(npidentifiers[i][0] == '\0');
    strncpy(npidentifiers[i], s, NPSTRINGMAXSIZE);
    return &npidentifiers[i];
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

    register_request(url, notifyData);

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
    printf("< NPN_InvokeProc:%p, obj: %p, methodName: %s, argCount:%d\n", npp, obj, methodName, argCount);
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
        // should never ask to deallocate the (statically allocated) browser window object
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
    printf("< NPN_GetValueProc %d\n", variable);

    browserObject.referenceCount++;

    NPObject **retPtr;
    retPtr = (NPObject**)ret_value;
    *retPtr = &browserObject;

    return NPERR_NO_ERROR;
}

bool
NPN_EvaluateProc(NPP npp, NPObject *obj, NPString *script, NPVariant *result)
{
    printf("< NPN_EvaluateProc %s\n", script->UTF8Characters);

    // Evaluates JS calls, like MarkProgress(1), which doesn't need to do anything.
    *result = {
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
    printf("< NPN_GetStringIdentifiersProc %d\n", nameCount);

    int i;

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
    printf("< NPAllocateFunction %p\n", aClass);
    assert(aClass);

    NPObject *npobj;

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
    printf("< NPHasMethodFunction %p %s\n", npobj, name);

    return 0;
}

bool
NPInvokeFunction(NPObject *npobj, NPIdentifier name, const NPVariant *args, uint32_t argCount, NPVariant *result)
{
    printf("< NPInvokeFunction %p %s\n", npobj, name);

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
unhandled(void)
{
    printf("< UNHANDLED!!!\n");
}

void
initNetscapeFuncs(void)
{
#if 1
    // none of these seem to be getting hit. -fpermissive allows compile.
    netscapeFuncs.geturl = unhandled;
    netscapeFuncs.posturl = unhandled;
    netscapeFuncs.requestread = unhandled;
    netscapeFuncs.newstream = unhandled;
    netscapeFuncs.write = unhandled;
    netscapeFuncs.destroystream = unhandled;
    netscapeFuncs.status = unhandled;
    netscapeFuncs.uagent = unhandled;
    netscapeFuncs.memalloc = unhandled;
    netscapeFuncs.memfree = unhandled;
    netscapeFuncs.memflush = unhandled;
    netscapeFuncs.reloadplugins = unhandled;
    netscapeFuncs.getJavaEnv = unhandled;
    netscapeFuncs.getJavaPeer = unhandled;
    netscapeFuncs.geturlnotify = unhandled;
    netscapeFuncs.posturlnotify = unhandled;
    netscapeFuncs.getvalue = unhandled;
    netscapeFuncs.setvalue = unhandled;
    netscapeFuncs.invalidaterect = unhandled;
    netscapeFuncs.invalidateregion = unhandled;
    netscapeFuncs.forceredraw = unhandled;
    netscapeFuncs.getstringidentifier = unhandled;
    netscapeFuncs.getstringidentifiers = unhandled;
    netscapeFuncs.getintidentifier = unhandled;
    netscapeFuncs.identifierisstring = unhandled;
    netscapeFuncs.utf8fromidentifier = unhandled;
    netscapeFuncs.intfromidentifier = unhandled;
    netscapeFuncs.createobject = unhandled;
    netscapeFuncs.retainobject = unhandled;
    netscapeFuncs.releaseobject = unhandled;
    netscapeFuncs.invoke = unhandled;
    netscapeFuncs.invokeDefault = unhandled;
    netscapeFuncs.evaluate = unhandled;
    netscapeFuncs.getproperty = unhandled;
    netscapeFuncs.setproperty = unhandled;
    netscapeFuncs.removeproperty = unhandled;
    netscapeFuncs.hasproperty = unhandled;
    netscapeFuncs.hasmethod = unhandled;
    netscapeFuncs.releasevariantvalue = unhandled;
    netscapeFuncs.setexception = unhandled;
    netscapeFuncs.pushpopupsenabledstate = unhandled;
    netscapeFuncs.poppopupsenabledstate = unhandled;
    netscapeFuncs.enumerate = unhandled;
    netscapeFuncs.pluginthreadasynccall = unhandled;
    netscapeFuncs.construct = unhandled;
    netscapeFuncs.getvalueforurl = unhandled;
    netscapeFuncs.setvalueforurl = unhandled;
    netscapeFuncs.getauthenticationinfo = unhandled;
    netscapeFuncs.scheduletimer = unhandled;
    netscapeFuncs.unscheduletimer = unhandled;
    netscapeFuncs.popupcontextmenu = unhandled;
    netscapeFuncs.convertpoint = unhandled;
    netscapeFuncs.handleevent = unhandled;
    netscapeFuncs.unfocusinstance = unhandled;
    netscapeFuncs.urlredirectresponse = unhandled;
#endif

    netscapeFuncs.size = 224;
    netscapeFuncs.version = 27;
    netscapeFuncs.geturl = NPN_GetURLProc;
    netscapeFuncs.uagent = NPN_UserAgentProc;
    netscapeFuncs.geturlnotify = NPN_GetURLNotifyProc;
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

int
main(void)
{
    char *cwd = getcwd(NULL, 0);
    printf("setenv(\"%s\")\n", cwd);
    SetEnvironmentVariable("UNITY_HOME_DIR", cwd);

    initNetscapeFuncs();
    initBrowserObject();

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

    char *argn[] = {
        "id",
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
        "style",
    };
    char *argv[] = {
        "unityEmbed",
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
        "width: 1272px;"
    };
    assert(ARRLEN(argn) == ARRLEN(argv));

    printf("> NPP_NewProc\n");
    ret = pluginFuncs.newp("application/vnd.ffuwp", &npp, 1, ARRLEN(argn), argn, argv, &saved);
    printf("returned %d\n", ret);

    HWND hwnd = prepare_window();
    assert(hwnd);

    NPWindow npWin = {
        .window = hwnd,
        .x = 0, .y = 0,
        .width = 1272, .height = 680,
        .clipRect = {
            0, 0, 680, 1272
        },
        .type = NPWindowTypeWindow
    };

    printf("> NPP_SetWindowProc\n");
    ret = pluginFuncs.setwindow(&npp, &npWin);
    printf("returned %d\n", ret);

    NPObject *scriptableObject = NULL;

    // TODO: this is probably unnecessary here
    printf("> NPP_GetValueProc\n");
    ret = pluginFuncs.getvalue(&npp, NPPVpluginScriptableNPObject, &scriptableObject);
    printf("returned %d and NPObject %p\n", ret, scriptableObject);
    assert(scriptableObject->_class);

    printf("> scriptableObject.hasMethod style\n");
    ret = scriptableObject->_class->hasMethod(scriptableObject, getNPIdentifier("style"));
    printf("returned %d\n");

    // trying to mimic the browser's calls as close as possible. isn't helping.
    NPWindow npWin1 = npWin;
    npWin1.clipRect = {0, 0, 0, 0};
    printf("> NPP_SetWindowProc\n");
    ret = pluginFuncs.setwindow(&npp, &npWin1);
    printf("returned %d\n", ret);

    NPWindow npWin2 = npWin1;
    npWin2.height = 693;
    npWin2.clipRect = {0, 0, 693, 1272};
    printf("> NPP_SetWindowProc\n");
    ret = pluginFuncs.setwindow(&npp, &npWin2);
    printf("returned %d\n", ret);

    handle_requests();

    register_request("http://cdn.dexlabs.systems/ff/big/beta-20100104/main.unity3d", NULL);

    message_loop();
    /*
    for (;;) {
        message_loop();
        handle_requests();
        sleep(50);
    }
    */

    return 0;
}
