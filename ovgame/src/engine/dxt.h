#pragma once
// Eenvoudige DXT1 (BC1) compressor voor luchtfoto's: per blok van 4x4 pixels twee eindkleuren (begrenzend kader,
// iets naar binnen getrokken) en per pixel de dichtstbijzijnde van de 4 tussenkleuren.
#include <cstdint>
#include <cstring>
#include <vector>

namespace Dxt {

inline uint16_t to565(int r, int g, int b) { return (uint16_t)(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255)); }
inline void from565(uint16_t c, int& r, int& g, int& b) {
    r = ((c >> 11) & 31) * 255 / 31;
    g = ((c >> 5) & 63) * 255 / 63;
    b = (c & 31) * 255 / 31;
}

// px: 16 pixels RGB (48 bytes)
inline void block(const uint8_t* px, uint8_t* out) {
    int mn[3] = {255, 255, 255}, mx[3] = {0, 0, 0};
    for (int i = 0; i < 16; i++)
        for (int c = 0; c < 3; c++) {
            int v = px[i * 3 + c];
            if (v < mn[c]) mn[c] = v;
            if (v > mx[c]) mx[c] = v;
        }
    for (int c = 0; c < 3; c++) {
        int inset = (mx[c] - mn[c]) / 16;
        mn[c] += inset;
        mx[c] -= inset;
    }
    uint16_t c0 = to565(mx[0], mx[1], mx[2]), c1 = to565(mn[0], mn[1], mn[2]);
    uint32_t idx = 0;
    if (c0 < c1) { uint16_t t = c0; c0 = c1; c1 = t; }
    if (c0 != c1) {
        int pal[4][3];
        from565(c0, pal[0][0], pal[0][1], pal[0][2]);
        from565(c1, pal[1][0], pal[1][1], pal[1][2]);
        for (int c = 0; c < 3; c++) {
            pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
            pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
        }
        for (int i = 0; i < 16; i++) {
            int best = 0, bd = 1 << 30;
            for (int k = 0; k < 4; k++) {
                int dr = px[i * 3] - pal[k][0], dg = px[i * 3 + 1] - pal[k][1], db = px[i * 3 + 2] - pal[k][2];
                int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
                if (d < bd) { bd = d; best = k; }
            }
            idx |= (uint32_t)best << (i * 2);
        }
    }
    out[0] = c0 & 255; out[1] = c0 >> 8; out[2] = c1 & 255; out[3] = c1 >> 8;
    memcpy(out + 4, &idx, 4);
}

// Hele afbeelding (RGB, w x h). Randen worden aangevuld door de laatste pixel te herhalen.
inline std::vector<uint8_t> compress(const uint8_t* img, int w, int h) {
    int bw = (w + 3) / 4, bh = (h + 3) / 4;
    std::vector<uint8_t> out((size_t)bw * bh * 8);
    uint8_t px[48];
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++) {
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++) {
                    int sx = bx * 4 + x, sy = by * 4 + y;
                    if (sx >= w) sx = w - 1;
                    if (sy >= h) sy = h - 1;
                    memcpy(px + (y * 4 + x) * 3, img + ((size_t)sy * w + sx) * 3, 3);
                }
            block(px, &out[((size_t)by * bw + bx) * 8]);
        }
    return out;
}

}  // namespace Dxt
