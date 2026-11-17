#pragma once
// Kaart (M of Home) en minimap (rechtsboven).
//  - getekend: wegen, water, landgebruik en plaatsnamen uit OSM (data/world/map.bin), spoor, perrons, stations,
//    bushaltes; of luchtfoto (PDOK) als ondergrond. Wisselen met L of de knop.
//  - live: alle treinen en bussen, de speler (pijl). De kaart volgt de speler (ook in trein of bus).
//  - teleporteren: klik op de kaart (of op een station in de lijst).
#include <list>
#include <string>
#include <unordered_map>
#include <vector>
#include "../engine/platform.h"
#include "../engine/renderer.h"
#include "../engine/text.h"
#include "world.h"

struct MapMarker {
    enum Kind { TRAIN_SPR, TRAIN_IC, BUS } kind;
    std::vector<Vec3> cars;   // per rijtuig: x, z, heading (in y)
    std::vector<float> lens;
    std::string label;
    std::vector<std::string> info;  // hover-info op de kaart: regel 0 = kop, daarna "Naam: waarde"
};

struct MapContext {
    Vec3 player;
    float yaw;
    bool inVehicle;
    float speed;   // m/s (minimap zoomt uit bij snelheid)
    std::vector<MapMarker> markers;
    std::string clock;
};

struct MapPlace { std::string name; float x, z; int spawn; };  // spawn: startpunt (1..9) of 0

class MapView {
public:
    bool init(const std::string& dataDir, const World& world);
    void addPlace(const MapPlace& p) { places_.push_back(p); }
    void addPlatforms(const std::vector<std::vector<std::pair<float, float>>>& polys);
    bool isOpen() const { return open_; }
    void open(const MapContext& ctx);
    void close() { open_ = false; }
    void setScale(float s) { scale_ = s; }
    // Volledige kaart tekenen en bedienen. Geeft true terug als er geteleporteerd moet worden (doel in tx, tz / spawn).
    bool drawFull(TextRenderer& text, Input& in, const MapContext& ctx, int w, int h, float& tx, float& tz, int& spawn, int& vehicle);
    void drawMini(TextRenderer& text, const MapContext& ctx, int w, int h);
    // test (--hover): schermpositie van het voertuig dat het dichtst bij het kaartmidden staat
    bool nearestMarkerScreen(const MapContext& ctx, int w, int h, float& sx, float& sy) const;
    bool aerial = false;

private:
    struct Layer { GLuint vao = 0, vbo = 0, ibo = 0; GLsizei count = 0; };
    struct V { float x, z, ox, oz, ww, wpx; uint32_t rgba; float u, v; };
    std::vector<V> lineV_, areaV_, railV_;
    std::vector<uint32_t> lineI_, areaI_, railI_;
    Layer lines_[8], areas_, rails_, dyn_;
    std::vector<int> lineStart_;  // per soort: [eerste index, aantal]
    struct Label { int kind; float x, z; std::string name; };
    std::vector<Label> labels_;
    std::vector<MapPlace> places_;
    std::vector<std::pair<float, float>> busStops_;
    GLuint prog_ = 0;
    GLint uA_ = -1, uB_ = -1, uScreen_ = -1, uScale_ = -1, uTex_ = -1, uUseTex_ = -1;
    bool open_ = false, follow_ = true, dragging_ = false;
    float cx_ = 0, cz_ = 0, scale_ = 0.5f;  // centrum (game) en pixels per meter
    float pressX_ = 0, pressY_ = 0;
    int hover_ = -1;  // marker onder de muis (volledige kaart)
    bool pressed_ = false;
    std::string dataDir_;

    // luchtfoto's per tegel (op aanvraag geladen, LRU)
    struct Photo { GLuint tex = 0; bool failed = false; };
    struct TileRef { int x, y; bool detail; };
    std::vector<TileRef> tiles_;
    std::unordered_map<int64_t, Photo> photos_;
    std::list<int64_t> lru_;
    int loadsThisFrame_ = 0;
    GLuint photoFor(const TileRef& t);

    void upload(Layer& l, const std::vector<V>& v, const std::vector<uint32_t>& i);
    void addLine(std::vector<V>& v, std::vector<uint32_t>& idx, const std::vector<std::pair<float, float>>& pts, float ww, float wpx, uint32_t c);
    void render(int x0, int y0, int w, int h, float cx, float cz, float scale, const MapContext& ctx, int screenW, int screenH, bool mini);
    void drawLayer(const Layer& l, GLsizei first, GLsizei count);
};
