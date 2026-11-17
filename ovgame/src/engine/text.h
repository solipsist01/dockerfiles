#pragma once
// Tekst en rechthoeken op het scherm (HUD). Het lettertype wordt bij het opstarten
// met Windows GDI in een textuur getekend (font-atlas).
#include "gl.h"
#include <string>
#include <vector>

struct Color4 { float r, g, b, a; };

class TextRenderer {
public:
    bool init();
    void begin(int screenW, int screenH);
    void rect(float x, float y, float w, float h, Color4 c);
    float text(float x, float y, const std::string& s, float size, Color4 c);  // geeft breedte terug
    float measure(const std::string& s, float size) const;
    void end();

private:
    struct Glyph { float u0, v0, u1, v1, w; };
    Glyph glyphs_[256] = {};
    float lineH_ = 1;
    GLuint prog_ = 0, vao_ = 0, vbo_ = 0, tex_ = 0;
    GLint uScreen_ = -1;
    int sw_ = 1, sh_ = 1;
    float whiteU_ = 0, whiteV_ = 0;
    std::vector<float> verts_;
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color4 c);
};
