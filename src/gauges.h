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
#include "drive_display.h" // DdShown, the drive display's record (it reads no game memory)
#define GAUGES_VERSION "0.18.4"

// The exe the addresses in this mod were taken from (SnowRunner on Steam, the build of 22 July 2026). On any other build
// the mod finds them by the code that uses them (game_globals.h, game_code.h), and stands down where it cannot.
static const DWORD kGaugesExeStamp = 0x6a607c05;

// Everything the cluster shows, as the mod's thread leaves it for the render thread.
struct GaugeState
{
    char odometer[24];              // the driven truck's count as shown under the speed ("" none)
    bool engineOn;
    float speedKmh;                 // |forward speed| in km/h
    float speedMax;                 // the speedometer's top in km/h (trucks 100, scouts 120, heavy 80)
    float rpm;                      // engine speed (the mod's own model)
    float rpmRedline, rpmMax;       // the tachometer's scale: red zone from rpmRedline to rpmMax
    bool natural;                   // the engine is naturally aspirated (its profile): no boost dial
    int gear;                       // below 0 reverse, 0 neutral, 1.. forward
    const char *gearMode;           // "A" automatic (drawn beside the gear's number); "H" high, "L", "L+", "L-" (drawn in the number's place);
                                    // "" a numbered gear. String literals only: the pointer crosses threads
    float fuelFrac;                 // 0..1
    float rangeKm;                  // < 0 = not known yet
    float burnPerMin;               // the fuel burn in litres a minute, smoothed; < 0 = not known
    float tempC;                    // coolant temperature (the mod's own model)
    float oilC;                     // oil temperature (the mod's own model)
    float oilWarnC;                 // where the oil dial's red zone starts: the settings' OilWarnC
    float boostBar;                 // boost pressure in bar, gauge (the mod's own model): under 0 a petrol engine's vacuum, up to 3
    bool boostVacuum;               // the dial starts at -1 bar: a petrol engine (BoostModel::petrol)
    float boostTop;                 // the turbo's most in bar: the dial ends at 2 bar up to 2, else at 3
    float airBar;                   // brake air in bar (0..10), from the model (AirModel)
    bool handbrake, diffLock, awd, lowGear, headlights, damage, cold;
    bool oilHot;                    // the oil lamp is lit (it blinks while the oil is too hot: the caller switches it on and off)
    int tireLamp;                   // the tire lamp: 0 none (no tire mod for this truck), 1 Low, 2 Reduced, 3 Normal, 4 Increased (tires.h)
    bool tireLit, tireWarn;         // it is lit (steady away from Normal, blinking while the pressure changes); the tires take damage: lit red
    int tilt;                       // 0 level, 1 warning, 2 danger, below 0 not known (the lamp stays dark)
    float rollDeg;                  // how far the truck leans sideways, above 0 to its right (the tilt lamp's truck leans with it)
    bool mph, psi, fahrenheit;      // units
    float hour;                     // the hour of the game's day, 0 to 24, for the clock; below 0 not known (no hands)
    float outsideC;                 // the outside temperature in force, in C, in the clock's face
};

// BackToA's default in seconds (0 never, so Gearbox=2 is as it was), what the tab's Sequential button sets, and its
// range: 0, else 3 to 30
static const int kBackToADefault = 0, kBackToASequential = 10;
// GameGearDisplay: the game's gear display left as it is, off while the mod's own gearbox drives (the default), or off
// always (game_script.h)
enum { kGameGearGame, kGameGearOffOwn, kGameGearOff };
static const int kGameGearDisplayDefault = kGameGearOffOwn;
inline int BackToARange(int v) { return v <= 0 ? 0 : v < 3 ? 3 : v > 30 ? 30 : v; }

// The letter the cluster draws for a gearbox position: the automatic's "A" as a real automatic's "D" (the user, 4
// November: "A should say D"); the rest as they are. Inside, gearMode, the ini keys, the bindings, the state block
// and the log keep A, so old ini files and logs read as before; the game's own shifter says A and is not the mod's.
inline const char *GearLetterShown(const char *mode) { return mode && mode[0] == 'A' && !mode[1] ? "D" : mode; }
// The digital readout's gear: "D 4" in automatic, the gearbox's own positions by their names, "R", "N" (the user, 2
// November: "it should show the NGG active gear at all times").
inline void GearHudText(int gear, const char *mode, char *buf, size_t size)
{
    const bool named = gear > 0 && mode && mode[0] && mode[0] != 'A';
    if (gear < 0) strncpy_s(buf, size, "R", _TRUNCATE);
    else if (gear == 0) strncpy_s(buf, size, "N", _TRUNCATE);
    else if (named) strncpy_s(buf, size, mode, _TRUNCATE);
    else if (mode && mode[0]) _snprintf_s(buf, size, _TRUNCATE, "%s %d", GearLetterShown(mode), gear);
    else _snprintf_s(buf, size, _TRUNCATE, "%d", gear);
}

// GaugeSettings::layout. An older Gauges reads 2, 3 and 4 as the strip. Off is no place: the cluster is not drawn, and
// nothing that places a cluster is asked about it (BRIEF_dials_off_option.md).
enum { kLayoutStrip, kLayoutBlock, kLayoutHudDials, kLayoutHudDigital, kLayoutOff, kLayoutCount };
// The layout's name for the log. A number that is no layout reads as the strip.
inline const wchar_t *LayoutName(int layout)
{
    static const wchar_t *names[kLayoutCount] = { L"strip", L"block", L"Spotter dials", L"Spotter digital", L"off" };
    return names[layout < 0 || layout >= kLayoutCount ? kLayoutStrip : layout];
}
enum { kFaceSolid, kFaceTinted, kFaceOpen, kFaceCount };     // GaugeSettings::style
#ifndef NG_SLIP_CHOICES // the same line in gauges.h and gearbox_model.h: each is read without the other by some test
#define NG_SLIP_CHOICES
enum { kSlipEngineHeld, kSlipLowGears, kSlipAllGears, kSlipCount }; // GaugeSettings::heldGearSlip (gearbox_model.h, GearboxPosCap)
#endif
enum { kHwNone, kHwAsDials, kHwLimits, kHwCount };           // GaugeSettings::hiddenWarnings (warn_list.h)
enum { kClassTruck, kClassScout, kClassHeavy, kClassCount }; // GaugeSettings::tachClass
enum { kOilOff, kOilNeedle, kOilDial, kOilGaugeCount };      // GaugeSettings::oilGauge
enum { kSpoolStandard, kSpoolQuick, kSpoolLaggy, kSpoolTwoStage, kSpoolCount }; // GaugeSettings::boostProfile
enum { kGearboxGame, kGearboxModel, kGearboxOwn, kGearboxCount }; // GaugeSettings::gearbox: the game's own, the mod's automatic in A, the mod's own gearbox
enum { kInputWheel, kInputPad, kInputKeys, kInputCount }; // GaugeSettings::input
enum { kAspTurbo, kAspNatural, kAspCount };                  // an engine's profile's Aspiration
enum { kFuelClass, kFuelDiesel, kFuelPetrol, kFuelCount };   // an engine's profile's Fuel: the class's (trucks diesel, scouts petrol), or its own

