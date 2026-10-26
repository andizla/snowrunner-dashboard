// Gauges.asi for SnowRunner (Next Gen Gameplay): a gauge cluster for the truck being driven, drawn by Next Gen's own
// overlay (gauge_overlay.cpp, ng_overlay.cpp; no ReShade needed). This file is the mod's own thread: it owns the settings (Gauges.ini next to the exe, written
// with the defaults on the first start), the log (Gauges.log, rewritten at each game start), the toggle key and pad
// buttons, and the state the cluster shows, which it leaves for the render thread 20 times a second.
//
// The state comes from the truck being driven, read out of the game's memory (truck.h): speed, gear, fuel and the
// lamps. The engine speed, the range, the tilt and the coolant temperature are worked out from those values
// (models.h). With the ini's Damage on, what the temperature costs the engine goes into the game's own engine
// damage. With Gearbox=1 the mod's automatic sets the gears while the game's shifter is in A: it puts the game's
// automatic flag down and calls the game's own shift function (models.h, TakeoverStep). With Powertrain=1, and for
// the pull test's seven seconds, it writes the game's torque factor (TorqueLift, PullTestStep). Those are all the
// mod writes into the game and all it calls there. The cluster is on screen only while a truck is driven in the world. On another build than the one its addresses are from (kGaugesExeStamp) the mod finds them by the code that uses them (game_globals.h), and stands down where it cannot. The map
// a truck is on comes from the game's save (maps.h), for the map's outside temperature.
#include <windows.h>
#include <xinput.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string>
#include <vector>
#include <algorithm>
#include "gauges.h"
#include "truck.h"
#include "models.h"
#include "gearbox_hooks.h"
#include "gearbox_model.h"
#include "input_bindings.h"
#ifndef GAUGES_SETTINGS_TEST
#include "input_devices.h"
#endif
bool NgMenuOpen(); // ng_overlay.cpp (its header wants Dear ImGui): the toggle key rests while the NEXT GEN menu has the keys
int NgMenuKeyFromIni(); // the menu's key, which a capture never binds
#include "tires.h"
#include "maps.h"
#include "game_code.h"
#include "game_globals.h"
#include "clock_scan.h"
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#ifndef GAUGES_SETTINGS_TEST
#include "sound.h"
#include "state.h"
#endif

static HMODULE g_self = nullptr;
static std::wstring g_dir; // the game's folder, with the last backslash
static uint64_t g_base = 0;
static bool g_overlayOk = false;
static bool g_addon = false;  // loaded by ReShade under the name from before 0.16 (Gauges.addon64), not by an ASI loader (Gauges.asi)
static bool g_second = false; // another copy of the mod runs in this game, and this one stands down

// The ini's settings in force. The Gameplay tab of the NEXT GEN menu reads them and hands back edits (SettingsPut,
// render thread); the mod's thread takes those on its next pass and saves the ini a second later. g_setTurn counts
// the times the mod's thread put settings in force itself (SettingsPublish).
static SRWLOCK g_setLock = SRWLOCK_INIT;
static GaugeSettings g_set;
static LONG g_setTurn = 0;
static volatile LONG g_setDirty = 0, g_reloadAsked = 0, g_truckDropAsked = 0, g_resetAsked = 0;
static volatile LONG g_pullTestAsked = 0; // 1 arm the pull test, 2 cancel it
static volatile LONG g_mapAmbientAsked = 0, g_mapAmbientTenths = 0; // the tab set the map's outside temperature, in tenths of a degree C
static volatile LONG g_gearHoldAsked = 0, g_gearHoldTenths = 0;     // the tab set the driven gearbox's hold, in tenths of a second
static volatile LONG g_engineAsked = 0, g_enginePeak = 0, g_engineAspiration = 0; // the tab set (1) or dropped (2) the driven engine's profile: the peak torque's rpm, the aspiration
static volatile LONG g_engineTorqueAsked = 0, g_engineTorquePercent = 0; // the tab set the driven engine's torque share, in percent

// What the cluster shows, written by the mod's thread and read by the render thread.
static SRWLOCK g_viewLock = SRWLOCK_INIT;
static GaugeView g_view = {};

// The log is rewritten at each game start. The mod's thread and the game's render thread (the overlay) both write it: one at a time,
// as the file is opened for one writer and a second open would fail and lose its line.
static SRWLOCK g_logLock = SRWLOCK_INIT;
static bool g_logStarted = false; // true once this start's first line is in
static std::wstring g_logKept;    // where the last start's log was kept, "" for nowhere

// The last start's log kept: Gauges.log in dir moves into Gauges\logs beside it, named by the date and time that
// start began, so every start's log is kept and a log passed on says when it was. The time is its first line's; the
// date the day it was last written, or the day before where that is later in the day than its last write (a drive
// past midnight). The name taken, "" where there was no log or it could not be moved.
static std::wstring LogKeepLast(const std::wstring &dir)
{
    const std::wstring path = dir + L"Gauges.log";
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a) || (a.nFileSizeHigh == 0 && a.nFileSizeLow == 0)) return L"";
    FILETIME local;
    SYSTEMTIME last, began;
    if (!FileTimeToLocalFileTime(&a.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &last)) return L"";
    began = last;
    char head[24] = {};
    FILE *f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f)
    {
        fread(head, 1, sizeof head - 1, f);
        fclose(f);
    }
    const char *line = head + (memcmp(head, "\xEF\xBB\xBF", 3) ? 0 : 3);
    int h = 0, m = 0, sec = 0;
    if (sscanf_s(line, "%2d:%2d:%2d", &h, &m, &sec) == 3 && h >= 0 && h < 24 && m >= 0 && m < 60 && sec >= 0 && sec < 60)
    {
        if (h * 3600 + m * 60 + sec > last.wHour * 3600 + last.wMinute * 60 + last.wSecond)
        {
            ULARGE_INTEGER day;
            day.LowPart = local.dwLowDateTime;
            day.HighPart = local.dwHighDateTime;
            day.QuadPart -= 24ULL * 3600ULL * 10000000ULL;
            const FILETIME before = { day.LowPart, day.HighPart };
            FileTimeToSystemTime(&before, &began);
        }
        began.wHour = (WORD)h;
        began.wMinute = (WORD)m;
        began.wSecond = (WORD)sec;
    }
    const std::wstring folder = dir + L"Gauges\\logs\\";
    CreateDirectoryW((dir + L"Gauges").c_str(), nullptr);
    CreateDirectoryW(folder.c_str(), nullptr);
    wchar_t stem[48];
    swprintf_s(stem, L"Gauges %04u-%02u-%02u %02u-%02u-%02u", began.wYear, began.wMonth, began.wDay, began.wHour, began.wMinute, began.wSecond);
    std::wstring name = std::wstring(stem) + L".log";
    for (int i = 2; i < 100 && GetFileAttributesW((folder + name).c_str()) != INVALID_FILE_ATTRIBUTES; i++)
        name = std::wstring(stem) + L" (" + std::to_wstring(i) + L").log";
    return MoveFileExW(path.c_str(), (folder + name).c_str(), MOVEFILE_COPY_ALLOWED) ? name : L"";
}

static void LogV(const wchar_t *fmt, va_list ap)
{
    AcquireSRWLockExclusive(&g_logLock);
#ifndef GAUGES_SETTINGS_TEST
    if (!g_logStarted) g_logKept = LogKeepLast(g_dir);
#endif
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

#ifndef GAUGES_SETTINGS_TEST
// Says where the last start's log went, once this start's first line is in.
static void LogKeptSay()
{
    if (!g_logKept.empty()) Log(L"log: the last start's log is kept as Gauges\\logs\\%s", g_logKept.c_str());
}
#endif

// ---- Gauges.ini ----

static const wchar_t *const kIniSection = L"Gauges";
static std::wstring IniPath() { return g_dir + L"Gauges.ini"; }

// A number from the ini; def for a missing key and for anything that is not a finite number ("nan" would pass every
// range check after it).
static float IniFloatIn(const wchar_t *section, const wchar_t *key, float def, const std::wstring &ini)
{
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(section, key, L"", buf, 64, ini.c_str());
    if (!buf[0]) return def;
    const float v = (float)_wtof(buf);
    return isfinite(v) ? v : def;
}

static float IniFloat(const wchar_t *key, float def, const std::wstring &ini) { return IniFloatIn(kIniSection, key, def, ini); }
static int IniInt(const wchar_t *key, int def, const std::wstring &ini) { return (int)GetPrivateProfileIntW(kIniSection, key, def, ini.c_str()); }
static bool IniBool(const wchar_t *key, bool def, const std::wstring &ini) { return IniInt(key, def ? 1 : 0, ini) != 0; }

// Text as UTF-8 (the game's names, such as Grüntal Hollow), or byte by byte where it is not valid UTF-8.
static std::wstring Wide(const char *s)
{
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, nullptr, 0);
    if (n > 0)
    {
        std::wstring w((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, &w[0], n);
        w.resize((size_t)n - 1);
        return w;
    }
    std::wstring w;
    while (*s) w += (wchar_t)(unsigned char)*s++;
    return w;
}

// A number into a section of the ini, with the fewest digits that read back as the same float.
static void IniPutFloat(const wchar_t *section, const wchar_t *key, float v, const std::wstring &ini)
{
    wchar_t b[32];
    swprintf_s(b, L"%g", v);
    for (int digits = 7; digits <= 9 && (float)_wtof(b) != v; digits++) swprintf_s(b, L"%.*g", digits, v);
    WritePrivateProfileStringW(section, key, b, ini.c_str());
}

static void IniPutInt(const wchar_t *section, const wchar_t *key, int v, const std::wstring &ini)
{
    wchar_t b[16];
    swprintf_s(b, L"%d", v);
    WritePrivateProfileStringW(section, key, b, ini.c_str());
}

// ---- setups of their own: a file each in the folder Gauges\ beside Gauges.ini ----
// A truck's own scales stand in Gauges\trucks\<kind>.ini, a gearbox's own hold in Gauges\gearboxes\<name>.ini, each
// under one section ([Truck], [Gearbox]) with the kind or the name written in it: a file for each, so that a setup
// can be passed on and dropped in as it is. A file's name cannot hold a stroke, which a modded truck's kind does
// ("<name>/<mod id>"): it is written "@" there, and any other character a file's name cannot hold "_". Up to 0.13.9
// they stood in Gauges.ini as [Truck:<kind>] and [Gearbox:<name>]: SetupsMove takes them out into the files.
enum { kSetupTruck, kSetupGearbox, kSetupEngine, kSetupKinds };
static const wchar_t *const kSetupFolders[kSetupKinds] = { L"trucks", L"gearboxes", L"engines" };
static const wchar_t *const kSetupSections[kSetupKinds] = { L"Truck", L"Gearbox", L"Engine" };
static const wchar_t *const kSetupNameKeys[kSetupKinds] = { L"Kind", L"Name", L"Name" };
static const wchar_t *const kSetupIniPrefixes[kSetupKinds] = { L"Truck:", L"Gearbox:", L"Engine:" };

// The file's name for a key, without the folder and ".ini".
static std::wstring SetupFileName(const std::wstring &key)
{
    std::wstring name;
    for (wchar_t c : key) name += c == L'/' ? L'@' : (c < 32 || wcschr(L"\\:*?\"<>|", c)) ? L'_' : c;
    return name;
}

static std::wstring SetupFolder(int kind) { return g_dir + L"Gauges\\" + kSetupFolders[kind]; }
static std::wstring SetupFile(int kind, const std::wstring &key) { return SetupFolder(kind) + L"\\" + SetupFileName(key) + L".ini"; }
static std::wstring SetupFile(int kind, const char *key) { return SetupFile(kind, Wide(key)); }

// The folder made where it is not (WritePrivateProfileString makes a file, not a folder).
static void SetupFolderMake(int kind)
{
    CreateDirectoryW((g_dir + L"Gauges").c_str(), nullptr);
    CreateDirectoryW(SetupFolder(kind).c_str(), nullptr);
}

// A setup's file begun: the folder, and the kind or the name written in it.
static void SetupBegin(int kind, const std::wstring &key)
{
    SetupFolderMake(kind);
    WritePrivateProfileStringW(kSetupSections[kind], kSetupNameKeys[kind], key.c_str(), SetupFile(kind, key).c_str());
}

// The setups Gauges.ini still holds, [Truck:<kind>] and [Gearbox:<name>], moved into their files: each key the file
// does not hold yet goes in, and the section leaves Gauges.ini. Their count, by kind.
static void SetupsMove(int (&moved)[kSetupKinds])
{
    const std::wstring ini = IniPath();
    for (int &n : moved) n = 0;
    std::vector<wchar_t> names(65536, L'\0');
    const DWORD got = GetPrivateProfileSectionNamesW(names.data(), (DWORD)names.size(), ini.c_str());
    if (!got) return;
    std::vector<std::wstring> sections;
    for (const wchar_t *n = names.data(); *n && n < names.data() + got; n += wcslen(n) + 1) sections.push_back(n);
    std::vector<wchar_t> pairs(32768, L'\0');
    for (const std::wstring &section : sections)
        for (int kind = 0; kind < kSetupKinds; kind++)
        {
            const size_t prefix = wcslen(kSetupIniPrefixes[kind]);
            if (section.size() <= prefix || _wcsnicmp(section.c_str(), kSetupIniPrefixes[kind], prefix)) continue;
            const std::wstring key = section.substr(prefix), file = SetupFile(kind, key);
            SetupBegin(kind, key);
            const DWORD length = GetPrivateProfileSectionW(section.c_str(), pairs.data(), (DWORD)pairs.size(), ini.c_str());
            for (const wchar_t *pair = pairs.data(); length && *pair && pair < pairs.data() + length; pair += wcslen(pair) + 1)
            {
                const wchar_t *eq = wcschr(pair, L'=');
                if (!eq || eq == pair) continue;
                const std::wstring name(pair, eq);
                wchar_t there[8] = {};
                if (!GetPrivateProfileStringW(kSetupSections[kind], name.c_str(), L"", there, 8, file.c_str()))
                    WritePrivateProfileStringW(kSetupSections[kind], name.c_str(), eq + 1, file.c_str());
            }
            WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, ini.c_str());
            moved[kind]++;
        }
}

// Every key of [Gauges] into the ini (WritePrivateProfileString keeps the comments, the keys it is not given and the
// other sections).
static void WriteIni(const GaugeSettings &s)
{
    const std::wstring ini = IniPath();
    auto num = [&](const wchar_t *key, float v) { IniPutFloat(kIniSection, key, v, ini); };
    auto whole = [&](const wchar_t *key, int v) { IniPutInt(kIniSection, key, v, ini); };
    whole(L"Layout", s.layout);
    whole(L"SwapDials", s.swapDials ? 1 : 0);
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
    whole(L"ShowClock", s.showClock ? 1 : 0);
    whole(L"ShowBurn", s.showBurn ? 1 : 0);
    whole(L"ShowLamps", s.showLamps ? 1 : 0);
    whole(L"OilGauge", s.oilGauge);
    whole(L"TachClass", s.tachClass);
    num(L"IdleRpm", s.idleRpm);
    num(L"RedlineRpm", s.redlineRpm);
    num(L"MaxRpm", s.maxRpm);
    num(L"SpeedMax", s.speedMax);
    num(L"BoostMax", s.boostMax);
    whole(L"BoostProfile", s.boostProfile);
    whole(L"Gearbox", s.gearbox);
    whole(L"RequireClutch", s.requireClutch ? 1 : 0);
    whole(L"RequireGearHeld", s.requireGearHeld ? 1 : 0);
    whole(L"SkipNeutral", s.skipNeutral ? 1 : 0);
    whole(L"Input", s.input);
    whole(L"Powertrain", s.powertrain ? 1 : 0);
    whole(L"EngineShare", s.engineShare);
    num(L"RollWarnDeg", s.rollWarnDeg);
    num(L"RollDangerDeg", s.rollDangerDeg);
    num(L"PitchDangerDeg", s.pitchDangerDeg);
    num(L"WarmUpMinutes", s.warmUpMinutes);
    num(L"OverheatMinutes", s.overheatMinutes);
    num(L"CoolDownMinutes", s.coolDownMinutes);
    num(L"AmbientC", s.ambientC);
    whole(L"AmbientByMap", s.ambientByMap ? 1 : 0);
    num(L"AirFullKmh", s.airFullKmh);
    num(L"OilWarmUpMinutes", s.oilWarmUpMinutes);
    num(L"OilWarnC", s.oilWarnC);
    whole(L"Damage", s.damage ? 1 : 0);
    num(L"HotDamagePercentPerMinute", s.hotDamage);
    num(L"HotDamageFrom", s.hotDamageFrom);
    num(L"ColdDamagePercentPerMinute", s.coldDamage);
    num(L"StallDamagePercent", s.stallDamage);
    whole(L"ClutchBurn", s.clutchBurn ? 1 : 0);
    whole(L"ToggleKey", s.toggleKey);
    WritePrivateProfileStringW(kIniSection, L"PadToggle", Wide(s.padToggle).c_str(), ini.c_str());
    whole(L"SoundVolume", s.soundVolume);
    whole(L"SoundsOnly", s.soundsOnly ? 1 : 0);
    whole(L"Demo", s.demo ? 1 : 0);
    whole(L"Probe", s.probe);
}

// ---- the outside temperature by map: the sections [Map:<name>] of Gauges.ini ----
// A region can have an outside temperature of its own (AmbientC in a section named after it, [Map:Michigan]), and a
// map one apart from its region's ([Map:Black River]); a map of a mod goes by its id ([Map:level_my_map]). They are
// in force while a truck drives there; elsewhere AmbientC of [Gauges] is. The names are the game's own (maps.h). The
// mod writes the regions' sections with a first guess into an ini that has no map section; one taken out by hand
// stays out.

static std::wstring MapSection(const char *name) { return L"Map:" + Wide(name); }

// The outside temperature of one section, held to its range. False when the ini has no such section with AmbientC.
static bool MapSectionAmbient(const char *name, float &out)
{
    if (!name[0]) return false;
    const std::wstring ini = IniPath(), section = MapSection(name);
    wchar_t buf[8] = {};
    if (!GetPrivateProfileStringW(section.c_str(), L"AmbientC", L"", buf, 8, ini.c_str())) return false;
    out = SettingsRange(IniFloatIn(section.c_str(), L"AmbientC", kTempOutside, ini), kAmbientRange[0], kAmbientRange[1]);
    return true;
}

// A section's air over the game's day: the day's warmest and the night's coldest, DayC and NightC where it has both,
// else AmbientC as the mean with kDaySwing between them.
struct MapAir
{
    float mean, day, night;
    bool dayNight; // DayC and NightC are the section's own
};

static bool MapSectionAir(const char *name, MapAir &out)
{
    if (!MapSectionAmbient(name, out.mean))
    {
        // a section with DayC and NightC needs no AmbientC
        const std::wstring ini = IniPath(), section = MapSection(name);
        wchar_t buf[8] = {};
        if (!name[0] || !GetPrivateProfileStringW(section.c_str(), L"DayC", L"", buf, 8, ini.c_str())) return false;
        out.mean = kTempOutside;
    }
    const std::wstring ini = IniPath(), section = MapSection(name);
    wchar_t day[16] = {}, night[16] = {};
    GetPrivateProfileStringW(section.c_str(), L"DayC", L"", day, 16, ini.c_str());
    GetPrivateProfileStringW(section.c_str(), L"NightC", L"", night, 16, ini.c_str());
    out.dayNight = day[0] && night[0];
    if (out.dayNight)
    {
        out.day = SettingsRange(IniFloatIn(section.c_str(), L"DayC", out.mean, ini), kAmbientRange[0], kAmbientRange[1]);
        out.night = SettingsRange(IniFloatIn(section.c_str(), L"NightC", out.mean, ini), kAmbientRange[0], kAmbientRange[1]);
        if (out.night > out.day)
        {
            const float t = out.day;
            out.day = out.night;
            out.night = t;
        }
        out.mean = (out.day + out.night) * 0.5f;
    }
    else
    {
        out.day = out.mean + kDaySwing * 0.5f;
        out.night = out.mean - kDaySwing * 0.5f;
    }
    return true;
}

// The air of the map with the game's id level: its own section, else its region's, else one named after the id.
// from: the section's name. False when none of them is in the ini.
static bool MapAmbient(const char *level, MapAir &out, char *from, size_t size)
{
    const char *name = MapNameOf(level);
    const MapRegion *region = MapRegionOf(level);
    const char *tries[3] = { name ? name : "", region ? region->name : "", level };
    for (const char *t : tries)
        if (MapSectionAir(t, out))
        {
            snprintf(from, size, "%s", t);
            return true;
        }
    from[0] = 0;
    return false;
}

// The section's air moved by delta C: DayC and NightC where it has them, else AmbientC.
static void MapAirShift(const char *name, const MapAir &air, float delta)
{
    const std::wstring ini = IniPath(), section = MapSection(name);
    if (air.dayNight)
    {
        IniPutFloat(section.c_str(), L"DayC", SettingsRange(air.day + delta, kAmbientRange[0], kAmbientRange[1]), ini);
        IniPutFloat(section.c_str(), L"NightC", SettingsRange(air.night + delta, kAmbientRange[0], kAmbientRange[1]), ini);
    }
    else IniPutFloat(section.c_str(), L"AmbientC", SettingsRange(air.mean + delta, kAmbientRange[0], kAmbientRange[1]), ini);
}

// ---- the game's clock (clock_scan.h): where it is, kept under [Clock] for the exe it was found in ----
static bool ClockLoad(DWORD stamp, ClockPath &p)
{
    const std::wstring ini = IniPath();
    wchar_t exe[16] = {}, place[64] = {};
    GetPrivateProfileStringW(L"Clock", L"Exe", L"", exe, 16, ini.c_str());
    GetPrivateProfileStringW(L"Clock", L"Place", L"", place, 64, ini.c_str());
    if (wcstoul(exe, nullptr, 16) != stamp || !place[0]) return false;
    char text[64];
    size_t n = 0;
    for (const wchar_t *c = place; *c && n + 1 < sizeof text; c++) text[n++] = *c < 128 ? (char)*c : '?';
    text[n] = 0;
    return ClockPathParse(text, p);
}

static void ClockSave(DWORD stamp, const ClockPath &p)
{
    const std::wstring ini = IniPath();
    wchar_t exe[16];
    swprintf_s(exe, L"%08lx", stamp);
    char text[64];
    ClockPathText(p, text, sizeof text);
    WritePrivateProfileStringW(L"Clock", L"Exe", exe, ini.c_str());
    WritePrivateProfileStringW(L"Clock", L"Place", Wide(text).c_str(), ini.c_str());
}

static void ClockForget()
{
    const std::wstring ini = IniPath();
    WritePrivateProfileStringW(L"Clock", nullptr, nullptr, ini.c_str());
}

// The exe's image size in memory, which the clock's search keeps its pointers out of.
static uint64_t ImageSize()
{
    const uint8_t *image = (const uint8_t *)g_base;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(image + ((const IMAGE_DOS_HEADER *)image)->e_lfanew);
    return nt->OptionalHeader.SizeOfImage;
}

// The names of the ini's sections.
static std::vector<std::wstring> IniSections(const std::wstring &ini)
{
    static wchar_t names[32768];
    std::vector<std::wstring> out;
    const DWORD n = GetPrivateProfileSectionNamesW(names, 32768, ini.c_str());
    for (const wchar_t *p = names; p < names + n && *p; p += wcslen(p) + 1) out.push_back(p);
    return out;
}

static bool IniHas(const std::vector<std::wstring> &sections, const std::wstring &name)
{
    for (const auto &s : sections)
        if (!_wcsicmp(s.c_str(), name.c_str())) return true;
    return false;
}

