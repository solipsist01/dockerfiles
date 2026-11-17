#include "people.h"
#include <algorithm>
#include <cmath>

namespace {
uint32_t hh(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
    return x;
}
struct R {
    uint32_t s;
    uint32_t next() { s = hh(s + 0x9E3779B9u); return s; }
    float f() { return (next() & 0xFFFFFF) / 16777216.0f; }
};
int poisson(float mean, R& r) {
    float L = std::exp(-mean), p = 1;
    int k = 0;
    do { k++; p *= r.f(); } while (p > L && k < 20);
    return k - 1;
}
void turn(float& h, float target, float rate) { h = wrapAngle(h + wrapAngle(target - h) * std::min(1.0f, rate)); }
bool walkW(Vec3& p, float& h, Vec3 t, float speed, float dt, float& cur) {
    float dx = t.x - p.x, dz = t.z - p.z, d = std::hypot(dx, dz);
    if (d < 0.04f) return true;
    float s = std::min(d, speed * dt);
    p.x += dx / d * s;
    p.z += dz / d * s;
    p.y += (t.y - p.y) * std::min(1.0f, s / std::max(d, 0.01f));
    turn(h, std::atan2(dx, dz), dt * 9);
    cur = speed;
    return d <= speed * dt;
}
bool walkL(float& lx, float& lz, float& lh, float tx, float tz, float speed, float dt, float& cur) {
    float dx = tx - lx, dz = tz - lz, d = std::hypot(dx, dz);
    if (d < 0.03f) return true;
    float s = std::min(d, speed * dt);
    lx += dx / d * s;
    lz += dz / d * s;
    turn(lh, std::atan2(dx, dz), dt * 9);
    cur = speed;
    return d <= speed * dt;
}
Vec3 poseToWorld(const CarPose& c, float lx, float ly, float lz) {
    float ch = std::cos(c.h), sh = std::sin(c.h);
    return Vec3(c.x + ch * lx + sh * lz, c.y + ly, c.z - sh * lx + ch * lz);
}
const float STRIDE = 1.35f;
float busyFactor(double clock) {
    double hod = std::fmod(clock, 86400.0) / 3600.0;
    if ((hod >= 7 && hod < 9.5) || (hod >= 16 && hod < 18.5)) return 1.6f;
    if (hod < 6.5 || hod > 21.5) return 0.35f;
    return 1.0f;
}
}  // namespace

// ============================== pool ==============================
void People::init(int n) {
    for (int i = 0; i < n; i++) pool_.push_back(buildCharacter(Appearance::random(1000u + (uint32_t)i * 7919u)));
    for (int i = 0; i < CONDUCTORS; i++) conductors_.push_back(buildCharacter(Appearance::conductor(500u + (uint32_t)i * 131u)));
    for (int i = 0; i < DRIVERS; i++) drivers_.push_back(buildCharacter(Appearance::driver(900u + (uint32_t)i * 17u)));
}

void People::setPlayer(const Appearance& a) {
    destroyCharacter(player_);
    player_ = buildCharacter(a);
}

const CharModel& People::model(int look) const {
    if (look == LOOK_PLAYER) return player_;
    if (look <= LOOK_DRIVER) return drivers_[(size_t)(LOOK_DRIVER - look) % drivers_.size()];
    if (look <= LOOK_CONDUCTOR) return conductors_[(size_t)(LOOK_CONDUCTOR - look) % conductors_.size()];
    return pool_[(size_t)look % pool_.size()];
}

// ============================== treinen ==============================
void TrainPeople::reset() {
    cond_.clear();
    pax_.clear();
    spawned_.clear();
    alighted_.clear();
    talking_ = nullptr;
    prompt.clear();
}

bool TrainPeople::conductorNear(Vec3 from, Vec3& pos, float& h) const {
    float best = 1e9f;
    for (const auto& kv : cond_) {
        const Conductor& c = kv.second;
        if (c.mode != 2) continue;
        float d = std::hypot(c.pos.x - from.x, c.pos.z - from.z);
        if (d < best) { best = d; pos = c.pos; h = c.h; }
    }
    return best < 1e8f;
}

void TrainPeople::showTicket() {
    if (!talking_) return;
    talking_->checkedPlayer = true;
    talking_->atPlayer = false;
    talking_->checkT = 2.0f;
    talking_ = nullptr;
    notice = "Conducteur: \"Dank u wel, goede reis!\"";
    noticeT = 4;
}

