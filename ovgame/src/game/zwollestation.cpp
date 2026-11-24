// Station Zwolle: reizigerstunnel (2015) met opgangen naar alle eilandperrons, noordentree onder de vierkante kap
// (2003), zuidopgang naar het busstation/de Zuidzijde, stationskap (1995) boven perron 1 en 3/5 en perronkappen.
// Gereconstrueerd uit:
//  - OpenStreetMap: tunnel (indoor=corridor), roltrappen (conveying=yes), trappen, liftschachten, perrongaten,
//    winkels (Dunkin', AH to go, Bruna, The Doner Company), stationspiano, kaartautomaten, overkappingen
//  - Agrob Buchtal / Arcadis (Jeroen Eulderink): tunnel 120 x 17 x 3,5 m, wanden van mozaiek (2,5 cm) in wit en twee
//    grijzen in horizontale lagen als een zandsteenkloof (Antelope Canyon), stalen band met informatie erboven,
//    doorlopende lichtlijn, stalen plafondcassettes met gekleurd licht tussen de trappen, vrijstaande glazen
//    liftschachten, grote openingen naar de perrons met daglicht, videowand "Tijdtunnel" (Katzir, Hertog, Nadler)
//  - Wikipedia: perrontunnel 1952, verbreed van 7 naar 15 m (2011-2015), stationskap 1995, vierkante kap 2003
// Lokaal assenstelsel: zwolle.h (u langs de sporen, v naar het zuiden).
#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include "escalator.h"
#include "station.h"
#include "zwolle.h"

using namespace WorldFormat;
using namespace Zwolle;

