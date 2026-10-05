// Gauges.asi for SnowRunner: a gauge cluster for the truck being driven, drawn through ReShade's add-on overlay
// (gauge_overlay.cpp). This file is the mod's own thread: it owns the settings (Gauges.ini next to the exe, written
// with the defaults on the first start), the log (Gauges.log, rewritten at each game start), the toggle key and pad
// buttons, and the state the cluster shows, which it leaves for the render thread 20 times a second.
//
// This version reads nothing from the game yet: the cluster shows demo values (ini Demo=1). The mod stands down on
// any exe but the one its addresses are from (kGaugesExeStamp), so the versions that read the game start from here.
#include <windows.h>
#include <xinput.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string>
#include "gauges.h"

static HMODULE g_self = nullptr;
static std::wstring g_dir; // the game's folder, with the last backslash
static uint64_t g_base = 0;
static bool g_reshadeOk = false;

// The ini's settings in force. The Gauges tab in ReShade's overlay reads them and hands back edits (SettingsPut,
// render thread); the mod's thread takes those on its next pass and saves the ini a second later.
static SRWLOCK g_setLock = SRWLOCK_INIT;
static GaugeSettings g_set;
static volatile LONG g_setDirty = 0, g_reloadAsked = 0;

// What the cluster shows, written by the mod's thread and read by the render thread.
static SRWLOCK g_viewLock = SRWLOCK_INIT;
static GaugeView g_view = {};

// The log is rewritten at each game start. The mod's thread and ReShade's render thread both write it: one at a time,
// as the file is opened for one writer and a second open would fail and lose its line.
static SRWLOCK g_logLock = SRWLOCK_INIT;
static bool g_logStarted = false; // true once this start's first line is in
static void LogV(const wchar_t *fmt, va_list ap)
{
    AcquireSRWLockExclusive(&g_logLock);
    FILE *f = nullptr;
    if (_wfopen_s(&f, (g_dir + L"Gauges.log").c_str(), g_logStarted ? L"a, ccs=UTF-8" : L"w, ccs=UTF-8") == 0 && f)
    {
        g_logStarted = true;
        SYSTEMTIME t;
        GetLocalTime(&t);
        fwprintf(f, L"%02u:%02u:%02u ", t.wHour, t.wMinute, t.wSecond);
        vfwprintf(f, fmt, ap);
        fputwc(L'\n', f);
        fclose(f);
    }
    ReleaseSRWLockExclusive(&g_logLock);
}

void GaugeLog(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    LogV(fmt, ap);
    va_end(ap);
}
#define Log GaugeLog

// ---- Gauges.ini ----

static const wchar_t *const kIniSection = L"Gauges";
static std::wstring IniPath() { return g_dir + L"Gauges.ini"; }

// A number from the ini; def for a missing key and for anything that is not a finite number ("nan" would pass every
// range check after it).
static float IniFloat(const wchar_t *key, float def, const std::wstring &ini)
{
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(kIniSection, key, L"", buf, 64, ini.c_str());
    if (!buf[0]) return def;
    const float v = (float)_wtof(buf);
    return isfinite(v) ? v : def;
}

static int IniInt(const wchar_t *key, int def, const std::wstring &ini) { return (int)GetPrivateProfileIntW(kIniSection, key, def, ini.c_str()); }
static bool IniBool(const wchar_t *key, bool def, const std::wstring &ini) { return IniInt(key, def ? 1 : 0, ini) != 0; }

static std::wstring Wide(const char *s) { std::wstring w; while (*s) w += (wchar_t)(unsigned char)*s++; return w; }

