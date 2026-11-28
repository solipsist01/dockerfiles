// VIRMm (gemoderniseerd Verlengd InterRegio Materieel), NS Intercity-dubbeldekker, 6-delig (VIRMm-VI).
// Eigen model op basis van openbare gegevens (Wikipedia "VIRM") en vrije foto's (Wikimedia Commons):
//   rijtuigen (lengte over buffers): mBvk1 27,26 - ABv5 26,46 - mBv7 26,96 - ABv6 27,46 - ABv4 26,46 - mBvk2 27,26 m
//   breedte 3,02 m, hoogte 4,67 m; vloeren: balkon 1,163 m, benedendek 0,356 m, bovendek 2,444 m
//   deuren 1,30 m (draai-schuifdeuren), 2 per zijde per rijtuig; wielen 920 mm; pantograaf op de ABv6
//   huisstijl VIRMm-1: geel, blauwe band om de benedenramen die bij de balkons omhoog loopt, blauw omlijste
//   bovenramen, zwarte cabinekop, grijs dak. Interieur: cremekleurige wanden, bovendek in tentvorm met donker
//   plafond en lichtlijnen, 2e klas blauw (2+2), 1e klas rood (2+1), donkere stoelschalen, ronde tafeltjes.
// Lopen: balkons, trappen naar beneden (linkerhelft) en naar boven (rechterhelft), beneden- en bovendek.
#include <algorithm>
#include "../engine/meshbuilder.h"
#include "bus.h"
#include "cabin.h"
#include "rollingstock.h"
#include "tslayout.h"
#include "tsmodel.h"

using namespace WorldFormat;

namespace {
// ---------- kleuren ----------
const Vec3 YELLOW(1.0f, 0.77f, 0.08f);
const Vec3 BLUE(0.03f, 0.16f, 0.48f);
const Vec3 SKIRT(0.16f, 0.16f, 0.17f);
const Vec3 ROOFC(0.58f, 0.59f, 0.60f);
const Vec3 BLACK(0.04f, 0.04f, 0.045f);
const Vec3 RUBBER(0.05f, 0.05f, 0.055f);
const Vec3 GLASSC(0.16f, 0.20f, 0.22f);
const Vec3 CREAM(0.86f, 0.83f, 0.76f);
const Vec3 CREAM2(0.80f, 0.77f, 0.70f);
const Vec3 FLOORC(0.30f, 0.25f, 0.21f);
const Vec3 DARKPANEL(0.12f, 0.12f, 0.13f);
const Vec3 CEILDARK(0.28f, 0.27f, 0.27f);
const Vec3 SEAT2(0.17f, 0.22f, 0.52f);
const Vec3 SEAT2B(0.26f, 0.32f, 0.62f);
const Vec3 SEAT1(0.72f, 0.10f, 0.12f);
const Vec3 SHELL(0.22f, 0.21f, 0.21f);
const Vec3 ALU(0.72f, 0.73f, 0.75f);
const Vec3 STEEL(0.42f, 0.43f, 0.45f);
const Vec3 BOGIEC(0.13f, 0.13f, 0.14f);
const Vec3 LIGHTC(1.3f, 1.27f, 1.18f);

// ---------- maten ----------
const float HW = 1.51f;           // halve breedte (3,02 m)
const float H = 4.67f;
const float BAL_Y = 1.12f;        // balkon (instap); vloeren zoals in het Train Simulator-model
const float LOW_Y = 0.37f;        // benedendek
const float UP_Y = 2.44f;         // bovendek
const float LOW_CEIL = 2.30f;     // plafond benedendek
const float UP_CEIL = 4.45f;      // plafond bovendek
const float BAL_CEIL = 3.25f;     // plafond balkon
const float DD_BOTTOM = 0.10f;    // onderkant dubbeldeks deel
const float END_BOTTOM = 0.95f;   // onderkant boven de draaistellen
const float DOOR_W = 1.30f;
const float DOOR_TOP = 3.12f;
const float BALCONY = 4.3f;       // lengte balkon (rijtuigeinde tot dubbeldeks deel); deuren zoals in het TS-model, 3,3 m van het einde
const float NOSE = 2.6f;          // neus (front)
const float CAB = 2.9f;           // neus + cabine
const float STAIR = 1.7f;         // lengte trappartij in het dubbeldeks deel
const float WALL = 0.10f;         // wanddikte
const float WIN_PITCH = 1.85f;    // raamsteek (= zitcoupe)
const float WIN_W = 1.40f;

// buitenprofiel: halve breedte als functie van de hoogte
const float PY[] = {0.24f, 0.30f, 0.40f, 0.55f, 0.85f, 1.10f, 2.40f, 2.70f, 3.00f, 3.30f, 3.60f, 3.90f, 4.10f, 4.25f, 4.40f, 4.52f, 4.60f, 4.65f, 4.67f};
const float PX[] = {1.30f, 1.37f, 1.41f, 1.44f, 1.47f, 1.50f, 1.51f, 1.505f, 1.48f, 1.445f, 1.40f, 1.33f, 1.25f, 1.16f, 1.00f, 0.80f, 0.58f, 0.32f, 0.0f};
const int NPY = sizeof(PY) / sizeof(PY[0]);
float outerX(float y) {
    if (y <= PY[0]) return PX[0];
    for (int i = 0; i + 1 < NPY; i++)
        if (y <= PY[i + 1]) return PX[i] + (PX[i + 1] - PX[i]) * (y - PY[i]) / (PY[i + 1] - PY[i]);
    return 0.0f;
}
float outerSlope(float y) { return (outerX(y + 0.01f) - outerX(y - 0.01f)) / 0.02f; }

// ---------- vlakken met openingen (ramen, deuren) op het gebogen profiel ----------
struct Hole {
    float z0, z1, y0, y1, r;
    bool window;   // raam (met rubber, nis en glas) of deuropening (alleen uitsparing)
    bool blueRim;  // blauwe omlijsting (bovenramen)
};

// punt op het profiel: zijde s (+1/-1), lengte z, hoogte y, inset = afstand naar binnen langs de normaal
struct Surf {
    float inset = 0;
    Vec3 P(float s, float z, float y) const {
        float x = outerX(y) - inset;
        Vec3 n = N(s, y);
        (void)n;
        return Vec3(s * x, y, z);
    }
    Vec3 N(float s, float y) const {
        float d = outerSlope(y);
        return normalize(Vec3(s * 1.0f, -d, 0));
    }
};

std::vector<std::pair<float, float>> roundRect(const Hole& h, int segs = 6) {
    std::vector<std::pair<float, float>> out;
    float r = std::min(h.r, std::min((h.z1 - h.z0), (h.y1 - h.y0)) * 0.49f);
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

// Livrei op de buitenhuid: kleur op (z, y). dd0/dd1 = dubbeldeks deel, sweep = blauwe band loopt bij de uiteinden op.
struct Livery {
    float dd0, dd1;
    std::vector<Hole> upperWins;
    std::vector<std::pair<float, float>> doors;  // deur-z (midden) voor blauwe omlijsting
    float bandTop(float z) const {
        float e = std::min(z - dd0, dd1 - z);
        if (e < 1.6f) return 2.08f + (1.6f - std::max(0.0f, e)) / 1.6f * 0.75f;
        return 2.08f;
    }
    Vec3 at(float z, float y, float& mat) const {
        mat = MAT_PAINT;
        if (y < 0.55f) return SKIRT;
        if (y > 4.30f) return ROOFC;
        // deuromlijsting (blauw, 10 cm)
        for (auto& d : doors)
            if (std::fabs(z - d.first) < DOOR_W / 2 + 0.11f && y > BAL_Y - 0.05f && y < DOOR_TOP + 0.11f) return BLUE;
        // blauwe band om de benedenramen; bij de uiteinden loopt de bovenrand schuin op naar het balkon
        if (z > dd0 - 0.6f && z < dd1 + 0.6f && y > 0.82f && y < bandTop(z)) return BLUE;
        // blauwe rand om de bovenramen
        for (const Hole& h : upperWins)
            if (z > h.z0 - 0.07f && z < h.z1 + 0.07f && y > h.y0 - 0.07f && y < h.y1 + 0.07f) return BLUE;
        return YELLOW;
    }
};

struct Builder {
    MeshBuilder& op;
    MeshBuilder& gl;

    // Huid tussen z0 en z1, van yBot tot de nok, met gaten. outer: buitenhuid (livrei) of binnenwand (kleur in).
    void shell(float z0, float z1, float yBot, float yTop, const std::vector<Hole>& holes, const Livery* liv, Vec3 in, float inset,
               const std::vector<float>& extraZ = {}) {
        Surf S{inset};
        std::vector<float> zs{z0, z1};
        for (float z = z0; z < z1; z += 0.30f) zs.push_back(z);
        for (float z : extraZ) if (z > z0 && z < z1) zs.push_back(z);
        std::vector<float> ys{yBot, yTop};
        for (int i = 0; i < NPY; i++) if (PY[i] > yBot && PY[i] < yTop) ys.push_back(PY[i]);
        for (float y = yBot; y < yTop; y += 0.25f) ys.push_back(y);
        for (const Hole& h : holes) {
            if (h.z0 > z0 && h.z0 < z1) zs.push_back(h.z0);
            if (h.z1 > z0 && h.z1 < z1) zs.push_back(h.z1);
            if (h.y0 > yBot && h.y0 < yTop) ys.push_back(h.y0);
            if (h.y1 > yBot && h.y1 < yTop) ys.push_back(h.y1);
            if (liv && h.blueRim) {
                for (float z : {h.z0 - 0.07f, h.z1 + 0.07f}) zs.push_back(z);
                for (float y : {h.y0 - 0.07f, h.y1 + 0.07f}) ys.push_back(y);
            }
        }
        if (liv) {
            for (float y : {0.55f, 0.82f, 2.08f, 4.30f, BAL_Y - 0.05f, DOOR_TOP + 0.11f}) ys.push_back(y);
            for (auto& d : liv->doors) { zs.push_back(d.first - DOOR_W / 2 - 0.11f); zs.push_back(d.first + DOOR_W / 2 + 0.11f); }
            // knikpunten van de schuine bandrand en de uiteinden van de band
            for (float z : {liv->dd0, liv->dd0 + 1.6f, liv->dd1 - 1.6f, liv->dd1, liv->dd0 - 0.6f, liv->dd1 + 0.6f}) zs.push_back(z);
            ys.push_back(2.83f);
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
                if (liv && zc > liv->dd0 - 0.6f && zc < liv->dd1 + 0.6f) {
                    float fa = liv->bandTop(za), fb = liv->bandTop(zb);
                    if (std::fabs(fa - fb) > 1e-4f && std::max(fa, fb) > ya && std::min(fa, fb) < yb) {
                        // rechthoek knippen met de lijn y = f(z): onder = band, boven = geel
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
                                for (auto& q : poly) ids.push_back(op.vert(S.P(s, q.first, q.second), S.N(s, q.second), pc, m2));
                                for (size_t k = 1; k + 1 < ids.size(); k++)
                                    if (s > 0) op.i.insert(op.i.end(), {ids[0], ids[k], ids[k + 1]});
                                    else op.i.insert(op.i.end(), {ids[0], ids[k + 1], ids[k]});
                            }
                        }
                        continue;
                    }
                }
                for (int s = -1; s <= 1; s += 2) {
                    Vec3 a = S.P(s, za, ya), b = S.P(s, zb, ya), cc = S.P(s, zb, yb), d = S.P(s, za, yb);
                    Vec3 na = S.N(s, ya), nb = S.N(s, yb);
                    if (!liv) { na = na * -1.0f; nb = nb * -1.0f; }
                    op.quad(a, b, cc, d, na, na, nb, nb, c, mat);
                }
            }
        }
        // rondingen in de hoeken van de gaten, nis en glas
        for (const Hole& h : holes) {
            if (h.z0 < z0 - 1e-3f || h.z1 > z1 + 1e-3f) continue;  // hoort bij een ander stuk huid
            auto rr = roundRect(h);
            const int seg = (int)rr.size() / 4;
            const float cz[4] = {h.z1, h.z1, h.z0, h.z0}, cy[4] = {h.y0, h.y1, h.y1, h.y0};
            for (int s = -1; s <= 1; s += 2) {
                for (int c = 0; c < 4; c++) {
                    float mat = MAT_PAINT;
                    Vec3 col = liv ? liv->at(cz[c], cy[c], mat) : in;
                    if (liv && h.blueRim) col = BLUE;
                    Vec3 C = S.P(s, cz[c], cy[c]);
                    for (int k = 0; k < seg - 1; k++) {
                        auto p0 = rr[c * seg + k], p1 = rr[c * seg + k + 1];
                        Vec3 A = S.P(s, p0.first, p0.second), B = S.P(s, p1.first, p1.second);
                        Vec3 n = S.N(s, cy[c]) * (liv ? 1.0f : -1.0f);
                        uint32_t i0 = op.vert(C, n, col, mat), i1 = op.vert(A, n, col, mat), i2 = op.vert(B, n, col, mat);
                        op.i.insert(op.i.end(), {i0, i1, i2});
                    }
                }
                if (!h.window || !liv) continue;
                // nis door de wand (zwart rubber), glas iets naar binnen
                for (size_t k = 0; k < rr.size(); k++) {
                    auto p0 = rr[k], p1 = rr[(k + 1) % rr.size()];
                    Vec3 A = S.P(s, p0.first, p0.second), B = S.P(s, p1.first, p1.second);
                    Vec3 nA = S.N(s, p0.second), nB = S.N(s, p1.second);
                    Vec3 A2 = A - nA * WALL, B2 = B - nB * WALL;
                    Vec3 n = normalize(cross(B - A, nA));
                    op.quad(A, B, B2, A2, n, RUBBER, MAT_RUBBER);
                }
                Surf G{0.035f};
                float zc = (h.z0 + h.z1) / 2, yc = (h.y0 + h.y1) / 2;
                Vec3 n = S.N(s, yc);
                uint32_t c0 = gl.vert(G.P(s, zc, yc), n, GLASSC, EX_GLASS);
                std::vector<uint32_t> ring;
                for (auto& p : rr) ring.push_back(gl.vert(G.P(s, p.first, p.second), n, GLASSC, EX_GLASS));
                for (size_t k = 0; k < ring.size(); k++) gl.i.insert(gl.i.end(), {c0, ring[k], ring[(k + 1) % ring.size()]});
            }
        }
    }

    // dichte doorsnede (profiel tussen y0 en y1) op z, bijv. rijtuigeind of overgang
    void section(float z, float y0, float y1, Vec3 c, float shrink = 0) {
        std::vector<float> ys;
        for (float y = y0; y < y1; y += 0.15f) ys.push_back(y);
        ys.push_back(y1);
        for (size_t k = 0; k + 1 < ys.size(); k++) {
            float xa = std::max(0.0f, outerX(ys[k]) - shrink), xb = std::max(0.0f, outerX(ys[k + 1]) - shrink);
            op.quad(Vec3(-xa, ys[k], z), Vec3(xa, ys[k], z), Vec3(xb, ys[k + 1], z), Vec3(-xb, ys[k + 1], z), Vec3(0, 0, 1), c, MAT_PAINT);
        }
    }
    // kopwand met deuropening (doorloop): breedte 2*dx, van dy0 tot dy1
    void sectionDoor(float z, float y0, float y1, Vec3 c, float shrink, float dx, float dy0, float dy1) {
        std::vector<float> ys;
        for (float y = y0; y < y1; y += 0.15f) ys.push_back(y);
        ys.push_back(y1);
        ys.push_back(dy0);
        ys.push_back(dy1);
        std::sort(ys.begin(), ys.end());
        for (size_t k = 0; k + 1 < ys.size(); k++) {
            float ya = ys[k], yb = ys[k + 1];
            if (yb - ya < 1e-3f) continue;
            float xa = std::max(0.0f, outerX(ya) - shrink), xb = std::max(0.0f, outerX(yb) - shrink);
            bool door = (ya + yb) / 2 > dy0 && (ya + yb) / 2 < dy1;
            if (!door) {
                op.quad(Vec3(-xa, ya, z), Vec3(xa, ya, z), Vec3(xb, yb, z), Vec3(-xb, yb, z), Vec3(0, 0, 1), c, MAT_PAINT);
            } else {
                op.quad(Vec3(-xa, ya, z), Vec3(-dx, ya, z), Vec3(-dx, yb, z), Vec3(-xb, yb, z), Vec3(0, 0, 1), c, MAT_PAINT);
                op.quad(Vec3(dx, ya, z), Vec3(xa, ya, z), Vec3(xb, yb, z), Vec3(dx, yb, z), Vec3(0, 0, 1), c, MAT_PAINT);
            }
        }
        // kozijn
        float zz = z;
        op.box({-dx - 0.03f, (dy0 + dy1) / 2, zz}, {0.06f, dy1 - dy0, 0.16f}, Vec3(0.18f, 0.18f, 0.19f), MAT_PAINT);
        op.box({dx + 0.03f, (dy0 + dy1) / 2, zz}, {0.06f, dy1 - dy0, 0.16f}, Vec3(0.18f, 0.18f, 0.19f), MAT_PAINT);
        op.box({0, dy1 + 0.03f, zz}, {2 * dx + 0.12f, 0.06f, 0.16f}, Vec3(0.18f, 0.18f, 0.19f), MAT_PAINT);
    }
    // vlakke onderkant
    void underside(float z0, float z1, float y) {
        float x = outerX(y);
        op.quad(Vec3(-x, y, z0), Vec3(x, y, z0), Vec3(x, y, z1), Vec3(-x, y, z1), Vec3(0, -1, 0), SKIRT * 0.7f, MAT_PAINT);
    }
    void box(Vec3 c, Vec3 s, Vec3 col, float mat = MAT_PAINT) { op.box(c, s, col, mat); }
};

