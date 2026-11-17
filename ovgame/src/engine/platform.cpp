#include "platform.h"
#include "gl.h"
#include <cstring>

namespace {
HWND g_hwnd = nullptr;
HDC g_dc = nullptr;
HGLRC g_rc = nullptr;
bool g_quit = false;
int g_w = 0, g_h = 0;
Input g_input;
LARGE_INTEGER g_freq, g_start;
int g_mode = 0;                 // Platform::WindowMode
bool g_displayChanged = false;  // exclusief volledig scherm: beeldschermmodus is aangepast
WINDOWPLACEMENT g_placement{sizeof(WINDOWPLACEMENT)};  // venster-plek om naar terug te keren

// exclusief volledig scherm: beeldscherm op de eigen (register)modus zetten, venster erover als topmost
void enterExclusive() {
    HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXA mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoA(mon, &mi);
    DEVMODEA dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsA(mi.szDevice, ENUM_REGISTRY_SETTINGS, &dm)) {
        dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
        g_displayChanged = ChangeDisplaySettingsExA(mi.szDevice, &dm, nullptr, CDS_FULLSCREEN, nullptr) == DISP_CHANGE_SUCCESSFUL;
    }
    GetMonitorInfoA(mon, &mi);
    const RECT& m = mi.rcMonitor;
    SetWindowPos(g_hwnd, HWND_TOPMOST, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}
void leaveExclusive() {
    if (g_displayChanged) ChangeDisplaySettingsA(nullptr, 0);
    g_displayChanged = false;
}

void applyClip() {
    if (g_input.captured) {
        RECT r;
        GetClientRect(g_hwnd, &r);
        POINT a{r.left, r.top}, b{r.right, r.bottom};
        ClientToScreen(g_hwnd, &a);
        ClientToScreen(g_hwnd, &b);
        RECT c{a.x, a.y, b.x, b.y};
        ClipCursor(&c);
    } else {
        ClipCursor(nullptr);
    }
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Input& in = g_input;
    switch (msg) {
    case WM_CLOSE: g_quit = true; leaveExclusive(); return 0;
    case WM_SIZE: g_w = LOWORD(lp); g_h = HIWORD(lp); applyClip(); return 0;
    case WM_MOVE: applyClip(); return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wp < 256) {
            if (!(lp & (1 << 30))) in.pressed[wp] = true;
            in.down[wp] = true;
        }
        if (msg == WM_SYSKEYDOWN && wp == VK_F4) break;  // Alt+F4 laten werken
        return 0;
    case WM_CHAR:
        if (wp == 8 || (wp >= 32 && wp < 256)) in.chars += (char)wp;
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wp < 256) in.down[wp] = false;
        return 0;
    case WM_LBUTTONDOWN:
        in.lDown = true;
        in.lClick = true;
        in.mouseX = (float)(short)LOWORD(lp);
        in.mouseY = (float)(short)HIWORD(lp);
        if (!in.captured && in.autoCapture) Platform::setCapture(true);
        return 0;
    case WM_LBUTTONUP:
        in.lDown = false;
        in.lRelease = true;
        return 0;
    case WM_RBUTTONDOWN:
        in.rClick = true;
        return 0;
    case WM_MOUSEMOVE:
        in.mouseX = (float)(short)LOWORD(lp);
        in.mouseY = (float)(short)HIWORD(lp);
        return 0;
    case WM_MOUSEWHEEL:
        in.wheel += (float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        return 0;
    case WM_INPUT: {
        RAWINPUT ri;
        UINT sz = sizeof(ri);
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
            ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) && in.captured) {
            in.mouseDX += (float)ri.data.mouse.lLastX;
            in.mouseDY += (float)ri.data.mouse.lLastY;
        }
        break;
    }
    case WM_ACTIVATEAPP:
        // exclusief volledig scherm: bij Alt+Tab het bureaublad terugzetten en minimaliseren, bij terugkomen herstellen
        if (g_mode == 2) {
            if (wp) {
                if (!g_displayChanged) {
                    ShowWindow(h, SW_RESTORE);
                    enterExclusive();
                }
            } else {
                leaveExclusive();
                ShowWindow(h, SW_MINIMIZE);
            }
        }
        break;
    case WM_KILLFOCUS:
        Platform::setCapture(false);
        memset(in.down, 0, sizeof(in.down));
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}
}  // namespace

