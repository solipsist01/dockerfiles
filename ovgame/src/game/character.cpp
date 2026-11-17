#include "character.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include "world.h"

// ============================== keuzes (personage-editor) ==============================
namespace {
const char* const AGES[] = {"Kind", "Tiener", "Volwassene", "Senior"};
const char* const SEXES[] = {"Man / jongen", "Vrouw / meisje"};
const char* const HEIGHTS[] = {"Heel klein", "Klein", "Gemiddeld", "Lang", "Heel lang"};
const char* const BUILDS[] = {"Slank", "Gemiddeld", "Stevig", "Gespierd", "Zwaar"};
const char* const SKINS[] = {"Zeer licht", "Licht", "Lichtgetint", "Getint", "Olijf", "Bruin", "Donkerbruin", "Zeer donker"};
const char* const HAIRS[] = {"Kaal", "Kort", "Opgeschoren", "Middellang", "Lang", "Paardenstaart", "Knot",
                             "Krullen", "Afro", "Bob", "Vlechten", "Kuif", "Hanenkam"};
const char* const HAIRCOLS[] = {"Zwart", "Donkerbruin", "Bruin", "Lichtbruin", "Blond", "Lichtblond", "Rood", "Grijs", "Wit", "Blauw", "Roze"};
const char* const BEARDS[] = {"Geen", "Stoppels", "Snor", "Sik", "Volle baard"};
const char* const GLASSES[] = {"Geen", "Rond", "Rechthoekig", "Dik montuur", "Zonnebril"};
const char* const HATS[] = {"Geen", "Pet", "Muts", "Hoed", "Pet achterstevoren", "Uniformpet"};
const char* const TOPS[] = {"T-shirt", "Polo", "Overhemd", "Trui", "Hoodie", "Jas", "Colbert", "Jurk", "NS-uniform"};
const char* const BOTTOMS[] = {"Lange broek", "Spijkerbroek", "Korte broek", "Rok", "Legging"};
const char* const SHOES[] = {"Sneakers", "Nette schoenen", "Laarzen", "Sandalen"};
const char* const BAGS[] = {"Geen", "Rugzak", "Schoudertas", "Handtas"};
const char* const COLORS[] = {"Zwart", "Donkergrijs", "Grijs", "Wit", "Marineblauw", "Blauw", "Lichtblauw", "Rood", "Bordeaux",
                              "Groen", "Olijfgroen", "Geel", "Oranje", "Roze", "Paars", "Bruin", "Beige", "Denim"};
template <size_t N> constexpr int cnt(const char* const (&)[N]) { return (int)N; }

const Vec3 SKIN_C[] = {{0.96f, 0.82f, 0.74f}, {0.92f, 0.74f, 0.63f}, {0.86f, 0.66f, 0.52f}, {0.78f, 0.57f, 0.42f},
                       {0.70f, 0.52f, 0.36f}, {0.55f, 0.37f, 0.25f}, {0.40f, 0.26f, 0.17f}, {0.27f, 0.17f, 0.11f}};
const Vec3 HAIR_C[] = {{0.05f, 0.045f, 0.04f}, {0.16f, 0.10f, 0.06f}, {0.30f, 0.19f, 0.10f}, {0.48f, 0.33f, 0.19f}, {0.80f, 0.64f, 0.38f},
                       {0.92f, 0.82f, 0.58f}, {0.62f, 0.25f, 0.10f}, {0.55f, 0.55f, 0.55f}, {0.88f, 0.88f, 0.86f}, {0.10f, 0.30f, 0.80f},
                       {0.90f, 0.40f, 0.62f}};
const Vec3 CLOTH_C[] = {{0.06f, 0.06f, 0.07f}, {0.20f, 0.21f, 0.23f}, {0.45f, 0.46f, 0.48f}, {0.90f, 0.90f, 0.88f}, {0.08f, 0.11f, 0.25f},
                        {0.12f, 0.28f, 0.66f}, {0.50f, 0.68f, 0.88f}, {0.72f, 0.10f, 0.10f}, {0.40f, 0.07f, 0.12f}, {0.13f, 0.45f, 0.20f},
                        {0.32f, 0.36f, 0.18f}, {0.95f, 0.78f, 0.12f}, {0.92f, 0.45f, 0.10f}, {0.92f, 0.55f, 0.70f}, {0.38f, 0.18f, 0.55f},
                        {0.38f, 0.24f, 0.13f}, {0.80f, 0.72f, 0.56f}, {0.22f, 0.32f, 0.50f}};
}  // namespace

const LookField LOOK_FIELDS[] = {
    {"Leeftijd", &Appearance::age, AGES, cnt(AGES), 0},
    {"Geslacht", &Appearance::sex, SEXES, cnt(SEXES), 0},
    {"Lengte", &Appearance::height, HEIGHTS, cnt(HEIGHTS), 0},
    {"Postuur", &Appearance::build, BUILDS, cnt(BUILDS), 0},
    {"Huidskleur", &Appearance::skin, SKINS, cnt(SKINS), 1},
    {"Kapsel", &Appearance::hair, HAIRS, cnt(HAIRS), 0},
    {"Haarkleur", &Appearance::hairColor, HAIRCOLS, cnt(HAIRCOLS), 2},
    {"Baard", &Appearance::beard, BEARDS, cnt(BEARDS), 0},
    {"Bril", &Appearance::glasses, GLASSES, cnt(GLASSES), 0},
    {"Hoofddeksel", &Appearance::hat, HATS, cnt(HATS), 0},
    {"Kleur hoofddeksel", &Appearance::hatColor, COLORS, cnt(COLORS), 3},
    {"Bovenkleding", &Appearance::top, TOPS, cnt(TOPS), 0},
    {"Kleur bovenkleding", &Appearance::topColor, COLORS, cnt(COLORS), 3},
    {"Onderkleding", &Appearance::bottom, BOTTOMS, cnt(BOTTOMS), 0},
    {"Kleur onderkleding", &Appearance::bottomColor, COLORS, cnt(COLORS), 3},
    {"Schoenen", &Appearance::shoes, SHOES, cnt(SHOES), 0},
    {"Kleur schoenen", &Appearance::shoeColor, COLORS, cnt(COLORS), 3},
    {"Tas", &Appearance::bag, BAGS, cnt(BAGS), 0},
    {"Kleur tas", &Appearance::bagColor, COLORS, cnt(COLORS), 3},
};
const int LOOK_FIELD_COUNT = sizeof(LOOK_FIELDS) / sizeof(LOOK_FIELDS[0]);

Vec3 lookSkinColor(int i) { return SKIN_C[std::max(0, std::min(i, cnt(SKINS) - 1))]; }
Vec3 lookHairColor(int i) { return HAIR_C[std::max(0, std::min(i, cnt(HAIRCOLS) - 1))]; }
Vec3 lookClothColor(int i) { return CLOTH_C[std::max(0, std::min(i, cnt(COLORS) - 1))]; }

// ============================== opslaan / inlezen ==============================
namespace {
const char* KEYS[] = {"leeftijd", "geslacht", "lengte", "postuur", "huid", "kapsel", "haarkleur", "baard", "bril", "hoed",
                      "hoedkleur", "boven", "bovenkleur", "onder", "onderkleur", "schoenen", "schoenkleur", "tas", "taskleur"};
}

std::string Appearance::serialize() const {
    std::string n;
    for (char c : name) if (c != ';' && c != '=' && c != '\n' && c != '\r') n += c;
    std::string s = "naam=" + n;
    for (int k = 0; k < LOOK_FIELD_COUNT; k++) s += std::string(";") + KEYS[k] + "=" + std::to_string(this->*LOOK_FIELDS[k].member);
    return s;
}

bool Appearance::parse(const std::string& line) {
    size_t p = 0;
    bool any = false;
    while (p < line.size()) {
        size_t e = line.find(';', p);
        if (e == std::string::npos) e = line.size();
        std::string kv = line.substr(p, e - p);
        size_t eq = kv.find('=');
        if (eq != std::string::npos) {
            std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
            if (k == "naam") { name = v; any = true; }
            for (int f = 0; f < LOOK_FIELD_COUNT; f++)
                if (k == KEYS[f]) {
                    int x = atoi(v.c_str());
                    this->*LOOK_FIELDS[f].member = std::max(0, std::min(x, LOOK_FIELDS[f].count - 1));
                    any = true;
                }
        }
        p = e + 1;
    }
    return any;
}

float Appearance::stature() const {
    static const float base[4][2] = {{1.30f, 1.28f}, {1.66f, 1.61f}, {1.81f, 1.68f}, {1.75f, 1.62f}};
    static const float hf[5] = {0.91f, 0.955f, 1.0f, 1.045f, 1.09f};
    return base[std::max(0, std::min(age, 3))][sex ? 1 : 0] * hf[std::max(0, std::min(height, 4))];
}

// ---------- willekeurig ----------
namespace {
struct Rng {
    uint32_t s;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (next() & 0xFFFFFF) / 16777216.0f; }
    int pick(std::initializer_list<int> w) {
        int tot = 0;
        for (int x : w) tot += x;
        int r = (int)(f() * tot), i = 0;
        for (int x : w) { if (r < x) return i; r -= x; i++; }
        return 0;
    }
};
const char* const NAMES_M[] = {"Daan", "Sem", "Lucas", "Milan", "Levi", "Noah", "Jan", "Pieter", "Kees", "Bram", "Thijs", "Ruben", "Mohamed",
                               "Youssef", "Mark", "Hans", "Wim", "Joost", "Tim", "Stijn", "Jesse", "Gerrit", "Ahmed", "Sven"};
const char* const NAMES_F[] = {"Emma", "Julia", "Mila", "Sophie", "Tess", "Anna", "Fleur", "Sara", "Lotte", "Eva", "Noor", "Femke",
                               "Ingrid", "Marieke", "Annemiek", "Fatima", "Yasmina", "Lisa", "Sanne", "Joke", "Ria", "Iris", "Zoë", "Lieke"};
}  // namespace

