#include "world.h"
#include "zwolle.h"
#include "datastore.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include "../engine/dxt.h"
#include "../third_party/stb_image.h"

using namespace WorldFormat;

static const char* WORLD_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aN;
layout(location=2) in vec3 aC;
layout(location=3) in float aE;
uniform mat4 uViewProj;  // camera-relatief (camera in de oorsprong)
uniform mat4 uModel;     // translatie al min uOrigin (zie World::setModel)
uniform vec3 uOrigin;    // camerapositie: op 76 km van Eindhoven zijn floats te grof voor de projectie (z-fighting)
out vec3 vW; out vec3 vN; out vec3 vC; out float vE;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vW = w.xyz + uOrigin; vN = mat3(uModel) * aN; vC = aC; vE = aE;
    gl_Position = uViewProj * w;
})";

static const char* WORLD_FS = R"(#version 330 core
in vec3 vW; in vec3 vN; in vec3 vC; in float vE;
uniform int uMode;                 // 0 = grond (materialen), 1 = objecten/gebouwen, 2 = voertuigen
uniform sampler2DArray uPhoto;     // luchtfoto's detailtegels (slots)
uniform sampler2DArray uGround;    // materiaalkaarten (slots)
uniform sampler2DArray uCoarse;    // luchtfoto's grove tegels (slots)
uniform sampler2D uLookup;         // per tegel: fotoslot+1, grondslot+1, grofslot+1
uniform sampler2D uSigns;          // tekstatlas voor borden
uniform sampler2D uBoards;         // dynamische bordenatlas (vertrekborden, binnenschermen)
uniform vec2 uGridOrigin;          // game-oorsprong t.o.v. de zuidwesthoek van het tegelraster (m)
uniform vec3 uSun, uCam, uFog, uSky, uSunCol;
uniform float uAmb;                // omgevingslicht (1 = middag, lager bij schemering)
uniform float uFogDist;
out vec4 oColor;

const float CELL = 500.0;
const float GROUND_PX = 2000.0;

// ---------- ruis ----------
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) { float s = 0.0, a = 0.5; for (int k = 0; k < 4; k++) { s += a * vnoise(p); p *= 2.03; a *= 0.5; } return s; }

// ---------- luchtfoto / materiaalkaart ----------
vec2 cellCoord(vec3 w) { return vec2(w.x + uGridOrigin.x, uGridOrigin.y - w.z) / CELL; }
vec4 slots(vec2 c) {
    ivec2 sz = textureSize(uLookup, 0);
    ivec2 ic = ivec2(floor(c));
    if (any(lessThan(ic, ivec2(0))) || any(greaterThanEqual(ic, sz))) return vec4(0.0);
    return texelFetch(uLookup, ic, 0);
}

vec3 photo(vec3 w) {
    vec2 c = cellCoord(w);
    vec4 s = slots(c);
    vec2 f = c - floor(c);
    vec2 gx = dFdx(c) * vec2(1.0, -1.0), gy = dFdy(c) * vec2(1.0, -1.0);
    vec2 uv = vec2(f.x, 1.0 - f.y);
    if (s.x > 0.5) return textureGrad(uPhoto, vec3(uv, s.x - 1.0), gx, gy).rgb;
    if (s.z > 0.5) return textureGrad(uCoarse, vec3(uv, s.z - 1.0), gx, gy).rgb;
    return vec3(0.36, 0.44, 0.30);
}

int groundId(vec3 w) {
    vec2 c = cellCoord(w);
    vec4 s = slots(c);
    if (s.y < 0.5) return 0;
    vec2 f = c - floor(c);
    ivec2 px = clamp(ivec2(vec2(f.x, 1.0 - f.y) * GROUND_PX), ivec2(0), ivec2(int(GROUND_PX) - 1));
    return int(texelFetch(uGround, ivec3(px, int(s.y - 1.0 + 0.5)), 0).r * 255.0 + 0.5);
}

// voegen tussen tegels/klinkers (1 = voeg), vervaagt op afstand tegen flikkeren
float joints(vec2 q, vec2 jw) {
    vec2 f = abs(fract(q) - 0.5);
    vec2 fw = fwidth(q);
    vec2 m = smoothstep(0.5 - jw - fw, 0.5 - jw + fw, f);
    return max(m.x, m.y) * (1.0 - smoothstep(0.2, 0.5, max(fw.x, fw.y)));
}
vec3 bricks(vec2 p, vec2 size, float offset, vec3 c1, vec3 c2, vec3 joint) {
    vec2 q = p / size;
    q.x += offset * mod(floor(q.y), 2.0);
    float fade = smoothstep(0.15, 0.5, max(fwidth(q).x, fwidth(q).y));
    vec3 c = mix(mix(c1, c2, hash12(floor(q))), (c1 + c2) * 0.5, fade);
    return mix(c, joint, joints(q, vec2(0.05, 0.09)) * 0.85);
}

vec3 groundColor(int id, vec3 w, vec3 ph) {
    vec2 p = w.xz;
    float n1 = fbm(p * 0.3), n3 = vnoise(p * 22.0);
    float fine = mix(0.92 + 0.16 * n3, 1.0, smoothstep(0.03, 0.1, fwidth(p.x)));
    if (id == 1) return vec3(0.25, 0.25, 0.255) * (0.82 + 0.3 * n1) * fine;                       // asfalt
    if (id == 2) { vec3 c = vec3(0.56, 0.56, 0.54) * (0.88 + 0.2 * n1); return mix(c, c * 0.65, joints(p / 3.0, vec2(0.004))); } // beton
    if (id == 3) return bricks(p, vec2(0.21, 0.105), 0.5, vec3(0.40, 0.38, 0.37), vec3(0.50, 0.47, 0.44), vec3(0.28)) * (0.9 + 0.2 * n1);
    if (id == 4) return bricks(p, vec2(0.30, 0.30), 0.0, vec3(0.58, 0.58, 0.56), vec3(0.66, 0.65, 0.63), vec3(0.42)) * (0.9 + 0.2 * n1);
    if (id == 5) return vec3(0.50, 0.19, 0.16) * (0.85 + 0.3 * n1) * fine;                        // rood fietspad
    if (id == 6) return mix(mix(vec3(0.25, 0.40, 0.15), vec3(0.36, 0.47, 0.19), n1) * (0.85 + 0.3 * n3), ph, 0.35);
    if (id == 7) return mix(vec3(0.15, 0.25, 0.10) * (0.7 + 0.6 * vnoise(p * 3.0)), ph, 0.4);
    if (id == 8) return vec3(0.09, 0.13, 0.14);
    if (id == 9) return vec3(0.40, 0.37, 0.34) * (0.7 + 0.5 * n3);                                // spoorballast
    if (id == 10) return vec3(0.58, 0.53, 0.45) * (0.8 + 0.35 * n3);                              // half verhard
    if (id == 11) return bricks(p, vec2(0.20, 0.10), 0.5, vec3(0.50, 0.49, 0.47), vec3(0.58, 0.56, 0.53), vec3(0.38)) * (0.9 + 0.2 * n1);
    if (id == 12) return bricks(p, vec2(0.21, 0.105), 0.5, vec3(0.30, 0.30, 0.31), vec3(0.38, 0.38, 0.39), vec3(0.21)) * (0.9 + 0.2 * n1);
    if (id == 13) return mix(vec3(0.70, 0.63, 0.50) * (0.85 + 0.25 * n1) * fine, ph, 0.4);       // zand
    if (id == 14) return bricks(p, vec2(0.21, 0.105), 0.5, vec3(0.48, 0.21, 0.17), vec3(0.57, 0.26, 0.20), vec3(0.30, 0.20, 0.18));
    return ph;
}

