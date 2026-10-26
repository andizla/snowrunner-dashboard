// input_bindings.h: the bindings of the mod's own gearbox and the reader of them, without the devices: the actions a
// player binds, a binding and its text form for the ini, one snapshot of the keys, the pad and the wheels and shifters
// (input_devices.cpp fills it from Windows), and from snapshot to snapshot the state of every action (held, pressed
// on this pass, the clutch's travel) and the capture of a new binding. The list of actions is the manual transmission
// mod's (its SMT.ini), without its menu key, AWD, the diff lock and the handbrake, which the game and the NEXT GEN menu
// have. Nothing here reads a device or the game: the tests run it on made up snapshots.
#pragma once
#include "gauges.h"
#include <ctype.h>
#include <stdlib.h>

enum GearAction
{
    kActGear1, kActGear2, kActGear3, kActGear4, kActGear5, kActGear6, kActGear7, kActGear8, kActGear9, kActGear10, kActGear11, kActGear12,
    kActGearH, kActGearLMinus, kActGearL, kActGearLPlus, kActGearN, kActGearR, kActUp, kActDown, kActClutch, kActRangeHigh, kActRangeLow,
    kActCount
};
// Their names, as the ini's keys and the log have them.
static const char *const kActionNames[kActCount] = { "Gear1", "Gear2", "Gear3", "Gear4", "Gear5", "Gear6", "Gear7", "Gear8", "Gear9", "Gear10",
                                                     "Gear11", "Gear12", "GearH", "GearL-", "GearL", "GearL+", "GearN", "GearR", "GearUp",
                                                     "GearDown", "Clutch", "RangeHigh", "RangeLow" };

// A binding: a key (its virtual key code), a pad chord (XInput buttons, PadParse), a wheel's or shifter's button, one
// direction of its POV hat, or one of its axes (the clutch: inverted where the pedal reads backwards). Wheels count
// from 1 in the order Windows lists them; buttons and axes count from 1 too, as a player reads them.
enum BindKind { kBindNone, kBindKey, kBindPad, kBindButton, kBindPov, kBindAxis };
enum { kInputDevicesMost = 8, kInputButtonsMost = 128, kInputPovsMost = 4, kInputAxesMost = 8 };
enum { kPovUp, kPovRight, kPovDown, kPovLeft };
static const char *const kPovNames[4] = { "Up", "Right", "Down", "Left" };
struct Binding
{
    int kind;      // BindKind
    int device;    // the wheel, 1 to kInputDevicesMost (button, POV and axis)
    uint32_t code; // the key, the pad's buttons, the button (1 to 128), the POV (1 to 4) or the axis (1 to 8)
    int direction; // the POV's direction (kPovUp ..)
    bool inverted; // the axis reads backwards: travel is 1 less its value
};
// Each action holds up to two bindings, as a player uses a keyboard and a wheel or a pad.
enum { kBindingSlots = 2 };
struct GearBindings { Binding slot[kActCount][kBindingSlots]; };

// A key's text: its name as the tab shows it (KeyName), or its code where it has none.
inline void KeyText(int vk, char *out, size_t size)
{
    KeyName(vk, out, size);
    if (!strncmp(out, "Key ", 4)) sprintf_s(out, size, "%d", vk);
}

// A key from its text: a name KeyName gives, or a virtual key code (1 to 254). 0 where it is neither.
inline int KeyFromText(const char *text)
{
    char *end = nullptr;
    const long n = strtol(text, &end, 10);
    if (end != text && !*end) return n >= 1 && n <= 254 ? (int)n : 0;
    for (int vk = 1; vk <= 254; vk++)
    {
        char name[32];
        KeyName(vk, name, sizeof name);
        if (strncmp(name, "Key ", 4) && !_stricmp(name, text)) return vk;
    }
    return 0;
}

// A binding's text for the ini: "Key F3", "Key 211", "Pad LB+DPadDown", "Wheel 1 Button 5", "Wheel 1 POV Up" (its second
// hat "Wheel 1 POV 2 Up"), "Wheel 1 Axis 3 inverted", "None".
inline void BindingText(const Binding &b, char *out, size_t size)
{
    char part[64];
    switch (b.kind)
    {
    case kBindKey: KeyText((int)b.code, part, sizeof part); sprintf_s(out, size, "Key %s", part); break;
    case kBindPad: PadName(b.code, part, sizeof part); sprintf_s(out, size, "Pad %s", part); break;
    case kBindButton: sprintf_s(out, size, "Wheel %d Button %u", b.device, b.code); break;
    case kBindPov:
        if (b.code > 1) sprintf_s(out, size, "Wheel %d POV %u %s", b.device, b.code, kPovNames[b.direction & 3]);
        else sprintf_s(out, size, "Wheel %d POV %s", b.device, kPovNames[b.direction & 3]);
        break;
    case kBindAxis: sprintf_s(out, size, "Wheel %d Axis %u%s", b.device, b.code, b.inverted ? " inverted" : ""); break;
    default: strcpy_s(out, size, "None");
    }
}

