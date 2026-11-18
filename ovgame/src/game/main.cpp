// OV Game - Eindhoven
// Hoofdprogramma: opstarten, hoofdmenu, game-loop, interactie, kaart en HUD.
//
// Opties (handig om te testen; met een van deze opties start het spel meteen, zonder menu):
//   --pos X Z        startpositie (game-coordinaten, meters t.o.v. Eindhoven Centraal)
//   --spawn N        startpunt 1..9 (zie toetsen 1-9)
//   --time HH:MM     kloktijd (dienstregeling en daglicht)
//   --yaw GRADEN     kijkrichting (0 = noord, 90 = oost)
//   --pitch GRADEN
//   --height M       start vliegend op M meter boven het maaiveld
//   --sim S          laat de wereld eerst S seconden doorlopen (treinen en bussen rijden)
//   --ride N         meteen in rijdende trein N (volgorde van binnenkomst) of --bus N in bus N
//   --ridedest NAAM  meteen in de eerstvolgende trein met deze bestemming die op Centraal staat
//   --shot BESTAND   maak na --frames frames een screenshot (PNG) en sluit af (wacht tot alles geladen is)
//   --frames N
//   --menu           (met --shot) het hoofdmenu fotograferen;  --newgame: het scherm 'Nieuw spel'
//   --map            (met --shot) de kaart open;  --aerial: luchtfoto als ondergrond
//   --dumprail BESTAND   routes en tijden van de dienstregeling wegschrijven
//   --trafficlog BESTAND 20 uur treinverkeer simuleren en controleren (beveiliging, wachttijden)
//   --server URL     wereld van een OV Game server streamen en daar multiplayer spelen (de launcher geeft dit mee)
//   --connect        (met --server) meteen verbinden en spelen;  --password WACHTWOORD
#include <algorithm>
#include <map>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../engine/audio.h"
#include "../engine/meshbuilder.h"
#include "../engine/platform.h"
#include "../engine/renderer.h"
#include "../engine/text.h"
#include "busstation.h"
#include "announcer.h"
#include "character.h"
#include "citybus.h"
#include "people.h"
#include "collision.h"
#include "mapview.h"
#include "menu.h"
#include "datastore.h"
#include "netplay.h"
#include "player.h"
#include "railways.h"
#include "signs.h"
#include "sky.h"
#include "station.h"
#include "traffic.h"
#include "train.h"
#include "trainsound.h"
#include "world.h"

using namespace WorldFormat;

static std::string exeDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf);
    return s.substr(0, s.find_last_of("\\/"));
}

static std::string findDataDir() {
    std::string d = exeDir();
    for (int i = 0; i < 3; i++) {
        std::string c = d + "\\data";
        if (GetFileAttributesA((c + "\\world\\world.bin").c_str()) != INVALID_FILE_ATTRIBUTES) return c;
        d = d.substr(0, d.find_last_of("\\/"));
    }
    return exeDir() + "\\data";
}

static std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

static std::string mmss(float s) {
    int t = (int)std::max(0.0f, s + 0.5f);
    return fmt("%d:%02d", t / 60, t % 60);
}
static std::string hhmm(double clock) {
    int m = (int)std::floor(clock / 60.0);
    m = ((m % 1440) + 1440) % 1440;
    return fmt("%02d:%02d", m / 60, m % 60);
}

// Vaste startpunten. frame: 0 = Centraal (u, v), 1 = Strijp-S (u, v), 2 = Best (level 0 hal / 1 perron), 3 = busstation
struct Spawn { const char* name; float u, v; int level; float yawDeg; int frame; };
static const Spawn SPAWNS[] = {
    {"Eindhoven Centraal - Stationsplein Noord", 10.0f, 76.0f, 0, 168, 0},
    {"Eindhoven Centraal - Passage", 2.0f, 35.0f, 0, 168, 0},
    {"Eindhoven Centraal - Perron 3/4", 45.0f, 0.4f, 1, 258, 0},
    {"Eindhoven Centraal - Zuidhal", 8.0f, -84.0f, 0, -12, 0},
    {"Eindhoven Strijp-S - Tunnel", 108.5f, -22.0f, 0, 49, 1},
    {"Eindhoven Strijp-S - Perron 2/3", 40.0f, -0.3f, 1, -41, 1},
    {"Best - Perron", 0, 0, 1, 150, 2},
    {"Best - Stationshal", 0, 0, 0, 150, 2},
    {"Busstation Eindhoven Centraal", 0, 0, 0, 0, 3},
    {"Boxtel - Perron", 0, 0, 1, 0, 4},
    {"Vught - Perron", 0, 0, 1, 0, 4},
    {"'s-Hertogenbosch - Perron", 0, 0, 1, 0, 4},
    {"Zaltbommel - Perron", 0, 0, 1, 0, 4},
    {"Geldermalsen - Perron", 0, 0, 1, 0, 4},
    {"Culemborg - Perron", 0, 0, 1, 0, 4},
    {"Houten Castellum - Perron", 0, 0, 1, 0, 4},
    {"Houten - Perron", 0, 0, 1, 0, 4},
    {"Utrecht Lunetten - Perron", 0, 0, 1, 0, 4},
    {"Utrecht Vaartsche Rijn - Perron", 0, 0, 1, 0, 4},
    {"Utrecht Centraal - Stationshal", 0, 0, 0, 0, 5},
    {"Utrecht Centraal - Perron 18/19", 0, 0, 1, 0, 5},
};
static const int NSPAWN = sizeof(SPAWNS) / sizeof(SPAWNS[0]);

