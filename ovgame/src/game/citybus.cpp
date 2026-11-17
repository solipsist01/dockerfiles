#include "citybus.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include "../engine/meshbuilder.h"
#include "announcer.h"

using namespace WorldFormat;

namespace {
const float ACC = 1.0f, DEC = 1.3f, LAT_ACC = 1.3f;
const float BUS_VMAX = 80 / 3.6f;
Vec3 flat(Vec3 v) { return Vec3(v.x, 0, v.z); }
Mat4 rotX(float a) {
    Mat4 r = Mat4::identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
    return r;
}
Mat4 rotZ(float a) {
    Mat4 r = Mat4::identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
    return r;
}
std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
// bestandsnaam van een omroepfragment: kleine letters, alleen a-z0-9 en _
std::string slug(const std::string& s) {
    std::string o;
    for (char c : lower(s)) o += std::isalnum((unsigned char)c) ? c : '_';
    while (o.find("__") != std::string::npos) o.replace(o.find("__"), 2, "_");
    return o;
}
uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
    return x;
}
// uniform Catmull-Rom door de punten (inclusief begin- en eindpunt)
void catmull(const std::vector<Vec3>& c, std::vector<Vec3>& out) {
    for (size_t k = 1; k + 2 < c.size(); k++) {
        const Vec3 &p0 = c[k - 1], &p1 = c[k], &p2 = c[k + 1], &p3 = c[k + 2];
        int n = std::max(2, (int)(length(flat(p2 - p1)) / 2.0f));
        for (int i = 0; i < n; i++) {
            float t = (float)i / n, t2 = t * t, t3 = t2 * t;
            out.push_back((p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f);
        }
    }
}
}  // namespace

// ============================== CityBus: pad ==============================
const LineDir& CityBus::dir() const { return fleet_->dirs_[fleet_->trips_[trip_].dir]; }
const LineTrip& CityBus::trip() const { return fleet_->trips_[trip_]; }

void CityBus::addPoint(Vec3 p, float lim) {
    if (!pts_.empty() && std::hypot(p.x - pts_.back().x, p.z - pts_.back().z) < 0.05f) return;
    float c = pts_.empty() ? 0 : cum_.back() + std::hypot(p.x - pts_.back().x, p.z - pts_.back().z);
    pts_.push_back(p);
    cum_.push_back(c);
    lim_.push_back(lim);
}

Vec3 CityBus::posAt(float s) const {
    if (pts_.empty()) return Vec3();
    if (s <= 0) {
        if (pts_.size() < 2) return pts_[0];
        Vec3 d = normalize(flat(pts_[1] - pts_[0]));
        return pts_[0] + d * s;
    }
    if (s >= cum_.back()) {
        if (pts_.size() < 2) return pts_.back();
        Vec3 d = normalize(flat(pts_.back() - pts_[pts_.size() - 2]));
        return pts_.back() + d * (s - cum_.back());
    }
    size_t lo = 0, hi = cum_.size() - 1;
    while (hi - lo > 1) {
        size_t m = (lo + hi) / 2;
        if (cum_[m] <= s) lo = m; else hi = m;
    }
    float seg = cum_[hi] - cum_[lo];
    float t = seg > 0 ? (s - cum_[lo]) / seg : 0;
    return pts_[lo] + (pts_[hi] - pts_[lo]) * t;
}

float CityBus::limAt(float s) const {
    if (cum_.empty()) return 5;
    auto it = std::upper_bound(cum_.begin(), cum_.end(), s);
    size_t k = (size_t)(it - cum_.begin());
    k = std::min(k, lim_.size() - 1);
    return lim_[k];
}

float CityBus::stopS(int k) const {
    const BusSection& f = model_->sec[0];
    float doorOff = (f.length / 2 - 0.85f) - f.axleF;  // voordeur t.o.v. de vooras
    return routeOff_ + dir().stops[k].s - doorOff;
}

void CityBus::computePose() {
    const BusSection& f = model_->sec[0];
    float wb = f.axleF - f.axleR;
    Vec3 Pf = posAt(s_), Pr = posAt(s_ - wb);
    Vec3 d = flat(Pf - Pr);
    float l = std::hypot(d.x, d.z);
    float h = l > 0.01f ? std::atan2(d.x, d.z) : pose_[0].h;
    if (l > 0.01f) d = d * (1.0f / l); else d = Vec3(std::sin(h), 0, std::cos(h));
    Vec3 c = Pr + d * (-f.axleR);
    pose_[0] = {c.x, (Pf.y + Pr.y) / 2, c.z, h};
    if (model_->articulated) {
        const BusSection& r = model_->sec[1];
        float jz = -f.length / 2 - model_->jointGap / 2;
        Vec3 J = Pr + d * (jz - f.axleR);
        float jToAxle = r.axleF - r.axleR;
        Vec3 R2 = posAt(s_ - wb - (f.axleR - jz) - jToAxle);
        Vec3 d2 = flat(J - R2);
        float l2 = std::hypot(d2.x, d2.z);
        float h2 = l2 > 0.01f ? std::atan2(d2.x, d2.z) : h;
        if (l2 > 0.01f) d2 = d2 * (1.0f / l2); else d2 = d;
        Vec3 c2 = J - d2 * r.axleF;
        pose_[1] = {c2.x, (Pr.y + R2.y) / 2, c2.z, h2};
    }
}

// Rit beginnen. connect: vanaf de huidige plek over een verbindingsstuk (keerlus op het eindpunt) naar de route.
void CityBus::startTrip(int tripIdx, bool connect, bool nearPlayer) {
    const World& w = *fleet_->world_;
    Vec3 E = posAt(s_), dE(std::sin(pose_[0].h), 0, std::cos(pose_[0].h));
    trip_ = tripIdx;
    const LineDir& D = dir();
    const LineTrip& T = trip();
    pts_.clear(); cum_.clear(); lim_.clear();
    Vec3 S = D.pts[0], dS = normalize(flat(D.pts[1] - D.pts[0]));
    if (connect && nearPlayer) {
        std::vector<Vec3> ctrl, out;
        Vec3 f = dE, l(dE.z, 0, -dE.x);  // links van de rijrichting
        float dist = length(flat(S - E));
        if (dot(dE, dS) < -0.3f && dist < 80) {
            // keerlus: vooruit voorbij het vertrekpunt, ruime bocht naar links en terug
            float ahead = std::max(0.0f, dot(flat(S - E), f));
            Vec3 B = E + f * ahead;
            ctrl = {E - f * 6.0f, E, E + f * (ahead * 0.5f + 6.0f), B + f * 14.0f - l * 2.5f, B + f * 24.0f + l * 5.0f,
                    B + f * 15.0f + l * 12.5f, S - dS * 8.0f, S, S + dS * 6.0f};
        } else {
            float k = std::clamp(dist * 0.4f, 6.0f, 25.0f);
            ctrl = {E - dE * 6.0f, E, E + dE * k, S - dS * k, S, S + dS * 6.0f};
        }
        catmull(ctrl, out);
        for (Vec3 p : out) { p.y = w.heightAt(p.x, p.z); addPoint(p, 4.0f); }
        s_ = 0;
        positioning_ = true;
    } else {
        // zonder verbindingsstuk: een stuk recht ervoor, zodat de achteras ook een pad heeft
        addPoint(S - dS * 40.0f, D.lim[0]);
        positioning_ = connect;  // ver van de speler: meteen op het vertrekperron
    }
    routeOff_ = pts_.empty() ? 0 : cum_.back() + std::hypot(S.x - pts_.back().x, S.z - pts_.back().z);
    for (size_t k = 0; k < D.pts.size(); k++) addPoint(D.pts[k], D.lim[k]);
    if (!connect || !nearPlayer) s_ = stopS(T.first);
    next_ = T.first;
    atStop_ = -1;
    seatTaken_.resize(model_->seats.size(), 0);
    boardQ_ = boardServed_ = 0;
    stopReq_ = false;
    announced_ = -2;
    welcomed_ = false;
    pace_ = 1;
    paceTimer_ = 0;
    if (!connect || !nearPlayer) {
        // klaar voor vertrek op de eerste halte
        state_ = LAYOVER;
        atStop_ = T.first;
        depart_ = T.plan[T.first];
        v_ = 0;
        positioning_ = false;
    } else {
        state_ = DRIVE;
    }
    computePose();
}

// Bij het opstarten: de bus staat waar hij volgens de dienstregeling nu is
void CityBus::placeBySchedule(double clock) {
    const LineTrip& T = trip();
    for (int k = T.first; k < T.last; k++) {
        int k2 = k + 1;
        while (k2 < T.last && T.plan[k2] < 0) k2++;
        if (T.plan[k] < 0 || clock < T.plan[k] || clock >= T.plan[k2]) continue;
        double f = (clock - T.plan[k]) / std::max(1.0, T.plan[k2] - T.plan[k]);
        float a = stopS(k), b = stopS(k2);
        s_ = a + (b - a) * (float)f;
        v_ = std::min(limAt(s_) * 0.8f, (b - a) / (float)std::max(1.0, T.plan[k2] - T.plan[k]) * 1.4f);
        next_ = k2;
        announced_ = k2;
        welcomed_ = true;
        state_ = DRIVE;
        atStop_ = -1;
        door_ = 0;
        computePose();
        return;
    }
}

bool CityBus::inDoorway(int car, float lz, int side) const {
    (void)side;
    for (float d : model_->sec[car].doorZ)
        if (std::fabs(lz - d) < BUS_DOOR_W / 2 - 0.15f) return true;
    return false;
}

bool CityBus::walkable(int car, float lx, float lz) const {
    const BusSection& s = model_->sec[car];
    float L = s.length;
    bool articulated = model_->articulated;
    if (articulated && ((car == 0 && lz < -L / 2 + 0.3f) || (car == 1 && lz > L / 2 - 0.3f))) return std::fabs(lx) < 0.6f;
    if (lz > L / 2 - 0.4f || lz < -L / 2 + 0.4f) return false;
    for (float d : s.doorZ)
        if (std::fabs(lz - d) < 0.8f) return lx > -BUS_HW + 0.25f && lx < 0.5f;  // deurportaal
    if (car == 0 && lz > L / 2 - 2.6f) return lx > -BUS_HW + 0.3f && lx < 0.15f;  // instap voor, naast de cabine
    return std::fabs(lx) < 0.38f;  // gangpad
}

// ============================== CityBus: informatie ==============================
std::string CityBus::nextStop() const {
    if (!inService()) return "";
    const LineTrip& T = trip();
    int k = (state_ == STOP || state_ == LAYOVER) && atStop_ >= 0 ? atStop_ + 1 : next_;
    if (positioning_) k = T.first;
    if (k > T.last) return "";
    return dir().stops[k].name;
}

std::string CityBus::currentStop() const {
    if (!inService() || (state_ != STOP && state_ != LAYOVER) || atStop_ < 0) return "";
    return dir().stops[atStop_].name;
}

float CityBus::departureIn() const {
    if (!inService() || (state_ != STOP && state_ != LAYOVER)) return -1;
    if (state_ == LAYOVER && atStop_ == trip().last) return -1;  // eindpunt
    return (float)std::max(0.0, depart_ - fleet_->clock_);
}

std::string CityBus::destination() const {
    if (trip_ < 0) return "";
    return trip().dest.empty() ? dir().dest : trip().dest;
}

std::string CityBus::display() const {
    if ((manual_ && !duty_) || state_ == OFFDUTY || trip_ < 0) return "Niet instappen";
    return dir().ref + "  " + destination();
}

std::string CityBus::lineText() const {
    if (!inService()) return "Bus (niet in dienst)";
    return "Lijn " + dir().ref + " naar " + destination();
}

// vertraging: verwachte tijd op de huidige plek t.o.v. de planning
double CityBus::delay(double clock) const {
    if (!inService() || positioning_) return 0;
    const LineTrip& T = trip();
    int startIdx = (state_ == STOP || state_ == LAYOVER) && atStop_ >= 0 ? atStop_ + 1 : next_;
    int k = std::max(T.first, std::min(startIdx, T.last));
    if (k <= T.first || T.plan[k - 1] < 0) return 0;
    if (state_ == STOP || state_ == LAYOVER) return std::max(0.0, clock - T.plan[k - 1]);
    float a = stopS(k - 1), c = stopS(k);
    float f = clampf((s_ - a) / std::max(1.0f, c - a), 0, 1);
    double expect = T.plan[k - 1] + (T.plan[k] - T.plan[k - 1]) * f;
    return std::max(0.0, clock - expect);
}

