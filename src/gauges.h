// gauges.h: what the parts of the mod share: the state the cluster shows, the settings of Gauges.ini, and the
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
#define GAUGES_VERSION "0.13.4"

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
    int gear;                       // below 0 reverse, 0 neutral, 1.. forward
    const char *gearMode;           // "A" automatic (drawn beside the gear's number); "H" high, "L", "L+", "L-" (drawn in the number's place);
                                    // "" a numbered gear. String literals only: the pointer crosses threads
    float fuelFrac;                 // 0..1
    float rangeKm;                  // < 0 = not known yet
    float tempC;                    // coolant temperature (the mod's own model)
    float oilC;                     // oil temperature (the mod's own model)
    float oilWarnC;                 // where the oil dial's red zone starts: the settings' OilWarnC
    float boostBar;                 // boost pressure in bar, 0..2 (the mod's own model)
    float airBar;                   // brake air in bar (0..10), from the model (AirModel)
    bool handbrake, diffLock, awd, lowGear, headlights, damage, cold;
    bool oilHot;                    // the oil lamp is lit (it blinks while the oil is too hot: the caller switches it on and off)
    int tilt;                       // 0 level, 1 warning, 2 danger, below 0 not known (the lamp stays dark)
    float rollDeg;                  // how far the truck leans sideways, above 0 to its right (the tilt lamp's truck leans with it)
    bool mph, psi, fahrenheit;      // units
};

