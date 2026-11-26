#pragma once
// Een trein die een rit uit de dienstregeling rijdt (zie traffic.h). Het materieel (FLIRT, VIRM) bepaalt vorm en maten.
// De trein rijdt altijd in de richting van toenemende d langs het pad; rijtuig 0 rijdt voorop.
// De beveiliging (traffic.cpp) geeft per stap een 'rijtoestemming': hoe ver de kop maximaal mag rijden.
#include <algorithm>
#include <string>
#include "../engine/renderer.h"
#include "collision.h"
#include "railways.h"
#include "rollingstock.h"
#include "vehicle.h"
#include "world.h"

// Halte (of doorkomst) van een rit. Tijden in seconden sinds middernacht van dag 0 (kan > 86400 zijn).
struct StopTime {
    std::string station;
    int track = 0;        // spoornummer
    float d = 0;          // positie van het treinmidden langs het pad
    double arr = 0, dep = 0;
    bool stop = true;     // false = doorkomst (Intercity door Strijp-S en Best)
    float vcruise = 99;   // kruissnelheid op het baanvak naar deze halte (m/s), zo gekozen dat de trein op tijd rijdt
};

struct SpeedZone { float d0, d1, v; };  // snelheidsbeperking langs het pad (m/s)

// Een rit uit de dienstregeling
struct Service {
    std::vector<SpeedZone> zones;
    int id = 0;
    const RollingStock* rs = nullptr;
    std::string number;       // treinserie, bijv. "6500"
    std::string dest;         // eindbestemming (ook buiten de spelwereld)
    const TrackPath* path = nullptr;
    std::vector<StopTime> stops;
    double entry = 0;         // tijdstip waarop de trein het begin van het pad binnenrijdt
    float entrySpeed = 20;    // m/s
    int destIdx = 0;          // index in de namenlijst van het materieel
    std::vector<int> stopIdx; // per halte: index in de namenlijst
    float lineMax = 38.9f;    // baanvaksnelheid (m/s)
    bool startAtStop = false; // begint stilstaand bij de eerste halte (geen binnenkomst uit de mist)
    bool hasPred = false;     // wordt gereden door de trein van een eerdere rit (kopmaken), niet apart starten
    const Service* next = nullptr;  // vervolgrit na kopmaken op de laatste halte
    std::string product() const { return rs ? rs->product : "Trein"; }
};

struct TrainSoundState {
    int id = 0;            // welke trein (om gebeurtenissen niet dubbel af te spelen bij wisselen van geluidskanaal)
    float speed = 0;       // m/s
    float effort = 0;      // -1 (remmen) .. 1 (optrekken)
    float doorMove = 0;    // 0..1: deuren in beweging
    float doors = 0;       // 0 dicht .. 1 open
    const char* soundSet = nullptr;  // opgenomen geluid (RollingStock::soundSet) of nullptr
    bool beeping = false;  // deursluitsignaal
    int doorOpenEvents = 0, whistleEvents = 0, airEvents = 0, doorClosedEvents = 0;
};

class Train : public Vehicle {
public:
    bool richScreens = false;  // speler zit erin: de binnenschermen tekent de game (Boards), niet de vaste "Volgende station"
    void start(const Service* s, const Collision& col);
    // Een stap: authority = maximale positie van de kop langs het pad (beveiliging); clock = huidige tijd.
    void update(float dt, double clock, float authority, const Collision& col);
    void addCollision(Collision& col) const;   // wanden als botsingsblokken (met open deuren)
    void drawOpaque(World& world) const;
    void drawGlass(World& world) const;

    // rijtuigen (Vehicle)
    int carCount() const override { return rs_->cars; }
    float carLen(int i) const override { return rs_->len[i]; }
    CarPose carPose(int i) const override;
    float floorAt(int car, float lz) const override { return rs_->floorAt(car, lz); }
    bool walkable(int car, float lx, float lz) const override { return rs_->walkable(car, lx, lz); }
    bool inDoorway(int car, float lz, int side) const override { (void)side; return rs_->inDoorway(car, lz); }
    float doorOpen() const override { return doorOpen_; }
    int doorSide() const override { return doorSide_; }
    float halfWidth() const override { return rs_->halfW; }
    float jointGap() const override { return rs_->gap; }
    float floorHeight() const override { return rs_->floorY; }
    float floorAt3(int car, float lx, float lz, float curY) const override { return floorAt3Cab(car, lx, lz, curY); }
    bool walkable3(int car, float lx, float lz, float curY) const override;
    float floorAt3Cab(int car, float lx, float lz, float curY) const;
    void hotspots(std::vector<Hotspot>& out) const override;
    void activate(const Hotspot& h) override;
    void playerAt(int car, float lx, float lz) override;
    int seatCount() const override { return (int)rs_->seats.size(); }
    SeatInfo seat(int i) const override { return rs_->seats[i]; }
    bool seatTaken(int i) const override { return i >= 0 && i < (int)seatLook.size() && seatLook[i] >= 0; }
    // zelf rijden
    bool driverEye(float& lx, float& ly, float& lz) const override;
    void setDriver(bool on) override;
    void drive(const DriveControls& c) override;
    int driverCar() const override { return leadCar(); }
    void driverStand(float& lx, float& lz) const override;
    bool railVehicle() const override { return true; }
    int leadCar() const { return flipped_ ? rs_->cars - 1 : 0; }
    bool manual() const { return manual_; }
    bool driverIn() const { return driverIn_; }
    void setManual(bool on, int realism, bool dutyMode);
    static int defaultRealism;     // 0 simpel, 1 middel, 2 zo echt mogelijk (opties)
    DutyStats* duty = nullptr;     // dienst van de speler (anders nullptr)
    // cabinescherm
    struct CabState {
        float vPerm = 0, vLine = 0, distAuth = 0, distStop = 1e9f;
        int notch = 0, code = 140, realism = 1;
        bool bell = false, vigilance = false, emergency = false, ready = true, cabActive = true, panto = true, departReady = false;
        std::string msg;
    };
    CabState cabState() const;
    // conducteur en reizigers
    bool dwelling() const { return state_ == DWELL; }
    double dwellLeft() const;      // s tot de deuren sluiten (stilstaand)
    int atIndex() const { return at_; }
    int targetIndex() const { return target_; }
    CarPose poseAtStop(int stopIdx, int car) const;
    int sideAtStop(int stopIdx, const Collision& col) const;
    float cabDoor(int end) const { return cabDoor_[end]; }
    void openGangway(int g) { if (g >= 0 && g < (int)gang_.size()) { gangTarget_[g] = 1; gangTimer_[g] = std::max(gangTimer_[g], 3.0f); } }
    std::vector<int16_t> seatLook;  // per stoel: personage dat er zit (-1 = vrij)
    bool seatsFilled = false;

