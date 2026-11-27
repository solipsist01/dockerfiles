#include "train.h"
#include "tslayout.h"
#include "tsmodel.h"
#include <algorithm>
#include <cstdio>

using namespace WorldFormat;

int Train::defaultRealism = 1;

void Train::start(const Service* s, const Collision& col) {
    svc_ = s;
    rs_ = s->rs;
    float total = rs_->totalLength();
    halfLen_ = total / 2;
    off_.assign(rs_->cars, 0);
    float pos = total / 2;
    for (int i = 0; i < rs_->cars; i++) {
        off_[i] = pos - rs_->len[i] / 2;
        pos -= rs_->len[i] + rs_->gap;
    }
    d_ = halfLen_ + 1.0f;
    v_ = s->entrySpeed;
    state_ = RUN;
    at_ = -1;
    doorOpen_ = 0;
    flipped_ = false;
    if (s->startAtStop) {
        // staat al klaar bij de eerste halte, deuren open
        d_ = s->stops[0].d;
        v_ = 0;
        at_ = 0;
        state_ = DWELL;
        dwellElapsed_ = 10;
        doorOpen_ = 1;
        opened_ = aired_ = true;
        whistled_ = false;
        computeDoorSide(col);
    }
    snd_ = TrainSoundState();
    snd_.id = s->id;
    snd_.doors = doorOpen_;
    snd_.soundSet = rs_->soundSet.empty() ? nullptr : rs_->soundSet.c_str();
    gang_.assign(std::max(0, rs_->cars - 1), 0.0f);  // tussendeuren dicht
    gangTarget_ = gang_;
    gangTimer_ = gang_;
    seatLook.assign(rs_->seats.size(), -1);
    seatsFilled = false;
    if (!s->startAtStop) nextTarget();
}

void Train::nextTarget() {
    target_ = -1;
    for (int k = at_ + 1; k < (int)svc_->stops.size(); k++)
        if (svc_->stops[k].stop) { target_ = k; break; }
}

CarPose Train::carPose(int i) const {
    float d = d_ + (flipped_ ? -off_[i] : off_[i]);
    Vec3 p = path().pos(d);
    return {p.x, p.y, p.z, wrapAngle(path().heading(d) + (flipped_ ? PI : 0.0f))};
}

CarPose Train::poseAtStop(int stopIdx, int car) const {
    float d = svc_->stops[stopIdx].d + (flipped_ ? -off_[car] : off_[car]);
    Vec3 p = path().pos(d);
    return {p.x, p.y, p.z, wrapAngle(path().heading(d) + (flipped_ ? PI : 0.0f))};
}

int Train::sideAtStop(int stopIdx, const Collision& col) const {
    CarPose c = poseAtStop(stopIdx, std::min(1, rs_->cars - 1));
    float ch = std::cos(c.h), sh = std::sin(c.h);
    for (int side = -1; side <= 1; side += 2) {
        float x = c.x + ch * side * (rs_->halfW + 0.8f), z = c.z - sh * side * (rs_->halfW + 0.8f);
        float f = col.floorAt(x, z, c.y + rs_->floorY + 0.4f);
        if (f > c.y + rs_->floorY - 0.4f) return side;
    }
    return 1;
}

void Train::computeDoorSide(const Collision& col) {
    CarPose c = carPose(std::min(1, rs_->cars - 1));
    float ch = std::cos(c.h), sh = std::sin(c.h);
    for (int side = -1; side <= 1; side += 2) {
        float x = c.x + ch * side * (rs_->halfW + 0.8f), z = c.z - sh * side * (rs_->halfW + 0.8f);
        float f = col.floorAt(x, z, c.y + 0.8f + 0.4f);
        if (f > c.y + 0.8f - 0.4f) { doorSide_ = side; return; }
    }
}

std::string Train::nextStop() const {
    if (state_ == DWELL && at_ >= 0) return svc_->stops[at_].station;
    return target_ >= 0 ? svc_->stops[target_].station : "";
}
std::string Train::currentStation() const { return state_ == DWELL && at_ >= 0 ? svc_->stops[at_].station : ""; }
int Train::currentTrack() const { return state_ == DWELL && at_ >= 0 ? svc_->stops[at_].track : 0; }
double Train::departureTime() const { return state_ == DWELL && at_ >= 0 ? svc_->stops[at_].dep : -1; }
double Train::dwellLeft() const {
    if (state_ != DWELL || at_ < 0) return 1e9;
    double t = svc_->stops[at_].dep - clock_;
    if (manual_) t = std::max(t, (double)(18.0f - dwellElapsed_ + 6.0f));
    return t;
}