std::vector<std::string> CityBus::infoLines(double clock) const {
    std::vector<std::string> o;
    auto hm = [](double t) {
        char b[16];
        t = std::fmod(std::max(0.0, t), 86400.0);
        snprintf(b, sizeof b, "%02d:%02d", (int)(t / 3600), (int)std::fmod(t / 60, 60));
        return std::string(b);
    };
    char buf[160];
    const BusLine* L = trip_ >= 0 ? &fleet_->lines_[dir().line] : nullptr;
    if (duty_) {
        const LineTrip& T = trip();
        const LineDir& D = dir();
        o.push_back("Lijn " + D.ref + " naar " + destination());
        o.push_back("Bestuurd door: jou (dienst)");
        int k = dutyAt_ >= 0 ? dutyAt_ : next_;
        if (k <= T.last) o.push_back((dutyAt_ >= 0 ? "Staat bij: " : "Volgende halte: ") + D.stops[k].name);
        double dl = dutyDelay(clock);
        int m = (int)std::lround(dl / 60.0);
        o.push_back(m > 0 ? "Vertraging: +" + std::to_string(m) + " min" : m < 0 ? "Te vroeg: " + std::to_string(-m) + " min" : "Op tijd: ja");
        snprintf(buf, sizeof buf, "%.0f km/u", std::fabs(v_) * 3.6f);
        o.push_back(std::string("Snelheid: ") + buf);
        o.push_back("Reizigers: " + std::to_string(passengers()));
        (void)hm;
    } else if (manual_) {
        o.push_back(L ? "Lijn " + L->ref + " (buiten dienst)" : "Bus (buiten dienst)");
        o.push_back(driverAboard_ ? "Bestuurd door: jou" : "Status: geparkeerd");
    } else if (!inService()) {
        o.push_back("Bus (niet in dienst)");
        o.push_back(state_ == OFFDUTY ? "Status: rijdt naar de stalling" : "Status: wacht op de volgende rit");
    } else {
        const LineTrip& T = trip();
        const LineDir& D = dir();
        o.push_back("Lijn " + D.ref + " naar " + destination());
        std::string cur = currentStop();
        bool terminal = state_ == LAYOVER && atStop_ == T.last;
        if (positioning_) {
            o.push_back("Status: rijdt leeg naar " + D.stops[T.first].name);
            o.push_back("Vertrek: " + hm(T.plan[T.first]));
        } else if (!cur.empty() && terminal) {
            o.push_back("Staat bij: " + cur + " (eindpunt)");
        } else if (!cur.empty()) {
            o.push_back("Staat bij: " + cur);
            o.push_back("Vertrek: " + hm(std::max(depart_, clock)) + " (over " + std::to_string((int)std::ceil(departureIn() / 60.0)) + " min)");
            std::string ns = nextStop();
            if (!ns.empty()) o.push_back("Volgende halte: " + ns);
        } else {
            int k = next_;
            if (k <= T.last) {
                double eta = T.plan[k] + delay(clock);
                int mins = (int)std::ceil(std::max(0.0, eta - clock) / 60.0);
                o.push_back("Volgende halte: " + D.stops[k].name + (mins <= 0 ? " (< 1 min)" : " (" + std::to_string(mins) + " min)"));
            }
        }
        const LineStop& ls = D.stops[T.last];
        o.push_back("Eindbestemming: " + (ls.place.empty() ? ls.name : ls.place + ", " + ls.name));
        snprintf(buf, sizeof buf, "%.0f km/u", std::fabs(v_) * 3.6f);
        o.push_back(std::string("Snelheid: ") + buf);
        double dl = delay(clock);
        o.push_back(dl < 60 ? std::string("Op tijd: ja") : "Vertraging: +" + std::to_string((int)(dl / 60)) + " min");
        o.push_back("Rit: " + D.stops[T.first].name + " " + hm(T.plan[T.first]) + " - " + D.stops[T.last].name + " " + hm(T.plan[T.last]));
        if (stopReq_) o.push_back("Stopknop: halte aangevraagd");
        o.push_back("Reizigers: " + std::to_string(passengers()));
    }
    o.push_back(model_->articulated ? "Bus: VDL Citea SLFA-181 Electric (18 m, geleed)" : "Bus: VDL Citea SLF-120 Electric (12 m)");
    if (L) o.push_back("Vervoerder: " + L->operatorName);
    return o;
}

void CityBus::requestStop() {
    if (!inService() || stopReq_ || positioning_) return;
    if (state_ == LAYOVER && atStop_ == trip().last) return;
    stopReq_ = true;
    if (fleet_->ann_ && fleet_->annBus_ == this) fleet_->ann_->ding();
}

float CityBus::frontDoorZ() const {
    float z = -1e9f;
    for (float d : model_->sec[0].doorZ) z = std::max(z, d);
    return z > -1e8f ? z : model_->sec[0].length / 2 - 0.85f;
}

bool CityBus::servingStop(int k) const {
    if (trip_ < 0 || k < 0 || door_ < 0.8f) return false;
    if (duty_) return dutyAt_ == k && std::fabs(v_) < 0.3f;
    if (manual_) return false;
    return atStop_ == k && (state_ == STOP || state_ == LAYOVER);
}

int CityBus::passengers() const {
    int n = 0;
    for (const BusPax& p : fleet_->pax_)
        if (p.bus == this && (p.st == BusPax::CHECKIN || p.st == BusPax::TO_SEAT || p.st == BusPax::RIDE)) n++;
    return n;
}

int CityBus::waitingAtNext(double clock) const {
    (void)clock;
    if (trip_ < 0) return 0;
    const LineTrip& T = trip();
    int k = dutyAt_ >= 0 ? dutyAt_ : next_;
    if (k > T.last || k == T.last) return 0;
    return fleet_->waitingAt(T.dir, k);
}

double CityBus::dutyDelay(double clock) const {
    if (trip_ < 0) return 0;
    const LineTrip& T = trip();
    if (dutyAt_ >= 0) return clock - T.plan[dutyAt_];
    int k = std::max(T.first + 1, std::min(next_, T.last));
    float a = stopS(k - 1), c = stopS(k);
    float f = clampf((s_ - a) / std::max(1.0f, c - a), 0, 1);
    double expect = T.plan[k - 1] + (T.plan[k] - T.plan[k - 1]) * f;
    return clock - expect;
}

float CityBus::projectS(Vec3 p, float guess) const {
    if (pts_.size() < 2) return guess;
    size_t lo = (size_t)(std::lower_bound(cum_.begin(), cum_.end(), guess - 60.0f) - cum_.begin());
    lo = lo > 0 ? lo - 1 : 0;
    float best = 1e30f, bs = guess;
    for (size_t k = lo; k + 1 < pts_.size() && cum_[k] < guess + 150.0f; k++) {
        Vec3 a = pts_[k], b = pts_[k + 1];
        float dx = b.x - a.x, dz = b.z - a.z, l2 = dx * dx + dz * dz;
        float u = l2 > 0 ? clampf(((p.x - a.x) * dx + (p.z - a.z) * dz) / l2, 0, 1) : 0;
        float qx = a.x + dx * u - p.x, qz = a.z + dz * u - p.z;
        float d2 = qx * qx + qz * qz;
        if (d2 < best) { best = d2; bs = cum_[k] + u * std::sqrt(l2); }
    }
    offRoute_ = std::sqrt(best);
    return offRoute_ < 45.0f ? bs : guess;
}

void CityBus::hotspots(std::vector<Hotspot>& out) const {
    if (driverAboard_) return;
    const Vec3& p = model_->wheelPos;
    out.push_back({Hotspot::DRIVE, 0, p.x, p.y, p.z, 0.32f, 0, "E / klik: stuur - zelf rijden"});
    if (inService())
        for (size_t i = 0; i < model_->stopButtons.size(); i++) {
            const BusModel::Spot& b = model_->stopButtons[i];
            out.push_back({Hotspot::DOOR, b.sec, b.x, b.y, b.z, 0.14f, (int)i, stopReq_ ? "Stop is aangevraagd" : "E / klik: stopknop"});
        }
}

void CityBus::activate(const Hotspot& h) {
    if (h.kind == Hotspot::DOOR) requestStop();
}

// ============================== zelf rijden ==============================
bool CityBus::driverEye(float& lx, float& ly, float& lz) const {
    lx = model_->driverEye.x;
    ly = model_->driverEye.y;
    lz = model_->driverEye.z;
    return true;
}

void CityBus::setDriver(bool on) {
    if (on && !manual_) {
        // de chauffeur geeft het stuur over: de rit vervalt, huidige stand van de bus overnemen
        const BusSection& f = model_->sec[0];
        mh_ = pose_[0].h;
        mR_ = carToWorld(0, 0, 0, f.axleR);
        mF_ = carToWorld(0, 0, 0, f.axleF);
        if (model_->articulated) mR2_ = carToWorld(1, 0, 0, model_->sec[1].axleR);
        manualDoor_ = door_ > 0.5f;
        steer_ = 0;
        reverseHold_ = 0;
        manual_ = true;
        if (!duty_) {
            // vrij spelen: de rit vervalt, reizigers stappen bij de volgende gelegenheid niet meer in
            stopReq_ = false;
            if (fleet_->ann_ && fleet_->annBus_ == this) fleet_->ann_->stop();
        }
    }
    driverAboard_ = on;
    ctl_ = DriveControls();
}

void CityBus::poseFromManual() {
    const World& w = *fleet_->world_;
    const BusSection& f = model_->sec[0];
    float wb = f.axleF - f.axleR;
    Vec3 d(std::sin(mh_), 0, std::cos(mh_));
    mF_ = mR_ + d * wb;
    mR_.y = w.heightAt(mR_.x, mR_.z);
    mF_.y = w.heightAt(mF_.x, mF_.z);
    Vec3 c = mR_ + d * (-f.axleR);
    pose_[0] = {c.x, (mF_.y + mR_.y) / 2, c.z, mh_};
    if (model_->articulated) {
        const BusSection& r = model_->sec[1];
        float jz = -f.length / 2 - model_->jointGap / 2;
        Vec3 J = mR_ + d * (jz - f.axleR);
        float jTo = r.axleF - r.axleR;
        Vec3 d2 = flat(J - mR2_);
        float l2 = std::hypot(d2.x, d2.z);
        d2 = l2 > 0.01f ? d2 * (1.0f / l2) : d;
        mR2_ = J - d2 * jTo;
        mR2_.y = w.heightAt(mR2_.x, mR2_.z);
        Vec3 c2 = J - d2 * r.axleF;
        pose_[1] = {c2.x, (mR_.y + mR2_.y) / 2, c2.z, std::atan2(d2.x, d2.z)};
    }
}

void CityBus::updateManual(float dt, bool playerInside) {
    const BusSection& f = model_->sec[0];
    float wb = f.axleF - f.axleR;
    DriveControls c = driverAboard_ ? ctl_ : DriveControls();
    if (!driverAboard_) {
        c.brake = 1;  // niemand aan het stuur: op de rem, deuren open zolang de speler nog binnen is
        if (std::fabs(v_) < 0.1f) manualDoor_ = playerInside;
    }
    if (c.doorToggle && std::fabs(v_) < 0.3f) manualDoor_ = !manualDoor_;
    door_ = clampf(door_ + (manualDoor_ ? dt / 1.2f : -dt / 1.5f), 0, 1);
    bool shut = !manualDoor_ && door_ <= 0.0f;
    float va = std::fabs(v_);
    float maxSteer = 0.62f / (1.0f + va * va / 40.0f);
    float target = clampf(c.steer, -1, 1) * maxSteer;
    float rate = (std::fabs(target) < std::fabs(steer_) ? 1.4f : 0.8f) * dt;
    steer_ += clampf(target - steer_, -rate, rate);
    const float VMAX = 70 / 3.6f, VREV = 3.0f;
    bool rev = false;
    if (c.brake > 0 && v_ <= 0.05f && shut && driverAboard_) {
        reverseHold_ += dt;
        rev = reverseHold_ > 0.5f;
    } else if (c.brake <= 0) {
        reverseHold_ = 0;
    }
    if (rev) v_ = std::max(-VREV, v_ - 0.9f * dt);
    else if (c.brake > 0) v_ = v_ > 0 ? std::max(0.0f, v_ - 3.5f * dt) : std::min(0.0f, v_ + 3.5f * dt);
    else if (c.throttle > 0 && shut) v_ = v_ < -0.05f ? std::min(0.0f, v_ + 3.0f * dt) : std::min(VMAX, v_ + 1.3f * c.throttle * (1.0f - 0.5f * v_ / VMAX) * dt);
    else v_ = v_ > 0 ? std::max(0.0f, v_ - 0.3f * dt) : std::min(0.0f, v_ + 0.3f * dt);
    float ds = v_ * dt;
    float k = std::tan(steer_) / wb;
    float hm = mh_ + 0.5f * ds * k;
    mR_ = mR_ + Vec3(std::sin(hm), 0, std::cos(hm)) * ds;
    mh_ = wrapAngle(mh_ + ds * k);
    poseFromManual();
}

// ============================== BusFleet ==============================
BusFleet::~BusFleet() {}

void BusFleet::reset() {
    buses_.clear();
    pax_.clear();
    generated_.clear();
    duty = DutyStats();
    for (LineBlock& b : blocks_) b.spawned = b.done = false;
    if (ann_) ann_->stop();
    annBus_ = nullptr;
}

int BusFleet::textIndex(const std::string& s) const {
    for (size_t i = 0; i < texts_.size(); i++)
        if (texts_[i] == s) return (int)i;
    return 0;
}

int BusFleet::screenIndex(const std::string& s) const {
    for (size_t i = 0; i < screenNames_.size(); i++)
        if (screenNames_[i] == s) return (int)i;
    return -1;
}

std::vector<CityBus*> BusFleet::buses() {
    std::vector<CityBus*> v;
    for (auto& b : buses_) v.push_back(b.get());
    return v;
}