enum { kLayoutStrip, kLayoutBlock, kLayoutCount };           // GaugeSettings::layout
enum { kFaceSolid, kFaceTinted, kFaceOpen, kFaceCount };     // GaugeSettings::style
enum { kClassTruck, kClassScout, kClassHeavy, kClassCount }; // GaugeSettings::tachClass
enum { kOilOff, kOilNeedle, kOilDial, kOilGaugeCount };      // GaugeSettings::oilGauge
enum { kSpoolStandard, kSpoolQuick, kSpoolLaggy, kSpoolTwoStage, kSpoolCount }; // GaugeSettings::boostProfile
enum { kGearboxGame, kGearboxModel, kGearboxCount };          // GaugeSettings::gearbox

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
    int oilGauge;            // OilGauge: the oil temperature: 0 not shown, 1 a second needle in the temperature dial, 2 a dial of its own left of it
    int tachClass;           // TachClass: 0 truck, 1 scout, 2 heavy: the scales of the tachometer and the speedometer
    float idleRpm, redlineRpm, maxRpm, speedMax;  // IdleRpm, RedlineRpm, MaxRpm, SpeedMax: 0 = the class's own
    float boostMax;          // BoostMax: the boost in bar the turbo reaches with the pedal floored at high engine speed; 0 = the class's own
    int boostProfile;        // BoostProfile: how the turbo builds its boost: 0 standard, 1 small and quick, 2 big and slow, 3 two stages
    int gearbox;             // Gearbox: 0 the game's own automatic, 1 the mod's: a torque converter and its own shift points pick the gears in A
    bool powertrain;         // Powertrain: the engine's torque (its curve, the cold oil, the boost) and the converter's handed to the game
    int engineShare;         // EngineShare: how much of the mod's engine the game gets with it, in percent: 0 the game's own pull, 100 the mod's
    float rollWarnDeg, rollDangerDeg; // RollWarnDeg, RollDangerDeg: degrees of lean to the side from which the tilt lamp is amber, and red
    float pitchDangerDeg;    // PitchDangerDeg: degrees up or down a slope from which the tilt lamp is red
    float warmUpMinutes;     // WarmUpMinutes: minutes a cold engine takes from 15 C to 85 C when it is driven as usual
    float overheatMinutes;   // OverheatMinutes: minutes from 90 C into the red zone at 118 C with the engine revved out and the wheels spinning at a standstill
    float coolDownMinutes;   // CoolDownMinutes: minutes a stopped engine takes from 90 C down to 40 C
    float ambientC;          // AmbientC: the outside temperature in degrees C; an engine that has stood for long is this cold
    bool ambientByMap;       // AmbientByMap: where the map is known, its section's or its region's temperature ([Map:<name>]) in place of AmbientC
    float airFullKmh;        // AirFullKmh: the speed in km/h from which the radiator gets all the air that driving gives it
    float oilWarmUpMinutes;  // OilWarmUpMinutes: minutes a cold engine's oil takes from 15 C to 85 C when it is driven as usual (at least a minute more than the coolant's)
    float oilWarnC;          // OilWarnC: the oil temperature in C from which the oil lamp blinks and the warning sounds
    bool damage;             // Damage: an engine in the red zone and a cold engine worked hard take the game's engine damage
    float hotDamage;         // HotDamagePercentPerMinute: percent of what the engine takes that it loses a minute at 125 C
    float coldDamage;        // ColdDamagePercentPerMinute: the same for a cold engine floored at the red zone at 0 C
    int soundVolume;         // SoundVolume: how loud the turbo's flutter and the oil warning are, 0 to 100; 0 = no sound
    bool soundsOnly;         // SoundsOnly: the sounds are played and the cluster is not drawn
    int toggleKey;           // ToggleKey: virtual key code of the key that hides and shows the cluster, 0 = none
    char padToggle[32];      // PadToggle: pad buttons that do the same ("LB+DPadUp"), "None"
    bool demo;               // Demo: for work on the mod: demo values are drawn whenever no truck is driven, menus and loading screens included
    int probe;               // Probe: for work on the mod: the values read from the game go into Gauges.log (0 = off)
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
    s.oilGauge = kOilNeedle;
    s.tachClass = kClassTruck;
    s.boostProfile = kSpoolStandard;
    s.gearbox = kGearboxGame;
    s.powertrain = false;
    s.engineShare = 50;
    s.rollWarnDeg = 15.0f;
    s.rollDangerDeg = 25.0f;
    s.pitchDangerDeg = 35.0f;
    s.warmUpMinutes = 3.0f;
    s.overheatMinutes = 1.5f;
    s.coolDownMinutes = 15.0f;
    s.ambientC = 15.0f;
    s.ambientByMap = true;
    s.airFullKmh = 30.0f;
    s.oilWarmUpMinutes = 5.0f;
    s.oilWarnC = 120.0f;
    s.damage = true;
    s.hotDamage = 10.0f;
    s.coldDamage = 5.0f;
    s.soundVolume = 50;
    strcpy_s(s.padToggle, "None");
    s.demo = false;
    s.probe = 0;
}

// The range of each number: what the ini and the tab hold the settings to. A tinted face stops at 0.85, as a darker
// one is the solid style. The tilt lamp's angles are degrees, the coolant's three times and the oil's warm-up minutes,
// the outside temperature and the oil's warning degrees C, the speed of the radiator's full airflow km/h, the two
// damage rates percent a minute, the boost bar (the dial ends at 2).
static const float kFaceOpacityMax = 0.85f, kScaleMin = 0.4f, kScaleMax = 3.0f, kOpacityMin = 0.1f, kOffsetMax = 4000.0f;
static const float kIdleRange[2] = { 300.0f, 3000.0f }, kRedlineRange[2] = { 1000.0f, 12000.0f }, kMaxRpmRange[2] = { 1500.0f, 15000.0f },
                   kSpeedMaxRange[2] = { 40.0f, 300.0f }, kTiltRange[2] = { 1.0f, 89.0f }, kBoostRange[2] = { 0.3f, 2.0f };
static const float kWarmUpRange[2] = { 0.5f, 60.0f }, kOverheatRange[2] = { 0.25f, 60.0f }, kCoolDownRange[2] = { 1.0f, 240.0f },
                   kAmbientRange[2] = { -40.0f, 50.0f }, kAirFullRange[2] = { 10.0f, 80.0f }, kDamageRange[2] = { 0.0f, 100.0f },
                   kOilWarmUpRange[2] = { 1.0f, 60.0f }, kOilWarnRange[2] = { 100.0f, 150.0f };

