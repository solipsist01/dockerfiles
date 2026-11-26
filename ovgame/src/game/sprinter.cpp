// Generieke NS-Sprinter (lage vloer, Jacobs-draaistellen): SNG (CAF Civity) en SLT (Sprinter Lighttrain).
// Maten, raamindeling en livrei uit mijn eerdere modellen (Mods/mtr-sng-hd, Mods/mtr-slt-hd: gemeten van vrije foto's
// op Wikimedia Commons), hier omgezet naar spoorstaafhoogte (+0,80 m) en canonieke richting (cabine aan +z).
#include "sprinter.h"
#include <algorithm>
#include <cmath>
#include "../engine/meshbuilder.h"
#include "cabin.h"

using namespace WorldFormat;

namespace {
const Vec3 BLACK(0.04f, 0.04f, 0.045f), RUBBER(0.05f, 0.05f, 0.055f), GLASSC(0.16f, 0.20f, 0.22f), STEEL(0.42f, 0.43f, 0.45f);
const Vec3 ALU(0.72f, 0.73f, 0.75f), BOGIEC(0.13f, 0.13f, 0.14f), LIGHTC(1.3f, 1.27f, 1.18f), SHELL(0.30f, 0.31f, 0.33f);
const Vec3 POLE(0.98f, 0.76f, 0.10f), SKIRTC(0.22f, 0.24f, 0.28f);
const float WALL = 0.08f;

Vec3 rgb(int r, int g, int b) { return Vec3(r / 255.0f, g / 255.0f, b / 255.0f); }

float lerpTab(const std::vector<float>& xs, const std::vector<float>& ys, float x) {
    if (x <= xs.front()) return ys.front();
    for (size_t i = 0; i + 1 < xs.size(); i++)
        if (x <= xs[i + 1]) return ys[i] + (ys[i + 1] - ys[i]) * (x - xs[i]) / (xs[i + 1] - xs[i]);
    return ys.back();
}
float lerpPairs(const std::vector<std::pair<float, float>>& p, float x) {
    if (x <= p.front().first) return p.front().second;
    for (size_t i = 0; i + 1 < p.size(); i++)
        if (x <= p[i + 1].first) return p[i].second + (p[i + 1].second - p[i].second) * (x - p[i].first) / (p[i + 1].first - p[i].first);
    return p.back().second;
}

// ===================================== SNG =====================================
// CAF Civity "Sprinter Nieuwe Generatie", gele kop: lichtblauwe bak met een witte lijn boven de ramen, witte band en gele
// streep onderlangs, deuren in een blauw kader. Kop: gele kap, witte band met de lampen, blauwe wig naar voren,
// grote voorruit, donkere bumper met de koppeling.
const Vec3 SNG_WHITE = rgb(240, 242, 242), SNG_BLUE = rgb(30, 108, 204), SNG_YEL = rgb(255, 194, 20), SNG_ROOF = rgb(150, 156, 162),
           SNG_SKIRT = rgb(62, 68, 78);
const float SF = 0.80f;  // vloer boven de spoorstaaf (de mtr-modellen hebben y = 0 op de vloer)

Vec3 sngSide(const SprinterSpec::Car& car, float z, float y, float& mat) {
    mat = MAT_PAINT;
    float yl = y - SF;  // hoogte boven de vloer
    if (yl < -0.20f) return SNG_SKIRT;
    for (float d : car.doors)
        if (std::fabs(z - d) < 1.30f / 2 + 0.09f && yl < 2.08f + 0.10f) return SNG_BLUE;  // deur in een blauw kader
    if (car.cab) {
        float zb = car.len / 2 - 2.4f;  // begin neus (canoniek)
        // achter de neus: wit met de gele kap erboven en de blauwe wig
        float fromNose = zb - z;                              // 0 bij de neus, positief naar achteren
        if (fromNose < 2.0f) {
            if (yl > 2.38f - 0.0f && fromNose < 1.4f + (yl - 2.38f) * 1.6f) return SNG_YEL;  // gele kap, schuin afgesneden
            // blauwe wig: van de volle band bij de cabinedeur naar een punt op de neus
            float f = std::min(1.0f, std::max(0.0f, fromNose / 1.9f));
            float lo = 0.66f + (0.42f - 0.66f) * f, hi = 1.52f + (2.30f - 1.52f) * f;
            if (yl > lo && yl < hi) return SNG_BLUE;
            if (yl < 0.14f + 0.16f + (-0.20f - 0.30f) * std::min(1.0f, fromNose / 2.0f)) return SNG_SKIRT;
            return SNG_WHITE;
        }
    }
    if (yl < 0.02f) return SNG_YEL;
    if (yl < 0.42f) return SNG_WHITE;
    if (yl < 2.30f) return SNG_BLUE;
    if (yl < 2.40f) return SNG_WHITE;
    if (yl < 2.80f) return SNG_BLUE;
    return SNG_ROOF;
}

Vec3 sngFront(float y, float a, float s, float& mat) {
    mat = MAT_PAINT;
    float yl = y - SF;
    // voorruit: vlakke V aan de onderkant, ronde hoeken
    float hw = 1.08f + (0.86f - 1.08f) * std::min(1.0f, std::max(0.0f, (yl - 1.22f) / (2.92f - 1.22f)));
    float bottom = 1.22f + 0.05f * std::min(1.0f, a / 1.08f);
    if (yl > bottom && yl < 2.92f && a < hw) { mat = MAT_GLASS; return Vec3(0.03f, 0.035f, 0.04f); }
    if (yl >= 2.92f) return SNG_YEL;                                   // gele kap
    if (yl > 2.38f && s > 0.55f) return SNG_YEL;
    if (yl < -0.20f) return SNG_SKIRT;
    if (yl < 0.14f) return std::fabs(a) < 0.55f && yl > -0.70f ? Vec3(0.10f, 0.10f, 0.11f) : SNG_SKIRT;  // bumper + koppelingsopening
    // blauwe wig die van de zijkant naar de lampen loopt
    if (s > 0.35f) {
        float f = (s - 0.35f) / 0.65f;
        float lo = 0.66f + (0.42f - 0.66f) * f * 0.3f, hi = 1.12f + (1.52f - 1.12f) * f;
        if (yl > lo && yl < hi) return SNG_BLUE;
    }
    if (yl > 0.80f && yl < 1.22f) return Vec3(0.12f, 0.13f, 0.15f);    // zwarte band rond de lampen onder de ruit
    return SNG_WHITE;
}

// ===================================== SLT =====================================
// Sprinter Lighttrain: witte bak met een blauwe raamband, gele streep onderlangs, donkere schort. Kop: de band eindigt in
// een ronde "bel" om de machinistendeur, een blauwe "hoorn" langs de dakrand, witte neus met de lampen in een blauwe U,
// donkere schort die naar de neus oploopt.
const Vec3 SLT_WHITE = rgb(236, 238, 236), SLT_BLUE = rgb(14, 50, 138), SLT_YEL = rgb(250, 195, 24), SLT_ROOF = rgb(134, 140, 146),
           SLT_SKIRT = rgb(54, 58, 66);

Vec3 sltSide(const SprinterSpec::Car& car, float z, float y, float& mat) {
    mat = MAT_PAINT;
    float yl = y - SF;
    if (car.cab) {
        float zb = car.len / 2 - 2.0f;           // begin neus
        const float zd = 6.42f;                  // midden van de "bel" om de machinistendeur (canoniek)
        if (z > zb - 4.0f) {
            // de bel: superellips om de machinistendeur
            float dz = (z - zd) / 0.98f, dy = (yl - 1.165f) / 0.915f;
            bool bubble = std::pow(std::fabs(dz), 2.6f) + std::pow(std::fabs(dy), 2.6f) < 1.0f;
            if (bubble || (z < zd && yl > 0.68f && yl < 2.08f)) return SLT_BLUE;
            // hoorn langs de dakrand, van de voorruitstijl naar achteren
            if (yl > 2.62f - 0.05f && yl < 3.0f && z > zb - 3.7f) return SLT_BLUE;
            if (yl > 3.0f) return Vec3(0.25f, 0.27f, 0.30f);       // donkere kap boven de cabine
            float dark = -0.22f + (0.50f + 0.22f) * std::pow(std::min(1.0f, std::max(0.0f, (z - (zb - 2.6f)) / 2.6f)), 1.4f);
            if (yl < dark) return SLT_SKIRT;
            if (yl < 0.08f && z < zb - 2.6f) return SLT_YEL;
            return SLT_WHITE;
        }
    }
    if (yl < -0.22f) return SLT_SKIRT;
    if (yl < 0.08f) return SLT_YEL;
    if (yl > 0.68f && yl < 2.08f) return SLT_BLUE;
    if (yl > 2.62f) return SLT_ROOF;
    return SLT_WHITE;
}

Vec3 sltFront(float y, float a, float s, float& mat) {
    mat = MAT_PAINT;
    float yl = y - SF;
    float hw = 0.40f * 1.62f, bottom = 1.48f;
    if (yl > bottom && yl < 2.93f && a < hw + 0.12f * (yl - 1.48f)) { mat = MAT_GLASS; return Vec3(0.03f, 0.035f, 0.04f); }
    if (yl >= 2.93f) return s > 0.45f ? SLT_BLUE : Vec3(0.25f, 0.27f, 0.30f);  // donkere kap, blauwe hoorn opzij
    float valance = lerpTab({0.0f, 0.35f, 0.6f, 0.85f, 1.0f}, {0.50f, 0.48f, 0.36f, 0.14f, 0.0f}, s);
    if (yl < valance) return SLT_SKIRT;
    // blauwe U onder de lampen
    if (yl < 0.99f && yl > 0.79f) return SLT_BLUE;
    if (yl < 1.48f && s > 0.25f && yl > 0.79f && a > 0.45f) return SLT_BLUE;
    return SLT_WHITE;
}

// ===================================== ICM =====================================
// Intercitymaterieel "Koploper" (ICMm, gemoderniseerd): gele bak met een blauwe band onderlangs tot de raamonderkant,
// zwart omlijste ramen, grijs dak. Kop: lange vooruitstekende neus met in het midden de doorloopdeur (geel, zwart
// rubberen kader), de cabine hoog erboven met een brede, schuin naar achteren lopende voorruit in een zwart masker,
// koplampen op de hoeken onder in de neus, blauwe band rond de onderkant.
const Vec3 ICM_YEL = rgb(255, 200, 24), ICM_BLUE = rgb(0, 48, 130), ICM_ROOF = rgb(140, 145, 150), ICM_SKIRT = rgb(48, 50, 56);
const float ICM_FY = 1.20f;

Vec3 icmSide(const SprinterSpec::Car& car, float z, float y, float& mat) {
    mat = MAT_PAINT;
    (void)car; (void)z;
    if (y < 0.85f) return ICM_SKIRT;
    if (y < 1.92f) return ICM_BLUE;                 // blauwe band tot onder de ramen
    if (y < 1.98f) return rgb(230, 232, 232);       // dunne witte bies
    if (y > 3.62f) return ICM_ROOF;
    return ICM_YEL;
}

Vec3 icmFront(float y, float a, float s, float& mat) {
    mat = MAT_PAINT;
    // voorruit: breed, loopt om de hoeken door
    if (y > 2.78f && y < 3.52f && a < 1.02f + (y - 2.78f) * 0.15f) { mat = MAT_GLASS; return Vec3(0.03f, 0.035f, 0.04f); }
    if (y > 2.70f && y < 3.60f && a < 1.18f) return BLACK;            // zwart masker rond de ruit
    // doorloopdeur in het midden: geel blad met een zwart rubberen kader en een raampje
    if (a < 0.47f && y > 1.25f && y < 2.66f) {
        if (a > 0.42f || y < 1.30f || y > 2.61f) return Vec3(0.06f, 0.06f, 0.06f);
        if (a < 0.24f && y > 2.05f && y < 2.50f) { mat = MAT_GLASS; return Vec3(0.05f, 0.06f, 0.07f); }
        return ICM_YEL * 0.96f;
    }
    if (y < 0.85f) return ICM_SKIRT;
    if (y < 1.25f) return ICM_BLUE;                 // blauwe band rond de onderkant van de neus
    if (y > 3.62f && s > 0.6f) return ICM_ROOF;
    return ICM_YEL;
}

// ===================================== gedeelde bouwstenen =====================================
struct Prof {
    const SprinterSpec* S;
    float outerX(float y) const { return std::max(0.0f, lerpTab(S->py, S->px, y)); }
    float slope(float y) const { return (outerX(y + 0.01f) - outerX(y - 0.01f)) / 0.02f; }
    Vec3 P(float s, float z, float y, float inset) const { return Vec3(s * (outerX(y) - inset), y, z); }
    Vec3 N(float s, float y) const { return normalize(Vec3(s * 1.0f, -slope(y), 0)); }
};

struct Hole { float z0, z1, y0, y1, r; bool window; };
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

// zijwand (beide kanten) met openingen; liv: kleurfunctie (buitenkant), anders binnenkleur 'in'
void shell(MeshBuilder& op, MeshBuilder& gl, const Prof& pr, const SprinterSpec::Car* car, float z0, float z1, float yBot, float yTop,
           const std::vector<Hole>& holes, Vec3 in, float inset, bool outside) {
    const SprinterSpec& S = *pr.S;
    std::vector<float> zs{z0, z1}, ys{yBot, yTop};
    for (float z = z0; z < z1; z += 0.30f) zs.push_back(z);
    if (outside && car && car->cab)
        for (float z = car->len / 2 - 6.5f; z < z1; z += 0.08f) if (z > z0) zs.push_back(z);  // fijner bij de kop (livrei)
    for (size_t i = 0; i < S.py.size(); i++) if (S.py[i] > yBot && S.py[i] < yTop) ys.push_back(S.py[i]);
    for (float y = yBot; y < yTop; y += outside && car && car->cab ? 0.08f : 0.25f) ys.push_back(y);
    if (outside) for (float y : S.bandYs) if (y > yBot && y < yTop) ys.push_back(y);
    for (const Hole& h : holes) {
        if (h.z0 > z0 && h.z0 < z1) zs.push_back(h.z0);
        if (h.z1 > z0 && h.z1 < z1) zs.push_back(h.z1);
        if (h.y0 > yBot && h.y0 < yTop) ys.push_back(h.y0);
        if (h.y1 > yBot && h.y1 < yTop) ys.push_back(h.y1);
    }
    if (outside && car)
        for (float d : car->doors) for (float e : {-1.0f, 1.0f}) { zs.push_back(d + e * (S.doorW / 2 + 0.09f)); zs.push_back(d + e * S.doorW / 2); }
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
            Vec3 c = outside ? S.side(*car, zc, yc, mat) : in;
            for (int s = -1; s <= 1; s += 2) {
                float fs = (float)s;
                Vec3 a = pr.P(fs, za, ya, inset), b = pr.P(fs, zb, ya, inset), cc = pr.P(fs, zb, yb, inset), d = pr.P(fs, za, yb, inset);
                Vec3 na = pr.N(fs, ya), nb = pr.N(fs, yb);
                if (!outside) { na = na * -1.0f; nb = nb * -1.0f; }
                op.quad(a, b, cc, d, na, na, nb, nb, c, mat);
            }
        }
    }
    // ramen: omlijsting (rubber), dagkant en vlak glas
    for (const Hole& h : holes) {
        if (h.z0 < z0 - 1e-3f || h.z1 > z1 + 1e-3f) continue;
        auto rr = roundRect(h);
        const int seg = (int)rr.size() / 4;
        const float cz[4] = {h.z1, h.z1, h.z0, h.z0}, cy[4] = {h.y0, h.y1, h.y1, h.y0};
        for (int s = -1; s <= 1; s += 2) {
            float fs = (float)s;
            for (int c = 0; c < 4; c++) {
                float mat = MAT_PAINT;
                Vec3 col = outside ? (h.window ? RUBBER : S.side(*car, cz[c], cy[c], mat)) : in;
                Vec3 C = pr.P(fs, cz[c], cy[c], inset);
                for (int k = 0; k < seg - 1; k++) {
                    auto p0 = rr[c * seg + k], p1 = rr[c * seg + k + 1];
                    Vec3 n = pr.N(fs, cy[c]) * (outside ? 1.0f : -1.0f);
                    uint32_t i0 = op.vert(C, n, col, mat), i1 = op.vert(pr.P(fs, p0.first, p0.second, inset), n, col, mat),
                             i2 = op.vert(pr.P(fs, p1.first, p1.second, inset), n, col, mat);
                    op.i.insert(op.i.end(), {i0, i1, i2});
                }
            }
            if (!h.window || !outside) continue;
            for (size_t k = 0; k < rr.size(); k++) {
                auto p0 = rr[k], p1 = rr[(k + 1) % rr.size()];
                Vec3 A = pr.P(fs, p0.first, p0.second, 0), Bq = pr.P(fs, p1.first, p1.second, 0);
                Vec3 nA = pr.N(fs, p0.second), nB = pr.N(fs, p1.second);
                Vec3 A2 = A - nA * WALL, B2 = Bq - nB * WALL;
                op.quad(A, Bq, B2, A2, normalize(cross(Bq - A, nA)), RUBBER, MAT_RUBBER);
            }
            float zc = (h.z0 + h.z1) / 2, yc = (h.y0 + h.y1) / 2;
            Vec3 n = pr.N(fs, yc);
            uint32_t c0 = gl.vert(pr.P(fs, zc, yc, 0.015f), n, GLASSC, EX_GLASS);
            std::vector<uint32_t> ring;
            for (auto& p : rr) ring.push_back(gl.vert(pr.P(fs, p.first, p.second, 0.015f), n, GLASSC, EX_GLASS));
            for (size_t k = 0; k < ring.size(); k++) gl.i.insert(gl.i.end(), {c0, ring[k], ring[(k + 1) % ring.size()]});
        }
    }
}