// Dienstregeling van een lijn inlezen (data/bus/lijnNN.txt) en ritten + omlopen maken voor vandaag en morgen.
//   busboek-formaat (lijn 18): rit <richting> halte=hh:mm;...  (alleen hoofdhaltes, haltenamen zoals in OSM)
//   GTFS-formaat ("bron gtfs"): halte <id> <x> <z> <Plaats, Naam>
//                               rit <richting> <omloop> <bestemming>|g<id>=hh:mm:ss[~];...   (~ = geen tijdhalte)
//   GTFS-haltes worden op positie gekoppeld aan de OSM-haltes van de best passende OSM-route van de lijn.
bool BusFleet::loadTimetable(const std::string& file, int weekday) {
    std::ifstream in(file);
    if (!in) { printf("Geen dienstregeling: %s\n", file.c_str()); return false; }
    std::map<std::string, std::string> place, spoken;
    std::string destName[2], ref, oper = "Hermes";
    bool gtfs = false;
    struct GStop { float x = 0, z = 0; std::string place, name; };
    std::map<std::string, GStop> gstops;
    struct Raw { std::string day, dest; int dir; std::vector<std::pair<std::string, int>> times; std::vector<bool> tp; };
    std::vector<Raw> raws;
    std::string day, line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        std::string rest;
        std::getline(ls, rest);
        rest = trim(rest);
        if (key == "lijn") {
            ref = rest;
        } else if (key == "vervoerder") {
            oper = rest;
        } else if (key == "bron") {
            gtfs = rest == "gtfs";
        } else if (key == "halte") {
            std::istringstream hs(rest);
            std::string id, nm;
            GStop g;
            hs >> id >> g.x >> g.z;
            std::getline(hs, nm);
            nm = trim(nm);
            size_t c = nm.find(',');
            if (c != std::string::npos) { g.place = trim(nm.substr(0, c)); g.name = trim(nm.substr(c + 1)); }
            else g.name = nm;
            gstops[id] = g;
        } else if (key == "eindbestemming") {
            int d = rest[0] - '0';
            if (d == 0 || d == 1) destName[d] = trim(rest.substr(1));
        } else if (key == "plaats") {
            size_t eq = rest.find('=');
            std::string p = trim(rest.substr(0, eq));
            std::stringstream ss(rest.substr(eq + 1));
            std::string n;
            while (std::getline(ss, n, ',')) place[lower(trim(n))] = p;
        } else if (key == "uitspraak") {
            size_t eq = rest.find('=');
            spoken[lower(trim(rest.substr(0, eq)))] = trim(rest.substr(eq + 1));
        } else if (key == "dag") {
            day = rest;
        } else if (key == "rit" && !rest.empty()) {
            Raw r;
            r.day = day;
            r.dir = rest[0] - '0';
            std::string body = trim(rest.substr(1));
            if (gtfs) {
                size_t bar = body.find('|');
                if (bar == std::string::npos) continue;
                std::string head = body.substr(0, bar);
                body = body.substr(bar + 1);
                size_t sp = head.find(' ');
                r.dest = sp == std::string::npos ? "" : trim(head.substr(sp + 1));
            }
            std::stringstream ss(body);
            std::string item;
            while (std::getline(ss, item, ';')) {
                size_t eq = item.find('=');
                if (eq == std::string::npos) continue;
                int h = 0, m = 0, s = 0;
                std::string tv = item.substr(eq + 1);
                sscanf(tv.c_str(), "%d:%d:%d", &h, &m, &s);
                std::string k = trim(item.substr(0, eq));
                if (gtfs) { if (!k.empty() && k[0] == 'g') k = k.substr(1); }
                else k = lower(k);
                r.times.push_back({k, h * 3600 + m * 60 + s});
                r.tp.push_back(!gtfs || tv.find('~') == std::string::npos);
            }
            raws.push_back(std::move(r));
        }
    }
    if (ref.empty()) return false;
    const int li = (int)lines_.size();
    {
        BusLine L;
        L.ref = ref;
        L.operatorName = oper;
        L.articulated = ref == "401" || ref == "402";  // HOV-lijnen: altijd gelede bussen
        lines_.push_back(L);
    }
    // ---- richtingen: alle OSM-routes van deze lijn ----
    const size_t d0 = dirs_.size();
    for (size_t i = 0; i < world_->busRoutes.size(); i++) {
        const BusRoute& r = world_->busRoutes[i];
        if (r.ref != ref || r.stops.size() < 2 || r.pts.size() < 2) continue;
        LineDir D;
        D.line = li;
        D.ref = ref;
        if (!gtfs) {
            std::string lastStop = lower(r.stops.back().name);
            int d = destName[0].size() && lower(destName[0]).find(lastStop) != std::string::npos ? 0 : 1;
            D.dest = destName[d];
        } else {
            D.dest = r.stops.back().name;
        }
        for (const BusRoutePoint& p : r.pts) {
            D.pts.push_back(Vec3(p.x, p.y, p.z));
            D.lim.push_back(p.lim);
            D.cum.push_back(D.cum.empty() ? 0 : D.cum.back() + std::hypot(p.x - D.pts[D.pts.size() - 2].x, p.z - D.pts[D.pts.size() - 2].z));
        }
        for (const BusRouteStop& s : r.stops) {
            LineStop st;
            st.name = s.name;
            st.place = place.count(lower(s.name)) ? place[lower(s.name)] : "";
            if (gtfs) {  // plaats van de dichtstbijzijnde GTFS-halte
                float bd = 120;
                for (auto& g : gstops) {
                    float d = std::hypot(g.second.x - s.x, g.second.z - s.z);
                    if (d < bd) { bd = d; st.place = g.second.place; }
                }
            }
            // omroepfragment: bij GTFS-lijnen met plaatsnaam (Centraal Station in Eindhoven en Den Bosch, Station in Best/Helmond)
            st.clip = slug(gtfs && !st.place.empty() ? st.place + " " + s.name : s.name);
            st.x = s.x; st.z = s.z; st.s = s.s; st.lat = s.lat; st.flags = s.flags;
            D.stops.push_back(st);
        }
        // legacy: richting 0 en 1 vast (eindbestemming 0 / 1)
        dirs_.push_back(std::move(D));
    }
    if (dirs_.size() == d0) { printf("Buslijn %s: geen route in world.bin\n", ref.c_str()); return false; }
    // busboek: richting r.dir -> OSM-route met de bijbehorende eindbestemming
    int legacyDir[2] = {-1, -1};
    if (!gtfs)
        for (size_t d = d0; d < dirs_.size(); d++) legacyDir[dirs_[d].dest == destName[0] ? 0 : 1] = (int)d;

    // ---- ritten voor vandaag (dag 0) en morgen (dag 1) ----
    auto dayType = [](int wd) { return wd == 0 ? std::string("zo") : wd == 6 ? std::string("za") : std::string("ma-vr"); };
    const size_t t0 = trips_.size();
    int unmatched = 0;
    for (int dd = 0; dd < 2; dd++) {
        std::string dt = dayType((weekday + dd) % 7);
        for (const Raw& r : raws) {
            if (r.day != dt || r.times.size() < 2) continue;
            LineTrip T;
            if (!gtfs) {
                if (r.dir < 0 || r.dir > 1 || legacyDir[r.dir] < 0) continue;
                T.dir = legacyDir[r.dir];
                const LineDir& D = dirs_[T.dir];
                T.plan.assign(D.stops.size(), -1.0);
                T.timing.assign(D.stops.size(), false);
                int from = 0;
                double prev = -1;
                bool ok = true;
                for (const auto& tt : r.times) {
                    int found = -1;
                    for (int k = from; k < (int)D.stops.size(); k++)
                        if (lower(D.stops[k].name) == tt.first) { found = k; break; }
                    if (found < 0) { printf("  lijn %s: halte '%s' niet gevonden in de route\n", ref.c_str(), tt.first.c_str()); ok = false; break; }
                    double t = tt.second + dd * 86400.0;
                    if (prev >= 0 && t < prev) t += 86400.0;  // over middernacht
                    T.plan[found] = t;
                    T.timing[found] = true;
                    prev = t;
                    from = found + 1;
                }
                if (!ok) continue;
            } else {
                // beste OSM-richting: de meeste GTFS-haltes in volgorde binnen 70 m
                int bestDir = -1, bestN = 0;
                float bestSum = 1e30f;
                std::vector<int> bestMk;
                for (size_t d = d0; d < dirs_.size(); d++) {
                    const LineDir& D = dirs_[d];
                    std::vector<int> mk(r.times.size(), -1);
                    int from = 0, n = 0;
                    float sum = 0;
                    for (size_t i = 0; i < r.times.size(); i++) {
                        auto g = gstops.find(r.times[i].first);
                        if (g == gstops.end()) continue;
                        int kb = -1;
                        float bd = 70;
                        int kEnd = n == 0 ? (int)D.stops.size() : std::min((int)D.stops.size(), from + 15);
                        for (int k = from; k < kEnd; k++) {
                            float dist = std::hypot(D.stops[k].x - g->second.x, D.stops[k].z - g->second.z);
                            if (dist < bd) { bd = dist; kb = k; }
                        }
                        if (kb < 0) continue;
                        mk[i] = kb;
                        from = kb + 1;
                        n++;
                        sum += bd;
                    }
                    // begin- en eindpunt (busstations): ruimer koppelen aan de eerste/laatste OSM-halte
                    auto gpos = [&](size_t i, float& x, float& z) {
                        auto g = gstops.find(r.times[i].first);
                        if (g == gstops.end()) return false;
                        x = g->second.x; z = g->second.z;
                        return true;
                    };
                    float gx, gz;
                    int firstOther = -1, lastOther = -1;
                    for (size_t i = 1; i < mk.size(); i++) if (mk[i] >= 0) { firstOther = mk[i]; break; }
                    for (size_t i = mk.size() - 1; i-- > 0;) if (mk[i] >= 0) { lastOther = mk[i]; break; }
                    if (mk.front() < 0 && gpos(0, gx, gz) && (firstOther < 0 || firstOther > 0) &&
                        std::hypot(D.stops.front().x - gx, D.stops.front().z - gz) < 250) { mk.front() = 0; n++; }
                    int lastK = (int)D.stops.size() - 1;
                    if (mk.back() < 0 && gpos(mk.size() - 1, gx, gz) && (lastOther < 0 || lastOther < lastK) &&
                        std::hypot(D.stops.back().x - gx, D.stops.back().z - gz) < 250) { mk.back() = lastK; n++; }
                    if (mk.front() < 0 || mk.back() < 0) continue;
                    if (n > bestN || (n == bestN && sum < bestSum)) { bestN = n; bestSum = sum; bestDir = (int)d; bestMk = mk; }
                }
                if (bestDir < 0 || bestN < (int)(r.times.size() * 0.7f)) { unmatched++; continue; }
                T.dir = bestDir;
                T.dest = r.dest;
                const LineDir& D = dirs_[T.dir];
                T.plan.assign(D.stops.size(), -1.0);
                T.timing.assign(D.stops.size(), false);
                double prev = -1;
                for (size_t i = 0; i < r.times.size(); i++) {
                    if (bestMk[i] < 0) continue;
                    double t = r.times[i].second + dd * 86400.0;
                    if (prev >= 0 && t < prev) t += 86400.0;
                    T.plan[bestMk[i]] = t;
                    if (r.tp[i]) T.timing[bestMk[i]] = true;
                    prev = t;
                }
            }
            const LineDir& D = dirs_[T.dir];
            T.first = -1; T.last = -1;
            for (int k = 0; k < (int)T.plan.size(); k++)
                if (T.plan[k] >= 0) { if (T.first < 0) T.first = k; T.last = k; }
            if (T.first < 0 || T.first == T.last) continue;
            T.timing[T.first] = T.timing[T.last] = true;
            // GTFS zonder tijdhaltes: ongeveer elke 1,8 km een halte waar de bus wacht als hij te vroeg is
            if (gtfs) {
                float lastS = D.stops[T.first].s;
                for (int k = T.first + 1; k < T.last; k++) {
                    if (T.plan[k] < 0) continue;
                    if (!T.timing[k] && D.stops[k].s - lastS > 1800) T.timing[k] = true;
                    if (T.timing[k]) lastS = D.stops[k].s;
                }
            }
            // haltes zonder tijd (busboek: tussenliggende haltes; GTFS: OSM-haltes die de rit niet noemt): naar afstand
            int a = T.first;
            for (int k = T.first + 1; k <= T.last; k++) {
                if (T.plan[k] < 0) continue;
                for (int j = a + 1; j < k; j++) {
                    float f = (D.stops[j].s - D.stops[a].s) / std::max(1.0f, D.stops[k].s - D.stops[a].s);
                    T.plan[j] = T.plan[a] + (T.plan[k] - T.plan[a]) * f;
                }
                a = k;
            }
            T.id = (int)trips_.size() + 1;
            trips_.push_back(std::move(T));
        }
    }
    for (size_t i = t0; i < trips_.size(); i++) {
        LineDir& D = dirs_[trips_[i].dir];
        if (D.trips++ == 0 && !trips_[i].dest.empty()) D.dest = trips_[i].dest;
    }
    makeBlocks(li, t0);
    int nb = 0;
    for (const LineBlock& b : blocks_) if (!b.trips.empty() && dirs_[trips_[b.trips[0]].dir].line == li) nb++;
    printf("Lijn %s (%s): %zu ritten, %d omlopen (%s)%s\n", ref.c_str(), oper.c_str(), trips_.size() - t0, nb, dayType(weekday).c_str(),
           unmatched ? (", " + std::to_string(unmatched) + " ritten niet op de OSM-route te leggen").c_str() : "");
    return trips_.size() > t0;
}

// Omlopen: na aankomst op een eindpunt neemt de bus de eerstvolgende rit van dezelfde lijn die daar vertrekt
// (minimaal 3 min keertijd, de kortste die past)
void BusFleet::makeBlocks(int line, size_t firstTrip) {
    std::vector<int> order;
    for (size_t i = firstTrip; i < trips_.size(); i++) order.push_back((int)i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return trips_[a].plan[trips_[a].first] < trips_[b].plan[trips_[b].first]; });
    size_t b0 = blocks_.size();
    for (int ti : order) {
        const LineTrip& T = trips_[ti];
        double dep = T.plan[T.first];
        const LineDir& TD = dirs_[T.dir];
        int best = -1;
        double bestEnd = 1e30;
        for (size_t b = b0; b < blocks_.size(); b++) {
            const LineTrip& L = trips_[blocks_[b].trips.back()];
            const LineStop& end = dirs_[L.dir].stops[L.last];
            // aansluiting: de vorige rit eindigt op het vertrekpunt, of op een halte eerder op de route van deze rit
            // (lus, bv. lijn 402: eindigt in Sondervick, volgende rit begint bij De Naaldenmaker; de bus rijdt de
            // route vanaf het begin)
            int at = -1;
            for (int k = T.first; k >= 0 && at < 0; k--)
                if (lower(end.name) == lower(TD.stops[k].name) || std::hypot(end.x - TD.stops[k].x, end.z - TD.stops[k].z) < 250) at = k;
            if (at < 0) continue;
            double arr = L.plan[L.last];
            double minLay = at == T.first ? 120 : 60;
            if (arr + minLay <= dep && dep - arr <= 45 * 60 && (best < 0 || arr > bestEnd)) { bestEnd = arr; best = (int)b; }
        }
        if (best < 0) {
            blocks_.push_back({});
            best = (int)blocks_.size() - 1;
            blocks_[best].start = dep;
        }
        blocks_[best].trips.push_back(ti);
        blocks_[best].end = T.plan[T.last];
    }
    (void)line;
}