// The temperatures in C the coolant's three times are measured between: WarmUpMinutes from kTempOutside outside up
// to kTempWarm, OverheatMinutes from kTempHot into the red zone at kTempRed, CoolDownMinutes from kTempHot down to
// kTempCooled. The cold lamp is lit below kTempCold. HotDamagePercentPerMinute is what an engine at kTempHotDamage
// loses. The model, the dial's zones and the tab's texts all use these.
static const float kTempOutside = 15.0f, kTempWarm = 85.0f, kTempHot = 90.0f, kTempRed = 118.0f, kTempCooled = 40.0f, kTempCold = 60.0f,
                   kTempHotDamage = 125.0f;

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
    s.boostMax = SettingsOverride(s.boostMax, kBoostRange);
    s.boostProfile = s.boostProfile < 0 || s.boostProfile >= kSpoolCount ? kSpoolStandard : s.boostProfile;
    s.engineShare = s.engineShare < 0 ? 0 : s.engineShare > 100 ? 100 : s.engineShare;
    s.gearbox = s.gearbox < 0 || s.gearbox >= kGearboxCount ? kGearboxGame : s.gearbox;
    s.oilGauge = s.oilGauge < 0 || s.oilGauge >= kOilGaugeCount ? kOilNeedle : s.oilGauge;
    s.rollWarnDeg = SettingsRange(s.rollWarnDeg, kTiltRange[0], kTiltRange[1]);
    s.rollDangerDeg = SettingsRange(s.rollDangerDeg, s.rollWarnDeg, kTiltRange[1]); // red never comes before amber
    s.pitchDangerDeg = SettingsRange(s.pitchDangerDeg, kTiltRange[0], kTiltRange[1]);
    s.warmUpMinutes = SettingsRange(s.warmUpMinutes, kWarmUpRange[0], kWarmUpRange[1]);
    s.overheatMinutes = SettingsRange(s.overheatMinutes, kOverheatRange[0], kOverheatRange[1]);
    s.coolDownMinutes = SettingsRange(s.coolDownMinutes, kCoolDownRange[0], kCoolDownRange[1]);
    s.ambientC = SettingsRange(s.ambientC, kAmbientRange[0], kAmbientRange[1]);
    s.airFullKmh = SettingsRange(s.airFullKmh, kAirFullRange[0], kAirFullRange[1]);
    s.oilWarmUpMinutes = SettingsRange(s.oilWarmUpMinutes, kOilWarmUpRange[0], kOilWarmUpRange[1]);
    s.oilWarnC = SettingsRange(s.oilWarnC, kOilWarnRange[0], kOilWarnRange[1]);
    s.hotDamage = SettingsRange(s.hotDamage, kDamageRange[0], kDamageRange[1]);
    s.coldDamage = SettingsRange(s.coldDamage, kDamageRange[0], kDamageRange[1]);
    s.toggleKey = s.toggleKey < 0 || s.toggleKey > 255 ? 0 : s.toggleKey;
    s.soundVolume = s.soundVolume < 0 ? 0 : s.soundVolume > 100 ? 100 : s.soundVolume;
    s.probe = s.probe < 0 ? 0 : s.probe;
}

// The scales of a truck class: idle, where the red zone starts, the tachometer's top (rpm), the speedometer's top
// (km/h) and the boost its turbo reaches (bar).
struct GaugeScale { float idle, redline, max, speedMax, boost; };
static const GaugeScale kClassScales[kClassCount] = { { 650.0f, 2200.0f, 3000.0f, 100.0f, 1.8f }, { 800.0f, 5500.0f, 7000.0f, 120.0f, 1.0f }, { 600.0f, 1900.0f, 2500.0f, 80.0f, 2.0f } };