float fogAmount(float dist) { float t = dist / uFogDist; return clamp(1.0 - exp(-t * t), 0.0, 1.0); }

void main() {
    vec3 n = normalize(vN);
    vec3 toCam = uCam - vW;
    float dist = length(toCam);
    vec3 v = toCam / dist;
    if (dot(n, v) < 0.0 && uMode != 0) n = -n;   // dubbelzijdige belichting
    float diff = max(dot(n, uSun), 0.0);
    float hemi = 0.5 + 0.5 * n.y;
    vec3 ambient = mix(vec3(0.56, 0.57, 0.60), uSky * 0.85, 0.3) * (0.86 + 0.14 * hemi) * uAmb;
    vec3 sunCol = uSunCol;
    vec3 col;
    float fog = fogAmount(dist);

    if (uMode == 0) {
        vec3 ph = photo(vW);
        int id = groundId(vW);
        vec3 base = groundColor(id, vW, ph);
        if (id == 8) {   // water: lucht weerspiegelen
            float fres = 0.04 + 0.96 * pow(1.0 - max(v.y, 0.0), 5.0);
            base = mix(base, uSky * 0.9, fres * 0.8);
        }
        base = mix(base, ph, smoothstep(150.0, 450.0, dist) * 0.75 * float(id != 0));
        col = base * (id == 0 ? vec3(0.80 * uAmb) + sunCol * (0.30 * diff) : (ambient * 0.9 + sunCol * diff * 0.55));
    } else if (uMode == 1) {
        vec3 base = vC;
        if (vE > 6500.0 && vE < 7500.0) {
            // LCD-bord: licht zelf op (overdag iets minder fel dan het helderste wit, 's nachts goed leesbaar)
            vec3 t = texture(uBoards, vC.xy).rgb;
            oColor = vec4(mix(t * 1.05, uFog, fog * 0.85), 1.0);
            return;
        }
        if (vE > 4500.0 && vE < 9000.0) {
            vec3 t = texture(uSigns, vC.xy).rgb;
            oColor = vec4(mix(t * 0.95, uFog, fog), 1.0);
            return;
        }
        if (vE >= 10000.0) {
            // spoorbed: ballast met betonnen dwarsliggers (vC.x = afstand tot hartlijn)
            float along = vE - 10000.0;
            float lat = abs(vC.x);
            float nn = vnoise(vW.xz * 18.0);
            base = vec3(0.36, 0.33, 0.30) * (0.75 + 0.45 * nn);
            float q = fract(along / 0.6);
            float fw = fwidth(along / 0.6);
            float sl = smoothstep(0.0, fw, q) * (1.0 - smoothstep(0.38 - fw, 0.38, q));
            if (lat < 1.3) base = mix(base, vec3(0.58, 0.56, 0.53) * (0.9 + 0.1 * nn), sl * (1.0 - smoothstep(0.3, 0.8, fw)));
        } else if (vE < -5900.0 && vE > -6100.0) {
            oColor = vec4(mix(vC, uFog, fog), 1.0);   // lichtvlak: zelflichtend
            return;
        } else if (vE < -4900.0 && vE > -5100.0) {
            // glas: weerspiegeling + doorschijnend (transparante pass)
            vec3 r = reflect(-v, n);
            float fres = pow(1.0 - abs(dot(v, n)), 3.0);
            vec3 env = mix(vec3(0.35, 0.36, 0.36), uSky * 1.1, smoothstep(-0.1, 0.4, r.y));
            vec3 c = mix(vC, env, 0.25 + 0.6 * fres);
            float sp = pow(max(dot(r, uSun), 0.0), 150.0) * uSunCol.g;
            oColor = vec4(mix(c + sp, uFog, fog), 0.18 + 0.55 * fres);
            return;
        } else if (vE < -2999.0 && vE > -3100.0) {
            // hekwerk: spijlen
            vec2 t = normalize(vec2(-n.z, n.x) + vec2(1e-5));
            float s = dot(vW.xz, t) / 0.11;
            float fw = fwidth(s);
            float keep = mix(0.30, 0.14, smoothstep(0.2, 1.0, fw));
            if (fract(s) > keep) discard;
        } else if (vE < -6900.0 && vE > -7100.0) {
            // tegelwand (station Best): beige/bruin ruitpatroon, vC = basiskleur
            vec2 t = normalize(vec2(-n.z, n.x) + vec2(1e-5));
            vec2 q = vec2(dot(vW.xz, t), vW.y) / 0.30;
            float chk = mod(floor(q.x) + floor(q.y), 2.0);
            float band = step(0.5, fract(floor(q.y) / 6.0 + 0.01)) * 0.08;
            base = mix(vC, vC * vec3(0.78, 0.66, 0.55), chk * 0.6) * (0.95 + band);
            base = mix(base, vec3(0.45), joints(q, vec2(0.03)) * 0.5);
        } else if (vE < -8700.0 && vE > -8900.0) {
            oColor = vec4(mix(vC * (0.80 + 0.15 * uAmb), uFog, fog), 1.0);  // binnen (tunnel): egaal kunstlicht
            return;
        } else if (vE < -7900.0 && vE > -8100.0) {
            // mozaiek (tunnel Zwolle): tegeltjes van 2,5 cm, matglanzend, licht wisselend per tegel; vC = kleur van de laag
            vec2 t = normalize(vec2(-n.z, n.x) + vec2(1e-5));
            vec2 q = vec2(dot(vW.xz, t), vW.y) / 0.025;
            float fw = max(fwidth(q.x), fwidth(q.y));
            float var = (hash12(floor(q)) - 0.5) * 0.07 * (1.0 - smoothstep(0.3, 1.0, fw));
            base = vC * (1.0 + var);
            base = mix(base, base * 0.82, joints(q, vec2(0.08)) * (1.0 - smoothstep(0.2, 0.6, fw)));
            oColor = vec4(mix(base * (0.80 + 0.15 * uAmb), uFog, fog), 1.0);  // binnen: kunstlicht, geen zon
            return;
        } else if (vE < -8400.0 && vE > -8600.0) {
            // tegelvloer 60 x 60 cm (natuursteen/beton), vC = basiskleur
            vec2 q = vW.xz / 0.6;
            float fw = max(fwidth(q.x), fwidth(q.y));
            float var = (hash12(floor(q)) - 0.5) * 0.06 + (vnoise(vW.xz * 9.0) - 0.5) * 0.05;
            base = vC * (1.0 + var);
            base = mix(base, base * 0.7, joints(q, vec2(0.012)) * (1.0 - smoothstep(0.1, 0.5, fw)));
            oColor = vec4(mix(base * (0.80 + 0.15 * uAmb), uFog, fog), 1.0);  // binnen: kunstlicht
            return;
        } else if (vE < -500.0 && vE > -1500.0 && n.y > 0.3) {
            base = mix(photo(vW) * 1.05, vC, 0.2);   // daken: luchtfoto
        } else if (vE > -500.0 && abs(n.y) < 0.4) {
            // procedurele ramen op gevels
            float h = vW.y - vE;
            vec2 t = normalize(vec2(-n.z, n.x) + vec2(1e-5));
            float s = dot(vW.xz, t) / 2.8;
            float vv = h / 3.1;
            float wx = fract(s), wy = fract(vv);
            float fw = max(fwidth(s), fwidth(vv));
            float mx = smoothstep(0.22 - fw, 0.22 + fw, wx) * (1.0 - smoothstep(0.78 - fw, 0.78 + fw, wx));
            float my = smoothstep(0.30 - fw, 0.30 + fw, wy) * (1.0 - smoothstep(0.85 - fw, 0.85 + fw, wy));
            float m = mx * my * step(0.6, h);
            m = mix(m, 0.28, smoothstep(0.25, 0.6, fw));
            vec3 glass = vec3(0.15, 0.18, 0.22) + uSky * 0.22 * (0.6 + 0.4 * wy);
            base = mix(base, glass, m);
        }
        vec3 light = ambient + sunCol * diff * 0.65;
        if (vE < -500.0 && vE > -1500.0 && n.y > 0.3) light = vec3(0.80 * uAmb) + sunCol * (0.30 * diff);
        col = base * light;
    } else {
        // voertuigen: lak, glas, rubber, lampen
        int mat = int(vE + 0.5);
        if (mat == 3) {
            col = vC;
        } else if (mat == 5) {
            // interieur: egaal kunstlicht (plafondverlichting), iets lichter bij de vloer-normaal omhoog
            col = vC * (0.62 + 0.30 * uAmb + 0.08 * hemi);
        } else {
            col = vC * (ambient + sunCol * diff * 0.7);
            vec3 r = reflect(-v, n);
            float fres = pow(1.0 - max(dot(v, n), 0.0), 4.0);
            vec3 env = mix(vec3(0.30, 0.30, 0.29), uSky * 1.05, smoothstep(-0.15, 0.25, r.y));
            float spec = pow(max(dot(r, uSun), 0.0), mat == 1 ? 120.0 : 260.0);  // lak: kleine, scherpe zonreflectie
            if (mat == 1) { col = mix(col, env, 0.10 + 0.65 * fres); col += sunCol * spec * 0.9; }
            else if (mat == 0 || mat == 4) { col = mix(col, env, 0.05 + 0.28 * fres); col += sunCol * spec * 0.45; }
        }
    }
    oColor = vec4(mix(col, uFog, fog), 1.0);
})";

