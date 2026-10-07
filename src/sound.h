// sound.h: what the turbo sounds like when the pedal comes up quickly under boost: the air beating back through the
// compressor wheel, a flutter. It comes in three strengths, by how much boost the lift had (kTurboHigh, kTurboMid,
// kTurboLow), and in up to kSoundVariants recordings of each, of which one is played and never the same twice
// running. gauges.cpp asks for one when models.h tells a lift (SoundPlay) and for silence when the pedal goes down
// again (SoundQuiet); a thread here plays it.
//
// Where the sounds of a strength come from:
//   WAV files sounds\turbo_high_1.wav to _4.wav (turbo_mid_, turbo_low_) next to the mod, where there is at least
//     one (PCM of 8 to 32 bits or 32-bit float, any number of channels, played as one)
//   else the recordings the mod's file carries (src\sounds.rc; SOUND_CREDITS.md says whose they are)
//   else a sound made by code (SoundMakeBlowOff for the strong one, SoundMakeFlutter for the others)
// Every sound is brought to the same loudness, so a file of the player's is as loud as the mod's own.
//
// Played through XAudio2 2.9: the xaudio2_9redist.dll the game brings and uses itself, or Windows' own xaudio2_9.dll.
// The engine only exists while a sound plays and for a few seconds after. In between the mod holds no audio device,
// so a default device that changes needs no handling. Without XAudio2 or without a device there is no sound and
// nothing else changes.
#pragma once
#include <windows.h>
#include <objbase.h>
#include <xaudio2.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include "gauges.h"

struct SoundClip
{
    std::vector<float> x; // one channel
    uint32_t rate = 0;
};

static const float kSoundRms = 0.1f;       // every sound's loudest tenth of a second is brought to this loudness (RMS)...
static const float kSoundPeak = 0.6f;      // ...and down again where its peaks would pass this
static const float kSoundMostPitch = 2.0f; // the fastest a voice can be asked to play
static const uint64_t kSoundMostBytes = 16ull << 20;
static const float kSoundMostSeconds = 6.0f; // a longer file is cut here
enum { kSoundVariants = 4, kSoundShots = 8 }; // recordings of one strength at most; shots that can wait

// The three strengths: the name in the log, and what the files next to the mod and the recordings in src\sounds.rc
// are called (with _1 to _4 after it).
struct SoundTier { const wchar_t *what, *file, *carried; };
static const SoundTier kSoundTiers[kTurboTiers] = { { L"the turbo at full boost", L"turbo_high", L"TURBO_HIGH" }, { L"the turbo at half boost", L"turbo_mid", L"TURBO_MID" },
                                                    { L"the turbo at little boost", L"turbo_low", L"TURBO_LOW" } };
static const float kSoundLevel = 0.45f; // how loud a sound is at SoundVolume=100 with the turbo at its full boost (against kSoundRms): two that ring together stay below 1
static const float kSoundHush = 0.08f;  // seconds a sound that still rings takes to go when the pedal goes down again