void Train::cabMsg(const std::string& s, bool penalty) {
    cabMsg_ = s;
    cabMsgT_ = 6;
    if (duty) duty->note(s, penalty);
}

// ---------- stilstand bij een halte (computer en speler) ----------
void Train::dwellStep(float dt, double clock, float authority, const Collision& col) {
    dwellElapsed_ += dt;
    float timer = (float)(svc_->stops[at_].dep - clock);
    // zelf rijden: minstens ~18 s deuren open, ook als je te laat bent
    float closeIn = manual_ ? std::max(timer - 6.0f, 18.0f - dwellElapsed_) : timer - 6.0f;
    effort_ = 0;
    if (!aired_ && dwellElapsed_ > 0.8f) { aired_ = true; snd_.airEvents++; }
    if (!opened_ && dwellElapsed_ > 2.0f) { opened_ = true; snd_.doorOpenEvents++; }
    if (!whistled_ && closeIn < 4.0f && opened_) { whistled_ = true; snd_.whistleEvents++; }  // conducteur fluit
    snd_.beeping = closeIn < 2.5f && doorOpen_ > 0.02f;  // deursluitsignaal
    bool wantOpen = opened_ && closeIn > 0;
    float before = doorOpen_;
    doorOpen_ = clampf(doorOpen_ + (wantOpen ? dt / 2.5f : -dt / 2.8f), 0, 1);
    if (before > 0 && doorOpen_ <= 0) snd_.doorClosedEvents++;
    snd_.doorMove = (doorOpen_ > 0 && doorOpen_ < 1) ? 1.0f : 0.0f;
    snd_.doors = doorOpen_;
    snd_.effort = 0;
    bool lastStop = at_ == (int)svc_->stops.size() - 1;
    if (manual_) {
        v_ = 0;
        if (lastStop) {
            // eindpunt van de dienst: klaar; de computer neemt het weer over (kopmaken / opstellen)
            if (dutyMode_ && duty && !duty->finished && dwellElapsed_ > 3) {
                duty->finished = true;
                char buf[200];
                snprintf(buf, sizeof buf, "Dienst voltooid! %d stations bediend, %d op tijd, %d te laat, %d te vroeg, %d overgeslagen, %d strafpunten.",
                         duty->served, duty->onTime, duty->late, duty->early, duty->missed, duty->penalties);
                duty->note(buf);
            }
            if (dwellElapsed_ > 20) setManual(false, realism_, false);
            return;
        }
        if (doorOpen_ <= 0 && opened_ && closeIn <= 0) {
            state_ = RUN;
            departReady_ = true;
            departPending_ = true;
            nextTarget();
            cabMsg("Vertrekgereed: de conducteur heeft de deuren gesloten", false);
        }
        return;
    }
    // eindpunt met vervolgrit: deuren blijven open tot vlak voor vertrek van de vervolgrit, dan kopmaken
    if (lastStop && svc_->next) {
        const Service* nx = svc_->next;
        timer = (float)(nx->stops[0].dep - clock);
        if (timer < 60.0f) {
            Vec3 c = path().pos(d_);
            svc_ = nx;
            flipped_ = !flipped_;
            d_ = path().closest(c.x, c.z);
            at_ = 0;
            snd_.id = nx->id;
            computeDoorSide(col);
        }
        return;
    }
    // eindpunt zonder vervolgrit (bijv. Sprinter die in Breukelen keert via een opstelspoor): na een paar minuten
    // rijdt de trein naar het opstelterrein (verdwijnt), zodat hij het perronspoor niet blokkeert
    if (lastStop && !svc_->next && svc_->path->length - svc_->stops[at_].d < 600.0f) {
        if (dwellElapsed_ > 240.0f) parked_ = true;
        return;
    }
    signalHold_ = timer <= 0 && authority < front() + 25.0f;
    if (timer <= 0 && doorOpen_ <= 0 && !signalHold_) {
        maxDelay_ = std::max(maxDelay_, (float)(clock - svc_->stops[at_].dep));
        state_ = RUN;
        nextTarget();
    }
}

