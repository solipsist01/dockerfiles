#include "mapview.h"
#include "datastore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include "../third_party/stb_image.h"

using namespace WorldFormat;

namespace {
const char* MAP_VS = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aOff;
layout(location=2) in vec2 aW;
layout(location=3) in vec4 aCol;
layout(location=4) in vec2 aUV;
uniform vec4 uA;      // 2x2: wereld -> scherm (pixels)
uniform vec2 uB;
uniform float uScale; // pixels per meter
uniform vec2 uScreen;
out vec4 vCol; out vec2 vUV;
void main() {
    mat2 A = mat2(uA.x, uA.y, uA.z, uA.w);
    vec2 p = A * aPos + uB;
    if (dot(aOff, aOff) > 0.0) p += normalize(A * aOff) * max(aW.x * uScale, aW.y);
    vCol = aCol; vUV = aUV;
    gl_Position = vec4(p.x / uScreen.x * 2.0 - 1.0, 1.0 - p.y / uScreen.y * 2.0, 0.0, 1.0);
})";
const char* MAP_FS = R"(#version 330 core
in vec4 vCol; in vec2 vUV;
uniform sampler2D uTex;
uniform int uUseTex;
out vec4 oColor;
void main() { oColor = uUseTex == 1 ? vec4(texture(uTex, vUV).rgb, 1.0) * vCol : vCol; })";

uint32_t rgba(float r, float g, float b, float a = 1) {
    auto c = [](float v) { return (uint32_t)(clampf(v, 0, 1) * 255 + 0.5f); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
}

// kleuren (lichte kaartstijl)
const uint32_t C_BG = rgba(0.93f, 0.92f, 0.89f);
const uint32_t C_AREA[7] = {rgba(0.62f, 0.78f, 0.90f), rgba(0.68f, 0.82f, 0.62f), rgba(0.88f, 0.86f, 0.83f), rgba(0.86f, 0.82f, 0.86f),
                            rgba(0.86f, 0.90f, 0.76f), rgba(0.74f, 0.87f, 0.68f), rgba(0.84f, 0.82f, 0.80f)};
struct RoadStyle { float ww, wpx; uint32_t fill, casing; };
const RoadStyle ROAD[7] = {
    {7.0f, 2.2f, rgba(0.95f, 0.62f, 0.35f), rgba(0.70f, 0.40f, 0.20f)},  // snelweg
    {5.5f, 1.8f, rgba(0.99f, 0.84f, 0.50f), rgba(0.70f, 0.58f, 0.35f)},  // hoofdweg
    {4.5f, 1.4f, rgba(1.0f, 0.97f, 0.80f), rgba(0.66f, 0.64f, 0.58f)},   // secundair
    {3.2f, 0.8f, rgba(1.0f, 1.0f, 1.0f), rgba(0.70f, 0.69f, 0.67f)},     // woonstraat
    {1.2f, 0.5f, rgba(0.80f, 0.45f, 0.42f, 0.8f), 0},                    // fiets/voet
    {9.0f, 1.6f, rgba(0.62f, 0.78f, 0.90f), 0},                          // rivier/kanaal
    {2.0f, 0.8f, rgba(0.62f, 0.78f, 0.90f), 0},                          // beek
};
}  // namespace

void MapView::addLine(std::vector<V>& v, std::vector<uint32_t>& idx, const std::vector<std::pair<float, float>>& pts, float ww, float wpx, uint32_t c) {
    for (size_t k = 0; k + 1 < pts.size(); k++) {
        float ax = pts[k].first, az = pts[k].second, bx = pts[k + 1].first, bz = pts[k + 1].second;
        float dx = bx - ax, dz = bz - az, l = std::sqrt(dx * dx + dz * dz);
        if (l < 0.01f) continue;
        float nx = -dz / l, nz = dx / l;
        // iets verlengen zodat de stukken aansluiten
        float ex = dx / l * ww * 0.5f, ez = dz / l * ww * 0.5f;
        uint32_t b = (uint32_t)v.size();
        v.push_back({ax - ex, az - ez, nx, nz, ww, wpx, c, 0, 0});
        v.push_back({ax - ex, az - ez, -nx, -nz, ww, wpx, c, 0, 0});
        v.push_back({bx + ex, bz + ez, -nx, -nz, ww, wpx, c, 0, 0});
        v.push_back({bx + ex, bz + ez, nx, nz, ww, wpx, c, 0, 0});
        idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    }
}

