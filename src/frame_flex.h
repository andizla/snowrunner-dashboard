// frame_flex.h: Frame flex and Twist range (BRIEF_frame_flex.md part 2). A truck whose frame twists has a Havok
// limited hinge about its long axis between two sections of the frame, with a spring-damper motor: the truck file's
// <Constraint Type="Hinge" AxisLocal="(1; 0; 0)"> with <Motor Type="Spring">. Frame flex scales the motor's spring
// (and its damping by the square root, so the damping ratio stays the file's), Twist range scales the hinge's limits.
//
// The way to the joint (review\frame_flex_chain.md, the same in the images of 6ab52f5d and 6a607c05, from Havok's own
// reflection tables and the game's code):
//   wheel object + 0x18          the wheel's Havok body (an hkpEntity)
//   wheel object + 0x28          its wheel constraint's data (suspension_scan.h)
//   body + 0xF8, u16 + 0x100     constraintsMaster: 0x50 byte elements, the constraint instance at + 0, its bodies at + 8, + 0x10
//   body + 0x108, int + 0x110    constraintsSlave: constraint instance pointers
//   instance + 0x18              the constraint's data; + 0x28 and + 0x30 its two bodies
//   hinge data + 0x30            the hinge's axis in its first body's frame (Havok's transformA, its first column)
//   hinge data + 0xC8            its motor; + 0xF8 and + 0xFC its limits in radians (the game's parser stores them there)
//   motor + 0x10                 type, 3 for the spring-damper; + 0x20 the spring, + 0x24 the damping
// A front wheel's own constraint gives the body it hangs on (the frame's front section). The frame's joint is the
// hinge on that body whose other body carries wheels too: the chassis, another wheel's body, or a body joined to one
// of those (the articulated trucks' roll hinge sits beside their steering pivot). So a cab mount (a body with no
// wheels) and a steering pivot (an axis up, not along) are never taken. Every pointer is checked by its vtable on each
// pass, and every read goes through TruckMemory, which fails on a page that is not there.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "truck.h"

static const char *const kFrameHingeName = ".?AVhkpLimitedHingeConstraintData@@";
static const char *const kFrameMotorName = ".?AVhkpSpringDamperConstraintMotor@@";
static const uint32_t kFrameWheelBody = 0x18, kFrameWheelData = 0x28;
static const uint32_t kFrameMaster = 0xF8, kFrameMasterCount = 0x100, kFrameMasterStep = 0x50, kFrameSlave = 0x108, kFrameSlaveCount = 0x110;
static const uint32_t kFrameInstData = 0x18, kFrameInstBodyA = 0x28, kFrameInstBodyB = 0x30;
static const uint32_t kFrameHingeAxis = 0x30, kFrameHingeMotor = 0xC8, kFrameHingeMin = 0xF8, kFrameHingeMax = 0xFC;
static const uint32_t kFrameMotorType = 0x10, kFrameMotorSpring = 0x20, kFrameMotorDamping = 0x24;
static const uint8_t kFrameSpringDamper = 3;
static const int kFrameListMost = 64; // constraints read of one body's two lists
// The widest twist a slider may give: 175 degrees, the widest absolute limit in RLM's spread (the user's word allows
// the range), as no hinge may reach 180. A file's own wider limit is never cut.
static const float kFrameLimitMost = 175.0f * 3.14159265f / 180.0f;

struct FrameVtables { uint64_t wheelData, hinge, motor; };