// ---------- haltepalen, abri's en bankjes ----------
void BusFleet::buildStops(SignAtlas& atlas) {
    MeshBuilder body, signs, glass;
    const Vec3 POLE(0.55f, 0.57f, 0.60f), FRAME(0.30f, 0.32f, 0.35f), ROOF(0.22f, 0.23f, 0.25f), WOOD(0.45f, 0.33f, 0.22f);
    // een paal per halteplek; lijnen die dezelfde halte gebruiken komen samen op het bord
    struct Pole { const LineDir* D; const LineStop* st; std::vector<std::string> refs; };
    std::vector<Pole> poles;
    for (const LineDir& D : dirs_) {
        if (!D.trips) continue;
        for (const LineStop& st : D.stops) {
            if (st.name == "Centraal Station" && std::hypot(st.x, st.z) < 500) continue;  // busstation Neckerspoel heeft eigen perrons
            Pole* p = nullptr;
            for (Pole& q : poles)
                if (q.st->name == st.name && std::hypot(q.st->x - st.x, q.st->z - st.z) < 6) p = &q;
            if (!p) { poles.push_back({&D, &st, {}}); p = &poles.back(); }
            if (std::find(p->refs.begin(), p->refs.end(), D.ref) == p->refs.end()) p->refs.push_back(D.ref);
        }
    }
    for (Pole& pole : poles) {
        const LineDir& D = *pole.D;
        const LineStop& st = *pole.st;
        std::sort(pole.refs.begin(), pole.refs.end(), [](const std::string& a, const std::string& b) {
            return atoi(a.c_str()) != atoi(b.c_str()) ? atoi(a.c_str()) < atoi(b.c_str()) : a < b; });
        std::string refText;
        for (const std::string& r : pole.refs) refText += (refText.empty() ? "" : "  ") + r;
        {
            // rijrichting bij de halte
            size_t k = (size_t)(std::upper_bound(D.cum.begin(), D.cum.end(), st.s) - D.cum.begin());
            k = std::clamp<size_t>(k, 1, D.pts.size() - 1);
            Vec3 t = normalize(flat(D.pts[k] - D.pts[k - 1]));
            Vec3 r(-t.z, 0, t.x);  // rechts = weg van de weg (haltes staan rechts)
            if (st.lat < 0) r = r * -1.0f;
            float y = world_->heightAt(st.x, st.z);
            Vec3 P(st.x, y, st.z);
            float hd = std::atan2(t.x, t.z);  // heading langs de weg
            auto box = [&](MeshBuilder& m, Vec3 c, float along, float h, float across, Vec3 col, float mat) {
                // doos uitgelijnd met de weg: 'along' langs t, 'across' langs r
                m.orientedBox(c, {across, h, along}, hd, col, mat);
            };
            // paal met bord (haaks op de weg) en een kastje met vertrektijden
            box(body, P + Vec3(0, 1.45f, 0), 0.07f, 2.9f, 0.07f, POLE, MAT_METAL);
            Vec3 plate = P + r * 0.30f + Vec3(0, 2.55f, 0);
            box(body, plate, 0.03f, 0.62f, 0.52f, Vec3(0.95f, 0.95f, 0.95f), MAT_PAINT);
            box(body, P + Vec3(0, 1.45f, 0) + t * 0.06f, 0.05f, 0.55f, 0.36f, FRAME, MAT_PAINT);  // dienstregelingkastje
            {
                SignRect rl = atlas.add(refText, Vec3(0.02f, 0.30f, 0.62f), Vec3(1, 1, 1), 44, true, 0.25f);
                SignRect rn = atlas.add(st.name, Vec3(0.96f, 0.96f, 0.96f), Vec3(0.05f, 0.05f, 0.08f), 34, true, 0.2f);
                SignRect rb = atlas.add("bus", Vec3(0.96f, 0.96f, 0.96f), Vec3(0.02f, 0.30f, 0.62f), 40, true, 0.2f);
                for (int s = -1; s <= 1; s += 2) {
                    Vec3 c = plate + t * (s * 0.02f);
                    Vec3 right = r * (float)-s;  // leesbaar vanaf beide kanten
                    SignAtlas::quad(signs, rb, c + Vec3(0, 0.16f, 0), right, 0.16f, false);
                    float wn = std::min(0.48f, 0.075f * rn.aspect);
                    SignAtlas::quad(signs, rn, c + Vec3(0, -0.02f, 0), right, wn / rn.aspect, false);
                    float wl = std::min(0.48f, 0.13f * rl.aspect);
                    SignAtlas::quad(signs, rl, c + Vec3(0, -0.20f, 0), right, wl / rl.aspect, false);
                }
            }
            if (st.flags & 1) {
                // abri: glazen wanden, dak, bank; open naar de weg
                Vec3 C = P + r * 1.6f - t * 2.4f;
                const float L = 3.6f, W = 1.4f, H = 2.4f;
                for (int e = -1; e <= 1; e += 2) {
                    box(body, C + t * (e * L / 2) + r * (W / 2) + Vec3(0, H / 2, 0), 0.06f, H, 0.06f, FRAME, MAT_METAL);
                    box(body, C + t * (e * L / 2) - r * (W / 2) + Vec3(0, H / 2, 0), 0.06f, H, 0.06f, FRAME, MAT_METAL);
                    box(glass, C + t * (e * L / 2) + Vec3(0, H / 2 + 0.1f, 0), 0.02f, H - 0.3f, W, Vec3(0.5f, 0.6f, 0.65f), EX_GLASS);
                }
                box(glass, C + r * (W / 2) + Vec3(0, H / 2 + 0.1f, 0), L, H - 0.3f, 0.02f, Vec3(0.5f, 0.6f, 0.65f), EX_GLASS);
                box(body, C + Vec3(0, H + 0.06f, 0), L + 0.3f, 0.10f, W + 0.4f, ROOF, MAT_PAINT);
                box(body, C + r * (W / 2 - 0.25f) + Vec3(0, 0.45f, 0), L - 0.6f, 0.05f, 0.40f, WOOD, MAT_PAINT);
                box(body, C + r * (W / 2 - 0.25f) + Vec3(0, 0.22f, 0), 0.06f, 0.44f, 0.06f, FRAME, MAT_METAL);
                if (st.flags & 4) {  // reisinformatiescherm
                    box(body, C - t * (L / 2 - 0.4f) + r * (W / 2 - 0.05f) + Vec3(0, 2.0f, 0), 0.6f, 0.35f, 0.08f, FRAME, MAT_PAINT);
                    box(body, C - t * (L / 2 - 0.4f) + r * (W / 2 - 0.10f) + Vec3(0, 2.0f, 0), 0.52f, 0.27f, 0.01f, Vec3(0.05f, 0.04f, 0.0f), MAT_LIGHT);
                }
            } else if (st.flags & 2) {
                Vec3 C = P + r * 1.4f - t * 1.5f;
                box(body, C + Vec3(0, 0.45f, 0), 1.8f, 0.05f, 0.42f, WOOD, MAT_PAINT);
                box(body, C + r * 0.2f + Vec3(0, 0.70f, 0), 1.8f, 0.30f, 0.05f, WOOD, MAT_PAINT);
                for (int e = -1; e <= 1; e += 2) box(body, C + t * (e * 0.8f) + Vec3(0, 0.22f, 0), 0.06f, 0.44f, 0.4f, FRAME, MAT_METAL);
            }
        }
    }
    stopBody_ = body.upload();
    stopSigns_ = signs.upload();
    stopGlass_ = glass.upload();
}

void BusFleet::build(const World& world, SignAtlas& sharedAtlas, const std::string& dataDir, int weekday) {
    (void)sharedAtlas;  // de bussen hebben een eigen atlas (honderden haltenamen passen niet meer in de gedeelde)
    SignAtlas& atlas = atlas_;
    atlas.begin();
    world_ = &world;
    citea12_ = buildCitea(false);
    citea18_ = buildCitea(true);
    bays_ = world.bays;
    for (const char* l : {"18", "8", "9", "24", "156", "401", "402"}) loadTimetable(dataDir + "/bus/lijn" + l + ".txt", weekday);
    prepareStops();
    buildStops(atlas);
    writeAnnouncementList(dataDir);
    // frontborden (oranje LED-tekst): lijn + bestemming van elke rit
    texts_ = {"Niet instappen"};
    for (const LineTrip& T : trips_) {
        std::string s = dirs_[T.dir].ref + "  " + (T.dest.empty() ? dirs_[T.dir].dest : T.dest);
        if (std::find(texts_.begin(), texts_.end(), s) == texts_.end()) texts_.push_back(s);
    }
    for (const std::string& t : texts_) {
        MeshBuilder m;
        SignRect r = atlas.add(t, Vec3(0.02f, 0.02f, 0.02f), Vec3(1.0f, 0.62f, 0.10f), 56, true, 0.3f);
        float h = 0.22f, w = std::min(1.9f, h * r.aspect);
        h = w / r.aspect;
        Vec3 n(0, 0, 1);
        auto V = [&](Vec3 p, float u, float v) { return m.vert(p, n, Vec3(u, v, 0), EX_SIGN); };
        // kijker voor de bus kijkt naar -z: zijn rechterkant is +x, dus het einde van de tekst (u1) op +x
        uint32_t i0 = V(Vec3(w / 2, 2.75f - h / 2, 0), r.u1, r.v1), i1 = V(Vec3(-w / 2, 2.75f - h / 2, 0), r.u0, r.v1);
        uint32_t i2 = V(Vec3(-w / 2, 2.75f + h / 2, 0), r.u0, r.v0), i3 = V(Vec3(w / 2, 2.75f + h / 2, 0), r.u1, r.v0);
        m.i.insert(m.i.end(), {i0, i1, i2, i0, i2, i3});
        textMesh_.push_back(m.upload());
    }
    // informatieschermen in de bus: losse teksten, links of rechts uitgelijnd op de oorsprong (beeld naar -z)
    const Vec3 SCR(1.0f, 1.0f, 1.0f), BLUE_T(0.04f, 0.20f, 0.58f);  // wit scherm, blauwe letters
    auto screenText = [&](const std::string& s, Vec3 bg, Vec3 fg, float h, float maxW, bool rightAlign) {
        SignRect r = atlas.add(s, bg, fg, 34, true, 0.15f);
        float w = h * r.aspect;
        if (w > maxW) { w = maxW; h = w / r.aspect; }
        MeshBuilder m;
        SignAtlas::quad(m, r, Vec3(rightAlign ? w / 2 : -w / 2, 0, 0), Vec3(-1, 0, 0), h, false);
        return ScreenText{m.upload(), w};
    };
    for (const LineDir& D : dirs_)
        if (D.trips)
            for (const LineStop& st : D.stops)
                if (screenIndex(st.name) < 0) {
                    screenNames_.push_back(st.name);
                    nameWhite_.push_back(screenText(st.name, SCR, BLUE_T, 0.032f, 0.28f, false));
                    nameYellow_.push_back(screenText(st.name, BLUE_T, Vec3(1, 1, 1), 0.032f, 0.28f, false));  // volgende halte: wit op blauw
                }
    minutes_.push_back(screenText("<1 min", SCR, BLUE_T, 0.028f, 0.09f, true));
    for (int m = 1; m <= 60; m++) minutes_.push_back(screenText(std::to_string(m) + " min", SCR, BLUE_T, 0.028f, 0.09f, true));
    // koppen: dezelfde teksten als de frontborden (index = textIndex)
    for (const std::string& t : texts_) headers_.push_back(screenText(t, BLUE_T, Vec3(1, 1, 1), 0.036f, 0.38f, false));
    stopBanner_ = screenText("  STOPT  ", Vec3(0.80f, 0.05f, 0.05f), Vec3(1, 1, 1), 0.030f, 0.18f, false);
    atlasTex_ = atlas.finish();
    extern int g_signAtlasFull;
    printf("Bussen: %zu lijnen, %zu richtingen, %zu ritten, %zu omlopen, %zu haltenamen%s\n", lines_.size(), dirs_.size(), trips_.size(), blocks_.size(),
           screenNames_.size(), g_signAtlasFull ? " - LET OP: tekstatlas vol" : "");
}

void BusFleet::bindAtlas(World& world) const { world.bindSignTexture(atlasTex_); }

// Lijst van omroepteksten die nog geen geluidsbestand hebben (data/bus/omroep_nieuw.txt, "clip=tekst"), voor
// tools/make_announcements.ps1
void BusFleet::writeAnnouncementList(const std::string& dataDir) const {
    std::map<std::string, std::string> need;
    auto spokenName = [](const LineStop& st) {
        std::string n = st.name;
        if (n == "Centraal Station" || n == "Station" || n.rfind("Station ", 0) == 0) n = st.place.empty() ? n : st.place + ", " + n;
        return n;
    };
    for (const LineDir& D : dirs_) {
        if (!D.trips || D.ref == "18") continue;  // lijn 18 heeft een eigen lijst (plaats/uitspraak in lijn18.txt)
        for (const LineStop& st : D.stops) need["halte_" + st.clip] = "Volgende halte: " + spokenName(st) + ".";
        const LineStop& last = D.stops.back();
        need["eind_" + last.clip] = "Volgende halte: " + spokenName(last) + ". Dit is de eindhalte van deze bus." +
                                    (last.name.find("Station") != std::string::npos ? " U kunt hier overstappen op de trein." : "");
    }
    for (const LineTrip& T : trips_) {
        const LineDir& D = dirs_[T.dir];
        if (D.ref == "18") continue;
        std::string dest = T.dest.empty() ? D.dest : T.dest;
        need["welkom_" + slug(D.ref + " " + dest)] = "Welkom in lijn " + D.ref + " naar " + dest + ".";
    }
    FILE* f = fopen((dataDir + "/bus/omroep_nieuw.txt").c_str(), "w");
    if (!f) return;
    int n = 0;
    for (auto& kv : need) {
        FILE* w = fopen((dataDir + "/audio/bus/" + kv.first + ".wav").c_str(), "rb");
        if (w) { fclose(w); continue; }
        fprintf(f, "%s=%s\n", kv.first.c_str(), kv.second.c_str());
        n++;
    }
    fclose(f);
    if (n) printf("Omroep: %d teksten nog in te spreken (data/bus/omroep_nieuw.txt)\n", n);
}

