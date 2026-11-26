#pragma once
// Treingeluid. Met een opgenomen geluidsset (RollingStock::soundSet, zie recsound.h) klinkt de echte trein en komt alleen
// het fluitje van de conducteur uit de synthesizer. Zonder set (FLIRT en de rest) volledig gesynthetiseerd:
//  - IGBT-omvormer/asynchrone motor: "zingende" tonen die met de snelheid stijgen (vooral bij optrekken/remmen)
//  - rolgeluid (gefilterde ruis), ventilatie in het rijtuig
//  - deuren: gong bij openen, deursluitsignaal (piepjes), dichtslaan, deurmotor
//  - fluitje van de conducteur, perslucht na het stoppen, piepende remmen
#include <mutex>
#include "recsound.h"
#include "train.h"

class TrainSound {
public:
    // vanuit de game-thread
    void set(const TrainSoundState& s, float gain, float pan, bool inside, const SoundSet* rec = nullptr);
    // vanuit de audio-thread
    void mix(float* out, int frames);

private:
    std::mutex m_;
    TrainSoundState s_;
    float gain_ = 0, pan_ = 0;
    bool inside_ = false;
    const SoundSet* rec_ = nullptr;
    RecordedSound recorded_;
    const SoundSet* lastRec_ = nullptr;

    // synthesizer-toestand (alleen audio-thread)
    uint32_t rng_ = 12345;
    double t_ = 0;
    float spd_ = 0, eff_ = 0, g_ = 0, p_ = 0, inside_f_ = 0;
    float lp1_ = 0, lp2_ = 0, lp3_ = 0, hp_ = 0;
    double phA_ = 0, phB_ = 0, phC_ = 0, phD_ = 0, phE_ = 0;
    int seenOpen_ = 0, seenWhistle_ = 0, seenAir_ = 0, seenClosed_ = 0, seenId_ = -1;
    float tOpen_ = -1, tWhistle_ = -1, tAir_ = -1, tClosed_ = -1, beepT_ = 0;
    float noise();
};
