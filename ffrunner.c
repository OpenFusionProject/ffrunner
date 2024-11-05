#include "ffrunner.h"
#include "arg.h"

NPP_t npp;
NPPluginFuncs pluginFuncs;
NPNetscapeFuncs netscapeFuncs;
NPSavedData saved;
NPObject browserObject;
NPClass browserClass;
NPWindow npWin;
NPObject *scriptableObject;

Arguments args = { 0 };

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
    logmsg("< NPN_GetURLProc:%p, url: %s, window: %s\n", instance, url, window);

    register_get_request(url, false, NULL);

    return NPERR_NO_ERROR;
}

NPError
NPN_GetURLNotifyProc(NPP instance, const char* url, const char* window, void* notifyData)
{
    logmsg("< NPN_GetURLNotifyProc:%p, url: %s, window: %s, notifyData: %p\n", instance, url, window, notifyData);

    register_get_request(url, true, notifyData);

    return NPERR_NO_ERROR;
}

NPError
NPN_PostURLProc(NPP instance, const char* url, const char* window, uint32_t len, const char* buf, NPBool file)
{
    logmsg("< NPN_PostURLProc:%p, url: %s, window: %s, len: %d, buf: %s, file: %d\n",
            instance, url, window, len, buf, file);

    register_post_request(url, false, NULL, len, buf);

    return NPERR_NO_ERROR;
}

NPError
NPN_PostURLNotifyProc(NPP instance, const char* url, const char* window, uint32_t len, const char* buf, NPBool file, void* notifyData)
{
    logmsg("< NPN_PostURLNotifyProc:%p, url: %s, window: %s, len: %d, buf: %s, file: %d, notifyData: %p\n",
            instance, url, window, len, buf, file, notifyData);

    register_post_request(url, true, notifyData, len, buf);

    return NPERR_NO_ERROR;
}

const char *
NPN_UserAgentProc(NPP instance)
{
    logmsg("< NPN_UserAgentProc, NPP:%p\n", instance);
    return USERAGENT;
}

bool
NPN_GetPropertyProc(NPP npp, NPObject *obj, NPIdentifier propertyName, NPVariant *result)
{
    logmsg("< NPN_GetPropertyProc\n");
    return false;
}

bool
NPN_InvokeProc(NPP npp, NPObject* obj, NPIdentifier methodName, const NPVariant *args, uint32_t argCount, NPVariant *result)
{
    logmsg("< NPN_InvokeProc:%p, obj: %p, methodName: %p, argCount:%d\n", npp, obj, methodName, argCount);
    return false;
}

void
NPN_ReleaseVariantValueProc(NPVariant *variant)
{
    logmsg("< NPN_ReleaseVariantValueProc\n");
}

NPObject *
NPN_CreateObjectProc(NPP npp, NPClass *aClass)
{
    NPObject *npobj;

    logmsg("< NPN_CreateObjectProc\n");
    assert(aClass);

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
    logmsg("< NPN_RetainObjectProc\n");
    assert(obj);
    obj->referenceCount++;
    return obj;
}

void
NPN_ReleaseObjectProc(NPObject *obj)
{
    logmsg("< NPN_ReleaseObjectProc\n");
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

    logmsg("< NPN_GetValueProc %d\n", variable);

    browserObject.referenceCount++;

    retPtr = (NPObject**)ret_value;
    *retPtr = &browserObject;

    return NPERR_NO_ERROR;
}

#define AUTH_CALLBACK_SCRIPT "authDoCallback(\"UnityEngine.GameObject\");"
#define HOMEPAGE_CALLBACK_SCRIPT "HomePage(\"UnityEngine.GameObject\");"
#define PAGEOUT_CALLBACK_SCRIPT "PageOut(\"UnityEngine.GameObject\");"
#define NAVIGATE_SCRIPT "location.href=\""

#define TARGET_REGISTER "https://audience.fusionfall.com/ff/regWizard.do?_flowId=fusionfall-registration-flow"
#define TARGET_MANAGE_ACCOUNT "https://audience.fusionfall.com/ff/login.do"
#define TARGET_COMMUNITY "http://forums.fusionfall.com"

#define DISCORD_LINK "https://discord.gg/DYavckB"

