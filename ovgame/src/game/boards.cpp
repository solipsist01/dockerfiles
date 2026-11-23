// Elektronische NS-borden en binnenschermen: zie boards.h
#include "boards.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "../engine/meshbuilder.h"
#include "rollingstock.h"
#include "traffic.h"
#include "train.h"
#include "world.h"

namespace {
// NS-huisstijl voor de elektronische borden: donkerblauw met witte tekst, geel voor afwijkingen
const Color4 BG{0.00f, 0.12f, 0.38f, 1}, BG2{0.00f, 0.08f, 0.27f, 1}, ROWALT{0.02f, 0.16f, 0.45f, 1};
const Color4 WHITE{1, 1, 1, 1}, GREY{0.70f, 0.77f, 0.90f, 1}, YEL{1.0f, 0.79f, 0.09f, 1}, NSBLUE{0.00f, 0.19f, 0.51f, 1};
const Color4 SQUARE{0.96f, 0.97f, 1.0f, 1};
const int TRACK_H = 160;  // perronbord: bovenste 160 px van het vak

std::string hhmm(double t) {
    t = std::fmod(std::max(0.0, t), 86400.0);
    char b[8];
    snprintf(b, sizeof b, "%02d:%02d", (int)(t / 3600), (int)std::fmod(t / 60, 60));
    return b;
}

// tekst die in maxW past: eerst kleiner (tot minSize), dan afkappen met "..."
float fit(TextRenderer& t, float x, float y, float maxW, std::string s, float size, float minSize, Color4 c) {
    while (size > minSize && t.measure(s, size) > maxW) size -= 1.0f;
    if (t.measure(s, size) > maxW) {
        while (s.size() > 1 && t.measure(s + "...", size) > maxW) s.pop_back();
        s += "...";
    }
    return t.text(x, y, s, size, c);
}

std::string joinVia(const std::vector<std::string>& v, size_t n) {
    std::string s;
    for (size_t i = 0; i < v.size() && i < n; i++) s += (i ? ", " : "") + v[i];
    return s;
}

// spoornummer in een wit vierkant (zoals op de NS-borden)
void trackSquare(TextRenderer& t, float x, float y, float sz, int track) {
    t.rect(x, y, sz, sz, SQUARE);
    std::string n = std::to_string(track);
    float fs = sz * 0.72f;
    float w = t.measure(n, fs);
    t.text(x + (sz - w) / 2, y + sz * 0.10f, n, fs, NSBLUE);
}

// treinsamenstelling: een blokje per bak
void consist(TextRenderer& t, float x, float y, int cars, Color4 c) {
    for (int k = 0; k < cars; k++) t.rect(x + k * 13.0f, y, 11.0f, 7.0f, c);
}
}  // namespace