namespace {
const Vec3 WHITE(1, 1, 1);
const Vec3 NSBLUE(0.0f, 0.13f, 0.43f);
const Vec3 NSYEL(1.0f, 0.78f, 0.0f);
const Vec3 STEEL(0.36f, 0.37f, 0.39f);
const Vec3 STEEL_LT(0.62f, 0.63f, 0.65f);
const Vec3 DARK(0.14f, 0.14f, 0.15f);
const Vec3 CONCRETE(0.64f, 0.64f, 0.62f);
const Vec3 FLOOR_T(0.62f, 0.62f, 0.60f);   // tunnelvloer: lichtgrijze tegels
const Vec3 STAIR_T(0.56f, 0.56f, 0.55f);
const Vec3 PLAT(0.60f, 0.60f, 0.58f);
const Vec3 GLASS_T(0.62f, 0.72f, 0.74f);
const Vec3 MOS_WHITE(0.93f, 0.93f, 0.91f), MOS_LGREY(0.80f, 0.80f, 0.79f), MOS_MGREY(0.64f, 0.64f, 0.64f), MOS_DARK(0.42f, 0.42f, 0.43f);
const Vec3 KAP_WHITE(0.90f, 0.91f, 0.92f);
const Vec3 ROOF_UNDER(0.86f, 0.87f, 0.88f);
const Vec3 LEDW(1.0f, 0.97f, 0.90f);

struct ZB {
    MeshBuilder& op;
    MeshBuilder& gl;
    Collision& col;
    SignAtlas& atlas;
    Vec3 U, V;
    float hU;
    ZB(MeshBuilder& o, MeshBuilder& g, Collision& c, SignAtlas& a) : op(o), gl(g), col(c), atlas(a) {
        Vec3 o0 = P(0, 0, 0);
        U = normalize(P(1, 0, 0) - o0);
        V = normalize(P(0, 1, 0) - o0);
        hU = std::atan2(U.x, U.z);
    }
    static Vec3 P(float u, float v, float y) {
        double rx, ry;
        toRd(u, v, rx, ry);
        return Vec3(gameX(rx), y, gameZ(ry));
    }
    float heading(float du, float dv) const {
        Vec3 d = U * du + V * dv;
        return std::atan2(d.x, d.z);
    }
    void box(float u0, float u1, float v0, float v1, float y0, float y1, Vec3 c, float ex = EX_PLAIN, bool solid = false) {
        op.orientedBox(P((u0 + u1) / 2, (v0 + v1) / 2, (y0 + y1) / 2), {v1 - v0, y1 - y0, u1 - u0}, hU, c, ex);
        if (solid) solidBox(u0, u1, v0, v1, y0, y1);
    }
    void glassBox(float u0, float u1, float v0, float v1, float y0, float y1, bool solid = true) {
        gl.orientedBox(P((u0 + u1) / 2, (v0 + v1) / 2, (y0 + y1) / 2), {v1 - v0, y1 - y0, u1 - u0}, hU, GLASS_T, EX_GLASS);
        if (solid) solidBox(u0, u1, v0, v1, y0, y1);
    }
    void solidBox(float u0, float u1, float v0, float v1, float y0, float y1) {
        Vec3 c = P((u0 + u1) / 2, (v0 + v1) / 2, 0);
        col.addBox({c.x, c.z, U.x, U.z, (u1 - u0) / 2, (v1 - v0) / 2, y0, y1});
    }
    void flat(float u0, float u1, float v0, float v1, float y, Vec3 c, float ex, bool up, bool walk = false) {
        op.quad(P(u0, v0, y), P(u1, v0, y), P(u1, v1, y), P(u0, v1, y), Vec3(0, up ? 1.0f : -1.0f, 0), c, ex);
        if (walk) floorCol(u0, u1, v0, v1, y);
    }
    void floorCol(float u0, float u1, float v0, float v1, float y) {
        Vec3 c = P((u0 + u1) / 2, (v0 + v1) / 2, 0);
        col.addFloor({c.x, c.z, U.x, U.z, (u1 - u0) / 2, (v1 - v0) / 2, y, y, 0});
    }
    // verticaal vlak tussen (ua,va) en (ub,vb), van y0 tot y1
    void wall(float ua, float va, float ub, float vb, float y0, float y1, Vec3 c, float ex, Vec3 n) {
        op.quad(P(ua, va, y0), P(ub, vb, y0), P(ub, vb, y1), P(ua, va, y1), n, c, ex);
    }
    void sign(const std::string& text, Vec3 bg, Vec3 fg, float u, float v, float y, Vec3 right, float h, bool twoSided = true) {
        SignRect r = atlas.add(text, bg, fg, 72);
        SignAtlas::quad(op, r, P(u, v, y), right, h, twoSided);
    }
    // vrijhangend bord aan twee stangen tot aan het plafond
    void hangSign(const std::string& text, float u, float v, float y, Vec3 right, float h, float ceil) {
        SignRect r = atlas.add(text, NSBLUE, WHITE, 72);
        Vec3 c = P(u, v, y);
        SignAtlas::quad(op, r, c, right, h, true);
        float w = h * r.aspect;
        for (int s = -1; s <= 1; s += 2) {
            Vec3 a = c + right * (s * (w / 2 - 0.25f)) + Vec3(0, h / 2, 0);
            op.tube(a, Vec3(a.x, ceil, a.z), 0.012f, 4, STEEL, EX_PLAIN);
        }
        op.orientedBox(c + Vec3(0, h / 2 + 0.02f, 0), {0.06f, 0.04f, w}, std::atan2(right.x, right.z), STEEL, EX_PLAIN);
    }
};

// ---------- mozaiekwand met 'zandsteenlagen' ----------
// Banden in de reizigerszone (tot 2,45 m): golvende grenzen, wit en twee grijzen; dan een stalen band met de doorlopende
// lichtlijn en daarboven een egale witte informatiezone.
struct Strata {
    float base[9] = {0.0f, 0.12f, 0.45f, 0.78f, 1.05f, 1.42f, 1.76f, 2.08f, 2.45f};
    Vec3 col[8] = {MOS_DARK, MOS_MGREY, MOS_LGREY, MOS_WHITE, MOS_MGREY, MOS_WHITE, MOS_LGREY, MOS_WHITE};
    float y(int k, float t) const {
        if (k == 0 || k == 1 || k == 8) return base[k];
        float a = 0.05f + 0.03f * (k % 3);
        return base[k] + a * std::sin(t * (0.21f + 0.05f * k) + k * 1.7f) + 0.03f * std::sin(t * 0.83f + k);
    }
};

void mosaicWall(ZB& z, float ua, float va, float ub, float vb, float floorY, float topY, float t0) {
    static const Strata S;
    float len = std::hypot(ub - ua, vb - va);
    if (len < 0.01f) return;
    Vec3 a = z.P(ua, va, 0), b = z.P(ub, vb, 0);
    Vec3 d = normalize(b - a);
    Vec3 n(-d.z, 0, d.x);  // normaal: links van de looprichting a -> b
    int pieces = std::max(1, (int)std::ceil(len / 0.8f));
    for (int p = 0; p < pieces; p++) {
        float f0 = (float)p / pieces, f1 = (float)(p + 1) / pieces;
        float u0 = ua + (ub - ua) * f0, v0 = va + (vb - va) * f0, u1 = ua + (ub - ua) * f1, v1 = va + (vb - va) * f1;
        float ta = t0 + len * f0, tb = t0 + len * f1;
        for (int k = 0; k < 8; k++) {
            float y0a = floorY + S.y(k, ta), y0b = floorY + S.y(k, tb), y1a = floorY + S.y(k + 1, ta), y1b = floorY + S.y(k + 1, tb);
            if (y0a >= topY) break;
            y1a = std::min(y1a, topY);
            y1b = std::min(y1b, topY);
            z.op.quad(z.P(u0, v0, y0a), z.P(u1, v1, y0b), z.P(u1, v1, y1b), z.P(u0, v0, y1a), n, S.col[k], EX_MOSAIC);
        }
    }
    float yb = floorY + S.base[8];
    if (topY <= yb) return;
    // stalen band + lichtlijn
    Vec3 off = n * 0.025f;
    z.op.quad(z.P(ua, va, yb) + off, z.P(ub, vb, yb) + off, z.P(ub, vb, yb + 0.16f) + off, z.P(ua, va, yb + 0.16f) + off, n, STEEL, EX_PLAIN);
    z.op.quad(z.P(ua, va, yb) + off * 0.0f, z.P(ub, vb, yb), z.P(ub, vb, yb) + off, z.P(ua, va, yb) + off, Vec3(0, -1, 0), STEEL, EX_PLAIN);
    z.op.quad(z.P(ua, va, yb + 0.16f) + off, z.P(ub, vb, yb + 0.16f) + off, z.P(ub, vb, yb + 0.16f), z.P(ua, va, yb + 0.16f), Vec3(0, 1, 0), STEEL, EX_PLAIN);
    z.op.quad(z.P(ua, va, yb + 0.17f) + off * 0.4f, z.P(ub, vb, yb + 0.17f) + off * 0.4f, z.P(ub, vb, yb + 0.21f) + off * 0.4f,
              z.P(ua, va, yb + 0.21f) + off * 0.4f, n, LEDW, EX_LIGHT);
    // informatiezone
    if (topY > yb + 0.21f) z.op.quad(z.P(ua, va, yb + 0.21f), z.P(ub, vb, yb + 0.21f), z.P(ub, vb, topY), z.P(ua, va, topY), n, MOS_WHITE * 0.97f, EX_MOSAIC);
    // botsing
    Vec3 m = (a + b) * 0.5f - n * 0.1f;
    z.col.addBox({m.x, m.z, d.x, d.z, len / 2, 0.1f, floorY - 0.5f, topY});
}

// mozaiekwand langs een boog (kwart ellips), middelpunt (cu, cv), stralen ru, rv, hoeken in graden
void mosaicArc(ZB& z, float cu, float cv, float ru, float rv, float a0, float a1, float floorY, float topY, float& t) {
    const int N = 8;
    for (int k = 0; k < N; k++) {
        float p0 = (a0 + (a1 - a0) * k / N) * PI / 180, p1 = (a0 + (a1 - a0) * (k + 1) / N) * PI / 180;
        float ua = cu + ru * std::cos(p0), va = cv + rv * std::sin(p0), ub = cu + ru * std::cos(p1), vb = cv + rv * std::sin(p1);
        mosaicWall(z, ua, va, ub, vb, floorY, topY, t);
        t += std::hypot(ub - ua, vb - va);
    }
}

// trap van (ua, va) [boven, hoogte yTop] naar (ub, vb) [onder, yBot], breedte w; optioneel fietsgoot aan een kant
void stairs(ZB& z, float ua, float va, float ub, float vb, float w, float yTop, float yBot, int gutterSide, float ex = EX_INDOOR) {
    float len = std::hypot(ub - ua, vb - va);
    float du = (ub - ua) / len, dv = (vb - va) / len;
    float rise = yTop - yBot;
    int n = std::max(2, (int)std::round(rise / 0.165f));
    float run = len / n, rh = rise / n;
    float hd = z.heading(du, dv);
    Vec3 D = normalize(z.U * du + z.V * dv), A(-D.z, 0, D.x);  // A = dwars
    Vec3 top = z.P(ua, va, 0);
    for (int k = 0; k < n; k++) {
        float ty = yTop - (k + 1) * rh;
        Vec3 c = top + D * ((k + 0.5f) * run);
        z.op.orientedBox(Vec3(c.x, (ty + yBot - 0.1f) / 2, c.z), {w, ty - yBot + 0.1f, run + 0.005f}, hd, k % 2 ? STAIR_T : STAIR_T * 0.96f, ex);
        // antislipneus (donker) en contraststrook
        Vec3 nose = top + D * (k * run + 0.04f);
        z.op.orientedBox(Vec3(nose.x, ty + 0.004f, nose.z), {w - 0.02f, 0.012f, 0.07f}, hd, Vec3(0.22f, 0.22f, 0.23f), EX_PLAIN);
    }
    Vec3 mid = top + D * (len / 2);
    z.col.addFloor({mid.x, mid.z, D.x, D.z, len / 2, w / 2, yTop, yBot, 0});
    // leuningen (rvs) aan beide kanten en in het midden bij brede trappen
    std::vector<float> rails{-w / 2 + 0.06f, w / 2 - 0.06f};
    if (w > 3.6f) rails.push_back(0.0f);
    for (float r : rails) {
        Vec3 a = top + A * r + Vec3(0, yTop + 0.9f, 0) - D * 0.3f, b = top + D * (len + 0.3f) + A * r + Vec3(0, yBot + 0.9f, 0);
        z.op.tube(a, b, 0.024f, 8, STEEL_LT, EX_PLAIN);
        for (int k = 0; k <= 4; k++) {
            float f = k / 4.0f;
            Vec3 p = top + D * (len * f) + A * r;
            float y = yTop - rise * f;
            z.op.tube(Vec3(p.x, y, p.z), Vec3(p.x, y + 0.9f, p.z), 0.018f, 6, STEEL_LT, EX_PLAIN);
        }
    }
    // fietsgoot (schuine betonstrook langs de wand)
    if (gutterSide != 0) {
        float r = gutterSide * (w / 2 - 0.2f);
        Vec3 a = top + A * r, b = top + D * len + A * r;
        Vec3 ga(a.x, yTop + 0.02f, a.z), gb(b.x, yBot + 0.02f, b.z);
        Vec3 s = A * 0.12f;
        z.op.quad(ga - s, gb - s, gb + s, ga + s, Vec3(0, 1, 0), Vec3(0.40f, 0.40f, 0.42f), EX_PLAIN);
    }
}

// Overkapping langs een OSM-veelhoek: dak in stroken (volgt de vorm), kolommen op de middellijn (of twee rijen
// als daar een trapgat zit), lichtlijnen. yBase = vloer, h = hoogte onderkant dak.
void canopy(ZB& z, const Poly& p, float yBase, float h, float u0clip, float u1clip, bool woodUnder = false) {
    float umin = 1e9f, umax = -1e9f;
    for (int k = 0; k < p.n; k++) { umin = std::min(umin, p.p[k][0]); umax = std::max(umax, p.p[k][0]); }
    umin = std::max(umin, u0clip);
    umax = std::min(umax, u1clip);
    auto range = [&](float u, float& a, float& b) {
        a = 1e9f; b = -1e9f;
        for (int k = 0, j = p.n - 1; k < p.n; j = k++) {
            float u0 = p.p[j][0], v0 = p.p[j][1], u1 = p.p[k][0], v1 = p.p[k][1];
            if ((u0 <= u && u1 > u) || (u1 <= u && u0 > u)) {
                float v = v0 + (v1 - v0) * (u - u0) / (u1 - u0);
                a = std::min(a, v); b = std::max(b, v);
            }
        }
        return b > a + 1.0f;
    };
    const float Y = yBase + h;
    const float step = 3.0f;
    Vec3 under = woodUnder ? Vec3(0.72f, 0.55f, 0.36f) : ROOF_UNDER;
    int idx = 0;
    for (float u = umin + 0.3f; u < umax - 0.3f; u += step, idx++) {
        float ub = std::min(umax - 0.3f, u + step);
        float a0, b0, a1, b1;
        if (!range(u + 0.05f, a0, b0) || !range(ub - 0.05f, a1, b1)) continue;
        // licht naar het midden aflopend (vlinderdak), dakrand
        float cm0 = (a0 + b0) / 2, cm1 = (a1 + b1) / 2;
        Vec3 A0 = z.P(u, a0, Y + 0.25f), B0 = z.P(u, b0, Y + 0.25f), A1 = z.P(ub, a1, Y + 0.25f), B1 = z.P(ub, b1, Y + 0.25f);
        Vec3 M0 = z.P(u, cm0, Y + 0.05f), M1 = z.P(ub, cm1, Y + 0.05f);
        // bovenkant
        z.op.quad(A0 + Vec3(0, 0.3f, 0), A1 + Vec3(0, 0.3f, 0), M1 + Vec3(0, 0.3f, 0), M0 + Vec3(0, 0.3f, 0), Vec3(0, 1, 0), Vec3(0.40f, 0.42f, 0.45f), EX_PLAIN);
        z.op.quad(M0 + Vec3(0, 0.3f, 0), M1 + Vec3(0, 0.3f, 0), B1 + Vec3(0, 0.3f, 0), B0 + Vec3(0, 0.3f, 0), Vec3(0, 1, 0), Vec3(0.40f, 0.42f, 0.45f), EX_PLAIN);
        // onderkant (zelflichtend licht, anders is het onder de kap pikdonker: zie de BGT-kappen)
        z.op.quad(A0, M0, M1, A1, Vec3(0, -1, 0), under * 0.92f, EX_INDOOR);
        z.op.quad(M0, B0, B1, M1, Vec3(0, -1, 0), under * 0.92f, EX_INDOOR);
        // dakranden
        z.op.quad(A0, A1, A1 + Vec3(0, 0.45f, 0), A0 + Vec3(0, 0.45f, 0), Vec3(0, 0, 0) - z.V, Vec3(0.30f, 0.31f, 0.33f), EX_PLAIN);
        z.op.quad(B1, B0, B0 + Vec3(0, 0.45f, 0), B1 + Vec3(0, 0.45f, 0), z.V, Vec3(0.30f, 0.31f, 0.33f), EX_PLAIN);
        // lichtlijnen langs beide randen
        for (float f : {0.18f, 0.82f}) {
            float va = a0 + (b0 - a0) * f, vb = a1 + (b1 - a1) * f;
            Vec3 la = z.P(u, va, Y + 0.12f), lb = z.P(ub, vb, Y + 0.12f);
            Vec3 s = z.V * 0.08f;
            z.op.quad(la - s, la + s, lb + s, lb - s, Vec3(0, -1, 0), LEDW, EX_LIGHT);
        }
        // kolommen
        if (idx % 4 == 0) {
            std::vector<float> rows;
            if (inOpening(u, cm0) || inOpening(u, cm0 + 2) || inOpening(u, cm0 - 2)) rows = {a0 + 0.6f, b0 - 0.6f};
            else rows = {cm0};
            for (float v : rows) {
                if (inOpening(u, v)) continue;
                Vec3 c = z.P(u, v, yBase);
                z.op.tube(c, Vec3(c.x, Y + 0.1f, c.z), 0.16f, 12, Vec3(0.30f, 0.32f, 0.35f), EX_PLAIN);
                z.op.orientedBox(Vec3(c.x, Y + 0.05f, c.z), {0.5f, 0.1f, 1.6f}, z.hU, Vec3(0.30f, 0.32f, 0.35f), EX_PLAIN);
                z.col.addBox({c.x, c.z, z.U.x, z.U.z, 0.17f, 0.17f, yBase, Y});
            }
        }
    }
}

// Winkel in de tunnelwand: open pui naar de tunnel (aan de kant 'side' = -1 west, +1 oost)
void tunnelShop(ZB& z, const char* name, Vec3 bg, Vec3 fg, float side, float v0, float v1, float depth, float F, float C) {
    float uw = side < 0 ? TUN_U0 : TUN_U1;
    float ub = uw + side * depth;
    float ua = std::min(uw, ub), ubb = std::max(uw, ub);
    z.flat(ua, ubb, v0, v1, F + 0.005f, Vec3(0.55f, 0.53f, 0.50f), EX_TILEFLOOR, true, true);
    z.flat(ua, ubb, v0, v1, F + 3.0f, WHITE * 0.92f, EX_INDOOR, false);
    for (float v = v0 + 1.0f; v < v1 - 0.6f; v += 1.8f) z.box(ua + 0.8f, ubb - 0.8f, v - 0.25f, v + 0.25f, F + 2.97f, F + 2.99f, LEDW, EX_LIGHT);
    // wanden
    z.box(ua, ubb, v0 - 0.15f, v0, F, C, WHITE * 0.9f, EX_INDOOR, true);
    z.box(ua, ubb, v1, v1 + 0.15f, F, C, WHITE * 0.9f, EX_INDOOR, true);
    float back = ub;
    z.box(std::min(back, back + side * 0.15f), std::max(back, back + side * 0.15f), v0, v1, F, C, bg * 0.9f, EX_INDOOR, true);
    // pui-latei met naam
    z.box(std::min(uw, uw + side * 0.2f), std::max(uw, uw + side * 0.2f), v0, v1, F + 3.0f, C, bg, EX_INDOOR);
    SignRect r = z.atlas.add(name, bg, fg, 72);
    float h = std::min(0.42f, (v1 - v0 - 0.8f) / r.aspect);
    SignAtlas::quad(z.op, r, z.P(uw - side * 0.02f, (v0 + v1) / 2, F + 3.25f), z.V * -side, h, false);
    // toonbank, schappen, verlichte achterwand
    float cu = uw + side * depth * 0.45f;
    z.box(std::min(cu, cu + side * 0.7f), std::max(cu, cu + side * 0.7f), v0 + 0.8f, v1 - 1.6f, F, F + 1.05f, bg * 0.8f, EX_INDOOR, true);
    for (int k = 0; k < 4; k++) {
        float bu = back - side * 0.45f;
        z.box(std::min(bu, bu + side * 0.4f), std::max(bu, bu + side * 0.4f), v0 + 0.4f, v1 - 0.4f, F + 0.35f + k * 0.5f, F + 0.4f + k * 0.5f,
              k % 2 ? fg * 0.8f : bg * 0.8f, EX_INDOOR);
    }
}

}  // namespace

