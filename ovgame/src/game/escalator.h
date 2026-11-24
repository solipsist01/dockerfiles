#pragma once
// Roltrappen zoals op Nederlandse stations (Schindler/Kone-type): vlakke instap met kamplaten, overgangsbochten,
// helling van 30 graden, bewegende geribbelde treden met gele afbakening, rvs plinten met borstels, glazen balustrade
// met een zwarte rubberen leuning die om de koppen heen loopt, groene tredeverlichting en richtingslampjes.
// De vaste delen gaan in de stationsmesh (MeshBuilder); de treden zijn een gedeelde mesh die per trede met een
// modelmatrix wordt getekend, zodat ze echt meelopen met de snelheid van de botsingsvloer.
#include <vector>
#include "../engine/meshbuilder.h"
#include "collision.h"

class World;

struct EscalatorDef {
    Vec3 base;       // midden van de onderste kamplaat (waar de treden onder de kam verdwijnen), op vloerniveau
    float heading;   // richting naar boven (zoals orientedBox: lokale +z wijst naar (sin h, 0, cos h))
    float rise;      // hoogteverschil (m)
    bool up = true;  // rijrichting
    float speed = 0.5f;
};

class Escalators {
public:
    // Horizontale lengte van onderste tot bovenste kamplaat bij een gegeven hoogteverschil
    static float length(float rise);
    // Hoogte van de tredevloer op afstand s (horizontaal) vanaf de onderste kamplaat
    static float heightAt(float rise, float s);
    // Totale lengte inclusief de koppen (balustradeneuzen) aan beide kanten
    static float footprint(float rise) { return length(rise) + 2 * NEWEL; }
    // Halve breedte over alles (buitenkant bekleding)
    static constexpr float HALF_W = 0.82f;
    static constexpr float NEWEL = 1.25f;  // kop voorbij de kamplaat (balustrade + vloerplaat)

    // Vaste delen in op/gl, botsing (vloeren met lopende band + balustrades) in col
    void add(const EscalatorDef& d, MeshBuilder& op, MeshBuilder& gl, Collision& col);
    void finish();  // tredemesh naar de GPU
    void update(float dt) { time_ += dt; }
    void draw(World& world) const;
    size_t count() const { return list_.size(); }

private:
    struct Esc {
        EscalatorDef d;
        float L;  // kam tot kam
        float Lm; // lengte van het rechte hellende deel
        Vec3 bmin, bmax;
    };
    static float profile(const Esc& e, float s);  // hoogte van de tredevloer op afstand s vanaf de onderste kam
    std::vector<Esc> list_;
    Mesh step_, stepLo_;  // trede: gedetailleerd (dichtbij) en eenvoudig (verder weg)
    bool built_ = false;
    double time_ = 0;
};