void
handle_navigation(char *target)
{
    if (strncmp(target, TARGET_REGISTER, sizeof(TARGET_REGISTER) - 2) == 0) {
        show_error_dialog("The register page is currently unimplemented.\n\n" \
            "You can still create an account: type your desired username and password into the provided boxes and click \"Log In\". " \
            "Your account will then be automatically created on the server. \nBe sure to remember these details!");
    } else if (strncmp(target, TARGET_MANAGE_ACCOUNT, sizeof(TARGET_MANAGE_ACCOUNT) - 2) == 0) {
        show_error_dialog("Account management is not available.");
    } else if (strncmp(target, TARGET_COMMUNITY, sizeof(TARGET_COMMUNITY) - 2) == 0) {
        open_link(DISCORD_LINK);
    } else {
        logmsg("Unhandled navigation target: %s\n", target);
    }
}

char *
get_navigation_target(const char *script)
{
    char *found;

    found = strstr(script, NAVIGATE_SCRIPT);
    if (!found) {
        return NULL;
    }

    return found + sizeof(NAVIGATE_SCRIPT) - 1;
}

void
unity_send_message(const char *class, const char *msg, NPVariant val)
{
    NPVariant args[3];
    NPVariant ret;

    assert(scriptableObject->_class);
    assert(scriptableObject->_class->hasMethod(scriptableObject, getNPIdentifier("SendMessage")));

    STRINGZ_TO_NPVARIANT(class, args[0]);
    STRINGZ_TO_NPVARIANT(msg, args[1]);
    args[2] = val;
    scriptableObject->_class->invoke(scriptableObject, getNPIdentifier("SendMessage"), args, ARRLEN(args), &ret);
}

void
auth(char *username, char *password)
{
    NPVariant val;
    logmsg("Auto-auth as %s\n", username);

    STRINGZ_TO_NPVARIANT(username, val);
    unity_send_message("GlobalManager", "SetTEGid", val);

    STRINGZ_TO_NPVARIANT(password, val);
    unity_send_message("GlobalManager", "SetAuthid", val);

    INT32_TO_NPVARIANT(0, val);
    unity_send_message("GlobalManager", "DoAuth", val);
}

bool
NPN_EvaluateProc(NPP npp, NPObject *obj, NPString *script, NPVariant *result)
{
    char *navigationTarget;

    logmsg("< NPN_EvaluateProc %s\n", script->UTF8Characters);

    /* Evaluates JS calls, like MarkProgress(1), most of which doesn't need to do anything. */
    if (strncmp(script->UTF8Characters, HOMEPAGE_CALLBACK_SCRIPT, sizeof(HOMEPAGE_CALLBACK_SCRIPT)) == 0
        || strncmp(script->UTF8Characters, PAGEOUT_CALLBACK_SCRIPT, sizeof(PAGEOUT_CALLBACK_SCRIPT)) == 0) {
        /* Gracefully exit game. */
        PostQuitMessage(0);
    } else if (strncmp(script->UTF8Characters, AUTH_CALLBACK_SCRIPT, sizeof(AUTH_CALLBACK_SCRIPT)) == 0) {
        /* Execute authentication callback */
        if (args.tegId != NULL && args.authId != NULL) {
            auth(args.tegId, args.authId);
        }
    } else {
        /* If navigation, handle */
        navigationTarget = get_navigation_target(script->UTF8Characters);
        if (navigationTarget != NULL) {
            handle_navigation(navigationTarget);
        }
    }

    *result = (NPVariant){
        .type = NPVariantType_Void
    };

    return true;
}

NPIdentifier
NPN_GetStringIdentifierProc(const NPUTF8* name)
{
    logmsg("< NPN_GetStringIdentifierProc %s\n", name);

    return getNPIdentifier(name);
}

void
NPN_GetStringIdentifiersProc(const NPUTF8** names, int32_t nameCount, NPIdentifier* identifiers)
{
    int i;

    logmsg("< NPN_GetStringIdentifiersProc %d\n", nameCount);

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

    logmsg("< NPAllocateFunction %p\n", aClass);
    assert(aClass);

    if (aClass->allocate)
        npobj = aClass->allocate(instance, aClass);
    else
        npobj = malloc(sizeof(*npobj));

    if (npobj) {
        npobj->_class = aClass;
        npobj->referenceCount = 1;
    }

    return npobj;
}

