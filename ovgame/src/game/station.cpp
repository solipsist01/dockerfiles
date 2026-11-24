// Station Eindhoven Centraal, gereconstrueerd op basis van:
//  - OpenStreetMap: positie van passage, trappen, roltrappen, liften, winkels en perrons
//  - 3D BAG: plattegronden van Zuidhal (1956) en Noordhal (Noordzicht, 1985)
//  - AHN: hoogte van het spoor (perrons = spoor + 0,8 m)
// Alles wordt opgebouwd in het lokale stationsassenstelsel (u langs de sporen, v naar het noorden).
#include "station.h"
#include <map>
#include <set>
#include <unordered_map>
#include <algorithm>
#include <array>
#include "../third_party/earcut.hpp"
#include "zwolle.h"

using namespace WorldFormat;

void buildUtrechtCentraal(const World& world, Collision& col, SignAtlas& atlas, MeshBuilder& op, MeshBuilder& gl,
                          std::vector<Elevator>& elevators, Vec3& hallSpawn, Vec3& platformSpawn, float& hallY);
void buildZwolle(const World& world, Collision& col, SignAtlas& atlas, MeshBuilder& op, MeshBuilder& gl, std::vector<Elevator>& elevators,
                 Escalators& esc, std::vector<BoardSpot>& boards, Vec3& tunnelSpawn);
void buildBestStation(const World& world, Collision& col, SignAtlas& atlas, MeshBuilder& op, MeshBuilder& gl,
                      std::vector<Elevator>& elevators, Vec3& center, float& platformY, float& hallY);

