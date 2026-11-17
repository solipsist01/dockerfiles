#include "menu.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>

namespace {
const Color4 WHITE{1, 1, 1, 1}, YEL{1.0f, 0.80f, 0.10f, 1}, GREY{0.70f, 0.74f, 0.80f, 1}, DIM{0.45f, 0.48f, 0.54f, 1};
std::string hhmm(int m) {
    m = ((m % 1440) + 1440) % 1440;
    char b[16];
    snprintf(b, sizeof(b), "%02d:%02d", m / 60, m % 60);
    return b;
}
const char* REALISM[] = {"Simpel", "Middel", "Zo echt mogelijk"};
const char* REALISM_INFO[] = {
    "W gas, S rem (vasthouden). Deuren gaan vanzelf open als je langs het perron stopt. Je kunt niet door rood rijden.",
    "Rijremschakelaar in standen (W/S), ATB bewaakt de snelheid (bel: op tijd remmen), T = deuren vrijgeven, conducteur geeft vertreksein.",
    "Als middel, plus ATB-EG met snelheidscodes (spatie = bevestigen), waakzaamheid elke minuut, cabine activeren (K) en stroomafnemer (P), deuren links (Q) of rechts (R)."};
}  // namespace

bool Menu::button(TextRenderer& text, Input& in, float x, float y, float bw, float bh, const std::string& label, bool enabled, bool selected,
                  float size) {
    bool hover = enabled && in.mouseX > x && in.mouseX < x + bw && in.mouseY > y && in.mouseY < y + bh;
    Color4 bg = selected ? Color4{0.0f, 0.32f, 0.75f, 0.95f} : hover ? Color4{1.0f, 0.80f, 0.10f, 0.95f} : Color4{0.10f, 0.13f, 0.19f, 0.88f};
    text.rect(x, y, bw, bh, bg);
    if (selected) text.rect(x, y, 5 * ui_, bh, YEL);
    float fs = size > 0 ? size : 24 * ui_;
    Color4 fg = !enabled ? DIM : (hover && !selected) ? Color4{0.05f, 0.07f, 0.12f, 1} : WHITE;
    text.text(x + 14 * ui_, y + (bh - fs * 1.25f) / 2, label, fs, fg);
    return hover && in.lClick;
}