// A WAV file (PCM of 8 to 32 bits or 32-bit float) as one channel of floats. why: what was wrong with it.
inline bool SoundDecode(const std::vector<uint8_t> &file, SoundClip &out, std::wstring &why)
{
    const uint8_t *f = file.data();
    const size_t n = file.size();
    auto u16 = [&](size_t o) { return (uint32_t)(f[o] | f[o + 1] << 8); };
    auto u32 = [&](size_t o) { return (uint32_t)f[o] | (uint32_t)f[o + 1] << 8 | (uint32_t)f[o + 2] << 16 | (uint32_t)f[o + 3] << 24; };
    if (n < 12 || memcmp(f, "RIFF", 4) || memcmp(f + 8, "WAVE", 4)) { why = L"it is not a WAV file"; return false; }
    size_t fmt = 0, fmtSize = 0, data = 0, dataSize = 0;
    for (size_t p = 12; p + 8 <= n;)
    {
        const size_t size = u32(p + 4), room = n - p - 8;
        if (!memcmp(f + p, "fmt ", 4)) { fmt = p + 8; fmtSize = min(size, room); }
        else if (!memcmp(f + p, "data", 4)) { data = p + 8; dataSize = min(size, room); }
        p += 8 + size + (size & 1);
    }
    if (!fmt || fmtSize < 16 || !data || !dataSize) { why = L"it has no format or no samples"; return false; }
    uint32_t tag = u16(fmt);
    const uint32_t channels = u16(fmt + 2), rate = u32(fmt + 4), align = u16(fmt + 12), bits = u16(fmt + 14), bytes = bits / 8;
    if (tag == 0xFFFE && fmtSize >= 26) tag = u16(fmt + 24); // the extensible format: its sub format starts with the tag
    if ((tag != 1 && tag != 3) || !channels || channels > 8 || rate < 4000 || rate > 192000 || bits % 8 || bytes < 1 || bytes > 4 || (tag == 3 && bytes != 4) ||
        align < bytes * channels)
    {
        why = L"its format is not one the mod reads (PCM of 8 to 32 bits and 32-bit float are)";
        return false;
    }
    const size_t frames = min(dataSize / align, (size_t)((float)rate * kSoundMostSeconds));
    if (frames < rate / 20) { why = L"it is shorter than a twentieth of a second"; return false; }
    out.x.resize(frames);
    for (size_t i = 0; i < frames; i++)
    {
        float sum = 0.0f;
        for (uint32_t c = 0; c < channels; c++)
        {
            const size_t o = data + i * align + (size_t)c * bytes;
            float v = 0.0f;
            if (tag == 3) memcpy(&v, f + o, 4);
            else if (bytes == 1) v = (float)((int)f[o] - 128) / 128.0f;
            else if (bytes == 2) v = (float)(int16_t)u16(o) / 32768.0f;
            else if (bytes == 3) v = (float)(int)(f[o] | f[o + 1] << 8 | (int)(int8_t)f[o + 2] * 65536) / 8388608.0f;
            else v = (float)(int32_t)u32(o) / 2147483648.0f;
            sum += isfinite(v) ? max(-4.0f, min(4.0f, v)) : 0.0f;
        }
        out.x[i] = sum / (float)channels;
    }
    out.rate = rate;
    return true;
}

inline bool SoundFile(const std::wstring &path, std::vector<uint8_t> &out, std::wstring &why)
{
    const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { why = L"the file could not be opened"; return false; }
    LARGE_INTEGER size = {};
    DWORD got = 0;
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart > 0 && (uint64_t)size.QuadPart <= kSoundMostBytes;
    if (ok)
    {
        out.resize((size_t)size.QuadPart);
        ok = ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr) && got == out.size();
    }
    CloseHandle(h);
    if (!ok) why = L"the file could not be read, or is larger than 16 MB";
    return ok;
}

struct SoundBiquad { double b0, b1, b2, a1, a2; };
enum { kSoundHighPass, kSoundBandPass };

inline SoundBiquad SoundFilter(int kind, double hz, double q, double rate)
{
    const double w = 2.0 * 3.14159265358979323846 * hz / rate, c = cos(w), a = sin(w) / (2.0 * q), a0 = 1.0 + a;
    if (kind == kSoundHighPass) return { (1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, -2.0 * c / a0, (1.0 - a) / a0 };
    return { a / a0, 0.0, -a / a0, -2.0 * c / a0, (1.0 - a) / a0 }; // band pass, as loud as the input at its centre
}

inline std::vector<float> SoundFiltered(const std::vector<float> &x, const SoundBiquad &k)
{
    std::vector<float> y(x.size());
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    for (size_t i = 0; i < x.size(); i++)
    {
        const double v = k.b0 * x[i] + k.b1 * x1 + k.b2 * x2 - k.a1 * y1 - k.a2 * y2;
        x2 = x1;
        x1 = x[i];
        y2 = y1;
        y1 = v;
        y[i] = (float)v;
    }
    return y;
}

// The same numbers on every start: -1 .. 1.
struct SoundNoise
{
    uint32_t s;
    float operator()()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float)((double)s / 4294967296.0 * 2.0 - 1.0);
    }
};