template <typename T>
static bool readT(FILE* f, T& v) { return fread(&v, sizeof(T), 1, f) == 1; }

// ======================================================================
// Laadthread
// ======================================================================
struct TileLoad {
    int idx = -1;
    bool withGround = false, groundOnly = false, ok = false;
    int n = 0;
    std::vector<float> h;
    std::vector<Vertex> tv;
    std::vector<uint32_t> ti;
    std::vector<std::vector<Vertex>> mv[3];
    std::vector<std::vector<uint32_t>> mi[3];
    std::vector<std::vector<uint8_t>> photo;  // per mipniveau (detail: DXT1, grof: RGB)
    int photoSize = 0;
    std::vector<uint8_t> ground;               // GROUND_PX^2
};

struct Job { int idx; bool withGround, groundOnly; };

struct World::Shared {
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
    std::deque<Job> jobs;
    std::vector<TileLoad*> done;
    int busy = 0;
    bool quit = false;
    World* world = nullptr;
    std::vector<HANDLE> threads;
};

static std::string tileName(int x, int y) { return std::to_string(x) + "_" + std::to_string(y); }

// Mipketen (RGB, vierkant) door 2x2 middelen
static std::vector<std::vector<uint8_t>> mipChain(const uint8_t* img, int size) {
    std::vector<std::vector<uint8_t>> lv;
    lv.emplace_back(img, img + (size_t)size * size * 3);
    int s = size;
    while (s > 1) {
        int ns = s / 2;
        const std::vector<uint8_t>& cur = lv.back();
        std::vector<uint8_t> nx((size_t)ns * ns * 3);
        for (int y = 0; y < ns; y++)
            for (int x = 0; x < ns; x++)
                for (int c = 0; c < 3; c++) {
                    int sum = cur[((size_t)(y * 2) * s + x * 2) * 3 + c] + cur[((size_t)(y * 2) * s + x * 2 + 1) * 3 + c] +
                              cur[((size_t)(y * 2 + 1) * s + x * 2) * 3 + c] + cur[((size_t)(y * 2 + 1) * s + x * 2 + 1) * 3 + c];
                    nx[((size_t)y * ns + x) * 3 + c] = (uint8_t)(sum / 4);
                }
        lv.push_back(std::move(nx));
        s = ns;
    }
    return lv;
}