// Every key into the ini (WritePrivateProfileString keeps the comments and the keys it is not given).
static void WriteIni(const GaugeSettings &s)
{
    const std::wstring ini = IniPath();
    // numbers go in with the fewest digits that read back as the same float
    auto num = [&](const wchar_t *key, float v) {
        wchar_t b[32];
        swprintf_s(b, L"%g", v);
        for (int digits = 7; digits <= 9 && (float)_wtof(b) != v; digits++) swprintf_s(b, L"%.*g", digits, v);
        WritePrivateProfileStringW(kIniSection, key, b, ini.c_str());
    };
    auto whole = [&](const wchar_t *key, int v) {
        wchar_t b[16];
        swprintf_s(b, L"%d", v);
        WritePrivateProfileStringW(kIniSection, key, b, ini.c_str());
    };
    whole(L"Layout", s.layout);
    whole(L"Style", s.style);
    num(L"FaceOpacity", s.faceOpacity);
    num(L"OffsetX", s.offsetX);
    num(L"OffsetY", s.offsetY);
    num(L"Scale", s.scale);
    num(L"Opacity", s.opacity);
    whole(L"Mph", s.mph ? 1 : 0);
    whole(L"Psi", s.psi ? 1 : 0);
    whole(L"Fahrenheit", s.fahrenheit ? 1 : 0);
    whole(L"ShowTemp", s.showTemp ? 1 : 0);
    whole(L"ShowBoost", s.showBoost ? 1 : 0);
    whole(L"ShowAir", s.showAir ? 1 : 0);
    whole(L"ShowLamps", s.showLamps ? 1 : 0);
    whole(L"TachClass", s.tachClass);
    num(L"IdleRpm", s.idleRpm);
    num(L"RedlineRpm", s.redlineRpm);
    num(L"MaxRpm", s.maxRpm);
    num(L"SpeedMax", s.speedMax);
    whole(L"ToggleKey", s.toggleKey);
    WritePrivateProfileStringW(kIniSection, L"PadToggle", Wide(s.padToggle).c_str(), ini.c_str());
    whole(L"Demo", s.demo ? 1 : 0);
    whole(L"Probe", s.probe);
}

// The ini into s (defaults for missing keys, every number held to its range); a missing ini is written with the
// defaults and how to use them.
static void ReadIni(GaugeSettings &s)
{
    SettingsDefaults(s);
    const std::wstring ini = IniPath();
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        FILE *f = nullptr;
        if (_wfopen_s(&f, ini.c_str(), L"w, ccs=UTF-16LE") == 0 && f)
        {
            fwprintf(f, L"; Gauges: a gauge cluster for the truck being driven. It needs ReShade with add-on support.\n"
                        L"; The Gauges tab in ReShade's overlay edits everything here in the game.\n"
                        L"; Layout: 0 = a strip along the bottom centre, 1 = a block in the bottom right corner.\n"
                        L"; Style: 0 = solid dial faces, 1 = tinted see-through faces (FaceOpacity 0 to 0.85 sets how dark),\n"
                        L"; 2 = open: no faces, only the scales, numbers and needles.\n"
                        L"; OffsetX and OffsetY move the cluster from its place, in pixels of a 1080p screen (right and down).\n"
                        L"; Scale is its size (1 = as designed; it grows with the screen's height). Opacity 0.1 to 1.\n"
                        L"; Mph=1: speed in mph and range in miles. Psi=1: boost in psi. Fahrenheit=1: temperature in F.\n"
                        L"; ShowTemp, ShowBoost, ShowAir and ShowLamps switch the small dials and the lamps on (1) or off (0).\n"
                        L"; TachClass sets the scales: 0 = truck (idle 650 rpm, red zone from 2200 of 3000, speedometer to 100 km/h),\n"
                        L"; 1 = scout (800, 5500 of 7000, 120 km/h), 2 = heavy (600, 1900 of 2500, 80 km/h).\n"
                        L"; IdleRpm, RedlineRpm, MaxRpm and SpeedMax (km/h) replace the class's numbers; 0 = the class's own.\n"
                        L"; ToggleKey is the virtual key code of a key that hides and shows the cluster (0 = none; 120 = F9).\n"
                        L"; PadToggle does the same with pad buttons (XInput): DPadUp, DPadDown, DPadLeft, DPadRight, A, B, X, Y,\n"
                        L"; LB, RB, LS, RS, Start, Back, joined with + (LB+DPadUp), or None. The game sees these buttons too.\n"
                        L"; Demo=1 draws demo values while no truck is driven. Probe=1 logs the values read from the game.\n");
            fclose(f);
            WriteIni(s);
        }
        return;
    }
    s.layout = IniInt(L"Layout", s.layout, ini);
    s.style = IniInt(L"Style", s.style, ini);
    s.faceOpacity = IniFloat(L"FaceOpacity", s.faceOpacity, ini);
    s.offsetX = IniFloat(L"OffsetX", s.offsetX, ini);
    s.offsetY = IniFloat(L"OffsetY", s.offsetY, ini);
    s.scale = IniFloat(L"Scale", s.scale, ini);
    s.opacity = IniFloat(L"Opacity", s.opacity, ini);
    s.mph = IniBool(L"Mph", s.mph, ini);
    s.psi = IniBool(L"Psi", s.psi, ini);
    s.fahrenheit = IniBool(L"Fahrenheit", s.fahrenheit, ini);
    s.showTemp = IniBool(L"ShowTemp", s.showTemp, ini);
    s.showBoost = IniBool(L"ShowBoost", s.showBoost, ini);
    s.showAir = IniBool(L"ShowAir", s.showAir, ini);
    s.showLamps = IniBool(L"ShowLamps", s.showLamps, ini);
    s.tachClass = IniInt(L"TachClass", s.tachClass, ini);
    s.idleRpm = IniFloat(L"IdleRpm", s.idleRpm, ini);
    s.redlineRpm = IniFloat(L"RedlineRpm", s.redlineRpm, ini);
    s.maxRpm = IniFloat(L"MaxRpm", s.maxRpm, ini);
    s.speedMax = IniFloat(L"SpeedMax", s.speedMax, ini);
    s.toggleKey = IniInt(L"ToggleKey", s.toggleKey, ini);
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(kIniSection, L"PadToggle", Wide(s.padToggle).c_str(), buf, 64, ini.c_str());
    size_t n = 0;
    for (const wchar_t *p = buf; *p && n + 1 < sizeof s.padToggle; p++) s.padToggle[n++] = *p < 128 ? (char)*p : '?';
    s.padToggle[n] = 0;
    s.demo = IniBool(L"Demo", s.demo, ini);
    s.probe = IniInt(L"Probe", s.probe, ini);
    SettingsClamp(s);
}