Appearance Appearance::random(uint32_t seed) {
    Rng r{seed * 2654435761u + 0x9E3779B9u};
    for (int k = 0; k < 4; k++) r.next();
    Appearance a;
    a.age = r.pick({12, 14, 56, 18});
    a.sex = r.next() & 1;
    a.name = a.sex ? NAMES_F[r.next() % cnt(NAMES_F)] : NAMES_M[r.next() % cnt(NAMES_M)];
    a.height = r.pick({8, 22, 40, 22, 8});
    a.build = a.age == 0 ? r.pick({45, 45, 10, 0, 0}) : r.pick({22, 42, 18, a.sex ? 3 : 9, 10});
    a.skin = r.pick({16, 26, 22, 8, 7, 9, 7, 5});
    bool darkSkin = a.skin >= 5;
    if (a.sex == 0)
        a.hair = r.pick({a.age >= 2 ? 10 : 0, 35, 18, 10, 3, 1, 2, 6, darkSkin ? 14 : 1, 0, 1, 8, 1});
    else
        a.hair = r.pick({0, 4, 1, 18, 28, 18, 10, 7, darkSkin ? 12 : 1, 12, 3, 0, 0});
    if (a.age == 3) a.hairColor = r.pick({3, 3, 4, 2, 2, 1, 0, 45, 40, 0, 0});
    else if (darkSkin || a.skin == 4) a.hairColor = r.pick({70, 25, 5, 0, 0, 0, 0, 0, 0, 0, 0});
    else a.hairColor = r.pick({8, 24, 22, 16, 14, 8, 5, a.age == 2 ? 2 : 0, 0, 1, 1});
    if (a.sex == 0 && a.age >= 2) a.beard = r.pick({50, 18, 7, 8, a.age == 3 ? 20 : 14});
    a.glasses = r.pick({a.age == 3 ? 40 : a.age == 0 ? 92 : 70, 6, 10, 6, 4});
    a.hat = r.pick({86, a.age <= 2 ? 6 : 1, 4, a.age == 3 ? 6 : 1, a.age == 1 ? 3 : 0, 0});
    a.hatColor = r.pick({20, 14, 12, 4, 16, 6, 3, 4, 3, 4, 4, 2, 2, 2, 1, 6, 6, 3});
    a.top = a.sex ? r.pick({20, 6, 8, 16, 10, 22, 4, a.age >= 1 ? 12 : 4, 0}) : r.pick({22, 10, 14, 16, 12, 22, 4, 0, 0});
    a.topColor = r.pick({18, 12, 10, 12, 12, 8, 6, 5, 4, 4, 4, 2, 2, 3, 2, 4, 5, 3});
    if (a.top == 7) a.bottom = 0;
    else a.bottom = a.sex ? r.pick({20, 35, 8, 12, 15}) : r.pick({40, 45, 15, 0, 0});
    a.bottomColor = a.bottom == 1 ? r.pick({8, 0, 2, 0, 3, 4, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 40})
                                  : r.pick({26, 16, 10, 2, 16, 3, 1, 1, 1, 1, 4, 0, 0, 1, 1, 6, 10, 2});
    a.shoes = r.pick({60, 20, a.sex ? 14 : 8, 6});
    a.shoeColor = r.pick({34, 10, 8, 26, 4, 3, 1, 3, 1, 1, 0, 0, 0, 1, 0, 10, 4, 0});
    a.bag = r.pick({52, a.age == 1 ? 40 : 18, 12, a.sex && a.age >= 2 ? 16 : 0});
    a.bagColor = r.pick({30, 14, 10, 2, 12, 6, 2, 6, 4, 4, 4, 1, 1, 2, 1, 10, 6, 2});
    return a;
}

Appearance Appearance::conductor(uint32_t seed) {
    Appearance a = random(seed);
    a.age = 2;
    if (a.build == 4) a.build = 2;
    a.top = 8; a.topColor = 4;
    a.bottom = a.sex && (seed & 4) ? 3 : 0; a.bottomColor = 4;
    a.shoes = 1; a.shoeColor = 0;
    a.hat = (seed >> 3) & 1 ? 5 : 0; a.hatColor = 4;
    a.bag = 2; a.bagColor = 0;
    a.glasses = a.glasses == 4 ? 0 : a.glasses;
    if (a.hair == 12 || a.hairColor >= 9) { a.hair = 1; a.hairColor = 1; }
    return a;
}

Appearance Appearance::driver(uint32_t seed) {
    Appearance a = conductor(seed ^ 0x5bd1e995u);
    a.hat = 0;
    a.bag = 0;
    return a;
}

