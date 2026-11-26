#pragma once
// Opgenomen treingeluid: een geluidsset uit data/audio/<naam>/ (sound.txt + WAV's, gemaakt door tools/make_train_sound.py
// uit een Train Simulator-geluidsset). Werkt zoals de "audio proxy" daar:
//  - loops die spelen zolang een regelwaarde in een bereik ligt (motoren, omvormers, rolgeluid, ventilatie),
//  - losse geluiden als een regelwaarde een grens passeert (deuren, omvormer start, remmen los, lasnaden),
//  - volume en toonhoogte uit curves per regelwaarde (snelheid, tractiestroom, remkracht, deuren).
// Elk geluid hoort binnen (in), buiten (out) of allebei.
// De WAV's komen pas van de server als een trein met deze set in de buurt is (DataStore::peek); tot dan blijft die
// loop stil. SoundSet::get/updateAll vanuit de game-thread; RecordedSound::mix vanuit de audio-thread.
#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "train.h"

class SoundSet {
public:
    enum Ctrl { SPEED, CURRENT, BRAKE, ACTIVE, STARTUP, DOORS, JOINT, DOORBELL, NCTRL };
    enum Kind { IN_RANGE, INCREASE_PAST, DECREASE_PAST, CHANGING, CHANGING_UP };
    struct Curve {
        std::vector<std::pair<float, float>> pts;
        float at(float x) const;
    };
    struct Mod { bool pitch; int ctrl; Curve c; };
    struct Item {
        bool loop;
        int where;                  // 0 binnen, 1 buiten, 2 allebei
        std::vector<int> samples;   // index in files (losse geluiden: willekeurige variant)
        float vol, pitch;
        int ctrl, kind;
        float a, b;
        std::vector<Mod> mods;
        // volume (zonder basisvolume) en toonhoogte voor regelwaarden v
        void eval(const float* v, float& vol, float& pitch) const;
    };
    std::vector<Item> items;
    std::vector<std::string> files;           // relatief aan data/, met .wav
    // geladen geluid (44,1 kHz mono) of nullptr; wordt eenmaal gezet en daarna niet meer veranderd
    const std::vector<float>* sample(int i) const { return i >= 0 && i < (int)files.size() ? data_[i].load(std::memory_order_acquire) : nullptr; }
    bool parsed() const { return parsed_; }

    // game-thread: de set met deze naam (wordt bij het eerste gebruik geladen), of nullptr als hij (nog) niet bestaat
    static const SoundSet* get(const std::string& name);
    static void updateAll();  // elke frame: binnengekomen WAV's laden (een per frame, om haperen te voorkomen)
    // alles van deze set nu laden (blokkerend; voor tests)
    static const SoundSet* loadNow(const std::string& name);

private:
    std::string name_;
    bool parsed_ = false, wanted_ = false;
    std::unique_ptr<std::atomic<const std::vector<float>*>[]> data_;
    std::vector<std::unique_ptr<std::vector<float>>> owned_;
    size_t next_ = 0;
    bool parse(const std::string& path);
    bool loadOne(bool block);  // true = er is iets gebeurd
};

class RecordedSound {
public:
    // audio-thread: mix één blok van de trein (s) met set 'set' bij in out (stereo); gain/pan/inside zoals TrainSound
    void mix(float* out, int frames, const SoundSet& set, const TrainSoundState& s, float gain, float pan, bool inside);
    void reset() { reset_ = true; }

private:
    bool reset_ = true;
    float v_[SoundSet::NCTRL] = {}, prev_[SoundSet::NCTRL] = {};
    float spd_ = 0, eff_ = 0, in_ = 0, g_ = 0, p_ = 0, hold_ = 0;
    double dist_ = 0, nextJoint_ = 40;
    std::vector<double> axles_;               // nog te passeren assen (afstand) over de laatste lasnaad
    uint32_t rng_ = 987654;
    struct LoopVoice { double pos = 0; float vol = 0; };
    std::vector<LoopVoice> loops_;
    struct Voice { int item, sample; double pos; float vol; bool fresh; };
    std::vector<Voice> voices_;
    float rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (rng_ & 0xFFFFFF) / 16777216.0f; }
    void trigger(const SoundSet& set, int item);
};
