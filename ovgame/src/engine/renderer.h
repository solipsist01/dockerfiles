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
    void beginScene(Vec3 clearColor);  // tekent naar multisample-buffer
    void endScene();                   // kopieert naar het scherm
    bool saveScreenshot(const char* path);
    int width() const { return w_; }
    int height() const { return h_; }

private:
    GLuint fbo_ = 0, rbColor_ = 0, rbDepth_ = 0;
    int w_ = 0, h_ = 0;
    int samples_ = 4;
};
