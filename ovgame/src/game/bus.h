#pragma once
// 3D-model van een VDL Citea (elektrisch), 12 m of 18 m geleed, opgebouwd uit code: carrosserie,
// doorzichtige ramen, losse deurbladen (rechterzijde), interieur met stoelen, stangen en chauffeur.
#include <vector>
#include "../engine/renderer.h"
#include "vehicle.h"

// Materiaalcodes in Vertex::extra voor voertuigen (shader-modus 2)
enum VehicleMaterial { MAT_PAINT = 0, MAT_GLASS = 1, MAT_RUBBER = 2, MAT_LIGHT = 3, MAT_METAL = 4, MAT_INTERIOR = 5 };  // 5: binnen, egaal kunstlicht

struct BusSection {
    Mesh body, glass;          // opaak (voertuig-shader) en ramen (glas-shader, transparant)
    float length = 12.0f;
    std::vector<float> doorZ;  // deuren (rechterzijde = lokale -x), midden
    float axleF = 0, axleR = 0;  // assen (lokale z); bij de achterwagen: axleF = koppelpunt
};

struct BusModel {
    bool articulated = false;
    BusSection sec[2];         // voorwagen (bij 12 m de hele bus) en achterwagen
    Mesh bellows;              // harmonica (alleen geleed)
    Mesh doorLeaf;             // een deurblad (midden in de oorsprong), wordt per deur twee keer getekend
    float jointGap = 0;
    Mesh driver, wheel;        // chauffeur (niet als de speler rijdt) en stuur (draait mee)
    Vec3 wheelPos, driverEye;  // voorwagen, lokaal
    struct Spot { int sec; float x, y, z; };
    std::vector<Spot> stopButtons;  // rode stopknoppen op de stangen
    std::vector<Spot> screens;      // midden van de informatieschermen (beeld naar -z)
    std::vector<SeatInfo> seats;
    int sections() const { return articulated ? 2 : 1; }
    float totalLength() const { return sec[0].length + (articulated ? jointGap + sec[1].length : 0); }
};

constexpr float BUS_HW = 1.275f;      // halve breedte (2,55 m)
constexpr float BUS_FLOOR = 0.36f;    // lagevloer
constexpr float BUS_DOOR_W = 1.2f;

BusModel buildCitea(bool articulated);
