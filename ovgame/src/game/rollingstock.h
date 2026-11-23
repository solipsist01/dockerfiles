#pragma once
// Treinmaterieel: vorm, maten, deuren en meshes van een treintype (gedeeld door alle treinen van dat type).
//  - FLIRT (NS Sprinter, 4-delig)      flirt.cpp
//  - VIRM  (NS Intercity dubbeldekker)  virm.cpp
//  - ICNG  (NS Intercity Nieuwe Generatie, 5-delig)  icng.cpp
// Lokale assen per rijtuig zoals in vehicle.h: +z = voorkant, y = 0 op spoorhoogte. Rijtuig 0 rijdt voorop
// (cabine aan +z), het laatste rijtuig heeft de cabine aan -z (wordt gedraaid getekend).
#include <array>
#include <string>
#include <vector>
#include "../engine/renderer.h"
#include "signs.h"
#include "vehicle.h"
#include "world.h"

// Wat de trein op dit moment toont (per frame)
struct StockDraw {
    const Vehicle* v;
    float doorOpen;   // 0..1
    int doorSide;     // +1 / -1 (wereldzijde t.o.v. rijtuig-assen van een niet-gedraaid rijtuig)
    int dest;         // index in de namenlijst (frontdisplay)
    int next;         // index in de namenlijst (binnenschermen: volgende station)
    int lead = 0;     // 0: rijtuig 0 rijdt voorop, 1: het laatste rijtuig rijdt voorop (koplampen / sluitseinen)
    const float* gang = nullptr;  // tussendeuren per doorloop i (tussen rijtuig i en i+1): 0 dicht .. 1 open
    float cabDoor[2] = {0, 0};    // cabinedeuren (rijtuig 0 / laatste rijtuig): 0 dicht .. 1 open
    float lever = 0;              // rijremschakelaar in de voorste cabine: -1 (snelrem) .. 1 (vol vermogen)
};

// Cabine in canonieke coordinaten van een eindrijtuig (cabine aan +z)
struct CabInfo {
    float wallZ;        // cabinewand (passagierskant)
    float doorX, doorW; // deur in de wand (midden, breedte)
    float floorY;       // cabinevloer
    float deskZ;        // achterkant van de lessenaar (verder kun je niet lopen)
    float seatX, seatZ; // bestuurdersstoel (midden zitting)
    float seatTop;      // hoogte zitting
    float leverX, leverY, leverZ;  // draaipunt van de rijremschakelaar
    float passFloor;    // vloer aan de passagierskant van de wand
};

class RollingStock {
public:
    virtual ~RollingStock() {}
    std::string product = "Sprinter";  // getoond op displays en in de HUD
    std::string typeName = "FLIRT";
    int cars = 4;
    std::vector<float> len;                    // lengte per rijtuig
    std::vector<std::array<float, 2>> doorZ;   // deurposities per rijtuig (lokale z)
    float halfW = 1.41f, gap = 0.6f, floorY = 0.80f, doorW = 1.30f, height = 4.12f, nose = 2.4f;
    float vmax = 26.0f, acc = 0.9f, dec = 0.8f;  // m/s, m/s2

    int cabSign(int car) const { return car == 0 ? 1 : (car == cars - 1 ? -1 : 0); }
    float totalLength() const {
        float t = gap * (cars - 1);
        for (float l : len) t += l;
        return t;
    }
    bool inDoorway(int car, float lz) const {
        for (float dz : doorZ[car])
            if (std::fabs(lz - dz) < doorW / 2 - 0.25f) return true;
        return false;
    }
    virtual float floorAt(int car, float lz) const = 0;
    virtual bool walkable(int car, float lx, float lz) const = 0;
    // dubbeldekker: vloer/beloopbaarheid afhankelijk van de huidige hoogte (zie Vehicle::floorAt3)
    virtual float floorAt3(int car, float lx, float lz, float curY) const { (void)lx; (void)curY; return floorAt(car, lz); }
    virtual bool walkable3(int car, float lx, float lz, float curY) const { (void)curY; return walkable(car, lx, lz); }
    std::vector<SeatInfo> seats;  // zitplaatsen (lokale rijtuigcoordinaten)
    bool gangwayDoors = false;    // tussendeuren in de kopwanden (VIRM); anders open doorloop (FLIRT)
    CabInfo cab{};                // cabine van de eindrijtuigen (canoniek)
    float screenY = 2.80f;        // hoogte van de binnenschermen boven de deuren (midden), op doorZ
    // Meshes bouwen; names = alle teksten voor front- en binnendisplays (index = StockDraw::dest/next)
    virtual void build(SignAtlas& atlas, const std::vector<std::string>& names) = 0;
    virtual void drawOpaque(World& w, const StockDraw& s) const = 0;
    virtual void drawGlass(World& w, const StockDraw& s) const = 0;
};

RollingStock* makeFlirt();
RollingStock* makeVirm();
RollingStock* makeIcng();
