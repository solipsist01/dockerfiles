#include "traffic.h"
#include <algorithm>
#include <cstdio>
#include <map>

using namespace WorldFormat;

namespace {
const float TRACK_V[7] = {0, -30.68f, -11.73f, -7.23f, 7.99f, 12.94f, 30.37f};  // sporen Eindhoven Centraal (lokaal v)
const float SAMPLE = 2.0f;        // afstand tussen beveiligingsmonsters (m)
const float CLEAR = 2.9f;         // minimale afstand tussen treinen op verschillende sporen (hart-hart)
const float YARD_ZONE = 900.0f;   // emplacementen Eindhoven en 's-Hertogenbosch: 80 km/u
const float EXTRA = 1500.0f;      // pad doortrekken buiten de spoordata (de mist in)
const float LINE_MAX = 38.9f;     // baanvaksnelheid Eindhoven - Boxtel - 's-Hertogenbosch: 140 km/u
const char* CS = "Eindhoven Centraal";

Vec3 csPoint(int track, float u) {
    float x, z;
    stationToGame(u, TRACK_V[track], x, z);
    return Vec3(x, 0, z);
}
Vec3 rdPoint(double x, double y) { return Vec3(gameX(x), 0, gameZ(y)); }

TrackPath reversed(const TrackPath& p) {
    TrackPath r;
    r.pts.assign(p.pts.rbegin(), p.pts.rend());
    r.build();
    return r;
}

// pad aan het begin en/of eind recht doortrekken (buiten de detailtegels liggen geen sporen)
void extrapolate(TrackPath& p, bool atStart, bool atEnd) {
    if (p.pts.size() < 3) return;
    auto dirOf = [&](const Vec3& a, const Vec3& b) {
        Vec3 d = b - a;
        float l = std::hypot(d.x, d.z);
        return l > 1e-3f ? Vec3(d.x / l, 0, d.z / l) : Vec3(1, 0, 0);
    };
    if (atEnd) {
        size_t n = p.pts.size();
        Vec3 last = p.pts[n - 1], d = dirOf(p.pts[n >= 8 ? n - 8 : 0], last);
        for (float s = 25; s <= EXTRA; s += 25) p.pts.push_back(Vec3(last.x + d.x * s, last.y, last.z + d.z * s));
    }
    if (atStart) {
        Vec3 first = p.pts[0], d = dirOf(p.pts[std::min<size_t>(p.pts.size() - 1, 7)], first);
        std::vector<Vec3> pre;
        for (float s = EXTRA; s >= 25; s -= 25) pre.push_back(Vec3(first.x + d.x * s, first.y, first.z + d.z * s));
        p.pts.insert(p.pts.begin(), pre.begin(), pre.end());
    }
    p.build();
}

TrackPath concat(const TrackPath& a, const TrackPath& b) {
    TrackPath r = a;
    for (size_t k = 0; k < b.pts.size(); k++) {
        if (k == 0 && !r.pts.empty() && std::hypot(r.pts.back().x - b.pts[0].x, r.pts.back().z - b.pts[0].z) < 0.5f) continue;
        r.pts.push_back(b.pts[k]);
    }
    r.build();
    return r;
}

Vec3 dirAt(const TrackPath& p, float d) {
    Vec3 a = p.pos(d - 10), b = p.pos(d + 10);
    return b - a;
}

// Route langs vaste punten, met rechts-rijden-tussenpunten (rank 0 = buitenspoor, 1 = middenspoor)
// buiten de beschermde zones (emplacementen, aftakkingen).
bool smartRoute(const Railways& rail, const std::vector<Vec3>& fixed, Vec3 dir, int rank, float extend,
                const std::vector<std::pair<Vec3, float>>& protect, TrackPath& out) {
    std::vector<bool> req(fixed.size(), true);
    TrackPath base;
    if (!rail.routeSmart(fixed, req, dir.x, dir.z, base, extend)) return false;
    struct Pt { float d; Vec3 p; bool req; };
    std::vector<Pt> pts;
    for (const Vec3& f : fixed) pts.push_back({base.closest(f.x, f.z), f, true});
    pts[0].d = -1;  // eerste punt blijft eerst
    for (size_t k = 1; k < pts.size(); k++) pts[k].d = std::max(pts[k].d, pts[k - 1].d + 0.01f);
    for (float d = 100; d < base.length - 30; d += 200) {
        Vec3 p = base.pos(d);
        bool skip = false;
        for (auto& pr : protect)
            if (std::hypot(p.x - pr.first.x, p.z - pr.first.z) < pr.second) skip = true;
        for (const Vec3& f : fixed)
            if (std::hypot(p.x - f.x, p.z - f.z) < 120) skip = true;
        if (skip) continue;
        Vec3 r;
        if (rail.rankedTrack(p, dirAt(base, d), 30.0f, rank, r)) pts.push_back({d, r, false});
    }
    std::stable_sort(pts.begin(), pts.end(), [](const Pt& a, const Pt& b) { return a.d < b.d; });
    std::vector<Vec3> all;
    std::vector<bool> rq;
    for (auto& p : pts) { all.push_back(p.p); rq.push_back(p.req); }
    if (!rail.routeSmart(all, rq, dir.x, dir.z, out, extend)) out = base;
    return true;
}

// Rijtijd (numeriek, zoals Train::update) van het treinmidden van d0 naar d1, met kruissnelheid vcap
struct SimResult { double t; float v; };
SimResult simulate(const RollingStock& rs, const std::vector<SpeedZone>& zones, float half, float d0, float d1, float v0, bool stopAtEnd,
                   float vcap, const std::vector<float>& marks = {}, std::vector<double>* markT = nullptr) {
    const float dt = 0.25f;
    float d = d0, v = v0;
    double t = 0;
    size_t mk = 0;
    if (markT) markT->assign(marks.size(), 0);
    for (int guard = 0; guard < 800000 && d < d1 - 0.02f; guard++) {
        float rem = stopAtEnd ? d1 - d : 1e9f;
        float lim = std::min(std::min(rs.vmax, vcap), std::sqrt(2 * rs.dec * std::max(0.0f, rem)));
        for (const SpeedZone& z : zones) {
            if (d + half >= z.d0 && d - half <= z.d1) lim = std::min(lim, z.v);
            else if (z.d0 > d + half) lim = std::min(lim, std::sqrt(z.v * z.v + 2 * rs.dec * (z.d0 - d - half)));
        }
        v = v < lim ? std::min(v + rs.acc * dt, lim) : std::max(lim, v - rs.dec * 1.6f * dt);
        float step = std::min(std::max(v, 0.3f) * dt, d1 - d);
        d += step;
        t += dt;
        while (markT && mk < marks.size() && d >= marks[mk]) (*markT)[mk++] = t;
    }
    return {t, v};
}
}  // namespace