// A binding from its text, the words in any case. False with why it is no binding (why: a short reason for the log).
inline bool BindingParse(const char *text, Binding &out, char *why, size_t whySize)
{
    out = Binding{};
    why[0] = 0;
    char buf[128];
    strncpy_s(buf, text, _TRUNCATE);
    char *word[8] = {};
    int words = 0;
    char *ctx = nullptr;
    for (char *w = strtok_s(buf, " \t", &ctx); w && words < 8; w = strtok_s(nullptr, " \t", &ctx)) word[words++] = w;
    auto refuse = [&](const char *reason) { strcpy_s(why, whySize, reason); out = Binding{}; return false; };
    auto number = [](const char *w, int lo, int hi) {
        char *end = nullptr;
        const long n = w ? strtol(w, &end, 10) : 0;
        return w && end != w && !*end && n >= lo && n <= hi ? (int)n : 0;
    };
    if (!words) return refuse("empty");
    if (!_stricmp(word[0], "None")) return words == 1 ? true : refuse("nothing may follow None");
    if (!_stricmp(word[0], "Key"))
    {
        if (words < 2) return refuse("Key wants a key's name or code");
        // a name of two words ("Page Up", "Num 3") is joined again
        char name[64] = "";
        for (int i = 1; i < words; i++)
        {
            if (i > 1) strcat_s(name, " ");
            strcat_s(name, word[i]);
        }
        const int vk = KeyFromText(name);
        if (!vk) return refuse("no such key (a name as the tab shows it, or a code from 1 to 254)");
        out.kind = kBindKey;
        out.code = (uint32_t)vk;
        return true;
    }
    if (!_stricmp(word[0], "Pad"))
    {
        if (words != 2) return refuse("Pad wants its buttons joined with + (LB+DPadDown)");
        const uint32_t mask = PadParse(word[1]);
        if (!mask) return refuse("no such pad button (DPadUp, DPadDown, DPadLeft, DPadRight, A, B, X, Y, LB, RB, LS, RS, Start, Back)");
        out.kind = kBindPad;
        out.code = mask;
        return true;
    }
    if (!_stricmp(word[0], "Wheel"))
    {
        const int device = number(word[1], 1, kInputDevicesMost);
        if (!device) return refuse("Wheel wants its number, 1 to 8");
        out.device = device;
        if (words >= 3 && !_stricmp(word[2], "Button"))
        {
            const int button = number(word[3], 1, kInputButtonsMost);
            if (!button || words != 4) return refuse("Button wants its number, 1 to 128");
            out.kind = kBindButton;
            out.code = (uint32_t)button;
            return true;
        }
        if (words >= 3 && !_stricmp(word[2], "POV"))
        {
            int at = 3, pov = 1;
            if (words == 5)
            {
                pov = number(word[3], 1, kInputPovsMost);
                if (!pov) return refuse("POV wants its number, 1 to 4, before the direction");
                at = 4;
            }
            else if (words != 4) return refuse("POV wants a direction: Up, Right, Down or Left");
            for (int d = 0; d < 4; d++)
                if (!_stricmp(word[at], kPovNames[d]))
                {
                    out.kind = kBindPov;
                    out.code = (uint32_t)pov;
                    out.direction = d;
                    return true;
                }
            return refuse("POV wants a direction: Up, Right, Down or Left");
        }
        if (words >= 3 && !_stricmp(word[2], "Axis"))
        {
            const int axis = number(word[3], 1, kInputAxesMost);
            if (!axis) return refuse("Axis wants its number, 1 to 8");
            if (words == 5 && _stricmp(word[4], "inverted")) return refuse("only inverted may follow an axis");
            if (words > 5) return refuse("only inverted may follow an axis");
            out.kind = kBindAxis;
            out.code = (uint32_t)axis;
            out.inverted = words == 5;
            return true;
        }
        return refuse("Wheel wants Button, POV or Axis after its number");
    }
    return refuse("a binding begins with Key, Pad, Wheel or None");
}

