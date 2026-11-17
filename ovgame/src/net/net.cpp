// Netwerk via WinHTTP: zie net.h
#include "net.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <cstring>

namespace Net {

static std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static std::string lastError(const char* what) {
    DWORD e = GetLastError();
    const char* msg = "";
    switch (e) {
        case ERROR_WINHTTP_CANNOT_CONNECT: msg = " (kan geen verbinding maken)"; break;
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: msg = " (servernaam onbekend)"; break;
        case ERROR_WINHTTP_TIMEOUT: msg = " (time-out)"; break;
        case ERROR_WINHTTP_SECURE_FAILURE: msg = " (beveiligde verbinding mislukt)"; break;
        case ERROR_WINHTTP_CONNECTION_ERROR: msg = " (verbinding verbroken)"; break;
        default: break;
    }
    return std::string(what) + ": fout " + std::to_string(e) + msg;
}

// Eén WinHTTP-sessie voor het hele programma (thread-safe)
static HINTERNET session() {
    static HINTERNET s = nullptr;
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once, [](PINIT_ONCE, PVOID, PVOID*) -> BOOL {
        s = WinHttpOpen(L"OVGame/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (s) {
            // TLS 1.2 (en 1.3 waar Windows dat kan) expliciet aanzetten; oudere Windows 10-versies doen dat niet altijd vanzelf
            DWORD proto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
            proto |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
            if (!WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof proto)) {
                proto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
                WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof proto);
            }
        }
        return TRUE;
    }, nullptr, nullptr);
    return s;
}

struct Url {
    bool secure = false;
    std::wstring host, path;
    INTERNET_PORT port = 80;
};

static bool crack(const std::string& url, Url& u, std::string& err) {
    std::string s = url;
    if (s.rfind("wss://", 0) == 0) s = "https://" + s.substr(6);
    else if (s.rfind("ws://", 0) == 0) s = "http://" + s.substr(5);
    std::wstring w = widen(s);
    URL_COMPONENTS c;
    memset(&c, 0, sizeof c);
    c.dwStructSize = sizeof c;
    wchar_t host[256], path[2048], extra[2048];
    c.lpszHostName = host; c.dwHostNameLength = 256;
    c.lpszUrlPath = path; c.dwUrlPathLength = 2048;
    c.lpszExtraInfo = extra; c.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(w.c_str(), (DWORD)w.size(), 0, &c)) { err = "Ongeldig adres: " + url; return false; }
    u.secure = c.nScheme == INTERNET_SCHEME_HTTPS;
    u.host.assign(host, c.dwHostNameLength);
    u.path.assign(path, c.dwUrlPathLength);
    u.path.append(extra, c.dwExtraInfoLength);
    if (u.path.empty()) u.path = L"/";
    u.port = c.nPort;
    return true;
}

std::string normalizeBase(const std::string& in) {
    std::string s;
    for (char ch : in) if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n' && ch != '"') s += ch;
    if (s.empty()) return s;
    if (s.find("://") == std::string::npos) s = "https://" + s;
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char ch : s) {
        if (isalnum(ch) || ch == '/' || ch == '-' || ch == '_' || ch == '.' || ch == '~') o += (char)ch;
        else { o += '%'; o += hex[ch >> 4]; o += hex[ch & 15]; }
    }
    return o;
}

Response get(const std::string& url, const Progress& progress, int timeoutMs) {
    Response r;
    Url u;
    if (!crack(url, u, r.error)) return r;
    HINTERNET s = session();
    if (!s) { r.error = lastError("WinHttpOpen"); return r; }
    HINTERNET con = WinHttpConnect(s, u.host.c_str(), u.port, 0);
    if (!con) { r.error = lastError("Verbinden"); return r; }
    HINTERNET req = WinHttpOpenRequest(con, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       u.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { r.error = lastError("Aanvraag"); WinHttpCloseHandle(con); return r; }
    WinHttpSetTimeouts(req, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(req, nullptr)) {
        r.error = lastError("Verbinding");
    } else {
        DWORD code = 0, sz = sizeof code;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
        r.status = (int)code;
        uint64_t total = 0;
        wchar_t lenBuf[32];
        DWORD lb = sizeof lenBuf;
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &lb, WINHTTP_NO_HEADER_INDEX))
            total = _wcstoui64(lenBuf, nullptr, 10);
        if (total > 0 && total < (1ull << 31)) r.body.reserve((size_t)total);
        std::vector<char> buf(1 << 16);
        for (;;) {
            DWORD got = 0;
            if (!WinHttpReadData(req, buf.data(), (DWORD)buf.size(), &got)) { r.error = lastError("Ontvangen"); break; }
            if (got == 0) break;
            r.body.append(buf.data(), got);
            if (progress && !progress(r.body.size(), total)) { r.error = "Afgebroken"; break; }
        }
        if (r.error.empty() && total && r.body.size() != total) r.error = "Download onvolledig";
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    return r;
}

