#pragma once
// Platformlaag: Win32-venster, OpenGL-context, toetsenbord/muis en tijd.
#include <string>

struct Input {
    bool down[256] = {};     // toets ingedrukt (virtual key codes)
    bool pressed[256] = {};  // toets deze frame ingedrukt
    float mouseDX = 0, mouseDY = 0;
    float wheel = 0;
    bool captured = false;   // muis gevangen (first-person besturing)
    // muis in het venster (pixels, linksboven = 0,0) voor menu's en de kaart
    float mouseX = 0, mouseY = 0;
    bool lDown = false, lClick = false, rClick = false, lRelease = false;
    bool autoCapture = true; // klikken in het venster vangt de muis (uit in menu's en op de kaart)
    bool pad = false;        // laatst gebruikt: controller (XInput: Xbox-controller, Steam Deck) i.p.v. muis/toetsenbord
    std::string chars;       // deze frame getypte tekens (Latin-1; '\b' = backspace), voor tekstvelden
};

namespace Platform {
bool init(const char* title, int width, int height);
bool pump();                 // verwerkt berichten; false = afsluiten
void swap();
Input& input();
int width();
int height();
double time();
void setCapture(bool capture);
void setTitle(const char* title);
// Venstermodus: WINDOWED = gewoon venster, BORDERLESS = randloos venster over het hele scherm (snel wisselen met
// Alt+Tab), FULLSCREEN = exclusief volledig scherm (eigen beeldschermmodus, op de oorspronkelijke resolutie)
enum WindowMode { WINDOWED = 0, BORDERLESS = 1, FULLSCREEN = 2 };
void setWindowMode(int mode);
int windowMode();
void setVSync(bool on);
void showError(const char* msg);
}
