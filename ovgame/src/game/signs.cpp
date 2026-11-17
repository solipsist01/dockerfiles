#include "signs.h"
#include <cstdio>
#include <cstring>
#include "world.h"

static const int SA = 4096;  // atlasgrootte (stations, treinen en buslijnen)
int g_signAtlasFull = 0;    // aantal teksten dat niet meer paste (diagnose)

bool SignAtlas::begin() {
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = SA;
    bi.bmiHeader.biHeight = -SA;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(dc, bmp);
    memset(bits, 0, SA * SA * 4);
    dc_ = dc;
    bmp_ = bmp;
    bits_ = (uint8_t*)bits;
    x_ = 2; y_ = 2; rowH_ = 0;
    return true;
}

SignRect SignAtlas::add(const std::string& text, Vec3 bg, Vec3 fg, int height, bool bold, float padX) {
    char key[512];
    snprintf(key, sizeof(key), "%s|%.3f,%.3f,%.3f|%.3f,%.3f,%.3f|%d|%d|%.2f", text.c_str(), bg.x, bg.y, bg.z, fg.x, fg.y, fg.z, height, (int)bold, padX);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    SignRect res = addNew(text, bg, fg, height, bold, padX);
    cache_[key] = res;
    return res;
}

SignRect SignAtlas::addNew(const std::string& text, Vec3 bg, Vec3 fg, int height, bool bold, float padX) {
    HDC dc = (HDC)dc_;
    HFONT font = CreateFontA(-height, 0, 0, 0, bold ? FW_BOLD : FW_SEMIBOLD, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    HGDIOBJ old = SelectObject(dc, font);
    SIZE sz;
    GetTextExtentPoint32A(dc, text.c_str(), (int)text.size(), &sz);
    int padx = (int)(height * padX), pady = (int)(height * 0.25f);
    int w = sz.cx + padx * 2, h = sz.cy + pady * 2;
    if (x_ + w + 2 > SA) { x_ = 2; y_ += rowH_ + 2; rowH_ = 0; }
    if (y_ + h + 2 > SA) { SelectObject(dc, old); DeleteObject(font); g_signAtlasFull++; return {0, 0, 0, 0, 1}; }  // atlas vol
    RECT r{x_, y_, x_ + w, y_ + h};
    HBRUSH br = CreateSolidBrush(RGB((int)(bg.x * 255), (int)(bg.y * 255), (int)(bg.z * 255)));
    FillRect(dc, &r, br);
    DeleteObject(br);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB((int)(fg.x * 255), (int)(fg.y * 255), (int)(fg.z * 255)));
    TextOutA(dc, x_ + padx, y_ + pady, text.c_str(), (int)text.size());
    GdiFlush();
    SelectObject(dc, old);
    DeleteObject(font);
    SignRect sr;
    // halve pixel naar binnen om randbloeding te voorkomen
    sr.u0 = (x_ + 0.5f) / SA; sr.v0 = (y_ + 0.5f) / SA;
    sr.u1 = (x_ + w - 0.5f) / SA; sr.v1 = (y_ + h - 0.5f) / SA;
    sr.aspect = (float)w / h;
    x_ += w + 2;
    rowH_ = std::max(rowH_, h);
    return sr;
}

GLuint SignAtlas::finish() {
    for (int i = 0; i < SA * SA; i++) bits_[i * 4 + 3] = 255;
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SA, SA, 0, GL_BGRA, GL_UNSIGNED_BYTE, bits_);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
    glGetError();
    DeleteObject((HBITMAP)bmp_);
    DeleteDC((HDC)dc_);
    return tex;
}

void SignAtlas::quad(MeshBuilder& mb, const SignRect& r, Vec3 c, Vec3 right, float h, bool doubleSided) {
    float w = h * r.aspect;
    Vec3 up(0, 1, 0);
    Vec3 n = normalize(cross(right, up));  // voorkant: kijker die naar -n kijkt heeft 'right' rechts
    Vec3 hr = right * (w / 2), hu = up * (h / 2);
    Vec3 off = n * 0.012f;
    auto V = [&](Vec3 p, float u, float v) { return mb.vert(p, n, Vec3(u, v, 0), EX_SIGN); };
    uint32_t a = V(c - hr - hu + off, r.u0, r.v1), b = V(c + hr - hu + off, r.u1, r.v1);
    uint32_t cc = V(c + hr + hu + off, r.u1, r.v0), d = V(c - hr + hu + off, r.u0, r.v0);
    mb.i.insert(mb.i.end(), {a, b, cc, a, cc, d});
    if (doubleSided) {
        // achterkant: gespiegeld zodat de tekst leesbaar is
        Vec3 nb = n * -1.0f;
        auto W = [&](Vec3 p, float u, float v) { return mb.vert(p, nb, Vec3(u, v, 0), EX_SIGN); };
        uint32_t e = W(c + hr - hu - off, r.u0, r.v1), f = W(c - hr - hu - off, r.u1, r.v1);
        uint32_t g = W(c - hr + hu - off, r.u1, r.v0), hh = W(c + hr + hu - off, r.u0, r.v0);
        mb.i.insert(mb.i.end(), {e, f, g, e, g, hh});
    }
}
