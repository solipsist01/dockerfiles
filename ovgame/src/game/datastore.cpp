// Wereldbestanden lokaal of van de server: zie datastore.h
#include "datastore.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <deque>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "../net/net.h"

namespace DataStore {
namespace {

struct Entry {
    uint32_t crc = 0;
    uint64_t size = 0;
    bool z = false;
    bool local = false;  // lokaal aanwezig en gecontroleerd (crc gelijk aan de server)
};

struct State {
    bool remote = false;
    std::string base, dataDir;
    std::unordered_map<std::string, Entry> index;
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
    std::set<std::string> busy;          // wordt nu gedownload
    std::deque<std::string> prefetch;    // achtergrond (peek)
    std::set<std::string> queued;
    Stats st;
    FILE* log = nullptr;                 // stream.idx: "crc grootte pad" van wat lokaal klopt
    State() {
        InitializeCriticalSection(&cs);
        InitializeConditionVariable(&cv);
    }
};
State& S() {
    static State s;
    return s;
}

struct Lock {
    Lock() { EnterCriticalSection(&S().cs); }
    ~Lock() { LeaveCriticalSection(&S().cs); }
};

std::string localPath(const std::string& rel) {
    std::string p = S().dataDir + "\\" + rel;
    for (char& c : p) if (c == '/') c = '\\';
    return p;
}

void makeDirs(const std::string& file) {
    for (size_t i = S().dataDir.size() + 1; i < file.size(); i++)
        if (file[i] == '\\') CreateDirectoryA(file.substr(0, i).c_str(), nullptr);
}

uint64_t fileSize(const std::string& p) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &a)) return UINT64_MAX;
    return ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
}

// Download + controle + wegschrijven. Wordt buiten de lock aangeroepen.
bool download(const std::string& rel, const Entry& e) {
    char h[16];
    snprintf(h, sizeof h, "%08x", e.crc);
    std::string url = S().base + "/w/" + Net::urlEncode(rel) + "?h=" + h;
    std::string err;
    for (int attempt = 0; attempt < 4; attempt++) {
        if (attempt) Sleep(500u << attempt);
        Net::Response r = Net::get(url, nullptr, 60000);
        if (!r.ok()) {
            err = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error;
            if (r.status == 404) break;
            continue;
        }
        uint64_t wire = r.body.size();
        std::string data;
        if (e.z) {
            if (!Net::inflate(r.body, data, (size_t)e.size)) { err = "uitpakken mislukt"; continue; }
        } else {
            data.swap(r.body);
        }
        if (data.size() != e.size || Net::crc32(data.data(), data.size()) != e.crc) { err = "controle (crc) mislukt"; continue; }
        std::string p = localPath(rel), tmp = p + ".part";
        makeDirs(p);
        FILE* f = fopen(tmp.c_str(), "wb");
        if (!f) { err = "kan " + tmp + " niet schrijven"; break; }
        bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
        ok = fclose(f) == 0 && ok;
        if (!ok || !MoveFileExA(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileA(tmp.c_str());
            err = "kan " + p + " niet schrijven";
            break;
        }
        Lock l;
        S().st.bytes += wire;
        S().st.files++;
        if (S().log) {
            fprintf(S().log, "%08x %llu %s\n", e.crc, (unsigned long long)e.size, rel.c_str());
            fflush(S().log);
        }
        return true;
    }
    Lock l;
    S().st.failed++;
    S().st.lastError = rel + ": " + err;
    return false;
}

// Zorgt dat rel lokaal klopt. wait = false: niet wachten als een andere thread hem al downloadt.
bool ensure(const std::string& rel, bool wait) {
    State& s = S();
    Entry e;
    {
        Lock l;
        auto it = s.index.find(rel);
        if (it == s.index.end()) return false;
        if (it->second.local) return true;
        while (s.busy.count(rel)) {
            if (!wait) return false;
            SleepConditionVariableCS(&s.cv, &s.cs, INFINITE);
        }
        it = s.index.find(rel);
        if (it->second.local) return true;
        s.busy.insert(rel);
        s.st.active++;
        e = it->second;
    }
    bool ok = download(rel, e);
    {
        Lock l;
        s.busy.erase(rel);
        s.st.active--;
        if (ok) s.index[rel].local = true;
    }
    WakeAllConditionVariable(&s.cv);
    return ok;
}

DWORD WINAPI prefetchThread(LPVOID) {
    State& s = S();
    for (;;) {
        std::string rel;
        {
            Lock l;
            while (s.prefetch.empty()) SleepConditionVariableCS(&s.cv, &s.cs, INFINITE);
            rel = s.prefetch.front();
            s.prefetch.pop_front();
        }
        ensure(rel, true);
        Lock l;
        s.queued.erase(rel);
    }
}

}  // namespace

