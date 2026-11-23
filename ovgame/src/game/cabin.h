#pragma once
// Machinistencabine (FLIRT en VIRM): vloer, wanden met deur naar het reizigersgedeelte, plafond, lessenaar met
// beeldschermen (snelheid / ATB, informatie), knoppenrijen, rijremschakelaar, bestuurdersstoel, dodemanspedaal,
// zonnescherm, brandblusser. Canonieke coordinaten van een eindrijtuig (cabine aan +z).
#include <algorithm>
#include "../engine/meshbuilder.h"
#include "bus.h"
#include "rollingstock.h"

namespace cabin {

struct Params {
    CabInfo c;
    float halfW;      // binnenbreedte / 2
    float ceilY;      // plafond
    float frontZ;     // voorkant van de lessenaar (onder de voorruit)
    float deskTopY;   // hoogte van de lessenaar aan de kant van de machinist
    float glassY;     // onderkant voorruit
    float sideWin0, sideWin1;  // zijraam (hoogte onder/boven): geen wandpanelen ervoor
    Vec3 wall, desk, floor, seat;
    // vorm van de neus: binnenkant van de voorruit/kap op hoogte y = noseBase + noseAt(y) (nullptr: recht front op frontZ).
    // Wanden en zonneklep eindigen op elke hoogte daarbinnen, anders steken ze door een terugwijkende voorruit naar buiten.
    float (*noseAt)(float y) = nullptr;
    float (*noseHalfAt)(float y) = nullptr;  // halve breedte van het vlakke front op hoogte y (de neus loopt smal toe)
    float noseBase = 0;
    // zijwanden lopen tot het begin van de neus; daar voorbij vormt de neus zelf de wand
    float wallEnd() const { return noseAt ? std::min(frontZ - 0.3f, noseBase - 0.03f) : frontZ - 0.3f; }
    float frontLimit(float y) const { return noseAt ? std::min(frontZ, noseBase + noseAt(y) - 0.12f) : frontZ; }
};

// vlak paneel op het schuine bovenblad van de lessenaar: u = links/rechts (x), v = 0 (machinist) .. 1 (voorruit)
inline void deskQuad(MeshBuilder& m, const Params& p, float x, float v, float w, float h, float lift, Vec3 col, float mat) {
    float z0 = p.c.deskZ, z1 = p.frontZ, y0 = p.deskTopY, y1 = p.glassY - 0.03f;
    Vec3 A(0, y0, z0), B(0, y1, z1);
    Vec3 V = normalize(B - A), U(1, 0, 0), N = normalize(cross(V, U));
    if (N.y < 0) N = N * -1.0f;
    float len = length(B - A);
    Vec3 c = A + V * (v * len) + Vec3(x, 0, 0) + N * lift;
    Vec3 a = c - U * (w / 2) - V * (h / 2), b = c + U * (w / 2) - V * (h / 2), d = c + U * (w / 2) + V * (h / 2), e = c - U * (w / 2) + V * (h / 2);
    m.quad(a, b, d, e, N, col, mat);
}

// rechtopstaand scherm op de lessenaar, gekanteld naar de machinist
inline void screen(MeshBuilder& m, Vec3 c, float w, float h, Vec3 bg, float tilt) {
    Vec3 U(1, 0, 0), V(0, std::cos(tilt), std::sin(tilt)), N(0, -std::sin(tilt), std::cos(tilt));
    N = N * -1.0f;  // naar de machinist (-z)
    auto Q = [&](Vec3 cc, float ww, float hh, Vec3 col, float mat, float off) {
        Vec3 o = cc + N * off;
        m.quad(o - U * (ww / 2) - V * (hh / 2), o + U * (ww / 2) - V * (hh / 2), o + U * (ww / 2) + V * (hh / 2), o - U * (ww / 2) + V * (hh / 2), N, col, mat);
    };
    m.box(c + N * -0.03f, {w + 0.04f, h + 0.04f, 0.05f}, Vec3(0.10f, 0.10f, 0.11f), MAT_PAINT);
    Q(c, w, h, bg, MAT_LIGHT, 0.002f);
    // eenvoudige weergave: wijzerboog, balkjes en tekstregels
    for (int k = 0; k < 12; k++) {
        float a = PI * (0.85f + 1.3f * k / 11.0f);
        Vec3 pc = c - U * (w * 0.2f) + U * (std::cos(a) * h * 0.30f) + V * (std::sin(a) * h * 0.30f);
        Q(pc, 0.008f, 0.008f, Vec3(0.85f, 0.85f, 0.85f), MAT_LIGHT, 0.004f);
    }
    Q(c - U * (w * 0.2f), 0.04f, 0.02f, Vec3(0.95f, 0.95f, 0.95f), MAT_LIGHT, 0.004f);
    for (int k = 0; k < 4; k++) Q(c + U * (w * 0.25f) + V * (h * (0.25f - k * 0.15f)), w * 0.35f, 0.012f, Vec3(0.55f, 0.75f, 0.95f), MAT_LIGHT, 0.004f);
    Q(c + V * (-h * 0.40f), w * 0.8f, 0.016f, Vec3(0.20f, 0.65f, 0.30f), MAT_LIGHT, 0.004f);
}

inline void build(MeshBuilder& op, MeshBuilder& gl, const Params& p) {
    const CabInfo& c = p.c;
    const float W = p.halfW, z0 = c.wallZ, zF = p.frontZ;
    const Vec3 BLACK(0.06f, 0.06f, 0.065f), DARK(0.16f, 0.16f, 0.17f), STEEL(0.55f, 0.56f, 0.58f);
    // vloer
    op.box({0, c.floorY - 0.025f, (z0 + zF) / 2}, {2 * W, 0.05f, zF - z0}, p.floor, MAT_RUBBER);
    if (c.floorY > c.passFloor + 0.05f)  // opstapje bij de deur
        op.box({c.doorX, (c.floorY + c.passFloor) / 2, z0 + 0.02f}, {c.doorW, c.floorY - c.passFloor, 0.06f}, p.floor * 1.2f, MAT_RUBBER);
    // achterwand met deuropening
    float yb = std::min(c.floorY, c.passFloor), doorTop = c.passFloor + 2.0f;
    float xl = c.doorX - c.doorW / 2, xr = c.doorX + c.doorW / 2;
    op.box({(-W + xl) / 2, (yb + p.ceilY) / 2, z0}, {xl + W, p.ceilY - yb, 0.06f}, p.wall, MAT_PAINT);
    op.box({(xr + W) / 2, (yb + p.ceilY) / 2, z0}, {W - xr, p.ceilY - yb, 0.06f}, p.wall, MAT_PAINT);
    op.box({c.doorX, (doorTop + p.ceilY) / 2, z0}, {c.doorW, p.ceilY - doorTop, 0.06f}, p.wall, MAT_PAINT);
    for (int s = -1; s <= 1; s += 2) op.box({c.doorX + s * c.doorW / 2, (yb + doorTop) / 2, z0}, {0.03f, doorTop - yb, 0.09f}, DARK, MAT_PAINT);  // kozijn
    // zijwanden onder en boven het zijraam, plafond met lichtstrook
    for (int s = -1; s <= 1; s += 2) {
        float x = s * (W - 0.02f);
        const float zw = p.wallEnd();
        op.box({x, (c.floorY + p.sideWin0) / 2, (z0 + zw) / 2}, {0.04f, p.sideWin0 - c.floorY, zw - z0}, p.wall, MAT_PAINT);
        // boven het zijraam: in stroken, elk tot waar de neus op die hoogte nog ruimte laat
        for (int k = 0; k < 4; k++) {
            float ya = p.sideWin1 + (p.ceilY - p.sideWin1) * k / 4, yb2 = p.sideWin1 + (p.ceilY - p.sideWin1) * (k + 1) / 4;
            float zb = std::min(zw, p.frontLimit(yb2));
            if (zb > z0 + 0.05f) op.box({x, (ya + yb2) / 2, (z0 + zb) / 2}, {0.04f, yb2 - ya, zb - z0}, p.wall, MAT_PAINT);
        }
        op.box({x - s * 0.05f, p.sideWin0 + 0.02f, (z0 + zw) / 2}, {0.10f, 0.03f, zw - z0}, DARK, MAT_PAINT);  // vensterbank
    }
    {
        float zc = std::min(c.deskZ + 0.2f, p.frontLimit(p.ceilY + 0.04f));  // plafond: niet voorbij de kap
        if (p.noseAt) zc = std::min(zc, p.noseBase - 0.03f);                 // in de neus vormt de kap zelf het plafond
        op.box({0, p.ceilY + 0.02f, (z0 + zc) / 2}, {2 * W, 0.04f, zc - z0}, p.wall * 1.05f, MAT_PAINT);
    }
    op.box({0, p.ceilY - 0.005f, (z0 + c.deskZ) / 2}, {0.5f, 0.02f, 0.9f}, Vec3(1.3f, 1.27f, 1.18f), MAT_LIGHT);
    // ---- lessenaar ----
    float dw = std::min(W, 1.12f);
    if (p.noseHalfAt) dw = std::min(dw, p.noseHalfAt(p.deskTopY) - 0.18f);  // lessenaar past in de smaller wordende neus
    op.box({0, (c.floorY + p.deskTopY) / 2 - 0.02f, (c.deskZ + zF) / 2}, {2 * dw, p.deskTopY - c.floorY, zF - c.deskZ}, p.desk, MAT_PAINT);
    deskQuad(op, p, 0, 0.5f, 2 * dw, 1.0f, 0.004f, p.desk * 0.8f, MAT_PAINT);
    // voorrand met handsteun
    op.box({0, p.deskTopY + 0.01f, c.deskZ + 0.03f}, {2 * dw - 0.1f, 0.04f, 0.08f}, BLACK, MAT_RUBBER);
    // beeldschermen: links snelheid/ATB (DMI), rechts informatie / camera
    screen(op, {c.seatX + 0.30f, p.deskTopY + 0.12f, c.deskZ + 0.42f}, 0.26f, 0.19f, Vec3(0.02f, 0.03f, 0.06f), 0.35f);
    screen(op, {c.seatX - 0.30f, p.deskTopY + 0.12f, c.deskZ + 0.42f}, 0.26f, 0.19f, Vec3(0.03f, 0.05f, 0.09f), 0.35f);
    screen(op, {c.seatX, p.deskTopY + 0.10f, c.deskZ + 0.50f}, 0.16f, 0.12f, Vec3(0.04f, 0.04f, 0.04f), 0.40f);  // ATB-paneel
    // knoppenrijen
    const Vec3 BTN[] = {{0.1f, 0.75f, 0.2f}, {0.9f, 0.1f, 0.1f}, {1.0f, 0.75f, 0.1f}, {0.92f, 0.92f, 0.92f}, {0.15f, 0.35f, 0.85f}};
    for (int row = 0; row < 2; row++)
        for (int k = 0; k < 7; k++) {
            float x = -dw + 0.18f + k * 0.07f;
            deskQuad(op, p, x, 0.15f + row * 0.12f, 0.035f, 0.035f, 0.008f, BTN[(k + row * 2) % 5], (k + row) % 3 == 0 ? MAT_LIGHT : MAT_PAINT);
            deskQuad(op, p, -x, 0.15f + row * 0.12f, 0.035f, 0.035f, 0.008f, BTN[(k + row * 3 + 1) % 5], (k + row) % 4 == 0 ? MAT_LIGHT : MAT_PAINT);
        }
    // deurknoppen links en rechts (groen = vrijgeven, rood = sluiten)
    for (int s = -1; s <= 1; s += 2) {
        op.box({s * (dw - 0.06f), p.deskTopY + 0.04f, c.deskZ + 0.15f}, {0.06f, 0.06f, 0.06f}, Vec3(0.1f, 0.8f, 0.25f), MAT_LIGHT);
        op.box({s * (dw - 0.06f), p.deskTopY + 0.04f, c.deskZ + 0.25f}, {0.06f, 0.06f, 0.06f}, Vec3(0.85f, 0.1f, 0.1f), MAT_PAINT);
    }
    // rijremschakelaar: voet (de hendel wordt los getekend)
    op.box({c.leverX, c.leverY - 0.02f, c.leverZ}, {0.10f, 0.06f, 0.22f}, BLACK, MAT_RUBBER);
    op.box({c.leverX, c.leverY + 0.012f, c.leverZ}, {0.012f, 0.004f, 0.18f}, Vec3(0.6f, 0.6f, 0.6f), MAT_PAINT);  // sleuf
    // hoorntoets en noodrem (rode paddenstoel)
    op.tube({c.seatX - 0.45f, p.deskTopY, c.deskZ + 0.12f}, {c.seatX - 0.45f, p.deskTopY + 0.05f, c.deskZ + 0.12f}, 0.035f, 10, Vec3(0.85f, 0.08f, 0.06f), MAT_PAINT);
    op.box({c.seatX - 0.18f, p.deskTopY + 0.02f, c.deskZ + 0.12f}, {0.07f, 0.03f, 0.05f}, Vec3(0.9f, 0.9f, 0.9f), MAT_PAINT);
    // zonnescherm (rolgordijn) en spiegelbeeld-ruitje
    op.box({0, p.ceilY - 0.06f, std::min(zF, p.frontLimit(p.ceilY - 0.02f)) - 0.05f}, {2 * dw - 0.2f, 0.08f, 0.06f}, DARK, MAT_PAINT);
    (void)gl;
    // ---- bestuurdersstoel ----
    Vec3 sc(c.seatX, c.seatTop, c.seatZ);
    op.tube({sc.x, c.floorY, sc.z}, {sc.x, c.seatTop - 0.1f, sc.z}, 0.05f, 10, STEEL, MAT_METAL);
    op.box({sc.x, c.floorY + 0.02f, sc.z}, {0.45f, 0.04f, 0.45f}, DARK, MAT_METAL);
    op.box({sc.x, c.seatTop - 0.05f, sc.z}, {0.52f, 0.10f, 0.50f}, p.seat, MAT_RUBBER);
    op.box({sc.x, c.seatTop + 0.33f, sc.z - 0.27f}, {0.50f, 0.66f, 0.10f}, p.seat, MAT_RUBBER);
    op.box({sc.x, c.seatTop + 0.76f, sc.z - 0.29f}, {0.30f, 0.20f, 0.08f}, p.seat, MAT_RUBBER);
    for (int s = -1; s <= 1; s += 2) op.box({sc.x + s * 0.30f, c.seatTop + 0.17f, sc.z - 0.02f}, {0.06f, 0.04f, 0.36f}, DARK, MAT_RUBBER);
    // dodemanspedaal
    op.box({c.seatX + 0.12f, c.floorY + 0.04f, c.deskZ - 0.05f}, {0.16f, 0.04f, 0.26f}, BLACK, MAT_RUBBER);
    // brandblusser en kapstok aan de achterwand, kastje
    op.tube({-W + 0.15f, c.floorY + 0.05f, z0 + 0.12f}, {-W + 0.15f, c.floorY + 0.55f, z0 + 0.12f}, 0.07f, 10, Vec3(0.80f, 0.06f, 0.05f), MAT_PAINT);
    op.box({-W + 0.15f, c.floorY + 0.60f, z0 + 0.12f}, {0.06f, 0.08f, 0.06f}, BLACK, MAT_PAINT);
    op.box({W - 0.25f, c.floorY + 0.5f, z0 + 0.25f}, {0.40f, 1.0f, 0.40f}, p.wall * 0.85f, MAT_PAINT);
    op.box({-0.6f, c.floorY + 1.75f, z0 + 0.04f}, {0.04f, 0.04f, 0.08f}, STEEL, MAT_METAL);
}

// deurblad: scharnier in de oorsprong, blad langs +x, onderkant y = 0
inline Mesh doorLeaf(float w, float h, Vec3 col) {
    MeshBuilder m;
    m.box({w / 2, h / 2, 0}, {w - 0.02f, h, 0.04f}, col, MAT_PAINT);
    m.box({w / 2, h * 0.72f, 0}, {w * 0.5f, h * 0.22f, 0.045f}, Vec3(0.05f, 0.06f, 0.07f), MAT_GLASS);
    for (int s = -1; s <= 1; s += 2) m.box({w - 0.12f, 1.05f, s * 0.05f}, {0.12f, 0.03f, 0.04f}, Vec3(0.6f, 0.6f, 0.62f), MAT_METAL);
    return m.upload();
}

// hendel van de rijremschakelaar: draaipunt in de oorsprong, steekt omhoog
inline Mesh lever() {
    MeshBuilder m;
    m.tube({0, 0, 0}, {0, 0.13f, 0}, 0.012f, 8, Vec3(0.5f, 0.5f, 0.52f), MAT_METAL);
    m.box({0, 0.15f, 0}, {0.09f, 0.045f, 0.05f}, Vec3(0.05f, 0.05f, 0.06f), MAT_RUBBER);
    m.box({0.035f, 0.15f, 0}, {0.02f, 0.03f, 0.03f}, Vec3(0.9f, 0.75f, 0.1f), MAT_PAINT);
    return m.upload();
}

}  // namespace cabin
