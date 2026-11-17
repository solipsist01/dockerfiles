// Stadler FLIRT (NS Sprinter, 4-delig), eigen model op basis van openbare gegevens (Stadler-datablad) en vrije
// foto's (Wikimedia Commons):
//   lengte 80,7 m (eindrijtuig 20,85 m, middenrijtuig 18,6 m), breedte 2,82 m, hoogte 4,12 m
//   lagevloer 0,78 m, hogevloer boven de motordraaistellen 1,18 m, deuren 1,30 m (2 per rijtuig per zijde)
//   Bo'2'2'2'Bo' met Jacobs-draaistellen tussen de rijtuigen (open doorloop), motordraaistellen onder de cabines
//   huisstijl NS Sprinter: wit, donkerblauwe band om de ramen die bij de cabine omhoog loopt tot een blauwe
//   kap, gele deuren, dunne gele lijn onderlangs, donkergrijze onderkant/front met lichtclusters.
//   Binnen: lichtgrijze wanden, wit plafond met lichtlijnen, donkere vloer, 2e klas blauw (2+2), 1e klas op de
//   hogevloer achter de cabines (2+1), gele stangen in de deurportalen, bagagerekken.
#include <algorithm>
#include "../engine/meshbuilder.h"
#include "bus.h"
#include "cabin.h"
#include "rollingstock.h"

using namespace WorldFormat;

