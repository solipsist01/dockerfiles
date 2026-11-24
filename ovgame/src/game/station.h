#pragma once
// Station Eindhoven Centraal (interieur, perrons, overkappingen) en eenvoudige perrons van andere stations.
#include <string>
#include <vector>
#include "../engine/meshbuilder.h"
#include "collision.h"
#include "escalator.h"
#include "signs.h"
#include "world.h"

struct StationDoor {
    Vec3 center;      // midden van de deuropening op vloerniveau
    float ax, az;     // richting langs de pui
    float width;      // totale breedte
    float height;
    float open = 0;   // 0..1
    Mesh left, right; // deurbladen (gesloten positie), schuiven langs (ax, az)
};

struct Elevator {
    Vec3 bottom, top;  // uitstappunten beneden en boven
    std::string label;                    // bestemming vanaf beneden
    std::string topLabel = "de passage";  // bestemming vanaf boven
};

struct PlatformInfo {
    std::string station;  // stationsnaam
    int track;            // spoornummer
    float x, z;           // een punt op de perronrand bij dit spoor
};

// Plek van een elektronisch NS-bord (zie boards.h). Bord in het verticale vlak: breedte langs 'across', voor- en achterkant
// kijken langs +normal / -normal. Perronbord: voorkant toont trackA, achterkant trackB (0 = zelfde als trackA).
struct BoardSpot {
    std::string station;
    int trackA = 0, trackB = 0;
    Vec3 c, normal, across;
    float w = 1.8f, h = 0.56f;
    bool overview = false;  // vertrekstaat in plaats van perronbord
};

class Stations {
public:
    void build(const World& world, Collision& col, SignAtlas& atlas);
    void update(float dt, Vec3 playerPos, Collision& col);
    void drawOpaque(World& world) const;
    void drawGlass(World& world) const;

    float platformY = 21.1f;          // perronhoogte Eindhoven Centraal
    std::vector<Elevator> elevators;
    std::vector<PlatformInfo> tracks;  // spoorposities voor treinen
    std::vector<BoardSpot> boards;     // elektronische borden (vertrekinformatie)
    // Is de positie in/bij Eindhoven Centraal? (voor de HUD)
    bool nearCentraal(Vec3 p) const;
    // omtrek van de eilandperrons van Eindhoven Centraal (game x, z) voor de kaart
    std::vector<std::vector<std::pair<float, float>>> centraalPlatforms() const;
    Vec3 bestCenter;       // midden station Best (perronniveau)
    float bestPlatformY = 10.3f;
    float bestHallY = 16.8f;
    // Utrecht Centraal (utrechtstation.cpp): startpunten in de hal en op perron 18/19, hoogte van de halvloer
    Vec3 utrechtHall, utrechtPlatform;
    float utrechtHallY = 0;
    // Zwolle (zwollestation.cpp): startpunt in de reizigerstunnel
    Vec3 zwolleTunnel;

private:
    std::vector<Mesh> opaque_, glass_;
    std::vector<StationDoor> doors_;
    Escalators escalators_;  // roltrappen met bewegende treden (Eindhoven Centraal, Zwolle)
};