// Everything in Gauges.ini ([Gauges]); the Gameplay tab of the NEXT GEN menu edits all of it.
struct GaugeSettings
{
    int layout;              // Layout: 0 a strip along the bottom centre, 1 a block in the bottom right corner, 2 Spotter dials, 3 Spotter digital (left of Spotter's gear panel)
    bool swapDials;          // SwapDials: the speedometer and the tachometer change places, in the strip, the block and Spotter dials
    int style;               // Style: 0 solid dial faces, 1 tinted (see-through) faces, 2 open (no faces, the scale arcs only); the strip and the block only
    float faceOpacity;       // FaceOpacity: how dark the tinted faces are, 0 = clear
    float offsetX, offsetY;  // OffsetX, OffsetY: moves the cluster from its place, in pixels of a 1080p screen (right and down)
    float scale;             // Scale: the cluster's size (1 = its size on a 1080p screen; it grows with the screen's height)
    float opacity;           // Opacity of the whole cluster
    bool mph, psi, fahrenheit;                    // Mph, Psi, Fahrenheit
    bool showTemp, showBoost, showAir, showLamps; // ShowTemp, ShowBoost, ShowAir, ShowLamps
    bool showClock;          // ShowClock: a clock dial with the game's time of day, the outside temperature in its face
    bool showOdometer;       // ShowOdometer: the driven truck's count under the speed (odometer.h)
    bool showFuelUse;           // ShowFuelUse: the fuel used a minute, after the range under the fuel dial
    int oilGauge;            // OilGauge: the oil temperature: 0 not shown, 1 a second needle in the temperature dial, 2 a dial of its own left of it
    int tachClass;           // TachClass: 0 truck, 1 scout, 2 heavy: the scales of the tachometer and the speedometer
    float idleRpm, redlineRpm, maxRpm, speedMax;  // IdleRpm, RedlineRpm, MaxRpm, SpeedMax: 0 = the class's own
    float boostMax;          // BoostMax: the boost in bar the turbo reaches with the pedal floored at high engine speed; 0 = the class's own
    int boostProfile;        // BoostProfile: how the turbo builds its boost: 0 standard, 1 small and quick, 2 big and slow, 3 two stages
    int gearbox;             // Gearbox: 0 the game's own, 1 the mod's automatic in A (only the game's reverse on the brake refused),
                             // 2 the mod's own gearbox (gearbox_model.h): the shifter taken, its positions from the bindings
    bool requireClutch;      // RequireClutch: a shift into a gear without the clutch stalls the engine
    bool requireGearHeld;    // RequireGearHeld: neutral while no gear key is held, as an H shifter
    bool skipNeutral;        // SkipNeutral: from the lowest forward place to reverse and back without stopping in neutral
    bool downStaysForward;   // DownStaysForward: Gear down stops at the lowest forward place; wins over SkipNeutral
    bool walkSkipsLow;       // WalkSkipsLow: Gear up and Gear down walk R, N, 1, 2 and on; the low range only on its own keys
    bool positionsByGearbox; // PositionsByGearbox: at Gearbox=2, L-, L, L+ and H only where the fitted gearbox has them in the game
    bool ignoreShifter;      // IgnoreShifter: at Gearbox=2 a position picked on the game's shifter does nothing
    int backToA;             // BackToA: at Gearbox=2, seconds after a shift by hand until the gearbox goes back to A by itself; 0 never, else 3 to 30
    int gameGearDisplay;     // GameGearDisplay: the game's gear display at the bottom right; 0 the game's, 1 off with the mod's gearbox, 2 off always
    int input;               // Input: what the pedal is: 0 a wheel's pedals, 1 a pad's trigger, 2 keys. The mod's automatic kicks down by it
    bool powertrain;         // Powertrain: the engine's torque (its curve, the cold oil, the boost) and the converter's handed to the game
    bool roadLoad;           // RoadLoad: with Powertrain, the air's drag taken off the torque handed to the game (models.h, road load); on by default
    bool modEngineSpeed;     // ModEngineSpeed: with Powertrain, the game's engine speed (its tachometer, sound and effects) is the mod engine's (gearbox_hooks.h, EngineSpeedHook); on by default
    float engineSpeedTop;    // EngineSpeedTop: the mod engine's speed the game reads as its 1, as a share of the redline, 0.5 to 1.5 (1: the redline)
    int diffLockBite;        // DiffLockBite: with Powertrain, how firmly the diff lock ties the driven wheels together, 0 (the game's lock) to 100 (one shaft), 0
    int lowRangeSlip;        // LowRangeSlip: with the torque cut in L- and L, the wheel cap is the gear's own plus this percent of the way to the cap of the highest low gear the gearbox has (BRIEF_low_range_pull.md), 0 to 100, 100
    int pushShare;           // PushShare: with Powertrain, the share in percent of the game's chassis push at speed that carries the mod's surplus over its own pull (BRIEF_chassis_push.md), 0 to 100
    int diffType;            // DiffType: the differential of a truck where the torque cut acts, 0 Auto (by the lock kind at vehicle + 0xE8), 1 Open, 2 Limited slip, 3 Locked (torque_cut.h, kDiff...); a truck file may hold its own
    int diffAxle[kAxleRowsMost]; // DiffAxle1 to DiffAxle8, in a truck file alone: each axle's own type front to back, 0 to 3 as DiffType; -1 no key, the axle goes by DiffType (BRIEF_axle_diff.md, M6)
    int airBrakes;           // AirBrakes, in a truck file alone: this truck has air brakes, 1 or 0; -1 no choice, its class decides (AirBrakesOn; BRIEF_spring_brakes.md, M4 and M7)
    int heldGearSlip;        // HeldGearSlip: the slip room of a numbered gear held by hand: 0 the governor as before, 1 the cap of L+ in the gears near it, 2 the wheel cap in every gear (gearbox_model.h, GearboxPosCap); 0 by default until the user has driven
    int wheelCap;            // WheelCap: with the torque cut in L-, L, L+ and R, no driven wheel runs faster than this percent of the gear's top (BRIEF_drivetrain_2c_wheel_cap.md), 110 to 200, 150; no box, for the A/B
    float engineBrake;       // EngineBrake: with Powertrain in a gear held by hand at Gearbox=2 (the torque cut), the share of the gear's torque in percent the engine holds the truck back with at the redline with the foot off, 0 to 30 (torque_cut.h)
    float roadCdA;           // RoadCdA: the drag's area in m², 0 by the truck's mass (1.1 under 4 t, 3.5 under 10 t, else 6); a truck's file may set its own
    int engineShare;         // EngineShare: how much of the mod's engine the game gets with it, in percent: 0 the game's own pull, 100 the mod's
    bool creepManual;        // CreepManual: the creep in a gear picked by hand too (off: only where an automatic picks the gear)
    bool creep;              // Creep: an idling truck in gear moves, an automatic to walking pace, a manual at the gear's idle speed (models.h, CreepShare)
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
    float hotDamageFrom;     // HotDamageFrom: the coolant temperature in C where the hot damage starts (kHotDamageFromRange)
    float coldDamage;        // ColdDamagePercentPerMinute: the same for a cold engine floored at the red zone at 0 C
    float overRevDamage;     // OverRevDamagePercentPerMinute: the same for a held gear driven 0.2 of its top past the governor's end (BRIEF_overrev_downshift.md)
    float stallDamage;       // StallDamagePercent: percent of what the gearbox takes that a shift without the clutch costs it (with Damage)
    bool clutchBurn;         // ClutchBurn: a slipping clutch under load heats up and costs the gearbox (with Damage)
    float clutchHeat;        // ClutchHeatPercent: how fast it heats, percent of the model's own pace (kClutchHeatRange); cooling is not scaled
    float springFront, springRear;   // SpringFront, SpringRear: each wheel's suspension spring as a factor of the truck's own (kSuspScaleRange), by the wheel's end
                                     // (suspension_scan.h, SuspEnds); 1 writes nothing (a test). The one SpringScale of before reads as both
    float dampingFront, dampingRear; // DampingFront, DampingRear: the same for its damping (DampingScale before)
    float frameFlex;         // FrameFlex: the frame's twist spring as a factor of the truck's own, its damping by the square root (kFrameFlexRange); 1 writes nothing
    float twistRange;        // TwistRange: how far the frame twists, as a factor of the truck's own limits (kTwistRange); 1 writes nothing
    float extraTravel;       // ExtraTravel: cm added past the file's compression end (SuspensionMin), kExtraTravelRange; 0 = the file's, it never shortens (a test).
                             // TravelMinScale and TravelMaxScale are no longer read: shortening did not act, and SuspensionMax does nothing in the game
    int damperCurve;         // DamperCurve: the damper's curve above its knee, 0 linear, 1 digressive, 2 progressive (a test, BRIEF_suspension_damper.md)
    float lowCompression, highCompression, lowRebound, highRebound; // LowCompression ..: the damping under and over the knee, rising and falling, as factors (kDamperRange)
    float damperKnee;        // DamperKnee: the shaft speed in m/s where low speed turns into high (kKneeRange)
    bool bumpStops;          // BumpStops: a rising spring and damping over the last BumpZone metres before the compression end (a test, BRIEF_suspension_extras.md)
    float bumpZone, bumpForce, bumpDamping; // BumpZone (kBumpZoneRange, at most a third of the travel), BumpForce: the force at the end as a multiple of the spring's, BumpDamping: the compression damping there
    int springCurve;         // SpringCurve: 0 linear (nothing written), 1 progressive, 2 dual rate
    float springRise, springCross; // SpringRise: the rate at the compression end, a factor; SpringCross: where dual rate's second rate begins, as a share of the travel
    bool dampingZones;       // DampingZones: the damping softer in the middle of the travel (ZoneSoft, from ZoneFrom to ZoneTo) and firmer toward its ends (ZoneFirm)
    float zoneSoft, zoneFirm, zoneFrom, zoneTo;
    float rideFront, rideRear; // RideFront, RideRear: each axle's ride height in cm (kRideRange), its attach points shifted; middle axles go with the rear (a test)
    float swayFront, swayRear; // SwayFront, SwayRear: each axle's sway bar as a share of its spring rate (kSwayRange), 0 none (a test)
    float camberGainFront, camberGainRear; // CamberGainFront, CamberGainRear: the wheels' camber multiplier as a factor of the file's (kCamberGainRange), the front axle and every other; 1 writes nothing (camber.h)
    float camberRestFront, camberRestRear; // CamberRestFront, CamberRestRear: degrees added to the file's render camber (kCamberRestRange), an offset, never the angle; 0 writes nothing
    bool driveDisplay;       // DriveDisplay: the drive display beside the suspension's, each wheel's spin against the truck's speed and its share of the drive (drive_display.h); per player, off by default
    bool suspDisplay;        // SuspDisplay: the suspension display, each wheel's travel, its end hits and whether it hangs, and the articulation
    bool airSuspension;      // AirSuspension: each wheel held at its rest place of the first standstill, cargo or not; BindRideUp and BindRideDown move it (a test)
    bool airHorizon;         // AirHorizon: with AirSuspension, the body's pitch and roll levelled to the horizon instead of to the wheels (a test)
    int airAxleMask;         // AirAxleMask: the ends with air the player ticked (air_axles.h: bit 0 the front axle, bit 1 every other), -1 none ticked (the start order
                             // gives one). Only in a truck's [Suspension] and [Suspension:<id>], never in Gauges.ini; written once the player ticks
    int airMaskFrom;         // not a setting: the level airAxleMask came from (kAirFromKind, kAirFromTruck, kAirFromTicked)
    bool airSync;            // AirSync: the Suspension tab shows the row with the air's height slider (a view setting)
    int shiftOnGameThread;   // ShiftOnGameThread (ini only, 1 by default): the mod's shifts dealt on the game's thread in its truck update; 0 calls the game's ShiftGear from the mod's thread as before (review 17, step 1)
    int suspStepTest;        // SuspStepTest (ini only, a test, with Probe=1): every 20 s wheel 0's spring at half for 1 s standing (1), its damping at a tenth for 0.5 s moving (2), or its attach point 2 cm toward the droop for 1 s standing (3)
    int hiddenWarnings;      // HiddenWarnings: what warns while the Layout is Off: 0 nothing, seen or heard, 1 the clutch's and the engine's box as with dials, 2 a box at the limits below
    int warnFuel;            // WarnFuelUnder: percent of the tank, 0 to 50; 0 = off
    int warnEngine;          // WarnEngineOver: percent of the engine damage the engine takes, 0 to 100; 100 = off
    int warnGearbox;         // WarnGearboxOver: the same of the gearbox
    int warnCoolant;         // WarnCoolantOver: percent of the way from kTempWarm to kTempRed, 50 to 100; 100 = off
    int soundVolume;        // SoundVolume: how loud the turbo's flutter and the oil warning are, 0 to 100; 0 = no sound
    bool soundsOnly;         // SoundsOnly: the sounds are played and the cluster is not drawn
    bool dieselFlutter;      // DieselFlutter: a diesel's lift flutters too; off, it is quiet, or plays the player's own turbo whistle files (review 12, finding 6)
    int toggleKey;           // ToggleKey: virtual key code of the key that hides and shows the cluster, 0 = none
    char padToggle[32];      // PadToggle: pad buttons that do the same ("LB+DPadUp"), "None"
    int physicsRate;         // PhysicsRate: 60 or 120, a test (physics_rate.h): read once at the start, a change acts at the next one
    bool demo;               // Demo: for work on the mod: demo values are drawn whenever no truck is driven, menus and loading screens included
    int probe;               // Probe: for work on the mod: the values read from the game go into Gauges.log (0 = off)
    int keepLogs;            // KeepLogs: how many of the last starts' logs Gauges\logs keeps, at most 500 MB of them; 0 keeps all
};

