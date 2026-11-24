#pragma once
// Station Zwolle: lokaal assenstelsel, vormen uit OpenStreetMap en de zones waar het terrein en de tegelgeometrie
// (BGT-vlakken, 3D BAG-kappen) bij het laden worden weggesneden, zodat de eigen tunnel, trapgaten en kappen
// (zwollestation.cpp) zichtbaar zijn. Gedeeld door world.cpp (laden) en zwollestation.cpp (bouwen).
//   u: langs de sporen (richting 120 graden, oost-zuidoost: Herfte/Kampen-kant)
//   v: dwars (positief = zuid-zuidwest: van het stationsgebouw naar de Zuidzijde)
#include <cmath>

namespace Zwolle {

constexpr double RD_X = 202756.0, RD_Y = 502218.0, BEARING_DEG = 120.0;

inline void local(double rx, double ry, float& u, float& v) {
    const double b = BEARING_DEG * 3.14159265358979 / 180.0;
    double dx = rx - RD_X, dy = ry - RD_Y;
    u = (float)(dx * std::sin(b) + dy * std::cos(b));
    v = (float)(dx * std::cos(b) - dy * std::sin(b));
}
inline void toRd(float u, float v, double& rx, double& ry) {
    const double b = BEARING_DEG * 3.14159265358979 / 180.0;
    rx = RD_X + u * std::sin(b) + v * std::cos(b);
    ry = RD_Y + u * std::cos(b) - v * std::sin(b);
}

struct Rect { float u0, u1, v0, v1; };
struct Poly { int n; float p[20][2]; };

// ---- reizigerstunnel (2015): 17 m breed, ~120 m lang, 3,5 m hoog ----
constexpr float TUN_U0 = 76.4f, TUN_U1 = 93.6f;   // wanden
constexpr float TUN_V0 = 10.0f, TUN_V1 = 100.2f;  // van de noordtrappen tot de winkelzone bij het busperron
// winkelzone en zuidopgang (niveau -1) tot de roltrappen naar de Zuidzijde
constexpr float SOUTH_U0 = 70.6f, SOUTH_U1 = 98.0f, SOUTH_V1 = 128.7f;

// Trap/roltrapgaten per eilandperron (OSM): west (opgang naar het westen) en oost; v-bereik van het gat
struct Island { float v0, v1, escV, stairV; const char* tracks; bool westUp, eastUp; };
constexpr Island ISLANDS[4] = {
    {27.5f, 34.0f, 28.3f, 31.55f, "3  5", true, false},
    {47.5f, 54.0f, 48.3f, 51.55f, "6  7", true, false},
    {72.0f, 78.5f, 72.8f, 76.05f, "9  10", true, false},
    {108.5f, 114.0f, 109.3f, 112.0f, "", true, false},  // busperron (maaiveld)
};
constexpr float WEST_U0 = 56.7f, WEST_U1 = 78.0f;   // westelijk trapgat (bovenkant bij u0)
constexpr float EAST_U0 = 90.4f, EAST_U1 = 111.6f;  // oostelijk trapgat (bovenkant bij u1)
constexpr float LIFT_U0 = 82.7f, LIFT_U1 = 85.5f;   // glazen liftschachten midden in de tunnel
// noordopgang naar de entree onder de vierkante kap (2003)
constexpr Rect NORTH_HOLE{76.3f, 93.4f, -5.0f, 10.6f};
// zuidopgang naar de Zuidzijde (busstation / Hanzelaan)
constexpr Rect SOUTH_HOLE{74.9f, 93.8f, 128.5f, 144.4f};

// stationskap (1995) boven perron 1 en 3/5 (west), vervangt de oude kap uit de 19e eeuw
constexpr Rect KAP{-64.0f, 67.2f, 6.3f, 37.0f};

// overkappingen uit OSM (building=roof), worden vervangen door eigen kappen
constexpr Poly CANOPIES[] = {
    // Perron 3/5 oost (OSM way 1134520434)
    {7, {{67.2f, 24.8f}, {67.2f, 36.1f}, {323.5f, 35.3f}, {332.5f, 34.6f}, {332.5f, 24.8f}, {330.6f, 24.7f}, {295.6f, 24.0f}}},
    // Perron 6/7 (OSM way 1134520436)
    {8, {{16.8f, 44.6f}, {16.5f, 55.0f}, {22.4f, 55.4f}, {42.4f, 56.4f}, {62.4f, 56.7f}, {78.0f, 56.6f}, {204.9f, 56.0f}, {204.9f, 44.2f}}},
    // Perron 8/9/10 (OSM way 1134520435)
    {20, {{-40.1f, 65.2f}, {-40.9f, 74.5f}, {-29.3f, 75.9f}, {-4.8f, 78.3f}, {5.2f, 79.0f}, {25.3f, 80.3f}, {45.1f, 81.1f}, {59.9f, 81.5f}, {76.3f, 81.7f}, {81.2f, 81.7f},
          {99.9f, 81.5f}, {144.8f, 78.9f}, {148.2f, 78.5f}, {147.9f, 69.2f}, {139.5f, 69.2f}, {94.9f, 69.3f}, {61.3f, 69.4f}, {32.3f, 69.0f}, {4.1f, 68.3f}, {-17.3f, 67.0f}}},
    // Perron 1b/14 (2003) (OSM way 1134520437)
    {16, {{97.2f, 3.6f}, {97.2f, 7.8f}, {93.4f, 7.8f}, {76.3f, 7.8f}, {65.1f, 7.8f}, {65.0f, 13.4f}, {97.2f, 13.4f}, {176.6f, 13.3f}, {331.8f, 12.6f}, {331.9f, 4.8f},
          {326.1f, 4.3f}, {308.2f, 3.8f}, {176.8f, 3.9f}, {125.0f, 3.6f}, {114.8f, 3.6f}, {106.6f, 3.6f}}},
    // Busperron (OSM way 1134520433)
    {8, {{50.8f, 104.6f}, {48.7f, 107.4f}, {48.6f, 114.9f}, {50.8f, 117.9f}, {118.2f, 117.9f}, {119.9f, 114.8f}, {119.8f, 107.4f}, {118.0f, 104.5f}}},
    // Entree Zuidzijde (OSM way 1134520432)
    {10, {{93.6f, 128.5f}, {90.5f, 128.5f}, {87.2f, 128.6f}, {81.4f, 128.7f}, {78.7f, 128.7f}, {76.0f, 128.7f}, {74.7f, 128.7f}, {74.9f, 144.4f}, {93.8f, 144.2f}, {93.8f, 141.4f}}},
};
constexpr int NUM_CANOPIES = sizeof(CANOPIES) / sizeof(CANOPIES[0]);
// vierkante kap (2003) bij de noordentree: alles boven het maaiveld wordt vervangen
constexpr Rect SQUARE{65.1f, 97.2f, -20.4f, 7.8f};

inline bool inRect(const Rect& r, float u, float v, float m = 0) { return u > r.u0 - m && u < r.u1 + m && v > r.v0 - m && v < r.v1 + m; }
inline bool inPoly(const Poly& p, float u, float v) {
    bool in = false;
    for (int k = 0, j = p.n - 1; k < p.n; j = k++)
        if ((p.p[k][1] > v) != (p.p[j][1] > v) && u < (p.p[j][0] - p.p[k][0]) * (v - p.p[k][1]) / (p.p[j][1] - p.p[k][1]) + p.p[k][0]) in = !in;
    return in;
}

// Open gaten (trappen, roltrappen, liften): hier geen terrein en geen lage BGT-vlakken
inline bool inOpening(float u, float v, float m = 0) {
    if (inRect(NORTH_HOLE, u, v, m) || inRect(SOUTH_HOLE, u, v, m)) return true;
    for (const Island& is : ISLANDS) {
        if (v < is.v0 - 0.3f - m || v > is.v1 + 0.3f + m) continue;
        if ((u > WEST_U0 - 1.5f - m && u < WEST_U1 + m) || (u > EAST_U0 - m && u < EAST_U1 + 1.5f + m) || (u > LIFT_U0 - m && u < LIFT_U1 + m)) return true;
    }
    return false;
}

// Alle open gaten als rechthoeken (voor de driehoekstoets)
inline int openings(Rect* out) {
    int n = 0;
    out[n++] = NORTH_HOLE;
    out[n++] = SOUTH_HOLE;
    for (const Island& is : ISLANDS) {
        out[n++] = {WEST_U0 - 1.5f, WEST_U1, is.v0 - 0.3f, is.v1 + 0.3f};
        out[n++] = {EAST_U0, EAST_U1 + 1.5f, is.v0 - 0.3f, is.v1 + 0.3f};
        out[n++] = {LIFT_U0, LIFT_U1, is.v0 - 0.3f, is.v1 + 0.3f};
    }
    return n;
}

// Overlapt driehoek (u[3], v[3]) de rechthoek? (scheidende-assentoets in 2D)
inline bool triRect(const float* u, const float* v, const Rect& r) {
    float umin = std::fmin(u[0], std::fmin(u[1], u[2])), umax = std::fmax(u[0], std::fmax(u[1], u[2]));
    float vmin = std::fmin(v[0], std::fmin(v[1], v[2])), vmax = std::fmax(v[0], std::fmax(v[1], v[2]));
    if (umax < r.u0 || umin > r.u1 || vmax < r.v0 || vmin > r.v1) return false;
    const float cu[4] = {r.u0, r.u1, r.u1, r.u0}, cv[4] = {r.v0, r.v0, r.v1, r.v1};
    for (int e = 0; e < 3; e++) {
        int f = (e + 1) % 3, g = (e + 2) % 3;
        float nu = -(v[f] - v[e]), nv = u[f] - u[e];
        float tri = nu * (u[g] - u[e]) + nv * (v[g] - v[e]);  // kant waar de driehoek ligt
        bool allOut = true;
        for (int c = 0; c < 4; c++) {
            float d = nu * (cu[c] - u[e]) + nv * (cv[c] - v[e]);
            if (d * tri >= 0) { allOut = false; break; }
        }
        if (allOut) return false;
    }
    return true;
}

// Tegelgeometrie wegsnijden? Driehoek in RD (rx, ry), yRel = hoogte zwaartepunt boven het terrein
inline bool cutTriangle(const double* rx, const double* ry, float yRel) {
    float u[3], v[3];
    for (int k = 0; k < 3; k++) local(rx[k], ry[k], u[k], v[k]);
    float cu = (u[0] + u[1] + u[2]) / 3, cv = (v[0] + v[1] + v[2]) / 3;
    if (std::fmax(u[0], std::fmax(u[1], u[2])) < -250 || std::fmin(u[0], std::fmin(u[1], u[2])) > 450) return false;
    if (std::fmax(v[0], std::fmax(v[1], v[2])) < -60 || std::fmin(v[0], std::fmin(v[1], v[2])) > 170) return false;
    if (yRel < 4.5f) {  // ook BAG-dakdelen net boven het perron (de stationsvoetafdruk loopt over de trapgaten)
        Rect rs[20];
        int n = openings(rs);
        for (int k = 0; k < n; k++)
            if (triRect(u, v, rs[k])) return true;
    }
    if (inRect(SQUARE, cu, cv) && yRel > 0.5f) return true;
    if (yRel > 3.3f) {
        if (inRect(KAP, cu, cv)) return true;
        for (const Poly& p : CANOPIES)
            if (inPoly(p, cu, cv)) return true;
    }
    return false;
}

// Terreinvlakje (middelpunt) wegsnijden?
inline bool cut(double rx, double ry, float yRel) {
    float u, v;
    local(rx, ry, u, v);
    if (u < -250 || u > 450 || v < -60 || v > 170) return false;
    // ruim (1,5 m): het terreinraster (2 m, langs RD) staat schuin op de gaten en zou er met een zaagrand in steken;
    // de randen worden afgedekt door de perrons en de eigen vloeren (plein, busperron, rand zuidopgang)
    if (inOpening(u, v, 1.5f) && yRel < 2.5f) return true;
    return false;
}
inline bool isNear(double rx, double ry, double margin) {
    float u, v;
    local(rx, ry, u, v);
    return u > -250 - margin && u < 450 + margin && v > -60 - margin && v < 170 + margin;
}

}  // namespace Zwolle