namespace {
const Vec3 WHITE(0.92f, 0.93f, 0.94f);
const Vec3 BLUE(0.03f, 0.17f, 0.50f);
const Vec3 YELLOW(1.0f, 0.77f, 0.08f);
const Vec3 DARK(0.20f, 0.21f, 0.23f);
const Vec3 SKIRT(0.16f, 0.16f, 0.17f);
const Vec3 ROOFC(0.60f, 0.61f, 0.62f);
const Vec3 BLACK(0.04f, 0.04f, 0.045f);
const Vec3 RUBBER(0.05f, 0.05f, 0.055f);
const Vec3 GLASSC(0.16f, 0.20f, 0.22f);
const Vec3 WALLC(0.86f, 0.86f, 0.84f);
const Vec3 WALL2(0.78f, 0.79f, 0.79f);
const Vec3 CEILC(0.93f, 0.93f, 0.92f);
const Vec3 FLOORC(0.24f, 0.26f, 0.29f);
const Vec3 SEAT2(0.10f, 0.20f, 0.48f);
const Vec3 SEAT1(0.62f, 0.12f, 0.14f);
const Vec3 SHELL(0.30f, 0.31f, 0.33f);
const Vec3 ALU(0.72f, 0.73f, 0.75f);
const Vec3 STEEL(0.42f, 0.43f, 0.45f);
const Vec3 BOGIEC(0.13f, 0.13f, 0.14f);
const Vec3 LIGHTC(1.3f, 1.27f, 1.18f);

const float HW = 1.41f;
const float H = 4.12f;
const float FLOOR_Y = 0.80f;      // lagevloer (gelijk met het perron)
const float HIGH_Y = 1.18f;       // hogevloer boven de motordraaistellen
const float CEIL = 3.02f;         // plafond
const float BODY_BOTTOM = 0.42f;
const float DOOR_W = 1.30f;
const float DOOR_TOP = 2.78f;
const float NOSE = 2.4f;
const float CAB = 3.3f;           // neus + cabine (tot de cabinewand)
const float HF_LEN = 3.1f;        // hogevloer (1e klas) achter de cabine
const float WALL = 0.08f;
const float BAY = 1.70f;          // zitcoupé (twee rijen tegenover elkaar)
const float GAP = 0.60f;

const float PY[] = {0.42f, 0.50f, 0.65f, 0.90f, 1.10f, 2.60f, 2.90f, 3.20f, 3.50f, 3.75f, 3.92f, 4.03f, 4.10f, 4.12f};
const float PX[] = {1.30f, 1.36f, 1.40f, 1.41f, 1.41f, 1.41f, 1.395f, 1.36f, 1.28f, 1.12f, 0.92f, 0.65f, 0.35f, 0.0f};
const int NPY = sizeof(PY) / sizeof(PY[0]);
float outerX(float y) {
    if (y <= PY[0]) return PX[0];
    for (int i = 0; i + 1 < NPY; i++)
        if (y <= PY[i + 1]) return PX[i] + (PX[i + 1] - PX[i]) * (y - PY[i]) / (PY[i + 1] - PY[i]);
    return 0.0f;
}
float outerSlope(float y) { return (outerX(y + 0.01f) - outerX(y - 0.01f)) / 0.02f; }

struct Hole { float z0, z1, y0, y1, r; bool window; };
struct Surf {
    float inset = 0;
    Vec3 P(float s, float z, float y) const { return Vec3(s * (outerX(y) - inset), y, z); }
    Vec3 N(float s, float y) const { return normalize(Vec3(s * 1.0f, -outerSlope(y), 0)); }
};
std::vector<std::pair<float, float>> roundRect(const Hole& h, int segs = 6) {
    std::vector<std::pair<float, float>> out;
    float r = std::min(h.r, std::min(h.z1 - h.z0, h.y1 - h.y0) * 0.49f);
    const float cz[4] = {h.z1 - r, h.z1 - r, h.z0 + r, h.z0 + r};
    const float cy[4] = {h.y0 + r, h.y1 - r, h.y1 - r, h.y0 + r};
    const float a0[4] = {-PI / 2, 0, PI / 2, PI};
    for (int c = 0; c < 4; c++)
        for (int k = 0; k <= segs; k++) {
            float a = a0[c] + (PI / 2) * k / segs;
            out.push_back({cz[c] + r * std::cos(a), cy[c] + r * std::sin(a)});
        }
    return out;
}

// Livrei NS Sprinter
struct Livery {
    float sweep0 = 1e9f, sweep1 = 1e9f;  // eindrijtuig: band loopt op tussen sweep0 en sweep1 (canoniek z)
    std::vector<float> doors;
    float bandTop(float z) const {
        if (z <= sweep0) return 2.62f;
        if (z >= sweep1) return 4.2f;
        return 2.62f + (z - sweep0) / (sweep1 - sweep0) * (4.2f - 2.62f);
    }
    Vec3 at(float z, float y, float& mat) const {
        mat = MAT_PAINT;
        if (y < 0.55f) return SKIRT;
        if (y > 0.62f && y < 0.67f) return YELLOW;
        for (float d : doors)
            if (std::fabs(z - d) < DOOR_W / 2 + 0.06f && y > FLOOR_Y - 0.05f && y < DOOR_TOP + 0.06f) return DARK;  // deurkozijn
        if (y > 1.02f && y < bandTop(z)) return BLUE;
        if (y > 3.78f) return ROOFC;
        return WHITE;
    }
};

struct Builder {
    MeshBuilder& op;
    MeshBuilder& gl;
    void shell(float z0, float z1, float yBot, float yTop, const std::vector<Hole>& holes, const Livery* liv, Vec3 in, float inset,
               const std::vector<float>& extraZ = {}) {
        Surf S{inset};
        std::vector<float> zs{z0, z1}, ys{yBot, yTop};
        for (float z = z0; z < z1; z += 0.30f) zs.push_back(z);
        for (float z : extraZ) if (z > z0 && z < z1) zs.push_back(z);
        for (int i = 0; i < NPY; i++) if (PY[i] > yBot && PY[i] < yTop) ys.push_back(PY[i]);
        for (float y = yBot; y < yTop; y += 0.25f) ys.push_back(y);
        for (const Hole& h : holes) {
            if (h.z0 > z0 && h.z0 < z1) zs.push_back(h.z0);
            if (h.z1 > z0 && h.z1 < z1) zs.push_back(h.z1);
            if (h.y0 > yBot && h.y0 < yTop) ys.push_back(h.y0);
            if (h.y1 > yBot && h.y1 < yTop) ys.push_back(h.y1);
        }
        if (liv) {
            for (float y : {0.55f, 0.62f, 0.67f, 1.02f, 2.62f, 3.78f, FLOOR_Y - 0.05f, DOOR_TOP + 0.06f})
                if (y > yBot && y < yTop) ys.push_back(y);
            for (float d : liv->doors) { zs.push_back(d - DOOR_W / 2 - 0.06f); zs.push_back(d + DOOR_W / 2 + 0.06f); }
            for (float z : {liv->sweep0, liv->sweep1}) if (z > z0 && z < z1) zs.push_back(z);
        }
        std::sort(zs.begin(), zs.end());
        zs.erase(std::unique(zs.begin(), zs.end(), [](float a, float b) { return std::fabs(a - b) < 0.004f; }), zs.end());
        std::sort(ys.begin(), ys.end());
        ys.erase(std::unique(ys.begin(), ys.end(), [](float a, float b) { return std::fabs(a - b) < 0.004f; }), ys.end());
        for (size_t i = 0; i + 1 < zs.size(); i++) {
            float za = zs[i], zb = zs[i + 1];
            if (za < z0 - 1e-3f || zb > z1 + 1e-3f) continue;
            for (size_t j = 0; j + 1 < ys.size(); j++) {
                float ya = ys[j], yb = ys[j + 1];
                float zc = (za + zb) / 2, yc = (ya + yb) / 2;
                bool cut = false;
                for (const Hole& h : holes)
                    if (zc > h.z0 && zc < h.z1 && yc > h.y0 && yc < h.y1) cut = true;
                if (cut) continue;
                float mat = MAT_PAINT;
                Vec3 c = liv ? liv->at(zc, yc, mat) : in;
                if (liv) {
                    // schuine bandrand exact knippen
                    float fa = liv->bandTop(za), fb = liv->bandTop(zb);
                    if (std::fabs(fa - fb) > 1e-4f && std::max(fa, fb) > ya && std::min(fa, fb) < yb && yc > 1.02f) {
                        std::vector<std::pair<float, float>> rect{{za, ya}, {zb, ya}, {zb, yb}, {za, yb}};
                        auto f = [&](float z) { return fa + (fb - fa) * (z - za) / (zb - za); };
                        for (int part = 0; part < 2; part++) {
                            std::vector<std::pair<float, float>> poly;
                            for (size_t k = 0; k < 4; k++) {
                                auto p = rect[k], q = rect[(k + 1) % 4];
                                float gp = p.second - f(p.first), gq = q.second - f(q.first);
                                bool ip = part == 0 ? gp <= 0 : gp >= 0, iq = part == 0 ? gq <= 0 : gq >= 0;
                                if (ip) poly.push_back(p);
                                if (ip != iq) {
                                    float t = gp / (gp - gq);
                                    poly.push_back({p.first + (q.first - p.first) * t, p.second + (q.second - p.second) * t});
                                }
                            }
                            if (poly.size() < 3) continue;
                            float pz = 0, py = 0;
                            for (auto& q : poly) { pz += q.first; py += q.second; }
                            pz /= poly.size(); py /= poly.size();
                            float m2;
                            Vec3 pc = liv->at(pz, py, m2);
                            for (int s = -1; s <= 1; s += 2) {
                                std::vector<uint32_t> ids;
                                for (auto& q : poly) ids.push_back(op.vert(S.P((float)s, q.first, q.second), S.N((float)s, q.second), pc, m2));
                                for (size_t k = 1; k + 1 < ids.size(); k++)
                                    if (s > 0) op.i.insert(op.i.end(), {ids[0], ids[k], ids[k + 1]});
                                    else op.i.insert(op.i.end(), {ids[0], ids[k + 1], ids[k]});
                            }
                        }
                        continue;
                    }
                }
                for (int s = -1; s <= 1; s += 2) {
                    float fs = (float)s;
                    Vec3 a = S.P(fs, za, ya), b = S.P(fs, zb, ya), cc = S.P(fs, zb, yb), d = S.P(fs, za, yb);
                    Vec3 na = S.N(fs, ya), nb = S.N(fs, yb);
                    if (!liv) { na = na * -1.0f; nb = nb * -1.0f; }
                    op.quad(a, b, cc, d, na, na, nb, nb, c, mat);
                }
            }
        }
        for (const Hole& h : holes) {
            if (h.z0 < z0 - 1e-3f || h.z1 > z1 + 1e-3f) continue;
            auto rr = roundRect(h);
            const int seg = (int)rr.size() / 4;
            const float cz[4] = {h.z1, h.z1, h.z0, h.z0}, cy[4] = {h.y0, h.y1, h.y1, h.y0};
            for (int s = -1; s <= 1; s += 2) {
                float fs = (float)s;
                for (int c = 0; c < 4; c++) {
                    float mat = MAT_PAINT;
                    Vec3 col = liv ? liv->at(cz[c], cy[c], mat) : in;
                    Vec3 C = S.P(fs, cz[c], cy[c]);
                    for (int k = 0; k < seg - 1; k++) {
                        auto p0 = rr[c * seg + k], p1 = rr[c * seg + k + 1];
                        Vec3 n = S.N(fs, cy[c]) * (liv ? 1.0f : -1.0f);
                        uint32_t i0 = op.vert(C, n, col, mat), i1 = op.vert(S.P(fs, p0.first, p0.second), n, col, mat), i2 = op.vert(S.P(fs, p1.first, p1.second), n, col, mat);
                        op.i.insert(op.i.end(), {i0, i1, i2});
                    }
                }
                if (!h.window || !liv) continue;
                for (size_t k = 0; k < rr.size(); k++) {
                    auto p0 = rr[k], p1 = rr[(k + 1) % rr.size()];
                    Vec3 A = S.P(fs, p0.first, p0.second), Bq = S.P(fs, p1.first, p1.second);
                    Vec3 nA = S.N(fs, p0.second), nB = S.N(fs, p1.second);
                    Vec3 A2 = A - nA * WALL, B2 = Bq - nB * WALL;
                    op.quad(A, Bq, B2, A2, normalize(cross(Bq - A, nA)), RUBBER, MAT_RUBBER);
                }
                // vlak verlijmd glas, iets naar binnen
                Surf G{0.015f};
                float zc = (h.z0 + h.z1) / 2, yc = (h.y0 + h.y1) / 2;
                Vec3 n = S.N(fs, yc);
                uint32_t c0 = gl.vert(G.P(fs, zc, yc), n, GLASSC, EX_GLASS);
                std::vector<uint32_t> ring;
                for (auto& p : rr) ring.push_back(gl.vert(G.P(fs, p.first, p.second), n, GLASSC, EX_GLASS));
                for (size_t k = 0; k < ring.size(); k++) gl.i.insert(gl.i.end(), {c0, ring[k], ring[(k + 1) % ring.size()]});
            }
        }
    }
    // kopwand met (brede) doorloopopening, dubbelzijdig
    void endFrame(float z, float y0, float y1, Vec3 c, float shrink, float dx, float dy0, float dy1) {
        std::vector<float> ys;
        for (float y = y0; y < y1; y += 0.15f) ys.push_back(y);
        ys.push_back(y1);
        if (dy0 > y0 && dy0 < y1) ys.push_back(dy0);
        if (dy1 > y0 && dy1 < y1) ys.push_back(dy1);
        std::sort(ys.begin(), ys.end());
        auto q2 = [&](Vec3 a, Vec3 b, Vec3 cc, Vec3 d) {
            op.quad(a, b, cc, d, Vec3(0, 0, 1), c, MAT_PAINT);
        };
        for (size_t k = 0; k + 1 < ys.size(); k++) {
            float ya = ys[k], yb = ys[k + 1];
            if (yb - ya < 1e-3f) continue;
            float xa = std::max(0.0f, outerX(ya) - shrink), xb = std::max(0.0f, outerX(yb) - shrink);
            if ((ya + yb) / 2 > dy0 && (ya + yb) / 2 < dy1) {
                q2(Vec3(-xa, ya, z), Vec3(-dx, ya, z), Vec3(-dx, yb, z), Vec3(-xb, yb, z));
                q2(Vec3(dx, ya, z), Vec3(xa, ya, z), Vec3(xb, yb, z), Vec3(dx, yb, z));
            } else {
                q2(Vec3(-xa, ya, z), Vec3(xa, ya, z), Vec3(xb, yb, z), Vec3(-xb, yb, z));
            }
        }
    }
    void underside(float z0, float z1, float y) {
        float x = outerX(y);
        op.quad(Vec3(-x, y, z0), Vec3(x, y, z0), Vec3(x, y, z1), Vec3(-x, y, z1), Vec3(0, -1, 0), SKIRT * 0.7f, MAT_PAINT);
    }
};

// stoel (2e klas blauw, 1e klas rood); dir = kijkrichting langs z
void seat(MeshBuilder& op, float x, float floor, float z, float dir, bool first, bool aisleRight) {
    Vec3 fab = first ? SEAT1 : SEAT2;
    float w = first ? 0.50f : 0.44f;
    op.box({x, floor + 0.44f, z + dir * 0.02f}, {w, 0.11f, 0.47f}, fab, MAT_RUBBER);
    op.box({x, floor + 0.36f, z}, {w - 0.04f, 0.06f, 0.42f}, SHELL, MAT_PAINT);
    for (int k = 0; k < 3; k++) {
        float y = floor + 0.62f + k * 0.21f, back = -dir * (0.22f + k * 0.03f);
        op.box({x, y, z + back}, {w, 0.21f, 0.09f}, k == 2 ? fab * 1.18f : fab, MAT_RUBBER);
        op.box({x, y, z + back - dir * 0.065f}, {w + 0.02f, 0.22f, 0.04f}, SHELL, MAT_PAINT);
    }
    op.box({x, floor + 1.26f, z - dir * 0.32f}, {w - 0.06f, 0.14f, 0.10f}, fab * 1.25f, MAT_RUBBER);  // hoofdsteun
    float gx = x + (aisleRight ? 1.0f : -1.0f) * (w / 2 - 0.04f);
    op.box({gx, floor + 1.36f, z - dir * 0.34f}, {0.03f, 0.05f, 0.10f}, ALU, MAT_METAL);              // handgreep
    if (first)
        for (int s = -1; s <= 1; s += 2) op.box({x + s * (w / 2 + 0.03f), floor + 0.66f, z}, {0.05f, 0.05f, 0.36f}, fab, MAT_RUBBER);
    op.box({x, floor + 0.17f, z}, {0.08f, 0.34f, 0.28f}, BOGIEC, MAT_METAL);
}

void bogie(MeshBuilder& op, bool motor) {
    const float r = motor ? 0.46f : 0.38f, wb = motor ? 2.5f : 2.7f;
    for (int a = -1; a <= 1; a += 2) {
        for (int s = -1; s <= 1; s += 2)
            op.cylX({s * 0.7175f, r, a * wb / 2}, r, 0.13f, 20, Vec3(0.20f, 0.19f, 0.18f), MAT_METAL, STEEL * 0.8f, r * 0.55f, MAT_METAL);
        op.tube({-0.70f, r, a * wb / 2}, {0.70f, r, a * wb / 2}, 0.07f, 8, STEEL * 0.6f, MAT_METAL);
        if (!motor)
            for (float x : {-0.25f, 0.25f}) op.cylX({x, r, a * wb / 2}, 0.28f, 0.05f, 16, STEEL, MAT_METAL, STEEL * 0.7f, 0.10f, MAT_METAL);
        for (int s = -1; s <= 1; s += 2) {
            op.box({s * 0.98f, r, a * wb / 2}, {0.20f, 0.22f, 0.28f}, BOGIEC, MAT_PAINT);
            op.tube({s * 0.98f, r + 0.12f, a * wb / 2}, {s * 0.98f, r + 0.32f, a * wb / 2}, 0.09f, 10, Vec3(0.25f, 0.25f, 0.27f), MAT_METAL);
        }
    }
    for (int s = -1; s <= 1; s += 2) {
        op.box({s * 0.98f, r + 0.38f, 0}, {0.18f, 0.16f, wb + 0.7f}, BOGIEC, MAT_PAINT);
        op.tube({s * 0.98f, r + 0.46f, 0}, {s * 0.98f, r + 0.70f, 0}, 0.26f, 14, Vec3(0.06f, 0.06f, 0.06f), MAT_RUBBER);  // luchtvering
    }
    op.box({0, r + 0.25f, 0}, {1.9f, 0.20f, 0.45f}, BOGIEC, MAT_PAINT);
    if (motor)
        for (int a = -1; a <= 1; a += 2) op.tube({-0.42f, r + 0.02f, a * 0.55f}, {0.42f, r + 0.02f, a * 0.55f}, 0.25f, 14, Vec3(0.18f, 0.19f, 0.20f), MAT_METAL);
}

// ---------- neus ----------
float lerpT(const float* xs, const float* ys, int n, float x) {
    if (x <= xs[0]) return ys[0];
    for (int i = 0; i + 1 < n; i++)
        if (x <= xs[i + 1]) return ys[i] + (ys[i + 1] - ys[i]) * (x - xs[i]) / (xs[i + 1] - xs[i]);
    return ys[n - 1];
}
// lengte van de neus voorbij het einde van de bak, als functie van de hoogte (zijaanzicht)
float noseZ(float y) {
    const float ys[] = {0.42f, 1.00f, 1.60f, 1.95f, 2.60f, 3.30f, 3.80f, 4.12f};
    const float zs[] = {2.25f, 2.36f, 2.30f, 2.15f, 1.65f, 1.05f, 0.55f, 0.0f};
    return lerpT(ys, zs, 8, y);
}
// halve breedte van het front (bovenaanzicht)
float noseHalfW(float y) {
    const float ys[] = {0.42f, 1.2f, 2.4f, 3.3f, 3.9f, 4.12f};
    const float ws[] = {0.95f, 1.10f, 1.10f, 0.98f, 0.72f, 0.35f};
    return lerpT(ys, ws, 6, y);
}
void buildNose(MeshBuilder& mb, MeshBuilder& gl, float z0) {
    std::vector<float> NY;
    for (float y = 0.42f; y < 4.12f; y += 0.07f) NY.push_back(y);
    for (float y : {0.55f, 0.62f, 0.67f, 1.02f, 1.38f, 1.95f, 3.35f}) NY.push_back(y);
    NY.push_back(4.12f);
    std::sort(NY.begin(), NY.end());
    NY.erase(std::unique(NY.begin(), NY.end(), [](float a, float b) { return std::fabs(a - b) < 0.01f; }), NY.end());
    const int SIDE = 12, ARC = 14, FRONT = 6;
    int rows = (int)NY.size();
    std::vector<std::vector<Vec3>> P(rows);
    std::vector<std::vector<int>> reg(rows);
    for (int r = 0; r < rows; r++) {
        float y = NY[r];
        float hb = std::max(0.02f, outerX(y)), hf = std::min(noseHalfW(y), hb), zf = noseZ(y);
        float rad = std::max(0.01f, std::min(0.70f, std::min(hf * 0.9f, zf * 0.9f + 0.01f)));
        std::vector<Vec3> half;
        std::vector<int> rg;
        for (int k = 0; k <= SIDE; k++) {
            float f = (float)k / SIDE;
            half.push_back(Vec3(hb + (hf - hb) * f, y, z0 + std::max(0.0f, zf - rad) * f));
            rg.push_back(0);
        }
        for (int k = 1; k <= ARC; k++) {
            float a = (PI / 2) * k / ARC;
            half.push_back(Vec3(hf - rad + rad * std::cos(a), y, z0 + std::max(0.0f, zf - rad) + rad * std::sin(a)));
            rg.push_back(1);
        }
        for (int k = 1; k <= FRONT; k++) {
            float f = (float)k / FRONT;
            half.push_back(Vec3((hf - rad) * (1 - f), y, z0 + std::max(zf, rad)));
            rg.push_back(2);
        }
        for (size_t k = 0; k < half.size(); k++) { P[r].push_back(half[k]); reg[r].push_back(rg[k]); }
        for (int k = (int)half.size() - 2; k >= 0; k--) { P[r].push_back(Vec3(-half[k].x, half[k].y, half[k].z)); reg[r].push_back(rg[k]); }
    }
    int cols = (int)P[0].size();
    auto colorAt = [&](float y, int rg, float ax, float& mat) {
        mat = MAT_PAINT;
        if (rg == 0) {
            // zijkant van de cabine: vervolg van de livrei (band loopt hier tot de kap)
            if (y < 0.55f) return SKIRT;
            if (y > 0.62f && y < 0.67f) return YELLOW;
            if (y > 1.02f) return BLUE;
            return WHITE;
        }
        if (y < 1.38f) return DARK;                                        // donkere onderkant met lichtclusters
        if (y < 1.95f) return WHITE;
        if (y < 3.35f && ax < noseHalfW(y) - 0.02f) { mat = MAT_GLASS; return Vec3(0.03f, 0.035f, 0.04f); }  // voorruit
        return BLUE;                                                       // blauwe kap
    };
    auto vn = [&](int rr, int cc) {
        Vec3 du = P[rr][std::min(cc + 1, cols - 1)] - P[rr][std::max(cc - 1, 0)];
        Vec3 dv = P[std::min(rr + 1, rows - 1)][cc] - P[std::max(rr - 1, 0)][cc];
        Vec3 m = normalize(cross(dv, du));
        Vec3 o = P[rr][cc] - Vec3(0, P[rr][cc].y, z0 - 1.0f);
        return dot(m, o) < 0 ? m * -1.0f : m;
    };
    for (int r = 0; r + 1 < rows; r++)
        for (int c = 0; c + 1 < cols; c++) {
            float mat;
            int rg = std::max(reg[r][c], reg[r][c + 1]);
            colorAt((NY[r] + NY[r + 1]) / 2, rg, std::fabs((P[r][c].x + P[r][c + 1].x) / 2), mat);
            float ym = (NY[r] + NY[r + 1]) / 2;
            auto vc = [&](int rr, int cc) { float m2; (void)rr; return colorAt(ym, rg, std::fabs((P[r][cc].x + P[r + 1][cc].x) / 2), m2); };
            if (mat == MAT_GLASS) {
                // voorruit: doorzichtig (glas-pass), zodat je vanuit de cabine naar buiten kijkt
                Vec3 gc(0.16f, 0.20f, 0.22f);
                uint32_t ga = gl.vert(P[r][c], vn(r, c), gc, EX_GLASS), gb = gl.vert(P[r][c + 1], vn(r, c + 1), gc, EX_GLASS);
                uint32_t ge = gl.vert(P[r + 1][c + 1], vn(r + 1, c + 1), gc, EX_GLASS), gd = gl.vert(P[r + 1][c], vn(r + 1, c), gc, EX_GLASS);
                gl.i.insert(gl.i.end(), {ga, gb, ge, ga, ge, gd});
                continue;
            }
            uint32_t ia = mb.vert(P[r][c], vn(r, c), vc(r, c), mat), ib = mb.vert(P[r][c + 1], vn(r, c + 1), vc(r, c + 1), mat);
            uint32_t ie = mb.vert(P[r + 1][c + 1], vn(r + 1, c + 1), vc(r + 1, c + 1), mat), id = mb.vert(P[r + 1][c], vn(r + 1, c), vc(r + 1, c), mat);
            mb.i.insert(mb.i.end(), {ia, ib, ie, ia, ie, id});
        }
    for (int c = 0; c + 1 < cols; c++) {
        uint32_t i0 = mb.vert(P[0][c], Vec3(0, -1, 0), SKIRT, MAT_PAINT), i1 = mb.vert(P[0][c + 1], Vec3(0, -1, 0), SKIRT, MAT_PAINT);
        uint32_t i2 = mb.vert(Vec3(0, 0.42f, z0), Vec3(0, -1, 0), SKIRT, MAT_PAINT);
        mb.i.insert(mb.i.end(), {i0, i1, i2});
    }
    float zf = z0 + noseZ(0.9f);
    mb.box({0, 0.82f, zf - 0.10f}, {0.80f, 0.55f, 0.30f}, BLACK, MAT_PAINT);        // koppelingsnis
    mb.box({0, 0.80f, zf + 0.08f}, {0.34f, 0.24f, 0.30f}, STEEL, MAT_METAL);         // Scharfenberg-koppeling
    mb.box({0, 0.50f, z0 + noseZ(0.5f) - 0.03f}, {1.9f, 0.16f, 0.10f}, SKIRT * 0.8f, MAT_PAINT);  // baanschuiver
    for (int s = -1; s <= 1; s += 2) {
        float y = 1.18f, z = z0 + noseZ(y);
        mb.box({s * 0.82f, y, z - 0.03f}, {0.34f, 0.14f, 0.08f}, Vec3(0.15f, 0.15f, 0.16f), MAT_GLASS);  // lichtcluster
        mb.box({s * 0.45f, 2.22f, z0 + noseZ(2.22f) + 0.02f}, {0.75f, 0.025f, 0.025f}, BLACK, MAT_RUBBER);  // ruitenwissers
    }
}

class FlirtStock : public RollingStock {
public:
    FlirtStock();
    float floorAt(int car, float lz) const override;
    bool walkable(int car, float lx, float lz) const override;
    void build(SignAtlas& atlas, const std::vector<std::string>& names) override;
    void drawOpaque(World& world, const StockDraw& s) const override;
    void drawGlass(World& world, const StockDraw& s) const override;

private:
    static constexpr int CARS = 4;
    float doorC_[CARS][2];                      // canonieke deurposities
    std::vector<Mesh> car_, carInt_, carGlass_, leaf_;
    Mesh motor_, jacobs_, panto_, gangway_, headlight_, taillight_, cabDoor_, lever_;
    std::vector<Mesh> display_, screen_;
    void canon(int car, float lx, float lz, float& x, float& z) const {
        int cs = cabSign(car);
        x = cs < 0 ? -lx : lx;
        z = cs < 0 ? -lz : lz;
    }
    float hfStart(int car) const { return len[car] / 2 - CAB - HF_LEN; }  // begin hogevloer (canoniek, eindrijtuig)
    void buildCar(int i, SignAtlas& atlas);
};
}  // namespace