// ---------- stoelen ----------
// Stoel met zitting, rugleuning (licht achterover), hoofdsteun, donkere schaal en handgreep aan de gangkant.
void seat(MeshBuilder& op, float x, float floor, float z, float dir, Vec3 fab, bool first, bool aisleRight) {
    float w = first ? 0.50f : 0.46f;
    Vec3 fab2 = fab * 1.12f;
    // zitting
    op.box({x, floor + 0.44f, z + dir * 0.02f}, {w, 0.11f, 0.48f}, fab, MAT_RUBBER);
    op.box({x, floor + 0.36f, z + dir * 0.0f}, {w - 0.04f, 0.06f, 0.44f}, SHELL, MAT_PAINT);
    // rugleuning in drie stukken, iets achterover
    for (int k = 0; k < 3; k++) {
        float y = floor + 0.62f + k * 0.22f, back = -dir * (0.22f + k * 0.035f);
        op.box({x, y, z + back}, {w, 0.22f, 0.10f}, k == 2 ? fab2 : fab, MAT_RUBBER);
        op.box({x, y, z + back - dir * 0.07f}, {w + 0.02f, 0.23f, 0.04f}, SHELL, MAT_PAINT);  // schaal
    }
    // hoofdsteun: niet tot aan de achterkant (stoelen rug aan rug: anders z-fighting met de andere stoel)
    op.box({x, floor + 1.30f, z - dir * 0.31f}, {w - 0.06f, 0.16f, 0.08f}, fab2, MAT_RUBBER);
    // handgreep aan de gangkant bovenop
    float gx = x + (aisleRight ? 1.0f : -1.0f) * (w / 2 - 0.03f);
    op.tube({gx, floor + 1.30f, z - dir * 0.36f}, {gx, floor + 1.42f, z - dir * 0.36f}, 0.018f, 5, ALU, MAT_METAL);
    op.box({gx, floor + 1.42f, z - dir * 0.36f}, {0.03f, 0.03f, 0.10f}, ALU, MAT_METAL);
    if (first) {
        for (int s = -1; s <= 1; s += 2) op.box({x + s * (w / 2 + 0.03f), floor + 0.66f, z - dir * 0.02f}, {0.06f, 0.05f, 0.38f}, fab, MAT_RUBBER);  // armleuningen
    }
    op.box({x, floor + 0.17f, z}, {0.08f, 0.34f, 0.30f}, BOGIEC, MAT_METAL);  // poot
}