void Train::update(float dt, double clock, float authority, const Collision& col) {
    clock_ = clock;
    // tussendeuren: openen/sluiten (1 s), na 8 s vanzelf dicht als er niemand in de opening staat
    for (size_t g = 0; g < gang_.size(); g++) {
        gangTimer_[g] -= dt;
        if (gangTimer_[g] <= 0) gangTarget_[g] = 0;
        gang_[g] = clampf(gang_[g] + (gangTarget_[g] > 0.5f ? dt : -dt) / 1.0f, 0, 1);
    }
    for (int e = 0; e < 2; e++) cabDoor_[e] = clampf(cabDoor_[e] + (cabDoorTarget_[e] > 0.5f ? dt : -dt) / 0.9f, 0, 1);
    cabMsgT_ = std::max(0.0f, cabMsgT_ - dt);
    snd_.speed = v_;
    if (state_ == DWELL) {
        dwellStep(dt, clock, authority, col);
        return;
    }
    if (manual_) {
        updateManual(dt, clock, authority, col);
        return;
    }
    const RollingStock& rs = *rs_;
    snd_.beeping = false;
    snd_.doorMove = 0;
    snd_.doors = doorOpen_;
    // doorkomsten bijhouden
    while (at_ + 1 < (int)svc_->stops.size() && !svc_->stops[at_ + 1].stop && svc_->stops[at_ + 1].d <= d_) {
        at_++;
        maxDelay_ = std::max(maxDelay_, (float)(clock - svc_->stops[at_].dep));
    }
    float stopRem = target_ >= 0 ? svc_->stops[target_].d - d_ : 1e9f;
    float sigRem = authority - front();
    float rem = std::min(stopRem, sigRem);
    signalHold_ = sigRem < stopRem;
    float lim = std::min(std::min(rs.vmax, svc_->lineMax), std::sqrt(2 * rs.dec * std::max(0.0f, rem)));
    // kruissnelheid volgens de dienstregeling (zo rijden dat de trein op tijd is, niet te vroeg)
    {
        int leg = target_ >= 0 ? target_ : (int)svc_->stops.size() - 1;
        for (int k = at_ + 1; k <= leg && k < (int)svc_->stops.size(); k++) { leg = k; if (svc_->stops[k].stop) break; }
        if (leg >= 0 && leg < (int)svc_->stops.size() && at_ < (int)svc_->stops.size() - 1) lim = std::min(lim, svc_->stops[leg].vcruise);
    }
    // snelheidsbeperkingen (bijv. emplacement Eindhoven): voor de zone afremmen, in de zone (hele trein) blijven
    for (const SpeedZone& z : svc_->zones) {
        if (front() >= z.d0 && rear() <= z.d1) lim = std::min(lim, z.v);
        else if (z.d0 > front()) lim = std::min(lim, std::sqrt(z.v * z.v + 2 * rs.dec * (z.d0 - front())));
    }
    float v0 = v_;
    v_ = v_ < lim ? std::min(v_ + rs.acc * dt, lim) : std::max(lim, v_ - rs.dec * 1.6f * dt);
    float a = (v_ - v0) / std::max(dt, 1e-4f);
    effort_ = effort_ + (clampf(a > 0 ? a / rs.acc : a / rs.dec, -1, 1) - effort_) * std::min(1.0f, dt * 3);
    snd_.effort = effort_;
    float step = std::min(std::max(v_, rem > 0.5f ? 0.3f : 0.0f) * dt, std::max(0.0f, rem));
    d_ += step;
    if (target_ >= 0 && stopRem - step < 0.02f) {
        d_ = svc_->stops[target_].d;
        v_ = 0;
        at_ = target_;
        state_ = DWELL;
        dwellElapsed_ = 0;
        opened_ = whistled_ = aired_ = false;
        computeDoorSide(col);
    }
}

// ---------- zelf rijden ----------
void Train::setManual(bool on, int realism, bool dutyMode) {
    if (on && !manual_) {
        notch_ = state_ == DWELL ? -3 : 0;
        emergency_ = atbBell_ = atbAck_ = vigBell_ = false;
        atbT_ = vigT_ = 0;
        atbCode_ = 140;
        departReady_ = state_ != DWELL;
        departPending_ = false;
        cabActive_ = pantoUp_ = realism < 2 || v_ > 0.5f;
        readyT_ = cabActive_ ? 10.0f : 0.0f;
    }
    manual_ = on;
    realism_ = realism;
    dutyMode_ = on && dutyMode;
    if (!on) { driverIn_ = false; duty = nullptr; }
}

void Train::setDriver(bool on) {
    if (on) {
        if (!manual_) setManual(true, defaultRealism, false);
        driverIn_ = true;
    } else {
        driverIn_ = false;
        // vrij spelen: de machinist van de computer neemt het weer over; dienst: de trein blijft staan (remt)
        if (!dutyMode_) setManual(false, realism_, false);
    }
    ctl_ = DriveControls();
}