void TrainPeople::update(float dt, const std::vector<Train*>& trains, double clock, Vec3 cam, const Vehicle* pv, int pcar, float plx, float plz,
                         const Collision& col, int lookCount) {
    noticeT = std::max(0.0f, noticeT - dt);
    prompt.clear();
    talking_ = nullptr;
    lookCount = std::max(1, lookCount);
    auto alive = [&](const Train* t) { return std::find(trains.begin(), trains.end(), t) != trains.end(); };
    for (auto it = cond_.begin(); it != cond_.end();) {
        if (!alive(it->first)) it = cond_.erase(it);
        else ++it;
    }
    pax_.erase(std::remove_if(pax_.begin(), pax_.end(), [&](const Pax& p) { return !alive(p.t); }), pax_.end());
    float busy = busyFactor(clock);

    for (Train* t : trains) {
        const RollingStock& rs = t->stock();
        const Service& sv = t->service();
        CarPose mid = t->carPose(t->carCount() / 2);
        float dist = std::hypot(mid.x - cam.x, mid.z - cam.z);
        bool playerIn = pv == t;
        float hw = t->halfWidth();
        // ---- bezette stoelen ----
        if (!t->seatsFilled && dist < 450) {
            t->seatsFilled = true;
            R r{hh((uint32_t)sv.id * 2654435761u + 77)};
            float occ = 0.32f * busy * (rs.product == "Intercity" ? 1.15f : 1.0f);
            occ = std::min(occ, 0.75f);
            for (size_t s = 0; s < t->seatLook.size(); s++) t->seatLook[s] = r.f() < occ ? (int16_t)(r.next() % (uint32_t)lookCount) : (int16_t)-1;
        }
        if (dist > 300 && !playerIn) continue;
        bool dw = t->dwelling();
        double left = t->dwellLeft();

        // ---- conducteur ----
        Conductor& c = cond_[t];
        if (!c.seen) {
            c.seen = true;
            c.t = t;
            c.look = LOOK_CONDUCTOR - (int)((uint32_t)sv.id % People::CONDUCTORS);
            c.car = t->carCount() / 2;
            c.lz = 0;
            c.ly = t->floorAt3(c.car, 0, 0, rs.floorY);
            c.dir = (sv.id & 1) ? 1.0f : -1.0f;
        }
        c.speed = 0;
        c.whistleT = std::max(0.0f, c.whistleT - dt);
        if (!dw && c.mode != 0) {  // trein rijdt: de conducteur is binnen
            c.mode = 0;
            c.lx = 0;
            c.ly = t->floorAt3(c.car, 0, c.lz, rs.floorY);
        }
        if (c.mode == 0 && dw && t->doorOpen() > 0.95f && left > 14 && !c.atPlayer) {
            float bz = rs.doorZ[c.car][0];
            for (float z : rs.doorZ[c.car]) if (std::fabs(z - c.lz) < std::fabs(bz - c.lz)) bz = z;
            c.doorZ = bz;
            c.side = t->doorSide();
            c.mode = 1;
            c.whistled = false;
        }
        switch (c.mode) {
        case 0: {
            if (c.atPlayer) {
                if (!playerIn || pcar != c.car || std::fabs(plz - c.lz) > 3.5f || dw) c.atPlayer = false;
                else {
                    talking_ = &c;
                    prompt = "Conducteur: \"Goedendag, mag ik uw vervoersbewijs zien?\"   O: OV-chipkaart laten zien";
                    turn(c.lh, std::atan2(plx - c.lx, plz - c.lz), dt * 6);
                    break;
                }
            }
            if (dw) break;
            if (c.checkT > 0) { c.checkT -= dt; break; }
            float L = t->carLen(c.car);
            float nz = c.lz + c.dir * 0.85f * dt;
            float lxT = 0;
            bool ok = t->walkable3(c.car, 0, nz, c.ly);
            if (!ok)
                for (float tx : {0.5f, -0.5f})
                    if (t->walkable3(c.car, tx, nz, c.ly)) { ok = true; lxT = tx; break; }
            bool atEnd = c.dir > 0 ? nz > L / 2 - 0.45f : nz < -L / 2 + 0.45f;
            if (ok && !atEnd) {
                c.lx += clampf(lxT - c.lx, -dt * 0.9f, dt * 0.9f);
                c.lz = nz;
                c.speed = 0.85f;
                turn(c.lh, c.dir > 0 ? 0.0f : PI, dt * 6);
            } else {
                int next = c.dir > 0 ? c.car - 1 : c.car + 1;
                if (atEnd && next >= 0 && next < t->carCount()) {
                    t->openGangway(c.dir > 0 ? c.car - 1 : c.car);
                    c.car = next;
                    c.lz = c.dir > 0 ? -t->carLen(next) / 2 + 0.5f : t->carLen(next) / 2 - 0.5f;
                    c.lx = 0;
                } else {
                    c.dir = -c.dir;
                }
            }
            c.ly += (t->floorAt3(c.car, c.lx, c.lz, c.ly) - c.ly) * std::min(1.0f, dt * 8);
            // kaartjes controleren
            c.nextCheck -= dt;
            if (c.nextCheck <= 0) {
                R r{hh((uint32_t)(clock * 10) + (uint32_t)sv.id)};
                c.nextCheck = 5 + r.f() * 9;
                for (size_t s = 0; s < rs.seats.size(); s++) {
                    const SeatInfo& si = rs.seats[s];
                    if (si.car != c.car || t->seatLook[s] < 0 || std::fabs(si.lz - c.lz) > 1.0f) continue;
                    if (si.y >= 0 && std::fabs(si.y - c.ly) > 0.8f) continue;
                    c.checkT = 3.5f;
                    c.lh = std::atan2(si.lx - c.lx, si.lz - c.lz);
                    break;
                }
            }
            if (playerIn && pcar == c.car && std::fabs(plz - c.lz) < 2.0f && !c.checkedPlayer) c.atPlayer = true;
            break;
        }
        case 1: {
            float tx = c.side * (hw - 0.35f);
            if (std::fabs(c.lz - c.doorZ) > 0.05f) walkL(c.lx, c.lz, c.lh, 0, c.doorZ, 1.1f, dt, c.speed);
            else if (walkL(c.lx, c.lz, c.lh, tx, c.doorZ, 1.0f, dt, c.speed)) {
                CarPose cp = t->carPose(c.car);
                Vec3 w = poseToWorld(cp, c.side * (hw + 0.75f), 0, c.doorZ);
                float fy = col.floorAt(w.x, w.z, cp.y + rs.floorY + 0.6f);
                if (fy < cp.y) fy = cp.y + rs.floorY;
                c.pos = Vec3(w.x, fy, w.z);
                c.h = cp.h + c.lh;
                c.mode = 2;
                c.t0 = 0;
            }
            c.ly += (t->floorAt3(c.car, c.lx, c.lz, c.ly) - c.ly) * std::min(1.0f, dt * 8);
            break;
        }
        case 2: {
            // op het perron: langs de trein kijken (vooral naar voren), fluiten, instappen
            c.t0 += dt;
            float front = t->carPose(t->leadCar()).h;
            float look = std::fmod(c.t0, 9.0f) < 6.0f ? 0.0f : PI;
            turn(c.h, front + look, dt * 2.5f);
            if (left < 11.5 && !c.whistled) { c.whistled = true; c.whistleT = 1.8f; }
            if (left < 8.5 && c.whistleT <= 0) {
                c.mode = 3;
                c.lx = c.side * (hw + 0.7f);
                c.lz = c.doorZ;
            }
            break;
        }
        case 3: {
            float tx = std::fabs(c.lx) > hw - 0.42f ? c.side * (hw - 0.40f) : 0.0f;
            if (walkL(c.lx, c.lz, c.lh, tx, c.doorZ, 1.2f, dt, c.speed) && tx == 0.0f) c.mode = 0;
            c.ly = t->floorAt3(c.car, std::max(-hw + 0.3f, std::min(hw - 0.3f, c.lx)), c.lz, c.ly);
            break;
        }
        }
        c.phase = std::fmod(c.phase + c.speed * dt / STRIDE, 1.0f);

        // ---- reizigers op het perron ----
        int si = dw ? t->atIndex() : t->targetIndex();
        if (si >= 0 && si < (int)sv.stops.size()) {
            const StopTime& st = sv.stops[si];
            float distStop = st.d - t->pathPos();
            uint64_t key = ((uint64_t)(uint32_t)sv.id << 16) | (uint64_t)si;
            if (!spawned_.count(key) && (dw || (distStop < 1600 && distStop > -10))) {
                CarPose p0 = t->poseAtStop(si, t->carCount() / 2);
                if (std::hypot(p0.x - cam.x, p0.z - cam.z) < 230) {
                    spawned_.insert(key);
                    int side = t->sideAtStop(si, col);
                    R r{hh(key * 2654435761u + 5)};
                    for (int ci = 0; ci < t->carCount(); ci++) {
                        CarPose cp = t->poseAtStop(si, ci);
                        for (int k = 0; k < 2; k++) {
                            int n = poisson(0.55f * busy, r);
                            float dz = rs.doorZ[ci][k];
                            for (int j = 0; j < n; j++) {
                                Pax p;
                                p.t = t;
                                p.svc = sv.id;
                                p.stop = si;
                                p.look = (int)(r.next() % (uint32_t)lookCount);
                                p.variant = (int)(r.next() % 4);
                                p.car = ci;
                                p.doorZ = dz;
                                p.side = side;
                                Vec3 w = poseToWorld(cp, side * (hw + 1.6f + 0.7f * j + r.f() * 0.8f), 0, dz + (r.f() - 0.5f) * 2.4f);
                                float fy = col.floorAt(w.x, w.z, cp.y + rs.floorY + 0.6f);
                                if (fy < cp.y) fy = cp.y + rs.floorY;
                                p.pos = Vec3(w.x, fy, w.z);
                                p.h = std::atan2(cp.x - w.x, cp.z - w.z) + (r.f() - 0.5f) * 1.2f;
                                p.delay = 1.0f + r.f() * 2.5f + j * 0.8f;
                                pax_.push_back(p);
                            }
                        }
                    }
                }
            }
            // uitstappen: bij aankomst een paar reizigers uit de stoelen bij de deuren
            if (dw && t->doorOpen() > 0.9f && !alighted_.count(key)) {
                alighted_.insert(key);
                if (dist < 160) {
                    R r{hh(key * 40503u + 11)};
                    for (int ci = 0; ci < t->carCount(); ci++)
                        for (int k = 0; k < 2; k++) {
                            float dz = rs.doorZ[ci][k];
                            std::vector<int> cand;
                            for (size_t s = 0; s < rs.seats.size(); s++)
                                if (rs.seats[s].car == ci && t->seatLook[s] >= 0 && std::fabs(rs.seats[s].lz - dz) < 5.0f) cand.push_back((int)s);
                            int n = std::min((int)cand.size(), poisson(0.75f * busy, r));
                            for (int j = 0; j < n; j++) {
                                int pick = (int)(r.next() % cand.size());
                                int s = cand[pick];
                                cand.erase(cand.begin() + pick);
                                const SeatInfo& seat = rs.seats[s];
                                Pax p;
                                p.t = t;
                                p.svc = sv.id;
                                p.stop = si;
                                p.look = t->seatLook[s];
                                p.variant = (int)(r.next() % 4);
                                t->seatLook[s] = -1;
                                p.st = Pax::ALIGHT;
                                p.inside = true;
                                p.car = ci;
                                p.doorZ = dz;
                                p.side = t->doorSide();
                                p.lx = seat.lx;
                                p.lz = seat.lz;
                                p.pos.y = seat.y >= 0 ? seat.y : rs.floorY;
                                p.delay = j * 1.2f + r.f();
                                p.path = {Vec3(0, 0, seat.lz), Vec3(0, 0, dz), Vec3(p.side * (hw - 0.3f), 0, dz), Vec3(p.side * (hw + 0.8f), 0, dz)};
                                pax_.push_back(p);
                            }
                        }
                }
            }
        }
    }

    // ---- reizigers bewegen ----
    for (Pax& p : pax_) {
        Train* t = p.t;
        const RollingStock& rs = t->stock();
        float hw = t->halfWidth();
        p.speed = 0;
        bool dw = t->dwelling() && t->atIndex() == p.stop;
        switch (p.st) {
        case Pax::WAIT: {
            bool departed = !dw && (t->atIndex() >= p.stop || t->targetIndex() != p.stop);
            if (departed || (dw && t->dwellLeft() < 3.0)) {
                // trein gemist / vertrokken: weglopen
                R r{hh((uint32_t)(p.pos.x * 100) + (uint32_t)p.look)};
                CarPose cp = t->carPose(p.car);
                Vec3 out = Vec3(p.pos.x - cp.x, 0, p.pos.z - cp.z);
                out = normalize(out);
                Vec3 along(std::sin(cp.h), 0, std::cos(cp.h));
                p.path = {p.pos + out * 2.0f, p.pos + out * 2.5f + along * ((r.f() < 0.5f ? -1.0f : 1.0f) * (12 + r.f() * 10))};
                p.st = Pax::LEAVE;
                break;
            }
            if (dw && t->doorOpen() > 0.95f) {
                p.delay -= dt;
                if (p.delay <= 0) {
                    p.st = Pax::BOARD;
                    Vec3 o = t->carToWorld(p.car, p.side * (hw + 0.45f), 0, p.doorZ);
                    Vec3 in = t->carToWorld(p.car, p.side * (hw - 0.55f), rs.floorY, p.doorZ);
                    o.y = p.pos.y;
                    p.path = {o, in};
                }
            }
            break;
        }
        case Pax::BOARD:
            if (!p.path.empty() && walkW(p.pos, p.h, p.path.front(), 1.3f, dt, p.speed)) {
                p.path.erase(p.path.begin());
                if (p.path.empty()) {
                    // binnen: een vrije stoel in dit rijtuig
                    std::vector<int> free;
                    for (size_t s = 0; s < rs.seats.size(); s++)
                        if (rs.seats[s].car == p.car && t->seatLook[s] < 0) free.push_back((int)s);
                    if (!free.empty()) t->seatLook[free[(size_t)p.look % free.size()]] = (int16_t)p.look;
                    p.st = Pax::LEAVE;  // verdwijnt (zit nu op een stoel)
                    p.path.clear();
                }
            }
            break;
        case Pax::ALIGHT: {
            if (p.delay > 0) { p.delay -= dt; break; }
            bool open = t->doorOpen() > 0.8f;
            if (p.path.size() == 1 && !open) break;
            if (!p.path.empty()) {
                Vec3 tg = p.path.front();
                if (walkL(p.lx, p.lz, p.lh, tg.x, tg.z, 1.1f, dt, p.speed)) p.path.erase(p.path.begin());
                float fy = t->floorAt3(p.car, std::max(-hw + 0.3f, std::min(hw - 0.3f, p.lx)), p.lz, p.pos.y);
                p.pos.y += (fy - p.pos.y) * std::min(1.0f, dt * 6);
            }
            if (p.path.empty()) {
                // buiten op het perron
                CarPose cp = t->carPose(p.car);
                Vec3 w = poseToWorld(cp, p.lx, 0, p.lz);
                float fy = col.floorAt(w.x, w.z, cp.y + rs.floorY + 0.6f);
                if (fy < cp.y) fy = cp.y + rs.floorY;
                p.pos = Vec3(w.x, fy, w.z);
                p.h = cp.h + p.lh;
                R r{hh((uint32_t)p.look * 97u + (uint32_t)p.stop)};
                Vec3 out(std::cos(cp.h) * p.side, 0, -std::sin(cp.h) * p.side);
                Vec3 along(std::sin(cp.h), 0, std::cos(cp.h));
                Vec3 a = p.pos + out * 2.8f;
                Vec3 b = a + along * ((r.f() < 0.5f ? -1.0f : 1.0f) * (12 + r.f() * 12));
                p.path = {a, b};
                p.inside = false;
                p.st = Pax::LEAVE;
            }
            break;
        }
        case Pax::LEAVE:
            if (!p.path.empty() && walkW(p.pos, p.h, p.path.front(), 1.25f, dt, p.speed)) p.path.erase(p.path.begin());
            break;
        }
        p.phase = std::fmod(p.phase + p.speed * dt / STRIDE, 1.0f);
    }
    pax_.erase(std::remove_if(pax_.begin(), pax_.end(), [](const Pax& p) { return p.st == Pax::LEAVE && p.path.empty(); }), pax_.end());
    if (spawned_.size() > 4000) spawned_.clear();
    if (alighted_.size() > 4000) alighted_.clear();
}