namespace {

// ---------- kleuren ----------
const Vec3 FLOOR_STONE(0.74f, 0.73f, 0.70f);
const Vec3 WALL_PANEL(0.86f, 0.86f, 0.84f);
const Vec3 WALL_DARK(0.30f, 0.31f, 0.33f);
const Vec3 CONCRETE(0.62f, 0.62f, 0.60f);
const Vec3 PLATFORM_TOP(0.60f, 0.60f, 0.58f);
const Vec3 STEEL_GREY(0.42f, 0.44f, 0.46f);
const Vec3 STAIR_STEP(0.50f, 0.50f, 0.50f);
const Vec3 GLASS_TINT(0.55f, 0.65f, 0.68f);
const Vec3 CEILING_LIGHT(0.92f, 0.92f, 0.88f);
const Vec3 WHITE_LINE(0.92f, 0.92f, 0.90f);
const Vec3 WOOD(0.55f, 0.38f, 0.22f);
const Vec3 NS_BLUE(0.0f, 0.13f, 0.43f);
const Vec3 NS_YELLOW(1.0f, 0.78f, 0.0f);

// Eilandperrons van Eindhoven Centraal (lokaal): v-grenzen uit OSM-sporen (+/- 1,65 m), u-grenzen uit OSM-perrons
struct Island { float v0, v1, u0, u1; int trackS, trackN; };
const Island ISLANDS[3] = {
    {-29.03f, -13.38f, -124.0f, 304.0f, 1, 2},
    {-5.58f, 6.34f, -51.0f, 304.0f, 3, 4},
    {14.59f, 28.72f, -115.0f, 305.0f, 5, 6},
};
// Sporen (hartlijn, lokaal v)
const float TRACK_V[7] = {0, -30.68f, -11.73f, -7.23f, 7.99f, 12.94f, 30.37f};

// Openingen in een perron (trappen, roltrappen, lift)
const float STAIR_U0 = -10.2f, STAIR_U1 = -0.6f, STAIR_W = 2.6f, STAIR_OFF = 3.1f, STAIR_HOLE_U0 = -8.2f;
const float ESC_U1 = 15.6f, ESC_HOLE_U0 = 6.5f, ESC_HALF = 1.75f;  // roltrappen: bovenste kam op ESC_U1
const float LIFT_U0 = -14.6f, LIFT_U1 = -12.2f;

struct Rect { float u0, u1, v0, v1; };

// Rechthoek min gaten -> lijst van rechthoeken (stroken langs u)
std::vector<Rect> subtract(const Rect& r, const std::vector<Rect>& holes) {
    std::vector<float> cuts{r.u0, r.u1};
    for (const Rect& h : holes) {
        if (h.u0 > r.u0 && h.u0 < r.u1) cuts.push_back(h.u0);
        if (h.u1 > r.u0 && h.u1 < r.u1) cuts.push_back(h.u1);
    }
    std::sort(cuts.begin(), cuts.end());
    std::vector<Rect> out;
    for (size_t k = 0; k + 1 < cuts.size(); k++) {
        float a = cuts[k], b = cuts[k + 1];
        if (b - a < 1e-3f) continue;
        float m = (a + b) / 2;
        std::vector<std::pair<float, float>> blocked;
        for (const Rect& h : holes)
            if (m > h.u0 && m < h.u1) blocked.push_back({std::max(h.v0, r.v0), std::min(h.v1, r.v1)});
        std::sort(blocked.begin(), blocked.end());
        float v = r.v0;
        for (auto& bl : blocked) {
            if (bl.first > v + 1e-3f) out.push_back({a, b, v, bl.first});
            v = std::max(v, bl.second);
        }
        if (r.v1 > v + 1e-3f) out.push_back({a, b, v, r.v1});
    }
    return out;
}

// ---------- bouwer in stationscoordinaten ----------
struct SB {
    MeshBuilder op, gl;
    Collision& col;
    SignAtlas& atlas;
    float b;
    Vec3 U, V;
    int frame;  // 0 = Eindhoven Centraal, 1 = Strijp-S
    SB(Collision& c, SignAtlas& a, int fr = 0) : col(c), atlas(a), frame(fr) {
        b = (float)((fr == 0 ? STATION_BEARING_DEG : STRIJPS_AXIS_DEG) * PI / 180.0);
        U = Vec3(std::cos(b), 0, -std::sin(b));
        V = Vec3(-std::sin(b), 0, -std::cos(b));
    }
    Vec3 P(float u, float v, float y) const {
        float x, z;
        if (frame == 0) stationToGame(u, v, x, z);
        else {
            double rx, ry;
            strijpsToRd(u, v, rx, ry);
            x = gameX(rx);
            z = gameZ(ry);
        }
        return Vec3(x, y, z);
    }
    // schuin vlak langs u (trap/helling): hoogte ya bij u0, yb bij u1
    void floorSlopeU(float u0, float u1, float v0, float v1, float ya, float yb) {
        Vec3 c = P((u0 + u1) / 2, (v0 + v1) / 2, 0);
        col.addFloor({c.x, c.z, U.x, U.z, (u1 - u0) / 2, (v1 - v0) / 2, ya, yb, 0});
    }
    void floorSlopeV(float u0, float u1, float v0, float v1, float ya, float yb) {
        Vec3 c = P((u0 + u1) / 2, (v0 + v1) / 2, 0);
        col.addFloor({c.x, c.z, V.x, V.z, (v1 - v0) / 2, (u1 - u0) / 2, ya, yb, 0});
    }
    void addBoxCollision(float u0, float u1, float v0, float v1, float y0, float y1) {
        Vec3 c = P((u0 + u1) / 2, (v0 + v1) / 2, 0);
        col.addBox({c.x, c.z, U.x, U.z, (u1 - u0) / 2, (v1 - v0) / 2, y0, y1});
    }
    void box(float u0, float u1, float v0, float v1, float y0, float y1, Vec3 c, float ex = EX_PLAIN, bool solid = false) {
        op.orientedBox(P((u0 + u1) / 2, (v0 + v1) / 2, (y0 + y1) / 2), {u1 - u0, y1 - y0, v1 - v0}, b, c, ex);
        if (solid) addBoxCollision(u0, u1, v0, v1, y0, y1);
    }
    void glassBox(float u0, float u1, float v0, float v1, float y0, float y1, bool solid = true) {
        gl.orientedBox(P((u0 + u1) / 2, (v0 + v1) / 2, (y0 + y1) / 2), {u1 - u0, y1 - y0, v1 - v0}, b, GLASS_TINT, EX_GLASS);
        if (solid) addBoxCollision(u0, u1, v0, v1, y0, y1);
    }
    void lightBox(float u0, float u1, float v0, float v1, float y0, float y1, Vec3 c = CEILING_LIGHT) {
        op.orientedBox(P((u0 + u1) / 2, (v0 + v1) / 2, (y0 + y1) / 2), {u1 - u0, y1 - y0, v1 - v0}, b, c, EX_LIGHT);
    }
    // horizontaal vlak (boven of onder zichtbaar)
    void flat(const Rect& r, float y, Vec3 c, float ex, bool up) {
        Vec3 n(0, up ? 1.0f : -1.0f, 0);
        op.quad(P(r.u0, r.v0, y), P(r.u1, r.v0, y), P(r.u1, r.v1, y), P(r.u0, r.v1, y), n, c, ex);
    }
    void floorCollision(const Rect& r, float y) {
        Vec3 c = P((r.u0 + r.u1) / 2, (r.v0 + r.v1) / 2, 0);
        col.addFloor({c.x, c.z, U.x, U.z, (r.u1 - r.u0) / 2, (r.v1 - r.v0) / 2, y, y, 0});
    }
    void sign(const std::string& text, Vec3 bg, Vec3 fg, float u, float v, float y, Vec3 right, float h) {
        SignRect r = atlas.add(text, bg, fg, 72);
        SignAtlas::quad(op, r, P(u, v, y), right, h, true);
    }
};

// Winkelunit: rechthoek (lokaal) met open pui aan zijde 'front' (0: u0, 1: u1, 2: v0, 3: v1)
struct ShopStyle { Vec3 bg, fg, floor, accent; };

void buildShop(SB& s, const std::string& name, Rect r, int front, ShopStyle st, float F) {
    const float H = 3.4f;
    // vloer en plafond (lichtpanelen)
    s.flat(r, F + 0.01f, st.floor, EX_PLAIN, true);
    s.flat(r, F + H, WALL_PANEL, EX_PLAIN, false);
    for (float u = r.u0 + 1.0f; u < r.u1 - 0.5f; u += 2.0f)
        s.lightBox(u - 0.3f, u + 0.3f, r.v0 + 0.8f, r.v1 - 0.8f, F + H - 0.03f, F + H - 0.01f);
    // wanden (3 zijden), pui boven de opening
    const float T = 0.15f;
    if (front != 0) s.box(r.u0 - T, r.u0, r.v0, r.v1, F, F + H, WALL_PANEL, EX_PLAIN, true);
    if (front != 1) s.box(r.u1, r.u1 + T, r.v0, r.v1, F, F + H, WALL_PANEL, EX_PLAIN, true);
    if (front != 2) s.box(r.u0, r.u1, r.v0 - T, r.v0, F, F + H, WALL_PANEL, EX_PLAIN, true);
    if (front != 3) s.box(r.u0, r.u1, r.v1, r.v1 + T, F, F + H, WALL_PANEL, EX_PLAIN, true);
    // gevelband boven de opening + naambord
    float fu = 0, fv = 0;
    Vec3 right, out;
    float width;
    if (front == 0 || front == 1) {
        fu = front == 0 ? r.u0 : r.u1;
        fv = (r.v0 + r.v1) / 2;
        width = r.v1 - r.v0;
        float o = front == 0 ? -1.0f : 1.0f;
        s.box(fu - 0.1f, fu + 0.1f, r.v0, r.v1, F + 2.8f, F + H, st.accent);
        right = s.V * (front == 0 ? -1.0f : 1.0f);
        out = s.U * o;
        fu += o * 0.12f;
    } else {
        fv = front == 2 ? r.v0 : r.v1;
        fu = (r.u0 + r.u1) / 2;
        width = r.u1 - r.u0;
        float o = front == 2 ? -1.0f : 1.0f;
        s.box(r.u0, r.u1, fv - 0.1f, fv + 0.1f, F + 2.8f, F + H, st.accent);
        right = s.U * (front == 2 ? 1.0f : -1.0f);
        out = s.V * o;
        fv += o * 0.12f;
    }
    SignRect sr = s.atlas.add(name, st.bg, st.fg, 72);
    float sh = std::min(0.48f, (width - 0.6f) / sr.aspect);
    SignAtlas::quad(s.op, sr, s.P(fu, fv, F + 3.1f), right, sh, false);
    (void)out;
    // inrichting: toonbank en schappen tegen de achterwand
    float cu = (r.u0 + r.u1) / 2, cv = (r.v0 + r.v1) / 2;
    float du = r.u1 - r.u0, dv = r.v1 - r.v0;
    if (front == 0 || front == 1) {
        float back = front == 0 ? r.u1 - 0.5f : r.u0 + 0.5f;
        float counter = front == 0 ? r.u0 + du * 0.45f : r.u1 - du * 0.45f;
        s.box(counter - 0.35f, counter + 0.35f, cv - dv * 0.25f, cv + dv * 0.3f, F, F + 1.05f, st.accent, EX_PLAIN, true);
        for (int k = 0; k < 4; k++)
            s.box(back - 0.25f, back + 0.25f, r.v0 + 0.5f, r.v1 - 0.5f, F + 0.3f + k * 0.5f, F + 0.36f + k * 0.5f, k % 2 ? st.bg : st.accent);
        s.lightBox(back - 0.26f, back + 0.26f, r.v0 + 0.6f, r.v1 - 0.6f, F + 2.3f, F + 2.5f, Vec3(0.9f, 0.85f, 0.7f));
    } else {
        float back = front == 2 ? r.v1 - 0.5f : r.v0 + 0.5f;
        float counter = front == 2 ? r.v0 + dv * 0.45f : r.v1 - dv * 0.45f;
        s.box(cu - du * 0.25f, cu + du * 0.3f, counter - 0.35f, counter + 0.35f, F, F + 1.05f, st.accent, EX_PLAIN, true);
        for (int k = 0; k < 4; k++)
            s.box(r.u0 + 0.5f, r.u1 - 0.5f, back - 0.25f, back + 0.25f, F + 0.3f + k * 0.5f, F + 0.36f + k * 0.5f, k % 2 ? st.bg : st.accent);
        s.lightBox(r.u0 + 0.6f, r.u1 - 0.6f, back - 0.26f, back + 0.26f, F + 2.3f, F + 2.5f, Vec3(0.9f, 0.85f, 0.7f));
    }
}

// ================= STATION EINDHOVEN STRIJP-S =================
struct SSStair { float vc, top, floor; int steps; };

std::vector<SSStair> strijpsStairs(float P) {
    std::vector<SSStair> out;
    for (float vc : SS_STAIR_V) {
        float F = strijpsTunnelFloor(vc);
        int n = (int)std::round((P - F) / 0.18f);
        out.push_back({vc, SS_TUN_U0 - n * 0.30f, F, n});
    }
    return out;
}

bool pointInPoly(const std::vector<std::pair<float, float>>& r, float x, float z) {
    bool in = false;
    for (size_t k = 0, j = r.size() - 1; k < r.size(); j = k++)
        if ((r[k].second > z) != (r[j].second > z) && x < (r[j].first - r[k].first) * (z - r[k].second) / (r[j].second - r[k].second) + r[k].first)
            in = !in;
    return in;
}

void buildStrijpS(SB& s, const World& world, float P) {
    const Vec3 TILE(0.88f, 0.88f, 0.86f), BAND(0.0f, 0.30f, 0.62f);
    const float R = P - 0.8f, C = R - 1.0f;
    const float FA = strijpsTunnelFloor(SS_TUN_V0), FB = strijpsTunnelFloor(SS_TUN_V1);
    const float FMIN = std::min(FA, FB) - 0.3f;
    // ---- tunnel onder de spoordijk ----
    s.op.quad(s.P(SS_TUN_U0, SS_TUN_V0, FA + 0.01f), s.P(SS_TUN_U1, SS_TUN_V0, FA + 0.01f), s.P(SS_TUN_U1, SS_TUN_V1, FB + 0.01f),
              s.P(SS_TUN_U0, SS_TUN_V1, FB + 0.01f), Vec3(0, 1, 0), FLOOR_STONE * 0.9f, EX_PLAIN);
    s.floorSlopeV(SS_TUN_U0, SS_TUN_U1, SS_TUN_V0, SS_TUN_V1, FA, FB);
    s.flat({SS_TUN_U0, SS_TUN_U1, SS_TUN_V0, SS_TUN_V1}, C, WALL_PANEL * 0.95f, EX_PLAIN, false);
    for (float v = SS_TUN_V0 + 2; v < SS_TUN_V1 - 1; v += 4) s.lightBox(SS_TUN_U0 + 1.0f, SS_TUN_U1 - 1.0f, v, v + 0.5f, C - 0.05f, C - 0.01f);
    s.box(SS_TUN_U0 - 0.3f, SS_TUN_U1 + 0.3f, SS_TUN_V0, SS_TUN_V1, C, R - 0.45f, CONCRETE);  // spoordek
    // zuidoostwand (dicht)
    s.box(SS_TUN_U1, SS_TUN_U1 + 0.3f, SS_TUN_V0, SS_TUN_V1, FMIN, P + 0.2f, TILE, EX_PLAIN, true);
    s.box(SS_TUN_U1 - 0.02f, SS_TUN_U1, SS_TUN_V0, SS_TUN_V1, FMIN + 1.4f, FMIN + 1.6f, BAND);
    // noordwestwand met openingen naar de trappen
    std::vector<SSStair> stairs = strijpsStairs(P);
    {
        float v = SS_TUN_V0;
        for (const SSStair& st : stairs) {
            float a = st.vc - SS_STAIR_W / 2 - 0.3f, b2 = st.vc + SS_STAIR_W / 2 + 0.3f;
            s.box(SS_TUN_U0 - 0.3f, SS_TUN_U0, v, a, FMIN, P + 0.2f, TILE, EX_PLAIN, true);
            s.box(SS_TUN_U0, SS_TUN_U0 + 0.02f, v, a, FMIN + 1.4f, FMIN + 1.6f, BAND);
            s.box(SS_TUN_U0 - 0.3f, SS_TUN_U0, a, b2, C, P + 0.2f, TILE);  // latei boven de trapopening
            v = b2;
        }
        s.box(SS_TUN_U0 - 0.3f, SS_TUN_U0, v, SS_TUN_V1, FMIN, P + 0.2f, TILE, EX_PLAIN, true);
        s.box(SS_TUN_U0, SS_TUN_U0 + 0.02f, v, SS_TUN_V1, FMIN + 1.4f, FMIN + 1.6f, BAND);
    }
    // portalen: kader en stationsnaam aan beide kanten
    for (int end = 0; end < 2; end++) {
        float v = end ? SS_TUN_V1 : SS_TUN_V0;
        float o = end ? 0.15f : -0.15f;
        s.box(SS_TUN_U0 - 0.3f, SS_TUN_U1 + 0.3f, std::min(v, v + o), std::max(v, v + o), C - 0.3f, R - 0.3f, BAND);
        SignRect r = s.atlas.add("Eindhoven Strijp-S", NS_BLUE, Vec3(1, 1, 1), 72);
        float h = std::min(0.55f, (R - 0.3f - C) * 0.9f);
        SignAtlas::quad(s.op, r, s.P((SS_TUN_U0 + SS_TUN_U1) / 2, v + o * 1.3f, (C + R - 0.6f) / 2), s.U * (end ? -1.0f : 1.0f), h, true);
    }
    // ---- trappen van de tunnel naar de perrons ----
    const char* labels[3] = {"Spoor 4", "Spoor 2 | 3", "Spoor 1"};
    for (int k = 0; k < 3; k++) {
        const SSStair& st = stairs[k];
        float va = st.vc - SS_STAIR_W / 2, vb = st.vc + SS_STAIR_W / 2;
        float rise = (P - st.floor) / st.steps;
        for (int i = 0; i < st.steps; i++) {
            float ub = SS_TUN_U0 - i * 0.30f, ua = ub - 0.30f;
            s.box(ua, ub + 0.01f, va, vb, st.floor - 0.2f, st.floor + (i + 1) * rise, i % 2 ? STAIR_STEP : STAIR_STEP * 0.92f);
            s.box(ub - 0.05f, ub, va, vb, st.floor + (i + 1) * rise, st.floor + (i + 1) * rise + 0.005f, NS_YELLOW * 0.9f);
        }
        s.floorSlopeU(st.top, SS_TUN_U0, va, vb, P, st.floor);
        // trapgatwanden (beton) met glazen balustrade boven het perron, leuningen
        for (float side : {va - 0.15f, vb + 0.15f}) {
            s.box(st.top - 0.3f, SS_TUN_U0, side - 0.15f, side + 0.15f, st.floor - 0.2f, P, CONCRETE, EX_PLAIN, true);
            s.glassBox(st.top - 0.3f, SS_TUN_U0, side - 0.03f, side + 0.03f, P, P + 1.1f, true);
            Vec3 a = s.P(SS_TUN_U0, side - (side < st.vc ? -0.16f : 0.16f), st.floor + 0.9f);
            Vec3 b2 = s.P(st.top, side - (side < st.vc ? -0.16f : 0.16f), P + 0.9f);
            s.op.tube(a, b2, 0.03f, 6, STEEL_GREY, EX_PLAIN);
        }
        // bord in de tunnel boven de trapopening
        s.sign(labels[k], NS_BLUE, Vec3(1, 1, 1), SS_TUN_U0 + 0.4f, st.vc, C - 0.45f, s.V, 0.42f);
    }
    // ---- trap van perron 1 naar de straat (noordoostzijde) ----
    {
        float F = SS_NE_FLOOR;
        int n = (int)std::round((P - F) / 0.18f);
        float rise = (P - F) / n, run = n * 0.30f;
        float ua = SS_NE_STAIR_U - SS_STAIR_W / 2, ub = SS_NE_STAIR_U + SS_STAIR_W / 2;
        for (int i = 0; i < n; i++) {
            float v0 = SS_NE_STAIR_V0 + i * 0.30f;
            s.box(ua, ub, v0, v0 + 0.31f, F - 0.2f, P - i * rise, i % 2 ? STAIR_STEP : STAIR_STEP * 0.92f);
            s.box(ua, ub, v0, v0 + 0.05f, P - i * rise, P - i * rise + 0.005f, NS_YELLOW * 0.9f);
        }
        s.floorSlopeV(ua, ub, SS_NE_STAIR_V0, SS_NE_STAIR_V0 + run, P, F);
        for (float side : {ua - 0.15f, ub + 0.15f})
            for (int seg = 0; seg < 8; seg++) {
                float v0 = SS_NE_STAIR_V0 + run * seg / 8, v1 = SS_NE_STAIR_V0 + run * (seg + 1) / 8;
                float top = P - (P - F) * seg / 8 + 1.0f;
                s.box(side - 0.15f, side + 0.15f, v0, v1, F - 0.2f, top, CONCRETE, EX_PLAIN, true);
            }
        SignRect r = s.atlas.add("Eindhoven Strijp-S  |  Treinen", NS_BLUE, Vec3(1, 1, 1), 72);
        SignAtlas::quad(s.op, r, s.P(SS_NE_STAIR_U, SS_NE_STAIR_V0 + run + 1.0f, F + 2.6f), s.U * -1.0f, 0.45f, true);
        Vec3 base = s.P(SS_NE_STAIR_U, SS_NE_STAIR_V0 + run + 1.0f, F);
        s.op.tube(base, base + Vec3(0, 2.4f, 0), 0.05f, 6, STEEL_GREY, EX_PLAIN);
    }
    // ---- wachthuisjes op de perrons (posities uit OSM) ----
    const double shelters[][2] = {{159805, 384642}, {159841, 384624}, {159821, 384647}, {159830, 384614}, {159816, 384612}, {159797, 384635}};
    for (auto& sh : shelters) {
        float gx = gameX(sh[0]), gz = gameZ(sh[1]);
        const OsmPlatform* on = nullptr;
        for (const OsmPlatform& p : world.platforms)
            if (pointInPoly(p.pts, gx, gz)) on = &p;
        if (!on) continue;
        float u, v;
        strijpsLocal(sh[0], sh[1], u, v);
        float y = on->height;
        s.glassBox(u - 2.0f, u + 2.0f, v - 0.05f, v + 0.05f, y, y + 2.4f);
        s.glassBox(u - 2.05f, u - 1.95f, v, v + 1.4f, y, y + 2.4f);
        s.glassBox(u + 1.95f, u + 2.05f, v, v + 1.4f, y, y + 2.4f);
        s.box(u - 2.2f, u + 2.2f, v - 0.2f, v + 1.6f, y + 2.4f, y + 2.55f, Vec3(0.25f, 0.27f, 0.30f));
        s.box(u - 1.6f, u + 1.6f, v + 0.15f, v + 0.6f, y + 0.45f, y + 0.5f, WOOD, EX_PLAIN, true);
        s.lightBox(u - 1.5f, u + 1.5f, v + 0.5f, v + 0.7f, y + 2.36f, y + 2.39f);
    }
}

ShopStyle style(Vec3 bg, Vec3 fg) { return {bg, fg, Vec3(0.55f, 0.53f, 0.50f), bg * 0.85f}; }

// Kleine hoogteverschuiving per OSM-perron (0, 1,5 of 3 cm): aangrenzende perronvlakken overlappen in OSM soms een
// stukje; op precies dezelfde hoogte geeft dat op afstand z-fighting (knipperen).
float platOffset(const World& w, const OsmPlatform& p) { return (float)((&p - w.platforms.data()) % 3) * 0.015f; }

bool inPassage(float u, float v, float margin) {
    return u > PASSAGE_U0 - margin && u < PASSAGE_U1 + margin && v > PASSAGE_V0 - margin && v < PASSAGE_V1 + margin;
}

// Inrichting van een OSM-perron (Strijp-S, Boxtel, Vught, 's-Hertogenbosch): perronrand met witte geleidelijn,
// banken met afvalbakken, lantaarns en (bij grotere stations) een overkapping met kolommen rond het stationsmidden.
void platformDetails(SB& s, const World& world, const OsmPlatform& p, const std::string& station, Vec3 ax, float cx, float cz) {
    const float y = p.height + platOffset(world, p);
    const auto& P = p.pts;
    Vec3 rx(-ax.z, 0, ax.x);
    float hd = std::atan2(ax.x, ax.z);
    // randen langs het spoor
    for (size_t k = 0; k < P.size(); k++) {
        Vec3 a(P[k].first, 0, P[k].second), b(P[(k + 1) % P.size()].first, 0, P[(k + 1) % P.size()].second);
        Vec3 d = b - a;
        float len = std::hypot(d.x, d.z);
        if (len < 3) continue;
        Vec3 u = d * (1.0f / len), n(-u.z, 0, u.x);
        Vec3 m = (a + b) * 0.5f;
        if (pointInPoly(P, m.x + n.x * 0.5f, m.z + n.z * 0.5f)) n = n * -1.0f;
        float best = 1e9f;
        for (const RailLine& r : world.rails)
            for (size_t q = 0; q < r.pts.size(); q += 2) {
                if (std::fabs(r.pts[q].x - m.x) > len + 10 || std::fabs(r.pts[q].z - m.z) > len + 10) continue;
                best = std::min(best, std::hypot(r.pts[q].x - (m.x + n.x * 1.65f), r.pts[q].z - (m.z + n.z * 1.65f)));
            }
        if (best > 2.0f) continue;
        float ehd = std::atan2(u.x, u.z);
        // iets boven het perron (een paar mm gaf op afstand z-fighting); de onderkant zit onder het perronvlak
        s.op.orientedBox(m - n * 0.15f + Vec3(0, y + 0.012f, 0), {0.30f, 0.04f, len}, ehd, Vec3(0.86f, 0.85f, 0.80f), EX_PLAIN);
        s.op.orientedBox(m - n * 0.95f + Vec3(0, y + 0.014f, 0), {0.60f, 0.04f, len}, ehd, Vec3(0.88f, 0.88f, 0.86f), EX_PLAIN);
    }
    // stationsmidden (overkapping)
    Vec3 stc(cx, 0, cz);
    for (const MapPoint& mp : world.points)
        if (mp.kind == POINT_STATION && mp.name == station) stc = Vec3(mp.x, 0, mp.z);
    bool canopy = station == "Boxtel" || station == "Vught" || station == "'s-Hertogenbosch";
    float tc = (stc.x - cx) * ax.x + (stc.z - cz) * ax.z;
    float t0 = 1e9f, t1 = -1e9f;
    for (auto& q : P) {
        float t = (q.first - cx) * ax.x + (q.second - cz) * ax.z;
        t0 = std::min(t0, t); t1 = std::max(t1, t);
    }
    float c0 = std::max(t0 + 3, tc - 55), c1 = std::min(t1 - 3, tc + 55);
    auto lateral = [&](float t, float& l0, float& l1) {
        l0 = 1e9f; l1 = -1e9f;
        for (float l = -14; l <= 14; l += 0.25f) {
            Vec3 q = Vec3(cx, 0, cz) + ax * t + rx * l;
            if (pointInPoly(P, q.x, q.z)) { l0 = std::min(l0, l); l1 = std::max(l1, l); }
        }
        return l1 - l0 > 2.4f;
    };
    // Strijp-S: niet bij de trapgaten
    auto nearStairs = [&](Vec3 c) {
        if (station == "Zwolle") {
            float u, v;
            Zwolle::local(rdX(c.x), rdY(c.z), u, v);
            for (float du : {-2.5f, 0.0f, 2.5f})
                if (Zwolle::inOpening(u + du, v) || Zwolle::inOpening(u + du, v - 1.5f) || Zwolle::inOpening(u + du, v + 1.5f)) return true;
            return false;
        }
        if (station != "Eindhoven Strijp-S") return false;
        float u, v;
        strijpsLocal(rdX(c.x), rdY(c.z), u, v);
        return u > SS_STAIR_U0 - 4 || std::fabs(u - SS_NE_STAIR_U) < 4;
    };
    int idx = 0;
    for (float t = t0 + 10; t < t1 - 8; t += 16, idx++) {
        float l0, l1;
        if (!lateral(t, l0, l1)) continue;
        Vec3 c = Vec3(cx, 0, cz) + ax * t + rx * ((l0 + l1) / 2);
        if (nearStairs(c)) continue;
        bool covered = canopy && t > c0 && t < c1;
        if (idx % 2 == 0) {
            s.op.orientedBox(c + Vec3(0, y + 0.45f, 0), {0.48f, 0.06f, 2.0f}, hd, Vec3(0.55f, 0.36f, 0.20f), EX_PLAIN);
            s.op.orientedBox(c + rx * 0.22f + Vec3(0, y + 0.75f, 0), {0.05f, 0.5f, 2.0f}, hd, Vec3(0.55f, 0.36f, 0.20f), EX_PLAIN);
            for (float q : {-0.85f, 0.85f}) s.op.orientedBox(c + ax * q + Vec3(0, y + 0.22f, 0), {0.42f, 0.44f, 0.08f}, hd, Vec3(0.25f, 0.26f, 0.28f), EX_PLAIN);
            s.col.addBox({c.x, c.z, ax.x, ax.z, 1.0f, 0.3f, y, y + 0.5f});
            Vec3 bin = c + ax * 1.6f;
            s.op.tube(bin + Vec3(0, y, 0), bin + Vec3(0, y + 0.85f, 0), 0.22f, 10, Vec3(0.42f, 0.44f, 0.46f), EX_PLAIN);
        } else if (!covered) {
            Vec3 lp = c + rx * ((l1 - l0) / 2 - 1.2f);
            s.op.tube(lp + Vec3(0, y, 0), lp + Vec3(0, y + 5.0f, 0), 0.07f, 8, Vec3(0.35f, 0.36f, 0.38f), EX_PLAIN);
            s.op.box(lp + Vec3(0, y + 5.0f, 0) - rx * 0.35f, {0.7f, 0.08f, 0.25f}, Vec3(0.35f, 0.36f, 0.38f), EX_PLAIN);
            s.op.box(lp + Vec3(0, y + 4.95f, 0) - rx * 0.45f, {0.45f, 0.03f, 0.2f}, Vec3(1.0f, 0.96f, 0.85f), EX_LIGHT);
        }
    }
    // overkapping: dak in stroken (volgt de perronbreedte), kolommen op de middellijn
    if (canopy && c1 > c0 + 10) {
        const float H = y + 3.8f;
        for (float t = c0; t < c1; t += 4.0f) {
            float tb = std::min(c1, t + 4.0f);
            float l0, l1, m0, m1;
            if (!lateral(t + 0.1f, l0, l1) || !lateral(tb - 0.1f, m0, m1)) continue;
            Vec3 A = Vec3(cx, 0, cz) + ax * t + rx * (l0 + 0.4f), B = Vec3(cx, 0, cz) + ax * t + rx * (l1 - 0.4f);
            Vec3 C = Vec3(cx, 0, cz) + ax * tb + rx * (m1 - 0.4f), D = Vec3(cx, 0, cz) + ax * tb + rx * (m0 + 0.4f);
            for (Vec3* v : {&A, &B, &C, &D}) v->y = H;
            s.op.quad(A, B, C, D, Vec3(0, 1, 0), Vec3(0.40f, 0.42f, 0.45f), EX_PLAIN);
            s.op.quad(A + Vec3(0, -0.02f, 0), D + Vec3(0, -0.02f, 0), C + Vec3(0, -0.02f, 0), B + Vec3(0, -0.02f, 0), Vec3(0, -1, 0), Vec3(0.82f, 0.82f, 0.80f), EX_PLAIN);
            // dakrand en lichtlijn
            s.op.box((A + D) * 0.5f, {0.12f, 0.25f, 0.12f}, Vec3(0.35f, 0.36f, 0.38f), EX_PLAIN);
            Vec3 mid = (A + B + C + D) * 0.25f;
            s.op.orientedBox(mid - Vec3(0, 0.05f, 0), {0.25f, 0.03f, 3.6f}, hd, Vec3(1.0f, 0.97f, 0.9f), EX_LIGHT);
            if (((int)((t - c0) / 4.0f)) % 3 == 0) {
                Vec3 col = Vec3(cx, 0, cz) + ax * t + rx * ((l0 + l1) / 2);
                s.op.tube(col + Vec3(0, y, 0), col + Vec3(0, H, 0), 0.14f, 10, Vec3(0.30f, 0.32f, 0.36f), EX_PLAIN);
                s.col.addBox({col.x, col.z, ax.x, ax.z, 0.16f, 0.16f, y, H});
            }
        }
    }
}

}  // namespace

