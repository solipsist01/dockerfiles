#pragma once
// Personages: uiterlijk (personage-editor), lichaam als gladde huid op een skelet (volledig in code gegenereerd),
// kleding, haar, baard, bril, hoofddeksel en tas, en procedurele animaties (staan, lopen, zitten, sturen, stopknop,
// fluiten, kaartjes controleren, op de telefoon kijken).
//
// Lokale assen van een personage: voeten op y = 0, gezicht naar +z, +x = links van het personage (zoals de bakken
// in vehicle.h). Plaatsen in de wereld met Mat4::pose(x, y, z, h): +z wijst dan naar (sin h, 0, cos h).
#include <cstdint>
#include <string>
#include <vector>
#include "../engine/renderer.h"

struct Appearance {
    std::string name = "Reiziger";
    int age = 2;          // 0 kind, 1 tiener, 2 volwassene, 3 senior
    int sex = 0;          // 0 man, 1 vrouw
    int height = 2;       // 0..4 (heel klein .. heel lang voor de leeftijd)
    int build = 1;        // 0 slank, 1 gemiddeld, 2 stevig, 3 gespierd, 4 zwaar
    int skin = 1;
    int hair = 1, hairColor = 1;
    int beard = 0;
    int glasses = 0;
    int hat = 0, hatColor = 0;
    int top = 0, topColor = 5;
    int bottom = 1, bottomColor = 17;
    int shoes = 0, shoeColor = 3;
    int bag = 0, bagColor = 0;

    std::string serialize() const;            // "naam=..;leeftijd=..;..." (een regel)
    bool parse(const std::string& line);
    static Appearance random(uint32_t seed);  // willekeurige reiziger (realistische verdeling)
    static Appearance conductor(uint32_t seed);  // NS-conducteur (uniform)
    static Appearance driver(uint32_t seed);     // machinist / buschauffeur
    float stature() const;                    // lengte in meters
};

// Velden voor de personage-editor (menu): label, lid van Appearance, namen van de keuzes
struct LookField {
    const char* label;
    int Appearance::*member;
    const char* const* names;
    int count;
    int swatch;  // 0 = tekst, 1 = huidskleur, 2 = haarkleur, 3 = kledingkleur (kleurvlakje in het menu)
};
extern const LookField LOOK_FIELDS[];
extern const int LOOK_FIELD_COUNT;
Vec3 lookSkinColor(int i);
Vec3 lookHairColor(int i);
Vec3 lookClothColor(int i);

// Skelet
enum Bone {
    B_PELVIS, B_SPINE, B_CHEST, B_NECK, B_HEAD,
    B_LUARM, B_LFARM, B_LHAND, B_RUARM, B_RFARM, B_RHAND,
    B_LTHIGH, B_LSHIN, B_LFOOT, B_RTHIGH, B_RSHIN, B_RFOOT,
    BONE_COUNT
};

struct CharModel {
    GLuint vao = 0, vbo = 0, ibo = 0;
    GLsizei count = 0;
    Vec3 joint[BONE_COUNT];   // gewrichten in rusthouding (modelruimte)
    float stature = 1.75f;
    float hipY = 0.9f;        // hoogte van de heupgewrichten
    float eyeY = 1.62f;       // ooghoogte (staand)
    Appearance look;
    bool valid() const { return count > 0; }
};

enum AnimKind { ANIM_IDLE, ANIM_WALK, ANIM_SIT, ANIM_DRIVE, ANIM_WHISTLE, ANIM_CHECK, ANIM_PHONE, ANIM_SIT_PHONE };
struct AnimState {
    AnimKind kind = ANIM_IDLE;
    float phase = 0;     // looppas 0..1
    float speed = 0;     // m/s (lopen)
    float t = 0;         // tijd (s) voor ademhalen en rondkijken
    float headYaw = 0;   // hoofd draaien (rad, + = naar links)
    float headPitch = 0; // + = omlaag kijken
    float press = 0;     // 0..1: rechterarm omhoog naar de stopknop
    float wheel = 0;     // stuur (rijden): -1..1
    int variant = 0;     // kleine verschillen in houding (staan)
};
struct CharPose {
    Mat4 skin[BONE_COUNT];    // vertex-matrices (modelruimte)
    Mat4 global[BONE_COUNT];  // gewrichtsframes (modelruimte), bijv. voor een telefoon in de hand
    float drop = 0;           // hoe ver het bekken zakt (lopen: op en neer)
};

// Iemand om te tekenen (verzameld door bussen, treinen en de speler)
//   look >= 0: willekeurig personage uit de pool; -1 = de speler; -100 - i: conducteur i; -200 - i: machinist i
struct PersonDraw {
    int look = 0;
    Vec3 pos;             // voeten (staan, lopen) of heupgewrichten (zitten)
    float h = 0;          // kijkrichting: +z van het personage wijst naar (sin h, 0, cos h)
    bool sitting = false;
    AnimState anim;
    bool interior = false;
    int prop = -1;        // 0 telefoon, 1 fluitje, 2 scanner (in de rechterhand)
    const struct CharModel* model = nullptr;  // eigen model (andere spelers); anders via look
};
constexpr int LOOK_PLAYER = -1, LOOK_CONDUCTOR = -100, LOOK_DRIVER = -200;

// Opbouwen / opruimen
CharModel buildCharacter(const Appearance& a);
void destroyCharacter(CharModel& m);
void computePose(const CharModel& m, const AnimState& s, CharPose& out);

// Tekenen (eigen shader met skinning; belichting en mist zoals de wereld)
class World;
class CharacterRenderer {
public:
    bool init();
    void begin(const Mat4& viewProj, const World& world);   // na World::beginDraw, gebruikt zon/mist van de wereld
    void beginStudio(const Mat4& viewProj, Vec3 cam);        // los (menu): eigen licht
    // interior: in een voertuig/gebouw (egaal kunstlicht in plaats van zon)
    void draw(const CharModel& m, const Mat4& world, const CharPose& p, bool interior = false);
    void drawProp(int prop, const Mat4& world, const CharPose& p, int bone, bool interior = false);  // 0 telefoon, 1 fluitje, 2 scanner
    void end();

private:
    GLuint prog_ = 0;
    GLint uVP_ = -1, uModel_ = -1, uBones_ = -1, uSun_ = -1, uSunCol_ = -1, uAmb_ = -1, uSky_ = -1, uCam_ = -1, uFogDist_ = -1, uInterior_ = -1;
    CharModel props_[3];
    Vec3 origin_;  // camera-relatief tekenen (zie World::setRelativeView)
};
