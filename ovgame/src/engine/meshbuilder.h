#pragma once
// Hulpmiddel om meshes in code op te bouwen (blokken, buizen, cilinders, vlakken).
#include <vector>
#include "renderer.h"

struct MeshBuilder {
    std::vector<Vertex> v;
    std::vector<uint32_t> i;

    uint32_t vert(Vec3 p, Vec3 n, Vec3 c, float extra) {
        v.push_back({p.x, p.y, p.z, n.x, n.y, n.z, c.x, c.y, c.z, extra});
        return (uint32_t)v.size() - 1;
    }
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 na, Vec3 nb, Vec3 nc, Vec3 nd, Vec3 col, float extra) {
        uint32_t ia = vert(a, na, col, extra), ib = vert(b, nb, col, extra), ic = vert(c, nc, col, extra), id = vert(d, nd, col, extra);
        i.insert(i.end(), {ia, ib, ic, ia, ic, id});
    }
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 n, Vec3 col, float extra) { quad(a, b, c, d, n, n, n, n, col, extra); }

    // Asgericht blok
    void box(Vec3 c, Vec3 s, Vec3 col, float extra) { orientedBox(c, s, 0, col, extra); }

    // Blok gedraaid om de y-as (heading h: lokale +z wijst naar (sin h, 0, cos h))
    void orientedBox(Vec3 c, Vec3 s, float h, Vec3 col, float extra) {
        float ch = std::cos(h), sh = std::sin(h);
        Vec3 ax(ch, 0, -sh), az(sh, 0, ch), ay(0, 1, 0);
        Vec3 hx = ax * (s.x / 2), hy = ay * (s.y / 2), hz = az * (s.z / 2);
        auto P = [&](float x, float y, float z) { return c + hx * x + hy * y + hz * z; };
        quad(P(1, -1, -1), P(1, 1, -1), P(1, 1, 1), P(1, -1, 1), ax, col, extra);
        quad(P(-1, -1, 1), P(-1, 1, 1), P(-1, 1, -1), P(-1, -1, -1), ax * -1, col, extra);
        quad(P(-1, 1, -1), P(-1, 1, 1), P(1, 1, 1), P(1, 1, -1), ay, col, extra);
        quad(P(-1, -1, 1), P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), ay * -1, col, extra);
        quad(P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), P(-1, -1, 1), az, col, extra);
        quad(P(-1, -1, -1), P(-1, 1, -1), P(1, 1, -1), P(1, -1, -1), az * -1, col, extra);
    }

    // Buis (cilinder) tussen twee willekeurige punten
    void tube(Vec3 a, Vec3 b, float r, int seg, Vec3 col, float extra) {
        Vec3 d = normalize(b - a);
        Vec3 ref = std::fabs(d.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
        Vec3 u = normalize(cross(d, ref)), w = cross(d, u);
        for (int k = 0; k < seg; k++) {
            float a0 = 2 * PI * k / seg, a1 = 2 * PI * (k + 1) / seg;
            Vec3 n0 = u * std::cos(a0) + w * std::sin(a0), n1 = u * std::cos(a1) + w * std::sin(a1);
            quad(a + n0 * r, a + n1 * r, b + n1 * r, b + n0 * r, n0, n1, n1, n0, col, extra);
        }
    }

    // Cilinder met as langs x (wielen), met velg aan beide kanten
    void cylX(Vec3 c, float r, float w, int seg, Vec3 col, float extra, Vec3 capCol, float capR, float capExtra) {
        for (int k = 0; k < seg; k++) {
            float a0 = 2 * PI * k / seg, a1 = 2 * PI * (k + 1) / seg;
            Vec3 n0(0, std::cos(a0), std::sin(a0)), n1(0, std::cos(a1), std::sin(a1));
            Vec3 p0 = c + Vec3(-w / 2, r * n0.y, r * n0.z), p1 = c + Vec3(-w / 2, r * n1.y, r * n1.z);
            Vec3 q0 = c + Vec3(w / 2, r * n0.y, r * n0.z), q1 = c + Vec3(w / 2, r * n1.y, r * n1.z);
            quad(p0, p1, q1, q0, n0, n1, n1, n0, col, extra);
            for (int side = -1; side <= 1; side += 2) {
                Vec3 cc = c + Vec3(side * w / 2, 0, 0);
                Vec3 nn((float)side, 0, 0);
                Vec3 o0 = cc + Vec3(0, r * n0.y, r * n0.z), o1 = cc + Vec3(0, r * n1.y, r * n1.z);
                Vec3 m0 = cc + Vec3(0, capR * n0.y, capR * n0.z), m1 = cc + Vec3(0, capR * n1.y, capR * n1.z);
                quad(m0, o0, o1, m1, nn, col, extra);
                uint32_t ia = vert(cc + Vec3(side * 0.01f, 0, 0), nn, capCol, capExtra);
                uint32_t ib = vert(m0 + Vec3(side * 0.01f, 0, 0), nn, capCol, capExtra);
                uint32_t id = vert(m1 + Vec3(side * 0.01f, 0, 0), nn, capCol, capExtra);
                i.insert(i.end(), {ia, ib, id});
            }
        }
    }

    Mesh upload() const { return createMesh(v, i); }
};