// =====================================================================================
void Stations::build(const World& world, Collision& col, SignAtlas& atlas) {
    SB s(col, atlas);
    const float F = STATION_FLOOR;
    const float C = 19.3f;  // plafond passage (onderkant spoordek)

    // Spoorhoogte bij het station meten
    float rsum = 0;
    int rn = 0;
    for (const RailLine& r : world.rails)
        for (const RailPoint& p : r.pts) {
            float u, v;
            gameToStation(p.x, p.z, u, v);
            if (std::fabs(u) < 40 && std::fabs(v) < 35) { rsum += p.y; rn++; }
        }
    const float R = rn ? rsum / rn : 20.35f;
    const float P = R + 0.80f;
    platformY = P;
    const float DECK = R - 0.45f;

    // ================= PASSAGE =================
    Rect pass{PASSAGE_U0, PASSAGE_U1, PASSAGE_V0, PASSAGE_V1};
    s.flat(pass, F + 0.005f, FLOOR_STONE, EX_PLAIN, true);
    // vloerlijnen (natuursteen-banen)
    for (float u = PASSAGE_U0 + 3; u < PASSAGE_U1; u += 6)
        s.flat({u - 0.05f, u + 0.05f, PASSAGE_V0, PASSAGE_V1}, F + 0.012f, FLOOR_STONE * 0.8f, EX_PLAIN, true);

    // gaten in plafond/perrons per eiland
    std::vector<Rect> holes;
    for (const Island& is : ISLANDS) {
        float c = (is.v0 + is.v1) / 2;
        holes.push_back({STAIR_HOLE_U0, STAIR_U1, c - STAIR_OFF - STAIR_W / 2, c - STAIR_OFF + STAIR_W / 2});
        holes.push_back({STAIR_HOLE_U0, STAIR_U1, c + STAIR_OFF - STAIR_W / 2, c + STAIR_OFF + STAIR_W / 2});
        holes.push_back({ESC_HOLE_U0, ESC_U1, c - ESC_HALF, c + ESC_HALF});
        holes.push_back({LIFT_U0, LIFT_U1, c - 1.2f, c + 1.2f});
    }
    // lichtgevend folieplafond: lichte vlakken met donkere naden
    for (const Rect& r : subtract(pass, holes)) {
        s.flat(r, C - 0.02f, CEILING_LIGHT * 0.95f, EX_LIGHT, false);
    }
    for (float v = PASSAGE_V0 + 4; v < PASSAGE_V1; v += 4)
        for (const Rect& r : subtract({PASSAGE_U0, PASSAGE_U1, v - 0.04f, v + 0.04f}, holes)) s.flat(r, C - 0.03f, Vec3(0.5f, 0.5f, 0.5f), EX_PLAIN, false);

    // spoordek tussen de eilanden (boven het plafond)
    std::vector<Rect> deck;
    {
        float vs[] = {PASSAGE_V0, ISLANDS[0].v0, ISLANDS[0].v1, ISLANDS[1].v0, ISLANDS[1].v1, ISLANDS[2].v0, ISLANDS[2].v1, PASSAGE_V1};
        for (int k = 0; k < 8; k += 2) s.box(PASSAGE_U0, PASSAGE_U1, vs[k], vs[k + 1], C, DECK, CONCRETE);
    }

    // zijwanden passage met openingen voor winkels
    struct WallGap { float v0, v1; };
    std::vector<WallGap> westGaps{{-27, -20}, {36, 42}};
    std::vector<WallGap> eastGaps{{-50, -43}, {-28, -21}, {-7, 0}, {14, 20}, {36, 41}, {41, 46.5f}};
    auto sideWall = [&](float u, float dir, std::vector<WallGap> gaps) {
        std::sort(gaps.begin(), gaps.end(), [](const WallGap& a, const WallGap& b2) { return a.v0 < b2.v0; });
        float v = -56.0f;
        gaps.push_back({PASSAGE_V1 - 1.5f, PASSAGE_V1 + 10});
        for (const WallGap& g : gaps) {
            if (g.v0 > v) {
                s.box(u, u + dir * 0.3f, v, g.v0, F, C, WALL_PANEL, EX_PLAIN, true);
                s.box(u + dir * 0.0f, u + dir * 0.02f, v, g.v0, F, F + 0.25f, WALL_DARK);  // plint
            }
            v = std::max(v, g.v1);
        }
        // boven het plafond tot het spoordek (buitenkant van het viaduct)
        s.box(u, u + dir * 0.6f, PASSAGE_V0, PASSAGE_V1, C, DECK + 0.4f, CONCRETE);
    };
    auto fixU = [](float a, float b2) { return std::make_pair(std::min(a, b2), std::max(a, b2)); };
    (void)fixU;
    // westwand: van u0 naar buiten (-u), oostwand van u1 naar buiten (+u)
    {
        auto mk = [&](float u, float dir, std::vector<WallGap> gaps) {
            std::sort(gaps.begin(), gaps.end(), [](const WallGap& a, const WallGap& b2) { return a.v0 < b2.v0; });
            float v = -56.0f;
            gaps.push_back({PASSAGE_V1 - 1.5f, PASSAGE_V1 + 10});
            for (const WallGap& g : gaps) {
                if (g.v0 > v) {
                    float ua = std::min(u, u + dir * 0.3f), ub = std::max(u, u + dir * 0.3f);
                    s.box(ua, ub, v, g.v0, F, C, WALL_PANEL, EX_PLAIN, true);
                    float pa = std::min(u - dir * 0.02f, u), pb = std::max(u - dir * 0.02f, u);
                    s.box(pa, pb, v, g.v0, F, F + 0.25f, WALL_DARK);
                }
                v = std::max(v, g.v1);
            }
            float ua = std::min(u, u + dir * 0.6f), ub = std::max(u, u + dir * 0.6f);
            s.box(ua, ub, PASSAGE_V0, PASSAGE_V1, C, DECK + 0.4f, CONCRETE);
        };
        mk(PASSAGE_U0, -1, westGaps);
        mk(PASSAGE_U1, 1, eastGaps);
    }
    (void)sideWall;

    // kolommen in de spoorzones tussen de eilanden
    for (float v : {-9.5f, 10.5f, -33.0f, -45.0f, 35.0f})
        for (float u : {-11.0f, -2.0f, 7.0f, 16.0f}) {
            Vec3 c = s.P(u, v, F);
            s.op.tube(c, c + Vec3(0, C - F, 0), 0.4f, 16, WALL_PANEL, EX_PLAIN);
            s.addBoxCollision(u - 0.4f, u + 0.4f, v - 0.4f, v + 0.4f, F, C);
        }

    // ================= TRAPPEN, ROLTRAPPEN, LIFTEN =================
    const int STEPS = 31;
    for (int k = 0; k < 3; k++) {
        const Island& is = ISLANDS[k];
        float c = (is.v0 + is.v1) / 2;
        std::string label = "Spoor " + std::to_string(is.trackS) + " | " + std::to_string(is.trackN);
        // -- trappen --
        for (float vs : {c - STAIR_OFF, c + STAIR_OFF}) {
            float va = vs - STAIR_W / 2, vb = vs + STAIR_W / 2;
            float run = (STAIR_U1 - STAIR_U0) / STEPS, rise = (P - F) / STEPS;
            for (int st = 0; st < STEPS; st++) {
                float u0 = STAIR_U0 + st * run;
                s.box(u0, u0 + run + 0.01f, va, vb, F + st * rise, F + (st + 1) * rise, st % 2 ? STAIR_STEP : STAIR_STEP * 0.92f);
                s.box(u0 + run - 0.06f, u0 + run, va, vb, F + (st + 1) * rise, F + (st + 1) * rise + 0.005f, NS_YELLOW * 0.9f);  // trapneus
            }
            Vec3 cc = s.P((STAIR_U0 + STAIR_U1) / 2, vs, 0);
            col.addFloor({cc.x, cc.z, s.U.x, s.U.z, (STAIR_U1 - STAIR_U0) / 2, STAIR_W / 2, F, P, 0});
            // glazen balustrades + leuning, schuin langs de trap (als reeks blokken)
            for (float side : {va - 0.06f, vb + 0.06f}) {
                for (int seg = 0; seg < 8; seg++) {
                    float ua = STAIR_U0 + (STAIR_U1 - STAIR_U0) * seg / 8, ub = STAIR_U0 + (STAIR_U1 - STAIR_U0) * (seg + 1) / 8;
                    float ya = F + (P - F) * seg / 8, yb = F + (P - F) * (seg + 1) / 8;
                    s.glassBox(ua, ub, side - 0.03f, side + 0.03f, std::min(ya, yb), std::max(ya, yb) + 1.0f, false);
                    s.addBoxCollision(ua, ub, side - 0.05f, side + 0.05f, F, std::max(ya, yb) + 1.0f);
                }
                Vec3 a = s.P(STAIR_U0, side, F + 1.0f), b2 = s.P(STAIR_U1, side, P + 1.0f);
                s.op.tube(a, b2, 0.03f, 6, STEEL_GREY, EX_PLAIN);
            }
            // dicht onder de trap (berging)
            s.box(-6.7f, STAIR_U1, va, vb, F, F + 2.0f, WALL_PANEL, EX_PLAIN, true);
        }
        // -- roltrappen (links omhoog, rechts omlaag): echte roltrappen met bewegende treden (escalator.cpp) --
        for (int dir = -1; dir <= 1; dir += 2) {
            float ve = c + dir * 0.86f;
            float L = Escalators::length(P - F);
            EscalatorDef d;
            d.base = s.P(ESC_U1 - L, ve, F);
            d.heading = std::atan2(s.U.x, s.U.z);
            d.rise = P - F;
            d.up = dir < 0;
            escalators_.add(d, s.op, s.gl, col);
        }
        // -- lift (glazen schacht) --
        s.glassBox(LIFT_U0, LIFT_U1, c - 1.2f, c + 1.2f, F, P + 2.6f, true);
        s.box(LIFT_U0 - 0.05f, LIFT_U1 + 0.05f, c - 1.25f, c + 1.25f, P + 2.6f, P + 2.75f, STEEL_GREY);
        s.box(LIFT_U0 + 0.1f, LIFT_U1 - 0.1f, c - 1.0f, c + 1.0f, F + 0.6f, F + 0.62f, STEEL_GREY);  // liftkooi vloer
        Elevator el;
        el.bottom = s.P(LIFT_U1 + 0.9f, c, F);
        el.top = s.P(LIFT_U1 + 0.9f, c, P);
        el.label = label;
        elevators.push_back(el);

        // -- bewegwijzering in de passage (hangend, dubbelzijdig, leesbaar vanuit noord en zuid) --
        for (float vsg : {is.v0 - 0.8f, is.v1 + 0.8f}) {
            float sy = C - 0.6f;
            SignRect sr = atlas.add(label, NS_BLUE, Vec3(1, 1, 1), 72);
            SignAtlas::quad(s.op, sr, s.P(-4.0f, vsg, sy), s.U, 0.55f, true);
            Vec3 a = s.P(-4.0f - 0.55f * sr.aspect / 2 + 0.2f, vsg, sy + 0.27f), b2 = s.P(-4.0f + 0.55f * sr.aspect / 2 - 0.2f, vsg, sy + 0.27f);
            s.op.tube(a, a + Vec3(0, C - sy - 0.27f, 0), 0.015f, 4, STEEL_GREY, EX_PLAIN);
            s.op.tube(b2, b2 + Vec3(0, C - sy - 0.27f, 0), 0.015f, 4, STEEL_GREY, EX_PLAIN);
        }
    }

    // ================= PERRONS =================
    for (int k = 0; k < 3; k++) {
        const Island& is = ISLANDS[k];
        float c = (is.v0 + is.v1) / 2;
        Rect top{is.u0, is.u1, is.v0, is.v1};
        std::vector<Rect> myHoles;
        for (const Rect& h : holes)
            if (h.v0 >= is.v0 - 0.01f && h.v1 <= is.v1 + 0.01f) myHoles.push_back(h);
        for (const Rect& r : subtract(top, myHoles)) {
            s.flat(r, P, PLATFORM_TOP, EX_PLAIN, true);
            s.floorCollision(r, P);
        }
        // witte randstreep en geleidelijn
        for (float e : {is.v0, is.v1}) {
            float in = e == is.v0 ? 1.0f : -1.0f;
            float a = std::min(e + in * 0.05f, e + in * 0.15f), b2 = std::max(e + in * 0.05f, e + in * 0.15f);
            s.flat({is.u0, is.u1, a, b2}, P + 0.006f, WHITE_LINE, EX_PLAIN, true);
            float ga = std::min(e + in * 0.8f, e + in * 1.1f), gb = std::max(e + in * 0.8f, e + in * 1.1f);
            s.flat({is.u0 + 2, is.u1 - 2, ga, gb}, P + 0.004f, Vec3(0.45f, 0.45f, 0.45f), EX_PLAIN, true);
        }
        // zijkanten: boven de passage tot het plafond, elders tot onder het ballastbed
        s.box(is.u0, is.u1, is.v0 - 0.02f, is.v0, R - 0.6f, P, CONCRETE);
        s.box(is.u0, is.u1, is.v1, is.v1 + 0.02f, R - 0.6f, P, CONCRETE);
        s.box(is.u0 - 0.02f, is.u0, is.v0, is.v1, R - 0.6f, P, CONCRETE);
        s.box(is.u1, is.u1 + 0.02f, is.v0, is.v1, R - 0.6f, P, CONCRETE);
        // onderkant boven de passage + zijwanden van de gaten
        s.box(PASSAGE_U0, PASSAGE_U1, is.v0, is.v0 + 0.02f, C, P, CONCRETE);
        s.box(PASSAGE_U0, PASSAGE_U1, is.v1 - 0.02f, is.v1, C, P, CONCRETE);
        for (const Rect& h : myHoles) {
            s.box(h.u0 - 0.05f, h.u0, h.v0, h.v1, C, P, WALL_PANEL);
            s.box(h.u0, h.u1, h.v0 - 0.05f, h.v0, C, P, WALL_PANEL);
            s.box(h.u0, h.u1, h.v1, h.v1 + 0.05f, C, P, WALL_PANEL);
            // balustrade op het perron rond het gat (open aan de bovenkant van trap/roltrap: +u)
            s.glassBox(h.u0 - 0.06f, h.u0, h.v0, h.v1, P, P + 1.1f);
            s.glassBox(h.u0, h.u1, h.v0 - 0.06f, h.v0, P, P + 1.1f);
            s.glassBox(h.u0, h.u1, h.v1, h.v1 + 0.06f, P, P + 1.1f);
            s.box(h.u0 - 0.06f, h.u1, h.v0 - 0.08f, h.v0 + 0.02f, P + 1.08f, P + 1.13f, STEEL_GREY);
            s.box(h.u0 - 0.06f, h.u1, h.v1 - 0.02f, h.v1 + 0.08f, P + 1.08f, P + 1.13f, STEEL_GREY);
        }
        // overkapping (behalve onder het grote stationsdak)
        const float CH = P + 4.2f;
        float hw = (is.v1 - is.v0) / 2 - 0.4f;
        for (auto seg : {std::make_pair(is.u0 + 6.0f, -6.0f), std::make_pair(31.0f, is.u1 - 40.0f)}) {
            if (seg.second <= seg.first) continue;
            s.box(seg.first, seg.second, c - hw, c + hw, CH, CH + 0.35f, Vec3(0.35f, 0.36f, 0.38f));
            s.flat({seg.first, seg.second, c - hw, c + hw}, CH - 0.005f, Vec3(0.80f, 0.80f, 0.78f), EX_PLAIN, false);
            s.lightBox(seg.first + 1, seg.second - 1, c - hw + 0.6f, c - hw + 0.8f, CH - 0.06f, CH - 0.01f);
            s.lightBox(seg.first + 1, seg.second - 1, c + hw - 0.8f, c + hw - 0.6f, CH - 0.06f, CH - 0.01f);
            for (float u = seg.first + 4; u < seg.second - 2; u += 16.0f) {
                Vec3 base = s.P(u, c, P);
                s.op.tube(base, base + Vec3(0, CH - P, 0), 0.18f, 10, Vec3(0.30f, 0.32f, 0.34f), EX_PLAIN);
                s.addBoxCollision(u - 0.2f, u + 0.2f, c - 0.2f, c + 0.2f, P, CH);
            }
        }
        // banken, stationsnaam- en spoornummerborden
        for (float u = is.u0 + 20; u < is.u1 - 20; u += 40) {
            if (u > -20 && u < 20) continue;  // niet bij de trappen
            s.box(u - 1.0f, u + 1.0f, c - 0.25f, c + 0.25f, P + 0.42f, P + 0.47f, WOOD, EX_PLAIN, true);
            s.box(u - 1.0f, u + 1.0f, c - 0.03f, c + 0.03f, P + 0.47f, P + 0.9f, WOOD);
            s.box(u - 0.9f, u - 0.8f, c - 0.2f, c + 0.2f, P, P + 0.42f, STEEL_GREY);
            s.box(u + 0.8f, u + 0.9f, c - 0.2f, c + 0.2f, P, P + 0.42f, STEEL_GREY);
            bool covered = (u > is.u0 + 6 && u < -6) || (u > -5 && u < 30) || (u > 31 && u < is.u1 - 40);
            float sy = covered ? P + 3.4f : P + 2.6f;
            s.sign("Eindhoven Centraal", NS_BLUE, Vec3(1, 1, 1), u + 8, c, sy, s.U, 0.5f);
            if (!covered) {
                Vec3 base = s.P(u + 8, c, P);
                s.op.tube(base, base + Vec3(0, sy - P + 0.25f, 0), 0.05f, 6, STEEL_GREY, EX_PLAIN);
            } else {
                Vec3 base = s.P(u + 8, c, sy + 0.25f);
                s.op.tube(base, base + Vec3(0, P + 4.2f - sy - 0.25f, 0), 0.015f, 4, STEEL_GREY, EX_PLAIN);
            }
            // spoornummers aan beide randen
            for (int side = 0; side < 2; side++) {
                int tr = side == 0 ? is.trackS : is.trackN;
                float ve = side == 0 ? is.v0 + 1.6f : is.v1 - 1.6f;
                SignRect sr = atlas.add(std::to_string(tr), Vec3(1, 1, 1), NS_BLUE, 96, true, 0.5f);
                SignAtlas::quad(s.op, sr, s.P(u - 6, ve, covered ? P + 3.6f : P + 2.8f), s.V * (side == 0 ? -1.0f : 1.0f), 0.6f, true);
                if (!covered) {
                    Vec3 base = s.P(u - 6, ve, P);
                    s.op.tube(base, base + Vec3(0, 2.5f, 0), 0.04f, 6, STEEL_GREY, EX_PLAIN);
                }
            }
        }
        // spoorinformatie bij de trappen
        for (int side = 0; side < 2; side++) {
            int tr = side == 0 ? is.trackS : is.trackN;
            static const char* INFO[7] = {"", "   IC Heerlen", "   SPR Deurne  IC Venlo", "   SPR Weert  IC Maastricht", "   SPR Tilburg Universiteit",
                                          "   IC Alkmaar  IC Den Haag C", "   SPR 's-Hertogenbosch  IC Dordrecht"};
            std::string txt = "Spoor " + std::to_string(tr) + INFO[tr];
            s.sign(txt, NS_BLUE, Vec3(1, 1, 1), 2.5f, c + (side == 0 ? -2.2f : 2.2f), P + 2.7f, s.V * (side == 0 ? -1.0f : 1.0f) * -1.0f, 0.42f);
            Vec3 base = s.P(2.5f, c + (side == 0 ? -2.2f : 2.2f), P);
            s.op.tube(base, base + Vec3(0, 2.5f, 0), 0.04f, 6, STEEL_GREY, EX_PLAIN);
        }
        for (int side = 0; side < 2; side++) {
            int tr = side == 0 ? is.trackS : is.trackN;
            float ve = side == 0 ? is.v0 : is.v1;
            tracks.push_back({"Eindhoven Centraal", tr, 0, 0});
            stationToGame(40.0f, ve, tracks.back().x, tracks.back().z);
        }
    }

    // ================= GROOT STATIONSDAK (zonnepanelen) =================
    {
        const float RY = 28.6f;
        Rect roof{-5.0f, 29.5f, -33.6f, 35.6f};
        s.box(roof.u0, roof.u1, roof.v0, roof.v1, RY, RY + 0.4f, Vec3(0.30f, 0.31f, 0.33f));
        s.flat(roof, RY - 0.005f, Vec3(0.78f, 0.78f, 0.76f), EX_PLAIN, false);
        for (float v = roof.v0 + 2; v < roof.v1 - 1; v += 2.2f) s.box(roof.u0 + 1, roof.u1 - 1, v, v + 1.7f, RY + 0.4f, RY + 0.48f, Vec3(0.10f, 0.13f, 0.22f));
        for (float v = roof.v0 + 3; v < roof.v1; v += 6) s.lightBox(roof.u0 + 1, roof.u1 - 1, v, v + 0.3f, RY - 0.05f, RY - 0.01f);
        for (int k = 0; k < 3; k++) {
            float c = (ISLANDS[k].v0 + ISLANDS[k].v1) / 2;
            for (float u : {-3.0f, 18.0f, 27.5f}) {  // niet tussen de roltrappen (kop tot u 16,9)
                Vec3 base = s.P(u, c, P);
                s.op.tube(base, base + Vec3(0, RY - P, 0), 0.3f, 12, Vec3(0.85f, 0.85f, 0.85f), EX_PLAIN);
                s.addBoxCollision(u - 0.3f, u + 0.3f, c - 0.3f, c + 0.3f, P, RY);
            }
        }
    }

    // ================= WINKELS =================
    const Vec3 W(1, 1, 1);
    // passage, westzijde (achter de wand: u < -17)
    buildShop(s, "Coffee Fellows", {-23.0f, PASSAGE_U0, -27, -20}, 1, style(Vec3(0.22f, 0.13f, 0.08f), W), F);
    buildShop(s, "Julia's", {-23.0f, PASSAGE_U0, 36, 42}, 1, style(Vec3(0.80f, 0.10f, 0.15f), W), F);
    // passage, oostzijde (u > 22)
    buildShop(s, "Albert Heijn to go", {PASSAGE_U1, 28.5f, -50, -43}, 0, style(Vec3(0.0f, 0.44f, 0.78f), W), F);
    buildShop(s, "Sissy-Boy", {PASSAGE_U1, 28.0f, -28, -21}, 0, style(Vec3(0.95f, 0.95f, 0.93f), Vec3(0.1f, 0.1f, 0.1f)), F);
    buildShop(s, "Etos", {PASSAGE_U1, 28.0f, -7, 0}, 0, style(Vec3(0.95f, 0.95f, 0.95f), Vec3(0.0f, 0.35f, 0.65f)), F);
    buildShop(s, "Bruna", {PASSAGE_U1, 28.0f, 14, 20}, 0, style(Vec3(0.0f, 0.25f, 0.55f), Vec3(1.0f, 0.85f, 0.0f)), F);
    buildShop(s, "Smullers", {PASSAGE_U1, 28.0f, 36, 41}, 0, style(Vec3(0.95f, 0.75f, 0.0f), Vec3(0.75f, 0.0f, 0.0f)), F);
    buildShop(s, "Burger King", {PASSAGE_U1, 28.0f, 41, 46.5f}, 0, style(Vec3(0.96f, 0.55f, 0.10f), Vec3(0.45f, 0.12f, 0.04f)), F);
    // Noordhal
    buildShop(s, "Albert Heijn to go", {-72, -62, 56, 63}, 2, style(Vec3(0.0f, 0.44f, 0.78f), W), F);
    buildShop(s, "The Döner Company", {-37, -28, 58, 64}, 2, style(Vec3(0.10f, 0.10f, 0.10f), Vec3(1.0f, 0.8f, 0.2f)), F);
    buildShop(s, "Toiletten", {-25, -19, 59, 64}, 2, style(Vec3(0.30f, 0.32f, 0.35f), W), F);
    buildShop(s, "Dunkin'", {-46, -38, 47.2f, 52}, 3, style(Vec3(1.0f, 0.45f, 0.05f), Vec3(0.85f, 0.10f, 0.50f)), F);
    buildShop(s, "Starbucks", {-14, -5, 47.2f, 53}, 3, style(Vec3(0.0f, 0.38f, 0.24f), W), F);
    buildShop(s, "Enterprise", {14.5f, 22, 52, 58}, 0, style(Vec3(0.0f, 0.45f, 0.25f), W), F);
    // NS-servicebalie in de Zuidhal
    buildShop(s, "NS Service", {-12, -2, -80, -74}, 1, style(NS_YELLOW, NS_BLUE), F);

    // incheckpalen (geel) bij beide uiteinden van de passage
    for (float v : {-54.0f, 44.0f})
        for (float u = -14.0f; u < 21.0f; u += 2.5f) {
            if (u > -1.0f && u < 3.0f) continue;
            s.box(u - 0.08f, u + 0.08f, v - 0.08f, v + 0.08f, F, F + 1.15f, Vec3(0.20f, 0.20f, 0.22f));
            s.box(u - 0.12f, u + 0.12f, v - 0.1f, v + 0.1f, F + 1.15f, F + 1.35f, NS_YELLOW);
        }
    // kaartautomaten (geel/blauw) in de Zuidhal
    for (float u = 4.0f; u < 18.0f; u += 1.6f) {
        s.box(u - 0.5f, u + 0.5f, -73.0f, -72.4f, F, F + 1.8f, NS_YELLOW, EX_PLAIN, true);
        s.box(u - 0.35f, u + 0.35f, -73.04f, -73.0f, F + 0.9f, F + 1.5f, Vec3(0.05f, 0.1f, 0.2f));
    }
    // keermuren langs het spoortalud tussen Zuidhal en sporen
    s.box(PASSAGE_U1, 46.0f, -50.6f, -50.0f, F, DECK, CONCRETE, EX_PLAIN, true);
    s.box(-30.0f, PASSAGE_U0, -50.6f, -50.0f, F, DECK, CONCRETE, EX_PLAIN, true);
    s.sign("Treinen", NS_BLUE, W, 2.0f, PASSAGE_V0 + 6.0f, C - 0.5f, s.U, 0.55f);
    s.sign("Treinen  |  Bus", NS_BLUE, W, 2.0f, PASSAGE_V1 - 4.0f, C - 0.5f, s.U, 0.55f);

    // ================= HALLEN: glazen puien met deuren =================
    struct Ent { float u, v; };
    const Ent ents[] = {{10.0f, 65.9f}, {-10.0f, 65.85f}, {5.0f, -91.7f}, {20.0f, -93.3f}};
    for (const HallRing& h : world.halls) {
        // vloer
        {
            using P2 = std::array<float, 2>;
            std::vector<std::vector<P2>> poly(1);
            for (auto& q : h.pts) poly[0].push_back({q.first, q.second});
            std::vector<uint32_t> idx = mapbox::earcut<uint32_t>(poly);
            uint32_t base = (uint32_t)s.op.v.size();
            for (auto& q : h.pts) s.op.vert(Vec3(q.first, F + 0.004f, q.second), Vec3(0, 1, 0), FLOOR_STONE * 0.95f, EX_PLAIN);
            for (uint32_t k : idx) s.op.i.push_back(base + k);
        }
        float top = h.cut;
        for (size_t k = 0; k < h.pts.size(); k++) {
            Vec3 a(h.pts[k].first, F, h.pts[k].second), b2(h.pts[(k + 1) % h.pts.size()].first, F, h.pts[(k + 1) % h.pts.size()].second);
            Vec3 d = b2 - a;
            float len = std::hypot(d.x, d.z);
            if (len < 0.05f) continue;
            Vec3 dir = d * (1.0f / len);
            // deur op deze rand?
            float doorT = -1;
            for (const Ent& e : ents) {
                Vec3 ep = s.P(e.u, e.v, F);
                float t = (ep.x - a.x) * dir.x + (ep.z - a.z) * dir.z;
                float off = std::fabs(-(ep.x - a.x) * dir.z + (ep.z - a.z) * dir.x);
                if (t > 1.8f && t < len - 1.8f && off < 1.5f) doorT = t;
            }
            // in stukjes van max 0,5 m; delen binnen de passage worden overgeslagen
            int n = std::max(1, (int)std::ceil(len / 0.5f));
            int runStart = -1;
            auto flush = [&](int i0, int i1) {
                float t0 = len * i0 / n, t1 = len * i1 / n;
                std::vector<std::pair<float, float>> parts;
                if (doorT > 0 && doorT - 1.6f < t1 && doorT + 1.6f > t0) {
                    if (doorT - 1.6f > t0) parts.push_back({t0, doorT - 1.6f});
                    if (doorT + 1.6f < t1) parts.push_back({doorT + 1.6f, t1});
                } else {
                    parts.push_back({t0, t1});
                }
                for (auto& pr : parts) {
                    Vec3 pa = a + dir * pr.first, pb = a + dir * pr.second;
                    Vec3 nrm(-dir.z, 0, dir.x);
                    float seglen = pr.second - pr.first;
                    // glas
                    s.gl.quad(pa, pb, pb + Vec3(0, top - F, 0), pa + Vec3(0, top - F, 0), nrm, GLASS_TINT, EX_GLASS);
                    // stijlen en regels
                    int m = std::max(1, (int)std::round(seglen / 1.5f));
                    for (int j = 0; j <= m; j++) {
                        Vec3 p = pa + dir * (seglen * j / m);
                        s.op.orientedBox(p + Vec3(0, (top - F) / 2, 0), {0.08f, top - F, 0.12f}, std::atan2(dir.x, dir.z) + PI / 2, WALL_DARK, EX_PLAIN);
                    }
                    Vec3 mid = (pa + pb) * 0.5f;
                    float hd = std::atan2(dir.x, dir.z);
                    s.op.orientedBox(mid + Vec3(0, 2.6f, 0), {0.12f, 0.1f, seglen}, hd, WALL_DARK, EX_PLAIN);
                    s.op.orientedBox(mid + Vec3(0, 0.05f, 0), {0.14f, 0.1f, seglen}, hd, WALL_DARK, EX_PLAIN);
                    col.addBox({mid.x, mid.z, dir.x, dir.z, seglen / 2, 0.1f, F, top});
                }
            };
            for (int j = 0; j < n; j++) {
                Vec3 p = a + dir * (len * (j + 0.5f) / n);
                float u, v;
                gameToStation(p.x, p.z, u, v);
                bool skip = inPassage(u, v, 0.5f);
                if (!skip && runStart < 0) runStart = j;
                if ((skip || j == n - 1) && runStart >= 0) {
                    flush(runStart, skip ? j : j + 1);
                    runStart = -1;
                }
            }
            if (doorT > 0) {
                StationDoor dr;
                dr.center = a + dir * doorT;
                dr.ax = dir.x; dr.az = dir.z;
                dr.width = 3.2f;
                dr.height = 2.6f;
                float hd = std::atan2(dir.x, dir.z);
                MeshBuilder L, Rm;
                L.orientedBox(dr.center - dir * 0.8f + Vec3(0, 1.3f, 0), {0.04f, 2.55f, 1.6f}, hd, GLASS_TINT, EX_GLASS);
                Rm.orientedBox(dr.center + dir * 0.8f + Vec3(0, 1.3f, 0), {0.04f, 2.55f, 1.6f}, hd, GLASS_TINT, EX_GLASS);
                dr.left = L.upload();
                dr.right = Rm.upload();
                // bovenlicht + deurkozijn
                s.op.orientedBox(dr.center + Vec3(0, 2.65f, 0), {0.15f, 0.12f, 3.3f}, hd, WALL_DARK, EX_PLAIN);
                s.gl.quad(dr.center - dir * 1.6f + Vec3(0, 2.7f, 0), dr.center + dir * 1.6f + Vec3(0, 2.7f, 0),
                          dr.center + dir * 1.6f + Vec3(0, top - F, 0), dr.center - dir * 1.6f + Vec3(0, top - F, 0), Vec3(-dir.z, 0, dir.x), GLASS_TINT, EX_GLASS);
                doors_.push_back(dr);
            }
        }
        // plafondverlichting in de hal
        float minu = 1e9f, maxu = -1e9f, minv = 1e9f, maxv = -1e9f;
        for (auto& q : h.pts) {
            float u, v;
            gameToStation(q.first, q.second, u, v);
            minu = std::min(minu, u); maxu = std::max(maxu, u); minv = std::min(minv, v); maxv = std::max(maxv, v);
        }
        for (float u = minu + 3; u < maxu - 2; u += 5)
            for (float v = minv + 3; v < maxv - 2; v += 5) {
                Vec3 p = s.P(u, v, top - 0.05f);
                bool inside = false;
                for (size_t k = 0, j = h.pts.size() - 1; k < h.pts.size(); j = k++) {
                    const auto& a = h.pts[k];
                    const auto& b2 = h.pts[j];
                    if ((a.second > p.z) != (b2.second > p.z) && p.x < (b2.first - a.first) * (p.z - a.second) / (b2.second - a.second) + a.first) inside = !inside;
                }
                if (inside) s.lightBox(u - 0.6f, u + 0.6f, v - 0.6f, v + 0.6f, top - 0.06f, top - 0.02f);
            }
    }
    // naam op de gevels
    s.sign("Eindhoven Centraal", NS_BLUE, W, 0.0f, 66.4f, STATION_FLOOR + STATION_NORTH_HALL_H + 1.0f, s.U * -1.0f, 1.4f);
    s.sign("Eindhoven Centraal", NS_BLUE, W, 12.0f, -93.5f, STATION_FLOOR + STATION_SOUTH_HALL_H + 1.2f, s.U, 1.6f);

    // ================= ANDERE STATIONS: perrons uit OSM =================
    // Strijp-S: perronhoogte en trapgaten
    SB ss(col, atlas, 1);
    float pss = 0;
    {
        float sum = 0;
        int n = 0;
        for (const OsmPlatform& p : world.platforms) {
            float cx = 0, cz = 0;
            for (auto& q : p.pts) { cx += q.first; cz += q.second; }
            cx /= p.pts.size(); cz /= p.pts.size();
            float u, v;
            strijpsLocal(rdX(cx), rdY(cz), u, v);
            if (std::fabs(u) < 250 && std::fabs(v) < 40) { sum += p.height; n++; }
        }
        pss = n ? sum / n : 25.0f;
    }
    std::vector<SSStair> ssStairs = strijpsStairs(pss);
    std::set<std::string> overviewDone;  // stations die al een vertrekstaat hebben
    for (const OsmPlatform& p : world.platforms) {
        using P2 = std::array<float, 2>;
        std::vector<std::vector<P2>> poly(1);
        std::vector<std::pair<float, float>> flatPts = p.pts;
        for (auto& q : p.pts) poly[0].push_back({q.first, q.second});
        // trapgaten (Strijp-S) uitsparen
        float pumax = -1e9f, pvmin = 1e9f, pvmax = -1e9f;
        for (auto& q : p.pts) {
            float u, v;
            strijpsLocal(rdX(q.first), rdY(q.second), u, v);
            pumax = std::max(pumax, u);
            pvmin = std::min(pvmin, v);
            pvmax = std::max(pvmax, v);
        }
        for (const SSStair& st : ssStairs) {
            Vec3 mid = ss.P((st.top + SS_TUN_U0) / 2, st.vc, 0);
            Vec3 topPt = ss.P(st.top + 0.5f, st.vc, 0);
            if (!pointInPoly(p.pts, topPt.x, topPt.z) && !pointInPoly(p.pts, mid.x, mid.z)) continue;
            float u1 = std::min(SS_TUN_U0, pumax - 0.25f);
            float va = std::max(st.vc - SS_STAIR_W / 2 - 0.15f, pvmin + 0.03f), vb = std::min(st.vc + SS_STAIR_W / 2 + 0.15f, pvmax - 0.03f);
            if (vb - va < 0.2f) continue;
            Vec3 c0 = ss.P(st.top, va, 0), c1 = ss.P(u1, va, 0), c2 = ss.P(u1, vb, 0), c3 = ss.P(st.top, vb, 0);
            poly.push_back({{c0.x, c0.z}, {c1.x, c1.z}, {c2.x, c2.z}, {c3.x, c3.z}});
            for (Vec3 q : {c0, c1, c2, c3}) flatPts.push_back({q.x, q.z});
            Vec3 cc = ss.P((st.top + u1) / 2, st.vc, 0);
            col.addCutout({cc.x, cc.z, ss.U.x, ss.U.z, (u1 - st.top) / 2, (vb - va) / 2, 0, 0});
        }
        std::vector<uint32_t> idx = mapbox::earcut<uint32_t>(poly);
        uint32_t base = (uint32_t)s.op.v.size();
        Vec3 topCol = PLATFORM_TOP;
        for (const MapPoint& mp : world.points)
            if (mp.kind == POINT_STATION && mp.name == "Best" && std::hypot(mp.x - p.pts[0].first, mp.z - p.pts[0].second) < 350)
                topCol = Vec3(0.55f, 0.30f, 0.24f);  // rode klinkers (Best)
        const float py = p.height + platOffset(world, p);
        for (auto& q : flatPts) s.op.vert(Vec3(q.first, py, q.second), Vec3(0, 1, 0), topCol, EX_PLAIN);
        for (uint32_t k : idx) s.op.i.push_back(base + k);
        for (size_t k = 0; k < p.pts.size(); k++) {
            auto a = p.pts[k], b2 = p.pts[(k + 1) % p.pts.size()];
            Vec3 A(a.first, py, a.second), B(b2.first, py, b2.second);
            Vec3 d = B - A;
            Vec3 n = normalize(Vec3(-d.z, 0, d.x));
            {
                // binnenrand tussen twee aansluitende perronvlakken (bijv. spoor 2 en 3): geen zijwand
                Vec3 m = (A + B) * 0.5f;
                bool shared = false;
                for (const OsmPlatform& o : world.platforms)
                    if (&o != &p && (pointInPoly(o.pts, m.x + n.x * 0.5f, m.z + n.z * 0.5f) || pointInPoly(o.pts, m.x - n.x * 0.5f, m.z - n.z * 0.5f)))
                        shared = true;
                if (shared) continue;
            }
            // in stukjes, niet boven een trapgat (Strijp-S)
            int pieces = std::max(1, (int)std::ceil(length(d) / 0.5f));
            for (int pc = 0; pc < pieces; pc++) {
                Vec3 pa = A + d * ((float)pc / pieces), pb = A + d * ((float)(pc + 1) / pieces);
                Vec3 m = (pa + pb) * 0.5f;
                float mu, mv;
                strijpsLocal(rdX(m.x), rdY(m.z), mu, mv);
                bool overStair = false;
                for (const SSStair& st : ssStairs)
                    if (mu > st.top - 0.3f && mu < SS_TUN_U0 + 0.5f && std::fabs(mv - st.vc) < SS_STAIR_W / 2 + 0.35f) overStair = true;
                {
                    float zu, zv;  // Zwolle: de trappenhuizen hebben eigen wanden
                    Zwolle::local(rdX(m.x), rdY(m.z), zu, zv);
                    if (Zwolle::inOpening(zu, zv) || Zwolle::inOpening(zu + 0.4f, zv) || Zwolle::inOpening(zu - 0.4f, zv) ||
                        Zwolle::inOpening(zu, zv + 0.4f) || Zwolle::inOpening(zu, zv - 0.4f))
                        overStair = true;
                }
                if (!overStair) s.op.quad(pa - Vec3(0, 1.4f, 0), pb - Vec3(0, 1.4f, 0), pb, pa, n, CONCRETE, EX_PLAIN);
            }
        }
        col.addPolyFloor(p.pts, py);
        // naam van het dichtstbijzijnde station + spoornummer
        float cx = 0, cz = 0;
        for (auto& q : p.pts) { cx += q.first; cz += q.second; }
        cx /= p.pts.size(); cz /= p.pts.size();
        std::string st;
        float best = 1e9f;
        for (const MapPoint& mp : world.points)
            if (mp.kind == POINT_STATION) {
                float d = std::hypot(mp.x - cx, mp.z - cz);
                if (d < best) { best = d; st = mp.name; }
            }
        if (st.empty() || best > 400) continue;
        // hoofdas van het perron
        float sxx = 0, szz = 0, sxz = 0;
        for (auto& q : p.pts) { float dx = q.first - cx, dz = q.second - cz; sxx += dx * dx; szz += dz * dz; sxz += dx * dz; }
        float ang = 0.5f * std::atan2(2 * sxz, sxx - szz);
        Vec3 ax(std::cos(ang), 0, std::sin(ang));
        for (float t : {-30.0f, 0.0f, 30.0f}) {
            Vec3 base(cx + ax.x * t, p.height, cz + ax.z * t);
            SignRect sr = atlas.add(st, NS_BLUE, W, 72);
            SignAtlas::quad(s.op, sr, base + Vec3(0, 2.6f, 0), ax, 0.5f, true);
            s.op.tube(base, base + Vec3(0, 2.4f, 0), 0.05f, 6, STEEL_GREY, EX_PLAIN);
        }
        int trackNo = 0;
        {
            const char* nm = p.name.c_str();
            while (*nm && (*nm < '0' || *nm > '9')) nm++;
            trackNo = atoi(nm);
        }
        if (trackNo > 0) {
            // elektronisch perronbord (PIDS) op twee palen, dwars boven het perron; naam "3/4": voorkant 3, achterkant 4
            {
                int second = 0;
                const char* nm = p.name.c_str();
                while (*nm && (*nm < '0' || *nm > '9')) nm++;
                while (*nm >= '0' && *nm <= '9') nm++;
                while (*nm && (*nm < '0' || *nm > '9')) nm++;
                if (*nm) second = atoi(nm);
                auto addBoard = [&](float t, float w, float h, float yc, bool overview, int a, int b) {
                    BoardSpot bs;
                    bs.station = st;
                    bs.trackA = a;
                    bs.trackB = b;
                    bs.normal = ax;
                    bs.across = Vec3(-ax.z, 0, ax.x);
                    bs.c = Vec3(cx + ax.x * t, p.height + yc, cz + ax.z * t);
                    bs.w = w;
                    bs.h = h;
                    bs.overview = overview;
                    boards.push_back(bs);
                    float hd = std::atan2(ax.x, ax.z);
                    const Vec3 HOUSING(0.12f, 0.12f, 0.13f);
                    s.op.orientedBox(bs.c, {w + 0.12f, h + 0.12f, 0.12f}, hd, HOUSING, EX_PLAIN);
                    s.op.orientedBox(bs.c + Vec3(0, h / 2 + 0.09f, 0), {w + 0.16f, 0.05f, 0.16f}, hd, HOUSING * 1.3f, EX_PLAIN);  // kapje
                    for (int e = -1; e <= 1; e += 2) {
                        Vec3 foot = bs.c + bs.across * (e * (w / 2 - 0.15f));
                        foot.y = p.height;
                        s.op.tube(foot, Vec3(foot.x, bs.c.y - h / 2, foot.z), 0.045f, 8, STEEL_GREY, EX_PLAIN);
                    }
                };
                addBoard(-12.0f, 1.8f, 0.56f, 2.75f, false, trackNo, second);
                if (!overviewDone.count(st)) {  // een vertrekstaat per station, op het eerste perron met een spoornummer
                    overviewDone.insert(st);
                    addBoard(6.0f, 2.3f, 1.15f, 2.55f, true, 0, 0);
                }
            }
            tracks.push_back({st, trackNo, cx, cz});
            Vec3 base(cx + ax.x * 15, p.height, cz + ax.z * 15);
            SignRect sr = atlas.add(std::to_string(trackNo), W, NS_BLUE, 96, true, 0.5f);
            SignAtlas::quad(s.op, sr, base + Vec3(0, 2.8f, 0), ax, 0.6f, true);
            s.op.tube(base, base + Vec3(0, 2.5f, 0), 0.04f, 6, STEEL_GREY, EX_PLAIN);
        }
        if (st != "Best") platformDetails(s, world, p, st, ax, cx, cz);
    }

    // ================= naar GPU =================
    // in vakken van 400 m opknippen (per driehoek, op het zwaartepunt), zodat alleen wat in beeld en dichtbij is
    // getekend wordt (alle stations langs de lijn zitten anders in een paar enorme meshes)
    auto split = [](MeshBuilder& mb, std::vector<Mesh>& out) {
        const float CELL = 400.0f;
        std::map<std::pair<int, int>, MeshBuilder> cells;
        std::map<std::pair<int, int>, std::unordered_map<uint32_t, uint32_t>> remap;
        for (size_t t = 0; t + 2 < mb.i.size(); t += 3) {
            const Vertex& a = mb.v[mb.i[t]];
            const Vertex& b2 = mb.v[mb.i[t + 1]];
            const Vertex& c = mb.v[mb.i[t + 2]];
            auto key = std::make_pair((int)std::floor((a.px + b2.px + c.px) / 3 / CELL), (int)std::floor((a.pz + b2.pz + c.pz) / 3 / CELL));
            MeshBuilder& dst = cells[key];
            auto& rm = remap[key];
            for (int k = 0; k < 3; k++) {
                uint32_t src = mb.i[t + k];
                auto it = rm.find(src);
                if (it == rm.end()) { it = rm.emplace(src, (uint32_t)dst.v.size()).first; dst.v.push_back(mb.v[src]); }
                dst.i.push_back(it->second);
            }
        }
        for (auto& kv : cells) out.push_back(kv.second.upload());
    };
    buildStrijpS(ss, world, pss);
    buildBestStation(world, col, atlas, s.op, s.gl, elevators, bestCenter, bestPlatformY, bestHallY);
    split(s.op, opaque_);
    {
        MeshBuilder uo, ug;  // Utrecht Centraal: eigen meshes (groot)
        buildUtrechtCentraal(world, col, atlas, uo, ug, elevators, utrechtHall, utrechtPlatform, utrechtHallY);
        if (!uo.v.empty()) split(uo, opaque_);
        if (!ug.v.empty()) split(ug, glass_);
    }
    {
        MeshBuilder zo, zg;  // Zwolle: tunnel, opgangen, kappen (zwollestation.cpp)
        buildZwolle(world, col, atlas, zo, zg, elevators, escalators_, boards, zwolleTunnel);
        if (!zo.v.empty()) split(zo, opaque_);
        if (!zg.v.empty()) split(zg, glass_);
    }
    escalators_.finish();
    split(s.gl, glass_);
    split(ss.op, opaque_);
    split(ss.gl, glass_);
    (void)TRACK_V;
}

