#pragma once
// Treinomroep zoals bij de NS, uit losse ingesproken fragmenten (data/audio/trein, tools/make_train_announcements.py):
//  - station (als de speler op of bij een station staat): "Op spoor 7 komt binnen: de Intercity naar Alkmaar. Deze trein
//    stopt in ...", vertragingen ("De Sprinter naar Uitgeest van twaalf uur elf heeft een vertraging van ongeveer vijf
//    minuten") en treinen die hier beginnen ("... staat klaar op spoor 4").
//  - trein (als de speler in een trein zit): welkom, "Volgende station: ...", eindstation.
// De fragmenten komen pas van de server als ze nodig zijn (Announcer/DataStore).
#include <set>
#include <string>
#include <vector>
#include "announcer.h"
#include "../engine/math.h"

class Traffic;
class Train;
class World;

class Omroep {
public:
    Announcer station, train;
    // elke frame; ride = de trein waar de speler in zit (of nullptr)
    void update(float dt, const Traffic& traffic, const World& world, const Train* ride, Vec3 player, double clock);
    void mix(float* out, int frames) { station.mix(out, frames); train.mix(out, frames); }

private:
    std::set<std::string> said_;     // al omgeroepen: "<rit>|<soort>|<station>"
    std::string here_;               // station waar de speler nu is
    float checkT_ = 0;
    const Train* ride_ = nullptr;
    int rideAt_ = -2;                // laatst bekende halte-index van de rit
    bool rideStopped_ = false, welcomed_ = false;
};
