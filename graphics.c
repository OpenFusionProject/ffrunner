#include "ffrunner.h"

#include "SDL2/SDL.h"
#include "SDL2/SDL_syswm.h"

HWND hwnd;
#if 0
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
#endif
void
prepare_window(void)
{
#if 0
    WNDCLASS wc = {0};
    HICON hIcon;

    wc.lpfnWndProc   = window_proc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = CLASS_NAME;

    hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(0));
    wc.hIcon = hIcon;

    RegisterClass(&wc);

    hwnd = CreateWindowExA(0, CLASS_NAME, "FusionFall", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, WIDTH, HEIGHT, 0, 0, GetModuleHandleA(0), 0);
#endif

    SDL_Window *sdlWindow;
    SDL_SysWMinfo info;

    if (SDL_Init(SDL_INIT_VIDEO) == -1) {
        SDL_Log("SDL_Init failed: %s\n", SDL_GetError());
        exit(1);
    }

    sdlWindow = SDL_CreateWindow("FusionFall", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            1280, 720, 0);
    if (!sdlWindow) {
        SDL_Log("SDL_CreateWindow failed: %s\n", SDL_GetError());
        exit(1);
    }

    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(sdlWindow, &info)) {
        SDL_Log("SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
        exit(1);
    }

    hwnd = info.info.win.window;
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
