#pragma once
// Waar de wereldbestanden vandaan komen.
//   Lokaal (geen server): gewoon uit data\ (zoals altijd).
//   Server (--server URL, de launcher geeft dat mee): tegels en luchtfoto's worden pas gedownload als de game ze nodig
//   heeft (streaming) en daarna in data\ bewaard. De server-index (crc per bestand) bepaalt of een bewaard bestand nog
//   actueel is; zo komt een nieuwe versie van de wereld vanzelf binnen.
// Paden zijn relatief aan data\ met '/', bijv. "world/tiles/160500_383000.bin" of "raw/luchtfoto/160500_383000.jpg".
#include <cstdint>
#include <string>

namespace DataStore {

// Index van de server ophalen. Bij een fout blijft alles lokaal (error gevuld).
bool initRemote(const std::string& baseUrl, const std::string& dataDir, std::string& error);
void initLocal(const std::string& dataDir);
bool remote();
const std::string& serverUrl();

// Lokaal pad voor een databestand. Met een server: downloadt als het nog niet (actueel) aanwezig is; blokkeert dan.
// Geeft "" als het bestand niet bestaat of niet te downloaden was.
std::string fetch(const std::string& rel);
// Niet-blokkerend: pad als het bestand er al is, anders "" (en het wordt op de achtergrond gedownload).
std::string peek(const std::string& rel);
// Bestaat het bestand op de server (of lokaal)?
bool known(const std::string& rel);

struct Stats {
    uint64_t bytes = 0;      // gedownload (over de lijn)
    int files = 0;           // gedownloade bestanden
    int active = 0;          // nu bezig
    int failed = 0;
    std::string lastError;
};
Stats stats();

}  // namespace DataStore