// The constraint instances in a body's two lists, each once. Returns how many.
inline int FrameBodyConstraints(const TruckMemory &m, uint64_t body, uint64_t *out, int most)
{
    int n = 0;
    uint64_t list = 0;
    uint16_t masters = 0;
    int slaves = 0;
    auto add = [&](uint64_t inst) {
        if (inst < 0x10000 || n >= most) return;
        for (int i = 0; i < n; i++)
            if (out[i] == inst) return;
        out[n++] = inst;
    };
    if (m.get(body + kFrameMaster, list) && m.get(body + kFrameMasterCount, masters) && masters <= kFrameListMost)
        for (int i = 0; i < masters; i++)
        {
            uint64_t inst = 0;
            if (m.get(list + (uint64_t)i * kFrameMasterStep, inst)) add(inst);
        }
    if (m.get(body + kFrameSlave, list) && m.get(body + kFrameSlaveCount, slaves) && slaves >= 0 && slaves <= kFrameListMost)
        for (int i = 0; i < slaves; i++)
        {
            uint64_t inst = 0;
            if (m.get(list + (uint64_t)i * 8, inst)) add(inst);
        }
    return n;
}

// The instance's body other than body, 0 where body is neither of its two.
inline uint64_t FrameOtherBody(const TruckMemory &m, uint64_t inst, uint64_t body)
{
    uint64_t a = 0, b = 0;
    if (!m.get(inst + kFrameInstBodyA, a) || !m.get(inst + kFrameInstBodyB, b)) return 0;
    return a == body ? b : b == body ? a : 0;
}

// What the finder found, and the steps it took (for the log).
struct FrameJoint
{
    uint64_t instance, data, motor;
    uint64_t front, other;       // the body the front wheel hangs on, and the hinge's other body
    int wheel;                   // the wheel whose body led there
    float axis[3];               // the hinge's axis in its first body's frame
    int hung;                    // wheels whose own constraint was found
    int frontOnChassis;          // of them, the wheels that hang on the chassis itself
    int refused;                 // spring hinges on a wheel's body that were not the frame's (an axis not along, or no wheels beyond)
};

// The axis along the truck: the hinge's first column mostly x (the long axis of the game's truck frames).
inline bool FrameAxisAlong(const float a[3])
{
    const float len = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    return len > 0.5f && len < 2.0f && fabsf(a[0]) >= 0.7f * len;
}

// The driven truck's frame joint. False where it has none (a rigid frame) or a step is not there; j says how far it got.
inline bool FrameJointFind(const TruckMemory &m, const TruckRefs &r, const FrameVtables &vt, FrameJoint &j)
{
    j = FrameJoint{};
    j.wheel = -1;
    if (!vt.wheelData || !vt.hinge || !vt.motor || !r.chassis) return false;
    uint64_t hang[kMaxWheels] = {};
    const int wheels = r.wheels < kMaxWheels ? r.wheels : kMaxWheels;
    uint64_t list[kFrameListMost];
    for (int i = 0; i < wheels; i++)
    {
        uint64_t body = 0, data = 0, v = 0;
        if (!m.get(r.wheel[i] + kFrameWheelBody, body) || !m.get(r.wheel[i] + kFrameWheelData, data) || !m.get(data, v) || v != vt.wheelData) continue;
        const int n = FrameBodyConstraints(m, body, list, kFrameListMost);
        for (int k = 0; k < n; k++)
        {
            uint64_t d = 0;
            if (m.get(list[k] + kFrameInstData, d) && d == data)
            {
                hang[i] = FrameOtherBody(m, list[k], body);
                break;
            }
        }
        if (!hang[i]) continue;
        j.hung++;
        if (hang[i] == r.chassis) j.frontOnChassis++;
    }
    auto carrier = [&](uint64_t b) {
        if (b == r.chassis) return true;
        for (int i = 0; i < wheels; i++)
            if (hang[i] == b) return true;
        return false;
    };
    // a body carries wheels: it holds some, or it is joined to one that does (other than through the section asked from)
    auto carries = [&](uint64_t b, uint64_t from) {
        if (carrier(b)) return true;
        uint64_t joined[kFrameListMost];
        const int n = FrameBodyConstraints(m, b, joined, kFrameListMost);
        for (int k = 0; k < n; k++)
        {
            const uint64_t o = FrameOtherBody(m, joined[k], b);
            if (o && o != from && carrier(o)) return true;
        }
        return false;
    };
    for (int i = 0; i < wheels; i++)
    {
        const uint64_t h = hang[i];
        if (!h || h == r.chassis) continue;
        bool seen = false;
        for (int k = 0; k < i; k++) seen = seen || hang[k] == h;
        if (seen) continue;
        const int n = FrameBodyConstraints(m, h, list, kFrameListMost);
        for (int k = 0; k < n; k++)
        {
            uint64_t data = 0, motor = 0, v = 0;
            uint8_t type = 0;
            if (!m.get(list[k] + kFrameInstData, data) || !m.get(data, v) || v != vt.hinge) continue;
            if (!m.get(data + kFrameHingeMotor, motor) || !m.get(motor, v) || v != vt.motor || !m.get(motor + kFrameMotorType, type) || type != kFrameSpringDamper) continue;
            float axis[3] = {};
            const uint64_t o = FrameOtherBody(m, list[k], h);
            if (!o || !m.get(data + kFrameHingeAxis, axis) || !FrameAxisAlong(axis) || !carries(o, h))
            {
                j.refused++;
                continue;
            }
            j.instance = list[k], j.data = data, j.motor = motor, j.front = h, j.other = o, j.wheel = i;
            memcpy(j.axis, axis, sizeof axis);
            return true;
        }
    }
    return false;
}