void Train::drive(const DriveControls& c) {
    // gebeurtenissen verzamelen tot de volgende update
    ctl_.throttle = c.throttle;
    ctl_.brake = c.brake;
    ctl_.notch += c.notch;
    ctl_.doorRelease |= c.doorRelease;
    ctl_.doorLeft |= c.doorLeft;
    ctl_.doorRight |= c.doorRight;
    ctl_.ack |= c.ack;
    ctl_.emergency |= c.emergency;
    ctl_.activate |= c.activate;
    ctl_.panto |= c.panto;
}

bool Train::driverEye(float& lx, float& ly, float& lz) const {
    const CabInfo& c = rs_->cab;
    if (c.deskZ <= 0) return false;
    int car = leadCar();
    float x = c.seatX, z = c.seatZ + 0.05f;
    lx = rs_->cabSign(car) < 0 ? -x : x;
    lz = rs_->cabSign(car) < 0 ? -z : z;
    ly = c.seatTop + 0.80f;
    return true;
}

void Train::driverStand(float& lx, float& lz) const {
    const CabInfo& c = rs_->cab;
    int car = leadCar();
    float x = c.seatX - 0.45f, z = c.seatZ - 0.35f;
    lx = rs_->cabSign(car) < 0 ? -x : x;
    lz = rs_->cabSign(car) < 0 ? -z : z;
}

Train::CabState Train::cabState() const {
    CabState s;
    s.vPerm = vPerm_;
    s.vLine = vLine_;
    s.distAuth = distAuth_;
    s.distStop = target_ >= 0 ? svc_->stops[target_].d - d_ : 1e9f;
    s.notch = notch_;
    s.code = atbCode_;
    s.realism = realism_;
    s.bell = atbBell_;
    s.vigilance = vigBell_;
    s.emergency = emergency_;
    s.ready = readyT_ > 5;
    s.cabActive = cabActive_;
    s.panto = pantoUp_;
    s.departReady = departReady_;
    s.msg = cabMsgT_ > 0 ? cabMsg_ : "";
    return s;
}