void BusFleet::spawnBlock(int bi, double clock, Vec3 playerPos) {
    (void)playerPos;
    LineBlock& B = blocks_[bi];
    B.spawned = true;
    int pos = -1;
    for (size_t j = 0; j < B.trips.size(); j++) {
        const LineTrip& T = trips_[B.trips[j]];
        if (clock < T.plan[T.last]) { pos = (int)j; break; }
    }
    if (pos < 0) { B.done = true; return; }
    {
        // staat de bus nu op een eindpunt te wachten op een rit die pas later vertrekt: pas 6 min ervoor verschijnen
        const LineTrip& T0 = trips_[B.trips[pos]];
        if (clock < T0.plan[T0.first] - 360) { B.spawned = false; return; }
    }
    auto bus = std::make_unique<CityBus>();
    CityBus& b = *bus;
    b.id = nextId_++;
    b.fleet_ = this;
    // VDL Citea 12 m, op lijn 401 en 402 altijd de gelede 18 m
    b.model_ = lines_[dirs_[trips_[B.trips[pos]].dir].line].articulated ? &citea18_ : &citea12_;
    b.block_ = bi;
    b.blockPos_ = pos;
    b.startTrip(B.trips[pos], false, false);
    const LineTrip& T = trips_[B.trips[pos]];
    if (clock > T.plan[T.first]) {
        b.placeBySchedule(clock);
        seedOnboard(b, clock);
    } else b.door_ = clock > T.plan[T.first] - 150 ? 1.0f : 0.0f;
    buses_.push_back(std::move(bus));
}

void BusFleet::update(float dt, double clock, Vec3 playerPos, const Vehicle* playerVehicle, bool playerSeated) {
    (void)playerSeated;
    clock_ = clock;
    // omlopen starten (6 min voor vertrek) en afsluiten
    for (size_t bi = 0; bi < blocks_.size(); bi++) {
        LineBlock& B = blocks_[bi];
        if (B.spawned || B.done) continue;
        if (clock > B.end + 60) { B.done = true; continue; }
        if (clock >= B.start - 360) spawnBlock((int)bi, clock, playerPos);
    }
    // omroep alleen in de bus waar de speler in zit
    const CityBus* inBus = nullptr;
    for (auto& b : buses_) if (playerVehicle == b.get()) inBus = b.get();
    if (ann_) {
        if (annBus_ && inBus != annBus_) ann_->stop();
        ann_->setGain(inBus ? 1.0f : 0.0f);
    }
    annBus_ = inBus;
    tipTimer_ -= dt;

    for (auto& bp : buses_) {
        CityBus& b = *bp;
        bool aboard = playerVehicle == &b;
        // ---------- zelf gereden ----------
        if (b.duty_) {
            if (!aboard) b.driverAboard_ = false;
            updateDuty(b, dt, clock, aboard);
            continue;
        }
        if (b.manual_) {
            if (!aboard) b.driverAboard_ = false;
            b.updateManual(dt, aboard);
            if (!aboard && std::fabs(b.v_) < 0.05f) {
                b.outTimer_ += dt;
                CarPose p = b.pose_[0];
                if (b.outTimer_ > 20 && std::hypot(p.x - playerPos.x, p.z - playerPos.z) > 150) b.state_ = CityBus::OFFDUTY, b.outTimer_ = 1e9f;
            }
            continue;
        }
        if (b.trip_ < 0) continue;
        const LineTrip& T = b.trip();
        const LineDir& D = b.dir();
        bool nearPlayer = aboard || std::hypot(b.pose_[0].x - playerPos.x, b.pose_[0].z - playerPos.z) < 250;
        // ---------- buiten dienst: uitstappen en weg ----------
        if (b.state_ == CityBus::OFFDUTY) {
            b.outTimer_ += dt;
            b.door_ = clampf(b.door_ + (b.outTimer_ < 40 ? dt / 1.2f : -dt / 1.5f), 0, 1);
            continue;
        }
        // ---------- staan: halte of eindpunt ----------
        if (b.state_ == CityBus::STOP || b.state_ == CityBus::LAYOVER) {
            b.v_ = 0;
            bool terminal = b.state_ == CityBus::LAYOVER && b.atStop_ == T.last;
            if (terminal) {
                // eindpunt: iedereen eruit, dan door naar de volgende rit van de omloop
                b.outTimer_ += dt;
                if (paxBusy(&b) || b.passengers() > 0) b.outTimer_ = std::min(b.outTimer_, 40.0f);
                b.door_ = clampf(b.door_ + (b.outTimer_ < 45 ? dt / 1.2f : -dt / 1.5f), 0, 1);
                if (b.outTimer_ > 45 && b.door_ <= 0) {
                    const LineBlock& B = blocks_[b.block_];
                    const LineTrip* NT = b.blockPos_ + 1 < (int)B.trips.size() ? &trips_[B.trips[b.blockPos_ + 1]] : nullptr;
                    if (NT && !aboard && NT->plan[NT->first] - clock > 480) {
                        // lange keertijd: even naar de stalling, 6 min voor vertrek staat hij weer klaar
                        blocks_[b.block_].spawned = false;
                        b.parked_ = true;
                        b.state_ = CityBus::OFFDUTY;
                        b.outTimer_ = 1e9f;
                        b.trip_ = -1;
                    } else if (NT) {
                        b.blockPos_++;
                        b.startTrip(B.trips[b.blockPos_], true, nearPlayer);
                        b.outTimer_ = 0;
                    } else {
                        b.state_ = CityBus::OFFDUTY;
                        b.outTimer_ = 0;
                        b.trip_ = -1;
                    }
                }
                b.computePose();
                continue;
            }
            bool boarding = b.state_ == CityBus::STOP || clock > b.depart_ - 150;
            // reizigers stappen nog in of uit: wachten
            if (boarding && b.door_ > 0.5f && (paxBusy(&b) || waitingAt(T.dir, b.atStop_) > 0)) b.depart_ = std::max(b.depart_, clock + 3.5);
            bool wantOpen = boarding && clock < b.depart_ - 3;
            // reizigers die nog instappen: de speler in de deuropening
            if (!aboard && clock > b.depart_ - 4 && b.door_ > 0.5f)
                for (int c = 0; c < b.carCount(); c++) {
                    float lx, lz;
                    b.worldToCar(c, playerPos, lx, lz);
                    if (std::fabs(lx) < BUS_HW + 0.8f && b.inDoorway(c, lz, -1) && std::fabs(playerPos.y - b.pose_[c].y) < 2.5f) {
                        b.depart_ = clock + 4;
                        wantOpen = true;
                    }
                }
            b.door_ = clampf(b.door_ + (wantOpen ? dt / 1.2f : -dt / 1.5f), 0, 1);
            if (clock >= b.depart_ && b.door_ <= 0) {
                if (b.state_ == CityBus::LAYOVER && aboard && ann_ && !b.welcomed_)
                    ann_->say("welkom_" + slug(D.ref == "18" ? b.destination() : D.ref + " " + b.destination()));
                b.welcomed_ = true;
                b.state_ = CityBus::DRIVE;
                b.next_ = b.atStop_ + 1;
                b.atStop_ = -1;
                b.paceTimer_ = 0;
            }
            b.computePose();
            continue;
        }
        // ---------- rijden ----------
        // tempo: zo rijden dat de bus op de volgende hoofdhalte niet (veel) te vroeg is
        b.paceTimer_ -= dt;
        if (b.paceTimer_ <= 0 && !b.positioning_) {
            b.paceTimer_ = 8;
            int j = b.next_;
            while (j < T.last && !T.timing[j]) j++;
            if (j <= T.last && T.timing[j]) {
                float tfree = 10;
                float sEnd = b.stopS(j);
                for (float s = b.s_; s < sEnd; s += 10) tfree += 10 / std::max(3.0f, std::min(b.limAt(s), BUS_VMAX) * 0.82f);
                double sched = T.plan[j] - clock;
                b.pace_ = sched > tfree ? clampf(tfree / (float)sched, 0.6f, 1.0f) : 1.0f;
            } else {
                b.pace_ = 1;
            }
        }
        // bij welke halte stoppen?
        int target = -1;
        if (b.positioning_) target = T.first;
        else
            for (int k = b.next_; k <= T.last; k++) {
                if (b.stopS(k) - b.s_ > 400) break;
                // stoppen op de eindhalte, na de stopknop, als er reizigers wachten of als jij bij de halte staat
                bool want = k == T.last || (k == b.next_ && b.stopReq_) || waitingAt(T.dir, k) > 0;
                if (!want && !playerVehicle) {
                    const LineStop& st = D.stops[k];
                    if (std::hypot(playerPos.x - st.x, playerPos.z - st.z) < 14) want = true;  // jij staat te wachten
                }
                if (want) { target = k; break; }
            }
        // doelsnelheid
        float vlim = std::min(b.limAt(b.s_), BUS_VMAX);
        float targetV = vlim * b.pace_;
        for (float d = 5; d <= 120; d += 5) {
            float l2 = std::min(b.limAt(b.s_ + d), BUS_VMAX) * b.pace_;
            targetV = std::min(targetV, std::sqrt(l2 * l2 + 2 * DEC * d));
        }
        {
            Vec3 p0 = b.posAt(b.s_), p1 = b.posAt(b.s_ + 4.0f);
            float h0 = std::atan2(p1.x - p0.x, p1.z - p0.z);
            for (int k = 1; k <= 12; k++) {
                float s0 = b.s_ + k * 4.0f;
                Vec3 q0 = b.posAt(s0), q1 = b.posAt(s0 + 4.0f);
                float h1 = std::atan2(q1.x - q0.x, q1.z - q0.z);
                float dh = std::fabs(wrapAngle(h1 - h0));
                h0 = h1;
                if (dh < 0.02f) continue;
                float radius = 4.0f / dh;
                float vt = std::max(2.5f, std::sqrt(LAT_ACC * radius));
                targetV = std::min(targetV, std::sqrt(vt * vt + 2 * DEC * std::max(0.0f, (k - 1) * 4.0f)));
            }
        }
        float stopAt = target >= 0 ? b.stopS(target) : 1e9f;
        if (target >= 0) targetV = std::min(targetV, std::sqrt(2 * 1.1f * std::max(0.0f, stopAt - b.s_)));
        // andere bussen en de speler voor de bus
        Vec3 fwd(std::sin(b.pose_[0].h), 0, std::cos(b.pose_[0].h));
        Vec3 nose = Vec3(b.pose_[0].x, 0, b.pose_[0].z) + fwd * (b.model_->sec[0].length / 2);
        for (auto& op : buses_) {
            const CityBus& o = *op;
            if (&o == &b || o.state_ == CityBus::OFFDUTY) continue;
            for (int c = 0; c < o.carCount(); c++) {
                const CarPose& p = o.pose_[c];
                if (std::sin(p.h) * fwd.x + std::cos(p.h) * fwd.z < 0.3f) continue;  // tegenligger (ook stilstaand: nooit op elkaar wachten)
                Vec3 rel = Vec3(p.x, 0, p.z) - nose;
                float along = dot(rel, fwd), lat = std::fabs(rel.x * fwd.z - rel.z * fwd.x);
                if (along < -2 || along > 40 || lat > 2.4f) continue;
                float gap = along - o.carLen(c) / 2;
                targetV = std::min(targetV, std::max(0.0f, (gap - 3.0f) * 0.6f));
            }
        }
        if (!aboard) {
            Vec3 rel = Vec3(playerPos.x, 0, playerPos.z) - nose;
            float along = dot(rel, fwd), lat = std::fabs(rel.x * fwd.z - rel.z * fwd.x);
            if (along > -1 && along < 12 && lat < 1.8f && std::fabs(playerPos.y - b.pose_[0].y) < 3) targetV = std::min(targetV, std::max(0.0f, along - 2.5f) * 0.5f);
        }
        if (b.v_ < targetV) b.v_ = std::min(targetV, b.v_ + ACC * dt);
        else b.v_ = std::max(targetV, b.v_ - DEC * 2.0f * dt);
        float step = b.v_ * dt;
        if (target >= 0) step = std::min(step, std::max(0.0f, stopAt - b.s_));
        b.s_ += step;
        b.door_ = clampf(b.door_ - dt / 1.5f, 0, 1);
        // haltes voorbij zonder te stoppen
        while (!b.positioning_ && b.next_ < T.last && b.next_ != target && b.s_ > b.stopS(b.next_) + 3) b.next_++;
        // aankomst
        if (target >= 0 && stopAt - b.s_ < 0.3f && b.v_ < 0.6f) {
            b.v_ = 0;
            b.atStop_ = target;
            b.stopReq_ = false;
            if (b.positioning_) {
                b.positioning_ = false;
                b.state_ = CityBus::LAYOVER;
                b.depart_ = T.plan[T.first];
            } else if (target == T.last) {
                b.state_ = CityBus::LAYOVER;
                b.outTimer_ = 0;
                if (aboard && ann_) ann_->say("uitstappen");
            } else {
                b.state_ = CityBus::STOP;
                b.boardQ_ = b.boardServed_ = 0;
                b.depart_ = clock + 5 + (hash32((uint32_t)(T.id * 31 + target)) % 4);
                if (T.timing[target]) b.depart_ = std::max(b.depart_, T.plan[target]);
            }
        }
        // omroep: "volgende halte" kort na vertrek of na het passeren van een halte
        if (aboard && ann_ && !b.positioning_ && b.state_ == CityBus::DRIVE) {
            int ns = b.next_;
            float prevS = ns > T.first ? b.stopS(ns - 1) : -1e9f;
            if (ns <= T.last && b.announced_ != ns && b.s_ > prevS + 25) {
                const LineStop& st = D.stops[ns];
                ann_->say((ns == T.last ? "eind_" : "halte_") + st.clip);
                b.announced_ = ns;
                if (tipTimer_ <= 0 && ns != T.last && (hash32((uint32_t)(T.id * 13 + ns)) % 100) < 30) {
                    int n = 1 + (int)(hash32((uint32_t)(ns * 7 + T.id)) % 6);
                    ann_->say("tip" + std::to_string(n), false);
                    tipTimer_ = 240 + (float)(hash32((uint32_t)T.id) % 240);
                }
            }
        }
        if (b.s_ > b.cum_.back() - 0.5f) { b.s_ = b.cum_.back() - 0.5f; b.v_ = 0; }
        b.computePose();
    }
    // reizigers
    genTimer_ -= dt;
    if (genTimer_ <= 0) {
        genTimer_ = 1.0f;
        generatePax(clock);
    }
    updatePax(dt, clock);
    // bussen die klaar zijn opruimen (niet als de speler erin zit)
    buses_.erase(std::remove_if(buses_.begin(), buses_.end(), [&](const std::unique_ptr<CityBus>& b) {
                     if (playerVehicle == b.get() || b->state_ != CityBus::OFFDUTY || b->outTimer_ < 60) return false;
                     if (!b->parked_ && b->block_ >= 0 && b->block_ < (int)blocks_.size()) blocks_[b->block_].done = true;
                     releasePax(b.get());
                     return true;
                 }),
                 buses_.end());
}

