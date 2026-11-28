#pragma once
// Multiplayer: verbinding met de OV Game server (WebSocket), andere spelers en chat.
//
// Wat gedeeld wordt:
//   - de klok: de server bepaalt de tijd (en weekdag). Treinen en bussen rijden bij iedereen volgens dezelfde
//     dienstregeling, dus ze staan overal op dezelfde plek als de klok gelijk loopt (de game stuurt zijn klok bij).
//   - spelers: positie, kijkrichting, animatie en in welk voertuig ze zitten. In een trein of bus wordt de positie
//     t.o.v. het rijtuig gestuurd, zodat iemand netjes meerijdt in jouw kopie van die trein.
//   - chat (T)
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../net/net.h"
#include "character.h"
#include "vehicle.h"

// Wat een speler over zichzelf stuurt (zie main.cpp: wat er van jou getekend wordt)
struct NetPose {
    Vec3 pos;              // wereld (voeten, of heupen bij zitten)
    float h = 0;           // kijkrichting van het lichaam
    bool sitting = false;
    int anim = 0;          // AnimKind
    float speed = 0, headPitch = 0, wheel = 0;
    int vkind = 0;         // 0 niets, 1 trein (vid = dienst-id), 2 bus (vid = omloop)
    int vid = -1, car = 0;
    float lx = 0, ly = 0, lz = 0, lh = 0;  // t.o.v. de bak (alleen als vkind != 0)
    bool fly = false;
};

struct RemotePlayer {
    int id = 0;
    std::string name;
    Appearance look;
    CharModel model;       // pas gebouwd als hij te zien is (OpenGL op de hoofdthread)
    bool modelDirty = true;
    bool hasPose = false;
    NetPose target;        // laatst ontvangen
    double targetAt = 0;   // wanneer ontvangen
    Vec3 vel;              // geschatte snelheid (buiten voertuigen, om tussen berichten door te voorspellen)
    // In een voertuig wordt alleen binnen de bak afgevlakt (lokale coördinaten); de wereldpositie volgt pas uit de
    // pose van de bak na de simulatie (place). Zo rijdt iemand exact mee, hoe hard de trein ook gaat.
    int vkind = 0, vid = -1, car = 0;
    float lx = 0, ly = 0, lz = 0, lh = 0;
    bool inVehicle = false;   // het voertuig bestaat hier ook: pos komt uit lx/ly/lz
    Vec3 wpos;             // afgevlakte wereldpositie (als er geen voertuig is)
    Vec3 pos;              // getekende positie
    float h = 0;
    float phase = 0;
    double lastSeen = 0;
};

struct ChatLine {
    std::string name, text;
    bool system = false;
    double t = 0;
};

class NetPlay {
public:
    ~NetPlay();
    // Verbinden en aanmelden (blokkeert hooguit ~15 s). false = mislukt (error gevuld).
    bool connect(const std::string& baseUrl, const std::string& password, const Appearance& me, std::string& error);
    void disconnect();
    bool connected() const { return connected_; }
    std::string lostReason;          // waarom de verbinding wegviel ("" = niet weggevallen)
    bool serverDown = false;         // weggevallen omdat de server niet meer antwoordt (of zomaar weg is): client stopt
    static constexpr double SERVER_TIMEOUT = 10.0;  // s zonder enig bericht van de server

    // Elke frame: berichten verwerken, andere spelers afvlakken. findVehicle: voertuig bij (soort, id) of nullptr.
    void update(double now, float dt, const std::function<Vehicle*(int, int)>& findVehicle);
    // Na de simulatie (treinen en bussen staan op hun plek van deze frame): getekende posities bepalen.
    void place(double now, float dt, const std::function<Vehicle*(int, int)>& findVehicle);
    void sendPose(double now, const NetPose& p);   // stuurt zelf maximaal ~10x per seconde
    void sendLook(const Appearance& a);
    void sendChat(const std::string& text);

    // Klok van de server (s na middernacht, loopt door tussen de berichten), weekdag 0 = zondag
    double serverClock(double now) const;
    int weekday() const { return weekday_; }
    bool hasClock() const { return clockAt_ > 0; }

    std::map<int, RemotePlayer>& players() { return players_; }
    const std::vector<ChatLine>& chat() const { return chat_; }
    std::string serverName, motd;
    int myId = 0;
    int pingMs = -1;

private:
    void handle(const std::string& msg, double now);
    void addChat(const std::string& name, const std::string& text, bool system, double now);
    Net::WebSocket ws_;
    bool connected_ = false;
    std::map<int, RemotePlayer> players_;
    std::vector<ChatLine> chat_;
    double clock_ = 0, clockAt_ = 0;  // klok van de server en wanneer (Platform::time) die binnenkwam
    int weekday_ = 0;
    double lastSend_ = 0, lastPing_ = 0;
    double lastRecv_ = -1, lastUpdate_ = -1;  // laatste bericht van de server / laatste update() (Platform::time)
    std::string lastPose_;
};
