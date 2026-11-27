// Treinmodellen uit Train Simulator: zie tsmodel.h en tools/ts/tsmodel.py (bestandsformaat)
#include "tsmodel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include "datastore.h"
#include "rollingstock.h"
#include "signs.h"
#include "world.h"

namespace {
const char* VS = R"(#version 330 core
layout(location = 0) in vec3 aP;
layout(location = 1) in vec3 aN;
layout(location = 2) in vec2 aT;
uniform mat4 uViewProj;  // camera-relatief
uniform mat4 uModel;     // al min uOrigin
uniform vec3 uOrigin;
out vec3 vW; out vec3 vN; out vec2 vT;
void main() {
    vec4 w = uModel * vec4(aP, 1.0);
    vW = w.xyz + uOrigin; vN = mat3(uModel) * aN; vT = aT;
    gl_Position = uViewProj * w;
})";

const char* FS = R"(#version 330 core
in vec3 vW; in vec3 vN; in vec2 vT;
uniform sampler2D uTex;
uniform int uHasTex;
uniform vec4 uColor;
uniform int uKind;     // 0 dicht, 1 alfatest, 2 glas, 3 lichtgevend
uniform int uInterior; // binnen (kunstlicht)
uniform float uGlow;   // lichthalo's: sterkte
uniform vec3 uSun, uSunCol, uCam, uSky, uFog;
uniform float uAmb, uFogDist;
out vec4 oColor;
void main() {
    vec4 t = uHasTex == 1 ? texture(uTex, vT) : vec4(1.0);
    vec4 base = t * uColor;
    if (uKind == 1 && base.a < 0.5) discard;
    vec3 n = normalize(vN);
    vec3 toCam = uCam - vW;
    float dist = length(toCam);
    vec3 v = toCam / max(dist, 1e-4);
    if (dot(n, v) < 0.0) n = -n;
    float diff = max(dot(n, uSun), 0.0);
    float hemi = 0.5 + 0.5 * n.y;
    vec3 ambient = mix(vec3(0.56, 0.57, 0.60), uSky * 0.85, 0.3) * (0.86 + 0.14 * hemi) * uAmb;
    vec3 col;
    if (uKind == 3) {
        oColor = vec4(base.rgb * base.a * uGlow, 1.0);  // optellend
        return;
    } else if (uInterior == 1) {
        col = base.rgb * (0.62 + 0.30 * uAmb + 0.08 * hemi);
    } else {
        col = base.rgb * (ambient + uSunCol * diff * 0.72);
        vec3 r = reflect(-v, n);
        float fres = pow(1.0 - max(dot(v, n), 0.0), 4.0);
        vec3 env = mix(vec3(0.30, 0.30, 0.29), uSky * 1.05, smoothstep(-0.15, 0.25, r.y));
        float spec = pow(max(dot(r, uSun), 0.0), 120.0);
        col = mix(col, env, 0.04 + 0.30 * fres) + uSunCol * spec * 0.35;
    }
    float tf = dist / uFogDist;
    float fog = clamp(1.0 - exp(-tf * tf), 0.0, 1.0);
    float a = 1.0;
    if (uKind == 2) {
        vec3 r = reflect(-v, n);
        float fres = pow(1.0 - abs(dot(v, n)), 3.0);
        vec3 env = mix(vec3(0.35, 0.36, 0.36), uSky * 1.1, smoothstep(-0.1, 0.4, r.y));
        col = mix(col, env, 0.15 + 0.5 * fres);
        a = clamp(base.a + 0.45 * fres, 0.0, 1.0);
    }
    oColor = vec4(mix(col, uFog, fog), a);
})";

GLuint prog = 0;
GLint uGlow, uVP, uModelL, uOriginL, uTexL, uHasTex, uColor, uKind, uInterior, uSun, uSunCol, uCam, uSky, uFog, uAmb, uFogDist;
bool frameSet = false;

std::map<std::string, std::unique_ptr<TsModel>>& models() {
    static std::map<std::string, std::unique_ptr<TsModel>> m;
    return m;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? n : 0);
    bool ok = n > 0 && fread(out.data(), 1, n, f) == (size_t)n;
    fclose(f);
    return ok;
}