// Writes the regions' sections with their first temperatures when the ini has no map section at all. An ini from
// 0.11.0 to 0.11.2 has the placeholder names of then: the five that are not the game's names are renamed with the
// temperatures they hold, and the four regions they lacked are added. A region the game added after an ini got its
// sections (Switzerland, Season 19, in 0.16.0) is offered to it once: [Gauges] MapRegionsOffered lists the ones
// offered, so a section taken out by hand stays out.
static void MapDefaultsWrite()
{
    static const struct { const char *was, *now; } renamed[] = {
        { "kola", "Kola Peninsula" }, { "glades", "Belozersk Glades" }, { "british_columbia", "British Columbia" },
        { "north_carolina", "North Carolina" }, { "almaty", "Almaty Region" } };
    static const char *added[] = { "Quebec", "Washington", "Chiapas", "Zurdania" };
    const std::wstring ini = IniPath();
    const std::vector<std::wstring> sections = IniSections(ini);
    bool any = false, placeholders = false;
    for (const auto &name : sections)
        if (!_wcsnicmp(name.c_str(), L"Map:", 4)) any = true;
    for (const auto &r : renamed)
        if (IniHas(sections, MapSection(r.was))) placeholders = true;
    static const char *later[] = { "Switzerland" };
    std::wstring offered;
    {
        wchar_t b[512] = {};
        GetPrivateProfileStringW(L"Gauges", L"MapRegionsOffered", L"", b, 512, ini.c_str());
        offered = b;
    }
    bool more = false;
    for (const char *name : later)
    {
        const std::wstring w = Wide(name);
        if ((L"," + offered + L",").find(L"," + w + L",") != std::wstring::npos) continue;
        if (any)
            for (const auto &r : kMapRegions)
                if (!strcmp(r.name, name) && !IniHas(sections, MapSection(name))) IniPutFloat(MapSection(name).c_str(), L"AmbientC", r.ambientC, ini);
        offered += (offered.empty() ? L"" : L",") + w;
        more = true;
    }
    if (more) WritePrivateProfileStringW(L"Gauges", L"MapRegionsOffered", offered.c_str(), ini.c_str());
    if (!any)
    {
        for (const auto &r : kMapRegions) IniPutFloat(MapSection(r.name).c_str(), L"AmbientC", r.ambientC, ini);
        return;
    }
    if (!placeholders) return;
    for (const auto &r : renamed)
    {
        const std::wstring was = MapSection(r.was), now = MapSection(r.now);
        if (!IniHas(sections, was)) continue;
        wchar_t value[32] = {};
        GetPrivateProfileStringW(was.c_str(), L"AmbientC", L"", value, 32, ini.c_str());
        if (!IniHas(sections, now) && value[0]) WritePrivateProfileStringW(now.c_str(), L"AmbientC", value, ini.c_str());
        WritePrivateProfileStringW(was.c_str(), nullptr, nullptr, ini.c_str());
    }
    for (const char *name : added)
        for (const auto &r : kMapRegions)
            if (!strcmp(r.name, name) && !IniHas(sections, MapSection(name))) IniPutFloat(MapSection(name).c_str(), L"AmbientC", r.ambientC, ini);
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
            fwprintf(f, L"; Gauges: the gauge cluster of Next Gen Gameplay for the truck being driven, drawn by the mod itself.\n"
                        L"; The Gameplay tab of the NEXT GEN menu (F2 opens it, or the key in NextGen.ini) edits everything here in the game.\n"
                        L"; Layout: 0 = a strip along the bottom centre, 1 = a block in the bottom right corner.\n"
                        L"; SwapDials=1 puts the speedometer where the tachometer is and the tachometer where the speedometer is, in both layouts.\n"
                        L"; Style: 0 = solid dial faces, 1 = tinted see-through faces (FaceOpacity 0 to 0.85 sets how dark),\n"
                        L"; 2 = open: no faces, only the scales, numbers and needles.\n"
                        L"; OffsetX and OffsetY move the cluster from its place, in pixels of a 1080p screen (right and down).\n"
                        L"; Scale is its size (1 = as designed; it grows with the screen's height). Opacity 0.1 to 1.\n"
                        L"; Mph=1: speed in mph and range in miles. Psi=1: boost in psi. Fahrenheit=1: temperature in F.\n"
                        L"; ShowTemp, ShowBoost, ShowAir and ShowLamps switch the small dials and the lamps on (1) or off (0).\n"
                        L"; ShowClock=1 draws a clock dial with the game's time of day, the outside temperature in its face.\n"
                        L"; ShowBurn=1 draws the fuel burn a minute after the range under the fuel dial (litres a minute, gallons an hour with Mph).\n"
                        L"; OilGauge shows the oil temperature: 1 = a second needle (amber) in the temperature dial, 2 = a dial of its\n"
                        L"; own left of the temperature dial, 0 = not at all. The oil lamp is in the lamps either way.\n"
                        L"; TachClass sets the scales: 0 = truck (idle 650 rpm, red zone from 2200 of 3000, speedometer to 100 km/h),\n"
                        L"; 1 = scout (800, 5500 of 7000, 120 km/h), 2 = heavy (600, 1900 of 2500, 80 km/h).\n"
                        L"; IdleRpm, RedlineRpm, MaxRpm and SpeedMax (km/h) replace the class's numbers; 0 = the class's own.\n"
                        L"; The boost dial shows a turbo that blows by how far the pedal is down and how fast the engine turns.\n"
                        L"; BoostMax is the boost in bar it reaches with the pedal floored at high engine speed (0.3 to 2);\n"
                        L"; 0 = the class's own: trucks 1.8 bar, scouts 1, heavy trucks 2.\n"
                        L"; BoostProfile is how the turbo gets there: 0 = standard, 1 = small and quick (boost early, little lag),\n"
                        L"; 2 = big and slow (boost late, long to come at low revs), 3 = two stages (a small turbo early, a big one after).\n"
                        L"; A kind of truck can have these seven of its own. They stand in a file of its own in the folder Gauges\\trucks,\n"
                        L"; such as Gauges\\trucks\\chevrolet_ck1500.ini, which the mod writes when one of the seven is changed on the tab\n"
                        L"; while such a truck is driven; a file can be passed on and dropped in. The tab and Gauges.log name the truck.\n"
                        L"; A truck without a file takes the seven from [Gauges], and so does a truck's file for each of them it leaves out.\n"
                        L"; The tilt lamp is green while the truck stands level. It turns amber when the truck leans to a side by\n"
                        L"; RollWarnDeg degrees and red from RollDangerDeg, or when it points up or down a slope by PitchDangerDeg.\n"
                        L"; The coolant temperature is worked out from the fuel the engine burns (the pedal and the engine speed) and\n"
                        L"; how fast the truck drives.\n"
                        L"; WarmUpMinutes is the time a cold engine takes from 15 C to 85 C when it is driven as usual, OverheatMinutes\n"
                        L"; the time from 90 C into the red zone at 118 C floored at the red zone with the wheels spinning at a\n"
                        L"; standstill, CoolDownMinutes the time a stopped engine takes from 90 C down to 40 C. AmbientC is the outside\n"
                        L"; temperature in degrees C: a truck that has stood for long is that cold. The cold lamp is lit below 60 C.\n"
                        L"; A region can have an outside temperature of its own: AmbientC in a section named after it, such as\n"
                        L"; [Map:Michigan], in force while a truck drives there. A map can have one apart from its region's, in a\n"
                        L"; section named after the map, such as [Map:Black River]; a map of a mod goes by its id, such as\n"
                        L"; [Map:level_my_map]. The names are the game's own, and Gauges.log names the map. The mod learns the map\n"
                        L"; from the game's save, as the map loaded last. With AmbientByMap=1, the default, the map's section or its\n"
                        L"; region's is in force where the map is known; elsewhere, and everywhere with AmbientByMap=0, AmbientC is.\n"
                        L"; A section's air follows the game's day: DayC the warmest in the afternoon, NightC the coldest before\n"
                        L"; sunrise, such as DayC=3 and NightC=-12; a section with only AmbientC swings 10 C around it. The mod finds\n"
                        L"; the game's clock while you drive with Probe=1 and keeps its place under [Clock]; until then the mean holds.\n"
                        L"; AirFullKmh is the speed from which the radiator gets all the air that driving gives it (10 to 80 km/h).\n"
                        L"; Lower keeps a slow, hard pulling engine cooler.\n"
                        L"; The oil follows the coolant slowly and runs a little hotter under load. OilWarmUpMinutes is the time the oil\n"
                        L"; takes from 15 C to 85 C when the engine is driven as usual, a minute after the coolant at the least.\n"
                        L"; From OilWarnC (100 to 150 C) the oil lamp blinks and a warning sounds every few seconds.\n"
                        L"; Damage=1: the temperature costs the engine. An engine in the red zone takes the game's own engine damage:\n"
                        L"; HotDamagePercentPerMinute percent of what it can take a minute at 125 C, less below that and more above.\n"
                        L"; HotDamageFrom: the coolant temperature in C where the hot damage starts, 100 to 124 (118, the red zone's start); the rate above is reached at 125.\n"
                        L"; A cold engine (below 60 C) worked hard, burning more than half the fuel it burns floored at the red zone,\n"
                        L"; takes ColdDamagePercentPerMinute percent a minute floored at the red zone at 0 C, and less the warmer it is\n"
                        L"; and the lighter the work. A garage repairs this like any engine damage.\n"
                        L"; Damage=0: the temperature is only shown.\n"
                        L"; ColdDamagePercentPerMinute=0 switches the cold part off. Damage has not been tried in co-op: leave it at 0 there.\n"
                        L"; StallDamagePercent: what a shift without the clutch costs the gearbox, percent of what it takes, 0 to 25 (5), with Damage=1.\n"
                        L"; ClutchBurn=1: a clutch pedal (on an axis) slipped under load heats up and costs the gearbox, faster the stronger the engine, with Damage=1.\n"
                        L"; SoundVolume (0 to 100) is how loud the turbo is heard when the pedal comes up quickly under boost: it flutters,\n"
                        L"; longer and louder the more boost there was, until the pedal goes down again. The oil warning goes by it too.\n"
                        L"; 0 = no sound. SoundsOnly=1 plays the sounds and draws no cluster.\n"
                        L"; WAV files sounds\\turbo_high_1.wav to _4.wav next to the game's exe (and turbo_mid_, turbo_low_ for less boost)\n"
                        L"; are played in place of the mod's own recordings of that strength, and sounds\\oil_beep_1.wav in place of the\n"
                        L"; oil warning.\n"
                        L"; ToggleKey is the virtual key code of a key that hides and shows the cluster (0 = none; 120 = F9).\n"
                        L"; PadToggle does the same with pad buttons (XInput): DPadUp, DPadDown, DPadLeft, DPadRight, A, B, X, Y,\n"
                        L"; LB, RB, LS, RS, Start, Back, joined with + (LB+DPadUp), or None. The game sees these buttons too.\n"
                        L"; The cluster is on screen while a truck is driven in the world: not on loading screens, in menus, the garage or the map.\n"
                        L"; Gearbox: 0 the game's own. 1 the mod's automatic in A (a torque converter, shifts up later the further the pedal\n"
                        L"; is down, a kickdown floored), the game's shifter yours, only its reverse on the brake refused. 2 the mod's own\n"
                        L"; gearbox takes the shifter: A, the numbered gears, N, R, H, L-, L, L+ on the bindings below (as 1 until one reaches R).\n"
                        L"; [Gearbox] Bind<Action> and Bind<Action>2: two bindings each for Gear1 to Gear12, GearH, GearL-, GearL, GearL+, GearN,\n"
                        L"; GearR, GearA, GearUp, GearDown, Clutch, RangeHigh, RangeLow, and at every Gearbox level DiffLock and Awd (the\n"
                        L"; differential lock and all-wheel drive flipped, on every truck, the upgrade fitted or not; none by default).\n"
                        L"; A binding is Key <name or code>, Pad <buttons with +>,\n"
                        L"; Wheel <n> Button <n>, Wheel <n> POV [<n>] Up/Right/Down/Left, Wheel <n> Axis <n> [inverted] (the clutch), or None.\n"
                        L"; The tab's Gearbox section binds them by a press. Without a key in the ini the defaults below hold:\n"
                        L";   BindGearUp=Key Num 8\n"
                        L";   BindGearDown=Key Num 2\n"
                        L";   BindGearN=Key Num 5\n"
                        L";   BindGearR=Key Num 0\n"
                        L";   BindGearA=Key Num 7\n"
                        L"; RequireClutch=1 stalls the engine on a shift into a gear without the clutch, RequireGearHeld=1 goes to N while no\n"
                        L"; gear key is held (an H shifter with every gear bound), SkipNeutral=1 steps from gear 1 to reverse and back past N.\n"
                        L"; Input is what you drive with: 0 a wheel's pedals, 1 a pad's trigger, 2 the keyboard. The mod's automatic kicks\n"
                        L"; down by it: a trigger has to come from under three quarters, and keys kick nothing down and shift as floored.\n"
                        L"; Powertrain=1: the truck pulls with the mod's engine: its torque curve against the engine speed the needle\n"
                        L"; shows, less with cold oil and without boost, and doubled by the converter at its stall (Gearbox=1), in place\n"
                        L"; of the game's own, which falls away toward each gear's top. The mod writes the game's torque factor for it.\n"
                        L"; EngineShare: how much of the mod's engine the game gets, 0 to 100 percent (50: half way between the two).\n"
                        L"; Gauges\\gearboxes\\<name>.ini GearHold: seconds a gearbox holds each gear going up with Powertrain=1, 1 to 3 (the tab).\n"
                        L"; Gauges\\engines\\<name>.ini: PeakTorqueRpm, Aspiration (0 turbo, 1 naturally aspirated), TorqueCurve (the tab's Engine).\n"
                        L";   Torque: how much of its torque the truck gets, 10 to 100 percent (100 when not set). Lower it for an RLM-like engine with several times a stock engine's torque.\n"
                        L"; For work on the mod: Demo=1 draws demo values whenever no truck is driven, Probe=1 logs the values read from the game.\n");
            fclose(f);
            WriteIni(s);
            MapDefaultsWrite();
        }
        return;
    }
    MapDefaultsWrite();
    int moved[kSetupKinds];
    SetupsMove(moved);
    if (moved[kSetupTruck] || moved[kSetupGearbox])
        Log(L"settings: %d trucks' scales and %d gearboxes' holds moved out of Gauges.ini, each into a file of its own in Gauges\\trucks and "
            L"Gauges\\gearboxes", moved[kSetupTruck], moved[kSetupGearbox]);
    s.layout = IniInt(L"Layout", s.layout, ini);
    s.swapDials = IniBool(L"SwapDials", s.swapDials, ini);
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
    s.showClock = IniBool(L"ShowClock", s.showClock, ini);
    s.showBurn = IniBool(L"ShowBurn", s.showBurn, ini);
    s.showLamps = IniBool(L"ShowLamps", s.showLamps, ini);
    s.oilGauge = IniInt(L"OilGauge", s.oilGauge, ini);
    s.tachClass = IniInt(L"TachClass", s.tachClass, ini);
    s.idleRpm = IniFloat(L"IdleRpm", s.idleRpm, ini);
    s.redlineRpm = IniFloat(L"RedlineRpm", s.redlineRpm, ini);
    s.maxRpm = IniFloat(L"MaxRpm", s.maxRpm, ini);
    s.speedMax = IniFloat(L"SpeedMax", s.speedMax, ini);
    s.boostMax = IniFloat(L"BoostMax", s.boostMax, ini);
    s.boostProfile = IniInt(L"BoostProfile", s.boostProfile, ini);
    s.gearbox = IniInt(L"Gearbox", s.gearbox, ini);
    s.requireClutch = IniBool(L"RequireClutch", s.requireClutch, ini);
    s.requireGearHeld = IniBool(L"RequireGearHeld", s.requireGearHeld, ini);
    s.skipNeutral = IniBool(L"SkipNeutral", s.skipNeutral, ini);
    s.input = IniInt(L"Input", s.input, ini);
    s.powertrain = IniBool(L"Powertrain", s.powertrain, ini);
    s.engineShare = IniInt(L"EngineShare", s.engineShare, ini);
    s.rollWarnDeg = IniFloat(L"RollWarnDeg", s.rollWarnDeg, ini);
    s.rollDangerDeg = IniFloat(L"RollDangerDeg", s.rollDangerDeg, ini);
    s.pitchDangerDeg = IniFloat(L"PitchDangerDeg", s.pitchDangerDeg, ini);
    s.warmUpMinutes = IniFloat(L"WarmUpMinutes", s.warmUpMinutes, ini);
    s.overheatMinutes = IniFloat(L"OverheatMinutes", s.overheatMinutes, ini);
    s.coolDownMinutes = IniFloat(L"CoolDownMinutes", s.coolDownMinutes, ini);
    s.ambientC = IniFloat(L"AmbientC", s.ambientC, ini);
    s.ambientByMap = IniBool(L"AmbientByMap", s.ambientByMap, ini);
    s.airFullKmh = IniFloat(L"AirFullKmh", s.airFullKmh, ini);
    s.oilWarmUpMinutes = IniFloat(L"OilWarmUpMinutes", s.oilWarmUpMinutes, ini);
    s.oilWarnC = IniFloat(L"OilWarnC", s.oilWarnC, ini);
    s.damage = IniBool(L"Damage", s.damage, ini);
    s.hotDamage = IniFloat(L"HotDamagePercentPerMinute", s.hotDamage, ini);
    s.hotDamageFrom = IniFloat(L"HotDamageFrom", s.hotDamageFrom, ini);
    s.coldDamage = IniFloat(L"ColdDamagePercentPerMinute", s.coldDamage, ini);
    s.stallDamage = IniFloat(L"StallDamagePercent", s.stallDamage, ini);
    s.clutchBurn = IniBool(L"ClutchBurn", s.clutchBurn, ini);
    s.toggleKey = IniInt(L"ToggleKey", s.toggleKey, ini);
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(kIniSection, L"PadToggle", Wide(s.padToggle).c_str(), buf, 64, ini.c_str());
    size_t n = 0;
    for (const wchar_t *p = buf; *p && n + 1 < sizeof s.padToggle; p++) s.padToggle[n++] = *p < 128 ? (char)*p : '?';
    s.padToggle[n] = 0;
    s.soundVolume = IniInt(L"SoundVolume", s.soundVolume, ini);
    s.soundsOnly = IniBool(L"SoundsOnly", s.soundsOnly, ini);
    s.demo = IniBool(L"Demo", s.demo, ini);
    s.probe = IniInt(L"Probe", s.probe, ini);
    SettingsClamp(s);
}

LONG SettingsGet(GaugeSettings &out)
{
    AcquireSRWLockShared(&g_setLock);
    out = g_set;
    const LONG turn = g_setTurn;
    ReleaseSRWLockShared(&g_setLock);
    return turn;
}

// The tab made in from the settings of a turn. When the mod's thread has put others in force since (another truck
// is driven, the ini was read again), in is not an edit of those and is dropped.
void SettingsPut(const GaugeSettings &in, LONG turn)
{
    AcquireSRWLockExclusive(&g_setLock);
    if (turn == g_setTurn)
    {
        g_set = in;
        InterlockedExchange(&g_setDirty, 1);
    }
    ReleaseSRWLockExclusive(&g_setLock);
}

// The settings in force, for the tab to show. What the tab handed back and the mod's thread has not taken yet is
// dropped, as is what it hands back from here on for the settings before these: neither is an edit of these.
static void SettingsPublish(const GaugeSettings &s)
{
    AcquireSRWLockExclusive(&g_setLock);
    g_set = s;
    g_setTurn++;
    InterlockedExchange(&g_setDirty, 0);
    ReleaseSRWLockExclusive(&g_setLock);
}

void SettingsReload() { InterlockedExchange(&g_reloadAsked, 1); }
void SettingsTruckDrop() { InterlockedExchange(&g_truckDropAsked, 1); }
void SettingsReset() { InterlockedExchange(&g_resetAsked, 1); }
void PullTestAsk(bool run) { InterlockedExchange(&g_pullTestAsked, run ? 1 : 2); }
void MapAmbientSet(float c)
{
    InterlockedExchange(&g_mapAmbientTenths, (LONG)lroundf(c * 10.0f));
    InterlockedExchange(&g_mapAmbientAsked, 1);
}

void EngineProfileSet(float peakRpm, int aspiration)
{
    InterlockedExchange(&g_enginePeak, (LONG)lroundf(peakRpm));
    InterlockedExchange(&g_engineAspiration, aspiration);
    InterlockedExchange(&g_engineAsked, 1);
}

void EngineProfileDrop() { InterlockedExchange(&g_engineAsked, 2); }


void EngineTorqueSet(int percent)
{
    InterlockedExchange(&g_engineTorquePercent, percent);
    InterlockedExchange(&g_engineTorqueAsked, 1);
}

void GearHoldSet(float seconds)
{
    InterlockedExchange(&g_gearHoldTenths, (LONG)lroundf(seconds * 10.0f));
    InterlockedExchange(&g_gearHoldAsked, 1);
}

// ---- a gearbox's own hold: its file Gauges\gearboxes\<name>.ini ----
// GearHold: the seconds each gear of that gearbox is held going up with the mod's engine (kGearHoldRange); without
// it the truck's class has it (kEngineGearHold, kEngineGearHoldScout). The key is GearboxKey's: the gearbox's name in
// the game's files, or its gears' speeds where the name is not read.

// The gearbox's own hold, into hold; false where its file has none.
static bool GearHoldRead(const char *key, float &hold)
{
    wchar_t buf[16] = {};
    if (!key[0] || GetPrivateProfileStringW(kSetupSections[kSetupGearbox], L"GearHold", L"", buf, 16, SetupFile(kSetupGearbox, key).c_str()) == 0) return false;
    const float v = (float)_wtof(buf);
    if (!isfinite(v) || v <= 0.0f) return false;
    hold = SettingsRange(v, kGearHoldRange[0], kGearHoldRange[1]);
    return true;
}

static void GearHoldWrite(const char *key, float hold)
{
    wchar_t buf[16];
    swprintf_s(buf, L"%.1f", hold);
    SetupBegin(kSetupGearbox, Wide(key));
    WritePrivateProfileStringW(kSetupSections[kSetupGearbox], L"GearHold", buf, SetupFile(kSetupGearbox, key).c_str());
}

// ---- an engine's own profile: its file Gauges\engines\<name>.ini ----
// PeakTorqueRpm: the engine speed of its peak torque (100 to 20000 rpm), the curve built around it (EngineCurveFromPeak);
// Aspiration: 0 turbo, 1 naturally aspirated (no boost, no turbo sounds, no boost dial); TorqueCurve: a whole curve of
// its own, "x:y,x:y,..." with x the engine speed over the red zone's and y the torque over its most, in place of the
// one from the peak. Torque: how much of its torque
// the truck gets, in percent (10 to 100, kTorqueShareRange), the whole push the hand gives scaled by it (TorqueHeld).
// The key is the engine's name in the game's files (EngineRecord::name).
static const float kPeakRpmRange[2] = { 100.0f, 20000.0f };

// The engine's profile, into p; false where its file holds none of them.
static bool EngineProfileRead(const char *name, EngineProfile &p)
{
    p = EngineProfile{};
    if (!name[0]) return false;
    const std::wstring file = SetupFile(kSetupEngine, name);
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    const wchar_t *section = kSetupSections[kSetupEngine];
    wchar_t buf[256] = {};
    if (GetPrivateProfileStringW(section, L"PeakTorqueRpm", L"", buf, 256, file.c_str()) > 0)
    {
        const float v = (float)_wtof(buf);
        p.peakRpm = isfinite(v) && v > 0.0f ? SettingsRange(v, kPeakRpmRange[0], kPeakRpmRange[1]) : 0.0f;
        p.own = true;
    }
    if (GetPrivateProfileStringW(section, L"Aspiration", L"", buf, 256, file.c_str()) > 0)
    {
        const int a = _wtoi(buf);
        p.aspiration = a >= 0 && a < kAspCount ? a : kAspTurbo;
        p.own = true;
    }
    if (GetPrivateProfileStringW(section, L"TorqueCurve", L"", buf, 256, file.c_str()) > 0)
    {
        char text[256];
        size_t n = 0;
        for (; buf[n] && n + 1 < sizeof text; n++) text[n] = buf[n] < 128 ? (char)buf[n] : '?';
        text[n] = 0;
        p.custom = TorqueCurveParse(text, p.curve);
        p.own = true;
    }
    if (GetPrivateProfileStringW(section, L"Torque", L"", buf, 256, file.c_str()) > 0)
    {
        const float v = (float)_wtof(buf);
        p.torque = isfinite(v) && v > 0.0f ? SettingsRange(v / 100.0f, kTorqueShareRange[0], kTorqueShareRange[1]) : 0.0f;
        p.own = true;
    }
    return p.own;
}

// The tab's Engine section into the engine's file: the peak torque's speed and the aspiration.
static void EngineProfileWrite(const char *name, float peakRpm, int aspiration)
{
    const std::wstring file = SetupFile(kSetupEngine, name);
    SetupBegin(kSetupEngine, Wide(name));
    IniPutFloat(kSetupSections[kSetupEngine], L"PeakTorqueRpm", roundf(SettingsRange(peakRpm, kPeakRpmRange[0], kPeakRpmRange[1])), file);
    IniPutInt(kSetupSections[kSetupEngine], L"Aspiration", aspiration >= 0 && aspiration < kAspCount ? aspiration : kAspTurbo, file);
}

// The tab's slider into the engine's file: how much of its torque the truck gets, in percent.
static void EngineTorqueWrite(const char *name, int percent)
{
    const std::wstring file = SetupFile(kSetupEngine, name);
    SetupBegin(kSetupEngine, Wide(name));
    IniPutInt(kSetupSections[kSetupEngine], L"Torque", (int)lroundf(SettingsRange((float)percent, kTorqueShareRange[0] * 100.0f, kTorqueShareRange[1] * 100.0f)), file);
}

// ---- a truck's own scales: its file Gauges\trucks\<kind>.ini ----
// A truck can have seven settings of its own: TachClass, IdleRpm, RedlineRpm, MaxRpm, SpeedMax, BoostMax and BoostProfile. They are written
// when one of them is changed on the tab while that truck is driven, and take the place of the plain ones of
// [Gauges] whenever that truck is driven again. The key is the truck's as truck.h makes it (TruckKey).

// Lays the truck's own scales over s: each of the seven its file holds. One the file does not hold stays as it is in
// s. False when the file holds none of them, or is not there.
static bool ReadTruckIni(const char *key, GaugeSettings &s)
{
    const std::wstring ini = SetupFile(kSetupTruck, key), section = kSetupSections[kSetupTruck];
    bool any = false;
    auto has = [&](const wchar_t *name) {
        wchar_t buf[8] = {};
        const bool there = GetPrivateProfileStringW(section.c_str(), name, L"", buf, 8, ini.c_str()) > 0;
        any = any || there;
        return there;
    };
    if (has(L"TachClass")) s.tachClass = (int)GetPrivateProfileIntW(section.c_str(), L"TachClass", s.tachClass, ini.c_str());
    if (has(L"IdleRpm")) s.idleRpm = IniFloatIn(section.c_str(), L"IdleRpm", s.idleRpm, ini);
    if (has(L"RedlineRpm")) s.redlineRpm = IniFloatIn(section.c_str(), L"RedlineRpm", s.redlineRpm, ini);
    if (has(L"MaxRpm")) s.maxRpm = IniFloatIn(section.c_str(), L"MaxRpm", s.maxRpm, ini);
    if (has(L"SpeedMax")) s.speedMax = IniFloatIn(section.c_str(), L"SpeedMax", s.speedMax, ini);
    if (has(L"BoostMax")) s.boostMax = IniFloatIn(section.c_str(), L"BoostMax", s.boostMax, ini);
    if (has(L"BoostProfile")) s.boostProfile = (int)GetPrivateProfileIntW(section.c_str(), L"BoostProfile", s.boostProfile, ini.c_str());
    if (any) SettingsClamp(s);
    return any;
}

// Writes the truck's file: the seven scales of s.
static void WriteTruckIni(const char *key, const GaugeSettings &s)
{
    const std::wstring ini = SetupFile(kSetupTruck, key), section = kSetupSections[kSetupTruck];
    SetupBegin(kSetupTruck, Wide(key));
    IniPutInt(section.c_str(), L"TachClass", s.tachClass, ini);
    IniPutFloat(section.c_str(), L"IdleRpm", s.idleRpm, ini);
    IniPutFloat(section.c_str(), L"RedlineRpm", s.redlineRpm, ini);
    IniPutFloat(section.c_str(), L"MaxRpm", s.maxRpm, ini);
    IniPutFloat(section.c_str(), L"SpeedMax", s.speedMax, ini);
    IniPutFloat(section.c_str(), L"BoostMax", s.boostMax, ini);
    IniPutInt(section.c_str(), L"BoostProfile", s.boostProfile, ini);
}

// Takes the truck's seven scales out of its file, and the file with them when nothing else is in it.
static void DropTruckIni(const char *key)
{
    const std::wstring ini = SetupFile(kSetupTruck, key), section = kSetupSections[kSetupTruck];
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    for (const wchar_t *name : { L"TachClass", L"IdleRpm", L"RedlineRpm", L"MaxRpm", L"SpeedMax", L"BoostMax", L"BoostProfile", L"Kind" })
        WritePrivateProfileStringW(section.c_str(), name, nullptr, ini.c_str());
    wchar_t rest[8] = {};
    if (!GetPrivateProfileSectionW(section.c_str(), rest, 8, ini.c_str())) DeleteFileW(ini.c_str());
    else SetupBegin(kSetupTruck, Wide(key)); // something else stays: the kind with it
}

// The seven scales a truck can have of its own: copied from one set of settings into another, and compared.
static void ScalesCopy(GaugeSettings &to, const GaugeSettings &from)
{
    to.tachClass = from.tachClass;
    to.idleRpm = from.idleRpm;
    to.redlineRpm = from.redlineRpm;
    to.maxRpm = from.maxRpm;
    to.speedMax = from.speedMax;
    to.boostMax = from.boostMax;
    to.boostProfile = from.boostProfile;
}

static bool ScalesSame(const GaugeSettings &a, const GaugeSettings &b)
{
    return a.tachClass == b.tachClass && a.idleRpm == b.idleRpm && a.redlineRpm == b.redlineRpm && a.maxRpm == b.maxRpm && a.speedMax == b.speedMax &&
           a.boostMax == b.boostMax && a.boostProfile == b.boostProfile;
}

// The settings as the mod's thread keeps them. plain is what [Gauges] holds. s is what is in force: plain, with the
// driven truck's own scales in the place of the plain ones where it has some (own). key is the driven truck's, ""
// while none is driven. changed: the truck's scales were changed on the tab and are not in the ini yet. The five
// functions below are all that changes a state once the ini is read into plain.
struct SettingsState { GaugeSettings plain, s; char key[kMaxText]; bool own, changed; };

// The settings in force for a truck (key "" for none), from the plain ones and the ini.
static void SettingsFor(SettingsState &st, const char *key)
{
    strcpy_s(st.key, key);
    st.s = st.plain;
    st.own = key[0] && ReadTruckIni(key, st.s);
    st.changed = false;
}

// An edit from the tab, in. A scale changed while a truck is driven makes the seven scales that truck's own, and
// they stay out of the plain ones from then on. Everything else on the tab is every truck's.
static void SettingsTake(SettingsState &st, const GaugeSettings &in)
{
    if (st.key[0] && !ScalesSame(in, st.s)) st.own = st.changed = true;
    const GaugeSettings before = st.plain;
    st.plain = in;
    if (st.own) ScalesCopy(st.plain, before);
    st.s = in;
}

// The driven truck gives up its own scales: they go out of the ini, and the plain ones are in force for it again.
static void SettingsDrop(SettingsState &st)
{
    DropTruckIni(st.key);
    ScalesCopy(st.s, st.plain);
    st.own = st.changed = false;
}

// Every setting back to its default. The driven truck gives up its own scales; other trucks keep theirs.
static void SettingsToDefaults(SettingsState &st)
{
    if (st.own) DropTruckIni(st.key);
    SettingsDefaults(st.plain);
    st.s = st.plain;
    st.own = st.changed = false;
}

// Into the ini: the plain settings, and the driven truck's own scales into its section when they were changed. A
// truck's section that the tab did not change stays as it is, with what it leaves to [Gauges].
static void SettingsWrite(SettingsState &st)
{
    WriteIni(st.plain);
    if (st.own && st.changed) WriteTruckIni(st.key, st.s);
    st.changed = false;
}

// ---- the gearbox's bindings: the [Gearbox] section of Gauges.ini ----
// One key per action and slot, Bind<Action> and Bind<Action>2, in the reader's text forms (input_bindings.h:
// "Key Num 8", "Pad LB+DPadDown", "Wheel 1 Button 5", "Wheel 1 POV Up", "Wheel 1 Axis 3 inverted", "None"). A key
// not there keeps the default: up, down, N, R and A on the number pad, which the game, the shipped ReShade.ini (Home
// alone) and the usual mods leave alone.
static_assert(kActCount <= kViewActions, "the view holds the bindings of every action");
static_assert(kActDiffLock == kViewShifts && kActAwd == kViewShifts + 1, "the tab's rows of the locks follow the shifter's");
static const wchar_t *const kBindSection = L"Gearbox";

static void BindingsDefault(GearBindings &b)
{
    b = GearBindings{};
    static const struct { int action, vk; } keys[] = { { kActUp, VK_NUMPAD8 }, { kActDown, VK_NUMPAD2 }, { kActGearN, VK_NUMPAD5 }, { kActGearR, VK_NUMPAD0 },
                                                       { kActGearA, VK_NUMPAD7 } };
    for (const auto &k : keys) b.slot[k.action][0] = Binding{ kBindKey, 0, (uint32_t)k.vk, 0, false };
}

static void BindingKey(int action, int slot, wchar_t *out, size_t size) { swprintf_s(out, size, L"Bind%S%s", kActionNames[action], slot ? L"2" : L""); }

// Whether the ini holds a binding key yet.
static bool BindingsInIni(const std::wstring &ini)
{
    static wchar_t keys[16384];
    if (!GetPrivateProfileSectionW(kBindSection, keys, 16384, ini.c_str())) return false;
    for (const wchar_t *p = keys; *p; p += wcslen(p) + 1)
        if (!_wcsnicmp(p, L"Bind", 4)) return true;
    return false;
}

// The bindings the ini holds over b; a text that is no binding is said with why (say) and leaves the slot as it was.
static void BindingsRead(const std::wstring &ini, GearBindings &b, bool say)
{
    for (int a = 0; a < kActCount; a++)
        for (int slot = 0; slot < kBindingSlots; slot++)
        {
            wchar_t key[48], buf[128];
            BindingKey(a, slot, key, 48);
            GetPrivateProfileStringW(kBindSection, key, L"\x01", buf, 128, ini.c_str());
            if (buf[0] == 1) continue; // not there
            char text[128];
            size_t n = 0;
            for (; buf[n] && n + 1 < sizeof text; n++) text[n] = buf[n] < 128 ? (char)buf[n] : '?';
            text[n] = 0;
            Binding bind = {};
            char why[96] = "";
            if (!text[0] || BindingParse(text, bind, why, sizeof why)) b.slot[a][slot] = bind;
            else if (say) Log(L"gearbox: %s=%S is no binding (%S), so it is left as it was", key, text, why);
        }
}

static void BindingsWrite(const std::wstring &ini, const GearBindings &b)
{
    for (int a = 0; a < kActCount; a++)
        for (int slot = 0; slot < kBindingSlots; slot++)
        {
            wchar_t key[48];
            char text[96];
            BindingKey(a, slot, key, 48);
            BindingText(b.slot[a][slot], text, sizeof text);
            WritePrivateProfileStringW(kBindSection, key, Wide(text).c_str(), ini.c_str());
        }
}

// Whether a binding that can be pressed now reaches reverse (Gear down, Reverse, or Neutral on the way): the mod's own
// gearbox takes the shifter only then, so a truck can always be backed up. A key counts always, a wheel's binding only
// while that wheel is there (s, read whether or not the game is in front), a pad's only while a pad answers (review 8,
// W5).
static bool BindingsCanReverse(const GearBindings &b, const InputSnapshot &s, bool padThere)
{
    auto reaches = [&](const Binding &x) {
        switch (x.kind)
        {
        case kBindKey: return true;
        case kBindPad: return padThere;
        case kBindButton:
        case kBindPov:
        case kBindAxis: return x.device >= 1 && x.device <= kInputDevicesMost && s.device[x.device - 1].present;
        default: return false;
        }
    };
    for (int slot = 0; slot < kBindingSlots; slot++)
        if (reaches(b.slot[kActDown][slot]) || reaches(b.slot[kActGearR][slot]) || reaches(b.slot[kActGearN][slot])) return true;
    return false;
}

// Why a capture may not bind what it caught: the NEXT GEN menu's key, the cluster's toggle key, or pad buttons that
// hold the PadToggle chord or are held in it, as both would then act at once (review 8, W1 and W9). nullptr: it may.
static const char *BindingClash(const Binding &b, int menuKey, int toggleKey, uint32_t padChord)
{
    if (b.kind == kBindKey && menuKey && (int)b.code == menuKey) return "the NEXT GEN menu's key";
    if (b.kind == kBindKey && toggleKey && (int)b.code == toggleKey) return "the cluster's toggle key (ToggleKey)";
    if (b.kind == kBindPad && padChord && ((b.code & padChord) == b.code || (b.code & padChord) == padChord)) return "the cluster's pad toggle (PadToggle)";
    return nullptr;
}

