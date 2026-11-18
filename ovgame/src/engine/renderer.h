#pragma once
#include "gl.h"
#include "math.h"
#include <cstdint>
#include <vector>

// Standaard vertex: positie, normaal, kleur en een extra waarde (materiaal-afhankelijk).
struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float r, g, b;
    float extra;
};

struct Mesh {
    GLuint vao = 0, vbo = 0, ibo = 0;
    GLsizei count = 0;
    Vec3 bmin, bmax;  // bounding box voor frustum culling
};

Mesh createMesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices);
void drawMesh(const Mesh& m);
void destroyMesh(Mesh& m);

GLuint compileProgram(const char* vs, const char* fs, const char* name);

// Zichtveld (frustum) om onzichtbare stukken wereld over te slaan.
struct Frustum {
    float p[6][4];
    void fromMatrix(const Mat4& vp);
    bool boxVisible(Vec3 mn, Vec3 mx) const;
};

class Renderer {
public:
    bool init();
    void resize(int w, int h);
    // kwaliteit: anti-aliasing (0 = uit, anders aantal samples) en resolutieschaal (0.5..1) van de 3D-wereld;
    // het menu en de HUD blijven op volle resolutie
    void setQuality(int samples, float scale);
    void beginScene(Vec3 clearColor);  // tekent naar de (multisample-)buffer op renderresolutie
    void endScene();                   // kopieert (geschaald) naar het scherm
    bool saveScreenshot(const char* path);
    int width() const { return w_; }
    int height() const { return h_; }

private:
    void rebuild();
    GLuint fbo_ = 0, rbColor_ = 0, rbDepth_ = 0;
    GLuint fboRes_ = 0, rbRes_ = 0;    // tussenbuffer: multisample oplossen voordat er geschaald wordt
    int w_ = 0, h_ = 0;                // scherm
    int rw_ = 0, rh_ = 0;              // renderresolutie
    int samples_ = 4;
    float scale_ = 1.0f;
};