// ============================== lichaamsmaten ==============================
namespace {
struct Dims {
    float S, headH, hw, hh, hd;
    Vec3 headC;
    float neckBase, neckR, shoulderY, shoulderHalf, shoulderX;
    float chestY, waistY, hipY, crotchY, kneeY, ankleY, hipX;
    float Whip, Wwaist, Wchest, Dhip, Dwaist, Dchest, belly, bust;
    float rUA, rFA, rWrist, rThigh, rKnee, rCalf, rAnkle;
    float lenUA, lenFA, lenHand, footLen;
};

Dims dimsOf(const Appearance& a) {
    Dims d;
    int age = std::max(0, std::min(a.age, 3)), b = std::max(0, std::min(a.build, 4));
    bool f = a.sex == 1;
    float S = a.stature();
    d.S = S;
    static const float headR[4] = {0.165f, 0.138f, 0.128f, 0.128f};
    static const float legR[4] = {0.455f, 0.49f, 0.50f, 0.49f};
    d.headH = headR[age] * S;
    d.hh = d.headH * 0.5f;
    d.hw = d.headH * (age == 0 ? 0.39f : 0.36f);
    d.hd = d.headH * (age == 0 ? 0.45f : 0.43f);
    d.headC = Vec3(0, S - d.hh, 0.004f * S);
    d.neckBase = S - d.headH - (age == 0 ? 0.030f : 0.046f) * S;
    d.neckR = (age == 0 ? 0.034f : 0.030f) * S * (b == 3 ? 1.15f : b == 4 ? 1.12f : 1.0f) * (f ? 0.9f : 1.0f);
    d.shoulderY = d.neckBase - 0.02f * S;
    d.chestY = d.shoulderY - 0.09f * S;
    d.hipY = legR[age] * S;
    d.waistY = d.hipY + (age == 0 ? 0.10f : 0.11f) * S;
    d.crotchY = d.hipY - 0.045f * S;
    d.kneeY = d.hipY * 0.52f;
    d.ankleY = 0.043f * S;
    static const float bShoulder[5] = {0.93f, 1.0f, 1.06f, 1.14f, 1.10f};
    static const float bWaist[5] = {0.88f, 1.0f, 1.15f, 1.02f, 1.42f};
    static const float bHip[5] = {0.92f, 1.0f, 1.08f, 1.0f, 1.28f};
    static const float bDepth[5] = {0.9f, 1.0f, 1.10f, 1.12f, 1.28f};
    static const float bLimb[5] = {0.86f, 1.0f, 1.10f, 1.24f, 1.30f};
    float child = age == 0 ? 1.0f : 0.0f;
    d.shoulderHalf = (age == 0 ? 0.105f : f ? 0.100f : 0.114f) * S * bShoulder[b];
    d.Wchest = d.shoulderHalf * 0.86f;
    d.Wwaist = (f ? 0.064f : 0.073f) * S * bWaist[b] * (age == 3 ? 1.06f : 1.0f) * (1 + child * 0.12f);
    d.Whip = (f && age >= 1 ? 0.096f : 0.083f) * S * bHip[b];
    d.Dchest = (f ? 0.056f : 0.060f) * S * bDepth[b];
    d.Dwaist = 0.050f * S * (b == 4 ? 1.45f : bDepth[b]) * (1 + child * 0.1f);
    d.Dhip = 0.058f * S * bHip[b];
    d.belly = (b == 4 ? 0.034f : b == 2 ? 0.012f : 0.0f) * S + (age == 3 && !f ? 0.010f * S : 0.0f);
    d.bust = f ? (age == 1 ? 0.014f : age >= 2 ? 0.022f : 0.0f) * S * (b == 4 ? 1.3f : 1.0f) : 0.0f;
    float lm = bLimb[b];
    d.rUA = 0.021f * S * lm * (f ? 0.92f : 1.0f);
    d.rFA = 0.017f * S * lm * (f ? 0.92f : 1.0f);
    d.rWrist = 0.0115f * S * std::sqrt(lm);
    d.rThigh = (f ? 0.046f : 0.043f) * S * lm;
    d.rKnee = 0.027f * S * std::sqrt(lm);
    d.rCalf = 0.030f * S * lm;
    d.rAnkle = 0.0155f * S;
    d.lenUA = 0.172f * S;
    d.lenFA = 0.150f * S;
    d.lenHand = 0.10f * S;
    d.footLen = 0.152f * S;
    d.shoulderX = d.shoulderHalf - d.rUA * 0.85f;
    d.hipX = d.Whip * 0.50f;
    return d;
}
const Vec3 ARM_L = normalize(Vec3(0.16f, -1.0f, 0.0f));
const Vec3 ARM_R = normalize(Vec3(-0.16f, -1.0f, 0.0f));

// ============================== meshbouw ==============================
struct CV { float p[3], n[3], c[3], s[3]; };
struct CB {
    std::vector<CV> v;
    std::vector<uint32_t> i;
    uint32_t add(Vec3 p, Vec3 c, int b0, int b1, float w) {
        CV x;
        x.p[0] = p.x; x.p[1] = p.y; x.p[2] = p.z;
        x.n[0] = x.n[1] = x.n[2] = 0;
        x.c[0] = c.x; x.c[1] = c.y; x.c[2] = c.z;
        x.s[0] = (float)b0; x.s[1] = (float)b1; x.s[2] = w;
        v.push_back(x);
        return (uint32_t)v.size() - 1;
    }
    void tri(uint32_t a, uint32_t b, uint32_t c) { i.insert(i.end(), {a, b, c}); }
};

struct RingDef {
    Vec3 c, u, w;         // midden en assen van de doorsnede
    float ru, rw, pw = 2; // stralen en superellips-exponent (2 = ellips)
    float front = 0;      // extra naar +w (buik/borst, alleen voorkant)
    Vec3 col;
    int b0 = 0, b1 = 0;
    float wt = 0;
};
float spow(float x, float e) { return x < 0 ? -std::pow(-x, e) : std::pow(x, e); }
Vec3 ringPoint(const RingDef& r, float a) {
    float ca = std::cos(a), sa = std::sin(a);
    float x = spow(ca, 2.0f / r.pw) * r.ru, y = spow(sa, 2.0f / r.pw) * r.rw;
    if (sa > 0 && r.front > 0) y += r.front * std::pow(sa, 2.5f) * (1.0f - 0.3f * std::fabs(ca));
    return r.c + r.u * x + r.w * y;
}
void frameFor(Vec3 d, Vec3& u, Vec3& w) {
    Vec3 ref = std::fabs(d.z) > 0.9f ? Vec3(0, 1, 0) : Vec3(0, 0, 1);
    u = normalize(cross(ref, d));
    if (std::fabs(d.z) > 0.9f) u = normalize(cross(Vec3(0, 1, 0), d));
    w = normalize(cross(d, u));
}

using VColFn = std::function<Vec3(const RingDef&, Vec3 p, float a)>;
using VSkinFn = std::function<void(const RingDef&, Vec3 p, int& b0, int& b1, float& w)>;

// Buis door ringen (gesloten rond), optioneel afgeronde kappen aan begin en eind
void loft(CB& cb, const std::vector<RingDef>& rings, int seg, bool capA, bool capB, VColFn vcol = nullptr, VSkinFn vskin = nullptr) {
    if (rings.size() < 2) return;
    std::vector<std::vector<uint32_t>> idx;
    auto emitRing = [&](const RingDef& r) {
        std::vector<uint32_t> row;
        for (int k = 0; k < seg; k++) {
            float a = 2 * PI * k / seg;
            Vec3 p = ringPoint(r, a);
            Vec3 c = vcol ? vcol(r, p, a) : r.col;
            int b0 = r.b0, b1 = r.b1;
            float w = r.wt;
            if (vskin) vskin(r, p, b0, b1, w);
            row.push_back(cb.add(p, c, b0, b1, w));
        }
        idx.push_back(row);
    };
    auto cap = [&](const RingDef& r0, Vec3 dir, bool atStart) {
        float rad = (r0.ru + r0.rw) * 0.5f * 0.85f;
        std::vector<std::vector<uint32_t>> rows;
        (void)atStart;
        for (int q = 0; q <= 3; q++) {  // q = 0: eigen kopie van de randring (eigen normalen)
            float ph = (PI / 2) * q / 4.0f;
            RingDef r = r0;
            r.c = r0.c + dir * (rad * std::sin(ph));
            r.ru = r0.ru * std::cos(ph);
            r.rw = r0.rw * std::cos(ph);
            r.front *= std::cos(ph);
            std::vector<uint32_t> row;
            for (int k = 0; k < seg; k++) {
                float a = 2 * PI * k / seg;
                Vec3 p = ringPoint(r, a);
                int b0 = r.b0, b1 = r.b1;
                float w = r.wt;
                if (vskin) vskin(r, p, b0, b1, w);
                row.push_back(cb.add(p, vcol ? vcol(r, p, a) : r.col, b0, b1, w));
            }
            rows.push_back(row);
        }
        int b0 = r0.b0, b1 = r0.b1;
        float w = r0.wt;
        Vec3 apex = r0.c + dir * rad;
        if (vskin) vskin(r0, apex, b0, b1, w);
        uint32_t ap = cb.add(apex, vcol ? vcol(r0, apex, 0) : r0.col, b0, b1, w);
        for (size_t q = 0; q + 1 < rows.size(); q++)
            for (int k = 0; k < seg; k++) {
                int k2 = (k + 1) % seg;
                cb.tri(rows[q][k], rows[q][k2], rows[q + 1][k2]);
                cb.tri(rows[q][k], rows[q + 1][k2], rows[q + 1][k]);
            }
        for (int k = 0; k < seg; k++) cb.tri(rows.back()[k], rows.back()[(k + 1) % seg], ap);
    };
    for (const RingDef& r : rings) emitRing(r);
    for (size_t q = 0; q + 1 < idx.size(); q++)
        for (int k = 0; k < seg; k++) {
            int k2 = (k + 1) % seg;
            cb.tri(idx[q][k], idx[q][k2], idx[q + 1][k2]);
            cb.tri(idx[q][k], idx[q + 1][k2], idx[q + 1][k]);
        }
    if (capA) cap(rings.front(), normalize(rings.front().c - rings[1].c), true);
    if (capB) cap(rings.back(), normalize(rings.back().c - rings[rings.size() - 2].c), false);
}

// Ellipsoide (UV-bol) met optionele vervorming en selectie (alleen driehoeken waarvan alle hoeken binnen vallen)
using DeformFn = std::function<Vec3(Vec3 unit)>;          // eenheidsrichting -> geschaalde vorm (in eenheden van de stralen)
using IncludeFn = std::function<bool(float theta, float phi)>;  // theta vanaf boven, phi 0 = voor (+z)
void ellipsoid(CB& cb, Vec3 c, Vec3 r, Vec3 col, int bone, int segU, int segV, DeformFn def = nullptr, IncludeFn inc = nullptr,
               std::function<Vec3(Vec3 p, float theta, float phi)> vcol = nullptr) {
    std::vector<std::vector<int>> id(segV + 1, std::vector<int>(segU, -1));
    for (int j = 0; j <= segV; j++) {
        float th = PI * j / segV;
        for (int k = 0; k < segU; k++) {
            float ph = 2 * PI * k / segU - PI;
            if (inc && !inc(th, ph)) continue;
            Vec3 un(std::sin(th) * std::sin(ph), std::cos(th), std::sin(th) * std::cos(ph));
            Vec3 s = def ? def(un) : un;
            Vec3 p = c + Vec3(s.x * r.x, s.y * r.y, s.z * r.z);
            id[j][k] = (int)cb.add(p, vcol ? vcol(p, th, ph) : col, bone, bone, 0);
        }
    }
    for (int j = 0; j < segV; j++)
        for (int k = 0; k < segU; k++) {
            int k2 = (k + 1) % segU;
            int a = id[j][k], b = id[j][k2], e = id[j + 1][k2], d = id[j + 1][k];
            if (a >= 0 && e >= 0 && d >= 0 && j + 1 <= segV) cb.tri(a, e, d);
            if (a >= 0 && b >= 0 && e >= 0 && j > 0) cb.tri(a, b, e);
        }
}

// Dunne buis langs een reeks punten (bril, bandjes, vlechten)
void wire(CB& cb, const std::vector<Vec3>& pts, float r, Vec3 col, int bone, int seg = 6, int bone2 = -1) {
    for (size_t k = 0; k + 1 < pts.size(); k++) {
        Vec3 d = normalize(pts[k + 1] - pts[k]);
        Vec3 u, w;
        frameFor(d, u, w);
        RingDef a{pts[k], u, w, r, r, 2, 0, col, bone, bone2 >= 0 ? bone2 : bone, 0};
        RingDef b = a;
        b.c = pts[k + 1];
        if (bone2 >= 0) { a.wt = (float)k / (pts.size() - 1); b.wt = (float)(k + 1) / (pts.size() - 1); }
        loft(cb, {a, b}, seg, false, false);
    }
}

// Platte schijf / waaier (rand van een pet, hoed)
void disc(CB& cb, Vec3 c, const std::vector<Vec3>& rim, Vec3 col, int bone) {
    uint32_t ci = cb.add(c, col, bone, bone, 0);
    std::vector<uint32_t> ids;
    for (const Vec3& p : rim) ids.push_back(cb.add(p, col, bone, bone, 0));
    for (size_t k = 0; k + 1 < ids.size(); k++) cb.tri(ci, ids[k], ids[k + 1]);
}

// Hoekige doos (tas)
void boxPart(CB& cb, Vec3 c, Vec3 h, Vec3 col, int bone, float round = 4.0f) {
    ellipsoid(cb, c, h, col, bone, 12, 8, [&](Vec3 u) {
        float l = std::pow(std::pow(std::fabs(u.x), round) + std::pow(std::fabs(u.y), round) + std::pow(std::fabs(u.z), round), 1.0f / round);
        return u * (1.0f / std::max(l, 1e-4f));
    });
}

void computeNormals(CB& cb) {
    for (CV& x : cb.v) x.n[0] = x.n[1] = x.n[2] = 0;
    for (size_t k = 0; k + 2 < cb.i.size(); k += 3) {
        CV &a = cb.v[cb.i[k]], &b = cb.v[cb.i[k + 1]], &c = cb.v[cb.i[k + 2]];
        Vec3 pa(a.p[0], a.p[1], a.p[2]), pb(b.p[0], b.p[1], b.p[2]), pc(c.p[0], c.p[1], c.p[2]);
        Vec3 n = cross(pb - pa, pc - pa);
        for (CV* x : {&a, &b, &c}) { x->n[0] += n.x; x->n[1] += n.y; x->n[2] += n.z; }
    }
    for (CV& x : cb.v) {
        Vec3 n = normalize(Vec3(x.n[0], x.n[1], x.n[2]));
        x.n[0] = n.x; x.n[1] = n.y; x.n[2] = n.z;
    }
}

void uploadCB(CharModel& m, CB& cb) {
    computeNormals(cb);
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, cb.v.size() * sizeof(CV), cb.v.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &m.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, cb.i.size() * sizeof(uint32_t), cb.i.data(), GL_STATIC_DRAW);
    for (int k = 0; k < 4; k++) {
        glEnableVertexAttribArray(k);
        glVertexAttribPointer(k, 3, GL_FLOAT, GL_FALSE, sizeof(CV), (void*)(k * 3 * sizeof(float)));
    }
    glBindVertexArray(0);
    m.count = (GLsizei)cb.i.size();
}

float lerpf(float a, float b, float t) { return a + (b - a) * t; }
float smooth01(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

// Haarkap over het hoofd: tot waar (theta, als fractie van pi) aan de voorkant, zijkant en achterkant
IncludeFn hairLimit(float front, float side, float back) {
    return [=](float th, float ph) {
        float c = std::cos(ph);
        float lim = c >= 0 ? side + (front - side) * c : side + (back - side) * (-c);
        return th <= lim * PI + 1e-4f;
    };
}
DeformFn hairScale(float scale, float bump = 0, float frontTop = 0) {
    return [=](Vec3 u) {
        float th = std::acos(clampf(u.y, -1, 1)), ph = std::atan2(u.x, u.z);
        float s = scale * (1 + bump * std::sin(9 * th) * std::sin(9 * ph));
        if (frontTop > 0 && u.z > 0 && u.y > 0.2f) s *= 1 + frontTop * u.z * (u.y - 0.2f);
        return u * s;
    };
}
}  // namespace

