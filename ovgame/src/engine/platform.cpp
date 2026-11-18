#include "platform.h"
#include "gl.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <xinput.h>

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

// ---------- controller (XInput; ook de Steam Deck onder Proton) ----------
// In het spel worden knoppen vertaald naar de toetsen die het spel al kent; de rechterstick wordt muisbeweging.
// In menu's en op de kaart (muis niet gevangen) is de linkerstick een muiscursor, A klikken, B terug.
typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD, XINPUT_STATE*);
PFN_XInputGetState g_xget = nullptr;
bool g_xtried = false;
bool g_padKeys[256] = {};
bool g_padA = false;
float g_padWheel = 0;
double g_padLast = 0;
double g_padRetry = 0;   // geen controller: niet elke frame opnieuw zoeken (XInputGetState is dan traag)
float g_curX = -1, g_curY = -1;

float stick(SHORT v, float dead) {
    float f = v / 32767.0f;
    if (f > 1) f = 1;
    if (f < -1) f = -1;
    float a = std::fabs(f);
    if (a < dead) return 0;
    a = (a - dead) / (1 - dead);
    return f < 0 ? -a : a;
}

void pollPad(Input& in, double now) {
    if (!g_xtried) {
        g_xtried = true;
        const char* dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
        for (const char* d : dlls)
            if (HMODULE m = LoadLibraryA(d)) {
                g_xget = (PFN_XInputGetState)(void*)GetProcAddress(m, "XInputGetState");
                if (g_xget) break;
            }
    }
    float dt = (float)std::min(0.05, now - g_padLast);
    g_padLast = now;
    bool keys[256] = {};
    XINPUT_STATE st{};
    bool ok = g_xget && now >= g_padRetry && g_xget(0, &st) == ERROR_SUCCESS;
    if (g_xget && !ok && now >= g_padRetry) g_padRetry = now + 2.0;
    bool aNow = false;
    if (ok) {
        const XINPUT_GAMEPAD& p = st.Gamepad;
        WORD b = p.wButtons;
        float lx = stick(p.sThumbLX, 0.24f), ly = stick(p.sThumbLY, 0.24f);
        float rx = stick(p.sThumbRX, 0.20f), ry = stick(p.sThumbRY, 0.20f);
        bool lt = p.bLeftTrigger > 100, rt = p.bRightTrigger > 100;
        bool any = b || lx || ly || rx || ry || lt || rt;
        if (any) in.pad = true;
        if (in.captured) {
            if (ly > 0.3f) keys['W'] = true;
            if (ly < -0.3f) keys['S'] = true;
            if (lx > 0.3f) keys['D'] = true;
            if (lx < -0.3f) keys['A'] = true;
            // kijken: kwadratische curve (fijn richten, snel draaien), ~3 rad/s op volle uitslag
            in.mouseDX += rx * std::fabs(rx) * 1400.0f * dt;
            in.mouseDY -= ry * std::fabs(ry) * 1000.0f * dt;
            if (b & XINPUT_GAMEPAD_A) keys[VK_SPACE] = true;           // springen / omhoog (2x = vliegen)
            if (b & XINPUT_GAMEPAD_X) keys['E'] = true;                // kiezen / instappen
            if (b & XINPUT_GAMEPAD_B) keys['Q'] = true;                // deuren
            if (b & XINPUT_GAMEPAD_Y) keys[VK_F7] = true;              // vliegen: snel aan/uit; anders spelerslijst
            if (b & XINPUT_GAMEPAD_LEFT_SHOULDER) keys['B'] = true;    // stopknop
            if (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) keys['R'] = true;   // deur rechts
            if (lt) keys[VK_SHIFT] = true;                             // sneller / omlaag (vliegen)
            if (rt) keys[VK_CONTROL] = true;                           // omhoog (vliegen)
            if (b & XINPUT_GAMEPAD_DPAD_UP) keys['K'] = true;          // cabine activeren
            if (b & XINPUT_GAMEPAD_DPAD_DOWN) keys['P'] = true;        // pantograaf
            if (b & XINPUT_GAMEPAD_DPAD_LEFT) keys['O'] = true;        // kaartje laten zien
            if (b & XINPUT_GAMEPAD_DPAD_RIGHT) keys['N'] = true;       // navigatielijn
            if (b & XINPUT_GAMEPAD_LEFT_THUMB) keys['F'] = true;       // vliegen
            if (b & XINPUT_GAMEPAD_RIGHT_THUMB) keys[VK_F5] = true;    // camera
        } else {
            // virtuele muis
            if (lx || ly) {
                POINT cp;
                GetCursorPos(&cp);
                float sp = 1100.0f * (g_h > 0 ? g_h / 900.0f : 1.0f) * dt;
                if (g_curX < 0 || std::fabs(g_curX - cp.x) > 2 || std::fabs(g_curY - cp.y) > 2) { g_curX = (float)cp.x; g_curY = (float)cp.y; }
                g_curX += lx * std::fabs(lx) * sp;
                g_curY -= ly * std::fabs(ly) * sp;
                SetCursorPos((int)g_curX, (int)g_curY);
                POINT c{(LONG)g_curX, (LONG)g_curY};
                ScreenToClient(g_hwnd, &c);
                in.mouseX = (float)c.x;
                in.mouseY = (float)c.y;
            }
            aNow = (b & XINPUT_GAMEPAD_A) != 0;
            if (aNow && !g_padA) { in.lDown = true; in.lClick = true; }
            if (!aNow && g_padA) { in.lDown = false; in.lRelease = true; }
            if (b & XINPUT_GAMEPAD_B) keys[VK_ESCAPE] = true;
            if (b & XINPUT_GAMEPAD_DPAD_UP) keys[VK_UP] = true;
            if (b & XINPUT_GAMEPAD_DPAD_DOWN) keys[VK_DOWN] = true;
            // scrollen / zoomen met de rechterstick (en het kruis)
            g_padWheel += ry * 8.0f * dt;
            if (b & XINPUT_GAMEPAD_DPAD_UP) g_padWheel += 6.0f * dt;
            if (b & XINPUT_GAMEPAD_DPAD_DOWN) g_padWheel -= 6.0f * dt;
            if (!ry && !(b & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN))) g_padWheel = 0;
            while (g_padWheel >= 1) { in.wheel += 1; g_padWheel -= 1; }
            while (g_padWheel <= -1) { in.wheel -= 1; g_padWheel += 1; }
        }
        if (b & XINPUT_GAMEPAD_START) keys[VK_ESCAPE] = true;          // menu
        if (b & XINPUT_GAMEPAD_BACK) keys['M'] = true;                 // kaart
        // in het spel maar muis (nog) niet gevangen: de controller vangt hem, zoals een klik
        if (any && !in.captured && in.autoCapture && !(b & (XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B)) && !(lx || ly))
            Platform::setCapture(true);
    }
    if (g_padA && !aNow && !ok) { in.lDown = false; in.lRelease = true; }
    g_padA = aNow;
    for (int k = 0; k < 256; k++) {
        if (keys[k] && !g_padKeys[k]) {
            if (!in.down[k]) in.pressed[k] = true;
            in.down[k] = true;
        } else if (!keys[k] && g_padKeys[k]) {
            in.down[k] = false;
        }
        g_padKeys[k] = keys[k];
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
        in.pad = false;
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
    pollPad(g_input, Platform::time());
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
