#include "renderer.h"
#include "platform.h"
#include <algorithm>
#include <cstdio>
#include <string>

Mesh createMesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices) {
    Mesh m;
    if (verts.empty() || indices.empty()) return m;
    m.bmin = m.bmax = Vec3(verts[0].px, verts[0].py, verts[0].pz);
    for (const Vertex& v : verts) {
        m.bmin.x = std::min(m.bmin.x, v.px); m.bmax.x = std::max(m.bmax.x, v.px);
        m.bmin.y = std::min(m.bmin.y, v.py); m.bmax.y = std::max(m.bmax.y, v.py);
        m.bmin.z = std::min(m.bmin.z, v.pz); m.bmax.z = std::max(m.bmax.z, v.pz);
    }
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &m.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
    const GLsizei stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)(9 * sizeof(float)));
    glBindVertexArray(0);
    m.count = (GLsizei)indices.size();
    return m;
}

void destroyMesh(Mesh& m) {
    if (m.vbo) glDeleteBuffers(1, &m.vbo);
    if (m.ibo) glDeleteBuffers(1, &m.ibo);
    if (m.vao) glDeleteVertexArrays(1, &m.vao);
    m = Mesh();
}

void drawMesh(const Mesh& m) {
    if (!m.count) return;
    glBindVertexArray(m.vao);
    glDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, nullptr);
}

static GLuint compileShader(GLenum type, const char* src, const char* name) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::string msg = std::string("Shader '") + name + "' compileert niet:\n" + log;
        Platform::showError(msg.c_str());
    }
    return s;
}

GLuint compileProgram(const char* vs, const char* fs, const char* name) {
    GLuint p = glCreateProgram();
    GLuint v = compileShader(GL_VERTEX_SHADER, vs, name);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs, name);
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::string msg = std::string("Shader '") + name + "' linkt niet:\n" + log;
        Platform::showError(msg.c_str());
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

void Frustum::fromMatrix(const Mat4& vp) {
    const float* m = vp.m;
    auto row = [&](int r, int i) { return m[i * 4 + r]; };
    for (int i = 0; i < 4; i++) {
        p[0][i] = row(3, i) + row(0, i);
        p[1][i] = row(3, i) - row(0, i);
        p[2][i] = row(3, i) + row(1, i);
        p[3][i] = row(3, i) - row(1, i);
        p[4][i] = row(3, i) + row(2, i);
        p[5][i] = row(3, i) - row(2, i);
    }
}

bool Frustum::boxVisible(Vec3 mn, Vec3 mx) const {
    for (int i = 0; i < 6; i++) {
        float x = p[i][0] >= 0 ? mx.x : mn.x;
        float y = p[i][1] >= 0 ? mx.y : mn.y;
        float z = p[i][2] >= 0 ? mx.z : mn.z;
        if (p[i][0] * x + p[i][1] * y + p[i][2] * z + p[i][3] < 0) return false;
    }
    return true;
}

bool Renderer::init() {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    return true;
}

void Renderer::resize(int w, int h) {
    if (w == w_ && h == h_ && fbo_) return;
    w_ = w;
    h_ = h;
    rebuild();
}

void Renderer::setQuality(int samples, float scale) {
    GLint maxS = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxS);
    samples = std::max(0, std::min(samples, (int)maxS));
    scale = std::max(0.4f, std::min(1.0f, scale));
    if (samples == samples_ && scale == scale_) return;
    samples_ = samples;
    scale_ = scale;
    if (w_ > 0) rebuild();
}

void Renderer::rebuild() {
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        glDeleteRenderbuffers(1, &rbColor_);
        glDeleteRenderbuffers(1, &rbDepth_);
        fbo_ = 0;
    }
    if (fboRes_) {
        glDeleteFramebuffers(1, &fboRes_);
        glDeleteRenderbuffers(1, &rbRes_);
        fboRes_ = 0;
    }
    if (w_ <= 0 || h_ <= 0) return;
    rw_ = std::max(1, (int)(w_ * scale_ + 0.5f));
    rh_ = std::max(1, (int)(h_ * scale_ + 0.5f));
    auto storage = [&](GLenum fmt, int smp) {
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, smp, fmt, rw_, rh_);  // 0 samples = gewone buffer
    };
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glGenRenderbuffers(1, &rbColor_);
    glBindRenderbuffer(GL_RENDERBUFFER, rbColor_);
    storage(GL_RGBA8, samples_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbColor_);
    glGenRenderbuffers(1, &rbDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, rbDepth_);
    storage(GL_DEPTH_COMPONENT24, samples_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbDepth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        Platform::showError("Kon geen framebuffer maken.");
    }
    // multisample en geschaald: eerst oplossen op renderresolutie, dan pas (lineair) opschalen
    if (samples_ > 0 && (rw_ != w_ || rh_ != h_)) {
        glGenFramebuffers(1, &fboRes_);
        glBindFramebuffer(GL_FRAMEBUFFER, fboRes_);
        glGenRenderbuffers(1, &rbRes_);
        glBindRenderbuffer(GL_RENDERBUFFER, rbRes_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, 0, GL_RGBA8, rw_, rh_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbRes_);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::beginScene(Vec3 c) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, rw_, rh_);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glClearColor(c.x, c.y, c.z, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::endScene() {
    GLuint src = fbo_;
    if (fboRes_) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboRes_);
        glBlitFramebuffer(0, 0, rw_, rh_, 0, 0, rw_, rh_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        src = fboRes_;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    bool same = rw_ == w_ && rh_ == h_;
    glBlitFramebuffer(0, 0, rw_, rh_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT, same ? GL_NEAREST : GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w_, h_);
}

// ---------- PNG-schrijver (ongecomprimeerde deflate-blokken, geen externe library nodig) ----------
static uint32_t crcTable[256];
static void initCrc() {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
}
static void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}
static void writeChunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> buf;
    put32(buf, (uint32_t)data.size());
    buf.insert(buf.end(), type, type + 4);
    buf.insert(buf.end(), data.begin(), data.end());
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 4; i < buf.size(); i++) c = crcTable[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    put32(buf, c ^ 0xFFFFFFFFu);
    fwrite(buf.data(), 1, buf.size(), f);
}

bool Renderer::saveScreenshot(const char* path) {
    initCrc();
    int w = w_, h = h_;
    std::vector<uint8_t> px((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(w * 3 + 1) * h);
    for (int y = h - 1; y >= 0; y--) {
        raw.push_back(0);
        for (int x = 0; x < w; x++) {
            const uint8_t* p = &px[((size_t)y * w + x) * 4];
            raw.push_back(p[0]); raw.push_back(p[1]); raw.push_back(p[2]);
        }
    }
    std::vector<uint8_t> z{0x78, 0x01};
    size_t pos = 0;
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    do {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(n & 0xFF); z.push_back(n >> 8);
        z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    } while (pos < raw.size());
    put32(z, (b << 16) | a);

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr;
    put32(ihdr, w); put32(ihdr, h);
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    writeChunk(f, "IHDR", ihdr);
    writeChunk(f, "IDAT", z);
    writeChunk(f, "IEND", {});
    fclose(f);
    return true;
}
