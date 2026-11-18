#pragma once
// De wereld: globale gegevens (spoor, perrons, wegen, haltes) en tegels van 500 m die rond de camera worden
// geladen en weer opgeruimd (streaming). Geheugen en videogeheugen blijven zo begrensd, hoe groot de wereld ook is.
#include <algorithm>
#include <cmath>
#include <functional>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>
#include "../common/worldformat.h"
#include "../engine/renderer.h"

struct MapPoint {
    uint8_t kind;
    float x, z;
    std::string name;
};

struct MapLine {
    uint8_t kind;
    std::vector<std::pair<float, float>> pts;
};

struct BusBay {
    float x, z, heading, length;  // midden van de haven; heading wijst naar de voorkant (neus van de bus)
};

struct RailPoint { float x, y, z; int64_t node; };   // game-coordinaten, y = bovenkant ballast/dwarsligger
struct RailLine { bool main; bool tunnel; bool sunken; std::vector<RailPoint> pts; };
struct OsmPlatform { std::string name; float height; std::vector<std::pair<float, float>> pts; };
struct HallRing { float cut; std::vector<std::pair<float, float>> pts; };  // plattegrond stationshal
struct RoadNode { float x, y, z; };
struct RoadEdge { int a, b; bool oneway; uint8_t cls; float speed; };
struct WorldFeature { uint8_t kind; float a, b; std::vector<Vec3> pts; };
// Buslijn (een richting) uit OSM: route op de rechterrijstrook en haltes als positie langs de route
struct BusRoutePoint { float x, y, z, lim; };   // lim = maximumsnelheid (m/s)
struct BusRouteStop { std::string name; float x, z, s, lat; uint8_t flags; };  // flags: 1 abri, 2 bank, 4 actuele tijden
struct BusRoute { std::string ref, name; std::vector<BusRoutePoint> pts; std::vector<BusRouteStop> stops; };

// Speciale waarden in Vertex::extra voor shader-modus 1
constexpr float EX_PLAIN = -2000.0f;
constexpr float EX_GLASS = -5000.0f;
constexpr float EX_LIGHT = -6000.0f;
constexpr float EX_SIGN = 5000.0f;     // kleur = (u, v, 0) in de tekstatlas
constexpr float EX_TRACKBED = 10000.0f; // + afstand langs het spoor

// Shader-modi
enum DrawMode { MODE_GROUND = 0, MODE_OBJECT = 1, MODE_VEHICLE = 2 };

struct TileLoad;  // resultaat van de laadthread

class World {
public:
    bool load(const std::string& dataDir, std::string& error);
    ~World();
    // Streaming: tegels rond de camera aanvragen, klaargezette tegels naar de GPU, verre tegels opruimen.
    void update(Vec3 cam);
    // Alles rond pos laden en wachten tot het klaar is (opstarten, screenshots).
    void preload(Vec3 pos);
    static std::function<void()> preloadTick;  // tijdens preload af en toe aangeroepen (laadscherm bijwerken)
    bool streamingIdle() const;
    int loadedTiles() const;

    void beginDraw(const Mat4& viewProj, Vec3 camPos);  // zet shader en uniforms klaar
    void drawStatic();                                   // terrein, gebouwen, details (geladen tegels)
    void setMode(DrawMode m);
    void setModel(const Mat4& m);
    GLint modelUniform() const { return uModel_; }
    // Camera-relatief tekenen: projectie x kijkmatrix met de camera in de oorsprong (lookAt(0, fwd, up)). Elke frame
    // vóór beginDraw zetten. Modelmatrices (setModel) worden dan automatisch t.o.v. de camera doorgegeven.
    void setRelativeView(const Mat4& relVP) { relVP_ = relVP; hasRel_ = true; }
    const Mat4& relativeView() const { return relVP_; }
    Vec3 origin() const { return origin_; }
    Vec3 camPos() const { return cam_; }   // camera van de huidige frame
    float heightAt(float x, float z) const;              // maaiveldhoogte (NAP) op game-positie