void MapView::upload(Layer& l, const std::vector<V>& v, const std::vector<uint32_t>& i) {
    if (!l.vao) {
        glGenVertexArrays(1, &l.vao);
        glGenBuffers(1, &l.vbo);
        glGenBuffers(1, &l.ibo);
        glBindVertexArray(l.vao);
        glBindBuffer(GL_ARRAY_BUFFER, l.vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, l.ibo);
        GLsizei st = sizeof(V);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, st, (void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, st, (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, st, (void*)(4 * sizeof(float)));
        glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(4); glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, st, (void*)(7 * sizeof(float)));
    }
    glBindVertexArray(l.vao);
    glBindBuffer(GL_ARRAY_BUFFER, l.vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(V), v.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, l.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, i.size() * 4, i.data(), GL_DYNAMIC_DRAW);
    l.count = (GLsizei)i.size();
    glBindVertexArray(0);
}

bool MapView::init(const std::string& dataDir, const World& world) {
    dataDir_ = dataDir;
    prog_ = compileProgram(MAP_VS, MAP_FS, "map");
    uA_ = glGetUniformLocation(prog_, "uA");
    uB_ = glGetUniformLocation(prog_, "uB");
    uScale_ = glGetUniformLocation(prog_, "uScale");
    uScreen_ = glGetUniformLocation(prog_, "uScreen");
    uTex_ = glGetUniformLocation(prog_, "uTex");
    uUseTex_ = glGetUniformLocation(prog_, "uUseTex");
    for (const TileId& t : planTiles()) tiles_.push_back({t.x, t.y, t.detail});

    // ---- OSM-kaartdata ----
    std::vector<std::vector<std::pair<float, float>>> byKind[7];
    FILE* f = fopen((dataDir + "\\world\\map.bin").c_str(), "rb");
    if (f) {
        auto r32 = [&]() { uint32_t v = 0; fread(&v, 4, 1, f); return v; };
        auto rf = [&]() { float v = 0; fread(&v, 4, 1, f); return v; };
        if (r32() == 0x314D564F && r32() == 1) {
            uint32_t nl = r32();
            for (uint32_t k = 0; k < nl; k++) {
                uint8_t kind = 0;
                fread(&kind, 1, 1, f);
                uint32_t n = r32();
                std::vector<std::pair<float, float>> pts(n);
                for (auto& p : pts) { p.first = rf(); p.second = rf(); }
                if (kind < 7) byKind[kind].push_back(std::move(pts));
            }
            uint32_t na = r32();
            for (uint32_t k = 0; k < na; k++) {
                uint8_t kind = 0;
                fread(&kind, 1, 1, f);
                uint32_t nv = r32();
                uint32_t base = (uint32_t)areaV_.size();
                uint32_t c = C_AREA[kind < 7 ? kind : 2];
                for (uint32_t j = 0; j < nv; j++) {
                    float x = rf(), z = rf();
                    areaV_.push_back({x, z, 0, 0, 0, 0, c, 0, 0});
                }
                uint32_t ni = r32();
                std::vector<uint32_t> idx(ni);
                if (ni) fread(idx.data(), 4, ni, f);
                for (uint32_t i : idx) areaI_.push_back(base + i);
            }
            uint32_t nn = r32();
            for (uint32_t k = 0; k < nn; k++) {
                uint8_t kind = 0;
                fread(&kind, 1, 1, f);
                float x = rf(), z = rf();
                uint16_t len = 0;
                fread(&len, 2, 1, f);
                std::string s(len, ' ');
                if (len) fread(&s[0], 1, len, f);
                labels_.push_back({kind, x, z, s});
            }
        }
        fclose(f);
    }
    // wegen: per soort eerst alle randen, dan alle vullingen (soort 6 .. 0, belangrijkste bovenop)
    lineStart_.assign(16, 0);
    int order[7] = {6, 5, 4, 3, 2, 1, 0};
    for (int pass = 0; pass < 2; pass++)
        for (int oi = 0; oi < 7; oi++) {
            int k = order[oi];
            const RoadStyle& st = ROAD[k];
            int slot = pass * 7 + oi;
            lineStart_[slot] = (int)lineI_.size();
            if (pass == 0 && !st.casing) continue;
            for (auto& pts : byKind[k]) {
                if (pass == 0) addLine(lineV_, lineI_, pts, st.ww / 2 + 0.8f, st.wpx + 0.9f, st.casing);
                else addLine(lineV_, lineI_, pts, st.ww / 2, st.wpx, st.fill);
            }
        }
    lineStart_[14] = (int)lineI_.size();
    // spoor en perrons (uit de wereld)
    for (const RailLine& r : world.rails) {
        std::vector<std::pair<float, float>> pts;
        for (const RailPoint& p : r.pts) pts.push_back({p.x, p.z});
        addLine(railV_, railI_, pts, 0.9f, r.main ? 1.2f : 0.7f, r.main ? rgba(0.30f, 0.30f, 0.34f) : rgba(0.50f, 0.50f, 0.52f));
    }
    for (const MapLine& l : world.lines)
        if (l.kind == LINE_RAIL && world.rails.empty()) addLine(railV_, railI_, l.pts, 0.9f, 1.2f, rgba(0.3f, 0.3f, 0.34f));
    // perrons als vlakken
    for (const OsmPlatform& p : world.platforms) {
        if (p.pts.size() < 3) continue;
        uint32_t base = (uint32_t)railV_.size();
        float cx = 0, cz = 0;
        for (auto& q : p.pts) { cx += q.first; cz += q.second; }
        cx /= p.pts.size(); cz /= p.pts.size();
        railV_.push_back({cx, cz, 0, 0, 0, 0, rgba(0.55f, 0.55f, 0.62f), 0, 0});
        for (auto& q : p.pts) railV_.push_back({q.first, q.second, 0, 0, 0, 0, rgba(0.55f, 0.55f, 0.62f), 0, 0});
        for (uint32_t k = 0; k < p.pts.size(); k++) railI_.insert(railI_.end(), {base, base + 1 + k, base + 1 + (k + 1) % (uint32_t)p.pts.size()});
    }
    for (const MapPoint& p : world.points)
        if (p.kind == POINT_BUS_STOP) busStops_.push_back({p.x, p.z});
    upload(lines_[0], lineV_, lineI_);
    upload(areas_, areaV_, areaI_);
    upload(rails_, railV_, railI_);
    return true;
}

