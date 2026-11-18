#pragma once
// Treinverkeer volgens de dienstregeling (NS, dienstregeling 2026, OV in Nederland Wiki en GTFS) op Eindhoven Centraal -
// Strijp-S - Best - Boxtel - Vught - 's-Hertogenbosch - Zaltbommel - Geldermalsen - Culemborg - Houten Castellum -
// Houten - Utrecht Lunetten - Utrecht Vaartsche Rijn - Utrecht Centraal, met de aftakkingen Boxtel - Tilburg en
// Geldermalsen - Tiel. De tabel staat in Traffic::setup. Sprinters stoppen overal; Intercity's alleen in Eindhoven,
// 's-Hertogenbosch en Utrecht.
// Sprinters 6500 maken kop in 's-Hertogenbosch. Op het viersporige deel rijden Sprinters op de buitenste en
// Intercity's op de middensporen (rechts rijden). Per baanvak kiest de trein zijn kruissnelheid zo, dat hij precies
// op tijd rijdt (max. 140 km/u baanvaksnelheid, 80 km/u op de emplacementen).
//
// Beveiliging: elke trein reserveert het spoor voor zich uit (minstens zijn remweg). Een trein mag alleen
// rijden over spoor dat hij zelf gereserveerd heeft; een stuk dat al door een andere trein is gereserveerd
// (of waar een trein staat) wordt pas vrijgegeven als die trein er voorbij is. Daardoor kunnen treinen
// nooit botsen, ook niet bij wissels en kruisingen.
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "railways.h"
#include "rollingstock.h"
#include "station.h"
#include "train.h"

struct Departure {
    double time;          // geplande vertrektijd
    std::string product;  // Sprinter / Intercity
    std::string number;
    std::string dest;
    int track;
    const Train* train;   // de trein als hij al rijdt (anders nullptr)
};

class Traffic {
public:
    ~Traffic();
    bool setup(const World& world, const Railways& rail, const Stations& stations, SignAtlas& atlas, std::string& err);
    // Alle treinen weg, klok zetten en de wereld tot nu laten doorlopen (treinen die al onderweg zijn).
    void reset(double clock, const Collision& col);
    void update(float dt, const Collision& col);
    double clock() const { return clock_; }
    const std::vector<Train*>& trains() const { return active_; }
    // Volgende vertrekken vanaf een station (alleen ritten die er stoppen)
    std::vector<Departure> departures(const std::string& station, int n, double horizon = 3600) const;
    // Wordt aangeroepen vlak voordat een trein de spelwereld verlaat (en verdwijnt)
    std::function<void(Train*)> onLeave;
    std::string debugInfo() const;
    std::string trainsInfo() const;  // toestand van alle rijdende treinen (test)
    std::string pathsInfo() const;   // paden per serie, elke 100 m "x z" (test)
    // dienst rijden: ritten die tussen from en to vertrekken vanaf hun eerste halte in de spelwereld
    struct DutyOption { int serviceId; int stop; double arr, dep; std::string label, info; };
    std::vector<DutyOption> dutyOptions(double from, double to) const;
    Train* trainOf(int serviceId) const;

private:
public:
    // Dienstregeling: per treinserie de haltes met tijden (minuten na het begin van het halfuur; -1 = uitrekenen)
    // waar het pad begint/eindigt: Eindhoven Centraal (oost), Tilburg-aftakking, kopmaken in 's-Hertogenbosch,
    // voorbij Utrecht Centraal richting Amsterdam (UTN) of Woerden/Gouda (UTW), Tiel-aftakking bij Geldermalsen.
    // (NORTH = vroeger: de mist in ten noorden van 's-Hertogenbosch; niet meer gebruikt)
    // Lijn Utrecht - Zwolle: keren in Utrecht Centraal (UTC) of Zwolle (ZWOLLE), voorbij Zwolle richting Meppel (ZWN),
    // voorbij Amersfoort richting Apeldoorn (AMFE).
    enum End { EAST, NORTH, TILBURG, DENBOSCH, UTN, UTW, TIEL, UTC, ZWOLLE, ZWN, AMFE };
    static bool isTerm(End e) { return e == DENBOSCH || e == UTC || e == ZWOLLE; }   // keren op een perron (kopmaken)
    static bool zwEnd(End e) { return e == UTC || e == ZWOLLE || e == ZWN || e == AMFE; }  // hoort bij de lijn naar Zwolle
    struct StopDef { const char* station; bool stop; double arr, dep; const char* track = nullptr; };  // track: echt spoor (bijv. "4b"), anders rechts rijden
    struct Def {
        const char* number; const char* dest; int stock;  // 0 = FLIRT, 1 = VIRM
        int csTrack; End origin, exit;
        std::vector<StopDef> stops;
        int linkFrom = -1;     // origin keert (isTerm): index van de serie waarvan de trein hier kopmaakt (mag ook later in de lijst)
        int linkBase = 0;      // halfuur-verschil (minuten) met die rit
        int hourly = 0;        // 0 = elk halfuur; 1 = alleen de halfuren vanaf :30, 2 = alleen die vanaf :00 (IC 500/600 om en om)
    };

private:
    struct Pattern {
        Def def;
        std::string number, dest;
        bool north = true;
        RollingStock* rs;
        TrackPath path;
        std::vector<Vec3> samples;      // elke 2 m langs het pad (beveiliging)
        std::vector<StopTime> stops;    // tijden in seconden t.o.v. het begin van het halfuur
        std::vector<SpeedZone> zones;
        double entryRel = 0;
        float entrySpeed = 20;
        Vec3 terminal;                  // spoorpunt van de laatste halte (kopmaken)
        int linkDef = -1;               // def-index van linkFrom (wordt na het opbouwen van alle series omgezet)
        std::string info;
    };
    struct Run {
        Train* train;
        const Pattern* pat;
        int i0 = 0, i1 = 0;   // gereserveerde monsters [i0, i1]
        Vec3 bmin, bmax;
    };
    std::vector<std::unique_ptr<RollingStock>> stock_;
    std::vector<std::unique_ptr<Pattern>> patterns_;
    std::vector<Service> services_;  // alle ritten van 2 dagen, gesorteerd op binnenkomst
    std::vector<int> serviceOf_;     // per rit: index van het patroon
    std::vector<Train*> active_;
    std::vector<Run> runs_;
    size_t nextSpawn_ = 0;
    double clock_ = 0;
    std::vector<std::string> names_;
    std::string warnings_;  // overgeslagen series (debugInfo)

    bool buildPattern(Pattern& p, const World& world, const Railways& rail, const Stations& st, std::string& err);
    void timetable(Pattern& p, std::string& log);
    int patternOf(const Service* s) const { return serviceOf_[s - services_.data()]; }
    void step(float dt, const Collision& col);
    bool conflict(const Vec3& p, const Run* self) const;
    void reserve(Run& r);
    bool spawn(const Service& s, const Collision& col);
    int nameIndex(const std::string& n);
};
