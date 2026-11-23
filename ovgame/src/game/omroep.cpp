// Treinomroep: zie omroep.h
#include "omroep.h"
#include <algorithm>
#include <cmath>
#include "traffic.h"
#include "train.h"
#include "world.h"

namespace {
const std::string DIR = "audio/trein/";

// zelfde regels als tools/make_train_announcements.py
std::string slug(const std::string& s) {
    std::string o;
    for (char ch : s) {
        unsigned char c = (unsigned char)std::tolower((unsigned char)ch);
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        char x = ok ? (char)c : '_';
        if (x == '_' && !o.empty() && o.back() == '_') continue;
        o += x;
    }
    while (!o.empty() && o.front() == '_') o.erase(o.begin());
    while (!o.empty() && o.back() == '_') o.pop_back();
    return o;
}
std::string st(const std::string& name) { return DIR + "st_" + slug(name); }
std::string frag(const std::string& f) { return DIR + f; }

// "de Intercity naar Alkmaar"
void trainName(std::vector<std::string>& p, const std::string& product, const std::string& dest) {
    p.push_back(frag(product == "Intercity" ? "de_intercity_naar" : "de_sprinter_naar"));
    p.push_back(st(dest));
}
// "Deze trein stopt in A, B en C"
void stopsIn(std::vector<std::string>& p, const std::vector<std::string>& via, const std::string& dest) {
    std::vector<std::string> list(via.begin(), via.begin() + std::min<size_t>(3, via.size()));
    list.push_back(dest);
    if (list.size() < 2) return;
    p.push_back(frag("deze_trein_stopt_in"));
    for (size_t i = 0; i < list.size(); i++) {
        if (i + 1 == list.size()) p.push_back(frag("en"));
        p.push_back(st(list[i]));
    }
}
}  // namespace

void Omroep::update(float dt, const Traffic& traffic, const World& world, const Train* ride, Vec3 player, double clock) {
    station.update(dt);
    train.update(dt);

    // ---------------- in de trein ----------------
    if (ride != ride_) {  // ingestapt / uitgestapt / overgestapt
        ride_ = ride;
        rideAt_ = ride ? ride->lastStopIndex() : -2;
        rideStopped_ = ride && ride->stopped();
        welcomed_ = false;
        train.stop();
    }
    if (ride) {
        const Service& s = ride->service();
        int at = ride->lastStopIndex();
        bool stopped = ride->stopped();
        // net vertrokken van een halte: welkom (eerste keer) en de volgende halte
        if (rideStopped_ && !stopped && at >= 0) {
            int next = -1;
            for (int k = at + 1; k < (int)s.stops.size(); k++)
                if (s.stops[k].stop) { next = k; break; }
            std::vector<std::string> p;
            bool gong = true;  // na het welkom geen tweede gong
            if (!welcomed_) {
                p.push_back(frag(s.product() == "Intercity" ? "welkom_intercity" : "welkom_sprinter"));
                p.push_back(st(s.dest));
                welcomed_ = true;
                train.sayParts(p, true, 12.0f);
                p.clear();
                gong = false;
            }
            if (next >= 0) {
                p.push_back(frag("volgende_station"));
                p.push_back(st(s.stops[next].station));
                if (s.stops[next].station == s.dest) p.push_back(frag("eindstation_volgend"));
                train.sayParts(p, gong, 15.0f);
            }
        }
        // aangekomen op het eindstation
        if (!rideStopped_ && stopped && at >= 0 && at < (int)s.stops.size() && s.stops[at].station == s.dest)
            train.sayParts({frag("eindstation_uitstappen")}, true, 10.0f);
        rideStopped_ = stopped;
        rideAt_ = at;
    }

    // ---------------- op het station ----------------
    station.setGain(ride ? 0.0f : 1.0f);  // in de trein hoor je de perronomroep niet
    checkT_ -= dt;
    if (checkT_ > 0) return;
    checkT_ = 1.0f;
    // dichtstbijzijnde station binnen 250 m
    std::string here;
    float best = 250.0f;
    for (const MapPoint& m : world.points)
        if (m.kind == WorldFormat::POINT_STATION) {
            float d = std::hypot(m.x - player.x, m.z - player.z);
            if (d < best) { best = d; here = m.name; }
        }
    if (here != here_) {
        here_ = here;
        station.stop();
    }
    if (here.empty() || ride) return;
    for (const Departure& d : traffic.departures(here, 8, 1800)) {
        std::string id = std::to_string(d.serviceId) + "|";
        double toDep = d.time + d.delay - clock;   // verwacht vertrek
        double toArr = d.arr + d.delay - clock;    // verwachte aankomst
        // vertraging: een keer, als hij nog ruim 3 minuten weg is
        if (d.delay >= 120 && toDep > 180 && toDep < 1200 && !said_.count(id + "vertraging|" + here)) {
            said_.insert(id + "vertraging|" + here);
            std::vector<std::string> p{frag("let_op")};
            trainName(p, d.product, d.dest);
            double t = std::fmod(d.time, 86400.0);
            int h = (int)(t / 3600), m = (int)std::fmod(t / 60, 60);
            p.push_back(frag("van"));
            p.push_back(frag("h_" + std::to_string(h)));
            if (m > 0) p.push_back(frag("m_" + std::to_string(m)));
            p.push_back(frag("heeft_vertraging"));
            p.push_back(frag("min_" + std::to_string(std::min(60, (int)std::lround(d.delay / 60)))));
            station.sayParts(p, true, 20.0f);
        }
        if (d.track <= 0) continue;
        // begint hier: staat klaar
        if (d.startsHere && toDep < 240 && toDep > 30 && !said_.count(id + "klaar|" + here)) {
            said_.insert(id + "klaar|" + here);
            std::vector<std::string> p;
            trainName(p, d.product, d.dest);
            p.push_back(frag("staat_klaar_op_spoor"));
            p.push_back(frag("sp_" + std::to_string(d.track)));
            stopsIn(p, d.via, d.dest);
            station.sayParts(p, true, 20.0f);
        }
        // komt binnen: ongeveer anderhalve minuut voor aankomst, zolang hij er nog niet staat
        if (!d.startsHere && toArr < 100 && toArr > 10 && !(d.train && d.train->stopped()) && !said_.count(id + "binnen|" + here)) {
            said_.insert(id + "binnen|" + here);
            std::vector<std::string> p{frag("op_spoor"), frag("sp_" + std::to_string(d.track)), frag("komt_binnen")};
            trainName(p, d.product, d.dest);
            stopsIn(p, d.via, d.dest);
            station.sayParts(p, true, 15.0f);
        }
    }
    if (said_.size() > 4000) said_.clear();
}