std::vector<std::vector<std::pair<float, float>>> Stations::centraalPlatforms() const {
    std::vector<std::vector<std::pair<float, float>>> out;
    for (const Island& is : ISLANDS) {
        std::vector<std::pair<float, float>> r;
        for (auto uv : {std::make_pair(is.u0, is.v0), std::make_pair(is.u1, is.v0), std::make_pair(is.u1, is.v1), std::make_pair(is.u0, is.v1)}) {
            float x, z;
            stationToGame(uv.first, uv.second, x, z);
            r.push_back({x, z});
        }
        out.push_back(r);
    }
    return out;
}

bool Stations::nearCentraal(Vec3 p) const {
    float u, v;
    gameToStation(p.x, p.z, u, v);
    return u > -130 && u < 310 && v > -105 && v < 90;
}

void Stations::update(float dt, Vec3 player, Collision& col) {
    escalators_.update(dt);
    col.clearDynamic();
    for (StationDoor& d : doors_) {
        float dx = player.x - d.center.x, dz = player.z - d.center.z;
        bool want = dx * dx + dz * dz < 4.0f * 4.0f && std::fabs(player.y - d.center.y) < 3.0f;
        d.open = clampf(d.open + (want ? dt * 2.0f : -dt * 1.2f), 0, 1);
        // deurbladen als botsing zolang ze niet open zijn
        float shift = d.open * 1.5f;
        for (int side = -1; side <= 1; side += 2) {
            float off = side * (0.8f + shift);
            col.addDynamicBox({d.center.x + d.ax * off, d.center.z + d.az * off, d.ax, d.az, 0.8f, 0.1f, d.center.y, d.center.y + 2.6f});
        }
    }
}

void Stations::drawOpaque(World& world) const {
    world.setMode(MODE_OBJECT);
    world.setModel(Mat4::identity());
    Vec3 cam = world.camPos();
    for (const Mesh& m : opaque_)
        if (World::boxDistance(cam, m.bmin, m.bmax) < std::min(2500.0f, world.drawDistance + 1000.0f) && world.visible(m.bmin, m.bmax)) drawMesh(m);
    escalators_.draw(world);
}

void Stations::drawGlass(World& world) const {
    Vec3 cam = world.camPos();
    for (const Mesh& m : glass_)
        if (World::boxDistance(cam, m.bmin, m.bmax) < world.drawDistance && world.visible(m.bmin, m.bmax)) drawMesh(m);
    for (const StationDoor& d : doors_) {
        float shift = d.open * 1.5f;
        world.setModel(Mat4::translation(-d.ax * shift, 0, -d.az * shift));
        drawMesh(d.left);
        world.setModel(Mat4::translation(d.ax * shift, 0, d.az * shift));
        drawMesh(d.right);
    }
    world.setModel(Mat4::identity());
}