void MapView::addPlatforms(const std::vector<std::vector<std::pair<float, float>>>& polys) {
    for (auto& p : polys) {
        if (p.size() < 3) continue;
        uint32_t base = (uint32_t)railV_.size();
        for (auto& q : p) railV_.push_back({q.first, q.second, 0, 0, 0, 0, rgba(0.55f, 0.55f, 0.62f), 0, 0});
        for (uint32_t k = 1; k + 1 < p.size(); k++) railI_.insert(railI_.end(), {base, base + k, base + k + 1});  // convex
    }
    upload(rails_, railV_, railI_);
}

GLuint MapView::photoFor(const TileRef& t) {
    int64_t key = ((int64_t)t.x << 32) | (uint32_t)t.y;
    auto it = photos_.find(key);
    if (it != photos_.end()) {
        lru_.remove(key);
        lru_.push_front(key);
        return it->second.tex;
    }
    if (loadsThisFrame_ >= 3) return 0;
    Photo ph;
    std::string base = std::to_string(t.x) + "_" + std::to_string(t.y);
    std::string rel = std::string(t.detail ? "raw/luchtfoto/" : "raw/luchtfoto_lo/") + base + ".jpg";
    std::string p = DataStore::peek(rel);  // met een server: nog niet binnen = later nog eens proberen
    if (p.empty()) {
        if (!DataStore::known(rel)) { ph.failed = true; photos_[key] = ph; }
        return 0;
    }
    loadsThisFrame_++;
    int w, h, comp;
    unsigned char* img = stbi_load(p.c_str(), &w, &h, &comp, 3);
    if (!img) { ph.failed = true; photos_[key] = ph; return 0; }
    // detailfoto's (2048 px) verkleinen tot 512 px
    std::vector<unsigned char> small;
    int sw = w, sh = h;
    const unsigned char* src = img;
    if (w > 512) {
        int f = w / 512;
        sw = w / f; sh = h / f;
        small.resize((size_t)sw * sh * 3);
        for (int y = 0; y < sh; y++)
            for (int x = 0; x < sw; x++)
                for (int c = 0; c < 3; c++) {
                    int sum = 0;
                    for (int yy = 0; yy < f; yy++)
                        for (int xx = 0; xx < f; xx++) sum += img[((size_t)(y * f + yy) * w + (x * f + xx)) * 3 + c];
                    small[((size_t)y * sw + x) * 3 + c] = (unsigned char)(sum / (f * f));
                }
        src = small.data();
    }
    glGenTextures(1, &ph.tex);
    glBindTexture(GL_TEXTURE_2D, ph.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, sw, sh, 0, GL_RGB, GL_UNSIGNED_BYTE, src);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(img);
    photos_[key] = ph;
    lru_.push_front(key);
    while (lru_.size() > 220) {
        int64_t old = lru_.back();
        lru_.pop_back();
        auto o = photos_.find(old);
        if (o != photos_.end()) {
            if (o->second.tex) glDeleteTextures(1, &o->second.tex);
            photos_.erase(o);
        }
    }
    return ph.tex;
}