Traffic::~Traffic() {
    for (Train* t : active_) delete t;
}

int Traffic::nameIndex(const std::string& n) {
    for (size_t i = 0; i < names_.size(); i++)
        if (names_[i] == n) return (int)i;
    names_.push_back(n);
    return (int)names_.size() - 1;
}

// ============================ routes ============================
bool Traffic::buildPattern(Pattern& p, const World& world, const Railways& rail, const Stations& st, std::string& err) {
    const Def& df = p.def;
    // Intercity's op de middensporen; richting Tilburg rijden ze over dezelfde sporen als de Sprinters (aftakking Boxtel)
    const bool tilburgLine = df.exit == TILBURG || df.origin == TILBURG;
    const int rank = p.rs->product == "Intercity" && !tilburgLine ? 1 : 0;
    const bool north = p.north;
    (void)st;
    auto stationPt = [&](const std::string& name, Vec3& out) {
        for (const MapPoint& m : world.points)
            if (m.kind == POINT_STATION && m.name == name) { out = Vec3(m.x, 0, m.z); return true; }
        return false;
    };
    // perronspoor Eindhoven Centraal: vanaf het stoppunt het spoor volgen naar oost en west
    Vec3 csStop = csPoint(df.csTrack, 40), csE = csPoint(df.csTrack, 130), csW = csPoint(df.csTrack, -50);
    {
        TrackPath t;
        Vec3 a = csPoint(df.csTrack, 55), b = csPoint(df.csTrack, 25);
        if (rail.routeVia({csStop, a}, a.x - csStop.x, a.z - csStop.z, t, 90)) csE = t.pts.back();
        if (rail.routeVia({csStop, b}, b.x - csStop.x, b.z - csStop.z, t, 90)) csW = t.pts.back();
    }
    const int NR = sizeof(ROUTE_PTS) / sizeof(ROUTE_PTS[0]);
    const Vec3 tilburg = rdPoint(148058, 399976), tilburgMid = rdPoint(149265, 400058);  // op het spoor naar Tilburg (OSM)
    Vec3 dbC, boxC;
    stationPt("'s-Hertogenbosch", dbC);
    stationPt("Boxtel", boxC);
    bool toTilburg = df.exit == TILBURG || df.origin == TILBURG;
    bool viaDB = df.exit == NORTH || df.exit == DENBOSCH || df.origin == NORTH || df.origin == DENBOSCH;

    // ---- voorlopige route langs de corridor (voor de rijrichting bij de stations) ----
    // (langs de stations zelf; tussenpunten zouden op een andere lijn kunnen liggen)
    std::vector<Vec3> cor;
    std::vector<bool> creq;
    for (const char* name : {"Eindhoven Strijp-S", "Best", "Boxtel", "Vught", "'s-Hertogenbosch"}) {
        if (!viaDB && std::string(name) == "Boxtel") break;  // richting Tilburg: Boxtel via het aftakpunt
        Vec3 c;
        if (stationPt(name, c)) { cor.push_back(c); creq.push_back(false); }
    }
    (void)NR;
    if (toTilburg) { cor.push_back(tilburgMid); creq.push_back(false); cor.push_back(tilburg); creq.push_back(true); }
    std::vector<Vec3> base0pts;
    std::vector<bool> base0req;
    if (north) {
        base0pts = {csE, csStop, csW};
        base0req = {true, true, true};
        base0pts.insert(base0pts.end(), cor.begin(), cor.end());
        base0req.insert(base0req.end(), creq.begin(), creq.end());
    } else {
        std::reverse(cor.begin(), cor.end());
        std::reverse(creq.begin(), creq.end());
        if (df.origin == DENBOSCH && df.linkFrom >= 0) cor.insert(cor.begin(), patterns_[df.linkFrom]->terminal), creq.insert(creq.begin(), true);
        base0pts = cor;
        base0req = creq;
        base0req[0] = true;
        for (Vec3 q : {csW, csStop, csE}) { base0pts.push_back(q); base0req.push_back(true); }
    }
    Vec3 d0 = base0pts[1] - base0pts[0];
    if (df.origin == DENBOSCH && df.linkFrom >= 0) d0 = rdPoint(ROUTE_PTS[24][0], ROUTE_PTS[24][1]) - base0pts[0];
    TrackPath base0;
    if (!rail.routeSmart(base0pts, base0req, d0.x, d0.z, base0, 0)) { err = "Geen route " + std::string(df.number) + " naar " + df.dest; return false; }

    std::string diag;
    // ---- spoor per station (rechts rijden: Sprinter buitenspoor, Intercity middenspoor op viersporig baanvak) ----
    struct Anchor { std::string name; Vec3 p; int count; bool fixed; };
    std::vector<Anchor> anchors;
    // spoor naast een perronvlak met dit nummer ("2", "4b", "6a") bij dit station
    auto trackAt = [&](Vec3 c, const char* want, Vec3& out) {
        float bestD = 1e9f;
        bool found = false;
        for (const OsmPlatform& op : world.platforms) {
            if (op.pts.size() < 3) continue;
            float cx = 0, cz = 0;
            for (auto& q : op.pts) { cx += q.first; cz += q.second; }
            cx /= op.pts.size(); cz /= op.pts.size();
            if (std::hypot(cx - c.x, cz - c.z) > 400) continue;
            const char* s = op.name.c_str();
            while (*s && (*s < '0' || *s > '9')) s++;
            if (std::string(s) != want) continue;
            // sporen langs dit perron (binnen 9 m van het midden); bij twijfel (eilandperron) het meest rechtse
            float bd0 = base0.closest(cx, cz);
            Vec3 dd = dirAt(base0, bd0);
            float dl = std::max(1e-3f, std::hypot(dd.x, dd.z));
            struct Cand { float dist, lat; Vec3 p; };
            std::vector<Cand> cands;
            for (const RailLine& r : world.rails) {
                float lineBest = 1e9f;  // ook "siding": in Boxtel zijn de perronsporen zo getagd
                Vec3 lp;
                for (const RailPoint& q : r.pts) {
                    float d = std::hypot(q.x - cx, q.z - cz);
                    if (d < lineBest) { lineBest = d; lp = Vec3(q.x, q.y, q.z); }
                }
                if (lineBest > 13.0f) continue;
                cands.push_back({lineBest, ((lp.x - cx) * -dd.z + (lp.z - cz) * dd.x) / dl, lp});  // lat: + = rechts
            }
            float nearest = 1e9f;
            for (auto& k : cands) nearest = std::min(nearest, k.dist);
            for (auto& k : cands) {
                if (k.dist > nearest + 2.0f) continue;  // alleen (bijna) even dichtbij: dan rechts rijden
                if (!found || k.lat > bestD) { bestD = k.lat; out = k.p; found = true; }
            }
        }
        return found;
    };
    for (const StopDef& s : df.stops) {
        if (std::string(s.station) == CS) continue;
        Vec3 c;
        if (!stationPt(s.station, c)) { err = std::string("Station niet gevonden: ") + s.station; return false; }
        Anchor a{s.station, c, 0, s.stop || s.track != nullptr};
        if (df.origin == DENBOSCH && df.linkFrom >= 0 && a.name == "'s-Hertogenbosch") a.p = patterns_[df.linkFrom]->terminal;
        else if (s.track && trackAt(c, s.track, a.p)) a.count = -1;
        else {
            float d = base0.closest(c.x, c.z);
            if (!rail.rankedTrack(base0.pos(d), dirAt(base0, d), 30.0f, rank, a.p, &a.count)) a.p = base0.pos(d);
            if (a.count >= 4) a.fixed = true;  // viersporig: ook bij doorkomst het juiste spoor vastleggen
            if (s.track) diag += std::string(" (spoor ") + s.track + " in " + s.station + " niet gevonden)";
        }
        anchors.push_back(a);
    }
    // controlepunt ~1 km ten zuiden van 's-Hertogenbosch: rechter spoor van het dubbelspoor naar Vught
    if (viaDB) {
        float dDB = base0.closest(dbC.x, dbC.z);
        float dc = north ? dDB - 1000 : dDB + 1000;
        Vec3 r;
        int cnt = 0;
        if (dc > 0 && dc < base0.length && rail.rankedTrack(base0.pos(dc), dirAt(base0, dc), 30.0f, 0, r, &cnt)) {
            Anchor a{"(Den Bosch-zuid)", r, cnt, true};
            char b3[160];
            Vec3 q = base0.pos(dc);
            int c30 = 0;
            Vec3 r30;
            rail.rankedTrack(q, dirAt(base0, dc), 30.0f, 0, r30, &c30);
            snprintf(b3, sizeof(b3), " [DB-zuid: %d sporen binnen 12 m, %d binnen 30 m, gekozen %.1f m rechts]", cnt, c30, ((r.x - q.x) * -dirAt(base0, dc).z + (r.z - q.z) * dirAt(base0, dc).x) / std::max(1e-3f, length(dirAt(base0, dc))));
            diag += b3;
            // invoegen in rijvolgorde: noordwaarts vóór Den Bosch, zuidwaarts erna
            auto it = std::find_if(anchors.begin(), anchors.end(), [](const Anchor& x) { return x.name == "'s-Hertogenbosch"; });
            if (north) anchors.insert(it, a);
            else if (it != anchors.end()) anchors.insert(it + 1, a);
        }
    }
    // controlepunt op de lijn naar Tilburg (~1 km ten westen van Boxtel): rechter spoor per rijrichting
    if (toTilburg) {
        Vec3 d = df.exit == TILBURG ? tilburg - tilburgMid : tilburgMid - tilburg, r;
        int cnt = 0;
        if (rail.rankedTrack(tilburgMid, d, 30.0f, 0, r, &cnt) && cnt >= 2) {
            Anchor a{"(Tilburg-lijn)", r, cnt, true};
            if (df.exit == TILBURG) anchors.push_back(a);
            else anchors.insert(anchors.begin(), a);
        }
    }
    // ---- definitieve route ----
    std::vector<Vec3> fixed;
    Vec3 dir;
    if (north) {
        fixed = {csE, csStop, csW};
        for (auto& a : anchors) if (a.fixed) fixed.push_back(a.p);
        if (df.exit == TILBURG) fixed.push_back(tilburg);
        dir = csW - csE;
    } else {
        if (df.origin == TILBURG) fixed.push_back(tilburg);
        for (auto& a : anchors) if (a.fixed) fixed.push_back(a.p);
        for (Vec3 q : {csW, csStop, csE}) fixed.push_back(q);
        float d = base0.closest(fixed[0].x, fixed[0].z);
        dir = dirAt(base0, d + 15);
    }
    std::vector<std::pair<Vec3, float>> protect{{csStop, YARD_ZONE}, {dbC, 120.0f}, {boxC, 600.0f}};
    if (toTilburg) {
        // eindpunt richting Tilburg: het rechter spoor voor deze rijrichting
        Vec3 d = df.exit == TILBURG ? tilburg - tilburgMid : tilburgMid - tilburg, r;
        if (rail.rankedTrack(tilburg, d, 30.0f, 0, r)) {
            for (Vec3& f : fixed) if (std::hypot(f.x - tilburg.x, f.z - tilburg.z) < 0.1f) f = r;
        }
    }
    float extEnd = df.exit == DENBOSCH ? 160.0f : 6000.0f;
    TrackPath main;
    // onbereikbaar vast punt (bijv. ontbrekende overloopwissel in de OSM-data): dat station terugvallen op rechts rijden
    for (int attempt = 0; attempt < 6; attempt++) {
        std::vector<bool> rq(fixed.size(), true);
        TrackPath t;
        if (rail.routeSmart(fixed, rq, dir.x, dir.z, t, 0)) break;
        size_t bad = 0;
        for (size_t n = 2; n <= fixed.size(); n++) {
            std::vector<Vec3> pre(fixed.begin(), fixed.begin() + n);
            std::vector<bool> r2(n, true);
            if (!rail.routeSmart(pre, r2, dir.x, dir.z, t, 0)) { bad = n - 1; break; }
        }
        if (bad == 0) break;
        bool fixedIt = false;
        Anchor* hit = nullptr;
        for (size_t bi : {bad, bad - 1})
            for (auto& a : anchors)
                if (!hit && a.fixed && std::hypot(a.p.x - fixed[bi].x, a.p.z - fixed[bi].z) < 0.1f) hit = &a;
        for (Anchor* ap : {hit})
            if (ap) {
                Anchor& a = *ap;
                Vec3 c = a.p;
                float d = base0.closest(c.x, c.z);
                Vec3 r;
                if (a.count == -1 && rail.rankedTrack(base0.pos(d), dirAt(base0, d), 30.0f, rank, r, &a.count)) {
                    diag += " (" + a.name + ": spoor niet bereikbaar, rechts rijden)";
                    a.p = r;
                } else {
                    diag += " (" + a.name + ": geen vast spoor)";
                    a.fixed = false;
                    a.p = base0.pos(d);
                }
                fixedIt = true;
                break;
            }
        if (!fixedIt) break;
        // vaste punten opnieuw samenstellen
        std::vector<Vec3> nf;
        if (north) { nf = {csE, csStop, csW}; for (auto& a : anchors) if (a.fixed) nf.push_back(a.p); if (df.exit == TILBURG) nf.push_back(tilburg); }
        else { if (df.origin == TILBURG) nf.push_back(tilburg); for (auto& a : anchors) if (a.fixed) nf.push_back(a.p); for (Vec3 q : {csW, csStop, csE}) nf.push_back(q); }
        fixed = nf;
    }
    if (!smartRoute(rail, fixed, dir, rank, extEnd, protect, main)) {
        err = "Geen route " + std::string(df.number) + " naar " + df.dest + ": vast punt";
        for (size_t n = 2; n <= fixed.size(); n++) {
            std::vector<Vec3> pre(fixed.begin(), fixed.begin() + n);
            std::vector<bool> rq(n, true);
            TrackPath t;
            if (!rail.routeSmart(pre, rq, dir.x, dir.z, t, 0)) { err += " " + std::to_string(n - 1) + " van " + std::to_string(fixed.size()) + " onbereikbaar"; break; }
        }
        for (auto& a : anchors) err += " [" + a.name + " " + std::to_string((int)a.p.x) + "," + std::to_string((int)a.p.z) + " sporen " + std::to_string(a.count) + "]";
        return false;
    }
    // ---- aanloop vóór het eerste vaste punt ----
    if (df.origin == EAST) {
        TrackPath ap;
        std::vector<bool> req{true, true};
        Vec3 farPt = csPoint(df.csTrack, 300);
        if (rail.routeSmart({csE, farPt}, req, farPt.x - csE.x, farPt.z - csE.z, ap, 4000) && ap.pts.size() >= 2) main = concat(reversed(ap), main);
    } else {
        Vec3 a0 = main.pos(0), a1 = main.pos(30);
        Vec3 away = a0 - a1;
        Vec3 farPt = a0 + normalize(away) * 40.0f;
        TrackPath ap;
        std::vector<bool> req{true, true};
        float len = df.origin == DENBOSCH ? 170.0f : 6000.0f;
        if (rail.routeSmart({a0, farPt}, req, away.x, away.z, ap, len) && ap.pts.size() >= 2) {
            if (df.origin != DENBOSCH) {
                // rechts rijden t.o.v. de echte rijrichting (tegengesteld aan de bouwrichting)
                std::vector<Vec3> pts{a0};
                std::vector<bool> rq{true};
                for (float d = 200; d < ap.length - 30; d += 200) {
                    Vec3 r, dd = dirAt(ap, d), q = ap.pos(d);
                    bool skip = false;
                    for (auto& pr : protect) if (std::hypot(q.x - pr.first.x, q.z - pr.first.z) < pr.second) skip = true;
                    if (!skip && rail.rankedTrack(q, Vec3(-dd.x, 0, -dd.z), 30.0f, rank, r)) { pts.push_back(r); rq.push_back(false); }
                }
                TrackPath ap2;
                if (pts.size() >= 2 && rail.routeSmart(pts, rq, away.x, away.z, ap2, len)) ap = ap2;
            }
            main = concat(reversed(ap), main);
        }
    }
    extrapolate(main, df.origin != DENBOSCH, df.exit != DENBOSCH);
    p.path = main;
    p.samples.clear();
    for (float d = 0; d <= main.length; d += SAMPLE) p.samples.push_back(main.pos(d));

    // ---- haltes: perron naast ons spoor (OSM-perronvlak "Spoor N" waarvan het midden het dichtst bij ons spoor ligt) ----
    auto platformOf = [&](Vec3 a, int& track, float& dStop) {
        track = 0;
        float bd = 11.0f;
        dStop = main.closest(a.x, a.z);
        for (const OsmPlatform& op : world.platforms) {
            if (op.pts.size() < 3) continue;
            float cx = 0, cz = 0;
            for (auto& q : op.pts) { cx += q.first; cz += q.second; }
            cx /= op.pts.size(); cz /= op.pts.size();
            if (std::hypot(cx - a.x, cz - a.z) > 300) continue;
            float dc = main.closest(cx, cz);
            Vec3 q = main.pos(dc);
            float dist = std::hypot(cx - q.x, cz - q.z);
            if (dist < bd) {
                bd = dist;
                // "Spoor 3", "Spoor 3a", "3b" ...: eerste getal
                const char* s = op.name.c_str();
                while (*s && (*s < '0' || *s > '9')) s++;
                track = atoi(s);
                dStop = dc;
            }
        }
    };
    p.stops.clear();
    char buf[512];
    for (const StopDef& s : df.stops) {
        StopTime t;
        t.station = s.station;
        t.stop = s.stop;
        if (t.station == CS) {
            t.track = df.csTrack;
            t.d = main.closest(csStop.x, csStop.z);
        } else {
            const Anchor& a = *std::find_if(anchors.begin(), anchors.end(), [&](const Anchor& x) { return x.name == s.station; });
            float dS;
            platformOf(a.p, t.track, dS);
            if (!s.stop) platformOf(main.pos(main.closest(a.p.x, a.p.z)), t.track, dS);
            t.d = s.stop ? dS : main.closest(a.p.x, a.p.z);
            if (s.track && a.count == -1) t.track = atoi(s.track);  // echt spoor uit de dienstregeling
            snprintf(buf, sizeof(buf), " %s:%d(%d sporen)", s.station, t.track, a.count);
            diag += buf;
            if (s.station == std::string("'s-Hertogenbosch")) p.terminal = main.pos(t.d);
        }
        p.stops.push_back(t);
    }
    for (size_t k = 1; k < p.stops.size(); k++)
        if (p.stops[k].d <= p.stops[k - 1].d) {
            err = "Haltevolgorde klopt niet (" + std::string(df.number) + " naar " + df.dest + "):";
            for (const StopTime& t : p.stops) err += " " + t.station + "=" + std::to_string((int)t.d);
            err += " pad " + std::to_string((int)main.length) + " m;" + diag;
            return false;
        }
    // snelheidsbeperkingen: emplacementen 80 km/u; aftakking Boxtel 80 km/u voor treinen van/naar Tilburg
    p.zones.clear();
    for (Vec3 c : {csStop, dbC}) {
        float d = main.closest(c.x, c.z);
        if (std::hypot(main.pos(d).x - c.x, main.pos(d).z - c.z) < 200) p.zones.push_back({d - YARD_ZONE, d + YARD_ZONE, 22.2f});
    }
    if (toTilburg) {
        float d = main.closest(boxC.x, boxC.z);
        p.zones.push_back({d - 450, d + 450, 22.2f});
    }
    std::sort(p.zones.begin(), p.zones.end(), [](const SpeedZone& a, const SpeedZone& b) { return a.d0 < b.d0; });
    snprintf(buf, sizeof(buf), "%s %s naar %s (%s, CS spoor %d): pad %.0f m\n   sporen:%s\n", p.rs->product.c_str(), df.number, df.dest,
             p.rs->typeName.c_str(), df.csTrack, main.length, diag.c_str());
    p.info = buf;
    return true;
}