unsigned long __stdcall World::threadMain(void* p) {
    Shared* sh = (Shared*)p;
    World* w = sh->world;
    for (;;) {
        EnterCriticalSection(&sh->cs);
        while (sh->jobs.empty() && !sh->quit) SleepConditionVariableCS(&sh->cv, &sh->cs, INFINITE);
        if (sh->quit) { LeaveCriticalSection(&sh->cs); return 0; }
        Job job = sh->jobs.front();
        sh->jobs.pop_front();
        sh->busy++;
        LeaveCriticalSection(&sh->cs);

        TileLoad* r = new TileLoad;
        r->idx = job.idx;
        r->withGround = job.withGround;
        r->groundOnly = job.groundOnly;
        const Tile& t = w->tiles_[job.idx];
        std::string base = tileName(t.x, t.y);
        std::string tp = DataStore::fetch("world/tiles/" + base + ".bin");  // met een server: downloaden (blokkeert deze laadthread)
        FILE* f = tp.empty() ? nullptr : fopen(tp.c_str(), "rb");
        if (f) {
            uint32_t magic = 0; int32_t ver = 0, tx, ty; uint8_t det; int32_t n;
            readT(f, magic); readT(f, ver); readT(f, tx); readT(f, ty); readT(f, det); readT(f, n);
            if (magic == TILE_MAGIC && ver == TILE_VERSION && n > 1 && n < 4096) {
                r->n = n;
                r->h.resize((size_t)n * n);
                fread(r->h.data(), 4, r->h.size(), f);
                uint32_t hb = 0;
                readT(f, hb);
                std::vector<uint8_t> holes(hb);
                if (hb) fread(holes.data(), 1, hb, f);
                for (int g = 0; g < 3; g++) {
                    uint32_t nc = 0;
                    readT(f, nc);
                    for (uint32_t c = 0; c < nc; c++) {
                        uint32_t nv = 0, ni = 0;
                        readT(f, nv); readT(f, ni);
                        if (job.groundOnly) { fseek(f, (long)(nv * sizeof(Vertex) + ni * 4), SEEK_CUR); continue; }
                        std::vector<Vertex> v(nv);
                        std::vector<uint32_t> ix(ni);
                        if (nv) fread(v.data(), sizeof(Vertex), nv, f);
                        if (ni) fread(ix.data(), 4, ni, f);
                        if (nv && ni) { r->mv[g].push_back(std::move(v)); r->mi[g].push_back(std::move(ix)); }
                    }
                }
                int32_t px = 0; uint32_t rb = 0;
                readT(f, px); readT(f, rb);
                if (job.withGround && px == GROUND_PX && rb) {
                    std::vector<uint8_t> rl(rb);
                    fread(rl.data(), 1, rb, f);
                    r->ground.assign((size_t)GROUND_PX * GROUND_PX, 0);
                    size_t o = 0;
                    for (size_t k = 0; k + 1 < rl.size() && o < r->ground.size(); k += 2) {
                        size_t cnt = std::min<size_t>(rl[k], r->ground.size() - o);
                        memset(&r->ground[o], rl[k + 1], cnt);
                        o += cnt;
                    }
                }
                r->ok = true;
                // station Zwolle: BGT-vlakken in de trapgaten en de oude (platte) kappen weglaten; de eigen tunnel en
                // kappen staan in zwollestation.cpp
                if (!job.groundOnly && t.detail && Zwolle::isNear(t.x + TILE / 2.0, t.y + TILE / 2.0, TILE)) {
                    const float sp = (float)TILE / (n - 1);
                    auto terrainH = [&](double rx, double ry) {
                        float fx = (float)std::max(0.0, std::min((double)n - 1.001, (rx - t.x) / sp));
                        float fy = (float)std::max(0.0, std::min((double)n - 1.001, (t.y + TILE - ry) / sp));
                        int i = (int)fx, j = (int)fy;
                        float tx = fx - i, ty = fy - j;
                        auto H = [&](int a, int b2) { return r->h[(size_t)b2 * n + a]; };
                        return (H(i, j) * (1 - tx) + H(i + 1, j) * tx) * (1 - ty) + (H(i, j + 1) * (1 - tx) + H(i + 1, j + 1) * tx) * ty;
                    };
                    for (int g = 0; g < 3; g++)
                        for (size_t k = 0; k < r->mv[g].size(); k++) {
                            const std::vector<Vertex>& vv = r->mv[g][k];
                            std::vector<uint32_t>& ix = r->mi[g][k];
                            std::vector<uint32_t> keep;
                            keep.reserve(ix.size());
                            for (size_t q = 0; q + 2 < ix.size(); q += 3) {
                                const Vertex &a = vv[ix[q]], &b2 = vv[ix[q + 1]], &c = vv[ix[q + 2]];
                                float cx = (a.px + b2.px + c.px) / 3, cy = (a.py + b2.py + c.py) / 3, cz = (a.pz + b2.pz + c.pz) / 3;
                                const double trx[3] = {rdX(a.px), rdX(b2.px), rdX(c.px)}, tryy[3] = {rdY(a.pz), rdY(b2.pz), rdY(c.pz)};
                                if (Zwolle::cutTriangle(trx, tryy, cy - terrainH(rdX(cx), rdY(cz)))) continue;
                                keep.insert(keep.end(), {ix[q], ix[q + 1], ix[q + 2]});
                            }
                            ix.swap(keep);
                        }
                }
                if (!job.groundOnly) {
                    // ---- terreinmesh (met gaten en randschort) ----
                    const float sp = (float)TILE / (n - 1);
                    const bool detail = t.detail;
                    auto H = [&](int i, int j) { return r->h[(size_t)std::max(0, std::min(n - 1, j)) * n + std::max(0, std::min(n - 1, i))]; };
                    r->tv.reserve((size_t)n * n + 8 * n);
                    for (int j = 0; j < n; j++)
                        for (int i = 0; i < n; i++) {
                            Vertex vx;
                            vx.px = gameX(t.x + i * sp);
                            vx.pz = gameZ(t.y + TILE - j * sp);
                            vx.py = H(i, j) - (detail ? 0.0f : 0.4f);
                            Vec3 nn = normalize(Vec3(H(i - 1, j) - H(i + 1, j), 2 * sp, H(i, j - 1) - H(i, j + 1)));
                            vx.nx = nn.x; vx.ny = nn.y; vx.nz = nn.z;
                            vx.r = vx.g = vx.b = 1;
                            vx.extra = 0;
                            r->tv.push_back(vx);
                        }
                    const int Q = n - 1;
                    const bool zw = detail && Zwolle::isNear(t.x + TILE / 2.0, t.y + TILE / 2.0, TILE);  // station Zwolle: tunnelgaten
                    for (int j = 0; j < Q; j++)
                        for (int i = 0; i < Q; i++) {
                            size_t b = (size_t)j * Q + i;
                            if (!holes.empty() && b / 8 < holes.size() && (holes[b / 8] >> (b % 8) & 1)) continue;
                            if (zw && Zwolle::cut(t.x + (i + 0.5) * sp, t.y + TILE - (j + 0.5) * sp, 0.0f)) continue;
                            uint32_t a = j * n + i, bb = a + 1, c = a + n, d = c + 1;
                            r->ti.insert(r->ti.end(), {a, c, bb, bb, c, d});
                        }
                    // randschort naar beneden waar de buur geen detailtegel is (geen kieren met grove tegels)
                    for (int side = 0; side < 4; side++) {
                        int nxT = t.x + (side == 0 ? -TILE : side == 1 ? TILE : 0), nyT = t.y + (side == 2 ? TILE : side == 3 ? -TILE : 0);
                        int ni = w->findTile(nxT + 1, nyT + 1);
                        if (detail && ni >= 0 && w->tiles_[ni].detail) continue;
                        for (int k = 0; k < Q; k++) {
                            int i0, j0, i1, j1;
                            if (side == 0) { i0 = 0; j0 = k; i1 = 0; j1 = k + 1; }
                            else if (side == 1) { i0 = Q; j0 = k; i1 = Q; j1 = k + 1; }
                            else if (side == 2) { i0 = k; j0 = 0; i1 = k + 1; j1 = 0; }
                            else { i0 = k; j0 = Q; i1 = k + 1; j1 = Q; }
                            uint32_t a = j0 * n + i0, b = j1 * n + i1;
                            Vertex va = r->tv[a], vb = r->tv[b];
                            va.py -= 4.0f; vb.py -= 4.0f;
                            uint32_t c = (uint32_t)r->tv.size();
                            r->tv.push_back(va);
                            r->tv.push_back(vb);
                            r->ti.insert(r->ti.end(), {a, b, c + 1, a, c + 1, c});
                        }
                    }
                    // ---- luchtfoto ----
                    std::string pp = DataStore::fetch(std::string(detail ? "raw/luchtfoto/" : "raw/luchtfoto_lo/") + base + ".jpg");
                    int iw = 0, ih = 0, comp;
                    unsigned char* img = pp.empty() ? nullptr : stbi_load(pp.c_str(), &iw, &ih, &comp, 3);
                    int want = detail ? 2048 : COARSE_PHOTO_PX;
                    if (img && iw == want && ih == want) {
                        auto lv = mipChain(img, iw);
                        r->photoSize = iw;
                        for (size_t l = 0; l < lv.size(); l++) {
                            int s = iw >> l;
                            if (detail) r->photo.push_back(Dxt::compress(lv[l].data(), s, s));
                            else r->photo.push_back(std::move(lv[l]));
                        }
                    }
                    if (img) stbi_image_free(img);
                }
            }
            fclose(f);
        }
        EnterCriticalSection(&sh->cs);
        sh->done.push_back(r);
        sh->busy--;
        LeaveCriticalSection(&sh->cs);
    }
}