void buildZwolle(const World& world, Collision& col, SignAtlas& atlas, MeshBuilder& op, MeshBuilder& gl, std::vector<Elevator>& elevators,
                 Escalators& esc, std::vector<BoardSpot>& boards, Vec3& tunnelSpawn) {
    ZB z(op, gl, col, atlas);
    // ---------- hoogtes ----------
    float psum = 0, p1sum = 0;
    int pn = 0, p1n = 0;
    for (const OsmPlatform& p : world.platforms) {
        if (p.pts.empty()) continue;
        float cx = 0, cz = 0;
        for (auto& q : p.pts) { cx += q.first; cz += q.second; }
        float u, v;
        local(rdX(cx / p.pts.size()), rdY(cz / p.pts.size()), u, v);
        if (u > -50 && u < 250 && v > 20 && v < 85) { psum += p.height; pn++; }
        if (u > -100 && u < 250 && v > 2 && v < 16) { p1sum += p.height; p1n++; }
    }
    if (pn == 0) return;  // Zwolle niet in deze wereld
    const float P = psum / pn;                 // eilandperrons
    const float P1 = p1n ? p1sum / p1n : P;    // perron 1 / noordentree
    const float TF = P - 6.30f;                // tunnelvloer
    const float TC = TF + 3.5f;                // plafond
    const float R = P - 0.76f;                 // spoor
    const float DECK = R - 0.55f;
    // busperron en Zuidzijde: maaiveld (gemeten, met terugval op spoorhoogte)
    auto ground = [&](float u, float v, float fallback) {
        Vec3 q = ZB::P(u, v, 0);
        float h = world.heightAt(q.x, q.z);
        return (h > TF + 2.0f && h < P + 3.0f) ? h : fallback;
    };
    const float HB = ground(52.0f, 104.0f, R + 0.1f) + 0.15f;  // busperron (verhoogd)
    const float HS = ground(84.0f, 148.0f, R);                 // straat Zuidzijde
    printf("[zwolle] P=%.2f P1=%.2f TF=%.2f HB=%.2f HS=%.2f\n", P, P1, TF, HB, HS);
    tunnelSpawn = ZB::P(85.0f, 40.0f, TF);

    // ================= TUNNELVLOER EN -PLAFOND =================
    z.flat(TUN_U0, TUN_U1, NORTH_HOLE.v0, SOUTH_HOLE.v0 + 0.5f, TF + 0.005f, FLOOR_T, EX_TILEFLOOR, true, true);
    // geleidelijnen (ribbeltegels) door de tunnel en naar de opgangen
    z.flat(84.6f, 84.9f, 10.6f, 126.0f, TF + 0.009f, Vec3(0.78f, 0.78f, 0.76f), EX_INDOOR, true);

    // plafond: cassettes met gekleurd licht tussen de opgangen, egaal wit bij de opgangen
    std::vector<std::pair<float, float>> cassetteZones;
    {
        float v = 10.6f;
        for (int k = 0; k < 3; k++) {
            cassetteZones.push_back({v, ISLANDS[k].v0 - 1.0f});
            v = ISLANDS[k].v1 + 1.0f;
        }
        cassetteZones.push_back({v, TUN_V1});
    }
    const Vec3 PAL[6] = {Vec3(0.85f, 0.30f, 0.70f), Vec3(0.95f, 0.55f, 0.20f), Vec3(0.90f, 0.80f, 0.55f), Vec3(0.45f, 0.35f, 0.90f), Vec3(0.30f, 0.65f, 0.85f), Vec3(0.95f, 0.40f, 0.35f)};
    for (size_t zi = 0; zi < cassetteZones.size(); zi++) {
        float va = cassetteZones[zi].first, vb = cassetteZones[zi].second;
        if (vb - va < 3) continue;
        const int NU = 6;
        float cw = (TUN_U1 - TUN_U0) / NU;
        int NV = std::max(1, (int)std::round((vb - va) / cw));
        float ch = (vb - va) / NV;
        for (int i = 0; i < NU; i++)
            for (int j = 0; j < NV; j++) {
                float u0 = TUN_U0 + i * cw, u1 = u0 + cw, v0 = va + j * ch, v1 = v0 + ch;
                const float bw = 0.22f;  // balk
                bool high = (i + j) % 2 == 0;  // afwisselend hogere en lagere cassettes
                float top = TC + (high ? 0.55f : 0.32f);
                // balken (onderkant op TC)
                z.flat(u0, u1, v0, v0 + bw, TC, STEEL * 1.2f, EX_INDOOR, false);
                z.flat(u0, u0 + bw, v0 + bw, v1, TC, STEEL * 1.2f, EX_INDOOR, false);
                // cassettewanden (staal)
                z.wall(u0 + bw, v0 + bw, u1, v0 + bw, TC, top, STEEL_LT, EX_INDOOR, z.V);
                z.wall(u0 + bw, v1, u1, v1, TC, top, STEEL_LT, EX_INDOOR, z.V * -1.0f);
                z.wall(u0 + bw, v0 + bw, u0 + bw, v1, TC, top, STEEL_LT, EX_INDOOR, z.U);
                z.wall(u1, v0 + bw, u1, v1, TC, top, STEEL_LT, EX_INDOOR, z.U * -1.0f);
                // lichtvlak met kleur (verloop door de tunnel)
                float tcol = (v0 - 10.0f) / 18.0f + i * 0.15f;
                int ci = ((int)std::floor(tcol)) % 6;
                float f = tcol - std::floor(tcol);
                Vec3 c = PAL[ci] * (1 - f) + PAL[(ci + 1) % 6] * f;
                z.flat(u0 + bw, u1, v0 + bw, v1, top, c * 0.35f + Vec3(0.40f, 0.40f, 0.40f), EX_LIGHT, false);
                z.flat(u0 + bw + 0.35f, u1 - 0.35f, v0 + bw + 0.35f, v1 - 0.35f, top - 0.01f, c * 0.55f + Vec3(0.35f, 0.35f, 0.35f), EX_LIGHT, false);
            }
        // laatste balken aan de rand
        z.flat(TUN_U0, TUN_U1, vb - 0.22f, vb, TC, STEEL * 1.2f, EX_INDOOR, false);
    }
    // egaal plafond met lichtlijnen bij de opgangen (behalve boven de gaten van de trappenhuizen)
    for (int k = 0; k < 3; k++) {
        const Island& is = ISLANDS[k];
        float va = is.v0 - 1.0f, vb = is.v1 + 1.0f;
        // westrand van het gat ligt tot u 78, oostrand vanaf 90,4
        z.flat(TUN_U0, WEST_U1, va, is.v0, TC, WHITE * 0.9f, EX_INDOOR, false);
        z.flat(TUN_U0, WEST_U1, is.v1, vb, TC, WHITE * 0.9f, EX_INDOOR, false);
        z.flat(EAST_U0, TUN_U1, va, is.v0, TC, WHITE * 0.9f, EX_INDOOR, false);
        z.flat(EAST_U0, TUN_U1, is.v1, vb, TC, WHITE * 0.9f, EX_INDOOR, false);
        z.flat(WEST_U1, EAST_U0, va, vb, TC, WHITE * 0.9f, EX_INDOOR, false);
        for (float u = WEST_U1 + 0.8f; u < EAST_U0; u += 2.2f)
            if (u < LIFT_U0 - 0.3f || u > LIFT_U1 + 0.3f) z.box(u - 0.06f, u + 0.06f, va + 0.3f, vb - 0.3f, TC - 0.03f, TC - 0.005f, LEDW, EX_LIGHT);
        // randen van het perrongat boven de tunnel (zichtbaar vanuit de tunnel)
        z.box(WEST_U1 - 0.02f, WEST_U1 + 0.3f, is.v0, is.v1, TC, P, WHITE * 0.9f, EX_INDOOR);
        z.box(EAST_U0 - 0.3f, EAST_U0 + 0.02f, is.v0, is.v1, TC, P, WHITE * 0.9f, EX_INDOOR);
    }
    // spoordek boven de tunnel (tussen de perrons; dicht)
    z.box(TUN_U0 - 0.5f, TUN_U1 + 0.5f, 13.4f, ISLANDS[0].v0 - 2.6f, TC + 0.6f, DECK, CONCRETE);
    z.box(TUN_U0 - 0.5f, TUN_U1 + 0.5f, 37.0f, ISLANDS[1].v0 - 3.6f, TC + 0.6f, DECK, CONCRETE);
    z.box(TUN_U0 - 0.5f, TUN_U1 + 0.5f, 57.0f, ISLANDS[2].v0 - 3.6f, TC + 0.6f, DECK, CONCRETE);

    // ================= TUNNELWANDEN =================
    {
        // westwand (u = TUN_U0, normaal naar +u), van noord naar zuid met openingen naar de trappenhuizen
        float t = 0;
        float v = NORTH_HOLE.v0;
        const float gaps[2][2] = {{100.8f, 105.8f}, {117.0f, 127.8f}};  // winkels (Dunkin', Bruna)
        auto westRun = [&](float v1, float top) {
            float a = v;
            for (auto& g : gaps) {
                if (g[1] <= a || g[0] >= v1) continue;
                if (g[0] > a + 0.01f) mosaicWall(z, TUN_U0, g[0], TUN_U0, a, TF, top, t + (a - v));
                a = g[1];
            }
            if (v1 > a + 0.01f) mosaicWall(z, TUN_U0, v1, TUN_U0, a, TF, top, t + (a - v));
            t += std::max(0.0f, v1 - v);
        };
        // noordtrappenhuis: wanden tot de plein-/perron 1-hoogte
        westRun(TUN_V0, P1);
        v = TUN_V0;
        for (int k = 0; k < 4; k++) {
            const Island& is = ISLANDS[k];
            float r = k < 3 ? 3.4f : 2.0f;
            westRun(is.v0 - r, TC);
            // boog naar het trappenhuis, dan de zijwand van het trappenhuis (tot perronhoogte)
            float top = k < 3 ? P : HB;
            mosaicArc(z, TUN_U0 - r, is.v0 - r, r, r, 90, 0, TF, TC, t);
            mosaicWall(z, WEST_U0, is.v0 + 0.03f, TUN_U0 - r, is.v0 + 0.03f, TF, top, t);
            mosaicWall(z, TUN_U0 - r, is.v1 - 0.03f, WEST_U0, is.v1 - 0.03f, TF, top, t);
            mosaicArc(z, TUN_U0 - r, is.v1 + r, r, r, 0, -90, TF, TC, t);
            // eindwand van het trappenhuis (bovenkant van de roltrap/trap)
            mosaicWall(z, WEST_U0 + 0.03f, is.v1, WEST_U0 + 0.03f, is.v0, TF, top, t);
            // boven de boog: dicht tot het perron (tussen tunnelplafond en perron)
            if (k < 3) {
                z.box(TUN_U0 - r, WEST_U1, is.v0, is.v0 + 0.05f, TC, top, WHITE * 0.9f, EX_INDOOR);
                z.box(TUN_U0 - r, WEST_U1, is.v1 - 0.05f, is.v1, TC, top, WHITE * 0.9f, EX_INDOOR);
            }
            v = is.v1 + r;
        }
        westRun(SOUTH_HOLE.v0, TC);
    }
    {
        // oostwand (u = TUN_U1, normaal naar -u)
        float t = 500;
        float v = NORTH_HOLE.v0;
        const float gaps[3][2] = {{100.8f, 105.8f}, {116.0f, 121.6f}, {122.0f, 127.8f}};  // AH to go, Doner, toiletten
        auto eastRun = [&](float v1, float top) {
            float a = v;
            for (auto& g : gaps) {
                if (g[1] <= a || g[0] >= v1) continue;
                if (g[0] > a + 0.01f) mosaicWall(z, TUN_U1, a, TUN_U1, g[0], TF, top, t + (a - v));
                a = g[1];
            }
            if (v1 > a + 0.01f) mosaicWall(z, TUN_U1, a, TUN_U1, v1, TF, top, t + (a - v));
            t += std::max(0.0f, v1 - v);
        };
        eastRun(TUN_V0, P1);
        v = TUN_V0;
        for (int k = 0; k < 4; k++) {
            const Island& is = ISLANDS[k];
            float r = k < 3 ? 3.4f : 2.0f;
            eastRun(is.v0 - r, TC);
            float top = k < 3 ? P : HB;
            mosaicArc(z, TUN_U1 + r, is.v0 - r, r, r, 180, 90, TF, TC, t);
            mosaicWall(z, TUN_U1 + r, is.v0 + 0.03f, EAST_U1, is.v0 + 0.03f, TF, top, t);
            mosaicWall(z, EAST_U1, is.v1 - 0.03f, TUN_U1 + r, is.v1 - 0.03f, TF, top, t);
            mosaicArc(z, TUN_U1 + r, is.v1 + r, r, r, 270, 180, TF, TC, t);
            mosaicWall(z, EAST_U1 - 0.03f, is.v0, EAST_U1 - 0.03f, is.v1, TF, top, t);
            if (k < 3) {
                z.box(EAST_U0, TUN_U1 + r, is.v0, is.v0 + 0.05f, TC, top, WHITE * 0.9f, EX_INDOOR);
                z.box(EAST_U0, TUN_U1 + r, is.v1 - 0.05f, is.v1, TC, top, WHITE * 0.9f, EX_INDOOR);
            }
            v = is.v1 + r;
        }
        eastRun(SOUTH_HOLE.v0, TC);
    }
    // noordwand van het noordtrappenhuis (achter de bovenkant van de roltrappen)
    mosaicWall(z, TUN_U0, NORTH_HOLE.v0 + 0.02f, TUN_U1, NORTH_HOLE.v0 + 0.02f, TF, P1, 900);

    // ================= OPGANGEN NAAR DE EILANDPERRONS (en het busperron) =================
    const char* dests[3] = {"Spoor 3 | 5", "Spoor 6 | 7", "Spoor 9 | 10"};
    for (int k = 0; k < 4; k++) {
        const Island& is = ISLANDS[k];
        const float top = k < 3 ? P : HB;
        const float rise = top - TF;
        const float L = Escalators::length(rise);
        // trappenhuisvloer
        z.flat(WEST_U0, TUN_U0, is.v0, is.v1, TF + 0.004f, FLOOR_T, EX_TILEFLOOR, true, true);
        z.flat(TUN_U1, EAST_U1, is.v0, is.v1, TF + 0.004f, FLOOR_T, EX_TILEFLOOR, true, true);
        for (int side = -1; side <= 1; side += 2) {
            bool west = side < 0;
            bool up = west ? is.westUp : is.eastUp;
            // roltrap: bovenste kop tegen de eindwand van het trappenhuis
            float uTopNewel = west ? WEST_U0 : EAST_U1;
            float uTopComb = uTopNewel + (west ? Escalators::NEWEL : -Escalators::NEWEL);
            float uBotComb = uTopComb + (west ? L : -L);
            EscalatorDef d;
            d.base = ZB::P(uBotComb, is.escV, TF);
            d.heading = z.heading(west ? -1.0f : 1.0f, 0.0f);
            d.rise = rise;
            d.up = up;
            esc.add(d, op, gl, col);
            // dichte wand onder de roltrap aan de trapkant (vakwerk en machinekamer)
            {
                float vw = is.escV + 0.84f;
                std::vector<float> S;
                for (float s = 0; s < L; s += 0.5f) S.push_back(s);
                S.push_back(L);
                for (size_t q = 0; q + 1 < S.size(); q++) {
                    float sa = S[q], sb = S[q + 1];
                    float ya = Escalators::heightAt(rise, sa) - 1.05f, yb = Escalators::heightAt(rise, sb) - 1.05f;
                    if (ya < 0.05f && yb < 0.05f) continue;
                    float ua = uBotComb + (west ? -sa : sa), ubb = uBotComb + (west ? -sb : sb);
                    z.op.quad(z.P(ua, vw, TF), z.P(ubb, vw, TF), z.P(ubb, vw, TF + std::max(0.0f, yb)), z.P(ua, vw, TF + std::max(0.0f, ya)), z.V,
                              CONCRETE * 1.1f, EX_INDOOR);
                }
                float ua = std::min(uBotComb, uTopComb), ubb = std::max(uBotComb, uTopComb);
                Vec3 c = ZB::P((ua + ubb) / 2, vw, 0);
                col.addBox({c.x, c.z, z.U.x, z.U.z, (ubb - ua) / 2, 0.05f, TF, top});
            }
            // vaste trap: boven bij de eindwand + 1,5 m bordes, onder richting de tunnel
            float sv0 = is.escV + 0.95f, sv1 = is.v1 - 0.1f;
            float uTop = west ? WEST_U0 + 1.5f : EAST_U1 - 1.5f;
            float run = rise / 0.165f * 0.30f;  // ~38 treden van 30 cm
            float uBot = uTop + (west ? run : -run);
            stairs(z, uTop, (sv0 + sv1) / 2, uBot, (sv0 + sv1) / 2, sv1 - sv0, top, TF, 1);
            // bordes boven (op perronhoogte) tussen eindwand en trap
            float ba = std::min(uTop, uTopNewel), bb = std::max(uTop, uTopNewel);
            z.flat(ba, bb, sv0 - 0.05f, sv1 + 0.1f, top + 0.003f, PLAT, EX_PLAIN, true, true);
            // balustrades rond het gat (op het perron): langs beide lange randen en aan de tunnelkant
            float hu0 = west ? WEST_U0 : EAST_U0, hu1 = west ? WEST_U1 : EAST_U1;
            float openU = west ? WEST_U0 + 0.4f : EAST_U1 - 0.4f;  // instap: niet dicht
            float bu0 = west ? openU + 1.2f : hu0, bu1 = west ? hu1 : openU - 1.2f;
            for (float ev : {is.v0, is.v1}) {
                float a = ev == is.v0 ? ev : ev - 0.06f;
                z.glassBox(bu0, bu1, a, a + 0.06f, top, top + 1.1f);
                z.box(bu0, bu1, a - 0.02f, a + 0.08f, top + 1.08f, top + 1.13f, STEEL_LT);
                for (float u = bu0; u <= bu1 + 0.01f; u += 1.5f) z.box(u - 0.03f, u + 0.03f, a, a + 0.06f, top, top + 1.1f, STEEL);
            }
            float eu = west ? hu1 : hu0;
            z.glassBox(eu - 0.03f, eu + 0.03f, is.v0, is.v1, top, top + 1.1f);
            z.box(eu - 0.05f, eu + 0.05f, is.v0, is.v1, top + 1.08f, top + 1.13f, STEEL_LT);
            // borden in de tunnel boven de opgang (leesbaar vanuit de tunnel) en op het perron
            float su = west ? TUN_U0 + 1.2f : TUN_U1 - 1.2f;
            std::string label = k < 3 ? dests[k] : "Busstation  |  Zuidzijde";
            z.hangSign(label, su, is.v0 - 0.6f, TC - 0.45f, z.U, 0.42f, TC);
            z.hangSign(label, su, is.v1 + 0.6f, TC - 0.45f, z.U, 0.42f, TC);
            if (k < 3) {
                std::string out = "Uitgang  Centrum  |  Zuid  |  Bus";
                float pu = west ? WEST_U0 - 0.5f : EAST_U1 + 0.5f;  // boven de instap
                z.sign(out, NSBLUE, WHITE, pu, (is.v0 + is.v1) / 2, top + 3.1f, z.V, 0.40f);
                Vec3 base = ZB::P(pu, (is.v0 + is.v1) / 2, top);
                z.op.tube(base, base + Vec3(0, 2.9f, 0), 0.04f, 6, STEEL, EX_PLAIN);
            }
        }
        // glazen liftschacht midden in de tunnel
        float lv0 = k < 3 ? (is.v0 + is.v1) / 2 - 1.7f : 108.7f, lv1 = k < 3 ? (is.v0 + is.v1) / 2 + 1.7f : 111.1f;
        float lu0 = LIFT_U0, lu1 = LIFT_U1;
        z.glassBox(lu0, lu1, lv0, lv1, TF, top + 2.5f, true);
        for (float u : {lu0, lu1})
            for (float v : {lv0, lv1}) z.box(u - 0.06f, u + 0.06f, v - 0.06f, v + 0.06f, TF, top + 2.5f, DARK);
        z.box(lu0 - 0.08f, lu1 + 0.08f, lv0 - 0.08f, lv1 + 0.08f, top + 2.5f, top + 2.7f, STEEL);
        // lichtlijn op dezelfde hoogte als langs de wanden
        z.box(lu0 - 0.02f, lu1 + 0.02f, lv0 - 0.02f, lv1 + 0.02f, TF + 2.62f, TF + 2.66f, LEDW, EX_LIGHT);
        // liftkooi (zichtbaar door het glas) en deuren aan de oostkant
        z.box(lu0 + 0.15f, lu1 - 0.15f, lv0 + 0.15f, lv1 - 0.15f, TF + 0.02f, TF + 0.08f, STEEL);
        z.box(lu1 - 0.02f, lu1 + 0.04f, (lv0 + lv1) / 2 - 0.55f, (lv0 + lv1) / 2 + 0.55f, TF, TF + 2.2f, STEEL_LT);
        z.box(lu1 - 0.02f, lu1 + 0.04f, (lv0 + lv1) / 2 - 0.55f, (lv0 + lv1) / 2 + 0.55f, top, top + 2.2f, STEEL_LT);
        Elevator el;
        el.bottom = ZB::P(lu1 + 0.9f, (lv0 + lv1) / 2, TF);
        el.top = ZB::P(lu1 + 0.9f, (lv0 + lv1) / 2, top);
        el.label = k < 3 ? std::string("perron ") + (k == 0 ? "3/5" : k == 1 ? "6/7" : "9/10") : std::string("busstation");
        el.topLabel = "de tunnel";
        elevators.push_back(el);
    }

    // ================= NOORDOPGANG (entree centrumzijde, onder de vierkante kap) =================
    {
        const float rise = P1 - TF;
        const float L = Escalators::length(rise);
        // roltrappen langs v: onderste kop bij v 10,6, bovenkant richting het noorden
        for (int k = 0; k < 2; k++) {
            float u = k == 0 ? 81.9f : 86.6f;
            EscalatorDef d;
            float botComb = 10.6f - Escalators::NEWEL;
            d.base = ZB::P(u, botComb, TF);
            d.heading = z.heading(0.0f, -1.0f);
            d.rise = rise;
            d.up = k == 1;  // OSM: 81,9 naar beneden, 86,2 omhoog
            esc.add(d, op, gl, col);
            (void)L;
        }
        // vaste trappen aan weerszijden (met fietsgoot)
        stairs(z, 78.7f, -3.0f, 78.7f, 10.2f, 4.6f, P1, TF, -1);
        stairs(z, 90.6f, -3.0f, 90.6f, 10.2f, 4.6f, P1, TF, 1);
        // lift tussen de roltrappen
        z.glassBox(83.0f, 85.4f, -2.9f, 0.1f, TF, P1 + 2.5f, true);
        z.box(82.92f, 85.48f, -2.98f, 0.18f, P1 + 2.5f, P1 + 2.7f, STEEL);
        z.box(83.0f, 85.4f, -2.9f, 0.1f, TF + 2.62f, TF + 2.66f, LEDW, EX_LIGHT);
        Elevator el;
        el.bottom = ZB::P(84.2f, 0.9f, TF);
        el.top = ZB::P(84.2f, -3.7f, P1);
        el.label = "uitgang Centrum";
        el.topLabel = "de tunnel";
        elevators.push_back(el);
        // plein onder de vierkante kap (perron 1-hoogte), met gat voor de opgang
        const Rect& h = NORTH_HOLE;
        z.flat(SQUARE.u0, SQUARE.u1, SQUARE.v0, h.v0, P1 + 0.004f, Vec3(0.66f, 0.65f, 0.62f), EX_PLAIN, true, true);
        z.flat(SQUARE.u0, h.u0, h.v0, SQUARE.v1, P1 + 0.004f, Vec3(0.66f, 0.65f, 0.62f), EX_PLAIN, true, true);
        z.flat(h.u1, SQUARE.u1, h.v0, SQUARE.v1, P1 + 0.004f, Vec3(0.66f, 0.65f, 0.62f), EX_PLAIN, true, true);
        z.flat(h.u0, h.u1, h.v1, SQUARE.v1, P1 + 0.004f, Vec3(0.66f, 0.65f, 0.62f), EX_PLAIN, true, true);
        // bordessen boven de trappen en bij de lift (niet over de roltrappen: daar lopen de treden)
        for (auto ur : {std::make_pair(h.u0, 81.08f), std::make_pair(82.72f, 85.78f), std::make_pair(87.42f, h.u1)})
            z.flat(ur.first, ur.second, h.v0, -2.9f, P1 + 0.004f, Vec3(0.66f, 0.65f, 0.62f), EX_PLAIN, true, true);
        // balustrades rond het gat (open aan de noordkant)
        for (float u : {h.u0, h.u1}) {
            z.glassBox(u - 0.03f, u + 0.03f, h.v0 + 1.4f, h.v1, P1, P1 + 1.1f);
            z.box(u - 0.05f, u + 0.05f, h.v0 + 1.4f, h.v1, P1 + 1.08f, P1 + 1.13f, STEEL_LT);
        }
        z.glassBox(h.u0, h.u1, h.v1 - 0.06f, h.v1, P1, P1 + 1.1f);
        z.box(h.u0, h.u1, h.v1 - 0.08f, h.v1 + 0.02f, P1 + 1.08f, P1 + 1.13f, STEEL_LT);
        // rand van het gat (onderkant plein boven de tunnel)
        z.box(h.u0, h.u1, h.v1 - 0.02f, h.v1 + 0.4f, TC, P1, WHITE * 0.9f, EX_INDOOR);
        // incheckpoortjes (NS): rij over het plein, met de lift en een brede poort vrij
        for (float u = 68.0f; u < 96.0f; u += 0.95f) {
            if (u > 82.5f && u < 86.0f) continue;
            float v = -9.5f;
            z.box(u - 0.10f, u + 0.10f, v - 0.5f, v + 0.5f, P1, P1 + 0.98f, Vec3(0.74f, 0.75f, 0.77f), EX_PLAIN);
            z.box(u - 0.11f, u + 0.11f, v - 0.5f, v + 0.5f, P1 + 0.98f, P1 + 1.0f, Vec3(0.20f, 0.20f, 0.22f), EX_PLAIN);
            z.box(u - 0.10f, u + 0.10f, v - 0.42f, v - 0.22f, P1 + 1.0f, P1 + 1.03f, NSYEL, EX_PLAIN);  // kaartlezer
            z.box(u - 0.105f, u + 0.105f, v + 0.25f, v + 0.4f, P1 + 0.995f, P1 + 1.005f, Vec3(0.2f, 0.9f, 0.4f), EX_LIGHT);
            z.gl.orientedBox(ZB::P(u + 0.47f, v, P1 + 0.75f), {0.03f, 0.9f, 0.36f}, z.hU, GLASS_T, EX_GLASS);  // klapdeurtjes
        }
        z.sign("Zwolle", NSBLUE, WHITE, 81.0f, SQUARE.v0 + 0.3f, P1 + 6.0f, z.U * -1.0f, 1.1f);
        z.hangSign("Treinen  |  Spoor 1 - 16", 84.2f, -6.5f, P1 + 3.6f, z.U, 0.45f, P1 + 7.6f);
        // vierkante kap (2003): plat stalen dak met lichtstraten op slanke kolommen
        const float KY = P1 + 7.6f;
        // dakrand rondom
        z.box(SQUARE.u0, SQUARE.u1, SQUARE.v0, SQUARE.v0 + 0.4f, KY - 0.2f, KY + 0.6f, Vec3(0.32f, 0.33f, 0.35f));
        z.box(SQUARE.u0, SQUARE.u1, SQUARE.v1 - 0.4f, SQUARE.v1, KY - 0.2f, KY + 0.6f, Vec3(0.32f, 0.33f, 0.35f));
        z.box(SQUARE.u0, SQUARE.u0 + 0.4f, SQUARE.v0, SQUARE.v1, KY - 0.2f, KY + 0.6f, Vec3(0.32f, 0.33f, 0.35f));
        z.box(SQUARE.u1 - 0.4f, SQUARE.u1, SQUARE.v0, SQUARE.v1, KY - 0.2f, KY + 0.6f, Vec3(0.32f, 0.33f, 0.35f));
        for (float u = SQUARE.u0 + 2.0f; u < SQUARE.u1 - 1; u += 4.0f)
            for (float v = SQUARE.v0 + 2.0f; v < SQUARE.v1 - 1; v += 4.0f) {
                bool sky = ((int)((u - SQUARE.u0) / 4) + (int)((v - SQUARE.v0) / 4)) % 3 == 0;
                if (sky) z.gl.orientedBox(ZB::P(u, v, KY + 0.3f), {3.9f, 0.02f, 3.9f}, z.hU, GLASS_T, EX_GLASS);
                else {
                    z.flat(u - 2.0f, u + 2.0f, v - 2.0f, v + 2.0f, KY - 0.005f, ROOF_UNDER, EX_INDOOR, false);
                    z.flat(u - 2.0f, u + 2.0f, v - 2.0f, v + 2.0f, KY + 0.55f, Vec3(0.42f, 0.43f, 0.45f), EX_PLAIN, true);
                    z.box(u - 0.5f, u + 0.5f, v - 0.5f, v + 0.5f, KY - 0.03f, KY - 0.008f, LEDW, EX_LIGHT);
                }
                z.box(u - 0.05f, u + 0.05f, v - 2.0f, v + 2.0f, KY - 0.35f, KY, STEEL);
                z.box(u - 2.0f, u + 2.0f, v - 0.05f, v + 0.05f, KY - 0.35f, KY, STEEL);
            }
        for (float u : {66.5f, 95.8f})
            for (float v : {-19.0f, -7.5f, 6.5f}) {
                Vec3 c = ZB::P(u, v, P1);
                z.op.tube(c, Vec3(c.x, KY, c.z), 0.2f, 14, WHITE * 0.85f, EX_PLAIN);
                col.addBox({c.x, c.z, z.U.x, z.U.z, 0.2f, 0.2f, P1, KY});
            }
        // kaartautomaten
        for (float u : {68.0f, 69.2f}) {
            z.box(u - 0.5f, u + 0.5f, -16.0f, -15.4f, P1, P1 + 1.8f, NSYEL, EX_PLAIN, true);
            z.box(u - 0.35f, u + 0.35f, -16.04f, -16.0f, P1 + 0.9f, P1 + 1.5f, Vec3(0.05f, 0.1f, 0.2f));
        }
    }

    // ================= ZUIDZONE: WINKELS, KAARTAUTOMATEN EN OPGANG NAAR DE ZUIDZIJDE =================
    {
        // winkels in de tunnelwanden (de wand loopt hier door, de pui is open)
        tunnelShop(z, "Dunkin'", Vec3(1.0f, 0.45f, 0.05f), Vec3(0.85f, 0.10f, 0.50f), -1, 100.8f, 105.8f, 5.8f, TF, TC);
        tunnelShop(z, "Bruna", Vec3(0.0f, 0.25f, 0.55f), Vec3(1.0f, 0.85f, 0.0f), -1, 117.0f, 127.8f, 5.8f, TF, TC);
        tunnelShop(z, "AH to go", Vec3(0.0f, 0.62f, 0.88f), WHITE, 1, 100.8f, 105.8f, 4.4f, TF, TC);
        tunnelShop(z, "The Doner Company", Vec3(0.10f, 0.10f, 0.10f), Vec3(1.0f, 0.8f, 0.2f), 1, 116.0f, 121.6f, 4.4f, TF, TC);
        tunnelShop(z, "Toiletten", Vec3(0.30f, 0.32f, 0.35f), WHITE, 1, 122.0f, 127.8f, 4.4f, TF, TC);
        // plafond zuidzone
        z.flat(TUN_U0, TUN_U1, TUN_V1, ISLANDS[3].v0 - 2.0f, TC, WHITE * 0.9f, EX_INDOOR, false);
        z.flat(TUN_U0, TUN_U1, ISLANDS[3].v1 + 2.0f, SOUTH_HOLE.v0, TC, WHITE * 0.9f, EX_INDOOR, false);
        z.flat(TUN_U0, WEST_U1, ISLANDS[3].v0 - 2.0f, ISLANDS[3].v0, TC, WHITE * 0.9f, EX_INDOOR, false);
        z.flat(TUN_U0, WEST_U1, ISLANDS[3].v1, ISLANDS[3].v1 + 2.0f, TC, WHITE * 0.9f, EX_INDOOR, false);
        z.flat(EAST_U0, TUN_U1, ISLANDS[3].v0 - 2.0f, ISLANDS[3].v0, TC, WHITE * 0.9f, EX_INDOOR, false);
        z.flat(EAST_U0, TUN_U1, ISLANDS[3].v1, ISLANDS[3].v1 + 2.0f, TC, WHITE * 0.9f, EX_INDOOR, false);
        z.flat(WEST_U1, EAST_U0, ISLANDS[3].v0 - 2.0f, ISLANDS[3].v1 + 2.0f, TC, WHITE * 0.9f, EX_INDOOR, false);
        for (float v = TUN_V1 + 1.5f; v < SOUTH_HOLE.v0 - 1; v += 3.0f)
            for (float u = TUN_U0 + 2.0f; u < TUN_U1 - 1.5f; u += 3.4f) {
                if (v > ISLANDS[3].v0 - 2.0f && v < ISLANDS[3].v1 + 2.0f && (u < WEST_U1 + 0.5f || u > EAST_U0 - 0.5f)) continue;
                z.box(u - 0.25f, u + 0.25f, v - 0.25f, v + 0.25f, TC - 0.02f, TC - 0.005f, LEDW, EX_LIGHT);
            }
        // kaartautomaten bij de lift naar het busstation
        for (float u : {80.6f, 81.8f}) {
            z.box(u - 0.5f, u + 0.5f, 102.4f, 103.0f, TF, TF + 1.8f, NSYEL, EX_INDOOR, true);
            z.box(u - 0.35f, u + 0.35f, 102.36f, 102.4f, TF + 0.9f, TF + 1.5f, Vec3(0.05f, 0.1f, 0.2f), EX_INDOOR);
        }
        // opgang naar de Zuidzijde: roltrappen (82,2 omhoog, 86,5 omlaag), trappen, lift
        const float rise = HS - TF;
        for (int k = 0; k < 2; k++) {
            float u = k == 0 ? 82.2f : 86.5f;
            EscalatorDef d;
            d.base = ZB::P(u, 126.8f + Escalators::NEWEL, TF);
            d.heading = z.heading(0.0f, 1.0f);
            d.rise = rise;
            d.up = k == 0;
            esc.add(d, op, gl, col);
        }
        // rand rond de opgang op straatniveau (dekt het weggesneden terrein af)
        {
            const Rect& h = SOUTH_HOLE;
            const Vec3 PAVE(0.58f, 0.57f, 0.55f);
            z.flat(h.u0 - 2.2f, h.u0, h.v0 - 2.0f, h.v1 + 2.2f, HS + 0.02f, PAVE, EX_PLAIN, true, true);
            z.flat(h.u1, h.u1 + 2.2f, h.v0 - 2.0f, h.v1 + 2.2f, HS + 0.02f, PAVE, EX_PLAIN, true, true);
            z.flat(h.u0, h.u1, h.v1, h.v1 + 2.2f, HS + 0.02f, PAVE, EX_PLAIN, true, true);
            z.flat(h.u0, h.u1, h.v0 - 2.0f, h.v0, HS + 0.02f, PAVE, EX_PLAIN, true, true);
        }
        stairs(z, 77.9f, 141.4f, 77.9f, 128.7f, 5.4f, HS, TF, -1);
        stairs(z, 90.6f, 141.4f, 90.6f, 128.7f, 5.4f, HS, TF, 1);
        z.flat(TUN_U0, TUN_U1, 141.4f, SOUTH_HOLE.v1, HS + 0.004f, Vec3(0.62f, 0.62f, 0.60f), EX_PLAIN, true, true);
        // zijwanden van de opgang (tot straatniveau) en scheiding roltrappen/lift
        float t = 2000;
        mosaicWall(z, TUN_U0, SOUTH_HOLE.v1, TUN_U0, SOUTH_HOLE.v0, TF, HS, t);
        mosaicWall(z, TUN_U1, SOUTH_HOLE.v0, TUN_U1, SOUTH_HOLE.v1, TF, HS, t + 30);
        z.box(TUN_U0 - 0.3f, TUN_U0, SOUTH_HOLE.v0, SOUTH_HOLE.v1, HS, HS + 1.1f, CONCRETE, EX_PLAIN, true);
        z.box(TUN_U1, TUN_U1 + 0.3f, SOUTH_HOLE.v0, SOUTH_HOLE.v1, HS, HS + 1.1f, CONCRETE, EX_PLAIN, true);
        z.glassBox(83.2f, 85.6f, 135.0f, 137.6f, TF, HS + 2.5f, true);
        z.box(83.12f, 85.68f, 134.92f, 137.68f, HS + 2.5f, HS + 2.7f, STEEL);
        Elevator el;
        el.bottom = ZB::P(84.4f, 133.9f, TF);
        el.top = ZB::P(84.4f, 138.6f, HS);
        el.label = "uitgang Zuid";
        el.topLabel = "de tunnel";
        elevators.push_back(el);
        z.hangSign("Uitgang Zuid  |  Hanzelaan", 84.2f, 126.0f, TC - 0.45f, z.U, 0.42f, TC);
        // entreekap Zuidzijde (OSM-vorm)
        canopy(z, CANOPIES[5], HS, 4.6f, -1e9f, 1e9f);
        z.sign("Zwolle", NSBLUE, WHITE, 84.2f, SOUTH_HOLE.v1 + 0.2f, HS + 5.6f, z.U, 0.9f);
    }

    // ================= VIDEOWAND "TIJDTUNNEL", STATIONSPIANO, VERTREKSTATEN =================
    {
        // videowand in de oostwand tussen perron 6/7 en 9/10 (5,6 m breed)
        float u = TUN_U1 - 0.06f, v0 = 60.0f, v1 = 65.6f, y0 = TF + 0.55f, y1 = TF + 3.05f;
        z.box(u - 0.04f, u + 0.06f, v0 - 0.12f, v1 + 0.12f, y0 - 0.12f, y1 + 0.12f, DARK, EX_PLAIN);
        z.wall(u - 0.05f, v0, u - 0.05f, v1, y0, y1, Vec3(0.20f, 0.17f, 0.14f), EX_LIGHT, z.U * -1.0f);
        // beeld: een stoet mensen in de tunnel (warme silhouetten tegen een lichte achtergrond)
        z.wall(u - 0.055f, v0, u - 0.055f, v1, y0 + 0.9f, y1, Vec3(0.55f, 0.48f, 0.40f), EX_LIGHT, z.U * -1.0f);
        for (int k = 0; k < 14; k++) {
            float vc = v0 + 0.25f + k * 0.38f + 0.1f * std::sin(k * 2.3f);
            float hh = 1.2f + 0.35f * std::sin(k * 1.3f) * std::sin(k * 1.3f);
            Vec3 c = k % 3 == 0 ? Vec3(0.45f, 0.20f, 0.12f) : k % 3 == 1 ? Vec3(0.18f, 0.20f, 0.28f) : Vec3(0.35f, 0.30f, 0.20f);
            z.wall(u - 0.06f, vc - 0.13f, u - 0.06f, vc + 0.13f, y0 + 0.25f, y0 + 0.25f + hh, c, EX_LIGHT, z.U * -1.0f);
            z.wall(u - 0.06f, vc - 0.08f, u - 0.06f, vc + 0.08f, y0 + 0.28f + hh, y0 + 0.5f + hh, Vec3(0.75f, 0.58f, 0.45f), EX_LIGHT, z.U * -1.0f);
        }
        z.sign("Tijdtunnel", DARK, WHITE, u - 0.07f, (v0 + v1) / 2, y1 + 0.3f, z.V * -1.0f, 0.22f, false);
        // stationspiano bij de lift naar perron 3/5
        {
            float pu = 87.8f, pv = 34.4f;
            z.box(pu - 0.75f, pu + 0.75f, pv - 0.3f, pv + 0.3f, TF, TF + 1.25f, Vec3(0.04f, 0.04f, 0.045f), EX_PLAIN, true);
            z.box(pu - 0.72f, pu + 0.72f, pv - 0.55f, pv - 0.3f, TF + 0.70f, TF + 0.76f, Vec3(0.92f, 0.92f, 0.90f));
            z.box(pu - 0.4f, pu + 0.4f, pv - 1.1f, pv - 0.8f, TF, TF + 0.5f, Vec3(0.05f, 0.05f, 0.05f), EX_PLAIN, true);
        }
        // vertrekstaten aan het plafond (voor- en achterkant)
        for (float v : {40.5f, 62.0f, 88.0f}) {
            BoardSpot bs;
            bs.station = "Zwolle";
            bs.overview = true;
            bs.normal = z.V;
            bs.across = z.U;
            bs.c = ZB::P(84.2f, v, TC - 0.85f);
            bs.w = 2.3f;
            bs.h = 1.15f;
            boards.push_back(bs);
            float hd = std::atan2(z.V.x, z.V.z);
            op.orientedBox(bs.c, {bs.w + 0.12f, bs.h + 0.12f, 0.14f}, hd, Vec3(0.12f, 0.12f, 0.13f), EX_PLAIN);
            for (int s = -1; s <= 1; s += 2) {
                Vec3 a = bs.c + z.U * (s * (bs.w / 2 - 0.2f)) + Vec3(0, bs.h / 2, 0);
                op.tube(a, Vec3(a.x, TC + 0.3f, a.z), 0.02f, 6, STEEL, EX_PLAIN);
            }
        }
        z.hangSign("Uitgang Centrum", 84.2f, 12.0f, TC - 0.45f, z.U, 0.42f, TC);
    }

    // ================= STATIONSKAP (1995) boven perron 1 en 3/5 =================
    {
        // gebogen stalen kap: spanten om de 9 m op twee kolomrijen (perron 1 en eiland 3/5), lichtstraat in de nok
        const float vA = 11.6f, vB = 31.0f;            // kolomrijen
        const float v0 = KAP.v0, v1 = KAP.v1;
        const float eave = P + 6.2f, crown = P + 10.4f;
        auto ky = [&](float v) { float x = (v - (v0 + v1) / 2) / ((v1 - v0) / 2); return eave + (crown - eave) * (1 - x * x); };
        const int NV = 16;
        for (float u = KAP.u0; u < KAP.u1 - 0.1f; u += 4.5f) {
            float ub = std::min(KAP.u1, u + 4.5f);
            for (int k = 0; k < NV; k++) {
                float va = v0 + (v1 - v0) * k / NV, vb = v0 + (v1 - v0) * (k + 1) / NV;
                bool glassStrip = k >= 6 && k < 10;
                Vec3 a = ZB::P(u, va, ky(va)), b = ZB::P(ub, va, ky(va)), c = ZB::P(ub, vb, ky(vb)), d = ZB::P(u, vb, ky(vb));
                if (glassStrip) {
                    gl.quad(a, b, c, d, Vec3(0, 1, 0), GLASS_T, EX_GLASS);
                } else {
                    op.quad(a + Vec3(0, 0.12f, 0), b + Vec3(0, 0.12f, 0), c + Vec3(0, 0.12f, 0), d + Vec3(0, 0.12f, 0), Vec3(0, 1, 0), Vec3(0.55f, 0.57f, 0.60f), EX_PLAIN);
                    op.quad(a, d, c, b, Vec3(0, -1, 0), ROOF_UNDER, EX_INDOOR);
                }
                // gordingen langs u
                op.orientedBox((a + b) * 0.5f - Vec3(0, 0.08f, 0), {0.08f, 0.16f, ub - u}, z.hU, KAP_WHITE, EX_PLAIN);
            }
            // dakrand (goot) aan beide kanten
            for (float ve : {v0, v1}) {
                Vec3 m = ZB::P((u + ub) / 2, ve, eave);
                op.orientedBox(m + Vec3(0, 0.1f, 0), {0.35f, 0.5f, ub - u}, z.hU, Vec3(0.35f, 0.36f, 0.38f), EX_PLAIN);
            }
            // spant (gebogen ligger) en kolommen, om de 9 m
            if (((int)std::round((u - KAP.u0) / 4.5f)) % 2 == 0) {
                Vec3 prev = ZB::P(u, v0, ky(v0) - 0.15f);
                for (int k = 1; k <= NV; k++) {
                    float v = v0 + (v1 - v0) * k / NV;
                    Vec3 p = ZB::P(u, v, ky(v) - 0.15f);
                    Vec3 dv = p - prev;
                    float l = length(dv);
                    Vec3 mid = (p + prev) * 0.5f;
                    MeshBuilder& m = op;
                    // ligger als schuin blok (via twee punten): gebruik een buis met rechthoekig gevoel
                    m.tube(prev - Vec3(0, 0.12f, 0), p - Vec3(0, 0.12f, 0), 0.16f, 6, KAP_WHITE, EX_PLAIN);
                    (void)l; (void)mid;
                    prev = p;
                }
                for (float vc : {vA, vB}) {
                    if (u < -60.0f || inOpening(u, vc) || inOpening(u + 1.5f, vc) || inOpening(u - 1.5f, vc)) continue;
                    Vec3 c = ZB::P(u, vc, vc < 20 ? P1 : P);
                    float yt = ky(vc) - 0.3f;
                    op.tube(c, Vec3(c.x, yt, c.z), 0.22f, 14, KAP_WHITE, EX_PLAIN);
                    // gaffelkop: twee schoren naar het spant
                    for (int s = -1; s <= 1; s += 2) {
                        Vec3 tp = ZB::P(u, vc + s * 2.6f, ky(vc + s * 2.6f) - 0.3f);
                        op.tube(Vec3(c.x, yt - 1.6f, c.z), tp, 0.09f, 8, KAP_WHITE, EX_PLAIN);
                    }
                    col.addBox({c.x, c.z, z.U.x, z.U.z, 0.23f, 0.23f, c.y, yt});
                    // lampen aan de kolom
                    op.orientedBox(Vec3(c.x, c.y + 4.2f, c.z), {0.7f, 0.12f, 0.25f}, z.hU, Vec3(0.3f, 0.3f, 0.32f), EX_PLAIN);
                    op.orientedBox(Vec3(c.x, c.y + 4.13f, c.z), {0.6f, 0.02f, 0.2f}, z.hU, LEDW, EX_LIGHT);
                }
            }
        }
    }

    // ================= PERRONKAPPEN (OSM-vormen) =================
    canopy(z, CANOPIES[0], P, 4.3f, KAP.u1, 1e9f);         // 3/5 oost (west ligt onder de stationskap)
    canopy(z, CANOPIES[1], P, 4.3f, -1e9f, 1e9f);          // 6/7
    canopy(z, CANOPIES[2], P, 4.3f, -1e9f, 1e9f);          // 8/9/10
    canopy(z, CANOPIES[3], P1, 5.2f, SQUARE.u1, 1e9f);     // 1b/14 (oost van de vierkante kap)
    canopy(z, CANOPIES[4], HB, 4.2f, -1e9f, 1e9f);         // busperron
    {
        // busperron: verhoogd eiland (15 cm) rond de opgangen, met een rand van banden
        const Island& is = ISLANDS[3];
        const float u0 = 49.0f, u1 = 119.5f, v0 = 104.6f, v1 = 117.8f;
        const Vec3 PAVE(0.62f, 0.61f, 0.58f);
        auto deck = [&](float a, float b, float c, float d) { if (b > a + 0.05f && d > c + 0.05f) z.flat(a, b, c, d, HB, PAVE, EX_PLAIN, true, true); };
        deck(u0, u1, v0, is.v0);
        deck(u0, u1, is.v1, v1);
        deck(u0, WEST_U0, is.v0, is.v1);
        deck(WEST_U1, LIFT_U0, is.v0, is.v1);
        deck(LIFT_U1, EAST_U0, is.v0, is.v1);
        deck(EAST_U1, u1, is.v0, is.v1);
        // banden (zijkant)
        z.box(u0, u1, v0 - 0.15f, v0, HB - 0.4f, HB, Vec3(0.75f, 0.75f, 0.72f));
        z.box(u0, u1, v1, v1 + 0.15f, HB - 0.4f, HB, Vec3(0.75f, 0.75f, 0.72f));
        z.box(u0 - 0.15f, u0, v0, v1, HB - 0.4f, HB, Vec3(0.75f, 0.75f, 0.72f));
        z.box(u1, u1 + 0.15f, v0, v1, HB - 0.4f, HB, Vec3(0.75f, 0.75f, 0.72f));
        // bushalteborden
        for (float u : {54.0f, 100.0f, 116.0f}) {
            for (float v : {v0 + 0.5f, v1 - 0.5f}) {
                Vec3 b = ZB::P(u, v, HB);
                z.op.tube(b, b + Vec3(0, 2.6f, 0), 0.04f, 6, STEEL, EX_PLAIN);
                z.sign(v < 110 ? "Bus  A" : "Bus  B", Vec3(0.0f, 0.45f, 0.25f), WHITE, u, v, HB + 2.4f, z.V, 0.35f);
            }
        }
    }
}
