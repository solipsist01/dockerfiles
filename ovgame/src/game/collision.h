#pragma once
// Botsingen voor de speler: beloopbare vloeren (ook trappen/roltrappen) en massieve blokken.
#include <utility>
#include <vector>
#include "../engine/math.h"

// Rechthoekige vloer, gedraaid; hoogte loopt lineair langs as a (trap/helling).
struct CFloor {
    float cx, cz;     // midden
    float ax, az;     // eenheidsvector van as a (horizontaal); as b staat er loodrecht op
    float ha, hb;     // halve maten langs a en b
    float y0, y1;     // hoogte bij a = -ha en a = +ha
    float speed = 0;  // roltrap: snelheid langs +a (m/s)
};

// Veelhoekige vlakke vloer (perrons uit OSM)
struct CPolyFloor {
    std::vector<std::pair<float, float>> pts;
    float y;
    float minx, maxx, minz, maxz;
};

// Massief blok, gedraaid om de y-as
struct CBox {
    float cx, cz, ax, az, ha, hb, y0, y1;
};

class Collision {
public:
    void addFloor(const CFloor& f) { floors_.push_back(f); }
    void addPolyFloor(std::vector<std::pair<float, float>> pts, float y);
    void addBox(const CBox& b) { boxes_.push_back(b); }
    void addCutout(const CBox& b) { cutouts_.push_back(b); }  // gat in veelhoekige vloeren (trapgat)
    void clearDynamic() { dynamic_.clear(); dynFloors_.clear(); }
    void addDynamicFloor(const CFloor& f) { dynFloors_.push_back(f); }
    void addDynamicBox(const CBox& b) { dynamic_.push_back(b); }

    // Hoogste vloer onder maxY op (x,z). Geeft -1e9 als er geen is. conveyor = snelheid van roltrap (x,z).
    float floorAt(float x, float z, float maxY, float* convX = nullptr, float* convZ = nullptr) const;
    // Duw een cilinder (straal r, van y tot y+h) uit alle blokken.
    void pushOut(Vec3& pos, float r, float h) const;

private:
    std::vector<CFloor> floors_, dynFloors_;
    std::vector<CPolyFloor> polys_;
    std::vector<CBox> boxes_, dynamic_, cutouts_;
};