// ---- the tune: the spring, the damping and the two limits as the stock times the sliders ----
enum { kFrameSpring, kFrameDamping, kFrameMin, kFrameMax, kFrameValues };

struct FrameTune
{
    uint64_t vehicle, key;       // the truck, and its wheels' constraints when the joint was looked for
    bool looked, found;
    FrameJoint joint;
    uint64_t motor, data;        // where the stock was read, 0 = not yet
    float stock[kFrameValues];
    float wrote[kFrameValues];
    bool ours[kFrameValues];
    bool fresh;                  // a stock was read on this pass (for the log)
    bool wroteNow;
    float back[kFrameValues];    // read back after a write
};

inline uint64_t FrameValueAt(const FrameTune &t, int k)
{
    return k == kFrameSpring ? t.motor + kFrameMotorSpring : k == kFrameDamping ? t.motor + kFrameMotorDamping : k == kFrameMin ? t.data + kFrameHingeMin : t.data + kFrameHingeMax;
}

// A stock the tune may scale: Havok's spring and damping from 0 to 1e9 (the parser's clamp), a limit on its side of 0.
inline bool FrameValueSane(int k, float v)
{
    if (k == kFrameMin) return v <= 0.0f && v > -3.1416f;
    if (k == kFrameMax) return v >= 0.0f && v < 3.1416f;
    return v >= 0.0f && v <= 1e9f;
}

inline bool FrameSameWord(float a, float b) { return !memcmp(&a, &b, sizeof a); }

// What the sliders give for a stock: the spring times flex, the damping times its square root, each limit times range
// and held to kFrameLimitMost, never below the stock's own width.
inline float FrameWant(int k, float stock, float flex, float range)
{
    if (k == kFrameSpring) return flex == 1.0f ? stock : fminf(1e9f, stock * flex);
    if (k == kFrameDamping) return flex == 1.0f ? stock : fminf(1e9f, stock * sqrtf(flex));
    if (range == 1.0f) return stock;
    const float most = fmaxf(kFrameLimitMost, fabsf(stock));
    const float v = stock * range;
    return k == kFrameMin ? fmaxf(-most, v) : fminf(most, v);
}

// The stock put back where the tune wrote, where the joint is still that class and the value still the tune's.
inline int FrameTuneRestore(FrameTune &t, const TruckMemory &m, const FrameVtables &vt)
{
    int restored = 0;
    uint64_t v = 0;
    const bool there = t.motor && t.data && m.get(t.motor, v) && v == vt.motor && m.get(t.data, v) && v == vt.hinge;
    for (int k = 0; k < kFrameValues; k++)
    {
        float now = 0.0f;
        if (there && t.ours[k] && m.get(FrameValueAt(t, k), now) && FrameSameWord(now, t.wrote[k]) && m.put(FrameValueAt(t, k), t.stock[k])) restored++;
        t.ours[k] = false;
    }
    return restored;
}

