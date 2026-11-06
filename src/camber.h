// camber.h: Camber gain and Camber at rest (BRIEF_swing_camber.md, from 8jk3's ask for the swing axles). The game draws
// a wheel's tilt from three figures of the truck's wheel file, which its wheel parser stores in the wheel object (the
// object at wheel model + 0x2C8, truck.h): CamberAngleRender at + 0x84 and CamberAnglePhysics at + 0x88, both in
// radians (the file's degrees times 0.01745329, held to -30 and 30), and CamberSuspensionMultiplier at + 0x8C as written.
// The parser is the function holding the three reads at RVA 0xc28340 in 6ab52f5d (0xc268a0 in 6a607c05); its stores
// are at 0xc2917a, 0xc2913d and 0xc291ba (0xc276da, 0xc2769d, 0xc2771a). No sign per side is taken there: the wheel
// file gives one figure for an axle's two wheels. The look's code, the function at 0xc26d60 (0xc252c0), reads + 0x8C
// and + 0x84 from the wheel object on each call and turns the tilt to the wheel's side by the byte at + 0xD0. The
// parser fills the wheel object itself (the same function reads the wheel's Havok body at + 0x18), so no record of
// the file's figures the mod never writes was found.
//
// So the stock is the first read of each wheel object, kept by the object's address in 128 places; a place is given
// up only once its object holds nothing the mod wrote (the review's F1), and with none to give up a new wheel is
// neither kept nor written. A value that is the word the mod
// last wrote to that object is the mod's, so a rebuild that keeps the object and leaves the written value in it never
// becomes a stock (the Reviewer's point 2 on the brief: x2 of the file, never x4). Any other value is the game's new
// stock, read again. A new truck or a new set of wheel objects is a new generation (its wheels' constraints too, as
// a garage or a recovery gives new ones). On the pass after a write the value is read again: one back at the stock
// means the game sets it from elsewhere, and the mod writes it no more on that wheel until a new generation.
//
// Camber gain is a factor on the stock multiplier, 0 to 3 (0 keeps the wheel at its rest tilt as it travels). Camber
// at rest is an offset in degrees on the stock render angle, -10 to 10, with the stock's own sign. Both are per end:
// the front axle (SuspPairs' front, as RideFront splits) and every other axle. At x1 and +0 nothing is written; a
// value the mod wrote goes back to the stock. CamberAnglePhysics is never written: the physics wheel stays as the game
// has it. A truck left keeps what its set wrote; it is never written once it is not driven.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "truck.h"
#include "air_axles.h"
#include "suspension_scan.h"

static const uint32_t kWheelCamberRender = 0x84;
enum { kCamberRender, kCamberGain, kCamberValues };
static const uint32_t kCamberAt[kCamberValues] = { kWheelCamberRender, kWheelCamberMultiplier };
static const float kCamberRad = 3.14159265f / 180.0f;
static const float kCamberRenderMost = 30.0f * 3.14159265f / 180.0f; // the parser's own bounds
static const int kCamberKept = 128;                                  // wheel objects whose stock is kept

struct CamberWheel
{
    uint64_t object;              // 0: a free place
    uint32_t used;                // the pass it was last seen on, for the oldest to go first
    float stock[kCamberValues];   // as the game built it
    float wrote[kCamberValues];   // the word last written
    bool ours[kCamberValues];     // the value there is the mod's (the word written, a factor not 1 or an offset not 0)
    bool check[kCamberValues];    // written last pass: read it again on this one
    bool refused[kCamberValues];  // the game put the stock back after a write: not written again this generation
    float physics;                // CamberAnglePhysics as read, for the log
};

struct CamberTune
{
    CamberWheel kept[kCamberKept];
    uint32_t pass;
    uint64_t vehicle, key;        // the truck and its wheels' objects and constraints the generation is of
    uint32_t generation;          // up on a new truck, new wheel objects or new constraints
    int wheels;
    int at[kMaxWheels];           // each driven wheel's place in kept, -1 where it is not a wheel object
    bool front[kMaxWheels];       // the front axle's wheels
    bool newGeneration;           // this pass began one (for the log of the file's figures)
    bool fresh;                   // a stock was read on this pass
    int wroteNow;                 // values written on this pass
    int putBack;                  // values the game put back at the stock the pass after a write
    int unkept;                   // wheels on this pass with no place to keep (every place holds the mod's word)
    bool saidFull;                // that was logged
};