// ======================================================================
World::~World() {
    if (!sh_) return;
    EnterCriticalSection(&sh_->cs);
    sh_->quit = true;
    sh_->jobs.clear();
    LeaveCriticalSection(&sh_->cs);
    WakeAllConditionVariable(&sh_->cv);
    for (HANDLE h : sh_->threads) { WaitForSingleObject(h, 5000); CloseHandle(h); }
}

int World::findTile(double rx, double ry) const {
    int x = (int)std::floor(rx / TILE) * TILE, y = (int)std::floor(ry / TILE) * TILE;
    auto it = tileIdx_.find(((int64_t)x << 32) | (uint32_t)y);
    return it == tileIdx_.end() ? -1 : it->second;
}

bool World::load(const std::string& dataDir, std::string& error) {
    dataDir_ = dataDir;
    std::string path = dataDir + "/world/world.bin";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { error = "Kan " + path + " niet openen. Draai eerst convert.bat."; return false; }
    uint32_t magic = 0; int32_t version = 0;
    readT(f, magic);
    readT(f, version);
    if (magic != MAGIC || version != VERSION) { fclose(f); error = "world.bin heeft een verkeerde versie. Draai convert.bat opnieuw."; return false; }

    uint32_t nt = 0;
    readT(f, nt);
    int minx = 1 << 30, miny = 1 << 30, maxx = -(1 << 30), maxy = -(1 << 30);
    for (uint32_t i = 0; i < nt; i++) {
        Tile t;
        int32_t x, y; uint8_t d;
        readT(f, x); readT(f, y); readT(f, d);
        t.x = x; t.y = y; t.detail = d != 0;
        tileIdx_[((int64_t)x << 32) | (uint32_t)y] = (int)tiles_.size();
        tiles_.push_back(t);
        minx = std::min(minx, x); miny = std::min(miny, y); maxx = std::max(maxx, x); maxy = std::max(maxy, y);
    }
    if (tiles_.empty()) { fclose(f); error = "world.bin bevat geen tegels."; return false; }
    gx0_ = minx; gy0_ = miny; gcols_ = (maxx - minx) / TILE + 1; grows_ = (maxy - miny) / TILE + 1;

    uint32_t nl = 0;
    readT(f, nl);
    for (uint32_t i = 0; i < nl; i++) {
        MapLine l;
        uint32_t np;
        readT(f, l.kind); readT(f, np);
        l.pts.resize(np);
        for (auto& p : l.pts) { readT(f, p.first); readT(f, p.second); }
        lines.push_back(std::move(l));
    }
    uint32_t npnt = 0;
    readT(f, npnt);
    for (uint32_t i = 0; i < npnt; i++) {
        MapPoint p;
        uint16_t len;
        readT(f, p.kind); readT(f, p.x); readT(f, p.z); readT(f, len);
        p.name.resize(len);
        if (len) fread(&p.name[0], 1, len, f);
        points.push_back(std::move(p));
    }
    uint32_t nb = 0;
    readT(f, nb);
    for (uint32_t i = 0; i < nb; i++) {
        BusBay b;
        readT(f, b.x); readT(f, b.z); readT(f, b.heading); readT(f, b.length);
        bays.push_back(b);
    }
    uint32_t nr = 0;
    readT(f, nr);
    for (uint32_t i = 0; i < nr; i++) {
        RailLine r;
        uint8_t svc; uint32_t np;
        readT(f, svc); readT(f, np);
        r.main = (svc & 1) == 0;
        r.tunnel = (svc & 2) != 0;
        r.sunken = (svc & 4) != 0;
        r.pts.resize(np);
        for (auto& p : r.pts) { readT(f, p.x); readT(f, p.y); readT(f, p.z); readT(f, p.node); }
        rails.push_back(std::move(r));
    }
    uint32_t npl = 0;
    readT(f, npl);
    for (uint32_t i = 0; i < npl; i++) {
        OsmPlatform p;
        uint16_t len; uint32_t np;
        readT(f, len);
        p.name.resize(len);
        if (len) fread(&p.name[0], 1, len, f);
        readT(f, p.height); readT(f, np);
        p.pts.resize(np);
        for (auto& q : p.pts) { readT(f, q.first); readT(f, q.second); }
        platforms.push_back(std::move(p));
    }
    uint32_t nh = 0;
    readT(f, nh);
    for (uint32_t i = 0; i < nh; i++) {
        HallRing h;
        uint32_t np;
        readT(f, h.cut); readT(f, np);
        h.pts.resize(np);
        for (auto& q : h.pts) { readT(f, q.first); readT(f, q.second); }
        halls.push_back(std::move(h));
    }
    uint32_t nn = 0, ne = 0;
    readT(f, nn);
    roadNodes.resize(nn);
    for (auto& n : roadNodes) { readT(f, n.x); readT(f, n.y); readT(f, n.z); }
    readT(f, ne);
    for (uint32_t i = 0; i < ne; i++) {
        int32_t a, b; uint8_t ow, cls; float sp;
        readT(f, a); readT(f, b); readT(f, ow); readT(f, cls); readT(f, sp);
        roadEdges.push_back({a, b, ow != 0, cls, sp});
    }
    uint32_t nf = 0;
    readT(f, nf);
    for (uint32_t i = 0; i < nf; i++) {
        WorldFeature ft;
        uint32_t np;
        readT(f, ft.kind); readT(f, ft.a); readT(f, ft.b); readT(f, np);
        ft.pts.resize(np);
        for (auto& q : ft.pts) { readT(f, q.x); readT(f, q.y); readT(f, q.z); }
        features.push_back(std::move(ft));
    }
    auto rstr = [&](std::string& s) {
        uint16_t len = 0;
        readT(f, len);
        s.resize(len);
        if (len) fread(&s[0], 1, len, f);
    };
    uint32_t nbr = 0;
    readT(f, nbr);
    for (uint32_t i = 0; i < nbr && i < 64; i++) {
        BusRoute r;
        uint32_t np = 0, ns = 0;
        rstr(r.ref);
        rstr(r.name);
        readT(f, np);
        r.pts.resize(np);
        for (auto& p : r.pts) { readT(f, p.x); readT(f, p.y); readT(f, p.z); readT(f, p.lim); }
        readT(f, ns);
        r.stops.resize(ns);
        for (auto& s : r.stops) { rstr(s.name); readT(f, s.x); readT(f, s.z); readT(f, s.s); readT(f, s.lat); readT(f, s.flags); }
        busRoutes.push_back(std::move(r));
    }
    fclose(f);

    // ---- textuur-arrays met vaste aantallen slots ----
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    photoLevels_ = 12;  // 2048 .. 1
    glGenTextures(1, &photoTex_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, photoTex_);
    for (int l = 0, s = 2048; l < photoLevels_; l++, s = std::max(1, s / 2))
        glTexImage3D(GL_TEXTURE_2D_ARRAY, l, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, s, s, PHOTO_SLOTS, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, photoLevels_ - 1);
    glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
    if (glGetError() != GL_NO_ERROR) { error = "Kan geen gecomprimeerde textuur-array maken (DXT1)."; return false; }

    glGenTextures(1, &groundTex_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, groundTex_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8, GROUND_PX, GROUND_PX, GROUND_SLOTS, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &coarseTex_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, coarseTex_);
    int clev = 0;
    for (int s = COARSE_PHOTO_PX; s >= 1; s /= 2, clev++)
        glTexImage3D(GL_TEXTURE_2D_ARRAY, clev, GL_RGB8, s, s, COARSE_SLOTS, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, clev - 1);

    lookup_.assign((size_t)gcols_ * grows_ * 4, 0.0f);
    glGenTextures(1, &lookupTex_);
    glBindTexture(GL_TEXTURE_2D, lookupTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, gcols_, grows_, 0, GL_RGBA, GL_FLOAT, lookup_.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGetError();

    for (int i = PHOTO_SLOTS - 1; i >= 0; i--) freePhoto_.push_back(i);
    for (int i = GROUND_SLOTS - 1; i >= 0; i--) freeGround_.push_back(i);
    for (int i = COARSE_SLOTS - 1; i >= 0; i--) freeCoarse_.push_back(i);

    prog_ = compileProgram(WORLD_VS, WORLD_FS, "world");
    uViewProj_ = glGetUniformLocation(prog_, "uViewProj");
    uModel_ = glGetUniformLocation(prog_, "uModel");
    uMode_ = glGetUniformLocation(prog_, "uMode");
    uPhoto_ = glGetUniformLocation(prog_, "uPhoto");
    uGround_ = glGetUniformLocation(prog_, "uGround");
    uCoarse_ = glGetUniformLocation(prog_, "uCoarse");
    uLookup_ = glGetUniformLocation(prog_, "uLookup");
    uSigns_ = glGetUniformLocation(prog_, "uSigns");
    uBoards_ = glGetUniformLocation(prog_, "uBoards");
    uGridOrigin_ = glGetUniformLocation(prog_, "uGridOrigin");
    uSun_ = glGetUniformLocation(prog_, "uSun");
    uCam_ = glGetUniformLocation(prog_, "uCam");
    uFog_ = glGetUniformLocation(prog_, "uFog");
    uSky_ = glGetUniformLocation(prog_, "uSky");
    uFogDist_ = glGetUniformLocation(prog_, "uFogDist");

    // laadthreads
    sh_ = new Shared;
    sh_->world = this;
    InitializeCriticalSection(&sh_->cs);
    InitializeConditionVariable(&sh_->cv);
    for (int i = 0; i < 3; i++) {
        HANDLE h = CreateThread(nullptr, 0, threadMain, sh_, 0, nullptr);
        if (h) sh_->threads.push_back(h);
    }
    if (sh_->threads.empty()) { error = "Kan geen laadthread starten."; return false; }
    return true;
}

// ---------- hoogte ----------
const World::HeightTile* World::heightTile(int idx) const {
    auto it = heights_.find(idx);
    if (it != heights_.end()) {
        lru_.remove(idx);
        lru_.push_front(idx);
        return &it->second;
    }
    const Tile& t = tiles_[idx];
    HeightTile ht;
    // hoofdthread: niet wachten op een download (dan tijdelijk geen hoogte, de tegel komt vanzelf binnen)
    std::string hp = DataStore::peek("world/tiles/" + tileName(t.x, t.y) + ".bin");
    if (hp.empty()) {
        static HeightTile none;
        return &none;
    }
    FILE* f = fopen(hp.c_str(), "rb");
    if (f) {
        uint32_t magic = 0; int32_t ver = 0, tx, ty, n = 0; uint8_t det;
        readT(f, magic); readT(f, ver); readT(f, tx); readT(f, ty); readT(f, det); readT(f, n);
        if (magic == TILE_MAGIC && n > 1 && n < 4096) {
            ht.n = n;
            ht.h.resize((size_t)n * n);
            if (fread(ht.h.data(), 4, ht.h.size(), f) != ht.h.size()) ht.n = 0;
        }
        fclose(f);
    }
    if (lru_.size() >= 160) { heights_.erase(lru_.back()); lru_.pop_back(); }
    lru_.push_front(idx);
    return &(heights_[idx] = std::move(ht));
}

float World::heightAt(float x, float z) const {
    double rx = rdX(x), ry = rdY(z);
    // Zwolle: in de trapgaten en opgangen is geen terrein (daar lopen trappen en roltrappen naar de tunnel)
    if (Zwolle::isNear(rx, ry, 0) && Zwolle::cut(rx, ry, 0.0f)) return -10.0f;
    int idx = findTile(rx, ry);
    if (idx < 0) return 17.0f;
    std::lock_guard<std::mutex> lock(heightMx_);
    const HeightTile* ht = heightTile(idx);
    if (!ht || ht->n < 2) return 17.0f;
    const Tile& t = tiles_[idx];
    int n = ht->n;
    float sp = (float)TILE / (n - 1);
    float fx = clampf((float)((rx - t.x) / sp), 0, n - 1.001f);
    float fy = clampf((float)((t.y + TILE - ry) / sp), 0, n - 1.001f);
    int i = (int)fx, j = (int)fy;
    float tx = fx - i, ty = fy - j;
    auto H = [&](int a, int b) { return ht->h[(size_t)b * n + a]; };
    float h = (H(i, j) * (1 - tx) + H(i + 1, j) * tx) * (1 - ty) + (H(i, j + 1) * (1 - tx) + H(i + 1, j + 1) * tx) * ty;
    return t.detail ? h : h - 0.4f;
}

// ---------- streaming ----------
float World::tileDistance(const Tile& t, Vec3 p) const {
    double rx = rdX(p.x), ry = rdY(p.z);
    double dx = std::max({(double)t.x - rx, 0.0, rx - (t.x + TILE)});
    double dy = std::max({(double)t.y - ry, 0.0, ry - (t.y + TILE)});
    return (float)std::sqrt(dx * dx + dy * dy);
}

void World::requestTile(int idx, bool withGround) {
    Tile& t = tiles_[idx];
    bool groundOnly = t.state == 2;
    t.state = groundOnly ? 2 : 1;
    t.wantGround = withGround;
    EnterCriticalSection(&sh_->cs);
    sh_->jobs.push_back({idx, withGround, groundOnly});
    LeaveCriticalSection(&sh_->cs);
    WakeConditionVariable(&sh_->cv);
}

void World::unloadTile(Tile& t) {
    destroyMesh(t.terrain);
    for (auto* v : {&t.buildings, &t.ground, &t.details}) {
        for (Mesh& m : *v) destroyMesh(m);
        v->clear();
    }
    if (t.photoSlot >= 0) freePhoto_.push_back(t.photoSlot);
    if (t.groundSlot >= 0) freeGround_.push_back(t.groundSlot);
    if (t.coarseSlot >= 0) freeCoarse_.push_back(t.coarseSlot);
    t.photoSlot = t.groundSlot = t.coarseSlot = -1;
    t.state = 0;
    t.wantGround = false;
    int c = (t.x - gx0_) / TILE, r = (t.y - gy0_) / TILE;
    float* L = &lookup_[((size_t)r * gcols_ + c) * 4];
    L[0] = L[1] = L[2] = 0;
    lookupDirty_ = true;
}

void World::finishLoad(TileLoad* r) {
    Tile& t = tiles_[r->idx];
    int c = (t.x - gx0_) / TILE, row = (t.y - gy0_) / TILE;
    float* L = &lookup_[((size_t)row * gcols_ + c) * 4];
    if (r->ok && r->n > 1) {
        // hoogtes meteen in de cache
        std::lock_guard<std::mutex> lock(heightMx_);
        if (!heights_.count(r->idx)) {
            if (lru_.size() >= 160) { heights_.erase(lru_.back()); lru_.pop_back(); }
            lru_.push_front(r->idx);
            heights_[r->idx] = HeightTile{r->n, r->h};
        }
    }
    if (!r->groundOnly) {
        if (t.state != 1) { delete r; return; }  // intussen weer opgeruimd
        t.terrain = createMesh(r->tv, r->ti);
        t.bmin = t.terrain.bmin; t.bmax = t.terrain.bmax;
        for (int g = 0; g < 3; g++) {
            std::vector<Mesh>& dst = g == 0 ? t.buildings : g == 1 ? t.ground : t.details;
            for (size_t k = 0; k < r->mv[g].size(); k++) {
                dst.push_back(createMesh(r->mv[g][k], r->mi[g][k]));
                Mesh& m = dst.back();
                t.bmin = Vec3(std::min(t.bmin.x, m.bmin.x), std::min(t.bmin.y, m.bmin.y), std::min(t.bmin.z, m.bmin.z));
                t.bmax = Vec3(std::max(t.bmax.x, m.bmax.x), std::max(t.bmax.y, m.bmax.y), std::max(t.bmax.z, m.bmax.z));
            }
        }
        if (!r->photo.empty()) {
            if (t.detail && !freePhoto_.empty()) {
                t.photoSlot = freePhoto_.back();
                freePhoto_.pop_back();
                glBindTexture(GL_TEXTURE_2D_ARRAY, photoTex_);
                for (int l = 0; l < (int)r->photo.size() && l < photoLevels_; l++) {
                    int s = std::max(1, r->photoSize >> l);
                    glCompressedTexSubImage3D(GL_TEXTURE_2D_ARRAY, l, 0, 0, t.photoSlot, s, s, 1, GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
                                              (GLsizei)r->photo[l].size(), r->photo[l].data());
                }
                L[0] = t.photoSlot + 1.0f;
            } else if (!t.detail && !freeCoarse_.empty()) {
                t.coarseSlot = freeCoarse_.back();
                freeCoarse_.pop_back();
                glBindTexture(GL_TEXTURE_2D_ARRAY, coarseTex_);
                for (int l = 0; l < (int)r->photo.size(); l++) {
                    int s = std::max(1, r->photoSize >> l);
                    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, l, 0, 0, t.coarseSlot, s, s, 1, GL_RGB, GL_UNSIGNED_BYTE, r->photo[l].data());
                }
                L[2] = t.coarseSlot + 1.0f;
            }
        }
        t.state = 2;
    }
    if (!r->ground.empty() && t.state == 2 && t.groundSlot < 0 && t.wantGround && !freeGround_.empty()) {
        t.groundSlot = freeGround_.back();
        freeGround_.pop_back();
        glBindTexture(GL_TEXTURE_2D_ARRAY, groundTex_);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, t.groundSlot, GROUND_PX, GROUND_PX, 1, GL_RED, GL_UNSIGNED_BYTE, r->ground.data());
        L[1] = t.groundSlot + 1.0f;
    }
    lookupDirty_ = true;
    delete r;
}