// tijd kiezen: klik of typ (uumm), knoppen voor stappen en vaste tijden
void Menu::timePicker(TextRenderer& text, Input& in, float tx, float ty, bool& enterUsed, bool& escUsed, bool compact) {
    float ui = ui_;
    float big = compact ? 44 * ui : 72 * ui;
    int before = minutes;
    int digit = -1;
    if (!nameEditing && mpField == 0)
        for (int k = 0; k <= 9; k++)
            if (in.pressed['0' + k] || in.pressed[VK_NUMPAD0 + k]) digit = k;
    float tbw = text.measure("00:00", big) + 24 * ui;
    bool overTime = in.mouseX > tx - 12 * ui && in.mouseX < tx + tbw && in.mouseY > ty && in.mouseY < ty + big * 1.25f;
    if (overTime && in.lClick && !editing) { editing = true; typed.clear(); }
    if (digit >= 0) {
        if (!editing) { editing = true; typed.clear(); }
        if (typed.size() < 4) typed += (char)('0' + digit);
    }
    if (editing && in.pressed[VK_BACK] && !typed.empty()) typed.pop_back();
    if (editing && in.pressed[VK_RETURN]) { acceptTyped(); enterUsed = true; }
    if (editing && typed.size() == 4 && digit >= 0) acceptTyped();
    if (editing && in.pressed[VK_ESCAPE]) { editing = false; escUsed = true; }
    if (editing) {
        std::string shown = typed;
        while (shown.size() < 4) shown += '_';
        shown = shown.substr(0, 2) + ":" + shown.substr(2, 2);
        text.rect(tx - 12 * ui, ty - 4 * ui, tbw, big * 1.28f, {0.15f, 0.25f, 0.45f, 0.9f});
        text.text(tx, ty, shown, big, {1.0f, 0.85f, 0.2f, 1});
        text.text(tx + tbw + 10 * ui, ty + big * 0.4f, "typ uu:mm, Enter = ok", 17 * ui, GREY);
    } else {
        if (overTime) text.rect(tx - 12 * ui, ty - 4 * ui, tbw, big * 1.28f, {1, 1, 1, 0.08f});
        text.text(tx, ty, hhmm(minutes), big, WHITE);
        text.text(tx + tbw + 10 * ui, ty + big * 0.4f, "klik of typ om zelf een tijd in te voeren", 17 * ui, GREY);
    }
    ty += big * 1.4f;
    float sb = compact ? 70 * ui : 80 * ui, gap = 8 * ui, bh = compact ? 36 * ui : 44 * ui;
    const struct { const char* l; int d; } steps[] = {{"- 1 uur", -60}, {"- 10 min", -10}, {"- 1 min", -1}, {"+ 1 min", 1}, {"+ 10 min", 10}, {"+ 1 uur", 60}};
    for (int k = 0; k < 6; k++)
        if (button(text, in, tx + k * (sb + gap), ty, sb, bh, steps[k].l, true, false, 15 * ui)) {
            minutes = ((minutes + steps[k].d) % 1440 + 1440) % 1440;
            editing = false;
        }
    ty += bh + 12 * ui;
    const struct { const char* l; int m; } pre[] = {{"Nu", -1}, {"07:30", 450}, {"12:00", 720}, {"17:30", 1050}, {"21:00", 1260}};
    for (int k = 0; k < 5; k++) {
        float bx = tx + k * (sb + gap);
        if (button(text, in, bx, ty, sb, bh, pre[k].l, true, false, 17 * ui)) {
            editing = false;
            if (pre[k].m >= 0) minutes = pre[k].m;
            else {
                SYSTEMTIME st;
                GetLocalTime(&st);
                minutes = st.wHour * 60 + st.wMinute;
            }
        }
    }
    if (minutes != before) dutyDirty = true;
}

// tekstveld: klik = typen; Enter of klik ernaast = klaar
bool Menu::textField(TextRenderer& text, Input& in, float x, float y, float w, float h, std::string& value, int id, bool mask, size_t maxLen) {
    bool over = in.mouseX > x && in.mouseX < x + w && in.mouseY > y && in.mouseY < y + h;
    if (in.lClick) {
        if (over) mpField = id;
        else if (mpField == id) mpField = 0;
    }
    bool active = mpField == id;
    bool changed = false;
    if (active) {
        if (in.pressed[VK_RETURN]) mpField = 0;
        for (char ch : in.chars) {
            if (ch == '\b') { if (!value.empty()) { value.pop_back(); changed = true; } }
            else if (ch == '\t') mpField = 0;
            else if ((unsigned char)ch >= 32 && value.size() < maxLen) { value += ch; changed = true; }
        }
        // plakken (Ctrl+V)
        if (in.down[VK_CONTROL] && in.pressed['V'] && OpenClipboard(nullptr)) {
            if (HANDLE hd = GetClipboardData(CF_TEXT)) {
                if (const char* c = (const char*)GlobalLock(hd)) {
                    for (; *c && value.size() < maxLen; c++) if ((unsigned char)*c >= 32) value += *c;
                    GlobalUnlock(hd);
                    changed = true;
                }
            }
            CloseClipboard();
        }
    }
    text.rect(x, y, w, h, active ? Color4{0.15f, 0.25f, 0.45f, 0.95f} : over ? Color4{0.16f, 0.19f, 0.26f, 0.95f} : Color4{0.10f, 0.13f, 0.19f, 0.9f});
    std::string shown = mask ? std::string(value.size(), '*') : value;
    float fs = 19 * ui_;
    while (!shown.empty() && text.measure(shown, fs) > w - 30 * ui_) shown.erase(0, 1);
    text.text(x + 10 * ui_, y + (h - fs * 1.25f) / 2, shown + (active && ((GetTickCount() / 500) & 1) ? "_" : ""), fs, WHITE);
    return changed;
}

