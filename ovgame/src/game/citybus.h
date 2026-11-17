#pragma once
// Buslijnen volgens de echte dienstregeling:
//   18 Eindhoven - Veldhoven - Bergeijk Loo, 8 Acht, 9 Best, 24 Geldrop - Mierlo - Helmond, 401 Airport,
//   402 Veldhoven Zonderwijk (Hermes) en 156 Eindhoven - 's-Hertogenbosch (Arriva). 401 en 402: altijd geleed (18 m).
//   - route en haltes (beide richtingen) uit OpenStreetMap (world.busRoutes, zie converter/busline.inc)
//   - lijn 18: tijden van de hoofdhaltes uit het busboek (data/bus/lijn18.txt); tussenliggende haltes naar afstand
//   - overige lijnen: alle haltetijden uit de GTFS-feed van OVapi (tools/gtfs_lines.ps1 -> data/bus/lijnNN.txt);
//     de GTFS-haltes worden op positie aan de OSM-haltes gekoppeld
//   - bussen rijden omlopen: heen, keren op het eindpunt (Bergeijk Loo, ~10 min), terug, enz.
//   - een bus stopt bij een halte alleen als iemand op de stopknop drukt of als jij er staat te wachten (en op de
//     eindhalte); het tempo wordt zo gekozen dat hij niet te vroeg bij een hoofdhalte langskomt
//   - in de bus: schermen met de volgende 5 haltes en de minuten tot de volgende halte, omroep (Announcer)
//   - je kunt de bus ook zelf besturen (richtkruis op het stuur); de rit vervalt dan
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include "bus.h"
#include "character.h"
#include "collision.h"
#include "signs.h"
#include "vehicle.h"
#include "world.h"

class BusFleet;
class CityBus;
class Announcer;

// een richting van een lijn
struct LineStop {
    std::string name, place, clip;  // clip = basisnaam van het omroepbestand
    float x, z, s, lat;             // haltepaal, positie langs de route, zijafstand (+ = rechts)
    uint8_t flags;
    float tx = 0, tz = 1;           // rijrichting bij de halte
    float side = 1;                 // trottoir: +1 = rechts van de rijrichting
};
// Een reiziger bij een bushalte of in een bus (ook bij bussen die de computer rijdt)
struct BusPax {
    uint32_t id = 0;
    int look = 0, variant = 0;
    int dir = 0, from = 0, to = 0;  // lijnrichting, opstap- en uitstaphalte (index in de richting)
    double appear = 0;              // vanaf dit moment staat hij bij de halte
    enum St { WAIT, TO_DOOR, CHECKIN, TO_SEAT, RIDE, ALIGHT, LEAVE } st = WAIT;
    CityBus* bus = nullptr;
    int queue = 0;                  // plek in de rij bij de voordeur
    int seat = -1;                  // stoel (index in het busmodel), -1 = staan
    int car = 0;                    // in de bus: bak en lokale positie/kijkrichting
    float lx = 0, lz = 0, lh = 0;
    Vec3 pos;                       // buiten: wereldpositie en kijkrichting
    float h = 0;
    Vec3 spot;                      // wachtplek
    float spotH = 0;
    bool bench = false;
    std::vector<Vec3> path;         // looproute (binnen: x = lx, z = lz)
    float timer = 0, phase = 0, speed = 0, press = 0, walked = 0;
};
struct BusLine {
    std::string ref, operatorName;   // "401", "Hermes"
    bool articulated = false;        // VDL Citea SLFA-181 (18 m) in plaats van SLF-120 (12 m)
};
struct LineDir {
    int line = 0;                    // index in BusFleet::lines_
    std::string ref, dest;           // "18", "Bergeijk Loo"
    std::vector<LineStop> stops;
    std::vector<Vec3> pts;
    std::vector<float> cum, lim;
    int trips = 0;                   // aantal ritten (0 = verouderde OSM-route, niet gebruikt)
};
struct LineTrip {
    int dir = 0;
    std::string dest;                // eindbestemming op het frontbord (GTFS headsign); "" = die van de richting
    int first = 0, last = 0;         // eerste / laatste halte (index in de richting)
    std::vector<double> plan;        // geplande tijd per halte (s, absolute klok); < 0 = niet bediend
    std::vector<bool> timing;        // hoofdhalte (busboek)
    int id = 0;
};
struct LineBlock {                   // omloop: ritten die dezelfde bus achter elkaar rijdt
    std::vector<int> trips;
    double start = 0, end = 0;
    bool spawned = false, done = false;
};