void
NPDeallocateFunction(NPObject *npobj)
{
    logmsg("< NPDeallocateFunction %p\n", npobj);
    assert(npobj != &browserObject);

    free(npobj);
}

void
NPInvalidateFunction(NPObject *npobj)
{
    logmsg("< NPInvalidateFunction %p\n", npobj);
}

bool
NPHasMethodFunction(NPObject *npobj, NPIdentifier name)
{
    logmsg("< NPHasMethodFunction %p %p\n", npobj, name);

    return 0;
}

bool
NPInvokeFunction(NPObject *npobj, NPIdentifier name, const NPVariant *args, uint32_t argCount, NPVariant *result)
{
    logmsg("< NPInvokeFunction %p %p\n", npobj, name);

    return 0;
}

bool
NPInvokeDefaultFunction(NPObject *npobj, const NPVariant *args, uint32_t argCount, NPVariant *result)
{
    logmsg("< NPInvokeDefaultFunction %p\n", npobj);

    return 0;
}

bool
NPHasPropertyFunction(NPObject *npobj, NPIdentifier name)
{
    logmsg("< NPHasPropertyFunction %p\n", npobj);

    return 0;
}

bool
NPGetPropertyFunction(NPObject *npobj, NPIdentifier name, NPVariant *result)
{
    logmsg("< NPGetPropertyFunction %p\n", npobj);

    return 0;
}

bool
NPSetPropertyFunction(NPObject *npobj, NPIdentifier name, const NPVariant *value)
{
    logmsg("< NPSetPropertyFunction %p\n", npobj);

    return 0;
}

bool
NPRemovePropertyFunction(NPObject *npobj, NPIdentifier name)
{
    logmsg("< NPRemovePropertyFunction %p\n", npobj);

    return 0;
}

bool
NPEnumerationFunction(NPObject *npobj, NPIdentifier **value, uint32_t *count)
{
    logmsg("< NPEnumerationFunction %p\n", npobj);

    return 0;
}