inline void SettingsDefaults(GaugeSettings &s)
{
    s = GaugeSettings{};
    s.layout = kLayoutStrip;
    s.swapDials = false;
    s.style = kFaceOpen;
    s.faceOpacity = 0.35f;
    s.scale = 1.0f;
    s.opacity = 1.0f;
    s.showTemp = s.showBoost = s.showLamps = true;
    s.showAir = false;
    s.showClock = true;
    s.showOdometer = true;
    s.showFuelUse = true;
    s.oilGauge = kOilNeedle;
    s.tachClass = kClassTruck;
    s.boostProfile = kSpoolStandard;
    s.gearbox = kGearboxGame;
    s.requireClutch = s.requireGearHeld = s.skipNeutral = s.downStaysForward = s.walkSkipsLow = s.ignoreShifter = false;
    s.positionsByGearbox = true;
    s.backToA = kBackToADefault;
    s.gameGearDisplay = kGameGearDisplayDefault;
    s.powertrain = false;
    s.engineShare = 50;
    s.roadLoad = true; // on by default (BRIEF_defaults_on.md); an explicit 0 in the ini stays 0
    s.engineBrake = 15.0f; // kEngineBrakeDefault (BRIEF_drivetrain_2_torque_cut.md)
    s.modEngineSpeed = true; // on by default (BRIEF_drivetrain_1_engine_speed.md); an explicit 0 stays 0
    s.heldGearSlip = 0;
    s.wheelCap = 150; // kCapDefault (BRIEF_drivetrain_2c_wheel_cap.md with M5)
    s.pushShare = 0; // the surplus taken back (BRIEF_chassis_push.md)
    s.lowRangeSlip = 100; // L- and L with the slip room of L+ (BRIEF_low_range_pull.md, the lead)
    s.diffLockBite = 0; // the game's own lock until the drive picks a default (BRIEF_diff_lock_bite.md)
    s.diffType = 0; // Auto (BRIEF_diff_types.md)
    s.airBrakes = -1; // by the class
    for (int a = 0; a < kAxleRowsMost; a++) s.diffAxle[a] = -1;
    s.engineSpeedTop = 1.0f;
    s.roadCdA = 0.0f;
    s.creep = true;
    s.creepManual = false;
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
    s.hotDamageFrom = 118.0f;
    s.coldDamage = 5.0f;
    s.overRevDamage = 10.0f;
    s.stallDamage = 5.0f;
    s.clutchBurn = true;
    s.clutchHeat = 60.0f;
    s.springFront = s.springRear = s.dampingFront = s.dampingRear = 1.0f;
    s.frameFlex = s.twistRange = 1.0f;
    s.extraTravel = 0.0f;
    s.lowCompression = s.highCompression = s.lowRebound = s.highRebound = 1.0f;
    s.damperKnee = 0.1f;
    s.airAxleMask = -1;
    s.bumpZone = 0.12f;
    s.bumpForce = 3.0f;
    s.bumpDamping = 2.0f;
    s.springRise = 2.0f;
    s.camberGainFront = s.camberGainRear = 1.0f;
    s.camberRestFront = s.camberRestRear = 0.0f;
    s.springCross = 0.5f;
    s.zoneSoft = 0.7f;
    s.zoneFirm = 1.5f;
    s.zoneFrom = 0.1f;
    s.zoneTo = 0.6f;
    s.hiddenWarnings = kHwAsDials;
    s.warnFuel = 15;
    s.warnEngine = 50;
    s.warnGearbox = 50;
    s.warnCoolant = 90;
    s.soundVolume = 50;
    strncpy_s(s.padToggle, "None", _TRUNCATE);
    s.physicsRate = 60;
    s.demo = false;
    s.probe = 0;
    s.keepLogs = 20;
    s.shiftOnGameThread = 1;
}

