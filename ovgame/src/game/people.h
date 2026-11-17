#pragma once
// Personages in de wereld:
//  - People: een pool willekeurige personages uit de personage-generator (reizigers), het personage van de speler,
//    conducteurs en machinisten (NS-uniform).
//  - TrainPeople: bij de treinen een conducteur (stapt uit op het perron, kijkt langs de trein, fluit, stapt in,
//    loopt door de trein en controleert kaartjes, ook bij jou), reizigers die op het perron wachten en in- en
//    uitstappen, bezette stoelen en een machinist in de voorste cabine.
#include <map>
#include <set>
#include <string>
#include <vector>
#include "character.h"
#include "train.h"

class People {
public:
    void init(int poolSize);
    void setPlayer(const Appearance& a);
    const CharModel& model(int look) const;
    int poolSize() const { return (int)pool_.size(); }
    const CharModel& player() const { return player_; }
    static const int CONDUCTORS = 6, DRIVERS = 4;

private:
    std::vector<CharModel> pool_, conductors_, drivers_;
    CharModel player_;
};

class TrainPeople {
public:
    // playerVehicle/Car/lx/lz: waar de speler is (kaartcontrole)
    void update(float dt, const std::vector<Train*>& trains, double clock, Vec3 cam, const Vehicle* playerVehicle, int playerCar, float playerLx,
                float playerLz, const Collision& col, int lookCount);
    void collect(std::vector<PersonDraw>& out, const std::vector<Train*>& trains, Vec3 cam) const;
    void reset();
    bool conductorNear(Vec3 from, Vec3& pos, float& h) const;  // test: dichtstbijzijnde conducteur op een perron
    // kaartcontrole
    std::string prompt;   // tekst bij het richtkruis als de conducteur bij jou staat
    void showTicket();    // E: OV-chipkaart laten zien
    std::string notice;   // melding (bijv. "Dank u wel, goede reis!")
    float noticeT = 0;

private:
    struct Conductor {
        Train* t = nullptr;
        int look = 0;
        int mode = 0;  // 0 binnen, 1 naar de deur, 2 op het perron, 3 terug naar binnen
        int car = 0;
        float lx = 0, lz = 0, ly = 0, lh = 0;
        Vec3 pos;
        float h = 0;
        float doorZ = 0;
        int side = 1;
        float speed = 0, phase = 0, dir = 1, nextCheck = 6, checkT = 0, whistleT = 0, t0 = 0;
        bool whistled = false, checkedPlayer = false, atPlayer = false;
        bool seen = false;
    };
    struct Pax {
        Train* t = nullptr;
        int svc = 0, stop = 0, look = 0, variant = 0;
        enum St { WAIT, BOARD, ALIGHT, LEAVE } st = WAIT;
        int car = 0;
        float doorZ = 0;
        int side = 1;
        bool inside = false;   // lopen in de bak (lokaal)
        float lx = 0, lz = 0, lh = 0;
        Vec3 pos;
        float h = 0, speed = 0, phase = 0, delay = 0;
        std::vector<Vec3> path;
    };
    std::map<const Train*, Conductor> cond_;
    std::vector<Pax> pax_;
    std::set<uint64_t> spawned_, alighted_;
    Conductor* talking_ = nullptr;
};