bool Boards::init() {
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TEX, TEX, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_, 0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glClearColor(BG.r, BG.g, BG.b, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

void Boards::slotRect(int slot, float& u0, float& v0, float& u1, float& v1, bool track) const {
    int col = slot % COLS, row = slot / COLS;
    u0 = (float)(col * SW) / TEX;
    u1 = (float)((col + 1) * SW) / TEX;
    v1 = (float)(TEX - row * SH) / TEX;                       // boven (tekst y = 0)
    v0 = v1 - (float)(track ? TRACK_H : SH) / TEX;            // onder
}

bool Boards::rideScreen(float& u0, float& v0, float& u1, float& v1) const {
    if (rideSlot_ < 0) return false;
    slotRect(rideSlot_, u0, v0, u1, v1, false);
    return true;
}

// ---------------- perronbord ----------------
void Boards::renderTrackBoard(TextRenderer& t, const Traffic& tr, const std::string& station, int track, double clock) {
    std::vector<Departure> all = tr.departures(station, 12, 3 * 3600);
    std::vector<Departure> d;
    for (const Departure& x : all) if (x.track == track) d.push_back(x);
    const float W = SW, H = TRACK_H;
    t.rect(0, 0, W, H, BG);
    // klok en spoor rechtsboven
    trackSquare(t, W - 62, 10, 52, track);
    t.text(W - 62 - t.measure(hhmm(clock), 20) - 10, 12, hhmm(clock), 20, GREY);
    if (d.empty()) {
        t.text(20, 52, "Geen treinen", 34, GREY);
        return;
    }
    const Departure& a = d[0];
    // vertrektijd groot, vertraging eronder in geel
    t.text(16, 8, hhmm(a.time), 40, WHITE);
    int late = (int)(a.delay / 60);
    if (late >= 1) t.text(18, 54, "+" + std::to_string(late), 28, YEL);
    // treinsoort en nummer, bestemming groot, "via"
    t.text(132, 10, a.product + " " + a.number, 20, GREY);
    fit(t, 130, 32, W - 132 - 80, a.dest, 46, 26, WHITE);
    if (!a.via.empty()) fit(t, 132, 86, W - 150, "via " + joinVia(a.via, 3), 19, 15, GREY);
    // onderbalk: daarna + samenstelling
    t.rect(0, H - 34, W, 34, BG2);
    if (d.size() > 1) {
        const Departure& b = d[1];
        fit(t, 14, H - 30, W * 0.62f, "Daarna  " + hhmm(b.time) + "  " + b.product + " " + b.dest, 18, 14, GREY);
    }
    if (a.cars > 0) {
        std::string c = a.stock + "  " + std::to_string(a.cars) + " bakken";
        float cw = t.measure(c, 16);
        t.text(W - cw - 14, H - 31, c, 16, WHITE);
        consist(t, W - cw - 24 - a.cars * 13.0f, H - 21, a.cars, YEL);
    }
}

// ---------------- vertrekstaat ----------------
void Boards::renderOverview(TextRenderer& t, const Traffic& tr, const std::string& station, double clock) {
    std::vector<Departure> d = tr.departures(station, 8, 3 * 3600);
    const float W = SW, H = SH;
    t.rect(0, 0, W, H, BG);
    t.rect(0, 0, W, 30, BG2);
    t.text(10, 5, "Vertrek", 17, GREY);
    t.text(110, 5, "Naar", 17, GREY);
    t.text(W - 132, 5, "Spoor", 17, GREY);
    t.text(W - 70, 5, hhmm(clock), 18, WHITE);
    const float rowH = (H - 32) / 8.0f;
    for (int i = 0; i < 8; i++) {
        float y = 32 + i * rowH;
        if (i % 2) t.rect(0, y, W, rowH, ROWALT);
        if (i >= (int)d.size()) continue;
        const Departure& a = d[i];
        t.text(10, y + 3, hhmm(a.time), 20, WHITE);
        int late = (int)(a.delay / 60);
        if (late >= 1) t.text(66, y + 5, "+" + std::to_string(late), 16, YEL);
        fit(t, 110, y + 2, W - 260, a.dest, 21, 14, WHITE);
        t.text(W - 200, y + 6, a.product == "Intercity" ? "IC" : "SPR", 14, GREY);
        trackSquare(t, W - 124, y + 2, rowH - 4, a.track);
        if (a.cars > 0) t.text(W - 92, y + 6, std::to_string(a.cars) + "b", 13, GREY);
    }
    if (d.empty()) t.text(110, 80, "Geen treinen", 28, GREY);
}

// ---------------- binnenscherm (trein) ----------------
void Boards::renderRide(TextRenderer& t, const Train& ride, double clock) {
    const Service& s = ride.service();
    const float W = SW, H = SH;
    t.rect(0, 0, W, H, BG);
    t.rect(0, 0, W, 34, BG2);
    fit(t, 10, 6, W - 90, s.product() + " naar " + s.dest, 20, 14, WHITE);
    t.text(W - 70, 7, hhmm(clock), 19, GREY);
    // staat hij bij een halte: die; anders de volgende halte waar hij stopt
    int at = ride.lastStopIndex(), next = -1;
    bool here = ride.stopped() && at >= 0 && at < (int)s.stops.size();
    if (here) next = at;
    else
        for (int k = at + 1; k < (int)s.stops.size(); k++)
            if (s.stops[k].stop) { next = k; break; }
    if (next < 0) {
        t.text(14, 60, "Eindbestemming", 30, WHITE);
        return;
    }
    t.text(14, 42, here ? "Station" : "Volgende station", 18, GREY);
    fit(t, 14, 62, W - 28, s.stops[next].station, 44, 24, WHITE);
    if (s.stops[next].track > 0) t.text(14, 112, "Spoor " + std::to_string(s.stops[next].track), 18, YEL);
    // haltes daarna, met tijden
    float y = 140;
    int shown = 0;
    for (int k = next + 1; k < (int)s.stops.size() && shown < 4; k++) {
        if (!s.stops[k].stop) continue;
        t.rect(22, y + 8, 6, 6, WHITE);
        t.text(40, y, hhmm(s.stops[k].arr), 18, GREY);
        fit(t, 100, y, W - 120, s.stops[k].station, 19, 14, WHITE);
        y += 26;
        shown++;
    }
}

void Boards::update(const std::vector<BoardSpot>& spots, const Traffic& traffic, Vec3 cam, double clock, TextRenderer& text,
                    const Train* ride, float dt) {
    timer_ -= dt;
    if (timer_ > 0 && hasMesh_) return;
    timer_ = 1.0f;
    // borden dichtbij: inhoud per (station, spoor, soort)
    struct Want { std::string key, station; int track; bool overview; float dist; };
    std::vector<Want> want;
    auto addWant = [&](const std::string& st, int track, bool ov, float dist) {
        std::string key = st + "|" + std::to_string(track) + (ov ? "|o" : "|t");
        for (Want& w : want) if (w.key == key) { w.dist = std::min(w.dist, dist); return; }
        want.push_back({key, st, track, ov, dist});
    };
    for (const BoardSpot& b : spots) {
        float d = std::hypot(b.c.x - cam.x, b.c.z - cam.z);
        if (d > 140.0f) continue;
        if (b.overview) addWant(b.station, 0, true, d);
        else {
            addWant(b.station, b.trackA, false, d);
            if (b.trackB > 0) addWant(b.station, b.trackB, false, d);
        }
    }
    std::sort(want.begin(), want.end(), [](const Want& a, const Want& b) { return a.dist < b.dist; });
    int maxBoards = SLOTS - (ride ? 1 : 0);
    if ((int)want.size() > maxBoards) want.resize(maxBoards);
    // vakken: bestaande toewijzing houden, vrije vakken uitdelen
    std::map<std::string, int> keep;
    std::vector<bool> used(SLOTS, false);
    for (const Want& w : want) {
        auto it = slotOf_.find(w.key);
        if (it != slotOf_.end()) { keep[w.key] = it->second; used[it->second] = true; }
    }
    for (const Want& w : want) {
        if (keep.count(w.key)) continue;
        for (int s = 0; s < SLOTS; s++)
            if (!used[s]) { keep[w.key] = s; used[s] = true; break; }
    }
    slotOf_ = keep;
    rideSlot_ = -1;
    if (ride)
        for (int s = 0; s < SLOTS; s++)
            if (!used[s]) { rideSlot_ = s; used[s] = true; break; }

    // ---- tekenen in de textuur ----
    GLint prevFbo = 0, vp[4];
    glGetIntegerv(0x8CA6 /* GL_FRAMEBUFFER_BINDING */, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, vp);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glEnable(GL_SCISSOR_TEST);
    auto slotView = [&](int slot, int h) {
        int col = slot % COLS, row = slot / COLS;
        int x = col * SW, y = TEX - row * SH - h;
        glViewport(x, y, SW, h);
        glScissor(x, y, SW, h);
        glClearColor(BG.r, BG.g, BG.b, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    };
    for (const Want& w : want) {
        int slot = slotOf_[w.key];
        int h = w.overview ? SH : TRACK_H;
        slotView(slot, h);
        text.begin(SW, h);
        if (w.overview) renderOverview(text, traffic, w.station, clock);
        else renderTrackBoard(text, traffic, w.station, w.track, clock);
        text.end();
    }
    if (ride && rideSlot_ >= 0) {
        slotView(rideSlot_, SH);
        text.begin(SW, SH);
        renderRide(text, *ride, clock);
        text.end();
    }
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);

    // ---- schermvlakken in de wereld ----
    MeshBuilder mb;
    for (const BoardSpot& b : spots) {
        if (std::hypot(b.c.x - cam.x, b.c.z - cam.z) > 140.0f) continue;
        for (int face = 0; face < 2; face++) {
            int track = face == 0 || b.trackB == 0 ? b.trackA : b.trackB;
            std::string key = b.station + "|" + std::to_string(b.overview ? 0 : track) + (b.overview ? "|o" : "|t");
            auto it = slotOf_.find(key);
            if (it == slotOf_.end()) continue;
            float u0, v0, u1, v1;
            slotRect(it->second, u0, v0, u1, v1, !b.overview);
            Vec3 n = face == 0 ? b.normal : b.normal * -1.0f;
            Vec3 r = normalize(cross(Vec3(0, 1, 0), n));  // rechts voor wie naar het bord kijkt
            Vec3 c = b.c + n * 0.09f;  // ruim voor de behuizing (±0,06 m): anders z-fighting op afstand
            Vec3 hx = r * (b.w / 2), hy = Vec3(0, b.h / 2, 0);
            uint32_t i0 = mb.vert(c - hx - hy, n, Vec3(u0, v0, 0), EX_BOARD), i1 = mb.vert(c + hx - hy, n, Vec3(u1, v0, 0), EX_BOARD);
            uint32_t i2 = mb.vert(c + hx + hy, n, Vec3(u1, v1, 0), EX_BOARD), i3 = mb.vert(c - hx + hy, n, Vec3(u0, v1, 0), EX_BOARD);
            mb.i.insert(mb.i.end(), {i0, i1, i2, i0, i2, i3});
        }
    }
    if (hasMesh_) destroyMesh(mesh_);
    hasMesh_ = !mb.i.empty();
    if (hasMesh_) mesh_ = mb.upload();
}

void Boards::dump(const char* path) const {
    std::vector<unsigned char> px((size_t)TEX * TEX * 4);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6 %d %d 255\n", TEX, TEX);
    for (int y = TEX - 1; y >= 0; y--)
        for (int x = 0; x < TEX; x++) fwrite(&px[((size_t)y * TEX + x) * 4], 1, 3, f);
    fclose(f);
}

void Boards::drawRide(World& world, const Train& ride) {
    float u0, v0, u1, v1;
    if (hasRide_) { destroyMesh(rideMesh_); hasRide_ = false; }
    if (!rideScreen(u0, v0, u1, v1)) return;
    const RollingStock& rs = ride.stock();
    MeshBuilder mb;
    const float W = 0.50f, H = 0.25f;
    for (int car = 0; car < ride.carCount(); car++) {
        CarPose p = ride.carPose(car);
        Vec3 fw(std::sin(p.h), 0, std::cos(p.h)), base(p.x, p.y + rs.screenY, p.z);
        for (float dz : rs.doorZ[car])
            for (int face = -1; face <= 1; face += 2) {
                Vec3 n = fw * (float)face, r = normalize(cross(Vec3(0, 1, 0), n));
                Vec3 c = base + fw * dz + n * 0.03f, hx = r * (W / 2), hy(0, H / 2, 0);
                uint32_t i0 = mb.vert(c - hx - hy, n, Vec3(u0, v0, 0), EX_BOARD), i1 = mb.vert(c + hx - hy, n, Vec3(u1, v0, 0), EX_BOARD);
                uint32_t i2 = mb.vert(c + hx + hy, n, Vec3(u1, v1, 0), EX_BOARD), i3 = mb.vert(c - hx + hy, n, Vec3(u0, v1, 0), EX_BOARD);
                mb.i.insert(mb.i.end(), {i0, i1, i2, i0, i2, i3});
            }
    }
    rideMesh_ = mb.upload();
    hasRide_ = true;
    world.setMode(MODE_OBJECT);
    world.setModel(Mat4::identity());
    drawMesh(rideMesh_);
}

void Boards::draw(World& world) const {
    if (!hasMesh_) return;
    world.setMode(MODE_OBJECT);
    world.setModel(Mat4::identity());
    drawMesh(mesh_);
}
