// gauges.h: what the parts of Gauges.asi share: the state the cluster shows, the settings of Gauges.ini, and the
// calls between the mod's thread (gauges.cpp, which owns the settings and the state) and ReShade's render thread
// (gauge_overlay.cpp, which draws the cluster and the settings tab). No Dear ImGui in here: gauges.cpp is built
// without it.
#pragma once
#include <windows.h>
#include <xinput.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define GAUGES_VERSION "0.1.0"

// The exe the addresses in this mod are from (SnowRunner on Steam, the build of 22 July 2026). On any other the mod
// stands down.
static const DWORD kGaugesExeStamp = 0x6a607c05;

// Everything the cluster shows, as the mod's thread leaves it for the render thread.
struct GaugeState
{
    bool engineOn;
    float speedKmh;                 // |forward speed| in km/h
    float speedMax;                 // the speedometer's top in km/h (trucks 100, scouts 120, heavy 80)
    float rpm;                      // engine speed (the mod's own model)
    float rpmRedline, rpmMax;       // the tachometer's scale: red zone from rpmRedline to rpmMax
    int gear;                       // -1 reverse, 0 neutral, 1.. forward
    const char *gearMode;           // "A" automatic, "H" high, "L", "L+", "L-", "" (a numbered gear). String literals only: the pointer crosses threads
    float fuelFrac;                 // 0..1
    float rangeKm;                  // < 0 = not known yet
    float tempC;                    // coolant temperature (the mod's own model)
    float boostBar;                 // 0..2 (the mod's own model)
    float airBar;                   // brake air, cosmetic (0..10)
    bool handbrake, diffLock, awd, lowGear, headlights, damage, cold;
    int tilt;                       // 0 level, 1 warning, 2 danger
    float rollDeg;                  // the truck's roll, signed (the tilt lamp leans with it)
    bool mph, psi, fahrenheit;      // units
};

enum { kLayoutStrip, kLayoutBlock, kLayoutCount };           // GaugeSettings::layout
enum { kFaceSolid, kFaceTinted, kFaceOpen, kFaceCount };     // GaugeSettings::style
enum { kClassTruck, kClassScout, kClassHeavy, kClassCount }; // GaugeSettings::tachClass

// Everything in Gauges.ini ([Gauges]); the Gauges tab in ReShade's overlay edits all of it.
struct GaugeSettings
{
    int layout;              // Layout: 0 a strip along the bottom centre, 1 a block in the bottom right corner
    int style;               // Style: 0 solid dial faces, 1 tinted (see-through) faces, 2 open (no faces, the scale arcs only)
    float faceOpacity;       // FaceOpacity: how dark the tinted faces are, 0 = clear
    float offsetX, offsetY;  // OffsetX, OffsetY: moves the cluster from its place, in pixels of a 1080p screen (right and down)
    float scale;             // Scale: the cluster's size (1 = its size on a 1080p screen; it grows with the screen's height)
    float opacity;           // Opacity of the whole cluster
    bool mph, psi, fahrenheit;                    // Mph, Psi, Fahrenheit
    bool showTemp, showBoost, showAir, showLamps; // ShowTemp, ShowBoost, ShowAir, ShowLamps
    int tachClass;           // TachClass: 0 truck, 1 scout, 2 heavy: the scales of the tachometer and the speedometer
    float idleRpm, redlineRpm, maxRpm, speedMax;  // IdleRpm, RedlineRpm, MaxRpm, SpeedMax: 0 = the class's own
    int toggleKey;           // ToggleKey: virtual key code of the key that hides and shows the cluster, 0 = none
    char padToggle[32];      // PadToggle: pad buttons that do the same ("LB+DPadUp"), "None"
    bool demo;               // Demo: demo values are drawn while no truck is driven
    int probe;               // Probe: the values read from the game go into Gauges.log (0 = off)
};

inline void SettingsDefaults(GaugeSettings &s)
{
    s = GaugeSettings{};
    s.layout = kLayoutStrip;
    s.style = kFaceOpen;
    s.faceOpacity = 0.35f;
    s.scale = 1.0f;
    s.opacity = 1.0f;
    s.showTemp = s.showBoost = s.showLamps = true;
    s.showAir = false;
    s.tachClass = kClassTruck;
    strcpy_s(s.padToggle, "None");
    s.demo = true; // nothing is read from the game yet: the demo values are all there is to draw
    s.probe = 0;
}

// The range of each number: what the ini and the tab hold the settings to. A tinted face stops at 0.85, as a darker
// one is the solid style.
static const float kFaceOpacityMax = 0.85f, kScaleMin = 0.4f, kScaleMax = 3.0f, kOpacityMin = 0.1f, kOffsetMax = 4000.0f;
static const float kIdleRange[2] = { 300.0f, 3000.0f }, kRedlineRange[2] = { 1000.0f, 12000.0f }, kMaxRpmRange[2] = { 1500.0f, 15000.0f },
                   kSpeedMaxRange[2] = { 40.0f, 300.0f };

