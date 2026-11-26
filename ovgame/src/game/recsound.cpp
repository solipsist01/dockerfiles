// Opgenomen treingeluid: zie recsound.h
#include "recsound.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include "../engine/audio.h"
#include "announcer.h"
#include "datastore.h"

namespace {
std::map<std::string, std::unique_ptr<SoundSet>>& sets() {
    static std::map<std::string, std::unique_ptr<SoundSet>> m;
    return m;
}
int ctrlId(const std::string& s) {
    if (s == "AbsoluteSpeed") return SoundSet::SPEED;
    if (s == "Current") return SoundSet::CURRENT;
    if (s == "BrakeAmount") return SoundSet::BRAKE;
    if (s == "Active") return SoundSet::ACTIVE;
    if (s == "Startup") return SoundSet::STARTUP;
    if (s == "Doors") return SoundSet::DOORS;
    if (s == "JointCount") return SoundSet::JOINT;
    if (s == "DoorBell") return SoundSet::DOORBELL;
    return -1;
}
int kindId(const std::string& s) {
    if (s == "ValueInRange") return SoundSet::IN_RANGE;
    if (s == "ValueIncreasePast") return SoundSet::INCREASE_PAST;
    if (s == "ValueDecreasePast") return SoundSet::DECREASE_PAST;
    if (s == "ValueIsChanging") return SoundSet::CHANGING;
    if (s == "ValueIsChangingUp") return SoundSet::CHANGING_UP;
    return -1;
}
// totale versterking van de opnames t.o.v. de rest van het spelgeluid; buiten iets harder (in Train Simulator staat de
// luisteraar op 7 m, de game rekent de afstand zelf mee)
const float MASTER = 0.8f, OUTSIDE = 2.4f;
}  // namespace

float SoundSet::Curve::at(float x) const {
    if (pts.empty()) return 1.0f;
    if (x <= pts.front().first) return pts.front().second;
    if (x >= pts.back().first) return pts.back().second;
    for (size_t i = 1; i < pts.size(); i++) {
        if (x <= pts[i].first) {
            float x0 = pts[i - 1].first, x1 = pts[i].first;
            if (x1 - x0 < 1e-6f) return pts[i].second;
            float t = (x - x0) / (x1 - x0);
            return pts[i - 1].second + (pts[i].second - pts[i - 1].second) * t;
        }
    }
    return pts.back().second;
}

void SoundSet::Item::eval(const float* v, float& vo, float& pi) const {
    vo = 1.0f;
    pi = 1.0f;
    for (const Mod& m : mods) {
        float y = m.c.at(v[m.ctrl]);
        if (m.pitch) pi *= y;
        else vo *= y;
    }
}

bool SoundSet::parse(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    std::map<std::string, int> fileIdx;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string w;
        ss >> w;
        if (w == "loop" || w == "shot") {
            Item it;
            std::string where, fl, ctrl, kind;
            it.loop = w == "loop";
            ss >> where >> fl >> it.vol >> it.pitch >> ctrl >> kind >> it.a;
            if (!(ss >> it.b)) it.b = it.a;
            it.where = where == "in" ? 0 : where == "out" ? 1 : 2;
            it.ctrl = ctrlId(ctrl);
            it.kind = kindId(kind);
            std::stringstream fs(fl);
            std::string one;
            while (std::getline(fs, one, ',')) {
                std::string rel = "audio/" + name_ + "/" + one;
                auto e = fileIdx.find(rel);
                if (e == fileIdx.end()) {
                    e = fileIdx.emplace(rel, (int)files.size()).first;
                    files.push_back(rel);
                }
                it.samples.push_back(e->second);
            }
            items.push_back(std::move(it));
            if (items.back().ctrl < 0 || items.back().kind < 0) items.pop_back();  // onbekend: overslaan
        } else if (w == "mod" && !items.empty()) {
            Mod m;
            std::string kind, ctrl, pt;
            ss >> kind >> ctrl;
            m.pitch = kind == "pitch";
            m.ctrl = ctrlId(ctrl);
            while (ss >> pt) {
                float x, y;
                if (sscanf(pt.c_str(), "%f:%f", &x, &y) == 2) m.c.pts.push_back({x, y});
            }
            if (m.ctrl >= 0) items.back().mods.push_back(std::move(m));
        }
    }
    data_.reset(new std::atomic<const std::vector<float>*>[files.size()]);
    for (size_t i = 0; i < files.size(); i++) data_[i].store(nullptr);
    owned_.resize(files.size());
    // loops eerst ophalen (die hoor je meteen), dan de losse geluiden
    std::vector<int> order;
    for (const Item& it : items) if (it.loop) for (int s : it.samples) order.push_back(s);
    for (const Item& it : items) if (!it.loop) for (int s : it.samples) order.push_back(s);
    for (int s : order) DataStore::peek(files[s]);
    parsed_ = true;
    return true;
}