void World::update(Vec3 cam) {
    cam_ = cam;
    // klaargezette tegels verwerken (max. een paar per frame tegen haperen)
    std::vector<TileLoad*> done;
    EnterCriticalSection(&sh_->cs);
    int pending = (int)sh_->jobs.size() + sh_->busy;
    size_t take = std::min<size_t>(sh_->done.size(), 3);
    done.assign(sh_->done.begin(), sh_->done.begin() + take);
    sh_->done.erase(sh_->done.begin(), sh_->done.begin() + take);
    pending += (int)sh_->done.size();
    LeaveCriticalSection(&sh_->cs);
    for (TileLoad* r : done) finishLoad(r);

    // opruimen en aanvragen
    std::vector<std::pair<float, int>> want;
    for (int i = 0; i < (int)tiles_.size(); i++) {
        Tile& t = tiles_[i];
        float d = tileDistance(t, cam);
        float R = t.detail ? DETAIL_RADIUS : COARSE_RADIUS;
        if (t.state == 2 && d > R + 300) { unloadTile(t); continue; }
        if (t.state == 2 && t.groundSlot >= 0 && d > GROUND_RADIUS + 250) {
            freeGround_.push_back(t.groundSlot);
            t.groundSlot = -1;
            t.wantGround = false;
            int c = (t.x - gx0_) / TILE, r = (t.y - gy0_) / TILE;
            lookup_[((size_t)r * gcols_ + c) * 4 + 1] = 0;
            lookupDirty_ = true;
        }
        if (t.state == 0 && d < R) want.push_back({d, i});
        else if (t.state == 2 && t.detail && t.groundSlot < 0 && !t.wantGround && d < GROUND_RADIUS && !freeGround_.empty()) want.push_back({d - 1e5f, i});
    }
    std::sort(want.begin(), want.end());
    for (auto& w : want) {
        if (pending >= 6) break;
        Tile& t = tiles_[w.second];
        bool ground = t.detail && tileDistance(t, cam) < GROUND_RADIUS;
        if (t.state == 0 && t.detail && freePhoto_.size() <= (size_t)pending) continue;  // geen fotoslot meer vrij
        requestTile(w.second, ground);
        pending++;
    }
    if (lookupDirty_) {
        glBindTexture(GL_TEXTURE_2D, lookupTex_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gcols_, grows_, GL_RGBA, GL_FLOAT, lookup_.data());
        lookupDirty_ = false;
    }
}

