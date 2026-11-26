#pragma once
// Omroep (bus, trein, station): vooraf ingesproken WAV-fragmenten (Nederlandse vrouwenstem) met een gong ervoor, door een
// "luidsprekerfilter" (bandbreedte van een omroepinstallatie). Verder het belletje van de stopknop.
// De fragmenten worden niet vooraf geladen: pas bij de eerste keer dat ze nodig zijn, komen ze via DataStore van de server
// (daarna uit de cache). Een bericht van meerdere fragmenten klinkt pas als alle fragmenten binnen zijn.
// say()/sayParts()/ding()/update() worden vanuit de game-thread aangeroepen, mix() vanuit de audio-thread.
#include <deque>
#include <map>
#include <string>
#include <vector>

class Announcer {
public:
    Announcer();
    ~Announcer();
    // standaardmap voor say()/has(): relatief aan data\ met '/', bijv. "audio/bus"
    void setDir(const std::string& rel) { dir_ = rel; }
    bool has(const std::string& clip) const;       // bestaat het fragment (op de server of lokaal)?
    void say(const std::string& clip, bool gong = true);  // in de wachtrij (fragment uit de standaardmap)
    // bericht uit fragmenten (paden relatief aan data\ zonder .wav, bijv. "audio/trein/naar"); maxWait: zo lang mag het
    // ophalen duren, daarna vervalt het bericht (verouderd)
    void sayParts(const std::vector<std::string>& parts, bool gong = true, float maxWait = 10.0f);
    void update(float dt);                         // elke frame: binnengekomen fragmenten laden, berichten starten
    void ding();                                   // stopknop
    void stop();                                   // wachtrij leeg (uitgestapt)
    void setGain(float g) { gain_ = g; }
    bool busy() const;
    void mix(float* out, int frames);

private:
    std::map<std::string, std::vector<float>> clips_;  // pad -> 44,1 kHz mono, al gefilterd (geladen fragmenten)
    std::string dir_ = "audio/bus";
    struct Pending { std::vector<std::string> parts; bool gong; float wait; };
    std::deque<Pending> pending_;                  // wacht op fragmenten van de server (game-thread)
    const std::vector<float>* clip(const std::string& rel);  // geladen fragment of nullptr (start zo nodig de download)
    std::vector<float> gong_, ding_;
    struct Item { const std::vector<float>* s; size_t pos; float delay; };
    std::deque<Item> queue_;
    float gain_ = 1.0f;
    void* cs_ = nullptr;  // CRITICAL_SECTION
};

// WAV-bestand (16 bit, mono of stereo, elke samplerate) -> 44,1 kHz mono
bool loadWav(const std::string& path, std::vector<float>& out);