bool SoundSet::loadOne(bool block) {
    for (size_t k = 0; k < files.size(); k++) {
        size_t i = (next_ + k) % files.size();
        if (owned_[i] || data_[i].load()) continue;
        std::string path = block ? DataStore::fetch(files[i]) : DataStore::peek(files[i]);
        if (path.empty()) continue;
        auto v = std::make_unique<std::vector<float>>();
        if (!loadWav(path, *v)) v->clear();
        owned_[i] = std::move(v);
        if (!owned_[i]->empty()) data_[i].store(owned_[i].get(), std::memory_order_release);
        next_ = i + 1;
        return true;
    }
    return false;
}

const SoundSet* SoundSet::get(const std::string& name) {
    auto& m = sets();
    auto it = m.find(name);
    if (it == m.end()) {
        it = m.emplace(name, std::make_unique<SoundSet>()).first;
        it->second->name_ = name;
    }
    SoundSet& s = *it->second;
    s.wanted_ = true;
    if (!s.parsed_) {
        std::string rel = "audio/" + name + "/sound.txt";
        std::string p = DataStore::peek(rel);
        if (p.empty() || !s.parse(p)) return nullptr;
    }
    return &s;
}

void SoundSet::updateAll() {
    for (auto& kv : sets())
        if (kv.second->parsed_ && kv.second->wanted_) kv.second->loadOne(false);
}

const SoundSet* SoundSet::loadNow(const std::string& name) {
    std::string p = DataStore::fetch("audio/" + name + "/sound.txt");
    if (p.empty()) return nullptr;
    auto& m = sets();
    auto& sp = m[name];
    if (!sp) {
        sp = std::make_unique<SoundSet>();
        sp->name_ = name;
    }
    if (!sp->parsed_ && !sp->parse(p)) return nullptr;
    while (sp->loadOne(true)) {}
    return sp.get();
}

// ================================ afspelen ================================

void RecordedSound::trigger(const SoundSet& set, int item) {
    const SoundSet::Item& it = set.items[item];
    if (it.samples.empty() || voices_.size() >= 24) return;
    int s = it.samples[std::min((int)it.samples.size() - 1, (int)(rnd() * it.samples.size()))];
    if (!set.sample(s)) return;
    voices_.push_back({item, s, 0.0, 0.0f, true});
}