class CityBus : public Vehicle {
public:
    // Vehicle
    int carCount() const override { return model_->sections(); }
    float carLen(int i) const override { return model_->sec[i].length; }
    CarPose carPose(int i) const override { return pose_[i]; }
    float floorAt(int, float) const override { return BUS_FLOOR; }
    bool walkable(int car, float lx, float lz) const override;
    bool inDoorway(int car, float lz, int side) const override;
    float doorOpen() const override { return door_; }
    int doorSide() const override { return -1; }
    float halfWidth() const override { return BUS_HW; }
    float jointGap() const override { return model_->jointGap; }
    float floorHeight() const override { return BUS_FLOOR; }
    int seatCount() const override { return (int)model_->seats.size(); }
    SeatInfo seat(int i) const override { return model_->seats[i]; }
    float seatHeight() const override { return 0.45f; }
    void hotspots(std::vector<Hotspot>& out) const override;
    void activate(const Hotspot& h) override;
    bool driverEye(float& lx, float& ly, float& lz) const override;
    void setDriver(bool on) override;
    void drive(const DriveControls& c) override { ctl_ = c; }
    void stopButton() override { requestStop(); }
    bool seatTaken(int i) const override { return i >= 0 && i < (int)seatTaken_.size() && seatTaken_[i]; }

    bool manual() const { return manual_ && !duty_; }
    bool playerDriving() const { return driverAboard_; }
    bool inService() const { return trip_ >= 0 && (!manual_ || duty_); }
    bool stopped() const { return (!manual_ && (state_ == STOP || state_ == LAYOVER)) || (duty_ && dutyAt_ >= 0); }
    // dienst rijden (speler aan het stuur, rit blijft gelden)
    bool duty() const { return duty_; }
    int dutyStop() const { return dutyAt_; }        // halte waar de speler nu staat (deuren open), -1 = geen
    int nextIndex() const { return next_; }
    double dutyDelay(double clock) const;            // + = te laat, - = te vroeg (s)
    float routePos() const { return s_ - routeOff_; }  // positie van de vooras langs de route
    bool servingStop(int k) const;                   // staat met open deuren bij halte k
    int passengers() const;
    int waitingAtNext(double clock) const;            // wachtende reizigers bij de volgende halte
    float offRoute() const { return offRoute_; }      // afstand tot de route (dienst rijden)
    float speed() const { return v_; }
    std::string nextStop() const;        // naam van de volgende halte
    std::string currentStop() const;     // halte waar de bus staat ("" = rijdt)
    std::string display() const;         // frontbord
    std::string lineText() const;        // "Lijn 18 naar Bergeijk Loo"
    std::string destination() const;     // eindbestemming van de huidige rit
    float departureIn() const;           // s tot vertrek (stilstaand), -1 = rijdt
    double delay(double clock) const;    // vertraging t.o.v. de planning (s, >= 0)
    std::vector<std::string> infoLines(double clock) const;  // voor de kaart (hover): kop + "Naam: waarde"
    bool stopRequested() const { return stopReq_; }
    void requestStop();                  // stopknop
    float wheelAngle() const { return steer_; }
    int block() const { return block_; }  // omloop (multiplayer: dezelfde bus bij iedereen)
    int id = 0;

private:
    friend class BusFleet;
    enum State { DRIVE, STOP, LAYOVER, OFFDUTY } state_ = LAYOVER;
    const BusModel* model_ = nullptr;
    BusFleet* fleet_ = nullptr;
    // pad (game-coordinaten): eventueel verbindingsstuk + route van de rit
    std::vector<Vec3> pts_;
    std::vector<float> cum_, lim_;
    float routeOff_ = 0;        // padpositie waar de route begint
    float s_ = 0, v_ = 0;       // positie vooras langs het pad, snelheid
    float door_ = 0;
    CarPose pose_[2] = {};
    // dienst
    int block_ = -1, blockPos_ = 0, trip_ = -1;
    int next_ = 0;              // volgende halte (index in de richting)
    int atStop_ = -1;           // halte waar de bus staat
    double depart_ = 0;         // vertrektijd (klok) bij STOP/LAYOVER
    bool positioning_ = false;  // rijdt leeg naar het vertrekperron
    bool stopReq_ = false;
    float pace_ = 1, paceTimer_ = 0;
    int announced_ = -2;        // halte waarvoor "volgende halte" al is omgeroepen
    bool welcomed_ = false;
    float outTimer_ = 0;
    bool parked_ = false;       // tijdelijk naar de stalling (omloop gaat later verder)
    // handbediening (speler aan het stuur, of geparkeerd nadat de speler heeft gereden)
    bool manual_ = false, driverAboard_ = false, manualDoor_ = false;
    DriveControls ctl_;
    float steer_ = 0;
    float reverseHold_ = 0;
    Vec3 mF_, mR_, mR2_;
    float mh_ = 0;
    bool duty_ = false;
    int dutyAt_ = -1;               // dienst: halte waar de bus staat
    mutable float offRoute_ = 0;
    std::vector<uint8_t> seatTaken_;
    int boardQ_ = 0, boardServed_ = 0;

public:
    const LineDir& dir() const;
    const LineTrip& trip() const;
    float stopS(int k) const;       // padpositie van de vooras als de voordeur bij de haltepaal staat
    Vec3 posAt(float s) const;
    float pathEnd() const { return cum_.empty() ? 0 : cum_.back(); }
    float pathPos() const { return s_; }  // vooras langs het pad (zelfde schaal als stopS / posAt)
    bool stopRequestedFor(int k) const { return stopReq_ && next_ == k; }
    float frontDoorZ() const;       // lokale z van de voordeur

private:
    float limAt(float s) const;
    float projectS(Vec3 p, float guess) const;  // dichtstbijzijnde padpositie rond guess
    void addPoint(Vec3 p, float lim);
    void computePose();
    void startTrip(int tripIdx, bool connect, bool nearPlayer);
    void placeBySchedule(double clock);
    void updateManual(float dt, bool playerInside);
    void poseFromManual();
};