// The scales in force: the class's, with the settings' own numbers in their place where given, made to fit each other
// (the tachometer's top a whole step of its numbers, the red zone inside it, idle well below the red zone).
inline GaugeScale GaugeScaleOf(const GaugeSettings &s)
{
    GaugeScale k = kClassScales[s.tachClass < 0 || s.tachClass >= kClassCount ? kClassTruck : s.tachClass];
    if (s.idleRpm > 0.0f) k.idle = s.idleRpm;
    if (s.redlineRpm > 0.0f) k.redline = s.redlineRpm;
    if (s.maxRpm > 0.0f) k.max = s.maxRpm;
    if (s.speedMax > 0.0f) k.speedMax = s.speedMax;
    if (s.boostMax > 0.0f) k.boost = s.boostMax;
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
    st.oilWarnC = s.oilWarnC;
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
    st.oilC = 97.0f;
    st.boostBar = 1.15f;
    st.airBar = 8.2f;
    st.awd = st.diffLock = st.headlights = true;
    st.rollDeg = 5.0f;
}

// What the render thread draws: show = the cluster is on screen, demo = st holds the demo values, hidden = why it is
// not on screen. engineDamage of engineCapacity is the driven truck's engine damage as the game holds it, for the
// tab; engineCapacity is 0 while it is not known.
enum { kShown, kHiddenByToggle, kHiddenNoTruck, kHiddenSoundsOnly };
// How strong the turbo's flutter is after a lift of the pedal, by the boost the lift had: the sounds come in these three.
// After them the oil warning, the mod's one other sound.
enum { kTurboHigh, kTurboMid, kTurboLow, kTurboTiers };
enum { kSoundOil = kTurboTiers, kSoundTierCount };
// truck: the kind of truck being driven, as the game's files name it ("" while none is driven): the scales in force
// are that kind's. truckOwn: it has scales of its own in Gauges.ini, and those are the ones in force. map: the map
// the truck drives on in words, with its region ("" while it is not known), and ambientC the outside temperature in
// force: its section's (mapOwn, from the section [Map:<mapFrom>], the map's or its region's) or AmbientC's.
// pullTest: kViewPullOff to kViewPullStopped, the pull test's state (PullTest in models.h); pullWhy: why it stopped, a
// string that lives as long as the mod
enum { kViewPullOff, kViewPullArmed, kViewPullRunning, kViewPullDone, kViewPullStopped };
struct GaugeView { bool show, demo; int hidden; GaugeState st; int engineDamage, engineCapacity; char truck[96]; bool truckOwn; char map[96], mapFrom[64]; bool mapOwn; float ambientC;
                   int pullTest, pullPhase; const char *pullWhy;
                   float ambientDay, ambientNight, hour; }; // the map's day and night (with mapOwn), the hour of the game's day (-1 not known)

LONG SettingsGet(GaugeSettings &out);       // the settings in force (any thread); returns their turn, for SettingsPut
void SettingsPut(const GaugeSettings &in, LONG turn); // settings edited from those of a turn: the mod takes them at once and saves the ini a
                                            // second later. They are dropped when the mod has put others in force since that turn
void SettingsReload();                      // read the ini again (the tab's button)
void SettingsTruckDrop();                   // the tab's button: the driven truck gives up its own scales and takes the plain ones
void SettingsReset();                       // the tab's button: every setting back to its default, the driven truck's own scales too
void PullTestAsk(bool run);                  // the tab's button: arm the pull test, or cancel it
void MapAmbientSet(float c);                 // the tab's slider: the outside temperature of the map's section in force
void DamageTestAsk(int points);             // the tab's test buttons: the mod adds this much to the driven truck's engine damage on its next pass
void ViewGet(GaugeView &out);               // what to draw (any thread)
void SoundHear(int tier);                   // plays the turbo's sound of that strength (kTurboHigh ..) or the oil warning (kSoundOil) as loud as the settings have it (the tab's buttons)
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
