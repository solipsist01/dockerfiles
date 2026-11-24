// Roltrappen: zie escalator.h.
#include "escalator.h"
#include <algorithm>
#include <cmath>
#include "world.h"

namespace {
const float TAN30 = 0.57735f;
const float FLAT = 1.0f;    // vlakke treden voorbij de kam
const float T1 = 1.2f;      // onderste overgangsboog (horizontaal)
const float T2 = 1.8f;      // bovenste overgangsboog
const float PITCH = 0.40f;  // tredediepte
const float STEP_W = 1.0f;  // tredebreedte
const float SKIRT_X = 0.52f, GLASS_X = 0.62f, OUT_X = Escalators::HALF_W;
const float DECK_Y = 0.40f, RAIL_Y = 0.95f, TRUSS = 1.05f;

const Vec3 STAINLESS(0.74f, 0.75f, 0.77f);
const Vec3 STAINLESS_DK(0.55f, 0.56f, 0.58f);
const Vec3 CLADDING(0.66f, 0.67f, 0.69f);
const Vec3 RUBBER(0.05f, 0.05f, 0.055f);
const Vec3 BRUSH(0.08f, 0.08f, 0.09f);
const Vec3 ALU(0.62f, 0.63f, 0.64f);
const Vec3 ALU_DK(0.38f, 0.39f, 0.40f);
const Vec3 STEP_BASE(0.16f, 0.16f, 0.17f);
const Vec3 STEP_RIB(0.55f, 0.56f, 0.57f);
const Vec3 YELLOW(1.0f, 0.80f, 0.0f);
const Vec3 GLASS(0.62f, 0.72f, 0.74f);
const Vec3 GREEN_LED(0.25f, 1.0f, 0.45f);
const Vec3 RED_LED(1.0f, 0.12f, 0.08f);

// Lokaal assenstelsel van een roltrap: x dwars, y omhoog, z = s langs de rijrichting (naar boven)
struct Frame {
    Vec3 o, ax, az;
    Vec3 P(float x, float y, float z) const { return o + ax * x + Vec3(0, y, 0) + az * z; }
};

// Balk tussen twee punten met dwarsrichting 'side' (horizontaal) en doorsnede w (dwars) x h (loodrecht daarop)
void beam(MeshBuilder& mb, Vec3 a, Vec3 b, Vec3 side, float w, float h, Vec3 col, float ex) {
    Vec3 d = b - a;
    float len = length(d);
    if (len < 1e-4f) return;
    d = d * (1.0f / len);
    Vec3 up = normalize(cross(side, d));
    Vec3 sx = side * (w / 2), sy = up * (h / 2);
    auto Q = [&](Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, Vec3 n) { mb.quad(p0, p1, p2, p3, n, col, ex); };
    Q(a + sx + sy, b + sx + sy, b - sx + sy, a - sx + sy, up);
    Q(a - sx - sy, b - sx - sy, b + sx - sy, a + sx - sy, up * -1.0f);
    Q(a + sx - sy, b + sx - sy, b + sx + sy, a + sx + sy, side);
    Q(a - sx + sy, b - sx + sy, b - sx - sy, a - sx - sy, side * -1.0f);
    Q(a - sx - sy, a + sx - sy, a + sx + sy, a - sx + sy, d * -1.0f);
    Q(b + sx - sy, b - sx - sy, b - sx + sy, b + sx + sy, d);
}

void addBoxLocal(MeshBuilder& mb, float x0, float x1, float y0, float y1, float z0, float z1, Vec3 col, float ex) {
    mb.orientedBox(Vec3((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), {x1 - x0, y1 - y0, z1 - z0}, 0, col, ex);
}
}  // namespace

float Escalators::length(float rise) {
    float lm = std::max(0.0f, rise / TAN30 - (T1 + T2) / 2);
    return 2 * FLAT + T1 + lm + T2;
}

float Escalators::heightAt(float rise, float s) {
    Esc e;
    e.d.rise = rise;
    e.Lm = std::max(0.0f, rise / TAN30 - (T1 + T2) / 2);
    e.L = length(rise);
    return profile(e, std::max(0.0f, std::min(e.L, s)));
}

float Escalators::profile(const Esc& e, float s) {
    const float s1 = FLAT, s2 = FLAT + T1, s3 = s2 + e.Lm, s4 = s3 + T2;
    if (s <= s1) return 0;
    if (s <= s2) { float x = s - s1; return TAN30 * x * x / (2 * T1); }
    if (s <= s3) return TAN30 * (T1 / 2 + (s - s2));
    if (s <= s4) { float x = s - s3; return TAN30 * (T1 / 2 + e.Lm + x - x * x / (2 * T2)); }
    return e.d.rise;
}

void Escalators::add(const EscalatorDef& d, MeshBuilder& op, MeshBuilder& gl, Collision& col) {
    Esc e;
    e.d = d;
    e.Lm = std::max(0.0f, d.rise / TAN30 - (T1 + T2) / 2);
    e.L = length(d.rise);
    // hoogteverschil precies: schaal de helling zodat de bovenkant klopt (afrondingsvrij)
    Frame F{d.base, Vec3(std::cos(d.heading), 0, -std::sin(d.heading)), Vec3(std::sin(d.heading), 0, std::cos(d.heading))};
    const float L = e.L, N = NEWEL;
    auto Y = [&](float s) { return profile(e, std::max(0.0f, std::min(L, s))); };

    // bemonstering langs s: fijn in de bogen, grof op de helling
    std::vector<float> S;
    for (float s = -N; s < 0; s += 0.25f) S.push_back(s);
    for (float s = 0; s < FLAT + T1; s += 0.2f) S.push_back(s);
    {
        int n = std::max(1, (int)std::ceil(e.Lm / 1.5f));
        for (int k = 0; k < n; k++) S.push_back(FLAT + T1 + e.Lm * k / n);
    }
    for (float s = FLAT + T1 + e.Lm; s < L; s += 0.2f) S.push_back(s);
    for (float s = L; s < L + N; s += 0.25f) S.push_back(s);
    S.push_back(L + N);

    for (int side = -1; side <= 1; side += 2) {
        const float sx = (float)side;
        const Vec3 nOut = F.ax * sx;
        for (size_t k = 0; k + 1 < S.size(); k++) {
            float a = S[k], b = S[k + 1];
            float ya = Y(a), yb = Y(b);
            bool inside = b > 0.02f && a < L - 0.02f;  // langs de treden (niet bij de koppen)
            // plint (rvs) met borstel
            if (inside) {
                op.quad(F.P(sx * SKIRT_X, ya - 0.05f, a), F.P(sx * SKIRT_X, yb - 0.05f, b), F.P(sx * SKIRT_X, yb + 0.30f, b),
                        F.P(sx * SKIRT_X, ya + 0.30f, a), nOut * -1.0f, STAINLESS, EX_PLAIN);
                beam(op, F.P(sx * (SKIRT_X - 0.02f), ya + 0.14f, a), F.P(sx * (SKIRT_X - 0.02f), yb + 0.14f, b), F.ax, 0.05f, 0.025f, BRUSH, EX_PLAIN);
                // groene ledstrook onder de borstel bij de in- en uitstap
                if (a < FLAT + 0.6f || b > L - FLAT - 0.6f)
                    beam(op, F.P(sx * (SKIRT_X - 0.006f), ya + 0.04f, a), F.P(sx * (SKIRT_X - 0.006f), yb + 0.04f, b), F.ax, 0.012f, 0.02f, GREEN_LED, EX_LIGHT);
            }
            // binnendek (schuin van plint naar balustradevoet)
            op.quad(F.P(sx * SKIRT_X, ya + 0.30f, a), F.P(sx * SKIRT_X, yb + 0.30f, b), F.P(sx * (GLASS_X - 0.03f), yb + DECK_Y, b),
                    F.P(sx * (GLASS_X - 0.03f), ya + DECK_Y, a), Vec3(0, 1, 0), STAINLESS_DK, EX_PLAIN);
            // klemprofiel onder het glas
            beam(op, F.P(sx * GLASS_X, ya + DECK_Y + 0.02f, a), F.P(sx * GLASS_X, yb + DECK_Y + 0.02f, b), F.ax, 0.07f, 0.06f, STAINLESS, EX_PLAIN);
            // buitendek en buitenbekleding (tot onder de vakwerkligger)
            op.quad(F.P(sx * (GLASS_X + 0.035f), ya + DECK_Y, a), F.P(sx * (GLASS_X + 0.035f), yb + DECK_Y, b), F.P(sx * OUT_X, yb + DECK_Y, b),
                    F.P(sx * OUT_X, ya + DECK_Y, a), Vec3(0, 1, 0), STAINLESS, EX_PLAIN);
            float da = a < 0 || a > L ? -0.05f : -TRUSS, db = b < 0 || b > L ? -0.05f : -TRUSS;
            if (a < 0 && b > 0) db = -TRUSS;
            op.quad(F.P(sx * OUT_X, ya + DECK_Y, a), F.P(sx * OUT_X, yb + DECK_Y, b), F.P(sx * OUT_X, yb + db, b), F.P(sx * OUT_X, ya + da, a),
                    nOut, CLADDING, EX_PLAIN);
            // glas
            gl.quad(F.P(sx * GLASS_X, ya + DECK_Y + 0.05f, a), F.P(sx * GLASS_X, yb + DECK_Y + 0.05f, b), F.P(sx * GLASS_X, yb + RAIL_Y - 0.03f, b),
                    F.P(sx * GLASS_X, ya + RAIL_Y - 0.03f, a), nOut, GLASS, EX_GLASS);
            // leuning (zwart rubber) op het glas
            beam(op, F.P(sx * GLASS_X, ya + RAIL_Y, a), F.P(sx * GLASS_X, yb + RAIL_Y, b), F.ax, 0.085f, 0.04f, RUBBER, EX_PLAIN);
            // botsing: balustrade
            Vec3 m = (F.P(sx * GLASS_X, 0, a) + F.P(sx * GLASS_X, 0, b)) * 0.5f;
            col.addBox({m.x, m.z, F.az.x, F.az.z, (b - a) / 2 + 0.02f, 0.06f, std::min(ya, yb) - 0.2f, std::max(ya, yb) + RAIL_Y + 0.05f});
        }
        // koppen: leuning in een halve cirkel omlaag, glas als waaier, eindkap
        for (int end = 0; end < 2; end++) {
            float s0 = end ? L + N : -N;
            float dir = end ? 1.0f : -1.0f;
            float yb = end ? d.rise : 0.0f;
            const float R = 0.27f;
            Vec3 c = F.P(sx * GLASS_X, yb + RAIL_Y - R, s0);
            Vec3 prev = c + Vec3(0, R, 0);
            for (int q = 1; q <= 10; q++) {
                float ang = PI * q / 10;
                Vec3 p = c + F.az * (dir * R * std::sin(ang)) + Vec3(0, R * std::cos(ang), 0);
                beam(op, prev, p, F.ax, 0.085f, 0.04f, RUBBER, EX_PLAIN);
                Vec3 pg0 = c + F.az * (dir * (R - 0.03f) * std::sin(PI * (q - 1) / 10)) + Vec3(0, (R - 0.03f) * std::cos(PI * (q - 1) / 10), 0);
                Vec3 pg1 = c + F.az * (dir * (R - 0.03f) * std::sin(ang)) + Vec3(0, (R - 0.03f) * std::cos(ang), 0);
                gl.quad(c, pg0, pg1, c, nOut, GLASS, EX_GLASS);
                prev = p;
            }
            // glas onder de leuningboog tot het dek
            gl.quad(F.P(sx * GLASS_X, yb + DECK_Y + 0.05f, s0), F.P(sx * GLASS_X, yb + DECK_Y + 0.05f, s0 + dir * 0.25f),
                    F.P(sx * GLASS_X, yb + RAIL_Y - R, s0 + dir * 0.25f), F.P(sx * GLASS_X, yb + RAIL_Y - R, s0), nOut, GLASS, EX_GLASS);
            // eindkap van de kop (dicht, rvs)
            op.quad(F.P(sx * SKIRT_X, yb - 0.05f, s0), F.P(sx * OUT_X, yb - 0.05f, s0), F.P(sx * OUT_X, yb + DECK_Y, s0),
                    F.P(sx * SKIRT_X, yb + 0.30f, s0), F.az * dir, STAINLESS, EX_PLAIN);
            // noodstop (rood) op het buitendek
            float sb = end ? L + 0.45f : -0.45f;
            Vec3 bp = F.P(sx * (OUT_X - 0.09f), yb + DECK_Y + 0.025f, sb);
            op.orientedBox(bp, {0.07f, 0.05f, 0.07f}, d.heading, Vec3(0.85f, 0.05f, 0.04f), EX_PLAIN);
            op.orientedBox(bp - Vec3(0, 0.015f, 0), {0.11f, 0.03f, 0.11f}, d.heading, YELLOW, EX_PLAIN);
        }
    }

    // plinten langs de koppen (vlak, tussen kamplaat en eindkap) + vloerplaten en kammen
    for (int end = 0; end < 2; end++) {
        float yb = end ? d.rise : 0.0f;
        float dir = end ? 1.0f : -1.0f;
        float sc = end ? L : 0.0f;  // kamrand
        // kamplaat (aluminium) met gele kamtanden
        float k0 = std::min(sc, sc + dir * 0.35f), k1 = std::max(sc, sc + dir * 0.35f);
        op.orientedBox(F.P(0, yb + 0.012f, (k0 + k1) / 2), {SKIRT_X * 2, 0.024f, k1 - k0}, d.heading, ALU, EX_PLAIN);
        float y0 = std::min(sc, sc + dir * 0.07f), y1 = std::max(sc, sc + dir * 0.07f);
        op.orientedBox(F.P(0, yb + 0.014f, (y0 + y1) / 2), {SKIRT_X * 2 - 0.02f, 0.026f, y1 - y0}, d.heading, YELLOW, EX_PLAIN);
        // geribbelde vloerplaat
        float f0 = std::min(sc + dir * 0.35f, sc + dir * (N - 0.3f)), f1 = std::max(sc + dir * 0.35f, sc + dir * (N - 0.3f));
        op.orientedBox(F.P(0, yb + 0.01f, (f0 + f1) / 2), {SKIRT_X * 2, 0.02f, f1 - f0}, d.heading, ALU_DK, EX_PLAIN);
        for (float s = f0 + 0.04f; s < f1 - 0.02f; s += 0.06f)
            op.orientedBox(F.P(0, yb + 0.021f, s), {SKIRT_X * 2 - 0.04f, 0.004f, 0.025f}, d.heading, ALU, EX_PLAIN);
        // vlakke plinten langs de vloerplaat
        for (int side = -1; side <= 1; side += 2) {
            float sx = (float)side;
            op.quad(F.P(sx * SKIRT_X, yb - 0.02f, f0), F.P(sx * SKIRT_X, yb - 0.02f, f1), F.P(sx * SKIRT_X, yb + 0.30f, f1),
                    F.P(sx * SKIRT_X, yb + 0.30f, f0), F.ax * -sx, STAINLESS, EX_PLAIN);
        }
        // richtingslampje in de kop: groen = instappen, rood = verboden in te gaan
        bool entry = (end == 0) == d.up;
        for (int side = -1; side <= 1; side += 2) {
            Vec3 lp = F.P(side * (SKIRT_X + 0.002f), yb + 0.22f, sc + dir * (N - 0.55f));
            op.orientedBox(lp, {0.006f, 0.07f, 0.12f}, d.heading, entry ? GREEN_LED : RED_LED, EX_LIGHT);
        }
        // groene tredeverlichting onder de kam (zichtbaar tussen kam en eerste trede)
        float g0 = std::min(sc, sc - dir * 0.45f), g1 = std::max(sc, sc - dir * 0.45f);
        op.orientedBox(F.P(0, yb - 0.30f, (g0 + g1) / 2), {STEP_W, 0.02f, g1 - g0}, d.heading, GREEN_LED * 0.8f, EX_LIGHT);
        // put onder de treden bij de kop (donker), zodat je niet in het niets kijkt
        op.orientedBox(F.P(0, yb - 0.33f, (g0 + g1) / 2 - dir * 0.2f), {SKIRT_X * 2, 0.02f, 1.6f}, d.heading, Vec3(0.05f, 0.05f, 0.05f), EX_PLAIN);
    }
    // onderkant van de vakwerkligger (bekleed) langs de helling
    for (size_t k = 0; k + 1 < S.size(); k++) {
        float a = S[k], b = S[k + 1];
        if (b <= 0 || a >= L) continue;
        float ya = Y(a) - TRUSS, yb = Y(b) - TRUSS;
        op.quad(F.P(-OUT_X, ya, a), F.P(-OUT_X, yb, b), F.P(OUT_X, yb, b), F.P(OUT_X, ya, a), Vec3(0, -1, 0), CLADDING * 0.9f, EX_PLAIN);
        // binnenkant: donkere bak onder de treden (gezien door de spleten)
        op.quad(F.P(-SKIRT_X, Y(a) - 0.32f, a), F.P(-SKIRT_X, Y(b) - 0.32f, b), F.P(SKIRT_X, Y(b) - 0.32f, b), F.P(SKIRT_X, Y(a) - 0.32f, a),
                Vec3(0, 1, 0), Vec3(0.07f, 0.07f, 0.075f), EX_PLAIN);
    }

    // botsing: loopvloer met lopende band langs het tredeprofiel, vaste vloerplaten aan de koppen
    float sp = d.up ? d.speed : -d.speed;
    for (size_t k = 0; k + 1 < S.size(); k++) {
        float a = S[k], b = S[k + 1];
        Vec3 m = (F.P(0, 0, a) + F.P(0, 0, b)) * 0.5f;
        bool moving = a >= -0.01f && b <= L + 0.01f;
        col.addFloor({m.x, m.z, F.az.x, F.az.z, (b - a) / 2 + 0.01f, SKIRT_X, d.base.y + Y(a), d.base.y + Y(b), moving ? sp : 0.0f});
    }

    // begrenzing voor het tekenen
    Vec3 lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
    for (float s : {-N, L + N})
        for (float x : {-OUT_X, OUT_X})
            for (float y : {-TRUSS, d.rise + RAIL_Y}) {
                Vec3 p = F.P(x, y, s);
                lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
                hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
            }
    e.bmin = lo;
    e.bmax = hi;
    list_.push_back(e);
}

void Escalators::finish() {
    if (built_) return;
    built_ = true;
    // Trede in lokale coordinaten: oorsprong = midden bovenkant tredevloer, +z = naar boven (rijrichting omhoog)
    MeshBuilder mb;
    const float hw = STEP_W / 2, hd = PITCH / 2;
    // donkere basis met aluminium ribbels in de looprichting (ribbels als losse vlakjes: weinig hoekpunten)
    addBoxLocal(mb, -hw, hw, -0.035f, -0.006f, -hd, hd, STEP_BASE, EX_PLAIN);
    for (int k = 0; k < 36; k++) {
        float x = -hw + 0.06f + (STEP_W - 0.12f) * (k + 0.5f) / 36;
        mb.quad(Vec3(x - 0.0065f, -0.002f, -hd + 0.01f), Vec3(x - 0.0065f, -0.002f, hd - 0.05f), Vec3(x + 0.0065f, -0.002f, hd - 0.05f),
                Vec3(x + 0.0065f, -0.002f, -hd + 0.01f), Vec3(0, 1, 0), STEP_RIB, EX_PLAIN);
    }
    // gele afbakening: zijkanten en achterrand (bovenkant van de trede)
    auto yellowTop = [&](MeshBuilder& m) {
        m.quad(Vec3(-hw, 0.001f, -hd), Vec3(-hw, 0.001f, hd), Vec3(-hw + 0.055f, 0.001f, hd), Vec3(-hw + 0.055f, 0.001f, -hd), Vec3(0, 1, 0), YELLOW, EX_PLAIN);
        m.quad(Vec3(hw - 0.055f, 0.001f, -hd), Vec3(hw - 0.055f, 0.001f, hd), Vec3(hw, 0.001f, hd), Vec3(hw, 0.001f, -hd), Vec3(0, 1, 0), YELLOW, EX_PLAIN);
        m.quad(Vec3(-hw, 0.0015f, hd - 0.05f), Vec3(-hw, 0.0015f, hd), Vec3(hw, 0.0015f, hd), Vec3(hw, 0.0015f, hd - 0.05f), Vec3(0, 1, 0), YELLOW, EX_PLAIN);
    };
    yellowTop(mb);
    // stootbord (geribbeld) aan de onderkant: van de tredevloer naar beneden
    addBoxLocal(mb, -hw + 0.01f, hw - 0.01f, -0.245f, -0.01f, -hd, -hd + 0.012f, Vec3(0.30f, 0.31f, 0.32f), EX_PLAIN);
    for (int k = 0; k < 36; k++) {
        float x = -hw + 0.06f + (STEP_W - 0.12f) * (k + 0.5f) / 36;
        mb.quad(Vec3(x + 0.006f, -0.24f, -hd - 0.003f), Vec3(x + 0.006f, -0.012f, -hd - 0.003f), Vec3(x - 0.006f, -0.012f, -hd - 0.003f),
                Vec3(x - 0.006f, -0.24f, -hd - 0.003f), Vec3(0, 0, -1), STEP_RIB * 0.9f, EX_PLAIN);
    }
    // tredeframe (zijwangen) zodat de trede van opzij massief oogt
    addBoxLocal(mb, -hw, -hw + 0.02f, -0.23f, -0.03f, -hd, hd - 0.05f, Vec3(0.12f, 0.12f, 0.13f), EX_PLAIN);
    addBoxLocal(mb, hw - 0.02f, hw, -0.23f, -0.03f, -hd, hd - 0.05f, Vec3(0.12f, 0.12f, 0.13f), EX_PLAIN);
    // eenvoudige trede voor op afstand: grijze vloer met gele randen en een stootbord
    {
        MeshBuilder lo;
        addBoxLocal(lo, -hw, hw, -0.035f, 0.0f, -hd, hd, (STEP_BASE + STEP_RIB) * 0.5f, EX_PLAIN);
        yellowTop(lo);
        addBoxLocal(lo, -hw + 0.01f, hw - 0.01f, -0.245f, -0.01f, -hd, -hd + 0.012f, Vec3(0.40f, 0.41f, 0.42f), EX_PLAIN);
        stepLo_ = lo.upload();
    }
    step_ = mb.upload();
}

void Escalators::draw(World& world) const {
    if (list_.empty() || !built_) return;
    Vec3 cam = world.camPos();
    world.setMode(MODE_OBJECT);
    for (const Esc& e : list_) {
        if (World::boxDistance(cam, e.bmin, e.bmax) > 90.0f || !world.visible(e.bmin, e.bmax)) continue;
        const float L = e.L;
        const float span = L + 2 * PITCH;  // van onder de onderste kam tot onder de bovenste kam
        int n = (int)std::ceil(span / PITCH);
        float loop = n * PITCH;
        float phase = (float)std::fmod(time_ * e.d.speed, (double)loop);
        if (!e.d.up) phase = loop - phase;
        Vec3 ax(std::cos(e.d.heading), 0, -std::sin(e.d.heading)), az(std::sin(e.d.heading), 0, std::cos(e.d.heading));
        (void)ax;
        for (int k = 0; k < n; k++) {
            float s = std::fmod(k * PITCH + phase, loop) - PITCH;
            if (s < -PITCH * 0.9f || s > L + PITCH * 0.9f) continue;
            float y = profile(e, std::max(0.0f, std::min(L, s)));
            // onder de kamplaat net iets zakken zodat de trede er netjes onder verdwijnt
            if (s < 0) y += s * 0.08f;
            if (s > L) y -= (s - L) * 0.08f;
            Vec3 p = e.d.base + az * s + Vec3(0, y + 0.012f, 0);  // net boven de vloertegels
            world.setModel(Mat4::pose(p.x, p.y, p.z, e.d.heading));
            drawMesh(::length(p - cam) < 22.0f ? step_ : stepLo_);
        }
    }
    world.setModel(Mat4::identity());
}
