#pragma once
#include <string>
#include <vector>
#include "../engine/math.h"
#include "../engine/platform.h"

class World;
class Collision;
class Vehicle;

class Player {
public:
    Vec3 pos;           // voeten (game-coordinaten)
    float yaw = 0;      // 0 = kijken naar noord (-z), positief = met de klok mee
    float pitch = 0;
    bool fly = false;
    float spaceTimer_ = 1e9f;   // tijd sinds de vorige spatie (dubbel spatie = vliegen aan/uit)
    float eyeH = 1.7f;  // ooghoogte boven de voeten (volgt het personage)
    float sitEye = 0.75f; // ooghoogte boven de zitting
    float moveSpeed = 0; // horizontale snelheid (m/s), voor de loopanimatie

    // In een voertuig (bak-coordinaten)
    Vehicle* vehicle = nullptr;
    int car = 0;
    float lx = 0, lz = 0;
    int seat = -1;      // zitplaats (-1 = staan)
    bool driving = false;  // zelf aan het stuur (bus)
    bool doorBlocked = false;  // gas gegeven met open deuren (melding)

    // Wat het richtkruis aanwijst (stoel, deurhendel, ...); E of klik kiest het
    bool hasTarget = false;
    std::string targetLabel;

    // vehicles: voertuigen waar je door een open deur in kunt lopen
    void update(float dt, const Input& in, const World& world, const Collision& col, const std::vector<Vehicle*>& vehicles);
    Vec3 eye() const;
    Vec3 forward() const;
    void board(Vehicle* v, int carIndex, float localX, float localZ);
    void alight(Vec3 worldPos);
    // dichtstbijzijnde vrije zitplaats binnen bereik (-1 = geen)
    int nearSeat() const;

private:
    float vy_ = 0;
    bool onGround_ = true;
    float prevH_ = 0;
};