bool
NPConstructFunction(NPObject *npobj, const NPVariant *args, uint32_t argCount, NPVariant *result)
{
    logmsg("< NPConstructFunction %p\n", npobj);

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
parse_args(int argc, char **argv)
{
    ARG_BEGIN {
        if (ARG_LONG("verbose")) case 'v': {
            args.verboseLogging = true;
            ARG_FLAG();
        } else if (ARG_LONG("main")) case 'm': {
            args.mainPathOrAddress = ARG_VAL();
        } else if (ARG_LONG("log")) case 'l': {
            args.logPath = ARG_VAL();
        } else if (ARG_LONG("address")) case 'a': {
            args.serverAddress = ARG_VAL();
        } else if (ARG_LONG("asseturl")) {
            args.assetUrl = ARG_VAL();
        } else if (ARG_LONG("rankurl")) case 'r': {
            args.rankUrl = ARG_VAL();
        } else if (ARG_LONG("images")) case 'i': {
            args.imagesUrl = ARG_VAL();
        } else if (ARG_LONG("sponsor")) case 's': {
            args.sponsorImageUrl = ARG_VAL();
        } else if (ARG_LONG("username")) case 'u': {
            args.tegId = ARG_VAL();
        } else if (ARG_LONG("token")) case 't': {
            args.authId = ARG_VAL();
        } else if (ARG_LONG("help")) case 'h': {
            printf("Usage: ffrunner.exe [OPTION...]\n");
            puts("  -m, --main=STR      The main URL to load");
            puts("  -l, --log=STR       The path to the log file");
            puts("  -v, --verbose       Enable verbose logging");
            puts("  -a, --address=STR   The address of the server");
            puts("      --asseturl=STR  The URL of the CDN for assets");
            puts("  -r, --rankurl=STR   The rank endpoint URL");
            puts("  -i, --images=STR    The images endpoint URL");
            puts("  -s, --sponsor=STR   The sponsor image URL");
            puts("  -u, --username=STR  Username for auto-login");
            puts("  -t, --token=STR     Password or token for auto-login");
            puts("  -h, --help          Display this");
            puts("");
            exit(0);
        }
    } ARG_END;

    if (args.mainPathOrAddress == NULL) {
        args.mainPathOrAddress = FALLBACK_SRC_URL;
    }

    if (args.logPath == NULL) {
        args.logPath = LOG_FILE_PATH;
    }
}

void
print_args()
{
    printf("main: %s\n", args.mainPathOrAddress);
    printf("log: %s\n", args.logPath);
    printf("verbose: %s\n", args.verboseLogging ? "true" : "false");
    printf("address: %s\n", args.serverAddress);
    printf("asseturl: %s\n", args.assetUrl);
    printf("rankurl: %s\n", args.rankUrl);
    printf("images: %s\n", args.imagesUrl);
    printf("sponsor: %s\n", args.sponsorImageUrl);
    printf("username: %s\n", args.tegId);
    printf("token: %s\n", args.authId == NULL ? "(null)" : "********");
}

int
main(int argc, char **argv)
{
    char* srcUrl;
    DWORD err;
    wchar_t cwd[MAX_PATH];
    NPError ret;
    HMODULE loader;
    RECT winRect;

    parse_args(argc, argv);
    print_args();
    init_logging(args.logPath, args.verboseLogging);

    if (args.serverAddress == NULL) {
        logmsg("No server address provided.");
        exit(1);
    }

    if (args.assetUrl == NULL) {
        logmsg("No asset URL provided.");
        exit(1);
    }

    if (GetCurrentDirectoryW(MAX_PATH, cwd)) {
        logmsg("setenv(\"%ls\")\n", cwd);
        SetEnvironmentVariableW(L"UNITY_HOME_DIR", cwd);
    }
    SetEnvironmentVariableA("UNITY_DISABLE_PLUGIN_UPDATES", "yes");

    initNetscapeFuncs();
    initBrowserObject();

    logmsg("LoadLibraryA\n");
    loader = LoadLibraryA("npUnity3D32.dll");
    if (!loader) {
        err = GetLastError();
        logmsg("Failed to load plugin DLL: 0x%x\n", err);
        exit(1);
    }

    logmsg("GetProcAddress\n");
    NP_GetEntryPointsFuncOS NP_GetEntryPoints = (NP_GetEntryPointsFuncOS)GetProcAddress(loader, "NP_GetEntryPoints");
    NP_InitializeFuncOS NP_Initialize = (NP_InitializeFuncOS)GetProcAddress(loader, "NP_Initialize");
    NP_ShutdownFuncOS NP_Shutdown = (NP_ShutdownFuncOS)GetProcAddress(loader, "NP_Shutdown");

    if (!NP_GetEntryPoints || !NP_Initialize || !NP_Shutdown) {
        logmsg("Failed to find one or more plugin symbols. Invalid plugin DLL?\n");
        exit(1);
    }

    prepare_window();
    assert(hwnd);

    srcUrl = args.mainPathOrAddress;
    init_network(srcUrl);

    logmsg("> NP_GetEntryPoints\n");
    ret = NP_GetEntryPoints(&pluginFuncs);
    logmsg("returned %d\n", ret);

    logmsg("> NP_Initialize\n");
    ret = NP_Initialize(&netscapeFuncs);
    logmsg("returned %d\n", ret);

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
    char *argp[] = {
        srcUrl,
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
    assert(ARRLEN(argn) == ARRLEN(argp));

    logmsg("> NPP_NewProc\n");
    ret = pluginFuncs.newp("application/vnd.ffuwp", &npp, 1, ARRLEN(argn), argn, argp, &saved);
    logmsg("returned %d\n", ret);

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

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

    logmsg("> NPP_SetWindowProc\n");
    ret = pluginFuncs.setwindow(&npp, &npWin);
    logmsg("returned %d\n", ret);


    logmsg("> NPP_GetValueProc\n");
    ret = pluginFuncs.getvalue(&npp, NPPVpluginScriptableNPObject, &scriptableObject);
    logmsg("returned %d and NPObject %p\n", ret, scriptableObject);
    assert(scriptableObject->_class);

    logmsg("> scriptableObject.hasMethod style\n");
    ret = scriptableObject->_class->hasMethod(scriptableObject, getNPIdentifier("style"));
    logmsg("returned %d\n", ret);

    message_loop();

    return 0;
}
