#include "collision.h"
#include <algorithm>

void Collision::addPolyFloor(std::vector<std::pair<float, float>> pts, float y) {
    CPolyFloor p;
    p.minx = p.minz = 1e9f;
    p.maxx = p.maxz = -1e9f;
    for (auto& q : pts) {
        p.minx = std::min(p.minx, q.first); p.maxx = std::max(p.maxx, q.first);
        p.minz = std::min(p.minz, q.second); p.maxz = std::max(p.maxz, q.second);
    }
    p.pts = std::move(pts);
    p.y = y;
    polys_.push_back(std::move(p));
}

float Collision::floorAt(float x, float z, float maxY, float* convX, float* convZ) const {
    float best = -1e9f;
    if (convX) *convX = 0;
    if (convZ) *convZ = 0;
    for (int list = 0; list < 2; list++)
    for (const CFloor& f : list ? dynFloors_ : floors_) {
        float dx = x - f.cx, dz = z - f.cz;
        float a = dx * f.ax + dz * f.az, b = -dx * f.az + dz * f.ax;
        if (std::fabs(a) > f.ha || std::fabs(b) > f.hb) continue;
        float y = f.y0 + (f.y1 - f.y0) * (a + f.ha) / (2 * f.ha);
        if (y <= maxY && y > best) {
            best = y;
            if (convX) *convX = f.ax * f.speed;
            if (convZ) *convZ = f.az * f.speed;
        }
    }
    for (const CPolyFloor& p : polys_) {
        if (x < p.minx || x > p.maxx || z < p.minz || z > p.maxz || p.y > maxY || p.y <= best) continue;
        bool in = false;
        for (size_t k = 0, j = p.pts.size() - 1; k < p.pts.size(); j = k++) {
            const auto& a = p.pts[k];
            const auto& b = p.pts[j];
            if ((a.second > z) != (b.second > z) && x < (b.first - a.first) * (z - a.second) / (b.second - a.second) + a.first) in = !in;
        }
        for (const CBox& h : cutouts_) {
            // y0 = y1 = 0: geldt voor alle vloeren, anders alleen voor vloeren tussen y0 en y1
            if (!(h.y0 == 0 && h.y1 == 0) && (p.y < h.y0 || p.y > h.y1)) continue;
            float dx = x - h.cx, dz = z - h.cz;
            float a = dx * h.ax + dz * h.az, b = -dx * h.az + dz * h.ax;
            if (std::fabs(a) < h.ha && std::fabs(b) < h.hb) in = false;
        }
        if (in) {
            best = p.y;
            if (convX) *convX = 0;
            if (convZ) *convZ = 0;
        }
    }
    return best;
}

static void pushBox(const CBox& b, Vec3& pos, float r, float h) {
    if (pos.y + h <= b.y0 || pos.y >= b.y1 - 0.05f) return;
    float dx = pos.x - b.cx, dz = pos.z - b.cz;
    float a = dx * b.ax + dz * b.az, bb = -dx * b.az + dz * b.ax;
    float ca = clampf(a, -b.ha, b.ha), cb = clampf(bb, -b.hb, b.hb);
    float ex = a - ca, ez = bb - cb;
    float d2 = ex * ex + ez * ez;
    if (d2 >= r * r) return;
    float na, nb;
    if (d2 > 1e-8f) {
        float d = std::sqrt(d2);
        na = ca + ex / d * r;
        nb = cb + ez / d * r;
    } else {
        // middelpunt binnen het blok: naar de dichtstbijzijnde zijde
        float pa = b.ha - std::fabs(a), pb = b.hb - std::fabs(bb);
        na = a; nb = bb;
        if (pa < pb) na = (a >= 0 ? 1 : -1) * (b.ha + r);
        else nb = (bb >= 0 ? 1 : -1) * (b.hb + r);
    }
    pos.x = b.cx + na * b.ax - nb * b.az;
    pos.z = b.cz + na * b.az + nb * b.ax;
}

void Collision::pushOut(Vec3& pos, float r, float h) const {
    for (const CBox& b : boxes_) pushBox(b, pos, r, h);
    for (const CBox& b : dynamic_) pushBox(b, pos, r, h);
}
