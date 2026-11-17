#include "bus.h"
#include <algorithm>
#include <vector>
#include "../engine/meshbuilder.h"
#include "world.h"

namespace {

// Kleuren (Hermes/Bravo-stijl: rood, wit, zwart), zonder logo's
const Vec3 WHITE(0.90f, 0.90f, 0.88f);
const Vec3 RED(0.78f, 0.07f, 0.06f);
const Vec3 GLASS(0.20f, 0.24f, 0.27f);
const Vec3 DARKGLASS(0.035f, 0.04f, 0.05f);
const Vec3 ROOF(0.84f, 0.84f, 0.83f);
const Vec3 DARK(0.08f, 0.08f, 0.09f);
const Vec3 TYRE(0.05f, 0.05f, 0.05f);
const Vec3 RIM(0.55f, 0.56f, 0.58f);
const Vec3 BUMPER(0.16f, 0.16f, 0.17f);
const Vec3 LED_ORANGE(1.0f, 0.62f, 0.10f);
const Vec3 HEADLIGHT(1.0f, 1.0f, 0.95f);
const Vec3 TAIL(0.9f, 0.05f, 0.03f);
// interieur
const Vec3 INT_WALL(0.78f, 0.79f, 0.80f);
const Vec3 INT_FLOOR(0.25f, 0.26f, 0.28f);
const Vec3 INT_CEIL(0.88f, 0.88f, 0.87f);
const Vec3 SEAT_FABRIC(0.16f, 0.20f, 0.34f);
const Vec3 SEAT_SHELL(0.30f, 0.31f, 0.33f);
const Vec3 POLE(0.85f, 0.10f, 0.08f);
const Vec3 STOPBTN(0.9f, 0.1f, 0.1f);

const float HW = BUS_HW;
const float FL = BUS_FLOOR;
const float CEIL = 2.42f;

using MB = MeshBuilder;

// Dwarsdoorsnede (rechts van het midden, x >= 0), van onder naar het dakmidden
struct Seg { float x0, y0, x1, y1; int band; };
enum Band { B_LOWER, B_WINDOW, B_TOPRED, B_ROOF };
const Seg PROFILE[] = {
    {HW, 0.32f, HW, 0.95f, B_LOWER},
    {HW, 0.95f, HW - 0.012f, 2.55f, B_WINDOW},
    {HW - 0.012f, 2.55f, HW - 0.03f, 2.80f, B_TOPRED},
    {HW - 0.03f, 2.80f, HW - 0.11f, 2.94f, B_ROOF},
    {HW - 0.11f, 2.94f, HW - 0.33f, 3.01f, B_ROOF},
    {HW - 0.33f, 3.01f, 0.0f, 3.03f, B_ROOF},
};
const int NSEG = sizeof(PROFILE) / sizeof(PROFILE[0]);

Vec3 segNormal(const Seg& s) { return normalize(Vec3(s.y1 - s.y0, -(s.x1 - s.x0), 0)); }

float halfWidthAt(float y) {
    for (const Seg& s : PROFILE)
        if (y >= s.y0 && y <= s.y1 && s.y1 > s.y0) return s.x0 + (s.x1 - s.x0) * (y - s.y0) / (s.y1 - s.y0);
    return y < PROFILE[0].y0 ? HW : 0.0f;
}

struct ZRange { float z0, z1; bool redRoof; bool arch; bool door; };

// Zijkanten + dak uitrekken over een z-bereik; ramen in de glasmesh. Deuren: rechterzijde (-x) open.
void extrude(MB& b, MB& g, const ZRange& r) {
    for (int k = 0; k < NSEG; k++) {
        const Seg& s = PROFILE[k];
        Vec3 n0 = segNormal(s), n1 = n0;
        if (k > 1) n0 = normalize(n0 + segNormal(PROFILE[k - 1]));
        if (k < NSEG - 1 && k > 0) n1 = normalize(n1 + segNormal(PROFILE[k + 1]));
        for (int side = -1; side <= 1; side += 2) {
            if (s.band == B_LOWER && r.arch) continue;
            if (r.door && side < 0 && (s.band == B_LOWER || s.band == B_WINDOW)) continue;
            Vec3 a(side * s.x0, s.y0, r.z0), bb(side * s.x0, s.y0, r.z1), c(side * s.x1, s.y1, r.z1), d(side * s.x1, s.y1, r.z0);
            Vec3 na(side * n0.x, n0.y, 0), nc(side * n1.x, n1.y, 0);
            if (s.band == B_WINDOW) {
                g.quad(a, bb, c, d, na, na, nc, nc, GLASS, EX_GLASS);
                // binnenwand onder de ramen en raamstijlen
                continue;
            }
            Vec3 col = s.band == B_LOWER ? WHITE : s.band == B_TOPRED ? RED : (r.redRoof ? RED : ROOF);
            b.quad(a, bb, c, d, na, na, nc, nc, col, MAT_PAINT);
        }
    }
    // raamstijlen (zwart) op de grenzen
    for (int side = -1; side <= 1; side += 2) {
        if (r.door && side < 0) continue;
        int n = std::max(1, (int)std::round((r.z1 - r.z0) / 1.6f));
        for (int w = 0; w <= n; w++) {
            float z = r.z0 + (r.z1 - r.z0) * w / n;
            b.box({side * (HW + 0.002f), 1.75f, clampf(z, r.z0 + 0.04f, r.z1 - 0.04f)}, {0.02f, 1.6f, 0.08f}, DARK, MAT_PAINT);
        }
    }
}

struct FaceBand { float y0, y1; Vec3 col; float mat; bool glass; };

// Kopse kant (voor/achter) als horizontale banden die de omtrek van het profiel volgen
void face(MB& b, MB& g, float z, float dir, const std::vector<FaceBand>& bands) {
    std::vector<float> levels;
    for (const Seg& s : PROFILE) levels.push_back(s.y0);
    levels.push_back(PROFILE[NSEG - 1].y1);
    for (const FaceBand& fb : bands) {
        std::vector<float> ys{fb.y0};
        for (float y : levels)
            if (y > fb.y0 && y < fb.y1) ys.push_back(y);
        ys.push_back(fb.y1);
        for (size_t k = 0; k + 1 < ys.size(); k++) {
            float ya = ys[k], yb = ys[k + 1], wa = halfWidthAt(ya), wbb = halfWidthAt(yb);
            if (fb.glass) g.quad({-wa, ya, z}, {wa, ya, z}, {wbb, yb, z}, {-wbb, yb, z}, {0, 0, dir}, GLASS, EX_GLASS);
            else b.quad({-wa, ya, z}, {wa, ya, z}, {wbb, yb, z}, {-wbb, yb, z}, {0, 0, dir}, fb.col, fb.mat);
        }
    }
}

void wheelsAndArches(MB& b, const std::vector<float>& axles) {
    for (float az : axles)
        for (int side = -1; side <= 1; side += 2) {
            b.cylX({side * (HW - 0.17f), 0.48f, az}, 0.48f, 0.30f, 20, TYRE, MAT_RUBBER, RIM, 0.30f, MAT_METAL);
            b.box({side * (HW - 0.16f), 0.80f, az}, {0.30f, 0.30f, 1.30f}, DARK, MAT_RUBBER);
            b.box({side * (HW - 0.01f), 1.00f, az}, {0.04f, 0.10f, 1.30f}, WHITE, MAT_PAINT);
            // wielkast binnen
            b.box({side * (HW - 0.32f), (FL + 0.95f) / 2, az}, {0.5f, 0.95f - FL, 1.4f}, INT_WALL * 0.8f, MAT_PAINT);
        }
}

std::vector<ZRange> sideRanges(float z0, float z1, const std::vector<float>& axles, const std::vector<float>& doorZ, float redRoofFrom) {
    std::vector<float> cuts{z0, z1};
    for (float a : axles) { cuts.push_back(a - 0.65f); cuts.push_back(a + 0.65f); }
    for (float d : doorZ) { cuts.push_back(d - BUS_DOOR_W / 2); cuts.push_back(d + BUS_DOOR_W / 2); }
    if (redRoofFrom < z1) cuts.push_back(redRoofFrom);
    std::sort(cuts.begin(), cuts.end());
    std::vector<ZRange> out;
    for (size_t k = 0; k + 1 < cuts.size(); k++) {
        float a = std::max(z0, cuts[k]), c = std::min(z1, cuts[k + 1]);
        if (c - a < 0.001f) continue;
        float m = (a + c) / 2;
        bool arch = false, door = false;
        for (float ax : axles) if (std::fabs(m - ax) < 0.65f) arch = true;
        for (float d : doorZ) if (std::fabs(m - d) < BUS_DOOR_W / 2) door = true;
        out.push_back({a, c, m > redRoofFrom, arch, door});
    }
    return out;
}

void doorFrames(MB& b, const std::vector<float>& zs) {
    for (float dz : zs) {
        for (float e : {-1.0f, 1.0f})
            b.box({-HW + 0.02f, 1.48f, dz + e * (BUS_DOOR_W / 2 + 0.03f)}, {0.08f, 2.3f, 0.06f}, BUMPER, MAT_METAL);
        b.box({-HW + 0.02f, 2.58f, dz}, {0.08f, 0.08f, BUS_DOOR_W + 0.12f}, BUMPER, MAT_METAL);
        // instap: vloerrand met gele lijn
        b.box({-HW + 0.12f, FL - 0.02f, dz}, {0.25f, 0.04f, BUS_DOOR_W}, Vec3(0.5f, 0.5f, 0.52f), MAT_METAL);
        b.box({-HW + 0.03f, FL + 0.002f, dz}, {0.05f, 0.006f, BUS_DOOR_W - 0.05f}, Vec3(1.0f, 0.8f, 0.0f), MAT_PAINT);
    }
}

void frontEnd(MB& b, MB& g, float z) {
    face(b, g, z, 1, {{0.32f, 0.58f, BUMPER, MAT_PAINT, false},
                      {0.58f, 0.95f, WHITE, MAT_PAINT, false},
                      {0.95f, 2.62f, GLASS, MAT_GLASS, true},
                      {2.62f, 2.88f, DARKGLASS, MAT_GLASS, false},
                      {2.88f, 3.03f, RED, MAT_PAINT, false}});
    for (int s = -1; s <= 1; s += 2) b.box({s * (HW - 0.06f), 1.9f, z + 0.005f}, {0.12f, 1.9f, 0.02f}, RED, MAT_PAINT);
    for (int s = -1; s <= 1; s += 2) {
        b.box({s * 0.92f, 0.72f, z + 0.012f}, {0.42f, 0.10f, 0.01f}, HEADLIGHT, MAT_LIGHT);
        b.box({s * 0.92f, 0.82f, z + 0.012f}, {0.30f, 0.03f, 0.01f}, HEADLIGHT, MAT_LIGHT);
        b.box({s * 1.38f, 2.55f, z - 0.15f}, {0.06f, 0.06f, 0.35f}, DARK, MAT_PAINT);
        b.box({s * 1.42f, 2.20f, z + 0.02f}, {0.05f, 0.50f, 0.18f}, DARK, MAT_PAINT);
    }
    b.box({-0.3f, 1.0f, z + 0.015f}, {0.9f, 0.03f, 0.01f}, DARK, MAT_PAINT);
}

void rearEnd(MB& b, MB& g, float z) {
    face(b, g, z, -1, {{0.32f, 0.60f, BUMPER, MAT_PAINT, false},
                       {0.60f, 1.25f, WHITE, MAT_PAINT, false},
                       {1.25f, 2.45f, GLASS, MAT_GLASS, true},
                       {2.45f, 3.03f, RED, MAT_PAINT, false}});
    for (int s = -1; s <= 1; s += 2) b.box({s * 1.08f, 1.0f, z - 0.012f}, {0.14f, 0.55f, 0.01f}, TAIL, MAT_LIGHT);
    b.box({0, 2.6f, z - 0.012f}, {0.8f, 0.18f, 0.01f}, LED_ORANGE * 0.9f, MAT_LIGHT);
}

void plainEnd(MB& b, float z, float dir) {
    // open doorloop naar de harmonica: alleen een rand
    for (int s = -1; s <= 1; s += 2) b.quad({s * 0.75f, 0.32f, z}, {s * HW, 0.32f, z}, {s * HW, 3.0f, z}, {s * 0.75f, 3.0f, z}, {0, 0, dir}, DARK, MAT_PAINT);
    b.quad({-0.75f, 2.3f, z}, {0.75f, 2.3f, z}, {0.75f, 3.03f, z}, {-0.75f, 3.03f, z}, {0, 0, dir}, DARK, MAT_PAINT);
    b.quad({-0.75f, 0.32f, z}, {0.75f, 0.32f, z}, {0.75f, FL, z}, {-0.75f, FL, z}, {0, 0, dir}, DARK, MAT_PAINT);
}

void roofPods(MB& b, float z0, float z1) {
    b.box({0, 3.14f, (z0 + z1) / 2}, {1.9f, 0.22f, z1 - z0}, ROOF * 0.95f, MAT_PAINT);
    b.box({0, 3.255f, (z0 + z1) / 2}, {1.6f, 0.02f, z1 - z0 - 0.2f}, ROOF * 0.85f, MAT_PAINT);
}

// Stoel (kijkrichting +z), zitting op hoogte sy
void seat(MB& b, float x, float z, float sy) {
    b.box({x, sy, z}, {0.44f, 0.10f, 0.44f}, SEAT_FABRIC, MAT_RUBBER);
    b.box({x, sy + 0.40f, z - 0.24f}, {0.44f, 0.70f, 0.07f}, SEAT_FABRIC, MAT_RUBBER);
    b.box({x, sy + 0.38f, z - 0.29f}, {0.46f, 0.76f, 0.03f}, SEAT_SHELL, MAT_PAINT);
    if (sy - FL > 0.25f) b.box({x, (FL + sy) / 2, z}, {0.40f, sy - FL, 0.36f}, SEAT_SHELL, MAT_PAINT);  // sokkel
    b.box({x + (x > 0 ? -0.24f : 0.24f), sy + 0.75f, z - 0.25f}, {0.03f, 0.03f, 0.03f}, STOPBTN, MAT_LIGHT);
}

// Interieur van een bak tussen z0 en z1; front: met chauffeurscabine
void interior(MB& b, BusModel& m, int sec, float z0, float z1, const std::vector<float>& axles, const std::vector<float>& doorZ,
              bool front, float frontZ) {
    b.box({0, FL - 0.02f, (z0 + z1) / 2}, {2 * HW - 0.08f, 0.04f, z1 - z0}, INT_FLOOR, MAT_RUBBER);
    b.box({0, CEIL, (z0 + z1) / 2}, {2 * HW - 0.3f, 0.04f, z1 - z0}, INT_CEIL, MAT_PAINT);
    for (int s = -1; s <= 1; s += 2) {
        b.box({s * 0.55f, CEIL - 0.03f, (z0 + z1) / 2}, {0.12f, 0.02f, z1 - z0 - 0.6f}, Vec3(1.2f, 1.2f, 1.15f), MAT_LIGHT);
        b.box({s * (HW - 0.18f), CEIL - 0.12f, (z0 + z1) / 2}, {0.25f, 0.22f, z1 - z0}, INT_CEIL * 0.9f, MAT_PAINT);  // kanaal
        // binnenwand onder de ramen (niet bij deuren rechts)
        std::vector<float> cuts{z0, z1};
        if (s < 0)
            for (float d : doorZ) { cuts.push_back(d - BUS_DOOR_W / 2); cuts.push_back(d + BUS_DOOR_W / 2); }
        std::sort(cuts.begin(), cuts.end());
        for (size_t k = 0; k + 1 < cuts.size(); k++) {
            float a = cuts[k], c = cuts[k + 1], m = (a + c) / 2;
            bool door = false;
            if (s < 0) for (float d : doorZ) if (std::fabs(m - d) < BUS_DOOR_W / 2) door = true;
            if (door || c - a < 0.01f) continue;
            b.box({s * (HW - 0.04f), (FL + 0.95f) / 2, m}, {0.04f, 0.95f - FL, c - a}, INT_WALL, MAT_PAINT);
        }
    }
    // stoelen: rijen van twee aan beide kanten, niet bij deuren, niet in de rolstoelplek (tegenover deur 2)
    auto blocked = [&](float z, int side) {
        if (front && z > frontZ - 2.6f) return true;  // cabine/instap
        for (float d : doorZ) {
            if (side < 0 && std::fabs(z - d) < 0.95f) return true;
            if (std::fabs(z - d) < 0.45f) return true;
        }
        if (front && doorZ.size() > 1 && side > 0 && std::fabs(z - doorZ[1]) < 1.1f) return true;  // rolstoelplek
        return false;
    };
    for (int side = -1; side <= 1; side += 2)
        for (float z = (front ? frontZ - 2.8f : z1 - 0.7f); z > z0 + 0.6f; z -= 0.80f) {
            if (blocked(z, side)) continue;
            bool overArch = false;
            for (float a : axles) if (std::fabs(z - a) < 0.8f) overArch = true;
            float sy = overArch ? FL + 0.62f : FL + 0.45f;
            for (float x : {0.62f, 1.05f}) {
                seat(b, side * x, z, sy);
                m.seats.push_back({sec, side * x, z, 0.0f, sy - 0.45f});  // y: zodat zithoogte = sy (ook op de verhoging)
            }
        }
    // stangen bij de deuren en langs het gangpad
    for (float d : doorZ)
        for (float o : {-0.75f, 0.75f}) b.box({-0.48f, (FL + CEIL) / 2, d + o}, {0.04f, CEIL - FL, 0.04f}, POLE, MAT_METAL);
    for (float z = z0 + 1.2f; z < z1 - 1.0f; z += 2.4f)
        for (int s = -1; s <= 1; s += 2) {
            b.box({s * 0.40f, (FL + CEIL) / 2, z}, {0.035f, CEIL - FL, 0.035f}, POLE, MAT_METAL);
            // stopknop op de stang (richting gangpad)
            float bx = s * 0.40f - s * 0.035f;
            b.box({bx, FL + 1.30f, z}, {0.04f, 0.09f, 0.06f}, Vec3(0.85f, 0.85f, 0.86f), MAT_PAINT);
            b.box({bx - s * 0.022f, FL + 1.30f, z}, {0.01f, 0.05f, 0.04f}, STOPBTN, MAT_LIGHT);
            m.stopButtons.push_back({sec, bx - s * 0.03f, FL + 1.30f, z});
        }
    for (int s = -1; s <= 1; s += 2) b.box({s * 0.40f, CEIL - 0.25f, (z0 + z1) / 2}, {0.03f, 0.03f, z1 - z0 - 0.4f}, POLE, MAT_METAL);
    if (front) {
        // chauffeurscabine links voor: stoel, dashboard, stuur, chauffeur, scheidingswand
        float cz = frontZ - 1.3f;
        b.box({0.62f, FL + 0.55f, cz}, {0.55f, 0.12f, 0.55f}, DARK, MAT_RUBBER);
        b.box({0.62f, FL + 0.95f, cz - 0.3f}, {0.55f, 0.8f, 0.08f}, DARK, MAT_RUBBER);
        b.box({0.0f, FL + 0.75f, frontZ - 0.35f}, {2 * HW - 0.2f, 0.5f, 0.5f}, DARK * 1.5f, MAT_PAINT);
        // stuurkolom (het stuur zelf en de chauffeur zijn losse meshes: het stuur draait mee, de chauffeur
        // verdwijnt als de speler zelf rijdt)
        b.tube({0.62f, FL + 0.55f, cz + 0.85f}, {0.62f, FL + 0.95f, cz + 0.62f}, 0.04f, 8, Vec3(0.10f, 0.10f, 0.11f), MAT_RUBBER);
        b.box({0.62f, FL + 0.92f, frontZ - 0.61f}, {0.40f, 0.14f, 0.03f}, Vec3(0.02f, 0.03f, 0.04f), MAT_GLASS);  // instrumentenscherm
        b.box({0.62f, FL + 0.93f, frontZ - 0.63f}, {0.10f, 0.06f, 0.01f}, Vec3(0.2f, 0.7f, 1.0f), MAT_LIGHT);
        b.box({0.10f, FL + 0.8f, cz}, {0.04f, 1.0f, 1.2f}, INT_WALL * 0.9f, MAT_PAINT);  // cabinedeur/scheiding
        // informatiescherm achter de cabine
        m.screens.push_back({sec, 0.0f, CEIL - 0.21f, frontZ - 2.4f});
    }
    // informatieschermen (de tekst tekent BusFleet): hangend aan het plafond, naar achteren gericht
    if (front || sec > 0) {
        float zs = front ? frontZ - 2.4f : z1 - 1.0f;
        if (!front) m.screens.push_back({sec, 0.0f, CEIL - 0.21f, zs});
        else if (z0 < -3.0f) {
            float zm = (z0 + z1) / 2 - 0.6f;  // tweede scherm halverwege
            m.screens.push_back({sec, 0.0f, CEIL - 0.21f, zm});
            b.box({0.0f, CEIL - 0.21f, zm + 0.03f}, {0.45f, 0.29f, 0.04f}, Vec3(0.05f, 0.05f, 0.06f), MAT_PAINT);
            b.box({0.0f, CEIL - 0.035f, zm + 0.03f}, {0.04f, 0.07f, 0.03f}, POLE * 0.5f, MAT_METAL);
            b.box({0.0f, CEIL - 0.21f, zm + 0.005f}, {0.41f, 0.25f, 0.01f}, Vec3(0.97f, 0.97f, 0.97f), MAT_LIGHT);
        }
        b.box({0.0f, CEIL - 0.21f, zs + 0.03f}, {0.45f, 0.29f, 0.04f}, Vec3(0.05f, 0.05f, 0.06f), MAT_PAINT);   // behuizing
        b.box({0.0f, CEIL - 0.035f, zs + 0.03f}, {0.04f, 0.07f, 0.03f}, POLE * 0.5f, MAT_METAL);
        b.box({0.0f, CEIL - 0.21f, zs + 0.005f}, {0.41f, 0.25f, 0.01f}, Vec3(0.97f, 0.97f, 0.97f), MAT_LIGHT);  // beeld (wit)
    }
}

}  // namespace