// ============================== personage opbouwen ==============================
CharModel buildCharacter(const Appearance& a) {
    Dims d = dimsOf(a);
    const float S = d.S;
    CharModel m;
    m.look = a;
    m.stature = S;
    m.hipY = d.hipY;
    m.eyeY = d.headC.y + 0.08f * d.hh;
    const Vec3 skin = lookSkinColor(a.skin), hairC = lookHairColor(a.hairColor);
    const Vec3 topC = lookClothColor(a.topColor), botC = lookClothColor(a.bottomColor), shoeC = lookClothColor(a.shoeColor);
    const Vec3 bagC = lookClothColor(a.bagColor), hatC = lookClothColor(a.hatColor);
    const Vec3 shirtC = a.top == 8 ? Vec3(0.62f, 0.76f, 0.92f) : Vec3(0.93f, 0.93f, 0.91f);
    const bool longSleeve = a.top == 2 || a.top == 3 || a.top == 4 || a.top == 5 || a.top == 6 || a.top == 8;
    const bool coat = a.top == 5 || a.top == 6 || a.top == 8;
    const bool dress = a.top == 7;
    const bool skirt = a.bottom == 3 || dress;
    const bool longLegs = !skirt && (a.bottom == 0 || a.bottom == 1 || a.bottom == 4);
    const float inflTop = coat ? (a.top == 5 ? 0.007f : 0.005f) * S : (a.top == 3 || a.top == 4) ? 0.003f * S : 0.0015f * S;
    const float inflLeg = a.bottom == 4 ? 0.001f * S : longLegs ? 0.004f * S : 0.0f;
    CB cb;

    // ---- gewrichten ----
    m.joint[B_PELVIS] = Vec3(0, d.hipY + 0.02f * S, 0);
    m.joint[B_SPINE] = Vec3(0, d.waistY, 0);
    m.joint[B_CHEST] = Vec3(0, d.chestY, 0);
    m.joint[B_NECK] = Vec3(0, d.neckBase, -0.004f * S);
    m.joint[B_HEAD] = Vec3(0, d.headC.y - d.hh * 0.62f, -0.008f * S);
    for (int s = -1; s <= 1; s += 2) {
        Vec3 dir = s > 0 ? ARM_L : ARM_R;
        int ua = s > 0 ? B_LUARM : B_RUARM;
        m.joint[ua] = Vec3(s * d.shoulderX, d.shoulderY - 0.012f * S, -0.004f * S);
        m.joint[ua + 1] = m.joint[ua] + dir * d.lenUA;
        m.joint[ua + 2] = m.joint[ua + 1] + dir * d.lenFA;
        int th = s > 0 ? B_LTHIGH : B_RTHIGH;
        m.joint[th] = Vec3(s * d.hipX, d.hipY, 0);
        m.joint[th + 1] = Vec3(s * d.hipX * 0.92f, d.kneeY, 0.006f * S);
        m.joint[th + 2] = Vec3(s * d.hipX * 0.88f, d.ankleY, -0.012f * S);
    }

    // ---- romp ----
    {
        struct Lv { float y, W, D, front; };
        float bl = d.belly, bu = d.bust;
        std::vector<Lv> lv = {
            {d.crotchY - 0.004f * S, d.Whip * 0.55f, d.Dhip * 0.72f, 0},
            {d.crotchY + 0.02f * S, d.Whip * 0.86f, d.Dhip * 0.92f, 0},
            {d.hipY, d.Whip, d.Dhip, 0},
            {d.hipY + 0.04f * S, lerpf(d.Whip, d.Wwaist, 0.45f), lerpf(d.Dhip, d.Dwaist, 0.5f), bl * 0.5f},
            {d.waistY, d.Wwaist, d.Dwaist, bl},
            {(d.waistY + d.chestY) / 2, lerpf(d.Wwaist, d.Wchest, 0.5f), lerpf(d.Dwaist, d.Dchest, 0.5f), bl * 0.55f + bu * 0.2f},
            {d.chestY, d.Wchest, d.Dchest, bu + bl * 0.15f},
            {d.chestY + 0.04f * S, d.Wchest * 1.03f, d.Dchest * 0.96f, bu * 0.5f},
            {d.shoulderY - 0.014f * S, d.shoulderHalf * 0.97f, d.Dchest * 0.78f, 0},
            {d.shoulderY + 0.004f * S, d.shoulderHalf * 0.78f, d.Dchest * 0.58f, 0},
            {d.neckBase - 0.004f * S, d.neckR * 1.5f, d.neckR * 1.35f, 0},
            {d.neckBase + 0.012f * S, d.neckR * 1.06f, d.neckR * 1.0f, 0},
        };
        float tY = dress || coat ? -1.0f : d.hipY + 0.035f * S;  // grens boven/onderkleding
        auto skinOf = [&](float y, RingDef& r) {
            if (y <= d.hipY) { r.b0 = r.b1 = B_PELVIS; r.wt = 0; }
            else if (y <= d.waistY) { r.b0 = B_PELVIS; r.b1 = B_SPINE; r.wt = smooth01((y - d.hipY) / (d.waistY - d.hipY)); }
            else if (y <= d.chestY) { r.b0 = B_SPINE; r.b1 = B_CHEST; r.wt = smooth01((y - d.waistY) / (d.chestY - d.waistY)); }
            else if (y <= d.neckBase) { r.b0 = r.b1 = B_CHEST; r.wt = 0; }
            else { r.b0 = B_CHEST; r.b1 = B_NECK; r.wt = 0.5f; }
        };
        auto ringAt = [&](float y, bool lowerCol) {
            size_t k = 0;
            while (k + 2 < lv.size() && lv[k + 1].y < y) k++;
            float t = clampf((y - lv[k].y) / std::max(1e-5f, lv[k + 1].y - lv[k].y), 0, 1);
            RingDef r;
            r.c = Vec3(0, y, 0);
            r.u = Vec3(1, 0, 0);
            r.w = Vec3(0, 0, 1);
            float infl = y >= d.hipY - 0.01f * S || coat ? inflTop : 0.0f;
            if (y < tY) infl = longLegs || a.bottom == 2 ? 0.003f * S : 0.0f;
            r.ru = lerpf(lv[k].W, lv[k + 1].W, t) + infl;
            r.rw = lerpf(lv[k].D, lv[k + 1].D, t) + infl;
            r.front = lerpf(lv[k].front, lv[k + 1].front, t);
            r.pw = 2.3f;
            r.col = lowerCol ? (a.bottom == 3 ? topC : botC) : topC;
            if (y > d.neckBase - 0.006f * S) r.col = skin;
            skinOf(y, r);
            return r;
        };
        std::vector<RingDef> rings;
        for (size_t k = 0; k + 1 < lv.size(); k++) {
            for (int q = 0; q < 2; q++) {
                float y = lerpf(lv[k].y, lv[k + 1].y, q * 0.5f);
                rings.push_back(ringAt(y, y < tY));
            }
            if (tY > lv[k].y && tY <= lv[k + 1].y) {
                rings.push_back(ringAt(tY, true));
                rings.push_back(ringAt(tY + 0.0005f, false));
            }
        }
        rings.push_back(ringAt(lv.back().y, false));
        std::stable_sort(rings.begin(), rings.end(), [](const RingDef& p, const RingDef& q) { return p.c.y < q.c.y; });
        float vY = d.chestY - 0.035f * S;
        VColFn vcol = [&](const RingDef& r, Vec3 p, float ang) {
            Vec3 c = r.col;
            (void)ang;
            if (p.y > d.neckBase - 0.006f * S) return skin;
            if (p.z > 0 && p.y > vY && p.y < d.neckBase) {
                float hwV = (p.y - vY) * 0.55f;
                if ((a.top == 6 || a.top == 8) && std::fabs(p.x) < hwV) {
                    c = shirtC;
                    if (a.sex == 0 && std::fabs(p.x) < 0.009f * S && p.y < d.neckBase - 0.01f * S) c = a.top == 8 ? Vec3(0.05f, 0.08f, 0.22f) : Vec3(0.35f, 0.06f, 0.08f);  // stropdas
                }
                if ((a.top == 0 || a.top == 7) && std::fabs(p.x) < 0.035f * S && p.y > d.shoulderY - 0.01f * S) c = skin;  // ronde hals
                if (a.top == 1 && std::fabs(p.x) < 0.006f * S && p.y > vY + 0.03f * S) c = topC * 0.75f;                  // knoopsluiting polo
            }
            if (a.top == 4 && p.z > 0 && std::fabs(p.x) < 0.065f * S && p.y > d.hipY + 0.02f * S && p.y < d.waistY + 0.03f * S) c = topC * 0.82f;  // buidelzak
            return c;
        };
        loft(cb, rings, 24, true, false, vcol);
    }
    // ---- jaspanden / rok / jurk ----
    auto sideSkin = [&](float yTop, float yBot) {
        return [=](const RingDef& r, Vec3 p, int& b0, int& b1, float& w) {
            (void)r;
            float t = clampf((yTop - p.y) / std::max(0.01f, yTop - yBot), 0, 1);
            b0 = B_PELVIS;
            b1 = p.x >= 0 ? B_LTHIGH : B_RTHIGH;
            w = std::min(0.75f, t * 0.8f) * clampf(std::fabs(p.x) / (d.hipX * 1.2f), 0.2f, 1.0f);
        };
    };
    if (coat) {
        float yTop = d.hipY + 0.02f * S, yBot = a.top == 5 ? d.crotchY - 0.13f * S : d.crotchY - 0.035f * S;
        std::vector<RingDef> rr;
        for (int q = 0; q <= 4; q++) {
            float t = q / 4.0f, y = lerpf(yTop, yBot, t);
            RingDef r;
            r.c = Vec3(0, y, 0);
            r.u = Vec3(1, 0, 0);
            r.w = Vec3(0, 0, 1);
            r.ru = (d.Whip + 0.010f * S) * (1 + 0.06f * t);
            r.rw = (d.Dhip + 0.010f * S) * (1 + 0.10f * t) + d.belly * 0.3f * (1 - t);
            r.pw = 2.3f;
            r.col = topC;
            rr.push_back(r);
        }
        loft(cb, rr, 24, false, false, nullptr, sideSkin(yTop, yBot));
    }
    if (skirt) {
        float yTop = dress ? d.hipY + 0.02f * S : d.waistY, yBot = d.kneeY + (dress ? 0.02f : 0.035f) * S;
        std::vector<RingDef> rr;
        for (int q = 0; q <= 5; q++) {
            float t = q / 5.0f, y = lerpf(yTop, yBot, t);
            RingDef r;
            r.c = Vec3(0, y, 0);
            r.u = Vec3(1, 0, 0);
            r.w = Vec3(0, 0, 1);
            float top = dress ? d.Whip + 0.004f * S : d.Wwaist + 0.004f * S;
            r.ru = lerpf(top, d.Whip * 1.32f, std::sqrt(t)) + (t > 0.25f ? 0 : 0.0f);
            r.rw = lerpf(dress ? d.Dhip + 0.004f * S : d.Dwaist + 0.004f * S, d.Dhip * 1.45f, std::sqrt(t)) + d.belly * (1 - t) * 0.6f;
            r.pw = 2.2f;
            r.col = dress ? topC : botC;
            rr.push_back(r);
        }
        loft(cb, rr, 24, false, false, nullptr, sideSkin(yTop, yBot));
    }

    // ---- benen ----
    for (int s = -1; s <= 1; s += 2) {
        int th = s > 0 ? B_LTHIGH : B_RTHIGH, sh = th + 1, ft = th + 2;
        Vec3 hip = m.joint[th], knee = m.joint[sh], ank = m.joint[ft];
        // dij
        {
            Vec3 A = hip + Vec3(0, 0.035f * S, 0), B = knee;
            Vec3 dir = normalize(B - A), u, w;
            frameFor(dir, u, w);
            std::vector<float> ts = {0, 0.12f, 0.3f, 0.5f, 0.7f, 0.88f, 1.0f};
            float cut = a.bottom == 2 && !skirt ? 0.55f : -1;
            if (cut > 0) { ts.push_back(cut); ts.push_back(cut + 0.001f); }
            std::sort(ts.begin(), ts.end());
            std::vector<RingDef> rr;
            for (float t : ts) {
                RingDef r;
                r.c = A + (B - A) * t;
                r.u = u;
                r.w = w;
                float rad = lerpf(d.rThigh, d.rKnee * 1.12f, std::pow(t, 1.4f));
                bool cloth = longLegs || (cut > 0 && t <= cut);
                if (cloth) rad += a.bottom == 2 ? 0.006f * S : inflLeg;
                r.ru = rad;
                r.rw = rad * 1.06f;
                r.col = cloth ? botC : skin;
                if (t < 0.15f) { r.b0 = B_PELVIS; r.b1 = th; r.wt = 0.35f + 0.65f * t / 0.15f; }
                else if (t > 0.88f) { r.b0 = th; r.b1 = sh; r.wt = (t - 0.88f) / 0.12f * 0.5f; }
                else { r.b0 = r.b1 = th; r.wt = 0; }
                rr.push_back(r);
            }
            loft(cb, rr, 14, false, false);
        }
        // scheen
        {
            Vec3 A = knee, B = ank + Vec3(0, 0.01f * S, 0);
            Vec3 dir = normalize(B - A), u, w;
            frameFor(dir, u, w);
            bool boots = a.shoes == 2;
            std::vector<float> ts = {0, 0.12f, 0.25f, 0.42f, 0.6f, 0.8f, 1.0f};
            if (boots) { ts.push_back(0.5f); ts.push_back(0.501f); }
            std::sort(ts.begin(), ts.end());
            std::vector<RingDef> rr;
            for (float t : ts) {
                RingDef r;
                r.c = A + (B - A) * t;
                r.u = u;
                r.w = w;
                float rad = t < 0.25f ? lerpf(d.rKnee * 1.1f, d.rCalf, t / 0.25f) : t < 0.6f ? lerpf(d.rCalf, d.rCalf * 0.82f, (t - 0.25f) / 0.35f)
                                                                                         : lerpf(d.rCalf * 0.82f, d.rAnkle * 1.05f, (t - 0.6f) / 0.4f);
                bool boot = boots && t > 0.5f;
                if (longLegs) rad = std::max(rad, lerpf(d.rKnee * 1.1f, d.rAnkle * 1.6f, t)) + inflLeg;
                if (boot) rad = std::max(rad, d.rCalf * 0.9f) + 0.006f * S;
                r.ru = rad;
                r.rw = rad * 1.08f;
                r.col = boot ? shoeC : longLegs ? botC : skin;
                if (t < 0.12f) { r.b0 = th; r.b1 = sh; r.wt = 0.5f + 0.5f * t / 0.12f; }
                else { r.b0 = r.b1 = sh; r.wt = 0; }
                rr.push_back(r);
            }
            loft(cb, rr, 14, false, false);
        }
        // voet / schoen
        {
            float heel = ank.z - 0.035f * S, toe = heel + d.footLen;
            std::vector<RingDef> rr;
            float infl = a.shoes == 3 ? 0.0f : a.shoes == 1 ? 0.002f * S : 0.004f * S;
            for (int q = 0; q <= 7; q++) {
                float t = q / 7.0f;
                RingDef r;
                float h = t < 0.35f ? 0.056f * S : lerpf(0.056f * S, 0.024f * S, (t - 0.35f) / 0.65f);
                if (a.shoes == 1) h *= 0.9f;
                float wdt = (0.019f + 0.011f * std::sin(PI * std::min(1.0f, t * 1.15f))) * S + infl;
                r.c = Vec3(ank.x, h / 2, lerpf(heel, toe, t));
                r.u = Vec3(1, 0, 0);
                r.w = Vec3(0, 1, 0);
                r.ru = wdt;
                r.rw = h / 2 + infl * 0.5f;
                r.pw = 3.0f;
                r.col = a.shoes == 3 ? skin : shoeC;
                r.b0 = r.b1 = ft;
                rr.push_back(r);
            }
            VColFn vc = nullptr;
            if (a.shoes == 3) vc = [&](const RingDef& r, Vec3 p, float) { return p.y < 0.011f * S ? Vec3(0.25f, 0.16f, 0.09f) : r.col; };
            else if (a.shoes == 0) vc = [&](const RingDef& r, Vec3 p, float) { return p.y < 0.010f * S ? Vec3(0.92f, 0.92f, 0.90f) : r.col; };
            loft(cb, rr, 14, true, true, vc);
        }
    }

    // ---- armen ----
    for (int s = -1; s <= 1; s += 2) {
        int ua = s > 0 ? B_LUARM : B_RUARM, fa = ua + 1, hd = ua + 2;
        Vec3 dir = s > 0 ? ARM_L : ARM_R;
        Vec3 sh = m.joint[ua], el = m.joint[fa], wr = m.joint[hd];
        float sleeveCut = longSleeve ? 2.0f : dress ? 0.12f : 0.45f;
        float inflA = longSleeve ? (a.top == 5 ? 0.006f : 0.003f) * S : 0.0015f * S;
        {
            Vec3 A = sh + Vec3(0, 0.008f * S, 0) - dir * (0.012f * S), B = el;
            Vec3 u, w;
            frameFor(normalize(B - A), u, w);
            std::vector<float> ts = {0, 0.15f, 0.35f, 0.6f, 0.82f, 1.0f};
            if (sleeveCut < 1) { ts.push_back(sleeveCut); ts.push_back(sleeveCut + 0.001f); }
            std::sort(ts.begin(), ts.end());
            std::vector<RingDef> rr;
            for (float t : ts) {
                RingDef r;
                r.c = A + (B - A) * t;
                r.u = u;
                r.w = w;
                float rad = t < 0.35f ? lerpf(d.rUA * 1.2f, d.rUA * 1.04f, t / 0.35f) : lerpf(d.rUA * 1.04f, d.rFA * 1.08f, (t - 0.35f) / 0.65f);
                bool cloth = t <= sleeveCut;
                if (cloth) rad += inflA;
                r.ru = rad;
                r.rw = rad;
                r.col = cloth ? topC : skin;
                if (t < 0.18f) { r.b0 = B_CHEST; r.b1 = ua; r.wt = 0.3f + 0.7f * t / 0.18f; }
                else if (t > 0.86f) { r.b0 = ua; r.b1 = fa; r.wt = (t - 0.86f) / 0.14f * 0.5f; }
                else { r.b0 = r.b1 = ua; r.wt = 0; }
                rr.push_back(r);
            }
            loft(cb, rr, 12, false, false);
        }
        {
            Vec3 A = el, B = wr;
            Vec3 u, w;
            frameFor(normalize(B - A), u, w);
            std::vector<RingDef> rr;
            for (float t : {0.0f, 0.12f, 0.3f, 0.6f, 0.9f, 1.0f}) {
                RingDef r;
                r.c = A + (B - A) * t;
                r.u = u;
                r.w = w;
                float rad = t < 0.3f ? lerpf(d.rFA * 1.08f, d.rFA * 1.06f, t / 0.3f) : lerpf(d.rFA * 1.06f, d.rWrist, (t - 0.3f) / 0.7f);
                if (longSleeve) rad += inflA;
                r.ru = rad;
                r.rw = rad * 1.12f;
                r.col = longSleeve ? topC : skin;
                if (t < 0.12f) { r.b0 = ua; r.b1 = fa; r.wt = 0.5f + 0.5f * t / 0.12f; }
                else if (t > 0.9f) { r.b0 = fa; r.b1 = hd; r.wt = (t - 0.9f) / 0.1f * 0.5f; }
                else { r.b0 = r.b1 = fa; r.wt = 0; }
                rr.push_back(r);
            }
            loft(cb, rr, 12, false, false);
        }
        {
            Vec3 A = wr - dir * (0.004f * S), B = wr + dir * d.lenHand;
            Vec3 u, w;
            frameFor(normalize(B - A), u, w);
            std::vector<RingDef> rr;
            for (float t : {0.0f, 0.15f, 0.4f, 0.7f, 1.0f}) {
                RingDef r;
                r.c = A + (B - A) * t;
                r.u = u;
                r.w = w;
                r.ru = lerpf(0.0125f, 0.0085f, t) * S;
                r.rw = (t < 0.4f ? lerpf(0.017f, 0.026f, t / 0.4f) : lerpf(0.026f, 0.016f, (t - 0.4f) / 0.6f)) * S;
                r.pw = 2.4f;
                r.col = skin;
                if (t < 0.15f) { r.b0 = fa; r.b1 = hd; r.wt = 0.5f + 0.5f * t / 0.15f; }
                else { r.b0 = r.b1 = hd; r.wt = 0; }
                rr.push_back(r);
            }
            loft(cb, rr, 10, false, true);
            wire(cb, {wr + dir * (0.025f * S) + w * (0.018f * S), wr + dir * (0.06f * S) + w * (0.032f * S) - u * (0.004f * S)}, 0.0065f * S, skin, hd, 6);
        }
    }

    // ---- hals ----
    {
        Vec3 A(0, d.neckBase - 0.02f * S, -0.004f * S), B(0, d.headC.y - d.hh * 0.45f, -0.012f * S);
        Vec3 u, w;
        frameFor(normalize(B - A), u, w);
        std::vector<RingDef> rr;
        for (float t : {0.0f, 0.3f, 0.7f, 1.0f}) {
            RingDef r;
            r.c = A + (B - A) * t;
            r.u = u;
            r.w = w;
            r.ru = d.neckR;
            r.rw = d.neckR * 1.05f;
            r.col = skin;
            if (t < 0.35f) { r.b0 = B_CHEST; r.b1 = B_NECK; r.wt = 0.4f + 0.6f * t / 0.35f; }
            else if (t > 0.65f) { r.b0 = B_NECK; r.b1 = B_HEAD; r.wt = (t - 0.65f) / 0.35f * 0.8f; }
            else { r.b0 = r.b1 = B_NECK; r.wt = 0; }
            rr.push_back(r);
        }
        loft(cb, rr, 12, false, false);
        // kraag (overhemd, polo, uniform, colbert) / col (trui)
        if (a.top == 1 || a.top == 2 || a.top == 3 || a.top == 6 || a.top == 8) {
            Vec3 cc = (a.top == 6 || a.top == 8) ? shirtC : topC;
            std::vector<RingDef> kr;
            for (int q = 0; q < 2; q++) {
                RingDef r;
                r.c = Vec3(0, d.neckBase - 0.004f * S + q * 0.026f * S, -0.004f * S);
                r.u = Vec3(1, 0, 0);
                r.w = Vec3(0, 0, 1);
                r.ru = d.neckR * (a.top == 3 ? 1.2f : 1.34f - q * 0.1f);
                r.rw = d.neckR * (a.top == 3 ? 1.22f : 1.36f - q * 0.1f);
                r.col = cc;
                r.b0 = B_CHEST;
                r.b1 = B_NECK;
                r.wt = 0.3f + q * 0.4f;
                kr.push_back(r);
            }
            loft(cb, kr, 16, false, false);
        }
        if (a.top == 4) ellipsoid(cb, Vec3(0, d.neckBase + 0.005f * S, -d.Dchest * 0.75f), Vec3(d.shoulderHalf * 0.55f, 0.04f * S, 0.035f * S), topC * 0.95f, B_CHEST, 12, 8);  // capuchon
    }

    // ---- hoofd en gezicht ----
    const Vec3 hc = d.headC;
    const float hw = d.hw, hh = d.hh, hdp = d.hd;
    ellipsoid(cb, hc, Vec3(hw, hh, hdp), skin, B_HEAD, 22, 16, [](Vec3 u) {
        float x = u.x, y = u.y, z = u.z;
        if (y < -0.15f) {
            float k = (-y - 0.15f) / 0.85f;
            x *= 1 - 0.30f * k;
            z = z * (1 - 0.10f * k) + (z > 0 ? 0.06f * k : 0.0f);
        }
        if (z < 0) z *= 1.04f;
        if (y > 0.5f && z > 0) z *= 0.97f;
        return Vec3(x, y, z);
    });
    {
        uint32_t hs = 0;
        for (char c : a.name) hs = hs * 31 + (uint8_t)c;
        Vec3 iris = a.skin <= 1 && (hs & 1) ? Vec3(0.22f, 0.38f, 0.58f) : a.skin <= 2 && (hs & 2) ? Vec3(0.26f, 0.36f, 0.20f) : Vec3(0.22f, 0.13f, 0.06f);
        float ey = hc.y + 0.08f * hh, ez = hc.z + hdp * 0.83f, ex = 0.36f * hw;
        float es = a.age == 0 ? 1.15f : 1.0f;
        for (int s = -1; s <= 1; s += 2) {
            ellipsoid(cb, Vec3(s * ex, ey, ez), Vec3(0.15f * hw * es, 0.10f * hh * es, 0.08f * hdp), Vec3(0.94f, 0.94f, 0.92f), B_HEAD, 8, 6);
            ellipsoid(cb, Vec3(s * ex, ey, ez + 0.07f * hdp), Vec3(0.075f * hw * es, 0.078f * hh * es, 0.03f * hdp), iris, B_HEAD, 8, 6);
            Vec3 brow = a.age == 3 ? Vec3(0.6f, 0.6f, 0.58f) : (a.hairColor >= 9 ? Vec3(0.2f, 0.15f, 0.1f) : hairC * 0.85f);
            ellipsoid(cb, Vec3(s * ex, ey + 0.18f * hh, ez + 0.03f * hdp), Vec3(0.21f * hw, 0.035f * hh, 0.05f * hdp), brow, B_HEAD, 8, 4);
            ellipsoid(cb, Vec3(s * hw * 0.97f, hc.y + 0.02f * hh, hc.z - 0.06f * hdp), Vec3(0.12f * hw, 0.27f * hh, 0.17f * hdp), skin * 0.93f, B_HEAD, 8, 6);
        }
        ellipsoid(cb, Vec3(0, hc.y - 0.10f * hh, hc.z + hdp * 0.95f), Vec3(0.13f * hw, 0.24f * hh, 0.16f * hdp), skin * 0.97f, B_HEAD, 8, 6,
                  [](Vec3 u) { return Vec3(u.x * (u.y < 0 ? 1.25f : 0.8f), u.y, u.z + (u.y < 0 ? 0.25f : 0.0f)); });
        Vec3 lips = skin * 0.55f + Vec3(0.62f, 0.28f, 0.28f) * 0.45f;
        ellipsoid(cb, Vec3(0, hc.y - 0.43f * hh, hc.z + hdp * 0.84f), Vec3(0.27f * hw, 0.05f * hh, 0.07f * hdp), lips, B_HEAD, 10, 4);
    }
    // ---- haar ----
    {
        Vec3 R(hw, hh, hdp);
        auto shell = [&](float sc, float fr, float sd, float bk, float bump = 0, float ft = 0, Vec3 col = Vec3(-1, 0, 0)) {
            ellipsoid(cb, hc, R, col.x < 0 ? hairC : col, B_HEAD, 24, 24, hairScale(sc, bump, ft), hairLimit(fr, sd, bk));
        };
        switch (a.hair) {
        case 1: shell(1.06f, 0.36f, 0.50f, 0.72f); break;
        case 2: shell(1.025f, 0.33f, 0.47f, 0.66f); break;
        case 3: shell(1.10f, 0.40f, 0.68f, 0.85f); break;
        case 4: {
            shell(1.10f, 0.38f, 0.66f, 0.86f);
            std::vector<RingDef> rr;
            float yTop = hc.y + 0.1f * hh, yBot = d.shoulderY - 0.10f * S;
            for (int q = 0; q <= 5; q++) {
                float t = q / 5.0f;
                RingDef r;
                r.c = Vec3(0, lerpf(yTop, yBot, t), lerpf(hc.z - hdp * 0.70f, -d.Dchest * 0.95f - 0.012f * S, t));
                r.u = Vec3(1, 0, 0);
                r.w = Vec3(0, 0, 1);
                r.ru = hw * lerpf(1.04f, 1.10f, t) * (1 - 0.25f * t * t);
                r.rw = 0.028f * S * (1 - 0.4f * t);
                r.col = hairC;
                r.b0 = B_HEAD;
                r.b1 = B_CHEST;
                r.wt = smooth01(t * 1.3f);
                rr.push_back(r);
            }
            loft(cb, rr, 14, false, true);
            break;
        }
        case 5: {
            shell(1.06f, 0.37f, 0.52f, 0.74f);
            std::vector<RingDef> rr;
            Vec3 p0(0, hc.y + 0.2f * hh, hc.z - hdp * 1.02f), p1(0, hc.y - 0.25f * hh, hc.z - hdp * 1.22f), p2(0, hc.y - 1.0f * hh, hc.z - hdp * 1.12f);
            for (int q = 0; q <= 6; q++) {
                float t = q / 6.0f;
                Vec3 c = t < 0.5f ? p0 + (p1 - p0) * (t * 2) : p1 + (p2 - p1) * ((t - 0.5f) * 2);
                Vec3 dd = t < 0.5f ? normalize(p1 - p0) : normalize(p2 - p1);
                RingDef r;
                frameFor(dd, r.u, r.w);
                r.c = c;
                r.ru = r.rw = 0.024f * S * (t < 0.15f ? 0.8f : 1.0f - 0.55f * t);
                r.col = hairC;
                r.b0 = r.b1 = B_HEAD;
                rr.push_back(r);
            }
            loft(cb, rr, 10, true, true);
            break;
        }
        case 6:
            shell(1.06f, 0.37f, 0.55f, 0.78f);
            ellipsoid(cb, Vec3(0, hc.y + 0.62f * hh, hc.z - 0.55f * hdp), Vec3(0.42f * hw, 0.36f * hh, 0.40f * hdp), hairC, B_HEAD, 12, 10);
            break;
        case 7: shell(1.15f, 0.40f, 0.62f, 0.80f, 0.05f); break;
        case 8: shell(1.38f, 0.42f, 0.62f, 0.80f, 0.04f); break;
        case 9: shell(1.12f, 0.41f, 0.74f, 0.80f); break;
        case 10: {
            shell(1.07f, 0.38f, 0.60f, 0.80f);
            for (int s = -1; s <= 1; s += 2) {
                std::vector<Vec3> pts;
                Vec3 p0(s * hw * 0.80f, hc.y - 0.2f * hh, hc.z - 0.3f * hdp), p1(s * hw * 1.0f, d.shoulderY - 0.01f * S, d.Dchest * 0.5f),
                    p2(s * 0.065f * S, d.chestY - 0.03f * S, d.Dchest + d.bust * 0.6f + 0.012f * S);
                for (int q = 0; q <= 8; q++) {
                    float t = q / 8.0f;
                    pts.push_back(t < 0.5f ? p0 + (p1 - p0) * (t * 2) : p1 + (p2 - p1) * ((t - 0.5f) * 2));
                }
                wire(cb, pts, 0.016f * S, hairC, B_HEAD, 8, B_CHEST);
            }
            break;
        }
        case 11: shell(1.07f, 0.35f, 0.50f, 0.70f, 0, 0.30f); break;
        case 12:
            shell(1.015f, 0.33f, 0.47f, 0.64f, 0, 0, hairC * 0.75f + skin * 0.25f);
            ellipsoid(cb, hc, R, hairC, B_HEAD, 24, 24, hairScale(1.24f), [](float th, float ph) { return th < 0.62f * PI && std::fabs(std::sin(ph)) * std::sin(th) < 0.17f; });
            break;
        default: break;
        }
        // baard
        if (a.beard == 1) ellipsoid(cb, hc, R, skin * 0.55f + hairC * 0.45f, B_HEAD, 24, 20, hairScale(1.012f),
                                    [](float th, float ph) { return th > 0.56f * PI && std::cos(ph) > -0.15f; });
        if (a.beard == 4) ellipsoid(cb, hc, R, hairC, B_HEAD, 24, 20, hairScale(1.07f), [](float th, float ph) { return th > 0.53f * PI && std::cos(ph) > -0.3f; });
        if (a.beard >= 2) ellipsoid(cb, Vec3(0, hc.y - 0.30f * hh, hc.z + hdp * 0.93f), Vec3(0.30f * hw, 0.065f * hh, 0.08f * hdp), hairC, B_HEAD, 10, 5);
        if (a.beard == 3) ellipsoid(cb, Vec3(0, hc.y - 0.78f * hh, hc.z + hdp * 0.70f), Vec3(0.22f * hw, 0.18f * hh, 0.17f * hdp), hairC, B_HEAD, 10, 8);
    }
    // ---- bril ----
    if (a.glasses > 0) {
        float ey = hc.y + 0.08f * hh, ez = hc.z + hdp * 0.95f, ex = 0.36f * hw;
        float wr = a.glasses == 3 ? 0.0045f * S : 0.0022f * S;
        Vec3 fc = a.glasses == 1 ? Vec3(0.62f, 0.60f, 0.55f) : a.glasses == 3 ? Vec3(0.04f, 0.04f, 0.045f) : Vec3(0.12f, 0.12f, 0.13f);
        float lw = a.glasses == 1 ? 0.2f * hw : 0.25f * hw, lh = a.glasses == 1 ? 0.2f * hw : 0.14f * hh * (a.glasses == 4 ? 1.35f : 1.0f);
        for (int s = -1; s <= 1; s += 2) {
            std::vector<Vec3> rim;
            for (int k = 0; k <= 16; k++) {
                float an = 2 * PI * k / 16;
                float cx = std::cos(an), cy = std::sin(an);
                if (a.glasses != 1) { cx = spow(cx, 0.55f); cy = spow(cy, 0.55f); }
                rim.push_back(Vec3(s * ex + cx * lw, ey + cy * lh, ez));
            }
            wire(cb, rim, wr, fc, B_HEAD, 5);
            if (a.glasses == 4) disc(cb, Vec3(s * ex, ey, ez - 0.001f), rim, Vec3(0.03f, 0.035f, 0.04f), B_HEAD);
            wire(cb, {Vec3(s * (ex + lw), ey + 0.3f * lh, ez), Vec3(s * hw * 1.0f, ey + 0.02f * hh, hc.z - 0.08f * hdp)}, wr * 0.8f, fc, B_HEAD, 5);
        }
        wire(cb, {Vec3(ex - lw, ey + 0.35f * lh, ez + 0.003f), Vec3(0, ey + 0.45f * lh, ez + 0.012f * S), Vec3(-ex + lw, ey + 0.35f * lh, ez + 0.003f)}, wr * 0.9f, fc, B_HEAD, 5);
    }
    // ---- hoofddeksel ----
    if (a.hat > 0) {
        Vec3 R(hw, hh, hdp);
        float big = a.hair == 8 ? 1.25f : a.hair == 7 ? 1.06f : 1.0f;
        if (a.hat == 1 || a.hat == 4) {
            ellipsoid(cb, hc, R, hatC, B_HEAD, 24, 20, hairScale(1.11f * big), [](float th, float) { return th < 0.45f * PI; });
            float zs = a.hat == 4 ? -1.0f : 1.0f;
            float yb = hc.y + 0.30f * hh;
            std::vector<Vec3> rim;
            for (int k = 0; k <= 12; k++) {
                float an = -PI / 2 + PI * k / 12;
                rim.push_back(Vec3(std::sin(an) * 0.80f * hw * big, yb - 0.06f * hh * std::cos(an), hc.z + zs * (hdp * 0.82f * big + std::cos(an) * 0.78f * hdp)));
            }
            disc(cb, Vec3(0, yb, hc.z + zs * hdp * 0.82f * big), rim, hatC * 0.85f, B_HEAD);
            ellipsoid(cb, Vec3(0, hc.y + hh * 1.10f * big, hc.z), Vec3(0.08f * hw, 0.05f * hh, 0.08f * hdp), hatC * 0.8f, B_HEAD, 6, 4);
        } else if (a.hat == 2) {
            ellipsoid(cb, hc, R, hatC, B_HEAD, 24, 20, hairScale(1.13f * big), hairLimit(0.42f, 0.55f, 0.64f));
            ellipsoid(cb, Vec3(0, hc.y + hh * 1.16f * big, hc.z - 0.05f * hdp), Vec3(0.20f * hw, 0.18f * hh, 0.20f * hdp), hatC * 1.1f, B_HEAD, 8, 6, nullptr, nullptr,
                      nullptr);
        } else if (a.hat == 3 || a.hat == 5) {
            bool uni = a.hat == 5;
            float y0 = hc.y + 0.26f * hh, y1 = hc.y + (uni ? 0.95f : 1.08f) * hh;
            std::vector<RingDef> rr;
            for (int q = 0; q <= 3; q++) {
                float t = q / 3.0f;
                RingDef r;
                r.c = Vec3(0, lerpf(y0, y1, t), hc.z - (uni ? 0.0f : 0.02f) * hdp);
                r.u = Vec3(1, 0, 0);
                r.w = Vec3(0, 0, 1);
                float sc = uni ? lerpf(1.08f, 1.20f, t) : lerpf(1.12f, 1.0f, t);
                r.ru = hw * sc * big;
                r.rw = hdp * sc * big;
                r.pw = 2.0f;
                r.col = q == 0 ? hatC * 0.4f : hatC;
                r.b0 = r.b1 = B_HEAD;
                rr.push_back(r);
            }
            loft(cb, rr, 20, false, true);
            if (uni) {
                std::vector<Vec3> rim;
                for (int k = 0; k <= 10; k++) {
                    float an = -PI / 2 + PI * k / 10;
                    rim.push_back(Vec3(std::sin(an) * 0.75f * hw, y0 - 0.02f * hh - 0.08f * hh * std::cos(an), hc.z + hdp * 1.0f + std::cos(an) * 0.42f * hdp));
                }
                disc(cb, Vec3(0, y0 - 0.02f * hh, hc.z + hdp), rim, Vec3(0.03f, 0.03f, 0.035f), B_HEAD);
            } else {
                RingDef a0, a1;
                a0.c = a1.c = Vec3(0, y0 + 0.01f * hh, hc.z);
                a0.u = a1.u = Vec3(1, 0, 0);
                a0.w = a1.w = Vec3(0, 0, 1);
                a0.ru = hw * 1.12f * big; a0.rw = hdp * 1.12f * big;
                a1.ru = hw * 1.75f * big; a1.rw = hdp * 1.65f * big;
                a0.col = a1.col = hatC * 0.9f;
                a0.b0 = a0.b1 = a1.b0 = a1.b1 = B_HEAD;
                loft(cb, {a0, a1}, 24, false, false);
            }
        }
    }
    // ---- tas ----
    if (a.bag == 1) {
        boxPart(cb, Vec3(0, d.chestY - 0.035f * S, -(d.Dchest + 0.045f * S)), Vec3(0.082f * S, 0.115f * S, 0.040f * S), bagC, B_CHEST);
        for (int s = -1; s <= 1; s += 2)
            wire(cb, {Vec3(s * 0.058f * S, d.chestY + 0.07f * S, -(d.Dchest + 0.012f * S)), Vec3(s * 0.064f * S, d.shoulderY + 0.012f * S, -0.012f * S),
                      Vec3(s * 0.068f * S, d.shoulderY - 0.012f * S, d.Dchest * 0.72f), Vec3(s * 0.075f * S, d.chestY - 0.06f * S, d.Dchest + d.bust + 0.006f * S)},
                 0.006f * S, bagC * 0.7f, B_CHEST, 6);
    } else if (a.bag == 2) {
        Vec3 bc(-(d.Whip + 0.032f * S), d.hipY - 0.012f * S, 0.02f * S);
        boxPart(cb, bc, Vec3(0.022f * S, 0.055f * S, 0.075f * S), bagC, B_PELVIS);
        wire(cb, {Vec3(d.Wchest * 0.72f, d.shoulderY + 0.012f * S, -0.004f * S), Vec3(0.02f * S, d.chestY + 0.015f * S, d.Dchest + d.bust * 0.8f + 0.008f * S),
                  Vec3(-d.Wwaist * 0.85f, d.waistY - 0.01f * S, d.Dwaist + d.belly + 0.008f * S), bc + Vec3(0, 0.055f * S, 0)},
             0.005f * S, bagC * 0.75f, B_CHEST, 6, B_PELVIS);
    } else if (a.bag == 3) {
        Vec3 tip = m.joint[B_RHAND] + ARM_R * (d.lenHand * 0.85f);
        boxPart(cb, tip + Vec3(0, -0.06f * S, 0), Vec3(0.018f * S, 0.048f * S, 0.068f * S), bagC, B_RHAND);
        wire(cb, {tip + Vec3(0, -0.012f * S, -0.035f * S), tip + Vec3(0, 0.012f * S, 0), tip + Vec3(0, -0.012f * S, 0.035f * S)}, 0.004f * S, bagC * 0.7f, B_RHAND, 5);
    }
    uploadCB(m, cb);
    return m;
}