// ---------- draaistel ----------
void bogie(MeshBuilder& op, bool motor) {
    const float r = 0.46f, wb = motor ? 2.75f : 2.5f;
    for (int a = -1; a <= 1; a += 2) {
        for (int s = -1; s <= 1; s += 2)
            op.cylX({s * 0.7175f, r, a * wb / 2}, r, 0.135f, 20, Vec3(0.20f, 0.19f, 0.18f), MAT_METAL, STEEL * 0.8f, r * 0.55f, MAT_METAL);
        op.tube({-0.70f, r, a * wb / 2}, {0.70f, r, a * wb / 2}, 0.08f, 8, STEEL * 0.6f, MAT_METAL);  // as
        if (!motor)
            for (float x : {-0.25f, 0.25f}) op.cylX({x, r, a * wb / 2}, 0.32f, 0.05f, 16, STEEL, MAT_METAL, STEEL * 0.7f, 0.12f, MAT_METAL);  // remschijven
        for (int s = -1; s <= 1; s += 2) {
            op.box({s * 1.00f, r, a * wb / 2}, {0.22f, 0.24f, 0.30f}, BOGIEC, MAT_PAINT);                                   // asbus
            op.tube({s * 1.00f, r + 0.13f, a * wb / 2}, {s * 1.00f, r + 0.38f, a * wb / 2}, 0.10f, 10, Vec3(0.25f, 0.25f, 0.27f), MAT_METAL);  // schroefveer
        }
    }
    for (int s = -1; s <= 1; s += 2) {
        // zijframe (laag in het midden)
        op.box({s * 1.00f, r + 0.43f, 0}, {0.20f, 0.18f, wb + 0.75f}, BOGIEC, MAT_PAINT);
        op.box({s * 1.00f, r + 0.28f, 0}, {0.20f, 0.20f, 1.2f}, BOGIEC, MAT_PAINT);
        op.tube({s * 1.00f, r + 0.52f, 0}, {s * 1.00f, r + 0.80f, 0}, 0.30f, 14, Vec3(0.06f, 0.06f, 0.06f), MAT_RUBBER);  // luchtveer
        op.box({s * 1.25f, r + 0.40f, 0.6f}, {0.08f, 0.08f, 0.6f}, Vec3(0.75f, 0.60f, 0.10f), MAT_PAINT);               // demper
        op.box({s * 0.7175f, 0.16f, 0}, {0.10f, 0.10f, wb - 0.95f}, STEEL * 0.5f, MAT_METAL);                          // magneetrem
    }
    op.box({0, r + 0.30f, 0}, {1.9f, 0.22f, 0.45f}, BOGIEC, MAT_PAINT);  // dwarsbalk
    if (motor)
        for (int a = -1; a <= 1; a += 2) {
            op.tube({-0.45f, r + 0.05f, a * 0.62f}, {0.45f, r + 0.05f, a * 0.62f}, 0.27f, 14, Vec3(0.18f, 0.19f, 0.20f), MAT_METAL);  // tractiemotor
            op.box({0.55f, r, a * (wb / 2 - 0.25f)}, {0.25f, 0.45f, 0.40f}, Vec3(0.16f, 0.16f, 0.17f), MAT_PAINT);                  // tandwielkast
        }
}

// ---------- neus (cabinefront) ----------
float lerpT(const float* xs, const float* ys, int n, float x) {
    if (x <= xs[0]) return ys[0];
    for (int i = 0; i + 1 < n; i++)
        if (x <= xs[i + 1]) return ys[i] + (ys[i + 1] - ys[i]) * (x - xs[i]) / (xs[i + 1] - xs[i]);
    return ys[n - 1];
}
float noseZ(float y) {  // hoe ver het front voor het begin van de neus uitsteekt
    const float ys[] = {0.55f, 0.95f, 1.40f, 1.95f, 2.10f, 2.70f, 3.35f, 3.90f, 4.35f, 4.67f};
    const float zs[] = {2.50f, 2.58f, 2.60f, 2.50f, 2.40f, 1.98f, 1.52f, 0.95f, 0.42f, 0.0f};
    return lerpT(ys, zs, 10, y);
}
float noseHalfW(float y) {  // halve breedte van het vlakke front
    const float ys[] = {0.55f, 1.0f, 1.8f, 2.6f, 3.4f, 4.0f, 4.4f, 4.67f};
    const float ws[] = {0.85f, 1.00f, 1.10f, 1.12f, 1.10f, 1.00f, 0.80f, 0.40f};
    return lerpT(ys, ws, 8, y);
}
void buildNose(MeshBuilder& mb, MeshBuilder& gl, float z0) {
    std::vector<float> NY;
    for (float y = 0.55f; y < 4.67f; y += 0.07f) NY.push_back(y);
    for (float y : {0.78f, 0.95f, 1.32f, 1.94f, 2.06f, 3.36f, 4.28f}) NY.push_back(y);
    NY.push_back(4.67f);
    std::sort(NY.begin(), NY.end());
    NY.erase(std::unique(NY.begin(), NY.end(), [](float a, float b) { return std::fabs(a - b) < 0.01f; }), NY.end());
    const int SIDE = 14, ARC = 14, FRONT = 6;
    int rows = (int)NY.size();
    std::vector<std::vector<Vec3>> P(rows);
    std::vector<std::vector<int>> reg(rows);
    std::vector<std::vector<float>> tt(rows);
    for (int r = 0; r < rows; r++) {
        float y = NY[r];
        float hb = std::max(0.02f, outerX(std::max(y, 0.30f)));
        float hf = std::min(noseHalfW(y), hb);
        float zf = noseZ(y);
        float rad = std::min(0.75f, std::min(hf * 0.9f, zf * 0.9f + 0.01f));
        std::vector<Vec3> half;
        std::vector<int> rg;
        std::vector<float> t;
        for (int k = 0; k <= SIDE; k++) {
            float f = (float)k / SIDE;
            half.push_back(Vec3(hb + (hf - hb) * f, y, z0 + (zf - rad) * f));
            rg.push_back(0);
            t.push_back(f);
        }
        for (int k = 1; k <= ARC; k++) {
            float a = (PI / 2) * k / ARC;
            half.push_back(Vec3(hf - rad + rad * std::cos(a), y, z0 + zf - rad + rad * std::sin(a)));
            rg.push_back(1);
            t.push_back(1);
        }
        for (int k = 1; k <= FRONT; k++) {
            float f = (float)k / FRONT;
            half.push_back(Vec3((hf - rad) * (1 - f), y, z0 + zf));
            rg.push_back(2);
            t.push_back(1);
        }
        for (size_t k = 0; k < half.size(); k++) { P[r].push_back(half[k]); reg[r].push_back(rg[k]); tt[r].push_back(t[k]); }
        for (int k = (int)half.size() - 2; k >= 0; k--) {
            P[r].push_back(Vec3(-half[k].x, half[k].y, half[k].z));
            reg[r].push_back(rg[k]);
            tt[r].push_back(t[k]);
        }
    }
    int cols = (int)P[0].size();
    // kleur per vlakje (vlak gekleurd, scherpe grenzen)
    auto colorAt = [&](float y, int rg, float t, float ax, float& mat) {
        mat = MAT_PAINT;
        if (rg == 2 && y > 0.78f && y < 1.32f && ax < 0.36f) return BLACK * 0.5f;  // koppelingsnis (in de neus, niet erbuiten)
        if (y < 0.95f) return SKIRT;
        if (y > 4.28f) return BLACK * 2.0f;
        if (y < 1.95f) return YELLOW;
        // zwart masker rond de voorruit, loopt schuin over de zijkant terug
        bool mask = rg > 0 || y > 2.06f + (1.0f - t) * 0.9f;
        if (!mask) return YELLOW;
        if (y > 2.06f && y < 3.36f && (rg == 2 || (rg == 1 && ax < noseHalfW(y) + 0.1f))) { mat = MAT_GLASS; return Vec3(0.03f, 0.035f, 0.04f); }
        return BLACK;
    };
    for (int r = 0; r + 1 < rows; r++)
        for (int c = 0; c + 1 < cols; c++) {
            Vec3 a = P[r][c], b = P[r][c + 1], d = P[r + 1][c], e = P[r + 1][c + 1];
            float mat;
            colorAt((NY[r] + NY[r + 1]) / 2, std::max(reg[r][c], reg[r][c + 1]), (tt[r][c] + tt[r][c + 1]) / 2, std::fabs((a.x + b.x) / 2), mat);
            // kleur per hoekpunt: vloeiende overgang geel/zwart (geen trapjes)
            auto vc = [&](int rr, int cc) { float m2; return colorAt(NY[rr], reg[rr][cc], tt[rr][cc], std::fabs(P[rr][cc].x), m2); };
            Vec3 n = normalize(cross(d - a, b - a));
            Vec3 outward = (a + e) * 0.5f - Vec3(0, (a.y + e.y) / 2, z0);
            if (dot(n, outward) < 0) n = n * -1.0f;
            // vloeiende normalen: per hoekpunt uit de buren
            auto vn = [&](int rr, int cc) {
                Vec3 du = P[rr][std::min(cc + 1, cols - 1)] - P[rr][std::max(cc - 1, 0)];
                Vec3 dv = P[std::min(rr + 1, rows - 1)][cc] - P[std::max(rr - 1, 0)][cc];
                Vec3 m = normalize(cross(dv, du));
                Vec3 o = P[rr][cc] - Vec3(0, P[rr][cc].y, z0);
                return dot(m, o) < 0 ? m * -1.0f : m;
            };
            (void)n;
            if (mat == MAT_GLASS) {
                // voorruit: doorzichtig (glas-pass), zodat je vanuit de cabine naar buiten kijkt
                Vec3 gc(0.16f, 0.20f, 0.22f);
                uint32_t ga = gl.vert(a, vn(r, c), gc, EX_GLASS), gb = gl.vert(b, vn(r, c + 1), gc, EX_GLASS);
                uint32_t ge = gl.vert(e, vn(r + 1, c + 1), gc, EX_GLASS), gd = gl.vert(d, vn(r + 1, c), gc, EX_GLASS);
                gl.i.insert(gl.i.end(), {ga, gb, ge, ga, ge, gd});
                continue;
            }
            uint32_t ia = mb.vert(a, vn(r, c), vc(r, c), mat), ib = mb.vert(b, vn(r, c + 1), vc(r, c + 1), mat);
            uint32_t ie = mb.vert(e, vn(r + 1, c + 1), vc(r + 1, c + 1), mat), id = mb.vert(d, vn(r + 1, c), vc(r + 1, c), mat);
            mb.i.insert(mb.i.end(), {ia, ib, ie, ia, ie, id});
        }
    // onderkant dicht
    for (int c = 0; c + 1 < cols; c++) {
        uint32_t i0 = mb.vert(P[0][c], Vec3(0, -1, 0), SKIRT, MAT_PAINT), i1 = mb.vert(P[0][c + 1], Vec3(0, -1, 0), SKIRT, MAT_PAINT);
        uint32_t i2 = mb.vert(Vec3(0, 0.55f, z0), Vec3(0, -1, 0), SKIRT, MAT_PAINT);
        mb.i.insert(mb.i.end(), {i0, i1, i2});
    }
    float zf = z0 + noseZ(1.2f);
    // koppelingsnis met Scharfenberg-koppeling, baanschuiver
    // Scharfenberg-kop: stang uit de nis, kop met trechter en contactblok; steekt maar ~15 cm voor de neus uit
    mb.tube({0, 1.02f, zf - 0.30f}, {0, 1.02f, zf + 0.02f}, 0.07f, 12, STEEL * 0.7f, MAT_METAL);
    mb.box({0, 1.02f, zf + 0.07f}, {0.28f, 0.22f, 0.10f}, STEEL, MAT_METAL);
    mb.box({0.06f, 1.05f, zf + 0.125f}, {0.08f, 0.08f, 0.01f}, BLACK, MAT_METAL);   // trechter
    mb.box({-0.07f, 1.02f, zf + 0.125f}, {0.07f, 0.14f, 0.01f}, Vec3(0.75f, 0.55f, 0.20f), MAT_METAL);  // elektrische contacten
    mb.box({0, 0.62f, z0 + noseZ(0.6f) - 0.05f}, {1.9f, 0.18f, 0.12f}, SKIRT * 0.8f, MAT_PAINT);
    // lampen: koplampen buiten, sluitseinen binnen, toplicht onder de voorruit (uitgeschakeld = donker glas)
    for (int s = -1; s <= 1; s += 2) {
        for (float x : {0.82f, 0.50f}) {
            float y = 1.62f, z = z0 + noseZ(y) + 0.005f;
            mb.box({s * x, y, z - 0.03f}, {0.20f, 0.14f, 0.08f}, Vec3(0.15f, 0.15f, 0.16f), MAT_GLASS);
        }
    }
    mb.box({0, 1.86f, z0 + noseZ(1.86f) - 0.02f}, {0.16f, 0.10f, 0.08f}, Vec3(0.15f, 0.15f, 0.16f), MAT_GLASS);
    // ruitenwissers
    for (int s = -1; s <= 1; s += 2) {
        float y = 2.14f;
        mb.box({s * 0.45f, y, z0 + noseZ(y) + 0.03f}, {0.75f, 0.025f, 0.025f}, BLACK, MAT_RUBBER);
    }
}