Menu::Action Menu::draw(TextRenderer& text, Input& in, int w, int h) {
    ui_ = h / 900.0f;
    float ui = ui_;
    Action act = NONE;
    showPreview = false;
    text.begin(w, h);
    // donkere band links met titel
    float pw = (screen == CHARACTER ? 640 : screen == DUTY ? 740 : 560) * ui;
    text.rect(0, 0, pw, (float)h, {0.03f, 0.05f, 0.09f, 0.80f});
    text.rect(pw, 0, 4 * ui, (float)h, YEL);
    float x = 56 * ui, y = 50 * ui;
    text.text(x, y, "OV Game", 60 * ui, YEL);
    y += 76 * ui;
    text.text(x, y, "Eindhoven  -  Best  -  Boxtel  -  Vught  -  's-Hertogenbosch", 21 * ui, GREY);
    y += 64 * ui;
    float bw = pw - 2 * x, bh = 50 * ui;

    if (screen == MAIN || screen == PAUSE) {
        float gap = 10 * ui;
        if (screen == PAUSE || gameRunning) {
            if (button(text, in, x, y, bw, bh, "Verder spelen")) act = RESUME;
            y += bh + gap;
        }
        // in multiplayer bepaalt de server de tijd: zelf een tijd kiezen of een dienst klaarzetten kan dan niet
        if (button(text, in, x, y, bw, bh, mpConnected ? "Vrij spelen  (niet in multiplayer)" : "Vrij spelen", !mpConnected)) screen = NEW_GAME;
        y += bh + gap;
        if (button(text, in, x, y, bw, bh, mpConnected ? "Dienst rijden  (niet in multiplayer)" : "Dienst rijden  (bus of trein)", !mpConnected)) { screen = DUTY; dutyDirty = true; dutySel = -1; dutyScroll = 0; }
        y += bh + gap;
        if (button(text, in, x, y, bw, bh, "Personage")) {
            back_ = screen;
            screen = CHARACTER;
            editIndex = chars.empty() ? -1 : std::min(activeChar, (int)chars.size() - 1);
            edit = editIndex >= 0 ? chars[editIndex] : Appearance();
            previewDirty = true;
        }
        y += bh + gap;
        button(text, in, x, y, bw, bh, "Opdrachten  (binnenkort)", false);
        y += bh + gap;
        if (button(text, in, x, y, bw, bh, mpConnected ? "Multiplayer  (verbonden)" : "Multiplayer")) { back_ = screen; screen = MULTI; mpField = mpUrl.empty() ? 1 : 0; }
        y += bh + gap;
        if (button(text, in, x, y, bw, bh, "Opties")) { back_ = screen; screen = OPTIONS; }
        y += bh + gap;
        if (gameRunning) {
            if (button(text, in, x, y, bw, bh, "Terug naar hoofdmenu")) act = TO_MAIN;
            y += bh + gap;
        }
        if (button(text, in, x, y, bw, bh, "Afsluiten")) act = QUIT;
        if (!chars.empty() && activeChar < (int)chars.size())
            text.text(x, h - 88 * ui, "Jouw personage: " + chars[activeChar].name, 18 * ui, GREY);
        text.text(x, h - 56 * ui, "Open data: 3D BAG, AHN, BGT, PDOK Luchtfoto, OpenStreetMap", 15 * ui, DIM);
        if (screen == PAUSE && in.pressed[VK_ESCAPE]) act = RESUME;
    } else if (screen == MULTI) {
        text.text(x, y - 20 * ui, "Multiplayer", 34 * ui, WHITE);
        y += 40 * ui;
        bool enter = in.pressed[VK_RETURN];
        if (mpConnected) {
            text.text(x, y, "Verbonden met " + mpServerName, 22 * ui, YEL);
            y += 34 * ui;
            text.text(x, y, mpUrl, 17 * ui, GREY);
            y += 44 * ui;
            if (button(text, in, x, y, bw, bh, "Verder spelen")) act = RESUME;
            y += bh + 10 * ui;
            if (button(text, in, x, y, bw, bh, "Verbinding verbreken")) act = DISCONNECT;
        } else {
            text.text(x, y, "Serveradres", 18 * ui, GREY);
            y += 26 * ui;
            textField(text, in, x, y, bw, 36 * ui, mpUrl, 1, false, 200);
            y += 50 * ui;
            text.text(x, y, "Wachtwoord (als de server er een heeft)", 18 * ui, GREY);
            y += 26 * ui;
            textField(text, in, x, y, bw, 36 * ui, mpPassword, 2, true, 64);
            y += 56 * ui;
            text.text(x, y, "Waar begin je?", 18 * ui, GREY);
            y += 28 * ui;
            float sh = 27 * ui;
            for (size_t i = 0; i < starts.size() && y < h - 200 * ui; i++) {
                if (button(text, in, x, y, bw, sh, starts[i], true, (int)i == startChoice, 16 * ui)) startChoice = (int)i;
                y += sh + 3 * ui;
            }
            float by = h - 84 * ui;
            if (!mpStatus.empty()) text.text(x, by - 40 * ui, mpStatus, 17 * ui, {1.0f, 0.55f, 0.45f, 1});
            if (button(text, in, x, by, bw * 0.58f, bh, "Verbinden") || (enter && !mpUrl.empty())) { mpField = 0; act = CONNECT; }
            if (button(text, in, x + bw * 0.62f, by, bw * 0.38f, bh, "Terug") || (in.pressed[VK_ESCAPE] && mpField == 0)) screen = back_;
            if (in.pressed[VK_ESCAPE]) mpField = 0;
        }
        if (mpConnected && in.pressed[VK_ESCAPE]) screen = back_;
        // uitleg rechts
        float tx = pw + 80 * ui, ty = 224 * ui;
        text.rect(tx - 24 * ui, ty - 20 * ui, 620 * ui, 250 * ui, {0.03f, 0.05f, 0.09f, 0.82f});
        const char* info[] = {"Samen spelen op een OV Game server.", "", "De server bepaalt de tijd: treinen en bussen rijden bij", "iedereen op dezelfde plek.",
                              "Je ziet elkaar lopen, zitten en meerijden.", "", "T = chatten,  Tab = wie is er online"};
        for (const char* l : info) { text.text(tx, ty, l, 18 * ui, l[0] ? GREY : DIM); ty += 28 * ui; }
    } else if (screen == NEW_GAME) {
        text.text(x, y - 20 * ui, "Vrij spelen", 34 * ui, WHITE);
        y += 36 * ui;
        text.text(x, y, "Waar begin je?", 22 * ui, GREY);
        y += 36 * ui;
        float sh = 31 * ui;
        for (size_t i = 0; i < starts.size(); i++) {
            if (button(text, in, x, y, bw, sh, starts[i], true, (int)i == startChoice, 18 * ui)) startChoice = (int)i;
            y += sh + 4 * ui;
        }
        float tx = pw + 80 * ui, ty = 224 * ui;
        text.rect(tx - 24 * ui, ty - 20 * ui, 520 * ui + 48 * ui, 400 * ui, {0.03f, 0.05f, 0.09f, 0.82f});
        text.text(tx, ty, "Hoe laat is het?", 22 * ui, GREY);
        ty += 40 * ui;
        bool enterUsed = false, escUsed = false;
        timePicker(text, in, tx, ty, enterUsed, escUsed, false);
        ty += 230 * ui;
        text.text(tx, ty, "Treinen en bussen rijden volgens de dienstregeling vanaf deze tijd.", 17 * ui, GREY);
        ty += 26 * ui;
        text.text(tx, ty, "De zon staat zoals vandaag in Eindhoven op dat moment.", 17 * ui, GREY);
        float by = h - 84 * ui;
        if (button(text, in, x, by, bw * 0.58f, bh, "Start")) { if (editing) acceptTyped(); act = START; }
        if (button(text, in, x + bw * 0.62f, by, bw * 0.38f, bh, "Terug")) screen = gameRunning ? PAUSE : MAIN;
        if (in.pressed[VK_RETURN] && !enterUsed && !editing) act = START;
        if (in.pressed[VK_ESCAPE] && !escUsed && !editing) screen = gameRunning ? PAUSE : MAIN;
    } else if (screen == DUTY) {
        text.text(x, y - 20 * ui, "Dienst rijden", 34 * ui, WHITE);
        y += 30 * ui;
        float hw2 = (bw - 8 * ui) / 2;
        if (button(text, in, x, y, hw2, 40 * ui, "Bus", true, dutyKind == 0, 20 * ui) && dutyKind != 0) { dutyKind = 0; dutyDirty = true; dutySel = -1; dutyScroll = 0; }
        if (button(text, in, x + hw2 + 8 * ui, y, hw2, 40 * ui, "Trein", true, dutyKind == 1, 20 * ui) && dutyKind != 1) { dutyKind = 1; dutyDirty = true; dutySel = -1; dutyScroll = 0; }
        y += 50 * ui;
        if (dutyKind == 0) {
            text.text(x, y, "Lijn", 18 * ui, GREY);
            float lx = x + 50 * ui, lw = 56 * ui;
            for (size_t i = 0; i < busLines.size(); i++) {
                if (button(text, in, lx, y - 4 * ui, lw, 32 * ui, busLines[i], true, (int)i == dutyLine, 17 * ui) && dutyLine != (int)i) {
                    dutyLine = (int)i;
                    dutyDirty = true;
                    dutySel = -1;
                    dutyScroll = 0;
                }
                lx += lw + 6 * ui;
            }
            y += 40 * ui;
        }
        text.text(x, y, dutyKind == 0 ? "Ritten vanaf (3 uur vooruit):" : "Vertrekken vanaf (3 uur vooruit):", 18 * ui, GREY);
        y += 28 * ui;
        // lijst
        float rowH = 29 * ui, listTop = y, listBottom = h - 100 * ui;
        int visible = std::max(1, (int)((listBottom - listTop) / rowH));
        bool overList = in.mouseX > x && in.mouseX < x + bw && in.mouseY > listTop && in.mouseY < listBottom;
        if (overList && in.wheel != 0) dutyScroll -= (int)in.wheel * 3;
        dutyScroll = std::max(0, std::min(dutyScroll, std::max(0, (int)dutyItems.size() - visible)));
        if (dutyItems.empty()) text.text(x, y + 6 * ui, "Geen ritten in deze periode - kies een andere tijd.", 17 * ui, DIM);
        for (int i = dutyScroll; i < (int)dutyItems.size() && i < dutyScroll + visible; i++) {
            if (button(text, in, x, y, bw, rowH - 3 * ui, dutyItems[i].label, true, i == dutySel, 16 * ui)) dutySel = i;
            y += rowH;
        }
        if ((int)dutyItems.size() > visible) text.text(x + bw - 150 * ui, listBottom + 2 * ui, "scrollwiel: meer", 14 * ui, DIM);
        // rechts: tijd en uitleg
        float tx = pw + 60 * ui, ty = 120 * ui, tw = std::min(720 * ui, w - tx - 40 * ui);
        text.rect(tx - 24 * ui, ty - 20 * ui, tw + 48 * ui, h - ty - 60 * ui, {0.03f, 0.05f, 0.09f, 0.82f});
        text.text(tx, ty, "Vanaf welke tijd?", 20 * ui, GREY);
        ty += 34 * ui;
        bool enterUsed = false, escUsed = false;
        timePicker(text, in, tx, ty, enterUsed, escUsed, true);
        ty += 170 * ui;
        auto para = [&](const std::string& s, float size, Color4 col) {
            // eenvoudige regelafbreking
            std::string line, word;
            auto flush = [&]() { if (!line.empty()) { text.text(tx, ty, line, size, col); ty += size * 1.35f; line.clear(); } };
            for (size_t k = 0; k <= s.size(); k++) {
                char ch = k < s.size() ? s[k] : ' ';
                if (ch == '\n') { line += (line.empty() ? "" : " ") + word; word.clear(); flush(); continue; }
                if (ch != ' ') { word += ch; continue; }
                if (word.empty()) continue;
                std::string tryL = line.empty() ? word : line + " " + word;
                if (text.measure(tryL, size) > tw) { flush(); line = word; }
                else line = tryL;
                word.clear();
            }
            flush();
        };
        if (dutySel >= 0 && dutySel < (int)dutyItems.size()) {
            para(dutyItems[dutySel].label, 21 * ui, YEL);
            ty += 6 * ui;
            para(dutyItems[dutySel].info, 17 * ui, WHITE);
            ty += 10 * ui;
        } else {
            para("Kies een rit in de lijst links.", 18 * ui, GREY);
        }
        if (dutyKind == 0) {
            para("Bus: W gas, S rem (vasthouden: achteruit), A/D sturen, Q deuren. Rijd de route (navigatie bovenin en de lijn op de weg, N = aan/uit), "
                 "stop bij haltes waar reizigers wachten of waar op de stopknop is gedrukt en open de deuren. Je ziet of je voor of achter loopt.",
                 16 * ui, GREY);
        } else {
            para(std::string("Trein (") + REALISM[std::max(0, std::min(realism, 2))] + ", te wijzigen in Opties): " + REALISM_INFO[std::max(0, std::min(realism, 2))], 16 * ui, GREY);
        }
        float by = h - 84 * ui;
        if (button(text, in, x, by, bw * 0.58f, bh, "Start dienst", dutySel >= 0)) { if (editing) acceptTyped(); act = START_DUTY; }
        if (button(text, in, x + bw * 0.62f, by, bw * 0.38f, bh, "Terug")) screen = gameRunning ? PAUSE : MAIN;
        if (in.pressed[VK_ESCAPE] && !escUsed && !editing) screen = gameRunning ? PAUSE : MAIN;
    } else if (screen == CHARACTER) {
        text.text(x, y - 24 * ui, "Personage", 34 * ui, WHITE);
        y += 20 * ui;
        // naam
        text.text(x, y + 4 * ui, "Naam", 18 * ui, GREY);
        float nx = x + 170 * ui, nw = bw - 170 * ui, nh = 32 * ui;
        bool overName = in.mouseX > nx && in.mouseX < nx + nw && in.mouseY > y && in.mouseY < y + nh;
        if (in.lClick) nameEditing = overName;
        if (nameEditing) {
            for (char ch : in.chars) {
                if (ch == '\b') { if (!edit.name.empty()) edit.name.pop_back(); }
                else if (ch == '\r' || ch == '\n') nameEditing = false;
                else if ((unsigned char)ch >= 32 && edit.name.size() < 22 && ch != ';' && ch != '=') edit.name += ch;
            }
            if (in.pressed[VK_RETURN]) nameEditing = false;
        }
        text.rect(nx, y, nw, nh, nameEditing ? Color4{0.15f, 0.25f, 0.45f, 0.95f} : overName ? Color4{0.16f, 0.19f, 0.26f, 0.95f} : Color4{0.10f, 0.13f, 0.19f, 0.9f});
        text.text(nx + 10 * ui, y + 4 * ui, edit.name + (nameEditing && ((GetTickCount() / 500) & 1) ? "_" : ""), 19 * ui, WHITE);
        y += nh + 12 * ui;
        // eigenschappen met < >
        float rh = 25 * ui, fs = 16 * ui, vx = x + 170 * ui, vw = bw - 170 * ui;
        for (int k = 0; k < LOOK_FIELD_COUNT; k++) {
            const LookField& f = LOOK_FIELDS[k];
            int& v = edit.*f.member;
            text.text(x, y + 3 * ui, f.label, fs, GREY);
            if (button(text, in, vx, y, 30 * ui, rh - 3 * ui, "<", true, false, fs)) { v = (v + f.count - 1) % f.count; previewDirty = true; nameEditing = false; }
            if (button(text, in, vx + vw - 30 * ui, y, 30 * ui, rh - 3 * ui, ">", true, false, fs)) { v = (v + 1) % f.count; previewDirty = true; nameEditing = false; }
            float cx = vx + 40 * ui;
            if (f.swatch) {
                Vec3 c = f.swatch == 1 ? lookSkinColor(v) : f.swatch == 2 ? lookHairColor(v) : lookClothColor(v);
                text.rect(cx, y + 4 * ui, 16 * ui, rh - 11 * ui, {c.x, c.y, c.z, 1});
                cx += 24 * ui;
            }
            text.text(cx, y + 3 * ui, f.names[std::max(0, std::min(v, f.count - 1))], fs, WHITE);
            y += rh;
        }
        // knoppen onderaan
        float by = h - 132 * ui, b3 = (bw - 16 * ui) / 3;
        if (button(text, in, x, by, b3, 42 * ui, "Willekeurig", true, false, 18 * ui)) {
            std::string keep = edit.name;
            edit = Appearance::random(GetTickCount());
            edit.name = keep;
            previewDirty = true;
        }
        if (button(text, in, x + b3 + 8 * ui, by, b3, 42 * ui, "Opslaan", true, false, 18 * ui)) {
            if (edit.name.empty()) edit.name = "Reiziger";
            if (editIndex < 0 || editIndex >= (int)chars.size()) { chars.push_back(edit); editIndex = (int)chars.size() - 1; }
            else chars[editIndex] = edit;
            activeChar = editIndex;
            act = SAVE_CHARS;
        }
        if (button(text, in, x + 2 * (b3 + 8 * ui), by, b3, 42 * ui, "Verwijderen", editIndex >= 0 && chars.size() > 1, false, 18 * ui)) {
            chars.erase(chars.begin() + editIndex);
            activeChar = std::min(activeChar, (int)chars.size() - 1);
            if (activeChar >= editIndex && activeChar > 0) activeChar--;
            activeChar = std::max(0, std::min(activeChar, (int)chars.size() - 1));
            editIndex = activeChar;
            edit = chars[editIndex];
            previewDirty = true;
            act = SAVE_CHARS;
        }
        if (button(text, in, x, h - 80 * ui, bw, 44 * ui, "Terug", true, false, 20 * ui) || (in.pressed[VK_ESCAPE] && !nameEditing)) screen = back_;
        // rechts: opgeslagen personages en het voorbeeld
        float rx = pw + 40 * ui, ry = 60 * ui;
        text.text(rx, ry, "Jouw personages (blauw = gekozen, actief = in het spel)", 17 * ui, GREY);
        ry += 28 * ui;
        float cx = rx;
        for (size_t i = 0; i <= chars.size(); i++) {
            std::string lab = i < chars.size() ? chars[i].name + ((int)i == activeChar ? "  (actief)" : "") : "+ Nieuw";
            float wbtn = text.measure(lab, 17 * ui) + 30 * ui;
            if (cx + wbtn > w - 30 * ui) { cx = rx; ry += 40 * ui; }
            if (button(text, in, cx, ry, wbtn, 34 * ui, lab, true, (int)i == editIndex || (i == chars.size() && editIndex < 0), 17 * ui)) {
                if (i < chars.size()) { editIndex = (int)i; edit = chars[i]; }
                else { editIndex = -1; edit = Appearance::random(GetTickCount()); edit.name = "Nieuw personage"; }
                previewDirty = true;
                nameEditing = false;
            }
            cx += wbtn + 8 * ui;
        }
        ry += 50 * ui;
        previewX = rx;
        previewY = ry;
        previewW = w - rx - 30 * ui;
        previewH = h - ry - 40 * ui;
        text.rect(previewX, previewY, previewW, previewH, {0.20f, 0.24f, 0.30f, 0.85f});
        text.text(previewX + 16 * ui, previewY + previewH - 34 * ui, "Sleep met de muis om te draaien.  Opslaan maakt dit je actieve personage.", 15 * ui, GREY);
        if (in.lDown && in.mouseX > previewX && in.mouseX < previewX + previewW && in.mouseY > previewY && in.mouseY < previewY + previewH) showPreview = true;
        showPreview = true;
    } else if (screen == OPTIONS) {
        text.text(x, y - 20 * ui, "Opties", 34 * ui, WHITE);
        y += 30 * ui;
        text.text(x, y, "Grafisch", 22 * ui, YEL);
        y += 36 * ui;
        text.text(x, y, "Weergave", 19 * ui, GREY);
        y += 30 * ui;
        const char* modes[] = {"Venster", "Volledig scherm (venster)", "Volledig scherm"};
        const char* hints[] = {"Gewoon venster, vrij te verplaatsen en te vergroten.",
                               "Randloos venster over het hele scherm; snel wisselen met Alt+Tab.",
                               "Exclusief volledig scherm op de resolutie van je beeldscherm."};
        float sh = 40 * ui;
        for (int k = 0; k < 3; k++) {
            if (button(text, in, x, y, bw, sh, modes[k], true, windowMode == k, 19 * ui) && windowMode != k) { windowMode = k; act = SETTINGS; }
            y += sh + 6 * ui;
        }
        text.text(x, y + 2 * ui, hints[windowMode], 15 * ui, GREY);
        y += 36 * ui;
        text.text(x, y, "VSync  (geen scheuren in het beeld)", 19 * ui, GREY);
        y += 30 * ui;
        float hw = (bw - 8 * ui) / 2;
        if (button(text, in, x, y, hw, sh, "Aan", true, vsync, 19 * ui) && !vsync) { vsync = true; act = SETTINGS; }
        if (button(text, in, x + hw + 8 * ui, y, hw, sh, "Uit", true, !vsync, 19 * ui) && vsync) { vsync = false; act = SETTINGS; }
        y += sh + 26 * ui;
        text.text(x, y, "Spel", 22 * ui, YEL);
        y += 36 * ui;
        text.text(x, y, "Treinrijden", 19 * ui, GREY);
        y += 30 * ui;
        float tw3 = (bw - 16 * ui) / 3;
        for (int k = 0; k < 3; k++)
            if (button(text, in, x + k * (tw3 + 8 * ui), y, tw3, sh, REALISM[k], true, realism == k, 17 * ui) && realism != k) { realism = k; act = SETTINGS; }
        y += sh + 10 * ui;
        // uitleg met regelafbreking
        {
            std::string s = REALISM_INFO[realism], line, word;
            float fsz = 15 * ui;
            for (size_t k = 0; k <= s.size(); k++) {
                char ch = k < s.size() ? s[k] : ' ';
                if (ch != ' ') { word += ch; continue; }
                std::string tryL = line.empty() ? word : line + " " + word;
                if (text.measure(tryL, fsz) > bw) { text.text(x, y, line, fsz, GREY); y += fsz * 1.35f; line = word; }
                else line = tryL;
                word.clear();
            }
            if (!line.empty()) text.text(x, y, line, fsz, GREY);
        }
        float by = h - 84 * ui;
        if (button(text, in, x, by, bw, bh, "Terug") || in.pressed[VK_ESCAPE]) screen = back_;
    }
    text.end();
    return act;
}

// ingetypte tijd overnemen: "7" -> 07:00, "730" -> 07:30, "1745" -> 17:45 (ongeldig = niets veranderen)
void Menu::acceptTyped() {
    std::string t = typed;
    if (t.size() <= 2) t += "00";
    else if (t.size() == 3) t = "0" + t;
    int hh = atoi(t.substr(0, 2).c_str()), mm = atoi(t.substr(2, 2).c_str());
    if (!typed.empty() && hh < 24 && mm < 60) { minutes = hh * 60 + mm; dutyDirty = true; }
    editing = false;
}