// A blow-off valve, made by code: charge air let go at once. A burst of noise between 1.5 and 6 kHz that opens in
// a few milliseconds, holds for a moment and dies away in a quarter of a second, a little lower in tone as it goes.
inline void SoundMakeBlowOff(SoundClip &c)
{
    const double rate = 44100.0;
    const size_t n = (size_t)(rate * 0.7);
    SoundNoise noise = { 0xb10ff };
    std::vector<float> x(n);
    for (float &v : x) v = noise();
    const std::vector<float> high = SoundFiltered(x, SoundFilter(kSoundBandPass, 4200.0, 0.7, rate)), low = SoundFiltered(x, SoundFilter(kSoundBandPass, 1900.0, 0.9, rate));
    std::vector<float> y(n);
    for (size_t i = 0; i < n; i++)
    {
        const float t = (float)i / (float)rate, open = min(1.0f, t / 0.006f), fade = t < 0.07f ? 1.0f : expf(-(t - 0.07f) / 0.13f), lower = min(1.0f, t / 0.4f);
        y[i] = (high[i] * (1.0f - 0.6f * lower) + low[i] * (0.7f + 0.6f * lower)) * open * fade;
    }
    c.x = SoundFiltered(y, SoundFilter(kSoundHighPass, 700.0, 0.707, rate));
    c.rate = 44100;
}

// A turbo's flutter, made by code: the air beating back through the compressor wheel. Short puffs of noise around
// 1.3 kHz, 26 a second at first and 13 at the end, each weaker than the one before, over half a second.
inline void SoundMakeFlutter(SoundClip &c)
{
    const double rate = 44100.0;
    const size_t n = (size_t)(rate * 0.6);
    SoundNoise noise = { 0xf1077e4 };
    std::vector<float> x(n);
    double phase = 0.0;
    for (size_t i = 0; i < n; i++)
    {
        const double t = (double)i / rate, hz = 26.0 - 13.0 * min(1.0, t / 0.5);
        phase += hz / rate;
        const double in = phase - floor(phase);       // how far into its puff: each opens at once and is over in a third of its time
        x[i] = (float)(noise() * exp(-in * 9.0) * exp(-t / 0.22));
    }
    std::vector<float> y = SoundFiltered(x, SoundFilter(kSoundBandPass, 1300.0, 1.1, rate));
    const std::vector<float> air = SoundFiltered(x, SoundFilter(kSoundBandPass, 3400.0, 0.8, rate));
    for (size_t i = 0; i < n; i++) y[i] += 0.35f * air[i];
    c.x = SoundFiltered(y, SoundFilter(kSoundHighPass, 400.0, 0.707, rate));
    c.rate = 44100;
}

// Every sound at the same loudness, whatever it was recorded at, by its loudest tenth of a second, and one with high
// peaks a little quieter. A recording's last 20 ms fade out, so one that was cut does not click.
inline void SoundLevel(SoundClip &c)
{
    const size_t n = c.x.size(), span = min(n, (size_t)c.rate / 10), fade = min(n, (size_t)c.rate / 50);
    if (!n || !span) return;
    for (size_t i = 0; i < fade; i++) c.x[n - 1 - i] *= (float)i / (float)fade;
    double sum = 0.0, most = 0.0;
    for (size_t i = 0; i < n; i++)
    {
        sum += (double)c.x[i] * c.x[i];
        if (i >= span) sum -= (double)c.x[i - span] * c.x[i - span];
        if (i + 1 >= span) most = max(most, sum);
    }
    float k = kSoundRms / max(1e-9f, (float)sqrt(most / (double)span)), peak = 0.0f;
    for (const float v : c.x) peak = max(peak, fabsf(v));
    if (peak * k > kSoundPeak) k = kSoundPeak / peak;
    for (float &v : c.x) v *= k;
}

static SRWLOCK g_soundLock = SRWLOCK_INIT;

// A sound the mod's own file carries (src\sounds.rc), by its name there.
inline bool SoundCarried(const wchar_t *name, std::vector<uint8_t> &out)
{
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&g_soundLock, &self)) return false;
    const HRSRC found = FindResourceW(self, name, (LPCWSTR)RT_RCDATA);
    const HGLOBAL loaded = found ? LoadResource(self, found) : nullptr;
    const uint8_t *bytes = loaded ? (const uint8_t *)LockResource(loaded) : nullptr;
    const DWORD size = found ? SizeofResource(self, found) : 0;
    if (!bytes || !size) return false;
    out.assign(bytes, bytes + size);
    return true;
}