int main(int argc, char** argv) {
    float startX = 0, startZ = 0, startYaw = 0, startPitch = 0, startHeight = -1, simTime = 0;
    bool customPos = false, yawSet = false, direct = false, shotMenu = false, shotNewGame = false, shotMap = false, shotAerial = false, shotHover = false;
    int spawn = 1, rideTrain = 0, rideBus = 0, startMinutes = -1;
    float mapScale = 0, forceY = -1e9f;
    float tv[6] = {-1, 0, 0, 10, 0, 2};  // --tv trein rijtuig lz afstand hoek hoogte (testcamera)
    const char* shotPath = nullptr;
    const char* rideDest = nullptr;
    std::string serverArg, passwordArg;
    bool autoConnect = false;
    int shotFrames = 30;
    for (int i = 1; i < argc; i++) {
        auto arg = [&](const char* n) { return strcmp(argv[i], n) == 0 && i + 1 < argc; };
        if (arg("--pos") && i + 2 < argc) { startX = (float)atof(argv[++i]); startZ = (float)atof(argv[++i]); customPos = direct = true; }
        else if (arg("--spawn")) { spawn = atoi(argv[++i]); direct = true; }
        else if (arg("--yaw")) { startYaw = (float)atof(argv[++i]); yawSet = true; }
        else if (arg("--pitch")) startPitch = (float)atof(argv[++i]);
        else if (arg("--height")) startHeight = (float)atof(argv[++i]);
        else if (arg("--sim")) simTime = (float)atof(argv[++i]);
        else if (arg("--ride")) { rideTrain = atoi(argv[++i]); direct = true; }
        else if (arg("--ridedest")) { rideDest = argv[++i]; direct = true; }
        else if (arg("--bus")) { rideBus = atoi(argv[++i]); direct = true; }
        else if (arg("--shot")) shotPath = argv[++i];
        else if (arg("--frames")) shotFrames = atoi(argv[++i]);
        else if (arg("--time")) {
            int h = 0, m = 0;
            sscanf(argv[++i], "%d:%d", &h, &m);
            startMinutes = h * 60 + m;
        }
        else if (arg("--server")) serverArg = argv[++i];
        else if (arg("--password")) passwordArg = argv[++i];
        else if (strcmp(argv[i], "--connect") == 0) autoConnect = true;
        else if (strcmp(argv[i], "--menu") == 0) shotMenu = true;
        else if (strcmp(argv[i], "--newgame") == 0) shotMenu = shotNewGame = true;
        else if (strcmp(argv[i], "--map") == 0) shotMap = true;
        else if (strcmp(argv[i], "--aerial") == 0) shotAerial = true;
        else if (strcmp(argv[i], "--hover") == 0) shotHover = true;  // test: muis op het voertuig het dichtst bij het kaartmidden
        else if (arg("--mapscale")) mapScale = (float)atof(argv[++i]);
        else if (arg("--y")) forceY = (float)atof(argv[++i]);
        else if (strcmp(argv[i], "--tv") == 0 && i + 6 < argc) { for (int k = 0; k < 6; k++) tv[k] = (float)atof(argv[++i]); direct = true; }
    }
    if (shotPath && !shotMenu) direct = true;

    if (!Platform::init("OV Game - Eindhoven", 1600, 900)) {
        MessageBoxA(nullptr, "OpenGL 3.3 kon niet worden gestart.", "OV Game", MB_OK | MB_ICONERROR);
        return 1;
    }
    // instellingen (data\instellingen.txt): venstermodus en vsync; testopnames (--shot) altijd in een venster
    const std::string settingsPath = findDataDir() + "\\instellingen.txt";
    int setWindowMode = 0, setRealism = 1;
    std::string setServer;
    bool setVsync = true;
    if (FILE* f = fopen(settingsPath.c_str(), "r")) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            int v;
            if (sscanf(line, "venster = %d", &v) == 1) setWindowMode = std::max(0, std::min(2, v));
            else if (sscanf(line, "vsync = %d", &v) == 1) setVsync = v != 0;
            else if (sscanf(line, "treinrealisme = %d", &v) == 1) setRealism = std::max(0, std::min(2, v));
            else if (strncmp(line, "server = ", 9) == 0) {
                setServer = line + 9;
                while (!setServer.empty() && (setServer.back() == '\n' || setServer.back() == '\r' || setServer.back() == ' ')) setServer.pop_back();
            }
        }
        fclose(f);
    }
    auto saveSettings = [&]() {
        if (FILE* f = fopen(settingsPath.c_str(), "w")) {
            fprintf(f, "# OV Game instellingen\n# venster: 0 = venster, 1 = volledig scherm (venster), 2 = volledig scherm\nvenster = %d\nvsync = %d\n"
                       "# treinrealisme: 0 = simpel, 1 = middel, 2 = zo echt mogelijk\ntreinrealisme = %d\n"
                       "# laatst gebruikte multiplayer-server\nserver = %s\n",
                    setWindowMode, setVsync ? 1 : 0, setRealism, setServer.c_str());
            fclose(f);
        }
    };
    Train::defaultRealism = setRealism;
    // personages (data\personages.txt): een regel per personage, "actief=N" kiest het jouwe
    const std::string charPath = findDataDir() + "\\personages.txt";
    std::vector<Appearance> chars;
    int activeChar = 0;
    if (FILE* f = fopen(charPath.c_str(), "r")) {
        char line[1024];
        while (fgets(line, sizeof line, f)) {
            std::string l(line);
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            if (l.rfind("actief=", 0) == 0) activeChar = atoi(l.c_str() + 7);
            else if (!l.empty() && l[0] != '#') {
                Appearance a;
                if (a.parse(l)) chars.push_back(a);
            }
        }
        fclose(f);
    }
    if (chars.empty()) {
        Appearance a = Appearance::random(20261117);
        a.age = 2;
        a.name = "Speler";
        chars.push_back(a);
    }
    activeChar = std::max(0, std::min(activeChar, (int)chars.size() - 1));
    auto saveChars = [&]() {
        if (FILE* f = fopen(charPath.c_str(), "w")) {
            fprintf(f, "# OV Game personages (personage-editor in het hoofdmenu)\nactief=%d\n", activeChar);
            for (const Appearance& a : chars) fprintf(f, "%s\n", a.serialize().c_str());
            fclose(f);
        }
    };
    if (!shotPath) Platform::setWindowMode(setWindowMode);
    for (int i = 1; i + 1 < argc; i++)  // test: venstermodus forceren (ook bij --shot), wordt niet opgeslagen
        if (strcmp(argv[i], "--windowmode") == 0) Platform::setWindowMode(atoi(argv[i + 1]));
    Platform::setVSync(setVsync);
    Renderer renderer;
    renderer.init();
    TextRenderer text;
    text.init();
    auto loadingScreen = [&](const std::string& s) {
        int w = Platform::width(), h = Platform::height();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w, h);
        glClearColor(0.05f, 0.07f, 0.11f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        text.begin(w, h);
        float ui = h / 900.0f;
        text.text((w - text.measure("OV Game", 64 * ui)) / 2, h / 2.0f - 110 * ui, "OV Game", 64 * ui, {1, 0.8f, 0.1f, 1});
        text.text((w - text.measure(s, 28 * ui)) / 2, h / 2.0f, s, 28 * ui, {1, 1, 1, 1});
        text.end();
        Platform::swap();
    };
    loadingScreen("Eindhoven wordt geladen...");

    World world;
    std::string err;
    std::string dataDir = findDataDir();
    // wereldbestanden: van de server (streaming) of lokaal
    std::string streamError;
    if (!serverArg.empty()) {
        loadingScreen("Verbinden met " + Net::normalizeBase(serverArg) + "...");
        if (!DataStore::initRemote(serverArg, dataDir, streamError)) {
            DataStore::initLocal(dataDir);
            if (GetFileAttributesA((dataDir + "\\world\\tiles").c_str()) == INVALID_FILE_ATTRIBUTES)
                Platform::showError((streamError + "\n\nZonder server is er geen wereld om te laden.").c_str());
        }
        if (setServer != Net::normalizeBase(serverArg)) { setServer = Net::normalizeBase(serverArg); saveSettings(); }
    } else {
        DataStore::initLocal(dataDir);
    }
    World::preloadTick = [&]() {
        Platform::pump();
        DataStore::Stats st = DataStore::stats();
        if (DataStore::remote() && st.active > 0)
            loadingScreen(fmt("Omgeving wordt gedownload...  %.1f MB", st.bytes / 1048576.0));
    };
    if (!world.load(dataDir, err)) { Platform::showError(err.c_str()); return 1; }

    Collision collision;
    SignAtlas atlas;
    atlas.begin();
    Stations stations;
    stations.build(world, collision, atlas);
    Railways railways;
    railways.build(world);
    BusStation busStation;
    busStation.build(world);
    BusFleet buses;
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        buses.build(world, atlas, dataDir, st.wDayOfWeek);  // dienstregeling van vandaag (ma-vr / za / zo)
    }
    Announcer announcer;
    {
        int n = announcer.load(dataDir + "\\audio\\bus");
        if (n == 0) printf("Geen omroepbestanden in %s\\audio\\bus\n", dataDir.c_str());
    }
    buses.setAnnouncer(&announcer);
    for (int i = 1; i < argc; i++)  // test: omroep (met gong en filter) naar een WAV-bestand: --dumpclip naam bestand
        if (strcmp(argv[i], "--dumpclip") == 0 && i + 2 < argc) {
            announcer.say(argv[i + 1]);
            std::vector<float> buf;
            std::vector<float> blk(2 * 1024);
            while (announcer.busy() && buf.size() < 44100 * 2 * 30) {
                std::fill(blk.begin(), blk.end(), 0.0f);
                announcer.mix(blk.data(), 1024);
                buf.insert(buf.end(), blk.begin(), blk.end());
            }
            FILE* wf = fopen(argv[i + 2], "wb");
            if (wf) {
                uint32_t n = (uint32_t)buf.size(), bytes = n * 2, v32;
                uint16_t v16;
                fwrite("RIFF", 1, 4, wf); v32 = 36 + bytes; fwrite(&v32, 4, 1, wf); fwrite("WAVEfmt ", 1, 8, wf);
                v32 = 16; fwrite(&v32, 4, 1, wf); v16 = 1; fwrite(&v16, 2, 1, wf); v16 = 2; fwrite(&v16, 2, 1, wf);
                v32 = 44100; fwrite(&v32, 4, 1, wf); v32 = 44100 * 4; fwrite(&v32, 4, 1, wf); v16 = 4; fwrite(&v16, 2, 1, wf); v16 = 16; fwrite(&v16, 2, 1, wf);
                fwrite("data", 1, 4, wf); fwrite(&bytes, 4, 1, wf);
                for (float x : buf) { int16_t s = (int16_t)std::max(-32767.0f, std::min(32767.0f, x * 32767.0f)); fwrite(&s, 2, 1, wf); }
                fclose(wf);
            }
            return 0;
        }
    loadingScreen("Dienstregeling wordt opgebouwd...");
    Traffic traffic;
    bool trainOk = traffic.setup(world, railways, stations, atlas, err);
    if (!trainOk && !shotPath) Platform::showError(err.c_str());
    world.setSignTexture(atlas.finish());
    MapView map;
    map.init(dataDir, world);
    map.addPlatforms(stations.centraalPlatforms());
    loadingScreen("Personages worden gemaakt...");
    People people;
    people.init(64);
    people.setPlayer(chars[activeChar]);
    CharacterRenderer charR;
    charR.init();
    buses.setLookCount(people.poolSize());
    TrainPeople tpeople;
    CharModel preview;  // voorbeeld in de personage-editor
    int camMode = 0;    // F5: 0 eerste persoon, 1 achter je, 2 voor je
    bool navLine = true;
    DutyStats trainDuty;

    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--dumprail") == 0 && i + 1 < argc) {
            FILE* df = fopen(argv[i + 1], "w");
            if (df) {
                fputs(traffic.debugInfo().c_str(), df);
                fputs(traffic.pathsInfo().c_str(), df);
                if (!trainOk) fprintf(df, "FOUT: %s\n", err.c_str());
                for (const OsmPlatform& op : world.platforms) {
                    float cx = 0, cz = 0;
                    for (auto& q : op.pts) { cx += q.first; cz += q.second; }
                    fprintf(df, "perron '%s' RD %.0f %.0f hoogte %.1f\n", op.name.c_str(), rdX(cx / op.pts.size()), rdY(cz / op.pts.size()), op.height);
                }
                fclose(df);
            }
        }

    // geluid: een paar synthesizerkanalen voor de dichtstbijzijnde treinen
    const int SOUND_SLOTS = 4;
    TrainSound trainSound[SOUND_SLOTS];
    bool audioOn = !shotPath && Audio::init([&](float* o, int n) {
        for (int k = 0; k < SOUND_SLOTS; k++) trainSound[k].mix(o, n);
        announcer.mix(o, n);
    });

    Player player;
    auto applyLook = [&]() {
        const CharModel& m = people.player();
        player.eyeH = m.eyeY;
        player.sitEye = m.eyeY - m.hipY + 0.09f;
    };
    applyLook();
    auto computeSpawn = [&](int n, Player& p) {
        n = std::max(1, std::min(n, NSPAWN));
        const Spawn& s = SPAWNS[n - 1];
        float x = 0, z = 0, y = 0;
        p.vehicle = nullptr;
        p.seat = -1;
        p.fly = false;
        if (s.frame == 0) {
            stationToGame(s.u, s.v, x, z);
            y = s.level ? stations.platformY : std::max(world.heightAt(x, z), STATION_FLOOR);
        } else if (s.frame == 1) {
            double rx, ry;
            strijpsToRd(s.u, s.v, rx, ry);
            x = gameX(rx);
            z = gameZ(ry);
            y = collision.floorAt(x, z, s.level ? 1e9f : 22.0f);
        } else if (s.frame == 2) {
            x = stations.bestCenter.x;
            z = stations.bestCenter.z;
            if (!s.level)
                for (const WorldFeature& f : world.features)
                    if (f.kind == FEAT_STATION_HALL && !f.pts.empty()) {
                        float cx = 0, cz = 0;
                        for (const Vec3& q : f.pts) { cx += q.x; cz += q.z; }
                        x = cx / f.pts.size();
                        z = cz / f.pts.size();
                    }
            float want = s.level ? stations.bestPlatformY : stations.bestHallY;
            y = collision.floorAt(x, z, want + 0.5f);
            if (y < want - 1.0f) y = want;
        } else if (s.frame == 5) {
            // Utrecht Centraal: in de stationshal of op perron 18/19 (utrechtstation.cpp)
            Vec3 q = s.level == 0 ? stations.utrechtHall : stations.utrechtPlatform;
            x = q.x; z = q.z; y = q.y;
            p.yaw = s.level == 0 ? -2.62f : 0.52f;
        } else if (s.frame == 4) {
            // station langs de lijn: op het grootste perron, bij het punt dat het dichtst bij het stationsmidden ligt
            std::string st = std::string(s.name).substr(0, std::string(s.name).find(" - "));
            Vec3 c;
            for (const MapPoint& mp : world.points)
                if (mp.kind == POINT_STATION && mp.name == st) c = Vec3(mp.x, 0, mp.z);
            const OsmPlatform* big = nullptr;
            float bigA = 0;
            for (const OsmPlatform& op : world.platforms) {
                if (op.pts.empty() || std::hypot(op.pts[0].first - c.x, op.pts[0].second - c.z) > 400) continue;
                float a = 0;
                for (size_t k = 0; k < op.pts.size(); k++) { auto q = op.pts[k], r = op.pts[(k + 1) % op.pts.size()]; a += q.first * r.second - r.first * q.second; }
                if (std::fabs(a) > bigA) { bigA = std::fabs(a); big = &op; }
            }
            x = c.x; z = c.z; y = world.heightAt(x, z);
            if (big) {
                float bd = 1e9f;
                for (size_t k = 0; k < big->pts.size(); k++)
                    for (size_t j = k + 1; j < big->pts.size(); j++) {
                        float mx = (big->pts[k].first + big->pts[j].first) / 2, mz = (big->pts[k].second + big->pts[j].second) / 2;
                        float d = std::hypot(mx - c.x, mz - c.z);
                        bool in = false;
                        for (size_t a = 0, b = big->pts.size() - 1; a < big->pts.size(); b = a++)
                            if ((big->pts[a].second > mz) != (big->pts[b].second > mz) && mx < (big->pts[b].first - big->pts[a].first) * (mz - big->pts[a].second) / (big->pts[b].second - big->pts[a].second) + big->pts[a].first) in = !in;
                        if (in && d < bd) { bd = d; x = mx; z = mz; }
                    }
                y = big->height;
                // kijkrichting langs het perron
                float sxx = 0, szz = 0, sxz = 0, ccx = 0, ccz = 0;
                for (auto& q : big->pts) { ccx += q.first; ccz += q.second; }
                ccx /= big->pts.size(); ccz /= big->pts.size();
                for (auto& q : big->pts) { float dx = q.first - ccx, dz = q.second - ccz; sxx += dx * dx; szz += dz * dz; sxz += dx * dz; }
                float ang = 0.5f * std::atan2(2 * sxz, sxx - szz);
                p.yaw = std::atan2(std::cos(ang), -std::sin(ang));
            }
        } else {
            if (!buses.bays().empty()) {
                const BusBay& b = buses.bays()[buses.bays().size() / 2];
                x = b.x + std::cos(b.heading) * 3.2f;
                z = b.z - std::sin(b.heading) * 3.2f;
                y = std::max(world.heightAt(x, z), collision.floorAt(x, z, world.heightAt(x, z) + 0.5f));
                p.yaw = std::atan2(-std::cos(b.heading), std::sin(b.heading));
            }
        }
        p.pos = Vec3(x, y, z);
        if (s.frame != 3 && s.frame != 4 && s.frame != 5) p.yaw = s.yawDeg * PI / 180;
        p.pitch = 0;
    };
    auto doSpawn = [&](int n) { computeSpawn(n, player); };
    // plaatsen op de kaart
    {
        const struct { const char* name; int spawn; } places[] = {
            {"Eindhoven Centraal", 3}, {"Busstation Centraal", 9}, {"Eindhoven Strijp-S", 6}, {"Best", 7},
            {"Boxtel", 10}, {"Vught", 11}, {"'s-Hertogenbosch", 12}, {"Zaltbommel", 13}, {"Geldermalsen", 14}, {"Culemborg", 15},
            {"Houten Castellum", 16}, {"Houten", 17}, {"Utrecht Lunetten", 18}, {"Utrecht Vaartsche Rijn", 19}, {"Utrecht Centraal", 20}};
        for (auto& pl : places) {
            Player tmp;
            computeSpawn(pl.spawn, tmp);
            map.addPlace({pl.name, tmp.pos.x, tmp.pos.z, pl.spawn});
        }
    }
    // teleporteren naar een willekeurig punt (kaart)
    auto teleport = [&](float x, float z) {
        player.vehicle = nullptr;
        player.seat = -1; player.driving = false;
        player.fly = false;
        float g = world.heightAt(x, z);
        float f = collision.floorAt(x, z, g + 3.0f);
        player.pos = Vec3(x, std::max(g, f), z);
    };

    // melding op het scherm (bijv. uitstappen aan de rand van de wereld)
    std::string notice;
    float noticeT = 0;
    traffic.onLeave = [&](Train* t) {
        if (t->duty && !t->duty->finished) {
            t->duty->finished = true;
            t->duty->note("Dienst voltooid: de trein rijdt verder buiten de spelwereld.");
        }
        if (player.vehicle != t) return;
        const Service& s = t->service();
        int li = t->lastStopIndex();
        std::string st = li >= 0 ? s.stops[li].station : "Eindhoven Centraal";
        int track = li >= 0 ? s.stops[li].track : 3;
        // op het perron van het laatste station
        bool placed = false;
        for (const PlatformInfo& pi : stations.tracks)
            if (pi.station == st && pi.track == track) {
                float x = pi.x, z = pi.z;
                if (st == "Eindhoven Centraal") {
                    float u, v;
                    gameToStation(x, z, u, v);
                    stationToGame(u, v + (track % 2 ? 2.0f : -2.0f), x, z);  // iets van de perronrand af
                }
                float y = collision.floorAt(x, z, 1e9f);
                if (y < -1e8f) y = world.heightAt(x, z);
                player.vehicle = nullptr;
                player.seat = -1; player.driving = false;
                player.pos = Vec3(x, y, z);
                placed = true;
                break;
            }
        if (!placed) doSpawn(st == "Best" ? 7 : st == "Eindhoven Strijp-S" ? 6 : 3);
        notice = "Deze trein rijdt verder naar " + s.dest + " (buiten de spelwereld). Je bent uitgestapt in " + st + ".";
        noticeT = 9.0f;
    };

    int dayOfYear = dayOfYearToday();
    SYSTEMTIME nowSt;
    GetLocalTime(&nowSt);
    int defaultMinutes = startMinutes >= 0 ? startMinutes : nowSt.wHour * 60 + nowSt.wMinute;
    double fallbackClock = defaultMinutes * 60.0;  // klok voor de bussen als er geen treinen zijn

    // verkeerstest: veel uren simuleren en de beveiliging controleren
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--trafficlog") == 0 && i + 1 < argc && trainOk) {
            FILE* lf = fopen(argv[i + 1], "w");
            traffic.reset(5.5 * 3600, collision);
            double maxWait = 0;
            std::string maxInfo;
            std::vector<std::pair<const Train*, double>> waits;
            int minDistViol = 0, maxActive = 0;
            std::map<int, std::pair<std::string, float>> delays;  // per rit (id): grootste vertraging
            for (double t = 0; t < 14 * 3600; t += 0.5) {
                traffic.update(0.5f, collision);
                const auto& tr = traffic.trains();
                maxActive = std::max(maxActive, (int)tr.size());
                for (const Train* a : tr) {
                    auto& dl = delays[a->service().id];
                    dl.first = a->label() + " (" + hhmm(a->service().stops[0].dep) + ")";
                    dl.second = std::max(dl.second, a->maxDelay());
                    double* w = nullptr;
                    for (auto& p : waits) if (p.first == a) w = &p.second;
                    if (!w) { waits.push_back({a, 0}); w = &waits.back().second; }
                    if (a->waitingForSignal()) *w += 0.5; else *w = 0;
                    if (*w > maxWait) { maxWait = *w; maxInfo = a->label() + " om " + hhmm(traffic.clock()); }
                    static std::map<int, bool> lateLogged;
                    if (a->maxDelay() > 300 && !lateLogged[a->service().id] && lateLogged.size() < 12) {
                        lateLogged[a->service().id] = true;
                        fprintf(lf, "TE LAAT GERAAKT: %s om %s: %.0f s, laatste halte %s, snelheid %.1f m/s%s\n", a->label().c_str(), hhmm(traffic.clock()).c_str(),
                                a->maxDelay(), a->lastStopIndex() >= 0 ? a->service().stops[a->lastStopIndex()].station.c_str() : "-", a->speed(),
                                a->waitingForSignal() ? " (wacht voor sein)" : "");
                    }
                    static bool dumped = false;
                    if (*w > 900 && !dumped) {  // eerste vastloper: toestand van alle treinen
                        dumped = true;
                        fprintf(lf, "VASTLOPER: %s wacht al %.0f s om %s\n%s\n", a->label().c_str(), *w, hhmm(traffic.clock()).c_str(), traffic.trainsInfo().c_str());
                    }
                }
                // controle: geen twee treinen dichter dan 2,5 m bij elkaar (rijtuigmiddens/einden)
                if (((int)(t * 2)) % 20 == 0)
                    for (size_t p = 0; p < tr.size(); p++)
                        for (size_t q = p + 1; q < tr.size(); q++) {
                            CarPose ca = tr[p]->carPose(tr[p]->carCount() / 2), cb = tr[q]->carPose(tr[q]->carCount() / 2);
                            if (std::hypot(ca.x - cb.x, ca.z - cb.z) > 200) continue;
                            for (int c = 0; c < tr[p]->carCount(); c++)
                                for (int e = 0; e < tr[q]->carCount(); e++) {
                                    for (float fa : {-0.45f, 0.0f, 0.45f})
                                        for (float fb : {-0.45f, 0.0f, 0.45f}) {
                                            Vec3 a = tr[p]->carToWorld(c, 0, 0, fa * tr[p]->carLen(c));
                                            Vec3 b = tr[q]->carToWorld(e, 0, 0, fb * tr[q]->carLen(e));
                                            if (std::hypot(a.x - b.x, a.z - b.z) < 2.5f && std::fabs(a.y - b.y) < 4) {
                                                if (minDistViol < 20)
                                                    fprintf(lf, "BOTSING? %s en %s om %s\n", tr[p]->label().c_str(), tr[q]->label().c_str(), hhmm(traffic.clock()).c_str());
                                                minDistViol++;
                                            }
                                        }
                                }
                        }
                waits.erase(std::remove_if(waits.begin(), waits.end(), [&](auto& p) { return std::find(tr.begin(), tr.end(), p.first) == tr.end(); }), waits.end());
            }
            int late60 = 0, late180 = 0;
            float worst = 0;
            std::string worstName;
            for (auto& d : delays) {
                if (d.second.second > 60) late60++;
                if (d.second.second > 180) late180++;
                if (d.second.second > worst) { worst = d.second.second; worstName = d.second.first; }
            }
            fprintf(lf, "ritten gereden: %zu\nmeer dan 1 min te laat: %d\nmeer dan 3 min te laat: %d\ngrootste vertraging: %.0f s (%s)\n", delays.size(), late60, late180, worst, worstName.c_str());
            for (auto& d : delays) if (d.second.second > 60) fprintf(lf, "  te laat: %s  %.0f s\n", d.second.first.c_str(), d.second.second);
            fprintf(lf, "max. treinen tegelijk: %d\nlangste wachttijd voor sein: %.0f s (%s)\nbijna-botsingen: %d\n%s", maxActive, maxWait, maxInfo.c_str(),
                    minDistViol, traffic.debugInfo().c_str());
            fclose(lf);
            return 0;
        }

    // ---------- toestand ----------
    enum State { MENU, PLAYING } state = direct ? PLAYING : MENU;
    Menu menu;
    for (const Spawn& s : SPAWNS) menu.starts.push_back(s.name);
    menu.minutes = defaultMinutes;
    menu.startChoice = 2;
    if (shotNewGame) menu.screen = Menu::NEW_GAME;
    menu.windowMode = setWindowMode;
    menu.vsync = setVsync;
    menu.realism = setRealism;
    menu.chars = chars;
    menu.activeChar = activeChar;
    {
        std::vector<std::string> refs;
        for (const BusLine& l : buses.lines()) refs.push_back(l.ref);
        std::sort(refs.begin(), refs.end(), [](const std::string& a, const std::string& b) { return atoi(a.c_str()) < atoi(b.c_str()); });
        menu.busLines = refs;
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--options") == 0) menu.screen = Menu::OPTIONS;  // test: opties-scherm
        if (strcmp(argv[i], "--charmenu") == 0) { menu.screen = Menu::CHARACTER; menu.editIndex = menu.activeChar; menu.edit = chars[activeChar]; }
        if (strcmp(argv[i], "--dutymenu") == 0) { menu.screen = Menu::DUTY; menu.dutyKind = i + 1 < argc && strcmp(argv[i + 1], "trein") == 0 ? 1 : 0; }
        if (strcmp(argv[i], "--cam") == 0 && i + 1 < argc) camMode = atoi(argv[i + 1]) % 3;
    }
    if (trainOk) traffic.reset(defaultMinutes * 60.0, collision);

    auto startGame = [&](int spawnNo, int minutes, double clockSec = -1) {
        double c0 = clockSec >= 0 ? clockSec : minutes * 60.0;
        if (trainOk) traffic.reset(c0, collision);
        fallbackClock = c0;
        player.vehicle = nullptr; player.seat = -1; player.driving = false;
        buses.reset();
        tpeople.reset();
        trainDuty = DutyStats();
        doSpawn(spawnNo);
        loadingScreen("Omgeving wordt geladen...");
        world.preload(player.eye());
        state = PLAYING;
        menu.gameRunning = true;
        map.close();
    };
    // lijst met ritten voor het scherm "Dienst rijden"
    auto hm = [](double t) { return hhmm(t); };
    auto fillDuty = [&]() {
        menu.dutyItems.clear();
        double t0 = menu.minutes * 60.0, t1 = t0 + 3 * 3600.0;
        if (menu.dutyKind == 0) {
            if (menu.busLines.empty()) return;
            std::string ref = menu.busLines[std::max(0, std::min(menu.dutyLine, (int)menu.busLines.size() - 1))];
            const auto& trips = buses.trips();
            const auto& dirs = buses.dirs();
            std::vector<int> idx;
            for (size_t i = 0; i < trips.size(); i++) {
                const LineTrip& T = trips[i];
                if (dirs[T.dir].ref != ref) continue;
                double dep = T.plan[T.first];
                if (dep >= t0 && dep <= t1) idx.push_back((int)i);
            }
            std::sort(idx.begin(), idx.end(), [&](int a, int b) { return trips[a].plan[trips[a].first] < trips[b].plan[trips[b].first]; });
            for (int i : idx) {
                const LineTrip& T = trips[i];
                const LineDir& D = dirs[T.dir];
                std::string dest = T.dest.empty() ? D.dest : T.dest;
                DutyItem it;
                it.id = i;
                it.time = T.plan[T.first];
                it.label = hm(T.plan[T.first]) + "  Lijn " + D.ref + "  " + D.stops[T.first].name + " - " + dest;
                std::string route;
                int n = 0;
                for (int k = T.first; k <= T.last; k++) {
                    if (T.plan[k] < 0) continue;
                    n++;
                    if (k == T.first || k == T.last || n % 4 == 0) route += (route.empty() ? "" : ", ") + D.stops[k].name;
                }
                const BusLine& L = buses.lines()[D.line];
                it.info = std::to_string(n) + " haltes, rijtijd " + std::to_string((int)((T.plan[T.last] - T.plan[T.first]) / 60)) + " min, aankomst " +
                          hm(T.plan[T.last]) + ".\nBus: " + (L.articulated ? "VDL Citea SLFA-181 Electric (18 m, geleed)" : "VDL Citea SLF-120 Electric (12 m)") +
                          ", vervoerder " + L.operatorName + ".\nRoute: " + route + ".";
                menu.dutyItems.push_back(it);
            }
        } else if (trainOk) {
            for (const Traffic::DutyOption& o : traffic.dutyOptions(t0, t1)) {
                DutyItem it;
                it.id = o.serviceId;
                it.stop = o.stop;
                it.time = o.dep;
                it.label = o.label;
                it.info = o.info;
                menu.dutyItems.push_back(it);
            }
        }
        menu.dutyDirty = false;
    };
    // dienst beginnen: bus of trein klaarzetten, jij achter het stuur / de lessenaar
    auto startDuty = [&](int kind, int id, int stopIdx, double dep) {
        player.vehicle = nullptr; player.seat = -1; player.driving = false; player.fly = false;
        tpeople.reset();
        trainDuty = DutyStats();
        if (kind == 0) {
            const LineTrip& T = buses.trips()[id];
            double clock = T.plan[T.first] - 150;
            loadingScreen("Dienst wordt klaargezet...");
            if (trainOk) traffic.reset(clock, collision);
            fallbackClock = clock;
            buses.reset();
            CityBus* b = buses.startDuty(id, clock);
            if (!b) return false;
            float ex, ey, ez;
            b->driverEye(ex, ey, ez);
            player.board(b, 0, 0.0f, ez);
            player.driving = true;
            b->setDriver(true);
            player.pos = b->carToWorld(0, ex, ey - player.eyeH, ez);
            player.yaw = PI - b->carPose(0).h;
            player.pitch = -0.10f;
            buses.update(0.0f, clock, player.pos, b, true);
        } else {
            if (!trainOk) return false;
            const Traffic::DutyOption* opt = nullptr;
            auto opts = traffic.dutyOptions(dep - 1, dep + 1);
            for (auto& o : opts) if (o.serviceId == id) opt = &o;
            double arr = opt ? opt->arr : dep;
            double clock = std::max(arr + 8.0, dep - 100.0);
            loadingScreen("Dienst wordt klaargezet...");
            traffic.reset(clock, collision);
            fallbackClock = clock;
            buses.reset();
            Train* t = traffic.trainOf(id);
            for (int k = 0; k < 600 && (!t || !t->dwelling() || t->atIndex() != stopIdx); k++) {
                traffic.update(0.5f, collision);
                t = traffic.trainOf(id);
            }
            if (!t) return false;
            trainDuty.active = true;
            trainDuty.title = t->label() + ", vertrek " + hm(dep) + " van " + t->service().stops[stopIdx].station;
            trainDuty.note("Dienst begonnen: " + trainDuty.title);
            t->setManual(true, setRealism, true);
            t->duty = &trainDuty;
            int lc = t->leadCar();
            float ex, ey, ez;
            t->driverEye(ex, ey, ez);
            player.board(t, lc, ex, ez);
            player.driving = true;
            t->setDriver(true);
            player.car = lc;
            player.pos = t->carToWorld(lc, ex, ey - player.eyeH, ez);
            float fh = t->carPose(lc).h + (t->stock().cabSign(lc) < 0 ? PI : 0.0f);
            player.yaw = PI - fh;
            player.pitch = -0.10f;
        }
        loadingScreen("Omgeving wordt geladen...");
        world.preload(player.eye());
        state = PLAYING;
        menu.gameRunning = true;
        map.close();
        return true;
    };

    // test: meteen een dienst rijden (--dutybus LIJN UU:MM / --dutytrain UU:MM)
    bool dutyStarted = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dutybus") == 0 && i + 2 < argc) {
            int hh = 0, mm = 0;
            sscanf(argv[i + 2], "%d:%d", &hh, &mm);
            double t0 = hh * 3600.0 + mm * 60.0;
            int best = -1;
            const auto& trips = buses.trips();
            for (size_t k = 0; k < trips.size(); k++)
                if (buses.dirs()[trips[k].dir].ref == argv[i + 1] && trips[k].plan[trips[k].first] >= t0 &&
                    (best < 0 || trips[k].plan[trips[k].first] < trips[best].plan[trips[best].first]))
                    best = (int)k;
            if (best >= 0) dutyStarted = startDuty(0, best, 0, 0);
        }
        if (strcmp(argv[i], "--dutytrain") == 0 && i + 1 < argc && trainOk) {
            int hh = 0, mm = 0;
            sscanf(argv[i + 1], "%d:%d", &hh, &mm);
            auto opts = traffic.dutyOptions(hh * 3600.0 + mm * 60.0, hh * 3600.0 + mm * 60.0 + 7200);
            int pick = 0;
            for (int k = 1; k < argc; k++) if (strcmp(argv[k], "--dutypick") == 0 && k + 1 < argc) pick = atoi(argv[k + 1]);
            if (!opts.empty()) {
                const auto& o = opts[std::min(pick, (int)opts.size() - 1)];
                dutyStarted = startDuty(1, o.serviceId, o.stop, o.dep);
            }
        }
    }
    if (direct) {
        if (customPos) {
            player.pos = Vec3(startX, std::max(world.heightAt(startX, startZ), collision.floorAt(startX, startZ, 1e9f)), startZ);
            player.yaw = startYaw * PI / 180;
            player.pitch = startPitch * PI / 180;
            if (startHeight >= 0) { player.fly = true; player.pos.y += startHeight; }
        } else if (!dutyStarted) {
            doSpawn(spawn);
            if (yawSet) player.yaw = startYaw * PI / 180;
            player.pitch = startPitch * PI / 180;
            if (startHeight >= 0) { player.fly = true; player.pos.y += startHeight; }
        }
        if (forceY > -1e8f) { player.pos.y = forceY; player.fly = false; }
        menu.gameRunning = true;
    }

    // wereld vooruitspoelen / tests
    Input noInput;
    auto vehicles = [&]() {
        std::vector<Vehicle*> v;
        for (Train* t : traffic.trains()) v.push_back(t);
        for (CityBus* b : buses.buses()) v.push_back(b);
        return v;
    };
    float worldDt = -1;  // multiplayer: tijdstap voor treinen en bussen (echte frametijd + bijsturen naar de serverklok)
    auto simulate = [&](float dt, const Input& in) {
        stations.update(dt, player.pos, collision);  // wist ook de bewegende botsingsobjecten
        float wdt = worldDt >= 0 ? worldDt : dt;
        if (trainOk) {
            traffic.update(wdt, collision);
            for (Train* t : traffic.trains()) t->addCollision(collision);
        }
        fallbackClock += wdt;
        buses.update(wdt, trainOk ? traffic.clock() : fallbackClock, player.pos, player.vehicle, player.seat >= 0);
        if (trainOk)
            tpeople.update(dt, traffic.trains(), traffic.clock(), player.pos, player.vehicle, player.car, player.lx, player.lz, collision, people.poolSize());
        buses.addCollision(collision, player.pos);
        player.update(dt, in, world, collision, vehicles());
    };
    if (direct) {
        buses.update(0.0f, trainOk ? traffic.clock() : fallbackClock, player.pos, nullptr, false);  // bussen die nu rijden
        if (rideTrain >= 1 && rideTrain <= (int)traffic.trains().size()) player.board(traffic.trains()[rideTrain - 1], 1, 0.0f, 0.0f);
        if (rideDest)
            for (Train* t : traffic.trains())
                if (t->destination().find(rideDest) != std::string::npos) { player.board(t, 1, 0.0f, 0.0f); break; }
        if (rideBus >= 1 && rideBus <= (int)buses.buses().size()) player.board(buses.buses()[rideBus - 1], 0, 0.0f, 0.0f);
        if (player.vehicle && yawSet) player.yaw = startYaw * PI / 180;
        for (int i = 1; i < argc; i++)  // test: in de trein op een bepaalde plek/hoogte (rijtuig lx lz y)
            if (strcmp(argv[i], "--inside") == 0 && i + 4 < argc && player.vehicle) {
                player.car = atoi(argv[i + 1]);
                player.lx = (float)atof(argv[i + 2]);
                player.lz = (float)atof(argv[i + 3]);
                player.pos = player.vehicle->carToWorld(player.car, player.lx, (float)atof(argv[i + 4]), player.lz);
            }
        for (float t = 0; t < simTime; t += 0.05f) simulate(0.05f, noInput);
        for (int i = 1; i < argc; i++)  // test: bij een bushalte staan (naam richting afstand hoek): camera op de haltepaal
            if (strcmp(argv[i], "--athalte") == 0 && i + 4 < argc) {
                const auto& dirs = buses.dirs();
                int d = atoi(argv[i + 2]);
                if (d < 0 || d >= (int)dirs.size()) break;
                for (const LineStop& st : dirs[d].stops)
                    if (st.name == argv[i + 1]) {
                        const LineDir& D = dirs[d];
                        size_t k = std::upper_bound(D.cum.begin(), D.cum.end(), st.s) - D.cum.begin();
                        k = std::max<size_t>(1, std::min(k, D.pts.size() - 1));
                        Vec3 t = normalize(Vec3(D.pts[k].x - D.pts[k - 1].x, 0, D.pts[k].z - D.pts[k - 1].z));
                        float dist = (float)atof(argv[i + 3]), ang = (float)atof(argv[i + 4]) * PI / 180;
                        Vec3 r(-t.z, 0, t.x);
                        Vec3 dir = t * std::cos(ang) + r * std::sin(ang);  // 0 = vanaf voren langs de weg
                        Vec3 eye = Vec3(st.x, 0, st.z) + dir * dist;
                        player.vehicle = nullptr;
                        player.fly = true;
                        player.pos = Vec3(eye.x, world.heightAt(eye.x, eye.z) + 0.2f, eye.z);
                        Vec3 to = Vec3(st.x, 0, st.z) - eye;
                        player.yaw = std::atan2(to.x, -to.z);
                        player.pitch = -0.05f;
                        world.preload(player.eye());
                        break;
                    }
            }
        for (int i = 1; i < argc; i++)  // test: treindienst rijden: wachten op vertrekgereed, optrekken, S seconden rijden, remmen, deuren
            if (strcmp(argv[i], "--traindrive") == 0 && i + 2 < argc) {
                Train* t = nullptr;
                for (Train* tr : traffic.trains()) if (player.vehicle == tr) t = tr;
                FILE* wf = fopen(argv[i + 2], "w");
                if (t && wf) {
                    Input fake;
                    fake.captured = true;
                    float S = (float)atof(argv[i + 1]);
                    int phase = 0;
                    float tp = 0;
                    for (float tt = 0; tt < S + 400 && phase < 5; tt += 0.05f) {
                        for (auto& p : fake.pressed) p = false;
                        Train::CabState cs = t->cabState();
                        tp += 0.05f;
                        if (phase == 0 && cs.departReady) { phase = 1; tp = 0; fprintf(wf, "[%s] vertrekgereed\n", hhmm(traffic.clock()).c_str()); }
                        if (phase == 1 && std::fmod(tp, 1.0f) < 0.05f) fake.pressed['W'] = cs.notch < 4;
                        if (phase == 1 && tp > S) { phase = 2; tp = 0; }
                        if (phase == 2 && std::fmod(tp, 0.5f) < 0.05f && cs.notch > -6) fake.pressed['S'] = true;
                        if (phase == 2 && t->speed() < 0.05f) { phase = 3; tp = 0; fake.pressed['T'] = true; }
                        if (phase == 3 && tp > 2) { phase = 4; tp = 0; }
                        if (phase == 4 && tp > 3) phase = 5;
                        if (cs.bell || cs.vigilance) fake.pressed[VK_SPACE] = true;
                        simulate(0.05f, fake);
                        static std::string lastMsg;
                        bool newMsg = cs.msg != lastMsg;
                        lastMsg = cs.msg;
                        if (std::fmod(tt, 4.0f) < 0.05f || (newMsg && !cs.msg.empty()))
                            fprintf(wf, "t=%.1f fase %d v=%.0f km/u stand %d toegestaan %.0f sein %.0f m halte %.0f m deur %.2f %s\n", tt, phase, t->speed() * 3.6f, cs.notch,
                                    cs.vPerm * 3.6f, cs.distAuth, cs.distStop, t->doorOpen(), cs.msg.c_str());
                    }
                    for (auto& l : trainDuty.log) fprintf(wf, "LOG %s\n", l.c_str());
                }
                if (wf) fclose(wf);
            }
        for (int i = 1; i < argc; i++)  // test: camera op de conducteur op het perron (afstand hoek)
            if (strcmp(argv[i], "--atconductor") == 0 && i + 2 < argc) {
                Vec3 P;
                float ch;
                if (tpeople.conductorNear(player.pos, P, ch)) {
                    float dist = (float)atof(argv[i + 1]), ang = ch + (float)atof(argv[i + 2]) * PI / 180;
                    Vec3 e = P + Vec3(std::sin(ang), 0, std::cos(ang)) * dist;
                    player.vehicle = nullptr;
                    player.fly = true;
                    player.pos = Vec3(e.x, P.y + 1.6f - player.eyeH, e.z);
                    Vec3 to = P + Vec3(0, 1.1f, 0) - player.eye();
                    player.yaw = std::atan2(to.x, -to.z);
                    player.pitch = std::atan2(to.y, std::hypot(to.x, to.z));
                    world.preload(player.eye());
                }
            }
        for (int i = 1; i < argc; i++)  // test: camera bij de halte met de meeste wachtende reizigers (afstand hoek hoogte)
            if (strcmp(argv[i], "--atwaiting") == 0 && i + 3 < argc) {
                Vec3 P, t, r;
                if (buses.busiestStop(P, t, r)) {
                    float dist = (float)atof(argv[i + 1]), ang = (float)atof(argv[i + 2]) * PI / 180, hgt = (float)atof(argv[i + 3]);
                    Vec3 c = P + r * 2.0f;
                    Vec3 e = c + (t * std::cos(ang) - r * std::sin(ang)) * dist;
                    player.vehicle = nullptr;
                    player.fly = true;
                    player.pos = Vec3(e.x, world.heightAt(e.x, e.z) + hgt - player.eyeH, e.z);
                    Vec3 to = c + Vec3(0, 1.0f, 0) - player.eye();
                    player.yaw = std::atan2(to.x, -to.z);
                    player.pitch = std::atan2(to.y, std::hypot(to.x, to.z));
                    world.preload(player.eye());
                }
            }
        for (int i = 1; i < argc; i++)  // test: S seconden meerijden/wachten, elke 20 s de bussen loggen (met stopknop op elke 3e halte)
            if (strcmp(argv[i], "--bustrace") == 0 && i + 2 < argc) {
                FILE* wf = fopen(argv[i + 2], "w");
                float S = (float)atof(argv[i + 1]);
                Input none;
                std::string lastNext;
                int n = 0;
                for (int k = 1; k < argc; k++)
                    if (strcmp(argv[k], "--boardat") == 0 && k + 1 < argc)
                        for (CityBus* b : buses.buses())
                            if (b->currentStop() == argv[k + 1]) { player.board(b, 0, 0.0f, 0.0f); break; }
                for (float t = 0; t < S; t += 0.05f) {
                    none.pressed['B'] = false;
                    CityBus* mine = nullptr;
                    for (CityBus* b : buses.buses()) if (player.vehicle == b) mine = b;
                    if (mine && mine->nextStop() != lastNext) {
                        lastNext = mine->nextStop();
                        if (++n % 3 == 0) none.pressed['B'] = true;
                        if (wf) fprintf(wf, "[%s] volgende halte %s%s\n", hhmm(trainOk ? traffic.clock() : fallbackClock).c_str(), lastNext.c_str(), none.pressed['B'] ? "  (stopknop)" : "");
                    }
                    simulate(0.05f, none);
                    if (wf && std::fmod(t, 20.0f) < 0.05f) fputs(buses.debugInfo(trainOk ? traffic.clock() : fallbackClock).c_str(), wf);
                }
                if (wf) fclose(wf);
                for (int k = 1; k < argc; k++)  // plek in de bus (lx lz) voor de schermafbeelding
                    if (strcmp(argv[k], "--buspos") == 0 && k + 2 < argc && player.vehicle) { player.car = 0; player.lx = (float)atof(argv[k + 1]); player.lz = (float)atof(argv[k + 2]); }
                if (player.vehicle && yawSet) {  // kijkrichting t.o.v. de bus (0 = naar voren)
                    player.yaw = PI - player.vehicle->carPose(player.car).h + startYaw * PI / 180;
                    player.pitch = startPitch * PI / 180;
                    simulate(0.05f, noInput);
                }
            }
        for (int i = 1; i < argc; i++)  // test: zelf rijden, S seconden gas, daarna sturen (A) en remmen; pose loggen
            if (strcmp(argv[i], "--drivetest") == 0 && i + 2 < argc && player.vehicle) {
                float ex, ey, ez;
                if (!player.vehicle->driverEye(ex, ey, ez)) break;
                player.driving = true;
                player.car = 0;
                player.vehicle->setDriver(true);
                if (!yawSet) player.yaw = PI - player.vehicle->carPose(0).h;
                player.pitch = startPitch * PI / 180;
                Input fake;
                fake.captured = true;
                FILE* wf = fopen(argv[i + 2], "w");
                float S = (float)atof(argv[i + 1]);
                for (float t = 0; t < S + 12; t += 0.05f) {
                    fake.pressed['Q'] = t == 0 && player.vehicle->doorOpen() > 0.5f;
                    fake.down['W'] = t > 2.0f && t < S;
                    fake.down['A'] = t > S * 0.5f && t < S;
                    fake.down['S'] = t >= S;
                    simulate(0.05f, fake);
                    CarPose p = player.vehicle->carPose(0);
                    if (wf && std::fmod(t, 1.0f) < 0.05f)
                        fprintf(wf, "t=%.1f x=%.1f z=%.1f h=%.1f v=%.1f deur=%.2f rijdt=%d\n", t, p.x, p.z, p.h * 180 / PI, ((CityBus*)player.vehicle)->speed() * 3.6f,
                                player.vehicle->doorOpen(), (int)player.driving);
                }
                bool leave = false;
                for (int k = 1; k < argc; k++) if (strcmp(argv[k], "--leave") == 0) leave = true;
                if (leave) {  // opstaan, uitstappen (wegteleporteren) en kijken of de computer het overneemt
                    CityBus* bus = (CityBus*)player.vehicle;
                    Input none;
                    none.pressed['E'] = true;
                    simulate(0.05f, none);
                    none.pressed['E'] = false;
                    for (float t = 0; t < 3; t += 0.05f) simulate(0.05f, none);
                    if (wf) fprintf(wf, "opgestaan: rijdt=%d deur=%.2f handmatig=%d\n", (int)player.driving, bus->doorOpen(), (int)bus->manual());
                    player.alight(player.pos + Vec3(0, 0, 30));
                    for (float t = 0; t < 30; t += 0.05f) {
                        simulate(0.05f, none);
                        CarPose p = bus->carPose(0);
                        if (wf && std::fmod(t, 3.0f) < 0.05f) fprintf(wf, "na uitstappen t=%.0f x=%.1f z=%.1f v=%.1f handmatig=%d\n", t, p.x, p.z, bus->speed() * 3.6f, (int)bus->manual());
                    }
                }
                if (wf) fclose(wf);
            }
        for (int i = 1; i < argc; i++)  // test: S seconden vooruit lopen (W), hoogte in de bak wegschrijven
            if (strcmp(argv[i], "--walk") == 0 && i + 2 < argc) {
                Input fake;
                fake.captured = true;
                fake.down['W'] = true;
                FILE* wf = fopen(argv[i + 2], "w");
                bool useFirst = false;
                for (int k = 1; k < argc; k++) if (strcmp(argv[k], "--usefirst") == 0) useFirst = true;
                for (float t = 0; t < (float)atof(argv[i + 1]); t += 0.05f) {
                    fake.pressed['E'] = useFirst && t == 0;
                    fake.down['W'] = !(useFirst && t < 0.6f);  // eerst even kijken/kiezen, dan lopen
                    simulate(0.05f, fake);
                    if (wf && t == 0) fprintf(wf, "doel: %s\n", player.hasTarget ? player.targetLabel.c_str() : "(geen)");
                    if (wf && player.vehicle) fprintf(wf, "t=%.2f rijtuig %d lx=%.2f lz=%.2f hoogte=%.2f\n", t, player.car, player.lx, player.lz, player.pos.y - player.vehicle->carPose(player.car).y);
                }
                if (wf) fclose(wf);
            }
        if (tv[0] >= 0 && (int)tv[0] < (int)traffic.trains().size()) {
            // testcamera: kijk op punt (rijtuig, lz) van trein tv[0], vanaf afstand/hoek/hoogte
            Train* t = traffic.trains()[(int)tv[0]];
            int car = std::min((int)tv[1], t->carCount() - 1);
            Vec3 target = t->carToWorld(car, 0, 2.0f, tv[2]);
            CarPose cp = t->carPose(car);
            float a = tv[4] * PI / 180;
            Vec3 fw(std::sin(cp.h), 0, std::cos(cp.h)), rt(std::cos(cp.h), 0, -std::sin(cp.h));
            Vec3 eye = target + fw * (std::cos(a) * tv[3]) + rt * (std::sin(a) * tv[3]);
            eye.y = target.y - 2.0f + tv[5];
            player.vehicle = nullptr;
            player.fly = true;
            player.pos = Vec3(eye.x, eye.y - 1.7f, eye.z);
            Vec3 d = target - eye;
            player.yaw = std::atan2(d.x, -d.z);
            player.pitch = std::atan2(d.y, std::hypot(d.x, d.z));
        }
        for (int i = 1; i < argc; i++)
            if (strcmp(argv[i], "--dumptrains") == 0 && i + 1 < argc) {
                FILE* df = fopen(argv[i + 1], "w");
                if (df) { fprintf(df, "klok %s\n", hhmm(traffic.clock()).c_str()); fputs(traffic.trainsInfo().c_str(), df); fclose(df); }
            } else if (strcmp(argv[i], "--dumpbus") == 0 && i + 1 < argc) {
                FILE* df = fopen(argv[i + 1], "w");
                if (df) { fputs(buses.debugInfo(trainOk ? traffic.clock() : fallbackClock).c_str(), df); fclose(df); }
            }
        loadingScreen("Omgeving wordt geladen...");
        world.preload(player.eye());
        if (shotMap) {
            map.aerial = shotAerial;
            if (mapScale > 0) map.setScale(mapScale);
            MapContext c;
            c.player = player.pos;
            map.open(c);
        }
    } else {
        // menu-achtergrond: rondvlucht boven Eindhoven Centraal
        float x, z;
        stationToGame(40, 0, x, z);
        loadingScreen("Omgeving wordt geladen...");
        world.preload(Vec3(x, 80, z));
    }

    double last = Platform::time();
    // ---------- multiplayer ----------
    NetPlay net;
    bool chatOpen = false, chatJustOpened = false;
    std::string chatText;
    Mat4 lastVP = Mat4::identity();
    menu.mpUrl = !serverArg.empty() ? Net::normalizeBase(serverArg) : setServer;
    menu.mpPassword = passwordArg;
    auto findVehicle = [&](int kind, int id) -> Vehicle* {
        if (kind == 1) return trainOk ? traffic.trainOf(id) : nullptr;
        if (kind == 2)
            for (CityBus* b : buses.buses())
                if (b->block() == id) return b;
        return nullptr;
    };
    auto doConnect = [&]() {
        std::string url = Net::normalizeBase(menu.mpUrl);
        loadingScreen("Verbinden met " + url + "...");
        std::string e;
        if (!net.connect(url, menu.mpPassword, chars[activeChar], e)) {
            menu.mpStatus = e;
            return false;
        }
        menu.mpStatus.clear();
        menu.mpConnected = true;
        menu.mpServerName = net.serverName;
        menu.mpUrl = url;
        if (setServer != url) { setServer = url; saveSettings(); }
        net.update(Platform::time(), 0, findVehicle);
        startGame(menu.startChoice + 1, 0, net.serverClock(Platform::time()));
        notice = net.motd.empty() ? "Verbonden met " + net.serverName : net.motd;
        noticeT = 6;
        last = Platform::time();
        return true;
    };
    if (autoConnect && !menu.mpUrl.empty() && doConnect() && customPos) {  // test: --connect met --pos/--yaw
        player.pos = Vec3(startX, std::max(world.heightAt(startX, startZ), collision.floorAt(startX, startZ, 1e9f)), startZ);
        player.yaw = startYaw * PI / 180;
        player.pitch = startPitch * PI / 180;
        world.preload(player.eye());
    }
    // test: na verbinden instappen in de snelste rijdende trein (bij iedereen dezelfde) en evt. gaan zitten:
    //   --mpride BESTEMMING [--mpseat N | --mpnear N] [--yaw GRADEN t.o.v. de kijkrichting van de stoel]
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--mpride") == 0 && i + 1 < argc && net.connected() && trainOk) {
            Train* best = nullptr;  // de rijdende trein met deze bestemming (bij iedereen dezelfde)
            for (Train* t : traffic.trains())
                if (t->destination().find(argv[i + 1]) != std::string::npos && (!best || t->speed() > best->speed())) best = t;
            if (!best) break;
            player.board(best, 1, 0.0f, 0.0f);
            player.pos = best->carToWorld(1, 0, best->floorAt(1, 0), 0);
            for (int k = 1; k + 1 < argc; k++)
                if (strcmp(argv[k], "--mpseat") == 0) {
                    int s = atoi(argv[k + 1]);
                    if (s >= 0 && s < best->seatCount()) {
                        SeatInfo si = best->seat(s);
                        player.seat = s;
                        player.car = si.car;
                        player.yaw = PI - best->carPose(si.car).h - si.facing + (yawSet ? startYaw * PI / 180 : 0.0f);
                        player.pitch = startPitch * PI / 180;
                    }
                }
            for (int k = 1; k + 1 < argc; k++)
                if (strcmp(argv[k], "--mpnear") == 0) {  // test: in het gangpad naast stoel N staan en ernaar kijken
                    int s = atoi(argv[k + 1]);
                    if (s >= 0 && s < best->seatCount()) {
                        SeatInfo si = best->seat(s);
                        player.car = si.car;
                        player.lx = 0;
                        player.lz = si.lz + 1.6f;
                        player.pos = best->carToWorld(si.car, 0, best->floorAt(si.car, player.lz), player.lz);
                        Vec3 to = best->carToWorld(si.car, si.lx, best->floorAt(si.car, si.lz) + 1.0f, si.lz) - player.eye();
                        player.yaw = std::atan2(to.x, -to.z);
                        player.pitch = std::atan2(to.y, std::hypot(to.x, to.z));
                    }
                }
            world.preload(player.eye());
            notice = "Test: in " + best->label() + fmt(" (%.0f km/u)", best->speed() * 3.6f);
            noticeT = 5;
            break;
        }

    double fpsTimer = 0;
    int fpsFrames = 0, fps = 0, frame = 0;
    double menuT = 0;
    Vec3 lastPlayerPos = player.pos;
    float lastPLx = 0, lastPLz = 0, playerSpeed = 0, playerPhase = 0, lastMouseX = 0;
    int lastPCar = 0;

    double prevNow = Platform::time();
    while (Platform::pump()) {
        double now = Platform::time();
        float dt = (float)std::min(0.05, now - last);
        last = now;
        fpsTimer += dt; fpsFrames++;
        if (fpsTimer >= 0.5) { fps = (int)(fpsFrames / fpsTimer + 0.5); fpsTimer = 0; fpsFrames = 0; }
        // zichtafstand meeschalen met de framerate (drukke steden zoals Utrecht): onder 30 fps minder ver tekenen
        // (minimaal 700 m), boven 50 fps weer verder (tot 1500 m); de mist schuift mee. Niet bij testopnames.
        {
            static double adaptT = 0;
            adaptT += dt;
            if (!shotPath && state == PLAYING && adaptT > 1.0 && fps > 0) {
                adaptT = 0;
                if (fps < 30 && world.drawDistance > 700.0f) world.drawDistance = std::max(700.0f, world.drawDistance * 0.85f);
                else if (fps > 50 && world.drawDistance < 1500.0f) world.drawDistance = std::min(1500.0f, world.drawDistance * 1.08f);
                world.fogDistance = 1300.0f * clampf(world.drawDistance / 1500.0f, 0.6f, 1.0f);
            }
        }
        int w = Platform::width(), h = Platform::height();
        Input& in = Platform::input();
        float ui = h / 900.0f;
        struct MouseKeep { float& dst; const Input& src; ~MouseKeep() { dst = src.mouseX; } } mouseKeep{lastMouseX, in};

        // ---------- chat (multiplayer) ----------
        if (chatOpen) {
            if (state != PLAYING) chatOpen = false;
            for (char ch : in.chars) {
                    // het teken van de toets waarmee je de chat opende (t/y) niet meetellen
                    if (chatJustOpened) {
                        chatJustOpened = false;
                        if (ch == 't' || ch == 'T' || ch == 'y' || ch == 'Y') continue;
                    }
                    if (ch == '\b') { if (!chatText.empty()) chatText.pop_back(); }
                    else if ((unsigned char)ch >= 32 && chatText.size() < 180) chatText += ch;
                }
            if (in.pressed[VK_RETURN]) { net.sendChat(chatText); chatText.clear(); chatOpen = false; }
            if (in.pressed[VK_ESCAPE]) { chatOpen = false; chatText.clear(); }
            // de rest van het spel ziet geen toetsen zolang je typt
            memset(in.pressed, 0, sizeof in.pressed);
            memset(in.down, 0, sizeof in.down);
        } else if (state == PLAYING && net.connected() && !map.isOpen()) {
            // T = chatten (bij treinrijden is T de deurknop: dan Y)
            bool trainDriving = player.driving && player.vehicle && player.vehicle->railVehicle();
            if (in.pressed[(unsigned char)(trainDriving ? 'Y' : 'T')]) { chatOpen = true; chatJustOpened = true; chatText.clear(); in.pressed['T'] = in.pressed['Y'] = false; }
        }
        bool showPlayers = net.connected() && in.down[VK_TAB] && state == PLAYING;

        // ---------- toetsen ----------
        bool mapToggle = in.pressed['M'] || in.pressed[VK_HOME];
        if (state == PLAYING) {
            if (map.isOpen()) {
                if (mapToggle || in.pressed[VK_ESCAPE]) { map.close(); }
            } else if (in.pressed[VK_ESCAPE]) {
                state = MENU;
                menu.screen = Menu::PAUSE;
                in.pressed[VK_ESCAPE] = false;  // anders sluit het menu in dezelfde frame meteen weer
            } else if (mapToggle) {
                MapContext c;
                c.player = player.pos;
                map.open(c);
                Platform::setCapture(false);
            } else {
                if (in.pressed[VK_F5]) camMode = (camMode + 1) % 3;  // zoals Minecraft: eerste persoon, achter je, voor je
                if (in.pressed['N']) navLine = !navLine;
                if (in.pressed[VK_RETURN] && (buses.duty.finished || trainDuty.finished)) { buses.duty.active = false; trainDuty.active = false; buses.duty.finished = trainDuty.finished = false; }
                if (!player.driving)
                    for (int k = 1; k <= std::min(9, NSPAWN); k++)  // alleen de cijfertoetsen 1-9 (niet doorlopen naar letters)
                        if (in.pressed['0' + k]) { doSpawn(k); world.preload(player.eye()); last = Platform::time(); }
            }
        }
        bool uiMode = state == MENU || map.isOpen();
        in.autoCapture = !uiMode && !shotPath;
        if (uiMode && in.captured) Platform::setCapture(false);

        // ---------- multiplayer: berichten, klok gelijk houden met de server ----------
        worldDt = -1;
        double rawDt = std::min(2.0, std::max(0.0, now - prevNow));
        prevNow = now;
        if (net.connected()) {
            net.update(now, dt, findVehicle);
            if (!net.connected()) {
                menu.mpConnected = false;
                notice = "Multiplayer: " + net.lostReason;
                noticeT = 8;
            } else if (net.hasClock() && menu.gameRunning) {
                double sc = net.serverClock(now);
                double cur = trainOk ? traffic.clock() : fallbackClock;
                double drift = std::remainder(sc - cur, 86400.0);
                if (std::fabs(drift) > 120) {
                    // te ver uit de pas (bijv. na een lange hapering): opnieuw neerzetten op de tijd van de server
                    loadingScreen("Dienstregeling wordt gelijkgezet...");
                    if (player.vehicle) { player.alight(player.pos); player.seat = -1; player.driving = false; }
                    if (trainOk) traffic.reset(sc, collision);
                    fallbackClock = sc;
                    buses.reset();
                    tpeople.reset();
                    last = Platform::time();
                } else {
                    // de echte frametijd (ook als de game hapert) plus bijsturen: achter = inhalen (max 1 s per frame), voor = langzamer
                    double corr = std::max(-0.9 * rawDt, std::min(1.0, drift * std::min(1.0, rawDt * 2.0 + 0.02)));
                    worldDt = (float)(rawDt + corr);
                }
            }
        }

        // ---------- simulatie ----------
        simulate(dt, uiMode ? noInput : in);
        net.place(now, dt, findVehicle);  // andere spelers op de plek van deze frame (mee in treinen en bussen)
        if (!uiMode && in.pressed['O'] && !tpeople.prompt.empty()) {
            tpeople.showTicket();
            notice = tpeople.notice;
            noticeT = 4;
        }
        {
            // loopsnelheid van de speler (animatie in derde persoon)
            float d = 0;
            if (player.vehicle && player.seat < 0 && !player.driving && player.car == lastPCar) d = std::hypot(player.lx - lastPLx, player.lz - lastPLz);
            else if (!player.vehicle && !player.fly) d = std::hypot(player.pos.x - lastPlayerPos.x, player.pos.z - lastPlayerPos.z);
            float sp = dt > 0 && d < 3 ? d / dt : 0;
            playerSpeed += (sp - playerSpeed) * std::min(1.0f, dt * 10);
            playerPhase = std::fmod(playerPhase + playerSpeed * dt / 1.35f, 1.0f);
            lastPlayerPos = player.pos;
            lastPLx = player.lx;
            lastPLz = player.lz;
            lastPCar = player.car;
        }

        // ---------- geluid: dichtstbijzijnde treinen ----------
        if (audioOn && trainOk) {
            std::vector<std::pair<float, const Train*>> nearT;
            for (const Train* t : traffic.trains()) {
                float best = 1e9f;
                for (int c = 0; c < t->carCount(); c++) {
                    CarPose p = t->carPose(c);
                    best = std::min(best, length(Vec3(p.x, p.y + 2, p.z) - player.eye()));
                }
                if (player.vehicle == t) best = -1;
                nearT.push_back({best, t});
            }
            std::sort(nearT.begin(), nearT.end(), [](auto& a, auto& b) { return a.first < b.first; });
            for (int k = 0; k < SOUND_SLOTS; k++) {
                if (k >= (int)nearT.size() || nearT[k].first > 900 || state == MENU) {
                    TrainSoundState silent;
                    silent.id = -1;
                    trainSound[k].set(silent, 0, 0, false);
                    continue;
                }
                const Train& tr = *nearT[k].second;
                float gain = 1.0f, pan = 0.0f;
                bool inside = player.vehicle == &tr;
                if (!inside) {
                    float best = 1e9f;
                    Vec3 bp;
                    for (int c = 0; c < tr.carCount(); c++) {
                        CarPose p = tr.carPose(c);
                        Vec3 cp(p.x, p.y + 2.0f, p.z);
                        float d = length(cp - player.eye());
                        if (d < best) { best = d; bp = cp; }
                    }
                    best = std::max(0.0f, best - 6.0f);
                    gain = 1.0f / (1.0f + std::pow(best / 12.0f, 1.6f));
                    Vec3 to = normalize(bp - player.eye());
                    Vec3 rightV(std::cos(player.yaw), 0, std::sin(player.yaw));
                    pan = clampf(dot(to, rightV), -1, 1) * 0.7f;
                }
                trainSound[k].set(tr.sound(), gain * 0.9f, pan, inside);
            }
        }

        // ---------- interactie ----------
        std::string prompt;
        Train* inTrain = nullptr;
        CityBus* inBus = nullptr;
        for (Train* t : traffic.trains()) if (player.vehicle == t) inTrain = t;
        for (CityBus* b : buses.buses()) if (player.vehicle == b) inBus = b;
        if (state == PLAYING && !map.isOpen()) {
            if (player.vehicle) {
                if (player.driving && inTrain) prompt = "";
                else if (player.driving) prompt = player.doorBlocked ? "Eerst de deuren sluiten (Q) - met open deuren kun je niet wegrijden" : "";
                else if (!tpeople.prompt.empty() && inTrain) prompt = tpeople.prompt;
                else if (player.seat >= 0) prompt = "E / klik: opstaan";
                else if (player.hasTarget) prompt = player.targetLabel;
                else if (player.vehicle->doorOpen() > 0.9f) prompt = "Deuren open: loop door de deur naar buiten om uit te stappen";
            }
            if (!player.vehicle) {
                for (Train* t : traffic.trains()) {
                    if (!t->doorsOpen()) continue;
                    float dmin = 1e9f, dy = 0;
                    for (int c = 0; c < t->carCount(); c++) {
                        float lx, lz;
                        t->worldToCar(c, player.pos, lx, lz);
                        float d = std::hypot(std::max(0.0f, std::fabs(lx) - t->halfWidth()), std::max(0.0f, std::fabs(lz) - t->carLen(c) / 2));
                        if (d < dmin) { dmin = d; dy = player.pos.y - t->carPose(c).y; }
                    }
                    if (dmin < 6.0f && std::fabs(dy - 0.8f) < 1.0f)
                        prompt = "Deuren open: loop de trein in (" + t->stock().product + " naar " + t->destination() + ")";
                }
                for (CityBus* b : buses.buses()) {
                    if (b->doorOpen() < 0.9f) continue;
                    CarPose c = b->carPose(0);
                    if (std::hypot(player.pos.x - c.x, player.pos.z - c.z) < 12)
                        prompt = "Deuren open: loop de bus in (" + std::string(b->display()) + ")";
                }
                for (const Elevator& e : stations.elevators) {
                    bool atBottom = std::hypot(player.pos.x - e.bottom.x, player.pos.z - e.bottom.z) < 1.6f && std::fabs(player.pos.y - e.bottom.y) < 1.0f;
                    bool atTop = std::hypot(player.pos.x - e.top.x, player.pos.z - e.top.z) < 1.6f && std::fabs(player.pos.y - e.top.y) < 1.0f;
                    if (atBottom || atTop) {
                        prompt = atBottom ? "E: lift naar " + e.label : "E: lift naar " + e.topLabel;
                        if (in.pressed['E']) player.pos = atBottom ? e.top : e.bottom;
                        break;
                    }
                }
            }
        }

        if (w <= 0 || h <= 0) continue;
        renderer.resize(w, h);

        // ---------- daglicht ----------
        double clock = trainOk ? traffic.clock() : defaultMinutes * 60.0;
        SkyState sky = computeSky(dayOfYear, std::fmod(clock, 86400.0));
        world.skyColor = sky.sky;
        world.sunDir = sky.sunDir;
        world.sunColor = sky.sunColor;
        world.ambient = sky.ambient;

        // ---------- camera ----------
        Vec3 eye = player.eye(), fwd = player.forward();
        if (camMode != 0 && menu.gameRunning) {
            if (player.driving && player.vehicle) {
                // zelf rijden: volgcamera buiten het voertuig (achter of voor), gericht op de bestuurdersplek
                Vehicle* v = player.vehicle;
                int dc = v->driverCar();
                CarPose cp = v->carPose(dc);
                float fs = 1.0f;
                for (Train* t : traffic.trains())
                    if (t == v && t->stock().cabSign(dc) < 0) fs = -1.0f;
                Vec3 vf(std::sin(cp.h) * fs, 0, std::cos(cp.h) * fs);
                Vec3 target = eye + Vec3(0, -0.4f, 0);
                float back = v->railVehicle() ? 16.0f : 11.0f, up = v->railVehicle() ? 5.5f : 4.0f;
                if (camMode == 1) eye = target - vf * back + Vec3(0, up, 0);
                else eye = target + vf * (back * 0.8f) + Vec3(0, 1.0f, 0);
                fwd = normalize(target - eye);
            } else {
                // derde persoon: achter of voor je personage
                float dist = player.vehicle ? 1.7f : 3.4f;
                Vec3 f2 = player.vehicle ? normalize(Vec3(fwd.x, 0, fwd.z)) : fwd;
                if (camMode == 1) eye = eye - f2 * dist + Vec3(0, player.vehicle ? 0.05f : 0.30f, 0);
                else {
                    eye = eye + f2 * dist + Vec3(0, 0.05f, 0);
                    fwd = fwd * -1.0f;
                }
                if (!player.vehicle) eye.y = std::max(eye.y, world.heightAt(eye.x, eye.z) + 0.3f);
                else {
                    // onder het plafond blijven
                    CarPose cp = player.vehicle->carPose(player.car);
                    float ceilY = cp.y + player.vehicle->floorAt3(player.car, player.lx, player.lz, player.pos.y - cp.y) + 1.95f;
                    eye.y = std::min(eye.y, ceilY);
                }
            }
        }
        if (state == MENU && !menu.gameRunning) {
            menuT += dt;
            float cx, cz;
            stationToGame(40, 0, cx, cz);
            float a = (float)(menuT * 0.03) + 2.2f;
            eye = Vec3(cx + std::sin(a) * 260, 95, cz + std::cos(a) * 260);
            Vec3 target(cx, 25, cz);
            fwd = normalize(target - eye);
        }
        world.update(eye);
        Mat4 proj = Mat4::perspective(70 * PI / 180, (float)w / h, 0.1f, 3200.0f);
        Mat4 view = Mat4::lookAt(eye, eye + fwd, Vec3(0, 1, 0));
        Mat4 vp = proj * view;
        lastVP = vp;

        MapContext mctx;
        mctx.player = player.pos;
        mctx.yaw = player.yaw;
        mctx.inVehicle = player.vehicle != nullptr;
        mctx.speed = inTrain ? inTrain->speed() : inBus ? inBus->speed() : 0;
        mctx.clock = hhmm(clock);
        for (const Train* t : traffic.trains()) {
            MapMarker m;
            m.kind = t->stock().product == "Intercity" ? MapMarker::TRAIN_IC : MapMarker::TRAIN_SPR;
            for (int c = 0; c < t->carCount(); c++) {
                CarPose p = t->carPose(c);
                m.cars.push_back(Vec3(p.x, p.h, p.z));
                m.lens.push_back(t->carLen(c));
            }
            m.label = t->stock().product + " " + t->destination();
            {
                // hover-info op de kaart
                const RollingStock& rs = t->stock();
                char buf[96];
                m.info.push_back(rs.product + " " + t->service().number + " naar " + t->destination());
                const StopTime* ns = t->nextStopTime();
                if (ns && t->stopped()) {
                    m.info.push_back("Staat op: " + ns->station + (ns->track ? ", spoor " + std::to_string(ns->track) : ""));
                    m.info.push_back("Vertrek: " + hhmm(ns->dep));
                } else if (ns) {
                    m.info.push_back("Volgende halte: " + ns->station + (ns->track ? ", spoor " + std::to_string(ns->track) : ""));
                    m.info.push_back("Aankomst (gepland): " + hhmm(ns->arr));
                } else {
                    m.info.push_back("Volgende halte: geen (rijdt naar het eindpunt)");
                }
                m.info.push_back("Eindbestemming: " + t->destination());
                snprintf(buf, sizeof buf, "%.0f km/u%s", t->speed() * 3.6f, t->waitingForSignal() ? " (wacht voor sein)" : "");
                m.info.push_back(std::string("Snelheid: ") + buf);
                float len = rs.totalLength();
                snprintf(buf, sizeof buf, "NS %s, %d bakken, %.0f m", rs.typeName.c_str(), rs.cars, len);
                m.info.push_back(std::string("Materieel: ") + buf);
                m.info.push_back("Vervoerder: NS");
                if (player.vehicle == t) m.info.push_back("Jij: zit in deze trein");
            }
            mctx.markers.push_back(m);
        }
        for (CityBus* b : buses.buses()) {
            MapMarker m;
            m.kind = MapMarker::BUS;
            for (int c = 0; c < b->carCount(); c++) {
                CarPose p = b->carPose(c);
                m.cars.push_back(Vec3(p.x, p.h, p.z));
                m.lens.push_back(b->carLen(c));
            }
            m.info = b->infoLines(trainOk ? traffic.clock() : fallbackClock);
            if (player.vehicle == b) m.info.push_back("Jij: zit in deze bus");
            mctx.markers.push_back(m);
        }

        bool fullMap = state == PLAYING && map.isOpen();
        if (!fullMap) {
            // trein in beeld? (omhullende van alle bakken)
            auto trainVisible = [&](const Train* t) {
                Vec3 mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
                for (int c = 0; c < t->carCount(); c++) {
                    CarPose p = t->carPose(c);
                    float r = t->carLen(c) * 0.5f + 2.0f;
                    mn = Vec3(std::min(mn.x, p.x - r), std::min(mn.y, p.y - 1.5f), std::min(mn.z, p.z - r));
                    mx = Vec3(std::max(mx.x, p.x + r), std::max(mx.y, p.y + 6.0f), std::max(mx.z, p.z + r));
                }
                return world.visible(mn, mx);
            };
            renderer.beginScene(world.skyColor);
            world.beginDraw(vp, eye);
            world.drawStatic();
            railways.draw(world);
            stations.drawOpaque(world);
            busStation.draw(world);
            buses.drawOpaque(world, eye, trainOk ? traffic.clock() : fallbackClock);
            for (Train* t : traffic.trains()) {
                CarPose c = t->carPose(t->carCount() / 2);
                if (std::hypot(c.x - eye.x, c.z - eye.z) < std::min(2600.0f, world.drawDistance + 600.0f) && trainVisible(t)) t->drawOpaque(world);
            }
            // navigatielijn op de weg (bus in dienst)
            if (inBus && inBus->duty() && navLine && player.driving) {
                MeshBuilder nb;
                float s0 = inBus->pathPos();
                const Vec3 C(0.15f, 0.85f, 1.0f);
                for (float d = 4; d < 170; d += 3) {
                    float sa = s0 + d, sb = sa + 2.2f;
                    if (sb > inBus->pathEnd()) break;
                    Vec3 a = inBus->posAt(sa), b = inBus->posAt(sb);
                    Vec3 dir = normalize(Vec3(b.x - a.x, 0, b.z - a.z)), r(dir.z, 0, -dir.x);
                    float wdt = 0.28f * (1.0f - d / 200.0f);
                    float ya = world.heightAt(a.x, a.z) + 0.07f, yb = world.heightAt(b.x, b.z) + 0.07f;
                    nb.quad(Vec3(a.x, ya, a.z) - r * wdt, Vec3(a.x, ya, a.z) + r * wdt, Vec3(b.x, yb, b.z) + r * wdt * 0.2f, Vec3(b.x, yb, b.z) - r * wdt * 0.2f,
                            Vec3(0, 1, 0), C, (float)MAT_LIGHT);
                }
                Mesh nm = nb.upload();
                world.setMode(MODE_VEHICLE);
                world.setModel(Mat4::identity());
                drawMesh(nm);
                destroyMesh(nm);
            }
            // personages: reizigers, conducteurs, machinisten en (derde persoon) jijzelf
            {
                std::vector<PersonDraw> persons;
                double pc = trainOk ? traffic.clock() : fallbackClock;
                buses.collectPeople(persons, eye, pc);
                if (trainOk) tpeople.collect(persons, traffic.trains(), eye);
                if (menu.gameRunning) {
                    PersonDraw pd;
                    pd.look = LOOK_PLAYER;
                    pd.interior = player.vehicle != nullptr;
                    pd.anim.t = (float)now;
                    pd.anim.headPitch = clampf(-player.pitch * 0.6f, -0.5f, 0.6f);
                    if (player.driving && player.vehicle) {
                        float ex, ey, ez;
                        player.vehicle->driverEye(ex, ey, ez);
                        int dc = player.vehicle->driverCar();
                        CarPose cp = player.vehicle->carPose(dc);
                        float fs = (inTrain && inTrain->stock().cabSign(dc) < 0) ? -1.0f : 1.0f;
                        Vec3 e = player.vehicle->carToWorld(dc, ex, ey, ez - 0.10f * fs);
                        pd.pos = e - Vec3(0, player.sitEye - 0.09f, 0);
                        pd.h = cp.h + (fs < 0 ? PI : 0.0f);
                        pd.sitting = true;
                        pd.anim.kind = ANIM_DRIVE;
                        if (inBus) pd.anim.wheel = clampf(inBus->wheelAngle() * 2.0f, -1, 1);
                    } else if (player.vehicle && player.seat >= 0) {
                        SeatInfo si = player.vehicle->seat(player.seat);
                        CarPose cp = player.vehicle->carPose(si.car);
                        float fy = si.y >= 0 ? si.y : player.vehicle->floorHeight();
                        pd.pos = player.vehicle->carToWorld(si.car, si.lx - std::sin(si.facing) * 0.10f, fy + player.vehicle->seatHeight() + 0.09f,
                                                            si.lz - std::cos(si.facing) * 0.10f);
                        pd.h = cp.h + si.facing;
                        pd.sitting = true;
                        pd.anim.kind = ANIM_SIT;
                    } else {
                        pd.pos = player.pos;
                        pd.h = PI - player.yaw;
                        if (playerSpeed > 0.25f) {
                            pd.anim.kind = ANIM_WALK;
                            pd.anim.speed = playerSpeed;
                            pd.anim.phase = playerPhase;
                        }
                    }
                    if (camMode != 0) persons.push_back(pd);
                    // multiplayer: dit van jezelf naar de anderen sturen
                    if (net.connected() && state == PLAYING) {
                        NetPose np;
                        np.pos = pd.pos;
                        np.h = pd.h;
                        np.sitting = pd.sitting;
                        np.anim = (int)pd.anim.kind;
                        np.speed = pd.anim.speed;
                        np.headPitch = pd.anim.headPitch;
                        np.wheel = pd.anim.wheel;
                        np.fly = player.fly;
                        if (player.vehicle && (inTrain || inBus)) {
                            np.vkind = inTrain ? 1 : 2;
                            np.vid = inTrain ? inTrain->service().id : inBus->block();
                            np.car = player.driving ? player.vehicle->driverCar() : player.seat >= 0 ? player.vehicle->seat(player.seat).car : player.car;
                            CarPose cp = player.vehicle->carPose(np.car);
                            player.vehicle->worldToCar(np.car, pd.pos, np.lx, np.lz);
                            np.ly = pd.pos.y - cp.y;
                            np.lh = pd.h - cp.h;
                            if (inBus && inBus->block() < 0) np.vkind = 0;  // eigen bus (zelf rijden buiten dienst): alleen wereldpositie
                        }
                        net.sendPose(now, np);
                    }
                }
                // andere spelers
                for (auto& kv : net.players()) {
                    RemotePlayer& r = kv.second;
                    if (!r.hasPose || length(r.pos - eye) > 400) continue;
                    if (r.modelDirty) {
                        destroyCharacter(r.model);
                        r.model = buildCharacter(r.look);
                        r.modelDirty = false;
                    }
                    PersonDraw pd;
                    pd.model = &r.model;
                    pd.pos = r.pos;
                    pd.h = r.h;
                    pd.sitting = r.target.sitting;
                    pd.interior = r.target.vkind != 0;
                    pd.anim.kind = (AnimKind)std::max(0, std::min((int)ANIM_SIT_PHONE, r.target.anim));
                    pd.anim.speed = r.target.speed;
                    pd.anim.phase = r.phase;
                    pd.anim.headPitch = r.target.headPitch;
                    pd.anim.wheel = r.target.wheel;
                    pd.anim.t = (float)(r.id * 3.7);
                    persons.push_back(pd);
                }
                charR.begin(vp, world);
                for (const PersonDraw& p : persons) {
                    Vec3 to = p.pos - eye;
                    if (dot(to, fwd) < -2.0f && length(to) > 4.0f) continue;  // achter de camera
                    const CharModel& m = p.model ? *p.model : people.model(p.look);
                    CharPose pose;
                    AnimState an = p.anim;
                    an.t += (float)now;
                    computePose(m, an, pose);
                    Mat4 W = Mat4::pose(p.pos.x, p.sitting ? p.pos.y - m.hipY : p.pos.y, p.pos.z, p.h);
                    charR.draw(m, W, pose, p.interior);
                    if (p.prop >= 0) charR.drawProp(p.prop, W, pose, B_RHAND, p.interior);
                }
                charR.end();
                world.beginDraw(vp, eye);
            }
            world.beginTransparent();
            stations.drawGlass(world);
            buses.drawGlass(world, eye);
            for (Train* t : traffic.trains()) {
                CarPose c = t->carPose(t->carCount() / 2);
                if (std::hypot(c.x - eye.x, c.z - eye.z) < 1500 && trainVisible(t)) t->drawGlass(world);
            }
            world.endTransparent();
            glBindVertexArray(0);
            renderer.endScene();
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, w, h);
        }

        if (state == MENU) {
            if (menu.screen == Menu::DUTY && menu.dutyDirty) fillDuty();
            Menu::Action act = menu.draw(text, in, w, h);
            if (act == Menu::START) startGame(menu.startChoice + 1, menu.minutes);
            else if (act == Menu::CONNECT) doConnect();
            else if (act == Menu::DISCONNECT) {
                net.disconnect();
                menu.mpConnected = false;
                menu.screen = Menu::PAUSE;
                notice = "Multiplayer: verbinding verbroken";
                noticeT = 4;
            }
            else if (act == Menu::RESUME) state = PLAYING;
            else if (act == Menu::QUIT) break;
            else if (act == Menu::SETTINGS) {
                setWindowMode = menu.windowMode;
                setVsync = menu.vsync;
                setRealism = menu.realism;
                Train::defaultRealism = setRealism;
                Platform::setWindowMode(setWindowMode);
                Platform::setVSync(setVsync);
                saveSettings();
            } else if (act == Menu::SAVE_CHARS) {
                chars = menu.chars;
                activeChar = menu.activeChar;
                saveChars();
                people.setPlayer(chars[activeChar]);
                applyLook();
                net.sendLook(chars[activeChar]);
                notice = "Personage opgeslagen: " + chars[activeChar].name;
                noticeT = 3;
            } else if (act == Menu::START_DUTY && menu.dutySel >= 0 && menu.dutySel < (int)menu.dutyItems.size()) {
                const DutyItem it = menu.dutyItems[menu.dutySel];
                if (!startDuty(menu.dutyKind, it.id, it.stop, it.time)) {
                    notice = "Deze dienst kon niet worden klaargezet - kies een andere";
                    noticeT = 4;
                }
            }
            else if (act == Menu::TO_MAIN) {
                menu.gameRunning = false;
                menu.screen = Menu::MAIN;
                if (net.connected()) { net.disconnect(); menu.mpConnected = false; }
            }
            // personage-editor: 3D-voorbeeld rechts (draaien met de muis)
            if (menu.showPreview && state == MENU && menu.screen == Menu::CHARACTER && menu.previewW > 10 && menu.previewH > 10) {
                if (menu.previewDirty || !preview.valid()) {
                    destroyCharacter(preview);
                    preview = buildCharacter(menu.edit);
                    menu.previewDirty = false;
                }
                bool inRect = in.mouseX > menu.previewX && in.mouseX < menu.previewX + menu.previewW && in.mouseY > menu.previewY &&
                              in.mouseY < menu.previewY + menu.previewH;
                if (in.lDown && inRect) menu.previewYaw += (in.mouseX - lastMouseX) * 0.01f;
                else menu.previewYaw += dt * 0.25f;
                int vx = (int)menu.previewX, vy = h - (int)(menu.previewY + menu.previewH), vw = (int)menu.previewW, vh = (int)menu.previewH;
                glViewport(vx, vy, vw, vh);
                glEnable(GL_SCISSOR_TEST);
                glScissor(vx, vy, vw, vh);
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
                glClear(GL_DEPTH_BUFFER_BIT);
                float S = preview.stature;
                Vec3 target(0, S * 0.53f, 0), camP(0, S * 0.60f, S * 1.25f + 0.65f);
                Mat4 pv = Mat4::perspective(32 * PI / 180, (float)vw / vh, 0.1f, 50.0f) * Mat4::lookAt(camP, target, Vec3(0, 1, 0));
                charR.beginStudio(pv, camP);
                AnimState an;
                an.t = (float)now;
                an.variant = 0;
                CharPose pose;
                computePose(preview, an, pose);
                charR.draw(preview, Mat4::rotY(menu.previewYaw), pose, false);
                charR.end();
                glDisable(GL_SCISSOR_TEST);
                glViewport(0, 0, w, h);
            }
            last = std::max(last, Platform::time() - 0.05);
        } else if (fullMap) {
            float tx, tz;
            int sp, vi;
            if (shotHover) {
                float sx, sy;
                if (map.nearestMarkerScreen(mctx, w, h, sx, sy)) { in.mouseX = sx; in.mouseY = sy; }
            }
            if (map.drawFull(text, in, mctx, w, h, tx, tz, sp, vi)) {
                // markers: eerst alle treinen, dan alle bussen (zelfde volgorde als hierboven)
                Vehicle* target = nullptr;
                std::string what;
                if (vi >= 0 && vi < (int)traffic.trains().size()) {
                    Train* t = traffic.trains()[vi];
                    target = t;
                    what = t->label();
                } else if (vi >= (int)traffic.trains().size()) {
                    int bi = vi - (int)traffic.trains().size();
                    if (bi < (int)buses.buses().size()) { target = buses.buses()[bi]; what = "de bus"; }
                }
                if (target) {
                    // in het voertuig, bij de dichtstbijzijnde deur van het aangeklikte rijtuig
                    int bestCar = 0;
                    float bd = 1e9f;
                    for (int c = 0; c < target->carCount(); c++) {
                        CarPose p = target->carPose(c);
                        float d = std::hypot(p.x - tx, p.z - tz);
                        if (d < bd) { bd = d; bestCar = c; }
                    }
                    float lz = 0;
                    for (Train* t : traffic.trains())
                        if (t == target) lz = t->stock().doorZ[bestCar][0];
                    if (target != player.vehicle) player.board(target, bestCar, 0.0f, lz);
                    player.pos = target->carToWorld(bestCar, 0, target->floorAt(bestCar, lz), lz);
                    notice = "Je bent ingestapt: " + what;
                } else {
                    if (sp > 0) doSpawn(sp);
                    else teleport(tx, tz);
                    notice = sp > 0 ? std::string("Geteleporteerd naar ") + SPAWNS[sp - 1].name : "Geteleporteerd";
                }
                loadingScreen("Omgeving wordt geladen...");
                world.preload(player.eye());
                last = Platform::time();
                map.close();
                noticeT = 3.0f;
            }
        } else {
            // ---------- minimap ----------
            map.drawMini(text, mctx, w, h);

            // ---------- HUD ----------
            text.begin(w, h);
            double rdx = rdX(player.pos.x), rdy = rdY(player.pos.z);
            float x = 16 * ui, y = 14 * ui, fs = 20 * ui, lh = 26 * ui;
            std::vector<std::pair<std::string, Color4>> lines;
            const Color4 WHITE{1, 1, 1, 1}, INFO{0.75f, 0.85f, 1, 1}, YEL{1, 0.85f, 0.2f, 1};
            auto departures = [&](const std::string& station) {
                if (!trainOk) return;
                for (const Departure& d : traffic.departures(station, 5, 2400)) {
                    std::string when;
                    float in_ = (float)(d.time - clock);
                    if (d.train && d.train->stopped() && d.train->currentStation() == station)
                        when = in_ > 0 ? "staat klaar, vertrekt over " + mmss(in_) : "vertrekt nu";
                    else
                        when = in_ > 60 ? fmt("over %d min", (int)(in_ / 60)) : "zo";
                    lines.push_back({fmt("%s  %-9s %s, spoor %d  (%s)", hhmm(d.time).c_str(), d.product.c_str(), d.dest.c_str(), d.track, when.c_str()), INFO});
                }
            };
            if (inTrain) {
                lines.push_back({inTrain->label(), YEL});
                if (inTrain->stopped()) {
                    float dep = (float)(inTrain->departureTime() - clock);
                    lines.push_back({fmt("Station: %s, spoor %d   vertrek %s (%s)", inTrain->currentStation().c_str(), inTrain->currentTrack(),
                                         hhmm(inTrain->departureTime()).c_str(), dep > 0 ? ("over " + mmss(dep)).c_str() : "wacht op vertrek"),
                                     WHITE});
                } else {
                    std::string nx = inTrain->nextStop();
                    if (nx.empty()) lines.push_back({fmt("Rijdt verder naar %s (buiten de spelwereld)   %.0f km/u", inTrain->destination().c_str(), inTrain->speed() * 3.6f), WHITE});
                    else lines.push_back({fmt("Volgende station: %s   %.0f km/u", nx.c_str(), inTrain->speed() * 3.6f), WHITE});
                    if (inTrain->waitingForSignal()) lines.push_back({"Wacht voor een rood sein", INFO});
                }
                if (inTrain->service().next && inTrain->stopped() && inTrain->lastStopIndex() == (int)inTrain->service().stops.size() - 1)
                    lines.push_back({"Deze trein keert hier en vertrekt als " + inTrain->service().next->product() + " naar " + inTrain->service().next->dest + " - je kunt blijven zitten", INFO});
                if (!inTrain->service().next && (inTrain->nextStop().empty() || (inTrain->stopped() && inTrain->currentStation() == inTrain->service().stops.back().station)))
                    lines.push_back({"Laatste station in de spelwereld - hierna stap je automatisch uit", INFO});
                lines.push_back({fmt("Rijtuig %d van %d  (%s)", player.car + 1, inTrain->carCount(), inTrain->stock().typeName.c_str()), INFO});
            } else if (inBus && inBus->manual()) {
                lines.push_back({player.driving ? "Je rijdt zelf de bus" : "Bus geparkeerd (stap uit, dan rijdt de chauffeur weer verder)", YEL});
                lines.push_back({fmt("%.0f km/u%s   deuren %s", std::fabs(inBus->speed()) * 3.6f, inBus->speed() < -0.05f ? " (achteruit)" : "",
                                     inBus->doorOpen() > 0.01f ? "open" : "dicht"), WHITE});
                if (!player.driving) lines.push_back({player.seat >= 0 ? "Je zit" : "Je staat", INFO});
            } else if (inBus) {
                lines.push_back({inBus->lineText(), YEL});
                std::string nx = inBus->nextStop();
                if (inBus->stopped() && inBus->departureIn() >= 0)
                    lines.push_back({fmt("Halte: %s   vertrek over %s", inBus->currentStop().c_str(), mmss(inBus->departureIn()).c_str()), WHITE});
                else if (inBus->stopped())
                    lines.push_back({"Eindhalte " + inBus->currentStop() + " - iedereen uitstappen", WHITE});
                else lines.push_back({fmt("Volgende halte: %s   %.0f km/u", nx.empty() ? "-" : nx.c_str(), inBus->speed() * 3.6f), WHITE});
                if (inBus->stopRequested()) lines.push_back({"STOP aangevraagd - de bus stopt bij " + nx, {1, 0.4f, 0.3f, 1}});
                lines.push_back({std::string(player.seat >= 0 ? "Je zit" : "Je staat") + "   |   B: stopknop", INFO});
            } else {
                {
                    // plaatsnaam: dichtstbijzijnde station of halte van een buslijn
                    std::string place = "Eindhoven";
                    float bd = 1e9f;
                    for (const MapPoint& mp : world.points)
                        if (mp.kind == POINT_STATION) {
                            float d = std::hypot(player.pos.x - mp.x, player.pos.z - mp.z);
                            if (d < bd) { bd = d; place = mp.name.rfind("Eindhoven", 0) == 0 ? "Eindhoven" : mp.name; }
                        }
                    for (const LineDir& D : buses.dirs())
                        for (const LineStop& st : D.stops) {
                            float d = std::hypot(player.pos.x - st.x, player.pos.z - st.z);
                            if (d < bd && !st.place.empty()) { bd = d; place = st.place; }
                        }
                    lines.push_back({place, YEL});
                }
                lines.push_back({fmt("RD %.0f, %.0f   NAP %+.1f m", rdx, rdy, player.pos.y), WHITE});
                if (stations.nearCentraal(player.pos)) {
                    lines.push_back({"Station Eindhoven Centraal - vertrek", WHITE});
                    departures("Eindhoven Centraal");
                } else {
                    // dichtstbijzijnde station langs de lijn
                    std::string st;
                    float bd = 300;
                    for (const MapPoint& mp : world.points)
                        if (mp.kind == POINT_STATION && mp.name != "Eindhoven Centraal") {
                            float d = std::hypot(player.pos.x - mp.x, player.pos.z - mp.z);
                            if (d < bd) { bd = d; st = mp.name; }
                        }
                    if (!st.empty()) {
                        lines.push_back({"Station " + st + " - vertrek", WHITE});
                        departures(st);
                    }
                }
                {
                    std::vector<std::string> deps;
                    std::string halte = buses.stopInfo(player.pos, clock, deps);
                    if (!halte.empty()) {
                        lines.push_back({"Bushalte " + halte + (deps.empty() ? "  - vandaag geen bussen meer" : ""), WHITE});
                        for (auto& d : deps) lines.push_back({"   " + d, INFO});
                    }
                }
                lines.push_back({fmt("%s   |   %d fps   |   zicht %d m   |   %d tegels   |   %d treinen", player.fly ? "Vliegen" : "Lopen", fps,
                                     (int)world.drawDistance, world.loadedTiles(), (int)traffic.trains().size()),
                                 INFO});
                if (net.connected()) {
                    double drift = std::remainder(net.serverClock(now) - (trainOk ? traffic.clock() : fallbackClock), 86400.0);
                    lines.push_back({fmt("Multiplayer: %s, %d speler(s), ping %d ms, klok %+.1f s", net.serverName.c_str(), (int)net.players().size() + 1,
                                         net.pingMs, drift),
                                     INFO});
                }
                DataStore::Stats ds = DataStore::stats();
                if (DataStore::remote() && (ds.active > 0 || ds.failed > 0))
                    lines.push_back({fmt("Wereld downloaden: %d bezig, %.0f MB binnen%s", ds.active, ds.bytes / 1048576.0, ds.failed ? fmt(", %d mislukt", ds.failed).c_str() : ""),
                                     ds.failed ? YEL : INFO});
            }
            float bw = 0;
            for (auto& l : lines) bw = std::max(bw, text.measure(l.first, fs));
            text.rect(8 * ui, 8 * ui, bw + 20 * ui, lines.size() * lh + 12 * ui, {0, 0, 0, 0.45f});
            for (auto& l : lines) { text.text(x, y, l.first, fs, l.second); y += lh; }

            if (!prompt.empty()) {
                float pw = text.measure(prompt, 24 * ui);
                text.rect((w - pw) / 2 - 14 * ui, h * 0.62f, pw + 28 * ui, 40 * ui, {0, 0, 0, 0.55f});
                text.text((w - pw) / 2, h * 0.62f + 6 * ui, prompt, 24 * ui, YEL);
            }
            const Color4 GREY{0.70f, 0.74f, 0.80f, 1};
            auto delayStr = [&](double dl, Color4& col) {
                int m = (int)std::lround(dl / 60.0);
                if (m > 0) { col = {1.0f, 0.35f, 0.30f, 1}; return "+" + std::to_string(m) + " min te laat"; }
                if (m < 0) { col = {0.45f, 0.75f, 1.0f, 1}; return std::to_string(-m) + " min te vroeg"; }
                col = {0.45f, 0.95f, 0.45f, 1};
                return std::string("op tijd");
            };
            // ---- dienst rijden: bus (navigatie en punctualiteit) ----
            if (inBus && inBus->duty() && inBus->inService()) {
                const LineTrip& T = inBus->trip();
                const LineDir& D = inBus->dir();
                int k = inBus->dutyStop() >= 0 ? inBus->dutyStop() : std::min(inBus->nextIndex(), T.last);
                float s = inBus->pathPos();
                std::vector<std::pair<std::string, Color4>> nl;
                nl.push_back({"Lijn " + D.ref + " naar " + inBus->destination(), YEL});
                // afslag
                auto headingAt = [&](float q) { Vec3 a = inBus->posAt(q - 3), b = inBus->posAt(q + 3); return std::atan2(b.x - a.x, b.z - a.z); };
                std::string turn = "Volg de weg";
                for (float d = 8; d < 320 && s + d + 70 < inBus->pathEnd(); d += 4) {
                    float dh = wrapAngle(headingAt(s + d + 12) - headingAt(s + d - 12));
                    if (std::fabs(dh) > 0.55f) {
                        float tot = 0, prev = headingAt(s + d - 12);
                        for (float q = d - 8; q < d + 60; q += 4) { float hq = headingAt(s + q); tot += std::fabs(wrapAngle(hq - prev)); prev = hq; }
                        std::string what = tot > 2.6f ? "rotonde" : dh > 0 ? "linksaf" : "rechtsaf";
                        turn = d < 25 ? "Nu " + what : "Over " + std::to_string((int)(std::round(d / 10) * 10)) + " m " + what;
                        break;
                    }
                }
                if (inBus->offRoute() > 20) nl.push_back({fmt("Van de route af (%.0f m) - keer terug naar de route", inBus->offRoute()), {1.0f, 0.35f, 0.3f, 1}});
                else nl.push_back({turn, WHITE});
                if (k <= T.last) {
                    float ds = inBus->stopS(k) - s;
                    if (inBus->dutyStop() >= 0)
                        nl.push_back({"Halte " + D.stops[k].name + (k == T.last ? " (eindhalte)" : "") + "   vertrek " + hhmm(T.plan[k]), WHITE});
                    else
                        nl.push_back({fmt("Volgende halte: %s  (%.0f m)   gepland %s", D.stops[k].name.c_str(), std::max(0.0f, ds), hhmm(T.plan[k]).c_str()), WHITE});
                }
                Color4 dc;
                double bdl = inBus->dutyDelay(clock);
                std::string dtxt = delayStr(bdl, dc);
                bool waitHere = (inBus->dutyStop() >= 0 || (inBus->nextIndex() == T.first && std::fabs(inBus->stopS(T.first) - s) < 30)) && bdl < 0;
                if (waitHere) {
                    dtxt = "vertrek over " + mmss((float)-bdl) + (inBus->dutyStop() < 0 ? " - eerst de deuren openen (Q) voor instappers" : "");
                    dc = WHITE;
                }
                nl.push_back({"Dienstregeling: " + dtxt, dc});
                int waiting = inBus->waitingAtNext(clock);
                if (inBus->stopRequestedFor(inBus->nextIndex()) && inBus->dutyStop() < 0) nl.push_back({"STOP aangevraagd - stoppen bij " + D.stops[std::min(inBus->nextIndex(), T.last)].name, {1, 0.4f, 0.3f, 1}});
                else if (waiting > 0 && inBus->dutyStop() < 0) nl.push_back({std::to_string(waiting) + (waiting == 1 ? " reiziger wacht" : " reizigers wachten") + " - stoppen en deuren open (Q)", {1, 0.85f, 0.3f, 1}});
                else if (inBus->dutyStop() < 0 && k < T.last) nl.push_back({"Geen instappers of stopknop - je mag doorrijden", INFO});
                nl.push_back({"Reizigers in de bus: " + std::to_string(inBus->passengers()), INFO});
                float nw = 0;
                for (auto& l : nl) nw = std::max(nw, text.measure(l.first, 21 * ui));
                float nx = (w - nw) / 2 - 16 * ui, ny = 10 * ui;
                text.rect(nx, ny, nw + 32 * ui, nl.size() * 28 * ui + 14 * ui, {0.02f, 0.06f, 0.14f, 0.78f});
                text.rect(nx, ny, 5 * ui, nl.size() * 28 * ui + 14 * ui, {0.15f, 0.85f, 1.0f, 1});
                float yy = ny + 8 * ui;
                for (size_t q = 0; q < nl.size(); q++) {
                    text.text(nx + 16 * ui, yy, nl[q].first, (q == 1 ? 24.0f : 20.0f) * ui, nl[q].second);
                    yy += 28 * ui;
                }
            }
            // ---- treinrijden: cabinescherm (DMI) ----
            if (inTrain && player.driving) {
                Train::CabState cs = inTrain->cabState();
                float v = inTrain->speed() * 3.6f;
                float px = 18 * ui, pwd = 470 * ui, phh = 300 * ui, py = h - phh - 46 * ui;
                text.rect(px, py, pwd, phh, {0.02f, 0.03f, 0.06f, 0.85f});
                text.rect(px, py, pwd, 4 * ui, {0.15f, 0.45f, 0.95f, 1});
                text.text(px + 18 * ui, py + 10 * ui, fmt("%.0f", v), 64 * ui, WHITE);
                text.text(px + 18 * ui + text.measure(fmt("%.0f", v), 64 * ui) + 8 * ui, py + 42 * ui, "km/u", 20 * ui, GREY);
                float lim = cs.realism == 0 ? cs.vLine * 3.6f : cs.realism == 1 ? cs.vPerm * 3.6f : (float)cs.code;
                std::string limTxt = cs.realism == 0 ? fmt("Max %.0f km/u", lim) : cs.realism == 1 ? fmt("Toegestaan %.0f km/u", lim) : fmt("ATB-code %d", cs.code);
                text.text(px + 230 * ui, py + 18 * ui, limTxt, 21 * ui, {1, 0.85f, 0.2f, 1});
                std::string notch = cs.notch == -7 ? "SNELREM" : cs.notch < 0 ? fmt("Rem B%d", -cs.notch) : cs.notch == 0 ? "Rijremschakelaar 0" : fmt("Tractie P%d", cs.notch);
                text.text(px + 230 * ui, py + 48 * ui, cs.realism == 0 ? (cs.notch > 0 ? "Gas" : cs.notch < 0 ? "Remmen" : "Uitrollen") : notch, 19 * ui, WHITE);
                // snelheidsbalk
                float bx = px + 18 * ui, by2 = py + 96 * ui, bwid = pwd - 36 * ui;
                text.rect(bx, by2, bwid, 12 * ui, {0.2f, 0.2f, 0.25f, 1});
                text.rect(bx, by2, bwid * clampf(v / 160.0f, 0, 1), 12 * ui, v > lim + 3 ? Color4{1, 0.3f, 0.25f, 1} : Color4{0.9f, 0.9f, 0.95f, 1});
                text.rect(bx + bwid * clampf(lim / 160.0f, 0, 1) - 2 * ui, by2 - 5 * ui, 4 * ui, 22 * ui, {1, 0.85f, 0.2f, 1});
                float ty = by2 + 24 * ui;
                const StopTime* ns = inTrain->nextStopTime();
                if (ns) {
                    double dl;
                    if (inTrain->dwelling()) {
                        dl = clock - ns->dep;
                        text.text(bx, ty, fmt("Station %s, spoor %d   vertrek %s", ns->station.c_str(), ns->track, hhmm(ns->dep).c_str()), 19 * ui, WHITE);
                    } else {
                        double eta = clock + std::max(0.0f, cs.distStop) / std::max(8.0f, inTrain->speed());
                        dl = eta - ns->arr;
                        text.text(bx, ty, fmt("Volgende halte: %s (sp. %d)  %.1f km   aankomst %s", ns->station.c_str(), ns->track, std::max(0.0f, cs.distStop) / 1000.0f,
                                              hhmm(ns->arr).c_str()), 19 * ui, WHITE);
                    }
                    ty += 26 * ui;
                    Color4 dc;
                    std::string dtxt = delayStr(dl, dc);
                    if (inTrain->dwelling() && dl < 0) {
                        dtxt = "vertrek over " + mmss((float)-dl);
                        dc = WHITE;
                    }
                    text.text(bx, ty, "Dienstregeling: " + dtxt, 19 * ui, dc);
                    ty += 26 * ui;
                }
                if (cs.distAuth < 3000)
                    text.text(bx, ty, fmt("Sein: stop over %.0f m", std::max(0.0f, cs.distAuth)), 19 * ui, cs.distAuth < 600 ? Color4{1, 0.35f, 0.3f, 1} : Color4{1, 0.85f, 0.3f, 1});
                else
                    text.text(bx, ty, "Rijweg vrij", 19 * ui, {0.45f, 0.95f, 0.45f, 1});
                ty += 28 * ui;
                bool blink = std::fmod(now, 0.8) < 0.45;
                if (cs.emergency) text.text(bx, ty, "SNELREMMING - wacht tot stilstand" + std::string(cs.realism > 0 ? ", dan rijremschakelaar in de remstand" : ""), 19 * ui, {1, 0.3f, 0.25f, 1});
                else if (cs.bell && blink) text.text(bx, ty, cs.realism == 2 ? "ATB-BEL: remmen en bevestigen (spatie)" : "ATB-BEL: te hard - remmen!", 20 * ui, {1, 0.3f, 0.25f, 1});
                else if (cs.vigilance && blink) text.text(bx, ty, "Waakzaamheid: druk op spatie", 20 * ui, {1, 0.75f, 0.2f, 1});
                else if (!cs.ready) text.text(bx, ty, std::string(!cs.cabActive ? "Cabine activeren (K)  " : "") + (!cs.panto ? "Stroomafnemer omhoog (P)" : "Systemen starten..."), 19 * ui, {1, 0.75f, 0.2f, 1});
                else if (cs.departReady) text.text(bx, ty, "Vertrekgereed - je mag vertrekken", 19 * ui, {0.45f, 0.95f, 0.45f, 1});
                else if (inTrain->doorOpen() > 0.02f) text.text(bx, ty, "Deuren open", 19 * ui, GREY);
                ty += 26 * ui;
                if (!cs.msg.empty()) text.text(bx, ty, cs.msg, 17 * ui, {1, 0.9f, 0.6f, 1});
            }
            // ---- dienstverslag ----
            {
                DutyStats* ds = buses.duty.active ? &buses.duty : trainDuty.active ? &trainDuty : nullptr;
                if (ds) {
                    ds->msgT = std::max(0.0f, ds->msgT - dt);
                    if (ds->finished) {
                        std::vector<std::string> sl;
                        sl.push_back(ds->title);
                        size_t from = ds->log.size() > 12 ? ds->log.size() - 12 : 0;
                        for (size_t q = from; q < ds->log.size(); q++) sl.push_back(ds->log[q]);
                        sl.push_back("Enter: verslag sluiten (je kunt gewoon verder spelen)");
                        float sw = 0;
                        for (auto& l : sl) sw = std::max(sw, text.measure(l, 18 * ui));
                        float sx = (w - sw) / 2 - 20 * ui, sy = h * 0.18f;
                        text.rect(sx, sy, sw + 40 * ui, sl.size() * 26 * ui + 20 * ui, {0.02f, 0.06f, 0.14f, 0.92f});
                        float yy = sy + 10 * ui;
                        for (size_t q = 0; q < sl.size(); q++) {
                            text.text(sx + 20 * ui, yy, sl[q], 18 * ui, q == 0 ? YEL : q + 1 == sl.size() ? INFO : WHITE);
                            yy += 26 * ui;
                        }
                    } else if (ds->msgT > 0 && !(inTrain && player.driving)) {
                        float mw = text.measure(ds->msg, 20 * ui);
                        text.rect((w - mw) / 2 - 14 * ui, h * 0.70f, mw + 28 * ui, 36 * ui, {0.02f, 0.06f, 0.14f, 0.85f});
                        text.text((w - mw) / 2, h * 0.70f + 6 * ui, ds->msg, 20 * ui, WHITE);
                    }
                }
            }
            std::string help = "WASD lopen   Muis kijken   Shift snel   Spatie springen   E kiezen (richtkruis)   F vliegen   F5 camera   M kaart   1-9 startpunten   Esc menu";
            if (net.connected()) help = "WASD lopen   Muis kijken   Spatie springen   E kiezen   F vliegen   F5 camera   M kaart   T chat   Tab spelers   Esc menu";
            if (player.driving && inTrain) {
                int rl = inTrain->cabState().realism;
                help = rl == 0 ? "W gas   S rem   E opstaan   F5 camera   M kaart   Esc menu"
                       : rl == 1 ? "W/S rijremschakelaar   T deuren vrijgeven   Spatie ATB bevestigen   Backspace snelrem   E opstaan   F5 camera"
                                 : "W/S rijremschakelaar   Q/R deuren links/rechts   Spatie bevestigen   K cabine   P stroomafnemer   Backspace snelrem   E opstaan";
            } else if (player.driving) {
                help = "W gas   S rem / achteruit (vasthouden)   A D sturen   Q deuren   N navigatielijn   E opstaan   F5 camera   M kaart";
            } else if (player.vehicle) {
                help = "WASD lopen   E kiezen (richtkruis)   B stopknop (bus)   O kaartje laten zien   F5 camera   M kaart   Esc menu";
            }
            float hw = text.measure(help, 16 * ui);
            text.rect((w - hw) / 2 - 10 * ui, h - 34 * ui, hw + 20 * ui, 26 * ui, {0, 0, 0, 0.45f});
            text.text((w - hw) / 2, h - 32 * ui, help, 16 * ui, {1, 1, 1, 0.9f});

            if (!in.captured && !shotPath) {
                std::string s = "Klik in het venster om te spelen";
                float sw = text.measure(s, 30 * ui);
                text.rect((w - sw) / 2 - 20 * ui, h / 2.0f - 30 * ui, sw + 40 * ui, 60 * ui, {0, 0, 0, 0.55f});
                text.text((w - sw) / 2, h / 2.0f - 20 * ui, s, 30 * ui, WHITE);
            } else {
                if (player.hasTarget) {
                    // richtkruis op een doel: gele ring
                    const Color4 Y{1, 0.85f, 0.2f, 1};
                    float r = 9 * ui, t = 2.5f * ui;
                    text.rect(w / 2.0f - r, h / 2.0f - r, 2 * r, t, Y);
                    text.rect(w / 2.0f - r, h / 2.0f + r - t, 2 * r, t, Y);
                    text.rect(w / 2.0f - r, h / 2.0f - r, t, 2 * r, Y);
                    text.rect(w / 2.0f + r - t, h / 2.0f - r, t, 2 * r, Y);
                    text.rect(w / 2.0f - 2, h / 2.0f - 2, 4, 4, Y);
                } else {
                    text.rect(w / 2.0f - 2, h / 2.0f - 2, 4, 4, {1, 1, 1, 0.8f});
                }
            }
            text.end();
        }
        // ---------- multiplayer: namen boven de hoofden, chat, spelerslijst ----------
        if (net.connected() && state == PLAYING) {
            text.begin(w, h);
            if (!map.isOpen()) {
                for (auto& kv : net.players()) {
                    const RemotePlayer& r = kv.second;
                    if (!r.hasPose) continue;
                    float top = r.target.sitting ? 1.0f : (r.model.valid() ? r.model.stature : 1.75f) + 0.30f;
                    Vec3 p = r.pos + Vec3(0, top, 0);
                    float dist = length(p - eye);
                    if (dist > 120 || dot(p - eye, fwd) < 0.5f) continue;
                    const float* m = lastVP.m;
                    float cx = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], cy = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
                    float cw = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
                    if (cw <= 0.1f) continue;
                    float sx = (cx / cw * 0.5f + 0.5f) * w, sy = (0.5f - cy / cw * 0.5f) * h;
                    float fs2 = clampf(26.0f - dist * 0.12f, 14.0f, 24.0f) * ui;
                    float nw = text.measure(r.name, fs2);
                    float a = clampf((120 - dist) / 30, 0, 1);
                    text.rect(sx - nw / 2 - 6 * ui, sy - fs2 * 1.3f, nw + 12 * ui, fs2 * 1.3f, {0, 0, 0, 0.45f * a});
                    text.text(sx - nw / 2, sy - fs2 * 1.25f, r.name, fs2, {1, 1, 1, a});
                }
            }
            // chat linksonder (boven de hulpregel)
            const auto& cl = net.chat();
            float cy = h - 70 * ui, cfs = 18 * ui;
            if (chatOpen) {
                std::string line = "Zeg: " + chatText + (((GetTickCount() / 500) & 1) ? "_" : "");
                float lw = std::max(420 * ui, text.measure(line, cfs) + 20 * ui);
                text.rect(14 * ui, cy - 4 * ui, lw, 30 * ui, {0, 0.13f, 0.43f, 0.9f});
                text.text(24 * ui, cy, line, cfs, {1, 1, 1, 1});
                cy -= 34 * ui;
            }
            int shown = 0;
            for (int i = (int)cl.size() - 1; i >= 0 && shown < (chatOpen ? 12 : 6); i--) {
                double age = now - cl[i].t;
                if (!chatOpen && age > 15) break;
                float a = chatOpen ? 1.0f : clampf((float)(15 - age), 0, 1);
                std::string line = cl[i].system ? cl[i].text : cl[i].name + ": " + cl[i].text;
                float lw = text.measure(line, cfs);
                text.rect(14 * ui, cy - 2 * ui, lw + 20 * ui, 26 * ui, {0, 0, 0, 0.45f * a});
                text.text(24 * ui, cy, line, cfs, cl[i].system ? Color4{1, 0.85f, 0.2f, a} : Color4{1, 1, 1, a});
                cy -= 28 * ui;
                shown++;
            }
            // spelerslijst (Tab)
            if (showPlayers) {
                std::vector<std::string> names;
                names.push_back(chars[activeChar].name + "  (jij)");
                for (auto& kv : net.players()) names.push_back(kv.second.name);
                float bw2 = 420 * ui, rh = 30 * ui, bx = (w - bw2) / 2, by = h * 0.18f;
                text.rect(bx, by, bw2, rh * (names.size() + 1.6f), {0.03f, 0.05f, 0.09f, 0.88f});
                text.text(bx + 16 * ui, by + 8 * ui, net.serverName + fmt("  -  %d online", (int)names.size()) + (net.pingMs >= 0 ? fmt(", %d ms", net.pingMs) : ""),
                          19 * ui, {1, 0.8f, 0.1f, 1});
                for (size_t i = 0; i < names.size(); i++) text.text(bx + 16 * ui, by + rh * (i + 1.3f), names[i], 19 * ui, {1, 1, 1, 1});
            }
            text.end();
        }
        // melding
        if (noticeT > 0) {
            noticeT -= dt;
            text.begin(w, h);
            float nw = text.measure(notice, 22 * ui);
            float a = clampf(noticeT, 0, 1);
            text.rect((w - nw) / 2 - 16 * ui, h * 0.30f, nw + 32 * ui, 44 * ui, {0.0f, 0.13f, 0.43f, 0.9f * a});
            text.text((w - nw) / 2, h * 0.30f + 8 * ui, notice, 22 * ui, {1, 1, 1, a});
            text.end();
        }

        if (!shotPath || world.streamingIdle()) frame++;
        if (shotPath && frame >= shotFrames) {
            renderer.saveScreenshot(shotPath);
            break;
        }
        Platform::swap();
    }
    if (audioOn) Audio::shutdown();
    return 0;
}