// ============================ tijden ============================
// Tijden uit de dienstregeling; per baanvak (tussen twee stops) wordt de kruissnelheid zo gekozen dat de trein
// precies op tijd aankomt (met 3% marge), nooit sneller dan de baanvaksnelheid (140 km/u) of het materieel.
void Traffic::timetable(Pattern& p, std::string& log) {
    const Def& df = p.def;
    const RollingStock& rs = *p.rs;
    float half = rs.totalLength() / 2;
    char buf[256];
    std::vector<StopTime>& S = p.stops;
    for (size_t k = 0; k < S.size(); k++) {
        S[k].arr = df.stops[k].arr < 0 ? -1 : df.stops[k].arr * 60.0;
        S[k].dep = df.stops[k].dep < 0 ? -1 : df.stops[k].dep * 60.0;
        S[k].vcruise = LINE_MAX;
    }
    // eerste halte
    int first = 0;
    if (S[0].arr < 0) S[0].arr = S[0].dep - (df.origin == DENBOSCH ? 0 : (S[0].station == CS || S[0].station == "'s-Hertogenbosch") ? 120 : 40);
    p.entrySpeed = std::min(rs.vmax, LINE_MAX);
    if (df.origin != DENBOSCH) p.entryRel = S[0].arr - simulate(rs, p.zones, half, half + 1, S[0].d, p.entrySpeed, true, LINE_MAX).t;
    else p.entryRel = S[0].dep - 300;
    // baanvakken
    int a = first;
    while (a < (int)S.size() - 1) {
        int b = a + 1;
        while (b < (int)S.size() - 1 && !S[b].stop) b++;
        std::vector<float> marks;
        for (int k = a + 1; k < b; k++) marks.push_back(S[k].d);
        std::vector<double> mt;
        double tmin = simulate(rs, p.zones, half, S[a].d, S[b].d, 0, S[b].stop, LINE_MAX, marks, &mt).t;
        double budget = -1;
        if (S[b].arr >= 0) budget = S[b].arr - S[a].dep;
        else if (S[b].dep >= 0 && b < (int)S.size() - 1 && S[b].station != CS) budget = S[b].dep - 25 - S[a].dep;
        float vc = LINE_MAX;
        const float MARGIN = 0.92f;  // speling: de trein rijdt iets sneller en wacht dan kort op de vertrektijd
        if (budget > 0 && tmin < budget * MARGIN) {
            float lo = 16.7f, hi = LINE_MAX;  // niet langzamer dan 60 km/u kruisen: dan liever even wachten
            for (int it = 0; it < 22; it++) {
                float m = (lo + hi) / 2;
                if (simulate(rs, p.zones, half, S[a].d, S[b].d, 0, S[b].stop, m).t > budget * MARGIN) lo = m; else hi = m;
            }
            vc = hi;
        }
        double t = simulate(rs, p.zones, half, S[a].d, S[b].d, 0, S[b].stop, vc, marks, &mt).t;
        for (int k = a + 1; k <= b; k++) S[k].vcruise = vc;
        for (int k = a + 1; k < b; k++) S[k].arr = S[k].dep = S[a].dep + mt[k - a - 1];
        if (S[b].arr < 0) S[b].arr = S[b].dep >= 0 && S[b].station != CS && budget > 0 ? std::max(S[a].dep + t, S[b].dep - 25) : std::ceil((S[a].dep + t * 1.03) / 30) * 30;
        if (S[b].dep < 0) S[b].dep = S[b].arr + (S[b].stop && b < (int)S.size() - 1 ? 30 : 0);
        snprintf(buf, sizeof(buf), "    %-20s -> %-20s %5.1f km  min %4.1f min, gepland %4.1f min -> %3.0f km/u%s\n", S[a].station.c_str(), S[b].station.c_str(),
                 (S[b].d - S[a].d) / 1000, tmin / 60, (S[b].arr - S[a].dep) / 60, vc * 3.6f, budget > 0 && tmin > budget ? "  TE LAAT" : "");
        log += buf;
        a = b;
    }
    for (const StopTime& s : S) {
        auto mm = [](double t) { return t < 0 ? std::string("  -  ") : std::to_string((int)std::floor(t / 60)) + ":" + (((int)t % 60) < 10 ? "0" : "") + std::to_string((int)t % 60); };
        snprintf(buf, sizeof(buf), "    %-20s spoor %d  aank %7s  vertr %7s  %s\n", s.station.c_str(), s.track, mm(s.arr).c_str(), mm(s.dep).c_str(),
                 s.stop ? "stopt" : "rijdt door");
        log += buf;
    }
}

