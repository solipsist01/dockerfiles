// OV Game launcher: installeert en werkt de game bij vanaf een OV Game server en start hem.
//
//  - Het serveradres zit in de exe zelf: de server schrijft het in g_serverSlot als je de launcher downloadt
//    (zie server/web.go). Anders: server.txt in de installatiemap, of de speler typt het in.
//  - Installatiemap: %LOCALAPPDATA%\OVGame. Daar staan OVGame.exe, OVLauncher.exe en data\ (de wereld die tijdens het
//    spelen binnenkomt blijft ook daar bewaard).
//  - /api/client geeft per bestand crc32, grootte en pad. Alleen wat anders is wordt gedownload; bestanden die de server
//    niet meer heeft (en die de launcher zelf ooit installeerde) worden opgeruimd.
//  - Een nieuwe versie van de launcher zelf: de draaiende exe wordt hernoemd (dat mag in Windows), de nieuwe komt
//    ervoor in de plaats en start opnieuw.
//  - Eerste keer: snelkoppeling "OV Game" op het bureaublad.
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "../net/net.h"

// Plek voor het serveradres (de server vervangt "@@OVSERVERURL@@..." door "OVURL=https://...")
extern "C" {
__attribute__((used)) volatile char g_serverSlot[512] = "@@OVSERVERURL@@";
}

namespace {

std::wstring W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
std::string U(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring installDir() {
    wchar_t buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf))) return std::wstring(buf) + L"\\OVGame";
    return L"C:\\OVGame";
}
std::wstring selfPath() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}
bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
void makeDirsFor(const std::wstring& file) {
    for (size_t i = 3; i < file.size(); i++)
        if (file[i] == L'\\') CreateDirectoryW(file.substr(0, i).c_str(), nullptr);
}
bool readFile(const std::wstring& p, std::string& out) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0;
    bool ok = sz.QuadPart == 0 || (ReadFile(h, &out[0], (DWORD)out.size(), &got, nullptr) && got == out.size());
    CloseHandle(h);
    return ok;
}
bool writeFile(const std::wstring& p, const std::string& data) {
    makeDirsFor(p);
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = data.empty() || (WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr) && put == data.size());
    CloseHandle(h);
    return ok;
}
uint64_t fileSize(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return UINT64_MAX;
    return ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
}
std::wstring winPath(const std::wstring& dir, const std::string& rel) {
    std::wstring p = dir + L"\\" + W(rel);
    for (wchar_t& c : p) if (c == L'/') c = L'\\';
    return p;
}
std::string human(uint64_t b) {
    char s[32];
    if (b > (1u << 20)) snprintf(s, sizeof s, "%.1f MB", b / 1048576.0);
    else snprintf(s, sizeof s, "%.0f kB", b / 1024.0);
    return s;
}

// ---------- gedeelde toestand (werkthread <-> venster) ----------
struct Shared {
    CRITICAL_SECTION cs;
    std::wstring status, detail;
    double progress = 0;  // 0..1
    enum Phase { WORKING, ASK_URL, FAILED, DONE } phase = WORKING;
    bool restartSelf = false;
    std::string url;
    bool cancel = false;
};
Shared g;
HWND g_wnd, g_status, g_detail, g_bar, g_edit, g_btnMain, g_btnAlt;
std::wstring g_dir;
bool g_startGame = true;

void setStatus(const std::wstring& s, const std::wstring& d = L"", double p = -1) {
    EnterCriticalSection(&g.cs);
    g.status = s;
    g.detail = d;
    if (p >= 0) g.progress = p;
    LeaveCriticalSection(&g.cs);
}

struct MfEntry { uint32_t crc; uint64_t size; std::string path; };