void Train::updateManual(float dt, double clock, float authority, const Collision& col) {
    const RollingStock& rs = *rs_;
    DriveControls c = driverIn_ ? ctl_ : DriveControls();
    ctl_.notch = 0;
    ctl_.doorRelease = ctl_.doorLeft = ctl_.doorRight = ctl_.ack = ctl_.emergency = ctl_.activate = ctl_.panto = false;
    snd_.beeping = false;
    snd_.doorMove = 0;
    snd_.doors = doorOpen_;
    // ---- cabine in bedrijf nemen (zo echt mogelijk) ----
    if (realism_ >= 2) {
        if (c.activate) { cabActive_ = !cabActive_; cabMsg(cabActive_ ? "Cabine in dienst genomen" : "Cabine buiten dienst", false); }
        if (c.panto) { pantoUp_ = !pantoUp_; cabMsg(pantoUp_ ? "Stroomafnemer omhoog" : "Stroomafnemer omlaag", false); }
        readyT_ = cabActive_ && pantoUp_ ? readyT_ + dt : 0.0f;
    } else {
        cabActive_ = pantoUp_ = true;
        readyT_ = 10;
    }
    bool ready = readyT_ > 5;
    // ---- rijremschakelaar ----
    float trac = 0, brk = 0;
    if (realism_ == 0) {
        trac = c.throttle;
        brk = c.brake;
        notch_ = c.throttle > 0 ? 4 : c.brake > 0 ? -6 : 0;
        if (!driverIn_) { trac = 0; brk = 1; notch_ = -6; }
    } else {
        notch_ = std::max(-7, std::min(4, notch_ + c.notch));
        if (!driverIn_) notch_ = std::min(notch_, -4);
        trac = notch_ > 0 ? notch_ / 4.0f : 0.0f;
        brk = notch_ < 0 ? std::min(1.0f, -notch_ / 6.0f) : 0.0f;
        if (notch_ == -7) brk = 1.3f;
    }
    if (c.emergency) { emergency_ = true; cabMsg("Noodrem bediend", false); }
    // ---- snelheden: baanvak, beperkingen vooruit, einde rijtoestemming ----
    float vLine = std::min(rs.vmax, svc_->lineMax);
    for (const SpeedZone& z : svc_->zones)
        if (front() >= z.d0 && rear() <= z.d1) vLine = std::min(vLine, z.v);
    float vZone = vLine;
    for (const SpeedZone& z : svc_->zones)
        if (z.d0 > front()) vZone = std::min(vZone, std::sqrt(z.v * z.v + 2 * 0.7f * (z.d0 - front())));
    float rem = authority - front();
    float vAuth = std::sqrt(2 * 0.7f * std::max(0.0f, rem - 10));
    vLine_ = vLine;
    vPerm_ = std::min(vZone, vAuth);
    distAuth_ = rem;
    signalHold_ = rem < 60 && v_ < 0.1f;
    // ---- ATB ----
    if (realism_ >= 1) {
        float vSup = vPerm_;
        if (realism_ == 2) {
            // ATB-EG: alleen snelheidscodes 40/60/80/130/140; bij een lagere code bel tot bevestiging
            static const int codes[] = {40, 60, 80, 130, 140};
            int code = 40;
            for (int k : codes) if (k / 3.6f <= vPerm_ + 0.1f) code = k;
            if (code < atbCode_) { atbBell_ = true; atbT_ = 0; atbAck_ = false; }
            atbCode_ = code;
            vSup = code / 3.6f;
        } else {
            atbCode_ = (int)std::lround(vPerm_ * 3.6f);
        }
        if (v_ > vSup + 1.4f && !atbBell_) { atbBell_ = true; atbT_ = 0; atbAck_ = false; }
        if (atbBell_) {
            atbT_ += dt;
            if (c.ack) atbAck_ = true;
            bool braking = brk >= 0.33f;
            bool fine = v_ <= vSup + 0.3f && (realism_ < 2 || atbAck_);
            if (fine) atbBell_ = false;
            else if (atbT_ > 2.5f && !braking && !(realism_ == 2 && atbAck_ && v_ <= vSup + 1.4f)) {
                emergency_ = true;
                atbBell_ = false;
                cabMsg("ATB-ingreep: snelremming (te hard en niet op tijd geremd)", true);
            }
        }
        // waakzaamheid (zo echt mogelijk): elke minuut bevestigen met spatie
        if (realism_ == 2 && v_ > 1.0f) {
            vigT_ += dt;
            if (c.notch != 0) vigT_ = std::min(vigT_, 30.0f);
            if (vigT_ > 55) vigBell_ = true;
            if (vigBell_ && c.ack) { vigBell_ = false; vigT_ = 0; }
            if (vigT_ > 62) { emergency_ = true; vigBell_ = false; vigT_ = 0; cabMsg("Waakzaamheid niet bevestigd: snelremming", true); }
        }
    } else if (v_ > vLine + 1.5f && cabMsgT_ <= 0) {
        char b[96];
        snprintf(b, sizeof b, "Te hard! Hier mag je %.0f km/u", vLine * 3.6f);
        cabMsg(b, false);
    }
    // ---- harde beveiliging: nooit voorbij het einde van de rijtoestemming ----
    if (!emergency_ && v_ > std::sqrt(2 * 1.15f * std::max(0.0f, rem - 3)) + 0.3f) {
        emergency_ = true;
        cabMsg(realism_ == 0 ? "Automatische remming: rood sein" : "Snelremming: rood sein (bijna) gepasseerd", realism_ > 0);
    }
    if (emergency_) {
        trac = 0;
        brk = 1.35f;
        if (v_ < 0.05f && (realism_ == 0 || notch_ < 0)) emergency_ = false;
    }
    // tractie alleen met gesloten deuren en een cabine in dienst
    if (trac > 0 && !ready) { if (cabMsgT_ <= 0) cabMsg(realism_ >= 2 ? "Geen tractie: cabine activeren (K) en stroomafnemer omhoog (P)" : "Geen tractie", false); trac = 0; }
    // ---- rijden ----
    float a;
    if (trac > 0) a = trac * rs.acc * std::max(0.25f, 1.0f - v_ / (rs.vmax * 1.1f)) - 0.012f - 0.00015f * v_ * v_;
    else a = -brk * rs.dec * 1.25f - 0.012f - 0.00015f * v_ * v_;
    float v0 = v_;
    v_ = std::max(0.0f, v_ + a * dt);
    float step = v_ * dt;
    if (step > std::max(0.0f, rem)) { step = std::max(0.0f, rem); v_ = 0; }
    d_ += step;
    float aa = (v_ - v0) / std::max(dt, 1e-4f);
    effort_ = effort_ + (clampf(aa > 0 ? aa / rs.acc : aa / rs.dec, -1, 1) - effort_) * std::min(1.0f, dt * 3);
    snd_.effort = effort_;
    if (v_ > 0.5f) departReady_ = false;
    if (departPending_ && v_ > 0.5f && at_ >= 0) {
        departPending_ = false;
        double dl = clock - svc_->stops[at_].dep;
        if (dl < -30) cabMsg("Te vroeg vertrokken uit " + svc_->stops[at_].station, true);
        if (duty) duty->departure(svc_->stops[at_].station, dl);
    }
    // doorkomsten (stations waar deze trein niet stopt)
    while (at_ + 1 < (int)svc_->stops.size() && !svc_->stops[at_ + 1].stop && svc_->stops[at_ + 1].d <= d_) {
        at_++;
        if (duty) duty->departure(svc_->stops[at_].station + " (doorkomst)", clock - svc_->stops[at_].dep);
    }
    // halte voorbij gereden
    if (target_ >= 0 && d_ > svc_->stops[target_].d + 30) {
        cabMsg("Station " + svc_->stops[target_].station + " overgeslagen!", true);
        if (duty) duty->missed++;
        at_ = target_;
        nextTarget();
    }
    // ---- stoppen langs het perron: deuren vrijgeven ----
    if (target_ >= 0 && v_ < 0.05f) {
        float err = d_ - svc_->stops[target_].d;
        bool atPlatform = std::fabs(err) < 18.0f;
        bool asked = realism_ == 0 ? atPlatform : realism_ == 1 ? c.doorRelease : (c.doorLeft || c.doorRight || c.doorRelease);
        if (asked && !atPlatform && realism_ > 0) {
            char b[120];
            if (std::fabs(err) < 300) snprintf(b, sizeof b, "Niet langs het perron: %.0f m %s", std::fabs(err), err < 0 ? "te vroeg gestopt" : "te ver doorgereden");
            else snprintf(b, sizeof b, "Hier is geen perron");
            cabMsg(b, false);
        }
        if (asked && atPlatform) {
            computeDoorSide(col);
            if (realism_ == 2 && !c.doorLeft && !c.doorRight) {
                cabMsg("Kies de kant: Q = deuren links, R = deuren rechts", false);
            } else if (realism_ == 2 && ((c.doorLeft && doorSide_ != 1) || (c.doorRight && doorSide_ != -1))) {
                cabMsg("Deuren aan de verkeerde kant vrijgegeven! (geen perron)", true);
            } else {
                at_ = target_;
                state_ = DWELL;
                dwellElapsed_ = 0;
                opened_ = whistled_ = aired_ = false;
                departReady_ = false;
                double dl = clock - svc_->stops[at_].arr;
                char b[160];
                snprintf(b, sizeof b, "Aangekomen in %s (spoor %d)%s", svc_->stops[at_].station.c_str(), svc_->stops[at_].track,
                         std::fabs(err) > 6 ? (std::string(", ") + std::to_string((int)std::fabs(err)) + " m " + (err < 0 ? "te kort" : "te ver")).c_str() : "");
                cabMsg(b, false);
                (void)dl;
            }
        }
    }
}

