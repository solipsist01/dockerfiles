// Station Best (2002, architect Theo Fikkers), verdiept aangelegd en deels in de Spoortunnel Best.
// Gereconstrueerd uit:
//  - OpenStreetMap: stationsgebouw (omtrek), trappen met bordessen, liften, keerwanden, perrons, tunnelsporen
//  - AHN: maaiveld (NAP 16,75 m) en spoorhoogte (NAP ~9,45 m: ca. 7 m diep)
//  - foto's: glazen hal met gebogen dak op schuine witte kolommen met pieken, roze plint, oranjebruine bakstenen
//    "trommel" met klok; ondergronds: donker geribbeld plafond met groene lichtlijn, beige/bruine tegelwanden,
//    blauwe kolommen, rode trapleuningen, blauwe stalen liftschachten; open bak met roze keerwanden.
#include <algorithm>
#include <array>
#include "../third_party/earcut.hpp"
#include "station.h"

using namespace WorldFormat;

namespace {
const Vec3 TILE_BEIGE(0.80f, 0.71f, 0.56f);
const Vec3 CONCRETE_DARK(0.40f, 0.40f, 0.41f);
const Vec3 CONCRETE(0.60f, 0.60f, 0.58f);
const Vec3 CEIL_DARK(0.28f, 0.29f, 0.31f);
const Vec3 RIB(0.36f, 0.37f, 0.39f);
const Vec3 GREEN_LIGHT(0.35f, 1.0f, 0.55f);
const Vec3 DOWNLIGHT(1.0f, 0.92f, 0.75f);
const Vec3 BLUE_STEEL(0.12f, 0.24f, 0.55f);
const Vec3 RED_RAIL(0.75f, 0.10f, 0.08f);
const Vec3 PINK(0.86f, 0.66f, 0.62f);
const Vec3 WHITE(0.93f, 0.93f, 0.92f);
const Vec3 BRICK(0.66f, 0.32f, 0.19f);
const Vec3 ROOF_TOP(0.62f, 0.63f, 0.64f);
const Vec3 ROOF_UNDER(0.82f, 0.80f, 0.74f);
const Vec3 HALL_FLOOR(0.62f, 0.60f, 0.57f);
const Vec3 STEP(0.50f, 0.48f, 0.47f);
const Vec3 NSYEL(1.0f, 0.78f, 0.0f);
const Vec3 NSBLUE(0.0f, 0.13f, 0.43f);
constexpr float EX_TILEWALL = -7000.0f;  // shader: tegelpatroon

using Poly = std::vector<std::pair<float, float>>;

bool inPoly(const Poly& r, float x, float z) {
    bool in = false;
    for (size_t k = 0, j = r.size() - 1; k < r.size(); j = k++)
        if ((r[k].second > z) != (r[j].second > z) && x < (r[j].first - r[k].first) * (z - r[k].second) / (r[j].second - r[k].second) + r[k].first)
            in = !in;
    return in;
}
float distToPoly(const Poly& r, float x, float z) {
    if (inPoly(r, x, z)) return 0;
    float best = 1e9f;
    for (size_t k = 0; k < r.size(); k++) {
        auto a = r[k], b = r[(k + 1) % r.size()];
        float dx = b.first - a.first, dz = b.second - a.second, l2 = dx * dx + dz * dz;
        float t = l2 > 0 ? clampf(((x - a.first) * dx + (z - a.second) * dz) / l2, 0, 1) : 0;
        best = std::min(best, std::hypot(x - a.first - dx * t, z - a.second - dz * t));
    }
    return best;
}

struct Builder {
    MeshBuilder& op;
    MeshBuilder& gl;
    Collision& col;
    // verticale wand tussen a en b (grondvlak), van y0 tot y1
    void wall(Vec3 a, Vec3 b, float y0a, float y1a, float y0b, float y1b, Vec3 c, float ex) {
        Vec3 d = b - a;
        Vec3 n = normalize(Vec3(-d.z, 0, d.x));
        op.quad(Vec3(a.x, y0a, a.z), Vec3(b.x, y0b, b.z), Vec3(b.x, y1b, b.z), Vec3(a.x, y1a, a.z), n, c, ex);
    }
    void solid(Vec3 a, Vec3 b, float thick, float y0, float y1) {
        Vec3 d = b - a;
        float len = std::hypot(d.x, d.z);
        if (len < 0.05f) return;
        col.addBox({(a.x + b.x) / 2, (a.z + b.z) / 2, d.x / len, d.z / len, len / 2, thick / 2, y0, y1});
    }
    // balk (box) tussen twee punten op gelijke hoogte, breedte w, hoogte h (midden op y)
    void beam(Vec3 a, Vec3 b, float w, float h, Vec3 c, float ex) {
        Vec3 d = b - a;
        float len = std::hypot(d.x, d.z);
        if (len < 0.01f) return;
        float hd = std::atan2(d.x, d.z);
        op.orientedBox((a + b) * 0.5f, {w, h, len}, hd, c, ex);
    }
};

struct Flight { Vec3 a, b; float ya = 0, yb = 0; bool used = false; int chain = -1; };
}  // namespace