bool parseManifest(const std::string& body, std::vector<MfEntry>& out) {
    if (body.rfind("OVCLIENT 1", 0) != 0) return false;
    size_t pos = body.find('\n') + 1;
    while (pos < body.size()) {
        size_t end = body.find('\n', pos);
        if (end == std::string::npos) end = body.size();
        std::string line = body.substr(pos, end - pos);
        pos = end + 1;
        unsigned crc;
        unsigned long long sz;
        int off = 0;
        if (sscanf(line.c_str(), "%x %llu %n", &crc, &sz, &off) == 2 && off > 0) {
            std::string p = line.substr(off);
            while (!p.empty() && (p.back() == '\r' || p.back() == ' ')) p.pop_back();
            // veiligheid: geen absolute paden of ..
            if (p.empty() || p.find("..") != std::string::npos || p[0] == '/' || p.find(':') != std::string::npos) continue;
            out.push_back({crc, sz, p});
        }
    }
    return true;
}

// installed.idx: wat de launcher heeft geïnstalleerd (crc grootte pad)
std::map<std::string, std::pair<uint32_t, uint64_t>> loadInstalled() {
    std::map<std::string, std::pair<uint32_t, uint64_t>> m;
    std::string s;
    if (!readFile(g_dir + L"\\installed.idx", s)) return m;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find('\n', pos);
        if (end == std::string::npos) end = s.size();
        std::string line = s.substr(pos, end - pos);
        pos = end + 1;
        unsigned crc;
        unsigned long long sz;
        int off = 0;
        if (sscanf(line.c_str(), "%x %llu %n", &crc, &sz, &off) == 2 && off > 0) m[line.substr(off)] = {crc, sz};
    }
    return m;
}

void createShortcut() {
    wchar_t desk[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desk))) return;
    std::wstring lnk = std::wstring(desk) + L"\\OV Game.lnk";
    if (exists(lnk) || exists(g_dir + L"\\shortcut.done")) return;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW* sl = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) {
        std::wstring target = g_dir + L"\\OVLauncher.exe";
        sl->SetPath(target.c_str());
        sl->SetWorkingDirectory(g_dir.c_str());
        sl->SetDescription(L"OV Game (multiplayer)");
        sl->SetIconLocation((g_dir + L"\\OVGame.exe").c_str(), 0);
        IPersistFile* pf = nullptr;
        if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
            pf->Save(lnk.c_str(), TRUE);
            pf->Release();
        }
        sl->Release();
    }
    CoUninitialize();
    writeFile(g_dir + L"\\shortcut.done", "1");  // niet opnieuw maken als de speler hem weggooit
}