// The range of each number: what the ini and the tab hold the settings to. A tinted face stops at 0.85, as a darker
// one is the solid style. The tilt lamp's angles are degrees, the coolant's three times and the oil's warm-up minutes,
// the outside temperature and the oil's warning degrees C, the speed of the radiator's full airflow km/h, the two
// damage rates percent a minute, a stall's gearbox damage percent, the boost bar (the dial ends at 2, or 3 over 2), an engine's torque a share of its own.
static const float kFaceOpacityMax = 0.85f, kScaleMin = 0.4f, kScaleMax = 3.0f, kOpacityMin = 0.1f, kOffsetMax = 4000.0f;
static const float kIdleRange[2] = { 300.0f, 3000.0f }, kRedlineRange[2] = { 1000.0f, 12000.0f }, kMaxRpmRange[2] = { 1500.0f, 15000.0f },
                   kSpeedMaxRange[2] = { 40.0f, 300.0f }, kTiltRange[2] = { 1.0f, 89.0f }, kBoostRange[2] = { 0.3f, 3.0f };
static const float kWarmUpRange[2] = { 0.5f, 60.0f }, kOverheatRange[2] = { 0.25f, 60.0f }, kCoolDownRange[2] = { 1.0f, 240.0f },
                   kAmbientRange[2] = { -40.0f, 50.0f }, kGearHoldRange[2] = { 1.0f, 3.0f }, kAirFullRange[2] = { 10.0f, 80.0f }, kDamageRange[2] = { 0.0f, 100.0f },
                   kOilWarmUpRange[2] = { 1.0f, 60.0f }, kOilWarnRange[2] = { 100.0f, 150.0f }, kTorqueShareRange[2] = { 0.10f, 1.00f },
                   kHotDamageFromRange[2] = { 100.0f, 124.0f }, kStallDamageRange[2] = { 0.0f, 25.0f },
                   kClutchHeatRange[2] = { 10.0f, 300.0f }, kSuspScaleRange[2] = { 0.25f, 4.0f }, kFrameFlexRange[2] = { 0.1f, 7.0f }, kTwistRange[2] = { 0.25f, 8.0f }, kExtraTravelRange[2] = { 0.0f, 30.0f }, kRideRange[2] = { -10.0f, 10.0f }, kSwayRange[2] = { 0.0f, 0.3f }, kDamperRange[2] = { 0.25f, 4.0f },
                   kKneeRange[2] = { 0.02f, 0.5f }, kBumpZoneRange[2] = { 0.03f, 0.2f }, kBumpForceRange[2] = { 1.0f, 6.0f },
                   kBumpDampingRange[2] = { 1.0f, 4.0f }, kSpringRiseRange[2] = { 1.0f, 3.0f }, kSpringCrossRange[2] = { 0.2f, 0.8f },
                   kZoneSoftRange[2] = { 0.5f, 1.0f }, kZoneFirmRange[2] = { 1.0f, 3.0f };
static const int kSpringCurves = 3; // linear, progressive, dual rate
static const int kDamperCurves = 3; // linear, digressive, progressive

// The damper's presets (BRIEF_suspension_damper.md), for the tab's buttons and the log's names.
struct DamperPreset { const char *name; float lowC, highC, lowR, highR, knee; int curve; };
static const DamperPreset kDamperPresets[] = { { "Stock", 1.0f, 1.0f, 1.0f, 1.0f, 0.1f, 0 }, { "Trail", 0.8f, 0.6f, 1.4f, 1.0f, 0.1f, 1 },
                                               { "Rock crawl", 0.6f, 0.8f, 0.9f, 0.7f, 0.08f, 2 }, { "Road", 1.4f, 0.8f, 1.8f, 1.0f, 0.1f, 1 } };
static const int kDamperPresetCount = (int)(sizeof kDamperPresets / sizeof *kDamperPresets);

// The preset all six damper settings match, -1 for none (custom).
inline int DamperPresetOf(const GaugeSettings &s)
{
    for (int i = 0; i < kDamperPresetCount; i++)
    {
        const DamperPreset &d = kDamperPresets[i];
        if (s.damperCurve == d.curve && s.lowCompression == d.lowC && s.highCompression == d.highC && s.lowRebound == d.lowR && s.highRebound == d.highR &&
            s.damperKnee == d.knee)
            return i;
    }
    return -1;
}

// The temperatures in C the coolant's three times are measured between: WarmUpMinutes from kTempOutside outside up
// to kTempWarm, OverheatMinutes from kTempHot into the red zone at kTempRed, CoolDownMinutes from kTempHot down to
// kTempCooled. The cold lamp is lit below kTempCold. HotDamagePercentPerMinute is what an engine at kTempHotDamage
// loses; the damage starts at HotDamageFrom, kTempRed by default. The model, the dial's zones and the tab's texts all use these.
static const float kTempOutside = 15.0f, kTempWarm = 85.0f, kTempHot = 90.0f, kTempRed = 118.0f, kTempCooled = 40.0f, kTempCold = 60.0f,
                   kTempHotDamage = 125.0f;