// rijtuigtypes van de VIRMm-VI
struct CarSpec {
    const char* type;
    float len;        // lengte van de bak (zonder koppelruimte)
    bool cab;         // cabine aan +z (canoniek)
    bool firstUp;     // bovendek 1e klas
    int motorEnd;     // 0 = geen, +1 = draaistel aan +z, -1 = aan -z
    bool panto;
    bool toilet;      // toilet op het balkon aan -z
};
}  // namespace

class VirmStock : public RollingStock {
public:
    explicit VirmStock(bool ddz = false);
    float floorAt(int car, float lz) const override { return floorAt3(car, 0, lz, BAL_Y); }
    bool walkable(int car, float lx, float lz) const override { return walkable3(car, lx, lz, BAL_Y); }
    float floorAt3(int car, float lx, float lz, float curY) const override;
    bool walkable3(int car, float lx, float lz, float curY) const override;
    void build(SignAtlas& atlas, const std::vector<std::string>& names) override;
    void drawOpaque(World& world, const StockDraw& s) const override;
    void drawGlass(World& world, const StockDraw& s) const override;

private:
    std::vector<CarSpec> spec_;
    // canoniek dubbeldeks deel [dd0, dd1] per rijtuig
    float dd0(int i) const { return -spec_[i].len / 2 + BALCONY; }
    float dd1(int i) const { return spec_[i].cab ? spec_[i].len / 2 - CAB - BALCONY + 0.4f : spec_[i].len / 2 - BALCONY; }
    std::vector<std::array<float, 2>> doorC_;  // canonieke deur-z per rijtuig
    std::vector<Mesh> car_, carInt_, carGlass_, leaf_;
    size_t vertCount_ = 0;  // per rijtuig; deurbladen: [rijtuig*4 + zijde*2 + blad]
    Mesh trailer_, motor_, panto_, gangway_, headlight_, taillight_, gangLeaf_, cabDoor_, lever_;
    std::vector<Mesh> display_, screen_;
    void canon(int car, float lx, float lz, float& x, float& z) const {
        int cs = cabSign(car);
        x = cs < 0 ? -lx : lx;
        z = cs < 0 ? -lz : lz;
    }
    void buildCar(int i, SignAtlas& atlas);
};

RollingStock* makeVirm() { return new VirmStock(); }
// DDZ (NID): dubbeldekker op hetzelfde interieur als de VIRM, maten en model uit Train Simulator
RollingStock* makeDdz() { return new VirmStock(true); }

VirmStock::VirmStock(bool ddz) {
    product = "Intercity";
    typeName = ddz ? "DDZ" : "VIRMm";
    tsModel = ddz ? "ddz" : "virm";
    soundSet = ddz ? "ddz" : "virm";  // opnames (Train Simulator, tools/ts/trains.py)
    tsWalk = true;  // lopen over de loopvloer van het model: zitplaatsen op de vloerhoogte van het model
    gap = 0.80f;
    gangwayDoors = true;  // schuifdeuren in de kopwanden tussen de rijtuigen
    halfW = HW;
    floorY = BAL_Y;
    doorW = DOOR_W;
    height = H;
    nose = NOSE;
    screenY = BAL_CEIL - 0.28f;
    vmax = 44.4f;  // 160 km/u (materieel); op dit traject begrensd door de baanvaksnelheid
    acc = 0.6f;    // aanzetversnelling (Wikipedia: 0,6 m/s2)
    dec = 0.8f;    // bedrijfsremming (max. 1,2 m/s2)
    // lengte over buffers minus koppelruimte (totaal 161,86 m)
    spec_ = {
        {"mBvk1", 27.26f - 0.67f, true, false, -1, false, false},
        {"ABv5", 26.46f - 0.67f, false, true, 0, false, true},
        {"mBv7", 26.96f - 0.67f, false, false, 1, false, true},
        {"ABv6", 27.46f - 0.67f, false, true, 0, true, false},
        {"ABv4", 26.46f - 0.67f, false, true, 0, false, true},
        {"mBvk2", 27.26f - 0.67f, true, false, -1, false, false},
    };
    if (ddz)  // DDZ-IV: mBk - Bv - ABv - ABvk (motorrijtuig zonder benedendek, maar voor het interieur gelijk)
        spec_ = {
            {"mBk", 21.2f - 0.8f, true, false, -1, true, false},
            {"Bv", 26.62f - 0.8f, false, false, 0, false, true},
            {"ABv", 26.62f - 0.8f, false, true, 0, false, true},
            {"ABvk", 27.15f - 0.8f, true, true, 0, false, false},
        };
    cars = (int)spec_.size();
    const TsLayout* ts = tsLayout(tsModel.c_str());  // maten van het Train Simulator-model (tslayout.h)
    if (ts && ts->cars != cars) ts = nullptr;
    for (int i = 0; i < cars; i++) {
        CarSpec& c = spec_[i];
        if (ts) c.len = ts->car[i].pitch - gap;
        len.push_back(c.len);
        std::array<float, 2> d;
        d[0] = -c.len / 2 + 1.25f;
        d[1] = c.cab ? c.len / 2 - CAB - 0.95f : c.len / 2 - 1.25f;
        if (ts && ts->car[i].doors == 2) { d[0] = ts->car[i].doorZ[0]; d[1] = ts->car[i].doorZ[1]; }
        doorC_.push_back(d);
        // lokale deur-z: laatste rijtuig is gespiegeld
        if (cabSign(i) < 0) doorZ.push_back({-d[1], -d[0]});
        else doorZ.push_back(d);
    }
    // cabine (verhoogde vloer, machinist in het midden), canoniek eindrijtuig
    const float LE = spec_[0].len;
    cab.wallZ = LE / 2 - CAB;
    cab.doorX = 0.45f;
    cab.doorW = 0.70f;
    cab.floorY = 1.45f;
    cab.passFloor = BAL_Y;
    cab.deskZ = LE / 2 - 1.30f;
    cab.seatX = 0;
    cab.seatZ = LE / 2 - 1.85f;
    cab.seatTop = cab.floorY + 0.52f;
    cab.leverX = 0.36f;
    cab.leverY = 2.06f;
    cab.leverZ = cab.deskZ + 0.14f;
}

