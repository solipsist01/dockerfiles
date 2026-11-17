#include "announcer.h"
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../engine/audio.h"

namespace {
const float PI_F = 3.14159265f;

// biquad (RBJ), voor het luidsprekerfilter
struct Biquad {
    float b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    static Biquad make(bool high, float f, float q) {
        float w = 2 * PI_F * f / Audio::SAMPLE_RATE, c = std::cos(w), s = std::sin(w), al = s / (2 * q);
        Biquad r;
        float a0 = 1 + al;
        if (high) { r.b0 = (1 + c) / 2; r.b1 = -(1 + c); r.b2 = (1 + c) / 2; }
        else { r.b0 = (1 - c) / 2; r.b1 = 1 - c; r.b2 = (1 - c) / 2; }
        r.a1 = -2 * c; r.a2 = 1 - al;
        r.b0 /= a0; r.b1 /= a0; r.b2 /= a0; r.a1 /= a0; r.a2 /= a0;
        return r;
    }
    float run(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

bool readWav(const std::string& path, std::vector<float>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<unsigned char> b;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b.resize(n > 0 ? n : 0);
    if (n > 0) fread(b.data(), 1, n, f);
    fclose(f);
    if (b.size() < 44 || memcmp(b.data(), "RIFF", 4) || memcmp(b.data() + 8, "WAVE", 4)) return false;
    int channels = 1, rate = 22050, bits = 16;
    size_t pos = 12, dataOff = 0, dataLen = 0;
    while (pos + 8 <= b.size()) {
        uint32_t len;
        memcpy(&len, b.data() + pos + 4, 4);
        if (!memcmp(b.data() + pos, "fmt ", 4) && pos + 24 <= b.size()) {
            uint16_t ch, bp; uint32_t r;
            memcpy(&ch, b.data() + pos + 10, 2);
            memcpy(&r, b.data() + pos + 12, 4);
            memcpy(&bp, b.data() + pos + 22, 2);
            channels = ch; rate = (int)r; bits = bp;
        } else if (!memcmp(b.data() + pos, "data", 4)) {
            dataOff = pos + 8;
            dataLen = std::min<size_t>(len, b.size() - dataOff);
            break;
        }
        pos += 8 + len + (len & 1);
    }
    if (!dataOff || bits != 16 || channels < 1) return false;
    size_t frames = dataLen / (2 * channels);
    std::vector<float> src(frames);
    for (size_t i = 0; i < frames; i++) {
        int16_t v;
        memcpy(&v, b.data() + dataOff + i * 2 * channels, 2);
        src[i] = v / 32768.0f;
    }
    // naar 44,1 kHz (lineair)
    double step = (double)rate / Audio::SAMPLE_RATE;
    size_t outN = (size_t)(frames / step);
    out.resize(outN);
    for (size_t i = 0; i < outN; i++) {
        double p = i * step;
        size_t k = (size_t)p;
        float t = (float)(p - k);
        float a = src[std::min(k, frames - 1)], c = src[std::min(k + 1, frames - 1)];
        out[i] = a + (c - a) * t;
    }
    return true;
}

// omroepinstallatie: 300 Hz - 4 kHz, lichte compressie, korte weerkaatsing in de bus
void loudspeaker(std::vector<float>& s) {
    Biquad hp = Biquad::make(true, 220, 0.7f), hp2 = Biquad::make(true, 220, 0.7f);
    Biquad lp = Biquad::make(false, 6500, 0.7f), lp2 = Biquad::make(false, 6500, 0.7f);
    float peak = 1e-4f;
    for (float& x : s) {
        x = lp2.run(lp.run(hp2.run(hp.run(x))));
        peak = std::max(peak, std::fabs(x));
    }
    // alleen normaliseren en een heel zwakke vroege weerkaatsing (geen vervorming, geen terugkoppeling)
    std::vector<float> dry = s;
    const int d1 = Audio::SAMPLE_RATE * 17 / 1000;
    for (size_t i = 0; i < s.size(); i++) {
        float x = dry[i] / peak * 0.55f;
        if (i >= (size_t)d1) x += 0.06f * dry[i - d1] / peak;
        s[i] = x;
    }
    // stilte erachter
    s.insert(s.end(), Audio::SAMPLE_RATE / 3, 0.0f);
}

// klokje met boventonen
void bell(std::vector<float>& s, float f, float start, float dur, float amp) {
    size_t a = (size_t)(start * Audio::SAMPLE_RATE), n = (size_t)(dur * Audio::SAMPLE_RATE);
    if (s.size() < a + n) s.resize(a + n, 0.0f);
    const float partial[] = {1.0f, 2.0f, 3.0f, 4.2f};
    const float pamp[] = {1.0f, 0.35f, 0.12f, 0.05f};
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / Audio::SAMPLE_RATE;
        float env = std::exp(-t * 3.2f) * std::min(1.0f, t * 400.0f);
        float v = 0;
        for (int k = 0; k < 4; k++) v += pamp[k] * std::sin(2 * PI_F * f * partial[k] * t) * std::exp(-t * 2.0f * k);
        s[a + i] += amp * env * v;
    }
}
}  // namespace

Announcer::Announcer() {
    auto* c = new CRITICAL_SECTION;
    InitializeCriticalSection(c);
    cs_ = c;
    // gong: drie tonen omhoog (zacht), daarna even stil
    bell(gong_, 554.4f, 0.00f, 1.2f, 0.20f);
    bell(gong_, 659.3f, 0.32f, 1.2f, 0.20f);
    bell(gong_, 880.0f, 0.64f, 1.6f, 0.18f);
    gong_.resize(gong_.size() + Audio::SAMPLE_RATE / 4, 0.0f);
    // stopknop: kort hoog belletje
    bell(ding_, 1318.5f, 0.0f, 0.7f, 0.22f);
}

Announcer::~Announcer() {
    DeleteCriticalSection((CRITICAL_SECTION*)cs_);
    delete (CRITICAL_SECTION*)cs_;
}

int Announcer::load(const std::string& dir) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*.wav").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        std::string name = fd.cFileName;
        std::vector<float> s;
        if (readWav(dir + "\\" + name, s)) {
            loudspeaker(s);
            clips_[name.substr(0, name.size() - 4)] = std::move(s);
            n++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

void Announcer::say(const std::string& clip, bool gong) {
    auto it = clips_.find(clip);
    if (it == clips_.end()) return;
    EnterCriticalSection((CRITICAL_SECTION*)cs_);
    if (queue_.size() < 6) {
        if (gong) queue_.push_back({&gong_, 0, 0.3f});
        queue_.push_back({&it->second, 0, 0});
    }
    LeaveCriticalSection((CRITICAL_SECTION*)cs_);
}

void Announcer::ding() {
    EnterCriticalSection((CRITICAL_SECTION*)cs_);
    queue_.push_front({&ding_, 0, 0});
    LeaveCriticalSection((CRITICAL_SECTION*)cs_);
}

void Announcer::stop() {
    EnterCriticalSection((CRITICAL_SECTION*)cs_);
    queue_.clear();
    LeaveCriticalSection((CRITICAL_SECTION*)cs_);
}

bool Announcer::busy() const {
    EnterCriticalSection((CRITICAL_SECTION*)cs_);
    bool b = !queue_.empty();
    LeaveCriticalSection((CRITICAL_SECTION*)cs_);
    return b;
}

void Announcer::mix(float* out, int frames) {
    EnterCriticalSection((CRITICAL_SECTION*)cs_);
    int i = 0;
    while (i < frames && !queue_.empty()) {
        Item& it = queue_.front();
        if (it.delay > 0) {
            int wait = std::min(frames - i, (int)(it.delay * Audio::SAMPLE_RATE) + 1);
            it.delay -= (float)wait / Audio::SAMPLE_RATE;
            i += wait;
            continue;
        }
        const std::vector<float>& s = *it.s;
        while (i < frames && it.pos < s.size()) {
            float v = s[it.pos++] * gain_;
            out[i * 2] += v;
            out[i * 2 + 1] += v;
            i++;
        }
        if (it.pos >= s.size()) queue_.pop_front();
    }
    LeaveCriticalSection((CRITICAL_SECTION*)cs_);
}