RollingStock* makeFlirt() { return new FlirtStock(); }

FlirtStock::FlirtStock() {
    product = "Sprinter";
    typeName = "FLIRT";
    cars = CARS;
    halfW = HW;
    gap = GAP;
    floorY = FLOOR_Y;
    doorW = DOOR_W;
    height = H;
    nose = NOSE;
    vmax = 44.4f;  // 160 km/u (materieel); op dit traject begrensd door de baanvaksnelheid
    acc = 1.2f;    // aanzetversnelling FLIRT (Stadler: 1,2 m/s2)
    dec = 0.9f;
    const float LE = 20.85f, LM = 18.6f;
    const float L[CARS] = {LE, LM, LM, LE};
    for (int i = 0; i < CARS; i++) len.push_back(L[i]);
    // deuren: eindrijtuig (cabine aan +z) bij de koppeling en voor de hogevloer; middenrijtuig bij beide uiteinden
    doorC_[0][0] = -LE / 2 + 3.0f;  doorC_[0][1] = LE / 2 - CAB - HF_LEN - 1.4f;
    doorC_[1][0] = -LM / 2 + 3.0f;  doorC_[1][1] = LM / 2 - 3.0f;
    doorC_[2][0] = doorC_[1][0];    doorC_[2][1] = doorC_[1][1];
    doorC_[3][0] = doorC_[0][0];    doorC_[3][1] = doorC_[0][1];
    for (int i = 0; i < CARS; i++) {
        if (cabSign(i) < 0) doorZ.push_back({-doorC_[i][1], -doorC_[i][0]});
        else doorZ.push_back({doorC_[i][0], doorC_[i][1]});
    }
    // cabine (machinist in het midden), canoniek eindrijtuig
    cab.wallZ = LE / 2 - CAB;
    cab.doorX = 0.45f;
    cab.doorW = 0.70f;
    cab.floorY = 1.25f;
    cab.passFloor = HIGH_Y;
    cab.deskZ = LE / 2 - 1.35f;
    cab.seatX = 0;
    cab.seatZ = LE / 2 - 1.85f;
    cab.seatTop = cab.floorY + 0.52f;
    cab.leverX = 0.36f;
    cab.leverY = 1.95f;
    cab.leverZ = cab.deskZ + 0.14f;
}

