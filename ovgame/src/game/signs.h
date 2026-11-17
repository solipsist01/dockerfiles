#pragma once
// Tekstatlas voor borden: elke tekst wordt met GDI op een eigen achtergrondkleur in een textuur gezet.
#include <map>
#include <string>
#include "../engine/meshbuilder.h"

struct SignRect {
    float u0, v0, u1, v1;
    float aspect;  // breedte / hoogte
};

class SignAtlas {
public:
    bool begin();
    // Tekst op achtergrond. Kleuren 0..1. height = letterhoogte in pixels in de atlas.
    SignRect add(const std::string& text, Vec3 bg, Vec3 fg, int height = 64, bool bold = true, float padX = 0.35f);
    GLuint finish();  // upload naar GPU

    // Voeg een bord toe aan een mesh: midden c, horizontale richting 'right' (eenheid), hoogte h.
    // Dubbelzijdig: de achterkant toont de tekst ook leesbaar.
    static void quad(MeshBuilder& mb, const SignRect& r, Vec3 c, Vec3 right, float h, bool doubleSided = true);

private:
    SignRect addNew(const std::string& text, Vec3 bg, Vec3 fg, int height, bool bold, float padX);
    void* dc_ = nullptr;
    void* bmp_ = nullptr;
    uint8_t* bits_ = nullptr;
    int x_ = 0, y_ = 0, rowH_ = 0;
    std::map<std::string, SignRect> cache_;  // identieke borden delen dezelfde plek in de atlas
};
