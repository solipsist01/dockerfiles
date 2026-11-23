#pragma once
// Elektronische NS-borden (PIDS) op de stations, en de binnenschermen in de trein waar de speler in zit.
//
// Alle bordinhoud wordt in één dynamische textuur getekend (2048 x 2048, vakken van 512 x 256 px) met de gewone
// tekstrenderer in een offscreen-framebuffer. De borden in de wereld tonen hun vak (materiaal EX_BOARD, licht zelf op).
// Alleen de borden dicht bij de camera krijgen een vak; ze worden elke seconde opnieuw getekend.
//
//  - perronbord (per spoor, boven het perron): vertrektijd + vertraging, treinsoort, bestemming groot, "via", wat er daarna
//    vertrekt, materieel met het aantal bakken, klok en spoornummer.
//  - vertrekstaat (een per station): de eerstvolgende acht treinen (tijd, vertraging, bestemming, spoor, trein).
//  - binnenscherm (in de trein): volgende station groot, de haltes daarna met tijden, eindbestemming.
#include <map>
#include <string>
#include <vector>
#include "../engine/gl.h"
#include "../engine/text.h"
#include "station.h"

class Traffic;
class Train;
class World;

class Boards {
public:
    bool init();  // textuur + framebuffer
    GLuint texture() const { return tex_; }
    // elke frame (vóór de scene): vakken toewijzen aan de borden dichtbij, elke seconde opnieuw tekenen, mesh bijwerken
    void update(const std::vector<BoardSpot>& spots, const Traffic& traffic, Vec3 cam, double clock, TextRenderer& text,
                const Train* ride, float dt);
    void draw(World& world) const;  // de schermvlakken (MODE_OBJECT)
    void drawRide(World& world, const Train& ride);  // binnenschermen boven de deuren van de trein van de speler
    // binnenscherm van de trein waar de speler in zit: uv-rechthoek van het vak (false als er geen is)
    bool rideScreen(float& u0, float& v0, float& u1, float& v1) const;
    void dump(const char* path) const;  // test: de textuur als PPM-plaatje wegschrijven

    static constexpr int TEX = 2048, SW = 512, SH = 256, COLS = TEX / SW, ROWS = TEX / SH, SLOTS = COLS * ROWS;

private:
    GLuint tex_ = 0, fbo_ = 0;
    Mesh mesh_, rideMesh_;
    bool hasMesh_ = false, hasRide_ = false;
    float timer_ = 0;
    std::map<std::string, int> slotOf_;   // inhoud (station|spoor|soort) -> vak
    int rideSlot_ = -1;
    void slotRect(int slot, float& u0, float& v0, float& u1, float& v1, bool track) const;
    void renderTrackBoard(TextRenderer& t, const Traffic& tr, const std::string& station, int track, double clock);
    void renderOverview(TextRenderer& t, const Traffic& tr, const std::string& station, double clock);
    void renderRide(TextRenderer& t, const Train& ride, double clock);
};