// ---- one snapshot of everything a binding can read ----
// A wheel or shifter as read on a pass: its buttons, its POV hats (hundredths of a degree clockwise from up, 0xFFFF
// centred) and its axes (0 to 1 over their travel: X, Y, Z, the three rotations and the two sliders).
struct InputDevice
{
    bool present;
    int buttons, povs, axes; // how many it has
    uint8_t button[kInputButtonsMost];
    uint32_t pov[kInputPovsMost];
    float axis[kInputAxesMost];
};
struct InputSnapshot
{
    bool front;          // the game's window is in front: the keys count only then
    uint8_t key[256];    // 1 = the key is down
    uint32_t pad;        // the buttons of every pad together (PadButtons)
    InputDevice device[kInputDevicesMost];
};

// Whether a POV reading points in a direction: within 45 degrees of it, so a diagonal counts for both of its sides,
// as on a d-pad.
inline bool PovPoints(uint32_t reading, int direction)
{
    if (reading > 36000) return false; // centred
    int off = (int)reading - direction * 9000;
    off = ((off % 36000) + 36000) % 36000;
    if (off > 18000) off = 36000 - off;
    return off <= 4500;
}

// A binding's raw state in a snapshot: all of it down, and for an axis its travel (0 to 1, inverted taken in). A key,
// button or POV has a travel of 1 while down.
inline bool BindingDown(const Binding &b, const InputSnapshot &s, float &travel)
{
    travel = 0.0f;
    const InputDevice *d = b.device >= 1 && b.device <= kInputDevicesMost ? &s.device[b.device - 1] : nullptr;
    bool down = false;
    switch (b.kind)
    {
    case kBindKey: down = s.front && b.code < 256 && s.key[b.code]; break;
    case kBindPad: down = b.code && (s.pad & b.code) == b.code; break;
    case kBindButton: down = d && d->present && b.code >= 1 && b.code <= kInputButtonsMost && d->button[b.code - 1]; break;
    case kBindPov: down = d && d->present && b.code >= 1 && b.code <= kInputPovsMost && PovPoints(d->pov[b.code - 1], b.direction); break;
    case kBindAxis:
        if (d && d->present && b.code >= 1 && b.code <= kInputAxesMost)
        {
            const float v = d->axis[b.code - 1] < 0.0f ? 0.0f : d->axis[b.code - 1] > 1.0f ? 1.0f : d->axis[b.code - 1];
            travel = b.inverted ? 1.0f - v : v;
            return travel > 0.5f;
        }
        return false;
    default: return false;
    }
    if (down) travel = 1.0f;
    return down;
}

// ---- the reader: from snapshot to snapshot, the state of every action ----
struct ActionState
{
    bool held;    // one of its bindings is down
    bool pressed; // one of them went down on this pass
    float travel; // the most travel of its bindings, 0 to 1 (the clutch's pedal)
};
struct InputReader { bool latched[kActCount][kBindingSlots]; };

// One pass: out holds every action's state. A binding counts as pressed on the pass it goes down, and not again until
// it was let go: a pad chord held over many passes is one press, and its buttons let go one by one give no second
// one. A pad chord that holds a smaller one bound elsewhere (LB+DPadDown and DPadDown) takes the press alone: the
// smaller one rests until its own buttons are let go.
inline void InputStep(InputReader &r, const GearBindings &b, const InputSnapshot &s, ActionState out[kActCount])
{
    bool raw[kActCount][kBindingSlots] = {};
    float travel[kActCount][kBindingSlots] = {};
    for (int a = 0; a < kActCount; a++)
        for (int k = 0; k < kBindingSlots; k++) raw[a][k] = BindingDown(b.slot[a][k], s, travel[a][k]);
    for (int a = 0; a < kActCount; a++)
    {
        ActionState &st = out[a];
        st = ActionState{};
        for (int k = 0; k < kBindingSlots; k++)
        {
            const Binding &x = b.slot[a][k];
            bool down = raw[a][k];
            if (down && x.kind == kBindPad)
                for (int a2 = 0; a2 < kActCount && down; a2++)
                    for (int k2 = 0; k2 < kBindingSlots && down; k2++)
                    {
                        const Binding &y = b.slot[a2][k2];
                        if (raw[a2][k2] && y.kind == kBindPad && y.code != x.code && (y.code & x.code) == x.code) down = false;
                    }
            st.held = st.held || down;
            st.pressed = st.pressed || (down && !r.latched[a][k]);
            st.travel = travel[a][k] > st.travel ? travel[a][k] : st.travel;
        }
    }
    for (int a = 0; a < kActCount; a++)
        for (int k = 0; k < kBindingSlots; k++) r.latched[a][k] = raw[a][k];
}

