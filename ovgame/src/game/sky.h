#pragma once
// Zonnestand en daglicht voor Eindhoven (51,44 N, 5,48 O) op een gegeven datum en klokentijd (CET/CEST).
// Echte nacht (lantaarns, verlichte ramen) komt later: het wordt nooit donkerder dan diepe schemering.
#include <windows.h>
#include <cmath>
#include "../engine/math.h"

struct SkyState {
    Vec3 sunDir;     // richting naar de zon (game-assen: x oost, z zuid, y omhoog)
    Vec3 sunColor;   // kleur x sterkte
    Vec3 sky;        // lucht/mist
    float ambient;   // omgevingslicht
    float elevation; // zonshoogte (graden)
};

inline float smooth01(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
}
inline Vec3 mix3(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

// dayOfYear 1..366, clock = seconden sinds middernacht (lokale tijd)
inline SkyState computeSky(int dayOfYear, double clock) {
    const double DEG = 3.14159265358979 / 180.0;
    const double lat = 51.44 * DEG, lon = 5.48;
    bool summer = dayOfYear >= 88 && dayOfYear < 300;  // zomertijd (ongeveer laatste zondag maart .. oktober)
    double utc = clock / 3600.0 - (summer ? 2.0 : 1.0);
    double g = 2 * 3.14159265358979 / 365.0 * (dayOfYear - 1 + (utc - 12) / 24);
    double decl = 0.006918 - 0.399912 * std::cos(g) + 0.070257 * std::sin(g) - 0.006758 * std::cos(2 * g) + 0.000907 * std::sin(2 * g);
    double eqt = 229.18 * (0.000075 + 0.001868 * std::cos(g) - 0.032077 * std::sin(g) - 0.014615 * std::cos(2 * g) - 0.040849 * std::sin(2 * g));
    double solar = utc + lon / 15.0 + eqt / 60.0;
    double H = (solar - 12.0) * 15.0 * DEG;
    double sinEl = std::sin(lat) * std::sin(decl) + std::cos(lat) * std::cos(decl) * std::cos(H);
    double el = std::asin(sinEl);
    double az = std::atan2(std::sin(H), std::cos(H) * std::sin(lat) - std::tan(decl) * std::cos(lat)) + 3.14159265358979;  // vanaf noord, met de klok mee
    SkyState s;
    float ce = (float)std::cos(el);
    Vec3 dir((float)(ce * std::sin(az)), (float)sinEl, (float)(-ce * std::cos(az)));
    s.elevation = (float)(el / DEG);
    float se = (float)sinEl;
    float day = smooth01(-0.03f, 0.30f, se);                // zonlicht
    float warm = 1.0f - smooth01(0.02f, 0.35f, se);          // laagstaande zon: oranje
    Vec3 noon(1.0f, 0.96f, 0.88f), low(1.0f, 0.62f, 0.36f);
    s.sunColor = mix3(noon, low, warm) * day;
    // licht van onder de horizon: richting omhoog houden zodat de belichting niet omklapt
    if (dir.y < 0.05f) { dir.y = 0.05f; }
    s.sunDir = normalize(dir);
    Vec3 skyDay(0.66f, 0.76f, 0.86f), skySet(0.86f, 0.64f, 0.50f), skyDusk(0.22f, 0.27f, 0.40f), skyNight(0.10f, 0.12f, 0.20f);
    float twilight = smooth01(-0.18f, 0.0f, se);   // -10 .. 0 graden
    Vec3 sky = mix3(skyNight, skyDusk, twilight);
    sky = mix3(sky, skySet, smooth01(-0.04f, 0.04f, se));
    sky = mix3(sky, skyDay, smooth01(0.05f, 0.35f, se));
    s.sky = sky;
    s.ambient = 0.30f + 0.70f * smooth01(-0.15f, 0.25f, se);
    return s;
}

inline int dayOfYearToday() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    static const int cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int d = cum[st.wMonth - 1] + st.wDay;
    bool leap = (st.wYear % 4 == 0 && st.wYear % 100 != 0) || st.wYear % 400 == 0;
    if (leap && st.wMonth > 2) d++;
    return d;
}