inline float SettingsRange(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
// 0 (the class's own) stays 0; anything else is held to the range.
inline float SettingsOverride(float v, const float range[2]) { return v <= 0.0f ? 0.0f : SettingsRange(v, range[0], range[1]); }
// A value on its slider's step; within half a step of 0 it is exactly 0, never -0.
inline float SettingsStep(float v, float step)
{
    const float a = fabsf(v) / step;
    if (!(a > 0.5f)) return 0.0f;
    const float r = floorf(a + 0.5f) * step;
    return v < 0.0f ? -r : r;
}
static const float kRideStep = 1.0f, kSwayStep = 0.01f;
// Camber gain and Camber at rest (BRIEF_swing_camber.md): x0 to x3 of the file's multiplier, the file's angle plus or minus 10 degrees
static const float kCamberGainRange[2] = { 0.0f, 3.0f }, kCamberRestRange[2] = { -10.0f, 10.0f }, kCamberGainStep = 0.05f, kCamberRestStep = 0.5f;
// The air suspension's keys (build C): steps of kRideKeyStep cm within kRideKeyMost, under kRideKeyKmh only. dir +1 up, -1 down.
static const int kRideKeyStep = 2, kRideKeyMost = 10;
static const float kRideKeyKmh = 20.0f;
inline int AirHeightStep(int cm, int dir, float kmh, bool on)
{
    if (!on || !(kmh < kRideKeyKmh) || !dir) return cm;
    const int next = cm + (dir > 0 ? kRideKeyStep : -kRideKeyStep);
    return next > kRideKeyMost ? kRideKeyMost : next < -kRideKeyMost ? -kRideKeyMost : next;
}
// The Suspension tab's height slider (BRIEF_air_axle_ticks.md): the keys' own figure set to cm, within kRideKeyMost and
// under kRideKeyKmh as the keys are.
inline int AirHeightSet(int cm, int to, float kmh, bool on)
{
    if (!on || !(kmh < kRideKeyKmh)) return cm;
    return to > kRideKeyMost ? kRideKeyMost : to < -kRideKeyMost ? -kRideKeyMost : to;
}
// The ride height slider's text: 0 with no sign, so the label keeps one form through 0.
inline const char *RideFormat(float v) { return v != 0.0f ? "%+.0f cm" : "0 cm"; }

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
    s.engineSpeedTop = isfinite(s.engineSpeedTop) ? SettingsRange(s.engineSpeedTop, 0.5f, 1.5f) : 1.0f;
    s.engineBrake = isfinite(s.engineBrake) ? SettingsRange(s.engineBrake, 0.0f, 30.0f) : 15.0f;
    s.wheelCap = s.wheelCap < 110 ? 110 : s.wheelCap > 200 ? 200 : s.wheelCap;
    s.heldGearSlip = s.heldGearSlip < 0 || s.heldGearSlip > 2 ? 0 : s.heldGearSlip;
    s.pushShare = s.pushShare < 0 ? 0 : s.pushShare > 100 ? 100 : s.pushShare;
    s.lowRangeSlip = s.lowRangeSlip < 0 ? 0 : s.lowRangeSlip > 100 ? 100 : s.lowRangeSlip;
    s.diffLockBite = s.diffLockBite < 0 ? 0 : s.diffLockBite > 100 ? 100 : s.diffLockBite;
    s.diffType = s.diffType < 0 || s.diffType > 3 ? 0 : s.diffType;
    s.airBrakes = s.airBrakes == 0 || s.airBrakes == 1 ? s.airBrakes : -1;
    for (int a = 0; a < kAxleRowsMost; a++) s.diffAxle[a] = s.diffAxle[a] < 0 || s.diffAxle[a] > 3 ? -1 : s.diffAxle[a];
    s.physicsRate = s.physicsRate == 120 ? 120 : 60;
    s.roadCdA = isfinite(s.roadCdA) ? (s.roadCdA <= 0.0f ? 0.0f : s.roadCdA < 0.3f ? 0.3f : s.roadCdA > 20.0f ? 20.0f : s.roadCdA) : 0.0f;
    s.gearbox = s.gearbox < 0 || s.gearbox >= kGearboxCount ? kGearboxGame : s.gearbox;
    s.input = s.input < 0 || s.input >= kInputCount ? kInputWheel : s.input;
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
    s.hotDamageFrom = SettingsRange(s.hotDamageFrom, kHotDamageFromRange[0], kHotDamageFromRange[1]);
    s.coldDamage = SettingsRange(s.coldDamage, kDamageRange[0], kDamageRange[1]);
    s.overRevDamage = SettingsRange(s.overRevDamage, kDamageRange[0], kDamageRange[1]);
    s.stallDamage = SettingsRange(s.stallDamage, kStallDamageRange[0], kStallDamageRange[1]);
    s.clutchHeat = SettingsRange(s.clutchHeat, kClutchHeatRange[0], kClutchHeatRange[1]);
    float *const ends[] = { &s.springFront, &s.springRear, &s.dampingFront, &s.dampingRear };
    for (float *v : ends) *v = SettingsRange(*v, kSuspScaleRange[0], kSuspScaleRange[1]);
    s.frameFlex = SettingsRange(s.frameFlex, kFrameFlexRange[0], kFrameFlexRange[1]);
    s.twistRange = SettingsRange(s.twistRange, kTwistRange[0], kTwistRange[1]);
    s.extraTravel = SettingsRange(s.extraTravel, kExtraTravelRange[0], kExtraTravelRange[1]);
    s.rideFront = SettingsRange(SettingsStep(s.rideFront, kRideStep), kRideRange[0], kRideRange[1]);
    s.rideRear = SettingsRange(SettingsStep(s.rideRear, kRideStep), kRideRange[0], kRideRange[1]);
    s.airAxleMask = s.airAxleMask < 0 ? -1 : s.airAxleMask & 3; // its two bits (air_axles.h)
    s.swayFront = SettingsRange(SettingsStep(s.swayFront, kSwayStep), kSwayRange[0], kSwayRange[1]);
    s.swayRear = SettingsRange(SettingsStep(s.swayRear, kSwayStep), kSwayRange[0], kSwayRange[1]);
    // on the sliders' steps, so x1 and +0 are exactly 1 and 0 and write nothing
    s.camberGainFront = SettingsRange(SettingsStep(s.camberGainFront, kCamberGainStep), kCamberGainRange[0], kCamberGainRange[1]);
    s.camberGainRear = SettingsRange(SettingsStep(s.camberGainRear, kCamberGainStep), kCamberGainRange[0], kCamberGainRange[1]);
    s.camberRestFront = SettingsRange(SettingsStep(s.camberRestFront, kCamberRestStep), kCamberRestRange[0], kCamberRestRange[1]);
    s.camberRestRear = SettingsRange(SettingsStep(s.camberRestRear, kCamberRestStep), kCamberRestRange[0], kCamberRestRange[1]);
    s.damperCurve = s.damperCurve < 0 || s.damperCurve >= kDamperCurves ? 0 : s.damperCurve;
    s.lowCompression = SettingsRange(s.lowCompression, kDamperRange[0], kDamperRange[1]);
    s.highCompression = SettingsRange(s.highCompression, kDamperRange[0], kDamperRange[1]);
    s.lowRebound = SettingsRange(s.lowRebound, kDamperRange[0], kDamperRange[1]);
    s.highRebound = SettingsRange(s.highRebound, kDamperRange[0], kDamperRange[1]);
    s.damperKnee = SettingsRange(s.damperKnee, kKneeRange[0], kKneeRange[1]);
    s.bumpZone = SettingsRange(s.bumpZone, kBumpZoneRange[0], kBumpZoneRange[1]);
    s.bumpForce = SettingsRange(s.bumpForce, kBumpForceRange[0], kBumpForceRange[1]);
    s.bumpDamping = SettingsRange(s.bumpDamping, kBumpDampingRange[0], kBumpDampingRange[1]);
    s.springCurve = s.springCurve < 0 || s.springCurve >= kSpringCurves ? 0 : s.springCurve;
    s.springRise = SettingsRange(s.springRise, kSpringRiseRange[0], kSpringRiseRange[1]);
    s.springCross = SettingsRange(s.springCross, kSpringCrossRange[0], kSpringCrossRange[1]);
    s.zoneSoft = SettingsRange(s.zoneSoft, kZoneSoftRange[0], kZoneSoftRange[1]);
    s.zoneFirm = SettingsRange(s.zoneFirm, kZoneFirmRange[0], kZoneFirmRange[1]);
    s.zoneFrom = SettingsRange(s.zoneFrom, 0.0f, 1.0f);
    s.zoneTo = SettingsRange(s.zoneTo, 0.0f, 1.0f);
    if (s.zoneFrom >= s.zoneTo) s.zoneFrom = 0.1f, s.zoneTo = 0.6f; // the soft part needs a width
    s.toggleKey = s.toggleKey < 0 || s.toggleKey > 255 ? 0 : s.toggleKey;
    s.hiddenWarnings = s.hiddenWarnings < 0 || s.hiddenWarnings >= kHwCount ? kHwAsDials : s.hiddenWarnings;
    s.warnFuel = s.warnFuel < 0 ? 0 : s.warnFuel > 50 ? 50 : s.warnFuel;
    s.warnEngine = s.warnEngine < 0 ? 0 : s.warnEngine > 100 ? 100 : s.warnEngine;
    s.warnGearbox = s.warnGearbox < 0 ? 0 : s.warnGearbox > 100 ? 100 : s.warnGearbox;
    s.warnCoolant = s.warnCoolant < 50 ? 50 : s.warnCoolant > 100 ? 100 : s.warnCoolant;
    s.soundVolume = s.soundVolume < 0 ? 0 : s.soundVolume > 100 ? 100 : s.soundVolume;
    s.probe = s.probe < 0 ? 0 : s.probe;
    s.keepLogs = s.keepLogs < 0 ? 0 : s.keepLogs > 1000 ? 1000 : s.keepLogs;
    s.suspStepTest = s.suspStepTest < 0 || s.suspStepTest > 3 ? 0 : s.suspStepTest;
    s.shiftOnGameThread = s.shiftOnGameThread ? 1 : 0;
    s.backToA = BackToARange(s.backToA);
    s.gameGearDisplay = s.gameGearDisplay < 0 || s.gameGearDisplay > 2 ? kGameGearDisplayDefault : s.gameGearDisplay;
}