class BusFleet {
public:
    ~BusFleet();
    void reset();  // nieuwe tijd gekozen: alle bussen weg, omlopen opnieuw
    void build(const World& world, SignAtlas& atlas, const std::string& dataDir, int weekday);  // weekday 0 = zondag
    void update(float dt, double clock, Vec3 playerPos, const Vehicle* playerVehicle, bool playerSeated);
    void addCollision(Collision& col, Vec3 playerPos) const;
    void drawOpaque(World& world, Vec3 cam, double clock) const;
    void drawGlass(World& world, Vec3 cam) const;
    std::vector<CityBus*> buses();
    const std::vector<BusBay>& bays() const { return bays_; }
    void setAnnouncer(Announcer* a) { ann_ = a; }
    // halte in de buurt van pos: naam en de eerstvolgende vertrekken (voor de HUD); "" = geen halte
    std::string stopInfo(Vec3 pos, double clock, std::vector<std::string>& lines) const;
    std::string debugInfo(double clock) const;
    const std::vector<LineDir>& dirs() const { return dirs_; }
    const std::vector<LineTrip>& trips() const { return trips_; }
    const std::vector<BusLine>& lines() const { return lines_; }
    void bindAtlas(World& world) const;  // eigen tekstatlas (haltenamen, schermen) op de bordentextuur-eenheid
    // reizigers
    void setLookCount(int n) { lookCount_ = std::max(1, n); }
    void collectPeople(std::vector<PersonDraw>& out, Vec3 cam, double clock) const;
    int waitingAt(int dir, int k) const;
    bool busiestStop(Vec3& pos, Vec3& tangent, Vec3& side) const;  // test: halte met de meeste wachtenden
    // dienst rijden: bus voor deze rit klaarzetten op de eerste halte (na reset en klok zetten)
    CityBus* startDuty(int tripIdx, double clock);
    DutyStats duty;

private:
    friend class CityBus;
    const World* world_ = nullptr;
    Announcer* ann_ = nullptr;
    BusModel citea12_, citea18_;
    std::vector<BusLine> lines_;
    SignAtlas atlas_;
    GLuint atlasTex_ = 0;
    std::vector<std::unique_ptr<CityBus>> buses_;
    std::vector<BusBay> bays_;
    std::vector<LineDir> dirs_;
    std::vector<LineTrip> trips_;
    std::vector<LineBlock> blocks_;
    double clock_ = 0;
    int nextId_ = 0;
    float tipTimer_ = 120;
    const CityBus* annBus_ = nullptr;  // bus waarin de speler zit (omroep)
    // teksten
    std::vector<std::string> texts_;
    std::vector<Mesh> textMesh_;       // frontborden
    struct ScreenText { Mesh mesh; float w; };
    std::vector<ScreenText> nameWhite_, nameYellow_;  // per halte-tekst (index in screenNames_)
    std::vector<std::string> screenNames_;
    std::vector<ScreenText> minutes_;  // 0..60 min
    std::vector<ScreenText> headers_;  // per richting
    ScreenText stopBanner_;
    // haltes
    Mesh stopBody_, stopSigns_, stopGlass_;
    // reizigers
    std::vector<BusPax> pax_;
    std::unordered_set<uint64_t> generated_;  // (rit, halte) waarvoor reizigers zijn gemaakt
    std::vector<std::vector<int>> waitCnt_;   // per richting per halte: wachtende reizigers (zichtbaar)
    int lookCount_ = 1;
    uint32_t paxId_ = 1;
    float genTimer_ = 0;
    void prepareStops();
    void generatePax(double clock);
    void seedOnboard(CityBus& b, double clock);
    void updatePax(float dt, double clock);
    bool paxBusy(const CityBus* b) const;
    void releasePax(CityBus* b);
    void updateDuty(CityBus& b, float dt, double clock, bool aboard);
    int textIndex(const std::string& s) const;
    int screenIndex(const std::string& s) const;
    bool loadTimetable(const std::string& file, int weekday);
    void makeBlocks(int line, size_t firstTrip);
    void writeAnnouncementList(const std::string& dataDir) const;
    void buildStops(SignAtlas& atlas);
    void spawnBlock(int b, double clock, Vec3 playerPos);
};
