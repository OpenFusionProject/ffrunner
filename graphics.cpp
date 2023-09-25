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

HWND global_handle;

LRESULT CALLBACK
window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // All painting occurs here, between BeginPaint and EndPaint.

            FillRect(hdc, &ps.rcPaint, (HBRUSH) (COLOR_WINDOW+1));

            EndPaint(hwnd, &ps);
        }
        return 0;
    case WM_SIZE:
        UINT width = LOWORD(lParam);
        UINT height = HIWORD(lParam);

        npWin.width = npWin.clipRect.right = width;
        npWin.height = npWin.clipRect.bottom = height;
        pluginFuncs.setwindow(&npp, &npWin);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

HWND
prepare_window(void)
{
    WNDCLASS wc = { };

    wc.lpfnWndProc   = window_proc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    HWND hwnd = CreateWindowExA(0, CLASS_NAME, "FusionFall", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, WIDTH, HEIGHT, 0, 0, GetModuleHandleA(0), 0);

    global_handle = hwnd;

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    return hwnd;
}

void
message_loop(void)
{
    MSG msg = { };
    int i = 0;

    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        //UpdateWindow(global_handle); // TODO: didn't help'
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        handle_requests();
    }
}