void buildBestStation(const World& world, Collision& col, SignAtlas& atlas, MeshBuilder& op, MeshBuilder& gl,
                      std::vector<Elevator>& elevators, Vec3& center, float& platformY, float& hallYOut) {
    Builder B{op, gl, col};
    // ---------- gegevens ----------
    Vec3 stationPt;
    bool haveStation = false;
    for (const MapPoint& p : world.points)
        if (p.kind == POINT_STATION && p.name == "Best") { stationPt = Vec3(p.x, 0, p.z); haveStation = true; }
    if (!haveStation) return;
    std::vector<const OsmPlatform*> plats;
    float psum = 0;
    for (const OsmPlatform& p : world.platforms) {
        float cx = 0, cz = 0;
        for (auto& q : p.pts) { cx += q.first; cz += q.second; }
        cx /= p.pts.size(); cz /= p.pts.size();
        if (std::hypot(cx - stationPt.x, cz - stationPt.z) < 350) { plats.push_back(&p); psum += p.height; }
    }
    platformY = plats.empty() ? 10.3f : psum / plats.size();
    // startpunt: midden van het grootste perron
    const OsmPlatform* biggest = nullptr;
    float bigA = 0;
    for (const OsmPlatform* p : plats) {
        float a = 0;
        for (size_t k = 0; k < p->pts.size(); k++) {
            auto q = p->pts[k], r = p->pts[(k + 1) % p->pts.size()];
            a += q.first * r.second - r.first * q.second;
        }
        if (std::fabs(a) > bigA) { bigA = std::fabs(a); biggest = p; }
    }
    const WorldFeature* hall = nullptr;
    for (const WorldFeature& f : world.features)
        if (f.kind == FEAT_STATION_HALL) hall = &f;
    const float hallY = hall ? hall->a + 0.05f : 16.8f;
    hallYOut = hallY;
    Poly hallPoly;
    if (hall) for (const Vec3& p : hall->pts) hallPoly.push_back({p.x, p.z});
    center = Vec3(stationPt.x, platformY, stationPt.z);
    if (biggest) {
        // punt op het perron dat het dichtst bij het stationspunt ligt (perron is lang en smal)
        float best = 1e9f;
        for (size_t k = 0; k < biggest->pts.size(); k++)
            for (size_t j = k + 1; j < biggest->pts.size(); j++) {
                float mx = (biggest->pts[k].first + biggest->pts[j].first) / 2, mz = (biggest->pts[k].second + biggest->pts[j].second) / 2;
                if (!inPoly(biggest->pts, mx, mz)) continue;
                float d = std::hypot(mx - stationPt.x, mz - stationPt.z);
                if (d < best) { best = d; center = Vec3(mx, biggest->height, mz); }
            }
    }
    auto onPlatform = [&](float x, float z, float tol) -> const OsmPlatform* {
        for (const OsmPlatform* p : plats)
            if (distToPoly(p->pts, x, z) <= tol) return p;
        return nullptr;
    };

    // ================= TUNNEL =================
    std::vector<const RailLine*> tun;
    for (const RailLine& r : world.rails)
        if (r.tunnel) tun.push_back(&r);
    const RailLine* ref = nullptr;
    for (const RailLine* r : tun)
        if (!ref || r->pts.size() > ref->pts.size()) ref = r;
    struct Sec { Vec3 p, d, r; float L = 0, R = 0, y = 0, ceil = 0; std::vector<float> lats; bool station = false; std::vector<std::pair<float, float>> plat; };
    std::vector<Sec> secs;
    if (ref) {
        // langs het referentiespoor elke 4 m een dwarsdoorsnede
        std::vector<Vec3> pts;
        for (const RailPoint& p : ref->pts) pts.push_back(Vec3(p.x, p.y, p.z));
        float next = 0;  // afstand in het huidige segment tot de volgende doorsnede
        for (size_t k = 0; k + 1 < pts.size(); k++) {
            Vec3 a = pts[k], b = pts[k + 1];
            float len = std::hypot(b.x - a.x, b.z - a.z);
            if (len < 1e-3f) continue;
            float s = next;
            for (; s < len; s += 4.0f) {
                Sec sc;
                sc.p = a + (b - a) * (s / len);
                sc.d = normalize(Vec3(b.x - a.x, 0, b.z - a.z));
                sc.r = Vec3(-sc.d.z, 0, sc.d.x);
                secs.push_back(sc);
            }
            next = s - len;
        }
        for (Sec& sc : secs) {
            float ysum = 0;
            int yn = 0;
            for (const RailLine* r : tun) {
                float best = 1e9f, lat = 0, y = 0;
                for (const RailPoint& q : r->pts) {
                    float ax = q.x - sc.p.x, az = q.z - sc.p.z;
                    float al = ax * sc.d.x + az * sc.d.z;
                    float dd = std::fabs(al);
                    if (dd < best && std::fabs(ax * sc.r.x + az * sc.r.z) < 25) { best = dd; lat = ax * sc.r.x + az * sc.r.z; y = q.y; }
                }
                if (best < 4.0f) { sc.lats.push_back(lat); ysum += y; yn++; }
            }
            std::sort(sc.lats.begin(), sc.lats.end());
            if (sc.lats.empty()) sc.lats.push_back(0);
            sc.y = yn ? ysum / yn : sc.p.y;
            // perrons in deze doorsnede
            float pmin = 1e9f, pmax = -1e9f;
            bool inP = false;
            float start = 0;
            for (float l = sc.lats.front() - 8; l <= sc.lats.back() + 8; l += 0.25f) {
                Vec3 q = sc.p + sc.r * l;
                bool on = false;
                for (const OsmPlatform* p : plats)
                    if (inPoly(p->pts, q.x, q.z)) on = true;
                if (on) { pmin = std::min(pmin, l); pmax = std::max(pmax, l); }
                if (on && !inP) start = l;
                if (!on && inP) sc.plat.push_back({start, l});
                inP = on;
            }
            sc.station = pmax > pmin;
            sc.L = sc.lats.front() - 2.4f;
            sc.R = sc.lats.back() + 2.4f;
            if (sc.station) { sc.L = std::min(sc.L, pmin - 0.1f); sc.R = std::max(sc.R, pmax + 0.1f); }
            float terr = world.heightAt(sc.p.x, sc.p.z);
            sc.ceil = std::min(sc.y + 6.4f, terr - 0.5f);
        }
    }
    auto underHall = [&](Vec3 p) { return !hallPoly.empty() && inPoly(hallPoly, p.x, p.z); };
    for (size_t k = 0; k + 1 < secs.size(); k++) {
        const Sec& a = secs[k];
        const Sec& b = secs[k + 1];
        if (std::hypot(b.p.x - a.p.x, b.p.z - a.p.z) > 6) continue;
        bool st = a.station || b.station;
        Vec3 aL = a.p + a.r * a.L, aR = a.p + a.r * a.R, bL = b.p + b.r * b.L, bR = b.p + b.r * b.R;
        // vloer (bodem van de tunnel; het ballastbed tekent Railways)
        op.quad(Vec3(aL.x, a.y - 0.45f, aL.z), Vec3(bL.x, b.y - 0.45f, bL.z), Vec3(bR.x, b.y - 0.45f, bR.z), Vec3(aR.x, a.y - 0.45f, aR.z), Vec3(0, 1, 0),
                CONCRETE_DARK, EX_PLAIN);
        Vec3 mid = (a.p + b.p) * 0.5f;
        col.addFloor({mid.x + a.r.x * (a.L + a.R) / 2, mid.z + a.r.z * (a.L + a.R) / 2, a.d.x, a.d.z, 2.1f, (a.R - a.L) / 2, (a.y + b.y) / 2, (a.y + b.y) / 2, 0});
        // buitenwanden: tegels in het station, anders beton
        for (int side = 0; side < 2; side++) {
            Vec3 p0 = side ? aR : aL, p1 = side ? bR : bL;
            p0.y = 0;
            p1.y = 0;
            float c0 = a.ceil, c1 = b.ceil;
            if (st) {
                B.wall(p0, p1, a.y - 0.45f, a.y + 3.2f, b.y - 0.45f, b.y + 3.2f, TILE_BEIGE, EX_TILEWALL);
                B.wall(p0, p1, a.y + 3.2f, c0, b.y + 3.2f, c1, CONCRETE_DARK, EX_PLAIN);
            } else {
                B.wall(p0, p1, a.y - 0.45f, c0, b.y - 0.45f, c1, CONCRETE * 0.85f, EX_PLAIN);
            }
            B.solid(p0, p1, 0.4f, a.y - 1, c0);
            // groene lichtlijn (station) / tunnelverlichting
            if (st) B.beam(p0 + Vec3(0, a.y + 4.6f, 0) + (side ? a.r * -0.06f : a.r * 0.06f), p1 + Vec3(0, b.y + 4.6f, 0) + (side ? b.r * -0.06f : b.r * 0.06f), 0.05f, 0.09f, GREEN_LIGHT, EX_LIGHT);
            else if (k % 6 == 0) {
                Vec3 lp = p0 + Vec3(0, a.y + 4.8f, 0) + (side ? a.r * -0.1f : a.r * 0.1f);
                op.box(lp, {0.25f, 0.12f, 0.25f}, Vec3(1.0f, 0.95f, 0.85f), EX_LIGHT);
            }
        }
        // plafond (niet onder de hal: daar is de halvloer het plafond)
        if (!underHall(mid)) {
            op.quad(Vec3(aL.x, a.ceil, aL.z), Vec3(aR.x, a.ceil, aR.z), Vec3(bR.x, b.ceil, bR.z), Vec3(bL.x, b.ceil, bL.z), Vec3(0, -1, 0), st ? CEIL_DARK : CONCRETE * 0.7f, EX_PLAIN);
            if (st) {
                // geribbeld plafond: dwarsribben
                for (float t = 0; t < 1.0f; t += 0.5f) {
                    Vec3 l = aL + (bL - aL) * t, r = aR + (bR - aR) * t;
                    float y = a.ceil + (b.ceil - a.ceil) * t;
                    B.beam(Vec3(l.x, y - 0.25f, l.z), Vec3(r.x, y - 0.25f, r.z), 0.14f, 0.5f, RIB, EX_PLAIN);
                }
            }
        }
        // middenwanden tussen de tunnelbuizen, of blauwe kolommen waar een perron tussen ligt
        for (size_t i = 0; i + 1 < a.lats.size(); i++) {
            float gap = a.lats[i + 1] - a.lats[i];
            if (gap < 5.5f) continue;
            float m = (a.lats[i] + a.lats[i + 1]) / 2;
            bool platformBetween = false;
            for (auto& iv : a.plat)
                if (iv.first < m + 0.5f && iv.second > m - 0.5f) platformBetween = true;
            Vec3 p0 = a.p + a.r * m, p1 = b.p + b.r * m;
            if (!platformBetween) {
                for (float off : {-0.4f, 0.4f}) B.wall(p0 + a.r * off, p1 + b.r * off, a.y - 0.45f, a.ceil, b.y - 0.45f, b.ceil, CONCRETE * 0.8f, EX_PLAIN);
                B.solid(p0, p1, 0.8f, a.y - 1, a.ceil);
            } else if (k % 2 == 0) {
                float pf = platformY;
                op.box(Vec3(p0.x, (pf + a.ceil) / 2, p0.z), {0.55f, a.ceil - pf, 0.55f}, BLUE_STEEL, EX_PLAIN);
                col.addBox({p0.x, p0.z, a.d.x, a.d.z, 0.3f, 0.3f, pf - 1, a.ceil});
            }
        }
        // plafondlampen boven de perrons
        if (st && k % 2 == 0)
            for (auto& iv : a.plat) {
                Vec3 q = a.p + a.r * ((iv.first + iv.second) / 2);
                float y = underHall(q) ? hallY - 0.95f : a.ceil - 0.52f;
                op.box(Vec3(q.x, y, q.z), {0.5f, 0.05f, 0.5f}, DOWNLIGHT, EX_LIGHT);
            }
    }
    // ---------- tunnelmonden: gevel boven de opening en een deksel over de uitsparing in het terrein ----------
    for (const WorldFeature& f : world.features) {
        if (f.kind != FEAT_TUNNEL_PORTAL || f.pts.size() < 2) continue;
        Vec3 p = f.pts[0], q = f.pts[1];
        Vec3 d = normalize(Vec3(q.x - p.x, 0, q.z - p.z)), r(-d.z, 0, d.x);
        float l0 = f.a - 1.0f, l1 = f.b + 1.0f;
        float top = world.heightAt(p.x + d.x * 8, p.z + d.z * 8);
        float ceil = std::min(p.y + 6.4f, top - 0.5f);
        Vec3 A = p + r * l0, Bp = p + r * l1;
        // gevel (beide zijden zichtbaar)
        op.quad(Vec3(A.x, ceil, A.z), Vec3(Bp.x, ceil, Bp.z), Vec3(Bp.x, top + 0.9f, Bp.z), Vec3(A.x, top + 0.9f, A.z), d * -1.0f, PINK * 0.95f, EX_PLAIN);
        op.box(Vec3((A.x + Bp.x) / 2, top + 0.95f, (A.z + Bp.z) / 2) - d * 0.0f, {0.3f, 0.1f, 0.3f}, PINK, EX_PLAIN);
        B.beam(Vec3(A.x, top + 0.9f, A.z), Vec3(Bp.x, top + 0.9f, Bp.z), 0.4f, 0.12f, PINK * 0.9f, EX_PLAIN);
        // deksel: maaiveld boven de eerste meters tunnel
        Vec3 C = A + d * 5.0f, D = Bp + d * 5.0f;
        op.quad(Vec3(A.x, top, A.z), Vec3(C.x, top, C.z), Vec3(D.x, top, D.z), Vec3(Bp.x, top, Bp.z), Vec3(0, 1, 0), Vec3(0.42f, 0.44f, 0.36f), EX_PLAIN);
        col.addFloor({(A.x + D.x) / 2, (A.z + D.z) / 2, d.x, d.z, 2.5f, (l1 - l0) / 2, top, top, 0});
        // tunnelvloer een stukje naar buiten doortrekken (gat in het terrein)
        Vec3 E = A - d * 3.5f, F = Bp - d * 3.5f;
        op.quad(Vec3(E.x, p.y - 0.45f, E.z), Vec3(A.x, p.y - 0.45f, A.z), Vec3(Bp.x, p.y - 0.45f, Bp.z), Vec3(F.x, p.y - 0.45f, F.z), Vec3(0, 1, 0), CONCRETE_DARK, EX_PLAIN);
    }

    // ================= KEERWANDEN (open bak) =================
    for (const WorldFeature& f : world.features) {
        if (f.kind != FEAT_RETAINING_WALL) continue;
        for (size_t k = 0; k + 1 < f.pts.size(); k++) {
            Vec3 a = f.pts[k], b = f.pts[k + 1];
            Vec3 m = (a + b) * 0.5f;
            // dichtstbijzijnde spoor: niet in de overdekte tunnel, en de onderkant op spoorhoogte
            float best = 1e9f, ry = platformY - 0.8f;
            bool tunnel = false;
            for (const RailLine& r : world.rails)
                for (const RailPoint& p : r.pts) {
                    float dd = std::hypot(p.x - m.x, p.z - m.z);
                    if (dd < best) { best = dd; ry = p.y; tunnel = r.tunnel; }
                }
            if (tunnel || best > 30) continue;
            Vec3 d = b - a;
            float len = std::hypot(d.x, d.z);
            int n = std::max(1, (int)std::ceil(len / 4.0f));
            for (int s = 0; s < n; s++) {
                Vec3 p0 = a + d * ((float)s / n), p1 = a + d * ((float)(s + 1) / n);
                Vec3 nn = normalize(Vec3(-d.z, 0, d.x));
                float t0 = -1e9f, t1 = -1e9f;
                for (float o = -4; o <= 4; o += 1) {
                    t0 = std::max(t0, world.heightAt(p0.x + nn.x * o, p0.z + nn.z * o));
                    t1 = std::max(t1, world.heightAt(p1.x + nn.x * o, p1.z + nn.z * o));
                }
                if (t0 - ry < 1.0f && t1 - ry < 1.0f) continue;
                B.wall(p0, p1, ry - 0.6f, t0 + 0.3f, ry - 0.6f, t1 + 0.3f, PINK, EX_PLAIN);
                B.wall(p0 + nn * 0.4f, p1 + nn * 0.4f, ry - 0.6f, t0 + 0.3f, ry - 0.6f, t1 + 0.3f, PINK * 0.95f, EX_PLAIN);
                // rode band en geluidsscherm (betonpanelen) bovenop
                B.wall(p0 - nn * 0.01f, p1 - nn * 0.01f, ry + 1.2f, ry + 1.32f, ry + 1.2f, ry + 1.32f, RED_RAIL, EX_PLAIN);
                B.wall(p0 + nn * 0.2f, p1 + nn * 0.2f, t0 + 0.3f, t0 + 2.0f, t1 + 0.3f, t1 + 2.0f, Vec3(0.74f, 0.70f, 0.64f), EX_PLAIN);
                B.solid(p0 + nn * 0.2f, p1 + nn * 0.2f, 0.5f, ry - 1, t0 + 2.0f);
                op.tube(Vec3(p0.x, t0 + 0.3f, p0.z) + nn * 0.2f, Vec3(p0.x, t0 + 2.1f, p0.z) + nn * 0.2f, 0.06f, 6, Vec3(0.5f, 0.5f, 0.52f), EX_PLAIN);
            }
        }
    }

    // ================= TRAPPEN EN BORDESSEN =================
    std::vector<Flight> flights;
    for (const WorldFeature& f : world.features)
        if (f.kind == FEAT_STEPS && f.pts.size() >= 2 && std::hypot(f.pts.front().x - stationPt.x, f.pts.front().z - stationPt.z) < 120) {
            Flight fl;
            fl.a = f.pts.front();
            fl.b = f.pts.back();
            flights.push_back(fl);
        }
    // ketens: begin bij een trap met een uiteinde op een perron, volg aansluitende trappen (bordes) naar boven
    struct Chain { std::vector<int> fl; std::vector<bool> flip; };
    std::vector<Chain> chains;
    for (int i = 0; i < (int)flights.size(); i++) {
        Flight& f = flights[i];
        if (f.used) continue;
        bool aOn = onPlatform(f.a.x, f.a.z, 1.2f), bOn = onPlatform(f.b.x, f.b.z, 1.2f);
        if (aOn == bOn) continue;
        Chain c;
        c.fl.push_back(i);
        c.flip.push_back(bOn);  // flip: b is het lage uiteinde
        f.used = true;
        Vec3 top = bOn ? f.a : f.b;
        for (int guard = 0; guard < 4; guard++) {
            int next = -1;
            bool nflip = false;
            float bd = 4.5f;
            for (int j = 0; j < (int)flights.size(); j++) {
                if (flights[j].used) continue;
                float da = std::hypot(flights[j].a.x - top.x, flights[j].a.z - top.z), db = std::hypot(flights[j].b.x - top.x, flights[j].b.z - top.z);
                if (da < bd) { bd = da; next = j; nflip = false; }
                if (db < bd) { bd = db; next = j; nflip = true; }
            }
            if (next < 0) break;
            flights[next].used = true;
            c.fl.push_back(next);
            c.flip.push_back(nflip);
            top = nflip ? flights[next].a : flights[next].b;
        }
        chains.push_back(c);
    }
    std::vector<CBox> openings;  // gaten in de halvloer
    for (const Chain& c : chains) {
        int n = (int)c.fl.size();
        const OsmPlatform* pf = nullptr;
        {
            const Flight& f0 = flights[c.fl[0]];
            Vec3 lo = c.flip[0] ? f0.b : f0.a;
            pf = onPlatform(lo.x, lo.z, 1.2f);
        }
        float y0 = pf ? pf->height : platformY;
        float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
        Vec3 axis;
        for (int s = 0; s < n; s++) {
            const Flight& f = flights[c.fl[s]];
            Vec3 lo = c.flip[s] ? f.b : f.a, hi = c.flip[s] ? f.a : f.b;
            float ylo = y0 + (hallY - y0) * s / n, yhi = y0 + (hallY - y0) * (s + 1) / n;
            Vec3 d = hi - lo;
            float len = std::hypot(d.x, d.z);
            if (len < 0.5f) continue;
            Vec3 u = Vec3(d.x / len, 0, d.z / len), r(-u.z, 0, u.x);
            if (s == 0) axis = u;
            const float W = 2.2f;
            int steps = std::max(3, (int)std::round((yhi - ylo) / 0.175f));
            float run = len / steps, rise = (yhi - ylo) / steps;
            float hd = std::atan2(u.x, u.z);
            for (int i = 0; i < steps; i++) {
                Vec3 c0 = lo + u * (run * (i + 0.5f));
                float top = ylo + rise * (i + 1);
                op.orientedBox(Vec3(c0.x, (top + ylo - 0.3f) / 2, c0.z), {W, top - ylo + 0.3f, run + 0.01f}, hd, i % 2 ? STEP : STEP * 0.93f, EX_PLAIN);
                op.orientedBox(Vec3(c0.x, top + 0.003f, c0.z) - u * (run / 2 - 0.03f), {W, 0.006f, 0.05f}, hd, NSYEL * 0.9f, EX_PLAIN);
            }
            col.addFloor({(lo.x + hi.x) / 2, (lo.z + hi.z) / 2, u.x, u.z, len / 2, W / 2, ylo, yhi, 0});
            // glazen balustrade met rode leuning, wangen
            for (int side = -1; side <= 1; side += 2) {
                Vec3 o = r * (side * (W / 2 + 0.05f));
                Vec3 a0 = lo + o, a1 = hi + o;
                gl.quad(Vec3(a0.x, ylo + 0.1f, a0.z), Vec3(a1.x, yhi + 0.1f, a1.z), Vec3(a1.x, yhi + 1.05f, a1.z), Vec3(a0.x, ylo + 1.05f, a0.z), r * (float)side,
                        Vec3(0.55f, 0.65f, 0.68f), EX_GLASS);
                op.tube(Vec3(a0.x, ylo + 1.05f, a0.z), Vec3(a1.x, yhi + 1.05f, a1.z), 0.035f, 6, RED_RAIL, EX_PLAIN);
                B.wall(a0, a1, ylo - 0.35f, ylo + 0.1f, yhi - 0.35f, yhi + 0.1f, CONCRETE_DARK, EX_PLAIN);
                col.addBox({(a0.x + a1.x) / 2, (a0.z + a1.z) / 2, u.x, u.z, len / 2, 0.05f, ylo, yhi + 1.1f});
            }
            // bordes boven deze trap (behalve de bovenste)
            if (s + 1 < n) {
                const Flight& f2 = flights[c.fl[s + 1]];
                Vec3 lo2 = c.flip[s + 1] ? f2.b : f2.a;
                Vec3 m = (hi + lo2) * 0.5f;
                float span = std::hypot(hi.x - lo2.x, hi.z - lo2.z) + W + 0.6f;
                op.orientedBox(Vec3(m.x, yhi - 0.15f, m.z), {span, 0.3f, 2.4f}, hd + PI / 2, STEP * 0.95f, EX_PLAIN);
                Vec3 ax(std::cos(hd), 0, -std::sin(hd));
                col.addFloor({m.x, m.z, ax.x, ax.z, span / 2, 1.3f, yhi, yhi, 0});
                // blauwe stalen steunen
                op.box(Vec3(m.x, (y0 + yhi) / 2, m.z), {0.25f, yhi - y0, 0.25f}, BLUE_STEEL, EX_PLAIN);
            }
            for (Vec3 p : {lo, hi}) { minx = std::min(minx, p.x); maxx = std::max(maxx, p.x); minz = std::min(minz, p.z); maxz = std::max(maxz, p.z); }
        }
        // opening in de halvloer rond de hele trapketen (vide)
        if (axis.x != 0 || axis.z != 0) {
            Vec3 cc((minx + maxx) / 2, 0, (minz + maxz) / 2);
            float ha = 0, hb = 0;
            Vec3 r(-axis.z, 0, axis.x);
            for (int s = 0; s < n; s++) {
                const Flight& f = flights[c.fl[s]];
                for (Vec3 p : {f.a, f.b}) {
                    ha = std::max(ha, std::fabs((p.x - cc.x) * axis.x + (p.z - cc.z) * axis.z));
                    hb = std::max(hb, std::fabs((p.x - cc.x) * r.x + (p.z - cc.z) * r.z));
                }
            }
            openings.push_back({cc.x, cc.z, axis.x, axis.z, ha + 0.3f, hb + 1.6f, hallY - 0.5f, hallY + 0.5f});
        }
    }

    // ================= LIFTEN =================
    for (const WorldFeature& f : world.features) {
        if (f.kind != FEAT_ELEVATOR || f.pts.empty()) continue;
        Vec3 p = f.pts[0];
        if (std::hypot(p.x - stationPt.x, p.z - stationPt.z) > 120) continue;
        const OsmPlatform* pf = onPlatform(p.x, p.z, 4.0f);
        float yb = pf ? pf->height : platformY;
        // blauwe stalen schacht met glas, tot boven de halvloer
        float y1 = hallY + 3.2f;
        for (int cx = -1; cx <= 1; cx += 2)
            for (int cz = -1; cz <= 1; cz += 2) op.box(Vec3(p.x + cx * 0.95f, (yb + y1) / 2, p.z + cz * 0.95f), {0.14f, y1 - yb, 0.14f}, BLUE_STEEL, EX_PLAIN);
        for (float y = yb + 2.6f; y < y1; y += 2.6f) {
            op.box(Vec3(p.x, y, p.z + 0.95f), {2.0f, 0.1f, 0.1f}, BLUE_STEEL, EX_PLAIN);
            op.box(Vec3(p.x, y, p.z - 0.95f), {2.0f, 0.1f, 0.1f}, BLUE_STEEL, EX_PLAIN);
        }
        gl.box(Vec3(p.x, (yb + y1) / 2, p.z), {1.85f, y1 - yb, 1.85f}, Vec3(0.55f, 0.65f, 0.68f), EX_GLASS);
        op.box(Vec3(p.x, y1 + 0.1f, p.z), {2.1f, 0.2f, 2.1f}, BLUE_STEEL, EX_PLAIN);
        Elevator e;
        e.bottom = Vec3(p.x, yb, p.z);
        e.top = Vec3(p.x, hallY, p.z);
        e.label = "de stationshal";
        e.topLabel = "het perron";
        elevators.push_back(e);
        openings.push_back({p.x, p.z, 1, 0, 1.0f, 1.0f, hallY - 0.5f, hallY + 0.5f});
    }

    // ================= STATIONSHAL =================
    if (hallPoly.size() >= 3) {
        // vloer (boven en onderkant) met vides
        using P2 = std::array<float, 2>;
        std::vector<std::vector<P2>> poly(1);
        std::vector<Vec3> flat;
        for (auto& q : hallPoly) { poly[0].push_back({q.first, q.second}); flat.push_back(Vec3(q.first, 0, q.second)); }
        for (const CBox& o : openings) {
            Vec3 a(o.ax, 0, o.az), r(-o.az, 0, o.ax);
            Vec3 c(o.cx, 0, o.cz);
            Vec3 cs[4] = {c - a * o.ha - r * o.hb, c + a * o.ha - r * o.hb, c + a * o.ha + r * o.hb, c - a * o.ha + r * o.hb};
            bool inside = true;
            for (Vec3 q : cs) if (!inPoly(hallPoly, q.x, q.z)) inside = false;
            if (!inside) continue;
            std::vector<P2> h;
            for (Vec3 q : cs) { h.push_back({q.x, q.z}); flat.push_back(q); }
            poly.push_back(h);
            col.addCutout(o);
            // rand van de vide: glazen balustrade
            for (int k = 0; k < 4; k++) {
                Vec3 p0 = cs[k], p1 = cs[(k + 1) % 4];
                gl.quad(Vec3(p0.x, hallY, p0.z), Vec3(p1.x, hallY, p1.z), Vec3(p1.x, hallY + 1.05f, p1.z), Vec3(p0.x, hallY + 1.05f, p0.z), Vec3(0, 0, 1),
                        Vec3(0.55f, 0.65f, 0.68f), EX_GLASS);
                op.tube(Vec3(p0.x, hallY + 1.05f, p0.z), Vec3(p1.x, hallY + 1.05f, p1.z), 0.035f, 6, RED_RAIL, EX_PLAIN);
                op.box(Vec3((p0.x + p1.x) / 2, hallY - 0.45f, (p0.z + p1.z) / 2), {0.1f, 0.9f, 0.1f}, CONCRETE_DARK, EX_PLAIN);
            }
        }
        std::vector<uint32_t> idx = mapbox::earcut<uint32_t>(poly);
        for (int side = 0; side < 2; side++) {
            uint32_t base = (uint32_t)op.v.size();
            float y = side ? hallY - 0.9f : hallY;
            for (const Vec3& q : flat) op.vert(Vec3(q.x, y, q.z), Vec3(0, side ? -1.0f : 1.0f, 0), side ? CEIL_DARK : HALL_FLOOR, EX_PLAIN);
            for (uint32_t k : idx) op.i.push_back(base + k);
        }
        col.addPolyFloor(hallPoly, hallY);

        // hoofdassen van de hal
        float cx = 0, cz = 0;
        for (auto& q : hallPoly) { cx += q.first; cz += q.second; }
        cx /= hallPoly.size(); cz /= hallPoly.size();
        float sxx = 0, szz = 0, sxz = 0;
        for (auto& q : hallPoly) { float dx = q.first - cx, dz = q.second - cz; sxx += dx * dx; szz += dz * dz; sxz += dx * dz; }
        float ang = 0.5f * std::atan2(2 * sxz, sxx - szz);
        Vec3 U(std::cos(ang), 0, std::sin(ang)), V(-U.z, 0, U.x);
        auto toUV = [&](float x, float z, float& u, float& v) { u = (x - cx) * U.x + (z - cz) * U.z; v = (x - cx) * V.x + (z - cz) * V.z; };
        auto fromUV = [&](float u, float v) { return Vec3(cx + U.x * u + V.x * v, 0, cz + U.z * u + V.z * v); };
        // convexe omhullende (dakvorm), 2,5 m overstek
        std::vector<std::pair<float, float>> uv;
        for (auto& q : hallPoly) { float u, v; toUV(q.first, q.second, u, v); uv.push_back({u, v}); }
        std::sort(uv.begin(), uv.end());
        std::vector<std::pair<float, float>> hull;
        auto crossz = [](std::pair<float, float> o, std::pair<float, float> a, std::pair<float, float> b) {
            return (a.first - o.first) * (b.second - o.second) - (a.second - o.second) * (b.first - o.first);
        };
        for (int pass = 0; pass < 2; pass++) {
            size_t start = hull.size();
            for (size_t k = 0; k < uv.size(); k++) {
                auto p = pass ? uv[uv.size() - 1 - k] : uv[k];
                while (hull.size() >= start + 2 && crossz(hull[hull.size() - 2], hull.back(), p) <= 0) hull.pop_back();
                hull.push_back(p);
            }
            hull.pop_back();
        }
        float hcu = 0, hcv = 0;
        for (auto& p : hull) { hcu += p.first; hcv += p.second; }
        hcu /= hull.size(); hcv /= hull.size();
        std::vector<std::pair<float, float>> roofH;
        for (auto& p : hull) {
            float du = p.first - hcu, dv = p.second - hcv, l = std::hypot(du, dv);
            roofH.push_back({p.first + du / l * 2.5f, p.second + dv / l * 2.5f});
        }
        float vmin = 1e9f, vmax = -1e9f;
        for (auto& p : roofH) { vmin = std::min(vmin, p.second); vmax = std::max(vmax, p.second); }
        const float EAVE = 7.5f, CROWN = 10.0f;
        float vc = (vmin + vmax) / 2, vh = (vmax - vmin) / 2;
        auto roofY = [&](float v) { float t = (v - vc) / vh; return hallY + EAVE + (CROWN - EAVE) * (1 - t * t); };
        auto uRange = [&](float v, float& u0, float& u1) {
            u0 = 1e9f; u1 = -1e9f;
            for (size_t k = 0; k < roofH.size(); k++) {
                auto a = roofH[k], b = roofH[(k + 1) % roofH.size()];
                if ((a.second - v) * (b.second - v) > 0 || a.second == b.second) continue;
                float u = a.first + (b.first - a.first) * (v - a.second) / (b.second - a.second);
                u0 = std::min(u0, u); u1 = std::max(u1, u);
            }
            return u1 > u0;
        };
        // dak: stroken over v (gebogen in dwarsrichting), boven grijs en onder lichte lamellen
        const int NV = 16;
        for (int k = 0; k < NV; k++) {
            float va = vmin + (vmax - vmin) * k / NV + 0.01f, vb = vmin + (vmax - vmin) * (k + 1) / NV - 0.01f;
            float a0, a1, b0, b1;
            if (!uRange(va, a0, a1) || !uRange(vb, b0, b1)) continue;
            float ya = roofY(va), yb = roofY(vb);
            Vec3 pa0 = fromUV(a0, va), pa1 = fromUV(a1, va), pb0 = fromUV(b0, vb), pb1 = fromUV(b1, vb);
            Vec3 nup = normalize(Vec3(0, 1, 0) + V * (-(yb - ya) / (vb - va)));
            op.quad(Vec3(pa0.x, ya + 0.25f, pa0.z), Vec3(pa1.x, ya + 0.25f, pa1.z), Vec3(pb1.x, yb + 0.25f, pb1.z), Vec3(pb0.x, yb + 0.25f, pb0.z), nup, ROOF_TOP, EX_PLAIN);
            op.quad(Vec3(pa0.x, ya, pa0.z), Vec3(pa1.x, ya, pa1.z), Vec3(pb1.x, yb, pb1.z), Vec3(pb0.x, yb, pb0.z), nup * -1.0f, ROOF_UNDER, EX_PLAIN);
            // lichtlijnen onder het dak
            if (k % 3 == 1) {
                Vec3 m0 = fromUV(a0 + 1, (va + vb) / 2), m1 = fromUV(a1 - 1, (va + vb) / 2);
                B.beam(Vec3(m0.x, (ya + yb) / 2 - 0.08f, m0.z), Vec3(m1.x, (ya + yb) / 2 - 0.08f, m1.z), 0.15f, 0.04f, Vec3(1, 0.97f, 0.9f), EX_LIGHT);
            }
        }
        // dakrand (aluminium) en schuine witte kolommen met pieken
        float perim = 0;
        for (size_t k = 0; k < roofH.size(); k++) {
            auto a = roofH[k], b = roofH[(k + 1) % roofH.size()];
            Vec3 pa = fromUV(a.first, a.second), pb = fromUV(b.first, b.second);
            float ya = roofY(a.second), yb = roofY(b.second);
            op.quad(Vec3(pa.x, ya - 0.1f, pa.z), Vec3(pb.x, yb - 0.1f, pb.z), Vec3(pb.x, yb + 0.35f, pb.z), Vec3(pa.x, ya + 0.35f, pa.z),
                    normalize(Vec3(pb.z - pa.z, 0, pa.x - pb.x)), Vec3(0.78f, 0.80f, 0.82f), EX_PLAIN);
            float len = std::hypot(pb.x - pa.x, pb.z - pa.z);
            for (float s = std::fmod(7.0f - std::fmod(perim, 7.0f), 7.0f); s < len; s += 7.0f) {
                float t = s / len;
                float u = a.first + (b.first - a.first) * t, v = a.second + (b.second - a.second) * t;
                float du = u - hcu, dv = v - hcv, l = std::hypot(du, dv);
                Vec3 top = fromUV(u, v);
                Vec3 foot = fromUV(u - du / l * 3.4f, v - dv / l * 3.4f);
                float yt = roofY(v);
                op.tube(Vec3(foot.x, hallY, foot.z), Vec3(top.x, yt, top.z), 0.16f, 10, WHITE, EX_PLAIN);
                Vec3 dir = normalize(Vec3(top.x - foot.x, yt - hallY, top.z - foot.z));
                op.tube(Vec3(top.x, yt, top.z), Vec3(top.x, yt, top.z) + dir * 2.2f, 0.07f, 8, WHITE, EX_PLAIN);
                op.box(Vec3(foot.x, hallY + 0.4f, foot.z), {0.9f, 0.8f, 0.9f}, PINK, EX_PLAIN);  // roze sokkel
                col.addBox({foot.x, foot.z, 1, 0, 0.45f, 0.45f, hallY, hallY + 0.8f});
            }
            perim += len;
        }
        // gevels: roze plint + glas met blauwe stijlen; bakstenen trommel aan het einde met de grootste u; ingangen op lange gevels
        float umax = -1e9f;
        for (auto& p : uv) umax = std::max(umax, p.first);
        for (size_t k = 0; k < hallPoly.size(); k++) {
            auto a = hallPoly[k], b = hallPoly[(k + 1) % hallPoly.size()];
            Vec3 pa(a.first, 0, a.second), pb(b.first, 0, b.second);
            float len = std::hypot(pb.x - pa.x, pb.z - pa.z);
            if (len < 0.05f) continue;
            float ua, va, ub, vb;
            toUV(pa.x, pa.z, ua, va);
            toUV(pb.x, pb.z, ub, vb);
            bool brick = (ua + ub) / 2 > umax - 14.0f;
            float ya = roofY(va) - 0.05f, yb = roofY(vb) - 0.05f;
            // ingang: 3,6 m opening midden in lange glazen gevels
            float g0 = 2, g1 = -2;
            if (!brick && len > 9.0f) { g0 = len / 2 - 1.8f; g1 = len / 2 + 1.8f; }
            Vec3 dir = (pb - pa) * (1.0f / len);
            Vec3 nrm(dir.z, 0, -dir.x);
            std::vector<std::pair<float, float>> parts;
            if (g1 > g0) { parts.push_back({0, g0}); parts.push_back({g1, len}); } else parts.push_back({0, len});
            for (auto& pr : parts) {
                Vec3 p0 = pa + dir * pr.first, p1 = pa + dir * pr.second;
                float y0r = ya + (yb - ya) * pr.first / len, y1r = ya + (yb - ya) * pr.second / len;
                if (brick) {
                    B.wall(p0, p1, hallY, y0r, hallY, y1r, BRICK, EX_PLAIN);
                    B.wall(p0 - nrm * 0.3f, p1 - nrm * 0.3f, hallY, y0r, hallY, y1r, BRICK * 0.9f, EX_PLAIN);
                } else {
                    B.wall(p0, p1, hallY, hallY + 1.0f, hallY, hallY + 1.0f, PINK, EX_PLAIN);
                    gl.quad(Vec3(p0.x, hallY + 1.0f, p0.z), Vec3(p1.x, hallY + 1.0f, p1.z), Vec3(p1.x, y1r, p1.z), Vec3(p0.x, y0r, p0.z), nrm,
                            Vec3(0.45f, 0.58f, 0.66f), EX_GLASS);
                    for (float s = 0; s <= pr.second - pr.first; s += 1.6f) {
                        Vec3 m = p0 + dir * s;
                        float y = y0r + (y1r - y0r) * s / std::max(0.01f, pr.second - pr.first);
                        op.box(Vec3(m.x, (hallY + y) / 2, m.z), {0.08f, y - hallY, 0.08f}, NSBLUE * 1.4f, EX_PLAIN);
                    }
                }
                B.solid(p0, p1, 0.3f, hallY - 0.5f, std::max(y0r, y1r));
            }
            if (g1 > g0) {
                // poortjes (OV-chipkaart) in de ingang
                for (int gi = 0; gi < 4; gi++) {
                    Vec3 m = pa + dir * (g0 + 0.45f + gi * 0.9f) - nrm * 1.2f;
                    op.box(Vec3(m.x, hallY + 0.5f, m.z), {0.25f, 1.0f, 0.25f}, Vec3(0.75f, 0.76f, 0.78f), EX_PLAIN);
                    op.box(Vec3(m.x, hallY + 1.02f, m.z), {0.2f, 0.04f, 0.2f}, NSYEL, EX_LIGHT);
                }
                // boven de ingang een bord
                SignRect sr = atlas.add("Best", NSBLUE, Vec3(1, 1, 1), 72);
                Vec3 m = pa + dir * ((g0 + g1) / 2) + nrm * 0.1f;
                SignAtlas::quad(op, sr, Vec3(m.x, hallY + 3.2f, m.z), dir, 0.6f, true);
            }
        }
        // klok op de bakstenen trommel en de luifel ernaast
        {
            Vec3 cpos = fromUV(umax - 6.0f, vmax - 2.5f);
            Vec3 out = normalize(V);
            op.tube(Vec3(cpos.x, hallY + 5.2f, cpos.z) + out * 0.2f, Vec3(cpos.x, hallY + 5.2f, cpos.z) + out * 0.35f, 0.75f, 20, WHITE, EX_LIGHT);
            op.box(Vec3(cpos.x, hallY + 5.2f, cpos.z) + out * 0.37f, {0.05f, 0.5f, 0.05f}, Vec3(0.05f, 0.05f, 0.05f), EX_PLAIN);
            Vec3 lpos = fromUV(umax - 18.0f, vmax + 1.0f);
            op.tube(Vec3(lpos.x, hallY + 4.2f, lpos.z), Vec3(lpos.x, hallY + 4.6f, lpos.z), 4.5f, 24, Vec3(0.72f, 0.74f, 0.76f), EX_PLAIN);
            op.box(Vec3(lpos.x, hallY + 4.6f, lpos.z), {6.5f, 0.05f, 6.5f}, Vec3(0.72f, 0.74f, 0.76f), EX_PLAIN);
            SignRect ns = atlas.add("NS", Vec3(0.98f, 0.98f, 0.98f), NSBLUE, 96, true, 0.3f);
            Vec3 npos = fromUV(umax - 24.0f, vmax + 0.3f);
            SignAtlas::quad(op, ns, Vec3(npos.x, hallY + 6.3f, npos.z), U * -1.0f, 0.9f, true);
        }
        // inrichting: banken, kaartautomaten, vertrekstaat
        {
            Vec3 c = fromUV(-6, 0);
            for (int k = 0; k < 3; k++) {
                Vec3 m = fromUV(-10.0f + k * 4.0f, (vmin + vc) / 2);
                if (!inPoly(hallPoly, m.x, m.z)) continue;
                op.orientedBox(Vec3(m.x, hallY + 0.45f, m.z), {1.8f, 0.08f, 0.5f}, ang, Vec3(0.45f, 0.30f, 0.18f), EX_PLAIN);
                op.orientedBox(Vec3(m.x, hallY + 0.22f, m.z), {0.1f, 0.44f, 0.4f}, ang, Vec3(0.3f, 0.3f, 0.32f), EX_PLAIN);
            }
            if (inPoly(hallPoly, c.x, c.z)) {
                op.box(Vec3(c.x, hallY + 0.85f, c.z), {0.8f, 1.7f, 0.5f}, NSYEL, EX_PLAIN);
                op.box(Vec3(c.x, hallY + 1.3f, c.z) + V * 0.26f, {0.5f, 0.35f, 0.02f}, Vec3(0.1f, 0.2f, 0.35f), EX_LIGHT);
                col.addBox({c.x, c.z, 1, 0, 0.45f, 0.35f, hallY, hallY + 1.7f});
            }
        }
    }
    // ================= TUNNELDETAILS =================
    // doorlopende lichtlijnen, wandribben (segmentvoegen), kabelgoten, vluchtpad met leuning, nooduitgangen,
    // plafondvoegen; in het stationsdeel luidsprekers en camera's aan de kolommen.
    {
        float run = 0;
        float nextExit = 60;
        for (size_t k = 0; k + 1 < secs.size(); k++) {
            const Sec& a = secs[k];
            const Sec& b = secs[k + 1];
            float seg = std::hypot(b.p.x - a.p.x, b.p.z - a.p.z);
            if (seg > 6) continue;
            run += seg;
            bool st = a.station || b.station;
            for (int side = 0; side < 2; side++) {
                float la = side ? a.R : a.L, lb = side ? b.R : b.L;
                float in = side ? -1.0f : 1.0f;  // naar binnen
                Vec3 p0 = a.p + a.r * la, p1 = b.p + b.r * lb;
                p0.y = 0; p1.y = 0;
                Vec3 i0 = a.r * in, i1 = b.r * in;
                if (!st) {
                    // lichtlijn (armaturen in een doorlopende rij) en kabelgoten
                    B.beam(p0 + i0 * 0.12f + Vec3(0, a.y + 4.9f, 0), p1 + i1 * 0.12f + Vec3(0, b.y + 4.9f, 0), 0.10f, 0.06f, Vec3(1.0f, 0.96f, 0.86f), EX_LIGHT);
                    B.beam(p0 + i0 * 0.10f + Vec3(0, a.y + 5.0f, 0), p1 + i1 * 0.10f + Vec3(0, b.y + 5.0f, 0), 0.16f, 0.10f, Vec3(0.55f, 0.56f, 0.58f), EX_PLAIN);
                    for (float h : {1.35f, 1.65f})
                        B.beam(p0 + i0 * 0.12f + Vec3(0, a.y + h, 0), p1 + i1 * 0.12f + Vec3(0, b.y + h, 0), 0.22f, 0.07f, Vec3(0.30f, 0.31f, 0.32f), EX_PLAIN);
                    // vluchtpad langs de wand met leuning
                    Vec3 w0 = p0 + i0 * 0.45f, w1 = p1 + i1 * 0.45f;
                    B.beam(w0 + Vec3(0, a.y - 0.05f, 0), w1 + Vec3(0, b.y - 0.05f, 0), 0.9f, 0.5f, CONCRETE * 0.95f, EX_PLAIN);
                    op.tube(p0 + i0 * 0.08f + Vec3(0, a.y + 1.0f, 0), p1 + i1 * 0.08f + Vec3(0, b.y + 1.0f, 0), 0.025f, 5, Vec3(0.75f, 0.76f, 0.78f), EX_PLAIN);
                    // wandribben om de 8 m
                    if (k % 2 == 0) {
                        Vec3 m = p0 + i0 * 0.12f;
                        op.box(Vec3(m.x, (a.y - 0.45f + a.ceil) / 2, m.z), {0.24f, a.ceil - a.y + 0.45f, 0.24f}, CONCRETE * 0.78f, EX_PLAIN);
                    }
                }
                // nooduitgang: groen kozijn, deur, verlicht bord, noodtelefoon
                if (!st && side == (int)(run / 150) % 2 && run > nextExit) {
                    Vec3 m = p0 + i0 * 0.03f;
                    float hd = std::atan2(a.d.x, a.d.z);
                    op.orientedBox(Vec3(m.x, a.y + 1.15f, m.z), {0.06f, 2.3f, 1.2f}, hd, Vec3(0.10f, 0.45f, 0.20f), EX_PLAIN);
                    op.orientedBox(Vec3(m.x, a.y + 1.15f, m.z) + i0 * 0.02f, {0.04f, 2.1f, 1.0f}, hd, Vec3(0.75f, 0.76f, 0.74f), EX_PLAIN);
                    op.orientedBox(Vec3(m.x, a.y + 2.65f, m.z) + i0 * 0.05f, {0.08f, 0.3f, 0.8f}, hd, Vec3(0.15f, 0.95f, 0.35f), EX_LIGHT);
                    op.orientedBox(Vec3(m.x, a.y + 1.3f, m.z) + i0 * 0.06f + a.d * 1.6f, {0.12f, 0.6f, 0.4f}, hd, Vec3(0.95f, 0.50f, 0.05f), EX_PLAIN);
                    nextExit = run + 150;
                }
            }
            // middenwanden tussen de tunnelbuizen: aan beide kanten lichtlijn, kabelgoten, ribben
            if (!st)
                for (size_t i = 0; i + 1 < a.lats.size() && i + 1 < b.lats.size(); i++) {
                    if (a.lats[i + 1] - a.lats[i] < 5.5f) continue;
                    float ma = (a.lats[i] + a.lats[i + 1]) / 2, mb2 = (b.lats[i] + b.lats[i + 1]) / 2;
                    for (int s = -1; s <= 1; s += 2) {
                        Vec3 p0 = a.p + a.r * (ma + s * 0.42f), p1 = b.p + b.r * (mb2 + s * 0.42f);
                        p0.y = 0; p1.y = 0;
                        B.beam(p0 + Vec3(0, a.y + 4.9f, 0), p1 + Vec3(0, b.y + 4.9f, 0), 0.10f, 0.06f, Vec3(1.0f, 0.96f, 0.86f), EX_LIGHT);
                        for (float h : {1.35f, 1.65f})
                            B.beam(p0 + a.r * (s * 0.1f) + Vec3(0, a.y + h, 0), p1 + b.r * (s * 0.1f) + Vec3(0, b.y + h, 0), 0.2f, 0.07f, Vec3(0.30f, 0.31f, 0.32f), EX_PLAIN);
                        if (k % 2 == 0)
                            op.box(Vec3(p0.x, (a.y - 0.45f + a.ceil) / 2, p0.z) + a.r * (s * 0.1f), {0.22f, a.ceil - a.y + 0.45f, 0.22f}, CONCRETE * 0.72f, EX_PLAIN);
                        // seinbord / kilometerbord om de 200 m
                        if (k % 50 == 25) {
                            Vec3 q = p0 + a.r * (s * 0.06f) + Vec3(0, a.y + 2.4f, 0);
                            op.box(q, {0.08f, 0.5f, 0.4f}, Vec3(0.92f, 0.92f, 0.90f), EX_PLAIN);
                            op.box(q + a.r * (s * 0.045f), {0.01f, 0.3f, 0.25f}, Vec3(0.05f, 0.05f, 0.05f), EX_PLAIN);
                        }
                    }
                }
            // plafondvoegen in de tunnel
            if (!st && k % 2 == 0 && !underHall(a.p)) {
                Vec3 l = a.p + a.r * a.L, r = a.p + a.r * a.R;
                B.beam(Vec3(l.x, a.ceil - 0.08f, l.z), Vec3(r.x, a.ceil - 0.08f, r.z), 0.25f, 0.16f, CONCRETE * 0.62f, EX_PLAIN);
            }
        }
    }

    // ================= PERRONS: rand, geleidelijn, meubilair, borden =================
    for (const OsmPlatform* pf : plats) {
        const Poly& P = pf->pts;
        float y = pf->height;
        int trackNo = pf->name.rfind("Spoor ", 0) == 0 ? atoi(pf->name.c_str() + 6) : 0;
        // randen langs het spoor
        for (size_t k = 0; k < P.size(); k++) {
            Vec3 a(P[k].first, 0, P[k].second), b(P[(k + 1) % P.size()].first, 0, P[(k + 1) % P.size()].second);
            Vec3 d = b - a;
            float len = std::hypot(d.x, d.z);
            if (len < 3) continue;
            Vec3 u = d * (1.0f / len), n(-u.z, 0, u.x);
            Vec3 m = (a + b) * 0.5f;
            if (inPoly(P, m.x + n.x * 0.5f, m.z + n.z * 0.5f)) n = n * -1.0f;  // n naar buiten
            float best = 1e9f;
            for (const RailLine& r : world.rails)
                for (size_t q = 0; q < r.pts.size(); q += 2) {
                    float dd = std::hypot(r.pts[q].x - (m.x + n.x * 1.65f), r.pts[q].z - (m.z + n.z * 1.65f));
                    best = std::min(best, dd);
                }
            if (best > 2.0f) continue;
            float hd = std::atan2(u.x, u.z);
            // rand (lichte band), witte geleidelijn met ribbels op 0,9 m van de rand
            op.orientedBox(m - n * 0.15f + Vec3(0, y + 0.004f, 0), {0.30f, 0.01f, len}, hd, Vec3(0.86f, 0.85f, 0.80f), EX_PLAIN);
            op.orientedBox(m - n * 0.95f + Vec3(0, y + 0.006f, 0), {0.60f, 0.012f, len}, hd, Vec3(0.88f, 0.88f, 0.86f), EX_PLAIN);
            for (float s = 0.15f; s < len; s += 0.6f) {
                Vec3 q = a + u * s - n * 0.95f;
                op.orientedBox(q + Vec3(0, y + 0.014f, 0), {0.6f, 0.01f, 0.05f}, hd, Vec3(0.72f, 0.72f, 0.70f), EX_PLAIN);
            }
        }
        // hoofdas en middellijn van het perron
        float cx = 0, cz = 0;
        for (auto& q : P) { cx += q.first; cz += q.second; }
        cx /= P.size(); cz /= P.size();
        float sxx = 0, szz = 0, sxz = 0;
        for (auto& q : P) { float dx = q.first - cx, dz = q.second - cz; sxx += dx * dx; szz += dz * dz; sxz += dx * dz; }
        float ang = 0.5f * std::atan2(2 * sxz, sxx - szz);
        Vec3 ax(std::cos(ang), 0, std::sin(ang)), rx(-ax.z, 0, ax.x);
        float t0 = 1e9f, t1 = -1e9f;
        for (auto& q : P) {
            float t = (q.first - cx) * ax.x + (q.second - cz) * ax.z;
            t0 = std::min(t0, t); t1 = std::max(t1, t);
        }
        float hd = std::atan2(ax.x, ax.z);
        int idx = 0;
        for (float t = t0 + 12; t < t1 - 10; t += 14, idx++) {
            // middellijn: midden tussen de randen op deze plek
            float l0 = 1e9f, l1 = -1e9f;
            for (float l = -12; l <= 12; l += 0.25f) {
                Vec3 q = Vec3(cx, 0, cz) + ax * t + rx * l;
                if (inPoly(P, q.x, q.z)) { l0 = std::min(l0, l); l1 = std::max(l1, l); }
            }
            if (l1 - l0 < 2.6f) continue;
            Vec3 c = Vec3(cx, 0, cz) + ax * t + rx * ((l0 + l1) / 2);
            // niet bij trappen, liften en kolommen
            bool blocked = false;
            for (const Flight& f : flights)
                for (float s = 0; s <= 1; s += 0.25f) {
                    Vec3 q = f.a + (f.b - f.a) * s;
                    if (std::hypot(q.x - c.x, q.z - c.z) < 4.0f) blocked = true;
                }
            for (const Elevator& e : elevators)
                if (std::hypot(e.bottom.x - c.x, e.bottom.z - c.z) < 4.0f) blocked = true;
            if (blocked) continue;
            bool covered = world.heightAt(c.x, c.z) > y + 4.0f || underHall(c);
            int kind = idx % 3;
            if (kind == 0 || kind == 2) {
                // bank (metaal/hout) met afvalbak
                op.orientedBox(c + Vec3(0, y + 0.45f, 0), {0.48f, 0.06f, 2.0f}, hd, Vec3(0.55f, 0.36f, 0.20f), EX_PLAIN);
                op.orientedBox(c + rx * 0.22f + Vec3(0, y + 0.75f, 0), {0.05f, 0.5f, 2.0f}, hd, Vec3(0.55f, 0.36f, 0.20f), EX_PLAIN);
                for (float s : {-0.85f, 0.85f}) op.orientedBox(c + ax * s + Vec3(0, y + 0.22f, 0), {0.42f, 0.44f, 0.08f}, hd, Vec3(0.25f, 0.26f, 0.28f), EX_PLAIN);
                col.addBox({c.x, c.z, ax.x, ax.z, 1.0f, 0.3f, y, y + 0.5f});
                Vec3 bin = c + ax * 1.6f;
                op.tube(bin + Vec3(0, y, 0), bin + Vec3(0, y + 0.85f, 0), 0.22f, 10, Vec3(0.42f, 0.44f, 0.46f), EX_PLAIN);
                op.tube(bin + Vec3(0, y + 0.85f, 0), bin + Vec3(0, y + 0.9f, 0), 0.24f, 10, Vec3(0.15f, 0.15f, 0.16f), EX_PLAIN);
            } else if (trackNo > 0) {
                // informatiezuil met spoornummer en bestemmingen (blauw/wit), dubbelzijdig
                std::string txt = trackNo == 1 ? "Sprinter  's-Hertogenbosch / Tilburg Universiteit"
                                : trackNo == 4 ? "Sprinter  Eindhoven Centraal / Deurne / Weert"
                                               : "Intercity's rijden door";
                op.tube(c + Vec3(0, y, 0), c + Vec3(0, y + 2.9f, 0), 0.06f, 8, Vec3(0.45f, 0.47f, 0.50f), EX_PLAIN);
                SignRect nr = atlas.add(std::to_string(trackNo), Vec3(1, 1, 1), NSBLUE, 96, true, 0.5f);
                SignAtlas::quad(op, nr, c + Vec3(0, y + 2.85f, 0), ax, 0.42f, true);
                SignRect sr = atlas.add(txt, NSBLUE, Vec3(1, 1, 1), 48, true, 0.3f);
                SignAtlas::quad(op, sr, c + Vec3(0, y + 2.42f, 0), ax, 0.24f, true);
                // kaartlezer/omroepzuil
                op.box(c + rx * 0.3f + Vec3(0, y + 0.6f, 0), {0.18f, 1.2f, 0.18f}, Vec3(0.80f, 0.81f, 0.82f), EX_PLAIN);
                op.box(c + rx * 0.3f + Vec3(0, y + 1.25f, 0), {0.2f, 0.1f, 0.2f}, NSYEL, EX_LIGHT);
            }
            if (!covered && idx % 2 == 1) {
                // lantaarn op het open perrondeel
                Vec3 lp = c + rx * ((l1 - l0) / 2 - 1.2f);
                op.tube(lp + Vec3(0, y, 0), lp + Vec3(0, y + 5.0f, 0), 0.07f, 8, Vec3(0.35f, 0.36f, 0.38f), EX_PLAIN);
                op.box(lp + Vec3(0, y + 5.0f, 0) - rx * 0.35f, {0.7f, 0.08f, 0.25f}, Vec3(0.35f, 0.36f, 0.38f), EX_PLAIN);
                op.box(lp + Vec3(0, y + 4.95f, 0) - rx * 0.45f, {0.45f, 0.03f, 0.2f}, Vec3(1.0f, 0.96f, 0.85f), EX_LIGHT);
            } else if (covered) {
                // camera en luidspreker aan het plafond
                float cy = underHall(c) ? hallY - 1.0f : world.heightAt(c.x, c.z) > y + 4 ? y + 4.6f : y + 4.0f;
                op.box(c + Vec3(0, cy, 0) + ax * 3.0f, {0.25f, 0.18f, 0.32f}, Vec3(0.85f, 0.85f, 0.86f), EX_PLAIN);
                op.box(c + Vec3(0, cy - 0.15f, 0) - ax * 3.0f, {0.3f, 0.3f, 0.2f}, Vec3(0.2f, 0.2f, 0.22f), EX_PLAIN);
            }
        }
    }

    // naamborden ondergronds tegen de wanden (om de 25 m)
    for (size_t k = 0; k < secs.size(); k += 6) {
        const Sec& s = secs[k];
        if (!s.station) continue;
        for (int side = 0; side < 2; side++) {
            float l = side ? s.R - 0.05f : s.L + 0.05f;
            Vec3 p = s.p + s.r * l;
            SignRect sr = atlas.add("Best", NSBLUE, Vec3(1, 1, 1), 72);
            SignAtlas::quad(op, sr, Vec3(p.x, s.y + 3.6f, p.z), s.d, 0.45f, true);
        }
    }
}