// ---------- het werk ----------
DWORD WINAPI worker(LPVOID) {
    EnterCriticalSection(&g.cs);
    std::string base = Net::normalizeBase(g.url);
    LeaveCriticalSection(&g.cs);
    setStatus(L"Verbinden met " + W(base) + L"...", L"", 0);

    Net::Response r;
    for (int attempt = 0; attempt < 20; attempt++) {
        r = Net::get(base + "/api/client", nullptr, 20000);
        if (r.status != 503) break;
        setStatus(L"De server bereidt de wereld nog voor...", L"even geduld", 0);
        Sleep(3000);
    }
    std::vector<MfEntry> mf;
    if (!r.ok() || !parseManifest(r.body, mf) || mf.empty()) {
        std::string why = !r.error.empty() ? r.error : r.status ? "HTTP " + std::to_string(r.status) : "onbekende fout";
        if (r.ok()) why = "de server heeft (nog) geen game-bestanden";
        EnterCriticalSection(&g.cs);
        g.phase = Shared::FAILED;
        g.status = L"Kan de server niet bereiken";
        g.detail = W(base + ": " + why);
        LeaveCriticalSection(&g.cs);
        return 0;
    }
    writeFile(g_dir + L"\\server.txt", base + "\n");

    // wat moet er gebeuren?
    auto installed = loadInstalled();
    std::vector<const MfEntry*> todo;
    uint64_t total = 0;
    setStatus(L"Bestanden controleren...", L"", 0);
    std::wstring self = selfPath();
    std::wstring selfInstalled = g_dir + L"\\OVLauncher.exe";
    bool runningInstalled = lstrcmpiW(self.c_str(), selfInstalled.c_str()) == 0;
    for (const MfEntry& e : mf) {
        std::wstring p = winPath(g_dir, e.path);
        auto it = installed.find(e.path);
        uint64_t sz = fileSize(p);
        bool ok = it != installed.end() && it->second.first == e.crc && it->second.second == e.size && sz == e.size;
        if (!ok && sz == e.size) {  // geen index (of oud): de inhoud controleren
            std::string d;
            if (readFile(p, d) && Net::crc32(d.data(), d.size()) == e.crc) ok = true;
        }
        if (!ok) { todo.push_back(&e); total += e.size; }
    }

    // downloaden
    uint64_t done = 0;
    std::string idx;
    for (const MfEntry& e : mf) {
        bool need = false;
        for (const MfEntry* t : todo) if (t == &e) need = true;
        if (!need) {
            char line[1100];
            snprintf(line, sizeof line, "%08x %llu %s\n", e.crc, (unsigned long long)e.size, e.path.c_str());
            idx += line;
        }
    }
    for (size_t i = 0; i < todo.size(); i++) {
        const MfEntry& e = *todo[i];
        char h[16];
        snprintf(h, sizeof h, "%08x", e.crc);
        std::string err;
        std::string data;
        for (int attempt = 0; attempt < 3 && data.empty(); attempt++) {
            Net::Response fr = Net::get(base + "/c/" + Net::urlEncode(e.path) + "?h=" + h, [&](uint64_t got, uint64_t) {
                double p = total ? (double)(done + got) / total : 1.0;
                setStatus(L"Bijwerken: " + std::to_wstring(i + 1) + L" van " + std::to_wstring(todo.size()),
                          W(e.path + "  (" + human(done + got) + " van " + human(total) + ")"), p);
                EnterCriticalSection(&g.cs);
                bool c = g.cancel;
                LeaveCriticalSection(&g.cs);
                return !c;
            }, 60000);
            if (!fr.ok()) { err = fr.error.empty() ? "HTTP " + std::to_string(fr.status) : fr.error; continue; }
            if (fr.body.size() != e.size || Net::crc32(fr.body.data(), fr.body.size()) != e.crc) { err = "controle mislukt"; continue; }
            data.swap(fr.body);
        }
        if (data.empty() && e.size > 0) {
            EnterCriticalSection(&g.cs);
            g.phase = Shared::FAILED;
            g.status = L"Download mislukt";
            g.detail = W(e.path + ": " + err);
            LeaveCriticalSection(&g.cs);
            writeFile(g_dir + L"\\installed.idx", idx);
            return 0;
        }
        std::wstring p = winPath(g_dir, e.path);
        bool isSelf = runningInstalled && lstrcmpiW(p.c_str(), selfInstalled.c_str()) == 0;
        std::wstring tmp = p + L".part";
        bool ok = writeFile(tmp, data);
        if (ok && isSelf) {
            // de draaiende launcher vervangen: hernoemen mag, overschrijven niet
            std::wstring old = p + L".old";
            DeleteFileW(old.c_str());
            ok = MoveFileExW(p.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING) && MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING);
            if (ok) g.restartSelf = true;
        } else if (ok) {
            ok = MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        if (!ok) {
            DeleteFileW(tmp.c_str());
            EnterCriticalSection(&g.cs);
            g.phase = Shared::FAILED;
            g.status = L"Kan niet schrijven";
            g.detail = p + L" (draait de game nog?)";
            LeaveCriticalSection(&g.cs);
            writeFile(g_dir + L"\\installed.idx", idx);
            return 0;
        }
        done += e.size;
        char line[1100];
        snprintf(line, sizeof line, "%08x %llu %s\n", e.crc, (unsigned long long)e.size, e.path.c_str());
        idx += line;
    }
    // opruimen: wat de launcher ooit installeerde maar de server niet meer heeft
    for (auto& kv : installed) {
        bool still = false;
        for (const MfEntry& e : mf) if (e.path == kv.first) still = true;
        if (!still) DeleteFileW(winPath(g_dir, kv.first).c_str());
    }
    writeFile(g_dir + L"\\installed.idx", idx);
    createShortcut();

    EnterCriticalSection(&g.cs);
    g.phase = Shared::DONE;
    g.status = todo.empty() ? L"De game is up-to-date" : L"Bijgewerkt";
    g.detail = todo.empty() ? L"" : W(std::to_string(todo.size()) + " bestand(en), " + human(total));
    g.progress = 1;
    LeaveCriticalSection(&g.cs);
    return 0;
}