    const Service& service() const { return *svc_; }
    const RollingStock& stock() const { return *rs_; }
    float front() const { return d_ + halfLen_; }   // kop langs het pad
    float rear() const { return d_ - halfLen_; }
    float pathPos() const { return d_; }
    bool finished() const { return parked_ || (!svc_->next && d_ + halfLen_ >= svc_->path->length - 1.0f); }
    bool doorsOpen() const { return doorOpen_ > 0.9f; }
    bool stopped() const { return state_ == DWELL; }
    float speed() const { return v_; }
    float brakingDistance() const { return v_ * v_ / (2 * rs_->dec); }
    const std::string& destination() const { return svc_->dest; }
    std::string nextStop() const;        // volgende halte (rijdend) of huidige halte (stilstaand); "" = geen meer
    std::string currentStation() const;  // "" als de trein rijdt
    int currentTrack() const;
    double departureTime() const;        // geplande vertrektijd van de huidige halte (stilstaand)
    int lastStopIndex() const { return at_; }  // laatst bereikte/gepasseerde halte (-1 = nog geen)
    bool waitingForSignal() const { return state_ == RUN && v_ < 0.1f && signalHold_; }
    const TrainSoundState& sound() const { return snd_; }
    float maxDelay() const { return maxDelay_; }  // grootste vertraging bij vertrek/doorkomst (s)
    std::string label() const { return rs_->product + " " + svc_->number + " naar " + svc_->dest; }
    // halte waar de trein staat, anders de volgende halte waar hij stopt (nullptr = geen meer)
    const StopTime* nextStopTime() const {
        if (state_ == DWELL && at_ >= 0) return &svc_->stops[at_];
        return target_ >= 0 && target_ < (int)svc_->stops.size() ? &svc_->stops[target_] : nullptr;
    }

private:
    enum State { DWELL, RUN } state_ = RUN;
    const Service* svc_ = nullptr;
    const RollingStock* rs_ = nullptr;
    float d_ = 0, v_ = 0, effort_ = 0, halfLen_ = 0;
    bool flipped_ = false;    // na kopmaken: rijtuig 0 rijdt achteraan
    float maxDelay_ = 0;
    std::vector<float> gang_, gangTarget_, gangTimer_;  // tussendeuren per doorloop
    std::vector<float> off_;  // middenpositie per rijtuig t.o.v. het treinmidden (+ = richting toenemende d)
    int at_ = -1;             // index van de halte waar de trein staat / als laatste stond of passeerde
    int target_ = 0;          // volgende halte waar gestopt wordt
    double clock_ = 0;
    bool parked_ = false;  // eindpunt zonder vervolgrit: naar het opstelterrein
    float dwellElapsed_ = 0;
    float doorOpen_ = 0;
    bool signalHold_ = false;
    int doorSide_ = 1;
    bool whistled_ = false, opened_ = true, aired_ = true;
    TrainSoundState snd_;
    // zelf rijden
    bool manual_ = false, driverIn_ = false, dutyMode_ = false;
    int realism_ = 1;
    DriveControls ctl_;
    int notch_ = 0;
    bool emergency_ = false, cabActive_ = true, pantoUp_ = true, atbBell_ = false, atbAck_ = false, vigBell_ = false;
    float readyT_ = 10, atbT_ = 0, vigT_ = 0;
    int atbCode_ = 140;
    float vPerm_ = 40, vLine_ = 40, distAuth_ = 1e9f;
    bool departReady_ = false, departPending_ = false;
    std::string cabMsg_;
    float cabMsgT_ = 0;
    float cabDoor_[2] = {0, 0}, cabDoorTarget_[2] = {0, 0};
    const TrackPath& path() const { return *svc_->path; }
    void computeDoorSide(const Collision& col);
    void nextTarget();
    void dwellStep(float dt, double clock, float authority, const Collision& col);
    void updateManual(float dt, double clock, float authority, const Collision& col);
    void cabMsg(const std::string& s, bool penalty);
    bool cabCanon(int car, float lx, float lz, float& x, float& z) const;  // in de cabine-zone van een eindrijtuig?
};