// kopwand (dubbelzijdig) met doorloopopening
void endFrame(MeshBuilder& op, const Prof& pr, float z, float y0, float y1, Vec3 c, float shrink, float dx, float dy0, float dy1) {
    std::vector<float> ys;
    for (float y = y0; y < y1; y += 0.15f) ys.push_back(y);
    ys.push_back(y1);
    if (dy0 > y0 && dy0 < y1) ys.push_back(dy0);
    if (dy1 > y0 && dy1 < y1) ys.push_back(dy1);
    std::sort(ys.begin(), ys.end());
    for (size_t k = 0; k + 1 < ys.size(); k++) {
        float ya = ys[k], yb = ys[k + 1];
        if (yb - ya < 1e-3f) continue;
        float xa = std::max(0.0f, pr.outerX(ya) - shrink), xb = std::max(0.0f, pr.outerX(yb) - shrink);
        if ((ya + yb) / 2 > dy0 && (ya + yb) / 2 < dy1) {
            op.quad(Vec3(-xa, ya, z), Vec3(-dx, ya, z), Vec3(-dx, yb, z), Vec3(-xb, yb, z), Vec3(0, 0, 1), c, MAT_PAINT);
            op.quad(Vec3(dx, ya, z), Vec3(xa, ya, z), Vec3(xb, yb, z), Vec3(dx, yb, z), Vec3(0, 0, 1), c, MAT_PAINT);
        } else {
            op.quad(Vec3(-xa, ya, z), Vec3(xa, ya, z), Vec3(xb, yb, z), Vec3(-xb, yb, z), Vec3(0, 0, 1), c, MAT_PAINT);
        }
    }
}