void destroyCharacter(CharModel& m) {
    if (m.vbo) glDeleteBuffers(1, &m.vbo);
    if (m.ibo) glDeleteBuffers(1, &m.ibo);
    if (m.vao) glDeleteVertexArrays(1, &m.vao);
    m.vao = m.vbo = m.ibo = 0;
    m.count = 0;
}

// ============================== animatie ==============================
namespace {
struct E3 { float x = 0, y = 0, z = 0; };
const int PARENT[BONE_COUNT] = {-1, 0, 1, 2, 3, 2, 5, 6, 2, 8, 9, 0, 11, 12, 0, 14, 15};
}

void computePose(const CharModel& m, const AnimState& s, CharPose& out) {
    E3 r[BONE_COUNT];
    Vec3 root(0, 0, 0);
    const float S = m.stature;
    const float breath = std::sin(s.t * 1.7f + s.variant * 1.3f);
    auto hang = [&]() {
        r[B_LUARM].z = -0.05f; r[B_RUARM].z = 0.05f;
        r[B_LFARM].x = -0.12f; r[B_RFARM].x = -0.12f;
    };
    auto sitLegs = [&](float thigh, float knee) {
        r[B_LTHIGH].x = r[B_RTHIGH].x = -thigh;
        r[B_LTHIGH].y = 0.07f; r[B_RTHIGH].y = -0.07f;
        r[B_LSHIN].x = r[B_RSHIN].x = knee;
        r[B_LFOOT].x = r[B_RFOOT].x = 0.05f;
    };
    switch (s.kind) {
    case ANIM_WALK: {
        float p = 2 * PI * s.phase;
        float sp = clampf(s.speed / 1.4f, 0.3f, 1.7f);
        float amp = 0.30f * std::min(1.3f, sp) + 0.04f;
        float legL = amp * std::sin(p);
        r[B_LTHIGH].x = -legL;
        r[B_RTHIGH].x = legL;
        auto knee = [&](float ph) { float c = std::max(0.0f, std::cos(ph)); return 0.10f + 0.95f * std::min(1.2f, sp) * c * c; };
        r[B_LSHIN].x = knee(p);
        r[B_RSHIN].x = knee(p + PI);
        r[B_LFOOT].x = -0.15f * std::sin(p - 0.6f);
        r[B_RFOOT].x = -0.15f * std::sin(p + PI - 0.6f);
        float armA = 0.55f * amp;
        r[B_LUARM].x = armA * std::sin(p);
        r[B_RUARM].x = -armA * std::sin(p);
        r[B_LUARM].z = -0.03f;
        r[B_RUARM].z = 0.03f;
        r[B_LFARM].x = -(0.25f + 0.25f * std::max(0.0f, -std::sin(p)));
        r[B_RFARM].x = -(0.25f + 0.25f * std::max(0.0f, std::sin(p)));
        r[B_PELVIS].y = 0.10f * std::sin(p) * std::min(1.0f, sp);
        r[B_CHEST].y = -0.16f * std::sin(p) * std::min(1.0f, sp);
        r[B_SPINE].x = 0.05f * std::min(1.0f, sp);
        root.y = -m.hipY * (1 - std::cos(amp)) * 0.7f * (1 - std::cos(2 * p)) * 0.5f;
        break;
    }
    case ANIM_SIT:
    case ANIM_SIT_PHONE:
        sitLegs(1.50f, 1.45f);
        r[B_SPINE].x = -0.06f;
        r[B_CHEST].x = 0.012f * breath;
        if (s.kind == ANIM_SIT_PHONE) {
            r[B_RUARM].x = -0.55f; r[B_RUARM].z = 0.15f; r[B_RFARM].x = -1.75f;
            r[B_LUARM].x = -0.45f; r[B_LUARM].z = -0.12f; r[B_LFARM].x = -1.5f;
        } else {
            r[B_LUARM].x = r[B_RUARM].x = -0.42f;
            r[B_LUARM].z = -0.08f; r[B_RUARM].z = 0.08f;
            r[B_LFARM].x = r[B_RFARM].x = -0.85f;
        }
        break;
    case ANIM_DRIVE:
        sitLegs(1.40f, 1.25f);
        r[B_SPINE].x = 0.02f;
        r[B_LUARM].x = r[B_RUARM].x = -1.0f;
        r[B_LUARM].z = -0.12f + 0.18f * s.wheel;
        r[B_RUARM].z = 0.12f + 0.18f * s.wheel;
        r[B_LUARM].y = -0.15f; r[B_RUARM].y = 0.15f;
        r[B_LFARM].x = r[B_RFARM].x = -0.55f;
        break;
    default: {
        // staan
        r[B_CHEST].x = 0.012f * breath;
        root.x = 0.008f * S * std::sin(0.35f * s.t + s.variant);
        hang();
        if (s.variant % 4 == 1) {
            r[B_LUARM].x = r[B_RUARM].x = -0.22f;
            r[B_LUARM].z = -0.18f; r[B_RUARM].z = 0.18f;
            r[B_LFARM].x = r[B_RFARM].x = -1.05f;
        } else if (s.variant % 4 == 3) {
            r[B_RTHIGH].x = -0.06f; r[B_RSHIN].x = 0.16f; r[B_RTHIGH].z = -0.04f;
            r[B_PELVIS].z = 0.03f;
        }
        if (s.kind == ANIM_WHISTLE) {
            r[B_RUARM].x = -1.15f; r[B_RUARM].z = 0.55f; r[B_RFARM].x = -2.05f;
        } else if (s.kind == ANIM_CHECK) {
            r[B_LUARM].x = r[B_RUARM].x = -0.42f;
            r[B_LUARM].z = -0.12f; r[B_RUARM].z = 0.12f;
            r[B_LFARM].x = r[B_RFARM].x = -1.30f;
            r[B_HEAD].x += 0.30f;
        } else if (s.kind == ANIM_PHONE) {
            r[B_RUARM].x = -0.30f; r[B_RUARM].z = 0.22f; r[B_RFARM].x = -1.75f;
            r[B_LUARM].x = -0.25f; r[B_LUARM].z = -0.20f; r[B_LFARM].x = -1.55f;
            r[B_HEAD].x += 0.35f;
        }
        break;
    }
    }
    if (s.press > 0) {
        float k = clampf(s.press, 0, 1);
        r[B_RUARM].x = lerpf(r[B_RUARM].x, -2.15f, k);
        r[B_RUARM].z = lerpf(r[B_RUARM].z, 0.25f, k);
        r[B_RFARM].x = lerpf(r[B_RFARM].x, -0.25f, k);
    }
    r[B_NECK].y += s.headYaw * 0.4f;
    r[B_HEAD].y += s.headYaw * 0.6f;
    r[B_NECK].x += s.headPitch * 0.3f;
    r[B_HEAD].x += s.headPitch * 0.7f;
    for (int b = 0; b < BONE_COUNT; b++) {
        Mat4 R = Mat4::rotY(r[b].y) * Mat4::rotX(r[b].x) * Mat4::rotZ(r[b].z);
        Vec3 J = m.joint[b];
        if (b == 0) out.global[b] = Mat4::translation(J.x + root.x, J.y + root.y, J.z + root.z) * R;
        else {
            Vec3 P = m.joint[PARENT[b]];
            out.global[b] = out.global[PARENT[b]] * Mat4::translation(J.x - P.x, J.y - P.y, J.z - P.z) * R;
        }
        out.skin[b] = out.global[b] * Mat4::translation(-J.x, -J.y, -J.z);
    }
    out.drop = -root.y;
}