// ---------- botsing ----------
void Train::addCollision(Collision& col) const {
    const RollingStock& rs = *rs_;
    for (int i = 0; i < rs.cars; i++) {
        CarPose c = carPose(i);
        float ax = std::sin(c.h), az = std::cos(c.h);  // as a = lokale +z
        float L = rs.len[i];
        float y0 = c.y + 0.3f, y1 = c.y + rs.height;
        auto box = [&](float lx, float lz, float hx, float hz) {
            Vec3 p = carToWorld(i, lx, 0, lz);
            col.addDynamicBox({p.x, p.z, ax, az, hz, hx, y0, y1});
        };
        float HW = rs.halfW;
        for (int side = -1; side <= 1; side += 2) {
            bool open = side == doorSide_ && doorOpen_ > 0.8f;
            std::vector<float> cuts{-L / 2, L / 2};
            if (open)
                for (float dz : rs.doorZ[i]) { cuts.push_back(dz - rs.doorW / 2 + 0.1f); cuts.push_back(dz + rs.doorW / 2 - 0.1f); }
            std::sort(cuts.begin(), cuts.end());
            for (size_t k = 0; k + 1 < cuts.size(); k++) {
                float a = cuts[k], b = cuts[k + 1], m = (a + b) / 2;
                bool gap = false;
                if (open)
                    for (float dz : rs.doorZ[i]) if (std::fabs(m - dz) < rs.doorW / 2) gap = true;
                if (!gap && b > a) box(side * (HW - 0.06f), m, 0.08f, (b - a) / 2);
            }
        }
        int cs = rs.cabSign(i);
        if (cs) box(0, cs * (L / 2 - rs.nose / 2), HW, rs.nose / 2 + 0.3f);  // neus
        {
            Vec3 p = carToWorld(i, 0, 0, 0);
            col.addDynamicFloor({p.x, p.z, ax, az, L / 2 - (cs ? rs.nose : 0.0f), HW + 0.35f, c.y + rs.floorY, c.y + rs.floorY, 0});
        }
        if (i + 1 < rs.cars)
            for (int side = -1; side <= 1; side += 2) box(side * (HW - 0.06f), -L / 2 - rs.gap / 2, 0.1f, rs.gap / 2 + 0.05f);
    }
}