void SettingsGet(GaugeSettings &out)
{
    AcquireSRWLockShared(&g_setLock);
    out = g_set;
    ReleaseSRWLockShared(&g_setLock);
}

void SettingsPut(const GaugeSettings &in)
{
    AcquireSRWLockExclusive(&g_setLock);
    g_set = in;
    ReleaseSRWLockExclusive(&g_setLock);
    InterlockedExchange(&g_setDirty, 1);
}

void SettingsReload() { InterlockedExchange(&g_reloadAsked, 1); }

void ViewGet(GaugeView &out)
{
    AcquireSRWLockShared(&g_viewLock);
    out = g_view;
    ReleaseSRWLockShared(&g_viewLock);
}

// ---- the pad (read only: the game keeps every button) ----

typedef DWORD(WINAPI *XInputGetStateFn)(DWORD, XINPUT_STATE *);

// XInputGetState of the XInput DLL the game uses, once the game has loaded it; XInput 1.4 until then.
static XInputGetStateFn PadFunction()
{
    static XInputGetStateFn game = nullptr, own = nullptr;
    if (game) return game;
    const HMODULE x = GetModuleHandleW(L"xinput9_1_0.dll");
    if (x) game = (XInputGetStateFn)GetProcAddress(x, "XInputGetState");
    if (game) return game;
    if (!own)
    {
        const HMODULE m = LoadLibraryW(L"xinput1_4.dll");
        own = m ? (XInputGetStateFn)GetProcAddress(m, "XInputGetState") : nullptr;
    }
    return own;
}