void initLocal(const std::string& dataDir) {
    S().remote = false;
    S().dataDir = dataDir;
}

bool initRemote(const std::string& baseUrl, const std::string& dataDir, std::string& error) {
    State& s = S();
    s.dataDir = dataDir;
    s.base = Net::normalizeBase(baseUrl);
    Net::Response r;
    for (int attempt = 0; attempt < 30; attempt++) {  // server kan nog bezig zijn met voorbereiden (503)
        r = Net::get(s.base + "/api/world?z=1", nullptr, 30000);
        if (r.status != 503) break;
        Sleep(2000);
    }
    if (!r.ok()) {
        error = "Wereldindex ophalen bij " + s.base + " mislukt: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
        return false;
    }
    std::string idx;
    if (!Net::inflate(r.body, idx, 0) || idx.rfind("OVWORLD 1", 0) != 0) { error = "Wereldindex van de server is ongeldig"; return false; }

    // wat we lokaal al hebben (en gecontroleerd zijn)
    std::unordered_map<std::string, std::pair<uint32_t, uint64_t>> have;
    std::string idxPath = dataDir + "\\stream.idx";
    if (FILE* f = fopen(idxPath.c_str(), "r")) {
        char line[1024];
        while (fgets(line, sizeof line, f)) {
            unsigned crc = 0;
            unsigned long long sz = 0;
            char path[900];
            if (sscanf(line, "%x %llu %899s", &crc, &sz, path) == 3) have[path] = {crc, sz};
        }
        fclose(f);
    }
    // index van de server inlezen
    size_t pos = idx.find('\n') + 1;
    int n = 0, valid = 0;
    while (pos < idx.size()) {
        size_t end = idx.find('\n', pos);
        if (end == std::string::npos) end = idx.size();
        std::string line = idx.substr(pos, end - pos);
        pos = end + 1;
        unsigned crc = 0;
        unsigned long long sz = 0;
        int z = 0;
        char path[900];
        if (sscanf(line.c_str(), "%x %llu %d %899s", &crc, &sz, &z, path) != 4) continue;
        Entry e;
        e.crc = crc;
        e.size = sz;
        e.z = z != 0;
        auto h = have.find(path);
        if (h != have.end() && h->second.first == crc && h->second.second == sz) {
            std::string lp = dataDir + "\\" + path;
            for (char& c : lp) if (c == '/') c = '\\';
            e.local = fileSize(lp) == sz;
        }
        valid += e.local;
        s.index[path] = e;
        n++;
    }
    if (n == 0) { error = "De server heeft geen wereld (index leeg)"; return false; }
    // stream.idx opnieuw schrijven met alleen wat klopt; nieuwe downloads komen erachter
    s.log = fopen(idxPath.c_str(), "w");
    if (s.log) {
        for (auto& kv : s.index)
            if (kv.second.local) fprintf(s.log, "%08x %llu %s\n", kv.second.crc, (unsigned long long)kv.second.size, kv.first.c_str());
        fflush(s.log);
    }
    s.remote = true;
    for (int i = 0; i < 2; i++) CreateThread(nullptr, 0, prefetchThread, nullptr, 0, nullptr);
    printf("Wereld van %s: %d bestanden, %d al lokaal\n", s.base.c_str(), n, valid);
    return true;
}

bool remote() { return S().remote; }
const std::string& serverUrl() { return S().base; }

std::string fetch(const std::string& rel) {
    if (!S().remote) return localPath(rel);
    return ensure(rel, true) ? localPath(rel) : std::string();
}

std::string peek(const std::string& rel) {
    if (!S().remote) return localPath(rel);
    State& s = S();
    {
        Lock l;
        auto it = s.index.find(rel);
        if (it == s.index.end()) return std::string();
        if (it->second.local) return localPath(rel);
        if (!s.queued.count(rel)) {
            s.queued.insert(rel);
            s.prefetch.push_back(rel);
        }
    }
    WakeAllConditionVariable(&s.cv);
    return std::string();
}

bool known(const std::string& rel) {
    if (!S().remote) return GetFileAttributesA(localPath(rel).c_str()) != INVALID_FILE_ATTRIBUTES;
    Lock l;
    return S().index.count(rel) != 0;
}

Stats stats() {
    Lock l;
    return S().st;
}

}  // namespace DataStore