// The recordings of one strength, count of them.
struct SoundSet
{
    SoundClip clip[kSoundVariants];
    int count = 0;
};

// The sounds of one strength (the top of this file has the rules). dir: the folder the mod is in, with its last
// backslash.
inline void SoundLoad(int tier, const std::wstring &dir, SoundSet &set)
{
    const SoundTier &k = kSoundTiers[tier];
    set = SoundSet{};
    std::vector<uint8_t> file;
    std::wstring why;
    // the player's files; where there is none, the mod's own recordings
    for (int own = 0; own < 2 && !set.count; own++)
    {
        for (int v = 1; v <= kSoundVariants; v++)
        {
            wchar_t name[64];
            SoundClip clip;
            if (!own)
            {
                swprintf_s(name, L"sounds\\%s_%d.wav", k.file, v);
                if (GetFileAttributesW((dir + name).c_str()) == INVALID_FILE_ATTRIBUTES) continue;
                if (!SoundFile(dir + name, file, why) || !SoundDecode(file, clip, why)) { GaugeLog(L"sound: %s is not used (%s)", name, why.c_str()); continue; }
                GaugeLog(L"sound: %s = %s (%.2f s, %u Hz)", k.what, name, (double)clip.x.size() / clip.rate, clip.rate);
            }
            else
            {
                swprintf_s(name, L"%s_%d", k.carried, v);
                if (!SoundCarried(name, file)) continue;
                if (!SoundDecode(file, clip, why)) { GaugeLog(L"sound: the mod's own recording %s could not be read (%s)", name, why.c_str()); continue; }
            }
            SoundLevel(clip);
            set.clip[set.count++] = std::move(clip);
        }
        if (own && set.count) GaugeLog(L"sound: %s = %d of the mod's own recordings", k.what, set.count);
    }
    if (set.count) return;
    if (tier == kTurboHigh) SoundMakeBlowOff(set.clip[0]);
    else SoundMakeFlutter(set.clip[0]);
    SoundLevel(set.clip[0]);
    set.count = 1;
    GaugeLog(L"sound: %s = made by the mod's code (%.2f s)", k.what, (double)set.clip[0].x.size() / set.clip[0].rate);
}

// A sound asked for: of which strength, how loud of 1 (the setting and the boost together, before kSoundLevel) and
// how fast (1 = as recorded).
struct SoundShot { int tier; float gain, pitch; };
static SoundShot g_soundShots[kSoundShots];
static int g_soundWaiting = 0;
static volatile LONG g_soundQuietAsked = 0;
static std::wstring g_soundDir;
static HANDLE g_soundWake = nullptr;
static volatile LONG g_soundOpens = 0, g_soundStarts[kTurboTiers] = {}, g_soundHushed = 0; // for the test

// XAudio2 says through this when its device is gone for good; the sound thread then lets go of the engine.
struct SoundEvents : IXAudio2EngineCallback
{
    volatile LONG failed = 0;
    void STDMETHODCALLTYPE OnProcessingPassStart() override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnCriticalError(HRESULT) override { InterlockedExchange(&failed, 1); }
};
static SoundEvents g_soundEvents;

struct SoundEngine
{
    IXAudio2 *xaudio = nullptr;
    IXAudio2MasteringVoice *master = nullptr;
    // one voice per recording. ringing: it was started and has not played out; volume: what it was started at;
    // hush: 1 while it plays as started, on its way to 0 once the pedal went down again
    struct Voice { IXAudio2SourceVoice *voice; bool ringing, hushing; float volume, hush; } voices[kTurboTiers][kSoundVariants] = {};
};

inline void SoundClose(SoundEngine &e)
{
    for (auto &tier : e.voices)
        for (SoundEngine::Voice &v : tier)
        {
            if (v.voice) v.voice->DestroyVoice();
            v = {};
        }
    if (e.master) e.master->DestroyVoice();
    if (e.xaudio)
    {
        e.xaudio->UnregisterForCallbacks(&g_soundEvents);
        e.xaudio->Release();
    }
    e.master = nullptr;
    e.xaudio = nullptr;
}