// The buttons of every connected pad together. A slot with no pad is slow to ask, so the empty ones are only asked
// again every two seconds.
uint32_t PadButtons()
{
    static volatile LONG connected = 0xF, askedAt = 0;
    const XInputGetStateFn get = PadFunction();
    if (!get) return 0;
    const DWORD now = GetTickCount();
    const bool all = now - (DWORD)askedAt > 2000;
    if (all) InterlockedExchange(&askedAt, (LONG)now);
    const LONG known = connected;
    LONG answered = 0;
    uint32_t buttons = 0;
    for (DWORD i = 0; i < XUSER_MAX_COUNT; i++)
    {
        if (!all && !(known & (1L << i))) continue;
        XINPUT_STATE st = {};
        if (get(i, &st) != ERROR_SUCCESS) continue;
        answered |= 1L << i;
        buttons |= st.Gamepad.wButtons;
    }
    if (all) InterlockedExchange(&connected, answered);
    return buttons;
}

#ifndef GAUGES_SETTINGS_TEST
static void LogSettings(const GaugeSettings &s)
{
    static const wchar_t *layouts[kLayoutCount] = { L"strip", L"block" }, *styles[kFaceCount] = { L"solid", L"tinted", L"open" },
                         *classes[kClassCount] = { L"truck", L"scout", L"heavy" };
    const GaugeScale k = GaugeScaleOf(s);
    char key[32];
    KeyName(s.toggleKey, key, sizeof key);
    Log(L"settings: %s layout, %s style, size x%g, opacity %g, offset %g / %g, dials:%s%s%s%s", layouts[s.layout], styles[s.style], s.scale, s.opacity, s.offsetX,
        s.offsetY, s.showTemp ? L" temperature" : L"", s.showBoost ? L" boost" : L"", s.showAir ? L" air" : L"", s.showLamps ? L" lamps" : L"");
    Log(L"settings: %s scales: idle %.0f rpm, red zone from %.0f of %.0f, speedometer to %.0f km/h; units %s, %s, %s", classes[s.tachClass], k.idle, k.redline, k.max,
        k.speedMax, s.mph ? L"mph" : L"km/h", s.psi ? L"psi" : L"bar", s.fahrenheit ? L"F" : L"C");
    Log(L"settings: toggle key %S, pad %S, demo values %s, probe %d", key, s.padToggle, s.demo ? L"on" : L"off", s.probe);
}

static bool GameInFront()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// ---- the mod's thread ----