bool Traffic::setup(const World& world, const Railways& rail, const Stations& st, SignAtlas& atlas, std::string& err) {
    stock_.emplace_back(makeFlirt());
    stock_.emplace_back(makeVirm());
    // Dienstregeling 2026 (OV in Nederland Wiki, vertrektijden per station). Minuten na het begin van het halfuur
    // (de rit een halfuur later rijdt 30 minuten later). -1 = uitrekenen uit de rijtijd.
    // Alle Intercity's worden (voorlopig) met VIRM gereden.
    const char *ES = "Eindhoven Strijp-S", *BE = "Best", *BX = "Boxtel", *VU = "Vught", *DB = "'s-Hertogenbosch";
    std::vector<Def> defs = {
        // ---- noordwaarts (vanaf Eindhoven Centraal) ----
        {"2700", "Alkmaar", 1, 5, EAST, NORTH, {{CS, true, -1, 4}, {ES, false, -1, -1}, {BE, false, -1, -1}, {BX, false, -1, -1, "2"}, {VU, false, -1, -1, "1"}, {DB, true, 24, 26, "3a"}}},
        {"6500", "'s-Hertogenbosch", 0, 6, EAST, DENBOSCH, {{CS, true, -1, 5}, {ES, true, -1, 9}, {BE, true, -1, 14}, {BX, true, -1, 21, "2"}, {VU, true, -1, 28, "1"}, {DB, true, -1, -1, "4b"}}},
        {"1100", "Den Haag Centraal", 1, 5, EAST, TILBURG, {{CS, true, -1, 13}, {ES, false, -1, -1}, {BE, false, -1, -1}, {BX, false, -1, -1, "6"}}},
        {"3500", "Dordrecht", 1, 6, EAST, NORTH, {{CS, true, -1, 16}, {ES, false, -1, -1}, {BE, false, -1, -1}, {BX, false, -1, -1, "2"}, {VU, false, -1, -1, "1"}, {DB, true, 36, 38, "3a"}}},
        {"6400", "Tilburg Universiteit", 0, 4, EAST, TILBURG, {{CS, true, -1, 21}, {ES, true, -1, 24}, {BE, true, -1, 30}, {BX, true, -1, 37, "6"}}},
        {"3900", "Enkhuizen", 1, 5, EAST, NORTH, {{CS, true, -1, 26}, {ES, false, -1, -1}, {BE, false, -1, -1}, {BX, false, -1, -1, "2"}, {VU, false, -1, -1, "1"}, {DB, true, 46, 48, "3a"}}},
        // ---- zuidwaarts (naar Eindhoven Centraal) ----
        {"6500", "Deurne", 0, 2, DENBOSCH, EAST, {{DB, true, -1, 28}, {VU, true, -1, 32.5, "2"}, {BX, true, -1, 39, "3"}, {BE, true, -1, 45}, {ES, true, -1, 51}, {CS, true, -1, 66}}, 1, -30},
        {"3900", "Heerlen", 1, 1, NORTH, EAST, {{DB, true, -1, 14, "6a"}, {VU, false, -1, -1, "2"}, {BX, false, -1, -1, "3"}, {BE, false, -1, -1}, {ES, false, -1, -1}, {CS, true, 35, 38}}},
        {"3500", "Venlo", 1, 2, NORTH, EAST, {{DB, true, -1, 24, "6a"}, {VU, false, -1, -1, "2"}, {BX, false, -1, -1, "3"}, {BE, false, -1, -1}, {ES, false, -1, -1}, {CS, true, 44.5, 49}}},
        {"2700", "Maastricht", 1, 3, NORTH, EAST, {{DB, true, -1, 3, "6a"}, {VU, false, -1, -1, "2"}, {BX, false, -1, -1, "3"}, {BE, false, -1, -1}, {ES, false, -1, -1}, {CS, true, 24, 27}}},
        {"6400", "Weert", 0, 3, TILBURG, EAST, {{BX, true, -1, 23, "7"}, {BE, true, -1, 29}, {ES, true, -1, 35}, {CS, true, -1, 47}}},
    };
    std::string log;
    for (const Def& d : defs) {
        auto p = std::make_unique<Pattern>();
        p->def = d;
        p->number = d.number;
        p->dest = d.dest;
        p->north = d.origin == EAST;
        p->rs = stock_[d.stock].get();
        if (!buildPattern(*p, world, rail, st, err)) return false;
        std::string tl;
        timetable(*p, tl);
        p->info += tl;
        patterns_.push_back(std::move(p));
    }
    // controle: rijden twee tegengestelde series ergens over hetzelfde spoor? (zou kunnen vastlopen)
    {
        std::map<std::pair<int, int>, std::vector<std::pair<int, int>>> grid;  // cel -> (patroon, monster)
        for (size_t pi = 0; pi < patterns_.size(); pi++)
            for (size_t k = 0; k < patterns_[pi]->samples.size(); k += 2) {
                const Vec3& s = patterns_[pi]->samples[k];
                grid[{(int)std::floor(s.x / 10), (int)std::floor(s.z / 10)}].push_back({(int)pi, (int)k});
            }
        for (size_t a = 0; a < patterns_.size(); a++)
            for (size_t b = a + 1; b < patterns_.size(); b++) {
                const Pattern& A = *patterns_[a];
                const Pattern& Bp = *patterns_[b];
                if (A.north == Bp.north) continue;
                if (A.def.linkFrom == (int)b || Bp.def.linkFrom == (int)a) continue;  // kopmaken: hetzelfde spoor is juist de bedoeling
                float shared = 0, from = -1;
                for (size_t k = 0; k + 1 < A.samples.size(); k += 5) {
                    const Vec3& s = A.samples[k];
                    Vec3 da = A.samples[k + 1] - s;
                    bool hit = false;
                    for (int gx = -1; gx <= 1 && !hit; gx++)
                        for (int gz = -1; gz <= 1 && !hit; gz++) {
                            auto it = grid.find({(int)std::floor(s.x / 10) + gx, (int)std::floor(s.z / 10) + gz});
                            if (it == grid.end()) continue;
                            for (auto& e : it->second) {
                                if (e.first != (int)b || e.second + 1 >= (int)Bp.samples.size()) continue;
                                const Vec3& q = Bp.samples[e.second];
                                if (std::hypot(q.x - s.x, q.z - s.z) > 1.5f) continue;
                                Vec3 db = Bp.samples[e.second + 1] - q;
                                if (da.x * db.x + da.z * db.z < 0) { hit = true; break; }
                            }
                        }
                    if (hit) { shared += 5 * SAMPLE; if (from < 0) from = k * SAMPLE; }
                }
                if (shared > 150) {
                    char b2[256];
                    snprintf(b2, sizeof(b2), "LET OP: %s %s en %s %s delen %.0f m spoor in tegengestelde richting (vanaf d=%.0f)\n", A.number.c_str(), A.dest.c_str(),
                             Bp.number.c_str(), Bp.dest.c_str(), shared, from);
                    patterns_[a]->info += b2;
                }
            }
    }
    for (const char* n : {CS, ES, BE, BX, VU, DB}) nameIndex(n);
    for (auto& p : patterns_) nameIndex(p->dest);
    for (auto& s : stock_) s->build(atlas, names_);

    // ritten: dag 0 en dag 1, halfuren van 05:30 tot 00:30
    int id = 1;
    struct Inst { double entry; int pat; double base; };
    std::vector<Inst> order;
    for (int day = 0; day < 2; day++)
        for (size_t pi = 0; pi < patterns_.size(); pi++)
            for (int slot = 11; slot <= 49; slot++) {
                double base = day * 86400.0 + slot * 1800.0;
                order.push_back({base + patterns_[pi]->entryRel, (int)pi, base});
            }
    std::sort(order.begin(), order.end(), [](const Inst& a, const Inst& b) { return a.entry < b.entry; });
    services_.reserve(order.size());
    std::vector<double> baseOf;
    for (auto& o : order) {
        const Pattern& p = *patterns_[o.pat];
        Service s;
        s.id = id++;
        s.rs = p.rs;
        s.number = p.number;
        s.dest = p.dest;
        s.path = &p.path;
        s.entry = o.entry;
        s.entrySpeed = p.entrySpeed;
        s.zones = p.zones;
        s.stops = p.stops;
        s.lineMax = LINE_MAX;
        for (StopTime& t : s.stops) { t.arr += o.base; t.dep += o.base; }
        s.destIdx = nameIndex(p.dest);
        for (const StopTime& t : s.stops) s.stopIdx.push_back(nameIndex(t.station));
        s.startAtStop = p.def.origin == DENBOSCH;
        services_.push_back(s);
        serviceOf_.push_back(o.pat);
        baseOf.push_back(o.base);
    }
    // kopmaken in 's-Hertogenbosch: koppel elke rit vanaf Den Bosch aan de aankomende rit van de andere richting
    for (size_t i = 0; i < services_.size(); i++) {
        const Def& d = patterns_[serviceOf_[i]]->def;
        if (d.linkFrom < 0) continue;
        double want = baseOf[i] + d.linkBase * 60.0;
        for (size_t j = 0; j < services_.size(); j++)
            if (serviceOf_[j] == d.linkFrom && std::fabs(baseOf[j] - want) < 1) {
                if (services_[j].stops.back().arr > services_[i].stops[0].dep - 120) break;
                services_[j].next = &services_[i];
                services_[j].stops.back().dep = services_[i].stops[0].dep;
                services_[i].hasPred = true;
                break;
            }
    }
    return true;
}

