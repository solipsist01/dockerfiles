// Multiplayer: zie netplay.h. Berichten: tekst, velden gescheiden door tabs (zie server/hub.go).
#include "netplay.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static const int PROTOCOL = 1;

static std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        size_t b = s.find('\t', a);
        if (b == std::string::npos) { out.push_back(s.substr(a)); break; }
        out.push_back(s.substr(a, b - a));
        a = b + 1;
    }
    return out;
}

static std::string clean(std::string s) {
    for (char& c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

// De game rekent in Latin-1 (zie de tekstrenderer); over de lijn gaat UTF-8.
static std::string toUtf8(const std::string& latin1) {
    std::string o;
    for (unsigned char c : latin1) {
        if (c < 0x80) o += (char)c;
        else { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
    }
    return o;
}
static std::string fromUtf8(const std::string& u) {
    std::string o;
    for (size_t i = 0; i < u.size(); i++) {
        unsigned char c = (unsigned char)u[i];
        if (c < 0x80) o += (char)c;
        else if ((c & 0xE0) == 0xC0 && i + 1 < u.size()) { int cp = ((c & 0x1F) << 6) | (u[i + 1] & 0x3F); o += cp < 256 ? (char)cp : '?'; i++; }
        else { o += '?'; while (i + 1 < u.size() && ((unsigned char)u[i + 1] & 0xC0) == 0x80) i++; }
    }
    return o;
}

NetPlay::~NetPlay() { disconnect(); }

bool NetPlay::connect(const std::string& baseUrl, const std::string& password, const Appearance& me, std::string& error) {
    disconnect();
    lostReason.clear();
    std::string base = Net::normalizeBase(baseUrl);
    if (base.empty()) { error = "Geen serveradres"; return false; }
    if (!ws_.connect(base + "/ws", error)) return false;
    if (!ws_.send(toUtf8("hello\t" + std::to_string(PROTOCOL) + "\t" + clean(password) + "\t" + clean(me.serialize())))) {
        error = "Versturen mislukt";
        ws_.close();
        return false;
    }
    // wachten op welcome / deny
    DWORD t0 = GetTickCount();
    std::string m;
    while (GetTickCount() - t0 < 15000) {
        if (ws_.poll(m)) {
            std::vector<std::string> f = split(fromUtf8(m));
            if (f[0] == "deny") {
                error = f.size() > 1 ? f[1] : "Geweigerd door de server";
                ws_.close();
                return false;
            }
            if (f[0] == "welcome" && f.size() >= 6) {
                myId = atoi(f[1].c_str());
                clock_ = atof(f[2].c_str());
                weekday_ = atoi(f[3].c_str());
                clockAt_ = -1;  // tijdstip vult update() in (Platform::time)
                serverName = f[4];
                motd = f[5];
                connected_ = true;
                return true;
            }
        } else if (!ws_.open()) {
            error = ws_.closeReason();
            if (error.empty()) error = "Verbinding verbroken";
            return false;
        } else {
            Sleep(10);
        }
    }
    error = "De server antwoordt niet";
    ws_.close();
    return false;
}

void NetPlay::disconnect() {
    ws_.close();
    connected_ = false;
    for (auto& kv : players_) destroyCharacter(kv.second.model);
    players_.clear();
    clockAt_ = 0;
}

double NetPlay::serverClock(double now) const {
    if (clockAt_ <= 0) return clock_;
    return std::fmod(clock_ + (now - clockAt_), 86400.0);
}

void NetPlay::addChat(const std::string& name, const std::string& text, bool system, double now) {
    chat_.push_back({name, text, system, now});
    if (chat_.size() > 50) chat_.erase(chat_.begin());
}

void NetPlay::handle(const std::string& raw, double now) {
    std::vector<std::string> f = split(fromUtf8(raw));
    const std::string& t = f[0];
    if (t == "st" && f.size() >= 18) {
        auto it = players_.find(atoi(f[1].c_str()));
        if (it == players_.end()) return;
        NetPose p;
        p.pos = Vec3((float)atof(f[2].c_str()), (float)atof(f[3].c_str()), (float)atof(f[4].c_str()));
        p.h = (float)atof(f[5].c_str());
        p.sitting = f[6] == "1";
        p.anim = atoi(f[7].c_str());
        p.speed = (float)atof(f[8].c_str());
        p.headPitch = (float)atof(f[9].c_str());
        p.wheel = (float)atof(f[10].c_str());
        p.vkind = atoi(f[11].c_str());
        p.vid = atoi(f[12].c_str());
        p.car = atoi(f[13].c_str());
        p.lx = (float)atof(f[14].c_str());
        p.ly = (float)atof(f[15].c_str());
        p.lz = (float)atof(f[16].c_str());
        p.lh = (float)atof(f[17].c_str());
        p.fly = f.size() > 18 && f[18] == "1";
        RemotePlayer& r = it->second;
        if (!r.hasPose) { r.pos = p.pos; r.h = p.h; }
        r.target = p;
        r.hasPose = true;
        r.lastSeen = now;
    } else if (t == "join" && f.size() >= 3) {
        int id = atoi(f[1].c_str());
        RemotePlayer& r = players_[id];
        r.id = id;
        r.look = Appearance();
        r.look.parse(f[2]);
        r.name = r.look.name;
        r.modelDirty = true;
        r.lastSeen = now;
    } else if (t == "look" && f.size() >= 3) {
        auto it = players_.find(atoi(f[1].c_str()));
        if (it != players_.end()) {
            std::string keep = it->second.name;
            it->second.look.parse(f[2]);
            it->second.name = keep;
            it->second.modelDirty = true;
        }
    } else if (t == "leave" && f.size() >= 2) {
        auto it = players_.find(atoi(f[1].c_str()));
        if (it != players_.end()) {
            destroyCharacter(it->second.model);
            players_.erase(it);
        }
    } else if (t == "chat" && f.size() >= 4) {
        addChat(f[2], f[3], f[1] == "0", now);
    } else if (t == "clock" && f.size() >= 3) {
        double c = atof(f[1].c_str());
        // gemiddelde vertraging (halve ping) meetellen
        if (pingMs > 0) c += pingMs / 2000.0;
        clock_ = c;
        clockAt_ = now;
        weekday_ = atoi(f[2].c_str());
    } else if (t == "pong" && f.size() >= 3) {
        double sent = atof(f[1].c_str());
        pingMs = (int)((now * 1000.0) - sent);
        clock_ = atof(f[2].c_str()) + pingMs / 2000.0;
        clockAt_ = now;
    }
}

void NetPlay::update(double now, float dt, const std::function<Vehicle*(int, int)>& findVehicle) {
    if (!connected_) return;
    if (clockAt_ < 0) clockAt_ = now;  // net verbonden
    std::string m;
    while (ws_.poll(m)) handle(m, now);
    if (!ws_.open()) {
        lostReason = ws_.closeReason();
        if (lostReason.empty()) lostReason = "Verbinding met de server verbroken";
        disconnect();
        return;
    }
    if (now - lastPing_ > 10) {
        lastPing_ = now;
        char b[64];
        snprintf(b, sizeof b, "ping\t%.0f", now * 1000.0);
        ws_.send(b);
    }
    // andere spelers: naar de laatst ontvangen positie toe bewegen
    for (auto& kv : players_) {
        RemotePlayer& r = kv.second;
        if (!r.hasPose) continue;
        const NetPose& p = r.target;
        Vec3 want = p.pos;
        float wh = p.h;
        if (p.vkind) {
            if (Vehicle* v = findVehicle(p.vkind, p.vid)) {
                if (p.car >= 0 && p.car < v->carCount()) {
                    want = v->carToWorld(p.car, p.lx, p.ly, p.lz);
                    wh = v->carPose(p.car).h + p.lh;
                    // in een voertuig: direct meebewegen met de (lokale) bak, alleen de stap in de bak afvlakken
                    Vec3 d = want - r.pos;
                    if (length(d) > 3.0f) r.pos = want;
                    else r.pos = r.pos + d * std::min(1.0f, dt * 12.0f);
                    r.h = wh;
                    continue;
                }
            }
        }
        Vec3 d = want - r.pos;
        if (length(d) > 25.0f) r.pos = want;  // teleport
        else r.pos = r.pos + d * std::min(1.0f, dt * 10.0f);
        float dh = std::remainder(wh - r.h, 6.2831853f);
        r.h += dh * std::min(1.0f, dt * 10.0f);
        r.phase = std::fmod(r.phase + p.speed * dt / 1.35f, 1.0f);
    }
}

void NetPlay::sendPose(double now, const NetPose& p) {
    if (!connected_ || now - lastSend_ < 0.1) return;
    char b[512];
    snprintf(b, sizeof b, "st\t%.2f\t%.2f\t%.2f\t%.3f\t%d\t%d\t%.2f\t%.2f\t%.2f\t%d\t%d\t%d\t%.3f\t%.3f\t%.3f\t%.3f\t%d", p.pos.x, p.pos.y, p.pos.z, p.h,
             p.sitting ? 1 : 0, p.anim, p.speed, p.headPitch, p.wheel, p.vkind, p.vid, p.car, p.lx, p.ly, p.lz, p.lh, p.fly ? 1 : 0);
    // stilstaan: maar 1x per seconde herhalen
    if (lastPose_ == b && now - lastSend_ < 1.0) return;
    lastPose_ = b;
    lastSend_ = now;
    ws_.send(b);
}

void NetPlay::sendLook(const Appearance& a) {
    if (connected_) ws_.send(toUtf8("look\t" + clean(a.serialize())));
}

void NetPlay::sendChat(const std::string& text) {
    if (connected_ && !text.empty()) ws_.send(toUtf8("chat\t" + clean(text)));
}
