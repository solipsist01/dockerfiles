#pragma once
// Generieke NS-Sprinter met lage vloer en Jacobs-draaistellen (SNG, SLT): vorm, indeling en livrei uit een specificatie.
// Canoniek rijtuig: +z = naar de cabine (eindrijtuigen), y omhoog vanaf de spoorstaaf.
#include <utility>
#include <vector>
#include "rollingstock.h"

struct SprinterSpec {
    const char* typeName;
    const char* product = "Sprinter";      // "Intercity" voor de ICM
    const char* soundSet = nullptr;         // opgenomen geluid (data/audio/<naam>, tools/make_train_sound.py)
    const char* tsModel = nullptr;          // model uit Train Simulator (data/trains/<naam>, tools/ts/trains.py)
    bool jacobs = true;                     // Jacobs-draaistellen tussen de rijtuigen; anders twee per rijtuig
    float cabFloor = 0;                     // cabinevloer (0 = vloer + 0,40; ICM: hoog boven de doorloop)
    bool steps = false;                     // hoge vloer: opstap onder de deur
    // dwarsprofiel (halve breedte als functie van de hoogte boven de spoorstaaf), van onderkant schort tot dakmidden
    std::vector<float> py, px;
    float floorY, ceilY, bodyBottom, doorW, doorTop, gap;
    float winY0, winY1, winR;               // zijramen
    float vmax, acc, dec;
    struct Car { float len; bool cab; std::vector<float> doors; std::vector<std::pair<float, float>> windows; };
    std::vector<Car> cars;                  // canoniek (cabine aan +z bij eindrijtuigen)
    // neus: superellips (bovenaanzicht), reikwijdte op de hartlijn als functie van de hoogte
    float noseLen, noseExp;
    std::vector<std::pair<float, float>> noseF;  // (hoogte, reikwijdte)
    float cabWall;                          // cabinewand (canoniek z, eindrijtuig)
    float firstEnd;                         // 1e klas tussen firstEnd en de cabinewand
    float wsY0, wsY1, wsA0, wsA1;           // voorruit: hoogte en halve breedte (m langs de neus) onder/boven
    float lampY, lampX, displayY;           // koplampen (hoogte, x), frontdisplay (midden)
    // livrei: kleur van de zijkant op (z, y) van rijtuig 'car'; kleur van de neus op hoogte y, op afstand a van het midden
    // (langs de neus, m) en aandeel s (0 = midden, 1 = waar de zijkant begint)
    Vec3 (*side)(const Car& car, float z, float y, float& mat);
    Vec3 (*front)(float y, float a, float s, float& mat);
    std::vector<float> bandYs;              // hoogtes waar de zijkleur verspringt (voor scherpe randen)
    Vec3 doorC, doorFrame, seat2, seat1, wall, floorC, ceilC;
    int pantoCar;
};

RollingStock* makeSprinter(const SprinterSpec* spec);
const SprinterSpec& sngSpec();
const SprinterSpec& sltSpec();
RollingStock* makeSng();
RollingStock* makeSlt();
const SprinterSpec& icmSpec();
RollingStock* makeIcm();