// ---------- lopen ----------
float VirmStock::floorAt3(int car, float lx, float lz, float curY) const {
    float x, z;
    canon(car, lx, lz, x, z);
    // loopvloer uit het Train Simulator-model (trappen en dekken zoals getekend)
    TsModel* tm = tsModel.empty() ? nullptr : ::tsModel(tsModel);
    float ty;
    if (tm && tm->hasWalk(car)) {
        if (tm->walkFloor(car, x, z, curY, ty)) return ty;
        // geen vloer binnen een stap (net ingestapt, of in een wand): de dichtstbijzijnde vloer, anders hier of eromheen
        for (float r = 0; r <= 0.6f; r += 0.1f)
            for (float dx : {0.0f, -r, r})
                for (float dz : {0.0f, -r, r})
                    if (tm->walkFloor(car, x + dx, z + dz, curY, ty, 100.0f)) return ty;
        return curY < 1.4f ? LOW_Y : UP_Y;
    }
    float a = dd0(car), b = dd1(car);
    if (z <= a || z >= b) return BAL_Y;
    // afstand vanaf het dichtstbijzijnde uiteinde van het dubbeldeks deel
    float s = std::min(z - a, b - z);
    bool nearA = (z - a) < (b - z);
    float side = nearA ? -x : x;  // >= 0: trap omhoog, < 0: trap omlaag (zelfde kanten als in het TS-model)
    if (s < STAIR) {
        if (side < 0 && curY < 1.9f) return s < 1.35f ? BAL_Y - (BAL_Y - LOW_Y) * s / 1.35f : LOW_Y;
        if (side >= 0 && curY > BAL_Y - 0.25f) return BAL_Y + (UP_Y - BAL_Y) * s / STAIR;
        return curY < 1.4f ? LOW_Y : UP_Y;
    }
    return curY < 1.4f ? LOW_Y : UP_Y;
}

bool VirmStock::walkable3(int car, float lx, float lz, float curY) const {
    float x, z;
    canon(car, lx, lz, x, z);
    const CarSpec& c = spec_[car];
    float L = c.len, a = dd0(car), b = dd1(car);
    if (std::fabs(z) > L / 2 - 0.25f) return std::fabs(x) < 0.55f;            // overgang
    if (c.cab && z > L / 2 - CAB) return false;                              // cabine
    TsModel* tm = tsModel.empty() ? nullptr : ::tsModel(tsModel);
    if (tm && tm->hasWalk(car)) {
        float ty;
        return std::fabs(x) < HW - 0.22f && tm->walkFloor(car, x, z, curY, ty);
    }
    if (z <= a || z >= b) {
        // balkon
        return std::fabs(x) < HW - 0.30f;
    }
    float s = std::min(z - a, b - z);
    bool nearA = (z - a) < (b - z);
    float side = nearA ? -x : x;
    if (s < STAIR) {
        if (std::fabs(side) < 0.06f) return false;  // tussenwand tussen de trappen
        return std::fabs(x) < 1.15f;
    }
    if (s < STAIR + 0.5f) return std::fabs(x) < 1.0f;  // ruimte bij de trap
    return std::fabs(x) < 0.32f;                       // gangpad
}