struct Rd {
    const uint8_t* p;
    const uint8_t* e;
    bool ok = true;
    template <typename T> T get() {
        T v{};
        if (p + sizeof(T) > e) { ok = false; return v; }
        memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        return v;
    }
    std::string str() {
        uint16_t n = get<uint16_t>();
        if (p + n > e) { ok = false; return ""; }
        std::string s((const char*)p, n);
        p += n;
        return s;
    }
    void bytes(void* dst, size_t n) {
        if (p + n > e) { ok = false; return; }
        memcpy(dst, p, n);
        p += n;
    }
};

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

Mat4 rotXm(float a) {
    Mat4 r = Mat4::identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
    return r;
}
}  // namespace

TsModel* tsModel(const std::string& name) {
    auto& m = models();
    auto it = m.find(name);
    if (it == m.end()) it = m.emplace(name, std::make_unique<TsModel>(name)).first;
    return it->second.get();
}

TsModel::~TsModel() {
    for (Car& c : cars_) {
        if (c.vbo) glDeleteBuffers(1, &c.vbo);
        if (c.ibo) glDeleteBuffers(1, &c.ibo);
        if (c.vao) glDeleteVertexArrays(1, &c.vao);
    }
    for (Tex& t : tex_)
        if (t.id) glDeleteTextures(1, &t.id);
}

bool TsModel::parse(const std::string& path) {
    std::vector<uint8_t> data;
    if (!readFile(path, data)) return false;
    Rd r{data.data(), data.data() + data.size()};
    char magic[4];
    r.bytes(magic, 4);
    if (memcmp(magic, "OVTM", 4) || r.get<uint32_t>() != 1) return false;
    uint32_t nt = r.get<uint32_t>();
    for (uint32_t i = 0; i < nt && r.ok; i++) {
        Tex t;
        t.file = r.str();
        t.alpha = r.get<uint8_t>();
        tex_.push_back(t);
    }
    uint32_t nc = r.get<uint32_t>();
    for (uint32_t ci = 0; ci < nc && r.ok; ci++) {
        Car c;
        c.name = r.str();
        c.front = r.get<float>(); c.back = r.get<float>();
        c.zmin = r.get<float>(); c.zmax = r.get<float>(); c.ymax = r.get<float>();
        uint32_t nn = r.get<uint32_t>();
        for (uint32_t k = 0; k < nn && r.ok; k++) {
            Node n;
            n.name = r.str();
            n.parent = r.get<int32_t>();
            r.bytes(n.local.m, 64);
            c.nodes.push_back(n);
        }
        std::vector<float> vtx;
        std::vector<uint32_t> idx;
        // deurbladen: middelpunt (rijtuigruimte) per knoop uit de vertices
        std::vector<Vec3> mn(nn, Vec3(1e9f, 1e9f, 1e9f)), mx(nn, Vec3(-1e9f, -1e9f, -1e9f));
        std::vector<Mat4> rest(nn);
        for (uint32_t k = 0; k < nn; k++)
            rest[k] = c.nodes[k].parent >= 0 && c.nodes[k].parent < (int)k ? rest[c.nodes[k].parent] * c.nodes[k].local : c.nodes[k].local;
        uint32_t nb = r.get<uint32_t>();
        for (uint32_t k = 0; k < nb && r.ok; k++) {
            Batch b;
            b.tex = r.get<uint16_t>();
            if (b.tex == 0xFFFF) b.tex = -1;
            b.kind = r.get<uint8_t>();
            b.flags = r.get<uint8_t>();
            b.node = r.get<int32_t>();
            b.range = r.get<float>();
            for (float& f : b.color) f = r.get<float>();
            uint32_t nv = r.get<uint32_t>();
            uint32_t base = (uint32_t)(vtx.size() / 8);
            size_t at = vtx.size();
            vtx.resize(at + (size_t)nv * 8);
            r.bytes(vtx.data() + at, (size_t)nv * 32);
            uint32_t ni = r.get<uint32_t>();
            b.first = (GLsizei)idx.size();
            b.count = (GLsizei)ni;
            size_t ia = idx.size();
            idx.resize(ia + ni);
            r.bytes(idx.data() + ia, (size_t)ni * 4);
            for (size_t q = ia; q < idx.size(); q++) idx[q] += base;
            if (b.node >= 0 && b.node < (int)nn) {
                const Mat4& W = rest[b.node];
                for (uint32_t q = 0; q < nv; q++) {
                    const float* p = &vtx[at + q * 8];
                    Vec3 w(W.m[0] * p[0] + W.m[4] * p[1] + W.m[8] * p[2] + W.m[12], W.m[1] * p[0] + W.m[5] * p[1] + W.m[9] * p[2] + W.m[13],
                           W.m[2] * p[0] + W.m[6] * p[1] + W.m[10] * p[2] + W.m[14]);
                    Vec3& a = mn[b.node];
                    Vec3& z = mx[b.node];
                    a = Vec3(std::min(a.x, w.x), std::min(a.y, w.y), std::min(a.z, w.z));
                    z = Vec3(std::max(z.x, w.x), std::max(z.y, w.y), std::max(z.z, w.z));
                }
            } else {
                b.node = -1;
            }
            c.batches.push_back(b);
        }
        // deurbladen herkennen en per zijde groeperen tot deuropeningen
        struct L { int node; Vec3 c; float w; };
        std::vector<L> leaves;
        for (uint32_t k = 0; k < nn; k++) {
            std::string n = lower(c.nodes[k].name);
            if (mn[k].x > mx[k].x) continue;
            if (n.compare(0, 4, "door") == 0 && n.find("step") == std::string::npos && n.find("frame") == std::string::npos &&
                n.find("panel") == std::string::npos && mx[k].z - mn[k].z < 1.6f) {
                leaves.push_back({(int)k, (mn[k] + mx[k]) * 0.5f, mx[k].z - mn[k].z});
            }
            if ((n.find("wh0") != std::string::npos || n.compare(0, 5, "wheel") == 0 || n.compare(0, 4, "axle") == 0)) c.wheels.push_back((int)k);
        }
        for (const L& l : leaves) {
            // deuropening: alle bladen aan dezelfde zijde binnen 1,6 m
            float sum = 0;
            int cnt = 0;
            for (const L& o : leaves)
                if ((o.c.x > 0) == (l.c.x > 0) && std::fabs(o.c.z - l.c.z) < 1.6f) { sum += o.c.z; cnt++; }
            float mid = sum / cnt;
            float dir = l.c.z > mid + 0.02f ? 1.0f : (l.c.z < mid - 0.02f ? -1.0f : 0.0f);
            Leaf lf;
            lf.node = l.node;
            lf.side = l.c.x > 0 ? 0 : 1;
            lf.slideZ = dir * std::max(0.3f, l.w * 0.92f);
            lf.outX = (l.c.x > 0 ? 1.0f : -1.0f) * 0.06f;
            c.leaves.push_back(lf);
        }
        vtx_.push_back(std::move(vtx));
        idx_.push_back(std::move(idx));
        cars_.push_back(std::move(c));
    }
    return r.ok && !cars_.empty();
}