// The key of the truck's wheels' constraints: a rebuild (garage, recovery) gives new ones, and the joint is looked for again.
inline uint64_t FrameKey(const TruckMemory &m, const TruckRefs &r)
{
    uint64_t key = r.vehicle;
    for (int i = 0; i < r.wheels && i < kMaxWheels; i++)
    {
        uint64_t d = 0;
        m.get(r.wheel[i] + kFrameWheelData, d);
        key = key * 31 + d;
    }
    return key;
}

// One pass for the driven truck. Returns true where it wrote. restored: values put back on the truck driven before.
// Nothing is written at flex 1 and range 1, but the stock goes back where the tune wrote.
inline bool FrameTuneStep(FrameTune &t, const TruckMemory &m, const TruckRefs &r, const FrameVtables &vt, float flex, float range, int *restored = nullptr)
{
    if (restored) *restored = 0;
    t.fresh = t.wroteNow = false;
    if (t.vehicle != r.vehicle)
    {
        const int back = FrameTuneRestore(t, m, vt);
        if (restored) *restored = back;
        t = FrameTune{};
        t.vehicle = r.vehicle;
    }
    const uint64_t key = FrameKey(m, r);
    uint64_t v = 0;
    const bool there = t.found && m.get(t.joint.motor, v) && v == vt.motor && m.get(t.joint.data, v) && v == vt.hinge;
    if (!t.looked || key != t.key || (t.found && !there))
    {
        t.looked = true;
        t.key = key;
        t.found = FrameJointFind(m, r, vt, t.joint);
    }
    if (!t.found) return false;
    float now[kFrameValues] = {};
    t.motor = t.motor == t.joint.motor && t.data == t.joint.data ? t.motor : 0;
    const uint64_t motor = t.joint.motor, data = t.joint.data;
    bool read = m.get(motor + kFrameMotorSpring, now[kFrameSpring]) && m.get(motor + kFrameMotorDamping, now[kFrameDamping]) &&
                m.get(data + kFrameHingeMin, now[kFrameMin]) && m.get(data + kFrameHingeMax, now[kFrameMax]);
    if (!read) return false;
    if (!t.motor)
    {
        // a fresh joint: its values as the game built them are the stock (never a value the tune wrote)
        t.motor = motor, t.data = data;
        memcpy(t.stock, now, sizeof now);
        memset(t.ours, 0, sizeof t.ours);
        t.fresh = true;
    }
    else
        for (int k = 0; k < kFrameValues; k++)
            if (!(t.ours[k] && FrameSameWord(now[k], t.wrote[k])) && !FrameSameWord(now[k], t.stock[k]))
            {
                // the game wrote this one anew: its new stock
                t.stock[k] = now[k];
                t.ours[k] = false;
                t.fresh = true;
            }
    for (int k = 0; k < kFrameValues; k++)
    {
        if (!FrameValueSane(k, t.stock[k])) continue;
        const float want = FrameWant(k, t.stock[k], flex, range);
        const bool tuned = !FrameSameWord(want, t.stock[k]);
        if (FrameSameWord(now[k], want) || (!tuned && !t.ours[k])) continue;
        if (!m.get(motor, v) || v != vt.motor || !m.get(data, v) || v != vt.hinge) break;
        if (!m.put(FrameValueAt(t, k), want)) continue;
        t.wrote[k] = want;
        t.ours[k] = tuned;
        t.wroteNow = true;
    }
    if (t.wroteNow)
        for (int k = 0; k < kFrameValues; k++) m.get(FrameValueAt(t, k), t.back[k]);
    return t.wroteNow;
}
