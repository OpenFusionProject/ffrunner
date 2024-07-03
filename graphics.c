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

NPWindow npWin;
HWND hwnd;

LRESULT CALLBACK
window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT width, height;
    PAINTSTRUCT ps;
    HDC hdc;

    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        SetEvent(shutdownEvent);
        return 0;
    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);

        /* All painting occurs here, between BeginPaint and EndPaint. */

        FillRect(hdc, &ps.rcPaint, (HBRUSH) (COLOR_WINDOW+1));

        EndPaint(hwnd, &ps);
        return 0;
    case WM_SIZE:
        width = LOWORD(lParam);
        height = HIWORD(lParam);
        EnterCriticalSection(&gfxCrit);
        npWin.width = npWin.clipRect.right = width;
        npWin.height = npWin.clipRect.bottom = height;
        LeaveCriticalSection(&gfxCrit);
        SetEvent(updateWindowEvent);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void
prepare_window(void)
{
    RECT winRect;

    WNDCLASS wc = { 0 };
    wc.lpfnWndProc   = window_proc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    hwnd = CreateWindowExA(0, CLASS_NAME, "FusionFall", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, WIDTH, HEIGHT, 0, 0, GetModuleHandleA(0), 0);

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
}

void
message_loop(void)
{
    MSG msg = {0};
    char buf[64];

    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        printf("Message time! hwnd: %p, msg: %x\n", msg.hwnd, msg.message);
        if (GetWindowTextA(msg.hwnd, buf, 64)) {
            printf("\t for window %s\n", buf);
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

DWORD
WINAPI
GfxThread(LPVOID param)
{
    prepare_window();
    message_loop();
}
