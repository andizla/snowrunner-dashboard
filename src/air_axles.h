// Which axles have air (BRIEF_air_axles.md part 2, PLANNED 100) and the swing axles' mark (101, tonight's part).
//
// The game's files carry no air mark (review\air_axles_part1.md): a suspension set gives only a template and a menu
// name, and its lines split only by front and rear. So the mod keeps its own table of the vanilla trucks whose real
// models have air at the rear only or none at all, by the truck's kind (the key Gauges\trucks\<kind>.ini uses). The
// reasons and their sources are in review\, not here. A truck the table doesn't list (every mod truck, and every
// vanilla truck no source settled) has air on every axle, as before. AirAxles=all|rear|none in [Truck] of the
// truck's file wins over the table.
//
// Rear means every axle but the front one, by place (SuspPairs' front[i], the split RideFront and RideRear use):
// pushers and middle axles count as rear. Only the levelling (AirSuspension), AirHorizon and the air's height keys
// follow it; RideFront, RideRear, the sway bars, the springs and the dampers stay on every axle.
#pragma once
#include <math.h>
#include <string.h>

enum { kAirAll, kAirRear, kAirNone, kAirKinds };
static const char *const kAirKindNames[kAirKinds] = { "all", "rear", "none" };
// where a truck's figure came from
enum { kAirFromNotListed, kAirFromTable, kAirFromKey };

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

// all, rear or none from a key's text, in any case and with spaces around ("Rear " is rear); -1 where it is none of them.
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

// A truck's air axles: its key's (fileKey, the AirAxles text from its file, nullptr or "" where it has none, and one
// that is no kind is no key), else the table's, else all. from: where it came from.
inline int AirAxlesOf(const char *key, const char *fileKey, int &from)
{
    const int own = fileKey && fileKey[0] ? AirKindOf(fileKey) : -1;
    if (own >= 0) return from = kAirFromKey, own;
    const int listed = AirTableKind(key);
    if (listed >= 0) return from = kAirFromTable, listed;
    return from = kAirFromNotListed, kAirAll;
}

// Each wheel's air by the kind: all, every wheel but the front axle's (front: SuspPairs' frontmost axle), or none.
inline void AirAxleWheels(int n, const bool *front, int kind, bool *air)
{
    for (int i = 0; i < n; i++) air[i] = kind == kAirAll || (kind == kAirRear && !front[i]);
}

// The swing axle's mark (101): the wheel's CamberSuspensionMultiplier (wheel object + 0x8C) over 1 with its
// CamberAnglePhysics (+ 0x88, radians) at 0. The physics wheel then stays upright and the camber lives in the look,
// following the travel by the multiplier. The Mamute 1519's 0.30 with a physics camber of 1° is a bogie, not this.
static const uint32_t kWheelCamberPhysics = 0x88, kWheelCamberMultiplier = 0x8C;
inline bool SwingMark(float multiplier, float physics) { return isfinite(multiplier) && isfinite(physics) && multiplier > 1.0f && fabsf(physics) < 1e-6f; }
