#pragma once
// Gedeelde afspraken tussen de converter en de game over de wereld en het bestandsformaat.
//
// Coordinaten: de game gebruikt meters met de y-as omhoog.
//   game x =  RD-x - ORIGIN_X           (oost is +x)
//   game z = -(RD-y - ORIGIN_Y)         (noord is -z)
//   game y =  hoogte t.o.v. NAP
// De oorsprong ligt bij Eindhoven Centraal.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace WorldFormat {
constexpr uint32_t MAGIC = 0x3157564F;  // "OVW1"
constexpr int32_t VERSION = 5;  // 5: buslijnen
constexpr uint32_t TILE_MAGIC = 0x3154564F;  // "OVT1"
constexpr int32_t TILE_VERSION = 1;

constexpr double ORIGIN_X = 161420.0;
constexpr double ORIGIN_Y = 383770.0;

// ---------- Tegels ----------
// De wereld bestaat uit tegels van 500 x 500 m (RD), elk in een eigen bestand data/world/tiles/X_Y.bin
// (X_Y = RD-coordinaat van de zuidwesthoek). De game laadt alleen tegels rond de camera (streaming).
//   - detailtegels: gebouwen (3D BAG), BGT, bomen, luchtfoto 2048 px, hoogte 2 m
//   - grove tegels: alleen hoogte (AHN DSM, 10 m) en luchtfoto 256 px, als achtergrond in de mist
// Het onderstaande gebied is alleen een omhullende voor de converter; de game gebruikt de tegelindex.
constexpr int TILE = 500;
constexpr int AREA_X0 = 130500, AREA_Y0 = 366500;  // zuidgrens: Bergeijk (buslijn 18); westgrens: Utrecht Leidsche Rijn
constexpr int AREA_X1 = 176500, AREA_Y1 = 461500;  // oostgrens: Helmond (buslijn 24); noordgrens: Utrecht Zuilen

constexpr int PHOTO_CELL = TILE;
constexpr int BUILDING_CHUNK = 250;  // gebouwen gegroepeerd per 250 m (4 chunks per tegel)
constexpr float TERRAIN_SPACING = 2.0f;
constexpr int TILE_HN = TILE / 2 + 1;   // hoogtepunten per tegelzijde (detail, 2 m)
constexpr int COARSE_N = 51;            // hoogtepunten per tegelzijde (grof, 10 m)
constexpr int GROUND_PX = 2000;         // materiaalkaart 0,25 m per pixel
constexpr int COARSE_PHOTO_PX = 256;

// Traject Eindhoven Strijp-S -> Best -> Boxtel -> Vught -> 's-Hertogenbosch (RD, uit OSM) en de stationscentra
constexpr double ROUTE_PTS[][2] = {{159794, 384658}, {159087, 385502}, {158346, 386416}, {157596, 387304}, {157114, 387907},
                                   {156851, 388212}, {156652, 388522}, {156154, 389415}, {155130, 391205}, {154440, 392402},
                                   {153906, 393321}, {153124, 394710}, {152443, 395919}, {151717, 397186}, {151023, 398352},
                                   {150275, 399505}, {149432, 400603}, {149261, 401614}, {149014, 403102}, {148767, 404302},
                                   {148592, 405631}, {148461, 406792}, {148351, 407900}, {148314, 409069}, {148351, 410134},
                                   {148535, 411308}, {148600, 412300}};
// Aftakking Boxtel -> Tilburg (een stuk, zodat treinen naar Tilburg de mist in kunnen rijden)
constexpr double BRANCH_PTS[][2] = {{150275, 399505}, {149300, 399420}, {148100, 399350}};
// Traject 's-Hertogenbosch -> Utrecht Centraal (48 km, OSM-spoor, ongeveer elke km een punt):
// Zaltbommel, Geldermalsen, Culemborg, Houten Castellum, Houten, Utrecht Lunetten, Utrecht Vaartsche Rijn, Utrecht Centraal
constexpr double UTRECHT_PTS[][2] = {
    {148547, 411315}, {148833, 412306}, {148762, 413328}, {148306, 414235}, {147705, 415424}, {147267, 416344}, {147163, 417354},
    {147093, 418365}, {146962, 419878}, {146848, 420903}, {146716, 422085}, {146605, 423081}, {146493, 424091}, {146378, 425119},
    {146252, 426272}, {146138, 427275}, {146032, 428316}, {146120, 429307}, {146547, 430270}, {147002, 431270}, {147128, 432282},
    {146867, 433287}, {146619, 434262}, {146185, 435284}, {145705, 436321}, {145179, 437460}, {144690, 438517}, {144177, 439480},
    {143657, 440354}, {143143, 441221}, {142604, 442130}, {142059, 443049}, {141697, 444232}, {141466, 445285}, {141164, 446304},
    {140845, 447273}, {140486, 448254}, {140105, 449188}, {139673, 450237}, {139114, 451585}, {138634, 452532}, {138107, 453421},
    {137422, 454170}, {136587, 454726}, {136056, 455572}, {135969, 455709}};
