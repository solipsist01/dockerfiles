#pragma once
// Netwerk via WinHTTP (onderdeel van Windows): HTTP(S)-downloads en WebSockets.
// HTTPS (Cloudflare) gaat via de certificaten van Windows zelf; er is geen extra bibliotheek nodig.
// Gedeeld door de game en de launcher.
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace Net {

// Bijv. "https://ovgame.ofnie.org" + "/api/client". Geeft false bij een netwerkfout (error gevuld).
struct Response {
    int status = 0;
    std::string body;
    std::string error;
    bool ok() const { return status == 200 && error.empty(); }
};
// progress(ontvangen, totaal) wordt tijdens het downloaden aangeroepen; return false = afbreken. totaal = 0 als onbekend.
using Progress = std::function<bool(uint64_t, uint64_t)>;
Response get(const std::string& url, const Progress& progress = nullptr, int timeoutMs = 30000);

// Basisadres netjes maken: spaties weg, "https://" ervoor als er geen schema is, geen / aan het eind.
std::string normalizeBase(const std::string& s);
std::string urlEncode(const std::string& s);  // voor delen van een pad (spaties e.d.)

uint32_t crc32(const void* data, size_t n, uint32_t crc = 0);
// zlib-stroom uitpakken (stb_image); expect = verwachte grootte (0 = onbekend)
bool inflate(const std::string& in, std::string& out, size_t expect);

// ---- WebSocket (tekstberichten) ----
// connect blokkeert tot de verbinding er is (of mislukt); daarna leest een eigen thread de berichten in een wachtrij.
class WebSocket {
public:
    WebSocket();
    ~WebSocket();
    bool connect(const std::string& url, std::string& error);  // ws://, wss://, http:// of https:// (pad bijv. /ws)
    bool send(const std::string& text);
    bool poll(std::string& msg);   // volgend ontvangen bericht (niet-blokkerend)
    bool open() const;             // nog verbonden
    std::string closeReason() const;
    void close();
private:
    struct Impl;
    Impl* p_;
};

}  // namespace Net
