#include "player.h"
#include "collision.h"
#include "vehicle.h"
#include "world.h"


static const float RADIUS = 0.3f;
static const float STEP = 0.45f;  // maximale stap omhoog (traptrede, stoeprand, instap bus)

Vec3 Player::eye() const { return Vec3(pos.x, pos.y + eyeH, pos.z); }

Vec3 Player::forward() const {
    return Vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw));
}

void Player::board(Vehicle* v, int carIndex, float localX, float localZ) {
    vehicle = v;
    car = carIndex;
    lx = localX;
    lz = localZ;
    seat = -1;
    driving = false;
    prevH_ = v->carPose(car).h;
    fly = false;
}

void Player::alight(Vec3 p) {
    if (vehicle && driving) vehicle->setDriver(false);
    vehicle = nullptr;
    seat = -1;
    driving = false;
    pos = p;
    vy_ = 0;
    onGround_ = true;
}

int Player::nearSeat() const {
    if (!vehicle || seat >= 0) return -1;
    int best = -1;
    float bd = 1.3f;
    for (int i = 0; i < vehicle->seatCount(); i++) {
        SeatInfo s = vehicle->seat(i);
        if (s.car != car) continue;
        if (s.y >= 0 && std::fabs(s.y - (pos.y - vehicle->carPose(car).y)) > 0.8f) continue;  // ander dek
        float d = std::hypot(s.lx - lx, s.lz - lz);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void Player::update(float dt, const Input& in, const World& world, const Collision& col, const std::vector<Vehicle*>& vehicles) {
    if (!vehicle) { hasTarget = false; targetLabel.clear(); }
    if (in.captured) {
        const float sens = 0.0022f;
        yaw += in.mouseDX * sens;
        pitch = clampf(pitch - in.mouseDY * sens, -1.5f, 1.5f);
    }
    if (in.pressed['F'] && !vehicle) {
        fly = !fly;
        vy_ = 0;
    }
    // dubbel spatie: vliegen aan/uit (zoals in Minecraft)
    spaceTimer_ += dt;
    if (in.captured && in.pressed[VK_SPACE] && !vehicle) {
        if (spaceTimer_ < 0.3f) {
            fly = !fly;
            vy_ = 0;
            spaceTimer_ = 1e9f;
        } else {
            spaceTimer_ = 0;
        }
    }
    float f = 0, r = 0;
    if (in.captured) {
        if (in.down['W']) f += 1;
        if (in.down['S']) f -= 1;
        if (in.down['D']) r += 1;
        if (in.down['A']) r -= 1;
    }
    bool fast = in.down[VK_SHIFT];
    Vec3 fwd(std::sin(yaw), 0, -std::cos(yaw));
    Vec3 right(std::cos(yaw), 0, std::sin(yaw));
    Vec3 move = fwd * f + right * r;
    float ml = length(move);
    if (ml > 0) move = move * (1.0f / ml);

    // ---------- in een voertuig: lopen in bak-coordinaten ----------
    if (vehicle) {
        Vehicle* v = vehicle;
        CarPose c = v->carPose(car);
        float dh = wrapAngle(c.h - prevH_);
        yaw -= dh;  // meedraaien
        prevH_ = c.h;
        // ---------- zelf rijden ----------
        if (driving) {
            hasTarget = false;
            targetLabel.clear();
            float ex, ey, ez;
            if (!v->driverEye(ex, ey, ez)) driving = false;
            bool use = in.pressed['E'] || (in.captured && in.lClick);
            int dcar = v->driverCar();
            if (driving && use) {
                // opstaan: naast de bestuurdersstoel
                v->setDriver(false);
                driving = false;
                car = dcar;
                v->driverStand(lx, lz);
                if (!v->walkable(car, lx, lz)) lx = -0.2f;
                float fy = v->floorAt3(car, lx, lz, ey - 1.2f);
                pos = v->carToWorld(car, lx, fy, lz);
                prevH_ = v->carPose(car).h;
                vy_ = 0;
                return;
            }
            if (driving) {
                DriveControls dc;
                if (in.captured) {
                    dc.throttle = in.down['W'] ? 1.0f : 0.0f;
                    dc.brake = in.down['S'] ? 1.0f : 0.0f;
                    dc.steer = (in.down['A'] ? 1.0f : 0.0f) - (in.down['D'] ? 1.0f : 0.0f);
                    dc.notch = (in.pressed['W'] ? 1 : 0) - (in.pressed['S'] ? 1 : 0);
                }
                dc.doorToggle = in.pressed['Q'];
                dc.doorRelease = in.pressed['T'] || (in.pad && in.pressed['Q']);  // controller: B
                dc.doorLeft = in.pressed['Q'];
                dc.doorRight = in.pressed['R'];
                dc.ack = in.pressed[VK_SPACE];
                dc.emergency = in.pressed[VK_BACK] || (in.pad && in.pressed['B']);  // controller: LB
                dc.activate = in.pressed['K'];
                dc.panto = in.pressed['P'];
                doorBlocked = !v->railVehicle() && dc.throttle > 0 && v->doorOpen() > 0.02f;
                v->drive(dc);
                car = dcar;
                pos = v->carToWorld(dcar, ex, ey - eyeH, ez);
                vy_ = 0;
                return;
            }
        }
        doorBlocked = false;
        if (in.pressed['B']) v->stopButton();
        // richtkruis: welke stoel of welk interactiepunt wordt aangekeken? (binnen 2,6 m)
        hasTarget = false;
        targetLabel.clear();
        Hotspot best{Hotspot::SEAT, 0, 0, 0, 0, 0, -1, ""};
        if (seat < 0) {
            std::vector<Hotspot> hs;
            for (int i = 0; i < v->seatCount(); i++) {
                SeatInfo s = v->seat(i);
                if (std::abs(s.car - car) > 1 || v->seatTaken(i)) continue;
                float fy = s.y >= 0 ? s.y : v->floorHeight();
                hs.push_back({Hotspot::SEAT, s.car, s.lx, fy + v->seatHeight() + 0.12f, s.lz, 0.30f, i, "E / klik: gaan zitten"});
            }
            v->hotspots(hs);
            Vec3 e = eye(), f = forward();
            float bestT = 1e9f;
            for (const Hotspot& h : hs) {
                if (std::abs(h.car - car) > 1) continue;
                Vec3 p = v->carToWorld(h.car, h.lx, h.ly, h.lz);
                Vec3 d = p - e;
                float t = dot(d, f);
                if (t < 0.15f || t > 2.6f) continue;
                Vec3 q = e + f * t;
                if (length(p - q) < h.radius && t < bestT) { bestT = t; best = h; hasTarget = true; }
            }
            if (hasTarget) targetLabel = best.label;
        }
        bool use = in.pressed['E'] || (in.captured && in.lClick);
        if (use && seat < 0 && hasTarget && best.kind == Hotspot::DOOR) v->activate(best);
        if (use && seat < 0 && hasTarget && best.kind == Hotspot::DRIVE) {
            // aan het stuur / achter de lessenaar: jij rijdt
            driving = true;
            car = v->driverCar();
            v->setDriver(true);
            yaw = PI - v->carPose(car).h;
            pitch = -0.12f;
            prevH_ = v->carPose(car).h;
            return;
        }
        // zitten / opstaan
        if (use && (seat >= 0 || (hasTarget && best.kind == Hotspot::SEAT))) {
            if (seat >= 0) {
                SeatInfo s = v->seat(seat);
                lx = s.lx > 0 ? 0.2f : -0.2f;
                lz = s.lz;
                float cy = s.y >= 0 ? s.y : v->floorHeight();
                if (!v->walkable3(car, lx, lz, cy)) lx = 0;
                pos.y = v->carPose(car).y + cy;
                seat = -1;
            } else {
                int s = best.id;
                if (s >= 0) {
                    car = v->seat(s).car;
                    seat = s;
                    SeatInfo si = v->seat(s);
                    // kijkrichting naar voren in de bak
                    yaw = PI - c.h - si.facing;
                    pitch = 0;
                }
            }
        }
        if (seat >= 0) {
            SeatInfo s = v->seat(seat);
            car = s.car;
            lx = s.lx;
            lz = s.lz;
            pos = v->carToWorld(car, lx, (s.y >= 0 ? s.y : v->floorHeight()) + v->seatHeight() + sitEye - eyeH, lz);
            vy_ = 0;
            return;
        }
        float speed = fast ? 3.5f : 1.8f;
        float wx = move.x * speed * dt, wz = move.z * speed * dt;
        float ch = std::cos(c.h), sh = std::sin(c.h);
        float nlx = lx + ch * wx - sh * wz;
        float nlz = lz + sh * wx + ch * wz;
        // via de doorloop naar de volgende bak (lokale +z wijst naar de bak met lagere index)
        float half = v->carLen(car) / 2, gap = v->jointGap();
        if (nlz > half + gap / 2 && car > 0 && std::fabs(nlx) < 0.6f) {
            car--;
            nlz -= half + gap + v->carLen(car) / 2;
            lz = nlz;
            prevH_ = v->carPose(car).h;
        } else if (nlz < -half - gap / 2 && car < v->carCount() - 1 && std::fabs(nlx) < 0.6f) {
            car++;
            nlz += half + gap + v->carLen(car) / 2;
            lz = nlz;
            prevH_ = v->carPose(car).h;
        }
        int side = v->doorSide();
        float hw = v->halfWidth();
        bool doorOk = v->doorOpen() > 0.8f && v->inDoorway(car, nlz, side) && nlx * side > hw - 0.6f;
        if (doorOk) {
            if (nlx * side > hw + 0.35f) {
                // uitstappen: de deur door, naar buiten
                Vec3 p = v->carToWorld(car, nlx, v->floorHeight(), nlz);
                float fl = col.floorAt(p.x, p.z, p.y + 0.5f);
                float g = world.heightAt(p.x, p.z);
                if (g <= p.y + 0.5f) fl = std::max(fl, g);
                if (fl > -1e8f) p.y = fl;
                alight(p);
                return;
            }
            lx = nlx; lz = nlz;
        } else {
            // huidige hoogte in de bak (dubbeldekker: welk dek / welke trap)
            float curY = pos.y - v->carPose(car).y;
            auto ok = [&](float x, float z) {
                return v->walkable3(car, x, z, curY) && std::fabs(v->floorAt3(car, x, z, curY) - curY) < 0.45f;
            };
            if (ok(nlx, nlz)) { lx = nlx; lz = nlz; }
            else if (ok(nlx, lz)) lx = nlx;
            else if (ok(lx, nlz)) lz = nlz;
        }
        {
            float curY = pos.y - v->carPose(car).y;
            pos = v->carToWorld(car, lx, v->floorAt3(car, lx, lz, curY), lz);
            v->playerAt(car, lx, lz);
        }
        vy_ = 0;
        return;
    }

    float ground = world.heightAt(pos.x, pos.z);
    if (fly) {
        // vliegen: spatie omhoog, shift omlaag, ctrl snel
        float speed = (in.down[VK_CONTROL]) ? 90.0f : 22.0f;
        Vec3 dir = forward() * f + right * r;
        float dl = length(dir);
        if (dl > 0) pos += dir * (speed * dt / dl);
        if (in.captured && in.down[VK_SPACE]) pos.y += speed * dt;
        if (in.captured && (in.down[VK_SHIFT] || in.down['C'])) pos.y -= speed * dt;
        ground = world.heightAt(pos.x, pos.z);
        if (pos.y < ground) pos.y = ground;
        return;
    }

    // ---------- lopen ----------
    float speed = fast ? 8.0f : 4.0f;
    auto groundAt = [&](float x, float z, float y, float* cx, float* cz) {
        float g = world.heightAt(x, z);
        float fl = col.floorAt(x, z, y + STEP, cx, cz);
        if (g > y + STEP) g = -1e9f;
        if (fl > g) return fl;
        if (cx) *cx = 0;
        if (cz) *cz = 0;
        return g;
    };
    Vec3 np = pos + move * (speed * dt);
    float gNew = groundAt(np.x, np.z, pos.y, nullptr, nullptr);
    if (gNew > pos.y + STEP) np = pos;
    pos.x = np.x;
    pos.z = np.z;
    col.pushOut(pos, RADIUS, 1.8f);

    // een voertuig in gelopen (alleen mogelijk door een open deur: de wanden zijn botsingsblokken)
    if (onGround_) {
        for (Vehicle* v : vehicles) {
            for (int c = 0; c < v->carCount(); c++) {
                float tx, tz;
                v->worldToCar(c, pos, tx, tz);
                float L = v->carLen(c);
                if (std::fabs(tz) < L / 2 - 0.3f && std::fabs(tx) < v->halfWidth() - 0.2f &&
                    std::fabs(pos.y - (v->carPose(c).y + v->floorHeight())) < 0.7f) {
                    board(v, c, tx, tz);
                    return;
                }
            }
        }
    }

    float cvx = 0, cvz = 0;
    ground = groundAt(pos.x, pos.z, pos.y, &cvx, &cvz);
    if (onGround_) { pos.x += cvx * dt; pos.z += cvz * dt; }  // roltrap
    if (onGround_ && in.captured && in.pressed[VK_SPACE]) { vy_ = 4.8f; onGround_ = false; }
    vy_ -= 18.0f * dt;
    pos.y += vy_ * dt;
    if (pos.y <= ground) {
        pos.y = ground;
        vy_ = 0;
        onGround_ = true;
    } else if (onGround_ && pos.y - ground < 0.35f && vy_ <= 0) {
        pos.y = ground;
        vy_ = 0;
    } else {
        onGround_ = false;
    }
    if (ground < -1e8f && pos.y < -50) pos.y = world.heightAt(pos.x, pos.z);  // vangnet
}