// Voorbij Utrecht Centraal (een stuk, daarna mist): richting Amsterdam (via Zuilen) en Woerden/Gouda (via Leidsche Rijn)
constexpr double UT_NORTH_PTS[][2] = {{135962, 455722}, {135742, 456174}, {135346, 456486}, {134979, 456870}, {134611, 457231},
                                      {134217, 457602}, {133831, 457961}, {133523, 458253}};
constexpr double UT_WEST_PTS[][2] = {{136001, 455738}, {135750, 456193}, {135360, 456500}, {134823, 456564}, {134303, 456624},
                                     {133663, 456698}, {133405, 456732}};
// Aftakking Geldermalsen -> Tiel (Sprinters Tiel - Utrecht)
constexpr double TIEL_PTS[][2] = {{147049, 432692}, {147155, 432198}, {147462, 431786}, {147920, 431577}, {148462, 431526},
                                  {149006, 431477}, {149440, 431439}};
constexpr double BEST_CX = 155125, BEST_CY = 391198;
// Buslijnen (18, 8, 9, 24, 156, 401, 402): detail binnen 400 m van de routes (tools\bus_route.ps1)
#include "busroutes.inc"
constexpr double BUS_CORRIDOR = 400;
// Stations op het traject (centrum, straal van het gedetailleerde gebied)
struct StationArea { const char* name; double x, y, r; };
constexpr StationArea STATION_AREAS[] = {
    {"Best", 155125, 391198, 900}, {"Boxtel", 150275, 399505, 750}, {"Vught", 148461, 406792, 750}, {"'s-Hertogenbosch", 148535, 411308, 1000},
    {"Zaltbommel", 146460, 424360, 750}, {"Geldermalsen", 147032, 432676, 800}, {"Culemborg", 143970, 439856, 750},
    {"Houten Castellum", 140740, 447632, 700}, {"Houten", 139967, 449542, 750}, {"Utrecht Lunetten", 138273, 453152, 700},
    {"Utrecht Vaartsche Rijn", 136811, 454531, 700}, {"Utrecht Centraal", 135989, 455717, 1500}};

inline double distToSquare(double px, double py, int x0, int y0) {
    double dx = std::max({(double)x0 - px, 0.0, px - (x0 + TILE)});
    double dy = std::max({(double)y0 - py, 0.0, py - (y0 + TILE)});
    return std::sqrt(dx * dx + dy * dy);
}
inline bool isDetailTile(int x0, int y0) {
    if (x0 >= 159500 && x0 < 162000 && y0 >= 383000 && y0 < 385000) return true;  // Eindhoven (oorspronkelijk gebied)
    for (const StationArea& s : STATION_AREAS)
        if (distToSquare(s.x, s.y, x0, y0) < s.r) return true;                      // stationscentra
    auto nearLine = [&](const double (*p)[2], int n, double r) {
        for (int k = 0; k + 1 < n; k++) {
            double ax = p[k][0], ay = p[k][1], bx = p[k + 1][0], by = p[k + 1][1];
            if ((ax == 0 && ay == 0) || (bx == 0 && by == 0)) continue;  // scheiding tussen routes
            int steps = (int)(std::hypot(bx - ax, by - ay) / 25) + 1;
            for (int s = 0; s <= steps; s++)
                if (distToSquare(ax + (bx - ax) * s / steps, ay + (by - ay) * s / steps, x0, y0) < r) return true;
        }
        return false;
    };
#define OV_N(a) (int)(sizeof(a) / sizeof(a[0]))
    if (nearLine(UTRECHT_PTS, OV_N(UTRECHT_PTS), 600) || nearLine(UT_NORTH_PTS, OV_N(UT_NORTH_PTS), 600) ||
        nearLine(UT_WEST_PTS, OV_N(UT_WEST_PTS), 600) || nearLine(TIEL_PTS, OV_N(TIEL_PTS), 600))
        return true;
#undef OV_N
    return nearLine(ROUTE_PTS, sizeof(ROUTE_PTS) / sizeof(ROUTE_PTS[0]), 600) || nearLine(BRANCH_PTS, sizeof(BRANCH_PTS) / sizeof(BRANCH_PTS[0]), 600) ||
           nearLine(BUS_PTS, sizeof(BUS_PTS) / sizeof(BUS_PTS[0]), BUS_CORRIDOR);
}
struct TileId { int x, y; bool detail; };
// Alle tegels: detail + grove ring tot 2 km eromheen
inline std::vector<TileId> planTiles() {
    std::vector<TileId> out;
    std::vector<std::pair<int, int>> det;
    for (int y = AREA_Y0; y < AREA_Y1; y += TILE)
        for (int x = AREA_X0; x < AREA_X1; x += TILE)
            if (isDetailTile(x, y)) det.push_back({x, y});
    for (int y = AREA_Y0; y < AREA_Y1; y += TILE)
        for (int x = AREA_X0; x < AREA_X1; x += TILE) {
            bool d = false, ring = false;
            for (auto& t : det) {
                if (t.first == x && t.second == y) d = true;
                if (std::abs(t.first - x) <= 2000 && std::abs(t.second - y) <= 2000) ring = true;
            }
            if (d || ring) out.push_back({x, y, d});
        }
    return out;
}