static GLuint loadTex(const std::string& path, int alpha) {
    std::vector<uint8_t> d;
    if (!readFile(path, d) || d.size() < 20 || memcmp(d.data(), "OVTX", 4)) return 0;
    uint32_t w, h, fmt, mips;
    memcpy(&w, &d[4], 4); memcpy(&h, &d[8], 4); memcpy(&fmt, &d[12], 4); memcpy(&mips, &d[16], 4);
    GLenum gf = fmt == 1 ? (alpha ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : GL_COMPRESSED_RGB_S3TC_DXT1_EXT)
                         : fmt == 3 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    size_t p = 20;
    uint32_t lw = w, lh = h, used = 0;
    for (uint32_t l = 0; l < mips && p + 4 <= d.size(); l++) {
        uint32_t n;
        memcpy(&n, &d[p], 4);
        p += 4;
        if (p + n > d.size()) break;
        if (fmt == 0) glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA8, lw, lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, &d[p]);
        else glCompressedTexImage2D(GL_TEXTURE_2D, l, gf, lw, lh, 0, n, &d[p]);
        p += n;
        used++;
        lw = std::max(1u, lw / 2);
        lh = std::max(1u, lh / 2);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, used > 0 ? used - 1 : 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, used > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    float an = 1;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &an);
    if (an > 1) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(8.0f, an));
    return id;
}

