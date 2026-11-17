#include "text.h"
#include "renderer.h"
#include <cstring>

static const char* TEXT_VS = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aCol;
uniform vec2 uScreen;
out vec2 vUV; out vec4 vCol;
void main() {
    vUV = aUV; vCol = aCol;
    gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
})";

static const char* TEXT_FS = R"(#version 330 core
in vec2 vUV; in vec4 vCol;
uniform sampler2D uTex;
out vec4 oColor;
void main() { oColor = vec4(vCol.rgb, vCol.a * texture(uTex, vUV).r); })";

static const int AW = 1024, AH = 512;
static const int FONT_PX = 40;

bool TextRenderer::init() {
    // Font-atlas tekenen met GDI (Latin-1 tekenset, dus ook é, ë enz.)
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = AW;
    bi.bmiHeader.biHeight = -AH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(dc, bmp);
    memset(bits, 0, AW * AH * 4);
    HFONT font = CreateFontA(-FONT_PX, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    SelectObject(dc, font);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    TEXTMETRICA tm;
    GetTextMetricsA(dc, &tm);
    lineH_ = (float)tm.tmHeight;

    int x = 16, y = 2;
    for (int c = 32; c < 256; c++) {
        if (c >= 127 && c < 160) continue;
        char ch = (char)c;
        SIZE sz;
        GetTextExtentPoint32A(dc, &ch, 1, &sz);
        if (x + sz.cx + 6 > AW) { x = 2; y += tm.tmHeight + 4; }
        TextOutA(dc, x, y, &ch, 1);
        Glyph& g = glyphs_[c];
        g.u0 = (float)x / AW;
        g.v0 = (float)y / AH;
        g.u1 = (float)(x + sz.cx) / AW;
        g.v1 = (float)(y + tm.tmHeight) / AH;
        g.w = (float)sz.cx;
        x += sz.cx + 6;
    }
    GdiFlush();
    std::vector<uint8_t> alpha(AW * AH);
    const uint8_t* src = (const uint8_t*)bits;
    for (int i = 0; i < AW * AH; i++) alpha[i] = src[i * 4 + 1];
    for (int yy = 0; yy < 8; yy++)
        for (int xx = 0; xx < 8; xx++) alpha[yy * AW + xx] = 255;  // wit blokje voor rechthoeken
    whiteU_ = 4.0f / AW;
    whiteV_ = 4.0f / AH;
    DeleteObject(font);
    DeleteObject(bmp);
    DeleteDC(dc);

    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, AW, AH, 0, GL_RED, GL_UNSIGNED_BYTE, alpha.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    prog_ = compileProgram(TEXT_VS, TEXT_FS, "text");
    uScreen_ = glGetUniformLocation(prog_, "uScreen");
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizei stride = 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float)));
    glBindVertexArray(0);
    return true;
}

void TextRenderer::begin(int w, int h) {
    sw_ = w;
    sh_ = h;
    verts_.clear();
}

void TextRenderer::quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color4 c) {
    const float q[6][4] = {{x0, y0, u0, v0}, {x1, y0, u1, v0}, {x1, y1, u1, v1},
                           {x0, y0, u0, v0}, {x1, y1, u1, v1}, {x0, y1, u0, v1}};
    for (auto& p : q) {
        verts_.insert(verts_.end(), {p[0], p[1], p[2], p[3], c.r, c.g, c.b, c.a});
    }
}

void TextRenderer::rect(float x, float y, float w, float h, Color4 c) {
    quad(x, y, x + w, y + h, whiteU_, whiteV_, whiteU_, whiteV_, c);
}

float TextRenderer::measure(const std::string& s, float size) const {
    float scale = size / FONT_PX, w = 0;
    for (unsigned char ch : s) w += glyphs_[ch].w * scale;
    return w;
}

float TextRenderer::text(float x, float y, const std::string& s, float size, Color4 c) {
    float scale = size / FONT_PX;
    float cx = x;
    for (unsigned char ch : s) {
        const Glyph& g = glyphs_[ch];
        if (g.w <= 0) continue;
        float w = g.w * scale, h = lineH_ * scale;
        if (ch != ' ') {
            quad(cx + 1.5f * scale, y + 1.5f * scale, cx + w + 1.5f * scale, y + h + 1.5f * scale, g.u0, g.v0, g.u1, g.v1,
                 {0, 0, 0, c.a * 0.6f});  // schaduw
            quad(cx, y, cx + w, y + h, g.u0, g.v0, g.u1, g.v1, c);
        }
        cx += w;
    }
    return cx - x;
}

void TextRenderer::end() {
    if (verts_.empty()) return;
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(prog_);
    glUniform2f(uScreen_, (float)sw_, (float)sh_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts_.size() * sizeof(float), verts_.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(verts_.size() / 8));
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}
