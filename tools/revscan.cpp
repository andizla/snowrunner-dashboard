// revscan.cpp: a research tool for the engine sound. It reads the running SnowRunner.exe from outside with
// ReadProcessMemory and writes nothing into the game. 20 times a second it takes every float of the driven truck's
// vehicle, its drive train (the action), the game's truck control and the three objects the control points to at
// + 0x70, + 0x78 and + 0x80 (its sounds), together with the pedal, the gear, the speed and the engine speed the mod
// publishes (src\state.h). When the game ends, the time is up or a file <out>.stop appears, it says which of those
// floats move like an engine's revs: in neutral at a standstill with the engine running, how well each follows the
// pedal (smoothed, as revs lag the foot), and over the whole run how well it follows the mod's engine speed.
//   Build: cl /nologo /O2 /W4 /EHsc /std:c++17 /Fe:out\test\revscan.exe tools\revscan.cpp
//   Run:   out\test\revscan.exe <out, without an ending> [seconds of driving to record, default 300]
// It waits for the game and for a truck to be driven, and writes <out>.txt (the report) and <out>.csv (the pedal and
// the best candidates over time). The drive it needs: the engine running in neutral at a standstill, the pedal
// blipped a few times, held half way, held to the floor and let go, for a minute; then a minute of driving.
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <string>
#include <vector>

static HANDLE g_proc = nullptr;
static uint64_t g_base = 0;

template <typename T> static bool Rd(uint64_t addr, T &out)
{
    SIZE_T got = 0;
    return addr >= 0x10000 && ReadProcessMemory(g_proc, (void *)addr, &out, sizeof out, &got) && got == sizeof out;
}

// A block that may end early or have holes: page by page, zeros where nothing can be read.
static void RdBlock(uint64_t addr, BYTE *out, size_t size)
{
    memset(out, 0, size);
    if (addr < 0x10000) return;
    SIZE_T got = 0;
    if (ReadProcessMemory(g_proc, (void *)addr, out, size, &got) && got == size) return;
    for (size_t o = 0; o < size;)
    {
        const size_t chunk = min(size - o, (size_t)(0x1000 - ((addr + o) & 0xFFF)));
        ReadProcessMemory(g_proc, (void *)(addr + o), out + o, chunk, &got);
        o += chunk;
    }
}

// Only the game itself counts: the exe under a Snowrunner\Sources\Bin folder.
static bool IsTheGame(DWORD pid)
{
    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    const HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return false;
    const bool ok = QueryFullProcessImageNameW(p, 0, path, &size) != 0;
    CloseHandle(p);
    if (!ok) return false;
    _wcslwr_s(path);
    return wcsstr(path, L"\\snowrunner\\sources\\bin\\snowrunner.exe") != nullptr;
}

static DWORD FindGame()
{
    DWORD pid = 0;
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W e = { sizeof e };
    if (Process32FirstW(snap, &e))
        do { if (!_wcsicmp(e.szExeFile, L"SnowRunner.exe") && IsTheGame(e.th32ProcessID)) pid = e.th32ProcessID; } while (!pid && Process32NextW(snap, &e));
    CloseHandle(snap);
    return pid;
}

static uint64_t ExeBase(DWORD pid)
{
    uint64_t base = 0;
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W m = { sizeof m };
    if (Module32FirstW(snap, &m))
        do { if (!_wcsicmp(m.szModule, L"SnowRunner.exe")) base = (uint64_t)m.modBaseAddr; } while (!base && Module32NextW(snap, &m));
    CloseHandle(snap);
    return base;
}

// The blocks read on every sample, one after the other in a sample's row of floats.
struct Block { const char *name; size_t size; size_t first; uint64_t at; uint64_t seenAt; };
static Block g_blocks[] = { { "vehicle", 0x1000 }, { "action", 0x200 }, { "control", 0x400 }, { "sound70", 0x400 }, { "sound78", 0x400 }, { "sound80", 0x400 } };
enum { kVehicle, kAction, kControl, kSound70, kSound78, kSound80, kBlocks };

struct Sample { float t, pedal, speed, modRpm; int gear; bool engine; };

