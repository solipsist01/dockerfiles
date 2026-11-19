#include "railways.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <queue>
#include "../engine/meshbuilder.h"

// ---------------- TrackPath ----------------
void TrackPath::build() {
    cum.assign(pts.size(), 0);
    for (size_t k = 1; k < pts.size(); k++) cum[k] = cum[k - 1] + std::hypot(pts[k].x - pts[k - 1].x, pts[k].z - pts[k - 1].z);
    length = cum.empty() ? 0 : cum.back();
}

Vec3 TrackPath::pos(float d) const {
    d = clampf(d, 0, length);
    size_t lo = 0, hi = cum.size() - 1;
    while (hi - lo > 1) {
        size_t m = (lo + hi) / 2;
        if (cum[m] <= d) lo = m; else hi = m;
    }
    float seg = cum[hi] - cum[lo];
    float t = seg > 0 ? (d - cum[lo]) / seg : 0;
    return pts[lo] + (pts[hi] - pts[lo]) * t;
}

float TrackPath::heading(float d) const {
    Vec3 a = pos(d - 2.0f), b = pos(d + 2.0f);
    return std::atan2(b.x - a.x, b.z - a.z);
}

float TrackPath::closest(float x, float z) const {
    float best = 1e18f, bd = 0;
    for (size_t k = 0; k + 1 < pts.size(); k++) {
        float ax = pts[k].x, az = pts[k].z, bx = pts[k + 1].x, bz = pts[k + 1].z;
        float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
        float t = l2 > 0 ? clampf(((x - ax) * dx + (z - az) * dz) / l2, 0, 1) : 0;
        float px = ax + dx * t, pz = az + dz * t;
        float d2 = (x - px) * (x - px) + (z - pz) * (z - pz);
        if (d2 < best) { best = d2; bd = cum[k] + (cum[k + 1] - cum[k]) * t; }
    }
    return bd;
}

// ---------------- Railways ----------------
namespace {
const Vec3 BALLAST(0.36f, 0.33f, 0.30f);
const Vec3 STEEL(0.55f, 0.53f, 0.50f);
const Vec3 RAILSIDE(0.32f, 0.24f, 0.18f);  // roestbruin
const Vec3 SLOPE(0.33f, 0.36f, 0.24f);
const float GAUGE_HALF = 0.7175f;  // halve spoorwijdte (1435 mm)
}  // namespace