// ============================ beveiliging en verloop ============================
bool Traffic::conflict(const Vec3& p, const Run* self) const {
    for (const Run& o : runs_) {
        if (&o == self) continue;
        if (p.x < o.bmin.x - CLEAR || p.x > o.bmax.x + CLEAR || p.z < o.bmin.z - CLEAR || p.z > o.bmax.z + CLEAR) continue;
        const std::vector<Vec3>& s = o.pat->samples;
        for (int k = o.i0; k <= o.i1 && k < (int)s.size(); k++) {
            float dx = s[k].x - p.x, dz = s[k].z - p.z;
            if (dx * dx + dz * dz < CLEAR * CLEAR && std::fabs(s[k].y - p.y) < 5.0f) return true;
        }
    }
    return false;
}

void Traffic::reserve(Run& r) {
    const std::vector<Vec3>& s = r.pat->samples;
    r.i0 = std::max(0, (int)(r.train->rear() / SAMPLE) - 1);
    r.i1 = std::max(r.i1, std::min((int)s.size() - 1, (int)(r.train->front() / SAMPLE) + 1));
    Vec3 mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
    for (int k = r.i0; k <= r.i1; k++) {
        mn.x = std::min(mn.x, s[k].x); mn.z = std::min(mn.z, s[k].z);
        mx.x = std::max(mx.x, s[k].x); mx.z = std::max(mx.z, s[k].z);
    }
    r.bmin = mn;
    r.bmax = mx;
}