// An engine on the default device with a voice for each recording.
inline bool SoundOpen(SoundEngine &e, const SoundSet *sets, HRESULT &hr, const wchar_t *&from)
{
    typedef HRESULT(__stdcall *Create)(IXAudio2 **, UINT32, XAUDIO2_PROCESSOR);
    from = L"the game's xaudio2_9redist.dll";
    HMODULE dll = GetModuleHandleW(L"xaudio2_9redist.dll");
    if (!dll) dll = LoadLibraryW((g_soundDir + L"xaudio2_9redist.dll").c_str());
    if (!dll)
    {
        from = L"Windows' xaudio2_9.dll";
        dll = LoadLibraryExW(L"xaudio2_9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    const Create create = dll ? (Create)(void *)GetProcAddress(dll, "XAudio2Create") : nullptr;
    if (!create) { hr = HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND); return false; }
    hr = create(&e.xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr) || !e.xaudio) { e.xaudio = nullptr; return false; }
    InterlockedExchange(&g_soundEvents.failed, 0);
    e.xaudio->RegisterForCallbacks(&g_soundEvents);
    hr = e.xaudio->CreateMasteringVoice(&e.master, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE, 0, nullptr, nullptr, AudioCategory_GameEffects);
    for (int t = 0; SUCCEEDED(hr) && t < kTurboTiers; t++)
        for (int v = 0; SUCCEEDED(hr) && v < sets[t].count; v++)
        {
            WAVEFORMATEX format = {};
            format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
            format.nChannels = 1;
            format.nSamplesPerSec = sets[t].clip[v].rate;
            format.wBitsPerSample = 32;
            format.nBlockAlign = 4;
            format.nAvgBytesPerSec = sets[t].clip[v].rate * 4;
            hr = e.xaudio->CreateSourceVoice(&e.voices[t][v].voice, &format, 0, kSoundMostPitch);
        }
    if (FAILED(hr)) { SoundClose(e); return false; }
    return true;
}

