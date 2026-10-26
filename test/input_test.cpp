// input_test.cpp: the gearbox's bindings (src\input_bindings.h) on made up snapshots of the keys, the pad and the
// wheels: the text forms both ways, bad texts refused with a reason, a press as one edge, pad chords, the clutch's axis
// and the capture; and the listing of wheels and shifters through DirectInput once on this machine (src\input_devices.cpp),
// which passes with none.
#include "input_devices.h"

static int g_failed = 0;
static void Check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failed++;
}

static Binding Parsed(const char *text)
{
    Binding b;
    char why[160];
    if (!BindingParse(text, b, why, sizeof why)) printf("     \"%s\" refused: %s\n", text, why);
    return b;
}

static void Log(const wchar_t *line) { wprintf(L"     %s\n", line); }

int main()
{
    // the text forms: each kind read and written back the same
    {
        static const char *const texts[] = { "Key F3", "Key 211", "Key Page Up", "Key Num 3", "Key A", "Pad LB+DPadDown", "Pad A", "Wheel 1 Button 5",
                                             "Wheel 2 Button 128", "Wheel 1 POV Up", "Wheel 1 POV 2 Left", "Wheel 1 Axis 3", "Wheel 8 Axis 8 inverted", "None" };
        bool same = true;
        for (const char *t : texts)
        {
            char back[64];
            Binding b;
            char why[160];
            const bool read = BindingParse(t, b, why, sizeof why);
            BindingText(b, back, sizeof back);
            if (!read || strcmp(back, t))
            {
                printf("     \"%s\" came back as \"%s\"%s%s\n", t, back, read ? "" : ": ", read ? "" : why);
                same = false;
            }
        }
        const Binding f3 = Parsed("key f3"), code = Parsed("Key 211"), pad = Parsed("pad dpaddown+lb"), axis = Parsed("WHEEL 1 axis 3 Inverted");
        Check(same && f3.kind == kBindKey && f3.code == VK_F3 && code.code == 211 && pad.kind == kBindPad &&
                  pad.code == (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_DPAD_DOWN) && axis.kind == kBindAxis && axis.device == 1 && axis.code == 3 && axis.inverted,
              "bindings: every kind's text read and written back the same, in any case");
    }
    // the locks: DiffLock and Awd by their names, their bindings' texts both ways, one press for a key held over passes
    {
        GearBindings b = {};
        b.slot[kActDiffLock][0] = Parsed("Key L");
        b.slot[kActDiffLock][1] = Parsed("Pad RB+DPadLeft");
        b.slot[kActAwd][0] = Parsed("Wheel 1 Button 7");
        char lock[64], lockPad[64], awd[64];
        BindingText(b.slot[kActDiffLock][0], lock, sizeof lock);
        BindingText(b.slot[kActDiffLock][1], lockPad, sizeof lockPad);
        BindingText(b.slot[kActAwd][0], awd, sizeof awd);
        InputReader r = {};
        ActionState st[kActCount];
        InputSnapshot s = {};
        s.front = true;
        s.device[0].present = true;
        int locks = 0, awds = 0;
        for (int pass = 0; pass < 5; pass++)
        {
            s.key['L'] = pass >= 1 && pass <= 3;
            s.device[0].button[6] = pass == 2;
            InputStep(r, b, s, st);
            locks += st[kActDiffLock].pressed;
            awds += st[kActAwd].pressed;
        }
        Check(!strcmp(kActionNames[kActDiffLock], "DiffLock") && !strcmp(kActionNames[kActAwd], "Awd") && !strcmp(lock, "Key L") &&
                  !strcmp(lockPad, "Pad RB+DPadLeft") && !strcmp(awd, "Wheel 1 Button 7") && locks == 1 && awds == 1,
              "bindings: DiffLock and Awd by name, their texts both ways, a key held three passes one press");
    }
    // bad texts: refused, each with its reason
    {
        static const char *const bad[] = { "", "Key", "Key Nothing", "Key 0", "Key 300", "Pad", "Pad LB+Nothing", "Wheel", "Wheel 0 Button 1", "Wheel 9 Button 1",
                                           "Wheel 1", "Wheel 1 Button", "Wheel 1 Button 0", "Wheel 1 Button 129", "Wheel 1 POV", "Wheel 1 POV Sideways",
                                           "Wheel 1 POV 5 Up", "Wheel 1 Axis 9", "Wheel 1 Axis 3 backwards", "None at all", "Mouse 1" };
        bool refused = true;
        for (const char *t : bad)
        {
            Binding b;
            char why[160] = "";
            const bool read = BindingParse(t, b, why, sizeof why);
            if (read || !why[0] || b.kind != kBindNone)
            {
                printf("     \"%s\" was taken\n", t);
                refused = false;
            }
        }
        char why[160];
        Binding b;
        BindingParse("Wheel 1 Button 0", b, why, sizeof why);
        printf("     for example \"Wheel 1 Button 0\": %s\n", why);
        Check(refused, "bindings: nonsense is refused with a reason, and leaves none");
    }
    // the reader: one press per push, chords, the larger chord over the smaller one, a wheel's button and POV
    {
        GearBindings b = {};
        b.slot[kActUp][0] = Parsed("Pad LB+DPadUp");
        b.slot[kActUp][1] = Parsed("Wheel 1 Button 5");
        b.slot[kActDown][0] = Parsed("Pad DPadUp"); // held inside the up chord
        b.slot[kActGearN][0] = Parsed("Key N");
        b.slot[kActGearR][1] = Parsed("Wheel 1 POV Down");
        InputReader r = {};
        ActionState st[kActCount];
        InputSnapshot s = {};
        s.front = true;
        s.device[0].present = true;
        for (uint32_t &p : s.device[0].pov) p = 0xFFFF;
        const uint32_t LB = XINPUT_GAMEPAD_LEFT_SHOULDER, UP = XINPUT_GAMEPAD_DPAD_UP;
        int ups = 0, downs = 0;
        // LB, then LB+DPadUp held three passes, then LB let go first, then DPadUp
        const uint32_t pads[] = { LB, LB | UP, LB | UP, LB | UP, UP, 0 };
        for (uint32_t p : pads)
        {
            s.pad = p;
            InputStep(r, b, s, st);
            ups += st[kActUp].pressed;
            downs += st[kActDown].pressed;
        }
        Check(ups == 1 && downs == 0, "reader: a chord held over passes is one press, and its buttons let go one by one give none (nor its smaller chord)");
        s.pad = UP;
        InputStep(r, b, s, st);
        downs += st[kActDown].pressed;
        s.pad = 0;
        InputStep(r, b, s, st);
        Check(downs == 1 && !st[kActDown].held, "reader: the smaller chord alone presses once it is pushed on its own");
        int ns = 0;
        for (int pass = 0; pass < 4; pass++)
        {
            s.key['N'] = pass < 3;
            s.front = pass != 1;
            InputStep(r, b, s, st);
            ns += st[kActGearN].pressed;
        }
        s.front = true;
        Check(ns == 2, "reader: a key counts only while the game is in front (a pass behind it lets it go)");
        int buttons = 0, povs = 0;
        for (int pass = 0; pass < 3; pass++)
        {
            s.device[0].button[4] = pass < 2;
            s.device[0].pov[0] = pass == 0 ? 18000 : pass == 1 ? 13500 : 0xFFFF; // down, then down-right
            InputStep(r, b, s, st);
            buttons += st[kActUp].pressed;
            povs += st[kActGearR].pressed;
        }
        s.device[0].present = false;
        s.device[0].button[4] = 1;
        InputStep(r, b, s, st);
        Check(buttons == 1 && povs == 1 && !st[kActUp].held, "reader: a wheel's button and its POV (down-right counts as down) press once; a wheel gone holds nothing");
    }
    // the clutch's axis, as it is and inverted
    {
        GearBindings b = {};
        b.slot[kActClutch][1] = Parsed("Wheel 2 Axis 3");
        InputReader r = {};
        ActionState st[kActCount];
        InputSnapshot s = {};
        s.device[1].present = true;
        s.device[1].axis[2] = 0.25f;
        InputStep(r, b, s, st);
        const float plain = st[kActClutch].travel;
        const bool plainHeld = st[kActClutch].held;
        b.slot[kActClutch][1].inverted = true;
        InputStep(r, b, s, st);
        const float inverted = st[kActClutch].travel;
        const bool invertedHeld = st[kActClutch].held, invertedPressed = st[kActClutch].pressed;
        b.slot[kActClutch][0] = Parsed("Key Space");
        s.front = true;
        s.key[VK_SPACE] = 1;
        b.slot[kActClutch][1].inverted = false;
        InputStep(r, b, s, st);
        Check(fabsf(plain - 0.25f) < 1e-6f && !plainHeld && fabsf(inverted - 0.75f) < 1e-6f && invertedHeld && invertedPressed && st[kActClutch].travel == 1.0f,
              "reader: the clutch's axis travel, 0.25 as it is and 0.75 inverted (held past half); a key on it is the full travel");
    }
    // the capture: armed only at rest, the pad's chord at its fullest, a key, a device's button, the clutch's axis, giving up
    {
        InputCapture c;
        InputSnapshot s = {};
        s.front = true;
        Binding out;
        BindingCaptureBegin(c, kActUp, 0, 1000);
        s.pad = XINPUT_GAMEPAD_A; // the press that began it, still held
        const int a = BindingCaptureStep(c, s, 1050, out);
        const bool armedEarly = c.armed;
        s.pad = 0;
        BindingCaptureStep(c, s, 1100, out); // arms
        s.pad = XINPUT_GAMEPAD_LEFT_SHOULDER;
        BindingCaptureStep(c, s, 1150, out);
        s.pad = XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_DPAD_DOWN;
        BindingCaptureStep(c, s, 1200, out);
        s.pad = XINPUT_GAMEPAD_DPAD_DOWN;
        const int partial = BindingCaptureStep(c, s, 1250, out);
        s.pad = 0;
        const int bound = BindingCaptureStep(c, s, 1300, out);
        char text[64];
        BindingText(out, text, sizeof text);
        Check(a == kCaptureWaiting && !armedEarly && !c.active && partial == kCaptureWaiting && bound == kCaptureBound && !strcmp(text, "Pad LB+DPadDown"),
              "capture: armed once the pad is let go, the chord held at its fullest bound when let go (Pad LB+DPadDown)");
        BindingCaptureBegin(c, kActGearN, 0, 2000);
        s.key[VK_LBUTTON] = 1; // the click on the tab's button
        s.key['G'] = 1;        // and G, down before it armed
        const int notArmed = BindingCaptureStep(c, s, 2050, out);
        s.key['G'] = 0;
        BindingCaptureStep(c, s, 2100, out); // arms: only the mouse is down
        s.key['G'] = 1;
        const int key = BindingCaptureStep(c, s, 2150, out);
        BindingText(out, text, sizeof text);
        s.key['G'] = s.key[VK_LBUTTON] = 0;
        Check(notArmed == kCaptureWaiting && key == kCaptureBound && !strcmp(text, "Key G"), "capture: a key held before it armed waits; the first key after it is bound, the mouse never");
        BindingCaptureBegin(c, kActGear1, 1, 3000);
        s.device[0].present = true;
        for (uint32_t &p : s.device[0].pov) p = 0xFFFF;
        BindingCaptureStep(c, s, 3050, out);
        s.device[0].axis[1] = 0.9f; // an axis moves: not for a gear
        const int axisForGear = BindingCaptureStep(c, s, 3100, out);
        s.device[0].button[11] = 1;
        const int button = BindingCaptureStep(c, s, 3150, out);
        BindingText(out, text, sizeof text);
        s.device[0].button[11] = 0;
        s.device[0].axis[1] = 0.0f;
        Check(axisForGear == kCaptureWaiting && button == kCaptureBound && !strcmp(text, "Wheel 1 Button 12"), "capture: a wheel's button for a gear; an axis does not bind a gear");
        BindingCaptureBegin(c, kActClutch, 1, 4000);
        s.device[0].axis[2] = 1.0f; // a pedal that reads 1 at rest
        s.device[0].axis[4] = 0.5f;
        BindingCaptureStep(c, s, 4050, out);
        s.device[0].axis[4] = 0.7f; // a little: not a third
        const int slight = BindingCaptureStep(c, s, 4100, out);
        s.device[0].axis[2] = 0.1f;
        const int pedal = BindingCaptureStep(c, s, 4150, out);
        BindingText(out, text, sizeof text);
        Check(slight == kCaptureWaiting && pedal == kCaptureBound && !strcmp(text, "Wheel 1 Axis 3 inverted"),
              "capture: the clutch takes the axis that moved most, past a third of its travel, inverted as it moved down");
        memset(&s, 0, sizeof s);
        s.front = true;
        BindingCaptureBegin(c, kActGear2, 0, 5000);
        BindingCaptureStep(c, s, 5050, out);
        const int waiting = BindingCaptureStep(c, s, 12000, out);
        const int gaveUp = BindingCaptureStep(c, s, 13100, out);
        BindingCaptureBegin(c, kActGear2, 0, 20000);
        BindingCaptureStep(c, s, 20050, out);
        s.key[VK_ESCAPE] = 1;
        const int escape = BindingCaptureStep(c, s, 20100, out);
        Check(waiting == kCaptureWaiting && gaveUp == kCaptureGone && escape == kCaptureGone && out.kind == kBindNone, "capture: given up after 8 s with nothing, and Escape cancels");
    }
    // the wheels and shifters on this machine, through DirectInput: once, for real; none is a pass
    {
        const bool started = InputDevicesStart(Log);
        InputSnapshot s;
        InputSnapshotRead(s, false, 0);
        int present = 0;
        for (const InputDevice &d : s.device) present += d.present;
        const int found = InputDevicesFound();
        printf("     DirectInput: %d wheel or shifter found on this machine, %d read on a pass\n", found, present);
        InputDevicesStop();
        Check(started && present == found && InputDevicesFound() == 0, "devices: DirectInput lists this machine's wheels and shifters (none is a pass), reads each, and lets them go");
    }
    if (g_failed) printf("INPUT TEST: %d FAILED\n", g_failed);
    return g_failed ? 1 : 0;
}