void MapView::drawLayer(const Layer& l, GLsizei first, GLsizei count) {
    if (!l.vao || count <= 0) return;
    glBindVertexArray(l.vao);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, (void*)(first * sizeof(uint32_t)));
}

void MapView::render(int x0, int y0, int w, int h, float cx, float cz, float scale, const MapContext& ctx, int sw, int shh, bool mini) {
    loadsThisFrame_ = 0;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x0, shh - (y0 + h), w, h);
    glClearColor(((C_BG) & 255) / 255.0f, ((C_BG >> 8) & 255) / 255.0f, ((C_BG >> 16) & 255) / 255.0f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(prog_);
    float bx = x0 + w / 2.0f - cx * scale, bz = y0 + h / 2.0f - cz * scale;
    glUniform4f(uA_, scale, 0, 0, scale);
    glUniform2f(uB_, bx, bz);
    glUniform1f(uScale_, scale);
    glUniform2f(uScreen_, (float)sw, (float)shh);
    glUniform1i(uUseTex_, 0);
    glUniform1i(uTex_, 0);
    // zichtbaar gebied (game)
    float vx0 = cx - w / 2.0f / scale, vx1 = cx + w / 2.0f / scale, vz0 = cz - h / 2.0f / scale, vz1 = cz + h / 2.0f / scale;

    std::vector<V> dv;
    std::vector<uint32_t> di;
    auto quad = [&](float ax, float az, float bx2, float bz2, uint32_t c, float u0, float v0, float u1, float v1) {
        uint32_t b = (uint32_t)dv.size();
        dv.push_back({ax, az, 0, 0, 0, 0, c, u0, v0});
        dv.push_back({bx2, az, 0, 0, 0, 0, c, u1, v0});
        dv.push_back({bx2, bz2, 0, 0, 0, 0, c, u1, v1});
        dv.push_back({ax, bz2, 0, 0, 0, 0, c, u0, v1});
        di.insert(di.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    };
    if (aerial) {
        glUniform1i(uUseTex_, 1);
        glActiveTexture(GL_TEXTURE0);
        for (const TileRef& t : tiles_) {
            float gx0 = gameX(t.x), gx1 = gameX(t.x + TILE), gzN = gameZ(t.y + TILE), gzS = gameZ(t.y);
            if (gx1 < vx0 || gx0 > vx1 || gzS < vz0 || gzN > vz1) continue;
            GLuint tex = photoFor(t);
            if (!tex) continue;
            dv.clear(); di.clear();
            quad(gx0, gzN, gx1, gzS, rgba(1, 1, 1), 0, 0, 1, 1);
            upload(dyn_, dv, di);
            glBindTexture(GL_TEXTURE_2D, tex);
            drawLayer(dyn_, 0, dyn_.count);
        }
        glUniform1i(uUseTex_, 0);
    } else {
        drawLayer(areas_, 0, areas_.count);
        // wegen: kleine wegen pas bij inzoomen
        for (int pass = 0; pass < 2; pass++)
            for (int oi = 0; oi < 7; oi++) {
                int k = 6 - oi;
                if ((k == 3 && scale < 0.18f) || (k == 4 && scale < 0.6f) || (k == 6 && scale < 0.25f)) continue;
                int slot = pass * 7 + oi;
                drawLayer(lines_[0], lineStart_[slot], lineStart_[slot + 1] - lineStart_[slot]);
            }
    }
    drawLayer(rails_, 0, rails_.count);

    // ---- live: bushaltes, treinen, bussen, speler ----
    dv.clear(); di.clear();
    if (!aerial && scale > 0.5f)
        for (auto& b : busStops_)
            if (b.first > vx0 && b.first < vx1 && b.second > vz0 && b.second < vz1)
                addLine(dv, di, {{b.first - 0.01f, b.second}, {b.first + 0.01f, b.second}}, 0, 2.5f, rgba(0.85f, 0.15f, 0.20f));
    for (size_t mi = 0; mi < ctx.markers.size(); mi++) {
        const MapMarker& m = ctx.markers[mi];
        uint32_t fill = m.kind == MapMarker::TRAIN_IC ? rgba(1.0f, 0.78f, 0.05f) : m.kind == MapMarker::TRAIN_SPR ? rgba(0.10f, 0.30f, 0.80f) : rgba(0.85f, 0.10f, 0.12f);
        bool hl = !mini && (int)mi == hover_;
        for (size_t c = 0; c < m.cars.size(); c++) {
            float hx = std::sin(m.cars[c].y) * m.lens[c] / 2, hz = std::cos(m.cars[c].y) * m.lens[c] / 2;
            float x = m.cars[c].x, z = m.cars[c].z;
            if (hl) addLine(dv, di, {{x - hx * 1.05f, z - hz * 1.05f}, {x + hx * 1.05f, z + hz * 1.05f}}, 3.6f, 6.0f, rgba(1, 1, 1));
            addLine(dv, di, {{x - hx, z - hz}, {x + hx, z + hz}}, 2.2f, 3.0f, rgba(0.1f, 0.1f, 0.1f));
            addLine(dv, di, {{x - hx * 0.96f, z - hz * 0.96f}, {x + hx * 0.96f, z + hz * 0.96f}}, 1.5f, 2.0f, fill);
        }
    }
    // speler: pijl
    {
        float fx = std::sin(ctx.yaw), fz = -std::cos(ctx.yaw);
        float px = ctx.player.x, pz = ctx.player.z;
        uint32_t b = (uint32_t)dv.size();
        auto P = [&](float ang, float r, uint32_t c) {
            float ca = std::cos(ang), sa = std::sin(ang);
            float ox = fx * ca - fz * sa, oz = fx * sa + fz * ca;
            dv.push_back({px, pz, ox, oz, 0, r, c, 0, 0});
        };
        float s = mini ? 1.0f : 1.25f;
        uint32_t ring = rgba(1, 1, 1);
        P(0, 13 * s, ring); P(2.5f, 10 * s, ring); P(PI, 4 * s, ring); P(-2.5f, 10 * s, ring);
        uint32_t col = rgba(0.90f, 0.12f, 0.15f);
        P(0, 10 * s, col); P(2.5f, 7.5f * s, col); P(PI, 2.5f * s, col); P(-2.5f, 7.5f * s, col);
        di.insert(di.end(), {b, b + 1, b + 2, b, b + 2, b + 3, b + 4, b + 5, b + 6, b + 4, b + 6, b + 7});
    }
    upload(dyn_, dv, di);
    drawLayer(dyn_, 0, dyn_.count);
    glBindVertexArray(0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void MapView::open(const MapContext& ctx) {
    open_ = true;
    follow_ = true;
    cx_ = ctx.player.x;
    cz_ = ctx.player.z;
    if (scale_ <= 0) scale_ = 0.5f;
}

bool MapView::drawFull(TextRenderer& text, Input& in, const MapContext& ctx, int w, int h, float& tx, float& tz, int& spawn, int& vehicle) {
    float ui = h / 900.0f;
    const float panelW = 300 * ui;
    int mx0 = (int)panelW, mw = w - mx0;
    bool tele = false;
    spawn = 0;
    vehicle = -1;
    // trein of bus onder de muis (binnen ~14 pixels van een rijtuig)
    auto hitMarker = [&](float wx, float wz) {
        int best = -1;
        float bd = 14.0f / scale_ + 2.0f;
        for (size_t i = 0; i < ctx.markers.size(); i++) {
            const MapMarker& m = ctx.markers[i];
            for (size_t c = 0; c < m.cars.size(); c++) {
                float hx = std::sin(m.cars[c].y) * m.lens[c] / 2, hz = std::cos(m.cars[c].y) * m.lens[c] / 2;
                float ax = m.cars[c].x - hx, az = m.cars[c].z - hz, dx = 2 * hx, dz = 2 * hz;
                float l2 = dx * dx + dz * dz;
                float t = l2 > 0 ? clampf(((wx - ax) * dx + (wz - az) * dz) / l2, 0, 1) : 0;
                float d = std::hypot(wx - ax - dx * t, wz - az - dz * t);
                if (d < bd) { bd = d; best = (int)i; }
            }
        }
        return best;
    };
    // ---- bediening ----
    auto toWorld = [&](float sx, float sy, float& x, float& z) {
        x = cx_ + (sx - (mx0 + mw / 2.0f)) / scale_;
        z = cz_ + (sy - h / 2.0f) / scale_;
    };
    bool overMap = in.mouseX > mx0;
    // controller: linkerstick verschuift de kaart (~half scherm per seconde op volle uitslag)
    if (in.padLX != 0 || in.padLY != 0) {
        float sp = h * 0.6f * in.padDt / scale_;
        cx_ += in.padLX * std::fabs(in.padLX) * sp;
        cz_ -= in.padLY * std::fabs(in.padLY) * sp;
        follow_ = false;
    }
    const float wheel = in.wheel - in.stickWheel;
    if (wheel != 0 && overMap) {
        float wx, wz;
        toWorld(in.mouseX, in.mouseY, wx, wz);
        float ns = clampf(scale_ * std::pow(1.25f, wheel), 0.04f, 6.0f);
        // zoomen rond de muis
        cx_ = wx - (in.mouseX - (mx0 + mw / 2.0f)) / ns;
        cz_ = wz - (in.mouseY - h / 2.0f) / ns;
        scale_ = ns;
        follow_ = false;
    }
    if (in.pressed[VK_OEM_PLUS] || in.pressed[VK_ADD]) scale_ = std::min(6.0f, scale_ * 1.25f);
    if (in.pressed[VK_OEM_MINUS] || in.pressed[VK_SUBTRACT]) scale_ = std::max(0.04f, scale_ / 1.25f);
    if (in.pressed['L']) aerial = !aerial;
    if (in.pressed['F'] || in.pressed[VK_SPACE]) follow_ = true;
    if (in.lClick && overMap) { pressed_ = true; dragging_ = false; pressX_ = in.mouseX; pressY_ = in.mouseY; }
    if (pressed_ && in.lDown) {
        float dx = in.mouseX - pressX_, dy = in.mouseY - pressY_;
        if (dragging_ || std::hypot(dx, dy) > 5) {
            if (!dragging_) { dragging_ = true; }
            cx_ -= dx / scale_;
            cz_ -= dy / scale_;
            pressX_ = in.mouseX; pressY_ = in.mouseY;
            follow_ = false;
        }
    }
    if (pressed_ && in.lRelease) {
        if (!dragging_ && overMap) {
            toWorld(in.mouseX, in.mouseY, tx, tz);
            // dichtbij een station: daar teleporteren (startpunt)
            for (const MapPlace& p : places_)
                if (std::hypot(p.x - tx, p.z - tz) * scale_ < 22) { tx = p.x; tz = p.z; spawn = p.spawn; }
            vehicle = hitMarker(tx, tz);
            if (vehicle >= 0) spawn = 0;
            tele = true;
        }
        pressed_ = dragging_ = false;
    }
    if (follow_) { cx_ = ctx.player.x; cz_ = ctx.player.z; }
    cx_ = clampf(cx_, gameX(AREA_X0), gameX(AREA_X1));
    cz_ = clampf(cz_, gameZ(AREA_Y1), gameZ(AREA_Y0));
    hover_ = -1;
    if (overMap && !dragging_) {
        float wx, wz;
        toWorld(in.mouseX, in.mouseY, wx, wz);
        hover_ = hitMarker(wx, wz);
    }

    render(mx0, 0, mw, h, cx_, cz_, scale_, ctx, w, h, false);

    // ---- teksten ----
    text.begin(w, h);
    const Color4 WHITE{1, 1, 1, 1}, DARKT{0.12f, 0.12f, 0.14f, 1}, YEL{1, 0.85f, 0.2f, 1}, GREY{0.75f, 0.78f, 0.85f, 1};
    auto toScreen = [&](float x, float z, float& sx, float& sy) {
        sx = mx0 + mw / 2.0f + (x - cx_) * scale_;
        sy = h / 2.0f + (z - cz_) * scale_;
    };
    // plaatsnamen (zonder overlap; belangrijkste eerst)
    std::vector<std::array<float, 4>> used;
    auto place = [&](const std::string& s, float x, float z, float size, Color4 c, bool box) {
        float sx, sy;
        toScreen(x, z, sx, sy);
        float tw = text.measure(s, size);
        float x0 = sx - tw / 2, y0 = sy - size * 0.6f;
        if (x0 < mx0 || x0 + tw > w || y0 < 0 || y0 + size > h) return;
        for (auto& r : used)
            if (x0 < r[2] && x0 + tw > r[0] && y0 < r[3] && y0 + size * 1.2f > r[1]) return;
        used.push_back({x0 - 4, y0 - 2, x0 + tw + 4, y0 + size * 1.2f + 2});
        if (box) text.rect(x0 - 5 * ui, y0 - 2 * ui, tw + 10 * ui, size * 1.25f, {0.0f, 0.13f, 0.43f, 0.92f});
        text.text(x0, y0, s, size, c);
    };
    for (const MapPlace& p : places_) place(p.name, p.x, p.z - 14 / scale_, 17 * ui, WHITE, true);
    Color4 lab = aerial ? Color4{1, 1, 1, 1} : Color4{0.25f, 0.25f, 0.28f, 1};
    for (int pass = 0; pass < 4; pass++)
        for (const Label& l : labels_) {
            if (l.kind != pass) continue;
            if ((pass == 2 && scale_ < 0.12f) || (pass == 3 && scale_ < 0.45f)) continue;
            place(l.name, l.x, l.z, (pass == 0 ? 26.0f : pass == 1 ? 21.0f : pass == 2 ? 16.0f : 14.0f) * ui, lab, false);
        }
    // treinen bij inzoomen: label
    if (scale_ > 0.25f)
        for (const MapMarker& m : ctx.markers)
            if (m.kind != MapMarker::BUS && !m.cars.empty()) place(m.label, m.cars[0].x, m.cars[0].z - 18 / scale_, 14 * ui, YEL, false);

    // muis: doel
    if (overMap && !dragging_) {
        float wx, wz;
        toWorld(in.mouseX, in.mouseY, wx, wz);
        std::string tip = "Klik: hierheen teleporteren";
        for (const MapPlace& p : places_)
            if (std::hypot(p.x - wx, p.z - wz) * scale_ < 22) tip = "Klik: teleporteren naar " + p.name;
        int hv = hover_;
        if (hv >= 0 && !ctx.markers[hv].info.empty()) {
            // informatiekaartje: kop, regels "Naam: waarde", klikhint
            const MapMarker& m = ctx.markers[hv];
            const float fs = 16 * ui, ts = 19 * ui, lh = 23 * ui, pad = 12 * ui;
            float keyW = 0, valW = 0;
            std::vector<std::pair<std::string, std::string>> rows;
            for (size_t i = 1; i < m.info.size(); i++) {
                size_t c = m.info[i].find(": ");
                std::string k = c == std::string::npos ? "" : m.info[i].substr(0, c);
                std::string v = c == std::string::npos ? m.info[i] : m.info[i].substr(c + 2);
                rows.push_back({k, v});
                keyW = std::max(keyW, text.measure(k, fs));
                valW = std::max(valW, text.measure(v, fs));
            }
            std::string hint = m.kind == MapMarker::BUS ? "Klik: instappen in deze bus" : "Klik: instappen in deze trein";
            float bw = std::max({text.measure(m.info[0], ts) + 16 * ui, keyW + 14 * ui + valW, text.measure(hint, 14 * ui)}) + 2 * pad;
            float bh = pad * 2 + ts * 1.3f + 6 * ui + rows.size() * lh + 22 * ui;
            float bx = in.mouseX + 18 * ui, by = in.mouseY + 18 * ui;
            if (bx + bw > w - 8) bx = in.mouseX - 18 * ui - bw;
            if (by + bh > h - 8) by = std::max(8.0f, h - 8 - bh);
            Color4 accent = m.kind == MapMarker::TRAIN_IC ? Color4{1.0f, 0.78f, 0.05f, 1} : m.kind == MapMarker::TRAIN_SPR ? Color4{0.15f, 0.40f, 0.95f, 1} : Color4{0.90f, 0.15f, 0.15f, 1};
            text.rect(bx, by, bw, bh, {0.05f, 0.07f, 0.11f, 0.93f});
            text.rect(bx, by, 6 * ui, bh, accent);
            float yy = by + pad;
            text.text(bx + pad + 4 * ui, yy, m.info[0], ts, WHITE);
            yy += ts * 1.3f + 6 * ui;
            for (auto& r : rows) {
                text.text(bx + pad + 4 * ui, yy, r.first, fs, GREY);
                text.text(bx + pad + 4 * ui + keyW + 14 * ui, yy, r.second, fs, WHITE);
                yy += lh;
            }
            text.text(bx + pad + 4 * ui, yy + 2 * ui, hint, 14 * ui, YEL);
        } else {
            if (hv >= 0) tip = ctx.markers[hv].kind == MapMarker::BUS ? "Klik: instappen in de bus" : "Klik: instappen in de " + ctx.markers[hv].label;
            float tw = text.measure(tip, 16 * ui);
            text.rect(in.mouseX + 14 * ui, in.mouseY + 14 * ui, tw + 14 * ui, 26 * ui, {0, 0, 0, 0.7f});
            text.text(in.mouseX + 21 * ui, in.mouseY + 17 * ui, tip, 16 * ui, WHITE);
        }
    }
    // schaalstok
    {
        float target = 120 * ui / scale_;
        float nice = 10;
        for (float c : {10.f, 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f}) if (c <= target) nice = c;
        float px = nice * scale_;
        float x1 = w - 24 * ui, y1 = h - 30 * ui;
        text.rect(x1 - px - 8 * ui, y1 - 26 * ui, px + 16 * ui, 36 * ui, {1, 1, 1, 0.75f});
        text.rect(x1 - px, y1, px, 3 * ui, DARKT);
        std::string s = nice >= 1000 ? std::to_string((int)(nice / 1000)) + " km" : std::to_string((int)nice) + " m";
        text.text(x1 - px, y1 - 22 * ui, s, 15 * ui, DARKT);
    }
    // ---- paneel ----
    text.rect(0, 0, panelW, (float)h, {0.06f, 0.08f, 0.12f, 0.96f});
    float y = 18 * ui;
    text.text(20 * ui, y, "Kaart", 34 * ui, YEL);
    y += 48 * ui;
    text.text(20 * ui, y, "Klok " + ctx.clock, 20 * ui, WHITE);
    y += 40 * ui;
    auto button = [&](const std::string& label, bool active) {
        float bx = 16 * ui, bw = panelW - 32 * ui, bh = 36 * ui;
        bool hover = in.mouseX > bx && in.mouseX < bx + bw && in.mouseY > y && in.mouseY < y + bh;
        Color4 bg = active ? Color4{0.0f, 0.30f, 0.70f, 1} : hover ? Color4{0.22f, 0.26f, 0.34f, 1} : Color4{0.14f, 0.17f, 0.23f, 1};
        text.rect(bx, y, bw, bh, bg);
        text.text(bx + 12 * ui, y + 6 * ui, label, 19 * ui, WHITE);
        bool clicked = hover && in.lClick;
        y += bh + 8 * ui;
        return clicked;
    };
    if (button(aerial ? "Ondergrond: luchtfoto  (L)" : "Ondergrond: getekend  (L)", false)) aerial = !aerial;
    if (button(follow_ ? "Volgt jou" : "Volg mij  (F)", follow_)) follow_ = true;
    y += 14 * ui;
    text.text(20 * ui, y, "Teleporteren naar:", 18 * ui, GREY);
    y += 32 * ui;
    for (const MapPlace& p : places_)
        if (button(p.name, false)) { tx = p.x; tz = p.z; spawn = p.spawn; tele = true; }
    y = h - 174 * ui;
    for (const char* s : {"Sleep: verschuiven", "Muiswiel: in-/uitzoomen", "Klik op de kaart: teleporteren", "Blauw Sprinter, geel IC, rood bus", "Muis op voertuig: informatie", "M / Home / Esc: sluiten"}) {
        text.text(20 * ui, y, s, 15 * ui, GREY);
        y += 24 * ui;
    }
    text.end();
    return tele;
}

bool MapView::nearestMarkerScreen(const MapContext& ctx, int w, int h, float& sx, float& sy) const {
    float ui = h / 900.0f, mx0 = 300 * ui, mw = w - mx0;
    float bd = 1e30f;
    for (const MapMarker& m : ctx.markers)
        for (const Vec3& c : m.cars) {
            float d = std::hypot(c.x - cx_, c.z - cz_);
            if (d < bd) { bd = d; sx = mx0 + mw / 2.0f + (c.x - cx_) * scale_; sy = h / 2.0f + (c.z - cz_) * scale_; }
        }
    return bd < 1e29f;
}

void MapView::drawMini(TextRenderer& text, const MapContext& ctx, int w, int h) {
    float ui = h / 900.0f;
    int size = (int)(250 * ui), m = (int)(14 * ui);
    int x0 = w - size - m, y0 = m;
    // uitzoomen met de snelheid (trein)
    float scale = clampf(0.75f / (1.0f + ctx.speed / 12.0f), 0.12f, 0.75f);
    text.begin(w, h);
    text.rect((float)x0 - 3, (float)y0 - 3, (float)size + 6, (float)size + 6, {0, 0, 0, 0.6f});
    text.end();
    render(x0, y0, size, size, ctx.player.x, ctx.player.z, scale, ctx, w, h, true);
    text.begin(w, h);
    const Color4 WHITE{1, 1, 1, 1};
    // noordpijl en klok
    text.rect((float)x0 + 6 * ui, (float)y0 + 6 * ui, 22 * ui, 24 * ui, {0, 0, 0, 0.55f});
    text.text((float)x0 + 10 * ui, (float)y0 + 7 * ui, "N", 18 * ui, WHITE);
    float cw = text.measure(ctx.clock, 26 * ui);
    text.rect(x0 + size - cw - 18 * ui, (float)y0 + size + 6 * ui, cw + 16 * ui, 36 * ui, {0, 0, 0, 0.6f});
    text.text(x0 + size - cw - 10 * ui, (float)y0 + size + 9 * ui, ctx.clock, 26 * ui, WHITE);
    std::string hint = "M: kaart";
    text.text((float)x0 + 4 * ui, (float)y0 + size + 12 * ui, hint, 15 * ui, {1, 1, 1, 0.85f});
    text.end();
}