bool World::streamingIdle() const {
    EnterCriticalSection(&sh_->cs);
    bool idle = sh_->jobs.empty() && sh_->busy == 0 && sh_->done.empty();
    LeaveCriticalSection(&sh_->cs);
    if (!idle) return false;
    for (const Tile& t : tiles_) {
        float d = tileDistance(t, cam_);
        if (t.state == 0 && d < (t.detail ? DETAIL_RADIUS : COARSE_RADIUS)) return false;
    }
    return true;
}

int World::loadedTiles() const {
    int n = 0;
    for (const Tile& t : tiles_) n += t.state == 2;
    return n;
}

std::function<void()> World::preloadTick;

void World::preload(Vec3 pos) {
    // lokaal: hooguit ~40 s; met een server mag het langer duren (eerste keer downloaden)
    double limit = DataStore::remote() ? 300.0 : 40.0;
    DWORD t0 = GetTickCount(), lastTick = t0;
    for (;;) {
        update(pos);
        if (streamingIdle()) break;
        Sleep(2);
        DWORD now = GetTickCount();
        if (preloadTick && now - lastTick > 200) { preloadTick(); lastTick = now; }
        if ((now - t0) / 1000.0 > limit) break;
    }
    update(pos);
}

// ---------- tekenen ----------
void World::beginDraw(const Mat4& viewProj, Vec3 cam) {
    frustum_.fromMatrix(viewProj);
    cam_ = cam;
    glUseProgram(prog_);
    // camera-relatief tekenen als main een relatieve kijkmatrix heeft gegeven (anders: absoluut, oorsprong 0)
    origin_ = hasRel_ ? cam : Vec3(0, 0, 0);
    glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, hasRel_ ? relVP_.m : viewProj.m);
    glUniform3f(glGetUniformLocation(prog_, "uOrigin"), origin_.x, origin_.y, origin_.z);
    setModel(Mat4::identity());
    Vec3 sun = normalize(sunDir);
    glUniform3f(uSun_, sun.x, sun.y, sun.z);
    glUniform3f(glGetUniformLocation(prog_, "uSunCol"), sunColor.x, sunColor.y, sunColor.z);
    glUniform1f(glGetUniformLocation(prog_, "uAmb"), ambient);
    glUniform3f(uCam_, cam.x, cam.y, cam.z);
    glUniform3f(uFog_, skyColor.x, skyColor.y, skyColor.z);
    glUniform3f(uSky_, skyColor.x, skyColor.y, skyColor.z);
    glUniform1f(uFogDist_, fogDistance);
    glUniform2f(uGridOrigin_, (float)(ORIGIN_X - gx0_), (float)(ORIGIN_Y - gy0_));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, photoTex_);
    glUniform1i(uPhoto_, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, groundTex_);
    glUniform1i(uGround_, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, signTex_);
    glUniform1i(uSigns_, 2);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, boardTex_ ? boardTex_ : signTex_);
    glUniform1i(uBoards_, 5);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D_ARRAY, coarseTex_);
    glUniform1i(uCoarse_, 3);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, lookupTex_);
    glUniform1i(uLookup_, 4);
    glActiveTexture(GL_TEXTURE0);
}

