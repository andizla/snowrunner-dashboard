// sound.h: what the turbo sounds like when the pedal comes up quickly under boost.
//   A blow-off: the valve letting the charge air go, after a full lift.
//   A flutter: the air beating back through the compressor wheel, after a smaller lift.
// gauges.cpp asks for one when models.h tells a lift (SoundPlay); a thread here plays it.
//
// Where each sound comes from, in this order:
//   a WAV file sounds\blowoff.wav or sounds\flutter.wav next to the mod (PCM of 8 to 32 bits or 32-bit float, any
//     number of channels, played as one)
//   the recording the mod's file carries, where it carries one (src\sounds.rc)
//   a sound made by code (SoundMakeBlowOff, SoundMakeFlutter)
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

void GaugeLog(const wchar_t *fmt, ...); // a line in Gauges.log (gauges.cpp)

enum { kSoundBlowOff, kSoundFlutter, kSoundCount };

struct SoundClip
{
    std::vector<float> x; // one channel
    uint32_t rate = 0;
};

static const float kSoundRms = 0.1f;       // every sound's loudest tenth of a second is brought to this loudness (RMS)...
static const float kSoundPeak = 0.6f;      // ...and down again where its peaks would pass this
static const float kSoundMostPitch = 2.0f; // the fastest a voice can be asked to play
static const uint64_t kSoundMostBytes = 16ull << 20;
static const float kSoundMostSeconds = 4.0f; // a longer file is cut here: these are short sounds
enum { kSoundVoicesEach = 2, kSoundShots = 8 }; // voices per sound, so one that still rings is not cut by the next; shots that can wait

// What each sound is: its name in the log, the file that replaces it, its name in src\sounds.rc, and how loud it is
// at SoundVolume=100 with the turbo at its full boost (against kSoundRms). Both can ring together: their levels
// times kSoundPeak, twice each, stay below 1.
struct SoundKind { const wchar_t *what, *file, *carried; float level; };
static const SoundKind kSoundKinds[kSoundCount] = { { L"blow-off", L"sounds\\blowoff.wav", L"BLOWOFF", 0.45f }, { L"flutter", L"sounds\\flutter.wav", L"FLUTTER", 0.36f } };

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

// One sound: the player's file, else the mod's own recording, else made by code (the top of this file has the rules).
// dir: the folder the mod is in, with its last backslash.
inline void SoundLoad(int kind, const std::wstring &dir, SoundClip &clip)
{
    const SoundKind &k = kSoundKinds[kind];
    clip = SoundClip{};
    std::vector<uint8_t> file;
    std::wstring why;
    const std::wstring path = dir + k.file;
    bool got = false;
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        got = SoundFile(path, file, why) && SoundDecode(file, clip, why);
        if (got) GaugeLog(L"sound: %s = %s (%.2f s, %u Hz)", k.what, k.file, (double)clip.x.size() / clip.rate, clip.rate);
        else GaugeLog(L"sound: %s: %s is not used (%s)", k.what, k.file, why.c_str());
    }
    if (!got && SoundCarried(k.carried, file))
    {
        got = SoundDecode(file, clip, why);
        if (got) GaugeLog(L"sound: %s = the mod's own recording (%.2f s, %u Hz)", k.what, (double)clip.x.size() / clip.rate, clip.rate);
        else GaugeLog(L"sound: %s: the mod's own recording could not be read (%s)", k.what, why.c_str());
    }
    if (!got)
    {
        clip = SoundClip{};
        if (kind == kSoundBlowOff) SoundMakeBlowOff(clip);
        else SoundMakeFlutter(clip);
        GaugeLog(L"sound: %s = made by the mod's code (%.2f s)", k.what, (double)clip.x.size() / clip.rate);
    }
    SoundLevel(clip);
}

// A sound asked for: which, how loud of 1 (the setting and the boost together, before the sound's own level) and
// how fast (1 = as recorded).
struct SoundShot { int kind; float gain, pitch; };
static SoundShot g_soundShots[kSoundShots];
static int g_soundWaiting = 0;
static std::wstring g_soundDir;
static HANDLE g_soundWake = nullptr;
static volatile LONG g_soundOpens = 0, g_soundStarts[kSoundCount] = {}; // for the test

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
    IXAudio2SourceVoice *voices[kSoundCount][kSoundVoicesEach] = {};
    int next[kSoundCount] = {}; // the voice a sound's next shot takes
};