void Railways::build(const World& world) {
    MeshBuilder mb;
    std::map<int64_t, int> idMap;
    for (const RailLine& r : world.rails) {
        // ---- geometrie ----
        float along = 0;
        for (size_t k = 0; k + 1 < r.pts.size(); k++) {
            Vec3 a(r.pts[k].x, r.pts[k].y, r.pts[k].z), b(r.pts[k + 1].x, r.pts[k + 1].y, r.pts[k + 1].z);
            Vec3 d = b - a;
            float len = std::hypot(d.x, d.z);
            if (len < 0.01f) continue;
            // zijrichting in het midden van de knik middelen voor naadloze aansluiting
            Vec3 dirA = d * (1.0f / len), dirB = dirA;
            if (k > 0) {
                Vec3 p(r.pts[k - 1].x, 0, r.pts[k - 1].z);
                Vec3 dp = Vec3(a.x, 0, a.z) - p;
                float lp = std::hypot(dp.x, dp.z);
                if (lp > 0.01f) dirA = normalize(dirA + dp * (1.0f / lp));
            }
            if (k + 2 < r.pts.size()) {
                Vec3 nx(r.pts[k + 2].x, 0, r.pts[k + 2].z);
                Vec3 dn = nx - Vec3(b.x, 0, b.z);
                float ln = std::hypot(dn.x, dn.z);
                if (ln > 0.01f) dirB = normalize(dirB + dn * (1.0f / ln));
            }
            Vec3 sa = normalize(Vec3(-dirA.z, 0, dirA.x)), sb = normalize(Vec3(-dirB.z, 0, dirB.x));
            float e0 = EX_TRACKBED + along, e1 = EX_TRACKBED + along + len;
            // ballastbed: boven 3,0 m breed, talud naar 4,4 m op -0,5 m
            {
                Vec3 up(0, 1, 0);
                uint32_t i0 = mb.vert(a - sa * 1.5f, up, Vec3(-1.5f, 0, 0), e0), i1 = mb.vert(a + sa * 1.5f, up, Vec3(1.5f, 0, 0), e0);
                uint32_t i2 = mb.vert(b + sb * 1.5f, up, Vec3(1.5f, 0, 0), e1), i3 = mb.vert(b - sb * 1.5f, up, Vec3(-1.5f, 0, 0), e1);
                mb.i.insert(mb.i.end(), {i0, i1, i2, i0, i2, i3});
                for (int s = -1; s <= 1; s += 2) {
                    Vec3 na = normalize(sa * (float)s + Vec3(0, 1.4f, 0)), nb = normalize(sb * (float)s + Vec3(0, 1.4f, 0));
                    mb.quad(a + sa * (1.5f * s), b + sb * (1.5f * s), b + sb * (2.2f * s) - Vec3(0, 0.5f, 0), a + sa * (2.2f * s) - Vec3(0, 0.5f, 0),
                            na, nb, nb, na, BALLAST * 0.9f, EX_PLAIN);
                }
            }
            // rails (kop 7 cm, 17 cm hoog boven de dwarsligger)
            for (int s = -1; s <= 1; s += 2) {
                Vec3 ra = a + sa * (GAUGE_HALF * s), rb = b + sb * (GAUGE_HALF * s);
                Vec3 h(0, 0.17f, 0);
                Vec3 wa = sa * 0.035f, wb = sb * 0.035f;
                mb.quad(ra - wa + h, rb - wb + h, rb + wb + h, ra + wa + h, Vec3(0, 1, 0), STEEL, 0.0f - 2000.0f);
                mb.quad(ra - wa, rb - wb, rb - wb + h, ra - wa + h, sa * -1.0f, RAILSIDE, EX_PLAIN);
                mb.quad(ra + wa, ra + wa + h, rb + wb + h, rb + wb, sa, RAILSIDE, EX_PLAIN);
            }
            // talud naar het maaiveld waar het spoor iets boven het terrein ligt (niet bij bruggen)
            for (int s = -1; s <= 1; s += 2) {
                Vec3 ea = a + sa * (2.2f * s) - Vec3(0, 0.5f, 0), eb = b + sb * (2.2f * s) - Vec3(0, 0.5f, 0);
                float ga = world.heightAt(ea.x + sa.x * s, ea.z + sa.z * s), gb = world.heightAt(eb.x + sb.x * s, eb.z + sb.z * s);
                float da = ea.y - ga, db = eb.y - gb;
                if (da > 0.05f && db > 0.05f && da < 2.5f && db < 2.5f) {
                    Vec3 fa = ea + sa * (da * 1.5f * s), fb = eb + sb * (db * 1.5f * s);
                    fa.y = ga - 0.1f; fb.y = gb - 0.1f;
                    Vec3 n = normalize(sa * (float)s + Vec3(0, 1.5f, 0));
                    mb.quad(ea, eb, fb, fa, n, SLOPE, EX_PLAIN);
                }
            }
            along += len;
            if (mb.v.size() > 400000) { meshes_.push_back(mb.upload()); mb.v.clear(); mb.i.clear(); }
        }
        // ---- graaf ----
        int prev = -1;
        for (size_t k = 0; k < r.pts.size(); k++) {
            const RailPoint& p = r.pts[k];
            int idx;
            if (p.node != 0) {
                auto it = idMap.find(p.node);
                if (it == idMap.end()) { idx = (int)graph_.size(); graph_.push_back({Vec3(p.x, p.y, p.z), {}, r.main}); idMap[p.node] = idx; }
                else idx = it->second;
            } else {
                idx = (int)graph_.size();
                graph_.push_back({Vec3(p.x, p.y, p.z), {}, r.main});
            }
            if (prev >= 0 && prev != idx) { graph_[prev].nb.push_back(idx); graph_[idx].nb.push_back(prev); }
            prev = idx;
        }
    }
    if (!mb.v.empty()) meshes_.push_back(mb.upload());
    // kaartfoutjes herstellen: een spoor dat doodloopt vlak vóór een ander spoor (binnen 3 m, in het verlengde)
    // hoort daar in werkelijkheid met een wissel op aan te sluiten (bijv. ten noorden van Amsterdam Muiderpoort)
    {
        std::map<std::pair<int, int>, std::vector<int>> cell;
        auto key = [](const Vec3& p) { return std::make_pair((int)std::floor(p.x / 4), (int)std::floor(p.z / 4)); };
        for (int k = 0; k < (int)graph_.size(); k++) cell[key(graph_[k].p)].push_back(k);
        int joined = 0;
        for (int k = 0; k < (int)graph_.size(); k++) {
            if (graph_[k].nb.size() != 1) continue;
            const Vec3 p = graph_[k].p, q = graph_[graph_[k].nb[0]].p;
            Vec3 d = p - q;
            float dl = std::hypot(d.x, d.z);
            if (dl < 0.5f) continue;
            float fx = d.x / dl, fz = d.z / dl;
            int best = -1;
            float bd = 3.0f;
            auto c0 = key(p);
            for (int gx = -1; gx <= 1; gx++)
                for (int gz = -1; gz <= 1; gz++) {
                    auto it = cell.find({c0.first + gx, c0.second + gz});
                    if (it == cell.end()) continue;
                    for (int m : it->second) {
                        if (m == k || m == graph_[k].nb[0] || graph_[m].nb.size() < 2) continue;
                        Vec3 e = graph_[m].p - p;
                        float el = std::hypot(e.x, e.z);
                        if (el < 0.3f || el > bd || std::fabs(e.y) > 1.5f) continue;
                        if ((e.x * fx + e.z * fz) / el < 0.85f) continue;  // in het verlengde (vooruit, max ~30 graden opzij)
                        // en het andere spoor loopt dezelfde kant op
                        bool aligned = false;
                        for (int n : graph_[m].nb) {
                            Vec3 t = graph_[n].p - graph_[m].p;
                            float tl = std::hypot(t.x, t.z);
                            if (tl > 0.1f && std::fabs((t.x * fx + t.z * fz) / tl) > 0.94f) aligned = true;
                        }
                        if (aligned) { bd = el; best = m; }
                    }
                }
            if (best >= 0) { graph_[k].nb.push_back(best); graph_[best].nb.push_back(k); joined++; }
        }
        if (joined) printf("Spoor: %d doodlopende sporen aangesloten op een spoor vlak ernaast\n", joined);
    }
    buildCatenary(world);
}

