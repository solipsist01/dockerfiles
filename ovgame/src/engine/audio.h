#pragma once
// Eenvoudige audio-uitvoer via Windows waveOut (44,1 kHz stereo).
// De game levert een mix-functie die in een aparte thread wordt aangeroepen.
#include <functional>

namespace Audio {
// mix(out, frames): vul 'frames' stereo-samples (L,R afwisselend, -1..1)
bool init(std::function<void(float* out, int frames)> mix);
void shutdown();
constexpr int SAMPLE_RATE = 44100;
}