void World::beginTransparent() {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    setMode(MODE_OBJECT);
    setModel(Mat4::identity());
}

void World::endTransparent() {
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void World::setMode(DrawMode m) { glUniform1i(uMode_, (int)m); }
void World::setModel(const Mat4& m) {
    Mat4 r = m;
    r.m[12] -= origin_.x; r.m[13] -= origin_.y; r.m[14] -= origin_.z;  // relatief t.o.v. de camera
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, r.m);
}

void World::drawStatic() {
    drawnChunks = 0;
    setModel(Mat4::identity());
    setMode(MODE_GROUND);
    for (const Tile& t : tiles_) {
        if (t.state != 2 || !frustum_.boxVisible(t.bmin, t.bmax)) continue;
        drawMesh(t.terrain);
        drawnChunks++;
        for (const Mesh& m : t.ground)
            if (frustum_.boxVisible(m.bmin, m.bmax)) drawMesh(m);
    }
    setMode(MODE_OBJECT);
    for (const Tile& t : tiles_) {
        if (t.state != 2 || !t.detail || !frustum_.boxVisible(t.bmin, t.bmax)) continue;
        float d = tileDistance(t, cam_);
        if (d > drawDistance) continue;
        for (const Mesh& m : t.buildings)
            if (boxDistance(cam_, m.bmin, m.bmax) < drawDistance && frustum_.boxVisible(m.bmin, m.bmax)) { drawMesh(m); drawnChunks++; }
        const float detailDist = std::min(1300.0f, drawDistance * detailFactor);
        if (d < detailDist)
            for (const Mesh& m : t.details)
                if (boxDistance(cam_, m.bmin, m.bmax) < detailDist && frustum_.boxVisible(m.bmin, m.bmax)) { drawMesh(m); drawnChunks++; }
    }
    glBindVertexArray(0);
}