bool Traffic::spawn(const Service& s, const Collision& col) {
    const Pattern* pat = patterns_[patternOf(&s)].get();
    Train* t = new Train();
    t->start(&s, col);
    Run r{t, pat, 0, 0, {}, {}};
    int i0 = std::max(0, (int)(t->rear() / SAMPLE) - 1);
    int i1 = std::min((int)pat->samples.size() - 1, (int)((t->front() + (s.startAtStop ? 4 : 120)) / SAMPLE));
    for (int k = i0; k <= i1; k++)
        if (conflict(pat->samples[k], nullptr)) { delete t; return false; }
    r.i1 = i1;
    reserve(r);
    runs_.push_back(r);
    active_.push_back(t);
    return true;
}

void Traffic::step(float dt, const Collision& col) {
    clock_ += dt;
    // nieuwe treinen
    while (nextSpawn_ < services_.size() && services_[nextSpawn_].entry <= clock_) {
        Service& s = services_[nextSpawn_];
        if (s.hasPred) {
            // wordt gereden door de aankomende trein; alleen als die er niet is (bijv. na opnieuw starten) los beginnen
            bool covered = false;
            for (const Train* a : active_)
                if (a->service().next == &s || &a->service() == &s) covered = true;
            if (covered) { nextSpawn_++; continue; }
        }
        if (spawn(s, col)) nextSpawn_++;
        else if (clock_ - s.entry > 600) nextSpawn_++;  // te laat: vervalt
        else break;
    }
    for (size_t i = 0; i < runs_.size(); i++) {
        Run& r = runs_[i];
        Train* t = r.train;
        const std::vector<Vec3>& s = r.pat->samples;
        int last = (int)s.size() - 1;
        float want = t->front() + t->brakingDistance() + 60.0f + t->speed() * 3.0f;
        if (t->stopped() && t->departureTime() - clock_ > 15) want = t->front() + 4.0f;  // staat stil: niets vasthouden
        else if (t->manual()) want = std::max(want, t->front() + 1500.0f);  // speler: rijweg ver vooruit (remweg bij baanvaksnelheid)
        int target = std::min(last, (int)std::ceil(want / SAMPLE));
        int fIdx = std::min(last, (int)(t->front() / SAMPLE) + 1);
        if (target <= r.i1) {
            r.i1 = std::max(target, fIdx);
        } else {
            int k = r.i1 + 1;
            while (k <= target && !conflict(s[k], &r)) k++;
            r.i1 = k - 1;
        }
        float authority = r.i1 >= last ? 1e9f : r.i1 * SAMPLE - 12.0f;
        const Service* before = &t->service();
        t->update(dt, clock_, authority, col);
        if (&t->service() != before) {
            // kopmaken: verder op het pad van de vervolgrit
            r.pat = patterns_[patternOf(&t->service())].get();
            r.i1 = 0;
        }
        reserve(r);
    }
    // treinen die de spelwereld verlaten
    for (size_t i = 0; i < runs_.size();) {
        if (runs_[i].train->finished()) {
            Train* t = runs_[i].train;
            if (onLeave) onLeave(t);
            active_.erase(std::find(active_.begin(), active_.end(), t));
            delete t;
            runs_.erase(runs_.begin() + i);
        } else {
            i++;
        }
    }
}

