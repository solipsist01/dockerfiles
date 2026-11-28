#pragma once
// Treinmodellen uit Train Simulator (omgezet met tools/ts/trains.py naar data/trains/<naam>/: model.bin + tex/*.tex).
// Getekend met een eigen shader (textures, zelfde belichting en mist als de wereld). Deuren schuiven open, wielen
// draaien; de rest staat vast in het rijtuig. Het model komt pas van de server als er een trein van dat type in de
// buurt is; tot dan tekent de game het eigen (procedurele) model.
#include <memory>
#include <string>
#include <vector>
#include "../engine/renderer.h"

class World;

class TsModel {
public:
    explicit TsModel(const std::string& name) : name_(name) {}
    ~TsModel();
    // game-thread: laden starten / afmaken zonder te wachten; true = klaar om te tekenen
    bool ready();
    bool loadNow();  // blokkerend (tests)
    int cars() const { return (int)cars_.size(); }
    // koppelpunten van rijtuig i (lokale z, voor = +)
    float front(int i) const { return cars_[i].front; }
    float back(int i) const { return cars_[i].back; }

    struct DrawState {
        float doorOpen[2] = {0, 0};  // deuren aan lokale +x / -x: 0 dicht .. 1 open
        float wheel = 0;             // wielhoek (rad)
        bool lit = false;            // binnenverlichting aan (avond)
        bool head = false, tail = false;  // koplampen / sluitseinen van dit rijtuig aan
        bool inside = false;         // speler zit in de trein: TS-interieur in plaats van het eenvoudige
    };
    // pass 0: dicht + alfatest, pass 1: glas (mengen; aanroepen tussen beginTransparent/endTransparent)
    void drawCar(const World& w, int car, const Mat4& m, const DrawState& s, int pass) const;

    static void beginFrame();  // shader opnieuw instellen bij de volgende drawCar

    // Loopvloer uit het model (walk.bin, tools/ts/walkgrid.py): per cel van 10 cm de vloeren waarop je kunt staan
    // (balkons, treden, beide dekken). Alleen als het model getekend wordt (anders de eigen looplogica).
    // y = vloer die het dichtst bij curY ligt (treden gladgestreken); false = hier geen vloer binnen een stap.
    bool walkFloor(int car, float x, float z, float curY, float& y, float tol = 0.45f) const;
    bool hasWalk(int car) const { return state_ == 2 && car >= 0 && car < (int)walk_.size() && walk_[car].nx > 0; }

private:
    struct Node { std::string name; int parent; Mat4 local; };
    struct Batch {
        int tex, kind, flags, node;
        float range;
        float color[4];
        GLsizei first, count;
    };
    struct Leaf { int node; int side; float slideZ; float outX; };  // deurblad: schuift slideZ (m), naar buiten outX
    struct Car {
        std::string name;
        float front, back, zmin, zmax, ymax;
        std::vector<Node> nodes;
        std::vector<Batch> batches;
        std::vector<Leaf> leaves;
        std::vector<int> wheels;   // knopen die om x draaien
        std::vector<int> moving;   // knopen met een eigen matrix (batch.node), in volgorde
        GLuint vao = 0, vbo = 0, ibo = 0;
    };
    std::string name_;
    int state_ = 0;  // 0 niets, 1 bestanden worden opgehaald, 2 klaar, -1 mislukt
    std::vector<Car> cars_;
    struct Tex { std::string file; int alpha; GLuint id = 0; };
    std::vector<Tex> tex_;
    bool parse(const std::string& path);
    bool upload();
    std::vector<std::vector<float>> vtx_;     // per rijtuig, tot upload
    std::vector<std::vector<uint32_t>> idx_;
    Mat4 nodeMatrix(const Car& c, int node, const DrawState& s) const;
    struct Walk {
        float x0 = 0, z0 = 0, cell = 0.1f;
        int nx = 0, nz = 0, layers = 0;
        std::vector<uint16_t> mm;  // [iz][ix][laag] hoogte in mm, 0 = geen
        float at(int ix, int iz, int k) const { return mm[((size_t)iz * nx + ix) * layers + k] * 0.001f; }
        float nearest(int ix, int iz, float y, float tol) const;  // laag het dichtst bij y (of -1)
    };
    std::vector<Walk> walk_;
    void parseWalk(const std::string& path);
};

// alle modellen (een per treintype), op naam
TsModel* tsModel(const std::string& name);