// Model uit Train Simulator: buiten de trein waar de speler in zit en binnen 900 m; anders het eigen model
bool Train::drawTs(World& world, int pass) const {
    if (rs_->tsModel.empty() || playerInside) return false;
    Vec3 cam = world.camPos();
    CarPose c0 = carPose(0);
    if (std::hypot(c0.x - cam.x, c0.z - cam.z) > 900.0f) return false;
    TsModel* tm = tsModel(rs_->tsModel);
    if (!tm->ready() || tm->cars() != rs_->cars) return false;
    TsModel::DrawState st;
    st.wheel = d_ / 0.43f;
    for (int i = 0; i < rs_->cars; i++) {
        CarPose c = carPose(i);
        int cs = rs_->cabSign(i);
        Vec3 p(c.x, c.y, c.z);
        float r = rs_->len[i] * 0.5f + 3.0f;
        if (!world.visible(p - Vec3(r, r, r), p + Vec3(r, r, r))) continue;
        // deuren: zijde in het model (+x / -x) -> wereldzijde zoals bij de eigen modellen
        for (int side = 0; side < 2; side++) {
            int meshSide = side == 0 ? 1 : -1;
            int worldSide = cs < 0 ? -meshSide : meshSide;
            st.doorOpen[side] = worldSide == doorSide_ ? doorOpen_ : 0.0f;
        }
        bool leading = cs != 0 && (cs > 0) == !flipped_;
        st.head = cs != 0 && leading;
        st.tail = cs != 0 && !leading;
        Mat4 m = Mat4::pose(c.x, c.y, c.z, cs < 0 ? c.h + PI : c.h);
        tm->drawCar(world, i, m, st, pass);
        // bestemmingsborden (tekstatlas, wereldshader)
        const TsLayout* lay = tsLayout(rs_->tsModel.c_str());
        int dest = svc_->destIdx;
        if (pass == 0 && lay && i < lay->cars && dest >= 0 && dest < (int)rs_->tsSignFront.size() &&
            std::hypot(c.x - cam.x, c.z - cam.z) < 250.0f) {
            world.setMode(MODE_OBJECT);
            for (int k = 0; k < lay->car[i].signs; k++) {
                const TsSign& sg = lay->car[i].sign[k];
                if (sg.kind == 0 && !leading) continue;  // achterkant: geen bestemming
                world.setModel(m * Mat4::translation(sg.x + sg.nx * 0.035f, sg.y, sg.z + sg.nz * 0.035f));
                drawMesh(sg.kind == 0 ? rs_->tsSignFront[dest] : rs_->tsSignSide[dest]);
            }
            world.setMode(MODE_VEHICLE);
        }
    }
    return true;
}

void Train::drawOpaque(World& world) const {
    if (drawTs(world, 0)) return;
    int next = -1;
    if (state_ == DWELL) {
        for (int k = at_ + 1; k < (int)svc_->stops.size(); k++)
            if (svc_->stops[k].stop) { next = svc_->stopIdx[k]; break; }
    } else if (target_ >= 0) {
        next = svc_->stopIdx[target_];
    }
    if (next < 0) next = svc_->destIdx;
    StockDraw sd{this, doorOpen_, doorSide_, svc_->destIdx, richScreens ? -1 : next, flipped_ ? 1 : 0, gang_.empty() ? nullptr : gang_.data()};
    sd.cabDoor[0] = cabDoor_[0];
    sd.cabDoor[1] = cabDoor_[1];
    sd.lever = manual_ ? (notch_ > 0 ? notch_ / 4.0f : notch_ / 7.0f) : 0.0f;
    rs_->drawOpaque(world, sd);
}

void Train::drawGlass(World& world) const {
    if (drawTs(world, 1)) return;
    StockDraw sd{this, doorOpen_, doorSide_, svc_->destIdx, 0, flipped_ ? 1 : 0};
    rs_->drawGlass(world, sd);
}

// ---------- cabine en tussendeuren ----------
// in de cabine-zone (vanaf de cabinewand) van een eindrijtuig? x, z = canonieke coordinaten
bool Train::cabCanon(int car, float lx, float lz, float& x, float& z) const {
    int cs = rs_->cabSign(car);
    if (!cs || rs_->cab.deskZ <= 0) return false;
    x = cs < 0 ? -lx : lx;
    z = cs < 0 ? -lz : lz;
    return z > rs_->cab.wallZ - 0.06f;
}