// The bound actions in the log, one line each.
static void BindingsLog(const GearBindings &b)
{
    for (int a = 0; a < kActCount; a++)
    {
        char first[96], second[96];
        BindingText(b.slot[a][0], first, sizeof first);
        BindingText(b.slot[a][1], second, sizeof second);
        if (b.slot[a][0].kind && b.slot[a][1].kind) Log(L"gearbox: %S = %S, %S", kActionNames[a], first, second);
        else if (b.slot[a][0].kind || b.slot[a][1].kind) Log(L"gearbox: %S = %S", kActionNames[a], b.slot[a][0].kind ? first : second);
    }
}

// The manual transmission mod's SMT.ini, taken over once: its [KEYBOARD] bindings (Kb.<n>, a DirectInput scan code,
// turned into a Windows key; a combination of keys or another code is left) into the first slot of each matching
// action, and its [OPTIONS] REQUIRE CLUTCH, REQUIRE GEAR HELD and SKIP NEUTRAL. Its [CONTROLLER] section is left (its
// codes are OIS's own, not Windows'), and so are SHOW MENU and HANDBRAKE. Nothing in SMT.ini is
// written. The number of bindings taken; taken and left name them for the log.
static int SmtIniImport(const std::wstring &path, GearBindings &b, GaugeSettings &s, std::wstring &taken, std::wstring &left)
{
    struct Pair { wchar_t smt[16]; int action; };
    Pair pairs[kActCount] = {};
    int count = 0;
    for (int g = 0; g < 12; g++)
    {
        swprintf_s(pairs[count].smt, L"GEAR %d", g + 1);
        pairs[count++].action = kActGear1 + g;
    }
    static const struct { const wchar_t *smt; int action; } named[] = { { L"GEAR H", kActGearH }, { L"GEAR L-", kActGearLMinus }, { L"GEAR L", kActGearL },
                                                                         { L"GEAR L+", kActGearLPlus }, { L"GEAR N", kActGearN }, { L"GEAR R", kActGearR },
                                                                         { L"GEAR UP", kActUp }, { L"GEAR DOWN", kActDown }, { L"CLUTCH", kActClutch },
                                                                         { L"RANGE HIGH", kActRangeHigh }, { L"RANGE LOW", kActRangeLow },
                                                                         { L"DIFF LOCK", kActDiffLock }, { L"AWD", kActAwd } };
    for (const auto &n : named)
    {
        wcscpy_s(pairs[count].smt, n.smt);
        pairs[count++].action = n.action;
    }
    // the numpad's codes 0x47 to 0x53: Windows maps them to the arrows, Home, End and the rest of that block, which
    // NumLock on makes VK_NUMPADn again, so they are named here (review 8, W6)
    static const UINT numpad[] = { VK_NUMPAD7, VK_NUMPAD8, VK_NUMPAD9, VK_SUBTRACT, VK_NUMPAD4, VK_NUMPAD5, VK_NUMPAD6,
                                   VK_ADD,     VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3,  VK_NUMPAD0, VK_DECIMAL };
    bool took[kActCount] = {};
    int bound = 0;
    for (int i = 0; i < count; i++)
    {
        wchar_t value[64];
        GetPrivateProfileStringW(L"KEYBOARD", pairs[i].smt, L"", value, 64, path.c_str());
        if (!value[0] || !_wcsicmp(value, L"NONE")) continue;
        const wchar_t *digits = !_wcsnicmp(value, L"Kb.", 3) ? value + 3 : nullptr;
        wchar_t *end = nullptr;
        const long code = digits ? wcstol(digits, &end, 10) : -1;
        // an extended key's code has its high bit set: Windows reads it as 0xE0 and the low seven bits
        const UINT scan = code >= 0x80 ? (0xE000u | (UINT)(code & 0x7F)) : (UINT)code;
        const bool valid = code > 0 && code < 0x100 && end && !*end;
        const UINT vk = !valid ? 0 : code >= 0x47 && code <= 0x53 ? numpad[code - 0x47] : MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX);
        wchar_t line[96];
        if (vk)
        {
            b.slot[pairs[i].action][0] = Binding{ kBindKey, 0, vk, 0, false };
            took[pairs[i].action] = true;
            bound++;
            swprintf_s(line, L"%s%s", taken.empty() ? L"" : L", ", pairs[i].smt);
            taken += line;
        }
        else
        {
            swprintf_s(line, L"%s%s (%s)", left.empty() ? L"" : L", ", pairs[i].smt, value);
            left += line;
        }
    }
    // a default whose key the import gave to another action goes, so one key does not shift two ways
    for (int a = 0; a < kActCount; a++)
        for (int slot = 0; slot < kBindingSlots && !took[a]; slot++)
        {
            const Binding &d = b.slot[a][slot];
            for (int t = 0; t < kActCount && d.kind == kBindKey; t++)
                if (took[t] && b.slot[t][0].code == d.code)
                {
                    char key[32];
                    KeyText((int)d.code, key, sizeof key);
                    wchar_t line[96];
                    swprintf_s(line, L"%sthe default of %S (%S, now %S's)", left.empty() ? L"" : L", ", kActionNames[a], key, kActionNames[t]);
                    left += line;
                    b.slot[a][slot] = Binding{};
                    break;
                }
        }
    auto option = [&](const wchar_t *key, bool &out) {
        wchar_t value[16];
        GetPrivateProfileStringW(L"OPTIONS", key, L"", value, 16, path.c_str());
        if (value[0]) out = !_wcsicmp(value, L"true") || !_wcsicmp(value, L"1") || !_wcsicmp(value, L"yes");
    };
    option(L"REQUIRE CLUTCH", s.requireClutch);
    option(L"REQUIRE GEAR HELD", s.requireGearHeld);
    option(L"SKIP NEUTRAL", s.skipNeutral);
    return bound;
}

// The gearbox's actions from the reader's states: the edges for up, down, the positions and the gears, the gear key
// held (RequireGearHeld), the clutch held, and its travel where it is bound to an axis (-1 without one).
static GearboxActions ActionsOf(const ActionState st[kActCount], const GearBindings &b)
{
    GearboxActions a = {};
    a.up = st[kActUp].pressed;
    a.down = st[kActDown].pressed;
    for (int g = 0; g < 12; g++)
    {
        if (st[kActGear1 + g].pressed && !a.gearPressed) a.gearPressed = g + 1;
        if (st[kActGear1 + g].held && !a.gearHeld) a.gearHeld = g + 1;
    }
    a.a = st[kActGearA].pressed;
    a.n = st[kActGearN].pressed;
    a.r = st[kActGearR].pressed;
    a.h = st[kActGearH].pressed;
    a.lowMinus = st[kActGearLMinus].pressed;
    a.low = st[kActGearL].pressed;
    a.lowPlus = st[kActGearLPlus].pressed;
    a.rangeHigh = st[kActRangeHigh].pressed;
    a.rangeLow = st[kActRangeLow].pressed;
    a.clutch = st[kActClutch].held;
    const bool axis = b.slot[kActClutch][0].kind == kBindAxis || b.slot[kActClutch][1].kind == kBindAxis;
    a.clutchTravel = axis ? st[kActClutch].travel : -1.0f;
    return a;
}

// The actions while the NEXT GEN menu is open or a capture runs: none goes down, and what was held (the gear key under
// RequireGearHeld, the clutch and its travel) stands as it was, so the position stays where it is (review 8, W2).
static GearboxActions ActionsResting(const GearboxActions &before)
{
    GearboxActions a = {};
    a.gearHeld = before.gearHeld;
    a.clutch = before.clutch;
    a.clutchTravel = before.clutchTravel;
    return a;
}

// The snapshot as the gearbox and a capture read it: the pad and the wheels only while the game is in front, as the
// keys (review 8, W7).
static InputSnapshot InputFrontOnly(const InputSnapshot &s)
{
    InputSnapshot out = s;
    if (!s.front)
    {
        memset(out.key, 0, sizeof out.key);
        out.pad = 0;
        for (InputDevice &d : out.device) d = InputDevice{};
    }
    return out;
}

// The tab's asks of the bindings (render thread), done by the mod's thread on its next pass.
static volatile LONG g_bindAsk = 0;
void BindingAsk(int what, int action, int slot) { InterlockedExchange(&g_bindAsk, (LONG)((what << 16) | ((action & 0xFF) << 8) | (slot & 0xFF))); }

// The tab's test buttons (render thread): the points wait here until the mod's thread writes them on its next pass.
static volatile LONG g_damageTest = 0;
void DamageTestAsk(int points) { InterlockedExchangeAdd(&g_damageTest, points); }

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
static volatile LONG g_padsConnected = 0xF; // the slots that answered when all were last asked

uint32_t PadButtons()
{
    static volatile LONG askedAt = 0;
    volatile LONG &connected = g_padsConnected;
    const XInputGetStateFn get = PadFunction();
    if (!get)
    {
        InterlockedExchange(&connected, 0);
        return 0;
    }
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

// Whether a pad answered when PadButtons last asked every slot.
static bool PadPresent() { return g_padsConnected != 0; }

// A path for the log with the Windows profile folder written %USERPROFILE%: the log is passed on, the account's name stays out.
static std::wstring PathForLog(const std::wstring &path)
{
    wchar_t profile[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (n > 0 && n < MAX_PATH && path.size() >= n && !_wcsnicmp(path.c_str(), profile, n)) return L"%USERPROFILE%" + path.substr(n);
    return path;
}

// The saves' names, by the store: Steam's and the Microsoft Store's .cfg, Epic's .dat.
static const wchar_t *const kSavePatterns[2] = { L"CompleteSave*.cfg", L"CompleteSave*.dat" };

// A save and when it was written.
typedef std::pair<std::wstring, FILETIME> SaveFile;

// Saves newest first; of two written at once, the one found first.
static void SavesNewestFirst(std::vector<SaveFile> &saves)
{
    std::stable_sort(saves.begin(), saves.end(), [](const SaveFile &a, const SaveFile &b) { return CompareFileTime(&a.second, &b.second) > 0; });
}

// The saves in the folders, newest first.
static void SavesIn(const std::vector<std::wstring> &folders, std::vector<SaveFile> &out)
{
    for (const auto &dir : folders)
        for (const wchar_t *pattern : kSavePatterns)
        {
            WIN32_FIND_DATAW f;
            const HANDLE h = FindFirstFileW((dir + L"\\" + pattern).c_str(), &f);
            if (h == INVALID_HANDLE_VALUE) continue;
            do
                if (!(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back({ dir + L"\\" + f.cFileName, f.ftLastWriteTime });
            while (FindNextFileW(h, &f));
            FindClose(h);
        }
    SavesNewestFirst(out);
}

// What a save says of the map: the one loaded last, an empty slot (SaveSlotEmpty), or nothing yet (being written, or
// not to be read).
enum SaveSays { kSaveUnread, kSaveEmpty, kSaveMap };

static SaveSays SaveReadMap(const std::wstring &path, char *level, size_t size)
{
    const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return kSaveUnread;
    LARGE_INTEGER length = {};
    std::string text;
    bool ok = GetFileSizeEx(h, &length) && length.QuadPart > 0 && length.QuadPart < (64LL << 20);
    if (ok)
    {
        text.resize((size_t)length.QuadPart);
        DWORD got = 0;
        ok = ReadFile(h, &text[0], (DWORD)text.size(), &got, nullptr) && got == text.size();
    }
    CloseHandle(h);
    if (!ok) return kSaveUnread;
    if (MapFromSave(text.data(), text.size(), level, size)) return kSaveMap;
    return SaveSlotEmpty(text.data(), text.size()) ? kSaveEmpty : kSaveUnread;
}

// The map of the slot being played, from the saves (newest first): the newest save that names one. Empty slots are
// passed over, as the game writes one a few seconds after the slot played; taken as the newest, it hid the map that
// changed. The look ends with nothing new at a save no newer than the one read last (written at lastWritten), and at
// a save that cannot be read yet (being written, unread names it), so an older slot does not take its place.
static bool SavePick(const std::vector<SaveFile> &saves, const FILETIME &lastWritten, SaveFile &picked, char *level, size_t size, std::wstring &unread)
{
    unread.clear();
    for (const auto &save : saves)
    {
        if (CompareFileTime(&save.second, &lastWritten) <= 0) return false;
        const SaveSays says = SaveReadMap(save.first, level, size);
        if (says == kSaveEmpty) continue;
        if (says == kSaveUnread)
        {
            unread = save.first;
            return false;
        }
        picked = save;
        return true;
    }
    return false;
}



#ifndef GAUGES_SETTINGS_TEST
// The game's folder in the log at the start, and the store its path suggests: where its saves are follows from it.
static void LogGameFolder()
{
    std::wstring lower = g_dir;
    for (auto &c : lower) c = (wchar_t)towlower(c);
    const wchar_t *store = lower.find(L"\\steamapps\\") != std::wstring::npos ? L"a Steam library"
                           : lower.find(L"epic games") != std::wstring::npos ? L"the Epic store's folder"
                           : lower.find(L"windowsapps") != std::wstring::npos || lower.find(L"xboxgames") != std::wstring::npos ? L"the Microsoft Store's folder"
                           : L"a folder that does not tell the store";
    Log(L"game: its folder %s, by its path %s", PathForLog(g_dir).c_str(), store);
}

// ---- the map a truck is on: the game's save says which map it loaded last (maps.h) ----
// Steam keeps the saves in userdata\<account>\1465360\remote of its own folder as CompleteSave*.cfg, the Epic store in
// Documents\My Games\SnowRunner\base\storage\<account> as CompleteSave*.dat (the same save under another name, as
// the community's guides have it). The newest that names a map is the slot being played (SavePick). Documents is the shell's
// (which OneDrive may have moved) and the profile's own, for a game that writes to the latter. The
// Microsoft Store's build keeps them in the app's containers, %LOCALAPPDATA%\Packages\<the game's package>\SystemAppData\wgs,
// as files without names of their own: one whose first bytes name a CompleteSave is taken as such a file. It is
// looked for every kSaveLookMs and read when it is another file or was written since.
static const ULONGLONG kSaveLookMs = 5000;

// The folders that may hold the saves.
static std::vector<std::wstring> SaveFolders()
{
    std::vector<std::wstring> roots, out;
    auto subfolders = [](const std::wstring &dir, const wchar_t *tail, std::vector<std::wstring> &into) {
        WIN32_FIND_DATAW f;
        const HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &f);
        if (h == INVALID_HANDLE_VALUE) return;
        do
            if ((f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wcscmp(f.cFileName, L".") && wcscmp(f.cFileName, L".."))
                into.push_back(dir + L"\\" + f.cFileName + tail);
        while (FindNextFileW(h, &f));
        FindClose(h);
    };
    // Steam's own folder from the registry, and five folders up from the game's (steamapps\common\Snowrunner\Sources\Bin)
    // for a game in Steam's own library
    wchar_t steam[MAX_PATH] = {};
    DWORD size = sizeof steam;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, steam, &size) == ERROR_SUCCESS && steam[0])
    {
        for (wchar_t *p = steam; *p; p++)
            if (*p == L'/') *p = L'\\';
        roots.push_back(steam);
    }
    std::wstring up = g_dir;
    for (int i = 0; i < 5 && !up.empty(); i++)
    {
        while (!up.empty() && up.back() == L'\\') up.pop_back();
        const size_t cut = up.find_last_of(L'\\');
        up = cut == std::wstring::npos ? L"" : up.substr(0, cut);
    }
    if (!up.empty()) roots.push_back(up);
    for (const auto &root : roots) subfolders(root + L"\\userdata", L"\\1465360\\remote", out);
    std::vector<std::wstring> documents;
    wchar_t docs[MAX_PATH] = {};
    if (SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docs) == S_OK && docs[0]) documents.push_back(docs);
    wchar_t profile[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) > 0)
    {
        const std::wstring own = std::wstring(profile) + L"\\Documents";
        if (documents.empty() || _wcsicmp(documents[0].c_str(), own.c_str())) documents.push_back(own);
    }
    for (const auto &d : documents)
    {
        const std::wstring storage = d + L"\\My Games\\SnowRunner\\base\\storage";
        out.push_back(storage);
        subfolders(storage, L"", out);
    }
    return out;
}


// The files of the Microsoft Store build's save containers: wgs\<account>\<container>\<file> in each package of the game.
static void SaveContainerFiles(std::vector<SaveFile> &out)
{
    wchar_t local[MAX_PATH] = {};
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local) != S_OK) return;
    auto each = [](const std::wstring &dir, bool folders, const auto &take) {
        WIN32_FIND_DATAW f;
        const HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &f);
        if (h == INVALID_HANDLE_VALUE) return;
        do
            if (wcscmp(f.cFileName, L".") && wcscmp(f.cFileName, L"..") && !(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == !folders)
                take(dir + L"\\" + f.cFileName, f);
        while (FindNextFileW(h, &f));
        FindClose(h);
    };
    WIN32_FIND_DATAW p;
    const std::wstring packages = std::wstring(local) + L"\\Packages";
    const HANDLE h = FindFirstFileW((packages + L"\\*SnowRunner*").c_str(), &p);
    if (h == INVALID_HANDLE_VALUE) return;
    do
        if (p.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            each(packages + L"\\" + p.cFileName + L"\\SystemAppData\\wgs", true, [&](const std::wstring &account, const WIN32_FIND_DATAW &) {
                each(account, true, [&](const std::wstring &container, const WIN32_FIND_DATAW &) {
                    each(container, false, [&](const std::wstring &file, const WIN32_FIND_DATAW &f) { out.push_back({ file, f.ftLastWriteTime }); });
                });
            });
    while (FindNextFileW(h, &p));
    FindClose(h);
}

// Whether the file's first bytes name a CompleteSave (a save of the game's progress, as the Microsoft Store build's
// containers hold it).
static bool SaveHeadIsComplete(const std::wstring &path)
{
    const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char head[96] = {};
    DWORD got = 0;
    const bool read = ReadFile(h, head, sizeof head - 1, &got, nullptr) != 0;
    CloseHandle(h);
    return read && got > 0 && strstr(head, "CompleteSave") != nullptr;
}

// The saves in those folders and the CompleteSaves in the Microsoft Store build's containers, newest first.
static void Saves(std::vector<SaveFile> &saves)
{
    SavesIn(SaveFolders(), saves);
    std::vector<SaveFile> contained;
    SaveContainerFiles(contained);
    for (const auto &c : contained)
        if (SaveHeadIsComplete(c.first)) saves.push_back(c);
    SavesNewestFirst(saves);
}

// The game's shift function (ShiftGear: a vehicle and a gear, true when the game took the gear), once the code look
// found it: where its bytes are, once only, or behind another mod's hook, which leads on to the game's own. The mod's
// automatic sets its gears through it (Gearbox).
typedef bool (*GameShiftFn)(void *vehicle, int32_t gear);
static GameShiftFn g_shiftGear = nullptr;

// Sets a gear through the game's shift function. A fault in it is caught where the compiler can (MSVC's structured
// exceptions), and the function is not called again in this game.
static bool GameShiftCall(uint64_t vehicle, int gear)
{
#ifdef _MSC_VER
    __try
    {
        return g_shiftGear((void *)vehicle, gear);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_shiftGear = nullptr;
        return false;
    }
#else
    return g_shiftGear((void *)vehicle, gear);
#endif
}

static bool GameShift(uint64_t vehicle, int gear)
{
    if (!g_shiftGear || vehicle < 0x10000) return false;
    const bool took = GameShiftCall(vehicle, gear);
    if (!g_shiftGear) Log(L"gearbox: the game's shift function failed, so the mod's automatic does not set gears again in this game");
    return took;
}

// The exe's code (CodeRangeOf), from its base. False where it has none.
static bool CodeRange(size_t &from, size_t &to) { return CodeRangeOf((const uint8_t *)g_base, 0x1000, from, to); }

// The exe's code copied out through ReadProcessMemory, at its own offsets in copy (the rest zeros): the searches read
// the copy, so a page of the exe that cannot be read is no fault on the mod's thread (it reads as zeros). whole: the
// exe's whole image, its headers and data too (a class's type information lies in its data).
static bool CodeCopy(std::vector<uint8_t> &copy, size_t &from, size_t &to, bool whole = false)
{
    if (!CodeRange(from, to)) return false;
    const size_t first = whole ? 0 : from, last = whole && ImageSize() > to ? (size_t)ImageSize() : to;
    copy.assign(last, 0);
    const size_t kChunk = 1 << 20;
    for (size_t at = first; at < last; at += kChunk)
    {
        const size_t n = last - at < kChunk ? last - at : kChunk;
        if (ReadProcessMemory(GetCurrentProcess(), (const void *)(g_base + at), copy.data() + at, n, nullptr)) continue;
        for (size_t p = at; p < at + n; p += 0x1000) // page by page where one of them cannot be read
        {
            const size_t c = at + n - p < 0x1000 ? at + n - p : 0x1000;
            if (!ReadProcessMemory(GetCurrentProcess(), (const void *)(g_base + p), copy.data() + p, c, nullptr)) memset(copy.data() + p, 0, c);
        }
    }
    return true;
}

// The game's statics in the build that runs (truck.h's g_places, game_globals.h): on the Steam build of 22 July 2026
// its known places, on any other found by the code that uses them. clock: the clock's static, 0 where it is not
// known. False where one the mod cannot do without is not found: it stands down then. quiet: no line for each place (a
// look made again, kPlacesRetryMs after the last).
static const DWORD kPlacesRetryMs = 2000;
static const int kPlacesLooks = 31;
static bool PlacesFind(DWORD stamp, uint64_t &clock, bool quiet = false)
{
    clock = 0;
    if (stamp == kGaugesExeStamp)
    {
        clock = kGameGlobals[kGlobClock].steam;
        return true;
    }
    size_t from = 0, to = 0;
    std::vector<uint8_t> image;
    if (!CodeCopy(image, from, to, true))
    {
        Log(L"places: the exe has no code section to look in");
        return false;
    }
    const ULONGLONG started = GetTickCount64();
    uint64_t found[kGlobCount] = {};
    bool missing = false;
    // each place by its ways in turn (game_globals.h): its signatures, then for the ones the mod cannot do without the
    // ways Next Gen Tire Inflation finds them by
    static const wchar_t *const kWayWords[kWaysMost + 1] = { L"", L"", L"the truck update's three calls", L"the truck update's three calls, the frame left open" };
    for (int g = 0; g < kGlobCount; g++)
    {
        int signatures = 0, way = 0, firstSignatures = 0;
        bool conflict = false, firstConflict = false;
        for (int w = 1; w <= (kGameGlobals[g].needed ? kWaysMost : 1) && !found[g]; w++)
        {
            found[g] = GlobalByWay(image.data(), image.size(), g_base, from, to, g, w, found[kGlobControl], signatures, conflict);
            if (w == 1)
            {
                firstSignatures = signatures;
                firstConflict = conflict;
            }
            if (found[g]) way = w;
        }
        if (quiet) {}
        else if (way == 1) Log(L"places: %S at +%llx, by %d of its signatures", kGameGlobals[g].name, found[g], firstSignatures);
        else if (found[g])
            Log(L"places: %S at +%llx, by way %d (%s)%S", kGameGlobals[g].name, found[g], way,
                g == kGlobGame ? L"beside control" : g == kGlobWheelClass ? L"its type information" : kWayWords[way],
                firstConflict ? "; its signatures point at different places" : firstSignatures ? "; its signatures point outside the exe's data" : "; its signatures stand nowhere once");
        else if (kGameGlobals[g].needed) Log(L"places: %S not found by any of its ways%S", kGameGlobals[g].name, firstConflict ? " (its signatures point at different places)" : "");
        else Log(L"places: %S not found%S, so %S", kGameGlobals[g].name, firstConflict ? " (its signatures point at different places)" : "", kGlobalWithout[g]);
        missing = missing || (!found[g] && kGameGlobals[g].needed);
    }
    if (!quiet) Log(L"places: looked for in %.1f s", (GetTickCount64() - started) / 1000.0);
    g_places = GamePlaces{ found[kGlobControl], found[kGlobWheelClass], found[kGlobGame], found[kGlobMap], found[kGlobViewer], found[kGlobGarage], found[kGlobInput],
                           found[kGlobEngineSets] };
    clock = found[kGlobClock];
    return !missing;
}

// On the Steam build of 22 July 2026 with Probe=1: the signatures of the game's statics (game_globals.h), for the
// mod to find them in other builds, into Gauges.signatures.txt next to the log, as lines for kGlobalSigs, and the
// ones it has checked against the known places. Once for each version of the harvest: the file's first line names
// it, and the harvest runs again where the file is gone or names another (a log of its own, as Gauges.log starts
// anew at each start of the game).
static void PlacesHarvest(DWORD stamp)
{
    if (stamp != kGaugesExeStamp) return;
    const std::wstring path = g_dir + L"Gauges.signatures.txt";
    char head[64], first[64] = "";
    sprintf_s(head, "// harvested from exe %08lx, harvest %d", stamp, kHarvestVersion);
    FILE *f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r") == 0 && f)
    {
        if (!fgets(first, sizeof first, f)) first[0] = 0;
        fclose(f);
        first[strcspn(first, "\r\n")] = 0;
    }
    WritePrivateProfileStringW(L"Signatures", nullptr, nullptr, IniPath().c_str()); // where 0.12.11 to 0.13.0 kept it
    size_t from = 0, to = 0;
    if (!strcmp(first, head) || !CodeRange(from, to) || _wfopen_s(&f, path.c_str(), L"w") != 0 || !f) return;
    const uint8_t *code = (const uint8_t *)g_base;
    const ULONGLONG started = GetTickCount64();
    Log(L"signatures: harvesting the game's statics from this build, for the other builds (a few seconds, once)");
    fprintf(f, "%s\n", head);
    int total = 0;
    for (int g = 0; g < kGlobCount; g++)
    {
        GlobalHarvested sigs[4];
        GlobalHarvestNote note;
        const int n = GlobalHarvest(code, from, to, ImageSize(), kGameGlobals[g].steam, kGameGlobals[g].reach, sigs, 4, &note);
        if (!n) fprintf(f, "// %s: %d references to it, none that stands once in the code, nor one to an object that starts up to 0x%x before it\n", kGameGlobals[g].name, note.direct, kGlobalBaseSpan);
        else if (note.base)
            fprintf(f, "// %s: %d references to it, none that stands once; by the object that starts 0x%x before it, which %d reach\n", kGameGlobals[g].name, note.direct, -note.base, note.baseRefs);
        for (int i = 0; i < n; i++) fprintf(f, "{ %s, \"%s\", %d, %d, %d },\n", kGameGlobals[g].id, sigs[i].pattern, sigs[i].at, sigs[i].after, sigs[i].into);
        total += n;
    }
    // the signatures the mod has, checked against the known places
    for (int g = 0; g < kGlobCount && kGlobalSigs[0].global >= 0; g++)
    {
        int signatures = 0;
        bool conflict = false;
        const uint64_t place = GlobalResolve(code, from, to, kGlobalSigs, g, signatures, conflict);
        fprintf(f, "// %s %s (%d of them stand once)\n", kGameGlobals[g].name, place == kGameGlobals[g].steam ? "found where it is" : place ? "found at the wrong place" : "not found",
                signatures);
    }
    fclose(f);
    Log(L"signatures: %d written to Gauges.signatures.txt in %.1f s", total, (GetTickCount64() - started) / 1000.0);
}

// Another gearbox in the game: the manual transmission mod (SMT) by its file or its published state, else another mod
// that holds one of the game's gearbox functions (review 6, G5: that one is not named SMT). The mod's gearbox stands
// down then, and the tab says why (GaugeView::smtInstalled, ::gearboxHeld).
enum { kGearboxFree, kGearboxSmt, kGearboxHeld };
static volatile LONG g_gearboxBlocked = kGearboxFree;
static int GearboxBlocked(const FunctionFind finds[kFnCount])
{
    if (GetModuleHandleW(L"SMT.addon64") || GetModuleHandleW(L"SMT.asi")) return kGearboxSmt;
    wchar_t name[64];
    swprintf_s(name, L"Local\\SnowRunnerMT.State.%lu", GetCurrentProcessId());
    const HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (map)
    {
        CloseHandle(map);
        return kGearboxSmt;
    }
    for (int f = 0; f < kFnCount; f++)
        if (finds[f].state == kFnHeld) return kGearboxHeld;
    return kGearboxFree;
}

// A stall's gearbox damage, dealt on the game's thread (truck.h, StallJob): the job, the game's damage update (0 until
// found) and the callback the truck update's hook runs every frame (below PeekGame).
static StallJob g_stallJob = {};
static void (*g_damageUpdate)(uint64_t) = nullptr;
static void OnTruckFrame(void *first);

