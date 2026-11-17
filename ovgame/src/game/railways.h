#pragma once
#include <vector>
#include "../engine/renderer.h"
#include "world.h"

// Pad in 3D met afstand langs het pad (voor treinen)
struct TrackPath {
    std::vector<Vec3> pts;
    std::vector<float> cum;
    float length = 0;
    void build();
    Vec3 pos(float d) const;
    float heading(float d) const;              // richting van toenemende d
    float closest(float x, float z) const;     // afstand langs het pad van het dichtstbijzijnde punt
};

class Railways {
public:
    void build(const World& world);
    void draw(World& world) const;
    // Route over het spoornet van (x0,z0) met beginrichting (dx,dz) naar het spoor dichtbij (x1,z1).
    // extend: pad voorbij de bestemming doortrekken (zodat een hele trein erop past).
    bool findRoute(float x0, float z0, float dx, float dz, float x1, float z1, TrackPath& out, float extend = 150.0f) const;
    // Route langs een reeks punten (elk punt: dichtstbijzijnd spoor), beginrichting (dx,dz).
    bool routeVia(const std::vector<Vec3>& pts, float dx, float dz, TrackPath& out, float extend = 150.0f) const;
    // Rechts rijden: kies bij punt p (rijrichting dir) het meest rechtse hoofdspoor binnen 'radius' meter.
    bool rightmostTrack(Vec3 p, Vec3 dir, float radius, Vec3& out) const;
    // Evenwijdige hoofdsporen bij p, gesorteerd van rechts naar links t.o.v. rijrichting dir.
    // rank 0 = meest rechtse spoor, 1 = het spoor daarnaast (alleen als er minstens 4 sporen naast elkaar liggen,
    // anders het meest rechtse). count = aantal gevonden sporen.
    bool rankedTrack(Vec3 p, Vec3 dir, float radius, int rank, Vec3& out, int* count = nullptr) const;
    // Route langs punten; required[k] = false: punt overslaan als het een omweg zou geven (of onbereikbaar is).
    bool routeSmart(const std::vector<Vec3>& pts, const std::vector<bool>& required, float dx, float dz, TrackPath& out,
                    float extend = 150.0f) const;

private:
    std::vector<Mesh> meshes_;
    struct Vtx { Vec3 p; std::vector<int> nb; bool main = true; };
    std::vector<Vtx> graph_;
    bool search(int start, float dx, float dz, int goal, std::vector<int>& nodes) const;
    int nearest(float x, float z) const;
    void buildCatenary(const World& world);
};