bool TsModel::upload() {
    std::string dir = "trains/" + name_ + "/";
    for (Tex& t : tex_) {
        std::string p = DataStore::peek(dir + t.file);
        t.id = p.empty() ? 0 : loadTex(p, t.alpha);
    }
    for (size_t i = 0; i < cars_.size(); i++) {
        Car& c = cars_[i];
        glGenVertexArrays(1, &c.vao);
        glBindVertexArray(c.vao);
        glGenBuffers(1, &c.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
        glBufferData(GL_ARRAY_BUFFER, vtx_[i].size() * 4, vtx_[i].data(), GL_STATIC_DRAW);
        glGenBuffers(1, &c.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_[i].size() * 4, idx_[i].data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);
        glBindVertexArray(0);
    }
    vtx_.clear();
    idx_.clear();
    vtx_.shrink_to_fit();
    idx_.shrink_to_fit();
    return true;
}

bool TsModel::ready() {
    if (state_ == 2) return true;
    if (state_ < 0) return false;
    std::string dir = "trains/" + name_ + "/";
    if (state_ == 0) {
        std::string p = DataStore::peek(dir + "model.bin");
        if (p.empty()) {
            if (!DataStore::known(dir + "model.bin")) state_ = -1;
            return false;
        }
        if (!parse(p)) { state_ = -1; cars_.clear(); return false; }
        for (const Tex& t : tex_) DataStore::peek(dir + t.file);  // downloads starten
        state_ = 1;
        return false;
    }
    for (const Tex& t : tex_)
        if (DataStore::peek(dir + t.file).empty() && DataStore::known(dir + t.file)) return false;
    upload();
    state_ = 2;
    return true;
}

bool TsModel::loadNow() {
    if (state_ == 2) return true;
    std::string dir = "trains/" + name_ + "/";
    std::string p = DataStore::fetch(dir + "model.bin");
    if (p.empty() || !parse(p)) { state_ = -1; return false; }
    for (const Tex& t : tex_) DataStore::fetch(dir + t.file);
    upload();
    state_ = 2;
    return true;
}

void TsModel::beginFrame() { frameSet = false; }

Mat4 TsModel::nodeMatrix(const Car& c, int node, const DrawState& s) const {
    Mat4 m = Mat4::identity();
    std::vector<int> chain;
    for (int n = node; n >= 0 && n < (int)c.nodes.size() && chain.size() < 32; n = c.nodes[n].parent) chain.push_back(n);
    for (int k = (int)chain.size() - 1; k >= 0; k--) {
        int n = chain[k];
        Mat4 l = c.nodes[n].local;
        for (const Leaf& lf : c.leaves)
            if (lf.node == n) {
                float o = s.doorOpen[lf.side];
                float out = std::min(1.0f, o * 4.0f);
                float slide = clampf((o - 0.12f) / 0.88f, 0, 1);
                l = Mat4::translation(lf.outX * out, 0, lf.slideZ * slide) * l;
            }
        for (int w : c.wheels)
            if (w == n) l = l * rotXm(s.wheel);
        m = m * l;
    }
    return m;
}

void TsModel::drawCar(const World& w, int ci, const Mat4& carM, const DrawState& s, int pass) const {
    if (state_ != 2 || ci < 0 || ci >= (int)cars_.size()) return;
    const Car& c = cars_[ci];
    if (!prog) {
        prog = compileProgram(VS, FS, "tsmodel");
        uVP = glGetUniformLocation(prog, "uViewProj");
        uModelL = glGetUniformLocation(prog, "uModel");
        uOriginL = glGetUniformLocation(prog, "uOrigin");
        uTexL = glGetUniformLocation(prog, "uTex");
        uHasTex = glGetUniformLocation(prog, "uHasTex");
        uColor = glGetUniformLocation(prog, "uColor");
        uKind = glGetUniformLocation(prog, "uKind");
        uInterior = glGetUniformLocation(prog, "uInterior");
        uSun = glGetUniformLocation(prog, "uSun");
        uSunCol = glGetUniformLocation(prog, "uSunCol");
        uCam = glGetUniformLocation(prog, "uCam");
        uSky = glGetUniformLocation(prog, "uSky");
        uFog = glGetUniformLocation(prog, "uFog");
        uAmb = glGetUniformLocation(prog, "uAmb");
        uFogDist = glGetUniformLocation(prog, "uFogDist");
        uGlow = glGetUniformLocation(prog, "uGlow");
    }
    GLint prev = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev);
    glUseProgram(prog);
    Vec3 o = w.origin();
    Vec3 cam = w.camPos();
    glUniformMatrix4fv(uVP, 1, GL_FALSE, w.relativeView().m);
    glUniform3f(uOriginL, o.x, o.y, o.z);
    Vec3 sun = normalize(w.sunDir);
    glUniform3f(uSun, sun.x, sun.y, sun.z);
    glUniform3f(uSunCol, w.sunColor.x, w.sunColor.y, w.sunColor.z);
    glUniform3f(uCam, cam.x, cam.y, cam.z);
    glUniform3f(uSky, w.skyColor.x, w.skyColor.y, w.skyColor.z);
    glUniform3f(uFog, w.skyColor.x, w.skyColor.y, w.skyColor.z);
    glUniform1f(uAmb, w.ambient);
    glUniform1f(uFogDist, w.fogDistance);
    glUniform1i(uTexL, 6);
    glActiveTexture(GL_TEXTURE6);
    float dist = length(Vec3(carM.m[12], carM.m[13], carM.m[14]) - cam);

    // matrices van de bewegende knopen (een keer per rijtuig)
    std::vector<std::pair<int, Mat4>> mats;
    auto modelFor = [&](int node) -> Mat4 {
        if (node < 0) return carM;
        for (auto& pm : mats)
            if (pm.first == node) return pm.second;
        mats.push_back({node, carM * nodeMatrix(c, node, s)});
        return mats.back().second;
    };
    glBindVertexArray(c.vao);
    int lastTex = -2;
    int lastNode = -2;
    glUniform1f(uGlow, 1.3f - 0.7f * clampf(w.ambient, 0, 1));
    for (int sub = 0; sub < (pass == 1 ? 2 : 1); sub++)
    for (const Batch& b : c.batches) {
        // pass 0: dicht + alfatest; pass 1: glas, daarna (sub 1) de lichthalo's optellend
        if (pass == 0 ? b.kind >= 2 : (sub == 0 ? b.kind != 2 : b.kind != 3)) continue;
        if (b.kind == 3) {
            if (!((b.flags & 2) && s.head) && !((b.flags & 4) && s.tail)) continue;
            glBlendFunc(GL_ONE, GL_ONE);
        }
        if (dist > b.range * 1.5f + 30.0f) continue;
        if (b.node != lastNode) {
            Mat4 m = modelFor(b.node);
            m.m[12] -= o.x; m.m[13] -= o.y; m.m[14] -= o.z;
            glUniformMatrix4fv(uModelL, 1, GL_FALSE, m.m);
            lastNode = b.node;
        }
        if (b.tex != lastTex) {
            GLuint id = b.tex >= 0 && b.tex < (int)tex_.size() ? tex_[b.tex].id : 0;
            glBindTexture(GL_TEXTURE_2D, id);
            glUniform1i(uHasTex, id ? 1 : 0);
            lastTex = b.tex;
        }
        glUniform4f(uColor, b.color[0], b.color[1], b.color[2], b.color[3]);
        glUniform1i(uKind, b.kind);
        glUniform1i(uInterior, (b.flags & 1) ? 1 : 0);
        glDrawElements(GL_TRIANGLES, b.count, GL_UNSIGNED_INT, (void*)((size_t)b.first * 4));
    }
    if (pass == 1) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(prev);
}

// bestemmingsborden: dezelfde stijl als de eigen frontdisplays (oranje leds op zwart); voor kijkt naar +z, zijkant naar +x
void RollingStock::buildTsSigns(SignAtlas& atlas, const std::vector<std::string>& names) {
    tsSignFront.assign(names.size(), Mesh());
    tsSignSide.assign(names.size(), Mesh());
    for (size_t t = 0; t < names.size(); t++) {
        SignRect r = atlas.add(names[t], Vec3(0.01f, 0.01f, 0.01f), Vec3(1.0f, 0.62f, 0.12f), 64, true, 0.3f);
        {
            MeshBuilder m;
            float h = 0.20f, w = std::min(1.45f, h * r.aspect);
            SignAtlas::quad(m, r, Vec3(0, 0, 0), Vec3(1, 0, 0), w / std::max(0.1f, r.aspect), true);
            tsSignFront[t] = m.upload();
        }
        {
            MeshBuilder m;
            float h = 0.17f, w = std::min(1.05f, h * r.aspect);
            SignAtlas::quad(m, r, Vec3(0, 0, 0), Vec3(0, 0, -1), w / std::max(0.1f, r.aspect), true);
            tsSignSide[t] = m.upload();
        }
    }
}
