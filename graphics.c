#include "ffrunner.h"

LRESULT CALLBACK
window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT width, height;
    PAINTSTRUCT ps;
    HDC hdc;

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
    HICON hIcon;

    wc.lpfnWndProc   = window_proc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = CLASS_NAME;

    hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(0));
    wc.hIcon = hIcon;

    RegisterClass(&wc);

    hwnd = CreateWindowExA(0, CLASS_NAME, "FusionFall", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, WIDTH, HEIGHT, 0, 0, GetModuleHandleA(0), 0);

    return hwnd;
}

void
message_loop(void)
{
    Request *req;
    bool done;
    MSG msg = {0};

    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        if (msg.message == ioMsg) {
            req = (Request*)msg.lParam;
            done = handle_io_progress(req);
            if (done) {
                free(req);
            } else {
                SetEvent(req->readyEvent);
            }
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}