// The scales of a truck class: idle, where the red zone starts, the tachometer's top (rpm), the speedometer's top
// (km/h) and the boost its turbo reaches (bar).
struct GaugeScale { float idle, redline, max, speedMax, boost; };
static const float kAirWarn = 5.5f; // bar: under this the brake air dial's red zone, too little air to brake safely (AirStep, GAir)
static const float kSpringHold = 3.0f;    // bar: under this the spring brakes hold the truck (models.h, SpringHeldStep)
static const float kSpringRelease = 4.5f; // bar: from here they let it go
// The truck has air brakes (BRIEF_spring_brakes.md, M4 and M5): its own choice where its file holds one, else by its
// class in force, every truck but a scout (scouts brake by hydraulics). Everything of the brake air asks this: the
// model, the dial, the HUD's cell, the warning, the purge and the spring brakes.
inline bool AirBrakesOn(const GaugeSettings &s) { return s.airBrakes == 1 || (s.airBrakes != 0 && s.tachClass != kClassScout); }
static const GaugeScale kClassScales[kClassCount] = { { 650.0f, 2200.0f, 3000.0f, 100.0f, 1.8f }, { 800.0f, 5500.0f, 7000.0f, 120.0f, 1.0f }, { 600.0f, 1900.0f, 2500.0f, 80.0f, 2.0f } };

// The class changed from was: a number set to the old class's own follows the new class (0, the class's), and one set
// by hand to anything else stays (Milos_Mk6 lost his numbers picking a class).
inline void ScalesClassChange(GaugeSettings &s, int was)
{
    const GaugeScale old = kClassScales[was < 0 || was >= kClassCount ? kClassTruck : was];
    if (s.idleRpm == old.idle) s.idleRpm = 0.0f;
    if (s.redlineRpm == old.redline) s.redlineRpm = 0.0f;
    if (s.maxRpm == old.max) s.maxRpm = 0.0f;
    if (s.speedMax == old.speedMax) s.speedMax = 0.0f;
    if (s.boostMax == old.boost) s.boostMax = 0.0f;
}

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
    if (s.showOdometer) strcpy_s(st.odometer, s.mph ? "7671 mi" : "12345 km");
    st.engineOn = true;
    st.speedKmh = 46.0f;
    st.rpm = k.idle + (k.redline - k.idle) * 0.77f;
    st.gear = 4;
    st.gearMode = "A";
    st.fuelFrac = 0.62f;
    st.rangeKm = 412.0f;
    st.burnPerMin = 4.1f;
    st.tempC = 88.0f;
    st.oilC = 97.0f;
    st.boostBar = 1.15f;
    st.boostTop = 1.8f;
    st.airBar = 8.2f;
    st.awd = st.diffLock = st.headlights = true;
    st.rollDeg = 5.0f;
    st.hour = 14.0f + 35.0f / 60.0f;
    st.outsideC = 12.0f;
}

// What the render thread draws: show = the cluster is on screen, demo = st holds the demo values, hidden = why it is
// not on screen. engineDamage of engineCapacity is the driven truck's engine damage as the game holds it, for the
// tab; engineCapacity is 0 while it is not known.
enum { kShown, kHiddenByToggle, kHiddenNoTruck, kHiddenSoundsOnly, kHiddenOff, kHiddenCount };
// Why the cluster is not on screen, or kShown. The Off layout comes first: there the toggle, a driven truck and the
// demo values count for nothing.
inline int ClusterHidden(int layout, bool visible, bool soundsOnly, bool driving, bool demo)
{
    return layout == kLayoutOff ? kHiddenOff : !visible ? kHiddenByToggle : soundsOnly ? kHiddenSoundsOnly : driving || demo ? kShown : kHiddenNoTruck;
}
// The toggle key or the pad chord was pressed with this layout in force. In Off it does nothing, or the player would
// leave Off and find the cluster hidden by a press made a while ago. True when it flipped.
inline bool ClusterToggle(bool &visible, int layout)
{
    if (layout == kLayoutOff) return false;
    visible = !visible;
    return true;
}
// The layout in force went from one to another: leaving Off shows the cluster, whatever the toggle had made of it.
inline void ClusterLayoutChange(bool &visible, int was, int now)
{
    if (was == kLayoutOff && now != kLayoutOff) visible = true;
}
// How strong the turbo's flutter is after a lift of the pedal, by the boost the lift had: the sounds come in these three.
// After them the oil warning, the air dryer's purge and a diesel turbo's whistle (the player's own files only), the
// mod's other sounds.
enum { kTurboHigh, kTurboMid, kTurboLow, kTurboTiers };
enum { kSoundOil = kTurboTiers, kSoundPurge, kSoundWhistle, kSoundTierCount }; // the oil warning, the air dryer's purge at the compressor's cut-out, a diesel's lift (own files)
// truck: the kind of truck being driven, as the game's files name it ("" while none is driven): the scales in force
// are that kind's. truckOwn: it has scales of its own in Gauges.ini, and those are the ones in force. map: the map
// the truck drives on in words, with its region ("" while it is not known), and ambientC the outside temperature in
// force: its section's (mapOwn, from the section [Map:<mapFrom>], the map's or its region's) or AmbientC's.
// pullTest: kViewPullOff to kViewPullStopped, the pull test's state (PullTest in models.h); pullWhy: why it stopped, a
// string that lives as long as the mod
enum { kViewPullOff, kViewPullArmed, kViewPullRunning, kViewPullDone, kViewPullStopped };
static const int kEnginePlot = 40; // points of the tab's torque plot
static const int kViewActions = 35; // the gearbox's actions the view holds the bindings of (input_bindings.h's kActCount)
static const int kViewShifts = 28;  // of them the shifter's, before the two locks (DiffLock, Awd)
// The tab's asks of the gearbox's bindings: begin a capture into an action's slot, or clear it (the mod's thread does
// both, with the snapshot of its pass).
void BindingAsk(int what, int action, int slot);
enum { kBindAskCapture = 1, kBindAskClear = 2, kBindAskCancel = 3 };
// A warning box (models.h, ClutchWarn and EngineWarn; drawn by gauge_draw.h, GWarnBox, the tire mod's TIRE DAMAGE box's
// shape): none, amber or red, its title and line of advice (string literals), and the number at its right and the bar
// along its bottom in percent, 0 to 100. figure: where it is not empty it stands at the right in place of the percent
// (degrees, bar), and the percent is the bar's alone.
enum { kWarnNone, kWarnAmber, kWarnRed };
struct WarnBox
{
    int state;
    const char *title, *line;
    int percent;
    char figure[12];
};
// The view holds three boxes. With dials shown the first is the clutch's and the second the engine's, each in its own
// slot. With the Layout at Off they are a stack, made by WarnListMake (warn_list.h).
enum { kWarnClutch, kWarnEngine, kWarnBoxes = 3 };

