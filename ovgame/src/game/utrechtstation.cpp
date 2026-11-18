// Station Utrecht Centraal: de OV-terminal (2016, Benthem Crouwel) - de stationshal over alle sporen heen.
// Gereconstrueerd uit OpenStreetMap (indoor-kartering) en foto's:
//  - hal (building=train_station): 91 x 247 m, vloer ~8 m boven het maaiveld (7,2 m boven de perrons), dak tot ~26 m
//  - per eilandperron (5/7, 8/9, 11/12, 14/15, 18/19, 20/21) twee roltrapparen (zuidrij en noordrij), twee vaste trappen
//    (zuid: naar buiten onder de perronkap; noord: twee bordessen) en een glazen lift midden in de hal
//  - uitgangen naar het Jaarbeursplein (west) en het Stationsplein/busstation Centrumzijde (oost) met roltrappen en trappen
//  - winkelpaviljoens tussen de zuidelijke roltrappen; winkels in de noordstrook met de Bistrot op de mezzanine
//  - lichte natuurstenen vloer, golvend wit dak met lichtstraten op slanke kolommen, glazen gevels rondom
//  - perronkappen ten zuiden en noorden van de hal (OSM-vormen), donker plafond met lichtlijnen onder de hal
// Lokaal assenstelsel (u, v): zie worldformat.h (UT_*).
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>
#include "../third_party/earcut.hpp"
#include "station.h"

using namespace WorldFormat;