float FlirtStock::floorAt(int car, float lz) const {
    if (!cabSign(car)) return FLOOR_Y;
    float x, z;
    canon(car, 0, lz, x, z);
    float hf = hfStart(car);
    if (z > hf) return HIGH_Y;
    if (z > hf - 0.6f) return FLOOR_Y + (HIGH_Y - FLOOR_Y) * (z - (hf - 0.6f)) / 0.6f;  // helling
    return FLOOR_Y;
}

bool FlirtStock::walkable(int car, float lx, float lz) const {
    float x, z;
    canon(car, lx, lz, x, z);
    float L = len[car];
    int cs = cabSign(car);
    if (cs && z > L / 2 - CAB - 0.05f) return false;                 // cabine
    if (std::fabs(z) > L / 2 - 0.35f) return std::fabs(x) < 0.68f;   // open doorloop
    for (float d : doorC_[car])
        if (std::fabs(z - d) < 1.05f) return std::fabs(x) < HW - 0.30f;  // deurportaal
    if (cs && z > hfStart(car) - 0.6f) return x > -0.66f && x < 0.22f; // 1e klas (2+1)
    return std::fabs(x) < 0.30f;                                     // gangpad
}

void FlirtStock::buildCar(int i, SignAtlas& atlas) {
    const float L = len[i];
    const int cs = cabSign(i);
    const bool cab = cs != 0;
    const float zEnd = cab ? L / 2 - NOSE : L / 2;
    MeshBuilder op, gl;
    Builder B{op, gl};
    Livery liv;
    for (float d : doorC_[i]) liv.doors.push_back(d);
    if (cab) { liv.sweep0 = L / 2 - CAB - HF_LEN - 0.2f; liv.sweep1 = zEnd; }

    // ---- indeling: zitzones tussen deuren en uiteinden ----
    float d0 = doorC_[i][0], d1 = doorC_[i][1];
    std::vector<std::pair<float, float>> zones;  // lagevloer-zitzones (canoniek)
    zones.push_back({-L / 2 + 0.6f, d0 - DOOR_W / 2 - 0.55f});
    zones.push_back({d0 + DOOR_W / 2 + 0.55f, d1 - DOOR_W / 2 - 0.55f});
    if (!cab) zones.push_back({d1 + DOOR_W / 2 + 0.55f, L / 2 - 0.6f});
    // ---- ramen ----
    std::vector<Hole> holes;
    std::vector<std::pair<float, float>> bays;  // (midden, vloer) per coupé
    for (auto& zn : zones) {
        int n = std::max(0, (int)std::floor((zn.second - zn.first) / BAY));
        float z0 = (zn.first + zn.second) / 2 - n * BAY / 2;
        for (int k = 0; k < n; k++) {
            float zc = z0 + (k + 0.5f) * BAY;
            holes.push_back({zc - 0.74f, zc + 0.74f, 1.12f, 2.45f, 0.14f, true});
            bays.push_back({zc, FLOOR_Y});
        }
    }
    if (cab) {
        float hz0 = hfStart(i) + 0.15f, hz1 = L / 2 - CAB - 0.15f;
        float zc = (hz0 + hz1) / 2;
        for (float off : {-0.72f, 0.72f}) {
            holes.push_back({zc + off - 0.64f, zc + off + 0.64f, 1.45f, 2.55f, 0.14f, true});
            bays.push_back({zc + off, HIGH_Y});
        }
        holes.push_back({L / 2 - CAB + 0.15f, zEnd - 0.05f, 1.95f, 2.85f, 0.12f, true});  // cabinezijraam
    }
    // deuropeningen
    for (float d : doorC_[i]) holes.push_back({d - DOOR_W / 2, d + DOOR_W / 2, FLOOR_Y - 0.05f, DOOR_TOP, 0.05f, false});
    // ---- buitenhuid ----
    std::vector<float> extra{d0 - DOOR_W / 2, d0 + DOOR_W / 2, d1 - DOOR_W / 2, d1 + DOOR_W / 2};
    B.shell(-L / 2, zEnd, BODY_BOTTOM, H, holes, &liv, {}, 0, extra);
    B.underside(-L / 2, zEnd, BODY_BOTTOM);
    B.endFrame(-L / 2 + 0.01f, BODY_BOTTOM, H, Vec3(0.25f, 0.25f, 0.26f), 0.0f, 0.74f, FLOOR_Y - 0.05f, 2.95f);
    if (!cab) B.endFrame(L / 2 - 0.01f, BODY_BOTTOM, H, Vec3(0.25f, 0.25f, 0.26f), 0.0f, 0.74f, FLOOR_Y - 0.05f, 2.95f);
    // deuren: treeplank met gele rand, kozijnzijden
    for (float d : doorC_[i])
        for (int s = -1; s <= 1; s += 2) {
            op.box({s * (HW - 0.05f), FLOOR_Y - 0.03f, d}, {0.12f, 0.04f, DOOR_W}, STEEL, MAT_METAL);
            op.box({s * (HW + 0.002f), FLOOR_Y - 0.01f, d}, {0.03f, 0.012f, DOOR_W - 0.08f}, YELLOW, MAT_PAINT);
            for (int e = -1; e <= 1; e += 2)
                op.box({s * (HW - WALL / 2), (FLOOR_Y + DOOR_TOP) / 2, d + e * (DOOR_W / 2 + 0.015f)}, {WALL, DOOR_TOP - FLOOR_Y, 0.03f}, DARK, MAT_PAINT);
            op.box({s * (HW - WALL / 2), DOOR_TOP + 0.015f, d}, {WALL, 0.03f, DOOR_W + 0.06f}, DARK, MAT_PAINT);
        }
    // klassenummers
    {
        SignRect r2 = atlas.add("2", BLUE, Vec3(1, 1, 1), 64, true, 0.3f);
        SignRect r1 = atlas.add("1", BLUE, Vec3(1, 1, 0.3f), 64, true, 0.3f);
        for (int s = -1; s <= 1; s += 2) {
            if (!bays.empty()) SignAtlas::quad(op, r2, Vec3(s * (HW + 0.004f), 2.54f, bays[0].first), Vec3(0, 0, (float)-s), 0.11f, false);
            if (cab) SignAtlas::quad(op, r1, Vec3(s * (HW + 0.004f), 2.62f, hfStart(i) + 0.08f), Vec3(0, 0, (float)-s), 0.11f, false);
        }
    }
    // dak: klimaatinstallatie, apparatuur
    op.box({0, H - 0.01f, 0}, {1.7f, 0.14f, 3.2f}, ROOFC * 0.9f, MAT_PAINT);
    for (int k = 0; k < 6; k++) op.box({0, H + 0.065f, -1.0f + k * 0.4f}, {1.3f, 0.012f, 0.18f}, ROOFC * 0.45f, MAT_PAINT);
    if (cab) op.box({0, H - 0.05f, -L / 2 + 3.5f}, {1.5f, 0.22f, 3.0f}, ROOFC * 0.85f, MAT_PAINT);
    if (i == 1) op.box({0, H + 0.03f, L / 2 - 3.5f}, {1.4f, 0.10f, 2.2f}, ROOFC * 0.8f, MAT_PAINT);
    if (cab) buildNose(op, gl, zEnd);

    // ======================= INTERIEUR =======================
    const size_t intV = op.v.size(), intI = op.i.size();
    float innerEnd = cab ? L / 2 - CAB : L / 2 - 0.05f;
    B.shell(-L / 2 + 0.05f, innerEnd, FLOOR_Y, CEIL, holes, nullptr, WALLC, WALL);
    B.endFrame(-L / 2 + 0.08f, FLOOR_Y, CEIL, WALL2, WALL, 0.74f, FLOOR_Y - 0.05f, 2.95f);
    if (!cab) B.endFrame(L / 2 - 0.08f, FLOOR_Y, CEIL, WALL2, WALL, 0.74f, FLOOR_Y - 0.05f, 2.95f);
    float xi = outerX(1.5f) - WALL;
    float zMid = (-L / 2 + innerEnd) / 2, zLen = innerEnd + L / 2;
    op.box({0, FLOOR_Y - 0.02f, zMid}, {2 * xi, 0.04f, zLen}, FLOORC, MAT_PAINT);
    op.box({0, CEIL + 0.01f, zMid}, {2 * (outerX(CEIL) - WALL), 0.03f, zLen}, CEILC, MAT_PAINT);
    for (int s = -1; s <= 1; s += 2) op.box({s * 0.62f, CEIL - 0.015f, zMid}, {0.12f, 0.02f, zLen - 0.6f}, LIGHTC, MAT_LIGHT);
    // bagagerekken boven de ramen
    for (int s = -1; s <= 1; s += 2)
        for (auto& zn : zones) op.box({s * (xi - 0.18f), 2.62f, (zn.first + zn.second) / 2}, {0.30f, 0.02f, zn.second - zn.first}, Vec3(0.55f, 0.62f, 0.66f), MAT_GLASS);
    if (cab) {
        float hf = hfStart(i);
        // hogevloer met helling en cabinewand
        op.box({0, (FLOOR_Y + HIGH_Y) / 2, (hf + L / 2 - CAB) / 2}, {2 * xi, HIGH_Y - FLOOR_Y, L / 2 - CAB - hf}, FLOORC * 1.1f, MAT_PAINT);
        for (int k = 0; k < 6; k++) {
            float t = (k + 0.5f) / 6, y = FLOOR_Y + (HIGH_Y - FLOOR_Y) * t;
            op.box({0, (FLOOR_Y + y) / 2, hf - 0.6f + t * 0.6f}, {2 * xi, y - FLOOR_Y, 0.1f}, FLOORC * 1.1f, MAT_PAINT);
        }
        op.box({0, HIGH_Y + 0.003f, hf}, {2 * xi, 0.01f, 0.05f}, YELLOW, MAT_PAINT);
        // cabinewand met deur en de cabine zelf
        cabin::Params cp;
        cp.c = this->cab;
        cp.halfW = 1.22f;
        cp.ceilY = 2.95f;
        cp.frontZ = zEnd + noseZ(1.95f) - 0.10f;
        cp.deskTopY = 1.92f;
        cp.glassY = 1.95f;
        cp.sideWin0 = 1.95f;
        cp.sideWin1 = 2.85f;
        cp.wall = WALL2;
        cp.desk = Vec3(0.22f, 0.23f, 0.25f);
        cp.floor = Vec3(0.18f, 0.18f, 0.19f);
        cp.seat = Vec3(0.12f, 0.16f, 0.30f);
        cabin::build(op, gl, cp);
        // glazen scheidingswand 1e klas
        op.box({-1.0f, (FLOOR_Y + 2.6f) / 2, hf - 0.66f}, {0.75f, 2.6f - FLOOR_Y, 0.03f}, Vec3(0.6f, 0.68f, 0.72f), MAT_GLASS);
        op.box({0.85f, (FLOOR_Y + 2.6f) / 2, hf - 0.66f}, {0.95f, 2.6f - FLOOR_Y, 0.03f}, Vec3(0.6f, 0.68f, 0.72f), MAT_GLASS);
    }
    // stoelen per coupé (vis-à-vis), tafeltje bij het raam
    auto addSeat = [&](float x, float floor, float z, float dir, bool first, bool aisleRight) {
        seat(op, x, floor, z, dir, first, aisleRight);
        SeatInfo si;
        si.car = i;
        si.lx = cs < 0 ? -x : x;
        si.lz = cs < 0 ? -z : z;
        si.facing = wrapAngle((dir > 0 ? 0.0f : PI) + (cs < 0 ? PI : 0.0f));
        si.y = floor;
        seats.push_back(si);
    };
    for (auto& b : bays) {
        bool first = b.second > 1.0f;
        for (int r = -1; r <= 1; r += 2) {
            float z = b.first + r * 0.42f, dir = (float)-r;
            if (first) {
                addSeat(-0.95f, b.second, z, dir, true, true);
                addSeat(0.48f, b.second, z, dir, true, false);
                addSeat(0.98f, b.second, z, dir, true, false);
            } else {
                addSeat(-1.00f, b.second, z, dir, false, true);
                addSeat(-0.54f, b.second, z, dir, false, true);
                addSeat(0.54f, b.second, z, dir, false, false);
                addSeat(1.00f, b.second, z, dir, false, false);
            }
        }
        for (int s = -1; s <= 1; s += 2) op.box({s * (xi - 0.12f), b.second + 0.72f, b.first}, {0.22f, 0.03f, 0.40f}, Vec3(0.40f, 0.41f, 0.43f), MAT_PAINT);
    }
    // deurportalen: gele stangen, klapstoeltjes
    for (float d : doorC_[i])
        for (int s = -1; s <= 1; s += 2) {
            for (float off : {-0.95f, 0.95f}) op.tube({s * 0.62f, FLOOR_Y, d + off}, {s * 0.62f, CEIL, d + off}, 0.02f, 6, YELLOW, MAT_METAL);
            op.box({s * (xi - 0.20f), FLOOR_Y + 0.48f, d + 1.35f}, {0.40f, 0.05f, 0.40f}, SEAT2, MAT_RUBBER);
        }
    for (size_t k = intV; k < op.v.size(); k++)
        if (op.v[k].extra != MAT_LIGHT && op.v[k].extra != MAT_GLASS && op.v[k].extra > -0.5f && op.v[k].extra < 4.5f) op.v[k].extra = MAT_INTERIOR;
    // ---- buitenkant en interieur apart ----
    MeshBuilder ext, inn;
    ext.v.assign(op.v.begin(), op.v.begin() + intV);
    ext.i.assign(op.i.begin(), op.i.begin() + intI);
    inn.v.assign(op.v.begin() + intV, op.v.end());
    for (size_t k = intI; k < op.i.size(); k++) inn.i.push_back(op.i[k] - (uint32_t)intV);
    car_[i] = ext.upload();
    carInt_[i] = inn.upload();
    carGlass_[i] = gl.upload();

    // ---- deurbladen: geel met groot raam, groene drukknop ----
    for (int s = 0; s < 2; s++)
        for (int leaf = 0; leaf < 2; leaf++) {
            MeshBuilder dm;
            float side = s ? 1.0f : -1.0f;
            float zc = (leaf ? 1.0f : -1.0f) * DOOR_W / 4;
            for (float d : doorC_[i]) {
                float z = d + zc, x = side * (HW - 0.02f), w = DOOR_W / 2 - 0.015f;
                dm.box({x, (FLOOR_Y + 1.25f) / 2, z}, {0.05f, 1.25f - FLOOR_Y, w}, YELLOW, MAT_PAINT);
                dm.box({x, (2.45f + DOOR_TOP) / 2, z}, {0.05f, DOOR_TOP - 2.45f, w}, YELLOW, MAT_PAINT);
                for (float e : {-1.0f, 1.0f}) dm.box({x, 1.85f, z + e * (w / 2 - 0.05f)}, {0.05f, 1.20f, 0.10f}, YELLOW, MAT_PAINT);
                dm.box({x, 1.85f, z}, {0.03f, 1.20f, w - 0.10f}, Vec3(0.03f, 0.035f, 0.04f), MAT_GLASS);
                dm.box({x + side * 0.03f, 1.80f, z + (leaf ? -1.0f : 1.0f) * (w / 2 - 0.01f)}, {0.012f, 1.95f, 0.02f}, RUBBER, MAT_RUBBER);
                if (leaf) dm.box({x + side * 0.03f, 1.20f, z - w / 2 + 0.12f}, {0.02f, 0.08f, 0.08f}, Vec3(0.1f, 0.85f, 0.25f), MAT_LIGHT);
            }
            leaf_[i * 4 + s * 2 + leaf] = dm.upload();
        }
}