// stoel; dir = kijkrichting langs z
void seat(MeshBuilder& op, float x, float floor, float z, float dir, Vec3 fab, bool first, bool aisleRight) {
    float w = first ? 0.50f : 0.44f;
    op.box({x, floor + 0.44f, z + dir * 0.02f}, {w, 0.11f, 0.47f}, fab, MAT_RUBBER);
    op.box({x, floor + 0.36f, z}, {w - 0.04f, 0.06f, 0.42f}, SHELL, MAT_PAINT);
    for (int k = 0; k < 3; k++) {
        float y = floor + 0.62f + k * 0.21f, back = -dir * (0.22f + k * 0.03f);
        op.box({x, y, z + back}, {w, 0.21f, 0.09f}, k == 2 ? fab * 1.18f : fab, MAT_RUBBER);
        op.box({x, y, z + back - dir * 0.065f}, {w + 0.02f, 0.22f, 0.04f}, SHELL, MAT_PAINT);
    }
    op.box({x, floor + 1.26f, z - dir * 0.30f}, {w - 0.06f, 0.14f, 0.08f}, fab * 1.25f, MAT_RUBBER);  // hoofdsteun
    float gx = x + (aisleRight ? 1.0f : -1.0f) * (w / 2 - 0.04f);
    op.box({gx, floor + 1.36f, z - dir * 0.34f}, {0.03f, 0.05f, 0.10f}, ALU, MAT_METAL);              // handgreep
    if (first)
        for (int s = -1; s <= 1; s += 2) op.box({x + s * (w / 2 + 0.03f), floor + 0.66f, z}, {0.05f, 0.05f, 0.36f}, fab, MAT_RUBBER);
    op.box({x, floor + 0.17f, z}, {0.08f, 0.34f, 0.28f}, BOGIEC, MAT_METAL);
}