// The game's gearbox functions in its code (game_code.h), looked for once a truck is driven, so a manual transmission
// mod has hooked its own by then: where each one is, or that another mod holds it. Then, without SMT in the game,
// eight of them are hooked, calling through (gearbox_hooks.h, phase 1 of the mod's own gearbox), and the game's truck
// update, for a stall's gearbox damage on the game's thread.
static void GameCodeLook()
{
    size_t from = 0, to = 0;
    std::vector<uint8_t> code;
    if (!CodeCopy(code, from, to))
    {
        Log(L"gearbox code: the exe has no code section to look in");
        return;
    }
    const ULONGLONG started = GetTickCount64();
    FunctionFind finds[kFnCount];
    GameFunctionsFind(code.data(), from, to, finds);
    static char line[2048];
    GameFunctionsText(finds, line, sizeof line);
    int held = 0;
    for (const auto &f : finds) held += f.state == kFnHeld;
    Log(L"gearbox code (%.1f s, nothing written yet): %S", (GetTickCount64() - started) / 1000.0, line);
    if (held) Log(L"gearbox code: another mod holds %d of the game's gearbox functions; its hooks pass the game's shifts on", held);
    const FunctionFind &shift = finds[kFnShiftGear];
    // one place only, also behind another mod's jump
    if ((shift.state == kFnFound || shift.state == kFnHeld) && shift.count == 1) g_shiftGear = (GameShiftFn)(g_base + shift.at);
    Log(g_shiftGear ? L"gearbox code: the mod's automatic can set gears through the game's shift function (Gearbox=1)"
                    : L"gearbox code: the game's shift function is not known for sure, so the mod's automatic cannot set gears (Gearbox stays the game's)");
    const int blocked = GearboxBlocked(finds);
    InterlockedExchange(&g_gearboxBlocked, blocked);
    if (blocked == kGearboxSmt)
    {
        Log(L"gearbox: the manual transmission mod (SMT) is installed. It is not compatible with this version and no longer needed: remove SMT.addon64 "
            L"(or SMT.asi) and SMT.ini from Sources\\Bin and start the game again. The mod's gearbox stands down until then");
        return;
    }
    if (blocked == kGearboxHeld)
    {
        Log(L"gearbox: another mod holds the game's gearbox functions, so the mod's gearbox stands down");
        return;
    }
    static char why[1024];
    const int hooked = GearboxHook((uintptr_t)g_base, finds, why, sizeof why);
    if (hooked) Log(hooked == 8 ? L"gearbox: eight of the game's gearbox functions hooked, calling through (phase 1)"
                                : L"gearbox: %d of the game's eight gearbox functions hooked, calling through (phase 1)", hooked);
    for (char *one = why; *one;)
    {
        char *end = strchr(one, '\n');
        if (end) *end = 0;
        Log(L"gearbox: not hooked: %S", one);
        if (!end) break;
        one = end + 1;
    }
    // the mod's own shifts go through to the game's function behind the hook, so the probe counts only the game's. With
    // fewer than eight hooked the mod runs with those it has; each one missing is in the log above
    if (void *own = GearboxOriginal(kFnShiftGear)) g_shiftGear = (GameShiftFn)own;
    // the damage path: the game's damage update, and its truck update hooked so the damage is dealt on its thread
    DamagePath path;
    char hookWhy[160] = "";
    if (!DamagePathFind(code.data(), from, to, path)) Log(L"gearbox damage: %S, so a stall costs the gearbox nothing", path.why);
    else if (!TruckUpdateHook((uintptr_t)(g_base + path.truckUpdate), &OnTruckFrame, hookWhy, sizeof hookWhy))
        Log(L"gearbox damage: %S, so a stall costs the gearbox nothing", hookWhy);
    else
    {
        g_damageUpdate = (void (*)(uint64_t))(g_base + path.damageUpdate);
        Log(L"gearbox damage: dealt on the game's own thread, from its truck update at +%zx, with its damage update at +%zx after", path.truckUpdate, path.damageUpdate);
    }
}

