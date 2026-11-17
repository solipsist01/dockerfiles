#include "trainsound.h"
#include <cmath>
#include "../engine/audio.h"

static const double TAU = 6.283185307179586;

void TrainSound::set(const TrainSoundState& s, float gain, float pan, bool inside) {
    std::lock_guard<std::mutex> lk(m_);
    s_ = s;
    gain_ = gain;
    pan_ = pan;
    inside_ = inside;
}

float TrainSound::noise() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (rng_ & 0xFFFFFF) / 8388608.0f - 1.0f;
}

static float env(float t, float attack, float len) {
    if (t < 0 || t > len) return 0;
    if (t < attack) return t / attack;
    return std::exp(-3.0f * (t - attack) / (len - attack));
}

void TrainSound::mix(float* out, int frames) {
    TrainSoundState s;
    float gain, pan;
    bool inside;
    {
        std::lock_guard<std::mutex> lk(m_);
        s = s_; gain = gain_; pan = pan_; inside = inside_;
    }
    // ander treinstel op dit kanaal: oude gebeurtenissen niet afspelen, snelheid direct overnemen
    if (s.id != seenId_) {
        seenId_ = s.id;
        seenOpen_ = s.doorOpenEvents; seenWhistle_ = s.whistleEvents; seenAir_ = s.airEvents; seenClosed_ = s.doorClosedEvents;
        spd_ = s.speed;
        eff_ = s.effort;
        g_ = 0;
        tOpen_ = tWhistle_ = tAir_ = tClosed_ = -1;
    }
    // nieuwe gebeurtenissen
    if (s.doorOpenEvents != seenOpen_) { seenOpen_ = s.doorOpenEvents; tOpen_ = 0; }
    if (s.whistleEvents != seenWhistle_) { seenWhistle_ = s.whistleEvents; tWhistle_ = 0; }
    if (s.airEvents != seenAir_) { seenAir_ = s.airEvents; tAir_ = 0; }
    if (s.doorClosedEvents != seenClosed_) { seenClosed_ = s.doorClosedEvents; tClosed_ = 0; }

    const float dt = 1.0f / Audio::SAMPLE_RATE;
    for (int i = 0; i < frames; i++) {
        // parameters vloeiend volgen
        spd_ += (s.speed - spd_) * 0.0005f;
        eff_ += (s.effort - eff_) * 0.0003f;
        g_ += (gain - g_) * 0.001f;
        p_ += (pan - p_) * 0.001f;
        inside_f_ += ((inside ? 1.0f : 0.0f) - inside_f_) * 0.002f;
        float n = noise();

        // ---- rolgeluid ----
        lp1_ += 0.015f * (n - lp1_);
        lp2_ += 0.10f * (n - lp2_);
        float sp = spd_ / 25.0f;
        float roll = (lp1_ * 3.5f + (lp2_ - lp1_) * (1.0f - 0.7f * inside_f_)) * std::pow(std::min(sp, 1.4f), 1.3f) * 0.35f;
        // lichte ritmische modulatie (draaistellen over lasnaden)
        float joint = spd_ > 1 ? 0.85f + 0.15f * (float)std::sin(t_ * TAU * spd_ / 18.0) : 1.0f;
        roll *= joint;

        // ---- tractie: asynchrone motor + IGBT-omvormer ----
        float fe = spd_ * 4.15f;  // elektrische frequentie (Hz) bij wieldiameter 920 mm
        float load = std::fabs(eff_);
        float moving = spd_ > 0.05f ? 1.0f : 0.0f;
        float trac = 0;
        if (moving > 0) {
            // motorbrom en "zingende" harmonischen
            phA_ += TAU * (fe * 9.0) * dt;
            phB_ += TAU * (fe * 15.0) * dt;
            // draaggolf: lage snelheid vaste 750 Hz met zijbanden, daarna synchroon stijgend
            double carrier = spd_ < 7.0f ? 750.0 : 750.0 + (fe * 27.0 - 750.0 * 0.0) * 0.0 + (spd_ - 7.0) * 95.0;
            phC_ += TAU * carrier * dt;
            phD_ += TAU * (2.0 * fe) * dt;
            float side = 0.6f + 0.4f * (float)std::sin(phD_);
            float a1 = (float)std::sin(phA_) * 0.5f + (float)std::sin(phB_) * 0.25f;
            float a2 = (float)std::sin(phC_) * side;
            float lowSpeedFade = std::min(1.0f, spd_ / 1.5f);
            trac = (a1 * (0.15f + 0.85f * load) * 0.12f + a2 * load * 0.07f) * lowSpeedFade;
        }

        // ---- ventilatie / airco ----
        lp3_ += 0.03f * (n - lp3_);
        float hvac = lp3_ * (0.10f * inside_f_ + 0.03f);

        // ---- deuren: gong bij openen (twee tonen) + deurmotor ----
        float door = 0;
        if (tOpen_ >= 0) {
            float a = env(tOpen_, 0.005f, 0.6f), b = env(tOpen_ - 0.35f, 0.005f, 0.8f);
            door += (float)std::sin(TAU * 1175.0 * tOpen_) * a * 0.18f + (float)std::sin(TAU * 880.0 * tOpen_) * b * 0.18f;
            tOpen_ += dt;
            if (tOpen_ > 3.0f) tOpen_ = -1;
        }
        if (s.doorMove > 0) {
            hp_ += 0.05f * (n - hp_);
            door += hp_ * 0.25f + (float)std::sin(t_ * TAU * 140.0) * 0.02f;
        }
        // deursluitsignaal: snelle piepjes
        if (s.beeping) {
            beepT_ += dt;
            float gate = std::fmod(beepT_, 0.36f) < 0.18f ? 1.0f : 0.0f;
            float tone = (float)std::sin(TAU * 1900.0 * t_) * 0.8f + (float)std::sin(TAU * 3800.0 * t_) * 0.2f;
            door += tone * gate * 0.12f;
        } else {
            beepT_ = 0;
        }
        // deur dicht: doffe klap
        if (tClosed_ >= 0) {
            door += ((float)std::sin(TAU * 85.0 * tClosed_) * 0.5f + n * 0.3f) * env(tClosed_, 0.002f, 0.25f) * 0.5f;
            tClosed_ += dt;
            if (tClosed_ > 0.4f) tClosed_ = -1;
        }

        // ---- fluitje van de conducteur (erwtenfluit: snelle triller) ----
        float whistle = 0;
        if (tWhistle_ >= 0) {
            float e = tWhistle_ < 1.1f ? std::min(1.0f, tWhistle_ / 0.03f) * std::min(1.0f, (1.1f - tWhistle_) / 0.08f) : 0.0f;
            float trill = 0.65f + 0.35f * (float)std::sin(TAU * 31.0 * tWhistle_);
            phE_ += TAU * (3150.0 + 40.0 * std::sin(TAU * 31.0 * tWhistle_)) * dt;
            whistle = (float)std::sin(phE_) * e * trill * 0.16f;
            tWhistle_ += dt;
            if (tWhistle_ > 1.2f) tWhistle_ = -1;
        }

        // ---- perslucht (sissen) na het stoppen ----
        float air = 0;
        if (tAir_ >= 0) {
            air = (n - lp2_) * env(tAir_, 0.02f, 1.4f) * 0.22f;
            tAir_ += dt;
            if (tAir_ > 1.5f) tAir_ = -1;
        }

        // ---- piepende remmen vlak voor stilstand ----
        float squeal = 0;
        if (eff_ < -0.2f && spd_ > 0.15f && spd_ < 3.5f) {
            float a = (3.5f - spd_) / 3.5f * std::min(1.0f, spd_ / 0.4f);
            squeal = (float)std::sin(TAU * (3650.0 + 60.0 * std::sin(t_ * TAU * 5.0)) * t_) * a * 0.03f;
        }

        // binnen: deur- en conducteurgeluid gedempt, ventilatie hoorbaar; buiten: alles met afstand
        float inMul = 1.0f - 0.35f * inside_f_;
        float sum = 3.0f * (roll + trac * (1.0f - 0.4f * inside_f_) + squeal * inMul + air * inMul + door + whistle * inMul) * g_ + 3.0f * hvac * g_;
        float l = sum * std::sqrt(0.5f * (1.0f - p_)), r = sum * std::sqrt(0.5f * (1.0f + p_));
        out[i * 2] += l;
        out[i * 2 + 1] += r;
        t_ += dt;
    }
}