// ============================== tekenen ==============================
namespace {
const char* CHAR_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aN;
layout(location=2) in vec3 aC;
layout(location=3) in vec3 aS;
uniform mat4 uVP;
uniform mat4 uModel;
uniform mat4 uBones[17];
out vec3 vW; out vec3 vN; out vec3 vC;
void main() {
    int b0 = int(aS.x + 0.5), b1 = int(aS.y + 0.5);
    mat4 S = uBones[b0] * (1.0 - aS.z) + uBones[b1] * aS.z;
    vec4 w = uModel * (S * vec4(aPos, 1.0));
    vW = w.xyz;
    vN = mat3(uModel) * (mat3(S) * aN);
    vC = aC;
    gl_Position = uVP * w;
})";
const char* CHAR_FS = R"(#version 330 core
in vec3 vW; in vec3 vN; in vec3 vC;
uniform vec3 uSun, uSunCol, uSky, uCam;
uniform float uAmb, uFogDist;
uniform int uInterior;
out vec4 oColor;
void main() {
    vec3 n = normalize(vN);
    vec3 toCam = uCam - vW;
    float dist = length(toCam);
    vec3 v = toCam / max(dist, 1e-4);
    if (dot(n, v) < 0.0) n = -n;
    float hemi = 0.5 + 0.5 * n.y;
    vec3 col;
    if (uInterior == 1) {
        col = vC * (0.50 + 0.22 * uAmb + 0.22 * hemi + 0.10 * max(dot(n, normalize(vec3(0.3, 1.0, 0.2))), 0.0));
    } else {
        float diff = max(dot(n, uSun), 0.0);
        vec3 amb = mix(vec3(0.56, 0.57, 0.60), uSky * 0.85, 0.3) * (0.80 + 0.20 * hemi) * uAmb;
        col = vC * (amb + uSunCol * diff * 0.72);
    }
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    col += uSky * rim * 0.05 * uAmb;
    float t = dist / uFogDist;
    float fog = clamp(1.0 - exp(-t * t), 0.0, 1.0);
    oColor = vec4(mix(col, uSky, fog), 1.0);
})";
}  // namespace