// Bovenleiding (1500 V): rijdraad op 5,5 m en draagkabel op 6,7 m boven het spoor, hangers om de 9 m.
// Buiten tunnels: masten met uithouder om de ~55 m aan de buitenkant van een sporenbundel, of een portaal over
// de middensporen. In tunnels hangt de rijdraad aan beugels aan het plafond.
void Railways::buildCatenary(const World& world) {
    const Vec3 WIRE(0.20f, 0.17f, 0.12f), MAST(0.55f, 0.57f, 0.58f);
    MeshBuilder mb;
    // raster met alle hoofdspoorpunten (zijwaartse buren zoeken)
    struct GP { float x, y, z; int line; };
    std::map<std::pair<int, int>, std::vector<GP>> grid;
    auto cell = [](float v) { return (int)std::floor(v / 12.0f); };
    for (size_t li = 0; li < world.rails.size(); li++) {
        const RailLine& r = world.rails[li];
        if (!r.main) continue;
        for (const RailPoint& p : r.pts) grid[{cell(p.x), cell(p.z)}].push_back({p.x, p.y, p.z, (int)li});
    }
    // laterale posities van andere sporen rond p (rechts positief), evenwijdig binnen 'along' meter
    auto lateral = [&](Vec3 p, Vec3 d, int self, float maxLat, std::vector<float>& out) {
        Vec3 r(-d.z, 0, d.x);
        out.clear();
        int cx = cell(p.x), cz = cell(p.z), R = (int)std::ceil(maxLat / 12.0f);
        for (int gx = cx - R; gx <= cx + R; gx++)
            for (int gz = cz - R; gz <= cz + R; gz++) {
                auto it = grid.find({gx, gz});
                if (it == grid.end()) continue;
                for (const GP& q : it->second) {
                    if (q.line == self) continue;
                    float ax = q.x - p.x, az = q.z - p.z;
                    float al = ax * d.x + az * d.z, lt = ax * r.x + az * r.z;
                    if (std::fabs(al) < 6 && std::fabs(lt) < maxLat && std::fabs(lt) > 1.0f && std::fabs(q.y - p.y) < 3) out.push_back(lt);
                }
            }
    };
    auto ribbon = [&](Vec3 a, Vec3 b, float w) {  // dunne draad: kruisprofiel (zichtbaar van onder en opzij)
        Vec3 d = b - a;
        float l = std::hypot(d.x, d.z);
        if (l < 0.01f) return;
        Vec3 s(-d.z / l * w, 0, d.x / l * w), up(0, w, 0);
        mb.quad(a - s, b - s, b + s, a + s, Vec3(0, 1, 0), WIRE, EX_PLAIN);
        mb.quad(a - up, b - up, b + up, a + up, normalize(Vec3(-d.z, 0, d.x)), WIRE, EX_PLAIN);
    };
    std::vector<float> lats;
    for (size_t li = 0; li < world.rails.size(); li++) {
        const RailLine& r = world.rails[li];
        if (!r.main || r.pts.size() < 2) continue;
        float along = 0, nextMast = 20.0f + (li % 5) * 7.0f, nextHanger = 0;
        for (size_t k = 0; k + 1 < r.pts.size(); k++) {
            Vec3 a(r.pts[k].x, r.pts[k].y, r.pts[k].z), b(r.pts[k + 1].x, r.pts[k + 1].y, r.pts[k + 1].z);
            float len = std::hypot(b.x - a.x, b.z - a.z);
            if (len < 0.01f) continue;
            Vec3 d((b.x - a.x) / len, 0, (b.z - a.z) / len), rr(-d.z, 0, d.x);
            // rijdraad (licht zigzag) en draagkabel
            float zz0 = std::sin(along / 40.0f) * 0.2f, zz1 = std::sin((along + len) / 40.0f) * 0.2f;
            ribbon(a + rr * zz0 + Vec3(0, 5.5f, 0), b + rr * zz1 + Vec3(0, 5.5f, 0), 0.012f);
            if (!r.tunnel) ribbon(a + Vec3(0, 6.7f, 0), b + Vec3(0, 6.7f, 0), 0.010f);
            while (nextHanger < along + len) {
                Vec3 h = a + (b - a) * ((nextHanger - along) / len);
                float zz = std::sin(nextHanger / 40.0f) * 0.2f;
                if (r.tunnel) {
                    // beugel aan het plafond
                    mb.box(h + rr * zz + Vec3(0, 5.95f, 0), {0.06f, 0.9f, 0.06f}, MAST * 0.8f, EX_PLAIN);
                    mb.box(h + Vec3(0, 6.42f, 0), {0.9f, 0.06f, 0.10f}, MAST * 0.8f, EX_PLAIN);
                } else {
                    mb.box(h + rr * (zz * 0.5f) + Vec3(0, 6.1f, 0), {0.012f, 1.2f, 0.012f}, WIRE, EX_PLAIN);
                }
                nextHanger += r.tunnel ? 12.0f : 9.0f;
            }
            // masten (niet in tunnels)
            while (!r.tunnel && nextMast < along + len) {
                Vec3 m = a + (b - a) * ((nextMast - along) / len);
                lateral(m, d, (int)li, 16.0f, lats);
                bool right = true, left = true;
                float lmin = 0, lmax = 0;
                for (float l : lats) {
                    if (l > 0 && l < 6.0f) right = false;
                    if (l < 0 && l > -6.0f) left = false;
                    lmin = std::min(lmin, l);
                    lmax = std::max(lmax, l);
                }
                auto mast = [&](Vec3 foot, float top) {
                    mb.box(foot + Vec3(0, top / 2 - 0.3f, 0), {0.28f, top + 0.6f, 0.28f}, MAST, EX_PLAIN);
                    mb.box(foot + Vec3(0, 0.0f, 0), {0.6f, 0.6f, 0.6f}, Vec3(0.55f, 0.55f, 0.53f), EX_PLAIN);  // fundering
                };
                if (right || left) {
                    float s = right ? 1.0f : -1.0f;
                    Vec3 foot = m + rr * (3.1f * s);
                    mast(foot, 7.4f);
                    // uithouder (bovenbuis naar de draagkabel, onderbuis naar de rijdraad) en isolatoren
                    Vec3 top = foot + Vec3(0, 6.75f, 0), low = foot + Vec3(0, 5.65f, 0);
                    mb.tube(top, m + Vec3(0, 6.75f, 0), 0.035f, 5, MAST, EX_PLAIN);
                    mb.tube(low, m + rr * (0.2f * s) + Vec3(0, 5.55f, 0), 0.03f, 5, MAST, EX_PLAIN);
                    mb.tube(low, top - rr * (0.9f * s), 0.025f, 5, MAST, EX_PLAIN);
                    mb.box(foot - rr * (0.35f * s) + Vec3(0, 6.75f, 0), {0.12f, 0.12f, 0.35f}, Vec3(0.55f, 0.28f, 0.18f), EX_PLAIN);
                    mb.box(foot - rr * (0.35f * s) + Vec3(0, 5.65f, 0), {0.12f, 0.12f, 0.35f}, Vec3(0.55f, 0.28f, 0.18f), EX_PLAIN);
                } else {
                    // portaal over de hele bundel
                    Vec3 f0 = m + rr * (lmin - 3.1f), f1 = m + rr * (lmax + 3.1f);
                    mast(f0, 8.2f);
                    mast(f1, 8.2f);
                    Vec3 t0 = f0 + Vec3(0, 7.9f, 0), t1 = f1 + Vec3(0, 7.9f, 0);
                    Vec3 dd = t1 - t0;
                    float bl = length(dd);
                    mb.orientedBox((t0 + t1) * 0.5f, {0.3f, 0.45f, bl}, std::atan2(dd.x, dd.z), MAST, EX_PLAIN);
                    mb.tube(m + Vec3(0, 7.7f, 0), m + Vec3(0, 6.7f, 0), 0.03f, 5, MAST, EX_PLAIN);
                    mb.tube(m + Vec3(0, 6.7f, 0), m + Vec3(0, 5.55f, 0), 0.02f, 5, MAST, EX_PLAIN);
                }
                nextMast += 55.0f;
            }
            along += len;
            if (mb.v.size() > 400000) { meshes_.push_back(mb.upload()); mb.v.clear(); mb.i.clear(); }
        }
    }
    if (!mb.v.empty()) meshes_.push_back(mb.upload());
}