// A file's time as the clock on the wall shows it, hh:mm:ss.
static void ClockText(const FILETIME &t, char *out, size_t size)
{
    FILETIME local;
    SYSTEMTIME st = {};
    FileTimeToLocalFileTime(&t, &local);
    FileTimeToSystemTime(&local, &st);
    snprintf(out, size, "%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
}

static void LogSettings(const GaugeSettings &s)
{
    static const wchar_t *layouts[kLayoutCount] = { L"strip", L"block" }, *styles[kFaceCount] = { L"solid", L"tinted", L"open" },
                         *classes[kClassCount] = { L"truck", L"scout", L"heavy" };
    const GaugeScale k = GaugeScaleOf(s);
    char key[32];
    KeyName(s.toggleKey, key, sizeof key);
    static const wchar_t *oils[kOilGaugeCount] = { L"", L", the oil as a second needle in the temperature dial", L", the oil in a dial of its own" },
                         *profiles[kSpoolCount] = { L"standard", L"small and quick", L"big and slow", L"two stages" };
    Log(L"settings: %s layout%s, %s style, size x%g, opacity %g, offset %g / %g, dials:%s%s%s%s%s%s%s", layouts[s.layout],
        s.swapDials ? L" with the speedometer and the tachometer swapped" : L"", styles[s.style], s.scale, s.opacity, s.offsetX,
        s.offsetY, s.showTemp ? L" temperature" : L"", s.showBoost ? L" boost" : L"", s.showAir ? L" air" : L"", s.showClock ? L" clock" : L"",
        s.showBurn ? L" burn" : L"", s.showLamps ? L" lamps" : L"", oils[s.oilGauge]);
    Log(L"settings: %s scales: idle %.0f rpm, red zone from %.0f of %.0f, speedometer to %.0f km/h, boost to %g bar, a %s turbo; units %s, %s, %s", classes[s.tachClass], k.idle,
        k.redline, k.max, k.speedMax, k.boost, profiles[s.boostProfile], s.mph ? L"mph" : L"km/h", s.psi ? L"psi" : L"bar", s.fahrenheit ? L"F" : L"C");
    Log(L"settings: tilt lamp amber from %g degrees to a side, red from %g, red from %g up or down a slope", s.rollWarnDeg, s.rollDangerDeg, s.pitchDangerDeg);
    Log(L"settings: coolant warm after %g minutes of driving, in the red zone after %g minutes floored with the wheels spinning at a standstill, cool %g minutes after the engine stops, %g C outside%s",
        s.warmUpMinutes, s.overheatMinutes, s.coolDownMinutes, s.ambientC, s.ambientByMap ? L" where the map's temperature is not known" : L" on every map");
    Log(L"settings: the oil warm after %g minutes of driving, too hot from %g C", s.oilWarmUpMinutes, s.oilWarnC);
    Log(L"settings: the radiator has all its air from %g km/h; engine damage %s: from %g C, %g percent a minute at 125 C, %g percent a minute for a cold engine floored at the red zone at 0 C; %g percent of the gearbox for a shift without the clutch; clutch burn %s",
        s.airFullKmh, s.damage ? L"on" : L"off", s.hotDamageFrom, s.hotDamage, s.coldDamage, s.stallDamage, s.clutchBurn ? L"on" : L"off");
    Log(L"settings: sound volume %d of 100%s", s.soundVolume, s.soundsOnly ? L", sounds only: the cluster is not drawn" : L"");
    Log(L"settings: driven with %s", s.input == kInputKeys ? L"the keyboard" : s.input == kInputPad ? L"a pad" : L"a wheel's pedals");
    Log(L"settings: gearbox %s; RequireClutch %d, RequireGearHeld %d, SkipNeutral %d",
        s.gearbox == kGearboxOwn     ? L"the mod's own (Gearbox=2): it takes the shifter, its positions on the bindings"
        : s.gearbox == kGearboxModel ? L"the mod's automatic in A (Gearbox=1), the game's shifter yours, its reverse on the brake refused"
                                     : L"the game's own (Gearbox=1 or 2 in the tab's Gearbox section hand it to the mod)",
        s.requireClutch, s.requireGearHeld, s.skipNeutral);
    if (s.powertrain) Log(L"settings: powertrain on: the truck pulls with %d percent of the mod's engine and converter, through the game's torque factor", s.engineShare);
    else Log(L"settings: powertrain off: the truck pulls with the game's own engine (Powertrain=1 hands it the mod's)");
    Log(L"settings: toggle key %S, pad %S, demo values %s, probe %d", key, s.padToggle, s.demo ? L"on" : L"off", s.probe);
}

// Saves the ini and says so in the log.
static void SettingsSave(SettingsState &st)
{
    if (st.own && st.changed) Log(L"settings: saved, the truck's scales in Gauges\\trucks\\%s.ini", SetupFileName(Wide(st.key)).c_str());
    else Log(L"settings: saved to Gauges.ini");
    SettingsWrite(st);
    LogSettings(st.s);
}

static bool GameInFront()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// Game memory is read through this, so an object the game freed under the mod is a failed read, not a fault.
static bool PeekGame(uint64_t addr, void *out, size_t size) { return ReadProcessMemory(GetCurrentProcess(), (const void *)addr, out, size, nullptr) != 0; }

// And written through this, for the same reason: the engine's damage (TruckDamageAdd), the gearbox's on a stall (on
// the game's thread, StallJobDeal), with Gearbox=1 the game's
// automatic flag, and with Powertrain=1 or the pull test the game's torque factor.
static bool PokeGame(uint64_t addr, const void *in, size_t size) { return WriteProcessMemory(GetCurrentProcess(), (void *)addr, in, size, nullptr) != 0; }

// On the game's thread, every frame of its truck update: a stall's job dealt for the truck the update is for.
static void OnTruckFrame(void *first)
{
    if (g_stallJob.state != kStallJobPosted) return;
    const TruckMemory m = { PeekGame, g_base, PokeGame };
    StallJobFrame(g_stallJob, m, (uint64_t)first, g_damageUpdate);
}

// The driven truck as the mod's thread follows it from pass to pass.
struct TruckWatch
{
    TruckRefs refs;
    bool found;                 // refs are a truck's
    ULONGLONG findAt;           // when to look the objects up again
    ULONGLONG drivingSince;     // since when a truck is driven in the world without a break, 0 = it is not
    TruckRaw probed;            // what the probe wrote last
    World probedWorld;
    ULONGLONG probeAt;          // when the probe writes next
    ULONGLONG gearboxProbeAt;   // and its line of the game's gearbox functions (gearbox_hooks.h)
    bool probedAny;
    float way[3];               // where the chassis was half a second ago, for the speed by the way gone
    ULONGLONG wayAt;
    float byWay;
    TachModel tach;             // the engine speed of the truck at modelVehicle
    BoostModel boost;           // and its boost
    PowertrainModel power;      // and what its engine delivers, handed to the game with Powertrain=1
    GearboxModel gearbox;       // and the gear an automatic with a torque converter would be in, the same way
    ClutchModel clutch;         // and, in a position of the mod's gearbox other than A, the engine behind its clutch
    TakeoverModel take;         // the mod's gearbox has the truck (Gearbox)
    GearboxPosModel pos;        // the gearbox's position, clutch, stalls and shifts (gearbox_model.h)
    GearboxActions actions;     // the player's actions on this pass (from the bindings, at Gearbox=2)
    int stallsOwed;             // stalls whose gearbox damage waits to be posted to the game's thread (StallJob)
    ClutchHeat clutchHeat;      // each kind of truck's clutch heat, and what its burn owes the gearbox (ClutchBurn)
    int burnPoints;             // the gearbox's points posted for the clutch's burn since it began
    bool lockPress[kLocks];     // DiffLock and Awd pressed on this pass (at every level of Gearbox)
    LocksWatch locks;           // what the mod's keys wrote to the locks (LocksStep)
    bool canReverse;            // a binding reaches reverse (GearDown, GearR or GearN): Gearbox=2 may take the shifter
    long gameShifts;            // GameShiftCount on the last pass
    PullTest pullTest;          // the pull test (the tab's button)
    uint64_t pullVehicle, pullAction; // the truck whose torque factor a running pull test changed
    TorqueHand hand;            // the engine's torque handed to the game (Powertrain)
    AirModel air;               // and the air in its brakes' tanks
    TiresReader tiresReader;    // the tire inflation mod's published state (tires.h)
    TiresView tires;            // as read on this pass
    bool tiresSeen;
    int probedLifts;            // the lifts of the pedal the boost had told when the probe wrote last
    uint32_t chance;            // what the pitch of the next lift's sound is drawn from
    bool pedalDown;             // the pedal was more than half down on the last step
    uint64_t modelVehicle;
    RangeModel range;           // the range of the truck with the tank it holds
    float burn;                 // the game's fuel burn in litres a minute, smoothed over about a second (the cluster's readout)
    bool burnKnown;             // burn holds a truck's
    TiltModel tilt;             // how the truck stands, and the tilt lamp
    int probedLamp;             // the tilt lamp the probe wrote last
    TempModel temp;             // the coolant and oil temperatures of every truck driven since the last load
    bool loadingWas;            // the game showed its loading screen on the last pass
    char lastId[40];            // the game's id of the last truck taken, kept while none is driven
    char loadId[40];            // lastId as the last loading screen came up
    bool loadPending;           // a load ended and whether the same truck drives on is not decided yet (LoadTruckAfter)
    uint64_t loadEndedAt;       // when it ended
    OilWarning oil;             // the oil warning: on while the driven truck's oil is too hot
    int probedZone;             // where the coolant stood when the probe wrote last (TempZone)
    DamageModel damage;         // what the temperature costs the engine of the truck at modelVehicle
    EngineRecord engine;        // the record of the engine of the truck at engineVehicle
    float mass;                 // that truck's mass in kg by its files (TruckMass), 0 where not read
    bool ownScales;             // the driven truck has scales of its own (Gauges\trucks): their class is the automatic's, else its weight's
    uint64_t engineVehicle;
    EngineProfile profile;      // the engine's own profile (Gauges\engines), for the engine named profileFor
    char profileFor[kMaxText];
    char gearboxKey[96];        // the gearbox of the truck at gearboxVehicle: its key (GearboxKey), "" before one is known
    bool gearHoldOwn;           // [Gearbox:<key>] holds GearHold, which is gearHold; else the truck's class has it
    float gearHold;
    uint64_t gearboxVehicle;
    int gearSpeeds;             // that gearbox's speeds as last read: another set means another gearbox (the garage)
    float gearSpeed[kMaxGears];
    ULONGLONG engineAt;         // when a record that was not found is looked for again
    int owed, asked;            // whole points of engine damage that wait to be written: the model's, and the tab's test buttons'
    int cause;                  // what the model's points came from: 1 the heat, 2 a cold engine worked hard
    ULONGLONG writeAt;          // when the next write may go out
    ULONGLONG addedAt;          // when damage was last written, 0 = never: the damage lamp blinks for two seconds after
    const char *refused;        // why the last write did not go out, a string literal; a reason is logged once
    bool probedAdding;          // whether the model was adding damage when the probe wrote last
    ClockWatch clock;           // the game's own count of the seconds it has simulated the truck for, and when it last moved
    ULONGLONG modelAt;          // when the models last ran
    TruckWho who;               // which truck refs are, as the passes read it; who.taken's key is "" while none is read
};

// The mod's gearbox has the truck in a position of the player's own (not A): the engine runs behind its clutch.
static bool GearboxManual(const TruckWatch &w) { return w.take.driving && w.pos.running && w.pos.pos != kPosA; }

// The mod's gearbox lets the truck go: the game's own shifting passes again.
static void GearboxLetGo(TruckWatch &w)
{
    GearboxPolicy(kPolicyPass, nullptr, false);
    w.pos = GearboxPosModel{};
}

// The game's own shifts that passed the hooks so far (its automatic picked again, ShiftToAutoGear, left out): one more
// on a pass at level 1 is the player's hand on the game's shifter.
static long GameShiftCount()
{
    GearboxCounts c;
    GearboxCountsGet(c);
    return (c.shift - c.refused[kFnShiftGear]) + (c.high - c.refused[kFnShiftToHigh]) + (c.reverse - c.refused[kFnShiftToReverse]) +
           (c.neutral - c.refused[kFnShiftToNeutral]) + (c.autoOff - c.refused[kFnAutoOffAndShift]);
}

// What the game asked for, by the hook that refused it, for the log.
static const wchar_t *GearboxAskName(int fn)
{
    switch (fn)
    {
    case kFnShiftGear: return L"a gear";
    case kFnShiftToAuto: return L"its automatic";
    case kFnShiftToHigh: return L"the high gear";
    case kFnShiftToReverse: return L"reverse";
    case kFnShiftToNeutral: return L"neutral";
    case kFnAutoOffAndShift: return L"a gear with its automatic off";
    case kFnSetPower: return L"a power factor";
    default: return L"something";
    }
}

// The seconds each gear is held going up with the mod's engine: the gearbox's own, else its class's.
static float GearHoldOf(const TruckWatch &w, int tachClass)
{
    return w.gearHoldOwn ? w.gearHold : tachClass == kClassScout ? kEngineGearHoldScout : kEngineGearHold;
}

static const wchar_t *const kClassWords[kClassCount] = { L"truck", L"scout", L"heavy" };

// The class the automatic goes by (each gear's hold, and the engine's curve where it has no profile of its own): the
// class of the truck's own scales, and without them the class of its weight (ClassByMass), the plain scales' where
// the weight is not read. tachClass: the class of the scales in force.
static int AutoClass(const TruckWatch &w, int tachClass)
{
    return w.ownScales ? tachClass : ClassByMass(w.mass, tachClass);
}

// Finds the driven truck's gearbox (its name, or its gears where the name is not read) and its own hold, and says so
// in the log when it is another than before.
static void GearboxLook(TruckWatch &w, const TruckMemory &m, const TruckRaw &raw, int tachClass)
{
    char name[kMaxText], stock[kMaxText];
    uint32_t at = 0;
    const bool named = TruckGearboxName(m, w.refs.vehicle, name, at, stock);
    char key[96];
    GearboxKey(raw, named ? name : "", key, sizeof key);
    w.gearboxVehicle = w.refs.vehicle;
    w.gearSpeeds = raw.gearSpeeds;
    memcpy(w.gearSpeed, raw.gearSpeed, sizeof w.gearSpeed);
    if (!strcmp(key, w.gearboxKey)) return;
    strcpy_s(w.gearboxKey, key);
    w.gearHoldOwn = GearHoldRead(key, w.gearHold);
    wchar_t whose[96];
    if (w.gearHoldOwn) wcscpy_s(whose, L"its own (its file in Gauges\\gearboxes)");
    else swprintf_s(whose, L"the %s class's%s", kClassWords[tachClass < 0 || tachClass >= kClassCount ? kClassTruck : tachClass],
                    w.ownScales || !(w.mass > 0.0f) ? L"" : L", the class of the truck's weight");
    // the gearbox fitted, and the truck's default where it is another (an upgrade fitted)
    char other[kMaxText + 40] = "";
    if (stock[0] && strcmp(stock, name)) sprintf_s(other, " (the truck's default is %s)", stock);
    if (named) Log(L"gearbox: %S fitted%S; each gear held going up %.1f s, %s", key, other, GearHoldOf(w, tachClass), whose);
    else Log(L"gearbox: its name not read, so it goes by its gears (%S); each gear held going up %.1f s, %s", key, GearHoldOf(w, tachClass), whose);
}

// Looks up the record of the driven truck's engine, and says so in the log when it is another one than before. With
// the probe on, the record's first 0x100 bytes and the floats of a mass's size in the vehicle and its model go into
// the log as well, to find the engine's Torque and the truck's mass.
static void EngineLook(TruckWatch &w, const TruckMemory &m, ULONGLONG now, bool probe)
{
    EngineRecord e;
    TruckEngineRecord(m, w.refs.vehicle, e);
    const float mass = TruckMass(m, w.refs.model);
    if (w.refs.vehicle != w.engineVehicle || mass != w.mass)
    {
        if (mass > 0.0f)
            Log(L"truck: it weighs %.0f kg by its files (without addons, cargo or a trailer): a %s's weight, the class the automatic goes by where the truck "
                L"has no scales of its own",
                mass, kClassWords[ClassByMass(mass, kClassTruck)]);
        else Log(L"truck: its mass by its files was not read (%.0f)", mass);
    }
    w.mass = mass;
    if (w.refs.vehicle != w.engineVehicle || e.found != w.engine.found || strcmp(e.name, w.engine.name) || e.capacity != w.engine.capacity ||
        e.threshold != w.engine.threshold || e.torqueMin != w.engine.torqueMin || e.torqueMax != w.engine.torqueMax || e.torque != w.engine.torque)
    {
        if (e.found)
            Log(L"engine: %S takes %d damage; past %.2f of it the torque falls, from x%.2f to x%.2f | its file's figures: torque %.0f, responsiveness %.2f, "
                L"MaxDeltaAngVel %.3f, fuel consumption %.2f (0: not read)",
                e.name, e.capacity, e.threshold, e.torqueMin, e.torqueMax, e.torque, e.responsiveness, e.deltaAngVel, e.consumption);
        else Log(L"engine: no record of \"%S\" was found; its damage is written without the effect, which the game sets at its next damage event", e.name);
        if (probe && e.found)
        {
            char words[1024];
            for (int row = 0; row < 4; row++)
            {
                TruckWordsText(m, e.at + 0x40 * (uint64_t)row, 16, words, sizeof words);
                Log(L"probe: engine record + 0x%02x: %S", 0x40 * row, words);
            }
        }
        if (probe && w.refs.vehicle != w.engineVehicle)
        {
            char masses[2048];
            const int inVehicle = TruckFloatsText(m, w.refs.vehicle, 0x1000, 300.0f, 100000.0f, 60, masses, sizeof masses);
            Log(L"probe: floats of a mass's size in the vehicle (%d): %S", inVehicle, masses);
            const int inModel = TruckFloatsText(m, w.refs.model, 0x1000, 300.0f, 100000.0f, 60, masses, sizeof masses);
            Log(L"probe: floats of a mass's size in its model (%d): %S", inModel, masses);
        }
    }
    w.engine = e;
    w.engineVehicle = w.refs.vehicle;
    // the engine's own profile, for each engine as it is first known
    if (e.name[0] && strcmp(e.name, w.profileFor))
    {
        strcpy_s(w.profileFor, e.name);
        if (EngineProfileRead(e.name, w.profile))
            Log(L"engine: %S has a profile of its own (Gauges\\engines\\%s.ini): %s%s", e.name, SetupFileName(Wide(e.name)).c_str(),
                w.profile.custom ? L"a torque curve of its own" : w.profile.peakRpm > 0.0f ? L"its torque curve from its peak torque's speed" : L"the class's torque curve",
                w.profile.aspiration == kAspNatural ? L", naturally aspirated" : L", turbocharged");
        else Log(L"engine: %S has no profile of its own: the class's torque curve, turbocharged", e.name);
        if (w.profile.peakRpm > 0.0f) Log(L"engine: its peak torque at %.0f rpm", w.profile.peakRpm);
        if (w.profile.torque > 0.0f) Log(L"engine: the truck gets %.0f percent of its torque (Torque)", w.profile.torque * 100.0f);
    }
    w.engineAt = now + 5000;
}

// One pass over the truck: finds its objects twice a second and when a read fails, reads its values and what the
// game is showing, runs the models, writes the engine's damage into the game when there is some to write, and
// writes the probe log. front: the game's window is in front. True when a truck is driven in the world and its
// values hold up: raw, world and the models in w are then what to draw from.
// A write of the torque factor (TorqueHandStep): at the driven truck's action, or, the game's own share given back to a
// truck no longer driven, only where its vehicle still holds that action.
static void TorqueGive(const TruckMemory &m, const TruckRefs &refs, bool read, const TorqueOrders &o)
{
    if (!o.write || !o.action) return;
    uint64_t action = 0;
    if ((!read || o.action != refs.action) && !(m.get(o.vehicle + 0x68, action) && action == o.action)) return;
    m.put(o.action + 0xCC, o.factor);
}

// A running pull test ends: the torque factor it changed goes back to its truck, where that truck's vehicle still holds
// the drive train it changed (a switch of trucks or a truck lost mid-test left it changed before).
static void PullGiveBack(const TruckMemory &m, TruckWatch &w, const wchar_t *why)
{
    if (w.pullTest.state != kPullRunning) return;
    uint64_t action = 0;
    const bool back = w.pullAction && m.get(w.pullVehicle + 0x68, action) && action == w.pullAction && m.put(w.pullAction + 0xCC, w.pullTest.factor);
    Log(L"pull test: stopped, %s; the truck's torque factor %s", why, back ? L"given back" : L"not given back (its drive train is gone)");
    w.pullTest = PullTest{};
}

static bool TruckPass(TruckWatch &w, const TruckMemory &m, const GaugeSettings &s, bool front, TruckRaw &raw, World &world)
{
    const int probe = s.probe;
    const ULONGLONG now = GetTickCount64();
    // the tire inflation mod, when it publishes its state: the cluster's tire lamp goes by it
    w.tires = TiresRead(w.tiresReader, now);
    if (w.tires.present != w.tiresSeen)
        Log(w.tires.present ? L"tire inflation mod: it publishes its state; the cluster shows the tires' pressure mode in a lamp"
                            : L"tire inflation mod: its state is no longer published; the tire lamp is gone");
    w.tiresSeen = w.tires.present;
    // the game's own shifts the hooks refused while the mod's gearbox has the truck: each kind said once a truck, so the
    // drive that first sees a truck in A reverse on the brake names the call that carries it
    const unsigned fresh = GearboxRefusedNew();
    if (fresh & kRefusedBrakeReverse)
    {
        const int door = GearboxBrakeDoor();
        Log(L"gearbox: the game's reverse on the brake refused: the game's automatic was up for a moment (the shift modifier or A picked again); through %S",
            door >= 0 && door < kFnCount ? kGameFunctions[door].name : "?");
    }
    for (unsigned fn = 0; fresh && fn < kFnCount; fn++)
        if (fresh & (1u << fn)) Log(L"gearbox: the game asked for %s, refused: the mod's gearbox has the truck", GearboxAskName((int)fn));
    const bool was = w.found;
    bool read = w.found && TruckRead(m, w.refs, raw);
    if (!read && (w.found || now >= w.findAt))
    {
        // lost, or time to look again
        w.found = TruckFind(m, w.refs);
        w.findAt = now + 500;
        read = w.found && TruckRead(m, w.refs, raw);
    }
    else if (now >= w.findAt)
    {
        // the wheels and the gearbox can be rebuilt under a truck that stays: the objects are looked up anew. When the
        // game drives another truck by now, the values read above are the old truck's: this pass has no truck, and the
        // next one finds the new truck.
        TruckRefs again;
        w.findAt = now + 500;
        if (TruckFind(m, again))
        {
            if (again.vehicle != w.refs.vehicle)
            {
                read = false;
                w.findAt = 0;
            }
            w.refs = again;
        }
    }
    if (!read) w.found = false;
    if (w.found != was)
    {
        if (w.found) Log(L"truck: found, %d wheels, %d gears and a high gear, a %.0f l tank", raw.wheels, TruckTopGear(raw), raw.tank);
        else Log(L"truck: none is driven now");
    }
    WorldRead(m, w.found ? w.refs.control : 0, world);
    // a load ended (the game's loading screen gone): the truck that drives on under the game's id it had before the
    // screen keeps its heat, so a travel between regions, a border crossing and a reload are no cold start (a tester's
    // UAZ arrived cold from each travel and took cold-engine damage at once, 2026-10-26; the author's answer: keep
    // it). A game start, the garage, a recovery (a new id) and a change of truck drop the coolant and oil the model
    // remembers for each kind of truck, so every truck starts as cold as the air outside. The id is taken as the screen
    // comes up, as the game can make a truck during it; the decision waits for the first truck read after it, a minute
    // at most (LoadTruckAfter). The game's save names a new map only minutes after a border crossing (found in the
    // game, 2026-10-25), so it is no guide.
    const bool loading = world.known && world.loading;
    if (!w.loadingWas && loading)
    {
        memcpy(w.loadId, w.lastId, sizeof w.loadId);
        w.loadPending = false;
    }
    if (w.loadingWas && !loading)
    {
        w.loadPending = true;
        w.loadEndedAt = now;
    }
    w.loadingWas = loading;
    if (w.loadPending && !read && LoadTruckAfter(w.loadId, nullptr, now - w.loadEndedAt) == kLoadNone)
    {
        w.loadPending = false;
        w.temp = TempModel{};
        w.oil = OilWarning{};
        Log(L"map: a load ended and no truck was driven for a minute, so every truck's engine is as cold as the air outside again (the coolant and the oil)");
    }
    const bool driving = read && TruckSane(raw) && WorldDriving(world);
    if (!driving) w.drivingSince = 0;
    else if (!w.drivingSince) w.drivingSince = now;
    // the game's own clock for the truck: while it stands, the game is not simulating and the models wait
    const bool held = read && ClockStands(w.clock, raw, now);
    // what the tab's test buttons asked for goes to a truck that is being driven, further down
    const int asked = (int)InterlockedExchange(&g_damageTest, 0);
    if (asked > 0 && (!driving || held)) Log(L"engine damage: the test's %d points were not written, as no truck is being driven", asked);
    if (!read)
    {
        // the truck is gone: what waited to be written goes with it, and the next truck is looked at anew, also when
        // the game puts it where the old one was
        w.wayAt = 0;
        w.probedAny = false;
        w.owed = w.asked = 0;
        w.engineVehicle = 0;
        w.clock = ClockWatch{};
        w.who = TruckWho{};
        w.burnKnown = false;
        if (w.take.driving)
        {
            Log(L"gearbox: the mod's gearbox lets the truck go: it is not read any more");
            w.take.driving = false;
        }
        GearboxLetGo(w);
        PullGiveBack(m, w, L"the truck is not read any more");
        // the engine's torque the mod handed to the game: the game's own share back, where the truck still is
        TorqueGive(m, w.refs, false, TorqueHandStep(w.hand, false, 0, 0, 0.0f, 1.0f));
        return false;
    }
    // which truck this is (TruckWhoStep). While another truck than the one taken is read, the models wait: on the
    // pass that takes it the settings in force are still the last truck's, and the mod's thread changes them right
    // after. The models that go with one truck then start over, also when the game put it where the last one was.
    TruckIdentity id;
    bool taking = false;
    TruckKey(m, w.refs, raw, id);
    const bool same = TruckWhoStep(w.who, id, taking);
    if (taking) w.modelVehicle = 0;
    if (w.loadPending)
    {
        const int after = LoadTruckAfter(w.loadId, id.id, now - w.loadEndedAt);
        if (after == kLoadSame) Log(L"map: a load ended and the same truck drives on (%S), so its engine keeps its heat", id.id);
        else if (after != kLoadWait)
        {
            w.temp = TempModel{};
            w.oil = OilWarning{};
            Log(after == kLoadOther ? L"map: a load ended and another truck is driven, so every truck's engine is as cold as the air outside again (the coolant and the oil)"
                                    : L"map: a load ended and the driven truck's id was not read for a minute, so every truck's engine is as cold as the air outside again (the coolant and the oil)");
        }
        if (after != kLoadWait) w.loadPending = false;
    }
    if (w.who.taken.id[0]) memcpy(w.lastId, w.who.taken.id, sizeof w.lastId);
    if (asked > 0 && driving && !held && !same) Log(L"engine damage: the test's %d points were not written, as the truck was only just taken", asked);
    // the speed by the way the chassis went in the last half second, for the probe to hold the speed against
    if (!w.wayAt || now - w.wayAt >= 500)
    {
        if (w.wayAt)
        {
            const float dx = raw.position[0] - w.way[0], dy = raw.position[1] - w.way[1], dz = raw.position[2] - w.way[2];
            w.byWay = sqrtf(dx * dx + dy * dy + dz * dz) / ((now - w.wayAt) / 1000.0f);
        }
        memcpy(w.way, raw.position, sizeof w.way);
        w.wayAt = now;
    }
    // the models run while the truck is driven: a game held by a menu stands still with its values as they were. They
    // stay with a truck through the menus, the map and the garage. On another truck the engine speed starts over; the
    // range starts over when the truck is of another kind (RangeStep), so a truck put back into the world keeps its
    // litres a kilometre. The tilt starts over on another truck and after a second without a step, as the truck may
    // stand elsewhere by then. The coolant temperature stays with each kind of truck (TempStep). Engine damage that
    // waits to be written belongs to the truck it came from and goes with it. While the game's clock for the truck
    // stands (held), nothing is stepped: the game is not simulating, whatever its window shows.
    const GaugeScale scale = GaugeScaleOf(s);
    const uint64_t kind = TruckKeyNumber(w.who.taken.key);
    // out of the world (the garage, the map, the viewer, a menu): the game's own calls pass, so a gearbox fitted in the
    // garage takes its gear; the gearbox keeps the truck and reads its position again on the way back (review 7, R6)
    if (w.take.driving && !(driving && !held && same) && GearboxHoldingNow()) GearboxPolicy(kPolicyPass, nullptr, false);
    if (driving && !held && same)
    {
        if (w.refs.vehicle != w.modelVehicle)
        {
            w.tach = TachModel{};
            w.boost = BoostModel{};
            w.power = PowertrainModel{};
            w.gearbox = GearboxModel{};
            w.clutch = ClutchModel{};
            if (w.take.driving) Log(L"gearbox: the mod's gearbox lets the last truck go: another truck is driven");
            w.take = TakeoverModel{};
            GearboxLetGo(w);
            PullGiveBack(m, w, L"another truck is driven");
            w.air = AirModel{};
            w.tilt = TiltModel{};
            w.owed = w.asked = 0;
            w.stallsOwed = 0;
            w.modelVehicle = w.refs.vehicle;
            w.modelAt = 0;
        }
        if (!w.modelAt || now - w.modelAt >= 1000) w.tilt = TiltModel{};
        const float dt = w.modelAt && now - w.modelAt < 1000 ? SettingsRange((now - w.modelAt) / 1000.0f, 0.001f, 0.2f) : 0.05f;
        // the needle: while the mod's gearbox is in A, its converter's engine speed (of the last pass)
        if (w.take.driving && !GearboxManual(w)) TachFollow(w.tach, raw, scale, w.gearbox.rpm, dt);
        else TachStep(w.tach, raw, scale, dt);
        // the engine's torque curve: its own profile's (Gauges\\engines), else the class's; a naturally aspirated one
        // has no boost, no turbo sounds and no boost dial
        const int autoClass = AutoClass(w, s.tachClass);
        const TorqueCurve curve = EngineCurveOf(w.profile, kClassCurves[autoClass], scale.redline);
        const bool natural = w.profile.aspiration == kAspNatural;
        // the boost, and a lift of the pedal under it, which is heard (sound.h). With the probe on it is logged
        if (natural) w.boost = BoostModel{};
        const int lift = natural ? kLiftNone : BoostStep(w.boost, raw, TachShare(w.tach, scale), scale.boost, s.boostProfile, dt);
        if (lift != kLiftNone && probe)
            Log(L"boost: a %s at %.2f bar of the turbo's %.2f, %.2f of its full sound", lift == kLiftBlowOff ? L"blow-off" : L"flutter", w.boost.lastBar, scale.boost, w.boost.lastVolume);
        LiftSound sound;
        if (LiftSoundOf(lift, w.boost.lastVolume, s.soundVolume, LiftChance(w.chance), sound)) SoundPlay(sound.tier, sound.gain, sound.pitch);
        // the pedal going down again ends a flutter that still rings
        const bool pedalDown = TruckPedal(raw) > 0.5f;
        if (pedalDown && !w.pedalDown) SoundQuiet();
        w.pedalDown = pedalDown;
        RangeStep(w.range, kind, raw, dt);
        // the burn for the readout: the game's rule, nothing with the engine off, smoothed so it does not flicker with the pedal
        // (the rule's numbers not read: not known)
        const float burnNow = TruckFuelRate(raw);
        if (!(burnNow >= 0.0f)) w.burnKnown = false;
        else
        {
            if (!w.burnKnown || !isfinite(w.burn)) w.burn = burnNow;
            else w.burn += (1.0f - expf(-dt / 1.0f)) * (burnNow - w.burn);
            w.burnKnown = isfinite(w.burn);
        }
        // the air dryer's purge: a hiss each time the governor stops the compressor, a little under the oil warning
        if (AirStep(w.air, raw, TachSpeed(w.tach, scale), dt) && TruckEngineOn(raw)) SoundPlay(kSoundPurge, 0.8f * OilBeepGain(s.soundVolume), 1.0f);
        TiltStep(w.tilt, raw, s, dt);
        TempStep(w.temp, kind, raw, TachSpeed(w.tach, scale), s, dt);
        // the oil warning: heard every few seconds while the oil is too hot, and said in the log when it comes and goes
        const bool oilWas = w.oil.on;
        if (OilWarnStep(w.oil, w.temp, raw, s, dt)) SoundPlay(kSoundOil, OilBeepGain(s.soundVolume), 1.0f);
        // the powertrain: what the engine delivers, logged, and handed to the game with Powertrain=1 (below)
        PowertrainStep(w.power, raw, curve, TachSpeed(w.tach, scale), scale.redline > 0.0f ? scale.max / scale.redline : 1.0f, w.temp.oil,
                       natural ? 1.0f : scale.boost > 0.0f ? w.boost.bar / scale.boost : 0.0f, dt);
        // the automatic: the gear it would be in, the same way, by the pedal as held. With the probe on each of its
        // shifts is logged beside the game's gear while the game's automatic drives a numbered gear
        const int boxGear = w.gearbox.gear;
        const float foot = TruckEngineOn(raw) ? TruckPedal(raw) : 0.0f;
        if (GearboxManual(w))
        {
            // a position of the player's own: the automatic's model stands down, and the engine runs behind the
            // gearbox's clutch; a clutch on an axis is the driver's, without one (a button, or none) it works by itself
            w.gearbox = GearboxModel{};
            if (ClutchStep(w.clutch, raw, scale, curve, w.pos.travel, w.power.pedal, w.power.cold * w.power.boost, w.actions.clutchTravel < 0.0f, dt) && probe)
                Log(L"clutch: the engine would have stalled, the clutch let up at %.0f rpm on the gearbox's input in gear %d", w.clutch.input, raw.gearEngaged);
        }
        else
        {
            // the shift points a truck automatic's schedule while the mod's engine is handed to the game, else by the
            // game's own pull; an engine held back (Torque) sets off as a weaker one
            const float launch = w.engine.torque > 0.0f && w.mass > 0.0f && w.engineVehicle == w.refs.vehicle
                                     ? w.engine.torque / w.mass * (w.profile.torque > 0.0f ? w.profile.torque : 1.0f) : 0.0f;
            const EngineHand hand{ w.hand.on, &curve, scale.redline > 0.0f ? scale.max / scale.redline : 1.0f, s.engineShare / 100.0f, launch,
                                   GearHoldOf(w, autoClass), kLightPeak * CurvePeak(curve) * scale.redline, s.input };
            const int huntsWere = w.gearbox.hunts;
            GearboxStep(w.gearbox, raw, scale, foot, w.power.power, dt, hand);
            if (w.gearbox.hunts != huntsWere)
                Log(L"gearbox: a shift up taken back within %.0f s with the foot on (the grade or the ground), the %d%s time; no shift up past %d for %.0f s, "
                    L"the limiter's neither, then it tries again",
                    kHuntWindow, w.gearbox.hunts, w.gearbox.hunts == 1 ? L"st" : w.gearbox.hunts == 2 ? L"nd" : w.gearbox.hunts == 3 ? L"rd" : L"th",
                    w.gearbox.keep, w.gearbox.keepFor);
            w.clutch = ClutchModel{};
        }
        // the clutch's heat from the clutch as stepped, by the engine's most torque and its profile's share. Only a
        // clutch pedal on an axis heats it (review 10, D1): the clutch that works by itself (a button, or none) slips on
        // every start and cannot be feathered, so it only cools, as one outside a position of the player's does
        {
            const float most = w.engine.torque > 0.0f && w.engineVehicle == w.refs.vehicle ? w.engine.torque * (w.profile.torque > 0.0f ? w.profile.torque : 1.0f) : 0.0f;
            const bool burnWas = w.clutchHeat.burning;
            ClutchHeatStep(w.clutchHeat, kind, w.actions.clutchTravel >= 0.0f ? w.clutch : ClutchModel{}, most, s.damage && s.clutchBurn, dt);
            if (w.clutchHeat.burning && !burnWas)
            {
                w.burnPoints = 0;
                Log(s.damage && s.clutchBurn && g_damageUpdate ? L"clutch: burning, the gearbox pays"
                    : s.damage && s.clutchBurn ? L"clutch: burning, but the game's damage path was not found, so the gearbox pays nothing"
                                               : L"clutch: burning, but Damage or ClutchBurn is off, so the gearbox pays nothing");
            }
            else if (!w.clutchHeat.burning && burnWas) Log(L"clutch: cooled under its burn point; the burn cost the gearbox %d points", w.burnPoints);
        }
        if (probe && boxGear >= 1 && w.gearbox.gear >= 1 && w.gearbox.gear != boxGear && raw.automatic && raw.gearEngaged >= 1 && raw.gearEngaged <= TruckTopGear(raw))
            Log(L"automatic: the model would shift %d to %d at %.0f rpm on the gearbox's input, the pedal at %.2f (the game's gear %d)", boxGear, w.gearbox.gear,
                w.gearbox.input, foot, raw.gearEngaged);
        // the pull test (the tab's button): armed until the truck is floored in the fourth gear or higher, then seven
        // seconds of the torque factor changed and the pull measured, the gear held and the mod's automatic left out
        const LONG pullAsk = InterlockedExchange(&g_pullTestAsked, 0);
        if (pullAsk == 1 && w.pullTest.state != kPullRunning)
        {
            w.pullTest = PullTest{ kPullArmed };
            Log(L"pull test: armed; it runs once the truck is floored in the fourth gear or higher, the wheels under %.0f %% of the gear's top, the game's "
                L"automatic flag down", kPullTopMost * 100.0f);
        }
        else if (pullAsk == 2 && (w.pullTest.state == kPullArmed || w.pullTest.state == kPullRunning))
        {
            if (w.pullTest.state == kPullRunning) m.put(w.refs.action + 0xCC, w.pullTest.factor);
            w.pullTest.state = kPullOff;
            Log(L"pull test: cancelled from the tab");
        }
        const int pullWas = w.pullTest.state;
        const PullOrders pull = PullTestStep(w.pullTest, raw, foot, dt);
        if (pullWas == kPullArmed && w.pullTest.state == kPullRunning)
        {
            w.pullVehicle = w.refs.vehicle;
            w.pullAction = w.refs.action;
            Log(L"pull test: running in gear %d at %.1f m/s, the torque factor %.3f; keep it floored", w.pullTest.gear, raw.speed, w.pullTest.factor);
        }
        if (pull.write) m.put(w.refs.action + 0xCC, pull.factor);
        if (pull.hold || pull.ended)
        {
            // the gear held is the game's: the automatic's model goes on from it, not from a gear it picked meanwhile
            w.gearbox.gear = raw.gear;
            w.gearbox.pause = fmaxf(w.gearbox.pause, kShiftPause);
        }
        if (pull.ended)
        {
            char first[1400], second[320];
            PullTestText(w.pullTest, first, sizeof first, second, sizeof second);
            Log(L"%S", first);
            Log(L"%S", second);
        }
        const bool testing = w.pullTest.state == kPullRunning || pull.ended;
        // the levels of Gearbox (with the hooks in place): 1 the mod's automatic in A, the game's shifter the player's and
        // only the game's reverse on the brake refused; 2 the mod's own gearbox, which takes the truck the moment it is
        // driven, in whatever position, and refuses every shift of the game's. Level 2 runs as 1 while no binding reaches
        // reverse (w.canReverse), so a truck can always be backed up. No gear is set while the pull test holds one
        const int level = s.gearbox == kGearboxOwn && !w.canReverse ? kGearboxModel : s.gearbox;
        const bool own = level == kGearboxOwn;
        const TakeoverOrders orders = TakeoverStep(w.take, raw, level >= kGearboxModel && g_shiftGear != nullptr && GearboxHooked(), !own);
        const GearboxTruck boxTruck{ raw.gear, TruckTopGear(raw), isfinite(raw.speed) ? raw.speed : 0.0f, TruckEngineOn(raw), raw.stall,
                                     raw.powerFactor, raw.automatic != 0, foot };
        if (orders.took)
        {
            GearboxPosTake(w.pos, boxTruck);
            w.gearbox.gear = raw.gear >= 1 && raw.gear <= TruckTopGear(raw) ? raw.gear : 1;
            w.gearbox.pause = kShiftPause;
            GearboxHuntClear(w.gearbox);
            w.gameShifts = GameShiftCount();
            if (own) Log(L"gearbox: the mod's gearbox takes the truck in %S (the game's gear %d); the game's own shifting is refused from now on", GearboxPosName(w.pos.pos), raw.gear);
            else Log(L"gearbox: the mod's automatic takes the truck in A (the game's gear %d); the game's reverse on the brake is refused", raw.gear);
        }
        if (orders.gaveBack)
        {
            Log(L"gearbox: the mod's gearbox lets the truck go: %S, after %d shifts and %d stalls", TakeoverWhy(w.take.lastHandBack), w.take.shifts, w.pos.stalls);
            // given back from A: the game's automatic back on, so the truck is not left in a plain gear (review 7, R10)
            if (w.pos.running && w.pos.pos == kPosA) m.put(w.refs.action + 0x3C, (uint8_t)1);
            GearboxLetGo(w);
        }
        if (w.take.driving)
        {
            // back in the world after the garage, the map or the viewer (the policy passed meanwhile): the position read
            // again from the game (review 7, R6)
            if (!GearboxHoldingNow())
            {
                GearboxPosTake(w.pos, boxTruck);
                w.gameShifts = GameShiftCount();
                Log(L"gearbox: back in the world, the mod's gearbox reads the truck in %S again", GearboxPosName(w.pos.pos));
            }
            // level 1: the game's shifter is the player's. A shift of the game's that leaves A (H, N, R, the low range or
            // a numbered gear picked) hands the truck back to the game's position; it is taken again when A is picked
            const long shiftsNow = GameShiftCount();
            if (!own && ((shiftsNow != w.gameShifts && !raw.automatic && raw.gear != w.pos.lastSet) || w.pos.pos != kPosA))
            {
                Log(L"gearbox: the game's shifter picked gear %d: the mod's automatic hands the truck back, and takes it again when A is picked", raw.gear);
                w.take.driving = false;
                w.take.handBacks++;
                GearboxLetGo(w);
            }
            w.gameShifts = shiftsNow;
        }
        if (w.take.driving)
        {
            // the policy for the held truck, each pass: everything refused at level 2; at level 1 only a reverse while
            // the truck stands in A with the brake down and the game's automatic flag up, read by the hook at the call
            // (the flag comes up for a moment after the shift modifier is tapped or A picked again, before this pass
            // puts it down; the first drives, 2026-10-26)
            const bool brakeStand = w.pos.pos == kPosA && fabsf(boxTruck.speed) < kStandStill && TruckBrake(raw) > 0.05f;
            GearboxPolicy(own ? kPolicyAll : kPolicyBrakeReverse, (void *)w.refs.vehicle, brakeStand, (const volatile uint8_t *)(w.refs.action + 0x3C));
        }
        bool shiftAsk = false;
        int shiftTo = 0;
        if (w.take.driving && !testing)
        {
            const GearboxOptions options{ s.requireClutch, s.requireGearHeld, s.skipNeutral };
            const int autoGear = TakeoverAutoGear(raw, w.gearbox.gear, GearboxLowestFit(w.gearbox, raw, scale, w.hand.on));
            const int posWas = w.pos.pos;
            const GearboxPosOrders box = GearboxPosStep(w.pos, options, w.actions, boxTruck, autoGear);
            if (box.flagDown) m.put(w.refs.action + 0x3C, (uint8_t)0);
            if (box.setPower) m.put(w.refs.action + 0x38, box.power);
            if (box.stall) m.put(w.refs.vehicle + 0x11C, kStallSeconds);
            // the stall the model ordered costs the gearbox (StallDamagePercent, with Damage), dealt on the game's thread;
            // a stall of the game's own does not
            const wchar_t *stallCost = L"";
            if (box.stall && StallDamageShare(s) > 0.0f)
            {
                if (!g_damageUpdate) stallCost = L"; it costs the gearbox nothing, as the game's damage path was not found";
                else
                {
                    w.stallsOwed++;
                    stallCost = L"; its gearbox damage goes to the game's thread";
                }
            }
            if (box.moved && w.pos.pos != posWas) Log(L"gearbox: %S to %S", GearboxPosName(posWas), GearboxPosName(w.pos.pos));
            if (box.why[0] && box.stall) Log(L"gearbox: %S (%d since it took the truck)%s", box.why, w.pos.stalls, stallCost);
            else if (box.why[0]) Log(L"gearbox: %S", box.why);
            shiftAsk = box.setGear;
            shiftTo = box.gear;
        }
        // a stall's gearbox damage: posted to the game's thread one stall at a time, and what came of it told; the
        // stalls owed go when the share has gone to 0 meanwhile (review 10, D5)
        if (StallDamageShare(s) <= 0.0f) w.stallsOwed = 0;
        if (w.stallsOwed && g_damageUpdate && StallJobPost(g_stallJob, w.refs, StallDamageShare(s), now)) w.stallsOwed--;
        // the clutch's burn, the same way once whole points are owed
        if (!w.stallsOwed && raw.gearboxKnown && g_damageUpdate) w.burnPoints += ClutchBurnPost(w.clutchHeat, g_stallJob, w.refs, raw.gearboxCapacity, now);
        {
            GearboxDamageWrite gw;
            bool updated = false, burn = false;
            int sum = 0, sumCapacity = 0;
            if (StallJobResult(g_stallJob, gw, updated, sum, sumCapacity, &burn))
            {
                const wchar_t *what = burn ? L"the clutch's burn" : L"the stall's damage";
                if (gw.written)
                    Log(L"gearbox damage: the game's thread dealt %s on its next frame: the gearbox took %d of %d (%d now)%s; the game's sum of "
                        L"the parts' damage %d of %d",
                        what, gw.points, gw.capacity, gw.wrote, updated ? L", the game's damage update run after it" : L"", sum, sumCapacity);
                else if (gw.capacity > 0 && gw.before >= gw.capacity) Log(L"gearbox damage: %s not dealt: the gearbox at its worst already", what);
                else Log(L"gearbox damage: %s not dealt: %S", what, gw.why);
            }
            if (StallJobDrop(g_stallJob, w.refs.vehicle))
                Log(L"gearbox damage: the last truck's gearbox damage not dealt: another truck is driven before the game's truck update took it");
        }
        // the differential lock and all-wheel drive on the mod's keys, at every level of Gearbox: not shifts, and the
        // game's own keys still work on a truck with the upgrade
        if (w.refs.action && !(w.refs.action >= m.base && w.refs.action < m.base + g_exeSize))
        {
            int lockSaid[kLocks];
            LocksStep(w.locks, m, w.refs.action, w.lockPress, lockSaid);
            static const wchar_t *const lockName[kLocks] = { L"the differential lock", L"all-wheel drive" };
            for (int i = 0; i < kLocks; i++)
                if (lockSaid[i] == kLockOn || lockSaid[i] == kLockOff) Log(L"locks: %s %s by the mod's key", lockName[i], lockSaid[i] == kLockOn ? L"on" : L"off");
                else if (lockSaid[i] == kLockCleared) Log(L"locks: the game cleared %s on this truck; it may not have the upgrade", lockName[i]);
        }
        // the engine's torque handed to the game (Powertrain): the torque factor for the engine's torque at the needle's
        // speed and the converter's ratio over the game's curve at the wheels' share of the gear's top, while the engine
        // runs and no pull test is armed or running; the game's own share given back once it stops
        {
            const bool pullBusy = w.pullTest.state == kPullArmed || w.pullTest.state == kPullRunning || pull.ended;
            const bool want = s.powertrain && TruckEngineOn(raw) && !pullBusy;
            // the wheels' share of the gear's top by the truck's speed over the ground, once the wheels' grip is known
            // (LiftShare: before the grip is known, no more than kLiftBlind)
            const float ground = isfinite(raw.speed) && raw.speed > 0.0f ? raw.speed : 0.0f;
            const float x = LiftShare(TruckDrivenSpin(raw), ground, w.gearbox.grip, TruckGearTop(raw, raw.gearEngaged));
            const float ratio = w.take.driving && !GearboxManual(w) ? w.gearbox.ratio : 1.0f;
            const bool handWas = w.hand.on;
            const float gameWas = w.hand.game;
            // held back to the engine's Torque share where one is set
            TorqueGive(m, w.refs, true, TorqueHandStep(w.hand, want, w.refs.action, w.refs.vehicle, raw.torqueFactor,
                                                 TorqueHeld(TorqueBlend(TorqueLift(w.power.curve, w.power.cold * w.power.boost, TruckPedal(raw), ratio, x), s.engineShare / 100.0f),
                                                            w.profile.torque)));
            if (w.hand.on != handWas)
                Log(w.hand.on ? L"powertrain: the engine's torque handed to the game, the game's own torque factor %.3f" : L"powertrain: the game's own torque factor %.3f given back",
                    w.hand.on ? w.hand.game : gameWas);
        }
        if (shiftAsk)
        {
            if (GameShift(w.refs.vehicle, shiftTo))
            {
                w.take.shifts++;
                if (probe && shiftTo >= 1 && raw.gear >= 1)
                {
                    // where the wheels are against each gear's top, by the ground, and what the game gives there
                    const float from = GearboxInput(raw, scale, raw.gear, w.gearbox.by) / scale.redline, into = GearboxInput(raw, scale, shiftTo, w.gearbox.by) / scale.redline;
                    Log(L"gearbox: %d to %d at %.0f rpm on the gearbox's input, the engine at %.0f rpm, the pedal at %.2f; the wheels at %.2f of the gear's top "
                        L"(the game's pull %.2f) and %.2f of the new one's (%.2f)",
                        raw.gear, shiftTo, w.gearbox.input, w.gearbox.rpm, foot, from, GamePull(from), into, GamePull(into));
                }
                else if (probe) Log(L"gearbox: %d to %d", raw.gear, shiftTo);
            }
            else
            {
                if (GearboxPosRefused(w.pos, shiftTo))
                    Log(L"gearbox: the game did not take gear %d from the mod's gearbox, in gear %d; asked again when the position changes", shiftTo, raw.gear);
            }
        }
        if (w.oil.on != oilWas)
            Log(w.oil.on ? L"oil: %.1f C, too hot from %g C: the lamp blinks and the warning sounds" : L"oil: %.1f C, the warning is over", w.temp.oil, s.oilWarnC);
        // the engine's record, for each truck as it is first driven; one that was not found is looked for again every
        // five seconds
        if (w.refs.vehicle != w.engineVehicle || (!w.engine.found && now >= w.engineAt)) EngineLook(w, m, now, s.probe != 0);
        // the gearbox: for each truck as it is first driven, and again when its gears change (another fitted in the garage)
        if (w.refs.vehicle != w.gearboxVehicle || raw.gearSpeeds != w.gearSpeeds || memcmp(raw.gearSpeed, w.gearSpeed, sizeof w.gearSpeed))
            GearboxLook(w, m, raw, AutoClass(w, s.tachClass));
        // what the temperature costs the engine: the whole points wait in owed until they are written. Nothing comes
        // together while the game's window is not in front, as the game may stand still then with its last values.
        const int points = DamageStep(w.damage, w.refs.vehicle, raw, w.temp, TachSpeed(w.tach, scale), s, front ? dt : 0.0f);
        if (points > 0)
        {
            w.owed = w.owed + points < 1000000 ? w.owed + points : 1000000; // more than any engine takes
            w.cause = w.damage.hot > 0.0f ? 1 : 2;
        }
        if (!s.damage) w.owed = 0;
        if (asked > 0) w.asked = w.asked + asked < 1000000 ? w.asked + asked : 1000000;
        // the write into the game: only once the truck has been driven for half a second without a break, with the
        // game in front and none of its menus up (and the two menu flags read, so that a 0 in them means that), and at
        // most once a second. The engine's record is looked up again for each write: an engine can change under a
        // truck that stays.
        if (w.owed + w.asked > 0 && front && world.menusKnown && world.menu == 0 && !world.functionMenu && !world.menuInput && now - w.drivingSince >= 500 &&
            now >= w.writeAt)
        {
            DamageWrite d;
            const bool test = w.asked > 0;
            EngineLook(w, m, now, false);
            if (TruckDamageAdd(m, w.refs, w.owed + w.asked, w.engine, d))
            {
                char cause[128], effect[256], after[64] = ""; // the effect and the torque factor are the game's: room for numbers of any size
                if (test) strcpy_s(cause, "the tab's test");
                else
                    sprintf_s(cause, w.cause == 2 ? "a cold engine worked hard at %.1f C (%.1f percent a minute)" : "the heat at %.1f C (%.1f percent a minute)", w.temp.temp,
                              w.damage.hot + w.damage.cold);
                if (d.effectAfter != d.effectBefore || d.torqueAfter != d.torqueBefore)
                    sprintf_s(effect, "effect %.3f to %.3f, torque x%.3f to x%.3f", d.effectBefore, d.effectAfter, d.torqueBefore, d.torqueAfter);
                else sprintf_s(effect, "effect %.3f, torque x%.3f", d.effectAfter, d.torqueAfter);
                if (d.after != d.wrote) sprintf_s(after, "; the game held %d right after", d.after);
                Log(L"engine damage: %d to %d of %d, from %S; %S%S", d.before, d.wrote, d.capacity, cause, effect, after);
                w.addedAt = now;
                w.refused = "";
            }
            else if (test || !w.refused || strcmp(w.refused, d.why))
            {
                Log(L"engine damage: %d of %d, not written: %S", d.before, d.capacity, d.why);
                w.refused = d.why;
            }
            w.owed = w.asked = 0;
            w.writeAt = now + 1000;
        }
        w.modelAt = now;
    }
    if (probe && (!w.probedAny || now >= w.probeAt || TruckProbeEvent(raw, world, w.probed, w.probedWorld) || w.tilt.lamp != w.probedLamp ||
                  TempZone(w.temp) != w.probedZone || DamageAdding(w.damage, s) != w.probedAdding ||
                  w.boost.flutters + w.boost.blowOffs != w.probedLifts))
    {
        char first[640], second[1024], third[512], fourth[256], fifth[640], sixth[1024], seventh[400], eighth[400], ninth[512], tenth[400], eleventh[320], twelfth[256];
        // while the mod's gearbox has the truck the letter is the model's, as the cluster's (the flag is down, so the
        // game's own rule would read the first gear's factor as L+)
        TruckProbeText(raw, world, w.byWay, first, sizeof first, second, sizeof second, w.take.driving && w.pos.running ? GearboxPosLetter(w.pos.pos) : nullptr);
        ModelProbeText(raw, w.tach, w.range, scale, third, sizeof third);
        Log(L"probe: %S", first);
        Log(L"probe: %S", second);
        TiltProbeText(raw, w.tilt, fourth, sizeof fourth);
        TempProbeText(raw, w.temp, s, fifth, sizeof fifth);
        // the record is the last driven truck's: a truck that is read and not driven yet has none
        DamageProbeText(raw, w.engineVehicle == w.refs.vehicle ? w.engine : EngineRecord{}, w.damage, s, sixth, sizeof sixth);
        Log(L"probe: %S", third);
        Log(L"probe: %S", fourth);
        Log(L"probe: %S", fifth);
        BoostProbeText(raw, w.boost, TachShare(w.tach, scale), scale.boost, seventh, sizeof seventh);
        Log(L"probe: %S", sixth);
        Log(L"probe: %S", seventh);
        PowertrainProbeText(raw, w.power, w.tach.rpm, w.hand.on, w.hand.written, w.hand.game, eighth, sizeof eighth);
        Log(L"probe: %S", eighth);
        const bool manual = GearboxManual(w);
        if (manual) snprintf(ninth, sizeof ninth, "automatic: stands down, the gearbox is in a position of the player's own");
        else GearboxProbeText(raw, w.gearbox, w.take.driving, ninth, sizeof ninth);
        Log(L"probe: %S", ninth);
        if (w.take.driving)
        {
            GearboxPosProbeText(w.pos, GearboxOptions{ s.requireClutch, s.requireGearHeld, s.skipNeutral }, raw.gear, tenth, sizeof tenth);
            Log(L"probe: %S", tenth);
        }
        if (manual)
        {
            ClutchProbeText(w.clutch, w.pos.travel, eleventh, sizeof eleventh);
            char heat[96];
            ClutchHeatProbeText(w.clutchHeat, heat, sizeof heat);
            Log(L"probe: %S%S", eleventh, heat);
        }
        AirProbeText(raw, w.air, twelfth, sizeof twelfth);
        Log(L"probe: %S", twelfth);
        w.probedLifts = w.boost.flutters + w.boost.blowOffs;
        w.probedAdding = DamageAdding(w.damage, s);
        w.probedLamp = w.tilt.lamp;
        w.probedZone = TempZone(w.temp);
        w.probed = raw;
        w.probedWorld = world;
        w.probedAny = true;
        w.probeAt = now + 2000;
    }
    // the game's gearbox functions as the hooks count them, every second while a truck is driven (phase 1: which of
    // them the game calls when, as when a truck in A reverses on the brake). The counts nest: reverse and neutral go
    // through auto off, and auto off through ShiftGear, so one reverse counts in all three (review 6, G2)
    if (probe && driving && GearboxHooked() && now >= w.gearboxProbeAt)
    {
        GearboxCounts c;
        GearboxCountsGet(c);
        Log(L"probe: gearbox code: ShiftGear %ld (last gear %d) | auto %ld | high %ld | reverse %ld | neutral %ld | auto off %ld (last %d) | power %ld (last %.3f) | "
            L"vehicle %ld (%s) | counts nest: reverse and neutral go through auto off, auto off through ShiftGear | refused%s: ShiftGear %ld, auto %ld, "
            L"high %ld, reverse %ld, neutral %ld, auto off %ld, power %ld",
            c.shift, c.shiftGear, c.autoGear, c.high, c.reverse, c.neutral, c.autoOff, c.autoOffGear, c.power, c.factor, c.vehicle, c.vehicleSet ? L"set" : L"cleared",
            GearboxHoldingNow() ? L" (the mod's gearbox has the truck)" : L"", c.refused[kFnShiftGear], c.refused[kFnShiftToAuto], c.refused[kFnShiftToHigh],
            c.refused[kFnShiftToReverse], c.refused[kFnShiftToNeutral], c.refused[kFnAutoOffAndShift], c.refused[kFnSetPower]);
        w.gearboxProbeAt = now + 1000;
    }
    // on screen once the truck has been driven for half a second: a state that only flickers by does not show it
    return driving && now - w.drivingSince >= 500;
}

// The tab's buttons: the turbo's sound of one strength, as loud as the settings in force have it at the boost that
// strength stands for, and the oil warning.
void SoundHear(int tier)
{
    GaugeSettings s;
    SettingsGet(s);
    if (tier == kSoundOil)
    {
        SoundPlay(kSoundOil, OilBeepGain(s.soundVolume), 1.0f);
        return;
    }
    static const float loud[kTurboTiers] = { 1.0f, 0.55f, 0.25f };
    LiftSound sound;
    if (tier >= 0 && tier < kTurboTiers && LiftSoundOf(kLiftBlowOff, loud[tier], s.soundVolume, 0.0f, sound)) SoundPlay(tier, sound.gain, sound.pitch);
}

// ---- the mod's thread ----

// The exe's link time stamp, which tells one build of the game from another.
static DWORD ExeStamp()
{
    const BYTE *image = (const BYTE *)g_base;
    return *(const DWORD *)(image + *(const DWORD *)(image + 0x3C) + 8);
}

static DWORD WINAPI Run(void *)
{
    if (g_second)
    {
        // a second file of this mod: DllMain found the first one's mutex
        wchar_t file[MAX_PATH] = {};
        GetModuleFileNameW(g_self, file, MAX_PATH);
        Sleep(1000); // after the first copy's first line, which starts the log anew
        g_logStarted = true;
        Log(L"this mod is in the folder twice: only one copy runs, and this one (%s) stands down. Keep one file of it, Gauges.asi.", file);
        return 0;
    }
    const DWORD stamp = ExeStamp();
    Log(L"Next Gen Gameplay " GAUGES_VERSION L" (Gauges): exe stamp %08lx, loaded %s", stamp,
        g_addon ? L"by ReShade as Gauges.addon64, the name from before 0.16 (the mod needs no ReShade now: keep it as Gauges.asi)" : L"by an ASI loader");
    LogKeptSay();
    LogGameFolder();
    SettingsDefaults(g_set); // what the tab shows until the ini is read
    g_overlayOk = OverlayInit(g_self);
    g_exeSize = ImageSize(); // where the exe ends, which the engine damage write keeps out of
    // the game's statics: known on the build the mod was made on, found by the code that uses them on any other
    uint64_t clockPlace = 0;
    if (stamp != kGaugesExeStamp) Log(L"places: another build of the game than the one the mod was made on (exe stamp %08lx): looking for what it reads", kGaugesExeStamp);
    // a look that finds them not, as when the mod is loaded before the game has its code ready (another mod's early
    // DirectX probe loads ReShade and this add-on with it: the Spotter minimap mod, 2026-10-21, where one look found
    // nothing and the next start without it found all), is made again every kPlacesRetryMs, kPlacesLooks in all
    bool placesOk = PlacesFind(stamp, clockPlace);
    for (int look = 2; !placesOk && look <= kPlacesLooks; look++)
    {
        Sleep(kPlacesRetryMs);
        if (PlacesFind(stamp, clockPlace, true))
        {
            Log(L"places: all found on look %d, %.0f s after the first (the game's code was not ready yet):", look, (look - 1) * kPlacesRetryMs / 1000.0);
            placesOk = PlacesFind(stamp, clockPlace);
        }
    }
    if (!placesOk)
    {
        Log(L"places: the mod cannot find the driven truck in this build of the game, %d looks over %.0f s: standing down, nothing in the game is changed and "
            L"no cluster is drawn (the Gameplay tab says the mod is not running). "
            L"Gauges.log above says what is missing", kPlacesLooks, (kPlacesLooks - 1) * kPlacesRetryMs / 1000.0);
        InterlockedExchange(&g_modOff, 1);
        return 0;
    }
    // the settings: what [Gauges] holds, and with it what is in force (s), which has the driven truck's own scales
    // in the place of the plain ones where the truck has some. truck is the truck they are for.
    SettingsState set = {};
    ReadIni(set.plain);
    SettingsFor(set, "");
    const GaugeSettings &s = set.s;
    SettingsPublish(s);
    // the gearbox's bindings: the defaults with the ini's over them; the first time, without one in the ini yet, the
    // manual transmission mod's SMT.ini taken over, and the result written so it happens once
    GearBindings binds;
    BindingsDefault(binds);
    {
        const std::wstring ini = IniPath(), smtIni = g_dir + L"SMT.ini";
        if (!BindingsInIni(ini) && GetFileAttributesW(smtIni.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            std::wstring taken, left;
            const int bound = SmtIniImport(smtIni, binds, set.plain, taken, left);
            set.s.requireClutch = set.plain.requireClutch;
            set.s.requireGearHeld = set.plain.requireGearHeld;
            set.s.skipNeutral = set.plain.skipNeutral;
            BindingsWrite(ini, binds);
            SettingsWrite(set);
            SettingsPublish(s);
            Log(L"gearbox: SMT.ini taken over once: %d keyboard bindings (%s), its options RequireClutch %d, RequireGearHeld %d, SkipNeutral %d; left: "
                L"its pad and wheel section (other codes than Windows'), its menu and handbrake keys%s%s",
                bound, taken.empty() ? L"none" : taken.c_str(), s.requireClutch, s.requireGearHeld, s.skipNeutral, left.empty() ? L"" : L", ", left.c_str());
        }
        else BindingsRead(ini, binds, true);
    }
    LogSettings(s);
    BindingsLog(binds);
    InputDevicesStart([](const wchar_t *line) { Log(L"%s", line); });
    if (s.probe) PlacesHarvest(stamp);
    Log(g_overlayOk ? L"overlay: the cluster and the Gameplay tab of the NEXT GEN menu are drawn by the mod's own overlay"
                    : L"overlay: the overlay did not start (the lines above say why), so nothing is drawn");
    SoundStart(g_dir); // the sounds are read or made now; the audio device is only taken while one plays
    // the block other mods read the mod's state from (state.h), written at the end of every pass
    StateWriter state = {};
    {
        wchar_t name[64];
        StateName(name, 64);
        if (StateOpen(state)) Log(L"state published for other mods in %s", name);
        else Log(L"state: Windows gave no shared memory for %s (error %lu), so nothing is published for other mods", name, GetLastError());
    }
    bool visible = true, keyDown = false;
    uint32_t padMask = PadParse(s.padToggle), padPrev = 0;
    ULONGLONG saveAt = 0;
    TruckIdentity truck = {};
    TruckWatch watch = {};
    InputReader bindReader = {};   // the bindings' states from pass to pass
    InputCapture bindCapture = {}; // a capture the tab began
    const TruckMemory memory = { PeekGame, g_base, PokeGame };
    // the map the truck drives on, the game's id for it from its save, and the outside temperature of its section,
    // its region's or its own, where the ini has one (mapOwn, from the section mapFrom). Without a save map stays ""
    // and AmbientC of [Gauges] is in force everywhere.
    char map[64] = "", mapRead[64] = "", mapFrom[64] = "";
    MapAir mapAir = {};
    // the game's clock: where it is (clockOwn), from Gauges.ini or found by the search (Probe=1), and the hour read on
    // this pass, -1 while it is not known
    ClockPath clockPath = {};
    bool clockOwn = ClockLoad(stamp, clockPath), clockSaid = false;
    const bool clockFromIni = clockOwn;
    if (!clockOwn && clockPlace)
    {
        // the place found in the running game on 2026-10-10: [exe + 0x2a8eb48] + 0x150 in that build, the hour
        clockPath = ClockPath{ (uint32_t)clockPlace, 1, { 0x150, 0 }, false, 1.0f };
        clockOwn = true;
    }
    ClockScan clockScan = {};
    float hour = -1.0f;
    int hourSaid = -1, clockFails = 0;
    bool wasDriving = false; // a truck was driven in the world on the last pass, where the clock must be readable
    if (clockOwn)
    {
        char words[96];
        ClockPathWords(clockPath, words, sizeof words);
        Log(clockFromIni ? L"clock: the game's time of day is read at %S, as found before (Gauges.ini, [Clock])"
                         : L"clock: the game's time of day is read at %S, the place known for this version of the game", words);
    }
    bool mapOwn = false, mapAsk = true, saveSeen = true;
    ULONGLONG mapAmbientLogAt = 0; // when to log the temperature the tab set, once the slider has rested
    ULONGLONG gearHoldLogAt = 0;   // and the gearbox's hold
    ULONGLONG engineLogAt = 0;     // and the engine's profile
    ULONGLONG torqueLogAt = 0;     // and the engine's torque share
    std::wstring saveUnread; // a save found that named no map (said once)
    FILETIME saveWritten = {}; // when the save read last was written
    ULONGLONG saveLookAt = 0;
    bool codeLooked = false; // the gearbox functions were looked for, once a truck was found
    for (;;)
    {
        Sleep(50);
        // the settings tab: edits count on this pass and go into the ini once they have rested a second; "read the
        // ini again" takes the file as it is
        bool edited = false;
        const int keyBefore = s.toggleKey;
        if (InterlockedExchange(&g_reloadAsked, 0))
        {
            ReadIni(set.plain);
            SettingsFor(set, truck.key);
            SettingsPublish(s);
            // the driven engine's profile and gearbox's hold read again too, as a file edited by hand may have changed them
            if (watch.profileFor[0]) EngineProfileRead(watch.profileFor, watch.profile);
            if (watch.gearboxKey[0]) watch.gearHoldOwn = GearHoldRead(watch.gearboxKey, watch.gearHold);
            BindingsDefault(binds);
            BindingsRead(IniPath(), binds, true);
            Log(L"settings: Gauges.ini read again, and the driven truck's engine and gearbox files");
            LogSettings(s);
            BindingsLog(binds);
            saveAt = 0;
            edited = true;
        }
        if (InterlockedExchange(&g_setDirty, 0))
        {
            const bool ownBefore = set.own;
            GaugeSettings in;
            SettingsGet(in);
            SettingsTake(set, in);
            if (set.own && !ownBefore) Log(L"truck: %S has scales of its own from now on, kept in Gauges\\trucks\\%s.ini", set.key, SetupFileName(Wide(set.key)).c_str());
            saveAt = GetTickCount64() + 1000;
            edited = true;
        }
        if (InterlockedExchange(&g_truckDropAsked, 0) && set.own)
        {
            // the tab's button
            SettingsDrop(set);
            SettingsPublish(s);
            Log(L"truck: %S gave up its own scales and takes the plain ones", set.key);
            LogSettings(s);
            edited = true;
        }
        if (InterlockedExchange(&g_resetAsked, 0))
        {
            // the tab's defaults button
            SettingsToDefaults(set);
            SettingsPublish(s);
            Log(L"settings: back to the defaults");
            saveAt = GetTickCount64() + 1000;
            edited = true;
        }
        // a key just bound on the tab is still down: it toggles from its next press on
        if (s.toggleKey != keyBefore) keyDown = true;
        if (saveAt && GetTickCount64() >= saveAt)
        {
            SettingsSave(set);
            saveAt = 0;
        }
        if (edited) padMask = PadParse(s.padToggle);
        // the toggle: the key, or the pad buttons all down that were not all down on the last pass; only while the
        // game's window is in front, and the pad not while the tab waits for buttons to bind
        const bool front = GameInFront();
        const bool keyNow = front && s.toggleKey && !NgMenuOpen() && (GetAsyncKeyState(s.toggleKey) & 0x8000); // not while the NEXT GEN menu has the keys
        bool toggle = keyNow && !keyDown;
        keyDown = keyNow;
        const LONG capture = g_pageCapturing;
        const bool capturing = capture && GetTickCount() - (DWORD)capture < 500;
        const uint32_t padHeld = padMask && front && !capturing ? PadButtons() : 0;
        // not while the NEXT GEN menu has the pad. What is held then still counts as held on the last pass, so a toggle
        // held as the menu opens or closes is no press when it closes
        const uint32_t pad = NgMenuOpen() ? 0 : padHeld;
        if (padMask && (pad & padMask) == padMask && (padPrev & padMask) != padMask) toggle = true;
        padPrev = padHeld;
        if (toggle)
        {
            visible = !visible;
            Log(L"cluster %s", visible ? L"shown" : L"hidden");
        }
        // the gearbox's bindings: one snapshot of the keys, the pad and the wheels and shifters a pass, read only while
        // the game is in front. The tab's asks (a capture into a slot, a slot cleared) are done here with it, and a
        // capture ends when the NEXT GEN menu closes; the actions go to the gearbox only at Gearbox=2 with a binding
        // that reaches reverse, the locks' at every level, and none goes down while the menu is open or a capture runs
        {
            static InputSnapshot snap, seen;
            InputSnapshotRead(snap, front, PadButtons());
            seen = InputFrontOnly(snap);
            const ULONGLONG bindNow = GetTickCount64();
            bool bindsChanged = false;
            const LONG ask = InterlockedExchange(&g_bindAsk, 0);
            const int askWhat = ask >> 16, askAction = (ask >> 8) & 0xFF, askSlot = ask & 0xFF;
            if (ask && askAction < kActCount && askSlot < kBindingSlots)
            {
                if (askWhat == kBindAskCapture) BindingCaptureBegin(bindCapture, askAction, askSlot, bindNow);
                else if (askWhat == kBindAskCancel) bindCapture.active = false;
                else if (askWhat == kBindAskClear)
                {
                    binds.slot[askAction][askSlot] = Binding{};
                    bindsChanged = true;
                }
            }
            if (bindCapture.active && !NgMenuOpen())
            {
                bindCapture.active = false; // the menu closed under it: the key that closed it, or the next one driven with, is no binding
                Log(L"gearbox: the capture for %S ended as the NEXT GEN menu closed; nothing bound", kActionNames[bindCapture.action]);
            }
            if (bindCapture.active)
            {
                Binding caught = {};
                const int step = BindingCaptureStep(bindCapture, seen, bindNow, caught);
                const char *clash = step == kCaptureBound ? BindingClash(caught, NgMenuKeyFromIni(), s.toggleKey, padMask) : nullptr;
                if (clash)
                {
                    char text[96];
                    BindingText(caught, text, sizeof text);
                    Log(L"gearbox: %S not bound to %S: it is %S", text, kActionNames[bindCapture.action], clash);
                }
                else if (step == kCaptureBound)
                {
                    binds.slot[bindCapture.action][bindCapture.slot] = caught;
                    // held as it is bound: a press only once it was let go
                    bindReader.latched[bindCapture.action][bindCapture.slot] = true;
                    bindsChanged = true;
                }
                if (step != kCaptureWaiting) bindCapture.active = false;
            }
            if (bindsChanged)
            {
                BindingsWrite(IniPath(), binds);
                Log(L"gearbox: the bindings set from the tab, written to Gauges.ini:");
                BindingsLog(binds);
            }
            // the reader steps on what is really held every pass, so its latches follow the hardware through the menu
            // (review 8, W2); reverse is reached by what is there, in front or not
            static ActionState states[kActCount];
            InputStep(bindReader, binds, seen, states);
            watch.canReverse = BindingsCanReverse(binds, snap, PadPresent());
            if (s.gearbox != kGearboxOwn || !watch.canReverse) watch.actions = GearboxActions{};
            else watch.actions = NgMenuOpen() || bindCapture.active ? ActionsResting(watch.actions) : ActionsOf(states, binds);
            // the locks at every level: a press while no truck is driven is gone, not kept for later
            const bool locksHeard = !NgMenuOpen() && !bindCapture.active;
            watch.lockPress[kLockDiff] = locksHeard && states[kActDiffLock].pressed;
            watch.lockPress[kLockAwd] = locksHeard && states[kActAwd].pressed;
        }
        // the map: the game's save, looked for every kSaveLookMs and read when it is another or was written since
        const ULONGLONG lookNow = GetTickCount64();
        if (lookNow >= saveLookAt)
        {
            saveLookAt = lookNow + kSaveLookMs;
            std::vector<SaveFile> saves;
            Saves(saves);
            const bool found = !saves.empty();
            if (found != saveSeen)
            {
                if (!found)
                {
                    // where it looked: the folders that are there, and how many more are not
                    std::wstring there;
                    int missing = 0;
                    for (const auto &dir : SaveFolders())
                    {
                        if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) { missing++; continue; }
                        if (there.size() < 1500) there += (there.empty() ? L"" : L"; ") + PathForLog(dir);
                    }
                    Log(L"map: no save of the game was found, so the map is not known and AmbientC of [Gauges] is in force (looked for CompleteSave*.cfg "
                        L"and .dat in: %s; %d more folders not there; and the Microsoft Store's containers)", there.empty() ? L"no folder that is there" : there.c_str(),
                        missing);
                }
                saveSeen = found;
            }
            char level[64];
            SaveFile picked;
            std::wstring unread;
            if (SavePick(saves, saveWritten, picked, level, sizeof level, unread))
            {
                saveWritten = picked.second;
                saveUnread.clear();
                if (strcmp(level, map))
                {
                    char at[16], title[96];
                    ClockText(picked.second, at, sizeof at);
                    MapTitle(level, title, sizeof title);
                    Log(L"map: %s (%S), the map loaded last as the game's save of %S says (%s)", Wide(title).c_str(), level, at, PathForLog(picked.first).c_str());
                    strcpy_s(map, level);
                }
            }
            else if (!unread.empty() && unread != saveUnread)
            {
                // said once for each save: one the game is writing reads again on the next look
                saveUnread = unread;
                Log(L"map: the save %s names no map the mod can read (yet: it is read again when it is written)", PathForLog(unread).c_str());
            }
        }
        // the outside temperature in force: the map's or its region's (AmbientByMap), looked up again when the map
        // changes or the ini was read or saved. The tab's slider sets the section in force
        // the hour of the game's day, where the clock is known; a clock that cannot be read for long (a game update
        // that kept the exe's stamp, a wrong find) is forgotten
        if (clockOwn)
        {
            if (ClockRead(memory, clockPath, hour)) clockFails = 0;
            else
            {
                hour = -1.0f;
                if (wasDriving && ++clockFails == 6000) // five minutes of passes with a truck driven
                {
                    Log(L"clock: the game's time of day could not be read for five minutes, so its place is forgotten and looked for again");
                    ClockForget();
                    clockOwn = false;
                }
            }
        }
        // the map's temperature now: between its night and its day by the hour, or the day's mean without a clock
        const float mapNow = hour >= 0.0f ? DayNightC(mapAir.day, mapAir.night, hour) : mapAir.mean;
        if (InterlockedExchange(&g_mapAmbientAsked, 0) && mapOwn && mapFrom[0])
        {
            // the slider moves the section's air so the temperature now is what it shows
            const float c = SettingsRange(g_mapAmbientTenths / 10.0f, kAmbientRange[0], kAmbientRange[1]);
            if (fabsf(c - mapNow) >= 0.05f)
            {
                MapAirShift(mapFrom, mapAir, c - mapNow);
                MapSectionAir(mapFrom, mapAir);
                mapAmbientLogAt = GetTickCount64() + 1000;
            }
        }
        if (InterlockedExchange(&g_gearHoldAsked, 0) && watch.gearboxKey[0])
        {
            const float hold = SettingsRange(g_gearHoldTenths / 10.0f, kGearHoldRange[0], kGearHoldRange[1]);
            if (!watch.gearHoldOwn || fabsf(hold - watch.gearHold) >= 0.05f)
            {
                watch.gearHold = hold;
                watch.gearHoldOwn = true;
                GearHoldWrite(watch.gearboxKey, hold);
                gearHoldLogAt = GetTickCount64() + 1000;
            }
        }
        // the engine's profile from the tab: into its file, in force at once; or given up, the file gone
        const LONG engineAsk = InterlockedExchange(&g_engineAsked, 0);
        if (engineAsk && watch.profileFor[0])
        {
            if (engineAsk == 1)
            {
                EngineProfileWrite(watch.profileFor, (float)g_enginePeak, (int)g_engineAspiration);
                engineLogAt = GetTickCount64() + 1000;
            }
            else
            {
                DeleteFileW(SetupFile(kSetupEngine, watch.profileFor).c_str());
                Log(L"engine: %S gave its profile up: the class's torque curve, turbocharged", watch.profileFor);
            }
            EngineProfileRead(watch.profileFor, watch.profile);
        }
        // the engine's torque share from the tab's slider: into its file, in force at once
        if (InterlockedExchange(&g_engineTorqueAsked, 0) && watch.profileFor[0])
        {
            EngineTorqueWrite(watch.profileFor, (int)g_engineTorquePercent);
            EngineProfileRead(watch.profileFor, watch.profile);
            torqueLogAt = GetTickCount64() + 1000;
        }
        if (torqueLogAt && GetTickCount64() >= torqueLogAt)
        {
            Log(L"engine: %S set from the tab: the truck gets %.0f percent of its torque (Gauges\\engines\\%s.ini)", watch.profileFor,
                (watch.profile.torque > 0.0f ? watch.profile.torque : 1.0f) * 100.0f, SetupFileName(Wide(watch.profileFor)).c_str());
            torqueLogAt = 0;
        }
        if (engineLogAt && GetTickCount64() >= engineLogAt)
        {
            Log(L"engine: %S set from the tab: its peak torque at %.0f rpm, %s (Gauges\\engines\\%s.ini)", watch.profileFor, watch.profile.peakRpm,
                watch.profile.aspiration == kAspNatural ? L"naturally aspirated" : L"turbocharged", SetupFileName(Wide(watch.profileFor)).c_str());
            engineLogAt = 0;
        }
        if (gearHoldLogAt && GetTickCount64() >= gearHoldLogAt)
        {
            Log(L"gearbox: %S set from the tab: each gear held %.1f s going up (Gauges\\gearboxes\\%s.ini)", watch.gearboxKey, watch.gearHold,
                SetupFileName(Wide(watch.gearboxKey)).c_str());
            gearHoldLogAt = 0;
        }
        if (mapAmbientLogAt && GetTickCount64() >= mapAmbientLogAt)
        {
            if (mapAir.dayNight) Log(L"map: [Map:%S] set from the tab to %g C by day and %g C by night", mapFrom, mapAir.day, mapAir.night);
            else Log(L"map: [Map:%S] set from the tab to %g C outside on the day's mean", mapFrom, mapAir.mean);
            mapAmbientLogAt = 0;
        }
        if (strcmp(mapRead, map) || edited) mapAsk = true;
        if (mapAsk)
        {
            const bool before = mapOwn;
            mapOwn = map[0] && MapAmbient(map, mapAir, mapFrom, sizeof mapFrom);
            if (strcmp(mapRead, map) || mapOwn != before)
            {
                char title[96];
                MapTitle(map, title, sizeof title);
                if (mapOwn && s.ambientByMap)
                    Log(L"map: %s, %g C outside by day and %g C by night%s ([Map:%s] in Gauges.ini)", Wide(title).c_str(), mapAir.day, mapAir.night,
                        mapAir.dayNight ? L"" : L", around the day's mean", Wide(mapFrom).c_str());
                else if (mapOwn) Log(L"map: %s, %g C outside (AmbientC: AmbientByMap=0 leaves [Map:%s] out)", Wide(title).c_str(), s.ambientC, Wide(mapFrom).c_str());
                else if (map[0]) Log(L"map: %s has no section in Gauges.ini, nor has its region, so it is %g C outside (AmbientC)", Wide(title).c_str(), s.ambientC);
            }
            strcpy_s(mapRead, map);
            mapAsk = false;
        }
        GaugeSettings inForce = s;
        if (mapOwn && s.ambientByMap) inForce.ambientC = hour >= 0.0f ? DayNightC(mapAir.day, mapAir.night, hour) : mapAir.mean;
        if (s.probe && hour >= 0.0f && (int)hour != hourSaid)
        {
            hourSaid = (int)hour;
            Log(L"clock: %02d:%02d of the game's day, %.1f C outside", (int)hour, (int)((hour - (int)hour) * 60.0f), inForce.ambientC);
        }
        // what the cluster shows: the truck being driven; with the ini's Demo, demo values while none is
        TruckRaw raw;
        World world;
        watch.ownScales = set.own;
        const bool driving = TruckPass(watch, memory, inForce, front, raw, world);
        wasDriving = driving;
        if (watch.found && !codeLooked)
        {
            codeLooked = true;
            GameCodeLook();
        }
        // the search for the game's clock (Probe=1, while its place is not known): begun once a truck is driven
        if (!clockOwn && s.probe && driving && raw.clockKnown)
        {
            if (clockScan.state == kClockIdle)
            {
                const ULONGLONG began = GetTickCount64();
                ClockScanBegin(clockScan, memory, ImageSize());
                Log(L"clock: looking for the game's time of day: %zu objects (%u KB), %zu places with a value from 0 to 24 (%llu ms). Drive on; "
                    L"skip the night on the map (or wait in a garage) once and it is found",
                    clockScan.objects.size(), clockScan.bytes / 1024, clockScan.slots.size(), GetTickCount64() - began);
            }
            const ClockNews news = ClockScanStep(clockScan, memory, watch.refs.vehicle, raw.clock, driving);
            if (news.steadyNow || news.jumpedNow)
                for (ClockSlot &slot : clockScan.slots)
                    if (slot.steady >= kClockSteady && (!slot.told || (slot.jumped && news.jumpedNow)))
                    {
                        char words[96];
                        const ClockPath p = ClockSlotPath(clockScan, slot, slot.most > 1.05 ? 1.0f : 24.0f);
                        ClockPathWords(p, words, sizeof words);
                        Log(L"clock: %S %s at %.3f, a day of %.0f minutes by its pace", words, slot.jumped ? L"jumped and kept its pace" : L"rises steadily",
                            slot.last, ClockDayMinutes(slot));
                        slot.told = true;
                    }
            if (news.looked && !news.found && !news.failed && clockScan.looks % 6 == 0)
                Log(L"clock: %d places still rise at a day's pace", news.left);
            if (news.found)
            {
                clockPath = clockScan.found;
                clockOwn = true;
                ClockSave(stamp, clockPath);
                char words[96];
                ClockPathWords(clockPath, words, sizeof words);
                Log(L"clock: the game's time of day is at %S (kept in Gauges.ini under [Clock]); the outside temperature follows it from now on", words);
                clockScan = ClockScan{};
            }
            else if (news.failed && !clockSaid)
            {
                Log(L"clock: no place rose at a day's pace; the time of day stays unknown until the game is started again");
                clockSaid = true;
            }
        }
        const TruckIdentity &now = watch.who.taken;
        if (strcmp(now.key, truck.key))
        {
            // another truck, or none: what was changed for the last one is saved first. Then the plain scales are in
            // force, with the new truck's own in their place where it has some.
            if (saveAt) SettingsSave(set);
            saveAt = 0;
            truck = now;
            SettingsFor(set, truck.key);
            SettingsPublish(s);
            const GaugeScale k = GaugeScaleOf(s);
            watch.ownScales = set.own;
            const int autoClass = AutoClass(watch, s.tachClass);
            if (truck.key[0])
                Log(L"truck: this is %S%s, the game's id for it %S; %s: idle %.0f rpm, red zone from %.0f of %.0f, speedometer to %.0f km/h; the automatic "
                    L"goes by the %s class%s",
                    truck.key, truck.named ? L"" : L" (the name of its kind was not read, so it goes by its build)", truck.id[0] ? truck.id : "not read",
                    set.own ? L"it has scales of its own" : L"it has no scales of its own and takes the plain ones", k.idle, k.redline, k.max, k.speedMax,
                    kClassWords[autoClass], set.own ? L", its scales'" : watch.mass > 0.0f ? L", its weight's" : L", the plain scales' (its weight not read)");
        }
        else if (strcmp(now.id, truck.id))
        {
            // the same kind under another id: the game made the truck anew, or this is another truck of the kind
            truck = now;
            Log(L"truck: %S again, the game's id for it now %S", truck.key, truck.id);
        }
        GaugeView v = {};
        strcpy_s(v.truck, truck.key);
        v.truckOwn = set.own;
        if (map[0]) MapTitle(map, v.map, sizeof v.map);
        else v.map[0] = 0;
        strcpy_s(v.mapFrom, mapFrom);
        v.mapOwn = mapOwn && s.ambientByMap;
        v.ambientC = inForce.ambientC;
        v.ambientDay = mapAir.day;
        v.ambientNight = mapAir.night;
        v.hour = hour;
        v.smtInstalled = g_gearboxBlocked == kGearboxSmt;
        v.gearboxHeld = g_gearboxBlocked == kGearboxHeld;
        for (int a = 0; a < kViewActions; a++)
            for (int slot = 0; slot < 2; slot++)
                if (a < kActCount) BindingText(binds.slot[a][slot], v.bindText[a][slot], sizeof v.bindText[a][slot]);
                else v.bindText[a][slot][0] = 0;
        v.captureAction = bindCapture.active ? bindCapture.action : -1;
        v.captureSlot = bindCapture.active ? bindCapture.slot : -1;
        v.canReverse = watch.canReverse;
        v.controllers = InputDevicesFound();
        v.gearStalls = watch.take.driving ? watch.pos.stalls : 0;
        v.gearShifts = watch.take.driving ? watch.take.shifts : 0;
        strcpy_s(v.gearbox, driving ? watch.gearboxKey : "");
        // the engine and its curve in force, for the tab's Engine section and its plot
        if (driving && watch.profileFor[0])
        {
            const GaugeScale es = GaugeScaleOf(inForce);
            const TorqueCurve ec = EngineCurveOf(watch.profile, kClassCurves[AutoClass(watch, inForce.tachClass)], es.redline);
            strncpy_s(v.engine, watch.profileFor, _TRUNCATE);
            v.engineOwn = watch.profile.own;
            v.engineCustom = watch.profile.custom;
            v.engineAspiration = watch.profile.aspiration;
            v.engineTorque = watch.profile.torque > 0.0f ? watch.profile.torque : 1.0f;
            v.engineFigure = watch.engine.torque;
            v.enginePeakRpm = watch.profile.peakRpm > 0.0f && !watch.profile.custom ? watch.profile.peakRpm : CurvePeak(ec) * es.redline;
            v.engineIdle = es.idle;
            v.engineRedline = es.redline;
            const float top = es.redline > 0.0f ? es.max / es.redline : 1.0f, from = es.redline > 0.0f ? es.idle / es.redline : 0.0f;
            for (int i = 0; i < kEnginePlot; i++) v.engineCurve[i] = TorqueAt(ec, from + (top - from) * i / (kEnginePlot - 1), top);
        }
        v.gearHold = GearHoldOf(watch, AutoClass(watch, inForce.tachClass));
        v.gearHoldOwn = watch.gearHoldOwn;
        v.autoClass = AutoClass(watch, inForce.tachClass);
        v.autoByWeight = driving && !watch.ownScales && watch.mass > 0.0f;
        v.massKg = driving ? watch.mass : 0.0f;
        v.pullTest = watch.pullTest.state;
        v.pullPhase = watch.pullTest.phase;
        v.pullWhy = watch.pullTest.why;
        v.demo = !driving && s.demo;
        v.hidden = !visible ? kHiddenByToggle : s.soundsOnly ? kHiddenSoundsOnly : driving || s.demo ? kShown : kHiddenNoTruck;
        v.show = v.hidden == kShown;
        if (driving)
        {
            TruckToState(raw, world, inForce, v.st);
            v.st.rpm = watch.tach.rpm;
            if (watch.take.driving && watch.pos.running)
            {
                // the game's flag is down, so its own letter would be a number: the gearbox's position gives it
                v.st.gearMode = GearboxPosLetter(watch.pos.pos);
                v.st.lowGear = watch.pos.pos >= kPosLowMinus && watch.pos.pos <= kPosLowPlus;
            }
            v.st.boostBar = watch.boost.bar;
            v.st.natural = watch.profile.aspiration == kAspNatural;
            v.st.airBar = watch.air.bar;
            v.st.rangeKm = watch.range.rangeKm;
            v.st.burnPerMin = watch.burnKnown ? watch.burn : -1.0f;
            v.st.tilt = watch.tilt.running ? watch.tilt.lamp : -1;
            v.st.rollDeg = watch.tilt.rollDeg;
            v.st.tempC = watch.temp.temp;
            v.st.oilC = watch.temp.oil;
            v.st.cold = TempCold(watch.temp);
            // the damage lamp blinks while the model adds damage, and for two seconds after a write; the oil lamp
            // while the oil is too hot
            const ULONGLONG ms = GetTickCount64();
            v.st.oilHot = OilLamp(watch.oil, ms);
            TiresLamp(watch.tires, watch.refs.vehicle, ms, v.st.tireLamp, v.st.tireLit, v.st.tireWarn);
            v.st.damage = DamageLamp(v.st.damage, DamageAdding(watch.damage, s) || (watch.addedAt && ms - watch.addedAt < 2000), ms);
            v.engineDamage = raw.damage;
            v.engineCapacity = raw.damageKnown ? raw.damageCapacity : 0;
        }
        else if (v.demo) GaugeDemo(v.st, s);
        // the clock: the game's hour and the outside temperature in force (the demo keeps its own)
        if (!v.demo)
        {
            v.st.hour = hour;
            v.st.outsideC = inForce.ambientC;
        }
        AcquireSRWLockExclusive(&g_viewLock);
        g_view = v;
        ReleaseSRWLockExclusive(&g_viewLock);
        // and for other mods: the outside temperature in force always, the truck's values while one is driven
        StateValues published = {};
        published.ambientC = inForce.ambientC;
        if (mapOwn && s.ambientByMap) published.flags |= kStateByMap;
        if (driving)
        {
            published.vehicle = watch.refs.vehicle;
            published.coolantC = watch.temp.temp;
            published.rpm = watch.tach.rpm;
            published.boostBar = watch.boost.bar;
            published.flags |= kStateVehicle | (TruckEngineOn(raw) ? kStateEngineOn : 0);
        }
        StateWrite(state, published, GetTickCount64());
    }
}

// Loaded by an ASI loader (Gauges.asi), or by ReShade under the name from before 0.16 (Gauges.addon64): the mod's thread
// starts the overlay either way. The mod pins itself first and runs for the whole game: ReShade unloads an add-on that
// does not register with it, and the gearbox hooks must outlive any unload.
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t *name = wcsrchr(path, L'\\');
    wchar_t own[MAX_PATH] = {};
    GetModuleFileNameW(inst, own, MAX_PATH);
    const wchar_t *dot = wcsrchr(own, L'.');
    const bool addon = dot && !_wcsnicmp(dot, L".addon", 6);
    if (!name || _wcsicmp(name + 1, L"SnowRunner.exe")) return addon ? FALSE : TRUE; // loaded into another program: do nothing
    g_dir.assign(path, name + 1 - path);
    g_base = (uint64_t)GetModuleHandleW(nullptr);
    g_self = inst;
    g_addon = addon;
    // One copy of the mod per game: a second file of it (Gauges.asi beside Gauges.addon64, or a copy left under
    // another name) would draw a second cluster and write the same ini. The mutex is the process's.
    wchar_t once[64];
    swprintf_s(once, L"Local\\SnowRunnerGauges-%lu", GetCurrentProcessId());
    const HANDLE mutex = CreateMutexW(nullptr, FALSE, once);
    g_second = mutex && GetLastError() == ERROR_ALREADY_EXISTS;
    // pinned in every case: the gearbox hooks live as long as the game, so no unload path exists (review 6, G3)
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&DllMain, &pinned);
    const HANDLE t = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    return TRUE;
}
#else
// The offline test of the settings: build.bat makes out\test\settings_test.exe from this file with
// GAUGES_SETTINGS_TEST. It works on a Gauges.ini next to itself: a missing ini comes out as the defaults with the help
// text, every key reads back the same (every setting is given a value that is not its default, so a key that is never
// written shows), the help text and keys the mod does not know survive a write, values that are no numbers give the
// defaults, values out of range are held to it, the scales fit each other, a truck's own scales come and go with its
// section, and the maps' sections are written once and read. With an ini as its argument, a copy of that file must
// read back unchanged after a write.
bool OverlayInit(HMODULE) { return false; }
volatile LONG g_pageCapturing = 0, g_modOff = 0;