// ============================== reizigers ==============================
namespace {
struct PRng {
    uint32_t s;
    uint32_t next() { s = hash32(s + 0x9E3779B9u); return s; }
    float f() { return (next() & 0xFFFFFF) / 16777216.0f; }
};
int poisson(float mean, PRng& r) {
    float L = std::exp(-mean), p = 1;
    int k = 0;
    do { k++; p *= r.f(); } while (p > L && k < 30);
    return k - 1;
}
void turnTo(float& h, float target, float dt) { h = wrapAngle(h + wrapAngle(target - h) * std::min(1.0f, dt * 9.0f)); }
// lopen in de wereld (x, z); true = aangekomen
bool stepTo(Vec3& p, float& h, Vec3 t, float speed, float dt, float& curSpeed) {
    float dx = t.x - p.x, dz = t.z - p.z, d = std::hypot(dx, dz);
    if (d < 0.04f) return true;
    float s = std::min(d, speed * dt);
    p.x += dx / d * s;
    p.z += dz / d * s;
    turnTo(h, std::atan2(dx, dz), dt);
    curSpeed = speed;
    return d <= speed * dt;
}
// lopen in de bus (lokale coordinaten) langs p.path
bool stepLocal(BusPax& p, float speed, float dt) {
    if (p.path.empty()) return true;
    Vec3 t = p.path.front();
    float dx = t.x - p.lx, dz = t.z - p.lz, d = std::hypot(dx, dz);
    if (d > 0.03f) {
        float s = std::min(d, speed * dt);
        p.lx += dx / d * s;
        p.lz += dz / d * s;
        turnTo(p.lh, std::atan2(dx, dz), dt);
        p.speed = speed;
    }
    if (d <= speed * dt + 0.03f) {
        p.path.erase(p.path.begin());
        return p.path.empty();
    }
    return false;
}
const float STRIDE = 1.35f;  // m per looppas
}  // namespace

void BusFleet::prepareStops() {
    for (LineDir& D : dirs_)
        for (LineStop& st : D.stops) {
            size_t k = (size_t)(std::upper_bound(D.cum.begin(), D.cum.end(), st.s) - D.cum.begin());
            k = std::clamp<size_t>(k, 1, D.pts.size() - 1);
            Vec3 t = normalize(flat(D.pts[k] - D.pts[k - 1]));
            st.tx = t.x;
            st.tz = t.z;
            st.side = st.lat < 0 ? -1.0f : 1.0f;
        }
    waitCnt_.assign(dirs_.size(), {});
    for (size_t d = 0; d < dirs_.size(); d++) waitCnt_[d].assign(dirs_[d].stops.size(), 0);
}

int BusFleet::waitingAt(int dir, int k) const {
    if (dir < 0 || dir >= (int)waitCnt_.size() || k < 0 || k >= (int)waitCnt_[dir].size()) return 0;
    return waitCnt_[dir][k];
}

bool BusFleet::busiestStop(Vec3& pos, Vec3& tangent, Vec3& side) const {
    int best = 0;
    for (size_t d = 0; d < waitCnt_.size(); d++)
        for (size_t k = 0; k < waitCnt_[d].size(); k++)
            if (waitCnt_[d][k] > best) {
                best = waitCnt_[d][k];
                const LineStop& st = dirs_[d].stops[k];
                pos = Vec3(st.x, world_->heightAt(st.x, st.z), st.z);
                tangent = Vec3(st.tx, 0, st.tz);
                side = Vec3(-st.tz * st.side, 0, st.tx * st.side);
            }
    return best > 0;
}

bool BusFleet::paxBusy(const CityBus* b) const {
    for (const BusPax& p : pax_)
        if (p.bus == b && (p.st == BusPax::TO_DOOR || p.st == BusPax::CHECKIN || p.st == BusPax::ALIGHT)) return true;
    return false;
}

void BusFleet::releasePax(CityBus* b) {
    for (BusPax& p : pax_)
        if (p.bus == b && p.st == BusPax::TO_DOOR) { p.st = BusPax::WAIT; p.bus = nullptr; }
    pax_.erase(std::remove_if(pax_.begin(), pax_.end(), [&](const BusPax& p) { return p.bus == b && p.st != BusPax::LEAVE; }), pax_.end());
    for (BusPax& p : pax_) if (p.bus == b) p.bus = nullptr;
}

// Reizigers voor de komende haltes van elke rit (tot 15 min vooruit), met een eigen bestemming
void BusFleet::generatePax(double clock) {
    for (auto& bp : buses_) {
        CityBus& b = *bp;
        if (b.trip_ < 0 || !b.inService()) continue;
        const LineTrip& T = b.trip();
        const LineDir& D = b.dir();
        int k0 = b.positioning_ ? T.first : b.atStop_ >= 0 ? b.atStop_ : b.dutyAt_ >= 0 ? b.dutyAt_ : b.next_;
        for (int k = std::max(k0, T.first); k < T.last; k++) {
            if (T.plan[k] < 0) continue;
            if (T.plan[k] - clock > 900) break;
            uint64_t key = ((uint64_t)T.id << 16) | (uint64_t)k;
            if (!generated_.insert(key).second) continue;
            if (T.plan[k] < clock - 20) continue;
            const LineStop& st = D.stops[k];
            float base = st.place == "Eindhoven" ? 1.0f
                         : (st.place == "Veldhoven" || st.place == "Helmond" || st.place == "'s-Hertogenbosch" || st.place == "Best") ? 0.7f : 0.45f;
            if (k == T.first) base *= 2.2f;
            base *= 1.0f - 0.7f * (float)(k - T.first) / std::max(1, T.last - T.first);
            double hod = std::fmod(T.plan[k], 86400.0) / 3600.0;
            float tod = ((hod >= 7 && hod < 9.5) || (hod >= 16 && hod < 18.5)) ? 1.6f : (hod < 6.5 || hod > 21) ? 0.35f : 1.0f;
            PRng r{hash32((uint32_t)(T.id * 7919 + k * 104729 + 17))};
            int n = poisson(base * tod, r);
            Vec3 t(st.tx, 0, st.tz), rt(-st.tz * st.side, 0, st.tx * st.side);
            float gy = world_->heightAt(st.x, st.z);
            Vec3 P(st.x, gy, st.z);
            for (int j = 0; j < n; j++) {
                BusPax p;
                p.id = paxId_++;
                p.look = (int)(r.next() % (uint32_t)lookCount_);
                p.variant = (int)(r.next() % 4);
                p.dir = T.dir;
                p.from = k;
                // bestemming: verder op de route, vaker een station, centrum of het eindpunt
                std::vector<float> w;
                float tot = 0;
                for (int q = k + 1; q <= T.last; q++) {
                    const std::string& nm = D.stops[q].name;
                    float x = 1.0f + (q == T.last ? 3.0f : 0.0f) + (nm.find("Station") != std::string::npos || nm.find("Centrum") != std::string::npos ? 2.0f : 0.0f);
                    w.push_back(x);
                    tot += x;
                }
                float pick = r.f() * tot;
                p.to = T.last;
                for (size_t q = 0; q < w.size(); q++) {
                    if (pick < w[q]) { p.to = k + 1 + (int)q; break; }
                    pick -= w[q];
                }
                double want = T.plan[k] - 30 - r.f() * 570;
                p.appear = want < clock + 15 ? clock + 15 + r.f() * 25 : want;
                // wachtplek: op de bank (abri / bankje) of op het trottoir achter de paal
                int idx = 0, benchUsed = 0;
                for (const BusPax& q : pax_)
                    if ((q.st == BusPax::WAIT || q.st == BusPax::TO_DOOR) && std::hypot(q.spot.x - st.x, q.spot.z - st.z) < 9) {
                        idx++;
                        if (q.bench) benchUsed++;
                    }
                int benchN = (st.flags & 1) ? 4 : (st.flags & 2) ? 3 : 0;
                if (benchUsed < benchN && r.f() < 0.55f) {
                    p.bench = true;
                    p.spot = (st.flags & 1) ? P + rt * 2.02f - t * 2.4f + t * (-1.1f + 0.73f * benchUsed) : P + rt * 1.42f - t * 1.5f + t * (-0.6f + 0.6f * benchUsed);
                    p.spotH = std::atan2(-rt.x, -rt.z);
                } else {
                    int q = idx - benchUsed;
                    float along = 1.0f - 0.95f * (q % 6) + (r.f() - 0.5f) * 0.4f;
                    float lat = ((st.flags & 1) ? 0.75f : 1.2f) + 0.75f * (q / 6) + r.f() * 0.3f;
                    p.spot = P + t * along + rt * lat;
                    Vec3 f = normalize(t * -0.65f - rt * 0.75f + Vec3(r.f() - 0.5f, 0, r.f() - 0.5f) * 0.6f);
                    p.spotH = std::atan2(f.x, f.z);
                }
                p.spot.y = world_->heightAt(p.spot.x, p.spot.z);
                Vec3 ap = p.spot + rt * 14.0f + t * (r.f() * 16 - 8);
                ap.y = world_->heightAt(ap.x, ap.z);
                p.path = {ap};  // komt hier vandaan aanlopen
                p.pos = p.spot;
                p.h = p.spotH;
                pax_.push_back(p);
            }
        }
    }
}

// Bus die midden in een rit verschijnt: er zitten al reizigers in
void BusFleet::seedOnboard(CityBus& b, double clock) {
    const LineTrip& T = b.trip();
    if (b.next_ > T.last) return;
    double hod = std::fmod(clock, 86400.0) / 3600.0;
    float tod = ((hod >= 7 && hod < 9.5) || (hod >= 16 && hod < 18.5)) ? 1.7f : (hod < 6.5 || hod > 21) ? 0.4f : 1.0f;
    PRng r{hash32((uint32_t)(T.id * 31337 + b.next_))};
    int n = poisson(4.5f * tod * (b.model_->articulated ? 1.6f : 1.0f), r);
    for (int j = 0; j < n; j++) {
        BusPax p;
        p.id = paxId_++;
        p.look = (int)(r.next() % (uint32_t)lookCount_);
        p.variant = (int)(r.next() % 4);
        p.dir = T.dir;
        p.from = std::max(T.first, b.next_ - 1);
        p.to = b.next_ + (int)(r.f() * (T.last - b.next_ + 1));
        p.to = std::min(p.to, T.last);
        p.st = BusPax::RIDE;
        p.bus = &b;
        std::vector<int> freeSeats;
        for (size_t s = 0; s < b.model_->seats.size(); s++) if (!b.seatTaken_[s]) freeSeats.push_back((int)s);
        if (!freeSeats.empty() && r.f() < 0.85f) {
            int s = freeSeats[r.next() % freeSeats.size()];
            b.seatTaken_[s] = 1;
            p.seat = s;
            const SeatInfo& si = b.model_->seats[s];
            p.car = si.car;
            p.lx = si.lx;
            p.lz = si.lz;
            p.lh = si.facing;
        } else {
            p.car = 0;
            const auto& dz = b.model_->sec[0].doorZ;
            float z = dz.size() > 1 ? dz[1] : dz.empty() ? 0.0f : dz[0];
            p.lx = -0.15f + r.f() * 0.4f;
            p.lz = z + (r.f() - 0.5f) * 1.4f;
            p.lh = r.f() * 2 * PI;
        }
        pax_.push_back(p);
    }
}