bool CharacterRenderer::init() {
    prog_ = compileProgram(CHAR_VS, CHAR_FS, "personages");
    uVP_ = glGetUniformLocation(prog_, "uVP");
    uModel_ = glGetUniformLocation(prog_, "uModel");
    uBones_ = glGetUniformLocation(prog_, "uBones");
    uSun_ = glGetUniformLocation(prog_, "uSun");
    uSunCol_ = glGetUniformLocation(prog_, "uSunCol");
    uAmb_ = glGetUniformLocation(prog_, "uAmb");
    uSky_ = glGetUniformLocation(prog_, "uSky");
    uCam_ = glGetUniformLocation(prog_, "uCam");
    uFogDist_ = glGetUniformLocation(prog_, "uFogDist");
    uInterior_ = glGetUniformLocation(prog_, "uInterior");
    // rekwisieten in het frame van de hand (gewricht pols, arm wijst ongeveer naar -y)
    {
        CB cb;
        boxPart(cb, Vec3(0.012f, -0.085f, 0.012f), Vec3(0.0045f, 0.068f, 0.034f), Vec3(0.05f, 0.05f, 0.06f), 0, 6.0f);
        boxPart(cb, Vec3(0.017f, -0.085f, 0.012f), Vec3(0.0015f, 0.06f, 0.029f), Vec3(0.20f, 0.32f, 0.48f), 0, 6.0f);
        uploadCB(props_[0], cb);
    }
    {
        CB cb;
        wire(cb, {Vec3(0.01f, -0.085f, -0.01f), Vec3(0.01f, -0.085f, 0.03f)}, 0.008f, Vec3(0.75f, 0.75f, 0.78f), 0, 8);
        uploadCB(props_[1], cb);
    }
    {
        CB cb;
        boxPart(cb, Vec3(0.012f, -0.09f, 0.012f), Vec3(0.012f, 0.075f, 0.035f), Vec3(0.12f, 0.12f, 0.13f), 0, 5.0f);
        boxPart(cb, Vec3(0.025f, -0.075f, 0.012f), Vec3(0.002f, 0.03f, 0.026f), Vec3(0.30f, 0.55f, 0.40f), 0, 6.0f);
        uploadCB(props_[2], cb);
    }
    return prog_ != 0;
}