// ---------- bouwen ----------
void VirmStock::buildCar(int i, SignAtlas& atlas) {
    const CarSpec& c = spec_[i];
    const float L = c.len, a = dd0(i), b = dd1(i);
    const float zEnd = c.cab ? L / 2 - NOSE : L / 2;  // begin neus / rijtuigeinde
    MeshBuilder op, gl;
    Builder B{op, gl};
    Livery liv;
    liv.dd0 = a;
    liv.dd1 = b;
    for (float d : doorC_[i]) liv.doors.push_back({d, 0});

    // ---- ramen ----
    std::vector<Hole> ddHoles, endHoles, upperOnly;
    float ddLen = b - a;
    int nw = std::max(2, (int)std::floor((ddLen - 0.6f) / WIN_PITCH));
    float w0 = (a + b) / 2 - (nw - 1) * WIN_PITCH / 2;
    for (int k = 0; k < nw; k++) {
        float zc = w0 + k * WIN_PITCH;
        ddHoles.push_back({zc - WIN_W / 2, zc + WIN_W / 2, 1.00f, 1.92f, 0.17f, true, false});   // benedendek
        Hole up{zc - WIN_W / 2, zc + WIN_W / 2, 2.98f, 3.70f, 0.15f, true, true};                 // bovendek
        ddHoles.push_back(up);
        liv.upperWins.push_back(up);
    }
    // balkons: deuropening + ovaal raampje tussen deur en trap
    auto balconyHoles = [&](float door, float towardDD) {
        std::vector<Hole> hs;
        hs.push_back({door - DOOR_W / 2, door + DOOR_W / 2, BAL_Y - 0.25f, DOOR_TOP, 0.06f, false, false});
        float oz = door + towardDD * (DOOR_W / 2 + 0.55f);
        hs.push_back({oz - 0.22f, oz + 0.22f, 1.70f, 2.55f, 0.21f, true, false});
        return hs;
    };
    for (Hole h : balconyHoles(doorC_[i][0], 1.0f)) endHoles.push_back(h);
    for (Hole h : balconyHoles(doorC_[i][1], -1.0f)) endHoles.push_back(h);
    if (c.cab) {
        // cabinezijraam en machinistendeur
        endHoles.push_back({L / 2 - CAB + 0.10f, L / 2 - NOSE - 0.05f, 2.10f, 2.95f, 0.12f, true, false});
    }

    // ---- buitenhuid ----
    std::vector<float> doorEdges;
    for (float d : doorC_[i]) { doorEdges.push_back(d - DOOR_W / 2); doorEdges.push_back(d + DOOR_W / 2); }
    B.shell(-L / 2, a, END_BOTTOM, H, endHoles, &liv, {}, 0, doorEdges);
    B.shell(a, b, DD_BOTTOM, H, ddHoles, &liv, {}, 0);
    B.shell(b, zEnd, END_BOTTOM, H, endHoles, &liv, {}, 0, doorEdges);
    B.underside(-L / 2, a, END_BOTTOM);
    B.underside(b, zEnd, END_BOTTOM);
    B.underside(a, b, DD_BOTTOM);
    B.section(a, DD_BOTTOM, END_BOTTOM + 0.01f, SKIRT);
    B.section(b, DD_BOTTOM, END_BOTTOM + 0.01f, SKIRT);
    // rijtuigeinden: kopwand (donker), met overgangsopening donkerder
    B.sectionDoor(-L / 2 + 0.02f, END_BOTTOM, H, Vec3(0.30f, 0.30f, 0.31f), 0.02f, 0.42f, BAL_Y, BAL_Y + 2.05f);
    if (!c.cab) B.sectionDoor(L / 2 - 0.02f, END_BOTTOM, H, Vec3(0.30f, 0.30f, 0.31f), 0.02f, 0.42f, BAL_Y, BAL_Y + 2.05f);
    // voegen: dunne donkere lijnen (paneelnaden) op de buitenhuid bij de overgang balkon - dubbeldeks deel
    for (float z : {a, b})
        for (int s = -1; s <= 1; s += 2)
            for (float y = 1.0f; y < 4.2f; y += 0.2f) {
                Vec3 p(s * (outerX(y + 0.1f) + 0.002f), y + 0.1f, z);
                op.box(p, {0.004f, 0.2f, 0.012f}, Vec3(0.35f, 0.27f, 0.05f), MAT_PAINT);
            }
    // treeplanken onder de deuren (geel randje)
    for (float d : doorC_[i])
        for (int s = -1; s <= 1; s += 2) {
            op.box({s * (HW - 0.06f), BAL_Y - 0.30f, d}, {0.22f, 0.04f, DOOR_W}, STEEL * 0.8f, MAT_METAL);
            op.box({s * (HW + 0.02f), BAL_Y - 0.30f, d}, {0.03f, 0.05f, DOOR_W}, YELLOW, MAT_PAINT);
            // deurdorpel en zijkanten van de opening
            op.box({s * (HW - 0.05f), BAL_Y - 0.02f, d}, {0.12f, 0.04f, DOOR_W}, STEEL, MAT_METAL);
            for (int e = -1; e <= 1; e += 2) op.box({s * (HW - WALL / 2), (BAL_Y + DOOR_TOP) / 2, d + e * (DOOR_W / 2 + 0.01f)}, {WALL, DOOR_TOP - BAL_Y, 0.03f}, BLUE * 0.8f, MAT_PAINT);
            op.box({s * (HW - WALL / 2), DOOR_TOP + 0.01f, d}, {WALL, 0.03f, DOOR_W}, BLUE * 0.8f, MAT_PAINT);
        }
    // klassenummers ("1" / "2") op de band en bij de bovenramen
    {
        SignRect r2 = atlas.add("2", BLUE, Vec3(1, 1, 1), 64, true, 0.3f);
        SignRect r1 = atlas.add("1", BLUE, Vec3(1, 1, 0.3f), 64, true, 0.3f);
        for (int s = -1; s <= 1; s += 2) {
            float zc = w0 + WIN_PITCH * 0.5f;
            SignAtlas::quad(op, r2, Vec3(s * (outerX(1.95f) + 0.004f), 1.98f, zc), Vec3(0, 0, (float)-s), 0.12f, false);
            SignAtlas::quad(op, c.firstUp ? r1 : r2, Vec3(s * (outerX(3.82f) + 0.004f), 3.82f, zc), Vec3(0, 0, (float)-s), 0.10f, false);
        }
    }
    // ---- dak ----
    for (int e = -1; e <= 1; e += 2) {
        float zc = e < 0 ? -L / 2 + 1.9f : (c.cab ? L / 2 - CAB - 0.9f : L / 2 - 1.9f);
        op.box({0, H - 0.02f, zc}, {1.9f, 0.16f, 2.5f}, ROOFC * 0.95f, MAT_PAINT);   // klimaatinstallatie
        for (int k = 0; k < 6; k++) op.box({0, H + 0.065f, zc - 1.0f + k * 0.4f}, {1.5f, 0.012f, 0.18f}, ROOFC * 0.45f, MAT_PAINT);  // roosters
    }
    for (int s = -1; s <= 1; s += 2) op.box({s * 0.82f, outerX(4.52f) > 0 ? 4.53f : 4.5f, (a + b) / 2}, {0.04f, 0.03f, b - a}, ROOFC * 0.7f, MAT_PAINT);  // dakgoten
    if (c.panto) {
        op.box({0, H + 0.04f, a + 2.5f}, {1.6f, 0.10f, 3.4f}, ROOFC * 0.8f, MAT_PAINT);
        for (int s = -1; s <= 1; s += 2)
            for (int e = -1; e <= 1; e += 2) op.tube({s * 0.55f, H + 0.09f, a + 2.5f + e * 1.2f}, {s * 0.55f, H + 0.30f, a + 2.5f + e * 1.2f}, 0.06f, 8, Vec3(0.55f, 0.30f, 0.20f), MAT_PAINT);  // isolatoren
        op.tube({0.55f, H + 0.30f, a + 3.7f}, {0.55f, H + 0.30f, b - 2.0f}, 0.025f, 5, Vec3(0.2f, 0.2f, 0.2f), MAT_RUBBER);  // dakleiding
    }
    if (c.cab) buildNose(op, gl, zEnd);

    // ======================= INTERIEUR =======================
    const size_t interiorStart = op.v.size(), interiorIdx = op.i.size();
    // binnenwanden (crème) met dezelfde openingen
    std::vector<Hole> inDD = ddHoles, inEnd = endHoles;
    B.shell(a, b, LOW_Y, UP_CEIL, inDD, nullptr, CREAM, WALL);
    B.shell(-L / 2 + 0.05f, a, BAL_Y, BAL_CEIL, inEnd, nullptr, CREAM2, WALL);
    B.shell(b, c.cab ? L / 2 - CAB : L / 2 - 0.05f, BAL_Y, BAL_CEIL, inEnd, nullptr, CREAM2, WALL);
    // binnenkant van de kopwanden (crème) met de tussendeuropening
    B.sectionDoor(-L / 2 + 0.12f, BAL_Y, BAL_CEIL, CREAM2, WALL, 0.42f, BAL_Y, BAL_Y + 2.05f);
    if (!c.cab) B.sectionDoor(L / 2 - 0.12f, BAL_Y, BAL_CEIL, CREAM2, WALL, 0.42f, BAL_Y, BAL_Y + 2.05f);
    // vloeren en plafonds
    auto slab = [&](float z0, float z1, float y, float thick, Vec3 col, float shrink) {
        float x = outerX(std::max(y, 0.4f)) - WALL - shrink;
        op.box({0, y - thick / 2, (z0 + z1) / 2}, {2 * x, thick, z1 - z0}, col, MAT_PAINT);
    };
    slab(-L / 2 + 0.05f, a, BAL_Y, 0.05f, FLOORC * 0.8f, 0);
    slab(b, c.cab ? L / 2 - CAB : L / 2 - 0.05f, BAL_Y, 0.05f, FLOORC * 0.8f, 0);
    slab(a + 1.35f, b - 1.35f, LOW_Y, 0.05f, FLOORC, 0);
    slab(a + STAIR, b - STAIR, UP_Y, 0.14f, FLOORC, 0);                      // bovendekvloer (= plafond benedendek)
    slab(a + STAIR, b - STAIR, LOW_CEIL + 0.01f, 0.02f, Vec3(0.90f, 0.89f, 0.85f), 0);
    slab(-L / 2 + 0.05f, a, BAL_CEIL + 0.03f, 0.06f, Vec3(0.90f, 0.89f, 0.85f), 0);
    slab(b, c.cab ? L / 2 - CAB : L / 2 - 0.05f, BAL_CEIL + 0.03f, 0.06f, Vec3(0.90f, 0.89f, 0.85f), 0);
    // bovendek: plafond (donker middenpaneel, crème zijkanten) en lichtlijnen
    {
        float zc = (a + b) / 2, zl = b - a;
        op.box({0, UP_CEIL, zc}, {1.10f, 0.03f, zl}, CEILDARK, MAT_PAINT);
        op.box({0, UP_CEIL + 0.01f, zc}, {2 * outerX(UP_CEIL) - 0.1f, 0.02f, zl}, CREAM, MAT_PAINT);
        for (int s = -1; s <= 1; s += 2) op.box({s * 0.60f, UP_CEIL - 0.03f, zc}, {0.10f, 0.03f, zl - 0.6f}, LIGHTC, MAT_LIGHT);
        // benedendek: lichtlijnen langs het plafond
        for (int s = -1; s <= 1; s += 2) op.box({s * 0.75f, LOW_CEIL - 0.01f, zc}, {0.12f, 0.02f, zl - 2 * STAIR - 0.4f}, LIGHTC, MAT_LIGHT);
        // balkons: lamp
        for (float zb : {(-L / 2 + a) / 2, (b + (c.cab ? L / 2 - CAB : L / 2)) / 2}) op.box({0, BAL_CEIL - 0.01f, zb}, {1.2f, 0.02f, 0.5f}, LIGHTC, MAT_LIGHT);
    }
    // bagagerekken (alleen benedendek) boven de ramen
    for (int s = -1; s <= 1; s += 2) {
        float x = outerX(2.10f) - WALL - 0.18f;
        op.box({s * x, 2.08f, (a + b) / 2}, {0.32f, 0.02f, b - a - 2 * STAIR - 0.3f}, Vec3(0.55f, 0.62f, 0.66f), MAT_GLASS);
        op.box({s * (x - 0.16f), 2.06f, (a + b) / 2}, {0.025f, 0.05f, b - a - 2 * STAIR - 0.3f}, ALU, MAT_METAL);
    }
    // trappen bij beide uiteinden
    for (int e = -1; e <= 1; e += 2) {
        float z0 = e < 0 ? a : b, dir = e < 0 ? 1.0f : -1.0f;  // dir: het dubbeldeks deel in
        float sx = e < 0 ? -1.0f : 1.0f;                        // kant van de trap omhoog (zoals in het TS-model)
        // omlaag (linkerhelft): 5 treden
        for (int k = 0; k < 5; k++) {
            float y = BAL_Y - (BAL_Y - LOW_Y) * (k + 1) / 5.0f;
            float z = z0 + dir * (0.135f + k * 0.27f);
            op.box({-sx * 0.60f, (y + LOW_Y - 0.05f) / 2, z}, {1.08f, y - LOW_Y + 0.05f, 0.27f}, FLOORC * 1.1f, MAT_RUBBER);
            op.box({-sx * 0.60f, y + 0.005f, z - dir * 0.12f}, {1.08f, 0.012f, 0.03f}, YELLOW, MAT_PAINT);
        }
        // omhoog (rechterhelft): 7 treden
        for (int k = 0; k < 7; k++) {
            float y = BAL_Y + (UP_Y - BAL_Y) * (k + 1) / 7.0f;
            float z = z0 + dir * (0.12f + k * (STAIR / 7.0f));
            op.box({sx * 0.60f, (y + BAL_Y - 0.05f) / 2, z}, {1.08f, y - BAL_Y + 0.05f, STAIR / 7.0f + 0.01f}, FLOORC * 1.1f, MAT_RUBBER);
            op.box({sx * 0.60f, y + 0.005f, z - dir * (STAIR / 14.0f - 0.01f)}, {1.08f, 0.012f, 0.03f}, YELLOW, MAT_PAINT);
        }
        // tussenwand met glazen bovenrand en leuningen
        op.box({0, (LOW_Y + UP_Y) / 2 + 0.2f, z0 + dir * STAIR / 2}, {0.06f, UP_Y - LOW_Y + 0.4f, STAIR}, CREAM2, MAT_PAINT);
        for (int s = -1; s <= 1; s += 2) {
            float yTop = s == sx ? UP_Y + 0.95f : BAL_Y + 0.95f, yBot = s == sx ? BAL_Y + 0.95f : LOW_Y + 0.95f;
            op.tube({s * 0.08f, yBot, z0}, {s * 0.08f, yTop, z0 + dir * STAIR}, 0.022f, 6, ALU, MAT_METAL);
        }
        // kopwanden bovendek (donker geperforeerd) en benedendek, met infoscherm
        float zu = z0 + dir * STAIR;
        op.box({-sx * 0.70f, (UP_Y + UP_CEIL) / 2, zu}, {1.30f, UP_CEIL - UP_Y, 0.05f}, DARKPANEL, MAT_PAINT);
        op.box({-sx * 0.70f, UP_Y + 1.55f, zu + dir * 0.04f}, {0.55f, 0.32f, 0.03f}, Vec3(0.20f, 0.35f, 0.55f), MAT_LIGHT);
        float zl = z0 + dir * (STAIR + 0.02f);  // onder de trap naar boven: dicht tot de bovendekvloer
        op.box({sx * 0.70f, (LOW_Y + UP_Y) / 2, zl}, {1.30f, UP_Y - LOW_Y, 0.05f}, DARKPANEL, MAT_PAINT);
        op.box({sx * 0.70f, LOW_Y + 1.55f, zl + dir * 0.04f}, {0.50f, 0.28f, 0.03f}, Vec3(0.20f, 0.35f, 0.55f), MAT_LIGHT);
        // wand boven het balkon tot het dak (achterkant trappartij)
        op.box({0, (BAL_CEIL + UP_CEIL) / 2, z0 - dir * 0.03f}, {2 * outerX(3.6f) - 0.2f, UP_CEIL - BAL_CEIL, 0.05f}, CREAM2, MAT_PAINT);
    }
    // stoelen (beide dekken), coupés per raam: rij naar voren en naar achteren (vis-à-vis), tafeltje bij het raam
    auto addSeat = [&](float x, float floor, float z, float dir, bool first, bool aisleRight) {
        Vec3 fab = first ? SEAT1 : SEAT2;
        seat(op, x, floor, z, dir, fab, first, aisleRight);
        SeatInfo si;
        si.car = i;
        si.lx = x;
        si.lz = z;
        si.facing = dir > 0 ? 0.0f : PI;
        si.y = floor;
        seats.push_back(si);
    };
    for (int deck = 0; deck < 2; deck++) {
        float floor = deck ? UP_Y : LOW_Y;
        bool first = deck == 1 && c.firstUp;
        float z0 = a + STAIR + 0.35f, z1 = b - STAIR - 0.35f;
        for (int k = 0; k < nw; k++) {
            float zc = w0 + k * WIN_PITCH;
            for (int r = -1; r <= 1; r += 2) {
                float z = zc + r * 0.47f;
                if (z < z0 || z > z1) continue;
                float dir = (float)-r;  // naar het raammidden toe gekeerd (vis-à-vis)
                if (first) {
                    addSeat(-0.93f, floor, z, dir, true, true);
                    addSeat(0.50f, floor, z, dir, true, false);
                    addSeat(0.99f, floor, z, dir, true, false);
                } else {
                    float wx = deck ? 1.00f : 1.04f;
                    addSeat(-wx, floor, z, dir, false, true);
                    addSeat(-0.55f, floor, z, dir, false, true);
                    addSeat(0.55f, floor, z, dir, false, false);
                    addSeat(wx, floor, z, dir, false, false);
                }
            }
            // tafeltjes bij het raam
            for (int s = -1; s <= 1; s += 2) {
                float x = s * (outerX(floor + 0.75f) - WALL - 0.20f);
                op.tube({x, floor + 0.74f, zc}, {x, floor + 0.77f, zc}, 0.17f, 16, Vec3(0.86f, 0.86f, 0.85f), MAT_PAINT);
            }
        }
    }
    // balkon: gele stangen bij de deuren, toilet, infoscherm boven de deur (apart getekend)
    for (float d : doorC_[i])
        for (int s = -1; s <= 1; s += 2)
            for (float off : {-0.80f, 0.80f}) op.tube({s * 0.95f, BAL_Y, d + off}, {s * 0.95f, BAL_CEIL, d + off}, 0.02f, 6, YELLOW, MAT_METAL);
    if (c.toilet) {
        // toiletdeur in de kopwand van het benedendek, onder de trap naar boven
        float zt = a + STAIR + 0.05f;
        op.box({0.70f, LOW_Y + 0.98f, zt}, {0.78f, 1.95f, 0.03f}, Vec3(0.55f, 0.56f, 0.58f), MAT_PAINT);
        op.box({0.38f, LOW_Y + 0.95f, zt + 0.03f}, {0.03f, 0.18f, 0.05f}, ALU, MAT_METAL);
        SignRect wc = atlas.add("WC", Vec3(0.12f, 0.12f, 0.13f), Vec3(1, 1, 1), 48, true, 0.4f);
        SignAtlas::quad(op, wc, Vec3(0.70f, LOW_Y + 2.02f, zt + 0.03f), Vec3(-1, 0, 0), 0.10f, true);
    }
    if (c.cab) {
        // cabinewand met deur en de cabine zelf
        cabin::Params cp;
        cp.c = this->cab;
        cp.halfW = 1.30f;
        cp.ceilY = 3.28f;
        cp.frontZ = zEnd + noseZ(2.06f) - 0.12f;
        cp.noseAt = noseZ;
        cp.noseHalfAt = noseHalfW;
        cp.noseBase = zEnd;
        cp.deskTopY = 2.03f;
        cp.glassY = 2.06f;
        cp.sideWin0 = 2.10f;
        cp.sideWin1 = 2.95f;
        cp.wall = CREAM2;
        cp.desk = Vec3(0.20f, 0.21f, 0.23f);
        cp.floor = Vec3(0.17f, 0.17f, 0.18f);
        cp.seat = Vec3(0.12f, 0.12f, 0.14f);
        cabin::build(op, gl, cp);
    }
    // interieur: egaal kunstlicht (behalve lampen en glas)
    for (size_t k = interiorStart; k < op.v.size(); k++)
        if (op.v[k].extra != MAT_LIGHT && op.v[k].extra != MAT_GLASS && op.v[k].extra > -0.5f && op.v[k].extra < 4.5f) op.v[k].extra = MAT_INTERIOR;
    // buitenkant en interieur als aparte meshes (interieur alleen van dichtbij tekenen)
    {
        MeshBuilder ext, inn;
        ext.v.assign(op.v.begin(), op.v.begin() + interiorStart);
        ext.i.assign(op.i.begin(), op.i.begin() + interiorIdx);
        inn.v.assign(op.v.begin() + interiorStart, op.v.end());
        for (size_t k = interiorIdx; k < op.i.size(); k++) inn.i.push_back(op.i[k] - (uint32_t)interiorStart);
        car_[i] = ext.upload();
        carInt_[i] = inn.upload();
        vertCount_ += op.v.size() + gl.v.size();
    }
    carGlass_[i] = gl.upload();

    // ---- deurbladen (draai-schuifdeuren): geel met smal raam per blad ----
    for (int s = 0; s < 2; s++)
        for (int leaf = 0; leaf < 2; leaf++) {
            MeshBuilder dm;
            float side = s ? 1.0f : -1.0f;
            float zc = (leaf ? 1.0f : -1.0f) * DOOR_W / 4;
            for (float d : doorC_[i]) {
                float z = d + zc, x = side * (HW + 0.005f);
                float w = DOOR_W / 2 - 0.015f;
                dm.box({x, (BAL_Y + 1.65f) / 2, z}, {0.05f, 1.65f - BAL_Y, w}, YELLOW, MAT_PAINT);
                dm.box({x, (2.80f + DOOR_TOP) / 2, z}, {0.05f, DOOR_TOP - 2.80f, w}, YELLOW, MAT_PAINT);
                float wz = z + (leaf ? -1.0f : 1.0f) * 0.10f;  // raam naar het midden van de deur
                for (float e : {-1.0f, 1.0f}) dm.box({x, 2.22f, (wz + e * 0.13f + z + e * w / 2) / 2}, {0.05f, 1.16f, std::fabs(z + e * w / 2 - (wz + e * 0.13f))}, YELLOW, MAT_PAINT);
                dm.box({x + side * 0.004f, 2.22f, wz}, {0.045f, 1.12f, 0.24f}, Vec3(0.03f, 0.035f, 0.04f), MAT_GLASS);
                dm.box({x + side * 0.03f, 2.22f, wz}, {0.01f, 1.18f, 0.30f}, RUBBER, MAT_RUBBER);
                dm.box({x + side * 0.03f, 1.10f + BAL_Y * 0.0f + 0.62f, z + (leaf ? -1.0f : 1.0f) * (w / 2 - 0.02f)}, {0.012f, 1.0f, 0.02f}, RUBBER, MAT_RUBBER);  // rubber sluitrand
                if (leaf) dm.box({x + side * 0.03f, 1.75f, z - w / 2 + 0.12f}, {0.02f, 0.08f, 0.08f}, Vec3(0.1f, 0.85f, 0.25f), MAT_LIGHT);  // drukknop
            }
            leaf_[i * 4 + s * 2 + leaf] = dm.upload();
        }
}