// ---------- Station Eindhoven Centraal ----------
// Lokaal stationsassenstelsel: u langs de sporen (oost), v loodrecht erop (noord). Oorsprong onder perron 3/4.
constexpr double STATION_RD_X = 161425.0, STATION_RD_Y = 383770.0;
constexpr double STATION_BEARING_DEG = 12.3;  // richting van de sporen t.o.v. het oosten (tegen de klok in)
constexpr float STATION_FLOOR = 15.7f;        // vloer passage/hallen (NAP)
constexpr float STATION_NORTH_HALL_H = 5.3f;  // hoogte van de open begane grond Noordhal
constexpr float STATION_SOUTH_HALL_H = 7.0f;  // hoogte van de open begane grond Zuidhal
constexpr const char* STATION_PAND = "NL.IMBAG.Pand.0772100000294834";
// Passage onder de sporen (lokaal): u en v grenzen
constexpr float PASSAGE_U0 = -17.0f, PASSAGE_U1 = 22.0f, PASSAGE_V0 = -62.0f, PASSAGE_V1 = 48.0f;
// Gebied waar het terrein vlak wordt gemaakt (passage + winkels aan beide zijden)
constexpr float STATION_FLAT_U0 = -25.0f, STATION_FLAT_U1 = 30.5f;