inline bool CamberSame(float a, float b) { return !memcmp(&a, &b, sizeof a); }

// A stock the sliders may change: a render angle within the parser's bounds, a multiplier from 0 to 10.
inline bool CamberSane(int k, float v)
{
    if (!isfinite(v)) return false;
    return k == kCamberRender ? fabsf(v) <= kCamberRenderMost + 1e-4f : v >= 0.0f && v <= 10.0f;
}

// What a slider gives for a stock: the multiplier times the gain; the render angle plus the offset in degrees, with
// the stock's sign (a stock below 0 takes the offset below 0, so a side the game holds negative keeps its mirror),
// held to the parser's bounds. x1 and +0 give the stock itself.
inline float CamberWant(int k, float stock, float gain, float restDeg)
{
    if (k == kCamberGain) return gain == 1.0f ? stock : stock * gain;
    if (restDeg == 0.0f) return stock;
    const float v = stock + restDeg * kCamberRad * (stock < 0.0f ? -1.0f : 1.0f);
    return fmaxf(-kCamberRenderMost, fminf(kCamberRenderMost, v));
}

// Whether a kept place's object still holds the mod's word: still the wheel class, and a value the mod wrote still
// there. Such a place is never given up, or the word would be read as a stock when the truck is driven again (the
// review's F1: x2 left on a truck, 128 other wheels, then x4).
inline bool CamberHolds(const CamberWheel &w, const TruckMemory &m, uint64_t wheelClass)
{
    if (!w.ours[kCamberRender] && !w.ours[kCamberGain]) return false;
    uint64_t cls = 0;
    if (!m.get(w.object, cls) || cls != wheelClass) return false;
    for (int k = 0; k < kCamberValues; k++)
    {
        float now = 0.0f;
        if (w.ours[k] && m.get(w.object + kCamberAt[k], now) && CamberSame(now, w.wrote[k])) return true;
    }
    return false;
}

// The kept place of a wheel object. Where it has none: a free place, else the oldest place not seen on this pass whose
// object holds nothing of the mod's. -1 where every place still holds the mod's word: the wheel is then neither kept
// nor written.
inline int CamberPlace(CamberTune &t, const TruckMemory &m, uint64_t wheelClass, uint64_t object, bool &isNew)
{
    isNew = false;
    int free = -1, oldest = -1;
    for (int i = 0; i < kCamberKept; i++)
    {
        const CamberWheel &w = t.kept[i];
        if (w.object == object) return i;
        if (!w.object)
        {
            if (free < 0) free = i;
            continue;
        }
        if (w.used == t.pass || (oldest >= 0 && w.used >= t.kept[oldest].used) || CamberHolds(w, m, wheelClass)) continue;
        oldest = i;
    }
    const int at = free >= 0 ? free : oldest;
    if (at < 0) return -1;
    isNew = true;
    t.kept[at] = CamberWheel{};
    t.kept[at].object = object;
    return at;
}

// The stock of a driven wheel (k kCamberRender or kCamberGain), false where it is not read. The swing mark reads this,
// never the live value, so a written gain of x0 is still a swing axle (the Reviewer's point 4).
inline bool CamberStock(const CamberTune &t, int wheel, int k, float &v)
{
    if (wheel < 0 || wheel >= t.wheels || t.at[wheel] < 0) return false;
    v = t.kept[t.at[wheel]].stock[k];
    return true;
}

// The key of a truck's wheels: their objects and their constraints (a rebuild gives new ones).
inline uint64_t CamberKey(const TruckMemory &m, const TruckRefs &r)
{
    uint64_t key = r.vehicle;
    for (int i = 0; i < r.wheels && i < kMaxWheels; i++)
    {
        uint64_t c = 0;
        m.get(r.wheel[i] + kSuspConstraintAt, c);
        key = key * 31 + r.wheel[i];
        key = key * 31 + c;
    }
    return key;
}