void VirmStock::build(SignAtlas& atlas, const std::vector<std::string>& names) {
    if (!tsModel.empty()) buildTsSigns(atlas, names);  // bestemmingsborden op het Train Simulator-model
    car_.resize(cars);
    carInt_.resize(cars);
    carGlass_.resize(cars);
    leaf_.resize(cars * 4);
    seats.clear();
    for (int i = 0; i < cars; i++) {
        size_t before = seats.size();
        buildCar(i, atlas);
        // stoelen van het laatste rijtuig: van canoniek naar lokaal (gespiegeld)
        if (cabSign(i) < 0)
            for (size_t k = before; k < seats.size(); k++) {
                seats[k].lx = -seats[k].lx;
                seats[k].lz = -seats[k].lz;
                seats[k].facing = wrapAngle(seats[k].facing + PI);
            }
    }
    {
        // tussendeur: grijs met groot raam en aan beide kanten een hendel (naar links: deur gaat open)
        MeshBuilder d;
        const float W = 0.86f, Hd = 2.05f, y0 = BAL_Y;
        d.box({0, y0 + 0.40f, 0}, {W, 0.80f, 0.04f}, Vec3(0.55f, 0.56f, 0.58f), MAT_PAINT);
        d.box({0, y0 + Hd - 0.10f, 0}, {W, 0.20f, 0.04f}, Vec3(0.55f, 0.56f, 0.58f), MAT_PAINT);
        for (int s = -1; s <= 1; s += 2) d.box({s * (W / 2 - 0.06f), y0 + 1.37f, 0}, {0.12f, 1.15f, 0.04f}, Vec3(0.55f, 0.56f, 0.58f), MAT_PAINT);
        d.box({0, y0 + 1.37f, 0}, {W - 0.24f, 1.13f, 0.02f}, Vec3(0.10f, 0.12f, 0.13f), MAT_GLASS);
        for (int f = -1; f <= 1; f += 2) {
            d.box({-0.30f, y0 + 1.05f, f * 0.05f}, {0.03f, 0.22f, 0.03f}, ALU, MAT_METAL);           // hendel
            d.box({-0.30f, y0 + 1.16f, f * 0.035f}, {0.05f, 0.03f, 0.04f}, ALU, MAT_METAL);
            d.box({-0.30f, y0 + 1.35f, f * 0.025f}, {0.10f, 0.10f, 0.01f}, Vec3(0.1f, 0.6f, 0.2f), MAT_LIGHT);  // groen lampje 'open'
        }
        gangLeaf_ = d.upload();
    }
    cabDoor_ = cabin::doorLeaf(cab.doorW - 0.02f, 2.0f, Vec3(0.45f, 0.45f, 0.48f));
    lever_ = cabin::lever();
    { MeshBuilder m; bogie(m, false); trailer_ = m.upload(); }
    { MeshBuilder m; bogie(m, true); motor_ = m.upload(); }
    {
        // eenarmige pantograaf (neergelaten stand is niet nodig: altijd op)
        MeshBuilder b;
        float y0 = H + 0.10f;
        b.box({0, y0, 0}, {1.3f, 0.10f, 1.5f}, STEEL * 0.7f, MAT_METAL);
        for (int s = -1; s <= 1; s += 2) b.tube({s * 0.5f, y0, 0.55f}, {s * 0.5f, y0 + 0.22f, 0.55f}, 0.07f, 8, Vec3(0.55f, 0.30f, 0.20f), MAT_PAINT);
        Vec3 hinge(0, y0 + 0.25f, 0.55f), knee(0, y0 + 0.95f, -0.85f), head(0, y0 + 1.55f, 0.10f);
        for (int s = -1; s <= 1; s += 2) b.tube(hinge + Vec3(s * 0.25f, 0, 0), knee + Vec3(s * 0.05f, 0, 0), 0.04f, 6, STEEL, MAT_METAL);
        b.tube(knee, head, 0.03f, 6, STEEL, MAT_METAL);
        b.tube(hinge, knee + Vec3(0, -0.15f, 0.15f), 0.015f, 5, STEEL * 0.8f, MAT_METAL);
        for (float dz : {-0.12f, 0.12f}) b.box(head + Vec3(0, 0.03f, dz), {1.95f, 0.04f, 0.06f}, Vec3(0.25f, 0.25f, 0.27f), MAT_METAL);
        for (int s = -1; s <= 1; s += 2) b.tube(head + Vec3(s * 0.97f, 0.03f, -0.12f), head + Vec3(s * 1.05f, -0.05f, 0.12f), 0.02f, 5, STEEL, MAT_METAL);
        panto_ = b.upload();
    }
    {
        // overgang tussen de rijtuigen: rubberen balg rond de doorloop
        MeshBuilder g;
        const float G = gap;
        for (int k = 0; k < 6; k++) {
            float z = -G / 2 + G * (k + 0.5f) / 6, w = 1.75f - (k % 2) * 0.05f, h = 2.30f - (k % 2) * 0.05f, t = 0.10f;
            Vec3 col = k % 2 ? RUBBER * 2.0f : RUBBER;
            float yc = BAL_Y + h / 2 - 0.05f;
            g.box({0, yc + h / 2 - t / 2, z}, {w, t, G / 6}, col, MAT_RUBBER);
            g.box({-w / 2 + t / 2, yc, z}, {t, h, G / 6}, col, MAT_RUBBER);
            g.box({w / 2 - t / 2, yc, z}, {t, h, G / 6}, col, MAT_RUBBER);
        }
        g.box({0, BAL_Y - 0.02f, 0}, {1.2f, 0.04f, G + 0.3f}, FLOORC, MAT_RUBBER);
        g.box({0, 0.85f, 0}, {0.30f, 0.25f, G + 0.4f}, BOGIEC, MAT_METAL);  // koppeling
        gangway_ = g.upload();
    }
    {
        // brandende lampen (los getekend, afhankelijk van de rijrichting): wit voorop, rood achteraan
        MeshBuilder hl, tl;
        float zN = spec_[0].len / 2 - NOSE;
        for (int s = -1; s <= 1; s += 2) {
            float y = 1.62f;
            hl.box({s * 0.82f, y, zN + noseZ(y) + 0.01f}, {0.17f, 0.11f, 0.03f}, Vec3(1.6f, 1.6f, 1.45f), MAT_LIGHT);
            tl.box({s * 0.50f, y, zN + noseZ(y) + 0.01f}, {0.17f, 0.11f, 0.03f}, Vec3(1.2f, 0.05f, 0.03f), MAT_LIGHT);
        }
        hl.box({0, 1.86f, zN + noseZ(1.86f) + 0.02f}, {0.13f, 0.08f, 0.03f}, Vec3(1.6f, 1.6f, 1.45f), MAT_LIGHT);
        headlight_ = hl.upload();
        taillight_ = tl.upload();
    }
    // frontdisplay (bestemming, oranje LED op zwart) en infoschermen op de balkons
    display_.resize(names.size());
    screen_.resize(names.size());
    float zN = spec_[0].len / 2 - NOSE;
    for (size_t t = 0; t < names.size(); t++) {
        {
            MeshBuilder m;
            SignRect r = atlas.add(names[t], Vec3(0.01f, 0.01f, 0.01f), Vec3(1.0f, 0.62f, 0.12f), 64, true, 0.3f);
            float y0 = 3.12f, y1 = 3.30f;
            float h = y1 - y0, w = std::min(1.5f, h * r.aspect * 1.3f);
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
            SignAtlas::quad(m, r, Vec3(0, BAL_CEIL - 0.22f, 0), Vec3(1, 0, 0), 0.16f, true);
            screen_[t] = m.upload();
        }
    }
}

