// Which axles have air (BRIEF_air_axles.md part 2, PLANNED 100; BRIEF_air_axle_ticks.md) and the swing axles' mark
// (101, tonight's part).
//
// The game's files carry no air mark (review\air_axles_part1.md): a suspension set gives only a template and a menu
// name, and its lines split only by front and rear. The game's airbags are only a look. So the player ticks the ends
// with air on the Suspension tab: "Air" beside Ride front (the front axle) and beside Ride rear (every other axle).
// The ticks are a mask, AirAxleMask (bit 0 the front, bit 1 the rear), kept with the suspension set in the truck's
// [Suspension] or [Suspension:<id>] in Gauges\trucks, never in Gauges.ini. Until a truck has a mask of its own, the
// start order gives one: the truck's own saved mask, else the kind's, else AirAxles=all|rear|none|front in [Truck] of
// the truck's file, else the mod's table of the vanilla trucks whose real models have air at the rear only or none at
// all (the reasons and their sources are in review\, not here), else none ticked: on a truck the mod knows nothing
// about, nothing moves until the player ticks it.
//
// Rear means every axle but the front one, by place (SuspPairs' front[i], the split RideFront and RideRear use):
// pushers and middle axles count as rear. Only the levelling (AirSuspension), AirHorizon and the air's height keys
// follow the mask; RideFront, RideRear, the sway bars, the springs and the dampers stay on every axle.
#pragma once
#include <math.h>
#include <string.h>

enum { kAirAll, kAirRear, kAirNone, kAirFront, kAirKinds };
static const char *const kAirKindNames[kAirKinds] = { "all", "rear", "none", "front" };
// the mask's bits: the front axle, every other axle
enum { kAirMaskFront = 1, kAirMaskRear = 2, kAirMaskAll = 3 };
// where a truck's mask came from: the start order's five sources (none ticked, the table, AirAxles=, the kind's saved
// mask, this truck's), and a tick not saved (no suspension set in force to keep it in)
enum { kAirFromNone, kAirFromTable, kAirFromKey, kAirFromKind, kAirFromTruck, kAirFromTicked };

// A kind's mask: all both bits, rear the rear's, front the front's, none 0.
inline int AirMaskOfKind(int kind) { return kind == kAirAll ? kAirMaskAll : kind == kAirRear ? kAirMaskRear : kind == kAirFront ? kAirMaskFront : 0; }

struct AirAxleRow { const char *key; int kind; };
// review\air_axles_part1.md with the lead's call of 09:48 (the Kodiak C70 and the TerraPro to rear): none 13, rear 19,
// all 4. Kept sorted by key.
static const AirAxleRow kAirAxleTable[] = {
    { "cat_745c", kAirNone },
    { "cat_770g", kAirNone },
    { "cat_ct680", kAirRear },
    { "cat_ct681", kAirRear },
    { "cat_th357", kAirNone },
    { "chevrolet_ck1500", kAirNone },
    { "chevrolet_kodiakc70", kAirRear },
    { "chevy_apache", kAirNone },
    { "earthroamer_lti", kAirAll },
    { "earthroamer_sx", kAirAll },
    { "ford_clt9000", kAirRear },
    { "ford_f750", kAirRear },
    { "freightliner_114sd", kAirRear },
    { "freightliner_fld120", kAirRear },
    { "hummer_h2", kAirRear },
    { "international_hx_520", kAirRear },
    { "international_scout_800", kAirNone },
    { "jeep_cj7_renegade", kAirNone },
    { "jeep_wrangler", kAirNone },
    { "kenworth_c500", kAirRear },
    { "kenworth_t880", kAirRear },
    { "kenworth_w990", kAirRear },
    { "land_rover_defender_110", kAirNone },
    { "land_rover_defender_90", kAirNone },
    { "mack_defense_m917", kAirRear },
    { "mack_pinnacle", kAirRear },
    { "mack_terra_pro", kAirRear },
    { "mercedes_benz_zetros_6x6", kAirNone },
    { "tatra_805", kAirNone },
    { "tatra_force_t815_7", kAirAll },
    { "tatra_phoenix", kAirAll },
    { "tatra_t813", kAirNone },
    { "western_star_49x", kAirRear },
    { "western_star_57x", kAirRear },
    { "western_star_6900xd", kAirRear },
    { "ws_4964_white", kAirRear },
};
static const int kAirAxleRows = (int)(sizeof kAirAxleTable / sizeof kAirAxleTable[0]);

// all, rear, none or front from a key's text, in any case and with spaces around ("Rear " is rear); -1 where it is none of them.
inline int AirKindOf(const char *text)
{
    if (!text) return -1;
    while (*text == ' ' || *text == '\t') text++;
    char word[8] = {};
    int n = 0;
    for (; text[n] && text[n] != ' ' && text[n] != '\t' && n < 7; n++) word[n] = (char)(text[n] >= 'A' && text[n] <= 'Z' ? text[n] - 'A' + 'a' : text[n]);
    for (const char *p = text + n; *p; p++)
        if (*p != ' ' && *p != '\t') return -1;
    for (int k = 0; k < kAirKinds; k++)
        if (!strcmp(word, kAirKindNames[k])) return k;
    return -1;
}

// The table's row for a truck's kind, -1 where it isn't listed.
inline int AirTableKind(const char *key)
{
    if (!key || !key[0]) return -1;
    for (int i = 0; i < kAirAxleRows; i++)
        if (!strcmp(kAirAxleTable[i].key, key)) return kAirAxleTable[i].kind;
    return -1;
}

// A truck's mask by the start order: the saved one (saved, -1 none; savedFrom kAirFromTruck, kAirFromKind or
// kAirFromTicked), else its file's AirAxles (fileKind, AirKindOf of its text, -1 where it has none or one that is no
// kind), else the table's, else none ticked. from: where it came from.
inline int AirMaskOf(const char *key, int fileKind, int saved, int savedFrom, int &from)
{
    if (saved >= 0) return from = savedFrom == kAirFromTruck || savedFrom == kAirFromTicked ? savedFrom : kAirFromKind, saved & kAirMaskAll;
    if (fileKind >= 0 && fileKind < kAirKinds) return from = kAirFromKey, AirMaskOfKind(fileKind);
    const int listed = AirTableKind(key);
    if (listed >= 0) return from = kAirFromTable, AirMaskOfKind(listed);
    return from = kAirFromNone, 0;
}

// Each wheel's air by the mask: the front axle's wheels (SuspPairs' frontmost axle) by its front bit, every other by
// its rear bit.
inline void AirAxleWheels(int n, const bool *front, int mask, bool *air)
{
    for (int i = 0; i < n; i++) air[i] = (mask & (front[i] ? kAirMaskFront : kAirMaskRear)) != 0;
}

// The swing axle's mark (101): the wheel's CamberSuspensionMultiplier (wheel object + 0x8C) over 1 with its
// CamberAnglePhysics (+ 0x88, radians) at 0. The physics wheel then stays upright and the camber lives in the look,
// following the travel by the multiplier. The Mamute 1519's 0.30 with a physics camber of 1° is a bogie, not this.
static const uint32_t kWheelCamberPhysics = 0x88, kWheelCamberMultiplier = 0x8C;
inline bool SwingMark(float multiplier, float physics) { return isfinite(multiplier) && isfinite(physics) && multiplier > 1.0f && fabsf(physics) < 1e-6f; }