// One pass for the driven truck: gain[0] and rest[0] for the front axle, [1] for every other; front each wheel's end;
// wheelClass the wheel object's vtable (a wheel whose first word is another is not touched). Returns the values written.
inline int CamberTuneStep(CamberTune &t, const TruckMemory &m, const TruckRefs &r, const bool *front, const float gain[2], const float rest[2], uint64_t wheelClass)
{
    t.pass++;
    t.fresh = t.newGeneration = false;
    t.wroteNow = t.putBack = t.unkept = 0;
    const uint64_t key = CamberKey(m, r);
    if (t.vehicle != r.vehicle || t.key != key)
    {
        t.vehicle = r.vehicle;
        t.key = key;
        t.generation++;
        t.newGeneration = true;
        // a rebuild may set the stock again: no read back across it, and a refused value is tried again
        for (int i = 0; i < kCamberKept; i++)
        {
            memset(t.kept[i].refused, 0, sizeof t.kept[i].refused);
            memset(t.kept[i].check, 0, sizeof t.kept[i].check);
        }
    }
    t.wheels = r.wheels < kMaxWheels ? r.wheels : kMaxWheels;
    for (int i = 0; i < t.wheels; i++)
    {
        t.at[i] = -1;
        t.front[i] = front && front[i];
        uint64_t cls = 0;
        float now[kCamberValues] = {}, physics = 0.0f;
        if (!m.get(r.wheel[i], cls) || cls != wheelClass || !m.get(r.wheel[i] + kWheelCamberRender, now[kCamberRender]) ||
            !m.get(r.wheel[i] + kWheelCamberMultiplier, now[kCamberGain]) || !m.get(r.wheel[i] + kWheelCamberPhysics, physics))
            continue;
        bool isNew = false;
        const int at = CamberPlace(t, m, wheelClass, r.wheel[i], isNew);
        if (at < 0)
        {
            t.unkept++; // nowhere to keep its stock: neither read as one nor written
            continue;
        }
        t.at[i] = at;
        CamberWheel &w = t.kept[at];
        w.used = t.pass;
        w.physics = physics;
        const int end = t.front[i] ? 0 : 1;
        for (int k = 0; k < kCamberValues; k++)
        {
            if (w.check[k])
            {
                // the read after a write: back at the stock, the game sets it from elsewhere (the Reviewer's point 3)
                w.check[k] = false;
                if (w.ours[k] && !CamberSame(now[k], w.wrote[k]) && CamberSame(now[k], w.stock[k]))
                {
                    w.ours[k] = false;
                    w.refused[k] = true;
                    t.putBack++;
                }
            }
            if (isNew)
            {
                // a fresh wheel object: its value as the game built it
                w.stock[k] = now[k];
                w.ours[k] = false;
                t.fresh = true;
            }
            else if (w.ours[k] && CamberSame(now[k], w.wrote[k])) {} // the mod's own word, left there: the stock stays
            else if (!CamberSame(now[k], w.stock[k]))
            {
                // the game wrote this one anew: its new stock
                w.stock[k] = now[k];
                w.ours[k] = false;
                t.fresh = true;
            }
            else w.ours[k] = false;
            if (!CamberSane(k, w.stock[k]) || w.refused[k]) continue;
            const float want = CamberWant(k, w.stock[k], gain[end], rest[end]);
            const bool tuned = !CamberSame(want, w.stock[k]);
            if (CamberSame(now[k], want) || (!tuned && !w.ours[k])) continue;
            if (!m.get(r.wheel[i], cls) || cls != wheelClass || !m.put(r.wheel[i] + kCamberAt[k], want)) continue;
            w.wrote[k] = want;
            w.ours[k] = tuned;
            w.check[k] = tuned;
            t.wroteNow++;
        }
    }
    return t.wroteNow;
}

// The figures of an end for the tab: the stock of its first wheel (render in degrees, the multiplier). False where the
// truck has no wheel read at that end.
inline bool CamberEndFile(const CamberTune &t, int end, float &renderDeg, float &multiplier)
{
    for (int i = 0; i < t.wheels; i++)
    {
        if ((t.front[i] ? 0 : 1) != end || t.at[i] < 0) continue;
        renderDeg = t.kept[t.at[i]].stock[kCamberRender] / kCamberRad;
        multiplier = t.kept[t.at[i]].stock[kCamberGain];
        return true;
    }
    return false;
}
