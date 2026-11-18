#pragma once
// Hoofdmenu (startscherm) en pauzemenu.
//   Hoofdmenu: Vrij spelen / Dienst rijden / Personage / Opdrachten (komt later) / Multiplayer / Opties / Afsluiten
//   Vrij spelen: kies het startpunt en de tijd (de dienstregeling en het daglicht volgen die tijd)
//   Dienst rijden: kies een busrit of treindienst uit de dienstregeling en rijd hem zelf
//   Personage: eigen personages maken (naam, leeftijd, lichaam, haar, kleding, ...), een ervan is actief
#include <string>
#include <vector>
#include "../engine/platform.h"
#include "../engine/text.h"
#include "character.h"

struct DutyItem {
    std::string label;  // regel in de lijst
    std::string info;   // uitleg rechts
    int id = -1;        // bus: ritindex, trein: dienst-id
    int stop = 0;       // trein: index van de vertrekhalte
    double time = 0;    // vertrektijd
};

class Menu {
public:
    enum Screen { MAIN, NEW_GAME, PAUSE, OPTIONS, DUTY, CHARACTER, MULTI };
    enum Action { NONE, START, RESUME, QUIT, TO_MAIN, SETTINGS, START_DUTY, SAVE_CHARS, CONNECT, DISCONNECT };
    Screen screen = MAIN;
    std::vector<std::string> starts;  // namen van de startpunten (index 0 = startpunt 1)
    int startChoice = 0;
    int lastStartChoice = -1;         // keuze waarvoor de lijst laatst in beeld is geschoven
    int startScroll = 0;              // eerste zichtbare regel in de lijst met startpunten
    int minutes = 12 * 60;            // gekozen tijd (minuten na middernacht)
    bool editing = false;             // tijd wordt met de toetsen ingetypt
    std::string typed;                // ingetypte cijfers (max. 4: uumm)
    void acceptTyped();
    bool gameRunning = false;         // er is een spel bezig (Verder spelen tonen)
    // opties
    int windowMode = 0;               // Platform::WindowMode: 0 venster, 1 randloos volledig scherm, 2 volledig scherm
    bool vsync = true;
    int realism = 1;                  // treinrijden: 0 simpel, 1 middel, 2 zo echt mogelijk
    // dienst rijden
    int dutyKind = 0;                 // 0 bus, 1 trein
    int dutyLine = 0;                 // index in busLines
    std::vector<std::string> busLines;
    std::vector<DutyItem> dutyItems;  // door main gevuld als dutyDirty
    int dutySel = -1, dutyScroll = 0;
    bool dutyDirty = true;
    // personages
    std::vector<Appearance> chars;
    int activeChar = 0;
    Appearance edit;                  // wordt bewerkt (voorbeeld rechts)
    int editIndex = -1;               // -1 = nieuw
    bool previewDirty = true;
    bool nameEditing = false;
    float previewX = 0, previewY = 0, previewW = 0, previewH = 0;  // waar main het 3D-voorbeeld tekent
    bool showPreview = false;
    float previewYaw = 0.4f;
    // multiplayer
    std::string mpUrl, mpPassword;
    std::string mpStatus;             // melding (fout of "verbonden")
    bool mpConnected = false;         // door main bijgehouden
    std::string mpServerName;
    int mpField = 0;                  // 0 geen, 1 adres, 2 wachtwoord (wordt getypt)

    // Tekent het menu en verwerkt de muis. Geeft de gekozen actie terug.
    Action draw(TextRenderer& text, Input& in, int w, int h);

private:
    bool button(TextRenderer& text, Input& in, float x, float y, float bw, float bh, const std::string& label, bool enabled = true,
                bool selected = false, float size = 0);
    void startList(TextRenderer& text, Input& in, float x, float y, float bw, float yMax, float sh, float fs);
    bool textField(TextRenderer& text, Input& in, float x, float y, float w, float h, std::string& value, int id, bool mask, size_t maxLen);
    void timePicker(TextRenderer& text, Input& in, float tx, float ty, bool& enterUsed, bool& escUsed, bool compact);
    float ui_ = 1;
    Screen back_ = MAIN;  // waar "Terug" in de opties heen gaat
};