    std::vector<MapPoint> points;
    std::vector<MapLine> lines;
    std::vector<BusBay> bays;
    std::vector<RailLine> rails;
    std::vector<OsmPlatform> platforms;
    std::vector<HallRing> halls;
    std::vector<RoadNode> roadNodes;
    std::vector<RoadEdge> roadEdges;
    std::vector<WorldFeature> features;
    std::vector<BusRoute> busRoutes;
    void setSignTexture(GLuint t) { signTex_ = t; }
    // een andere tekstatlas binden op de bordeneenheid (0 = de gedeelde atlas); alleen tussen beginDraw en het einde van de scene
    void bindSignTexture(GLuint t) const {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, t ? t : signTex_);
        glActiveTexture(GL_TEXTURE0);
    }
    void beginTransparent();  // glas: blending aan, diepte niet schrijven
    void endTransparent();
    Vec3 skyColor{0.66f, 0.76f, 0.86f};
    Vec3 sunDir{0.35f, 0.75f, 0.55f};      // richting naar de zon
    Vec3 sunColor{1.0f, 0.96f, 0.88f};     // kleur x sterkte van het zonlicht
    float ambient = 1.0f;                   // omgevingslicht (1 = middag)
    float fogDistance = 1300.0f;   // mist: ~60% op deze afstand, vrijwel dicht op 2x
    // Tekenafstand voor gebouwen en details (m). De game verlaagt hem als de framerate zakt (drukke steden zoals
    // Utrecht) en verhoogt hem weer als er ruimte is; laden gebeurt nog steeds tot DETAIL_RADIUS.
    float drawDistance = 1500.0f;
    bool visible(Vec3 mn, Vec3 mx) const { return frustum_.boxVisible(mn, mx); }  // in beeld (na beginDraw)
    static float boxDistance(Vec3 p, Vec3 mn, Vec3 mx) {
        float dx = std::max({mn.x - p.x, 0.0f, p.x - mx.x}), dz = std::max({mn.z - p.z, 0.0f, p.z - mx.z});
        return std::sqrt(dx * dx + dz * dz);
    }
    int drawnChunks = 0;

    // Laadafstanden (m, tot de rand van een tegel)
    static constexpr float DETAIL_RADIUS = 1500.0f;
    static constexpr float GROUND_RADIUS = 650.0f;   // materiaalkaart (fijne grondtextuur)
    static constexpr float COARSE_RADIUS = 2900.0f;

private:
    struct Tile {
        int x, y;            // RD zuidwesthoek
        bool detail;
        int state = 0;       // 0 = niet geladen, 1 = wordt geladen, 2 = op de GPU
        Mesh terrain;
        std::vector<Mesh> buildings, ground, details;
        int photoSlot = -1, groundSlot = -1, coarseSlot = -1;
        bool wantGround = false;
        Vec3 bmin, bmax;
    };
    std::vector<Tile> tiles_;
    std::unordered_map<int64_t, int> tileIdx_;
    int gx0_ = 0, gy0_ = 0, gcols_ = 0, grows_ = 0;  // raster (500 m) van de opzoektextuur
    std::string dataDir_;

    // hoogtecache (synchroon geladen uit de tegelbestanden), LRU
    struct HeightTile { int n = 0; std::vector<float> h; };
    mutable std::unordered_map<int, HeightTile> heights_;
    mutable std::list<int> lru_;
    const HeightTile* heightTile(int tile) const;

    // GPU-slots
    static constexpr int PHOTO_SLOTS = 56, GROUND_SLOTS = 24, COARSE_SLOTS = 176;
    std::vector<int> freePhoto_, freeGround_, freeCoarse_;
    GLuint photoTex_ = 0, groundTex_ = 0, coarseTex_ = 0, lookupTex_ = 0, signTex_ = 0;
    std::vector<float> lookup_;  // per rastercel: fotoslot+1, grondslot+1, grofslot+1, 0
    bool lookupDirty_ = true;
    int photoLevels_ = 0;

    GLuint prog_ = 0;
    GLint uViewProj_, uModel_, uMode_, uPhoto_, uGround_, uCoarse_, uLookup_, uSigns_, uGridOrigin_, uSun_, uCam_, uFog_, uSky_,
        uFogDist_;
    Frustum frustum_;
    Vec3 cam_;
    Mat4 relVP_ = Mat4::identity();
    bool hasRel_ = false;
    Vec3 origin_;

    // laadthread
    void* thread_ = nullptr;
    struct Shared;
    Shared* sh_ = nullptr;
    static unsigned long __stdcall threadMain(void* p);
    void finishLoad(TileLoad* r);
    void unloadTile(Tile& t);
    void requestTile(int idx, bool withGround);
    float tileDistance(const Tile& t, Vec3 p) const;
    int findTile(double rx, double ry) const;
};
