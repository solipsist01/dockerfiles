#include "audio.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <thread>
#include <vector>

namespace {
const int BUFFERS = 4;
const int FRAMES = 1024;  // ~23 ms per buffer
HWAVEOUT g_wave = nullptr;
HANDLE g_event = nullptr;
WAVEHDR g_hdr[BUFFERS];
std::vector<int16_t> g_data[BUFFERS];
std::thread g_thread;
std::atomic<bool> g_run{false};
std::function<void(float*, int)> g_mix;

void fill(int i) {
    static std::vector<float> f(FRAMES * 2);
    std::fill(f.begin(), f.end(), 0.0f);
    if (g_mix) g_mix(f.data(), FRAMES);
    for (int k = 0; k < FRAMES * 2; k++) {
        float s = f[k];
        s = s > 1 ? 1 : (s < -1 ? -1 : s);
        g_data[i][k] = (int16_t)(s * 32000);
    }
    g_hdr[i].dwFlags &= ~WHDR_DONE;
    waveOutWrite(g_wave, &g_hdr[i], sizeof(WAVEHDR));
}

void loop() {
    while (g_run) {
        WaitForSingleObject(g_event, 50);
        for (int i = 0; i < BUFFERS; i++)
            if (g_hdr[i].dwFlags & WHDR_DONE) fill(i);
    }
}
}  // namespace

namespace Audio {
bool init(std::function<void(float*, int)> mix) {
    g_mix = std::move(mix);
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = SAMPLE_RATE;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4;
    wf.nAvgBytesPerSec = SAMPLE_RATE * 4;
    g_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (waveOutOpen(&g_wave, WAVE_MAPPER, &wf, (DWORD_PTR)g_event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) return false;
    for (int i = 0; i < BUFFERS; i++) {
        g_data[i].assign(FRAMES * 2, 0);
        g_hdr[i] = {};
        g_hdr[i].lpData = (LPSTR)g_data[i].data();
        g_hdr[i].dwBufferLength = FRAMES * 4;
        waveOutPrepareHeader(g_wave, &g_hdr[i], sizeof(WAVEHDR));
        g_hdr[i].dwFlags |= WHDR_DONE;
    }
    g_run = true;
    for (int i = 0; i < BUFFERS; i++) fill(i);
    g_thread = std::thread(loop);
    return true;
}

void shutdown() {
    if (!g_run) return;
    g_run = false;
    SetEvent(g_event);
    if (g_thread.joinable()) g_thread.join();
    waveOutReset(g_wave);
    for (int i = 0; i < BUFFERS; i++) waveOutUnprepareHeader(g_wave, &g_hdr[i], sizeof(WAVEHDR));
    waveOutClose(g_wave);
    CloseHandle(g_event);
}
}  // namespace Audio
