#pragma once
// Omroep in de bus: vooraf ingesproken WAV-bestanden (data/audio/bus/*.wav, Nederlandse vrouwenstem) met een gong
// ervoor, door een "luidsprekerfilter" (bandbreedte van een omroepinstallatie). Verder het belletje van de stopknop.
// say()/ding() worden vanuit de game-thread aangeroepen, mix() vanuit de audio-thread.
#include <deque>
#include <map>
#include <string>
#include <vector>

class Announcer {
public:
    Announcer();
    ~Announcer();
    int load(const std::string& dir);              // alle .wav-bestanden; geeft het aantal terug
    bool has(const std::string& clip) const { return clips_.count(clip) != 0; }
    void say(const std::string& clip, bool gong = true);  // in de wachtrij
    void ding();                                   // stopknop
    void stop();                                   // wachtrij leeg (uitgestapt)
    void setGain(float g) { gain_ = g; }
    bool busy() const;
    void mix(float* out, int frames);

private:
    std::map<std::string, std::vector<float>> clips_;  // 44,1 kHz mono, al gefilterd
    std::vector<float> gong_, ding_;
    struct Item { const std::vector<float>* s; size_t pos; float delay; };
    std::deque<Item> queue_;
    float gain_ = 1.0f;
    void* cs_ = nullptr;  // CRITICAL_SECTION
};
