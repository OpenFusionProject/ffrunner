#include "ffrunner.h"

HWND hwnd;

LRESULT CALLBACK
window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    RECT windowRect;
    UINT windowState;
    PAINTSTRUCT ps;
    HDC hdc;
    Request *req;

    switch (uMsg) {
    case WM_CLOSE:
    case WM_DESTROY:
        pluginFuncs.setwindow(&npp, NULL);
        PostQuitMessage(0);
        return 0;
    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);

        /* All painting occurs here, between BeginPaint and EndPaint. */

        FillRect(hdc, &ps.rcPaint, (HBRUSH) (COLOR_WINDOW+1));

        EndPaint(hwnd, &ps);
        return 0;
    case WM_MOVE:
        if (GetClientRect(hwnd, &windowRect)) {
            npWin.x = windowRect.left;
            npWin.y = windowRect.top;
        }

        pluginFuncs.setwindow(&npp, &npWin);
        /* Let DefWindowProc handle the rest. */
        break;
    case WM_SIZE:
        windowState = wParam;

        npWin.clipRect.top = 0;
        npWin.clipRect.left = 0;

        if (windowState == SIZE_MINIMIZED) {
            npWin.clipRect.right = 0;
            npWin.clipRect.bottom = 0;
        } else {
            if (GetClientRect(hwnd, &windowRect)) {
                npWin.width = windowRect.right - windowRect.left;
                npWin.height = windowRect.bottom - windowRect.top;
                npWin.clipRect.right = npWin.width;
                npWin.clipRect.bottom = npWin.height;
            }
        }

        pluginFuncs.setwindow(&npp, &npWin);
        /* Let DefWindowProc handle the rest. */
        break;
    }

    if (uMsg == ioMsg) {
        req = (Request*)lParam;
        handle_io_progress(req);
        SetEvent(req->readyEvent);
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void
prepare_window(uint32_t width, uint32_t height, const char *iconFile)
{
    WNDCLASSW wc = {0};
    HICON hIcon = NULL;
    int x, y;
    wchar_t *windowName;
    size_t windowNameLen;

    wc.lpfnWndProc   = window_proc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.lpszClassName = CLASS_NAME;
    wc.style         = CS_DBLCLKS;

    if (iconFile) {
        hIcon = (HICON)LoadImageA(NULL, iconFile, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
        if (!hIcon) {
            logmsg("Failed to load icon from %s: %d\n", iconFile, GetLastError());
        } else {
            DeleteFileA(iconFile);
        }
    }

    if (!hIcon) {
        hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(0));
    }

    wc.hIcon = hIcon;

    RegisterClassW(&wc);

    x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    windowNameLen = strlen(args.windowName) + 1;
    windowName = (wchar_t*)malloc(sizeof(wchar_t) * windowNameLen);
    mbstowcs(windowName, args.windowName, windowNameLen);

    hwnd = CreateWindowExW(0, CLASS_NAME, windowName, WS_OVERLAPPEDWINDOW, x, y, width, height, 0, 0, GetModuleHandleW(NULL), 0);
    free(windowName);
}

void
show_error_dialog(char *msg)
{
    MessageBoxA(hwnd, msg, "Sorry!", MB_OK | MB_ICONERROR | MB_TOPMOST);
}

void
open_link(char *url)
{
    ShellExecuteA(hwnd, "open", url, NULL, NULL, SW_NORMAL);
}

void
message_loop(void)
{
    MSG msg = {0};

    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);

        /*
         * HACK: under Wine, mouse scroll messages are buffered until mouse movement (cause unknown).
         * Posting a mouse movement message does not work as a workaround, and neither does sending
         * a 0-pixel raw input (maybe optimized out by Wine).
         * What DOES work is sending two net-zero mouse moves. There is not jitter and the
         * scrolling is processed immediately. ¯\_(ツ)_/¯
         */
        if (msg.message == WM_MOUSEWHEEL || msg.message == WM_MOUSEHWHEEL) {
            INPUT in[2] = {0};
            /* one pixel move left */
            in[0].type = INPUT_MOUSE;
            in[0].mi.dx = 1;
            in[0].mi.dy = 0;
            in[0].mi.dwFlags = MOUSEEVENTF_MOVE;
            //
            in[1].type = INPUT_MOUSE;
            in[1].mi.dx = -1;
            in[1].mi.dy = 0;
            in[1].mi.dwFlags = MOUSEEVENTF_MOVE;
            SendInput(2, in, sizeof(INPUT));
        }
    }
}

static
HMONITOR
get_primary_monitor()
{
    POINT ptZero = {0, 0};
    return MonitorFromPoint(ptZero, MONITOR_DEFAULTTOPRIMARY);
}

static
bool
get_vram_from_dxgi(size_t* vramBytes)
{
	*vramBytes = 0;
    HMONITOR monitor = get_primary_monitor();

    IDXGIFactory* factory = NULL;
    CreateDXGIFactory(&IID_IDXGIFactory, (void**)&factory);
    if (factory == NULL) {
        return false;
    }

    for (int i = 0; ; i++) {
        bool isPrimaryAdapter = false;
        IDXGIAdapter* adapter = NULL;
        if (FAILED(factory->lpVtbl->EnumAdapters(factory, i, &adapter)))
            break;

        for (int j = 0; ; j++) {
            IDXGIOutput* output = NULL;
            if (FAILED(adapter->lpVtbl->EnumOutputs(adapter, j, &output)))
                break;

            DXGI_OUTPUT_DESC outputDesc = { 0 };
            if (SUCCEEDED(output->lpVtbl->GetDesc(output, &outputDesc)))
            {
                if (outputDesc.Monitor == monitor)
                    isPrimaryAdapter = true;
            }

            output->lpVtbl->Release(output);
        }

        if (!isPrimaryAdapter) {
            adapter->lpVtbl->Release(adapter);
            continue;
        }

        DXGI_ADAPTER_DESC adapterDesc = { 0 };
        HRESULT hr = adapter->lpVtbl->GetDesc(adapter, &adapterDesc);
        adapter->lpVtbl->Release(adapter);
        if (SUCCEEDED(hr)) {
            *vramBytes = adapterDesc.DedicatedVideoMemory + adapterDesc.SharedSystemMemory;
            factory->lpVtbl->Release(factory);
            return true;
        }
    }

    factory->lpVtbl->Release(factory);
    return false;
}

void
apply_vram_fix()
{
    size_t vramBytes;
    if (!get_vram_from_dxgi(&vramBytes)) {
        logmsg("Failed to get VRAM size from DXGI; game will try to query it\n");
        return;
    }

    size_t vramMegabytes = vramBytes >> 20;
    logmsg("VRAM size: %zu bytes (%zu MB)\n", vramBytes, vramMegabytes);
    char vramMbStr[32];
    snprintf(vramMbStr, sizeof(vramMbStr), "%zu", vramMegabytes);
    logmsg("setenv(\"UNITY_FF_VRAM_MB\", \"%s\")\n", vramMbStr);
    SetEnvironmentVariableA("UNITY_FF_VRAM_MB", vramMbStr);
}
