#pragma once
// Busstation Eindhoven Centraal (Neckerspoel): de witte buisconstructie met vertrekschermen boven de havens.
// De bussen zelf staan in citybus.h (BusFleet).
#include <vector>
#include "world.h"

class BusStation {
public:
    void build(const World& world);
    void draw(World& world) const;

private:
    Mesh structure_;
};