void FlirtStock::build(SignAtlas& atlas, const std::vector<std::string>& names) {
    car_.resize(CARS);
    carInt_.resize(CARS);
    carGlass_.resize(CARS);
    leaf_.resize(CARS * 4);
    seats.clear();
    for (int i = 0; i < CARS; i++) buildCar(i, atlas);
    cabDoor_ = cabin::doorLeaf(cab.doorW - 0.02f, 2.0f, Vec3(0.45f, 0.46f, 0.48f));
    lever_ = cabin::lever();
    { MeshBuilder m; bogie(m, true); motor_ = m.upload(); }
    { MeshBuilder m; bogie(m, false); jacobs_ = m.upload(); }
    {
        MeshBuilder b;
        float y0 = H + 0.08f;
        b.box({0, y0, 0}, {1.3f, 0.10f, 1.5f}, STEEL * 0.7f, MAT_METAL);
        for (int s = -1; s <= 1; s += 2) b.tube({s * 0.5f, y0, 0.55f}, {s * 0.5f, y0 + 0.22f, 0.55f}, 0.07f, 8, Vec3(0.55f, 0.30f, 0.20f), MAT_PAINT);
        Vec3 hinge(0, y0 + 0.25f, 0.55f), knee(0, y0 + 0.95f, -0.85f), head(0, y0 + 1.55f, 0.10f);
        for (int s = -1; s <= 1; s += 2) b.tube(hinge + Vec3(s * 0.25f, 0, 0), knee + Vec3(s * 0.05f, 0, 0), 0.04f, 6, STEEL, MAT_METAL);
        b.tube(knee, head, 0.03f, 6, STEEL, MAT_METAL);
        for (float dz : {-0.12f, 0.12f}) b.box(head + Vec3(0, 0.03f, dz), {1.95f, 0.04f, 0.06f}, Vec3(0.25f, 0.25f, 0.27f), MAT_METAL);
        panto_ = b.upload();
    }
    {
        // brede open doorloop: balg rond de doorgang
        MeshBuilder g;
        for (int k = 0; k < 5; k++) {
            float z = -GAP / 2 + GAP * (k + 0.5f) / 5, w = 2.60f - (k % 2) * 0.05f, h = 2.30f - (k % 2) * 0.05f, t = 0.14f;
            Vec3 col = k % 2 ? RUBBER * 2.2f : RUBBER;
            float yc = FLOOR_Y - 0.05f + h / 2;
            g.box({0, yc + h / 2 - t / 2, z}, {w, t, GAP / 5 + 0.02f}, col, MAT_RUBBER);
            g.box({-w / 2 + t / 2, yc, z}, {t, h, GAP / 5 + 0.02f}, col, MAT_RUBBER);
            g.box({w / 2 - t / 2, yc, z}, {t, h, GAP / 5 + 0.02f}, col, MAT_RUBBER);
        }
        g.box({0, FLOOR_Y - 0.02f, 0}, {1.5f, 0.04f, GAP + 0.4f}, FLOORC, MAT_INTERIOR);
        gangway_ = g.upload();
    }
    {
        MeshBuilder hl, tl;
        float zN = len[0] / 2 - NOSE;
        for (int s = -1; s <= 1; s += 2) {
            float y = 1.18f, z = zN + noseZ(y) + 0.01f;
            hl.box({s * 0.90f, y, z}, {0.16f, 0.10f, 0.03f}, Vec3(1.6f, 1.6f, 1.45f), MAT_LIGHT);
            tl.box({s * 0.72f, y, z}, {0.12f, 0.10f, 0.03f}, Vec3(1.2f, 0.05f, 0.03f), MAT_LIGHT);
        }
        hl.box({0, 3.42f, zN + noseZ(3.42f) + 0.02f}, {0.14f, 0.08f, 0.03f}, Vec3(1.6f, 1.6f, 1.45f), MAT_LIGHT);  // toplicht
        headlight_ = hl.upload();
        taillight_ = tl.upload();
    }
    display_.resize(names.size());
    screen_.resize(names.size());
    float zN = len[0] / 2 - NOSE;
    for (size_t t = 0; t < names.size(); t++) {
        {
            MeshBuilder m;
            SignRect r = atlas.add(names[t], Vec3(0.01f, 0.01f, 0.01f), Vec3(1.0f, 0.62f, 0.12f), 64, true, 0.3f);
            float y0 = 3.08f, y1 = 3.26f;
            float h = y1 - y0, w = std::min(1.4f, h * r.aspect * 1.3f);
            Vec3 a(-w / 2, y0, zN + noseZ(y0) - 0.06f), b(w / 2, y0, zN + noseZ(y0) - 0.06f);
            Vec3 c(w / 2, y1, zN + noseZ(y1) - 0.06f), d(-w / 2, y1, zN + noseZ(y1) - 0.06f);
            Vec3 n = normalize(cross(b - a, d - a));
            auto V = [&](Vec3 p, float u, float v) { return m.vert(p, n, Vec3(u, v, 0), EX_SIGN); };
            uint32_t i0 = V(b, r.u1, r.v1), i1 = V(a, r.u0, r.v1), i2 = V(d, r.u0, r.v0), i3 = V(c, r.u1, r.v0);
            m.i.insert(m.i.end(), {i0, i1, i2, i0, i2, i3});
            display_[t] = m.upload();
        }
        {
            MeshBuilder m;
            SignRect r = atlas.add("Volgende station: " + names[t], Vec3(0.0f, 0.13f, 0.43f), Vec3(1, 1, 1), 48, true, 0.3f);
            SignAtlas::quad(m, r, Vec3(0, CEIL - 0.20f, 0), Vec3(1, 0, 0), 0.16f, true);
            screen_[t] = m.upload();
        }
    }
}