void bogie(MeshBuilder& op, bool motor) {
    const float r = motor ? 0.42f : 0.38f, wb = motor ? 2.5f : 2.7f;
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

// =========================================================================================
class SprinterStock;
// de cabinebouwer wil gewone functies voor de neusvorm: per type een vaste "huidige" specificatie
const SprinterSpec* g_cabSpec = nullptr;
float cabNoseZ(float y) { return lerpPairs(g_cabSpec->noseF, y); }
float cabNoseHalf(float y) { (void)y; return 0.92f; }

class SprinterStock : public RollingStock {
public:
    explicit SprinterStock(const SprinterSpec* s);
    float floorAt(int car, float lz) const override { (void)car; (void)lz; return S_.floorY; }
    bool walkable(int car, float lx, float lz) const override;
    void build(SignAtlas& atlas, const std::vector<std::string>& names) override;
    void drawOpaque(World& world, const StockDraw& s) const override;
    void drawGlass(World& world, const StockDraw& s) const override;

private:
    const SprinterSpec& S_;
    Prof pr_;
    std::vector<Mesh> car_, carInt_, carGlass_, leaf_;
    Mesh motor_, jacobs_, panto_, gangway_, headlight_, taillight_, cabDoor_, lever_;
    std::vector<Mesh> display_, screen_;
    float noseF(float y) const { return lerpPairs(S_.noseF, y); }
    // punt op de neus (canoniek): t = 0 midden van het front .. 1 waar de zijkant begint (hoek)
    Vec3 nosePoint(float th, float y, float z0, float side) const {
        float e = 2.0f / S_.noseExp, xp = pr_.outerX(y);
        float c = std::pow(std::max(0.0f, std::cos(th)), e), s = std::pow(std::max(0.0f, std::sin(th)), e);
        return Vec3(side * xp * s, y, z0 + noseF(y) * c);
    }
    Vec3 noseAt(float x, float y, float z0) const {  // punt op het neusoppervlak bij afstand x van het midden
        float xp = std::max(0.05f, pr_.outerX(y));
        float c = std::min(1.0f, std::fabs(x) / xp);
        float th = std::asin(std::pow(c, S_.noseExp / 2.0f));
        return nosePoint(th, y, z0, x < 0 ? -1.0f : 1.0f);
    }
    void buildNose(MeshBuilder& mb, MeshBuilder& gl, float z0) const;
    void buildCar(int i, SignAtlas& atlas);
    float zEnd(int i) const { return cabSign(i) ? len[i] / 2 - S_.noseLen : len[i] / 2; }
};

SprinterStock::SprinterStock(const SprinterSpec* s) : S_(*s) {
    pr_.S = s;
    product = S_.product;
    typeName = S_.typeName;
    if (S_.soundSet) soundSet = S_.soundSet;
    cars = (int)S_.cars.size();
    halfW = pr_.outerX(1.5f);
    gap = S_.gap;
    floorY = S_.floorY;
    doorW = S_.doorW;
    height = S_.py.back();
    nose = S_.noseLen;
    screenY = S_.ceilY - 0.26f;
    vmax = S_.vmax;
    acc = S_.acc;
    dec = S_.dec;
    for (int i = 0; i < cars; i++) {
        len.push_back(S_.cars[i].len);
        std::array<float, 2> dz{0, 0};
        const auto& d = S_.cars[i].doors;
        dz[0] = d.empty() ? 0 : d[0];
        dz[1] = d.size() > 1 ? d[1] : dz[0];
        if (cabSign(i) < 0) dz = {-dz[1], -dz[0]};
        doorZ.push_back(dz);
    }
    const float LE = S_.cars[0].len;
    cab.wallZ = S_.cabWall;
    cab.doorX = 0.45f;
    cab.doorW = 0.70f;
    cab.floorY = S_.cabFloor > 0 ? S_.cabFloor : S_.floorY + 0.40f;
    cab.passFloor = S_.floorY;
    cab.deskZ = LE / 2 - S_.noseLen + 0.35f;
    cab.seatX = 0;
    cab.seatZ = cab.deskZ - 0.55f;
    cab.seatTop = cab.floorY + 0.52f;
    cab.leverX = 0.36f;
    cab.leverY = cab.floorY + 0.72f;
    cab.leverZ = cab.deskZ + 0.14f;
}

bool SprinterStock::walkable(int car, float lx, float lz) const {
    int cs = cabSign(car);
    float x = cs < 0 ? -lx : lx, z = cs < 0 ? -lz : lz;
    float L = len[car];
    float xi = pr_.outerX(1.5f) - WALL;
    if (cs && z > S_.cabWall - 0.05f) return false;                  // cabine
    if (std::fabs(z) > L / 2 - 0.35f) return std::fabs(x) < 0.68f;   // open doorloop
    for (float d : S_.cars[car].doors)
        if (std::fabs(z - d) < 1.05f) return std::fabs(x) < xi - 0.25f;  // deurportaal
    if (cs && z > S_.firstEnd) return x > -0.66f && x < 0.22f;       // 1e klas (2+1)
    return std::fabs(x) < 0.30f;                                     // gangpad
}

// ---------------- neus (superellips) ----------------
void SprinterStock::buildNose(MeshBuilder& mb, MeshBuilder& gl, float z0) const {
    std::vector<float> NY;
    for (float y = S_.py.front(); y < S_.py.back(); y += 0.05f) NY.push_back(y);
    NY.push_back(S_.py.back() - 0.001f);
    for (float y : {S_.wsY0, S_.wsY1, S_.lampY}) NY.push_back(y);
    std::sort(NY.begin(), NY.end());
    NY.erase(std::unique(NY.begin(), NY.end(), [](float a, float b) { return std::fabs(a - b) < 0.008f; }), NY.end());
    const int COLS = 48;  // per kant
    int rows = (int)NY.size();
    // per rij: punten van de linkerkant via het midden naar de rechterkant, met afstand a langs de neus en aandeel s
    std::vector<std::vector<Vec3>> P(rows);
    std::vector<std::vector<float>> A(rows), Sv(rows);
    for (int r = 0; r < rows; r++) {
        float y = NY[r];
        std::vector<Vec3> half;
        std::vector<float> ha, hs;
        float acc = 0;
        Vec3 prev = nosePoint(0, y, z0, 1);
        for (int k = 0; k <= COLS; k++) {
            float th = (PI / 2) * k / COLS;
            Vec3 p = nosePoint(th, y, z0, 1);
            acc += length(p - prev);
            prev = p;
            half.push_back(p);
            ha.push_back(acc);
            hs.push_back((float)k / COLS);
        }
        for (int k = COLS; k >= 1; k--) { P[r].push_back(Vec3(-half[k].x, half[k].y, half[k].z)); A[r].push_back(ha[k]); Sv[r].push_back(hs[k]); }
        for (int k = 0; k <= COLS; k++) { P[r].push_back(half[k]); A[r].push_back(ha[k]); Sv[r].push_back(hs[k]); }
    }
    int cols = (int)P[0].size();
    auto vn = [&](int rr, int cc) {
        Vec3 du = P[rr][std::min(cc + 1, cols - 1)] - P[rr][std::max(cc - 1, 0)];
        Vec3 dv = P[std::min(rr + 1, rows - 1)][cc] - P[std::max(rr - 1, 0)][cc];
        Vec3 m = normalize(cross(dv, du));
        Vec3 o = P[rr][cc] - Vec3(0, P[rr][cc].y, z0 - 1.0f);
        return dot(m, o) < 0 ? m * -1.0f : m;
    };
    for (int r = 0; r + 1 < rows; r++)
        for (int c = 0; c + 1 < cols; c++) {
            float ym = (NY[r] + NY[r + 1]) / 2, am = (A[r][c] + A[r][c + 1]) / 2, sm = (Sv[r][c] + Sv[r][c + 1]) / 2;
            float mat;
            Vec3 col = S_.front(ym, am, sm, mat);
            if (mat == MAT_GLASS) {
                Vec3 gc(0.16f, 0.20f, 0.22f);
                uint32_t ga = gl.vert(P[r][c], vn(r, c), gc, EX_GLASS), gb = gl.vert(P[r][c + 1], vn(r, c + 1), gc, EX_GLASS);
                uint32_t ge = gl.vert(P[r + 1][c + 1], vn(r + 1, c + 1), gc, EX_GLASS), gd = gl.vert(P[r + 1][c], vn(r + 1, c), gc, EX_GLASS);
                gl.i.insert(gl.i.end(), {ga, gb, ge, ga, ge, gd});
                continue;
            }
            uint32_t ia = mb.vert(P[r][c], vn(r, c), col, mat), ib = mb.vert(P[r][c + 1], vn(r, c + 1), col, mat);
            uint32_t ie = mb.vert(P[r + 1][c + 1], vn(r + 1, c + 1), col, mat), id = mb.vert(P[r + 1][c], vn(r + 1, c), col, mat);
            mb.i.insert(mb.i.end(), {ia, ib, ie, ia, ie, id});
        }
    // onderkant dicht
    for (int c = 0; c + 1 < cols; c++) {
        uint32_t i0 = mb.vert(P[0][c], Vec3(0, -1, 0), SKIRTC, MAT_PAINT), i1 = mb.vert(P[0][c + 1], Vec3(0, -1, 0), SKIRTC, MAT_PAINT);
        uint32_t i2 = mb.vert(Vec3(0, S_.py.front(), z0), Vec3(0, -1, 0), SKIRTC, MAT_PAINT);
        mb.i.insert(mb.i.end(), {i0, i1, i2});
    }
    // ruitframe (rubber) langs de onderkant van de voorruit en de ruitenwisser
    for (int s = -1; s <= 1; s += 2) {
        Vec3 w0 = noseAt(s * 0.10f, S_.wsY0 + 0.06f, z0), w1 = noseAt(s * 0.62f, S_.wsY0 + 0.40f, z0);
        mb.tube(w0 + Vec3(0, 0, 0.02f), w1 + Vec3(0, 0, 0.02f), 0.012f, 6, BLACK, MAT_RUBBER);
    }
    // koppeling (Scharfenberg) onder de bumper, half in de nis
    float zf = z0 + noseF(S_.floorY - 0.35f);
    mb.tube({0, S_.floorY - 0.32f, zf - 0.35f}, {0, S_.floorY - 0.32f, zf + 0.04f}, 0.06f, 12, STEEL * 0.7f, MAT_METAL);
    mb.box({0, S_.floorY - 0.32f, zf + 0.08f}, {0.26f, 0.20f, 0.09f}, STEEL, MAT_METAL);
    mb.box({0.05f, S_.floorY - 0.29f, zf + 0.13f}, {0.07f, 0.07f, 0.01f}, BLACK, MAT_METAL);
    mb.box({-0.06f, S_.floorY - 0.32f, zf + 0.13f}, {0.06f, 0.12f, 0.01f}, Vec3(0.75f, 0.55f, 0.20f), MAT_METAL);
    // baanschuiver
    mb.box({0, S_.py.front() + 0.10f, z0 + noseF(S_.py.front()) - 0.08f}, {1.9f, 0.16f, 0.10f}, SKIRTC * 0.8f, MAT_PAINT);
    // lamphuizen (donker glas; de lampen zelf zijn aparte meshes)
    for (int s = -1; s <= 1; s += 2) {
        Vec3 p = noseAt(s * S_.lampX, S_.lampY, z0);
        mb.box(p + Vec3(0, 0, -0.03f), {0.36f, 0.16f, 0.08f}, Vec3(0.10f, 0.10f, 0.11f), MAT_GLASS);
    }
}

void SprinterStock::buildCar(int i, SignAtlas& atlas) {
    const SprinterSpec::Car& C = S_.cars[i];
    const float L = C.len;
    const int cs = cabSign(i);
    const bool cab = cs != 0;
    const float ze = zEnd(i);
    MeshBuilder op, gl;
    const float FY = S_.floorY;
    // ---- openingen ----
    std::vector<Hole> holes;
    for (auto& w : C.windows) holes.push_back({w.first, w.second, S_.winY0, S_.winY1, S_.winR, true});
    for (float d : C.doors) holes.push_back({d - S_.doorW / 2, d + S_.doorW / 2, FY - 0.05f, S_.doorTop, 0.05f, false});
    if (cab) holes.push_back({S_.cabWall + 0.12f, ze - 0.06f, FY + 0.95f, FY + 2.15f, 0.10f, true});  // cabinezijraam
    // ---- buitenkant ----
    shell(op, gl, pr_, &C, -L / 2, ze, S_.bodyBottom, S_.py.back(), holes, {}, 0, true);
    float xb = pr_.outerX(S_.bodyBottom);
    op.quad(Vec3(-xb, S_.bodyBottom, -L / 2), Vec3(xb, S_.bodyBottom, -L / 2), Vec3(xb, S_.bodyBottom, ze), Vec3(-xb, S_.bodyBottom, ze),
            Vec3(0, -1, 0), SKIRTC * 0.7f, MAT_PAINT);
    endFrame(op, pr_, -L / 2 + 0.01f, S_.bodyBottom, S_.py.back(), Vec3(0.25f, 0.25f, 0.26f), 0.0f, 0.74f, FY - 0.05f, FY + 2.15f);
    if (!cab) endFrame(op, pr_, L / 2 - 0.01f, S_.bodyBottom, S_.py.back(), Vec3(0.25f, 0.25f, 0.26f), 0.0f, 0.74f, FY - 0.05f, FY + 2.15f);
    float HW = pr_.outerX(1.5f);
    // deuren: treeplank met gele rand, kozijn, drukknop
    for (float d : C.doors)
        for (int s = -1; s <= 1; s += 2) {
            op.box({s * (HW - 0.05f), FY - 0.03f, d}, {0.12f, 0.04f, S_.doorW}, STEEL, MAT_METAL);
            if (S_.steps) {  // hoge vloer: uitklaptrede onder de deur en een trede binnen
                op.box({s * (HW + 0.06f), FY - 0.36f, d}, {0.30f, 0.05f, S_.doorW - 0.1f}, STEEL * 0.9f, MAT_METAL);
                op.box({s * (HW + 0.06f), FY - 0.34f, d}, {0.31f, 0.012f, S_.doorW - 0.12f}, POLE, MAT_PAINT);
            }
            op.box({s * (HW + 0.002f), FY - 0.01f, d}, {0.03f, 0.012f, S_.doorW - 0.08f}, POLE, MAT_PAINT);
            for (int e = -1; e <= 1; e += 2)
                op.box({s * (HW - WALL / 2), (FY + S_.doorTop) / 2, d + e * (S_.doorW / 2 + 0.015f)}, {WALL, S_.doorTop - FY, 0.03f}, S_.doorFrame, MAT_PAINT);
            op.box({s * (HW - WALL / 2), S_.doorTop + 0.015f, d}, {WALL, 0.03f, S_.doorW + 0.06f}, S_.doorFrame, MAT_PAINT);
        }
    // NS-logo en klassenummers naast de deuren
    {
        SignRect r2 = atlas.add("2", Vec3(0.03f, 0.17f, 0.50f), Vec3(1, 1, 1), 64, true, 0.3f);
        SignRect r1 = atlas.add("1", Vec3(0.03f, 0.17f, 0.50f), Vec3(1, 1, 0.3f), 64, true, 0.3f);
        for (int s = -1; s <= 1; s += 2)
            for (float d : C.doors) {
                float zt = d + (S_.doorW / 2 + 0.30f);
                bool first = cab && zt > S_.firstEnd;
                SignAtlas::quad(op, first ? r1 : r2, Vec3(s * (HW + 0.004f), FY + 1.86f, zt), Vec3(0, 0, (float)-s), 0.11f, false);
            }
    }
    // dak: klimaatinstallatie, kasten, dakleuningen
    op.box({0, S_.py.back() - 0.01f, 0}, {1.6f, 0.14f, 3.0f}, Vec3(0.55f, 0.57f, 0.59f), MAT_PAINT);
    for (int k = 0; k < 6; k++) op.box({0, S_.py.back() + 0.065f, -1.0f + k * 0.4f}, {1.2f, 0.012f, 0.18f}, Vec3(0.26f, 0.27f, 0.28f), MAT_PAINT);
    if (i == S_.pantoCar) op.box({0, S_.py.back() + 0.03f, L / 2 - 3.5f}, {1.4f, 0.10f, 2.2f}, Vec3(0.45f, 0.46f, 0.48f), MAT_PAINT);
    if (cab) {
        op.box({0, S_.py.back() - 0.05f, -L / 2 + 3.5f}, {1.5f, 0.22f, 3.0f}, Vec3(0.50f, 0.52f, 0.54f), MAT_PAINT);
        buildNose(op, gl, ze);
    }
    // onderbak: kasten tussen de draaistellen
    for (float zq = -L / 2 + 3.5f; zq < ze - 3.5f; zq += 2.4f)
        op.box({0, S_.bodyBottom - 0.22f, zq}, {2.2f, 0.40f, 1.9f}, Vec3(0.17f, 0.18f, 0.20f), MAT_PAINT);

    // ======================= INTERIEUR =======================
    const size_t intV = op.v.size(), intI = op.i.size();
    float innerEnd = cab ? S_.cabWall : L / 2 - 0.05f;
    shell(op, gl, pr_, nullptr, -L / 2 + 0.05f, innerEnd, FY, S_.ceilY, holes, S_.wall, WALL, false);
    endFrame(op, pr_, -L / 2 + 0.08f, FY, S_.ceilY, S_.wall * 0.92f, WALL, 0.74f, FY - 0.05f, FY + 2.15f);
    if (!cab) endFrame(op, pr_, L / 2 - 0.08f, FY, S_.ceilY, S_.wall * 0.92f, WALL, 0.74f, FY - 0.05f, FY + 2.15f);
    float xi = pr_.outerX(1.5f) - WALL;
    float zMid = (-L / 2 + innerEnd) / 2, zLen = innerEnd + L / 2;
    op.box({0, FY - 0.02f, zMid}, {2 * xi, 0.04f, zLen}, S_.floorC, MAT_PAINT);
    // gele looplijn bij de deuren
    for (float d : C.doors) op.box({0, FY + 0.003f, d}, {2 * xi, 0.006f, 0.04f}, POLE, MAT_PAINT);
    op.box({0, S_.ceilY + 0.01f, zMid}, {2 * (pr_.outerX(S_.ceilY) - WALL), 0.03f, zLen}, S_.ceilC, MAT_PAINT);
    for (int s = -1; s <= 1; s += 2) op.box({s * 0.62f, S_.ceilY - 0.015f, zMid}, {0.12f, 0.02f, zLen - 0.6f}, LIGHTC, MAT_LIGHT);
    // ventilatieroosters in het plafond
    for (float z = -L / 2 + 1.0f; z < innerEnd - 0.8f; z += 2.2f) op.box({0, S_.ceilY - 0.008f, z}, {0.5f, 0.012f, 0.25f}, S_.ceilC * 0.8f, MAT_PAINT);
    // bagagerekken boven de ramen
    for (int s = -1; s <= 1; s += 2)
        for (auto& w : C.windows) op.box({s * (xi - 0.18f), S_.winY1 + 0.42f, (w.first + w.second) / 2}, {0.30f, 0.02f, w.second - w.first + 0.2f}, Vec3(0.55f, 0.62f, 0.66f), MAT_GLASS);
    if (cab) {
        cabin::Params cp;
        cp.c = this->cab;
        cp.halfW = 1.22f;
        cp.ceilY = S_.ceilY - 0.05f;
        cp.frontZ = ze + noseF(cp.c.floorY + 1.15f) - 0.15f;
        cp.deskTopY = cp.c.floorY + 0.67f;
        cp.glassY = S_.wsY0;
        cp.sideWin0 = FY + 0.95f;
        cp.sideWin1 = FY + 2.15f;
        cp.wall = S_.wall * 0.9f;
        cp.desk = Vec3(0.22f, 0.23f, 0.25f);
        cp.floor = Vec3(0.18f, 0.18f, 0.19f);
        cp.seat = Vec3(0.12f, 0.16f, 0.30f);
        g_cabSpec = &S_;
        cp.noseAt = cabNoseZ;
        cp.noseHalfAt = cabNoseHalf;
        cp.noseBase = ze;
        cabin::build(op, gl, cp);
        // glazen scheidingswand bij de 1e klas
        op.box({-0.95f, (FY + 2.2f) / 2, S_.firstEnd}, {0.75f, 2.2f - 0.0f, 0.03f}, Vec3(0.6f, 0.68f, 0.72f), MAT_GLASS);
        op.box({0.80f, (FY + 2.2f) / 2, S_.firstEnd}, {0.95f, 2.2f - 0.0f, 0.03f}, Vec3(0.6f, 0.68f, 0.72f), MAT_GLASS);
    }
    // toiletmodule (eerste middenrijtuig, naast de deur): wit blok met deur en bordje
    if (!cab && i == 1 && !C.doors.empty()) {
        float d = C.doors[0], z0 = d + S_.doorW / 2 + 0.45f, z1 = z0 + 2.1f;
        op.box({-xi + 0.95f, (FY + S_.ceilY) / 2, (z0 + z1) / 2}, {1.9f, S_.ceilY - FY, z1 - z0}, Vec3(0.86f, 0.87f, 0.88f), MAT_PAINT);
        op.box({-xi + 1.92f, FY + 1.0f, (z0 + z1) / 2}, {0.04f, 2.0f, 0.95f}, Vec3(0.70f, 0.72f, 0.75f), MAT_PAINT);
        op.box({-xi + 1.95f, FY + 1.6f, (z0 + z1) / 2}, {0.02f, 0.14f, 0.14f}, Vec3(0.03f, 0.17f, 0.50f), MAT_PAINT);
    }
    // stoelen: per raam een rij; om en om tegen elkaar in (vis-a-vis), tafeltje bij het raam
    auto addSeat = [&](float x, float z, float dir, bool first, bool aisleRight) {
        seat(op, x, FY, z, dir, first ? S_.seat1 : S_.seat2, first, aisleRight);
        SeatInfo si;
        si.car = i;
        si.lx = cs < 0 ? -x : x;
        si.lz = cs < 0 ? -z : z;
        si.facing = wrapAngle((dir > 0 ? 0.0f : PI) + (cs < 0 ? PI : 0.0f));
        si.y = FY;
        seats.push_back(si);
    };
    int k = 0;
    for (auto& w : C.windows) {
        float zc = (w.first + w.second) / 2;
        bool nearDoor = false;
        for (float d : C.doors) if (std::fabs(zc - d) < S_.doorW / 2 + 0.75f) nearDoor = true;
        if (nearDoor) continue;
        bool toilet = !cab && i == 1 && !C.doors.empty() && zc > C.doors[0] && zc < C.doors[0] + S_.doorW / 2 + 2.7f;
        bool first = cab && zc > S_.firstEnd;
        float dir = (k++ % 2 == 0) ? 1.0f : -1.0f;
        float z = zc - dir * 0.10f;
        if (first) {
            addSeat(-0.95f, z, dir, true, true);
            addSeat(0.48f, z, dir, true, false);
            addSeat(0.98f, z, dir, true, false);
        } else {
            if (!toilet) { addSeat(-1.00f, z, dir, false, true); addSeat(-0.54f, z, dir, false, true); }
            addSeat(0.54f, z, dir, false, false);
            addSeat(1.00f, z, dir, false, false);
        }
        for (int s = -1; s <= 1; s += 2) if (!(toilet && s < 0)) op.box({s * (xi - 0.12f), FY + 0.72f, zc}, {0.22f, 0.03f, 0.36f}, Vec3(0.40f, 0.41f, 0.43f), MAT_PAINT);
    }
    // deurportalen: gele stangen, klapstoeltjes, prullenbak
    for (float d : C.doors)
        for (int s = -1; s <= 1; s += 2) {
            for (float off : {-0.95f, 0.95f}) op.tube({s * 0.62f, FY, d + off}, {s * 0.62f, S_.ceilY, d + off}, 0.02f, 6, POLE, MAT_METAL);
            op.box({s * (xi - 0.20f), FY + 0.48f, d + 1.35f}, {0.40f, 0.05f, 0.40f}, S_.seat2, MAT_RUBBER);
            op.box({s * (xi - 0.10f), FY + 0.45f, d - 1.30f}, {0.18f, 0.55f, 0.25f}, Vec3(0.35f, 0.36f, 0.38f), MAT_PAINT);
        }
    for (size_t q = intV; q < op.v.size(); q++)
        if (op.v[q].extra != MAT_LIGHT && op.v[q].extra != MAT_GLASS && op.v[q].extra > -0.5f && op.v[q].extra < 4.5f) op.v[q].extra = MAT_INTERIOR;
    MeshBuilder ext, inn;
    ext.v.assign(op.v.begin(), op.v.begin() + intV);
    ext.i.assign(op.i.begin(), op.i.begin() + intI);
    inn.v.assign(op.v.begin() + intV, op.v.end());
    for (size_t q = intI; q < op.i.size(); q++) inn.i.push_back(op.i[q] - (uint32_t)intV);
    car_[i] = ext.upload();
    carInt_[i] = inn.upload();
    carGlass_[i] = gl.upload();

    // deurbladen: met groot raam en groene drukknop
    for (int s = 0; s < 2; s++)
        for (int leaf = 0; leaf < 2; leaf++) {
            MeshBuilder dm;
            float side = s ? 1.0f : -1.0f;
            float zc = (leaf ? 1.0f : -1.0f) * S_.doorW / 4;
            for (float d : C.doors) {
                float z = d + zc, x = side * (HW - 0.02f), w = S_.doorW / 2 - 0.015f;
                dm.box({x, (FY + 1.25f) / 2, z}, {0.05f, 1.25f - FY, w}, S_.doorC, MAT_PAINT);
                dm.box({x, (FY + 2.0f + S_.doorTop) / 2, z}, {0.05f, S_.doorTop - FY - 2.0f + 0.04f, w}, S_.doorC, MAT_PAINT);
                for (float e : {-1.0f, 1.0f}) dm.box({x, FY + 1.05f, z + e * (w / 2 - 0.05f)}, {0.05f, 1.20f, 0.10f}, S_.doorC, MAT_PAINT);
                dm.box({x, FY + 1.05f, z}, {0.03f, 1.20f, w - 0.10f}, Vec3(0.03f, 0.035f, 0.04f), MAT_GLASS);
                dm.box({x + side * 0.03f, FY + 1.0f, z + (leaf ? -1.0f : 1.0f) * (w / 2 - 0.01f)}, {0.012f, 1.95f, 0.02f}, RUBBER, MAT_RUBBER);
                if (leaf) dm.box({x + side * 0.03f, FY + 0.40f, z - w / 2 + 0.12f}, {0.02f, 0.08f, 0.08f}, Vec3(0.1f, 0.85f, 0.25f), MAT_LIGHT);
            }
            leaf_[i * 4 + s * 2 + leaf] = dm.upload();
        }
}

void SprinterStock::build(SignAtlas& atlas, const std::vector<std::string>& names) {
    car_.resize(cars);
    carInt_.resize(cars);
    carGlass_.resize(cars);
    leaf_.resize(cars * 4);
    seats.clear();
    for (int i = 0; i < cars; i++) buildCar(i, atlas);
    cabDoor_ = cabin::doorLeaf(cab.doorW - 0.02f, 2.0f, Vec3(0.45f, 0.46f, 0.48f));
    lever_ = cabin::lever();
    { MeshBuilder m; bogie(m, true); motor_ = m.upload(); }
    { MeshBuilder m; bogie(m, false); jacobs_ = m.upload(); }
    {
        MeshBuilder b;
        float y0 = S_.py.back() + 0.08f;
        b.box({0, y0, 0}, {1.3f, 0.10f, 1.5f}, STEEL * 0.7f, MAT_METAL);
        for (int s = -1; s <= 1; s += 2) b.tube({s * 0.5f, y0, 0.55f}, {s * 0.5f, y0 + 0.22f, 0.55f}, 0.07f, 8, Vec3(0.55f, 0.30f, 0.20f), MAT_PAINT);
        Vec3 hinge(0, y0 + 0.25f, 0.55f), knee(0, y0 + 0.95f, -0.85f), head(0, y0 + 1.45f, 0.10f);
        for (int s = -1; s <= 1; s += 2) b.tube(hinge + Vec3(s * 0.25f, 0, 0), knee + Vec3(s * 0.05f, 0, 0), 0.04f, 6, STEEL, MAT_METAL);
        b.tube(knee, head, 0.03f, 6, STEEL, MAT_METAL);
        for (float dz : {-0.12f, 0.12f}) b.box(head + Vec3(0, 0.03f, dz), {1.95f, 0.04f, 0.06f}, Vec3(0.25f, 0.25f, 0.27f), MAT_METAL);
        panto_ = b.upload();
    }
    {
        MeshBuilder g;
        float FY = S_.floorY;
        for (int k = 0; k < 5; k++) {
            float z = -S_.gap / 2 + S_.gap * (k + 0.5f) / 5, w = 2.60f - (k % 2) * 0.05f, h = 2.30f - (k % 2) * 0.05f, t = 0.14f;
            Vec3 col = k % 2 ? RUBBER * 2.2f : RUBBER;
            float yc = FY - 0.05f + h / 2;
            g.box({0, yc + h / 2 - t / 2, z}, {w, t, S_.gap / 5 + 0.02f}, col, MAT_RUBBER);
            g.box({-w / 2 + t / 2, yc, z}, {t, h, S_.gap / 5 + 0.02f}, col, MAT_RUBBER);
            g.box({w / 2 - t / 2, yc, z}, {t, h, S_.gap / 5 + 0.02f}, col, MAT_RUBBER);
        }
        g.box({0, FY - 0.02f, 0}, {1.5f, 0.04f, S_.gap + 0.4f}, S_.floorC, MAT_INTERIOR);
        gangway_ = g.upload();
    }
    {
        MeshBuilder hl, tl;
        float zN = zEnd(0);
        for (int s = -1; s <= 1; s += 2) {
            Vec3 p = noseAt(s * S_.lampX, S_.lampY, zN);
            hl.box(p + Vec3(s * 0.08f, 0, 0.012f), {0.14f, 0.09f, 0.03f}, Vec3(1.6f, 1.6f, 1.45f), MAT_LIGHT);
            tl.box(p + Vec3(-s * 0.10f, 0, 0.012f), {0.10f, 0.08f, 0.03f}, Vec3(1.2f, 0.05f, 0.03f), MAT_LIGHT);
        }
        Vec3 top = noseAt(0, S_.wsY1 + 0.12f, zN);
        hl.box(top + Vec3(0, 0, 0.01f), {0.14f, 0.07f, 0.03f}, Vec3(1.6f, 1.6f, 1.45f), MAT_LIGHT);  // toplicht
        headlight_ = hl.upload();
        taillight_ = tl.upload();
    }
    display_.resize(names.size());
    screen_.resize(names.size());
    float zN = zEnd(0);
    for (size_t t = 0; t < names.size(); t++) {
        {
            MeshBuilder m;
            SignRect r = atlas.add(names[t], Vec3(0.01f, 0.01f, 0.01f), Vec3(1.0f, 1.0f, 1.0f), 64, true, 0.3f);
            float y0 = S_.displayY - 0.09f, y1 = S_.displayY + 0.09f;
            float h = y1 - y0, w = std::min(1.3f, h * r.aspect * 1.3f);
            Vec3 a(-w / 2, y0, zN + noseF(y0) - 0.07f), b(w / 2, y0, zN + noseF(y0) - 0.07f);
            Vec3 c(w / 2, y1, zN + noseF(y1) - 0.07f), d(-w / 2, y1, zN + noseF(y1) - 0.07f);
            Vec3 n = normalize(cross(b - a, d - a));
            auto V = [&](Vec3 p, float u, float v) { return m.vert(p, n, Vec3(u, v, 0), EX_SIGN); };
            uint32_t i0 = V(b, r.u1, r.v1), i1 = V(a, r.u0, r.v1), i2 = V(d, r.u0, r.v0), i3 = V(c, r.u1, r.v0);
            m.i.insert(m.i.end(), {i0, i1, i2, i0, i2, i3});
            display_[t] = m.upload();
        }
        {
            MeshBuilder m;
            SignRect r = atlas.add("Volgende station: " + names[t], Vec3(0.0f, 0.13f, 0.43f), Vec3(1, 1, 1), 48, true, 0.3f);
            SignAtlas::quad(m, r, Vec3(0, S_.ceilY - 0.20f, 0), Vec3(1, 0, 0), 0.16f, true);
            screen_[t] = m.upload();
        }
    }
}

void SprinterStock::drawOpaque(World& world, const StockDraw& sd) const {
    const Vehicle& v = *sd.v;
    world.setMode(MODE_VEHICLE);
    Vec3 cam = world.camPos();
    for (int i = 0; i < cars; i++) {
        CarPose c = v.carPose(i);
        int cs = cabSign(i);
        Mat4 m = Mat4::pose(c.x, c.y, c.z, cs < 0 ? c.h + PI : c.h);
        world.setModel(m);
        drawMesh(car_[i]);
        if (std::hypot(c.x - cam.x, c.z - cam.z) < world.nearDistance) drawMesh(carInt_[i]);
        for (int s = 0; s < 2; s++)
            for (int leaf = 0; leaf < 2; leaf++) {
                int meshSide = s ? 1 : -1;
                int worldSide = cs < 0 ? -meshSide : meshSide;
                float open = worldSide == sd.doorSide ? sd.doorOpen : 0;
                float out = std::min(1.0f, open * 4.0f) * 0.07f;
                float slide = clampf((open - 0.12f) / 0.88f, 0, 1);
                float off = (leaf ? 1.0f : -1.0f) * (S_.doorW / 2 - 0.03f) * slide;
                world.setModel(m * Mat4::translation(meshSide * out, 0, off));
                drawMesh(leaf_[i * 4 + s * 2 + leaf]);
            }
        if (!S_.jacobs) {  // gewone draaistellen, twee per rijtuig
            for (int e = -1; e <= 1; e += 2) {
                float zb = e * (len[i] / 2 - (cs && e > 0 ? S_.noseLen + 1.6f : 2.6f));
                world.setModel(m * Mat4::translation(0, 0, zb));
                drawMesh(cs && e > 0 ? motor_ : jacobs_);
            }
        }
        if (cs) {
            if (S_.jacobs) {
                world.setModel(m * Mat4::translation(0, 0, len[i] / 2 - S_.noseLen - 1.8f));
                drawMesh(motor_);
            }
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
        if (i == S_.pantoCar) {
            world.setModel(m * Mat4::translation(0, 0, len[i] / 2 - 3.5f));
            drawMesh(panto_);
        }
        if (i + 1 < cars) {
            CarPose n = v.carPose(i + 1);
            Vec3 a = v.carToWorld(i, 0, 0, -len[i] / 2), b = v.carToWorld(i + 1, 0, 0, len[i + 1] / 2);
            world.setModel(Mat4::pose((a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2, std::atan2(c.x - n.x, c.z - n.z)));
            if (S_.jacobs) drawMesh(jacobs_);
            drawMesh(gangway_);
        }
    }
    world.setMode(MODE_OBJECT);
    for (int i = 0; i < cars; i++) {
        CarPose c = v.carPose(i);
        int cs = cabSign(i);
        if (cs && sd.dest >= 0 && sd.dest < (int)display_.size()) {
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

void SprinterStock::drawGlass(World& world, const StockDraw& sd) const {
    for (int i = 0; i < cars; i++) {
        CarPose c = sd.v->carPose(i);
        int cs = cabSign(i);
        world.setModel(Mat4::pose(c.x, c.y, c.z, cs < 0 ? c.h + PI : c.h));
        drawMesh(carGlass_[i]);
    }
    world.setModel(Mat4::identity());
}

// mtr-coordinaten (y = 0 op de vloer, cabine aan -z) naar canoniek (spoorstaaf, cabine aan +z)
std::vector<std::pair<float, float>> flipWin(const std::vector<std::pair<float, float>>& w) {
    std::vector<std::pair<float, float>> o;
    for (auto& p : w) o.push_back({-p.second, -p.first});
    std::sort(o.begin(), o.end());
    return o;
}
std::vector<float> flipDoors(std::vector<float> d) {
    for (float& x : d) x = -x;
    std::sort(d.begin(), d.end());
    return d;
}
}  // namespace

// ================================ specificaties ================================
const SprinterSpec& sngSpec() {
    static SprinterSpec s = [] {
        SprinterSpec p;
        p.typeName = "SNG";
        p.soundSet = "sng";  // opnames van een echte SNG (sound/sng)
        // profiel: mtr-sng-hd PROFILE (x, y boven de vloer) + 0,80 m
        const float prof[][2] = {{1.32f, -0.70f}, {1.39f, -0.30f}, {1.43f, -0.05f}, {1.43f, 2.20f}, {1.415f, 2.44f}, {1.36f, 2.68f},
                                 {1.24f, 2.92f}, {1.04f, 3.10f}, {0.74f, 3.22f}, {0.38f, 3.28f}, {0.0f, 3.30f}};
        for (auto& q : prof) { p.py.push_back(q[1] + SF); p.px.push_back(q[0]); }
        p.floorY = SF; p.ceilY = SF + 2.32f; p.bodyBottom = SF - 0.70f; p.doorW = 1.30f; p.doorTop = SF + 2.08f; p.gap = 0.60f;
        p.winY0 = SF + 0.95f; p.winY1 = SF + 2.06f; p.winR = 0.13f;
        p.vmax = 44.4f; p.acc = 1.1f; p.dec = 0.9f;
        auto row = [](float z0, float z1) {  // _row(z0, z1, 1.22, 1.52)
            const float w = 1.22f, pitch = 1.52f;
            int n = std::max(1, (int)((z1 - z0 - w) / pitch) + 1);
            float span = (n - 1) * pitch + w, a = (z0 + z1 - span) / 2;
            std::vector<std::pair<float, float>> o;
            for (int k = 0; k < n; k++) o.push_back({a + k * pitch, a + k * pitch + w});
            return o;
        };
        auto cat = [](std::vector<std::pair<float, float>> a, const std::vector<std::pair<float, float>>& b) { a.insert(a.end(), b.begin(), b.end()); return a; };
        SprinterSpec::Car cab{21.68f, true, flipDoors({-1.75f, 5.75f}), flipWin(cat(cat(row(-6.70f, -2.55f), row(-0.95f, 4.95f)), row(6.55f, 10.55f)))};
        SprinterSpec::Car mid{16.2f, false, {0.0f}, flipWin(cat(row(-7.95f, -0.80f), row(0.80f, 7.95f)))};
        p.cars = {cab, mid, mid, cab};
        p.noseLen = 2.4f; p.noseExp = 2.2f;
        const float nf[][2] = {{-0.70f, 2.25f}, {-0.40f, 2.36f}, {0.00f, 2.38f}, {0.35f, 2.30f}, {0.70f, 2.18f}, {1.00f, 2.08f}, {1.25f, 1.97f},
                               {1.60f, 1.73f}, {2.00f, 1.42f}, {2.40f, 1.10f}, {2.75f, 0.78f}, {3.00f, 0.48f}, {3.18f, 0.22f}, {3.30f, 0.0f}};
        for (auto& q : nf) p.noseF.push_back({q[0] + SF, q[1]});
        p.cabWall = 6.82f;                       // mtr CAB_WALL = -6.82 (vanaf het rijtuigmidden, cabine aan -z)
        p.firstEnd = 2.45f;
        p.wsY0 = SF + 1.22f; p.wsY1 = SF + 2.92f; p.wsA0 = 1.08f; p.wsA1 = 0.86f;
        p.lampY = SF + 0.98f; p.lampX = 0.98f; p.displayY = SF + 2.78f;
        p.side = sngSide; p.front = sngFront;
        p.bandYs = {SF - 0.20f, SF + 0.02f, SF + 0.42f, SF + 2.30f, SF + 2.40f, SF + 2.80f, SF + 2.38f, SF + 0.66f, SF + 1.52f};
        p.doorC = rgb(30, 108, 204) * 0.92f; p.doorFrame = rgb(20, 70, 150);
        p.seat2 = Vec3(0.12f, 0.30f, 0.62f); p.seat1 = Vec3(0.66f, 0.12f, 0.16f);
        p.wall = Vec3(0.90f, 0.90f, 0.88f); p.floorC = Vec3(0.24f, 0.26f, 0.30f); p.ceilC = Vec3(0.95f, 0.95f, 0.94f);
        p.pantoCar = 1;
        return p;
    }();
    return s;
}

const SprinterSpec& sltSpec() {
    static SprinterSpec s = [] {
        SprinterSpec p;
        p.typeName = "SLT";
        const float prof[][2] = {{1.30f, -0.75f}, {1.37f, -0.32f}, {1.42f, -0.05f}, {1.42f, 2.05f}, {1.405f, 2.30f}, {1.35f, 2.54f},
                                 {1.24f, 2.76f}, {1.05f, 2.94f}, {0.76f, 3.07f}, {0.40f, 3.14f}, {0.0f, 3.16f}};
        for (auto& q : prof) { p.py.push_back(q[1] + SF); p.px.push_back(q[0]); }
        p.floorY = SF; p.ceilY = SF + 2.26f; p.bodyBottom = SF - 0.75f; p.doorW = 1.30f; p.doorTop = SF + 2.02f; p.gap = 0.60f;
        p.winY0 = SF + 0.82f; p.winY1 = SF + 1.90f; p.winR = 0.11f;
        p.vmax = 44.4f; p.acc = 1.1f; p.dec = 0.9f;
        SprinterSpec::Car cab{19.0f, true, flipDoors({-0.6f}),
                              flipWin({{-5.55f, -3.95f}, {-3.65f, -2.05f}, {0.65f, 2.25f}, {2.5f, 4.1f}, {4.95f, 6.55f}, {6.8f, 8.4f}})};
        SprinterSpec::Car mid{16.0f, false, flipDoors({-3.7f, 3.7f}),
                              flipWin({{-7.3f, -6.0f}, {-5.75f, -4.55f}, {-2.75f, -1.15f}, {-0.8f, 0.8f}, {1.15f, 2.75f}, {4.55f, 5.75f}, {6.0f, 7.3f}})};
        p.cars = {cab, mid, mid, cab};
        p.noseLen = 2.0f; p.noseExp = 2.5f;
        const float nf[][2] = {{-0.75f, 1.78f}, {-0.30f, 1.90f}, {0.10f, 1.97f}, {0.50f, 2.00f}, {0.85f, 1.975f}, {1.17f, 1.88f},
                               {1.48f, 1.68f}, {2.00f, 1.24f}, {2.50f, 0.81f}, {2.93f, 0.44f}, {3.07f, 0.27f}, {3.16f, 0.12f}};
        for (auto& q : nf) p.noseF.push_back({q[0] + SF, q[1]});
        p.cabWall = 5.92f;
        p.firstEnd = 1.72f;
        p.wsY0 = SF + 1.48f; p.wsY1 = SF + 2.93f; p.wsA0 = 0.65f; p.wsA1 = 0.62f;
        p.lampY = SF + 1.17f; p.lampX = 0.68f; p.displayY = SF + 2.78f;
        p.side = sltSide; p.front = sltFront;
        p.bandYs = {SF - 0.22f, SF + 0.08f, SF + 0.68f, SF + 2.08f, SF + 2.62f, SF + 3.0f, SF + 0.50f};
        p.doorC = rgb(250, 195, 24); p.doorFrame = Vec3(0.20f, 0.21f, 0.23f);
        p.seat2 = Vec3(0.10f, 0.20f, 0.48f); p.seat1 = Vec3(0.62f, 0.12f, 0.14f);
        p.wall = Vec3(0.86f, 0.86f, 0.84f); p.floorC = Vec3(0.24f, 0.26f, 0.29f); p.ceilC = Vec3(0.93f, 0.93f, 0.92f);
        p.pantoCar = 2;
        return p;
    }();
    return s;
}


const SprinterSpec& icmSpec() {
    static SprinterSpec s = [] {
        SprinterSpec p;
        p.typeName = "ICMm";
        p.product = "Intercity";
        p.jacobs = false;
        p.steps = true;
        // profiel ICM: 2,84 m breed, dak 4,04 m boven de spoorstaaf; hoge vloer 1,20 m
        const float prof[][2] = {{1.30f, 0.55f}, {1.38f, 0.80f}, {1.42f, 1.10f}, {1.42f, 3.10f}, {1.40f, 3.35f}, {1.33f, 3.60f},
                                 {1.18f, 3.82f}, {0.90f, 3.95f}, {0.45f, 4.02f}, {0.0f, 4.04f}};
        for (auto& q : prof) { p.py.push_back(q[1]); p.px.push_back(q[0]); }
        const float FY = ICM_FY;
        p.floorY = FY; p.ceilY = FY + 2.25f; p.bodyBottom = 0.55f; p.doorW = 0.90f; p.doorTop = FY + 1.98f; p.gap = 0.80f;
        p.winY0 = FY + 0.80f; p.winY1 = FY + 1.86f; p.winR = 0.10f;
        p.vmax = 44.4f; p.acc = 0.9f; p.dec = 0.8f;  // 160 km/u
        auto row = [](float z0, float z1, float w, float pitch) {
            int n = std::max(1, (int)((z1 - z0 - w) / pitch) + 1);
            float span = (n - 1) * pitch + w, a = (z0 + z1 - span) / 2;
            std::vector<std::pair<float, float>> o;
            for (int k = 0; k < n; k++) o.push_back({a + k * pitch, a + k * pitch + w});
            return o;
        };
        auto cat = [](std::vector<std::pair<float, float>> a, const std::vector<std::pair<float, float>>& b) { a.insert(a.end(), b.begin(), b.end()); return a; };
        // ICMm-III: mBk - B - mBk (27,2 + 25,6 + 27,2 m); deuren bij de rijtuigeinden, ramen in paren per zitcoupe
        const float LC = 27.2f, LM = 25.6f;
        SprinterSpec::Car cab{LC, true, {-LC / 2 + 2.3f, LC / 2 - 5.6f}, cat(row(-LC / 2 + 3.3f, LC / 2 - 6.6f, 1.55f, 1.95f), {})};
        SprinterSpec::Car mid{LM, false, {-LM / 2 + 2.3f, LM / 2 - 2.3f}, row(-LM / 2 + 3.3f, LM / 2 - 3.3f, 1.55f, 1.95f)};
        p.cars = {cab, mid, cab};
        p.noseLen = 2.6f; p.noseExp = 3.0f;
        const float nf[][2] = {{0.55f, 2.30f}, {0.90f, 2.55f}, {1.30f, 2.60f}, {1.80f, 2.52f}, {2.40f, 2.25f}, {2.78f, 1.85f},
                               {3.30f, 1.10f}, {3.60f, 0.65f}, {3.85f, 0.30f}, {4.04f, 0.0f}};
        for (auto& q : nf) p.noseF.push_back({q[0], q[1]});
        p.cabWall = LC / 2 - 2.6f - 1.6f;
        p.cabFloor = 2.08f;                         // cabine hoog boven de doorloop
        p.firstEnd = 1.0f;
        p.wsY0 = 2.78f; p.wsY1 = 3.52f; p.wsA0 = 1.02f; p.wsA1 = 1.13f;
        p.lampY = 1.55f; p.lampX = 0.92f; p.displayY = 3.40f;
        p.side = icmSide; p.front = icmFront;
        p.bandYs = {0.85f, 1.92f, 1.98f, 3.62f};
        p.doorC = ICM_YEL; p.doorFrame = Vec3(0.12f, 0.12f, 0.13f);
        p.seat2 = Vec3(0.14f, 0.24f, 0.55f); p.seat1 = Vec3(0.60f, 0.10f, 0.14f);
        p.wall = Vec3(0.84f, 0.82f, 0.76f); p.floorC = Vec3(0.30f, 0.26f, 0.23f); p.ceilC = Vec3(0.92f, 0.91f, 0.88f);
        p.pantoCar = 1;
        return p;
    }();
    return s;
}

RollingStock* makeSprinter(const SprinterSpec* spec) { return new SprinterStock(spec); }
RollingStock* makeIcm() { return makeSprinter(&icmSpec()); }
RollingStock* makeSng() { return makeSprinter(&sngSpec()); }
RollingStock* makeSlt() { return makeSprinter(&sltSpec()); }