// ---------- Station Eindhoven Strijp-S ----------
// Lokaal assenstelsel: u langs de perrons (naar zuidoost), v loodrecht (naar noordoost). Oorsprong = midden perrons.
// Toegang (zoals in werkelijkheid, OSM): voetgangerstunnel onder de spoordijk aan de zuidoostkant met trappen naar
// de perrons, en een trap vanaf perron 1 naar de straat aan de noordoostkant.
constexpr double STRIJPS_RD_X = 159793.9, STRIJPS_RD_Y = 384658.5;
constexpr double STRIJPS_AXIS_DEG = -49.28;
constexpr float SS_TUN_U0 = 105.0f, SS_TUN_U1 = 112.0f, SS_TUN_V0 = -30.0f, SS_TUN_V1 = 28.0f;
constexpr float SS_STAIR_W = 2.4f;
constexpr float SS_STAIR_V[3] = {-13.6f, -0.3f, 12.7f};  // trappen naar spoor 4, spoor 2|3, spoor 1
constexpr float SS_STAIR_U0 = 92.5f;                       // bovenkant trapgat (lokale u)
constexpr float SS_NE_STAIR_U = 65.0f, SS_NE_STAIR_V0 = 15.3f, SS_NE_STAIR_V1 = 27.0f;  // trap perron 1 -> straat
constexpr float SS_NE_FLOOR = 18.65f;
inline float strijpsTunnelFloor(float v) { return 18.40f + (v - SS_TUN_V0) / (SS_TUN_V1 - SS_TUN_V0) * (17.66f - 18.40f); }
inline float clampTun(float v) { return v < SS_TUN_V0 ? SS_TUN_V0 : (v > SS_TUN_V1 ? SS_TUN_V1 : v); }
inline void strijpsLocal(double rx, double ry, float& u, float& v) {
    const double a = STRIJPS_AXIS_DEG * 3.14159265358979 / 180.0;
    double dx = rx - STRIJPS_RD_X, dy = ry - STRIJPS_RD_Y;
    u = (float)(dx * std::cos(a) + dy * std::sin(a));
    v = (float)(-dx * std::sin(a) + dy * std::cos(a));
}
inline void strijpsToRd(float u, float v, double& rx, double& ry) {
    const double a = STRIJPS_AXIS_DEG * 3.14159265358979 / 180.0;
    rx = STRIJPS_RD_X + u * std::cos(a) - v * std::sin(a);
    ry = STRIJPS_RD_Y + u * std::sin(a) + v * std::cos(a);
}
// Ligt een punt in de tunnel of een trapgat van Strijp-S? Dan: vloerhoogte in h.
inline bool strijpsCut(double rx, double ry, float& h) {
    float u, v;
    strijpsLocal(rx, ry, u, v);
    if (u > SS_TUN_U0 - 2.6f && u < SS_TUN_U1 + 2.6f && v > SS_TUN_V0 - 1.0f && v < SS_TUN_V1 + 1.0f) { h = strijpsTunnelFloor(clampTun(v)); return true; }
    for (float vc : SS_STAIR_V)
        if (u > SS_STAIR_U0 - 2.0f && u < SS_TUN_U0 && std::fabs(v - vc) < SS_STAIR_W / 2 + 1.2f) { h = strijpsTunnelFloor(vc); return true; }
    if (std::fabs(u - SS_NE_STAIR_U) < SS_STAIR_W / 2 + 1.2f && v > SS_NE_STAIR_V0 - 1.0f && v < SS_NE_STAIR_V1) { h = SS_NE_FLOOR; return true; }
    return false;
}

inline float gameX(double rdx) { return float(rdx - ORIGIN_X); }
inline float gameZ(double rdy) { return float(ORIGIN_Y - rdy); }
inline double rdX(float gx) { return gx + ORIGIN_X; }
inline double rdY(float gz) { return ORIGIN_Y - gz; }

// Stationsassenstelsel (u,v) <-> game (x,z)
inline void stationToGame(float u, float v, float& x, float& z) {
    const double b = STATION_BEARING_DEG * 3.14159265358979 / 180.0;
    double rx = STATION_RD_X + u * std::cos(b) - v * std::sin(b);
    double ry = STATION_RD_Y + u * std::sin(b) + v * std::cos(b);
    x = gameX(rx);
    z = gameZ(ry);
}
inline void gameToStation(float x, float z, float& u, float& v) {
    const double b = STATION_BEARING_DEG * 3.14159265358979 / 180.0;
    double dx = rdX(x) - STATION_RD_X, dy = rdY(z) - STATION_RD_Y;
    u = (float)(dx * std::cos(b) + dy * std::sin(b));
    v = (float)(-dx * std::sin(b) + dy * std::cos(b));
}

// ---------- Station Utrecht Centraal ----------
// Lokaal assenstelsel: u langs de sporen (naar het noordnoordwesten), v dwars erop (naar het oostnoordoosten, de
// Centrumzijde; -v = Jaarbeurszijde). Oorsprong = midden van de stationshal (OSM). Spoorrichting uit OSM: 330,8 graden.
constexpr double UT_RD_X = 135990.0, UT_RD_Y = 455714.0, UT_BEARING_DEG = 330.8;
// Stationshal (OV-terminal, 2016): vloer op ~8 m boven het maaiveld, over alle sporen heen (OSM building=train_station)
constexpr float UT_HALL_U0 = -40.0f, UT_HALL_U1 = 50.5f, UT_HALL_V0 = -123.5f, UT_HALL_V1 = 121.5f;
// uitsparing in de zuidoosthoek (toren erboven)
constexpr float UT_NOTCH_U0 = -40.0f, UT_NOTCH_U1 = -25.6f, UT_NOTCH_V0 = 56.5f, UT_NOTCH_V1 = 97.5f;
inline void utToRd(float u, float v, double& rx, double& ry) {
    const double b = UT_BEARING_DEG * 3.14159265358979 / 180.0;
    rx = UT_RD_X + u * std::sin(b) + v * std::cos(b);
    ry = UT_RD_Y + u * std::cos(b) - v * std::sin(b);
}
inline void rdToUt(double rx, double ry, float& u, float& v) {
    const double b = UT_BEARING_DEG * 3.14159265358979 / 180.0;
    double dx = rx - UT_RD_X, dy = ry - UT_RD_Y;
    u = (float)(dx * std::sin(b) + dy * std::cos(b));
    v = (float)(dx * std::cos(b) - dy * std::sin(b));
}
// ligt (u, v) onder de stationshal? (de converter laat de 3D BAG-hal daar weg: de game bouwt hem zelf)
inline bool utInHall(float u, float v, float margin = 0) {
    if (u < UT_HALL_U0 - margin || u > UT_HALL_U1 + margin || v < UT_HALL_V0 - margin || v > UT_HALL_V1 + margin) return false;
    return !(u > UT_NOTCH_U0 && u < UT_NOTCH_U1 && v > UT_NOTCH_V0 && v < UT_NOTCH_V1);
}

