#include "ffrunner.h"

LRESULT CALLBACK
window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT width, height;
    PAINTSTRUCT ps;
    HDC hdc;

    switch (uMsg) {
    case WM_CLOSE:
        /* fall-through */
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
    case WM_SIZE:
        width = LOWORD(lParam);
        height = HIWORD(lParam);

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
    WNDCLASS wc = {0};
    HWND hwnd;

    wc.lpfnWndProc   = window_proc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = CLASS_NAME;

    HICON hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(0));
    wc.hIcon = hIcon;

    RegisterClass(&wc);

    hwnd = CreateWindowExA(0, CLASS_NAME, "FusionFall", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, WIDTH, HEIGHT, 0, 0, GetModuleHandleA(0), 0);

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    return hwnd;
}

void
message_loop(void)
{
    MSG msg = {0};

    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        handle_requests();
    }
}