void Traffic::update(float dt, const Collision& col) {
    // grote stappen opdelen (bij haperingen)
    while (dt > 0) {
        float s = std::min(dt, 0.1f);
        step(s, col);
        dt -= s;
    }
}

void Traffic::reset(double clock, const Collision& col) {
    for (Train* t : active_) {
        if (onLeave) onLeave(t);
        delete t;
    }
    active_.clear();
    runs_.clear();
    // ruim een uur terug beginnen, zodat alle treinen die nu onderweg zijn er al zijn
    double start = clock - 4500;
    nextSpawn_ = 0;
    while (nextSpawn_ < services_.size() && services_[nextSpawn_].entry < start) nextSpawn_++;
    clock_ = start;
    auto keep = onLeave;
    onLeave = nullptr;
    while (clock_ < clock - 1e-6) step((float)std::min(0.5, clock - clock_), col);
    onLeave = keep;
}

std::vector<Departure> Traffic::departures(const std::string& station, int n, double horizon) const {
    std::vector<Departure> out;
    for (const Service& s : services_) {
        if (s.entry > clock_ + horizon) break;
        for (int k = 0; k < (int)s.stops.size(); k++) {
            const StopTime& t = s.stops[k];
            if (t.station != station || !t.stop) continue;
            if (k == (int)s.stops.size() - 1 && patterns_[patternOf(&s)]->def.exit == DENBOSCH) continue;  // eindpunt: geen vertrek
            if (t.dep > clock_ + horizon) continue;
            const Train* tr = nullptr;
            for (const Train* a : active_)
                if (&a->service() == &s) tr = a;
            // vertrokken treinen niet meer tonen
            if (tr) {
                int li = tr->lastStopIndex();
                if (li > k || (li == k && !tr->stopped())) continue;
            } else if (t.dep < clock_) {
                continue;
            }
            out.push_back({t.dep, s.product(), s.number, s.dest, t.track, tr});
        }
    }
    std::sort(out.begin(), out.end(), [](const Departure& a, const Departure& b) { return a.time < b.time; });
    if ((int)out.size() > n) out.resize(n);
    return out;
}