static DWORD WINAPI Run(void *)
{
    // One copy of the mod per game: the loader loads every .asi in the folder, and a second file of this mod (a copy
    // left under another name) would draw a second cluster and write the same ini. The mutex is the process's; the
    // second copy adds its line to the first one's log and ends.
    wchar_t once[64];
    swprintf_s(once, L"Local\\SnowRunnerGauges-%lu", GetCurrentProcessId());
    if (CreateMutexW(nullptr, FALSE, once) && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        wchar_t file[MAX_PATH] = {};
        GetModuleFileNameW(g_self, file, MAX_PATH);
        Sleep(1000); // after the first copy's first line, which starts the log anew
        g_logStarted = true;
        Log(L"this mod is in the folder twice: only one copy runs, and this one (%s) stands down. Keep one .asi file of it.", file);
        return 0;
    }
    const BYTE *image = (const BYTE *)g_base;
    const DWORD stamp = *(const DWORD *)(image + *(const DWORD *)(image + 0x3C) + 8);
    Log(L"Gauges " GAUGES_VERSION L": exe stamp %08lx", stamp);
    SettingsDefaults(g_set); // what the tab shows until the ini is read
    g_reshadeOk = OverlayInit(g_self);
    if (stamp != kGaugesExeStamp)
    {
        Log(L"this is not the game version the mod is made for (exe stamp %08lx): standing down, nothing is drawn or changed", kGaugesExeStamp);
        InterlockedExchange(&g_modOff, 1);
        return 0;
    }
    GaugeSettings s;
    ReadIni(s);
    AcquireSRWLockExclusive(&g_setLock);
    g_set = s;
    ReleaseSRWLockExclusive(&g_setLock);
    LogSettings(s);
    Log(g_reshadeOk ? L"overlay: the cluster and the Gauges tab are drawn through ReShade's overlay"
                    : L"overlay: ReShade with add-on support was not found, so nothing is drawn");
    bool visible = true, keyDown = false;
    uint32_t padMask = PadParse(s.padToggle), padPrev = 0;
    ULONGLONG saveAt = 0;
    for (;;)
    {
        Sleep(50);
        // the settings tab: edits count on this pass and go into the ini once they have rested a second; "read the
        // ini again" takes the file as it is
        bool edited = false;
        if (InterlockedExchange(&g_reloadAsked, 0))
        {
            ReadIni(s);
            AcquireSRWLockExclusive(&g_setLock);
            g_set = s;
            ReleaseSRWLockExclusive(&g_setLock);
            InterlockedExchange(&g_setDirty, 0);
            Log(L"settings: Gauges.ini read again");
            LogSettings(s);
            saveAt = 0;
            edited = true;
        }
        if (InterlockedExchange(&g_setDirty, 0))
        {
            const int keyBefore = s.toggleKey;
            SettingsGet(s);
            // a key just bound on the tab is still down: it toggles from its next press on
            if (s.toggleKey != keyBefore) keyDown = true;
            saveAt = GetTickCount64() + 1000;
            edited = true;
        }
        if (saveAt && GetTickCount64() >= saveAt)
        {
            WriteIni(s);
            saveAt = 0;
            Log(L"settings: saved to Gauges.ini");
            LogSettings(s);
        }
        if (edited) padMask = PadParse(s.padToggle);
        // the toggle: the key, or the pad buttons all down that were not all down on the last pass; only while the
        // game's window is in front, and the pad not while the tab waits for buttons to bind
        const bool front = GameInFront();
        const bool keyNow = front && s.toggleKey && (GetAsyncKeyState(s.toggleKey) & 0x8000);
        bool toggle = keyNow && !keyDown;
        keyDown = keyNow;
        const LONG capture = g_pageCapturing;
        const bool capturing = capture && GetTickCount() - (DWORD)capture < 500;
        const uint32_t pad = padMask && front && !capturing ? PadButtons() : 0;
        if (padMask && (pad & padMask) == padMask && (padPrev & padMask) != padMask) toggle = true;
        padPrev = pad;
        if (toggle)
        {
            visible = !visible;
            Log(L"cluster %s", visible ? L"shown" : L"hidden");
        }
        // what the cluster shows: nothing is read from the game yet, so the demo values or nothing
        GaugeView v = {};
        v.demo = s.demo;
        v.show = visible && s.demo;
        if (v.show) GaugeDemo(v.st, s);
        AcquireSRWLockExclusive(&g_viewLock);
        g_view = v;
        ReleaseSRWLockExclusive(&g_viewLock);
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t *name = wcsrchr(path, L'\\');
    if (!name || _wcsicmp(name + 1, L"SnowRunner.exe")) return TRUE; // loaded into another program: do nothing
    g_dir.assign(path, name + 1 - path);
    g_base = (uint64_t)GetModuleHandleW(nullptr);
    g_self = inst;
    const HANDLE t = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    return TRUE;
}
#else
// The offline test of the settings: build.bat makes out\test\settings_test.exe from this file with
// GAUGES_SETTINGS_TEST. It works on a Gauges.ini next to itself: a missing ini comes out as the defaults with the help
// text, every key reads back the same (every setting is given a value that is not its default, so a key that is never
// written shows), the help text and keys the mod does not know survive a write, values that are no numbers give the
// defaults, values out of range are held to it, and the scales fit each other. With an ini as its argument, a copy of
// that file must read back unchanged after a write.
bool OverlayInit(HMODULE) { return false; }
volatile LONG g_pageCapturing = 0, g_modOff = 0;

static bool SameSettings(const GaugeSettings &a, const GaugeSettings &b)
{
    return a.layout == b.layout && a.style == b.style && a.faceOpacity == b.faceOpacity && a.offsetX == b.offsetX && a.offsetY == b.offsetY &&
           a.scale == b.scale && a.opacity == b.opacity && a.mph == b.mph && a.psi == b.psi && a.fahrenheit == b.fahrenheit && a.showTemp == b.showTemp &&
           a.showBoost == b.showBoost && a.showAir == b.showAir && a.showLamps == b.showLamps && a.tachClass == b.tachClass && a.idleRpm == b.idleRpm &&
           a.redlineRpm == b.redlineRpm && a.maxRpm == b.maxRpm && a.speedMax == b.speedMax && a.toggleKey == b.toggleKey &&
           !strcmp(a.padToggle, b.padToggle) && a.demo == b.demo && a.probe == b.probe;
}

static int g_failed = 0;
static void Check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failed++;
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    g_dir.assign(path, wcsrchr(path, L'\\') + 1 - path);
    const std::wstring ini = IniPath();
    DeleteFileW(ini.c_str());
    const bool gone = GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES;
    GaugeSettings d, a, b, c;
    SettingsDefaults(d);
    ReadIni(a);
    const bool written = GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES;
    ReadIni(b);
    Check(gone && written && SameSettings(a, d) && SameSettings(b, d), "ini: a missing ini is written with the defaults and reads back as them");
    Check(d.layout == kLayoutStrip && d.style == kFaceOpen && !d.showAir && d.demo && d.probe == 0 && !d.mph && !d.psi && !d.fahrenheit && d.toggleKey == 0 &&
          !strcmp(d.padToggle, "None"), "defaults: strip, open style, metric, the air dial off, no toggle, demo on, probe off");

    // keys the mod does not know, in its own section and in another one
    WritePrivateProfileStringW(kIniSection, L"FromALaterVersion", L"7", ini.c_str());
    WritePrivateProfileStringW(L"Other", L"Kept", L"yes", ini.c_str());
    c = d;
    c.layout = kLayoutBlock;
    c.style = kFaceTinted;
    c.faceOpacity = 0.6f;
    c.offsetX = -212.5f;
    c.offsetY = 37.25f;
    c.scale = 1.35f;
    c.opacity = 0.8f;
    c.mph = c.psi = c.fahrenheit = true;
    c.showTemp = c.showBoost = c.showLamps = false;
    c.showAir = true;
    c.tachClass = kClassHeavy;
    c.idleRpm = 700.0f;
    c.redlineRpm = 2100.0f;
    c.maxRpm = 3500.0f;
    c.speedMax = 90.0f;
    c.toggleKey = VK_F9;
    strcpy_s(c.padToggle, "RB+DPadUp");
    c.demo = false;
    c.probe = 1;
    const bool allChanged = c.layout != d.layout && c.style != d.style && c.faceOpacity != d.faceOpacity && c.offsetX != d.offsetX && c.offsetY != d.offsetY &&
                            c.scale != d.scale && c.opacity != d.opacity && c.mph != d.mph && c.psi != d.psi && c.fahrenheit != d.fahrenheit &&
                            c.showTemp != d.showTemp && c.showBoost != d.showBoost && c.showAir != d.showAir && c.showLamps != d.showLamps &&
                            c.tachClass != d.tachClass && c.idleRpm != d.idleRpm && c.redlineRpm != d.redlineRpm && c.maxRpm != d.maxRpm &&
                            c.speedMax != d.speedMax && c.toggleKey != d.toggleKey && strcmp(c.padToggle, d.padToggle) && c.demo != d.demo && c.probe != d.probe;
    WriteIni(c);
    ReadIni(b);
    Check(allChanged, "ini: the test changes every setting from its default");
    Check(SameSettings(b, c), "ini: every key written reads back the same");
    wchar_t later[16] = {}, other[16] = {}, first[80] = {};
    GetPrivateProfileStringW(kIniSection, L"FromALaterVersion", L"", later, 16, ini.c_str());
    GetPrivateProfileStringW(L"Other", L"Kept", L"", other, 16, ini.c_str());
    FILE *f = nullptr;
    int comments = 0;
    if (_wfopen_s(&f, ini.c_str(), L"r, ccs=UTF-16LE") == 0 && f)
    {
        wchar_t line[400];
        for (int i = 0; fgetws(line, 400, f); i++)
        {
            if (i == 0) wcsncpy_s(first, line, _TRUNCATE);
            if (line[0] == L';') comments++;
        }
        fclose(f);
    }
    Check(!wcscmp(later, L"7") && !wcscmp(other, L"yes"), "ini: keys the mod does not know survive a write");
    Check(!wcsncmp(first, L"; Gauges:", 9) && comments == 16, "ini: the help text survives a write (16 comment lines)");

    // what is no number gives the default, what is out of range is held to it
    WritePrivateProfileStringW(kIniSection, L"Scale", L"nan", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"Opacity", L"inf", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"OffsetX", L"abc", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"FaceOpacity", L"5", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"Layout", L"9", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"Style", L"-3", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"TachClass", L"12", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"IdleRpm", L"50", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"MaxRpm", L"-200", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"ToggleKey", L"4000", ini.c_str());
    ReadIni(b);
    Check(b.scale == d.scale && b.opacity == d.opacity, "ini: \"nan\" and \"inf\" give the defaults");
    Check(b.offsetX == 0.0f && b.faceOpacity == kFaceOpacityMax && b.layout == kLayoutStrip && b.style == kFaceOpen && b.tachClass == kClassTruck &&
          b.idleRpm == kIdleRange[0] && b.maxRpm == 0.0f && b.toggleKey == 0, "ini: values out of range are held to it");

    // the scales in force
    GaugeScale k = GaugeScaleOf(d);
    Check(k.idle == 650.0f && k.redline == 2200.0f && k.max == 3000.0f && k.speedMax == 100.0f, "scales: the truck class by default");
    a = d;
    a.tachClass = kClassScout;
    k = GaugeScaleOf(a);
    Check(k.idle == 800.0f && k.redline == 5500.0f && k.max == 7000.0f && k.speedMax == 120.0f, "scales: the scout class");
    a.tachClass = kClassHeavy;
    a.maxRpm = 2750.0f;
    a.redlineRpm = 4000.0f;
    a.idleRpm = 2900.0f;
    a.speedMax = 65.0f;
    k = GaugeScaleOf(a);
    Check(k.max == 3000.0f && k.redline == 3000.0f && k.idle == 2400.0f && k.speedMax == 65.0f,
          "scales: own numbers replace the class's, the top rounds up to a whole step, the red zone and idle fit under it");
    GaugeState st;
    a = d;
    a.mph = true;
    GaugeDemo(st, a);
    Check(st.engineOn && st.rpm > 650.0f && st.rpm < 2200.0f && st.rpmMax == 3000.0f && st.speedMax == 100.0f && st.mph && !st.psi && st.gearMode &&
          !strcmp(st.gearMode, "A"), "demo: a cruising truck on the scales and units of the settings");

    // pad button names
    char name[32];
    PadName(PadParse("dpadup + rb"), name, sizeof name);
    Check(!strcmp(name, "RB+DPadUp") && PadParse("None") == 0 && PadParse("LB+Nothing") == 0, "pad: button names read and written, unknown names give none");
    KeyName(VK_F9, name, sizeof name);
    Check(!strcmp(name, "F9"), "key: F9 by name");
    KeyName(0, name, sizeof name);
    Check(!strcmp(name, "None"), "key: none by name");

    if (argc > 1)
    {
        if (!CopyFileW(argv[1], ini.c_str(), FALSE))
        {
            printf("FAIL ini: %ls could not be copied (error %lu)\n", argv[1], GetLastError());
            return 1;
        }
        ReadIni(a);
        WriteIni(a);
        ReadIni(b);
        Check(SameSettings(a, b), "ini: a copy of the given ini reads back the same after a write");
    }
    printf(g_failed ? "SETTINGS TEST FAILED (%d)\n" : "SETTINGS TEST OK\n", g_failed);
    return g_failed ? 1 : 0;
}
#endif