float Train::floorAt3Cab(int car, float lx, float lz, float curY) const {
    float x, z;
    if (cabCanon(car, lx, lz, x, z) && z > rs_->cab.wallZ + 0.05f) return rs_->cab.floorY;
    return rs_->floorAt3(car, lx, lz, curY);
}

// doorloop i ligt tussen rijtuig i en i+1; rijtuig i+1 zit aan de -z kant van rijtuig i
bool Train::walkable3(int car, float lx, float lz, float curY) const {
    float x, z;
    if (cabCanon(car, lx, lz, x, z)) {
        const CabInfo& c = rs_->cab;
        int end = rs_->cabSign(car) > 0 ? 0 : 1;
        if (z < c.wallZ + 0.10f) return cabDoor_[end] > 0.75f && std::fabs(x - c.doorX) < c.doorW / 2 - 0.12f;  // deuropening
        return z < c.deskZ - 0.30f && std::fabs(x) < 0.95f;  // in de cabine
    }
    if (rs_->gangwayDoors && !gang_.empty()) {
        float L = rs_->len[car];
        if (lz < -(L / 2 - 0.30f) && car < rs_->cars - 1 && gang_[car] < 0.7f) return false;
        if (lz > L / 2 - 0.30f && car > 0 && gang_[car - 1] < 0.7f) return false;
    }
    return rs_->walkable3(car, lx, lz, curY);
}

void Train::hotspots(std::vector<Hotspot>& out) const {
    if (rs_->gangwayDoors)
        for (int g = 0; g + 1 < rs_->cars; g++) {
            std::string label = gang_[g] > 0.5f ? "E / klik: tussendeur sluiten" : "E / klik: hendel - tussendeur openen";
            float y = rs_->floorY + 1.05f;
            // hendel aan beide kanten van de doorloop
            out.push_back({Hotspot::DOOR, g, 0.0f, y, -(rs_->len[g] / 2 - 0.12f), 0.45f, g, label});
            out.push_back({Hotspot::DOOR, g + 1, 0.0f, y, rs_->len[g + 1] / 2 - 0.12f, 0.45f, g, label});
        }
    const CabInfo& c = rs_->cab;
    if (c.deskZ <= 0) return;
    for (int car : {0, rs_->cars - 1}) {
        int cs = rs_->cabSign(car);
        int end = cs > 0 ? 0 : 1;
        auto loc = [&](float x, float y, float z, float r, int id, const std::string& lab, Hotspot::Kind k) {
            out.push_back({k, car, cs < 0 ? -x : x, y, cs < 0 ? -z : z, r, id, lab});
        };
        std::string lab = cabDoor_[end] > 0.5f ? "E / klik: cabinedeur sluiten" : "E / klik: cabinedeur openen";
        float hx = c.doorX + c.doorW / 2 - 0.12f;
        loc(hx, c.passFloor + 1.05f, c.wallZ - 0.06f, 0.30f, 100 + end, lab, Hotspot::DOOR);
        loc(hx, c.floorY + 1.05f, c.wallZ + 0.06f, 0.30f, 100 + end, lab, Hotspot::DOOR);
        if (car == leadCar() && !driverIn_)
            loc(c.leverX, c.leverY + 0.10f, c.leverZ, 0.32f, 0, "E / klik: plaatsnemen achter de lessenaar - zelf rijden", Hotspot::DRIVE);
    }
}

void Train::activate(const Hotspot& h) {
    if (h.kind != Hotspot::DOOR) return;
    if (h.id == 100 || h.id == 101) {
        int e = h.id - 100;
        cabDoorTarget_[e] = cabDoorTarget_[e] > 0.5f ? 0.0f : 1.0f;
        return;
    }
    if (h.id < 0 || h.id >= (int)gang_.size()) return;
    bool open = gangTarget_[h.id] < 0.5f;
    gangTarget_[h.id] = open ? 1.0f : 0.0f;
    gangTimer_[h.id] = open ? 8.0f : 0.0f;
}

void Train::playerAt(int car, float lx, float lz) {
    (void)lx;
    if (gang_.empty()) return;
    float L = rs_->len[car];
    // wie in de deuropening staat, houdt de deur open
    if (lz < -(L / 2 - 0.8f) && car < rs_->cars - 1 && gang_[car] > 0.5f) gangTimer_[car] = std::max(gangTimer_[car], 2.0f);
    if (lz > L / 2 - 0.8f && car > 0 && gang_[car - 1] > 0.5f) gangTimer_[car - 1] = std::max(gangTimer_[car - 1], 2.0f);
}