void RecordedSound::mix(float* out, int frames, const SoundSet& set, const TrainSoundState& s, float gain, float pan, bool inside) {
    const float dtB = (float)frames / Audio::SAMPLE_RATE;
    if (loops_.size() != set.items.size()) {
        loops_.assign(set.items.size(), LoopVoice());
        reset_ = true;
    }
    if (reset_) {  // andere trein op dit kanaal: overnemen zonder gebeurtenissen
        spd_ = s.speed;
        eff_ = s.effort;
        in_ = inside ? 1.0f : 0.0f;
        hold_ = std::fabs(s.speed) < 0.1f && s.effort <= 0 ? 0.3f : 0.0f;
        g_ = 0;
        p_ = pan;
        for (LoopVoice& l : loops_) { l.vol = 0; l.pos = rnd() * 100000.0; }
        voices_.clear();
        axles_.clear();
        dist_ = 0;
        nextJoint_ = 10 + rnd() * 60;
    }
    // regelwaarden vloeiend volgen
    spd_ += (s.speed - spd_) * std::min(1.0f, dtB * 4.0f);
    eff_ += (s.effort - eff_) * std::min(1.0f, dtB * 2.5f);
    in_ += ((inside ? 1.0f : 0.0f) - in_) * std::min(1.0f, dtB * 6.0f);
    float aspd = std::fabs(spd_);
    // stilstand: de rem blijft aan tot de trein weer trekt (dan "remmen los")
    if (s.effort > 0.02f) hold_ = 0;
    else if (aspd < 0.1f && s.effort <= 0) hold_ = 0.3f;
    // lasnaden/wissels: willekeurig om de 25-95 m, per draaistel twee assen
    dist_ += aspd * dtB;
    if (dist_ >= nextJoint_) {
        axles_.push_back(nextJoint_);
        axles_.push_back(nextJoint_ + 2.5);
        nextJoint_ += 25 + rnd() * 70;
    }
    float joints = prev_[SoundSet::JOINT];
    while (!axles_.empty() && axles_.front() <= dist_) {
        axles_.erase(axles_.begin());
        joints += 1;
        break;  // per blok hooguit een (de assen liggen 2,5 m uit elkaar: ruim een blok)
    }
    float* v = v_;
    v[SoundSet::SPEED] = aspd;
    v[SoundSet::CURRENT] = eff_ > 0 ? eff_ * 200.0f : eff_ * 50.0f;  // tractiestroom (A); negatief = elektrisch remmen
    v[SoundSet::BRAKE] = eff_ < -0.01f ? -eff_ : hold_;
    v[SoundSet::ACTIVE] = 1;
    v[SoundSet::STARTUP] = 1;
    v[SoundSet::DOORS] = s.doors;
    v[SoundSet::JOINT] = joints;
    v[SoundSet::DOORBELL] = s.beeping ? 1.0f : 0.0f;
    if (reset_) {
        std::memcpy(prev_, v, sizeof(prev_));
        reset_ = false;
    }

    // losse geluiden starten
    for (size_t i = 0; i < set.items.size(); i++) {
        const SoundSet::Item& it = set.items[i];
        if (it.loop) continue;
        float a = prev_[it.ctrl], b = v[it.ctrl];
        bool fire = false;
        switch (it.kind) {
            case SoundSet::INCREASE_PAST: fire = a < it.a && b >= it.a; break;
            case SoundSet::DECREASE_PAST: fire = a > it.a && b <= it.a; break;
            case SoundSet::CHANGING: fire = a != b; break;
            case SoundSet::CHANGING_UP: fire = b > a; break;
            default: break;
        }
        float where = it.where == 0 ? in_ : it.where == 1 ? (1.0f - in_) * OUTSIDE : in_ + (1.0f - in_) * OUTSIDE;
        if (fire && where > 0.05f) trigger(set, (int)i);
    }
    std::memcpy(prev_, v, sizeof(prev_));

    // mengen (mono), volume per blok lineair van oud naar nieuw
    static thread_local std::vector<float> acc;
    acc.assign(frames, 0.0f);
    for (size_t i = 0; i < set.items.size(); i++) {
        const SoundSet::Item& it = set.items[i];
        if (!it.loop) continue;
        LoopVoice& lv = loops_[i];
        float x = v[it.ctrl];
        bool on = it.kind != SoundSet::IN_RANGE || (x >= it.a && x <= it.b);
        float mv, mp;
        it.eval(v, mv, mp);
        float where = it.where == 0 ? in_ : it.where == 1 ? (1.0f - in_) * OUTSIDE : in_ + (1.0f - in_) * OUTSIDE;
        float target = on ? it.vol * mv * where : 0.0f;
        double step = std::max(0.05f, it.pitch * mp);
        const std::vector<float>* smp = set.sample(it.samples.empty() ? -1 : it.samples[0]);
        if (!smp || smp->size() < 2) continue;
        const float* d = smp->data();
        double n = (double)smp->size();
        if (lv.pos >= n) lv.pos = std::fmod(lv.pos, n);
        if (lv.vol < 1e-4f && target < 1e-4f) {  // stil: alleen doorlopen
            lv.pos = std::fmod(lv.pos + step * frames, n);
            lv.vol = 0;
            continue;
        }
        float v0 = lv.vol, dv = (target - v0) / frames;
        double pos = lv.pos;
        for (int k = 0; k < frames; k++) {
            size_t a = (size_t)pos;
            float t = (float)(pos - a);
            size_t b = a + 1 < smp->size() ? a + 1 : 0;
            acc[k] += (d[a] + (d[b] - d[a]) * t) * (v0 + dv * k);
            pos += step;
            if (pos >= n) pos -= n;
        }
        lv.pos = pos;
        lv.vol = target;
    }
    for (size_t vi = 0; vi < voices_.size();) {
        Voice& vo = voices_[vi];
        const SoundSet::Item& it = set.items[vo.item];
        const std::vector<float>* smp = set.sample(vo.sample);
        float mv, mp;
        it.eval(v, mv, mp);
        float where = it.where == 0 ? in_ : it.where == 1 ? (1.0f - in_) * OUTSIDE : in_ + (1.0f - in_) * OUTSIDE;
        float target = it.vol * mv * where;
        if (vo.fresh) { vo.vol = target; vo.fresh = false; }
        double step = std::max(0.05f, it.pitch * mp);
        bool done = !smp;
        if (smp) {
            const float* d = smp->data();
            size_t n = smp->size();
            float v0 = vo.vol, dv = (target - v0) / frames;
            for (int k = 0; k < frames; k++) {
                size_t a = (size_t)vo.pos;
                if (a + 1 >= n) { done = true; break; }
                float t = (float)(vo.pos - a);
                acc[k] += (d[a] + (d[a + 1] - d[a]) * t) * (v0 + dv * k);
                vo.pos += step;
            }
            vo.vol = target;
        }
        if (done) voices_.erase(voices_.begin() + vi);
        else vi++;
    }
    // naar stereo met afstand en richting
    for (int k = 0; k < frames; k++) {
        g_ += (gain - g_) * 0.001f;
        p_ += (pan - p_) * 0.001f;
        float x = acc[k] * g_ * MASTER;
        out[k * 2] += x * std::sqrt(0.5f * (1.0f - p_));
        out[k * 2 + 1] += x * std::sqrt(0.5f * (1.0f + p_));
    }
}
