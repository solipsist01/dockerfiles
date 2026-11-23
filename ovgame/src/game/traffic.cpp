#include <atomic>
#include <chrono>
#include <thread>
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
        // ten noorden van 's-Hertogenbosch lopen andere lijnen (Tiel, Dordrecht, Betuweroute) soms op 15-30 m naast
        // de lijn naar Utrecht: daar alleen de sporen van de eigen lijn bekijken
        float radius = p.z < -28500.0f ? 14.0f : 30.0f;
        if (rail.rankedTrack(p, dirAt(base, d), radius, p.z < -28500.0f ? 0 : rank, r)) pts.push_back({d, r, false});
    }
    std::stable_sort(pts.begin(), pts.end(), [](const Pt& a, const Pt& b) { return a.d < b.d; });
    std::vector<Vec3> all;
    std::vector<bool> rq;
    for (auto& p : pts) { all.push_back(p.p); rq.push_back(p.req); }
    if (!rail.routeSmart(all, rq, dir.x, dir.z, out, extend)) {
        // lukt het niet in één keer: per stuk tussen twee vaste punten (een stuk zonder rechts-rijden-punten als het
        // daarmee niet lukt), zodat één lastig stuk niet de hele rit terugzet naar de kortste route
        TrackPath acc;
        Vec3 d = dir;
        bool ok = true;
        size_t s0 = 0;
        for (size_t k = 1; k < all.size() && ok; k++) {
            if (!rq[k]) continue;
            bool last = k == all.size() - 1;
            std::vector<Vec3> seg(all.begin() + s0, all.begin() + k + 1);
            std::vector<bool> sr(rq.begin() + s0, rq.begin() + k + 1);
            sr[0] = true;
            TrackPath t;
            if (!rail.routeSmart(seg, sr, d.x, d.z, t, last ? extend : 0)) {
                std::vector<Vec3> two{all[s0], all[k]};
                std::vector<bool> r2{true, true};
                if (!rail.routeSmart(two, r2, d.x, d.z, t, last ? extend : 0)) { ok = false; break; }
            }
            acc = acc.pts.empty() ? t : concat(acc, t);
            if (t.pts.size() >= 2) d = t.pts.back() - t.pts[t.pts.size() - 2];
            s0 = k;
        }
        out = ok && !acc.pts.empty() ? acc : base;
    }
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
    const Vec3 tilburg = rdPoint(148058, 399976), tilburgMid = rdPoint(149265, 400058);  // op het spoor naar Tilburg (OSM)
    Vec3 dbC, boxC, utC, gdmC;
    stationPt("'s-Hertogenbosch", dbC);
    stationPt("Boxtel", boxC);
    stationPt("Utrecht Centraal", utC);
    stationPt("Geldermalsen", gdmC);
    bool toTilburg = df.exit == TILBURG || df.origin == TILBURG;
    // Stations langs de lijn (van Eindhoven naar Utrecht). Een rit gebruikt het stuk tussen zijn begin en eind.
    static const char* LINE_EU[] = {"Eindhoven Strijp-S", "Best", "Boxtel", "Vught", "'s-Hertogenbosch", "Zaltbommel", "Geldermalsen",
                                     "Culemborg", "Houten Castellum", "Houten", "Utrecht Lunetten", "Utrecht Vaartsche Rijn", "Utrecht Centraal"};
    // lijn Utrecht - Zwolle ("noord" = richting Zwolle)
    static const char* LINE_ZW[] = {"Utrecht Centraal", "Utrecht Overvecht", "Bilthoven", "Den Dolder", "Amersfoort Centraal",
                                     "Amersfoort Schothorst", "Amersfoort Vathorst", "Nijkerk", "Putten", "Ermelo", "Harderwijk", "Nunspeet",
                                     "'t Harde", "Wezep", "Zwolle"};
    const bool zwLine = zwEnd(df.origin) || zwEnd(df.exit);
    const char* const* LINE_ST = zwLine ? LINE_ZW : LINE_EU;
    const int NLS = zwLine ? (int)(sizeof(LINE_ZW) / sizeof(LINE_ZW[0])) : (int)(sizeof(LINE_EU) / sizeof(LINE_EU[0]));
    const End southEnd = north ? df.origin : df.exit, northEnd = north ? df.exit : df.origin;
    const bool zwUtOnly = zwLine && !(df.origin == ZWOLLE || df.origin == ZWN || df.origin == AMFE || df.exit == ZWOLLE || df.exit == ZWN || df.exit == AMFE);
    int i0 = zwLine ? 0 : southEnd == DENBOSCH ? 4 : southEnd == TIEL ? 6 : 0;
    int i1 = zwLine ? (zwUtOnly ? 0 : (northEnd == AMFE || southEnd == AMFE) ? 4 : NLS - 1) : northEnd == DENBOSCH || northEnd == NORTH ? 4 : northEnd == TILBURG ? 1 : NLS - 1;
    // lijn naar Alkmaar (en de IC's die vanuit Brabant doorrijden naar Alkmaar/Enkhuizen/Schiphol)
    const bool akLine = akEnd(df.origin) || akEnd(df.exit);
    auto euEnd = [](End e) { return e == EAST || e == HELMOND || e == TILBURG || e == DENBOSCH || e == NORTH || e == TIEL; };
    const bool viaDB = !zwLine && (!akLine || euEnd(df.origin) || euEnd(df.exit)) && i0 < 4 && i1 >= 4;  // rijdt ten zuiden van 's-Hertogenbosch
    Vec3 amfC, zwC;
    stationPt("Amersfoort Centraal", amfC);
    stationPt("Zwolle", zwC);
    // eindstation van een kerende rit
    auto termName = [&](End e) {
        return e == DENBOSCH ? "'s-Hertogenbosch" : e == UTC ? "Utrecht Centraal" : e == ZWOLLE ? "Zwolle" : e == AKM ? "Alkmaar" :
               e == BKL ? "Breukelen" : e == UITG ? "Uitgeest" : e == ASD ? "Amsterdam Centraal" : "";
    };
    const bool linked = isTerm(df.origin) && df.linkFrom >= 0;  // beginpunt = eindpunt van de aansluitende rit
    // uitlopers (de mist in): punten in de rijrichting "naar buiten"
    auto rdArr = [](const double (*p)[2], int k) { return rdPoint(p[k][0], p[k][1]); };
    auto branch = [&](End e, Vec3& bMid, Vec3& bFar) {
        if (e == TILBURG) { bMid = tilburgMid; bFar = tilburg; return true; }
        if (e == UTN) { bMid = rdArr(UT_NORTH_PTS, 3); bFar = rdArr(UT_NORTH_PTS, 7); return true; }
        if (e == UTW) { bMid = rdArr(UT_WEST_PTS, 3); bFar = rdArr(UT_WEST_PTS, 6); return true; }
        if (e == TIEL) { bMid = rdArr(TIEL_PTS, 3); bFar = rdArr(TIEL_PTS, 6); return true; }
        if (e == ZWN) { bMid = rdArr(ZW_NORTH_PTS, 7); bFar = rdArr(ZW_NORTH_PTS, 12); return true; }
        if (e == HELMOND) { bMid = rdArr(HELMOND_STUB_PTS, 2); bFar = rdArr(HELMOND_STUB_PTS, HELMOND_STUB_N - 1); return true; }
        if (e == AMFE) { bMid = rdArr(AMF_EAST_PTS, 8); bFar = rdArr(AMF_EAST_PTS, 10); return true; }
        if (e == UTO) { bMid = rdArr(UTO_PTS, 7); bFar = rdArr(UTO_PTS, 11); return true; }
        if (e == BKW) { bMid = rdArr(BKW_PTS, 5); bFar = rdArr(BKW_PTS, 10); return true; }
        if (e == HLM) { bMid = rdArr(HLM_PTS, 4); bFar = rdArr(HLM_PTS, 10); return true; }
        if (e == HRN) { bMid = rdArr(HRN_PTS, 6); bFar = rdArr(HRN_PTS, 10); return true; }
        if (e == BVW) { bMid = rdArr(BVW_PTS, 4); bFar = rdArr(BVW_PTS, 8); return true; }
        if (e == AKN) { bMid = rdArr(AKN_PTS, 4); bFar = rdArr(AKN_PTS, 10); return true; }
        if (e == DVZ) { bMid = rdArr(DVZ_PTS, 2); bFar = rdArr(DVZ_PTS, 7); return true; }
        return false;
    };

    // ---- voorlopige route langs de corridor (voor de rijrichting bij de stations), eerst in noordelijke volgorde ----
    // (langs de stations zelf; tussenpunten zouden op een andere lijn kunnen liggen). Voor de uitlopers drie pogingen:
    // het ruwe eindpunt, het rechter spoor in de rijrichting, of alleen tot het middelpunt van de uitloper.
    // voorbij Utrecht is de lijn viersporig: Intercity's op de binnenste sporen (die leiden naar de IC-perrons 7/18)
    auto branchRank = [&](End e) { return e == UTW && p.rs->product == "Intercity" ? 1 : 0; };
    auto rightOf = [&](End e, Vec3 bMid, Vec3 bFar) {
        Vec3 d = e == df.exit ? bFar - bMid : bMid - bFar, r = bFar;
        rail.rankedTrack(bFar, d, 30.0f, branchRank(e), r);
        return r;
    };
    // lijn naar Zwolle: Utrecht Centraal niet via het stationsmidden (bij spoor 14; vandaar is de lijn naar Amersfoort niet
    // bereikbaar) maar via het perronspoor uit de dienstregeling (beide sporen langs dat perron proberen)
    std::vector<Vec3> utCands;
    if (zwLine)
        for (const StopDef& sd : df.stops) {
            if (std::string(sd.station) != "Utrecht Centraal" || !sd.track) continue;
            for (const OsmPlatform& op : world.platforms) {
                if (op.pts.size() < 3) continue;
                float cx = 0, cz = 0;
                for (auto& q : op.pts) { cx += q.first; cz += q.second; }
                cx /= op.pts.size(); cz /= op.pts.size();
                if (std::hypot(cx - utC.x, cz - utC.z) > 450) continue;
                const char* t = op.name.c_str();
                while (*t && (*t < '0' || *t > '9')) t++;
                if (std::string(t) != sd.track) continue;
                for (const RailLine& r : world.rails) {
                    float bd = 13.0f;
                    Vec3 bp;
                    for (const RailPoint& q : r.pts) {
                        float dd = std::hypot(q.x - cx, q.z - cz);
                        if (dd < bd) { bd = dd; bp = Vec3(q.x, q.y, q.z); }
                    }
                    if (bd < 13.0f) utCands.push_back(bp);
                }
            }
        }
    TrackPath base0;
    bool base0ok = false;
    std::string base0diag;
    // spoorpunten langs een perronvlak met dit nummer ("7a"; als dat niet bestaat ook "7") bij dit station, dichtstbijzijnde eerst
    auto platTrackPts = [&](Vec3 c, const char* want, float maxDist) {
        std::vector<std::pair<float, Vec3>> found;
        for (int pass = 0; pass < 2 && found.empty(); pass++) {
            std::string w = want;
            if (pass == 1) { while (!w.empty() && (w.back() < '0' || w.back() > '9')) w.pop_back(); if (w == want) break; }
            for (const OsmPlatform& op : world.platforms) {
                if (op.pts.size() < 3) continue;
                float cx = 0, cz = 0;
                for (auto& q : op.pts) { cx += q.first; cz += q.second; }
                cx /= op.pts.size(); cz /= op.pts.size();
                if (std::hypot(cx - c.x, cz - c.z) > maxDist) continue;
                const char* t = op.name.c_str();
                while (*t && (*t < '0' || *t > '9')) t++;
                if (std::string(t) != w) continue;
                for (const RailLine& r : world.rails) {
                    float bd = 13.0f;
                    Vec3 bp;
                    for (const RailPoint& q : r.pts) {
                        if (std::fabs(q.y - (op.height - 0.8f)) > 4.0f) continue;  // ander niveau (sporen boven elkaar)
                        float dd = std::hypot(q.x - cx, q.z - cz);
                        if (dd < bd) { bd = dd; bp = Vec3(q.x, q.y, q.z); }
                    }
                    if (bd < 13.0f) found.push_back({bd, bp});
                }
            }
        }
        std::sort(found.begin(), found.end(), [](const std::pair<float, Vec3>& a, const std::pair<float, Vec3>& b) { return a.first < b.first; });
        std::vector<Vec3> out;
        for (auto& f : found) out.push_back(f.second);
        return out;
    };
    // lijn naar Alkmaar: voorlopige route in rijvolgorde langs alle haltes van de rit zelf (perronspoor uit de dienstregeling,
    // anders het stationsmidden), met de uitlopers of Eindhoven Centraal aan begin en eind
    for (int variant = 0; akLine && variant < 3 && !base0ok; variant++) {
        std::vector<Vec3> base0pts;
        std::vector<bool> base0req;
        Vec3 bm, bf;
        auto endPt = [&](End e) { return variant == 0 ? bf : variant == 1 ? rightOf(e, bm, bf) : bm; };
        static const char* HELMOND_ST[] = {"Helmond Brouwhuis", "Helmond", "Helmond 't Hout", "Helmond Brandevoort"};
        // middelpunt van een uitloper op een hoofdspoor in de rijrichting (niet op een zijspoor ernaast)
        auto midPt = [&](End e) {
            if (variant != 1) return bm;
            Vec3 d = e == df.exit ? bf - bm : bm - bf, r = bm;
            rail.rankedTrack(bm, d, 30.0f, 0, r);
            return r;
        };
        if (branch(df.origin, bm, bf)) {
            if (variant < 2) { base0pts.push_back(endPt(df.origin)); base0req.push_back(true); }
            base0pts.push_back(midPt(df.origin)); base0req.push_back(variant == 2);
        }
        if (df.origin == HELMOND)
            for (const char* hs : HELMOND_ST) { Vec3 c; if (stationPt(hs, c)) { base0pts.push_back(c); base0req.push_back(false); } }
        if (df.origin == EAST || df.origin == HELMOND) for (Vec3 q : {csE, csStop, csW}) { base0pts.push_back(q); base0req.push_back(true); }
        for (const StopDef& sd : df.stops) {
            if (std::string(sd.station) == CS) continue;
            Vec3 c;
            if (!stationPt(sd.station, c)) continue;
            std::vector<Vec3> tp;
            if (sd.track) tp = platTrackPts(c, sd.track, 450);
            base0pts.push_back(tp.empty() ? c : tp[0]);
            base0req.push_back(false);
        }
        if (df.exit == EAST || df.exit == HELMOND) for (Vec3 q : {csW, csStop, csE}) { base0pts.push_back(q); base0req.push_back(true); }
        if (df.exit == HELMOND)
            for (int k = 3; k >= 0; k--) { Vec3 c; if (stationPt(HELMOND_ST[k], c)) { base0pts.push_back(c); base0req.push_back(false); } }
        if (branch(df.exit, bm, bf)) {
            base0pts.push_back(midPt(df.exit)); base0req.push_back(variant == 2);
            if (variant < 2) { base0pts.push_back(endPt(df.exit)); base0req.push_back(true); }
        }
        if (linked) { base0pts.insert(base0pts.begin(), p.linkTerminal); base0req.insert(base0req.begin(), true); }
        if (base0pts.size() < 2) break;
        base0req[0] = true;
        base0req.back() = true;
        Vec3 d0 = base0pts[1] - base0pts[0];
        for (size_t q = 1; q < base0pts.size(); q++)
            if (std::hypot(base0pts[q].x - base0pts[0].x, base0pts[q].z - base0pts[0].z) > 100) { d0 = base0pts[q] - base0pts[0]; break; }
        base0ok = rail.routeSmart(base0pts, base0req, d0.x, d0.z, base0, 0);
        if (!base0ok && variant == 0) {
            for (size_t n = 2; n <= base0pts.size(); n++) {
                std::vector<Vec3> pre(base0pts.begin(), base0pts.begin() + n);
                std::vector<bool> rq(base0req.begin(), base0req.begin() + n);
                rq.back() = true;
                TrackPath t;
                if (!rail.routeSmart(pre, rq, d0.x, d0.z, t, 0)) {
                    char b[200];
                    snprintf(b, sizeof b, " [voorlopige route: punt %d van %d (%.0f,%.0f) niet bereikbaar vanaf (%.0f,%.0f)]", (int)n - 1,
                             (int)base0pts.size(), base0pts[n - 1].x, base0pts[n - 1].z, base0pts[n - 2].x, base0pts[n - 2].z);
                    base0diag = b;
                    break;
                }
            }
        }
    }
    for (size_t uc = 0; !akLine && uc < std::max<size_t>(1, utCands.size()) && !base0ok; uc++)
    for (int variant = 0; variant < 3 && !base0ok; variant++) {
        std::vector<Vec3> base0pts;
        std::vector<bool> base0req;
        Vec3 bm, bf;
        auto endPt = [&](End e) { return variant == 0 ? bf : variant == 1 ? rightOf(e, bm, bf) : bm; };
        if (southEnd == EAST) for (Vec3 q : {csE, csStop, csW}) { base0pts.push_back(q); base0req.push_back(true); }
        if (branch(southEnd, bm, bf)) {
            if (variant < 2) { base0pts.push_back(endPt(southEnd)); base0req.push_back(true); }
            base0pts.push_back(bm); base0req.push_back(variant == 2);
        }
        if (southEnd == HELMOND) {
            // van voorbij Helmond Brouwhuis langs de Helmondse stations naar Eindhoven Centraal (oostkant binnen)
            for (const char* hs : {"Helmond Brouwhuis", "Helmond", "Helmond 't Hout", "Helmond Brandevoort"}) {
                Vec3 c;
                if (stationPt(hs, c)) { base0pts.push_back(c); base0req.push_back(false); }
            }
            for (Vec3 q : {csE, csStop, csW}) { base0pts.push_back(q); base0req.push_back(true); }
        }
        for (int k = i0; k <= i1; k++) {
            // Utrecht Centraal met een uitloper erachter: niet via het stationsmidden (dat ligt bij spoor 14; vandaar is
            // niet elk spoor naar Amsterdam/Woerden bereikbaar). Het perronspoor (anker) bepaalt daar de weg.
            if (!zwLine && k == NLS - 1 && (northEnd == UTN || northEnd == UTW)) continue;
            if (zwLine && k == 0) {
                if (!utCands.empty()) { base0pts.push_back(utCands[uc]); base0req.push_back(false); continue; }
                if (southEnd == UTW || northEnd == UTW) continue;
            }
            Vec3 c;
            if (stationPt(LINE_ST[k], c)) { base0pts.push_back(c); base0req.push_back(false); }
        }
        if (branch(northEnd, bm, bf)) {
            base0pts.push_back(bm); base0req.push_back(variant == 2);
            if (variant < 2) { base0pts.push_back(endPt(northEnd)); base0req.push_back(true); }
        }
        if (!north) {
            std::reverse(base0pts.begin(), base0pts.end());
            std::reverse(base0req.begin(), base0req.end());
        }
        if (linked) { base0pts.insert(base0pts.begin(), p.linkTerminal); base0req.insert(base0req.begin(), true); }
        base0req[0] = true;
        base0req.back() = true;
        Vec3 d0 = base0pts[1] - base0pts[0];
        for (size_t q = 1; q < base0pts.size(); q++)  // richting naar het eerste punt dat echt verder ligt (niet hetzelfde perron)
            if (std::hypot(base0pts[q].x - base0pts[0].x, base0pts[q].z - base0pts[0].z) > 100) { d0 = base0pts[q] - base0pts[0]; break; }
        if (df.origin == DENBOSCH && linked)
            d0 = (north ? rdPoint(UTRECHT_PTS[2][0], UTRECHT_PTS[2][1]) : rdPoint(ROUTE_PTS[24][0], ROUTE_PTS[24][1])) - base0pts[0];
        base0ok = rail.routeSmart(base0pts, base0req, d0.x, d0.z, base0, 0);
        if (!base0ok && variant == 0 && uc == 0) {  // diagnose: eerste vereiste punt dat niet lukt
            for (size_t n = 2; n <= base0pts.size(); n++) {
                std::vector<Vec3> pre(base0pts.begin(), base0pts.begin() + n);
                std::vector<bool> rq(base0req.begin(), base0req.begin() + n);
                rq.back() = true;
                TrackPath t;
                if (!rail.routeSmart(pre, rq, d0.x, d0.z, t, 0)) {
                    char b[200];
                    snprintf(b, sizeof b, " [voorlopige route: punt %d van %d (%.0f,%.0f) niet bereikbaar vanaf (%.0f,%.0f), start (%.0f,%.0f) richting (%.0f,%.0f)]", (int)n - 1,
                             (int)base0pts.size(), base0pts[n - 1].x, base0pts[n - 1].z, base0pts[n - 2].x, base0pts[n - 2].z, base0pts[0].x, base0pts[0].z, d0.x, d0.z);
                    base0diag = b;
                    break;
                }
            }
        }
    }
    if (!base0ok) {
        err = "Geen route " + std::string(df.number) + " naar " + df.dest + base0diag + " (" + std::to_string(utCands.size()) + " sporen Utrecht)";
        // diagnose: welk stuk (tussen twee opeenvolgende punten van de voorlopige route) is onbereikbaar?
        std::vector<Vec3> pts;
        std::vector<std::string> nm;
        Vec3 bm2, bf2;
        if (branch(southEnd, bm2, bf2)) { pts.push_back(bf2); nm.push_back("uitloper-ver"); pts.push_back(bm2); nm.push_back("uitloper-midden"); }
        for (int k = i0; k <= i1; k++) {
            Vec3 c;
            if (stationPt(LINE_ST[k], c)) { pts.push_back(c); nm.push_back(LINE_ST[k]); }
            else err += std::string(" [station ") + LINE_ST[k] + " niet gevonden]";
        }
        if (branch(northEnd, bm2, bf2)) { pts.push_back(bm2); nm.push_back("uitloper-midden"); pts.push_back(bf2); nm.push_back("uitloper-ver"); }
        for (size_t k = 0; k + 1 < pts.size(); k++) {
            TrackPath t;
            Vec3 d = pts[k + 1] - pts[k];
            std::vector<bool> rq{true, true};
            if (!rail.routeSmart({pts[k], pts[k + 1]}, rq, d.x, d.z, t, 0)) err += " [onbereikbaar: " + nm[k] + " -> " + nm[k + 1] + "]";
        }
        err += " (sporen in de wereld: " + std::to_string(world.rails.size()) + ")";
        return false;
    }
    Vec3 bm, bf;

    std::string diag;
    // ---- spoor per station (rechts rijden: Sprinter buitenspoor, Intercity middenspoor op viersporig baanvak) ----
    struct Anchor { std::string name; Vec3 p; int count; bool fixed; std::vector<Vec3> alts = {}; };  // alts: andere sporen langs dat perron
    std::vector<Anchor> anchors;
    // spoor naast een perronvlak met dit nummer ("2", "4b", "6a") bij dit station
    bool lenient = false;
    std::string wantNum;
    auto trackAt0 = [&](Vec3 c, const char* want, Vec3& out) {
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
            if (std::string(s) != want && !(akLine && lenient && std::string(s) == wantNum)) continue;
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
                    if (akLine && std::fabs(q.y - (op.height - 0.8f)) > 4.0f) continue;  // ander niveau
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
    // lijn naar Alkmaar: als er geen perronvlak "7a" is, dan "7"
    auto trackAt = [&](Vec3 c, const char* want, Vec3& out) {
        lenient = false;
        if (trackAt0(c, want, out)) return true;
        wantNum = want;
        while (!wantNum.empty() && (wantNum.back() < '0' || wantNum.back() > '9')) wantNum.pop_back();
        if (!akLine || wantNum == want) return false;
        lenient = true;
        bool ok = trackAt0(c, want, out);
        lenient = false;
        return ok;
    };
    const bool viaHelmond = df.origin == HELMOND || df.exit == HELMOND;
    for (const StopDef& s : df.stops) {
        if (std::string(s.station) == CS) {
            // lijn naar Helmond: Eindhoven Centraal ligt midden in de rit; het perronspoor (csTrack) vastleggen
            if (viaHelmond) {
                bool fromEast = df.origin == HELMOND;
                for (Vec3 q : fromEast ? std::vector<Vec3>{csE, csStop, csW} : std::vector<Vec3>{csW, csStop, csE})
                    anchors.push_back(Anchor{"(Eindhoven Centraal)", q, -1, true});
            }
            continue;
        }
        Vec3 c;
        if (!stationPt(s.station, c)) { err = std::string("Station niet gevonden: ") + s.station; return false; }
        Anchor a{s.station, c, 0, s.stop || s.track != nullptr};
        if (linked && a.name == termName(df.origin)) a.p = p.linkTerminal;
        else if (s.track && trackAt(c, s.track, a.p)) a.count = -1;
        else {
            float d = base0.closest(c.x, c.z);
            // ten noorden van 's-Hertogenbosch (Houten, Houten Castellum): Sprinters langs het eilandperron tussen de
            // binnenste sporen, Intercity's over de buitenste - dus ook voor IC's het meest rechtse spoor
            // (gemeten vanaf het stationsmidden, niet vanaf de voorlopige route: die kan aan de verkeerde kant liggen)
            int rk = c.z < -28500.0f ? 0 : rank;
            Vec3 q = c.z < -28500.0f ? Vec3(c.x, base0.pos(d).y, c.z) : base0.pos(d);
            if (!rail.rankedTrack(q, dirAt(base0, d), c.z < -28500.0f ? 14.0f : 30.0f, rk, a.p, &a.count)) a.p = base0.pos(d);
            // viersporig: ook bij doorkomst het juiste spoor vastleggen (niet vlak bij Utrecht Centraal: daar lopen de lijnen
            // naar Arnhem en 's-Hertogenbosch naast elkaar; de route naar het perronspoor zoekt de router zelf)
            // Ten noorden van 's-Hertogenbosch is alleen Houten Castellum - Houten echt viersporig; Geldermalsen en Zaltbommel
            // hebben extra perronsporen (daar zou "middenspoor" het hoofdspoor van de tegenrichting zijn).
            // Geldermalsen: het meest rechtse spoor binnen 14 m is daar het doorgaande spoor naast het eiland 3/4
            bool fourTrackLine = c.z > -28500.0f || std::string(s.station) == "Houten" || std::string(s.station) == "Houten Castellum" ||
                                 std::string(s.station) == "Geldermalsen";
            if (a.count >= 4 && fourTrackLine && std::hypot(c.x - utC.x, c.z - utC.z) > 3500) a.fixed = true;
            if (std::string(s.station) == "Geldermalsen" && !s.stop) {
                // vanaf het stationsmidden: perronsporen 3/4 op +11/-13 m, de doorgaande sporen zonder perron op +16/-17 m,
                // spoor 1 en 6 op +21/-22 m. Binnen 18 m is het meest rechtse spoor dus het doorgaande spoor.
                Vec3 r;
                int cnt = 0;
                if (rail.rankedTrack(c, dirAt(base0, d), 18.0f, 0, r, &cnt) && cnt >= 2) { a.p = r; a.count = cnt; a.fixed = true; }
            }
            // lijn naar Alkmaar: ook doorkomende treinen vastleggen op het rechter spoor (anders kan de route kilometers op
            // het linker spoor blijven); niet bij de knooppunten met sporen op meerdere niveaus
            if (akLine && !s.stop && !s.track && (a.count == 2 || a.count == 4)) {
                static const char* COMPLEX[] = {"Duivendrecht", "Amsterdam Sloterdijk", "Amsterdam Centraal", "Utrecht Centraal", "Amsterdam Muiderpoort"};
                bool cx = false;
                for (const char* n : COMPLEX) if (std::string(s.station) == n) cx = true;
                if (!cx) a.fixed = true;
            }
            if (s.track) {
                // diagnose: perronvlakken met dit nummer en de afstand van het dichtstbijzijnde spoorpunt tot hun midden
                int np = 0;
                float md = 1e9f;
                for (const OsmPlatform& op : world.platforms) {
                    if (op.pts.size() < 3) continue;
                    float cx = 0, cz = 0;
                    for (auto& q : op.pts) { cx += q.first; cz += q.second; }
                    cx /= op.pts.size(); cz /= op.pts.size();
                    const char* t = op.name.c_str();
                    while (*t && (*t < '0' || *t > '9')) t++;
                    if (std::hypot(cx - c.x, cz - c.z) > 400 || std::string(t) != s.track) continue;
                    np++;
                    for (const RailLine& r : world.rails)
                        for (const RailPoint& q : r.pts) md = std::min(md, std::hypot(q.x - cx, q.z - cz));
                }
                char b4[96];
                snprintf(b4, sizeof b4, " [%d perrons, spoorpunt op %.1f m]", np, md);
                diag += std::string(" (spoor ") + s.track + " in " + s.station + " niet gevonden" + b4 + ")";
            }
        }
        // Amsterdam Centraal: perrons in een a- en b-helft met wissels halverwege. Wie van de b-kant (oost) naar een a-helft
        // rijdt (of van de a-kant naar een b-helft), eerst langs de eigen andere helft - anders steekt de route halverwege
        // over vanaf het naastgelegen spoor (waar dan net een trein kan staan)
        if (akLine && s.track && std::string(s.station) == "Amsterdam Centraal" && a.count == -1) {
            std::string t = s.track;
            char suf = t.empty() ? 0 : t.back();
            if ((north && suf == 'a') || (!north && suf == 'b')) {
                std::string other = t.substr(0, t.size() - 1) + (suf == 'a' ? "b" : "a");
                Anchor h{"(" + std::string(s.station) + " " + other + ")", c, -1, true};
                if (trackAt(c, other.c_str(), h.p)) anchors.push_back(h);
            }
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
    // controlepunt ~1 km ten noorden van 's-Hertogenbosch (lijn naar Utrecht): rechter spoor, zodat een trein niet al
    // kilometers voor het station naar een perronspoor aan de andere kant oversteekt
    if (i0 <= 4 && i1 > 4 && (df.exit == DENBOSCH || df.origin == DENBOSCH)) {  // alleen ritten die daar keren
        float dDB = base0.closest(dbC.x, dbC.z);
        float dc = north ? dDB + 1000 : dDB - 1000;
        Vec3 r;
        int cnt = 0;
        if (dc > 0 && dc < base0.length && rail.rankedTrack(base0.pos(dc), dirAt(base0, dc), 14.0f, 0, r, &cnt) && cnt >= 2) {
            Anchor a{"(Den Bosch-noord)", r, cnt, true};
            auto it = std::find_if(anchors.begin(), anchors.end(), [](const Anchor& x) { return x.name == "'s-Hertogenbosch"; });
            if (it == anchors.end()) anchors.push_back(a);
            else if (north) anchors.insert(it + 1, a);
            else anchors.insert(it, a);
        }
    }
    // controlepunt op een uitloper (Tilburg-, Tiel-lijn, voorbij Utrecht): rechter spoor per rijrichting
    for (End e : {df.origin, df.exit}) {
        if (!branch(e, bm, bf)) continue;
        bool out = e == df.exit;  // naar buiten rijden
        Vec3 d = out ? bf - bm : bm - bf, r;
        int cnt = 0;
        if (rail.rankedTrack(bm, d, 30.0f, branchRank(e), r, &cnt) && cnt >= 2) {
            Anchor a{e == TILBURG ? "(Tilburg-lijn)" : e == TIEL ? "(Tiel-lijn)" : e == ZWN ? "(voorbij Zwolle)" : e == AMFE ? "(Apeldoorn-lijn)" : e == HELMOND ? "(voorbij Helmond)" : e == AKN ? "(voorbij Alkmaar)" : e == UTO ? "(Bunnik-lijn)" : e == BKW ? "(Woerden-lijn)" :
                     e == HLM ? "(Haarlem-lijn)" : e == HRN ? "(Purmerend-lijn)" : e == BVW ? "(Beverwijk-lijn)" : e == DVZ ? "(Amsterdam Zuid-lijn)" : "(voorbij Utrecht)", r, cnt, true};
            if (out) anchors.push_back(a);
            else anchors.insert(anchors.begin(), a);
        }
    }
    // lijn naar Alkmaar: controlepunten op dubbelspoor zonder station (rechter spoor in de rijrichting), in rijvolgorde
    if (akLine) {
        // left: hier het linker spoor (Muiderpoort - Centraal: vanaf spoor 8 sluit in de spoordata alleen het linker spoor
        // aan, dus rijden de treinen daar links langs elkaar)
        static const struct { const char* name; double x, y; bool left; } CP[] = {{"(Muiderpoort - Centraal)", 123462, 487575, true},
                                                                              {"(Zaandam - Kogerveld)", 115905, 496489, true}};
        for (auto& cp : CP) {
            Vec3 q = rdPoint(cp.x, cp.y);
            float dq = base0.closest(q.x, q.z);
            Vec3 on = base0.pos(dq);
            if (std::hypot(on.x - q.x, on.z - q.z) > 60) continue;  // rit komt hier niet langs
            Vec3 r;
            int cnt = 0;
            Vec3 dd = dirAt(base0, dq);
            if (cp.left) dd = dd * -1.0f;
            if (!rail.rankedTrack(Vec3(q.x, on.y, q.z), dd, 14.0f, 0, r, &cnt) || cnt < 2) continue;
            Anchor a{cp.name, r, cnt, true};
            auto it = anchors.begin();
            while (it != anchors.end() && base0.closest(it->p.x, it->p.z) < dq) ++it;
            anchors.insert(it, a);
            diag += std::string(" ") + cp.name + ":" + std::to_string(cnt) + " sporen";
        }
    }
    // ---- definitieve route ----
    // eind- en beginpunten op een uitloper: het rechter spoor voor deze rijrichting
    auto farPt = [&](End e, Vec3& out) {
        if (!branch(e, bm, bf)) return false;
        Vec3 d = e == df.exit ? bf - bm : bm - bf;
        out = bf;
        rail.rankedTrack(bf, d, 30.0f, branchRank(e), out);
        return true;
    };
    auto assemble = [&]() {
        std::vector<Vec3> f;
        Vec3 q;
        if (df.origin == EAST) f = {csE, csStop, csW};
        else if (farPt(df.origin, q)) f.push_back(q);
        for (auto& a : anchors) if (a.fixed) f.push_back(a.p);
        if (df.exit == EAST) for (Vec3 c : {csW, csStop, csE}) f.push_back(c);
        else if (farPt(df.exit, q)) f.push_back(q);
        return f;
    };
    std::vector<Vec3> fixed = assemble();
    Vec3 dir;
    if (df.origin == EAST) dir = csW - csE;
    else {
        float d = base0.closest(fixed[0].x, fixed[0].z);
        dir = dirAt(base0, d + 15);
    }
    std::vector<std::pair<Vec3, float>> protect{{csStop, YARD_ZONE}, {dbC, 120.0f}, {boxC, 600.0f}, {utC, 1300.0f}};
    if (df.origin == TIEL || df.exit == TIEL) protect.push_back({gdmC, 700.0f});  // aftakking: de router kiest de wissels zelf
    if (zwLine) { protect.push_back({amfC, 1100.0f}); protect.push_back({zwC, 1100.0f}); }  // grote emplacementen
    // lijn naar Alkmaar: emplacementen en knooppunten (de router kiest daar de wissels zelf)
    std::vector<std::pair<Vec3, float>> akYards;
    if (akLine) {
        const std::pair<const char*, float> AKY[] = {{"Amsterdam Centraal", 1300.0f}, {"Amsterdam Sloterdijk", 900.0f}, {"Duivendrecht", 800.0f},
                                                     {"Amsterdam Amstel", 500.0f}, {"Zaandam", 700.0f}, {"Uitgeest", 600.0f}, {"Alkmaar", 900.0f},
                                                     {"Breukelen", 500.0f}, {"Amsterdam Muiderpoort", 500.0f}};
        for (auto& y : AKY) { Vec3 c; if (stationPt(y.first, c)) { protect.push_back({c, y.second}); akYards.push_back({c, y.second}); } }
    }
    float extEnd = isTerm(df.exit) ? 160.0f : 6000.0f;
    TrackPath main;
    // onbereikbaar vast punt (bijv. ontbrekende overloopwissel in de OSM-data): dat station terugvallen op rechts rijden
    for (int attempt = 0; attempt < 30; attempt++) {
        std::vector<bool> rq(fixed.size(), true);
        TrackPath t;
        size_t bad = rail.firstUnreachable(fixed, dir.x, dir.z);  // eerste onbereikbare vaste punt (0 = route lukt)
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
        fixed = assemble();
    }
    if (!smartRoute(rail, fixed, dir, rank, extEnd, protect, main)) {
        err = "Geen route " + std::string(df.number) + " naar " + df.dest + ": vast punt";
        for (size_t n = 2; n <= fixed.size(); n++) {
            std::vector<Vec3> pre(fixed.begin(), fixed.begin() + n);
            std::vector<bool> rq(n, true);
            TrackPath t;
            if (!rail.routeSmart(pre, rq, dir.x, dir.z, t, 0)) {
                err += " " + std::to_string(n - 1) + " van " + std::to_string(fixed.size()) + " onbereikbaar";
                for (size_t q : {n - 2, n - 1})
                    for (auto& a : anchors)
                        if (a.fixed && std::hypot(a.p.x - fixed[q].x, a.p.z - fixed[q].z) < 0.1f) { err += std::string(q == n - 1 ? " -> " : " (van ") + a.name; break; }
                err += ")";
                break;
            }
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
        float len = isTerm(df.origin) ? 170.0f : 6000.0f;
        if (rail.routeSmart({a0, farPt}, req, away.x, away.z, ap, len) && ap.pts.size() >= 2) {
            if (!isTerm(df.origin)) {
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
    extrapolate(main, !isTerm(df.origin), !isTerm(df.exit));
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
            if (akLine && std::fabs(q.y - (op.height - 0.8f)) > 4.0f) continue;  // perron op een ander niveau
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
            if (isTerm(df.exit) && s.station == std::string(termName(df.exit))) p.terminal = main.pos(t.d);
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
    std::vector<Vec3> yards{csStop, dbC, utC};
    if (zwLine) { yards.push_back(amfC); yards.push_back(zwC); }
    for (Vec3 c : yards) {
        float d = main.closest(c.x, c.z);
        float zone = c.x == utC.x && c.z == utC.z ? 1300.0f : YARD_ZONE;  // Utrecht: groot emplacement
        if (std::hypot(main.pos(d).x - c.x, main.pos(d).z - c.z) < 200) p.zones.push_back({d - zone, d + zone, 22.2f});
    }
    // lijn naar Alkmaar: 80 km/u op de grote emplacementen (Amsterdam Centraal, Alkmaar), 100 km/u bij de knooppunten
    for (auto& y : akYards) {
        float d = main.closest(y.first.x, y.first.z);
        if (std::hypot(main.pos(d).x - y.first.x, main.pos(d).z - y.first.z) < 250)
            p.zones.push_back({d - y.second * 0.8f, d + y.second * 0.8f, y.second >= 900.0f ? 22.2f : 27.8f});
    }
    if (toTilburg) {
        float d = main.closest(boxC.x, boxC.z);
        p.zones.push_back({d - 450, d + 450, 22.2f});
    }
    if (df.origin == TIEL || df.exit == TIEL) {  // aftakking Geldermalsen
        float d = main.closest(gdmC.x, gdmC.z);
        p.zones.push_back({d - 500, d + 500, 22.2f});
    }
    std::sort(p.zones.begin(), p.zones.end(), [](const SpeedZone& a, const SpeedZone& b) { return a.d0 < b.d0; });
    snprintf(buf, sizeof(buf), "%s %s naar %s (%s, CS spoor %d): pad %.0f m\n   sporen:", p.rs->product.c_str(), df.number, df.dest,
             p.rs->typeName.c_str(), df.csTrack, main.length);
    p.info = buf + diag + "\n";
    // diagnose: per 500 m welk spoor (0 = meest rechts in de rijrichting, binnen 14 m) - '.' = enkel spoor
    if (verbose) {
        std::string lane = "   rijstrook per 500 m: ";
        for (float d = 0; d < main.length; d += 500) {
            Vec3 q = main.pos(d), dd = dirAt(main, d);
            float dl = std::hypot(dd.x, dd.z);
            if (dl < 1e-3f) { lane += '?'; continue; }
            float rx = -dd.z / dl, rz = dd.x / dl;
            int right = 0, total = 0;
            for (const RailLine& rl : world.rails) {
                if (rl.pts.size() < 2) continue;
                float bestLat = 1e9f;
                for (size_t k = 0; k + 1 < rl.pts.size(); k++) {
                    float ax = rl.pts[k].x - q.x, az = rl.pts[k].z - q.z;
                    float along = (ax * dd.x + az * dd.z) / dl;
                    if (std::fabs(along) > 30) continue;
                    float lat = ax * rx + az * rz;
                    if (std::fabs(lat) < std::fabs(bestLat)) bestLat = lat;
                }
                if (std::fabs(bestLat) < 14.0f) { total++; if (bestLat > 1.2f) right++; }
            }
            lane += total <= 1 ? '.' : (char)('0' + std::min(9, right));
        }
        p.info += lane + "\n";
    }
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
    if (S[0].arr < 0) S[0].arr = S[0].dep - (isTerm(df.origin) ? 0 : (S[0].station == CS || S[0].station == "'s-Hertogenbosch") ? 120 : 40);
    p.entrySpeed = std::min(rs.vmax, LINE_MAX);
    if (!isTerm(df.origin)) p.entryRel = S[0].arr - simulate(rs, p.zones, half, half + 1, S[0].d, p.entrySpeed, true, LINE_MAX).t;
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
        // Intercity's op de lijn naar Alkmaar rijden door en wachten de speling op het station uit (anders worden ze tussen
        // Abcoude en Amsterdam Amstel ingehaald door het moment waarop de Sprinter voor ze op hetzelfde spoor komt)
        const bool fullSpeed = rs.product == "Intercity" && (akEnd(df.origin) || akEnd(df.exit));
        if (budget > 0 && tmin < budget * MARGIN && !fullSpeed) {
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
    auto tSetup = std::chrono::steady_clock::now();
    stock_.emplace_back(makeFlirt());
    stock_.emplace_back(makeVirm());
    stock_.emplace_back(makeIcng());  // 2
    // Dienstregeling 2026 (OV in Nederland Wiki; 's-Hertogenbosch - Utrecht uit de GTFS van OVapi, november 2026), vertrektijden
    // per station. Minuten na het begin van het halfuur (de rit een halfuur later rijdt 30 minuten later; tijden > 30 lopen
    // door in het volgende halfuur). -1 = uitrekenen uit de rijtijd. Sporen in Den Bosch en Utrecht zoals in de GTFS.
    // Intercity's met VIRM, behalve de IC 1100 (Eindhoven - Den Haag) en IC 3000 (Nijmegen - Den Helder): ICNG. Sprinters met FLIRT.
    // Utrecht: de IC 3500 naar Dordrecht (via Woerden/Gouda) vertrekt van spoor 8 - in de spoordata sluiten alleen 8/9/11/12
    // en 20/21 aan op de lijn naar Woerden (in werkelijkheid spoor 7). De 3500 naar Venlo komt van de kant van Amsterdam
    // binnen en vertrekt, zoals in de GTFS, van spoor 18.
    const char *ES = "Eindhoven Strijp-S", *BE = "Best", *BX = "Boxtel", *VU = "Vught", *DB = "'s-Hertogenbosch";
    const char *ZB = "Zaltbommel", *GM = "Geldermalsen", *CL = "Culemborg", *HC = "Houten Castellum", *HT = "Houten",
               *UL = "Utrecht Lunetten", *UV = "Utrecht Vaartsche Rijn", *UT = "Utrecht Centraal";
    const char *HBV = "Helmond Brandevoort", *HTH = "Helmond 't Hout", *HM = "Helmond", *HBW = "Helmond Brouwhuis";
    const char *UO = "Utrecht Overvecht", *BH = "Bilthoven", *DD = "Den Dolder", *AMF = "Amersfoort Centraal", *AS = "Amersfoort Schothorst",
               *AV = "Amersfoort Vathorst", *NK = "Nijkerk", *PT = "Putten", *EM = "Ermelo", *HD = "Harderwijk", *NS = "Nunspeet",
               *TH = "'t Harde", *WZ = "Wezep", *ZL = "Zwolle";
    // Intercity's Utrecht - Amersfoort - Zwolle: alle tussenstations zonder te stoppen
    // doorkomen langs dezelfde sporen als de Sprinter in die richting (dubbelspoor; anders blijft de route op het linker spoor hangen)
    auto icZw = [&](double utA, double utD, const char* utT, double amA, double amD, double zlA, double zlD, const char* zlT) {
        return std::vector<StopDef>{{UT, true, utA, utD, utT}, {UO, false, -1, -1, "1"}, {BH, false, -1, -1, "2"}, {DD, false, -1, -1, "2"},
                                    {AMF, true, amA, amD, "2"}, {AS, false, -1, -1, "3"}, {AV, false, -1, -1, "3"}, {NK, false, -1, -1, "2"},
                                    {PT, false, -1, -1, "1"}, {EM, false, -1, -1, "1"}, {HD, false, -1, -1, "1"}, {NS, false, -1, -1, "1"},
                                    {TH, false, -1, -1, "2"}, {WZ, false, -1, -1, "1"}, {ZL, true, zlA, zlD, zlT}};
    };
    auto icZwSouth = [&](double zlA, double zlD, double amA, double amD, double utA, double utD, const char* utT) {
        return std::vector<StopDef>{{ZL, true, zlA, zlD, "5"}, {WZ, false, -1, -1, "2"}, {TH, false, -1, -1, "1"}, {NS, false, -1, -1, "2"},
                                    {HD, false, -1, -1, "2"}, {EM, false, -1, -1, "2"}, {PT, false, -1, -1, "2"}, {NK, false, -1, -1, "1"},
                                    {AV, false, -1, -1, "1"}, {AS, false, -1, -1, "1"}, {AMF, true, amA, amD, "6"}, {DD, false, -1, -1, "1"},
                                    {BH, false, -1, -1, "1"}, {UO, false, -1, -1, "3"}, {UT, true, utA, utD, utT}};
    };
    // Amsterdam Centraal: Sprinters richting Utrecht van 8b (in de GTFS 2b; dat perron ontbreekt in de OSM-data)
    // Amsterdam Sloterdijk noordwaarts: spoor 3 (in de GTFS soms 4; in de spoordata komt de route vanaf 4 niet in Zaandam)
    // Intercity's tussen 's-Hertogenbosch en Utrecht: alle tussenstations zonder te stoppen
    auto icNorth = [&](double dbArr, double dbDep, double utArr, double utDep, const char* utTrack = "7") {
        // doorkomen langs de doorgaande sporen: Zaltbommel 1/2, Culemborg 1/2 (Geldermalsen: rechts rijden, niet langs de
        // perronsporen 3/6 waar de Sprinters 6 minuten staan)
        return std::vector<StopDef>{{DB, true, dbArr, dbDep, "3a"}, {ZB, false, -1, -1, "1"}, {GM, false, -1, -1}, {CL, false, -1, -1, "1"},
                                    {HC, false, -1, -1}, {HT, false, -1, -1}, {UL, false, -1, -1}, {UV, false, -1, -1},
                                    {UT, true, utArr, utDep, utTrack}};
    };
    auto icSouth = [&](double utArr, double utDep, double dbArr, double dbDep, const char* utTrack = "18") {
        return std::vector<StopDef>{{UT, true, utArr, utDep, utTrack}, {UV, false, -1, -1}, {UL, false, -1, -1}, {HT, false, -1, -1},
                                    {HC, false, -1, -1}, {CL, false, -1, -1, "2"}, {GM, false, -1, -1}, {ZB, false, -1, -1, "2"},
                                    {DB, true, dbArr, dbDep, "6a"}};
    };
    auto cat = [](std::vector<StopDef> a, const std::vector<StopDef>& b) { a.insert(a.end(), b.begin(), b.end()); return a; };
    // ---- lijn Utrecht - Amsterdam - Zaandam - Alkmaar (GTFS november 2026) ----
    const char *UZ = "Utrecht Zuilen", *MS = "Maarssen", *BKN = "Breukelen", *AC = "Abcoude", *AHD = "Amsterdam Holendrecht",
               *ABA = "Amsterdam Bijlmer ArenA", *DVD = "Duivendrecht", *ASA = "Amsterdam Amstel", *ASM = "Amsterdam Muiderpoort",
               *ASDC = "Amsterdam Centraal", *ASS = "Amsterdam Sloterdijk", *ZD = "Zaandam", *KZD = "Koog aan de Zaan",
               *ZZS = "Zaandijk Zaanse Schans", *WM = "Wormerveer", *KMA = "Krommenie-Assendelft", *UTG = "Uitgeest", *CAS = "Castricum",
               *HLO = "Heiloo", *AMR = "Alkmaar";
    static const char* LINE_AK[] = {"Utrecht Centraal", "Utrecht Zuilen", "Maarssen", "Breukelen", "Abcoude", "Amsterdam Holendrecht",
                                    "Amsterdam Bijlmer ArenA", "Duivendrecht", "Amsterdam Amstel", "Amsterdam Muiderpoort", "Amsterdam Centraal",
                                    "Amsterdam Sloterdijk", "Zaandam", "Koog aan de Zaan", "Zaandijk Zaanse Schans", "Wormerveer",
                                    "Krommenie-Assendelft", "Uitgeest", "Castricum", "Heiloo", "Alkmaar"};
    // tussen twee opeenvolgende haltes op deze lijn de stations ertussen invoegen (doorkomst, rechts rijden)
    auto akFill = [&](const std::vector<StopDef>& in) {
        auto idx = [&](const char* n) {
            for (int k = 0; k < (int)(sizeof(LINE_AK) / sizeof(LINE_AK[0])); k++) if (std::string(LINE_AK[k]) == n) return k;
            return -1;
        };
        std::vector<StopDef> out;
        for (size_t k = 0; k < in.size(); k++) {
            if (k > 0) {
                int a = idx(in[k - 1].station), b = idx(in[k].station);
                if (a >= 0 && b >= 0) {
                    int st = b > a ? 1 : -1;
                    for (int m = a + st; m != b; m += st) {
                        // doorkomst langs de sporen van de Sprinter in die richting (dubbelspoor langs het perron; anders kan
                        // de route kilometers op het spoor van de tegenrichting blijven). Utrecht Zuilen - Abcoude: viersporig,
                        // de Sprinters langs het eiland in het midden, doorgaande treinen buitenom (rechts rijden, geen vast spoor)
                        static const struct { const char* st; const char *n, *s; } PASS[] = {
                            {"Amsterdam Muiderpoort", "8", "9"}, {"Koog aan de Zaan", "1", "2"},
                            {"Zaandijk Zaanse Schans", "1", "2"}, {"Wormerveer", "2", "3"}, {"Krommenie-Assendelft", "1", "2"},
                            {"Castricum", "1", "2"}, {"Heiloo", "1", "2"},
                            {"Amsterdam Bijlmer ArenA", "1", "8"},
                            {"Uitgeest", "3", "3"}};  // niet langs 1b/4b, waar de Sprinters keren  // IC's buitenom, niet achter de Sprinters (3/6) aan
                        const char* tr = nullptr;
                        for (auto& q : PASS) if (std::string(q.st) == LINE_AK[m]) tr = st > 0 ? q.n : q.s;
                        out.push_back({LINE_AK[m], false, -1, -1, tr});
                    }
                }
            }
            out.push_back(in[k]);
        }
        return out;
    };
    const std::vector<StopDef> ehvNorth{{ES, false, -1, -1}, {BE, false, -1, -1}, {BX, false, -1, -1, "2"}, {VU, false, -1, -1, "1"}};
    const std::vector<StopDef> ehvSouth{{VU, false, -1, -1, "2"}, {BX, false, -1, -1, "3"}, {BE, false, -1, -1}, {ES, false, -1, -1}};
    std::vector<Def> defs = {
        // ---- noordwaarts (vanaf Eindhoven Centraal) ----
        {"2700", "Alkmaar", 1, 5, EAST, AKM, akFill(cat(cat(cat({{CS, true, -1, 4}}, ehvNorth), icNorth(26, 30, 57, 59)),
                                                       {{ASA, true, -1, 77.5, "1"}, {ASDC, true, 85, 88, "7a"}, {ASS, true, 93, 95, "3"},
                                                        {ZD, true, -1, 102.5, "2"}, {CAS, true, -1, 114.5, "1"}, {HLO, false, -1, -1}, {AMR, true, 123, -1, "3"}}))},
        // uit Deurne via Helmond (GTFS; een halfuur eerder gerekend dan Eindhoven, zodat alle tijden positief zijn)
        {"6500", "'s-Hertogenbosch", 0, 6, HELMOND, DENBOSCH, {{HBW, true, -1, 12, "1"}, {HM, true, -1, 16, "1"}, {HTH, true, -1, 19, "1"}, {HBV, true, -1, 21, "1"},
                                                                 {CS, true, 29, 35}, {ES, true, -1, 39}, {BE, true, -1, 44}, {BX, true, -1, 51, "2"}, {VU, true, -1, 58, "1"}, {DB, true, -1, -1, "4b"}}},
        {"1100", "Den Haag Centraal", 2, 5, EAST, TILBURG, {{CS, true, -1, 13}, {ES, false, -1, -1}, {BE, false, -1, -1}, {BX, false, -1, -1, "6"}}},
        {"3500", "Dordrecht", 1, 6, HELMOND, DVZ, akFill(cat(cat(cat({{HM, true, -1, 1, "1"}, {HTH, false, -1, -1, "1"}, {HBV, false, -1, -1, "1"}, {CS, true, 11, 16}}, ehvNorth),
                                                                   icNorth(36, 39, 65, 71, "8")), {{ABA, true, -1, 88.5, "2"}}))},
        {"6400", "Tilburg Universiteit", 0, 4, EAST, TILBURG, {{CS, true, -1, 21}, {ES, true, -1, 24}, {BE, true, -1, 30}, {BX, true, -1, 37, "6"}}},
        {"3900", "Enkhuizen", 1, 5, EAST, HRN, akFill(cat(cat(cat({{CS, true, -1, 26}}, ehvNorth), icNorth(46, 50, 76, 79)),
                                                         {{ASA, true, -1, 97.5, "1"}, {ASDC, true, 105, 108, "7a"}, {ASS, true, -1, 114.5, "3"}, {ZD, false, -1, -1}}))},
        // ---- zuidwaarts (naar Eindhoven Centraal) ----
        {"6500", "Deurne", 0, 2, DENBOSCH, HELMOND, {{DB, true, -1, 28}, {VU, true, -1, 32.5, "2"}, {BX, true, -1, 39, "3"}, {BE, true, -1, 45}, {ES, true, -1, 51}, {CS, true, -1, 66}, {HBV, true, -1, 73, "2"}, {HTH, true, -1, 76, "2"}, {HM, true, -1, 80, "2"}, {HBW, true, -1, 83, "2"}}, 1, -60},
        {"3900", "Heerlen", 1, 1, HRN, EAST, akFill(cat(cat(cat({{ZD, false, -1, -1}, {ASS, true, -1, 5.5, "6"}, {ASDC, true, 11, 14, "4b"}, {ASA, true, -1, 22.5, "4"}},
                                                                icSouth(41, 44, 72, 75)), ehvSouth), {{CS, true, 95, 98}}))},
        {"3500", "Venlo", 1, 2, DVZ, HELMOND, akFill(cat(cat(cat({{ABA, true, 1, 2, "7"}}, icSouth(18, 24, 52, 55)), ehvSouth),
                                                          {{CS, true, 74.5, 79}, {HBV, false, -1, -1, "2"}, {HTH, false, -1, -1, "2"}, {HM, true, 88, 89, "2"}}))},
        {"2700", "Maastricht", 1, 3, AKM, EAST, akFill(cat(cat(cat({{AMR, true, -1, 27, "3"}, {HLO, false, -1, -1}, {CAS, true, -1, 35.5, "2"}, {ZD, true, -1, 49.5, "4"},
                                                                    {ASS, true, 55, 56, "5"}, {ASDC, true, 62, 64, "4b"}, {ASA, true, -1, 72.5, "4"}},
                                                                   icSouth(91, 94, 122, 124)), ehvSouth), {{CS, true, 144, 147}})), 0, -120},
        {"6400", "Weert", 0, 3, TILBURG, EAST, {{BX, true, -1, 23, "7"}, {BE, true, -1, 29}, {ES, true, -1, 35}, {CS, true, -1, 47}}},
        // ---- Sprinters 's-Hertogenbosch - Utrecht (8800, kopmaken in Den Bosch) en Tiel - Utrecht (6900) ----
        {"8800", "'s-Hertogenbosch", 0, 0, UTW, DENBOSCH,
         {{UT, true, 6, 10, "21"}, {UV, true, -1, 12, "4"}, {UL, true, -1, 15, "2"}, {HT, true, 18, 19, "2"}, {HC, true, -1, 22, "2"},
          {CL, true, -1, 28, "2"}, {GM, true, 36, 42, "6"}, {ZB, true, -1, 48, "2"}, {DB, true, 57, -1, "4a"}}},
        {"8800", "Bodegraven", 0, 0, DENBOSCH, UTW,
         {{DB, true, -1, 3, "4a"}, {ZB, true, -1, 11, "1"}, {GM, true, 18, 24, "3"}, {CL, true, -1, 29, "1"}, {HC, true, 35, 36, "1"},
          {HT, true, -1, 39, "1"}, {UL, true, -1, 43, "1"}, {UV, true, -1, 46, "3"}, {UT, true, 49, 54, "20"}}, 11, -60},
        {"6900", "Den Haag Centraal", 0, 0, TIEL, UTW,
         {{GM, true, 34, 44, "1"}, {CL, true, -1, 50, "1"}, {HC, true, -1, 56, "1"}, {HT, true, -1, 59, "1"}, {UL, true, -1, 63, "1"},
          {UV, true, -1, 66, "3"}, {UT, true, 69, 74, "20"}}},
        {"6900", "Tiel", 0, 0, UTW, TIEL,
         {{UT, true, 14, 20, "21"}, {UV, true, -1, 22, "4"}, {UL, true, -1, 25, "2"}, {HT, true, 28, 29, "2"}, {HC, true, -1, 32, "2"},
          {CL, true, -1, 38, "2"}, {GM, true, 46, 54, "4"}}},
        // ---- Utrecht - Amersfoort - Zwolle (GTFS november 2026) ----
        // IC 500 (Groningen / Den Haag) en IC 600 (Leeuwarden / Den Haag) om en om elk uur; samen elk halfuur. De lijnen naar
        // Woerden/Gouda en naar Amersfoort komen allebei aan de noordkant van Utrecht Centraal binnen: deze IC's keren daar
        // (kopmaken). In de spoordata sluiten alleen 8 en 9 op allebei aan (in werkelijkheid noordwaarts 11/12).
        // Daarom per richting twee ritten: Woerden-kant -> Utrecht, en Utrecht -> verder (gekoppeld, zelfde trein).
        // Noordwaarts beginnen ze daarom op het perron in Utrecht (5 minuten voor vertrek klaar; in de spoordata is er vanaf
        // het rechter spoor van de Woerden-lijn geen verbinding naar 8/9). Zuidwaarts komen ze binnen, keren en rijden rechts
        // de Woerden-lijn op.
        {"500", "Groningen", 1, 0, UTC, ZWN, icZw(-1, 21, "5", 34, 36, 71, 76, "6"), -1, 0, 1},                      // 15
        {"600", "Leeuwarden", 1, 0, UTC, ZWN, icZw(-1, 21, "5", 34, 36, 71, 78, "7"), -1, 0, 2},                     // 16
        {"500", "Den Haag Centraal", 1, 0, ZWN, UTC, icZwSouth(14, 19, 54, 56, 69, -1, "9"), -1, 0, 2},             // 17
        {"500", "Den Haag Centraal", 1, 0, UTC, UTW, {{UT, true, -1, 73, "9"}}, 17, 0, 2},                            // 18
        {"600", "Den Haag Centraal", 1, 0, ZWN, UTC, icZwSouth(11, 19, 54, 56, 69, -1, "9"), -1, 0, 1},             // 19
        {"600", "Den Haag Centraal", 1, 0, UTC, UTW, {{UT, true, -1, 73, "9"}}, 19, 0, 1},                            // 20
        // IC 1700 Den Haag - Enschede: tot Amersfoort over deze lijn, dan richting Apeldoorn
        {"1700", "Enschede", 1, 0, UTC, AMFE, {{UT, true, -1, 7, "5"}, {UO, false, -1, -1, "1"}, {BH, false, -1, -1, "2"}, {DD, false, -1, -1, "2"}, {AMF, true, 20, 22, "2"}}},  // 21
        {"1700", "Den Haag Centraal", 1, 0, AMFE, UTC, {{AMF, true, 8, 10, "6"}, {DD, false, -1, -1, "1"}, {BH, false, -1, -1, "1"}, {UO, false, -1, -1, "3"}, {UT, true, 23, -1, "9"}}},  // 22
        {"1700", "Den Haag Centraal", 1, 0, UTC, UTW, {{UT, true, -1, 28, "9"}}, 22, 0},                             // 23
        // SPR 5600 Utrecht - Zwolle, alle stations; kopmaken in Utrecht (spoor 4) en Zwolle (spoor 1a/1b)
        {"5600", "Zwolle", 0, 0, UTC, ZWOLLE,
         {{UT, true, -1, 22, "4"}, {UO, true, -1, 25.5, "1"}, {BH, true, -1, 30, "2"}, {DD, true, -1, 33, "2"}, {AMF, true, 40, 42, "2"},
          {AS, true, -1, 46, "3"}, {AV, true, -1, 49, "3"}, {NK, true, -1, 54, "2"}, {PT, true, -1, 59, "1"}, {EM, true, -1, 64, "1"},
          {HD, true, -1, 68, "1"}, {NS, true, -1, 76, "1"}, {TH, true, -1, 82, "2"}, {WZ, true, -1, 89, "1"}, {ZL, true, 96, -1, "1b"}}, 25, -90},  // 24
        {"5600", "Utrecht Centraal", 0, 0, ZWOLLE, UTC,
         {{ZL, true, -1, 23, "1a"}, {WZ, true, -1, 30, "2"}, {TH, true, -1, 36, "1"}, {NS, true, -1, 42, "2"}, {HD, true, -1, 50, "2"},
          {EM, true, -1, 54, "2"}, {PT, true, -1, 59, "2"}, {NK, true, -1, 64, "1"}, {AV, true, -1, 69, "1"}, {AS, true, -1, 72, "1"},
          {AMF, true, 76, 79, "6"}, {DD, true, -1, 86, "1"}, {BH, true, -1, 90, "1"}, {UO, true, -1, 95, "3"}, {UT, true, 99, -1, "4"}}, 24, -90},  // 25
        // ---- Utrecht - Amsterdam - Zaandam - Alkmaar ----
        // IC 3000 Den Helder (in de GTFS overdag tot Heerhugowaard) - Utrecht - Arnhem - Nijmegen: van/naar Bunnik (de mist in).
        // Utrecht noordwaarts van spoor 7 (in werkelijkheid 5; dat is hier bezet door de IC 1700)
        {"3000", "Heerhugowaard", 2, 0, UTO, AKN, akFill({{UV, false, -1, -1}, {UT, true, 6, 9, "7"}, {ASA, true, -1, 28.5, "1"}, {ASDC, true, 35, 39, "7a"},
                                                          {ASS, true, -1, 44.5, "3"}, {ZD, true, -1, 52.5, "1"}, {CAS, true, -1, 64.5, "1"}, {HLO, true, -1, 70.5, "1"},
                                                          {AMR, true, 76, 79, "2"}})},  // 26
        {"3000", "Nijmegen", 2, 0, AKN, UTO, akFill({{AMR, true, 10, 13, "5"}, {HLO, true, -1, 19.5, "2"}, {CAS, true, -1, 25.5, "2"}, {ZD, true, -1, 38.5, "4"},
                                                     {ASS, true, -1, 45.5, "5"}, {ASDC, true, 51, 54, "4b"}, {ASA, true, 61, 62, "4"}, {UT, true, 81, 84, "19"},
                                                     {UV, false, -1, -1}})},  // 27
        // SPR 4000 Uitgeest - Breukelen - (Woerden - Rotterdam): kopmaken in Uitgeest (spoor 1b)
        {"4000", "Uitgeest", 0, 0, BKW, UITG, akFill({{BKN, true, -1, 11.5, "2"}, {AC, true, -1, 20.5, "2"}, {AHD, true, -1, 23.5, "2"}, {ABA, true, -1, 25.5, "3"},
                                                      {DVD, true, -1, 28.5, "5"}, {ASA, true, -1, 32.5, "1"}, {ASM, true, -1, 35.5, "8"}, {ASDC, true, 41, 43, "5a"},
                                                      {ASS, true, -1, 48.5, "3"}, {ZD, true, -1, 55.5, "2"}, {KZD, true, -1, 58.5, "1"}, {ZZS, true, -1, 60.5, "1"},
                                                      {WM, true, 63, 64, "2"}, {KMA, true, -1, 67.5, "1"}, {UTG, true, 72, -1, "1b"}})},  // 28
        {"4000", "Rotterdam Centraal", 0, 0, UITG, BKW, akFill({{UTG, true, -1, 18, "1b"}, {KMA, true, -1, 22.5, "2"}, {WM, true, -1, 26.5, "3"}, {ZZS, true, -1, 29.5, "2"},
                                                                {KZD, true, -1, 32.5, "2"}, {ZD, true, -1, 35.5, "5"}, {ASS, true, -1, 41.5, "6"}, {ASDC, true, 47, 49, "8b"},
                                                                {ASM, true, -1, 54.5, "9"}, {ASA, true, -1, 58.5, "4"}, {DVD, true, -1, 61.5, "8"}, {ABA, true, -1, 64.5, "6"},
                                                                {AHD, true, -1, 66.5, "5"}, {AC, true, -1, 70.5, "3"}, {BKN, true, -1, 78.5, "3"}}), 28, -60},  // 29
        // (SPR 7400 Uitgeest - Utrecht - Driebergen rijdt volgens de GTFS overdag niet; met zijn middagtijden haalt de IC 3900
        //  hem tussen Bijlmer ArenA en Duivendrecht in, waar maar twee sporen liggen)
        // SPR 7300 Breukelen - Utrecht - Rhenen (van/naar Bunnik); in Breukelen naar het opstelspoor (aankomst 2, vertrek 3)
        {"7300", "Breukelen", 0, 0, UTO, BKL, akFill({{UV, true, -1, 12.5, "1"}, {UT, true, 15, 19, "14"}, {UZ, true, -1, 22.5, "2"}, {MS, true, -1, 27.5, "2"},
                                                      {BKN, true, 31, -1, "2"}})},  // 30
        {"7300", "Rhenen", 0, 0, BKL, UTO, akFill({{BKN, true, -1, 28, "3"}, {MS, true, -1, 33.5, "3"}, {UZ, true, -1, 38.5, "3"}, {UT, true, 41, 44, "15"},
                                                   {UV, true, -1, 46.5, "2"}})},  // 31
        // SPR 4800 Amsterdam Centraal - Haarlem - Uitgeest - Alkmaar - Hoorn: in de spelwereld het stuk vanaf Beverwijk:
        // Uitgeest - Castricum - Heiloo - Alkmaar (het stuk Amsterdam - Sloterdijk - Haarlem: in de spoordata vindt de route
        // vanaf Sloterdijk de Haarlem-lijn niet)
        {"4800", "Hoorn", 0, 0, BVW, AKN, {{UTG, true, 70, 71, "4b"}, {CAS, true, -1, 75.5, "1"}, {HLO, true, -1, 81.5, "1"}, {AMR, true, 86, 91, "2"}}},  // 32
        {"4800", "Amsterdam Centraal", 0, 0, AKN, BVW, {{AMR, true, 29, 34, "4"}, {HLO, true, -1, 39.5, "2"}, {CAS, true, -1, 44.5, "2"}, {UTG, true, -1, 49.5, "4b"}}},  // 33
    };
    std::string log;
    // Een serie zonder route (bijv. een gat in de spoordata) wordt overgeslagen; de rest rijdt gewoon.
    std::vector<int> patOf(defs.size(), -1);  // def-index -> patroon-index
    // Alle series tegelijk opbouwen over de processorkernen (het routezoeken is per serie onafhankelijk); een rit die kopmaakt
    // op een eerdere serie (linkFrom eerder in de lijst) wacht tot die klaar is (niveau 1, 2, ...). Daarna in de volgorde
    // van de lijst achter elkaar zetten: zelfde indexen en uitkomst als één voor één.
    struct Job { std::unique_ptr<Pattern> p; bool ok = false, skipped = false; std::string perr; int level = 0; };
    std::vector<Job> jobs(defs.size());
    int maxLevel = 0;
    for (size_t di = 0; di < defs.size(); di++) {
        const Def& d = defs[di];
        auto p = std::make_unique<Pattern>();
        p->def = d;
        if (d.linkFrom >= 0) {
            p->linkDef = d.linkFrom;
            if (d.linkFrom < (int)di) jobs[di].level = jobs[d.linkFrom].level + 1;
            else p->def.linkFrom = -1;  // koppeling naar een latere serie: pas na afloop leggen
        }
        maxLevel = std::max(maxLevel, jobs[di].level);
        p->number = d.number;
        p->dest = d.dest;
        // "noord" = van Eindhoven af (richting Utrecht); volgorde van de eindpunten langs de lijn
        auto rankOf = [](End e) { return e == EAST || e == HELMOND || e == TILBURG ? 0 : e == DENBOSCH || e == NORTH ? 4 : e == TIEL ? 6 : 12; };
        // lijn naar Alkmaar: "noord" = van Utrecht af (volgorde van de eindpunten langs de lijn)
        auto akOrder = [](End e) {
            switch (e) { case UTO: return 0; case BKL: return 2; case BKW: return 3; case DVZ: return 4; case ASD: return 5; case HLM: return 6;
                         case HRN: return 7; case BVW: return 8; case UITG: return 9; case AKM: return 10; case AKN: return 11; default: return -1; }
        };
        if (akEnd(d.origin) || akEnd(d.exit)) p->north = akOrder(d.exit) > akOrder(d.origin);
        else if (zwEnd(d.origin) || zwEnd(d.exit)) p->north = d.exit == ZWOLLE || d.exit == ZWN || d.exit == AMFE;  // richting Zwolle
        else p->north = d.origin == EAST || d.origin == HELMOND || (d.exit != EAST && d.exit != HELMOND && rankOf(d.origin) < rankOf(d.exit));
        p->rs = stock_[d.stock].get();
        jobs[di].p = std::move(p);
    }
    unsigned nThreads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    for (int lvl = 0; lvl <= maxLevel; lvl++) {
        std::vector<size_t> todo;
        for (size_t di = 0; di < defs.size(); di++) {
            if (jobs[di].level != lvl) continue;
            const Def& d = defs[di];
            if (lvl > 0) {
                const Job& src = jobs[d.linkFrom];
                if (!src.ok) { jobs[di].skipped = true; continue; }  // aansluitende rit ontbreekt
                jobs[di].p->linkTerminal = src.p->terminal;
            }
            todo.push_back(di);
        }
        std::atomic<size_t> next{0};
        auto work = [&]() {
            for (size_t k; (k = next++) < todo.size();) {
                Job& j = jobs[todo[k]];
                j.ok = buildPattern(*j.p, world, rail, st, j.perr);
                if (j.ok) {
                    std::string tl;
                    timetable(*j.p, tl);
                    j.p->info += tl;
                }
            }
        };
        std::vector<std::thread> pool;
        for (unsigned t = 1; t < std::min<unsigned>(nThreads, (unsigned)todo.size()); t++) pool.emplace_back(work);
        work();
        for (auto& t : pool) t.join();
    }
    for (size_t di = 0; di < defs.size(); di++) {
        const Def& d = defs[di];
        Job& j = jobs[di];
        if (j.skipped) { warnings_ += "overgeslagen: " + std::string(d.number) + " naar " + d.dest + " (aansluitende rit ontbreekt)\n"; continue; }
        if (!j.ok) {
            warnings_ += "overgeslagen: " + std::string(d.number) + " naar " + d.dest + ": " + j.perr + "\n";
            printf("Trein %s naar %s overgeslagen: %s\n", d.number, d.dest, j.perr.c_str());
            continue;
        }
        if (d.linkFrom >= 0 && d.linkFrom < (int)di) j.p->def.linkFrom = patOf[d.linkFrom];
        patOf[di] = (int)patterns_.size();
        patterns_.push_back(std::move(j.p));
    }
    if (patterns_.empty()) { err = "Geen enkele treinserie kon worden opgebouwd. " + warnings_; return false; }
    // koppelingen naar series die later in de lijst stonden
    for (auto& p : patterns_)
        if (p->linkDef >= 0 && p->def.linkFrom < 0) p->def.linkFrom = patOf[p->linkDef];
    printf("[tijd] setup: na routes %.2f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - tSetup).count());
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
                std::vector<int> hitA, hitB;
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
                                if (da.x * db.x + da.z * db.z < 0) { hit = true; hitB.push_back(e.second); break; }
                            }
                        }
                    if (hit) { shared += 5 * SAMPLE; if (from < 0) from = k * SAMPLE; hitA.push_back((int)k); }
                }
                // gedeelde stukken (beide richtingen): alles-of-niets reserveren, anders kunnen twee treinen er van weerszijden
                // in rijden en elkaar vasthouden (bijv. enkelspoor in de spoordata tussen Amsterdam Muiderpoort en Centraal)
                auto addRanges = [](std::vector<int> h, std::vector<std::pair<int, int>>& out, int n) {
                    std::sort(h.begin(), h.end());
                    for (size_t q = 0; q < h.size();) {
                        size_t e = q;
                        while (e + 1 < h.size() && h[e + 1] - h[e] < 30) e++;
                        if (h[e] - h[q] >= 10) out.push_back({std::max(0, h[q] - 15), std::min(n - 1, h[e] + 15)});
                        q = e + 1;
                    }
                };
                addRanges(hitA, patterns_[a]->contested, (int)A.samples.size());
                addRanges(hitB, patterns_[b]->contested, (int)Bp.samples.size());
                if (shared > 150) {
                    char b2[256];
                    snprintf(b2, sizeof(b2), "LET OP: %s %s en %s %s delen %.0f m spoor in tegengestelde richting (vanaf d=%.0f)\n", A.number.c_str(), A.dest.c_str(),
                             Bp.number.c_str(), Bp.dest.c_str(), shared, from);
                    patterns_[a]->info += b2;
                }
            }
    }
    printf("[tijd] setup: na tegenrichting-controle %.2f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - tSetup).count());
    // gedeelde stukken per serie sorteren en samenvoegen
    for (auto& p : patterns_) {
        auto& c = p->contested;
        std::sort(c.begin(), c.end());
        std::vector<std::pair<int, int>> m;
        for (auto& r : c) {
            if (!m.empty() && r.first <= m.back().second + 10) m.back().second = std::max(m.back().second, r.second);
            else m.push_back(r);
        }
        c = m;
        if (!c.empty()) {
            std::string t = "   gedeeld met tegenrichting (alles-of-niets):";
            for (auto& r : c) t += " " + std::to_string((int)(r.first * SAMPLE)) + "-" + std::to_string((int)(r.second * SAMPLE));
            p->info += t + "\n";
        }
    }
    for (const char* n : {CS, ES, BE, BX, VU, DB, ZB, GM, CL, HC, HT, UL, UV, UT}) nameIndex(n);
    for (const char* n : {UO, BH, DD, AMF, AS, AV, NK, PT, EM, HD, NS, TH, WZ, ZL}) nameIndex(n);
    for (const char* n : {HBV, HTH, HM, HBW}) nameIndex(n);
    for (const char* n : LINE_AK) nameIndex(n);
    for (auto& p : patterns_) nameIndex(p->dest);
    for (auto& s : stock_) s->build(atlas, names_);

    printf("[tijd] setup: na materieel %.2f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - tSetup).count());
    // ritten: dag 0 en dag 1, halfuren van 05:30 tot 00:30
    int id = 1;
    struct Inst { double entry; int pat; double base; };
    std::vector<Inst> order;
    for (int day = 0; day < 2; day++)
        for (size_t pi = 0; pi < patterns_.size(); pi++)
            for (int slot = 11; slot <= 49; slot++) {
                const int hourly = patterns_[pi]->def.hourly;  // 1: alleen vanaf :30 (oneven halfuur), 2: alleen vanaf :00
                if (hourly && (slot % 2) != (hourly == 1 ? 1 : 0)) continue;
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
        s.startAtStop = isTerm(p.def.origin);
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
        // gedeeld stuk met een tegenrichting vóór ons: pas inrijden als het hele stuk vrij is, en dan alles reserveren
        bool waiting = false;
        for (const auto& c : r.pat->contested) {
            if (c.first <= r.i1 || c.first > target) continue;
            bool free = true;
            for (int q = r.i1 + 1; q <= c.second && free; q++) free = !conflict(s[q], &r);
            // eerlijk: wacht er al langer een andere trein voor een stuk dat (deels) hetzelfde spoor is, dan die eerst
            if (free)
                for (const Run& o : runs_) {
                    if (&o == &r || o.waitSince < 0 || (r.waitSince >= 0 && o.waitSince >= r.waitSince)) continue;
                    const std::vector<Vec3>& os = o.pat->samples;
                    bool overlap = false;
                    for (int q = c.first; q <= c.second && !overlap; q += 5)
                        for (int w = o.waitA; w <= o.waitB && w < (int)os.size(); w += 5)
                            if (std::fabs(os[w].x - s[q].x) < CLEAR && std::fabs(os[w].z - s[q].z) < CLEAR) { overlap = true; break; }
                    // staan we zelf al op het stuk waar die ander op wacht, dan geen voorrang geven (die wacht juist op ons)
                    if (overlap) {
                        bool inside = false;
                        for (int q = r.i0; q <= r.i1 && !inside; q += 3)
                            for (int w = o.waitA; w <= o.waitB && w < (int)os.size(); w += 5)
                                if (std::fabs(os[w].x - s[q].x) < CLEAR && std::fabs(os[w].z - s[q].z) < CLEAR) { inside = true; break; }
                        if (!inside) { free = false; break; }
                    }
                }
            if (free) target = std::max(target, c.second);
            else {
                target = c.first - 1;
                waiting = true;
                if (r.waitSince < 0) { r.waitSince = clock_; r.waitA = c.first; r.waitB = c.second; }
            }
            break;
        }
        if (!waiting) r.waitSince = -1;
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
            if (k == (int)s.stops.size() - 1 && isTerm(patterns_[patternOf(&s)]->def.exit)) continue;  // eindpunt: geen vertrek
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

std::string Traffic::pathsInfo() const {
    std::string s;
    char b[64];
    for (auto& p : patterns_) {
        s += "PAD " + p->number + " " + p->dest + "\n";
        for (float d = 0; d < p->path.length; d += 100) {
            Vec3 q = p->path.pos(d);
            snprintf(b, sizeof b, "%.1f %.1f\n", q.x, q.z);
            s += b;
        }
    }
    return s;
}

std::string Traffic::debugInfo() const {
    std::string s = warnings_;
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
