#include "busstation.h"
#include "../engine/meshbuilder.h"

namespace {
const Vec3 WHITE_STEEL(0.86f, 0.87f, 0.88f);
const Vec3 CABLE(0.15f, 0.15f, 0.16f);
const Vec3 SCREEN(0.03f, 0.03f, 0.035f);
const Vec3 HEADER_RED(0.75f, 0.05f, 0.05f);
const Vec3 TEXT_AMBER(1.0f, 0.55f, 0.1f);
constexpr float PLAIN = -2000.0f;  // geen ramen/dakfoto (shader-modus 1)
}  // namespace

void BusStation::build(const World& world) {
    if (world.bays.size() < 2) return;

    // ---------- buisconstructie boven de voorkant van de havens ----------
    MeshBuilder mb;
    std::vector<Vec3> front;  // punten boven de neus van elke haven
    for (const BusBay& b : world.bays) {
        float fx = std::sin(b.heading), fz = std::cos(b.heading);
        float d = b.length / 2 - 1.6f;
        float x = b.x + fx * d, z = b.z + fz * d;
        front.push_back(Vec3(x, world.heightAt(x, z), z));
    }
    const float PIPE_H = 4.9f;    // hoogte witte buis boven maaiveld
    const float MAST_H = 9.0f;    // hoogte portalen
    // buis verlengen aan beide uiteinden
    Vec3 dirStart = normalize(front[0] - front[1]), dirEnd = normalize(front.back() - front[front.size() - 2]);
    std::vector<Vec3> line;
    line.push_back(front[0] + dirStart * 4.0f);
    for (const Vec3& p : front) line.push_back(p);
    line.push_back(front.back() + dirEnd * 4.0f);
    float baseY = 0;
    for (const Vec3& p : line) baseY += p.y;
    baseY /= line.size();
    for (Vec3& p : line) p.y = baseY + PIPE_H;
    for (size_t k = 0; k + 1 < line.size(); k++) mb.tube(line[k], line[k + 1], 0.22f, 12, WHITE_STEEL, PLAIN);

    // steunpalen tussen de havens (op de eilanden) en aan de uiteinden
    std::vector<Vec3> supports;
    supports.push_back(line.front());
    for (size_t k = 0; k + 1 < front.size(); k++) supports.push_back((line[k + 1] + line[k + 2]) * 0.5f);
    supports.push_back(line.back());
    for (size_t k = 0; k < supports.size(); k++) {
        Vec3 top = supports[k];
        Vec3 bottom(top.x, world.heightAt(top.x, top.z) + 0.15f, top.z);
        if (k % 2 == 0) {
            mb.tube(bottom, top + Vec3(0, 0.2f, 0), 0.16f, 10, WHITE_STEEL, PLAIN);
        }
        // portaal (wit, twee benen + ligger) op elke tweede steun
        if (k % 2 == 0) {
            Vec3 along = normalize((k + 1 < supports.size() ? supports[k + 1] : supports[k]) - (k > 0 ? supports[k - 1] : supports[k]));
            Vec3 side = normalize(cross(Vec3(0, 1, 0), along)) * 1.2f;
            Vec3 l0(bottom.x - side.x - along.x * 0.8f, bottom.y, bottom.z - side.z - along.z * 0.8f);
            Vec3 l1(bottom.x + side.x - along.x * 0.8f, bottom.y, bottom.z + side.z - along.z * 0.8f);
            mb.box(l0 + Vec3(0, MAST_H / 2, 0), {0.28f, MAST_H, 0.28f}, WHITE_STEEL, PLAIN);
            mb.box(l1 + Vec3(0, MAST_H / 2, 0), {0.28f, MAST_H, 0.28f}, WHITE_STEEL, PLAIN);
            mb.tube(l0 + Vec3(0, MAST_H, 0), l1 + Vec3(0, MAST_H, 0), 0.16f, 8, WHITE_STEEL, PLAIN);
            mb.tube(l0 + Vec3(0, MAST_H - 1.2f, 0), l1 + Vec3(0, MAST_H - 1.2f, 0), 0.10f, 8, WHITE_STEEL, PLAIN);
        }
    }
    // draagkabel tussen de portalen, met hangers naar de buis
    std::vector<Vec3> tops;
    for (size_t k = 0; k < supports.size(); k += 2) tops.push_back(Vec3(supports[k].x, baseY + MAST_H - 0.2f, supports[k].z));
    for (size_t k = 0; k + 1 < tops.size(); k++) {
        Vec3 a = tops[k], b = tops[k + 1];
        const int SEG = 10;
        Vec3 prev = a;
        for (int s = 1; s <= SEG; s++) {
            float t = (float)s / SEG;
            Vec3 p = a + (b - a) * t;
            p.y -= 1.6f * 4 * t * (1 - t);  // doorhangen
            mb.tube(prev, p, 0.025f, 4, CABLE, PLAIN);
            if (s % 3 == 0) {
                Vec3 down(p.x, baseY + PIPE_H + 0.2f, p.z);
                mb.tube(p, down, 0.012f, 3, CABLE, PLAIN);
            }
            prev = p;
        }
    }

    // vertrekschermen per haven, hangend onder de buis, gericht naar de voorkant van de bussen
    for (size_t k = 0; k < world.bays.size(); k++) {
        const BusBay& b = world.bays[k];
        Vec3 p = line[k + 1];
        float h = b.heading;
        float fx = std::sin(h), fz = std::cos(h);
        Vec3 c = p + Vec3(fx * 0.35f, -0.85f, fz * 0.35f);
        mb.orientedBox(c, {2.1f, 0.85f, 0.14f}, h, SCREEN, PLAIN);
        mb.orientedBox(c + Vec3(fx * 0.075f, 0.36f, fz * 0.075f), {2.1f, 0.13f, 0.02f}, h, HEADER_RED, PLAIN);
        // "tekstregels" als oranje lijnen (echte tekst volgt later)
        for (int r = 0; r < 2; r++)
            mb.orientedBox(c + Vec3(fx * 0.075f, 0.12f - r * 0.22f, fz * 0.075f), {1.6f - r * 0.3f, 0.07f, 0.01f}, h, TEXT_AMBER, PLAIN);
        // ophangstangen
        Vec3 sideV = Vec3(std::cos(h), 0, -std::sin(h));
        mb.tube(c + sideV * 0.8f + Vec3(0, 0.42f, 0), p + sideV * 0.8f, 0.02f, 4, WHITE_STEEL, PLAIN);
        mb.tube(c - sideV * 0.8f + Vec3(0, 0.42f, 0), p - sideV * 0.8f, 0.02f, 4, WHITE_STEEL, PLAIN);
        // letterbord op de buis (havenaanduiding)
        mb.orientedBox(p + Vec3(fx * 0.24f, 0, fz * 0.24f), {0.45f, 0.35f, 0.04f}, h, WHITE_STEEL * 1.1f, PLAIN);
    }
    structure_ = mb.upload();
}

void BusStation::draw(World& world) const {
    world.setMode(MODE_OBJECT);
    world.setModel(Mat4::identity());
    drawMesh(structure_);
    glBindVertexArray(0);
}