namespace Platform {

bool init(const char* title, int width, int height) {
    SetProcessDPIAware();
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_start);

    HINSTANCE inst = GetModuleHandleA(nullptr);
    WNDCLASSA wc{};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(1));  // game.rc
    if (!wc.hIcon) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = "OVGameWindow";
    RegisterClassA(&wc);

    RECT r{0, 0, width, height};
    DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&r, style, FALSE);
    g_hwnd = CreateWindowA(wc.lpszClassName, title, style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                           r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) return false;
    g_dc = GetDC(g_hwnd);

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(g_dc, &pfd);
    if (!pf || !SetPixelFormat(g_dc, pf, &pfd)) return false;

    // Tijdelijke context om wglCreateContextAttribsARB te krijgen, daarna een echte OpenGL 3.3 core context.
    HGLRC tmp = wglCreateContext(g_dc);
    wglMakeCurrent(g_dc, tmp);
    typedef HGLRC(WINAPI * CreateCtx)(HDC, HGLRC, const int*);
    CreateCtx createCtx = (CreateCtx)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!createCtx) return false;
    const int attribs[] = {0x2091, 3, 0x2092, 3, 0x9126, 1, 0};  // versie 3.3, core profile
    g_rc = createCtx(g_dc, nullptr, attribs);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(tmp);
    if (!g_rc || !wglMakeCurrent(g_dc, g_rc)) return false;

    typedef BOOL(WINAPI * SwapInterval)(int);
    SwapInterval swapInterval = (SwapInterval)wglGetProcAddress("wglSwapIntervalEXT");
    if (swapInterval) swapInterval(1);

    if (!loadGLFunctions()) return false;

    RAWINPUTDEVICE rid{0x01, 0x02, 0, g_hwnd};  // muis
    RegisterRawInputDevices(&rid, 1, sizeof(rid));

    ShowWindow(g_hwnd, SW_SHOW);
    RECT cr;
    GetClientRect(g_hwnd, &cr);
    g_w = cr.right;
    g_h = cr.bottom;
    return true;
}

bool pump() {
    memset(g_input.pressed, 0, sizeof(g_input.pressed));
    g_input.mouseDX = g_input.mouseDY = 0;
    g_input.wheel = 0;
    g_input.lClick = g_input.rClick = g_input.lRelease = false;
    g_input.chars.clear();
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return !g_quit;
}

void swap() { SwapBuffers(g_dc); }
Input& input() { return g_input; }
int width() { return g_w; }
int height() { return g_h; }

double time() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart - g_start.QuadPart) / double(g_freq.QuadPart);
}

void setCapture(bool capture) {
    if (capture == g_input.captured) return;
    g_input.captured = capture;
    ShowCursor(capture ? FALSE : TRUE);
    applyClip();
}

void setTitle(const char* title) { SetWindowTextA(g_hwnd, title); }

void setWindowMode(int mode) {
    if (mode < 0 || mode > 2) mode = 0;
    if (mode == g_mode) return;
    if (g_mode == 0) GetWindowPlacement(g_hwnd, &g_placement);  // venster-plek onthouden
    if (g_mode == 2) leaveExclusive();
    g_mode = mode;
    if (mode == 0) {
        SetWindowLongA(g_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
        SetWindowPlacement(g_hwnd, &g_placement);
        ShowWindow(g_hwnd, g_placement.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    } else {
        SetWindowLongA(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        if (mode == 2) {
            enterExclusive();
        } else {
            MONITORINFO mi{sizeof(mi)};
            GetMonitorInfoA(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
            const RECT& m = mi.rcMonitor;
            SetWindowPos(g_hwnd, HWND_NOTOPMOST, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
    }
    RECT cr;
    GetClientRect(g_hwnd, &cr);
    g_w = cr.right;
    g_h = cr.bottom;
    applyClip();
}

int windowMode() { return g_mode; }

void setVSync(bool on) {
    typedef BOOL(WINAPI * SwapInterval)(int);
    SwapInterval swapInterval = (SwapInterval)wglGetProcAddress("wglSwapIntervalEXT");
    if (swapInterval) swapInterval(on ? 1 : 0);
}

void showError(const char* msg) { MessageBoxA(g_hwnd, msg, "OV Game - fout", MB_OK | MB_ICONERROR); }

}  // namespace Platform