// ---------- CRC32 (zelfde als zlib / Go hash/crc32 IEEE) ----------
uint32_t crc32(const void* data, size_t n, uint32_t crc) {
    static uint32_t table[256];
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once, [](PINIT_ONCE, PVOID, PVOID*) -> BOOL {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        return TRUE;
    }, nullptr, nullptr);
    const uint8_t* p = (const uint8_t*)data;
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// ---------- WebSocket ----------
struct WebSocket::Impl {
    HINTERNET con = nullptr, ws = nullptr;
    HANDLE thread = nullptr;
    CRITICAL_SECTION cs;
    std::deque<std::string> inbox;
    volatile LONG alive = 0;
    std::string reason;
    Impl() { InitializeCriticalSection(&cs); }
    ~Impl() { DeleteCriticalSection(&cs); }

    static DWORD WINAPI reader(LPVOID self) {
        Impl* p = (Impl*)self;
        std::string cur;
        std::vector<char> buf(1 << 16);
        for (;;) {
            DWORD got = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
            DWORD e = WinHttpWebSocketReceive(p->ws, buf.data(), (DWORD)buf.size(), &got, &type);
            if (e != NO_ERROR) {
                EnterCriticalSection(&p->cs);
                if (p->reason.empty()) p->reason = "Verbinding verbroken (fout " + std::to_string(e) + ")";
                LeaveCriticalSection(&p->cs);
                break;
            }
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                USHORT status = 0;
                char rb[128];
                DWORD rl = 0;
                WinHttpWebSocketQueryCloseStatus(p->ws, &status, rb, sizeof rb, &rl);
                EnterCriticalSection(&p->cs);
                if (p->reason.empty()) p->reason = rl ? std::string(rb, rl) : "De server heeft de verbinding gesloten";
                LeaveCriticalSection(&p->cs);
                break;
            }
            cur.append(buf.data(), got);
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
                EnterCriticalSection(&p->cs);
                p->inbox.push_back(std::move(cur));
                if (p->inbox.size() > 5000) p->inbox.pop_front();
                LeaveCriticalSection(&p->cs);
                cur.clear();
            }
            if (cur.size() > (1 << 20)) cur.clear();  // onzin: weggooien
        }
        InterlockedExchange(&p->alive, 0);
        return 0;
    }
};

WebSocket::WebSocket() : p_(new Impl) {}
WebSocket::~WebSocket() { close(); delete p_; }

bool WebSocket::connect(const std::string& url, std::string& error) {
    close();
    Url u;
    if (!crack(url, u, error)) return false;
    HINTERNET s = session();
    p_->con = WinHttpConnect(s, u.host.c_str(), u.port, 0);
    if (!p_->con) { error = lastError("Verbinden"); return false; }
    HINTERNET req = WinHttpOpenRequest(p_->con, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       u.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { error = lastError("Aanvraag"); close(); return false; }
    WinHttpSetTimeouts(req, 15000, 15000, 15000, 15000);
    if (!WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
        !WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(req, nullptr)) {
        error = lastError("Verbinding met de server");
        WinHttpCloseHandle(req);
        close();
        return false;
    }
    DWORD code = 0, sz = sizeof code;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
    if (code != 101) {
        error = "De server accepteert geen WebSocket (HTTP " + std::to_string(code) + ")";
        if (code == 302 || code == 403) error += " - staat Cloudflare Access ervoor?";
        WinHttpCloseHandle(req);
        close();
        return false;
    }
    p_->ws = WinHttpWebSocketCompleteUpgrade(req, 0);
    WinHttpCloseHandle(req);
    if (!p_->ws) { error = lastError("WebSocket"); close(); return false; }
    // geen time-out op het wachten op berichten (de server pingt zelf)
    DWORD zero = 0;
    WinHttpSetOption(p_->ws, WINHTTP_OPTION_RECEIVE_TIMEOUT, &zero, sizeof zero);
    p_->reason.clear();
    p_->inbox.clear();
    p_->alive = 1;
    p_->thread = CreateThread(nullptr, 0, Impl::reader, p_, 0, nullptr);
    return true;
}

bool WebSocket::send(const std::string& text) {
    if (!p_->ws || !p_->alive) return false;
    DWORD e = WinHttpWebSocketSend(p_->ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (PVOID)text.data(), (DWORD)text.size());
    return e == NO_ERROR;
}

bool WebSocket::poll(std::string& msg) {
    EnterCriticalSection(&p_->cs);
    bool any = !p_->inbox.empty();
    if (any) { msg = std::move(p_->inbox.front()); p_->inbox.pop_front(); }
    LeaveCriticalSection(&p_->cs);
    return any;
}

bool WebSocket::open() const { return p_->ws && p_->alive; }

std::string WebSocket::closeReason() const {
    EnterCriticalSection(&p_->cs);
    std::string r = p_->reason;
    LeaveCriticalSection(&p_->cs);
    return r;
}

void WebSocket::close() {
    if (p_->ws) {
        if (p_->alive) WinHttpWebSocketClose(p_->ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        WinHttpCloseHandle(p_->ws);  // maakt ook een wachtende Receive los
        p_->ws = nullptr;
    }
    if (p_->thread) {
        WaitForSingleObject(p_->thread, 3000);
        CloseHandle(p_->thread);
        p_->thread = nullptr;
    }
    if (p_->con) { WinHttpCloseHandle(p_->con); p_->con = nullptr; }
    p_->alive = 0;
}

}  // namespace Net