void BusFleet::updatePax(float dt, double clock) {
    for (auto& v : waitCnt_) std::fill(v.begin(), v.end(), 0);
    for (const BusPax& p : pax_)
        if (p.st == BusPax::WAIT && p.appear <= clock + 2 && p.dir < (int)waitCnt_.size() && p.from < (int)waitCnt_[p.dir].size()) waitCnt_[p.dir][p.from]++;
    auto startAlight = [&](CityBus& b, BusPax& p) {
        if (p.seat >= 0 && p.seat < (int)b.seatTaken_.size()) b.seatTaken_[p.seat] = 0;
        const BusSection& sec = b.model_->sec[p.car];
        float fdz = p.car == 0 ? b.frontDoorZ() : 1e9f;
        float bestZ = sec.doorZ.empty() ? 0.0f : sec.doorZ[0], bd = 1e9f;
        for (float dz : sec.doorZ) {
            if (sec.doorZ.size() > 1 && std::fabs(dz - fdz) < 0.1f) continue;  // voordeur is voor instappers
            if (std::fabs(dz - p.lz) < bd) { bd = std::fabs(dz - p.lz); bestZ = dz; }
        }
        p.path = {Vec3(0, 0, p.lz), Vec3(0, 0, bestZ), Vec3(-BUS_HW + 0.45f, 0, bestZ), Vec3(-BUS_HW - 0.7f, 0, bestZ)};
        p.seat = -1;
        p.st = BusPax::ALIGHT;
    };
    // instappen en uitstappen beginnen bij een bus die met open deuren bij een halte staat
    for (auto& bp : buses_) {
        CityBus& b = *bp;
        if (b.trip_ < 0 || !b.inService()) continue;
        const LineTrip& T = b.trip();
        int k = b.duty_ ? b.dutyAt_ : b.atStop_;
        if (k < 0 || !b.servingStop(k)) continue;
        bool terminal = k == T.last;
        for (BusPax& p : pax_) {
            if (p.st == BusPax::WAIT && !terminal && p.dir == T.dir && p.from == k && p.appear <= clock && p.to <= T.last) {
                p.st = BusPax::TO_DOOR;
                p.bus = &b;
                p.queue = b.boardQ_++;
                p.pos = p.spot;
                p.h = p.spotH;
            } else if (p.st == BusPax::RIDE && p.bus == &b && (p.to == k || terminal)) {
                startAlight(b, p);
            }
        }
    }
    // bewegen
    for (BusPax& p : pax_) {
        p.press = std::max(0.0f, p.press - dt);
        p.speed = 0;
        switch (p.st) {
        case BusPax::WAIT: break;
        case BusPax::TO_DOOR: {
            CityBus& b = *p.bus;
            if (!b.servingStop(p.from)) { p.st = BusPax::WAIT; p.bus = nullptr; break; }
            float fdz = b.frontDoorZ();
            Vec3 door = b.carToWorld(0, -(BUS_HW + 0.5f), 0, fdz);
            CarPose cp = b.carPose(0);
            Vec3 back(-std::sin(cp.h), 0, -std::cos(cp.h)), right(-std::cos(cp.h), 0, std::sin(cp.h));
            int ahead = p.queue - b.boardServed_;
            Vec3 tgt = door + back * (0.75f * std::max(0, ahead)) + right * (ahead > 0 ? 0.25f : 0.0f);
            bool arrived = stepTo(p.pos, p.h, tgt, 1.3f, dt, p.speed);
            if (arrived && ahead <= 0) {
                p.st = BusPax::CHECKIN;
                p.car = 0;
                p.lx = -BUS_HW + 0.30f;
                p.lz = fdz;
                p.lh = PI / 2;
                p.path = {Vec3(-BUS_HW + 0.85f, 0, fdz - 0.30f)};
                p.timer = 1.1f;
            }
            break;
        }
        case BusPax::CHECKIN: {
            CityBus& b = *p.bus;
            if (!p.path.empty()) { stepLocal(p, 0.9f, dt); break; }
            turnTo(p.lh, PI * 0.75f, dt);  // naar de kaartlezer bij de chauffeur
            p.timer -= dt;
            if (p.timer <= 0) {
                b.boardServed_++;
                if (b.duty_) duty.paxIn++;
                PRng r{hash32(p.id * 977u)};
                std::vector<int> freeSeats;
                for (size_t s = 0; s < b.model_->seats.size(); s++)
                    if (!b.seatTaken_[s] && b.model_->seats[s].car == 0) freeSeats.push_back((int)s);
                p.path = {Vec3(0, 0, p.lz - 0.4f)};
                if (!freeSeats.empty() && r.f() < 0.9f) {
                    int s = freeSeats[r.next() % freeSeats.size()];
                    b.seatTaken_[s] = 1;
                    p.seat = s;
                    const SeatInfo& si = b.model_->seats[s];
                    p.path.push_back(Vec3(0, 0, si.lz));
                    p.path.push_back(Vec3(si.lx, 0, si.lz));
                } else {
                    const auto& dz = b.model_->sec[0].doorZ;
                    float z = dz.size() > 1 ? dz[1] : p.lz - 2.0f;
                    p.path.push_back(Vec3(0, 0, z + (r.f() - 0.5f) * 1.2f));
                    p.path.push_back(Vec3(-0.25f + r.f() * 0.45f, 0, z + (r.f() - 0.5f) * 1.2f));
                }
                p.st = BusPax::TO_SEAT;
            }
            break;
        }
        case BusPax::TO_SEAT:
            if (stepLocal(p, 1.0f, dt)) {
                p.st = BusPax::RIDE;
                if (p.seat >= 0) p.lh = p.bus->model_->seats[p.seat].facing;
            }
            break;
        case BusPax::RIDE: {
            CityBus& b = *p.bus;
            if (!b.inService() || b.trip_ < 0) break;
            const LineTrip& T = b.trip();
            bool driving = b.duty_ ? b.dutyAt_ < 0 : (b.state_ == CityBus::DRIVE && !b.positioning_);
            if (driving && b.next_ == p.to && p.to != T.last && !b.stopReq_ && b.s_ > b.stopS(p.to) - 330) {
                b.requestStop();
                p.press = 1.3f;
            }
            break;
        }
        case BusPax::ALIGHT: {
            CityBus& b = *p.bus;
            bool open = b.door_ > 0.8f && std::fabs(b.v_) < 0.3f;
            if (p.path.size() == 1 && !open) break;  // wachten bij de deur
            if (stepLocal(p, 1.1f, dt)) {
                Vec3 w = b.carToWorld(p.car, p.lx, 0, p.lz);
                CarPose cp = b.carPose(p.car);
                Vec3 fwd(std::sin(cp.h), 0, std::cos(cp.h)), right(-std::cos(cp.h), 0, std::sin(cp.h));
                PRng r{hash32(p.id * 31u + 7)};
                Vec3 p1 = w + right * 3.5f + fwd * ((r.f() - 0.5f) * 3);
                Vec3 p2 = p1 + fwd * ((r.f() < 0.5f ? -1.0f : 1.0f) * (8 + r.f() * 8)) + right * 2.0f;
                p.pos = Vec3(w.x, world_->heightAt(w.x, w.z), w.z);
                p.h = cp.h + p.lh;
                p.path = {p1, p2};
                p.st = BusPax::LEAVE;
                p.bus = nullptr;
                if (b.duty_) duty.paxOut++;
            }
            break;
        }
        case BusPax::LEAVE:
            if (!p.path.empty() && stepTo(p.pos, p.h, p.path.front(), 1.25f, dt, p.speed)) p.path.erase(p.path.begin());
            p.pos.y = world_->heightAt(p.pos.x, p.pos.z);
            break;
        }
        p.phase = std::fmod(p.phase + p.speed * dt / STRIDE, 1.0f);
    }
    pax_.erase(std::remove_if(pax_.begin(), pax_.end(), [&](const BusPax& p) {
                   return (p.st == BusPax::LEAVE && p.path.empty()) || (p.st == BusPax::WAIT && clock - p.appear > 2400);
               }),
               pax_.end());
}

void BusFleet::collectPeople(std::vector<PersonDraw>& out, Vec3 cam, double clock) const {
    const float VIEW_R = 110.0f;
    for (const BusPax& p : pax_) {
        PersonDraw d;
        d.look = p.look;
        d.anim.t = (float)std::fmod(clock, 1000.0) + (float)(p.id % 50);
        d.anim.variant = p.variant;
        d.anim.press = std::min(1.0f, p.press * 2.0f);
        auto walkAnim = [&](float speed) {
            if (speed > 0.05f) { d.anim.kind = ANIM_WALK; d.anim.speed = speed; d.anim.phase = p.phase; }
            else d.anim.kind = p.variant == 2 ? ANIM_PHONE : ANIM_IDLE;
        };
        switch (p.st) {
        case BusPax::WAIT: {
            if (clock < p.appear - 15) continue;
            Vec3 pos = p.spot;
            float h = p.spotH;
            if (clock < p.appear && !p.path.empty()) {
                float f = clampf(1.0f - (float)(p.appear - clock) / 15.0f, 0, 1);
                Vec3 a = p.path[0];
                pos = a + (p.spot - a) * f;
                h = std::atan2(p.spot.x - a.x, p.spot.z - a.z);
                d.anim.kind = ANIM_WALK;
                d.anim.speed = 1.2f;
                d.anim.phase = std::fmod(f * length(p.spot - a) / STRIDE, 1.0f);
            } else if (p.bench) {
                d.sitting = true;
                d.anim.kind = p.variant == 2 ? ANIM_SIT_PHONE : ANIM_SIT;
                pos = p.spot + Vec3(std::sin(h), 0, std::cos(h)) * -0.10f + Vec3(0, 0.45f + 0.09f, 0);
            } else {
                d.anim.kind = p.variant == 2 ? ANIM_PHONE : ANIM_IDLE;
                d.anim.headYaw = 0.5f * std::sin(d.anim.t * 0.21f + p.id);
            }
            if (std::hypot(pos.x - cam.x, pos.z - cam.z) > VIEW_R) continue;
            if (!p.bench || clock < p.appear) pos.y = world_->heightAt(pos.x, pos.z);
            d.pos = pos;
            d.h = h;
            break;
        }
        case BusPax::TO_DOOR:
        case BusPax::LEAVE:
            if (std::hypot(p.pos.x - cam.x, p.pos.z - cam.z) > VIEW_R) continue;
            d.pos = Vec3(p.pos.x, world_->heightAt(p.pos.x, p.pos.z), p.pos.z);
            d.h = p.h;
            walkAnim(p.speed);
            break;
        default: {
            const CityBus* b = p.bus;
            if (!b) continue;
            CarPose cp = b->carPose(p.car);
            if (std::hypot(cp.x - cam.x, cp.z - cam.z) > 60) continue;
            d.interior = true;
            if (p.st == BusPax::RIDE && p.seat >= 0) {
                const SeatInfo& si = b->model_->seats[p.seat];
                float fy = si.y >= 0 ? si.y : BUS_FLOOR;
                float hx = si.lx - std::sin(si.facing) * 0.10f, hz = si.lz - std::cos(si.facing) * 0.10f;
                d.pos = b->carToWorld(si.car, hx, fy + 0.45f + 0.09f, hz);
                d.h = cp.h + si.facing;
                d.sitting = true;
                d.anim.kind = p.variant == 2 ? ANIM_SIT_PHONE : ANIM_SIT;
            } else {
                d.pos = b->carToWorld(p.car, p.lx, BUS_FLOOR, p.lz);
                d.h = cp.h + p.lh;
                walkAnim(p.speed);
                if (p.st == BusPax::CHECKIN && p.path.empty()) d.anim.kind = ANIM_IDLE;
            }
            break;
        }
        }
        out.push_back(d);
    }
}

// ============================== dienst rijden (bus) ==============================
CityBus* BusFleet::startDuty(int tripIdx, double clock) {
    if (tripIdx < 0 || tripIdx >= (int)trips_.size()) return nullptr;
    clock_ = clock;
    int bi = -1, pos = 0;
    for (size_t b = 0; b < blocks_.size() && bi < 0; b++)
        for (size_t j = 0; j < blocks_[b].trips.size(); j++)
            if (blocks_[b].trips[j] == tripIdx) { bi = (int)b; pos = (int)j; break; }
    if (bi >= 0) blocks_[bi].spawned = true;  // deze omloop rijdt de speler
    auto bus = std::make_unique<CityBus>();
    CityBus& b = *bus;
    b.id = nextId_++;
    b.fleet_ = this;
    const LineTrip& T = trips_[tripIdx];
    b.model_ = lines_[dirs_[T.dir].line].articulated ? &citea18_ : &citea12_;
    b.block_ = bi;
    b.blockPos_ = pos;
    b.startTrip(tripIdx, false, false);
    b.duty_ = true;
    b.state_ = CityBus::DRIVE;
    b.atStop_ = -1;
    b.dutyAt_ = -1;
    b.door_ = 0;
    b.next_ = T.first;
    b.welcomed_ = false;
    b.announced_ = T.first;
    b.computePose();
    duty = DutyStats();
    duty.active = true;
    auto hm = [](double t) { char s[16]; t = std::fmod(t, 86400.0); snprintf(s, sizeof s, "%02d:%02d", (int)(t / 3600), (int)std::fmod(t / 60, 60)); return std::string(s); };
    duty.title = "Lijn " + dirs_[T.dir].ref + " naar " + b.destination() + ", vertrek " + hm(T.plan[T.first]) + " van " + dirs_[T.dir].stops[T.first].name;
    duty.note("Dienst begonnen: " + duty.title + ". Rijd naar de haltes en open de deuren (Q).");
    buses_.push_back(std::move(bus));
    return &b;
}