void FlirtStock::drawOpaque(World& world, const StockDraw& sd) const {
    const Vehicle& v = *sd.v;
    world.setMode(MODE_VEHICLE);
    Vec3 cam = world.camPos();
    for (int i = 0; i < CARS; i++) {
        CarPose c = v.carPose(i);
        int cs = cabSign(i);
        Mat4 m = Mat4::pose(c.x, c.y, c.z, cs < 0 ? c.h + PI : c.h);
        world.setModel(m);
        drawMesh(car_[i]);
        if (std::hypot(c.x - cam.x, c.z - cam.z) < 300) drawMesh(carInt_[i]);
        for (int s = 0; s < 2; s++)
            for (int leaf = 0; leaf < 2; leaf++) {
                int meshSide = s ? 1 : -1;
                int worldSide = cs < 0 ? -meshSide : meshSide;
                float open = worldSide == sd.doorSide ? sd.doorOpen : 0;
                float out = std::min(1.0f, open * 4.0f) * 0.07f;
                float slide = clampf((open - 0.12f) / 0.88f, 0, 1);
                float off = (leaf ? 1.0f : -1.0f) * (DOOR_W / 2 - 0.03f) * slide;
                world.setModel(m * Mat4::translation(meshSide * out, 0, off));
                drawMesh(leaf_[i * 4 + s * 2 + leaf]);
            }
        if (cs) {
            world.setModel(m * Mat4::translation(0, 0, len[i] / 2 - NOSE - 1.6f));
            drawMesh(motor_);
            bool leading = (cs > 0) == (sd.lead == 0);
            world.setModel(m);
            drawMesh(leading ? headlight_ : taillight_);
            if (std::hypot(c.x - cam.x, c.z - cam.z) < 120) {
                float open = sd.cabDoor[cs > 0 ? 0 : 1];
                world.setModel(m * Mat4::translation(cab.doorX - cab.doorW / 2 + 0.01f, std::min(cab.floorY, cab.passFloor), cab.wallZ) * Mat4::rotY(-open * 1.45f));
                drawMesh(cabDoor_);
                world.setModel(m * Mat4::translation(cab.leverX, cab.leverY, cab.leverZ) * Mat4::rotX((leading ? sd.lever : -0.5f) * 0.6f));
                drawMesh(lever_);
            }
        }
        if (i == 1) {
            world.setModel(m * Mat4::translation(0, 0, len[i] / 2 - 3.5f));
            drawMesh(panto_);
        }
        if (i + 1 < CARS) {
            CarPose n = v.carPose(i + 1);
            Vec3 a = v.carToWorld(i, 0, 0, -len[i] / 2), b = v.carToWorld(i + 1, 0, 0, len[i + 1] / 2);
            world.setModel(Mat4::pose((a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2, std::atan2(c.x - n.x, c.z - n.z)));
            drawMesh(jacobs_);
            drawMesh(gangway_);
        }
    }
    world.setMode(MODE_OBJECT);
    for (int i = 0; i < CARS; i++) {
        CarPose c = v.carPose(i);
        int cs = cabSign(i);
        if (cs && sd.dest >= 0 && sd.dest < (int)display_.size()) {
            // niet vanuit de cabine zelf (dan zie je de achterkant in spiegelbeeld)
            Vec3 cabC = v.carToWorld(i, 0, 2.3f, cs * (len[i] / 2 - 1.6f)), cam = world.camPos();
            if (length(cabC - cam) > 2.6f) {
                world.setModel(Mat4::pose(c.x, c.y, c.z, cs < 0 ? c.h + PI : c.h));
                drawMesh(display_[sd.dest]);
            }
        }
        if (sd.next >= 0 && sd.next < (int)screen_.size())
            for (int k = 0; k < 2; k++) {
                world.setModel(Mat4::pose(c.x, c.y, c.z, c.h) * Mat4::translation(0, 0, doorZ[i][k]));
                drawMesh(screen_[sd.next]);
            }
    }
    world.setModel(Mat4::identity());
}

void FlirtStock::drawGlass(World& world, const StockDraw& sd) const {
    for (int i = 0; i < CARS; i++) {
        CarPose c = sd.v->carPose(i);
        int cs = cabSign(i);
        world.setModel(Mat4::pose(c.x, c.y, c.z, cs < 0 ? c.h + PI : c.h));
        drawMesh(carGlass_[i]);
    }
    world.setModel(Mat4::identity());
}