BusModel buildCitea(bool articulated) {
    BusModel m;
    m.articulated = articulated;
    auto section = [&](int si, float L, std::vector<float> axles, std::vector<float> doorZ, bool isFront, bool isRear, bool joinedBehind, bool joinedFront) {
        MB b, g;
        float zf = L / 2, zr = -L / 2;
        for (const ZRange& r : sideRanges(zr, zf, axles, doorZ, isFront ? zf - 1.1f : 99.0f)) extrude(b, g, r);
        if (isFront) frontEnd(b, g, zf);
        if (joinedFront) plainEnd(b, zf, 1);
        if (isRear) rearEnd(b, g, zr);
        if (joinedBehind) plainEnd(b, zr, -1);
        b.box({0, 0.33f, 0}, {2 * HW - 0.02f, 0.03f, L}, DARK, MAT_RUBBER);  // bodem
        wheelsAndArches(b, axles);
        doorFrames(b, doorZ);
        roofPods(b, zr + 1.4f, zf - 2.2f);
        if (isFront) b.box({-HW - 0.015f, 2.30f, zf - 2.0f}, {0.01f, 0.18f, 0.9f}, LED_ORANGE * 0.9f, MAT_LIGHT);
        interior(b, m, si, zr + 0.1f, zf - 0.1f, axles, doorZ, isFront, zf);
        m.sec[si].body = b.upload();
        m.sec[si].glass = g.upload();
        m.sec[si].length = L;
        m.sec[si].doorZ = doorZ;
        m.sec[si].axleF = axles.size() > 1 ? axles[0] : 0;
        m.sec[si].axleR = axles.back();
    };
    if (!articulated) {
        // VDL Citea SLF-120 Electric: 12,0 m, overhang voor 2,7 m, wielbasis 5,9 m
        section(0, 12.0f, {6.0f - 2.7f, 6.0f - 2.7f - 5.9f}, {6.0f - 0.85f, -0.4f}, true, true, false, false);
    } else {
        // VDL Citea SLFA-181 Electric: 18,1 m geleed (voorwagen 11,0 m + harmonica 0,7 m + achterwagen 6,4 m)
        section(0, 11.0f, {5.5f - 2.7f, 5.5f - 2.7f - 5.9f}, {5.5f - 0.85f, -0.9f}, true, false, true, false);
        section(1, 6.4f, {-0.3f}, {3.2f - 1.3f}, false, true, false, true);
        m.sec[1].axleF = 3.2f + 0.35f;  // koppelpunt (midden harmonica)
        const float G = 0.7f;
        MB b;
        int rings = 9;
        for (int k = 0; k < rings; k++) {
            float z = -G / 2 + G * (k + 0.5f) / rings;
            float s = (k % 2) ? 0.97f : 1.0f;
            // ring (hol), zodat je erdoorheen kunt lopen
            b.box({0, 2.95f * s, z}, {2.42f * s, 0.16f, G / rings}, (k % 2) ? DARK : BUMPER, MAT_RUBBER);
            for (int sd = -1; sd <= 1; sd += 2) b.box({sd * 1.15f * s, 1.65f, z}, {0.14f, 2.6f * s, G / rings}, (k % 2) ? DARK : BUMPER, MAT_RUBBER);
        }
        b.box({0, FL - 0.02f, 0}, {1.6f, 0.04f, G + 0.3f}, INT_FLOOR, MAT_RUBBER);  // draaischijf
        m.bellows = b.upload();
        m.jointGap = G;
    }
    // chauffeur en stuur (voorwagen, cabine links voor)
    {
        float cz = m.sec[0].length / 2 - 1.3f;
        MB d;
        d.box({0.62f, FL + 1.0f, cz - 0.05f}, {0.42f, 0.62f, 0.28f}, Vec3(0.12f, 0.14f, 0.20f), MAT_RUBBER);
        d.box({0.62f, FL + 1.48f, cz - 0.02f}, {0.20f, 0.24f, 0.22f}, Vec3(0.80f, 0.62f, 0.50f), MAT_RUBBER);
        d.box({0.62f, FL + 0.72f, cz + 0.25f}, {0.38f, 0.16f, 0.45f}, Vec3(0.12f, 0.14f, 0.20f), MAT_RUBBER);
        for (int s = -1; s <= 1; s += 2)  // armen naar het stuur
            d.tube({0.62f + s * 0.20f, FL + 1.22f, cz}, {0.62f + s * 0.17f, FL + 1.02f, cz + 0.45f}, 0.045f, 6, Vec3(0.12f, 0.14f, 0.20f), MAT_RUBBER);
        m.driver = d.upload();
        // stuur: rond, in het xy-vlak rond de oorsprong (wordt gekanteld en gedraaid getekend)
        MB w;
        const float R = 0.22f;
        const int N = 20;
        for (int k = 0; k < N; k++) {
            float a0 = 2 * PI * k / N, a1 = 2 * PI * (k + 1) / N;
            w.tube({R * std::cos(a0), R * std::sin(a0), 0}, {R * std::cos(a1), R * std::sin(a1), 0}, 0.018f, 6, Vec3(0.07f, 0.07f, 0.08f), MAT_RUBBER);
        }
        for (float a : {-PI / 2, PI / 6 - 0.2f, 5 * PI / 6 + 0.2f})
            w.tube({0, 0, 0.01f}, {R * std::cos(a), R * std::sin(a), 0}, 0.014f, 5, Vec3(0.18f, 0.18f, 0.19f), MAT_METAL);
        w.box({0, 0, 0.02f}, {0.10f, 0.10f, 0.05f}, Vec3(0.12f, 0.12f, 0.13f), MAT_RUBBER);
        w.box({0, R, 0}, {0.03f, 0.02f, 0.04f}, Vec3(0.9f, 0.6f, 0.1f), MAT_PAINT);  // markering bovenaan
        m.wheel = w.upload();
        m.wheelPos = Vec3(0.62f, FL + 1.02f, cz + 0.58f);
        m.driverEye = Vec3(0.62f, FL + 1.50f, cz - 0.02f);
    }
    // deurblad (rechterzijde, lokale -x): glas met rand
    {
        MB d;
        float w = BUS_DOOR_W / 2 - 0.02f;
        d.box({0, (FL + 0.6f) / 2 + 0.02f, 0}, {0.04f, 0.6f - FL, w}, BUMPER, MAT_PAINT);
        d.box({0, 1.55f, 0}, {0.035f, 1.9f, w - 0.06f}, DARKGLASS, MAT_GLASS);
        d.box({0, 2.52f, 0}, {0.04f, 0.08f, w}, BUMPER, MAT_PAINT);
        for (float e : {-1.0f, 1.0f}) d.box({0, 1.48f, e * (w / 2 - 0.02f)}, {0.045f, 2.1f, 0.04f}, BUMPER, MAT_PAINT);
        d.box({0.03f, 1.2f, 0}, {0.02f, 0.8f, 0.03f}, Vec3(0.7f, 0.7f, 0.72f), MAT_METAL);  // handgreep
        m.doorLeaf = d.upload();
    }
    return m;
}