namespace {
const Vec3 FLOOR_LIGHT(0.80f, 0.79f, 0.76f);   // natuursteen hal
const Vec3 FLOOR_BAND(0.62f, 0.61f, 0.59f);    // donkerder banen
const Vec3 CEIL_UNDER(0.26f, 0.27f, 0.29f);    // onderkant halvloer (boven de perrons)
const Vec3 EDGE(0.55f, 0.56f, 0.57f);
const Vec3 ROOF_UNDER(0.94f, 0.94f, 0.92f);    // wit dak (binnen)
const Vec3 ROOF_TOP(0.70f, 0.71f, 0.72f);
const Vec3 COLUMN_WHITE(0.92f, 0.92f, 0.90f);
const Vec3 STEEL(0.40f, 0.42f, 0.45f);
const Vec3 DARK_STEEL(0.18f, 0.19f, 0.21f);
const Vec3 ESC_SIDE(0.70f, 0.72f, 0.74f);
const Vec3 ESC_STEP(0.30f, 0.31f, 0.33f);
const Vec3 STAIR_STONE(0.66f, 0.65f, 0.62f);
const Vec3 GLASS_T(0.55f, 0.65f, 0.68f);
const Vec3 CONCRETE(0.62f, 0.62f, 0.60f);
const Vec3 CANOPY_UNDER(0.90f, 0.90f, 0.88f);
const Vec3 CANOPY_TOP(0.55f, 0.56f, 0.58f);
const Vec3 LIGHT(1.0f, 0.97f, 0.90f);
const Vec3 NSBLUE(0.0f, 0.13f, 0.43f);
const Vec3 NSYEL(1.0f, 0.78f, 0.0f);
const Vec3 WHITE(1, 1, 1);

// Banken: per eilandperron (of straatuitgang) de roltrappen, trappen en lift. u-waarden uit OSM (laag/hoog = perron/hal).
struct Bank {
    float v;                 // midden (dwars)
    float seLo, seHi;        // zuidelijke roltrappen: onderkant / bovenkant (u); gelijk = geen
    float neHi, neLo;        // noordelijke roltrappen: bovenkant / onderkant
    float ssLo, ssHi;        // zuidelijke vaste trap: onderkant (buiten de hal) / bovenkant (halrand)
    float nsHi, nsLo;        // noordelijke vaste trap (twee delen met bordes): boven / onder
    float liftU;             // lift (0 = geen)
    const char* tracks;      // "18 19" of "" (straat)
    const char* toLabel;     // waar de trap/lift naartoe gaat (beneden)
    const char* dests;       // bestemmingen (bord)
    bool street;             // beneden is het maaiveld (geen perron)
};
const Bank BANKS[] = {
    {-55.2f, -40.0f, -25.0f, 0.5f, 17.0f, -55.5f, -42.0f, 17.5f, 30.0f, -6.8f, "20  21", "perron 20/21", "Sprinter  Bodegraven  Den Haag C  's-Hertogenbosch  Tiel", false},
    {-35.6f, -40.0f, -25.0f, 0.5f, 16.5f, -55.4f, -42.0f, 17.8f, 29.9f, -6.6f, "18  19", "perron 18/19", "Intercity  Maastricht  Heerlen  Venlo", false},
    {-10.7f, -40.0f, -25.0f, 0.5f, 17.0f, -55.4f, -42.0f, 17.9f, 29.9f, -6.5f, "14  15", "perron 14/15", "Intercity  Arnhem  Nijmegen", false},
    {13.8f, -40.0f, -25.0f, 0.7f, 17.0f, -55.1f, -41.8f, 18.1f, 29.6f, -6.3f, "11  12", "perron 11/12", "Intercity  Amersfoort  Zwolle", false},
    {36.6f, -40.0f, -25.0f, 0.9f, 17.3f, -55.1f, -41.6f, 18.3f, 29.7f, -6.1f, "8  9", "perron 8/9", "Intercity  Rotterdam  Den Haag", false},
    {60.5f, -40.0f, -25.0f, 1.0f, 17.4f, -53.6f, -41.6f, 18.4f, 30.0f, -6.0f, "5  7", "perron 5/7", "Intercity  Alkmaar  Enkhuizen  Dordrecht", false},
    // straatuitgangen
    {-89.8f, -42.6f, -25.7f, -0.1f, 18.5f, -59.5f, -40.1f, 17.3f, 32.5f, -7.1f, "", "Jaarbeursplein", "", true},
    {-117.6f, -42.5f, -26.0f, 0.0f, 0.0f, -59.6f, -40.2f, 0.0f, 0.0f, -6.5f, "", "Jaarbeursplein", "", true},
    {116.0f, -33.4f, -23.8f, 1.9f, 10.2f, 0.0f, 0.0f, 19.2f, 30.7f, -5.2f, "", "Stationsplein / busstation", "", true},
};
// winkelpaviljoens in de zuidrij (u -34..-18) en winkels in de noordstrook (u 31.5..38): v-bereik, naam, kleuren
struct Shop { float u0, u1, v0, v1; const char* name; Vec3 bg, fg; };
const Shop SHOPS[] = {
    {-34.0f, -18.5f, 18.4f, 32.0f, "AH to go", Vec3(0.0f, 0.62f, 0.88f), WHITE},
    {-33.0f, -18.5f, -6.2f, 7.0f, "Kiosk", Vec3(0.98f, 0.80f, 0.10f), Vec3(0.0f, 0.13f, 0.43f)},
    {-33.3f, -18.9f, -31.6f, -18.0f, "Starbucks", Vec3(0.0f, 0.38f, 0.25f), WHITE},
    {-33.7f, -20.3f, -48.4f, -42.3f, "Julia's", Vec3(0.75f, 0.12f, 0.12f), WHITE},
    {-34.3f, -21.1f, -83.7f, -61.8f, "HEMA", Vec3(0.89f, 0.0f, 0.11f), WHITE},
    {-34.2f, -22.2f, -110.9f, -95.8f, "Burger King", Vec3(0.96f, 0.54f, 0.13f), Vec3(0.30f, 0.12f, 0.05f)},
    {31.5f, 38.0f, -84.0f, -59.0f, "Albert Heijn", Vec3(0.0f, 0.62f, 0.88f), WHITE},
    {31.5f, 38.0f, -51.0f, -39.5f, "Bruna", Vec3(0.0f, 0.30f, 0.60f), WHITE},
    {31.5f, 38.0f, -31.5f, -14.5f, "NS Service & Tickets", NSYEL, NSBLUE},
    {31.5f, 38.0f, -7.0f, 10.5f, "Rituals", Vec3(0.15f, 0.14f, 0.13f), Vec3(0.86f, 0.78f, 0.60f)},
    {31.5f, 38.0f, 17.0f, 33.5f, "Smullers", Vec3(0.85f, 0.15f, 0.10f), Vec3(1.0f, 0.90f, 0.20f)},
    {31.5f, 38.0f, 40.0f, 57.5f, "Etos", Vec3(0.55f, 0.25f, 0.60f), WHITE},
};
// perronkappen (OSM building=roof, layer 1) in (u, v)
struct CanopyPoly { int n; float p[16][2]; };
const CanopyPoly CANOPIES[] = {
    {8, {{-139.0f, 32.2f}, {-142.6f, 34.2f}, {-143.7f, 37.6f}, {-142.1f, 40.4f}, {-139.8f, 42.1f}, {-134.9f, 42.9f}, {-39.5f, 42.2f}, {-39.7f, 30.9f}}},
    {10, {{-126.6f, 9.0f}, {-136.0f, 8.8f}, {-140.8f, 10.0f}, {-143.1f, 11.3f}, {-144.3f, 13.6f}, {-143.3f, 16.5f}, {-140.9f, 19.3f}, {-137.7f, 19.9f}, {-39.7f, 19.1f}, {-39.5f, 8.6f}}},
    {12, {{-75.5f, -3.1f}, {-39.4f, -3.8f}, {-39.5f, -17.7f}, {-76.7f, -17.0f}, {-119.3f, -15.2f}, {-142.5f, -11.9f}, {-144.1f, -9.4f}, {-144.9f, -7.3f}, {-144.1f, -4.8f}, {-141.7f, -2.4f}, {-139.3f, -0.7f}, {-135.5f, -0.6f}}},
    {8, {{-39.8f, -41.5f}, {-174.7f, -38.6f}, {-180.6f, -36.0f}, {-181.2f, -33.5f}, {-180.9f, -30.8f}, {-178.8f, -27.8f}, {-175.2f, -25.7f}, {-39.7f, -26.2f}}},
    {10, {{-40.0f, -62.0f}, {-121.0f, -61.0f}, {-175.9f, -59.0f}, {-177.8f, -59.0f}, {-179.4f, -56.4f}, {-180.4f, -54.9f}, {-180.8f, -51.6f}, {-179.0f, -48.6f}, {-176.4f, -47.3f}, {-39.8f, -50.1f}}},
    {12, {{-299.1f, -25.8f}, {-297.3f, -25.2f}, {-211.5f, -26.2f}, {-209.9f, -27.2f}, {-208.5f, -28.7f}, {-208.1f, -32.0f}, {-209.3f, -35.2f}, {-211.8f, -36.4f}, {-298.9f, -32.2f}, {-300.3f, -31.2f}, {-300.9f, -29.7f}, {-300.7f, -27.1f}}},
    {12, {{145.6f, 88.4f}, {196.8f, 87.7f}, {230.6f, 86.1f}, {259.8f, 83.1f}, {262.5f, 84.4f}, {263.4f, 86.1f}, {263.5f, 87.7f}, {262.7f, 89.4f}, {261.7f, 90.7f}, {258.1f, 91.6f}, {224.7f, 94.4f}, {145.7f, 96.3f}}},
    {12, {{145.5f, 72.4f}, {175.1f, 71.7f}, {213.3f, 69.7f}, {260.2f, 66.2f}, {262.7f, 67.2f}, {263.7f, 69.2f}, {263.6f, 71.7f}, {262.2f, 74.0f}, {259.9f, 74.9f}, {232.7f, 77.7f}, {211.4f, 78.9f}, {145.5f, 79.9f}}},
    {11, {{51.2f, 50.2f}, {199.7f, 49.0f}, {202.1f, 50.0f}, {204.2f, 52.6f}, {204.6f, 54.5f}, {204.5f, 56.6f}, {203.1f, 59.1f}, {200.4f, 60.8f}, {196.4f, 61.1f}, {147.8f, 63.1f}, {51.2f, 64.5f}}},
    {9, {{51.0f, 30.7f}, {198.2f, 29.6f}, {201.2f, 29.8f}, {203.5f, 31.7f}, {204.4f, 33.6f}, {204.2f, 36.5f}, {202.1f, 39.6f}, {199.1f, 40.6f}, {51.1f, 41.5f}}},
    {10, {{51.0f, 7.9f}, {87.8f, 7.3f}, {146.5f, 8.8f}, {176.3f, 10.4f}, {198.9f, 10.9f}, {201.4f, 12.1f}, {201.6f, 15.4f}, {199.4f, 17.5f}, {196.6f, 17.6f}, {51.0f, 18.6f}}},
    {16, {{51.0f, -18.1f}, {68.2f, -18.2f}, {104.6f, -17.1f}, {124.0f, -15.9f}, {154.9f, -13.3f}, {198.1f, -7.5f}, {201.0f, -6.4f}, {201.7f, -3.4f}, {200.5f, -1.6f}, {199.0f, -0.7f}, {187.9f, -1.2f}, {174.1f, -2.4f}, {118.0f, -4.1f}, {96.9f, -4.2f}, {68.5f, -3.8f}, {51.0f, -4.1f}}},
    {11, {{50.9f, -40.6f}, {69.0f, -39.5f}, {78.6f, -38.4f}, {92.5f, -36.4f}, {94.3f, -34.7f}, {94.9f, -33.2f}, {95.0f, -31.3f}, {94.1f, -29.7f}, {92.2f, -27.9f}, {90.0f, -27.2f}, {50.9f, -27.7f}}},
    {12, {{50.8f, -60.3f}, {54.9f, -59.9f}, {75.1f, -57.4f}, {84.6f, -55.8f}, {91.2f, -54.4f}, {93.3f, -53.1f}, {94.0f, -51.6f}, {94.3f, -49.5f}, {92.9f, -46.9f}, {90.0f, -45.9f}, {66.2f, -48.5f}, {50.9f, -49.5f}}},
};

struct R { float u0, u1, v0, v1; };

struct UB {
    MeshBuilder& op;
    MeshBuilder& gl;
    Collision& col;
    SignAtlas& atlas;
    Vec3 U, V;   // game-richting van +u en +v
    float hU;    // heading van +u (orientedBox: lokale z langs +u)
    UB(MeshBuilder& o, MeshBuilder& g, Collision& c, SignAtlas& a) : op(o), gl(g), col(c), atlas(a) {
        Vec3 o0 = P(0, 0, 0);
        U = normalize(P(1, 0, 0) - o0);
        V = normalize(P(0, 1, 0) - o0);
        hU = std::atan2(U.x, U.z);
    }
    static Vec3 P(float u, float v, float y) {
        double rx, ry;
        utToRd(u, v, rx, ry);
        return Vec3(gameX(rx), y, gameZ(ry));
    }
    // blok in (u, v)
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
    // vlakke vloer (zichtbaar + beloopbaar)
    void floor(const R& r, float y, Vec3 c, bool walk = true) {
        op.quad(P(r.u0, r.v0, y), P(r.u0, r.v1, y), P(r.u1, r.v1, y), P(r.u1, r.v0, y), Vec3(0, 1, 0), c, EX_PLAIN);
        if (walk) {
            Vec3 m = P((r.u0 + r.u1) / 2, (r.v0 + r.v1) / 2, 0);
            col.addFloor({m.x, m.z, U.x, U.z, (r.u1 - r.u0) / 2, (r.v1 - r.v0) / 2, y, y, 0});
        }
    }
    void ceiling(const R& r, float y, Vec3 c) {
        op.quad(P(r.u0, r.v0, y), P(r.u1, r.v0, y), P(r.u1, r.v1, y), P(r.u0, r.v1, y), Vec3(0, -1, 0), c, EX_PLAIN);
    }
    // schuine vloer langs u: hoogte ya bij u0, yb bij u1; speed = roltrap (m/s langs +u)
    void slope(float u0, float u1, float v0, float v1, float ya, float yb, float speed) {
        Vec3 m = P((u0 + u1) / 2, (v0 + v1) / 2, 0);
        col.addFloor({m.x, m.z, U.x, U.z, (u1 - u0) / 2, (v1 - v0) / 2, ya, yb, speed});
    }
    // verticale wand langs u (v vast) of langs v (u vast)
    void wallU(float u0, float u1, float v, float y0, float y1, Vec3 c, float ex, bool facePlusV) {
        Vec3 n = facePlusV ? V : V * -1.0f;
        if (facePlusV) op.quad(P(u0, v, y0), P(u1, v, y0), P(u1, v, y1), P(u0, v, y1), n, c, ex);
        else op.quad(P(u1, v, y0), P(u0, v, y0), P(u0, v, y1), P(u1, v, y1), n, c, ex);
    }
    void sign(const std::string& text, Vec3 bg, Vec3 fg, Vec3 c, Vec3 right, float h, int px = 72) {
        SignRect r = atlas.add(text, bg, fg, px);
        SignAtlas::quad(op, r, c, right, h, true);
    }
};

// rechthoek r min een lijst gaten (alles asgericht in u, v): opdelen in strips
std::vector<R> subtract(const R& outer, const std::vector<R>& holes) {
    std::vector<float> us{outer.u0, outer.u1}, vs{outer.v0, outer.v1};
    for (const R& h : holes) {
        if (h.u1 <= outer.u0 || h.u0 >= outer.u1 || h.v1 <= outer.v0 || h.v0 >= outer.v1) continue;
        us.push_back(std::max(outer.u0, std::min(outer.u1, h.u0)));
        us.push_back(std::max(outer.u0, std::min(outer.u1, h.u1)));
        vs.push_back(std::max(outer.v0, std::min(outer.v1, h.v0)));
        vs.push_back(std::max(outer.v0, std::min(outer.v1, h.v1)));
    }
    std::sort(us.begin(), us.end());
    us.erase(std::unique(us.begin(), us.end()), us.end());
    std::sort(vs.begin(), vs.end());
    vs.erase(std::unique(vs.begin(), vs.end()), vs.end());
    std::vector<R> out;
    for (size_t i = 0; i + 1 < us.size(); i++) {
        float u0 = us[i], u1 = us[i + 1], um = (u0 + u1) / 2;
        if (u1 - u0 < 0.01f) continue;
        // strip langs v: aaneengesloten stukken samenvoegen
        float start = -1e9f;
        for (size_t j = 0; j + 1 < vs.size(); j++) {
            float v0 = vs[j], v1 = vs[j + 1], vm = (v0 + v1) / 2;
            bool hole = false;
            for (const R& h : holes)
                if (um > h.u0 && um < h.u1 && vm > h.v0 && vm < h.v1) { hole = true; break; }
            if (!hole && start < -1e8f) start = v0;
            if ((hole || j + 2 == vs.size()) && start > -1e8f) {
                float end = hole ? v0 : v1;
                if (end - start > 0.01f) out.push_back({u0, u1, start, end});
                start = -1e9f;
            }
        }
    }
    return out;
}

bool inR(const R& r, float u, float v) { return u > r.u0 && u < r.u1 && v > r.v0 && v < r.v1; }

}  // namespace