static bool SameSettings(const GaugeSettings &a, const GaugeSettings &b)
{
    return a.layout == b.layout && a.swapDials == b.swapDials && a.style == b.style && a.faceOpacity == b.faceOpacity && a.offsetX == b.offsetX && a.offsetY == b.offsetY &&
           a.scale == b.scale && a.opacity == b.opacity && a.mph == b.mph && a.psi == b.psi && a.fahrenheit == b.fahrenheit && a.showTemp == b.showTemp &&
           a.showBoost == b.showBoost && a.showAir == b.showAir && a.showClock == b.showClock && a.showBurn == b.showBurn && a.showLamps == b.showLamps && a.tachClass == b.tachClass && a.idleRpm == b.idleRpm &&
           a.redlineRpm == b.redlineRpm && a.maxRpm == b.maxRpm && a.speedMax == b.speedMax && a.boostMax == b.boostMax && a.toggleKey == b.toggleKey &&
           a.rollWarnDeg == b.rollWarnDeg && a.rollDangerDeg == b.rollDangerDeg && a.pitchDangerDeg == b.pitchDangerDeg &&
           a.warmUpMinutes == b.warmUpMinutes && a.overheatMinutes == b.overheatMinutes && a.coolDownMinutes == b.coolDownMinutes && a.ambientC == b.ambientC &&
           a.ambientByMap == b.ambientByMap && a.airFullKmh == b.airFullKmh && a.damage == b.damage && a.hotDamage == b.hotDamage && a.hotDamageFrom == b.hotDamageFrom && a.coldDamage == b.coldDamage && a.stallDamage == b.stallDamage && a.clutchBurn == b.clutchBurn &&
           !strcmp(a.padToggle, b.padToggle) && a.demo == b.demo && a.probe == b.probe && a.soundVolume == b.soundVolume && a.soundsOnly == b.soundsOnly &&
           a.oilGauge == b.oilGauge && a.boostProfile == b.boostProfile && a.oilWarmUpMinutes == b.oilWarmUpMinutes && a.oilWarnC == b.oilWarnC && a.gearbox == b.gearbox && a.requireClutch == b.requireClutch && a.requireGearHeld == b.requireGearHeld && a.skipNeutral == b.skipNeutral && a.input == b.input && a.powertrain == b.powertrain && a.engineShare == b.engineShare;
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
    Check(d.layout == kLayoutStrip && !d.swapDials && d.style == kFaceOpen && !d.showAir && d.showClock && d.showBurn && !d.demo && d.probe == 0 && !d.mph && !d.psi && !d.fahrenheit && d.toggleKey == 0 &&
          !strcmp(d.padToggle, "None") && d.rollWarnDeg == 15.0f && d.rollDangerDeg == 25.0f && d.pitchDangerDeg == 35.0f,
          "defaults: strip, open style, metric, the air dial off, no toggle, demo off, probe off, the tilt lamp at 15, 25 and 35 degrees");
    Check(d.warmUpMinutes == 3.0f && d.overheatMinutes == 1.5f && d.coolDownMinutes == 15.0f && d.ambientC == 15.0f && d.ambientByMap,
          "defaults: the coolant warm after 3 minutes, in the red zone after 1.5, cool after 15, with 15 C outside");
    Check(d.airFullKmh == 30.0f && d.damage && d.hotDamage == 10.0f && d.hotDamageFrom == kTempRed && d.coldDamage == 5.0f && d.stallDamage == 5.0f && d.clutchBurn,
          "defaults: the radiator's full airflow from 30 km/h, engine damage on from the red zone, 10 percent a minute for the heat and 5 for the cold");
    Check(d.oilGauge == kOilNeedle && d.oilWarmUpMinutes == 5.0f && d.oilWarnC == 120.0f && d.boostProfile == kSpoolStandard && d.gearbox == kGearboxGame && !d.requireClutch && !d.requireGearHeld && !d.skipNeutral && d.input == kInputWheel && !d.powertrain && d.engineShare == 50,
          "defaults: the oil as a second needle, warm after 5 minutes, too hot from 120 C; the standard turbo");

    // keys the mod does not know, in its own section and in another one
    WritePrivateProfileStringW(kIniSection, L"FromALaterVersion", L"7", ini.c_str());
    WritePrivateProfileStringW(L"Other", L"Kept", L"yes", ini.c_str());
    c = d;
    c.layout = kLayoutBlock;
    c.swapDials = true;
    c.style = kFaceTinted;
    c.faceOpacity = 0.6f;
    c.offsetX = -212.5f;
    c.offsetY = 37.25f;
    c.scale = 1.35f;
    c.opacity = 0.8f;
    c.mph = c.psi = c.fahrenheit = true;
    c.showTemp = c.showBoost = c.showLamps = false;
    c.showAir = true;
    c.showClock = false;
    c.showBurn = false;
    c.tachClass = kClassHeavy;
    c.idleRpm = 700.0f;
    c.redlineRpm = 2100.0f;
    c.maxRpm = 3500.0f;
    c.speedMax = 90.0f;
    c.boostMax = 1.25f;
    c.rollWarnDeg = 12.5f;
    c.rollDangerDeg = 22.0f;
    c.pitchDangerDeg = 30.0f;
    c.warmUpMinutes = 4.5f;
    c.overheatMinutes = 2.3f;
    c.coolDownMinutes = 20.0f;
    c.ambientC = -12.5f;
    c.ambientByMap = false;
    c.airFullKmh = 45.5f;
    c.damage = false;
    c.hotDamage = 22.5f;
    c.hotDamageFrom = 110.5f;
    c.coldDamage = 7.5f;
    c.stallDamage = 12.0f;
    c.clutchBurn = false;
    c.toggleKey = VK_F9;
    strcpy_s(c.padToggle, "RB+DPadUp");
    c.demo = true;
    c.probe = 1;
    c.soundVolume = 80;
    c.soundsOnly = true;
    c.oilGauge = kOilDial;
    c.boostProfile = kSpoolTwoStage;
    c.gearbox = kGearboxModel;
    c.requireClutch = c.requireGearHeld = c.skipNeutral = true;
    c.input = kInputKeys;
    c.powertrain = true;
    c.engineShare = 80;
    c.oilWarmUpMinutes = 12.5f;
    c.oilWarnC = 132.0f;
    const bool allChanged = c.layout != d.layout && c.swapDials != d.swapDials && c.style != d.style && c.faceOpacity != d.faceOpacity && c.offsetX != d.offsetX && c.offsetY != d.offsetY &&
                            c.scale != d.scale && c.opacity != d.opacity && c.mph != d.mph && c.psi != d.psi && c.fahrenheit != d.fahrenheit &&
                            c.showTemp != d.showTemp && c.showBoost != d.showBoost && c.showAir != d.showAir && c.showClock != d.showClock && c.showBurn != d.showBurn && c.showLamps != d.showLamps &&
                            c.tachClass != d.tachClass && c.idleRpm != d.idleRpm && c.redlineRpm != d.redlineRpm && c.maxRpm != d.maxRpm &&
                            c.speedMax != d.speedMax && c.boostMax != d.boostMax && c.rollWarnDeg != d.rollWarnDeg && c.rollDangerDeg != d.rollDangerDeg &&
                            c.pitchDangerDeg != d.pitchDangerDeg && c.warmUpMinutes != d.warmUpMinutes && c.overheatMinutes != d.overheatMinutes &&
                            c.coolDownMinutes != d.coolDownMinutes && c.ambientC != d.ambientC && c.ambientByMap != d.ambientByMap && c.airFullKmh != d.airFullKmh && c.damage != d.damage &&
                            c.hotDamage != d.hotDamage && c.hotDamageFrom != d.hotDamageFrom && c.coldDamage != d.coldDamage && c.stallDamage != d.stallDamage && c.clutchBurn != d.clutchBurn && c.toggleKey != d.toggleKey && strcmp(c.padToggle, d.padToggle) &&
                            c.demo != d.demo && c.probe != d.probe && c.soundVolume != d.soundVolume && c.soundsOnly != d.soundsOnly &&
                            c.oilGauge != d.oilGauge && c.boostProfile != d.boostProfile && c.oilWarmUpMinutes != d.oilWarmUpMinutes && c.oilWarnC != d.oilWarnC &&
                            c.gearbox != d.gearbox && c.requireClutch != d.requireClutch && c.requireGearHeld != d.requireGearHeld && c.skipNeutral != d.skipNeutral && c.input != d.input && c.powertrain != d.powertrain && c.engineShare != d.engineShare;
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
    Check(!wcsncmp(first, L"; Gauges:", 9) && comments == 94, "ini: the help text survives a write (94 comment lines)");

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
    WritePrivateProfileStringW(kIniSection, L"RollWarnDeg", L"40", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"RollDangerDeg", L"10", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"PitchDangerDeg", L"400", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"WarmUpMinutes", L"0", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"OverheatMinutes", L"1000", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"CoolDownMinutes", L"-5", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"AmbientC", L"nan", ini.c_str());
    ReadIni(b);
    Check(b.scale == d.scale && b.opacity == d.opacity, "ini: \"nan\" and \"inf\" give the defaults");
    Check(b.offsetX == 0.0f && b.faceOpacity == kFaceOpacityMax && b.layout == kLayoutStrip && b.style == kFaceOpen && b.tachClass == kClassTruck &&
          b.idleRpm == kIdleRange[0] && b.maxRpm == 0.0f && b.toggleKey == 0, "ini: values out of range are held to it");
    Check(b.rollWarnDeg == 40.0f && b.rollDangerDeg == 40.0f && b.pitchDangerDeg == kTiltRange[1], "ini: the tilt lamp's red never comes before its amber, and no angle passes 89 degrees");
    Check(b.warmUpMinutes == kWarmUpRange[0] && b.overheatMinutes == kOverheatRange[1] && b.coolDownMinutes == kCoolDownRange[0] && b.ambientC == d.ambientC,
          "ini: the coolant's three times are held to their ranges, and an outside temperature that is no number gives the default");
    WritePrivateProfileStringW(kIniSection, L"AmbientC", L"-90", ini.c_str());
    ReadIni(b);
    const float coldest = b.ambientC;
    WritePrivateProfileStringW(kIniSection, L"AmbientC", L"70", ini.c_str());
    ReadIni(b);
    Check(coldest == kAmbientRange[0] && b.ambientC == kAmbientRange[1], "ini: the outside temperature stays between 40 below and 50 C");
    WritePrivateProfileStringW(kIniSection, L"AirFullKmh", L"5", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"HotDamagePercentPerMinute", L"500", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"ColdDamagePercentPerMinute", L"-3", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"Damage", L"0", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"HotDamageFrom", L"90", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"StallDamagePercent", L"40", ini.c_str());
    ReadIni(b);
    const bool held = b.airFullKmh == kAirFullRange[0] && b.hotDamage == kDamageRange[1] && b.coldDamage == 0.0f && !b.damage && b.hotDamageFrom == kHotDamageFromRange[0] &&
                      b.stallDamage == kStallDamageRange[1];
    WritePrivateProfileStringW(kIniSection, L"StallDamagePercent", L"5", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"HotDamageFrom", L"130", ini.c_str());
    ReadIni(b);
    const bool fromHigh = b.hotDamageFrom == kHotDamageFromRange[1];
    WritePrivateProfileStringW(kIniSection, L"HotDamageFrom", L"118", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"AirFullKmh", L"200", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"HotDamagePercentPerMinute", L"nan", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"Damage", L"7", ini.c_str());
    ReadIni(b);
    Check(held && fromHigh && b.airFullKmh == kAirFullRange[1] && b.hotDamage == d.hotDamage && b.damage,
          "ini: the radiator's speed stays between 10 and 80 km/h, the damage rates between 0 and 100, where the hot damage starts between 100 and 124 C, "
          "and a rate that is no number gives the default");
    WritePrivateProfileStringW(kIniSection, L"OilWarmUpMinutes", L"0", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"OilWarnC", L"400", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"OilGauge", L"5", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"BoostProfile", L"-1", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"Gearbox", L"7", ini.c_str());
    ReadIni(b);
    Check(b.oilWarmUpMinutes == kOilWarmUpRange[0] && b.oilWarnC == kOilWarnRange[1] && b.oilGauge == kOilNeedle && b.boostProfile == kSpoolStandard &&
              b.gearbox == kGearboxGame,
          "ini: the oil's warm-up and warning temperature are held to their ranges, and an oil gauge, a turbo or a gearbox the mod does not have gives the default");

    // the scales in force
    GaugeScale k = GaugeScaleOf(d);
    Check(k.idle == 650.0f && k.redline == 2200.0f && k.max == 3000.0f && k.speedMax == 100.0f, "scales: the truck class by default");
    a = d;
    a.tachClass = kClassScout;
    k = GaugeScaleOf(a);
    Check(k.idle == 800.0f && k.redline == 5500.0f && k.max == 7000.0f && k.speedMax == 120.0f, "scales: the scout class");
    {
        // a class change: a number at the old class's own follows the new class, one set by hand stays, the class's stays the class's
        GaugeSettings sc = d;
        sc.tachClass = kClassTruck;
        sc.idleRpm = 650.0f;   // the truck class's own, written as a number
        sc.redlineRpm = 2600.0f; // set by hand
        sc.maxRpm = 0.0f;      // the class's
        sc.speedMax = 90.0f;   // set by hand
        sc.boostMax = 1.8f;    // the truck class's own
        sc.tachClass = kClassScout;
        ScalesClassChange(sc, kClassTruck);
        const GaugeScale ks = GaugeScaleOf(sc);
        Check(sc.idleRpm == 0.0f && sc.redlineRpm == 2600.0f && sc.maxRpm == 0.0f && sc.speedMax == 90.0f && sc.boostMax == 0.0f && ks.idle == 800.0f &&
                  ks.max == 7000.0f && ks.boost == 1.0f,
              "scales: on a class change the numbers set by hand stay, the ones at the old class's own follow the new class");
    }
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

    // a truck's own scales, in its file in Gauges\trucks: one state, as the mod's thread keeps it
    {
        auto truckFile = [](const char *key) { return SetupFile(kSetupTruck, key); };
        auto truckPut = [&](const char *key, const wchar_t *name, const wchar_t *value) {
            SetupFolderMake(kSetupTruck);
            WritePrivateProfileStringW(L"Truck", name, value, truckFile(key).c_str());
        };
        for (const char *key : { "someone_elses", "mod_suv/1000001", "chevrolet_ck1500", "half", "wild" }) DeleteFileW(truckFile(key).c_str());
        DeleteFileW(ini.c_str());
        SettingsState state = {};
        ReadIni(state.plain);
        state.plain.tachClass = kClassScout;
        state.plain.redlineRpm = 5000.0f;
        WriteIni(state.plain);
        // a truck this test never drives, and a truck's section with something in it that the mod does not know
        truckPut("someone_elses", L"RedlineRpm", L"1800");
        truckPut("mod_suv/1000001", L"FromALaterVersion", L"7");
        SettingsFor(state, "");
        const bool none = !state.own && SameSettings(state.s, state.plain);
        GaugeSettings in = state.s;
        in.maxRpm = 6000.0f;
        SettingsTake(state, in);
        const bool plainEdit = !state.own && !state.changed && state.plain.maxRpm == 6000.0f;
        SettingsFor(state, "mod_suv/1000001");
        Check(none && plainEdit && !state.own && SameSettings(state.s, state.plain), "trucks: with no truck driven the tab sets the plain scales, and a truck without scales of its own shows them");
        in = state.s;
        in.opacity = 0.5f;
        SettingsTake(state, in);
        Check(!state.own && state.plain.opacity == 0.5f && SameSettings(state.s, state.plain), "trucks: a setting that is no scale is every truck's, whichever truck is driven");
        in = state.s;
        in.redlineRpm = 4500.0f;
        SettingsTake(state, in);
        Check(state.own && state.changed && state.s.redlineRpm == 4500.0f && state.s.tachClass == kClassScout && state.plain.redlineRpm == 5000.0f,
              "trucks: a scale changed while a truck is driven makes the scales that truck's own and leaves the plain ones as they were");
        SettingsWrite(state);
        // a second truck, set up another way; then a setting that is no scale changed while it is driven
        SettingsFor(state, "chevrolet_ck1500");
        const bool ckPlain = !state.own && state.s.redlineRpm == 5000.0f && state.s.maxRpm == 6000.0f;
        in = state.s;
        in.tachClass = kClassHeavy;
        in.speedMax = 70.0f;
        in.boostMax = 1.45f;
        in.boostProfile = kSpoolLaggy;
        SettingsTake(state, in);
        in = state.s;
        in.scale = 1.5f;
        SettingsTake(state, in);
        SettingsWrite(state);
        Check(ckPlain && state.own && state.plain.scale == 1.5f && state.plain.tachClass == kClassScout && state.plain.speedMax == 0.0f && state.plain.boostMax == 0.0f &&
              state.plain.boostProfile == kSpoolStandard && state.plain.redlineRpm == 5000.0f,
              "trucks: a second truck starts from the plain scales, and what is changed beside its scales still goes to every truck");
        // the game is started again: [Gauges] holds the plain settings, each truck its own scales
        SettingsState next = {};
        ReadIni(next.plain);
        SettingsFor(next, "mod_suv/1000001");
        const bool moddedBack = next.own && next.s.redlineRpm == 4500.0f && next.s.tachClass == kClassScout && next.s.maxRpm == 6000.0f && next.s.speedMax == 0.0f && next.s.boostMax == 0.0f && next.s.scale == 1.5f;
        SettingsFor(next, "chevrolet_ck1500");
        Check(SameSettings(next.plain, state.plain) && moddedBack && next.own && next.s.tachClass == kClassHeavy && next.s.speedMax == 70.0f && next.s.boostMax == 1.45f &&
              GaugeScaleOf(next.s).boost == 1.45f && next.s.boostProfile == kSpoolLaggy && next.s.redlineRpm == 5000.0f,
              "trucks: after a new start [Gauges] reads back as the plain settings and each truck has its own scales again");
        wchar_t kept[16] = {}, later2[16] = {};
        GetPrivateProfileStringW(L"Truck", L"RedlineRpm", L"", kept, 16, truckFile("someone_elses").c_str());
        GetPrivateProfileStringW(L"Truck", L"FromALaterVersion", L"", later2, 16, truckFile("mod_suv/1000001").c_str());
        Check(!wcscmp(kept, L"1800") && !wcscmp(later2, L"7"), "trucks: the file of a truck that was not driven, and a key the mod does not know in a truck's file, survive the writes");
        // a section that holds one of the seven takes the others from [Gauges]; what is out of range is held to it
        truckPut("half", L"MaxRpm", L"4000");
        truckPut("wild", L"RedlineRpm", L"50");
        truckPut("wild", L"TachClass", L"9");
        truckPut("wild", L"IdleRpm", L"nan");
        truckPut("wild", L"BoostProfile", L"9");
        SettingsFor(next, "wild");
        const bool wild = next.own && next.s.redlineRpm == kRedlineRange[0] && next.s.tachClass == kClassTruck && next.s.idleRpm == next.plain.idleRpm &&
                          next.s.boostProfile == kSpoolStandard;
        SettingsFor(next, "half");
        Check(wild && next.own && !next.changed && next.s.maxRpm == 4000.0f && next.s.redlineRpm == 5000.0f && next.s.tachClass == kClassScout,
              "trucks: a file takes from [Gauges] what it leaves out, and its values are held to their ranges as those of [Gauges] are");
        // a save after a change of something else leaves such a section as it is; after a change of a scale it has all seven
        wchar_t halfRed[16] = {};
        in = next.s;
        in.opacity = 0.75f;
        SettingsTake(next, in);
        SettingsWrite(next);
        GetPrivateProfileStringW(L"Truck", L"RedlineRpm", L"", halfRed, 16, truckFile("half").c_str());
        const bool leftAlone = next.own && !halfRed[0];
        in = next.s;
        in.idleRpm = 900.0f;
        SettingsTake(next, in);
        SettingsWrite(next);
        GetPrivateProfileStringW(L"Truck", L"RedlineRpm", L"", halfRed, 16, truckFile("half").c_str());
        wchar_t halfTurbo[16] = {};
        GetPrivateProfileStringW(L"Truck", L"BoostProfile", L"", halfTurbo, 16, truckFile("half").c_str());
        Check(leftAlone && !wcscmp(halfRed, L"5000") && !wcscmp(halfTurbo, L"0") && !next.changed,
              "trucks: a save leaves a truck's file as it is until one of its scales is changed on the tab, and then writes all seven");
        // the truck gives its scales up: they go out of the ini, what else its section holds stays, and a section left empty goes
        SettingsFor(next, "mod_suv/1000001");
        SettingsDrop(next);
        const bool dropped = !next.own && SameSettings(next.s, next.plain);
        SettingsFor(next, "mod_suv/1000001");
        GetPrivateProfileStringW(L"Truck", L"FromALaterVersion", L"", later2, 16, truckFile("mod_suv/1000001").c_str());
        const bool moddedPlain = !next.own && SameSettings(next.s, next.plain) && !wcscmp(later2, L"7");
        SettingsFor(next, "chevrolet_ck1500");
        SettingsDrop(next);
        wchar_t rest[64] = {};
        const DWORD left = GetPrivateProfileSectionW(L"Truck", rest, 64, truckFile("chevrolet_ck1500").c_str());
        const bool ckSection = GetFileAttributesW(truckFile("chevrolet_ck1500").c_str()) != INVALID_FILE_ATTRIBUTES;
        Check(dropped && moddedPlain && !next.own && !left && !ckSection,
              "trucks: a truck that gives up its scales takes the plain ones, its file keeps what else it holds, and an empty file goes");
        // the defaults: the plain settings and the driven truck's own scales; another truck keeps its own
        SettingsFor(next, "half");
        const bool halfOwn = next.own;
        SettingsToDefaults(next);
        SettingsWrite(next);
        GaugeSettings defaults, fromIni;
        SettingsDefaults(defaults);
        ReadIni(fromIni);
        wchar_t halfMax[16] = {};
        GetPrivateProfileStringW(L"Truck", L"MaxRpm", L"", halfMax, 16, truckFile("half").c_str());
        GetPrivateProfileStringW(L"Truck", L"RedlineRpm", L"", kept, 16, truckFile("someone_elses").c_str());
        Check(halfOwn && !next.own && SameSettings(next.s, defaults) && SameSettings(fromIni, defaults) && !halfMax[0] && !wcscmp(kept, L"1800"),
              "trucks: the defaults take the plain settings and the driven truck's own scales, and leave the other trucks' files as they were");
            // the files' names: a modded truck's stroke as "@", what no file's name may hold as "_"
        Check(SetupFileName(L"mod_suv/1000001") == L"mod_suv@1000001" && SetupFileName(L"a:b*c?") == L"a_b_c_" &&
                  SetupFileName(L"gears 1.5 4 high 2") == L"gears 1.5 4 high 2" && truckFile("mod_suv/1000001").find(L"Gauges\\trucks\\mod_suv@1000001.ini") != std::wstring::npos,
              "setups: a file's name from the truck's kind or the gearbox's name");
        // a gearbox's hold into its file and back, held to its range
        DeleteFileW(SetupFile(kSetupGearbox, "g_test_box").c_str());
        float holdBack = 0.0f, holdWild = 0.0f;
        const bool noHold = !GearHoldRead("g_test_box", holdBack);
        GearHoldWrite("g_test_box", 1.7f);
        const bool holdRead = GearHoldRead("g_test_box", holdBack);
        WritePrivateProfileStringW(L"Gearbox", L"GearHold", L"9", SetupFile(kSetupGearbox, "g_test_box").c_str());
        GearHoldRead("g_test_box", holdWild);
        wchar_t boxName[32] = {};
        GetPrivateProfileStringW(L"Gearbox", L"Name", L"", boxName, 32, SetupFile(kSetupGearbox, "g_test_box").c_str());
        Check(noHold && holdRead && fabsf(holdBack - 1.7f) < 0.001f && holdWild == kGearHoldRange[1] && !wcscmp(boxName, L"g_test_box"),
              "setups: a gearbox's hold in its own file with its name, read back, held to 1 to 3 seconds");
        // an engine's profile: its peak torque's speed and aspiration into its file and back; a curve of its own taken
        // in place of the one from the peak; what is out of range held to it; none without a file
        const char *engineName = "test_engine_2";
        DeleteFileW(SetupFile(kSetupEngine, engineName).c_str());
        EngineProfile profile;
        const bool noProfile = !EngineProfileRead(engineName, profile) && !profile.own;
        EngineProfileWrite(engineName, 1950.0f, kAspNatural);
        const bool profileBack = EngineProfileRead(engineName, profile) && profile.own && profile.peakRpm == 1950.0f && profile.aspiration == kAspNatural && !profile.custom;
        const TorqueCurve fromPeak = EngineCurveOf(profile, kClassCurves[kClassScout], 4850.0f);
        WritePrivateProfileStringW(L"Engine", L"TorqueCurve", L"0.3:0.7,0.5:1,1:0.75", SetupFile(kSetupEngine, engineName).c_str());
        WritePrivateProfileStringW(L"Engine", L"Aspiration", L"7", SetupFile(kSetupEngine, engineName).c_str());
        EngineProfileRead(engineName, profile);
        const TorqueCurve own = EngineCurveOf(profile, kClassCurves[kClassScout], 4850.0f);
        WritePrivateProfileStringW(L"Engine", L"PeakTorqueRpm", L"50000", SetupFile(kSetupEngine, engineName).c_str());
        EngineProfile wildEngine;
        EngineProfileRead(engineName, wildEngine);
        Check(noProfile && profileBack && fabsf(CurvePeak(fromPeak) - 1950.0f / 4850.0f) < 0.001f && profile.custom && own.n == 3 && own.x[1] == 0.5f &&
                  profile.aspiration == kAspTurbo && wildEngine.peakRpm == kPeakRpmRange[1],
              "setups: an engine's profile in its own file, read back; its curve from its peak torque's speed, or its own written one; out of range held");
        // its torque share: into its file and back, held to 10 to 100 percent; none set, all of it
        const bool torqueNone = wildEngine.torque == 0.0f && TorqueHeld(3.0f, wildEngine.torque) == 3.0f;
        EngineTorqueWrite(engineName, 25);
        EngineProfile heldEngine;
        EngineProfileRead(engineName, heldEngine);
        const bool torqueBack = fabsf(heldEngine.torque - 0.25f) < 0.001f;
        WritePrivateProfileStringW(L"Engine", L"Torque", L"5", SetupFile(kSetupEngine, engineName).c_str());
        EngineProfileRead(engineName, heldEngine);
        const bool torqueLow = heldEngine.torque == kTorqueShareRange[0];
        WritePrivateProfileStringW(L"Engine", L"Torque", L"150", SetupFile(kSetupEngine, engineName).c_str());
        EngineProfileRead(engineName, heldEngine);
        Check(torqueNone && torqueBack && torqueLow && heldEngine.torque == kTorqueShareRange[1],
              "setups: an engine's torque share in its file, read back and held to 10 to 100 percent; none set leaves the push as it is");
        DeleteFileW(SetupFile(kSetupEngine, engineName).c_str());
        // the sections of 0.13.9 and before moved out of Gauges.ini into the files, a key the file holds already kept
        DeleteFileW(truckFile("mover/123").c_str());
        DeleteFileW(SetupFile(kSetupGearbox, "g_moved").c_str());
        truckPut("mover/123", L"TachClass", L"1");
        WritePrivateProfileStringW(L"Truck:mover/123", L"TachClass", L"2", ini.c_str());
        WritePrivateProfileStringW(L"Truck:mover/123", L"RedlineRpm", L"3000", ini.c_str());
        WritePrivateProfileStringW(L"Gearbox:g_moved", L"GearHold", L"1.2", ini.c_str());
        GaugeSettings afterMove;
        ReadIni(afterMove);
        wchar_t movedClass[16] = {}, movedRed[16] = {}, movedKind[32] = {}, iniNames[2048] = {};
        GetPrivateProfileStringW(L"Truck", L"TachClass", L"", movedClass, 16, truckFile("mover/123").c_str());
        GetPrivateProfileStringW(L"Truck", L"RedlineRpm", L"", movedRed, 16, truckFile("mover/123").c_str());
        GetPrivateProfileStringW(L"Truck", L"Kind", L"", movedKind, 32, truckFile("mover/123").c_str());
        float movedHold = 0.0f;
        const bool holdMoved = GearHoldRead("g_moved", movedHold) && fabsf(movedHold - 1.2f) < 0.001f;
        GetPrivateProfileSectionNamesW(iniNames, 2048, ini.c_str());
        bool leftInIni = false;
        for (const wchar_t *n = iniNames; *n; n += wcslen(n) + 1) leftInIni = leftInIni || !_wcsnicmp(n, L"Truck:", 6) || !_wcsnicmp(n, L"Gearbox:", 8);
        Check(!wcscmp(movedClass, L"1") && !wcscmp(movedRed, L"3000") && !wcscmp(movedKind, L"mover/123") && holdMoved && !leftInIni,
              "setups: the trucks' and gearboxes' sections of Gauges.ini moved into their own files, what a file held already kept, none left behind");
    }

    // the saves: the newest of Steam's CompleteSave*.cfg and Epic's CompleteSave*.dat in the folders looked in; a path
    // in the log with the profile's folder written %USERPROFILE%
    {
        const std::wstring root = g_dir + L"savetest", steamDir = root + L"\\steam", epicDir = root + L"\\epic";
        CreateDirectoryW(root.c_str(), nullptr);
        CreateDirectoryW(steamDir.c_str(), nullptr);
        CreateDirectoryW(epicDir.c_str(), nullptr);
        auto saveAt = [](const std::wstring &file, int minutesAgo, const char *text = "{}") {
            const HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            DWORD put = 0;
            WriteFile(h, text, (DWORD)strlen(text), &put, nullptr);
            FILETIME now;
            GetSystemTimeAsFileTime(&now);
            ULARGE_INTEGER t;
            t.LowPart = now.dwLowDateTime;
            t.HighPart = now.dwHighDateTime;
            t.QuadPart -= (ULONGLONG)minutesAgo * 60ULL * 10000000ULL;
            FILETIME then = { t.LowPart, t.HighPart };
            SetFileTime(h, nullptr, nullptr, &then);
            CloseHandle(h);
        };
        saveAt(steamDir + L"\\CompleteSave.cfg", 30);
        saveAt(epicDir + L"\\CompleteSave1.dat", 5);
        saveAt(epicDir + L"\\CompleteSaveBackup.txt", 1); // no save
        auto newestIn = [](const std::vector<std::wstring> &folders) {
            std::vector<SaveFile> found;
            SavesIn(folders, found);
            return found.empty() ? std::wstring() : found[0].first;
        };
        const bool epicNewest = newestIn({ steamDir, epicDir }) == epicDir + L"\\CompleteSave1.dat";
        const bool steamOnly = newestIn({ steamDir }) == steamDir + L"\\CompleteSave.cfg";
        const bool none = newestIn({ root }).empty();
        wchar_t profile[MAX_PATH] = {};
        GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
        const bool masked = !profile[0] || PathForLog(std::wstring(profile) + L"\\Documents\\x") == L"%USERPROFILE%\\Documents\\x";
        Check(epicNewest && steamOnly && none && masked && PathForLog(L"D:\\Games\\x") == L"D:\\Games\\x",
              "saves: the newest of Steam's .cfg and Epic's .dat is found, nothing else is taken for one; the profile's folder stays out of the log");
        // the slot being played: the newest save that names a map, past an empty slot the game wrote after it (as
        // on 25 October); nothing new while that save is the same; a save being written ends the look, and an older
        // slot does not take its place
        const std::wstring played = steamDir + L"\\CompleteSave.cfg", emptySlot = steamDir + L"\\CompleteSave1.cfg", older = steamDir + L"\\CompleteSave2.cfg";
        saveAt(older, 30, "{\"CompleteSave2\":{\"lastLoadedLevel\":\"level_us_01_01\"}}");
        saveAt(played, 3, "{\"CompleteSave\":{\"lastLoadedLevel\":\"level_ru_17_02\"}}");
        saveAt(emptySlot, 2, "{\"CompleteSave1\":{\"lastLoadedLevel\":\"\",\"discoveredObjects\":[]}}");
        std::vector<SaveFile> saves;
        SavesIn({ steamDir }, saves);
        SaveFile picked;
        char level[64] = "";
        std::wstring unread;
        const FILETIME never = {};
        const bool pastEmpty = SavePick(saves, never, picked, level, sizeof level, unread) && picked.first == played && !strcmp(level, "level_ru_17_02") && unread.empty();
        const FILETIME pickedAt = picked.second;
        SaveFile again;
        const bool nothingNew = !SavePick(saves, pickedAt, again, level, sizeof level, unread) && unread.empty();
        saveAt(played, 1, "{\"CompleteSave\":{\"lastLoadedLevel\":\"level_ru_"); // being written
        saves.clear();
        SavesIn({ steamDir }, saves);
        const bool waits = !SavePick(saves, never, again, level, sizeof level, unread) && unread == played;
        Check(pastEmpty && nothingNew && waits,
              "saves: the map from the newest save that names one, past an empty slot written after it; none from a save being written, nor from an older slot");
        for (const wchar_t *f : { L"\\steam\\CompleteSave.cfg", L"\\steam\\CompleteSave1.cfg", L"\\steam\\CompleteSave2.cfg", L"\\epic\\CompleteSave1.dat",
                                  L"\\epic\\CompleteSaveBackup.txt" })
            DeleteFileW((root + f).c_str());
        RemoveDirectoryW(steamDir.c_str());
        RemoveDirectoryW(epicDir.c_str());
        RemoveDirectoryW(root.c_str());
    }

    // the last start's log kept in Gauges\logs, named by the day and time it began: the same day, a drive past
    // midnight, another log of the same start's time; an empty log left where it is
    {
        const std::wstring root = g_dir + L"logtest\\", kept = root + L"Gauges\\logs\\", log = root + L"Gauges.log";
        CreateDirectoryW(root.c_str(), nullptr);
        auto logAt = [&](const char *first, WORD day, WORD hour, WORD minute) {
            FILE *f = nullptr;
            if (_wfopen_s(&f, log.c_str(), L"wb") == 0 && f)
            {
                fputs("\xEF\xBB\xBF", f);
                fputs(first, f);
                fputs(" Gauges " GAUGES_VERSION "\n", f);
                fclose(f);
            }
            SYSTEMTIME st = { 2026, 10, 0, day, hour, minute, 0, 0 };
            FILETIME lt, ut;
            SystemTimeToFileTime(&st, &lt);
            LocalFileTimeToFileTime(&lt, &ut);
            HANDLE file = CreateFileW(log.c_str(), FILE_WRITE_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (file != INVALID_HANDLE_VALUE)
            {
                SetFileTime(file, nullptr, nullptr, &ut);
                CloseHandle(file);
            }
            return LogKeepLast(root);
        };
        const std::wstring sameDay = logAt("21:29:11", 12, 23, 10), pastMidnight = logAt("23:50:00", 13, 0, 20), again = logAt("21:29:11", 12, 22, 0);
        const bool moved = GetFileAttributesW((kept + sameDay).c_str()) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW(log.c_str()) == INVALID_FILE_ATTRIBUTES;
        FILE *f = nullptr;
        if (_wfopen_s(&f, log.c_str(), L"wb") == 0 && f) fclose(f);
        const bool emptyStays = LogKeepLast(root).empty() && GetFileAttributesW(log.c_str()) != INVALID_FILE_ATTRIBUTES;
        Check(sameDay == L"Gauges 2026-10-12 21-29-11.log" && pastMidnight == L"Gauges 2026-10-12 23-50-00.log" && again == L"Gauges 2026-10-12 21-29-11 (2).log" &&
                  moved && emptyStays,
              "log: the last start's log is kept in Gauges\\logs by the day and time it began, a drive past midnight by the day before, a second of the "
              "same time beside the first; an empty one stays");
        for (const std::wstring &name : { sameDay, pastMidnight, again }) DeleteFileW((kept + name).c_str());
        DeleteFileW(log.c_str());
        RemoveDirectoryW(kept.c_str());
        RemoveDirectoryW((root + L"Gauges").c_str());
        RemoveDirectoryW(root.c_str());
    }

    // the maps' sections: the regions' written into an ini that has none; a map takes its own section, else its
    // region's, else one named after its id, held to the range; one taken out by hand stays out; the placeholder
    // names of 0.11.0 to 0.11.2 are renamed with their temperatures, once
    {
        DeleteFileW(ini.c_str());
        GaugeSettings fresh;
        ReadIni(fresh);
        char section[64];
        float deg = 0.0f;
        const bool regionsWritten = MapSectionAmbient("Michigan", deg) && deg == 12.0f && MapSectionAmbient("Zurdania", deg) && deg == 22.0f &&
                                    MapSectionAmbient("kola peninsula", deg) && deg == -10.0f;
        MapAir air = {};
        const bool byRegion = MapAmbient("level_us_01_04_new", air, section, sizeof section) && air.mean == 12.0f && !strcmp(section, "Michigan") &&
                              !air.dayNight && air.day == 12.0f + kDaySwing * 0.5f && air.night == 12.0f - kDaySwing * 0.5f;
        const bool unknown = !MapAmbient("level_nowhere", air, section, sizeof section) && !section[0] && !MapSectionAmbient("", deg);
        WritePrivateProfileStringW(L"Map:Black River", L"AmbientC", L"-3", ini.c_str());
        WritePrivateProfileStringW(L"Map:level_my_map", L"AmbientC", L"99", ini.c_str());
        WritePrivateProfileStringW(L"Map:Alaska", nullptr, nullptr, ini.c_str());
        ReadIni(fresh);
        const bool byMap = MapAmbient("level_us_01_01", air, section, sizeof section) && air.mean == -3.0f && !strcmp(section, "Black River");
        const bool sameRegion = MapAmbient("level_us_01_02", air, section, sizeof section) && air.mean == 12.0f;
        const bool byId = MapAmbient("level_my_map", air, section, sizeof section) && air.mean == kAmbientRange[1] && !strcmp(section, "level_my_map");
        const bool takenOut = !MapAmbient("level_us_02_01", air, section, sizeof section);
        const bool swissFresh = MapAmbient("level_us_19_02", air, section, sizeof section) && air.mean == -10.0f && !strcmp(section, "Switzerland");
        WritePrivateProfileStringW(L"Map:Switzerland", nullptr, nullptr, ini.c_str());
        WritePrivateProfileStringW(L"Gauges", L"MapRegionsOffered", nullptr, ini.c_str());
        ReadIni(fresh);
        const bool swissOffered = MapAmbient("level_us_19_01", air, section, sizeof section) && air.mean == -10.0f;
        WritePrivateProfileStringW(L"Map:Switzerland", nullptr, nullptr, ini.c_str());
        ReadIni(fresh);
        const bool swissStaysOut = !MapAmbient("level_us_19_03", air, section, sizeof section);
        char swissTitle[96];
        MapTitle("level_us_19_01", swissTitle, sizeof swissTitle);
        const bool swissName = Wide(swissTitle) == L"Gr\u00fcntal Hollow, Switzerland";
        Check(swissFresh && swissOffered && swissStaysOut && swissName,
              "maps: Switzerland (Season 19) is in a new ini, is offered once to an ini that has the other regions, stays out once taken out, "
              "and its map's name reads as UTF-8");
        Check(regionsWritten && byRegion && unknown && byMap && sameRegion && byId && takenOut,
              "maps: an ini without map sections gets the regions', a map takes its own section, else its region's, else its id's, held to the range, "
              "and a section taken out or changed by hand stays so");
        // a section's day and night: DayC and NightC as given (the two the right way round), the slider's move
        // shifting both, and a section with only them and no AmbientC
        WritePrivateProfileStringW(L"Map:Scandinavia", L"DayC", L"-12", ini.c_str());
        WritePrivateProfileStringW(L"Map:Scandinavia", L"NightC", L"3", ini.c_str());
        const bool dayNight = MapSectionAir("Scandinavia", air) && air.dayNight && air.day == 3.0f && air.night == -12.0f && air.mean == -4.5f;
        MapAirShift("Scandinavia", air, 2.0f);
        const bool shifted = MapSectionAir("Scandinavia", air) && air.day == 5.0f && air.night == -10.0f;
        WritePrivateProfileStringW(L"Map:Taymyr", L"AmbientC", nullptr, ini.c_str());
        WritePrivateProfileStringW(L"Map:Taymyr", L"DayC", L"14", ini.c_str());
        WritePrivateProfileStringW(L"Map:Taymyr", L"NightC", L"6", ini.c_str());
        const bool noMean = MapSectionAir("Taymyr", air) && air.dayNight && air.day == 14.0f && air.night == 6.0f && air.mean == 10.0f;
        MapSectionAir("Michigan", air);
        MapAirShift("Michigan", air, -4.0f);
        const bool meanShifted = MapSectionAir("Michigan", air) && air.mean == 8.0f && !air.dayNight;
        Check(dayNight && shifted && noMean && meanShifted, "maps: a section's day and night, shifted together by the slider; a section of only the mean moves its mean");
        // the curve over the game's day: the coldest at 5, the warmest at 15, half way at 10 and at 22, no step at midnight
        const bool curve = DayNightC(3.0f, -12.0f, kColdestHour) == -12.0f && fabsf(DayNightC(3.0f, -12.0f, kWarmestHour) - 3.0f) < 0.001f &&
                           fabsf(DayNightC(3.0f, -12.0f, 10.0f) + 4.5f) < 0.001f && fabsf(DayNightC(3.0f, -12.0f, 22.0f) + 4.5f) < 0.001f &&
                           fabsf(DayNightC(3.0f, -12.0f, 23.999f) - DayNightC(3.0f, -12.0f, 0.0f)) < 0.01f && DayNightC(3.0f, -12.0f, 2.0f) < DayNightC(3.0f, -12.0f, 0.0f);
        Check(curve, "maps: the air over the game's day, coldest before sunrise and warmest in the afternoon, without a step at midnight");
        DeleteFileW(ini.c_str());
        WritePrivateProfileStringW(kIniSection, L"Layout", L"1", ini.c_str());
        ReadIni(fresh);
        Check(fresh.layout == kLayoutBlock && MapSectionAmbient("Michigan", deg) && deg == 12.0f, "maps: an ini from an earlier version gets the map sections and keeps its settings");
        DeleteFileW(ini.c_str());
        WritePrivateProfileStringW(kIniSection, L"Layout", L"1", ini.c_str());
        WritePrivateProfileStringW(L"Map:michigan", L"AmbientC", L"7", ini.c_str());
        WritePrivateProfileStringW(L"Map:kola", L"AmbientC", L"-30", ini.c_str());
        WritePrivateProfileStringW(L"Map:british_columbia", L"AmbientC", L"4", ini.c_str());
        ReadIni(fresh);
        wchar_t kolaLeft[8] = {};
        GetPrivateProfileStringW(L"Map:kola", L"AmbientC", L"", kolaLeft, 8, ini.c_str());
        const bool renamedOk = MapSectionAmbient("Kola Peninsula", deg) && deg == -30.0f && MapSectionAmbient("British Columbia", deg) && deg == 4.0f && !kolaLeft[0];
        MapAir kept = {};
        const bool heldTemp = MapAmbient("level_us_01_01", kept, section, sizeof section) && kept.mean == 7.0f && !MapSectionAmbient("Taymyr", deg);
        const bool addedOk = MapSectionAmbient("Zurdania", deg) && deg == 22.0f && MapSectionAmbient("Chiapas", deg) && deg == 26.0f;
        ReadIni(fresh);
        const bool once = MapSectionAmbient("Kola Peninsula", deg) && deg == -30.0f && !MapSectionAmbient("Taymyr", deg) && fresh.layout == kLayoutBlock;
        Check(renamedOk && heldTemp && addedOk && once,
              "maps: the placeholder names of 0.11.0 are renamed to the game's with their temperatures, the regions they lacked are added, and nothing else");
    }

    // the tab's edits and the turn they were made from
    {
        GaugeSettings inForce, replaced, got;
        SettingsDefaults(inForce);
        replaced = inForce;
        replaced.opacity = 0.3f;
        SettingsPublish(inForce);
        const LONG turn = SettingsGet(got);
        GaugeSettings edit = got;
        edit.opacity = 0.6f;
        SettingsPut(edit, turn);
        const bool flagged = InterlockedExchange(&g_setDirty, 0) == 1;
        SettingsGet(got);
        const bool taken = flagged && got.opacity == 0.6f;
        // the mod puts other settings in force: an edit made from the ones before is no edit of these
        SettingsPublish(replaced);
        SettingsPut(edit, turn);
        const LONG turnNow = SettingsGet(got);
        Check(taken && turnNow != turn && got.opacity == 0.3f && !g_setDirty, "tab: an edit made from the settings in force is taken, one made from settings the mod has replaced since is dropped");
    }

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

    // the gearbox's bindings: the defaults without a key, a key written and read back, None clears, a text that is no
    // binding leaves the slot; Gearbox=2 takes the shifter only with a binding that reaches reverse
    {
        const std::wstring bindIni = g_dir + L"Gauges.binds_test.ini";
        DeleteFileW(bindIni.c_str());
        GearBindings b;
        BindingsDefault(b);
        const bool none = !BindingsInIni(bindIni);
        BindingsRead(bindIni, b, false);
        const bool defaults = b.slot[kActUp][0].kind == kBindKey && b.slot[kActUp][0].code == VK_NUMPAD8 && b.slot[kActDown][0].code == VK_NUMPAD2 &&
                              b.slot[kActGearN][0].code == VK_NUMPAD5 && b.slot[kActGearA][0].code == VK_NUMPAD7 && b.slot[kActGearR][0].code == VK_NUMPAD0 &&
                              !b.slot[kActClutch][0].kind && BindingsCanReverse(b, InputSnapshot{}, false);
        // the number pad's operators by name, and back from it
        char plus[16], dot[16];
        KeyName(VK_ADD, plus, sizeof plus);
        KeyName(VK_DECIMAL, dot, sizeof dot);
        Binding numPlus = {};
        char numWhy[64];
        const bool named = !strcmp(plus, "Num +") && !strcmp(dot, "Num .") && BindingParse("Key Num +", numPlus, numWhy, sizeof numWhy) &&
                           numPlus.kind == kBindKey && numPlus.code == VK_ADD && KeyFromText("Num /") == VK_DIVIDE;
        b.slot[kActClutch][1] = Binding{ kBindAxis, 1, 3, 0, true };
        b.slot[kActGear3][0] = Binding{ kBindButton, 2, 5, 0, false };
        b.slot[kActUp][0] = Binding{};
        BindingsWrite(bindIni, b);
        GearBindings back;
        BindingsDefault(back);
        BindingsRead(bindIni, back, false);
        const bool round = BindingsInIni(bindIni) && back.slot[kActClutch][1].kind == kBindAxis && back.slot[kActClutch][1].device == 1 &&
                           back.slot[kActClutch][1].code == 3 && back.slot[kActClutch][1].inverted && back.slot[kActGear3][0].kind == kBindButton &&
                           back.slot[kActGear3][0].code == 5 && !back.slot[kActUp][0].kind;
        WritePrivateProfileStringW(kBindSection, L"BindGearH", L"Wheel nine", bindIni.c_str());
        back.slot[kActGearH][0] = Binding{ kBindKey, 0, 'H', 0, false };
        BindingsRead(bindIni, back, false);
        const bool kept = back.slot[kActGearH][0].kind == kBindKey && back.slot[kActGearH][0].code == 'H';
        GearBindings bare = {};
        const bool fallback = !BindingsCanReverse(bare, InputSnapshot{}, true);
        bare.slot[kActGearR][1] = Binding{ kBindKey, 0, 'R', 0, false };
        DeleteFileW(bindIni.c_str());
        Check(none && defaults && named && round && kept && fallback && BindingsCanReverse(bare, InputSnapshot{}, false),
              "gearbox: bindings default to Num 8, Num 2, Num 5, Num 0 and Num 7, the number pad's operators by name, round trip through the ini, None "
              "clears, no binding leaves a slot; the shifter is taken only with one that reaches reverse");
    }
    // review 8: reverse reached only by what can be pressed now (W5), what a capture may not bind (W1, W9), the actions
    // from the reader's states, the actions resting while the menu is open (W2), the snapshot out of front (W7)
    {
        GearBindings b = {};
        b.slot[kActGearR][0] = Binding{ kBindButton, 2, 5, 0, false };
        b.slot[kActDown][1] = Binding{ kBindPad, 0, XINPUT_GAMEPAD_DPAD_DOWN, 0, false };
        InputSnapshot there = {};
        there.device[1].present = true;
        const bool wheel = !BindingsCanReverse(b, InputSnapshot{}, false) && BindingsCanReverse(b, there, false);
        b.slot[kActGearR][0] = Binding{};
        const bool pad = !BindingsCanReverse(b, there, false) && BindingsCanReverse(b, InputSnapshot{}, true);
        Check(wheel && pad, "gearbox: reverse counts a wheel's binding only while that wheel is there, a pad's only while a pad answers");

        const uint32_t chord = XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_DPAD_UP;
        const Binding f2 = { kBindKey, 0, VK_F2, 0, false }, f8 = { kBindKey, 0, VK_F8, 0, false }, num8 = { kBindKey, 0, VK_NUMPAD8, 0, false };
        const Binding rb = { kBindPad, 0, XINPUT_GAMEPAD_RIGHT_SHOULDER, 0, false }, rbUpA = { kBindPad, 0, chord | XINPUT_GAMEPAD_A, 0, false };
        const Binding padA = { kBindPad, 0, XINPUT_GAMEPAD_A, 0, false };
        Check(BindingClash(f2, VK_F2, VK_F8, chord) && BindingClash(f8, VK_F2, VK_F8, chord) && BindingClash(rb, VK_F2, VK_F8, chord) &&
                  BindingClash(rbUpA, VK_F2, VK_F8, chord) && !BindingClash(num8, VK_F2, VK_F8, chord) && !BindingClash(padA, VK_F2, VK_F8, chord) &&
                  !BindingClash(f8, VK_F2, 0, 0),
              "gearbox: a capture never binds the menu's key, the toggle key or pad buttons in or holding the PadToggle chord");

        ActionState rs[kActCount] = {};
        rs[kActUp].pressed = rs[kActUp].held = true;
        rs[kActGear3].held = true;
        rs[kActGear5].pressed = rs[kActGear5].held = true;
        rs[kActGearR].pressed = true;
        rs[kActClutch].held = true;
        rs[kActClutch].travel = 0.7f;
        GearBindings keys = {};
        keys.slot[kActClutch][0] = Binding{ kBindKey, 0, VK_SPACE, 0, false };
        const GearboxActions withKey = ActionsOf(rs, keys);
        GearBindings axis = {};
        axis.slot[kActClutch][1] = Binding{ kBindAxis, 1, 3, 0, false };
        const GearboxActions x = ActionsOf(rs, axis);
        Check(withKey.up && !withKey.down && withKey.gearPressed == 5 && withKey.gearHeld == 3 && withKey.r && !withKey.n && withKey.clutch && withKey.clutchTravel < 0.0f && x.clutchTravel == 0.7f,
              "gearbox: the actions from the reader's states: the edges, the gear held, the clutch's travel only from an axis");

        const GearboxActions r = ActionsResting(x);
        Check(!r.up && !r.r && !r.gearPressed && r.gearHeld == 3 && r.clutch && r.clutchTravel == 0.7f,
              "gearbox: while the menu is open or a capture runs, nothing goes down and what was held stands");

        InputSnapshot back = {};
        back.front = false;
        back.pad = XINPUT_GAMEPAD_A;
        back.device[0].present = true;
        back.device[0].button[0] = 1;
        const InputSnapshot behind = InputFrontOnly(back);
        back.front = true;
        const InputSnapshot kept = InputFrontOnly(back);
        Check(!behind.pad && !behind.device[0].present && kept.pad == XINPUT_GAMEPAD_A && kept.device[0].button[0],
              "gearbox: the pad and the wheels are read as nothing while the game is not in front");
    }
    // the SMT.ini import: a Kb. scan code turned into its key, a combination and an unknown code left, the options taken
    {
        const std::wstring smtIni = g_dir + L"SMT.test.ini";
        DeleteFileW(smtIni.c_str());
        WritePrivateProfileStringW(L"KEYBOARD", L"GEAR UP", L"Kb.18", smtIni.c_str());        // E
        WritePrivateProfileStringW(L"KEYBOARD", L"GEAR R", L"Kb.207", smtIni.c_str());        // End, an extended key
        WritePrivateProfileStringW(L"KEYBOARD", L"GEAR 1", L"Kb.42+Kb.2", smtIni.c_str());    // a combination: left
        WritePrivateProfileStringW(L"KEYBOARD", L"GEAR 3", L"Kb.80", smtIni.c_str());         // Num 2, the default of GearDown, which goes
        WritePrivateProfileStringW(L"KEYBOARD", L"GEAR 4", L"Kb.83", smtIni.c_str());         // Num .
        WritePrivateProfileStringW(L"KEYBOARD", L"SHOW MENU", L"Kb.211", smtIni.c_str());     // not an action of the mod's
        WritePrivateProfileStringW(L"KEYBOARD", L"DIFF LOCK", L"Kb.38", smtIni.c_str());      // L
        WritePrivateProfileStringW(L"KEYBOARD", L"AWD", L"Kb.30", smtIni.c_str());            // A
        WritePrivateProfileStringW(L"CONTROLLER", L"GEAR DOWN", L"Joy.0.Btn.5", smtIni.c_str());
        WritePrivateProfileStringW(L"OPTIONS", L"REQUIRE CLUTCH", L"true", smtIni.c_str());
        WritePrivateProfileStringW(L"OPTIONS", L"SKIP NEUTRAL", L"1", smtIni.c_str());
        GearBindings b;
        BindingsDefault(b);
        GaugeSettings o;
        SettingsDefaults(o);
        std::wstring taken, left;
        const int bound = SmtIniImport(smtIni, b, o, taken, left);
        DeleteFileW(smtIni.c_str());
        Check(bound == 6 && b.slot[kActUp][0].code == 'E' && b.slot[kActGearR][0].code == VK_END && b.slot[kActGear3][0].code == VK_NUMPAD2 &&
                  b.slot[kActGear4][0].code == VK_DECIMAL && !b.slot[kActDown][0].kind && b.slot[kActGearN][0].code == VK_NUMPAD5 &&
                  b.slot[kActDiffLock][0].code == 'L' && b.slot[kActAwd][0].code == 'A' && wcsstr(taken.c_str(), L"DIFF LOCK") &&
                  !b.slot[kActGear1][0].kind && o.requireClutch && o.skipNeutral && !o.requireGearHeld && wcsstr(left.c_str(), L"GEAR 1") &&
                  wcsstr(left.c_str(), L"the default of GearDown") && wcsstr(taken.c_str(), L"GEAR UP"),
              "gearbox: SMT.ini's keyboard bindings taken over by their scan codes (an extended key and the number pad too), a default on a key "
              "it took dropped, a combination left, its controller section and menu key left, its diff lock and AWD keys taken, its options taken");
    }
    printf(g_failed ? "SETTINGS TEST FAILED (%d)\n" : "SETTINGS TEST OK\n", g_failed);
    return g_failed ? 1 : 0;
}
#endif