void startWorker() {
    EnterCriticalSection(&g.cs);
    g.phase = Shared::WORKING;
    g.cancel = false;
    LeaveCriticalSection(&g.cs);
    ShowWindow(g_edit, SW_HIDE);
    EnableWindow(g_btnMain, FALSE);
    SetWindowTextW(g_btnMain, L"Spelen");
    ShowWindow(g_btnAlt, SW_HIDE);
    CloseHandle(CreateThread(nullptr, 0, worker, nullptr, 0, nullptr));
}

bool launchGame() {
    std::wstring exe = g_dir + L"\\OVGame.exe";
    if (!exists(exe)) return false;
    std::wstring cmd = L"\"" + exe + L"\" --server \"" + W(Net::normalizeBase(g.url)) + L"\"";
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, g_dir.c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void restartSelf() {
    std::wstring exe = g_dir + L"\\OVLauncher.exe";
    std::wstring cmd = L"\"" + exe + L"\" --updated";
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi;
    if (CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, g_dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void askUrl(const std::wstring& why) {
    EnterCriticalSection(&g.cs);
    g.phase = Shared::ASK_URL;
    g.status = why;
    g.detail = L"Typ het adres van de server, bijv. https://ovgame.example.nl";
    LeaveCriticalSection(&g.cs);
    ShowWindow(g_edit, SW_SHOW);
    SetWindowTextW(g_edit, W(g.url).c_str());
    SetWindowTextW(g_btnMain, L"Verbinden");
    EnableWindow(g_btnMain, TRUE);
    SetFocus(g_edit);
}

Shared::Phase g_lastPhase = Shared::WORKING;

LRESULT CALLBACK wndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER: {
            EnterCriticalSection(&g.cs);
            std::wstring s = g.status, d = g.detail;
            double p = g.progress;
            Shared::Phase ph = g.phase;
            LeaveCriticalSection(&g.cs);
            wchar_t cur[512];
            GetWindowTextW(g_status, cur, 512);
            if (s != cur) SetWindowTextW(g_status, s.c_str());
            GetWindowTextW(g_detail, cur, 512);
            if (d != cur) SetWindowTextW(g_detail, d.c_str());
            SendMessageW(g_bar, PBM_SETPOS, (WPARAM)(p * 1000), 0);
            if (ph != g_lastPhase) {
                g_lastPhase = ph;
                if (ph == Shared::DONE) {
                    if (g.restartSelf) { restartSelf(); DestroyWindow(hw); break; }
                    if (g_startGame) {
                        if (launchGame()) { DestroyWindow(hw); break; }
                        SetWindowTextW(g_status, L"De game kon niet worden gestart");
                    }
                    EnableWindow(g_btnMain, TRUE);
                } else if (ph == Shared::FAILED) {
                    SetWindowTextW(g_btnMain, L"Opnieuw");
                    EnableWindow(g_btnMain, TRUE);
                    ShowWindow(g_btnAlt, exists(g_dir + L"\\OVGame.exe") ? SW_SHOW : SW_HIDE);
                    ShowWindow(g_edit, SW_SHOW);
                    SetWindowTextW(g_edit, W(g.url).c_str());
                }
            }
            break;
        }
        case WM_COMMAND:
            if ((HWND)lp == g_btnMain) {
                Shared::Phase ph;
                EnterCriticalSection(&g.cs);
                ph = g.phase;
                LeaveCriticalSection(&g.cs);
                if (ph == Shared::DONE) {
                    if (launchGame()) DestroyWindow(hw);
                } else {
                    wchar_t buf[600];
                    GetWindowTextW(g_edit, buf, 600);
                    std::string u = Net::normalizeBase(U(buf));
                    if (u.empty()) break;
                    g.url = u;
                    startWorker();
                }
            } else if ((HWND)lp == g_btnAlt) {
                if (launchGame()) DestroyWindow(hw);  // zonder bijwerken spelen (de game probeert de server zelf ook)
            }
            break;
        case WM_CTLCOLORSTATIC:
            SetBkMode((HDC)wp, TRANSPARENT);
            if ((HWND)lp == g_status) SetTextColor((HDC)wp, RGB(0x00, 0x30, 0x82));
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        case WM_CLOSE:
            EnterCriticalSection(&g.cs);
            g.cancel = true;
            LeaveCriticalSection(&g.cs);
            DestroyWindow(hw);
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcW(hw, msg, wp, lp);
    }
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    InitializeCriticalSection(&g.cs);
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    g_dir = installDir();
    CreateDirectoryW(g_dir.c_str(), nullptr);
    DeleteFileW((g_dir + L"\\OVLauncher.exe.old").c_str());
    std::wstring args = cmdLine ? cmdLine : L"";
    if (args.find(L"--nostart") != std::wstring::npos) g_startGame = false;

    // serveradres: uit de exe (bij het downloaden ingevuld), --server, of server.txt
    char slot[513] = {};
    for (int i = 0; i < 512; i++) slot[i] = g_serverSlot[i];
    std::string url;
    if (strncmp(slot, "OVURL=", 6) == 0) url = slot + 6;
    size_t sp = args.find(L"--server ");
    if (sp != std::wstring::npos) {
        std::wstring rest = args.substr(sp + 9);
        size_t e = rest.find(L' ');
        url = U(rest.substr(0, e));
    }
    if (url.empty()) {
        std::string t;
        if (readFile(g_dir + L"\\server.txt", t)) url = t;
    }
    g.url = Net::normalizeBase(url);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"OVLauncher";
    wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1));
    RegisterClassW(&wc);
    int ww = 560, wh = 250;
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    g_wnd = CreateWindowExW(0, L"OVLauncher", L"OV Game", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            (wa.left + wa.right - ww) / 2, (wa.top + wa.bottom - wh) / 2, ww, wh, nullptr, nullptr, inst, nullptr);
    NONCLIENTMETRICSW ncm = {sizeof ncm};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    HFONT font = CreateFontIndirectW(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 3 / 2;
    ncm.lfMessageFont.lfWeight = FW_SEMIBOLD;
    HFONT big = CreateFontIndirectW(&ncm.lfMessageFont);
    auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int w, int h, HFONT f) {
        HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_wnd, nullptr, inst, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        return c;
    };
    g_status = mk(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 20, 16, 510, 30, big);
    g_detail = mk(L"STATIC", L"", SS_LEFT | SS_PATHELLIPSIS, 20, 50, 510, 20, font);
    g_bar = mk(PROGRESS_CLASSW, L"", 0, 20, 80, 510, 18, font);
    SendMessageW(g_bar, PBM_SETRANGE32, 0, 1000);
    g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 20, 112, 510, 26, g_wnd, nullptr, inst, nullptr);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)font, TRUE);
    g_btnMain = mk(L"BUTTON", L"Spelen", BS_DEFPUSHBUTTON, 390, 160, 140, 34, font);
    g_btnAlt = mk(L"BUTTON", L"Toch spelen", BS_PUSHBUTTON, 240, 160, 140, 34, font);
    ShowWindow(g_btnAlt, SW_HIDE);
    EnableWindow(g_btnMain, FALSE);
    ShowWindow(g_wnd, SW_SHOW);
    SetTimer(g_wnd, 1, 100, nullptr);

    if (g.url.empty()) askUrl(L"Met welke server wil je spelen?");
    else startWorker();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN && IsWindowEnabled(g_btnMain)) {
            SendMessageW(g_wnd, WM_COMMAND, 0, (LPARAM)g_btnMain);
            continue;
        }
        if (!IsDialogMessageW(g_wnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    return 0;
}