void buildUtrechtCentraal(const World& world, Collision& col, SignAtlas& atlas, MeshBuilder& op, MeshBuilder& gl,
                          std::vector<Elevator>& elevators, Vec3& hallSpawn, Vec3& platformSpawn, float& hallYOut) {
    // Is Utrecht er wel (staat het station in de wereld)?
    bool present = false;
    for (const MapPoint& mp : world.points)
        if (mp.kind == POINT_STATION && mp.name == "Utrecht Centraal") present = true;
    if (!present) return;
    UB b(op, gl, col, atlas);

    // ---- hoogtes: perrons (OSM, spoor + 0,8 m) en hal ----
    float psum = 0;
    int pn = 0;
    for (const OsmPlatform& p : world.platforms) {
        if (p.pts.empty()) continue;
        float u, v;
        rdToUt(rdX(p.pts[0].first), rdY(p.pts[0].second), u, v);
        if (std::fabs(u) < 450 && std::fabs(v) < 120) { psum += p.height; pn++; }
    }
    const float PY = pn ? psum / pn : 2.8f;   // perronhoogte (NAP)
    const float HF = PY + 7.2f;                 // halvloer
    const float SLAB = 1.3f;                    // dikte halvloer (onderkant boven de perrons)
    hallYOut = HF;
    auto roofY = [&](float u, float v) {
        // golvend dak: golven dwars over de hal (kammen evenwijdig aan de sporen), randen iets lager
        float w = std::cos(2 * PI * (v - 8.0f) / 61.0f);
        float edge = (u - 5.0f) / 48.0f;
        return HF + 13.0f + 2.6f * w - 1.6f * edge * edge;
    };
    auto groundAt = [&](float u, float v) {
        Vec3 g = UB::P(u, v, 0);
        return world.heightAt(g.x, g.z);
    };
    auto lowY = [&](const Bank& bk, float u) { return bk.street ? groundAt(u, bk.v) : PY; };

    // ---- gaten in de halvloer ----
    std::vector<R> holes;
    for (const Bank& bk : BANKS) {
        if (bk.seHi != bk.seLo) holes.push_back({UT_HALL_U0, bk.seHi - 1.2f, bk.v - 2.4f, bk.v + 2.4f});
        if (bk.neHi != bk.neLo) holes.push_back({bk.neHi + 1.2f, bk.neLo - 1.2f, bk.v - 2.4f, bk.v + 2.4f});
        if (bk.nsHi != bk.nsLo) holes.push_back({bk.nsHi, bk.nsLo + 0.5f, bk.v - 1.7f, bk.v + 1.7f});
        if (bk.liftU != 0) holes.push_back({bk.liftU - 1.4f, bk.liftU + 1.4f, bk.v - 1.4f, bk.v + 1.4f});
    }
    holes.push_back({UT_NOTCH_U0 - 1, UT_NOTCH_U1, UT_NOTCH_V0, UT_NOTCH_V1});
    const R hall{UT_HALL_U0, UT_HALL_U1, UT_HALL_V0, UT_HALL_V1};
    std::vector<R> parts = subtract(hall, holes);

    // ---- halvloer: bovenkant (steen met banen), onderkant (plafond boven de perrons), randen ----
    for (const R& r : parts) {
        b.floor(r, HF, FLOOR_LIGHT);
        b.ceiling(r, HF - SLAB, CEIL_UNDER);
    }
    // randen van de gaten en de hal (betonband)
    auto slabEdge = [&](float u0, float u1, float v0, float v1) {
        b.box(u0, u1, v0, v1, HF - SLAB, HF - 0.05f, EDGE);  // niet tot de vloer: geen z-fighting op afstand
    };
    for (const R& h : holes) {
        if (h.u0 < UT_HALL_U0 - 0.5f) continue;
        slabEdge(h.u0, h.u1, h.v0 - 0.05f, h.v0);
        slabEdge(h.u0, h.u1, h.v1, h.v1 + 0.05f);
        slabEdge(h.u0 - 0.05f, h.u0, h.v0, h.v1);
        slabEdge(h.u1, h.u1 + 0.05f, h.v0, h.v1);
    }
    // lichtlijnen onder de halvloer (boven de perrons)
    for (float v = UT_HALL_V0 + 4; v < UT_HALL_V1; v += 5.0f)
        for (float u = UT_HALL_U0 + 2; u < UT_HALL_U1 - 2; u += 7.0f) {
            bool over = false;
            for (const R& h : holes) if (inR(h, u + 1.5f, v)) over = true;
            if (!over) b.box(u, u + 3.0f, v - 0.12f, v + 0.12f, HF - SLAB - 0.08f, HF - SLAB - 0.03f, LIGHT, EX_LIGHT);
        }

    // ---- zuilen onder de hal (op de perrons) ----
    for (const Bank& bk : BANKS) {
        if (bk.street) continue;
        for (float side : {-4.6f, 4.6f})
            for (float u : {-19.0f, 9.0f, 38.0f}) {
                Vec3 a = UB::P(u, bk.v + side, PY), c = UB::P(u, bk.v + side, HF - SLAB);
                b.op.tube(a, c, 0.45f, 14, CONCRETE, EX_PLAIN);
                b.solidBox(u - 0.45f, u + 0.45f, bk.v + side - 0.45f, bk.v + side + 0.45f, PY, HF - SLAB);
            }
    }

    // ---- roltrappen en trappen ----
    auto escalator = [&](float uLo, float uHi, float vc, float yLo, float yHi, bool up) {
        // band 1,0 m + balustrades; landingsplaten van 1,2 m aan beide einden
        const float dir = uHi > uLo ? 1.0f : -1.0f;
        float a0 = uLo + dir * 1.2f, a1 = uHi - dir * 1.2f;  // schuin stuk
        float um0 = std::min(a0, a1), um1 = std::max(a0, a1);
        float y0 = a0 < a1 ? yLo : yHi, y1 = a0 < a1 ? yHi : yLo;
        // treden
        int n = std::max(4, (int)(std::fabs(a1 - a0) / 0.40f));
        for (int k = 0; k < n; k++) {
            float t0 = (float)k / n, t1 = (float)(k + 1) / n;
            float ua = um0 + (um1 - um0) * t0, ub = um0 + (um1 - um0) * t1;
            float ya = y0 + (y1 - y0) * t0, yb = y0 + (y1 - y0) * t1;
            float ytop = std::max(ya, yb);
            b.box(ua, ub, vc - 0.5f, vc + 0.5f, ytop - 0.06f, ytop, ESC_STEP);
        }
        // landingsplaten (geribbeld staal)
        for (float ue : {uLo, uHi}) {
            float u0 = std::min(ue, ue + (ue == uLo ? dir : -dir) * 1.2f), u1 = std::max(ue, ue + (ue == uLo ? dir : -dir) * 1.2f);
            float y = ue == uLo ? yLo : yHi;
            b.box(u0, u1, vc - 0.55f, vc + 0.55f, y - 0.03f, y + 0.03f, STEEL);
        }
        // zijkanten (glazen balustrade met zwarte leuning) en onderkant (vakwerk)
        for (float s : {-0.62f, 0.62f}) {
            Vec3 A = UB::P(um0, vc + s, y0), Bp = UB::P(um1, vc + s, y1);
            Vec3 Al = A + Vec3(0, 0.95f, 0), Bl = Bp + Vec3(0, 0.95f, 0);
            Vec3 n = s > 0 ? b.V : b.V * -1.0f;
            b.gl.quad(A + Vec3(0, 0.15f, 0), Bp + Vec3(0, 0.15f, 0), Bl, Al, n, GLASS_T, EX_GLASS);
            b.op.tube(Al, Bl, 0.04f, 6, DARK_STEEL, EX_PLAIN);
            b.op.quad(A + Vec3(0, -0.9f, 0), Bp + Vec3(0, -0.9f, 0), Bp + Vec3(0, 0.15f, 0), A + Vec3(0, 0.15f, 0), n, ESC_SIDE, EX_PLAIN);
        }
        // onderkant
        b.op.quad(UB::P(um0, vc - 0.62f, y0 - 0.9f), UB::P(um1, vc - 0.62f, y1 - 0.9f), UB::P(um1, vc + 0.62f, y1 - 0.9f),
                  UB::P(um0, vc + 0.62f, y0 - 0.9f), Vec3(0, -1, 0), ESC_SIDE, EX_PLAIN);
        // botsing: schuine vloer met snelheid (omhoog of omlaag), zijkanten massief
        float speed = (up ? 1.0f : -1.0f) * (uHi > uLo ? 0.5f : -0.5f);
        b.slope(um0, um1, vc - 0.5f, vc + 0.5f, y0, y1, speed);
        b.slope(std::min(uLo, uLo + dir * 1.2f), std::max(uLo, uLo + dir * 1.2f), vc - 0.55f, vc + 0.55f, yLo, yLo, 0);
        b.slope(std::min(uHi, uHi - dir * 1.2f), std::max(uHi, uHi - dir * 1.2f), vc - 0.55f, vc + 0.55f, yHi, yHi, 0);
        for (float s : {-0.66f, 0.66f}) {
            Vec3 c = UB::P((um0 + um1) / 2, vc + s, 0);
            float ymin = std::min(y0, y1), ymax = std::max(y0, y1) + 1.0f;
            (void)c;
            // massieve zijkant in stukjes (volgt de helling)
            for (int k = 0; k < 6; k++) {
                float ua = um0 + (um1 - um0) * k / 6.0f, ub = um0 + (um1 - um0) * (k + 1) / 6.0f;
                float ya = y0 + (y1 - y0) * k / 6.0f, yb = y0 + (y1 - y0) * (k + 1) / 6.0f;
                b.solidBox(ua, ub, vc + s - 0.04f, vc + s + 0.04f, std::min(ya, yb) + 0.1f, std::max(ya, yb) + 1.0f);
            }
            (void)ymin; (void)ymax;
        }
    };
    auto stair = [&](float uLo, float uHi, float vc, float yLo, float yHi, float w) {
        float um0 = std::min(uLo, uHi), um1 = std::max(uLo, uHi);
        float y0 = uLo < uHi ? yLo : yHi, y1 = uLo < uHi ? yHi : yLo;
        int n = std::max(3, (int)std::round((yHi - yLo) / 0.17f));
        for (int k = 0; k < n; k++) {
            float t0 = (float)k / n, t1 = (float)(k + 1) / n;
            float ua = um0 + (um1 - um0) * t0, ub = um0 + (um1 - um0) * t1;
            float ytop = std::max(y0 + (y1 - y0) * t0, y0 + (y1 - y0) * t1);
            b.box(ua, ub, vc - w / 2, vc + w / 2, ytop - 0.17f, ytop, k % 2 ? STAIR_STONE : STAIR_STONE * 0.92f);
        }
        b.slope(um0, um1, vc - w / 2, vc + w / 2, y0, y1, 0);
        // leuningen + glazen zijwanden
        for (float s : {-w / 2 - 0.05f, w / 2 + 0.05f}) {
            Vec3 A = UB::P(um0, vc + s, y0), Bp = UB::P(um1, vc + s, y1);
            b.gl.quad(A, Bp, Bp + Vec3(0, 1.0f, 0), A + Vec3(0, 1.0f, 0), s > 0 ? b.V : b.V * -1.0f, GLASS_T, EX_GLASS);
            b.op.tube(A + Vec3(0, 1.0f, 0), Bp + Vec3(0, 1.0f, 0), 0.035f, 6, STEEL, EX_PLAIN);
            for (int k = 0; k < 6; k++) {
                float ua = um0 + (um1 - um0) * k / 6.0f, ub = um0 + (um1 - um0) * (k + 1) / 6.0f;
                float ya = y0 + (y1 - y0) * k / 6.0f, yb = y0 + (y1 - y0) * (k + 1) / 6.0f;
                b.solidBox(ua, ub, vc + s - 0.04f, vc + s + 0.04f, std::min(ya, yb) + 0.1f, std::max(ya, yb) + 1.05f);
            }
        }
        // onderkant
        b.op.quad(UB::P(um0, vc - w / 2, y0 - 0.35f), UB::P(um1, vc - w / 2, y1 - 0.35f), UB::P(um1, vc + w / 2, y1 - 0.35f),
                  UB::P(um0, vc + w / 2, y0 - 0.35f), Vec3(0, -1, 0), CONCRETE, EX_PLAIN);
    };
    // glazen balustrade (1,1 m) langs een lijn op de halvloer
    auto railU = [&](float u0, float u1, float v) {
        if (u1 - u0 < 0.05f) return;
        b.glassBox(u0, u1, v - 0.03f, v + 0.03f, HF, HF + 1.1f, true);
        b.box(u0, u1, v - 0.05f, v + 0.05f, HF + 1.1f, HF + 1.16f, STEEL);
    };
    auto railV = [&](float v0, float v1, float u) {
        if (v1 - v0 < 0.05f) return;
        b.glassBox(u - 0.03f, u + 0.03f, v0, v1, HF, HF + 1.1f, true);
        b.box(u - 0.05f, u + 0.05f, v0, v1, HF + 1.1f, HF + 1.16f, STEEL);
    };

    int bankNo = 0;
    for (const Bank& bk : BANKS) {
        bankNo++;
        // zuidelijke roltrappen: van het perron (u ~ -40) omhoog naar de hal (u ~ -25)
        if (bk.seHi != bk.seLo) {
            float yl = lowY(bk, bk.seLo);
            escalator(bk.seLo, bk.seHi, bk.v - 0.95f, yl, HF, true);
            escalator(bk.seLo, bk.seHi, bk.v + 0.95f, yl, HF, false);
            railU(UT_HALL_U0, bk.seHi - 1.2f, bk.v - 2.4f);
            railU(UT_HALL_U0, bk.seHi - 1.2f, bk.v + 2.4f);
            // OV-poortjes bij de ingang (aan de halkant), twee doorgangen vrij
            float ug = bk.seHi + 1.5f;
            for (float dv : {-2.3f, -0.3f, 1.7f}) {
                b.box(ug - 0.6f, ug + 0.6f, bk.v + dv - 0.12f, bk.v + dv + 0.12f, HF, HF + 1.0f, Vec3(0.20f, 0.21f, 0.23f), EX_PLAIN, true);
                b.box(ug - 0.15f, ug + 0.15f, bk.v + dv - 0.13f, bk.v + dv + 0.13f, HF + 0.95f, HF + 1.05f, NSYEL);
                b.gl.orientedBox(UB::P(ug, bk.v + dv + 0.6f, HF + 0.7f), {0.7f, 0.6f, 0.03f}, b.hU, GLASS_T, EX_GLASS);
            }
        }
        // zuidelijke vaste trap: van de halrand (u ~ -42) naar buiten omlaag
        if (bk.ssLo != bk.ssHi) {
            float yl = lowY(bk, bk.ssLo);
            stair(bk.ssLo, bk.ssHi, bk.v, yl, HF, 3.2f);
            // bordes tussen trap en halvloer
            b.floor({bk.ssHi, UT_HALL_U0 + 0.01f, bk.v - 1.7f, bk.v + 1.7f}, HF, FLOOR_LIGHT);
            b.ceiling({bk.ssHi, UT_HALL_U0, bk.v - 1.7f, bk.v + 1.7f}, HF - SLAB, CEIL_UNDER);
            for (float s : {-1.7f, 1.7f}) b.box(bk.ssHi, UT_HALL_U0, bk.v + s - 0.03f, bk.v + s + 0.03f, HF, HF + 1.1f, STEEL, EX_PLAIN, true);
        }
        // noordelijke roltrappen: van de hal (u ~ 0,5) omlaag naar het perron (u ~ 17)
        if (bk.neHi != bk.neLo) {
            float yl = lowY(bk, bk.neLo);
            escalator(bk.neLo, bk.neHi, bk.v - 0.95f, yl, HF, false);
            escalator(bk.neLo, bk.neHi, bk.v + 0.95f, yl, HF, true);
            railU(bk.neHi + 1.2f, bk.neLo - 1.2f, bk.v - 2.4f);
            railU(bk.neHi + 1.2f, bk.neLo - 1.2f, bk.v + 2.4f);
            railV(bk.v - 2.4f, bk.v + 2.4f, bk.neLo - 1.2f);
            float ug = bk.neHi - 1.5f;
            for (float dv : {-2.3f, -0.3f, 1.7f}) {
                b.box(ug - 0.6f, ug + 0.6f, bk.v + dv - 0.12f, bk.v + dv + 0.12f, HF, HF + 1.0f, Vec3(0.20f, 0.21f, 0.23f), EX_PLAIN, true);
                b.box(ug - 0.15f, ug + 0.15f, bk.v + dv - 0.13f, bk.v + dv + 0.13f, HF + 0.95f, HF + 1.05f, NSYEL);
            }
        }
        // noordelijke vaste trap in twee delen met een bordes halverwege
        if (bk.nsHi != bk.nsLo) {
            float yl = lowY(bk, bk.nsLo);
            float um = bk.nsHi + (bk.nsLo - bk.nsHi) * 0.44f, um2 = bk.nsHi + (bk.nsLo - bk.nsHi) * 0.56f;
            float ym = (HF + yl) / 2;
            stair(um, bk.nsHi, bk.v, ym, HF, 3.0f);
            b.floor({um, um2, bk.v - 1.5f, bk.v + 1.5f}, ym, STAIR_STONE);
            b.ceiling({um, um2, bk.v - 1.5f, bk.v + 1.5f}, ym - 0.35f, CONCRETE);
            stair(bk.nsLo, um2, bk.v, yl, ym, 3.0f);
            railU(bk.nsHi + 0.5f, bk.nsLo + 0.5f, bk.v - 1.7f);
            railU(bk.nsHi + 0.5f, bk.nsLo + 0.5f, bk.v + 1.7f);
            railV(bk.v - 1.7f, bk.v + 1.7f, bk.nsLo + 0.5f);
        }
        // glazen lift met stalen frame
        if (bk.liftU != 0) {
            float u = bk.liftU, v = bk.v;
            float yl = bk.street ? groundAt(u, v) : PY;
            b.glassBox(u - 1.1f, u + 1.1f, v - 1.1f, v + 1.1f, yl, HF + 2.8f, false);
            // massief, behalve aan de deurkant (u+)
            b.solidBox(u - 1.15f, u - 1.05f, v - 1.15f, v + 1.15f, yl, HF + 2.8f);
            b.solidBox(u - 1.15f, u + 1.15f, v - 1.15f, v - 1.05f, yl, HF + 2.8f);
            b.solidBox(u - 1.15f, u + 1.15f, v + 1.05f, v + 1.15f, yl, HF + 2.8f);
            b.solidBox(u + 1.05f, u + 1.15f, v - 1.15f, v + 1.15f, yl, HF + 2.8f);
            for (float cu : {-1.12f, 1.12f})
                for (float cv : {-1.12f, 1.12f}) b.box(u + cu - 0.05f, u + cu + 0.05f, v + cv - 0.05f, v + cv + 0.05f, yl, HF + 2.9f, DARK_STEEL);
            b.box(u - 1.15f, u + 1.15f, v - 1.15f, v + 1.15f, HF + 2.8f, HF + 3.0f, DARK_STEEL);
            b.box(u - 0.9f, u + 0.9f, v - 0.9f, v + 0.9f, yl - 0.02f, yl + 0.05f, STEEL);  // cabine (onderaan)
            b.sign("Lift", NSBLUE, WHITE, UB::P(u + 1.17f, v, HF + 2.4f), b.V, 0.3f);
            Elevator e;
            e.bottom = UB::P(u + 1.9f, v, yl);
            e.top = UB::P(u + 1.9f, v, HF);
            e.label = "de stationshal";
            e.topLabel = bk.toLabel;
            elevators.push_back(e);
        }
        // borden: spoornummers en bestemmingen boven de roltrappen (NS-blauw met wit, geel nummer)
        if (!bk.street) {
            for (float u : {bk.seHi + 3.0f, bk.neHi - 3.0f}) {
                Vec3 c = UB::P(u, bk.v, HF + 3.6f);
                b.sign(std::string(bk.tracks), NSYEL, NSBLUE, c + b.V * -1.6f, b.V, 0.9f, 96);
                b.sign(bk.dests, NSBLUE, WHITE, c + b.V * 2.2f, b.V, 0.42f);
                b.op.tube(UB::P(u, bk.v - 1.6f, HF + 4.1f), UB::P(u, bk.v - 1.6f, roofY(u, bk.v)), 0.02f, 4, STEEL, EX_PLAIN);
            }
        } else {
            Vec3 c = UB::P(bk.seHi + 3.0f, bk.v, HF + 3.4f);
            b.sign(std::string("Uitgang  ") + bk.toLabel, NSBLUE, WHITE, c, b.V, 0.5f);
        }
    }

    // ---- winkels (paviljoens) ----
    for (const Shop& s : SHOPS) {
        const float H = 4.2f;
        bool north = s.u0 > 0;
        float fu = north ? s.u0 : s.u1;  // winkelpui (naar de hoofdpassage)
        b.box(s.u0, s.u1, s.v0, s.v1, HF + H - 0.5f, HF + H, Vec3(0.92f, 0.92f, 0.90f));            // dak/rand
        b.box(s.u0, s.u1, s.v0, s.v1, HF + H, HF + H + 0.05f, Vec3(0.75f, 0.75f, 0.74f));
        // zijwanden wit, achterwand
        b.box(s.u0, s.u1, s.v0, s.v0 + 0.15f, HF, HF + H - 0.5f, Vec3(0.93f, 0.93f, 0.91f), EX_PLAIN, true);
        b.box(s.u0, s.u1, s.v1 - 0.15f, s.v1, HF, HF + H - 0.5f, Vec3(0.93f, 0.93f, 0.91f), EX_PLAIN, true);
        float bu = north ? s.u1 : s.u0;
        b.box(std::min(bu, bu + (north ? -0.15f : 0.15f)), std::max(bu, bu + (north ? -0.15f : 0.15f)), s.v0, s.v1, HF, HF + H - 0.5f,
              Vec3(0.88f, 0.87f, 0.85f), EX_PLAIN, true);
        // pui: glas met deuropening in het midden
        float vm = (s.v0 + s.v1) / 2;
        b.glassBox(std::min(fu, fu + (north ? 0.08f : -0.08f)), std::max(fu, fu + (north ? 0.08f : -0.08f)), s.v0 + 0.15f, vm - 1.2f, HF, HF + H - 0.5f);
        b.glassBox(std::min(fu, fu + (north ? 0.08f : -0.08f)), std::max(fu, fu + (north ? 0.08f : -0.08f)), vm + 1.2f, s.v1 - 0.15f, HF, HF + H - 0.5f);
        // binnen: vloer, toonbank, schappen, verlichting
        float ui0 = s.u0 + 0.2f, ui1 = s.u1 - 0.2f;
        b.box(ui0, ui1, s.v0 + 0.2f, s.v1 - 0.2f, HF - 0.02f, HF + 0.03f, Vec3(0.55f, 0.50f, 0.45f));
        float bk = north ? s.u1 - 1.6f : s.u0 + 1.6f;
        for (float v = s.v0 + 1.2f; v < s.v1 - 1.2f; v += 2.4f)
            b.box(std::min(bk, bk + (north ? 0.7f : -0.7f)), std::max(bk, bk + (north ? 0.7f : -0.7f)), v - 0.9f, v + 0.9f, HF, HF + 2.0f, s.bg * 0.6f + Vec3(0.3f, 0.3f, 0.3f), EX_PLAIN, true);
        float cu = north ? s.u0 + 2.5f : s.u1 - 2.5f;
        b.box(std::min(cu, cu + 1.0f), std::max(cu, cu + 1.0f), s.v0 + 1.0f, s.v0 + 3.5f, HF, HF + 1.05f, Vec3(0.85f, 0.84f, 0.80f), EX_PLAIN, true);
        b.box(ui0, ui1, vm - 0.15f, vm + 0.15f, HF + H - 0.56f, HF + H - 0.52f, LIGHT, EX_LIGHT);
        // naambord boven de pui
        Vec3 fc = UB::P(fu + (north ? -0.06f : 0.06f), vm, HF + H - 0.25f);
        SignRect r = atlas.add(s.name, s.bg, s.fg, 72);
        float hgt = 0.42f;
        float wid = hgt * r.aspect;
        if (wid > (s.v1 - s.v0) - 1.0f) hgt = ((s.v1 - s.v0) - 1.0f) / r.aspect;
        SignAtlas::quad(op, r, fc, north ? b.V * -1.0f : b.V, hgt, false);
        // bord ook aan de andere kant van het dak (zichtbaar vanaf de roltrappen)
        SignAtlas::quad(op, r, UB::P(fu + (north ? -0.06f : 0.06f), vm, HF + H + 0.4f), north ? b.V * -1.0f : b.V, hgt * 0.9f, true);
    }

    // ---- mezzanine (noordstrook, met de Bistrot) ----
    {
        const float MZ = HF + 5.2f;
        R mz{31.5f, UT_HALL_U1, -95.0f, 98.0f};
        b.floor(mz, MZ, FLOOR_LIGHT, true);
        b.ceiling(mz, MZ - 0.6f, Vec3(0.88f, 0.88f, 0.86f));
        b.box(31.5f, 31.6f, mz.v0, mz.v1, MZ - 0.6f, MZ, Vec3(0.90f, 0.90f, 0.88f));
        b.glassBox(31.5f, 31.56f, mz.v0, mz.v1, MZ, MZ + 1.1f, true);
        b.box(31.45f, 31.6f, mz.v0, mz.v1, MZ + 1.1f, MZ + 1.16f, STEEL);
        for (float v = mz.v0 + 3; v < mz.v1; v += 9) b.box(31.6f, 37.5f, v, v + 0.1f, MZ - 0.67f, MZ - 0.63f, LIGHT, EX_LIGHT);
        // Bistrot: tafels en stoelen
        for (float v = 62; v < 95; v += 3.2f)
            for (float u : {34.0f, 38.5f, 43.0f}) {
                b.box(u - 0.4f, u + 0.4f, v - 0.4f, v + 0.4f, MZ + 0.72f, MZ + 0.76f, Vec3(0.45f, 0.30f, 0.18f));
                b.op.tube(UB::P(u, v, MZ), UB::P(u, v, MZ + 0.72f), 0.04f, 6, DARK_STEEL, EX_PLAIN);
                for (float dv : {-0.75f, 0.75f}) b.box(u - 0.22f, u + 0.22f, v + dv - 0.22f, v + dv + 0.22f, MZ + 0.42f, MZ + 0.46f, DARK_STEEL);
            }
        b.sign("Bistrot Utrecht Centraal", Vec3(0.12f, 0.12f, 0.12f), Vec3(0.95f, 0.85f, 0.55f), UB::P(31.45f, 78.0f, MZ + 1.7f), b.V * -1.0f, 0.5f);
        // trap naar de mezzanine (langs v)
        {
            float u = 29.6f, v0 = 88.5f, v1 = 97.2f;
            int n = (int)std::round((MZ - HF) / 0.17f);
            for (int k = 0; k < n; k++) {
                float va = v0 + (v1 - v0) * k / n, vb = v0 + (v1 - v0) * (k + 1) / n;
                float y = HF + (MZ - HF) * (k + 1) / n;
                b.box(u - 1.2f, u + 1.2f, va, vb, y - 0.17f, y, STAIR_STONE);
            }
            Vec3 m = UB::P(u, (v0 + v1) / 2, 0);
            col.addFloor({m.x, m.z, b.V.x, b.V.z, (v1 - v0) / 2, 1.2f, HF, MZ, 0});
            b.floor({28.4f, 31.6f, 97.2f, 99.0f}, MZ, FLOOR_LIGHT);
        }
    }

    // ---- kolommen en dak ----
    for (float u : {-12.0f, 44.0f})
        for (float v : {-104.0f, -76.0f, -46.0f, -23.0f, 1.5f, 25.0f, 48.5f, 80.0f, 106.0f}) {
            float top = roofY(u, v);
            b.op.tube(UB::P(u, v, HF), UB::P(u, v, top - 0.3f), 0.38f, 16, COLUMN_WHITE, EX_PLAIN);
            // kolomkop: vier schuine armen naar het dak
            for (float du : {-1.0f, 1.0f})
                for (float dv : {-1.0f, 1.0f})
                    b.op.tube(UB::P(u, v, top - 3.5f), UB::P(u + du * 3.0f, v + dv * 3.0f, roofY(u + du * 3, v + dv * 3) - 0.3f), 0.16f, 8, COLUMN_WHITE, EX_PLAIN);
            b.solidBox(u - 0.4f, u + 0.4f, v - 0.4f, v + 0.4f, HF, top);
        }
    {
        const float DU = 4.55f, DV = 3.0f;
        const float u0 = UT_HALL_U0 - 3.0f, u1 = UT_HALL_U1 + 1.5f, v0 = UT_HALL_V0 - 3.0f, v1 = UT_HALL_V1 + 3.0f;
        int nu = (int)std::ceil((u1 - u0) / DU), nv = (int)std::ceil((v1 - v0) / DV);
        for (int i = 0; i < nu; i++)
            for (int j = 0; j < nv; j++) {
                float ua = u0 + (u1 - u0) * i / nu, ub = u0 + (u1 - u0) * (i + 1) / nu;
                float va = v0 + (v1 - v0) * j / nv, vb = v0 + (v1 - v0) * (j + 1) / nv;
                float um = (ua + ub) / 2, vm = (va + vb) / 2;
                if (um > UT_NOTCH_U0 - 3 && um < UT_NOTCH_U1 && vm > UT_NOTCH_V0 && vm < UT_NOTCH_V1) continue;
                Vec3 A = UB::P(ua, va, roofY(ua, va)), Bq = UB::P(ub, va, roofY(ub, va)), C = UB::P(ub, vb, roofY(ub, vb)), D = UB::P(ua, vb, roofY(ua, vb));
                Vec3 n = normalize(cross(C - A, Bq - D));
                if (n.y < 0) n = n * -1.0f;
                // lichtstraat in elk golfdal (evenwijdig aan de sporen)
                float w = std::cos(2 * PI * (vm - 8.0f) / 61.0f);
                bool sky = w < -0.82f && um > UT_HALL_U0 + 2 && um < UT_HALL_U1 - 2;
                if (sky) {
                    b.gl.quad(A, D, C, Bq, n * -1.0f, Vec3(0.75f, 0.85f, 0.90f), EX_GLASS);
                    b.gl.quad(A, Bq, C, D, n, Vec3(0.75f, 0.85f, 0.90f), EX_GLASS);
                    continue;
                }
                // onderkant (wit, met lichte cassettes) en bovenkant
                Vec3 dn(0, -0.02f, 0);
                // onderkant: zelflichtend wit (de shader belicht een plafond van onderen alleen met omgevingslicht; in het
                // echt is het dak helder van het daglicht door de lichtstraten en 's avonds van de verlichting)
                float shade = 0.80f + 0.07f * std::sin(2 * PI * (vm - 8.0f) / 61.0f) - ((i + j) % 2) * 0.02f;
                b.op.quad(A + dn, D + dn, C + dn, Bq + dn, n * -1.0f, ROOF_UNDER * shade, EX_LIGHT);
                Vec3 up(0, 0.45f, 0);
                b.op.quad(A + up, Bq + up, C + up, D + up, n, ROOF_TOP, EX_PLAIN);
            }
        // dakrand
        auto edge = [&](float ua, float va, float ub, float vb) {
            int n = 40;
            for (int k = 0; k < n; k++) {
                float a0 = (float)k / n, a1 = (float)(k + 1) / n;
                float uA = ua + (ub - ua) * a0, vA = va + (vb - va) * a0, uB = ua + (ub - ua) * a1, vB = va + (vb - va) * a1;
                Vec3 A = UB::P(uA, vA, roofY(uA, vA) - 0.6f), Bq = UB::P(uB, vB, roofY(uB, vB) - 0.6f);
                Vec3 d = Bq - A;
                Vec3 nrm = normalize(Vec3(-d.z, 0, d.x));
                b.op.quad(A, Bq, Bq + Vec3(0, 1.1f, 0), A + Vec3(0, 1.1f, 0), nrm, Vec3(0.85f, 0.85f, 0.84f), EX_PLAIN);
                b.op.quad(Bq, A, A + Vec3(0, 1.1f, 0), Bq + Vec3(0, 1.1f, 0), nrm * -1.0f, Vec3(0.85f, 0.85f, 0.84f), EX_PLAIN);
            }
        };
        edge(u0, v0, u1, v0);
        edge(u1, v0, u1, v1);
        edge(u1, v1, u0, v1);
        edge(u0, v1, u0, v0);
    }

    // ---- gevels: glas van de vloer tot het dak, met stijlen; openingen bij de zuidelijke trappen ----
    auto facadeU = [&](float u, float va, float vb, const std::vector<std::pair<float, float>>& gaps) {
        for (float v = va; v < vb - 0.01f; v += 3.0f) {
            float v2 = std::min(vb, v + 3.0f);
            bool gap = false;
            for (auto& g : gaps) if ((v + v2) / 2 > g.first && (v + v2) / 2 < g.second) gap = true;
            float top = std::min(roofY(u, v), roofY(u, v2)) - 0.4f;
            if (!gap) {
                b.gl.quad(UB::P(u, v, HF), UB::P(u, v2, HF), UB::P(u, v2, top), UB::P(u, v, top), b.U, GLASS_T, EX_GLASS);
                b.solidBox(u - 0.05f, u + 0.05f, v, v2, HF, HF + 3.0f);
            } else {
                b.gl.quad(UB::P(u, v, HF + 3.0f), UB::P(u, v2, HF + 3.0f), UB::P(u, v2, top), UB::P(u, v, top), b.U, GLASS_T, EX_GLASS);
            }
            b.box(u - 0.08f, u + 0.08f, v - 0.06f, v + 0.06f, HF, top, DARK_STEEL);
            b.box(u - 0.06f, u + 0.06f, v, v2, HF + 3.0f, HF + 3.12f, DARK_STEEL);
        }
    };
    auto facadeV = [&](float v, float ua, float ub) {
        for (float u = ua; u < ub - 0.01f; u += 3.0f) {
            float u2 = std::min(ub, u + 3.0f);
            float top = std::min(roofY(u, v), roofY(u2, v)) - 0.4f;
            b.gl.quad(UB::P(u, v, HF), UB::P(u2, v, HF), UB::P(u2, v, top), UB::P(u, v, top), b.V, GLASS_T, EX_GLASS);
            b.solidBox(u, u2, v - 0.05f, v + 0.05f, HF, HF + 3.0f);
            b.box(u - 0.06f, u + 0.06f, v - 0.08f, v + 0.08f, HF, top, DARK_STEEL);
        }
    };
    {
        std::vector<std::pair<float, float>> gaps;
        for (const Bank& bk : BANKS) if (bk.ssLo != bk.ssHi) gaps.push_back({bk.v - 1.7f, bk.v + 1.7f});
        facadeU(UT_HALL_U0, UT_HALL_V0, UT_NOTCH_V0, gaps);
        facadeU(UT_HALL_U0, UT_NOTCH_V1, UT_HALL_V1, gaps);
        facadeU(UT_NOTCH_U1, UT_NOTCH_V0, UT_NOTCH_V1, {});
        facadeU(UT_HALL_U1, UT_HALL_V0, UT_HALL_V1, {});
        facadeV(UT_HALL_V0, UT_HALL_U0, UT_HALL_U1);
        facadeV(UT_HALL_V1, UT_HALL_U0, UT_HALL_U1);
        facadeV(UT_NOTCH_V0, UT_NOTCH_U0, UT_NOTCH_U1);
        facadeV(UT_NOTCH_V1, UT_NOTCH_U0, UT_NOTCH_U1);
    }
    // naam op de gevels (Jaarbeurszijde en Centrumzijde, buiten en binnen) en grote vertrekborden in de hal
    for (float v : {UT_HALL_V0 - 0.15f, UT_HALL_V1 + 0.15f}) {
        float top = roofY(5, v) - 2.2f;
        b.sign("Utrecht Centraal", NSBLUE, WHITE, UB::P(5.0f, v, top), v < 0 ? b.U : b.U * -1.0f, 2.2f, 96);
    }
    for (float v : {-70.0f, -22.0f, 25.0f, 72.0f}) {
        float u = -10.5f, y = HF + 5.2f;
        b.box(u - 0.15f, u + 0.15f, v - 4.2f, v + 4.2f, y - 1.4f, y + 1.4f, NSBLUE);
        b.sign("Vertrek  Departures", NSBLUE, WHITE, UB::P(u - 0.17f, v, y + 1.0f), b.V * -1.0f, 0.38f);
        b.sign("Vertrek  Departures", NSBLUE, WHITE, UB::P(u + 0.17f, v, y + 1.0f), b.V, 0.38f);
        const char* lines[] = {"IC  Alkmaar          7", "IC  Maastricht     18", "SPR Bodegraven     20", "SPR 's-Hertogenbosch 21"};
        for (int k = 0; k < 4; k++)
            for (float side : {-1.0f, 1.0f})
                b.sign(lines[(k + (int)(v / 7)) & 3], NSBLUE, WHITE, UB::P(u + side * 0.17f, v, y + 0.45f - k * 0.42f), side < 0 ? b.V * -1.0f : b.V, 0.32f);
        b.op.tube(UB::P(u, v - 3.5f, y + 1.4f), UB::P(u, v - 3.5f, roofY(u, v - 3.5f)), 0.02f, 4, STEEL, EX_PLAIN);
        b.op.tube(UB::P(u, v + 3.5f, y + 1.4f), UB::P(u, v + 3.5f, roofY(u, v + 3.5f)), 0.02f, 4, STEEL, EX_PLAIN);
    }
    // klok in het midden van de hal
    {
        Vec3 c = UB::P(-10.5f, 1.5f, HF + 7.5f);
        b.op.tube(c + b.U * -0.15f, c + b.U * 0.15f, 0.8f, 24, WHITE, EX_PLAIN);
        b.op.tube(c + b.U * -0.17f, c + b.U * 0.17f, 0.82f, 24, DARK_STEEL, EX_PLAIN);
        b.op.tube(c + Vec3(0, 0.82f, 0), UB::P(-10.5f, 1.5f, roofY(-10.5f, 1.5f)), 0.03f, 6, STEEL, EX_PLAIN);
    }
    // banken in de hal
    for (float v : {-60.0f, -42.0f, -2.0f, 30.0f, 50.0f, 86.0f}) {
        float u = -14.0f;
        b.box(u - 0.25f, u + 0.25f, v - 1.5f, v + 1.5f, HF + 0.42f, HF + 0.47f, Vec3(0.55f, 0.36f, 0.20f), EX_PLAIN, true);
        for (float dv : {-1.2f, 1.2f}) b.box(u - 0.2f, u + 0.2f, v + dv - 0.05f, v + dv + 0.05f, HF, HF + 0.42f, DARK_STEEL);
    }

    // ---- perronkappen (OSM-vormen): wit plafond, grijze bovenkant, kolommen op het perronmidden ----
    for (const CanopyPoly& cp : CANOPIES) {
        std::vector<std::vector<std::array<float, 2>>> poly(1);
        float umin = 1e9f, umax = -1e9f, vsum = 0;
        for (int k = 0; k < cp.n; k++) {
            poly[0].push_back({cp.p[k][0], cp.p[k][1]});
            umin = std::min(umin, cp.p[k][0]);
            umax = std::max(umax, cp.p[k][0]);
            vsum += cp.p[k][1];
        }
        std::vector<uint32_t> idx = mapbox::earcut<uint32_t>(poly);
        const float CY = PY + 5.4f;
        for (int pass = 0; pass < 2; pass++) {
            uint32_t base = (uint32_t)op.v.size();
            for (int k = 0; k < cp.n; k++)
                op.vert(UB::P(cp.p[k][0], cp.p[k][1], CY + (pass ? 0.35f : 0.0f)), Vec3(0, pass ? 1.0f : -1.0f, 0), pass ? CANOPY_TOP : CANOPY_UNDER * 0.74f,
                        pass ? EX_PLAIN : EX_LIGHT);
            for (size_t t = 0; t + 2 < idx.size(); t += 3) {
                if (pass) op.i.insert(op.i.end(), {base + idx[t], base + idx[t + 1], base + idx[t + 2]});
                else op.i.insert(op.i.end(), {base + idx[t], base + idx[t + 2], base + idx[t + 1]});
            }
        }
        // rand
        for (int k = 0; k < cp.n; k++) {
            Vec3 A = UB::P(cp.p[k][0], cp.p[k][1], CY), Bq = UB::P(cp.p[(k + 1) % cp.n][0], cp.p[(k + 1) % cp.n][1], CY);
            Vec3 d = Bq - A;
            Vec3 nrm = normalize(Vec3(-d.z, 0, d.x));
            op.quad(A, Bq, Bq + Vec3(0, 0.35f, 0), A + Vec3(0, 0.35f, 0), nrm, Vec3(0.80f, 0.80f, 0.79f), EX_PLAIN);
            op.quad(Bq, A, A + Vec3(0, 0.35f, 0), Bq + Vec3(0, 0.35f, 0), nrm * -1.0f, Vec3(0.80f, 0.80f, 0.79f), EX_PLAIN);
        }
        // kolommen en lichtlijnen langs het midden (v = gemiddelde)
        float vc = vsum / cp.n;
        float uStart = std::max(umin, -400.0f) + 6, uEnd = umax - 4;
        for (float u = uStart; u < uEnd; u += 13.0f) {
            if (u > UT_HALL_U0 - 3 && u < UT_HALL_U1 + 3) continue;
            b.op.tube(UB::P(u, vc, PY), UB::P(u, vc, CY), 0.16f, 10, Vec3(0.88f, 0.88f, 0.86f), EX_PLAIN);
            b.solidBox(u - 0.17f, u + 0.17f, vc - 0.17f, vc + 0.17f, PY, CY);
        }
        for (float u = umin + 2; u < umax - 3; u += 6.0f)
            b.box(u, u + 3.5f, vc - 0.15f, vc + 0.15f, CY - 0.08f, CY - 0.03f, LIGHT, EX_LIGHT);
    }

    hallSpawn = UB::P(-15.0f, 0.0f, HF);
    platformSpawn = UB::P(-75.0f, -35.6f, PY);
}