// ---------- Station Best ----------
constexpr const char* BEST_PAND = "NL.IMBAG.Pand.0753100000002146";  // stationsgebouw (2002); de game bouwt het zelf

enum LineKind : uint8_t { LINE_RAIL = 0, LINE_PLATFORM = 1 };
enum PointKind : uint8_t { POINT_STATION = 0, POINT_BUS_STOP = 1 };
enum FeatureKind : uint8_t {
    FEAT_RETAINING_WALL = 0,  // keerwand (lijn), y = maaiveld
    FEAT_STATION_HALL = 1,    // stationsgebouw Best (omtrek), a = maaiveldhoogte
    FEAT_STEPS = 2,           // trap (lijn), a = level, b = +1 omhoog / -1 omlaag (richting van de punten)
    FEAT_ELEVATOR = 3,        // lift (punt)
    FEAT_TUNNEL_PORTAL = 4,   // tunnelmond: pts[0] = midden op spoorhoogte, pts[1] = 10 m de tunnel in; a,b = dwarsbereik
    FEAT_INDOOR_BRIDGE = 5,   // bordes/loopbrug (omtrek), a = level, b = layer
    FEAT_FOOTWAY = 6,         // voetpad op level < 0
};
}  // namespace WorldFormat

// Bestandsindeling world.bin (little endian):
//   u32 magic, i32 version
//   tegels:    i32 n; per tegel: i32 x, y (RD zuidwesthoek); u8 detail
//   lijnen:    i32 n; per lijn: u8 kind; i32 punten; (f32 x, f32 z)[punten]
//   punten:    i32 n; per punt: u8 kind; f32 x, z; u16 len; char naam[len] (Latin-1)
//   bushavens: i32 n; per haven: f32 x, z, heading, lengte
//   sporen:    i32 n; per spoor: u8 flags (1 = zijspoor, 2 = tunnel); i32 punten; (f32 x, y, z; i64 osm-knoop)[punten]
//   perrons:   i32 n; per perron: u16 len, naam; f32 hoogte; i32 punten; (f32 x, z)[punten]
//   hallen:    i32 n; per hal: f32 snijhoogte; i32 punten; (f32 x, z)[punten]
//   wegen:     i32 knopen; (f32 x, y, z)[]; i32 wegdelen; per wegdeel: i32 a, b; u8 eenrichting; u8 klasse; f32 snelheid (m/s)
//   objecten:  i32 n; per object: u8 soort; f32 a, b; i32 punten; (f32 x, y, z)[punten]
// tiles/X_Y.bin:
//   u32 magic "OVT1", i32 versie, i32 x, y; u8 detail
//   i32 n; f32 hoogte[n*n]  (rij 0 = noord; afstand TILE/(n-1))
//   i32 bytes; u8 gaten[]   (1 bit per terreinvlakje, rij-voor-rij vanaf noord; alleen detail)
//   3x (gebouwen, verhoogde vlakken, details): i32 chunks; per chunk: u32 nv, ni; Vertex[nv]; u32 index[ni]
//   i32 px; u32 bytes; RLE materiaalkaart (paren aantal, materiaal-id; rij 0 = noord, 0,25 m per pixel)
// Luchtfoto's blijven in data/raw/luchtfoto (detail, 2048 px) en data/raw/luchtfoto_lo (grof, 256 px).