// The suspension display's figures (build B), at most kViewWheels wheels: each wheel's place along its axis and its travel
// (m, compression negative), its rest place, a hit of the compression end in the last second, whether it hangs (at or past
// its spring's zero), its side and its axle (0 the front), and the articulation in cm (the front pair's difference less
// the rearmost's). wheels 0: no figures.
static const int kViewWheels = 8;
struct SuspDisplayView
{
    int wheels, axles;
    float x[kViewWheels], travel[kViewWheels], travelMax[kViewWheels], rest[kViewWheels];
    bool hit[kViewWheels], hanging[kViewWheels], placed[kViewWheels], isLeft[kViewWheels];
    int axle[kViewWheels];
    bool stops;
    float zone;
    bool articulationKnown;
    float articulation;
};

// smtInstalled: the manual transmission mod is in the game; gearboxHeld: another mod holds the game's gearbox
// functions. Either way the mod's gearbox stands down (the tab says why). warn: the clutch's and the engine's warning
// boxes while a truck is driven.
enum { kRateNoteNone, kRateNoteOff, kRateNoteIter, kRateNoteStep, kRateNotePart };
struct GaugeView { bool show, demo; int hidden; GaugeState st; WarnBox warn[kWarnBoxes]; bool warnStacked; int engineDamage, engineCapacity; char truck[96]; char idWords[96]; bool truckOwn, suspOwn, suspKept, suspIdKnown, suspTruckKept, suspKindSet, smtInstalled, gearboxHeld; int suspLevel; char map[96], mapFrom[64]; bool mapOwn; float ambientC;
                   int pullTest, pullPhase; const char *pullWhy;
                   float ambientDay, ambientNight, hour; // the map's day and night (with mapOwn), the hour of the game's day (-1 not known)
                   SuspDisplayView susp; DdShown drive; bool driveShown; bool swayOff; float rideRest[2]; bool rideRestKnown; // the display's figures; the bars disconnected; each axle's rest place along its axis (front, rear; cm), from the probe
                   bool air, airTaken, airShown; int airHeight; float airLevel[2], airSide; bool airKnown; int airAxles, airFrom; bool diffKnown; int diffAuto;
                   // the differential by axle (BRIEF_axle_diff.md, M8): the rows, one for each axle of the driven truck that can be driven: its axle (0 the front),
                   // whether it is driven now and whether its key holds it Locked; diffNoPairs: the axles are not read as pairs; diffKind: the lock kind (-1 not read);
                   // diffBiteActs: the lock slot is published for this truck's kind
                   int diffRows, diffRowAxle[kAxleRowsMost], diffKind; bool diffRowNow[kAxleRowsMost], diffRowKey[kAxleRowsMost], diffNoPairs, diffBiteActs; // airKnown: a truck is driven and its air axles (air_axles.h) and their source are read // the air suspension runs on the driven truck, its rest places taken, the height box shown; the keys' height (cm) and the levelling (front, rear, left over right; cm)
                   bool frameKnown, frameRigid; float frameFile[4], frameMost; // the driven truck's frame joint (frame_flex.h): its stock spring, damping and limits in degrees; or none (a rigid frame)
                   bool suspKnown; float suspFile[3][2]; // the driven truck's file: spring, damping and the compression end (m), [front, rear] (wheel 0 and the last)
                   char gearbox[96]; float gearHold; bool gearHoldOwn; // the driven truck's gearbox (its key, "" for none), the seconds each gear is held going up with the mod's engine, and whether its file sets them
                   int autoClass; bool autoByWeight; float massKg; // the class the automatic goes by for it, whether by its weight (no scales of its own), and that weight (0 not read)
                   bool camberKnown; bool camberEnd[2]; float camberFile[2][2]; // the driven truck's camber (camber.h) is read; each end has a wheel; its file's render angle (degrees) and multiplier, [front, rear]
                   char engine[96]; bool engineOwn, engineCustom; float enginePeakRpm; int engineAspiration, engineFuel; float engineTorque, engineFigure; bool engSetOwn, engSetKept, engSetTruckKept, engSetKindSet; int engSetLevel, engTorqueFrom, engHoldFrom, engProfileFrom; // the engine's settings per truck: a level in force or kept, this truck's kept, the kind's there, the level in force (0 none, 1 the default, 2 the kind's, 3 this truck's), and the level each figure (and the profile, whole) came from // the driven truck's engine ("" not known): a profile other than the class's curve, turbo and the class's fuel or not, a whole curve of its own in it, the peak torque's speed in force, turbo or not, its fuel (kFuelClass the class's), the share of its torque the truck gets (1 none set), and the engine's torque figure from the game's files (0 not read)
                   float engineIdle, engineRedline; float engineCurve[kEnginePlot]; // the slider's range, and the torque in force from idle to the tachometer's top, for the tab's plot
                   // the gearbox's bindings as text (input_bindings.h, kActCount actions, two slots), the capture the tab
                   // asked for (-1 none), whether a binding reaches reverse (Gearbox=2 may take the shifter), the
                   // controllers read, and the stalls and shifts since the truck was taken
                   char bindText[kViewActions][2][40]; int captureAction, captureSlot; bool canReverse; int controllers, gearStalls, gearShifts;
                   int rateNow, rateNote; // the physics rate in force (60 or 120) and what the tab says of it (kRateNote...)
                   bool stepSign; // another mod changes the physics rate: the sign in the middle of the screen, on until the game ends (StepSignShown)
                   bool odoKnown, odoCanSet, odoHours; char odoText[24]; // the driven truck's count: decided, Set to open (its id read, the match not waiting), hours shown, the figure
                   };
// Whether the physics rate sign is drawn: by the view's flag alone. Not by show or hidden (the toggle key, no truck,
// sounds only), not by any setting: nothing hides it (BRIEF_step_check_always.md).
inline bool StepSignShown(const GaugeView &v) { return v.stepSign; }

LONG SettingsGet(GaugeSettings &out);       // the settings in force (any thread); returns their turn, for SettingsPut
void SettingsPut(const GaugeSettings &in, LONG turn); // settings edited from those of a turn: the mod takes them at once and saves the ini a
                                            // second later. They are dropped when the mod has put others in force since that turn