inline float SettingsRange(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
// 0 (the class's own) stays 0; anything else is held to the range.
inline float SettingsOverride(float v, const float range[2]) { return v <= 0.0f ? 0.0f : SettingsRange(v, range[0], range[1]); }

inline void SettingsClamp(GaugeSettings &s)
{
    s.layout = s.layout < 0 || s.layout >= kLayoutCount ? kLayoutStrip : s.layout;
    s.style = s.style < 0 || s.style >= kFaceCount ? kFaceOpen : s.style;
    s.faceOpacity = SettingsRange(s.faceOpacity, 0.0f, kFaceOpacityMax);
    s.offsetX = SettingsRange(s.offsetX, -kOffsetMax, kOffsetMax);
    s.offsetY = SettingsRange(s.offsetY, -kOffsetMax, kOffsetMax);
    s.scale = SettingsRange(s.scale, kScaleMin, kScaleMax);
    s.opacity = SettingsRange(s.opacity, kOpacityMin, 1.0f);
    s.tachClass = s.tachClass < 0 || s.tachClass >= kClassCount ? kClassTruck : s.tachClass;
    s.idleRpm = SettingsOverride(s.idleRpm, kIdleRange);
    s.redlineRpm = SettingsOverride(s.redlineRpm, kRedlineRange);
    s.maxRpm = SettingsOverride(s.maxRpm, kMaxRpmRange);
    s.speedMax = SettingsOverride(s.speedMax, kSpeedMaxRange);
    s.toggleKey = s.toggleKey < 0 || s.toggleKey > 255 ? 0 : s.toggleKey;
    s.probe = s.probe < 0 ? 0 : s.probe;
}

// The scales of a truck class: idle, where the red zone starts, the tachometer's top (rpm) and the speedometer's top
// (km/h).
struct GaugeScale { float idle, redline, max, speedMax; };
static const GaugeScale kClassScales[kClassCount] = { { 650.0f, 2200.0f, 3000.0f, 100.0f }, { 800.0f, 5500.0f, 7000.0f, 120.0f }, { 600.0f, 1900.0f, 2500.0f, 80.0f } };

// The scales in force: the class's, with the settings' own numbers in their place where given, made to fit each other
// (the tachometer's top a whole step of its numbers, the red zone inside it, idle well below the red zone).
inline GaugeScale GaugeScaleOf(const GaugeSettings &s)
{
    GaugeScale k = kClassScales[s.tachClass < 0 || s.tachClass >= kClassCount ? kClassTruck : s.tachClass];
    if (s.idleRpm > 0.0f) k.idle = s.idleRpm;
    if (s.redlineRpm > 0.0f) k.redline = s.redlineRpm;
    if (s.maxRpm > 0.0f) k.max = s.maxRpm;
    if (s.speedMax > 0.0f) k.speedMax = s.speedMax;
    const float step = k.max >= 5000.0f ? 1000.0f : 500.0f;
    k.max = ceilf(k.max / step - 0.001f) * step;
    if (k.redline > k.max) k.redline = k.max;
    if (k.idle > k.redline * 0.8f) k.idle = k.redline * 0.8f;
    return k;
}

// The parts of a state that come from the settings: the scales and the units.
inline void GaugeStateSettings(GaugeState &st, const GaugeSettings &s)
{
    const GaugeScale k = GaugeScaleOf(s);
    st.speedMax = k.speedMax;
    st.rpmRedline = k.redline;
    st.rpmMax = k.max;
    st.mph = s.mph;
    st.psi = s.psi;
    st.fahrenheit = s.fahrenheit;
}

// The demo values: a truck cruising in fourth gear, warm, the lights on.
inline void GaugeDemo(GaugeState &st, const GaugeSettings &s)
{
    const GaugeScale k = GaugeScaleOf(s);
    st = GaugeState{};
    GaugeStateSettings(st, s);
    st.engineOn = true;
    st.speedKmh = 46.0f;
    st.rpm = k.idle + (k.redline - k.idle) * 0.77f;
    st.gear = 4;
    st.gearMode = "A";
    st.fuelFrac = 0.62f;
    st.rangeKm = 412.0f;
    st.tempC = 88.0f;
    st.boostBar = 1.15f;
    st.airBar = 8.2f;
    st.awd = st.diffLock = st.headlights = true;
    st.rollDeg = 5.0f;
}

// What the render thread draws: show = the cluster is on screen, demo = st holds the demo values.
struct GaugeView { bool show, demo; GaugeState st; };

void SettingsGet(GaugeSettings &out);       // the settings in force (any thread)
void SettingsPut(const GaugeSettings &in);  // edited settings: the mod takes them at once and saves the ini a second later
void SettingsReload();                      // read the ini again (the tab's button)
void ViewGet(GaugeView &out);               // what to draw (any thread)
void GaugeLog(const wchar_t *fmt, ...);     // a line in Gauges.log (any thread)
bool OverlayInit(HMODULE self);             // registers with ReShade (the cluster and the Gauges tab); false = no ReShade
uint32_t PadButtons();                      // the buttons of all connected pads together, 0 without a pad

extern volatile LONG g_pageCapturing; // GetTickCount() of the last frame the tab waited for pad buttons, 0 = none: the mod ignores the pad then
extern volatile LONG g_modOff;        // 1 = the mod stood down (another version of the game): the tab says so

// Pad buttons by name, as the SnowRunner Tire Inflation System writes them: "LB+DPadDown", "None". The pad is only
// read here, never hidden from the game.
static const struct { const char *name; uint32_t mask; } kPadButtons[14] = {
    { "DPadUp", XINPUT_GAMEPAD_DPAD_UP }, { "DPadDown", XINPUT_GAMEPAD_DPAD_DOWN }, { "DPadLeft", XINPUT_GAMEPAD_DPAD_LEFT },
    { "DPadRight", XINPUT_GAMEPAD_DPAD_RIGHT }, { "Start", XINPUT_GAMEPAD_START }, { "Back", XINPUT_GAMEPAD_BACK },
    { "LS", XINPUT_GAMEPAD_LEFT_THUMB }, { "RS", XINPUT_GAMEPAD_RIGHT_THUMB }, { "LB", XINPUT_GAMEPAD_LEFT_SHOULDER },
    { "RB", XINPUT_GAMEPAD_RIGHT_SHOULDER }, { "A", XINPUT_GAMEPAD_A }, { "B", XINPUT_GAMEPAD_B }, { "X", XINPUT_GAMEPAD_X },
    { "Y", XINPUT_GAMEPAD_Y } };

// "LB+DPadDown" -> XINPUT_GAMEPAD_* mask; 0 for "None" or a name that is not a button
inline uint32_t PadParse(const char *text)
{
    char buf[64];
    strncpy_s(buf, text, _TRUNCATE);
    uint32_t mask = 0;
    char *ctx = nullptr;
    for (char *tok = strtok_s(buf, "+ ", &ctx); tok; tok = strtok_s(nullptr, "+ ", &ctx))
    {
        uint32_t m = 0;
        for (const auto &b : kPadButtons) if (!_stricmp(tok, b.name)) m = b.mask;
        if (!m) return 0;
        mask |= m;
    }
    return mask;
}

// mask -> "LB+DPadDown" (shoulders and sticks first), "None" for 0
inline void PadName(uint32_t mask, char *out, size_t size)
{
    static const int order[14] = { 8, 9, 6, 7, 4, 5, 0, 1, 2, 3, 10, 11, 12, 13 };
    out[0] = 0;
    for (int i : order)
        if ((mask & kPadButtons[i].mask) && strlen(out) + strlen(kPadButtons[i].name) + 2 <= size) // a name that does not fit is left out
        {
            if (out[0]) strcat_s(out, size, "+");
            strcat_s(out, size, kPadButtons[i].name);
        }
    if (!out[0]) strcpy_s(out, size, "None");
}

// A virtual key's name as the tab shows it; "None" for 0.
inline void KeyName(int vk, char *out, size_t size)
{
    static const struct { int vk; const char *name; } names[] = {
        { VK_INSERT, "Insert" }, { VK_DELETE, "Delete" }, { VK_HOME, "Home" }, { VK_END, "End" }, { VK_PRIOR, "Page Up" },
        { VK_NEXT, "Page Down" }, { VK_UP, "Up" }, { VK_DOWN, "Down" }, { VK_LEFT, "Left" }, { VK_RIGHT, "Right" },
        { VK_PAUSE, "Pause" }, { VK_SCROLL, "Scroll Lock" }, { VK_TAB, "Tab" }, { VK_SPACE, "Space" }, { VK_BACK, "Backspace" },
        { VK_RETURN, "Enter" } };
    if (vk <= 0) strcpy_s(out, size, "None");
    else if (vk >= VK_F1 && vk <= VK_F24) sprintf_s(out, size, "F%d", vk - VK_F1 + 1);
    else if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) sprintf_s(out, size, "%c", (char)vk);
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) sprintf_s(out, size, "Num %d", vk - VK_NUMPAD0);
    else
    {
        sprintf_s(out, size, "Key %d", vk);
        for (const auto &n : names) if (n.vk == vk) strcpy_s(out, size, n.name);
    }
}
