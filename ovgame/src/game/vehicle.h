#pragma once
// Gemeenschappelijke interface voor voertuigen waar de speler in kan lopen (trein, bus).
// Een voertuig bestaat uit een of meer bakken (rijtuigen / voor- en achterwagen) met elk een eigen pose.
// Lokale assen per bak: +z = voorkant, +x = links, y = omhoog (vloer van het voertuig op y = 0 is spoor/wegdek).
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
#include "../engine/math.h"

struct CarPose { float x, y, z, h; };

struct SeatInfo {
    int car;
    float lx, lz;     // zitplaats (midden van de zitting)
    float facing;     // kijkrichting t.o.v. de bak (0 = naar +z)
    float y = -1;     // vloerhoogte bij de stoel (dubbeldekker); -1 = floorHeight()
};

// Iets in een voertuig dat je met het richtkruis kunt kiezen (E / klik): een stoel, een tussendeur, ...
struct Hotspot {
    enum Kind { SEAT, DOOR, DRIVE } kind;
    int car;
    float lx, ly, lz;  // lokale positie (ly = hoogte boven spoor/weg)
    float radius;
    int id;            // stoelnummer of deurnummer
    std::string label; // tekst bij het richtkruis
};

// Bediening als de speler zelf rijdt (bus, trein)
struct DriveControls {
    float throttle = 0;   // 0..1 (W)
    float brake = 0;      // 0..1 (S; bij stilstand: achteruit)
    float steer = 0;      // -1 (rechts) .. +1 (links)
    bool doorToggle = false;
    // trein (gebeurtenissen deze frame)
    int notch = 0;        // rijremschakelaar een stand omhoog (+1, W) of omlaag (-1, S)
    bool doorRelease = false, doorLeft = false, doorRight = false;  // T / Q / R
    bool ack = false;     // spatie: ATB-bel / waakzaamheid bevestigen
    bool emergency = false, activate = false, panto = false;  // Backspace, K, P
};

// Verloop van een dienst die de speler rijdt (bus of trein): punctualiteit en meldingen
struct DutyStats {
    bool active = false, finished = false;
    std::string title;
    int served = 0, missed = 0, onTime = 0, late = 0, early = 0, penalties = 0, paxIn = 0, paxOut = 0;
    float worstLate = 0;
    std::vector<std::string> log;
    std::string msg;
    float msgT = 0;
    void note(const std::string& s, bool penalty = false) {
        msg = s;
        msgT = 6;
        log.push_back(s);
        if (penalty) penalties++;
        if (log.size() > 60) log.erase(log.begin());
    }
    // vertrek/doorkomst: vertraging in s (negatief = te vroeg)
    void departure(const std::string& where, double delay) {
        served++;
        if (delay > 179) late++;
        else if (delay < -59) early++;
        else onTime++;
        if (delay > worstLate) worstLate = (float)delay;
        int m = (int)std::lround(delay / 60.0);
        note(where + ": " + (m > 0 ? "+" + std::to_string(m) + " min" : m < 0 ? std::to_string(m) + " min (te vroeg)" : "op tijd"));
    }
};

class Vehicle {
public:
    // Zelf rijden: oogpositie van de bestuurder (bak 0, lokaal); false = kan niet bestuurd worden
    virtual bool driverEye(float& lx, float& ly, float& lz) const { (void)lx; (void)ly; (void)lz; return false; }
    virtual void setDriver(bool on) { (void)on; }
    virtual void drive(const DriveControls& c) { (void)c; }
    virtual int driverCar() const { return 0; }             // bak waarin de bestuurder zit
    virtual void driverStand(float& lx, float& lz) const {  // waar je staat na het opstaan
        float x = 0, y = 0, z = 0;
        driverEye(x, y, z);
        lx = 0;
        lz = z;
    }
    virtual bool railVehicle() const { return false; }      // trein: andere bediening
    virtual bool seatTaken(int i) const { (void)i; return false; }  // stoel bezet door een reiziger
    // stopknop (bus): toets B, ook als je zit
    virtual void stopButton() {}
    // Interactiepunten naast de stoelen (bijv. hendels van tussendeuren) en wat er gebeurt bij kiezen
    virtual void hotspots(std::vector<Hotspot>& out) const { (void)out; }
    virtual void activate(const Hotspot& h) { (void)h; }
    // waar de speler in het voertuig staat (bijv. tussendeur openhouden)
    virtual void playerAt(int car, float lx, float lz) { (void)car; (void)lx; (void)lz; }
    virtual ~Vehicle() {}
    virtual int carCount() const = 0;
    virtual float carLen(int i) const = 0;
    virtual CarPose carPose(int i) const = 0;
    virtual float floorAt(int car, float lz) const = 0;            // vloerhoogte in de bak
    virtual bool walkable(int car, float lx, float lz) const = 0;  // mag je hier staan?
    virtual bool inDoorway(int car, float lz, int side) const = 0; // staat lz voor een deur?
    virtual float doorOpen() const = 0;                            // 0..1
    virtual int doorSide() const = 0;                              // +1 = lokale +x, -1 = lokale -x
    virtual float halfWidth() const = 0;
    virtual float jointGap() const = 0;                            // ruimte tussen de bakken
    virtual float floorHeight() const = 0;                         // vloer bij de deuren (boven spoor/weg)
    // bak i+1 zit aan de -z kant van bak i
    virtual int seatCount() const { return 0; }
    virtual SeatInfo seat(int i) const { (void)i; return {0, 0, 0, 0}; }
    virtual float seatHeight() const { return 0.45f; }
    // Meerdere vloeren boven elkaar (dubbeldekker): vloer en beloopbaarheid hangen ook af van de huidige hoogte
    // (lokaal, boven spoor/weg) van wie er loopt.
    virtual float floorAt3(int car, float lx, float lz, float curY) const { (void)lx; (void)curY; return floorAt(car, lz); }
    virtual bool walkable3(int car, float lx, float lz, float curY) const { (void)curY; return walkable(car, lx, lz); }

    Vec3 carToWorld(int car, float lx, float ly, float lz) const {
        CarPose c = carPose(car);
        float ch = std::cos(c.h), sh = std::sin(c.h);
        return Vec3(c.x + ch * lx + sh * lz, c.y + ly, c.z - sh * lx + ch * lz);
    }
    void worldToCar(int car, Vec3 p, float& lx, float& lz) const {
        CarPose c = carPose(car);
        float ch = std::cos(c.h), sh = std::sin(c.h);
        float dx = p.x - c.x, dz = p.z - c.z;
        lx = ch * dx - sh * dz;
        lz = sh * dx + ch * dz;
    }
};