void CharacterRenderer::begin(const Mat4& vp, const World& world) {
    glUseProgram(prog_);
    glUniformMatrix4fv(uVP_, 1, GL_FALSE, vp.m);
    Vec3 sun = normalize(world.sunDir), cam = world.camPos();
    glUniform3f(uSun_, sun.x, sun.y, sun.z);
    glUniform3f(uSunCol_, world.sunColor.x, world.sunColor.y, world.sunColor.z);
    glUniform3f(uSky_, world.skyColor.x, world.skyColor.y, world.skyColor.z);
    glUniform3f(uCam_, cam.x, cam.y, cam.z);
    glUniform1f(uAmb_, world.ambient);
    glUniform1f(uFogDist_, world.fogDistance);
    glUniform1i(uInterior_, 0);
}

void CharacterRenderer::beginStudio(const Mat4& vp, Vec3 cam) {
    glUseProgram(prog_);
    glUniformMatrix4fv(uVP_, 1, GL_FALSE, vp.m);
    Vec3 sun = normalize(Vec3(0.45f, 0.8f, 0.55f));
    glUniform3f(uSun_, sun.x, sun.y, sun.z);
    glUniform3f(uSunCol_, 0.95f, 0.92f, 0.86f);
    glUniform3f(uSky_, 0.55f, 0.62f, 0.72f);
    glUniform3f(uCam_, cam.x, cam.y, cam.z);
    glUniform1f(uAmb_, 1.0f);
    glUniform1f(uFogDist_, 1e6f);
    glUniform1i(uInterior_, 0);
}

void CharacterRenderer::draw(const CharModel& m, const Mat4& world, const CharPose& p, bool interior) {
    if (!m.count) return;
    glUniform1i(uInterior_, interior ? 1 : 0);
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, world.m);
    glUniformMatrix4fv(uBones_, BONE_COUNT, GL_FALSE, p.skin[0].m);
    glBindVertexArray(m.vao);
    glDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, nullptr);
}

void CharacterRenderer::drawProp(int prop, const Mat4& world, const CharPose& p, int bone, bool interior) {
    if (prop < 0 || prop > 2 || !props_[prop].count) return;
    Mat4 id = Mat4::identity();
    glUniform1i(uInterior_, interior ? 1 : 0);
    Mat4 m = world * p.global[bone];
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, m.m);
    glUniformMatrix4fv(uBones_, 1, GL_FALSE, id.m);
    glBindVertexArray(props_[prop].vao);
    glDrawElements(GL_TRIANGLES, props_[prop].count, GL_UNSIGNED_INT, nullptr);
}

void CharacterRenderer::end() { glBindVertexArray(0); }