inline void SoundClose(SoundEngine &e)
{
    for (auto &kind : e.voices)
        for (IXAudio2SourceVoice *&v : kind)
        {
            if (v) v->DestroyVoice();
            v = nullptr;
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

// An engine on the default device with the voices of each sound.
inline bool SoundOpen(SoundEngine &e, const SoundClip *clips, HRESULT &hr, const wchar_t *&from)
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
    for (int k = 0; SUCCEEDED(hr) && k < kSoundCount; k++)
    {
        if (clips[k].x.empty()) continue;
        WAVEFORMATEX format = {};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = 1;
        format.nSamplesPerSec = clips[k].rate;
        format.wBitsPerSample = 32;
        format.nBlockAlign = 4;
        format.nAvgBytesPerSec = clips[k].rate * 4;
        for (int i = 0; SUCCEEDED(hr) && i < kSoundVoicesEach; i++) hr = e.xaudio->CreateSourceVoice(&e.voices[k][i], &format, 0, kSoundMostPitch);
    }
    if (FAILED(hr)) { SoundClose(e); return false; }
    return true;
}

static DWORD WINAPI SoundThread(void *)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SoundClip clips[kSoundCount];
    for (int k = 0; k < kSoundCount; k++) SoundLoad(k, g_soundDir, clips[k]);
    SoundEngine e;
    ULONGLONG quietSince = 0, againAt = 0;
    HRESULT lastError = S_OK;
    bool said = false;
    for (;;)
    {
        // with no engine there is nothing to do until a sound is asked for
        WaitForSingleObject(g_soundWake, e.xaudio ? 50 : INFINITE);
        SoundShot shots[kSoundShots];
        AcquireSRWLockExclusive(&g_soundLock);
        const int waiting = g_soundWaiting;
        for (int i = 0; i < waiting; i++) shots[i] = g_soundShots[i];
        g_soundWaiting = 0;
        ReleaseSRWLockExclusive(&g_soundLock);
        const ULONGLONG ms = GetTickCount64();
        if (!e.xaudio)
        {
            // a sound that finds no engine is played now or never: one that waited would come seconds after its lift
            if (!waiting || ms < againAt) continue;
            HRESULT hr = S_OK;
            const wchar_t *from = L"";
            if (!SoundOpen(e, clips, hr, from))
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
        for (int i = 0; i < waiting; i++)
        {
            const SoundShot &s = shots[i];
            IXAudio2SourceVoice *const v = e.voices[s.kind][e.next[s.kind]];
            if (!v) continue;
            e.next[s.kind] = (e.next[s.kind] + 1) % kSoundVoicesEach;
            XAUDIO2_BUFFER b = {};
            b.Flags = XAUDIO2_END_OF_STREAM;
            b.AudioBytes = (UINT32)(clips[s.kind].x.size() * sizeof(float));
            b.pAudioData = (const BYTE *)clips[s.kind].x.data();
            v->Stop(0);
            v->FlushSourceBuffers();
            v->SetVolume(min(1.0f, max(0.0f, s.gain)) * kSoundKinds[s.kind].level);
            v->SetFrequencyRatio(min(kSoundMostPitch, max(0.25f, s.pitch)));
            if (SUCCEEDED(v->SubmitSourceBuffer(&b)) && SUCCEEDED(v->Start(0))) InterlockedIncrement(&g_soundStarts[s.kind]);
        }
        bool any = false;
        for (auto &kind : e.voices)
            for (IXAudio2SourceVoice *v : kind)
            {
                XAUDIO2_VOICE_STATE state = {};
                if (v) v->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
                any = any || state.BuffersQueued != 0;
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

// Asks for a sound, from any thread: which, how loud of 1 and how fast. One that finds kSoundShots waiting is dropped.
inline void SoundPlay(int kind, float gain, float pitch)
{
    if (!g_soundWake || kind < 0 || kind >= kSoundCount || !(gain > 0.0f)) return;
    AcquireSRWLockExclusive(&g_soundLock);
    if (g_soundWaiting < kSoundShots) g_soundShots[g_soundWaiting++] = { kind, gain, pitch };
    ReleaseSRWLockExclusive(&g_soundLock);
    SetEvent(g_soundWake);
}