void TrainPeople::collect(std::vector<PersonDraw>& out, const std::vector<Train*>& trains, Vec3 cam) const {
    for (const Train* t : trains) {
        const RollingStock& rs = t->stock();
        CarPose mid = t->carPose(t->carCount() / 2);
        if (std::hypot(mid.x - cam.x, mid.z - cam.z) > rs.totalLength() / 2 + 180) continue;
        // zittende reizigers (alleen dichtbij)
        for (size_t s = 0; s < rs.seats.size() && s < t->seatLook.size(); s++) {
            if (t->seatLook[s] < 0) continue;
            const SeatInfo& si = rs.seats[s];
            CarPose cp = t->carPose(si.car);
            float fy = si.y >= 0 ? si.y : rs.floorY;
            Vec3 w = poseToWorld(cp, si.lx - std::sin(si.facing) * 0.10f, fy + 0.45f + 0.09f, si.lz - std::cos(si.facing) * 0.10f);
            if (std::hypot(w.x - cam.x, w.z - cam.z) > 34 || std::fabs(w.y - cam.y) > 12) continue;
            PersonDraw d;
            d.look = t->seatLook[s];
            d.pos = w;
            d.h = cp.h + si.facing;
            d.sitting = true;
            d.interior = true;
            d.anim.kind = (s % 5 == 2) ? ANIM_SIT_PHONE : ANIM_SIT;
            d.anim.t = (float)s;
            if (d.anim.kind == ANIM_SIT_PHONE) d.prop = 0;
            out.push_back(d);
        }
        // machinist in de voorste cabine
        if (!t->driverIn() && rs.cab.deskZ > 0) {
            int lc = t->leadCar();
            int cs = rs.cabSign(lc);
            CarPose cp = t->carPose(lc);
            float x = rs.cab.seatX, z = rs.cab.seatZ - 0.08f;
            Vec3 w = poseToWorld(cp, cs < 0 ? -x : x, rs.cab.seatTop + 0.09f, cs < 0 ? -z : z);
            if (std::hypot(w.x - cam.x, w.z - cam.z) < 200) {
                PersonDraw d;
                d.look = LOOK_DRIVER - (int)((uint32_t)t->service().id % People::DRIVERS);
                d.pos = w;
                d.h = cp.h + (cs < 0 ? PI : 0.0f);
                d.sitting = true;
                d.interior = true;
                d.anim.kind = ANIM_DRIVE;
                out.push_back(d);
            }
        }
        // conducteur
        auto it = cond_.find(t);
        if (it != cond_.end()) {
            const Conductor& c = it->second;
            PersonDraw d;
            d.look = c.look;
            if (c.mode == 2) {
                d.pos = c.pos;
                d.h = c.h;
                d.anim.kind = c.whistleT > 0 ? ANIM_WHISTLE : ANIM_IDLE;
                d.anim.variant = 1;
                if (c.whistleT > 0) d.prop = 1;
                d.anim.headYaw = 0.3f * std::sin(c.t0 * 0.8f);
            } else {
                CarPose cp = t->carPose(c.car);
                d.pos = poseToWorld(cp, c.lx, c.ly, c.lz);
                d.h = cp.h + c.lh;
                d.interior = std::fabs(c.lx) < t->halfWidth();
                if (c.speed > 0.05f) {
                    d.anim.kind = ANIM_WALK;
                    d.anim.speed = c.speed;
                    d.anim.phase = c.phase;
                } else if (c.checkT > 0 || c.atPlayer) {
                    d.anim.kind = ANIM_CHECK;
                    d.prop = 2;
                } else {
                    d.anim.kind = ANIM_IDLE;
                    d.anim.variant = 1;
                }
            }
            d.anim.t = (float)t->service().id;
            if (std::hypot(d.pos.x - cam.x, d.pos.z - cam.z) < 120) out.push_back(d);
        }
    }
    // reizigers op het perron en in de deuropening
    for (const Pax& p : pax_) {
        PersonDraw d;
        d.look = p.look;
        d.anim.variant = p.variant;
        d.anim.t = (float)(p.look % 37);
        if (p.inside) {
            CarPose cp = p.t->carPose(p.car);
            d.pos = poseToWorld(cp, p.lx, p.pos.y, p.lz);
            d.h = cp.h + p.lh;
            d.interior = std::fabs(p.lx) < p.t->halfWidth();
        } else {
            d.pos = p.pos;
            d.h = p.h;
        }
        if (std::hypot(d.pos.x - cam.x, d.pos.z - cam.z) > 120) continue;
        if (p.speed > 0.05f) {
            d.anim.kind = ANIM_WALK;
            d.anim.speed = p.speed;
            d.anim.phase = p.phase;
        } else {
            d.anim.kind = p.variant == 2 ? ANIM_PHONE : ANIM_IDLE;
            if (d.anim.kind == ANIM_PHONE) d.prop = 0;
        }
        out.push_back(d);
    }
}