// ---- the capture of a new binding ----
// Begun for an action and a slot, then stepped once a pass with the snapshot: it arms once every key, pad button,
// device button and POV is let go (the click or press that began it is not read), and binds the first key down, the
// most buttons of the pad held together once they are let go, the first device button or POV, or for the clutch the
// axis that moved most, more than a third of its travel from where it rested when the capture armed (inverted when it
// moved down). Escape cancels; with nothing held it gives up after kCaptureMs.
static const unsigned long long kCaptureMs = 8000;
struct InputCapture
{
    bool active, armed;
    int action, slot;
    unsigned long long began;
    uint32_t peak;                                   // the pad buttons held together so far
    float rest[kInputDevicesMost][kInputAxesMost];   // each axis where it rested when the capture armed
};
enum { kCaptureWaiting, kCaptureBound, kCaptureGone };

inline void BindingCaptureBegin(InputCapture &c, int action, int slot, unsigned long long now)
{
    c = InputCapture{};
    c.active = true;
    c.action = action;
    c.slot = slot;
    c.began = now;
}

// Whether a virtual key is the mouse's (its buttons): never bound, as the tab's button that begins a capture is clicked.
inline bool KeyIsMouse(int vk) { return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2; }

// Whether nothing of the snapshot is down (keys but the mouse's, the pad, device buttons and POVs).
inline bool InputAtRest(const InputSnapshot &s)
{
    if (s.pad) return false;
    for (int vk = 1; vk < 256; vk++)
        if (s.key[vk] && !KeyIsMouse(vk)) return false;
    for (const InputDevice &d : s.device)
    {
        if (!d.present) continue;
        for (int i = 0; i < kInputButtonsMost; i++)
            if (d.button[i]) return false;
        for (int i = 0; i < kInputPovsMost; i++)
            if (d.pov[i] <= 36000) return false;
    }
    return true;
}

// One pass of a capture: kCaptureBound with the binding in out, kCaptureGone when cancelled or given up (the old binding
// stays), else kCaptureWaiting.
inline int BindingCaptureStep(InputCapture &c, const InputSnapshot &s, unsigned long long now, Binding &out)
{
    out = Binding{};
    if (!c.active) return kCaptureGone;
    auto done = [&](int how) { c.active = false; return how; };
    if (s.key[VK_ESCAPE]) return done(kCaptureGone);
    if (!c.peak && now - c.began > kCaptureMs) return done(kCaptureGone);
    if (!c.armed)
    {
        if (!InputAtRest(s)) return kCaptureWaiting;
        c.armed = true;
        for (int d = 0; d < kInputDevicesMost; d++)
            for (int a = 0; a < kInputAxesMost; a++) c.rest[d][a] = s.device[d].axis[a];
        return kCaptureWaiting;
    }
    // the pad: its buttons held together, bound once they are let go
    if (s.pad)
    {
        c.peak |= s.pad;
        return kCaptureWaiting;
    }
    if (c.peak)
    {
        out.kind = kBindPad;
        out.code = c.peak;
        return done(kCaptureBound);
    }
    for (int vk = 1; vk < 256; vk++)
        if (s.key[vk] && !KeyIsMouse(vk))
        {
            out.kind = kBindKey;
            out.code = (uint32_t)vk;
            return done(kCaptureBound);
        }
    float most = 1.0f / 3.0f;
    for (int d = 0; d < kInputDevicesMost; d++)
    {
        const InputDevice &dev = s.device[d];
        if (!dev.present) continue;
        for (int i = 0; i < kInputButtonsMost; i++)
            if (dev.button[i])
            {
                out.kind = kBindButton;
                out.device = d + 1;
                out.code = (uint32_t)(i + 1);
                return done(kCaptureBound);
            }
        for (int i = 0; i < kInputPovsMost; i++)
            for (int dir = 0; dir < 4; dir++)
                if (PovPoints(dev.pov[i], dir))
                {
                    out.kind = kBindPov;
                    out.device = d + 1;
                    out.code = (uint32_t)(i + 1);
                    out.direction = dir;
                    return done(kCaptureBound);
                }
        if (c.action != kActClutch) continue;
        for (int a = 0; a < kInputAxesMost; a++)
        {
            const float moved = dev.axis[a] - c.rest[d][a];
            if (fabsf(moved) > most)
            {
                most = fabsf(moved);
                out.kind = kBindAxis;
                out.device = d + 1;
                out.code = (uint32_t)(a + 1);
                out.inverted = moved < 0.0f;
            }
        }
    }
    if (out.kind == kBindAxis) return done(kCaptureBound);
    out = Binding{};
    return kCaptureWaiting;
}