void Railways::draw(World& world) const {
    world.setMode(MODE_OBJECT);
    world.setModel(Mat4::identity());
    for (const Mesh& m : meshes_) drawMesh(m);
}

int Railways::nearest(float x, float z, float y) const {
    // y != 0: hoogte telt mee (knooppunten met sporen boven elkaar: Amsterdam Sloterdijk, Duivendrecht)
    int best = -1;
    float bd = 1e18f;
    for (int k = 0; k < (int)graph_.size(); k++) {
        const Vec3& p = graph_[k].p;
        float d = (p.x - x) * (p.x - x) + (p.z - z) * (p.z - z);
        if (y != 0.0f) d += 9.0f * (p.y - y) * (p.y - y);
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}

bool Railways::rightmostTrack(Vec3 p, Vec3 dir, float radius, Vec3& out) const {
    float dl = std::hypot(dir.x, dir.z);
    if (dl < 1e-4f) return false;
    float fx = dir.x / dl, fz = dir.z / dl, rx = -fz, rz = fx;  // rechts van de rijrichting
    float best = -1e9f;
    bool found = false;
    for (const Vtx& v : graph_) {
        if (!v.main) continue;
        float ax = v.p.x - p.x, az = v.p.z - p.z;
        float along = ax * fx + az * fz, lat = ax * rx + az * rz;
        if (std::fabs(along) > 4.0f || std::fabs(lat) > radius) continue;
        // alleen sporen die ongeveer evenwijdig lopen
        bool parallel = false;
        for (int n : v.nb) {
            Vec3 d = graph_[n].p - v.p;
            float l = std::hypot(d.x, d.z);
            if (l > 0.5f && std::fabs((d.x * fx + d.z * fz) / l) > 0.97f) parallel = true;
        }
        if (parallel && lat > best) { best = lat; out = v.p; found = true; }
    }
    return found;
}

bool Railways::rankedTrack(Vec3 p, Vec3 dir, float radius, int rank, Vec3& out, int* count) const {
    float dl = std::hypot(dir.x, dir.z);
    if (dl < 1e-4f) return false;
    float fx = dir.x / dl, fz = dir.z / dl, rx = -fz, rz = fx;  // rechts van de rijrichting
    struct Cand { float lat; Vec3 p; float along; };
    std::vector<Cand> c;
    for (const Vtx& v : graph_) {
        if (!v.main) continue;
        float ax = v.p.x - p.x, az = v.p.z - p.z;
        float along = ax * fx + az * fz, lat = ax * rx + az * rz;
        if (std::fabs(along) > 4.0f || std::fabs(lat) > radius) continue;
        bool parallel = false;
        for (int n : v.nb) {
            Vec3 d = graph_[n].p - v.p;
            float l = std::hypot(d.x, d.z);
            if (l > 0.5f && std::fabs((d.x * fx + d.z * fz) / l) > 0.97f) parallel = true;
        }
        if (parallel) c.push_back({lat, v.p, std::fabs(along)});
    }
    if (c.empty()) return false;
    std::sort(c.begin(), c.end(), [](const Cand& a, const Cand& b) { return a.lat > b.lat; });
    // sporen samenvoegen (meerdere knopen van hetzelfde spoor), per spoor de knoop het dichtst bij p
    std::vector<Cand> tr;
    for (const Cand& k : c) {
        if (!tr.empty() && std::fabs(tr.back().lat - k.lat) < 1.6f) {
            if (k.along < tr.back().along) tr.back() = Cand{tr.back().lat, k.p, k.along};
            continue;
        }
        tr.push_back(k);
    }
    if (count) *count = (int)tr.size();
    int idx = (tr.size() >= 4) ? std::min(rank, (int)tr.size() - 1) : 0;
    out = tr[idx].p;
    return true;
}

bool Railways::routeSmart(const std::vector<Vec3>& pts, const std::vector<bool>& required, float dx, float dz, TrackPath& out,
                          float extend) const {
    if (pts.size() < 2) return false;
    std::vector<int> all;
    int cur = nearest(pts[0].x, pts[0].z, pts[0].y);
    if (cur < 0) return false;
    all.push_back(cur);
    // begin van elke geaccepteerde etappe naar een optioneel tussenpunt: als het daarna niet verder kan (bijv. een
    // doodlopend spoor bij een station in aanbouw), dat tussenpunt alsnog laten vallen en het opnieuw proberen
    std::vector<size_t> optLeg;
    for (size_t k = 1; k < pts.size(); k++) {
        int goal = nearest(pts[k].x, pts[k].z, pts[k].y);
        if (goal < 0) { if (required[k]) return false; continue; }
        if (goal == all.back()) continue;
        std::vector<int> leg;
        bool ok = search(all.back(), dx, dz, goal, leg);
        if (!ok && !optLeg.empty()) {
            // terug naar vóór het laatste optionele tussenpunt (verplicht doel: zo ver als nodig; optioneel: één stap)
            std::vector<int> saveAll = all;
            std::vector<size_t> saveOpt = optLeg;
            float sdx = dx, sdz = dz;
            while (!ok && !optLeg.empty()) {
                all.resize(optLeg.back());
                optLeg.pop_back();
                if (all.size() >= 2) {
                    Vec3 d = graph_[all.back()].p - graph_[all[all.size() - 2]].p;
                    dx = d.x; dz = d.z;
                }
                ok = search(all.back(), dx, dz, goal, leg);
                if (!required[k]) break;
            }
            if (!ok) { all = saveAll; optLeg = saveOpt; dx = sdx; dz = sdz; }
        }
        if (ok) {
            float len = 0;
            for (size_t i = 1; i < leg.size(); i++) len += std::hypot(graph_[leg[i]].p.x - graph_[leg[i - 1]].p.x, graph_[leg[i]].p.z - graph_[leg[i - 1]].p.z);
            float straight = std::hypot(graph_[goal].p.x - graph_[all.back()].p.x, graph_[goal].p.z - graph_[all.back()].p.z);
            if (!required[k] && len > straight * 1.25f + 40.0f) ok = false;  // omweg: dit tussenpunt overslaan
        }
        if (!ok) { if (required[k]) return false; continue; }
        if (!required[k]) optLeg.push_back(all.size());
        else optLeg.clear();
        all.insert(all.end(), leg.begin() + 1, leg.end());
        if (all.size() >= 2) {
            Vec3 d = graph_[all.back()].p - graph_[all[all.size() - 2]].p;
            dx = d.x; dz = d.z;
        }
    }
    const float cosMax = std::cos(35.0f * PI / 180);
    float extra = 0;
    while (extra < extend && all.size() >= 2) {
        int a = all[all.size() - 2], bnode = all.back();
        Vec3 d0 = graph_[bnode].p - graph_[a].p;
        float l0 = std::hypot(d0.x, d0.z);
        int best = -1;
        float bestCos = cosMax;
        for (int n : graph_[bnode].nb) {
            if (n == a) continue;
            Vec3 d1 = graph_[n].p - graph_[bnode].p;
            float l1 = std::hypot(d1.x, d1.z);
            if (l1 < 1e-3f || l0 < 1e-3f) continue;
            float cs = (d0.x * d1.x + d0.z * d1.z) / (l0 * l1);
            if (cs > bestCos) { bestCos = cs; best = n; }
        }
        if (best < 0) break;
        extra += std::hypot(graph_[best].p.x - graph_[bnode].p.x, graph_[best].p.z - graph_[bnode].p.z);
        all.push_back(best);
    }
    out.pts.clear();
    for (int n : all) out.pts.push_back(graph_[n].p);
    out.build();
    return out.pts.size() >= 2;
}

bool Railways::findRoute(float x0, float z0, float dx, float dz, float x1, float z1, TrackPath& out, float extend) const {
    return routeVia({Vec3(x0, 0, z0), Vec3(x1, 0, z1)}, dx, dz, out, extend);
}

bool Railways::routeVia(const std::vector<Vec3>& pts, float dx, float dz, TrackPath& out, float extend) const {
    if (pts.size() < 2) return false;
    std::vector<int> all;
    int cur = nearest(pts[0].x, pts[0].z, pts[0].y);
    if (cur < 0) return false;
    all.push_back(cur);
    for (size_t k = 1; k < pts.size(); k++) {
        int goal = nearest(pts[k].x, pts[k].z, pts[k].y);
        if (goal < 0) return false;
        if (goal == all.back()) continue;
        std::vector<int> leg;
        if (!search(all.back(), dx, dz, goal, leg)) return false;
        all.insert(all.end(), leg.begin() + 1, leg.end());
        if (all.size() >= 2) {
            Vec3 d = graph_[all.back()].p - graph_[all[all.size() - 2]].p;
            dx = d.x; dz = d.z;
        }
    }
    std::vector<int>& nodes = all;
    const float cosMax = std::cos(35.0f * PI / 180);
    // pad voorbij de bestemming verlengen (zo recht mogelijk doorrijden), zodat een hele trein erop past
    {
        float extra = 0;
        while (extra < extend && nodes.size() >= 2) {
            int a = nodes[nodes.size() - 2], bnode = nodes.back();
            Vec3 d0 = graph_[bnode].p - graph_[a].p;
            float l0 = std::hypot(d0.x, d0.z);
            int best = -1;
            float bestCos = cosMax;
            for (int n : graph_[bnode].nb) {
                if (n == a) continue;
                Vec3 d1 = graph_[n].p - graph_[bnode].p;
                float l1 = std::hypot(d1.x, d1.z);
                if (l1 < 1e-3f || l0 < 1e-3f) continue;
                float cs = (d0.x * d1.x + d0.z * d1.z) / (l0 * l1);
                if (cs > bestCos) { bestCos = cs; best = n; }
            }
            if (best < 0) break;
            extra += std::hypot(graph_[best].p.x - graph_[bnode].p.x, graph_[best].p.z - graph_[bnode].p.z);
            nodes.push_back(best);
        }
    }
    out.pts.clear();
    for (int n : nodes) out.pts.push_back(graph_[n].p);
    out.build();
    return out.pts.size() >= 2;
}

bool Railways::search(int start, float dx, float dz, int goal, std::vector<int>& nodes) const {
    // Dijkstra over gerichte stappen (van -> naar); bochten > 35 graden zijn niet toegestaan (geen terugknikken bij wissels)
    struct State { float cost; int from, to; bool operator>(const State& o) const { return cost > o.cost; } };
    std::map<std::pair<int, int>, float> dist;
    std::map<std::pair<int, int>, std::pair<int, int>> parent;
    std::priority_queue<State, std::vector<State>, std::greater<State>> pq;
    float dl = std::hypot(dx, dz);
    for (int n : graph_[start].nb) {
        Vec3 d = graph_[n].p - graph_[start].p;
        float l = std::hypot(d.x, d.z);
        if (l < 1e-3f || (d.x * dx + d.z * dz) / (l * dl) < 0.5f) continue;  // beginrichting
        dist[{start, n}] = l;
        parent[{start, n}] = {-1, -1};
        pq.push({l, start, n});
    }
    const float cosMax = std::cos(35.0f * PI / 180);
    std::pair<int, int> found{-1, -1};
    while (!pq.empty()) {
        State s = pq.top();
        pq.pop();
        if (s.cost > dist[{s.from, s.to}] + 1e-3f) continue;
        if (s.to == goal) { found = {s.from, s.to}; break; }
        Vec3 d0 = graph_[s.to].p - graph_[s.from].p;
        float l0 = std::hypot(d0.x, d0.z);
        for (int n : graph_[s.to].nb) {
            if (n == s.from) continue;
            Vec3 d1 = graph_[n].p - graph_[s.to].p;
            float l1 = std::hypot(d1.x, d1.z);
            if (l1 < 1e-3f || l0 < 1e-3f) continue;
            if ((d0.x * d1.x + d0.z * d1.z) / (l0 * l1) < cosMax) continue;
            float c = s.cost + l1;
            auto key = std::make_pair(s.to, n);
            auto it = dist.find(key);
            if (it == dist.end() || c < it->second) {
                dist[key] = c;
                parent[key] = {s.from, s.to};
                pq.push({c, s.to, n});
            }
        }
    }
    if (found.first < 0) return false;
    nodes.clear();
    auto cur = found;
    nodes.push_back(cur.second);
    while (cur.first >= 0) {
        nodes.push_back(cur.first);
        cur = parent[cur];
    }
    std::reverse(nodes.begin(), nodes.end());
    return true;
}