// ---------- tekenen ----------
void VirmStock::drawOpaque(World& world, const StockDraw& sd) const {
    const Vehicle& v = *sd.v;
    world.setMode(MODE_VEHICLE);
    for (int i = 0; i < cars; i++) {
        CarPose c = v.carPose(i);
        int cs = cabSign(i);
        Mat4 m = Mat4::pose(c.x, c.y, c.z, cs < 0 ? c.h + PI : c.h);
        world.setModel(m);
        drawMesh(car_[i]);
        if (std::hypot(c.x - world.camPos().x, c.z - world.camPos().z) < world.nearDistance) drawMesh(carInt_[i]);  // interieur alleen dichtbij
        for (int s = 0; s < 2; s++)
            for (int leaf = 0; leaf < 2; leaf++) {
                int meshSide = s ? 1 : -1;
                int worldSide = cs < 0 ? -meshSide : meshSide;
                float open = worldSide == sd.doorSide ? sd.doorOpen : 0;
                // draai-schuifdeur: eerst naar buiten (12 cm), dan opzij
                float out = std::min(1.0f, open * 4.0f) * 0.12f;
                float slide = clampf((open - 0.15f) / 0.85f, 0, 1);
                float off = (leaf ? 1.0f : -1.0f) * (DOOR_W / 2 - 0.02f) * slide;
                world.setModel(m * Mat4::translation(meshSide * out, 0, off));
                drawMesh(leaf_[i * 4 + s * 2 + leaf]);
            }
        const CarSpec& sp = spec_[i];
        float L = sp.len;
        // tussendeuren in de kopwanden (schuiven opzij de wand in)
        for (int sc = -1; sc <= 1; sc += 2) {
            if (sp.cab && sc > 0) continue;
            int sl = cs < 0 ? -sc : sc;           // lokale kant
            int g = sl < 0 ? i : i - 1;           // doorloop
            if (g < 0 || g >= cars - 1) continue;
            float open = sd.gang ? sd.gang[g] : 0.0f;
            world.setModel(m * Mat4::translation(open * 0.84f, 0, sc * (L / 2 - 0.07f)));
            drawMesh(gangLeaf_);
        }
        float zf = sp.cab ? L / 2 - 3.6f : L / 2 - 2.4f, zr = -L / 2 + 2.4f;
        for (int e = -1; e <= 1; e += 2) {
            bool motor = sp.motorEnd == e;
            world.setModel(m * Mat4::translation(0, 0, e > 0 ? zf : zr));
            drawMesh(motor ? motor_ : trailer_);
        }
        if (sp.panto) {
            world.setModel(m * Mat4::translation(0, 0, dd0(i) + 2.5f));
            drawMesh(panto_);
        }
        if (cs) {
            // koplampen voorop, sluitseinen achteraan
            bool leading = (cs > 0) == (sd.lead == 0);
            world.setModel(m);
            drawMesh(leading ? headlight_ : taillight_);
            if (std::hypot(c.x - world.camPos().x, c.z - world.camPos().z) < 120) {
                float open = sd.cabDoor[cs > 0 ? 0 : 1];
                world.setModel(m * Mat4::translation(cab.doorX - cab.doorW / 2 + 0.01f, std::min(cab.floorY, cab.passFloor), cab.wallZ) * Mat4::rotY(-open * 1.45f));
                drawMesh(cabDoor_);
                world.setModel(m * Mat4::translation(cab.leverX, cab.leverY, cab.leverZ) * Mat4::rotX((leading ? sd.lever : -0.5f) * 0.6f));
                drawMesh(lever_);
            }
        }
        if (i + 1 < cars) {
            CarPose n = v.carPose(i + 1);
            Vec3 a = v.carToWorld(i, 0, 0, -spec_[i].len / 2), b = v.carToWorld(i + 1, 0, 0, spec_[i + 1].len / 2);
            world.setModel(Mat4::pose((a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2, std::atan2(c.x - n.x, c.z - n.z)));
            drawMesh(gangway_);
        }
    }
    world.setMode(MODE_OBJECT);
    for (int i = 0; i < cars; i++) {
        CarPose c = v.carPose(i);
        int cs = cabSign(i);
        if (cs && sd.dest >= 0 && sd.dest < (int)display_.size()) {
            // niet vanuit de cabine zelf (dan zie je de achterkant in spiegelbeeld)
            Vec3 cabC = v.carToWorld(i, 0, 2.5f, cs * (len[i] / 2 - 1.6f)), cam = world.camPos();
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

void VirmStock::drawGlass(World& world, const StockDraw& sd) const {
    for (int i = 0; i < cars; i++) {
        CarPose c = sd.v->carPose(i);
        int cs = cabSign(i);
        world.setModel(Mat4::pose(c.x, c.y, c.z, cs < 0 ? c.h + PI : c.h));
        drawMesh(carGlass_[i]);
    }
    world.setModel(Mat4::identity());
}