void SettingsReload();                      // read the ini again (the tab's button)
void SettingsTruckDrop();                   // the tab's button: the driven truck gives up its own scales and takes the plain ones
void SettingsSuspOwn(bool on);              // the Suspension tab's box: the driven truck's own suspension in force, or kept with Own=0
void SettingsSuspToDefault();               // the Suspension tab's button: the driven truck's suspension becomes the default
void SettingsSuspLevel(bool truckOnly);     // the Suspension tab's choice: this truck only, or every truck of its kind
void AirHeightAsk(int cm);                  // the Suspension tab's height slider: the air's height (what BindRideUp and BindRideDown step) set to cm
void EngineOwnAsk(bool on);                 // the Engine tab's box Own settings (BRIEF_engine_per_truck.md)
void EngineToDefaultAsk();                  // the Engine tab's button Make this the engine's default
void EngineLevelAsk(bool truckOnly);        // the Engine tab's choice: this truck only, or every truck of its kind
void SettingsReset();                       // the tab's button: every setting back to its default, the driven truck's own scales too
void PullTestAsk(bool run);                  // the tab's button: arm the pull test, or cancel it
void MapAmbientSet(float c);                 // the tab's slider: the outside temperature of the map's section in force
void GearHoldSet(float seconds);            // the tab's slider: the seconds each gear of the driven truck's gearbox is held going up
void EngineProfileSet(float peakRpm, int aspiration, int fuel); // the tab's Engine section: the driven truck's engine's profile into its file
void EngineProfileDrop();                   // the tab's button: the driven truck's engine gives its profile up
void PageTabRead(char *out, size_t size);   // the Gameplay tab's open tab, its id as Gauges.ini keeps it ("" for none)
void PageTabWrite(const char *id);           // the tab picked: into Gauges.ini on the mod's thread, on its own
void OdometerSetAsk(bool hours, double value); // the Display tab's Set: the driven truck's count is value (metres, or seconds with hours)
void OdometerHoursAsk(bool hours);          // the Display tab's Counts: this kind shows engine hours (OdometerHours=1 in [Truck]) or the distance
void EngineTorqueSet(int percent);          // the tab's slider: how much of its torque the driven truck's engine gives the truck (Torque), in percent
void DamageTestAsk(int points);             // the tab's test buttons: the mod adds this much to the driven truck's engine damage on its next pass
void ViewGet(GaugeView &out);               // what to draw (any thread)
void SoundHear(int tier);                   // plays the turbo's sound of that strength (kTurboHigh ..) or the oil warning (kSoundOil) as loud as the settings have it (the tab's buttons)
void GaugeLog(const wchar_t *fmt, ...);     // a line in Gauges.log (any thread)
bool OverlayHooks();                        // the overlay's hooks on the game's swap chains, first at the start; false = it did not start
void OverlayInit(HMODULE self);             // the overlay's fonts, then the word that frames may be drawn
uint32_t PadButtons();                      // the buttons of all connected pads together, 0 without a pad

extern volatile LONG g_pageCapturing; // GetTickCount() of the last frame the tab waited for pad buttons, 0 = none: the mod ignores the pad then
extern volatile LONG g_modOff;        // 1 = the mod stood down (another version of the game): the tab says so

// A stick's direction as a pad button, above XInput's 16 buttons in the mask: down while that axis of the stick is past
// half its travel from the dead zone (the right stick's, the larger, for both sticks; up is the axis' positive side,
// as XInput has it). A diagonal past half on both axes holds both directions, as a d-pad's does.
static const uint32_t kPadLSUp = 1u << 16, kPadLSDown = 1u << 17, kPadLSLeft = 1u << 18, kPadLSRight = 1u << 19, kPadRSUp = 1u << 20,
                      kPadRSDown = 1u << 21, kPadRSLeft = 1u << 22, kPadRSRight = 1u << 23;
static const int kPadStickDeadZone = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
static const int kPadStickPast = kPadStickDeadZone + (32767 - kPadStickDeadZone) / 2;
inline uint32_t PadStickButtons(int lx, int ly, int rx, int ry)
{
    uint32_t m = 0;
    if (ly > kPadStickPast) m |= kPadLSUp;
    if (ly < -kPadStickPast) m |= kPadLSDown;
    if (lx < -kPadStickPast) m |= kPadLSLeft;
    if (lx > kPadStickPast) m |= kPadLSRight;
    if (ry > kPadStickPast) m |= kPadRSUp;
    if (ry < -kPadStickPast) m |= kPadRSDown;
    if (rx < -kPadStickPast) m |= kPadRSLeft;
    if (rx > kPadStickPast) m |= kPadRSRight;
    return m;
}

// Pad buttons by name, as the SnowRunner Tire Inflation System writes them: "LB+DPadDown", "None", and the sticks'
// directions ("LB+RSUp"). The pad is only read here, never hidden from the game.
static const int kPadButtonCount = 22;
static const struct { const char *name; uint32_t mask; } kPadButtons[kPadButtonCount] = {
    { "DPadUp", XINPUT_GAMEPAD_DPAD_UP }, { "DPadDown", XINPUT_GAMEPAD_DPAD_DOWN }, { "DPadLeft", XINPUT_GAMEPAD_DPAD_LEFT },
    { "DPadRight", XINPUT_GAMEPAD_DPAD_RIGHT }, { "Start", XINPUT_GAMEPAD_START }, { "Back", XINPUT_GAMEPAD_BACK },
    { "LS", XINPUT_GAMEPAD_LEFT_THUMB }, { "RS", XINPUT_GAMEPAD_RIGHT_THUMB }, { "LB", XINPUT_GAMEPAD_LEFT_SHOULDER },
    { "RB", XINPUT_GAMEPAD_RIGHT_SHOULDER }, { "A", XINPUT_GAMEPAD_A }, { "B", XINPUT_GAMEPAD_B }, { "X", XINPUT_GAMEPAD_X },
    { "Y", XINPUT_GAMEPAD_Y }, { "LSUp", kPadLSUp }, { "LSDown", kPadLSDown }, { "LSLeft", kPadLSLeft }, { "LSRight", kPadLSRight },
    { "RSUp", kPadRSUp }, { "RSDown", kPadRSDown }, { "RSLeft", kPadRSLeft }, { "RSRight", kPadRSRight } };

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
    static const int order[kPadButtonCount] = { 8, 9, 6, 7, 14, 15, 16, 17, 18, 19, 20, 21, 4, 5, 0, 1, 2, 3, 10, 11, 12, 13 };
    out[0] = 0;
    for (int i : order)
        if ((mask & kPadButtons[i].mask) && strlen(out) + strlen(kPadButtons[i].name) + 2 <= size) // a name that does not fit is left out
        {
            if (out[0]) strncat_s(out, size, "+", _TRUNCATE);
            strncat_s(out, size, kPadButtons[i].name, _TRUNCATE);
        }
    if (!out[0]) strncpy_s(out, size, "None", _TRUNCATE);
}

// A virtual key's name as the tab shows it; "None" for 0.
inline void KeyName(int vk, char *out, size_t size)
{
    static const struct { int vk; const char *name; } names[] = {
        { VK_INSERT, "Insert" }, { VK_DELETE, "Delete" }, { VK_HOME, "Home" }, { VK_END, "End" }, { VK_PRIOR, "Page Up" },
        { VK_NEXT, "Page Down" }, { VK_UP, "Up" }, { VK_DOWN, "Down" }, { VK_LEFT, "Left" }, { VK_RIGHT, "Right" },
        { VK_PAUSE, "Pause" }, { VK_SCROLL, "Scroll Lock" }, { VK_TAB, "Tab" }, { VK_SPACE, "Space" }, { VK_BACK, "Backspace" },
        { VK_RETURN, "Enter" }, { VK_ADD, "Num +" }, { VK_SUBTRACT, "Num -" }, { VK_MULTIPLY, "Num *" }, { VK_DIVIDE, "Num /" }, { VK_DECIMAL, "Num ." } };
    if (vk <= 0) strncpy_s(out, size, "None", _TRUNCATE);
    else if (vk >= VK_F1 && vk <= VK_F24) _snprintf_s(out, size, _TRUNCATE, "F%d", vk - VK_F1 + 1);
    else if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) _snprintf_s(out, size, _TRUNCATE, "%c", (char)vk);
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) _snprintf_s(out, size, _TRUNCATE, "Num %d", vk - VK_NUMPAD0);
    else
    {
        _snprintf_s(out, size, _TRUNCATE, "Key %d", vk);
        for (const auto &n : names) if (n.vk == vk) strncpy_s(out, size, n.name, _TRUNCATE);
    }
}