void BusFleet::updateDuty(CityBus& b, float dt, double clock, bool aboard) {
    b.updateManual(dt, aboard);
    if (b.trip_ < 0) return;
    const LineTrip& T = b.trip();
    const LineDir& D = b.dir();
    b.s_ = b.projectS(b.mF_, b.s_);
    float fv = std::fabs(b.v_);
    // halte bedienen: stilstaan met open deuren bij een halte
    if (b.dutyAt_ < 0 && fv < 0.3f && b.door_ > 0.8f) {
        for (int k = std::max(T.first, b.next_ - 1); k <= std::min(T.last, b.next_ + 1); k++)
            if (std::fabs(b.s_ - b.stopS(k)) < 14.0f) {
                b.dutyAt_ = k;
                b.boardQ_ = b.boardServed_ = 0;
                if (k >= b.next_) b.next_ = k;
                if (b.stopReq_ && k == b.next_) b.stopReq_ = false;
                if (k == T.last) duty.note("Eindhalte " + D.stops[k].name + ": iedereen stapt uit.");
                break;
            }
    }
    // vertrokken
    if (b.dutyAt_ >= 0 && b.door_ < 0.05f && fv > 0.5f) {
        int k = b.dutyAt_;
        if (k != T.last) duty.departure(D.stops[k].name, clock - T.plan[k]);
        if (k == T.first && aboard && ann_ && !b.welcomed_) ann_->say("welkom_" + slug(D.ref == "18" ? b.destination() : D.ref + " " + b.destination()));
        b.welcomed_ = true;
        b.next_ = std::max(b.next_, k + 1);
        b.dutyAt_ = -1;
    }
    // haltes voorbij gereden
    while (b.dutyAt_ < 0 && b.next_ <= T.last && b.s_ > b.stopS(b.next_) + 15.0f) {
        int k = b.next_;
        int waiting = waitingAt(T.dir, k);
        bool needed = b.stopReq_ || waiting > 0 || k == T.last;
        if (needed) {
            duty.missed++;
            std::string why = b.stopReq_ ? "er was op de stopknop gedrukt" : waiting > 0 ? std::to_string(waiting) + " reiziger(s) bleven staan" : "eindhalte";
            duty.note("Halte " + D.stops[k].name + " overgeslagen (" + why + ")", true);
            for (BusPax& p : pax_)
                if (p.bus == &b && p.st == BusPax::RIDE && p.to == k) p.to = std::min(k + 1, T.last);
        }
        if (k == T.first) b.welcomed_ = true;
        b.stopReq_ = false;
        b.next_++;
    }
    // eindhalte: klaar als iedereen eruit is
    if (b.dutyAt_ == T.last && !duty.finished && !paxBusy(&b) && b.passengers() == 0) {
        duty.finished = true;
        char buf[200];
        snprintf(buf, sizeof buf, "Rit voltooid! %d haltes bediend, %d op tijd, %d te laat, %d te vroeg, %d overgeslagen. %d in- en %d uitgestapt.",
                 duty.served, duty.onTime, duty.late, duty.early, duty.missed, duty.paxIn, duty.paxOut);
        duty.note(buf);
    }
    // omroep: volgende halte
    if (aboard && ann_ && b.dutyAt_ < 0 && b.welcomed_) {
        int ns = b.next_;
        float prevS = ns > T.first ? b.stopS(ns - 1) : -1e9f;
        if (ns <= T.last && b.announced_ != ns && b.s_ > prevS + 25) {
            ann_->say((ns == T.last ? "eind_" : "halte_") + D.stops[ns].clip);
            b.announced_ = ns;
        }
    }
}

void BusFleet::addCollision(Collision& col, Vec3 p) const {
    for (const auto& bp : buses_) {
        const CityBus& b = *bp;
        if (std::hypot(b.pose_[0].x - p.x, b.pose_[0].z - p.z) > 40) continue;
        for (int c = 0; c < b.carCount(); c++) {
            const CarPose& cp = b.pose_[c];
            const BusSection& s = b.model_->sec[c];
            float ax = std::sin(cp.h), az = std::cos(cp.h);
            float L = s.length, y0 = cp.y + 0.3f, y1 = cp.y + 3.0f;
            auto box = [&](float lx, float lz, float hx, float hz) {
                Vec3 q = b.carToWorld(c, lx, 0, lz);
                col.addDynamicBox({q.x, q.z, ax, az, hz, hx, y0, y1});
            };
            for (int side = -1; side <= 1; side += 2) {
                bool open = side < 0 && b.door_ > 0.8f;
                std::vector<float> cuts{-L / 2, L / 2};
                if (open)
                    for (float d : s.doorZ) { cuts.push_back(d - BUS_DOOR_W / 2 + 0.05f); cuts.push_back(d + BUS_DOOR_W / 2 - 0.05f); }
                std::sort(cuts.begin(), cuts.end());
                for (size_t k = 0; k + 1 < cuts.size(); k++) {
                    float a = cuts[k], e = cuts[k + 1], m = (a + e) / 2;
                    bool gap = false;
                    if (open) for (float d : s.doorZ) if (std::fabs(m - d) < BUS_DOOR_W / 2) gap = true;
                    if (!gap && e > a) box(side * (BUS_HW - 0.06f), m, 0.08f, (e - a) / 2);
                }
            }
            bool jointFront = b.model_->articulated && c == 1, jointRear = b.model_->articulated && c == 0;
            if (!jointFront) box(0, L / 2 - 0.05f, BUS_HW, 0.1f);
            if (!jointRear) box(0, -L / 2 + 0.05f, BUS_HW, 0.1f);
            Vec3 q = b.carToWorld(c, 0, 0, 0);
            col.addDynamicFloor({q.x, q.z, ax, az, L / 2 + (b.model_->articulated ? 0.4f : 0.0f), BUS_HW + 0.3f, cp.y + BUS_FLOOR, cp.y + BUS_FLOOR, 0});
        }
    }
}

void BusFleet::drawOpaque(World& world, Vec3 cam, double clock) const {
    world.setMode(MODE_VEHICLE);
    world.setModel(Mat4::identity());
    drawMesh(stopBody_);
    for (const auto& bp : buses_) {
        const CityBus& b = *bp;
        if (std::hypot(b.pose_[0].x - cam.x, b.pose_[0].z - cam.z) > 1600) continue;
        for (int c = 0; c < b.carCount(); c++) {
            const CarPose& p = b.pose_[c];
            Mat4 m = Mat4::pose(p.x, p.y, p.z, p.h);
            world.setModel(m);
            drawMesh(b.model_->sec[c].body);
            if (c == 0) {
                if (!b.driverAboard_) drawMesh(b.model_->driver);
                const Vec3& wp = b.model_->wheelPos;
                world.setModel(m * Mat4::translation(wp.x, wp.y, wp.z) * rotX(-0.45f) * rotZ(-b.steer_ * 7.0f));
                drawMesh(b.model_->wheel);
            }
            float open = b.door_;
            float out = std::min(1.0f, open * 4.0f) * 0.09f;
            float slide = std::min(1.0f, open * 1.1f) * (BUS_DOOR_W / 2 - 0.08f);
            for (float d : b.model_->sec[c].doorZ)
                for (int leaf = -1; leaf <= 1; leaf += 2) {
                    world.setModel(m * Mat4::translation(-BUS_HW - 0.01f - out, 0, d + leaf * (BUS_DOOR_W / 4 + slide)));
                    drawMesh(b.model_->doorLeaf);
                }
        }
        if (b.model_->articulated) {
            const CarPose &f = b.pose_[0], &r = b.pose_[1];
            Vec3 J = b.carToWorld(0, 0, 0, -b.model_->sec[0].length / 2 - b.model_->jointGap / 2);
            float h = std::atan2(std::sin(f.h) + std::sin(r.h), std::cos(f.h) + std::cos(r.h));
            world.setModel(Mat4::pose(J.x, (f.y + r.y) / 2, J.z, h));
            drawMesh(b.model_->bellows);
        }
    }
    world.setMode(MODE_OBJECT);
    world.setModel(Mat4::identity());
    bindAtlas(world);  // haltenamen, frontborden en schermen staan in de eigen atlas van de bussen
    drawMesh(stopSigns_);
    for (const auto& bp : buses_) {
        const CityBus& b = *bp;
        float dist = std::hypot(b.pose_[0].x - cam.x, b.pose_[0].z - cam.z);
        if (dist > 600) continue;
        const CarPose& p = b.pose_[0];
        Mat4 m0 = Mat4::pose(p.x, p.y, p.z, p.h);
        world.setModel(m0 * Mat4::translation(0, 0, b.model_->sec[0].length / 2 + 0.012f));
        drawMesh(textMesh_[textIndex(b.display())]);
        if (dist > 30) continue;
        // ---- informatieschermen: lijn + bestemming, volgende 5 haltes met minuten, "STOPT" ----
        bool service = b.inService();
        int startIdx = -1, endIdx = -1;
        double delay = 0;
        if (service) {
            const LineTrip& T = b.trip();
            startIdx = (b.state_ == CityBus::STOP || b.state_ == CityBus::LAYOVER) && b.atStop_ >= 0 ? b.atStop_ + 1 : b.next_;
            if (b.positioning_) startIdx = T.first;
            endIdx = T.last;
            delay = b.delay(clock);
        }
        int hdr = textIndex(b.display());
        for (const BusModel::Spot& sp : b.model_->screens) {
            const CarPose& cp = b.pose_[sp.sec];
            Mat4 m = Mat4::pose(cp.x, cp.y, cp.z, cp.h) * Mat4::translation(sp.x, sp.y, sp.z - 0.012f);
            world.setModel(m * Mat4::translation(0.19f, 0.095f, 0));
            drawMesh(headers_[hdr].mesh);
            if (!service) continue;
            const LineTrip& T = b.trip();
            const LineDir& D = b.dir();
            for (int i = 0; i < 5; i++) {
                int k = startIdx + i;
                if (k > endIdx || k < 0) break;
                int ni = screenIndex(D.stops[k].name);
                if (ni < 0) continue;
                float y = 0.054f - i * 0.033f;
                world.setModel(m * Mat4::translation(0.19f, y, 0));
                drawMesh(i == 0 ? nameYellow_[ni].mesh : nameWhite_[ni].mesh);
                double eta = T.plan[k] + delay - clock;
                int mins = eta < 30 ? 0 : std::min(60, (int)std::ceil(eta / 60.0));
                world.setModel(m * Mat4::translation(-0.19f, y, 0));
                drawMesh(minutes_[mins].mesh);
            }
            if (b.stopReq_) {
                world.setModel(m * Mat4::translation(stopBanner_.w / 2, -0.103f, -0.001f));
                drawMesh(stopBanner_.mesh);
            }
        }
    }
    world.setModel(Mat4::identity());
    world.bindSignTexture(0);  // terug naar de gedeelde atlas
}

void BusFleet::drawGlass(World& world, Vec3 cam) const {
    world.setModel(Mat4::identity());
    drawMesh(stopGlass_);
    for (const auto& bp : buses_) {
        const CityBus& b = *bp;
        if (std::hypot(b.pose_[0].x - cam.x, b.pose_[0].z - cam.z) > 1600) continue;
        for (int c = 0; c < b.carCount(); c++) {
            const CarPose& p = b.pose_[c];
            world.setModel(Mat4::pose(p.x, p.y, p.z, p.h));
            drawMesh(b.model_->sec[c].glass);
        }
    }
    world.setModel(Mat4::identity());
}

// Halte in de buurt: naam en de eerstvolgende vertrekken
std::string BusFleet::stopInfo(Vec3 pos, double clock, std::vector<std::string>& lines) const {
    lines.clear();
    std::string name;
    float best = 25;
    for (size_t d = 0; d < dirs_.size(); d++)
        for (const LineStop& st : dirs_[d].stops) {
            float dd = std::hypot(pos.x - st.x, pos.z - st.z);
            if (dd < best) { best = dd; name = st.name; }
        }
    if (name.empty()) return "";
    struct Dep { double t; int dir; std::string dest; };
    std::vector<Dep> deps;
    for (const LineTrip& T : trips_) {
        const LineDir& D = dirs_[T.dir];
        for (int k = T.first; k < T.last; k++)
            if (D.stops[k].name == name && std::hypot(pos.x - D.stops[k].x, pos.z - D.stops[k].z) < 40 && T.plan[k] >= clock - 30)
                deps.push_back({T.plan[k], T.dir, T.dest.empty() ? D.dest : T.dest});
    }
    std::sort(deps.begin(), deps.end(), [](const Dep& a, const Dep& b) { return a.t < b.t; });
    for (size_t i = 0; i < deps.size() && i < 3; i++) {
        double t = std::fmod(deps[i].t, 86400.0);
        int mins = (int)std::max(0.0, std::ceil((deps[i].t - clock) / 60.0));
        char buf[160];
        snprintf(buf, sizeof buf, "%02d:%02d  %s %s  (over %d min)", (int)(t / 3600), (int)std::fmod(t / 60, 60), dirs_[deps[i].dir].ref.c_str(),
                 deps[i].dest.c_str(), mins);
        lines.push_back(buf);
    }
    return name;
}

extern int g_signAtlasFull;
std::string BusFleet::debugInfo(double clock) const {
    std::string s;
    char buf[256];
    int waiting = 0;
    for (const BusPax& p : pax_) if (p.st == BusPax::WAIT && p.appear <= clock) waiting++;
    snprintf(buf, sizeof buf, "klok %.0f  bussen %zu  ritten %zu  omlopen %zu  reizigers %zu (wachtend %d)  atlas vol %d\n", clock, buses_.size(), trips_.size(), blocks_.size(),
             pax_.size(), waiting, g_signAtlasFull);
    s += buf;
    static bool blocksPrinted = false;
    if (!blocksPrinted) {
        blocksPrinted = true;
        for (size_t bi = 0; bi < blocks_.size(); bi++) {
            s += "omloop " + std::to_string(bi) + ":";
            for (int ti : blocks_[bi].trips) {
                const LineTrip& T = trips_[ti];
                auto hm = [](double t) { char b[16]; t = std::fmod(t, 86400.0); snprintf(b, sizeof b, "%02d:%02d", (int)(t / 3600), (int)std::fmod(t / 60, 60)); return std::string(b); };
                s += "  [" + std::to_string(ti) + "] " + dirs_[T.dir].stops[T.first].name + " " + hm(T.plan[T.first]) + " -> " + dirs_[T.dir].stops[T.last].name + " " + hm(T.plan[T.last]);
            }
            s += "\n";
        }
    }
    for (const auto& bp : buses_) {
        const CityBus& b = *bp;
        double late = 0;
        if (b.trip_ >= 0) {
            const LineTrip& T = b.trip();
            if (b.state_ == CityBus::STOP && b.atStop_ >= 0) late = clock - T.plan[b.atStop_];
        }
        snprintf(buf, sizeof buf, "bus %d omloop %d rit %d toestand %d s=%.0f v=%.1f volgende=%s halte=%s tempo=%.2f te laat %.0f s pos %.0f,%.0f reizigers %d%s\n", b.id, b.block_,
                 b.trip_, (int)b.state_, b.s_, b.v_ * 3.6f, b.nextStop().c_str(), b.currentStop().c_str(), b.pace_, late, b.pose_[0].x, b.pose_[0].z, b.passengers(),
                 b.stopReq_ ? " STOP" : "");
        s += buf;
    }
    return s;
}