static double Correlation(const std::vector<float> &a, const std::vector<float> &b)
{
    const size_t n = a.size();
    if (n < 3) return 0.0;
    double ma = 0.0, mb = 0.0;
    for (size_t i = 0; i < n; i++) { ma += a[i]; mb += b[i]; }
    ma /= (double)n;
    mb /= (double)n;
    double sab = 0.0, saa = 0.0, sbb = 0.0;
    for (size_t i = 0; i < n; i++)
    {
        const double x = a[i] - ma, y = b[i] - mb;
        sab += x * y;
        saa += x * x;
        sbb += y * y;
    }
    return saa > 0.0 && sbb > 0.0 ? sab / sqrt(saa * sbb) : 0.0;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 2) { printf("revscan <out, without an ending> [seconds of driving, default 300]\n"); return 1; }
    const std::wstring out = argv[1];
    const int seconds = argc > 2 ? _wtoi(argv[2]) : 300;
    size_t slots = 0;
    for (Block &b : g_blocks) { b.first = slots; slots += b.size / 4; }

    printf("waiting for SnowRunner.exe\n");
    fflush(stdout);
    DWORD pid = 0;
    while (!pid) { pid = FindGame(); if (!pid) Sleep(500); }
    g_proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!g_proc) { printf("OpenProcess failed: %lu\n", GetLastError()); return 1; }
    for (int i = 0; i < 240 && !g_base; i++) { g_base = ExeBase(pid); if (!g_base) Sleep(250); }
    DWORD header = 0, stamp = 0;
    for (int i = 0; i < 240 && !(Rd(g_base + 0x3C, header) && Rd(g_base + header + 8, stamp) && stamp); i++) Sleep(250);
    printf("game pid %lu, exe stamp %08lx\n", pid, stamp);
    fflush(stdout);
    if (stamp != 0x6a607c05) { printf("not the build the addresses are from\n"); return 1; }

    // the mod's own engine speed, where the mod publishes it
    wchar_t stateName[64];
    swprintf(stateName, 64, L"Local\\SnowRunnerGauges.State.%lu", pid);
    HANDLE stateMap = nullptr;
    const volatile uint8_t *state = nullptr;

    std::vector<Sample> samples;
    std::vector<float> rows; // samples x slots
    std::vector<BYTE> raw;
    const std::wstring stopFile = out + L".stop";
    ULONGLONG t0 = 0, said = 0;
    std::string notes;
    for (;;)
    {
        if (WaitForSingleObject(g_proc, 50) != WAIT_TIMEOUT) { printf("the game ended\n"); break; }
        if (GetFileAttributesW(stopFile.c_str()) != INVALID_FILE_ATTRIBUTES) { printf("stopped by %ls\n", stopFile.c_str()); break; }
        const ULONGLONG now = GetTickCount64();
        if (!state)
        {
            if (!stateMap) stateMap = OpenFileMappingW(FILE_MAP_READ, FALSE, stateName);
            if (stateMap) state = (const volatile uint8_t *)MapViewOfFile(stateMap, FILE_MAP_READ, 0, 0, 64);
        }
        uint64_t control = 0, vehicle = 0, action = 0, s70 = 0, s78 = 0, s80 = 0;
        if (!Rd(g_base + 0x2a8eb78, control) || !Rd(control + 8, vehicle) || !vehicle || !Rd(vehicle + 0x68, action) || !action) continue;
        Rd(control + 0x70, s70);
        Rd(control + 0x78, s78);
        Rd(control + 0x80, s80);
        const uint64_t at[kBlocks] = { vehicle, action, control, s70, s78, s80 };
        for (int b = 0; b < kBlocks; b++)
        {
            g_blocks[b].at = at[b];
            if (at[b] != g_blocks[b].seenAt)
            {
                uint64_t vt = 0;
                Rd(at[b], vt);
                char line[160];
                sprintf_s(line, "%s at %p, its first 8 bytes %016llx%s\n", g_blocks[b].name, (void *)at[b], vt,
                          vt >= g_base && vt < g_base + 0x3000000 ? " (a pointer into the exe: a class)" : "");
                notes += line;
                if (vt >= g_base && vt < g_base + 0x3000000) { sprintf_s(line, "    class at exe + 0x%llx\n", vt - g_base); notes += line; }
                g_blocks[b].seenAt = at[b];
            }
        }
        if (!t0) { t0 = now; printf("a truck is driven: recording for up to %d s\n", seconds); }
        Sample s = {};
        s.t = (float)(now - t0) / 1000.0f;
        float throttle = 0.0f, velocity[3] = {}, forward[3] = {};
        uint8_t flags = 0;
        uint64_t model = 0, chassis = 0;
        Rd(action + 0x44, throttle);
        Rd(action + 0x74, s.gear);
        Rd(vehicle + 0x768, flags);
        if (Rd(vehicle + 0x60, model) && Rd(model + 0x230, chassis) && Rd(chassis + 0x230, velocity) && Rd(chassis + 0x170, forward))
            s.speed = velocity[0] * forward[0] + velocity[1] * forward[1] + velocity[2] * forward[2];
        s.pedal = min(1.0f, fabsf(throttle));
        s.engine = (flags & 1) != 0;
        if (state)
        {
            // the block while its sequence is even and unchanged; the engine speed is the float at 40
            for (int tries = 0; tries < 4; tries++)
            {
                long before, after;
                float rpm;
                memcpy(&before, (const void *)(state + 12), 4);
                memcpy(&rpm, (const void *)(state + 40), 4);
                memcpy(&after, (const void *)(state + 12), 4);
                if (!(before & 1) && before == after) { s.modRpm = rpm; break; }
            }
        }
        const size_t row = rows.size();
        rows.resize(row + slots);
        for (int b = 0; b < kBlocks; b++)
        {
            raw.resize(g_blocks[b].size);
            RdBlock(at[b], raw.data(), raw.size());
            memcpy(&rows[row + g_blocks[b].first], raw.data(), raw.size());
        }
        samples.push_back(s);
        if (now - said >= 5000)
        {
            said = now;
            printf("%5.0f s: pedal %.2f, gear %d, %.1f m/s, engine %s, the mod's %.0f rpm\n", s.t, s.pedal, s.gear, s.speed, s.engine ? "on" : "off", s.modRpm);
            fflush(stdout);
        }
        if (s.t >= (float)seconds) { printf("time is up\n"); break; }
    }

    const size_t n = samples.size();
    FILE *f = nullptr;
    if (_wfopen_s(&f, (out + L".txt").c_str(), L"w") || !f) { printf("cannot write the report\n"); return 1; }
    fprintf(f, "revscan: %zu samples over %.0f s, %zu floats each\n%s", n, n ? samples.back().t : 0.0f, slots, notes.c_str());
    // the pedal as revs would follow it, at three lags
    static const float kLags[3] = { 0.15f, 0.4f, 0.8f };
    std::vector<float> proxy[3];
    for (int k = 0; k < 3; k++)
    {
        float v = 0.0f, last = 0.0f;
        for (size_t i = 0; i < n; i++)
        {
            const float dt = i ? min(0.5f, samples[i].t - last) : 0.05f;
            last = samples[i].t;
            v += (samples[i].pedal - v) * (1.0f - expf(-dt / kLags[k]));
            proxy[k].push_back(v);
        }
    }
    // in neutral at a standstill with the engine running, and with the engine running at all
    std::vector<size_t> idle, running;
    for (size_t i = 0; i < n; i++)
    {
        if (!samples[i].engine) continue;
        running.push_back(i);
        if (samples[i].gear == 0 && fabsf(samples[i].speed) < 0.5f) idle.push_back(i);
    }
    float pedalLow = 1.0f, pedalHigh = 0.0f;
    for (const size_t i : idle) { pedalLow = min(pedalLow, samples[i].pedal); pedalHigh = max(pedalHigh, samples[i].pedal); }
    fprintf(f, "%zu samples with the engine running, %zu of them in neutral at a standstill (the pedal there from %.2f to %.2f)\n", running.size(), idle.size(),
            idle.empty() ? 0.0f : pedalLow, idle.empty() ? 0.0f : pedalHigh);
    struct Found { size_t slot; double r; int lag; float low, high, footOff, floored; int values; double withMod; };
    std::vector<Found> found;
    if (idle.size() >= 100 && pedalHigh - pedalLow > 0.5f)
    {
        std::vector<float> x(idle.size()), p(idle.size());
        for (size_t slot = 0; slot < slots; slot++)
        {
            bool sane = true;
            float low = 1e30f, high = -1e30f;
            double offSum = 0.0, onSum = 0.0;
            int offCount = 0, onCount = 0, values = 1;
            for (size_t j = 0; j < idle.size() && sane; j++)
            {
                const float v = rows[idle[j] * slots + slot];
                sane = isfinite(v) && fabsf(v) < 1e7f && (v == 0.0f || fabsf(v) > 1e-6f);
                x[j] = v;
                low = min(low, v);
                high = max(high, v);
                if (j && v != x[j - 1]) values++;
                if (samples[idle[j]].pedal < 0.05f) { offSum += v; offCount++; }
                if (samples[idle[j]].pedal > 0.9f) { onSum += v; onCount++; }
            }
            if (!sane || high - low < 1e-4f || values < 20) continue;
            Found best = { slot, 0.0, 0, low, high, offCount ? (float)(offSum / offCount) : 0.0f, onCount ? (float)(onSum / onCount) : 0.0f, values, 0.0 };
            for (int k = 0; k < 3; k++)
            {
                for (size_t j = 0; j < idle.size(); j++) p[j] = proxy[k][idle[j]];
                const double r = Correlation(x, p);
                if (fabs(r) > fabs(best.r)) { best.r = r; best.lag = k; }
            }
            if (fabs(best.r) < 0.6) continue;
            // and against the mod's own engine speed over the whole run, where the mod publishes one
            std::vector<float> all(running.size()), mod(running.size());
            for (size_t j = 0; j < running.size(); j++) { all[j] = rows[running[j] * slots + slot]; mod[j] = samples[running[j]].modRpm; }
            bool allSane = true;
            for (const float v : all) allSane = allSane && isfinite(v) && fabsf(v) < 1e7f;
            best.withMod = allSane ? Correlation(all, mod) : 0.0;
            found.push_back(best);
        }
        std::sort(found.begin(), found.end(), [](const Found &a, const Found &b) { return fabs(a.r) > fabs(b.r); });
        fprintf(f, "\nfloats that follow the pedal in neutral (|r| from 0.6), the best first:\n"
                   "  where                  r     lag     least      most   foot off   floored  changes  r with the mod's rpm, whole run\n");
        for (size_t i = 0; i < found.size() && i < 80; i++)
        {
            const Found &c = found[i];
            int b = kBlocks - 1;
            while (b > 0 && c.slot < g_blocks[b].first) b--;
            fprintf(f, "  %-8s + 0x%-4zX  %+.3f  %.2f s  %9.4g %9.4g  %9.4g %9.4g  %7d  %+.3f\n", g_blocks[b].name, (c.slot - g_blocks[b].first) * 4, c.r, kLags[c.lag], c.low, c.high,
                    c.footOff, c.floored, c.values, c.withMod);
        }
        if (found.empty()) fprintf(f, "  none\n");
    }
    else fprintf(f, "\nnot enough of the run was in neutral at a standstill with the pedal both up and down: nothing to say\n");
    fclose(f);

    // the pedal and the twelve best over time, for a look by eye
    if (_wfopen_s(&f, (out + L".csv").c_str(), L"w") == 0 && f)
    {
        fprintf(f, "seconds,pedal,gear,speed,engine,mod_rpm");
        const size_t most = min(found.size(), (size_t)12);
        for (size_t i = 0; i < most; i++)
        {
            int b = kBlocks - 1;
            while (b > 0 && found[i].slot < g_blocks[b].first) b--;
            fprintf(f, ",%s+0x%zX", g_blocks[b].name, (found[i].slot - g_blocks[b].first) * 4);
        }
        fprintf(f, "\n");
        for (size_t i = 0; i < n; i++)
        {
            fprintf(f, "%.2f,%.3f,%d,%.2f,%d,%.0f", samples[i].t, samples[i].pedal, samples[i].gear, samples[i].speed, samples[i].engine ? 1 : 0, samples[i].modRpm);
            for (size_t k = 0; k < most; k++) fprintf(f, ",%g", rows[i * slots + found[k].slot]);
            fprintf(f, "\n");
        }
        fclose(f);
    }
    printf("wrote %ls.txt and %ls.csv: %zu samples, %zu candidates\n", out.c_str(), out.c_str(), n, found.size());
    return 0;
}