std::vector<Traffic::DutyOption> Traffic::dutyOptions(double from, double to) const {
    std::vector<DutyOption> out;
    auto hm = [](double t) {
        char b[16];
        t = std::fmod(std::max(0.0, t), 86400.0);
        snprintf(b, sizeof b, "%02d:%02d", (int)(t / 3600), (int)std::fmod(t / 60, 60));
        return std::string(b);
    };
    for (const Service& s : services_) {
        if (!s.rs || s.stops.size() < 2) continue;
        int k0 = -1;
        for (int k = 0; k + 1 < (int)s.stops.size(); k++)
            if (s.stops[k].stop) { k0 = k; break; }
        if (k0 < 0) continue;
        double dep = s.stops[k0].dep;
        if (dep < from || dep > to) continue;
        // laatste halte in de spelwereld waar gestopt wordt
        int k1 = k0;
        std::string via;
        for (int k = k0 + 1; k < (int)s.stops.size(); k++)
            if (s.stops[k].stop) {
                k1 = k;
                via += (via.empty() ? "" : ", ") + s.stops[k].station;
            }
        if (k1 == k0) continue;
        DutyOption o;
        o.serviceId = s.id;
        o.stop = k0;
        o.arr = s.stops[k0].arr;
        o.dep = dep;
        char b[200];
        snprintf(b, sizeof b, "%s  %s %s  %s (sp. %d) - %s", hm(dep).c_str(), s.product().c_str(), s.number.c_str(), s.stops[k0].station.c_str(),
                 s.stops[k0].track, s.dest.c_str());
        o.label = b;
        snprintf(b, sizeof b, "Materieel: NS %s, %d bakken, %.0f m. Rijtijd in de spelwereld: %d min (aankomst %s in %s).",
                 s.rs->typeName.c_str(), s.rs->cars, s.rs->totalLength(), (int)((s.stops[k1].arr - dep) / 60), hm(s.stops[k1].arr).c_str(),
                 s.stops[k1].station.c_str());
        o.info = std::string(b) + "\nStopt in: " + via + ".\nEindbestemming: " + s.dest + (s.stops[k1].station != s.dest ? " (buiten de spelwereld)" : "") + ".";
        out.push_back(o);
    }
    std::sort(out.begin(), out.end(), [](const DutyOption& a, const DutyOption& b) { return a.dep < b.dep; });
    return out;
}

Train* Traffic::trainOf(int serviceId) const {
    for (Train* t : active_)
        if (t->service().id == serviceId) return t;
    return nullptr;
}

std::string Traffic::debugInfo() const {
    std::string s;
    for (auto& p : patterns_) s += p->info;
    char buf[256];
    snprintf(buf, sizeof(buf), "%zu ritten, %zu actief\n", services_.size(), active_.size());
    s += buf;
    return s;
}

std::string Traffic::trainsInfo() const {
    std::string s;
    char buf[512];
    for (const Run& r : runs_) {
        const Train* t = r.train;
        const Service& sv = t->service();
        int li = t->lastStopIndex();
        // wie houdt deze trein tegen? (eerste monster voorbij de reservering)
        std::string block;
        int k = r.i1 + 1;
        if (k < (int)r.pat->samples.size()) {
            Vec3 p = r.pat->samples[k];
            for (const Run& o : runs_) {
                if (&o == &r) continue;
                for (int j = o.i0; j <= o.i1 && j < (int)o.pat->samples.size(); j++) {
                    float dx = o.pat->samples[j].x - p.x, dz = o.pat->samples[j].z - p.z;
                    if (dx * dx + dz * dz < CLEAR * CLEAR) {
                        // afstand tussen de twee paden over de 600 m voor het blokkeerpunt
                        float mn = 1e9f, mx = 0;
                        for (int q = std::max(0, k - 300); q <= k; q += 10) {
                            float bd = 1e9f;
                            for (const Vec3& w : o.pat->samples) bd = std::min(bd, std::hypot(w.x - r.pat->samples[q].x, w.z - r.pat->samples[q].z));
                            mn = std::min(mn, bd); mx = std::max(mx, bd);
                        }
                        char b2[128];
                        snprintf(b2, sizeof(b2), " [padafstand %.1f..%.1f m]", mn, mx);
                        block = o.train->label() + b2;
                        break;
                    }
                }
                if (!block.empty()) break;
            }
        }
        snprintf(buf, sizeof(buf), "%-45s vertr %s  laatste halte %-20s d=%6.0f/%6.0f  v=%4.1f  %s%s  te laat %.0f s %s%s\n", t->label().c_str(),
                 std::to_string((int)(sv.stops[0].dep / 3600)).c_str(), li >= 0 ? sv.stops[li].station.c_str() : "-", t->pathPos(), sv.path->length,
                 t->speed(), t->stopped() ? "STAAT " : "rijdt ", t->waitingForSignal() ? "WACHT-SEIN" : "", t->maxDelay(),
                 block.empty() ? "" : "geblokkeerd door: ", block.c_str());
        s += buf;
    }
    return s;
}