static DWORD WINAPI SoundThread(void *)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SoundSet sets[kTurboTiers];
    for (int t = 0; t < kTurboTiers; t++) SoundLoad(t, g_soundDir, sets[t]);
    SoundEngine e;
    ULONGLONG quietSince = 0, againAt = 0, last = GetTickCount64();
    HRESULT lastError = S_OK;
    bool said = false;
    int played[kTurboTiers] = { -1, -1, -1 }; // the recording each strength played last
    SoundNoise chance = { (uint32_t)GetTickCount() | 1 };
    for (;;)
    {
        // with no engine there is nothing to do until a sound is asked for
        WaitForSingleObject(g_soundWake, e.xaudio ? 10 : INFINITE);
        SoundShot shots[kSoundShots];
        AcquireSRWLockExclusive(&g_soundLock);
        const int waiting = g_soundWaiting;
        for (int i = 0; i < waiting; i++) shots[i] = g_soundShots[i];
        g_soundWaiting = 0;
        ReleaseSRWLockExclusive(&g_soundLock);
        const bool quiet = InterlockedExchange(&g_soundQuietAsked, 0) != 0;
        const ULONGLONG ms = GetTickCount64();
        const float dt = min(0.1f, (float)(ms - last) / 1000.0f);
        last = ms;
        if (!e.xaudio)
        {
            // a sound that finds no engine is played now or never: one that waited would come seconds after its lift
            if (!waiting || ms < againAt) continue;
            HRESULT hr = S_OK;
            const wchar_t *from = L"";
            if (!SoundOpen(e, sets, hr, from))
            {
                againAt = ms + 5000;
                if (hr != lastError) GaugeLog(L"sound: none, as XAudio2 did not start (%08lx from %s); tried again at a later sound", (unsigned long)hr, from);
                lastError = hr;
                continue;
            }
            if (!said) GaugeLog(L"sound: played through %s", from);
            said = true;
            lastError = S_OK;
            quietSince = 0;
            InterlockedIncrement(&g_soundOpens);
        }
        if (InterlockedExchange(&g_soundEvents.failed, 0))
        {
            // the device went away under the engine: let go of it, a later sound starts a new one
            SoundClose(e);
            againAt = ms + 2000;
            continue;
        }
        // the pedal went down again: what rings fades out. A sound asked for on this same pass comes after that.
        if (quiet)
            for (auto &tier : e.voices)
                for (SoundEngine::Voice &v : tier)
                    if (v.ringing && !v.hushing) { v.hushing = true; InterlockedIncrement(&g_soundHushed); }
        for (int i = 0; i < waiting; i++)
        {
            const SoundShot &s = shots[i];
            const int count = sets[s.tier].count;
            if (!count) continue;
            // one of the strength's recordings, and not the one it played last
            int pick = (int)((chance() * 0.5f + 0.5f) * (float)count) % count;
            if (count > 1 && pick == played[s.tier]) pick = (pick + 1) % count;
            played[s.tier] = pick;
            SoundEngine::Voice &v = e.voices[s.tier][pick];
            if (!v.voice) continue;
            const SoundClip &clip = sets[s.tier].clip[pick];
            XAUDIO2_BUFFER b = {};
            b.Flags = XAUDIO2_END_OF_STREAM;
            b.AudioBytes = (UINT32)(clip.x.size() * sizeof(float));
            b.pAudioData = (const BYTE *)clip.x.data();
            v.volume = min(1.0f, max(0.0f, s.gain)) * kSoundLevel;
            v.hush = 1.0f;
            v.hushing = false;
            v.voice->Stop(0);
            v.voice->FlushSourceBuffers();
            v.voice->SetVolume(v.volume);
            v.voice->SetFrequencyRatio(min(kSoundMostPitch, max(0.25f, s.pitch)));
            v.ringing = SUCCEEDED(v.voice->SubmitSourceBuffer(&b)) && SUCCEEDED(v.voice->Start(0));
            if (v.ringing) InterlockedIncrement(&g_soundStarts[s.tier]);
        }
        bool any = false;
        for (auto &tier : e.voices)
            for (SoundEngine::Voice &v : tier)
            {
                if (!v.ringing) continue;
                XAUDIO2_VOICE_STATE state = {};
                v.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
                if (v.hushing) v.hush -= dt / kSoundHush;
                if (!state.BuffersQueued || v.hush <= 0.0f)
                {
                    v.voice->Stop(0);
                    v.voice->FlushSourceBuffers();
                    v.ringing = v.hushing = false;
                    continue;
                }
                if (v.hushing) v.voice->SetVolume(v.volume * v.hush);
                any = true;
            }
        if (any) quietSince = 0;
        else if (!quietSince) quietSince = ms;
        else if (ms - quietSince > 3000) SoundClose(e);
    }
}

// Starts the sound thread, which reads or makes the sounds at once. dir: the folder the mod is in, with its last
// backslash.
inline bool SoundStart(const std::wstring &dir)
{
    if (g_soundWake) return true;
    g_soundDir = dir;
    g_soundWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    const HANDLE t = g_soundWake ? CreateThread(nullptr, 0, SoundThread, nullptr, 0, nullptr) : nullptr;
    if (!t)
    {
        if (g_soundWake) CloseHandle(g_soundWake);
        g_soundWake = nullptr;
        return false;
    }
    CloseHandle(t);
    return true;
}

// Asks for a sound, from any thread: of which strength (kTurboHigh ..), how loud of 1 and how fast. One that finds
// kSoundShots waiting is dropped.
inline void SoundPlay(int tier, float gain, float pitch)
{
    if (!g_soundWake || tier < 0 || tier >= kTurboTiers || !(gain > 0.0f)) return;
    AcquireSRWLockExclusive(&g_soundLock);
    if (g_soundWaiting < kSoundShots) g_soundShots[g_soundWaiting++] = { tier, gain, pitch };
    ReleaseSRWLockExclusive(&g_soundLock);
    SetEvent(g_soundWake);
}

// Lets what still rings fade out, from any thread: the pedal went down again.
inline void SoundQuiet()
{
    if (!g_soundWake) return;
    InterlockedExchange(&g_soundQuietAsked, 1);
    SetEvent(g_soundWake);
}
