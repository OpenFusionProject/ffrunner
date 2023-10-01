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

#include "ffrunner.h"

bool windowChanged = false;
HWND hwnd;
CRITICAL_SECTION graphicsCrit;

LRESULT CALLBACK
window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT width, height;
    PAINTSTRUCT ps;
    HDC hdc;

    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
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

        EnterCriticalSection(&graphicsCrit);
        npWin.width = npWin.clipRect.right = width;
        npWin.height = npWin.clipRect.bottom = height;
        windowChanged = true;
        LeaveCriticalSection(&graphicsCrit);

        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void
prepare_window(void)
{
    WNDCLASS wc = {0};
    hwnd;

    wc.lpfnWndProc   = window_proc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    hwnd = CreateWindowExA(0, CLASS_NAME, "FusionFall", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, WIDTH, HEIGHT, 0, 0, GetModuleHandleA(0), 0);

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
}

void
message_loop(void)
{
    MSG msg = {0};

    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        //handle_requests();
    }
}

DWORD WINAPI
graphics_thread(void *unused)
{
    RECT winRect;

    EnterCriticalSection(&graphicsCrit);

    prepare_window();
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

    windowChanged = true;

    LeaveCriticalSection(&graphicsCrit);

    message_loop();

    /* not reached until quit */
    return 0;

}
