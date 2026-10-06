// models_test.cpp: the engine speed, the range and the tilt (src\models.h) on drives made up here: a truck with the gearbox of
// a scout (wheel speeds 1.5 in reverse, 3, 6, 12 and 16 in its four gears, 8 in the high gear), stepped 20 times a
// second as the mod steps it. It checks each gear's top speed, where the needle stands at idle, under load, in
// neutral and with the wheels spinning, that it never jumps, the start, the stall and the stop of the engine, and the
// range: when it shows, what standing, filling up and a change of the burn do to it, and the game's own burn rule.
// For the tilt it stands the truck on slopes and leans it to either side, over a bump and onto its roof.
// build.bat makes out\test\models_test.exe; test.bat runs it.
//
// With a file as its argument it runs the models over a recorded drive instead and prints what they make of it. One
// sample a line, numbers with spaces between: seconds, flags, stall, start timer, all wheel drive, gear, gear engaged,
// automatic, power factor, throttle, the action's + 0xB4, speed in m/s, fuel, tank, engine damage and its capacity,
// the number of gear speeds and each, the number of wheels and each one's drive and turning speed.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "gauges.h"
#include "truck.h"
#include "models.h"

static int g_failed = 0;
static void Check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failed++;
}

static bool Near(float a, float b, float by) { return fabsf(a - b) <= by; }

static const float kDt = 0.05f;

// Stands the truck's chassis nose up by pitch degrees and leaning to its right by roll degrees (below 0: nose down,
// to its left).
static void Stand(TruckRaw &t, float pitch, float roll)
{
    const float p = pitch / kDegrees, r = roll / kDegrees;
    t.forward[0] = cosf(p);
    t.forward[1] = sinf(p);
    t.up[1] = cosf(p) * cosf(r);
    t.left[1] = cosf(p) * sinf(r);
}

// A scout standing with its engine running in neutral: four wheels, the front two driven with all wheel drive only,
// 60 of 75 litres, and the burn numbers the game gives such a truck.
static TruckRaw Truck()
{
    TruckRaw t = {};
    t.flags = 3;
    t.powerFactor = 1.0f;
    t.torqueFactor = 1.0f;
    t.gearSpeeds = 6;
    static const float speeds[6] = { 1.5f, 3.0f, 6.0f, 12.0f, 16.0f, 8.0f };
    memcpy(t.gearSpeed, speeds, sizeof speeds);
    t.wheels = 4;
    t.drive[0] = t.drive[1] = 2;
    t.drive[2] = t.drive[3] = 1;
    t.fuel = 60.0f;
    t.tank = 75.0f;
    t.damageKnown = true;
    t.damageCapacity = 150;
    t.burnKnown = true;
    t.burnShare[0] = 2.0f;
    t.burnShare[1] = 2.2f;
    t.burnAwd = 1.8f;
    t.burnEngine = 1.5f;
    t.burnGearbox = 1.5f;
    t.burnIdle = 0.2f;
    t.burnLeak = 40.0f;
    t.gearBurns = 7;
    for (int i = 0; i < 7; i++) t.gearBurn[i] = 1.0f;
    return t;
}

// The truck in a gear with its wheels at a share of the gear's top speed and the pedal down by pedal.
static void Drive(TruckRaw &t, int gear, bool automatic, float share, float pedal)
{
    t.gear = t.gearEngaged = gear;
    t.automatic = automatic ? 1 : 0;
    t.throttle = automatic && gear < 0 ? pedal : -pedal;
    const float spin = TruckGearTop(t, gear) * share;
    for (int i = 0; i < t.wheels; i++) t.spin[i] = gear < 0 ? -spin : spin;
    t.speed = (gear < 0 ? -spin : spin) * 0.45f; // wheels of 0.45 m
}

// Steps the engine speed for a while and returns the largest change from one step to the next.
static float Run(TachModel &m, const TruckRaw &t, const GaugeScale &k, float seconds)
{
    float most = 0.0f;
    for (float at = 0.0f; at < seconds; at += kDt)
    {
        const float before = m.rpm;
        TachStep(m, t, k, kDt);
        most = fmaxf(most, fabsf(m.rpm - before));
    }
    return most;
}

static int Replay(const char *path)
{
    FILE *f = nullptr;
    if (fopen_s(&f, path, "r") || !f)
    {
        printf("cannot read %s\n", path);
        return 1;
    }
    GaugeSettings s;
    SettingsDefaults(s);
    s.tachClass = kClassScout;
    const GaugeScale k = GaugeScaleOf(s);
    TachModel tach = {};
    RangeModel range = {};
    float last = -1.0f, tank = 0.0f, mostStep = 0.0f, mostRpm = 0.0f;
    int samples = 0, steps = 0, redSteps = 0;
    char line[2048];
    while (fgets(line, sizeof line, f))
    {
        TruckRaw t = {};
        int flags = 0, awd = 0, automatic = 0, used = 0, at = 0;
        float seconds = 0.0f;
        if (sscanf_s(line, "%f %d %f %f %d %d %d %d %f %f %f %f %f %f %d %d %d%n", &seconds, &flags, &t.stall, &t.startTimer, &awd, &t.gear, &t.gearEngaged, &automatic,
                     &t.powerFactor, &t.throttle, &t.actionB4, &t.speed, &t.fuel, &t.tank, &t.damage, &t.damageCapacity, &t.gearSpeeds, &at) < 17 || t.gearSpeeds > kMaxGears)
            continue;
        for (int i = 0; i < t.gearSpeeds; i++) { sscanf_s(line + at, "%f%n", &t.gearSpeed[i], &used); at += used; }
        sscanf_s(line + at, "%d%n", &t.wheels, &used);
        at += used;
        if (t.wheels > kMaxWheels) continue;
        for (int i = 0; i < t.wheels; i++) { sscanf_s(line + at, "%d %f%n", &t.drive[i], &t.spin[i], &used); at += used; }
        t.flags = (uint8_t)flags;
        t.awd = (uint8_t)awd;
        t.automatic = (uint8_t)automatic;
        t.damageKnown = true;
        // another truck (a recovery, the garage) or a break in the drive: the models start over, as in the mod
        if (last < 0.0f || seconds - last > 5.0f || seconds < last || fabsf(t.tank - tank) > 0.01f)
        {
            tach = TachModel{};
            range = RangeModel{};
            last = seconds - kDt;
        }
        tank = t.tank;
        // the sample holds until the next one, in steps of the mod's own length
        for (float left = seconds - last; left > 0.0005f; left -= kDt)
        {
            const float dt = fminf(left, kDt), before = tach.rpm;
            const bool was = tach.running;
            TachStep(tach, t, k, dt);
            RangeStep(range, t, dt);
            if (was) mostStep = fmaxf(mostStep, fabsf(tach.rpm - before));
            mostRpm = fmaxf(mostRpm, tach.rpm);
            steps++;
            if (tach.rpm > k.redline) redSteps++;
        }
        last = seconds;
        samples++;
        printf("%8.2f gear %2d %s pedal %.2f wheels %5.2f of %5.2f | %4.0f rpm | %5.1f km/h | fuel %6.2f | range %6.1f km at %.3f l/km over %4.0f m, standing %.2f l/min\n", seconds,
               t.gearEngaged, t.automatic ? "A" : " ", TruckPedal(t), TruckDrivenSpin(t), TruckGearTop(t, t.gearEngaged), tach.rpm, t.speed * 3.6f, t.fuel, range.rangeKm,
               range.perKm, range.way - range.mark[0].way, range.standing);
    }
    fclose(f);
    printf("%d samples, %d steps: the largest change of the engine speed in one step %.0f rpm, the highest engine speed %.0f rpm, %d steps in the red zone (from %.0f rpm)\n",
           samples, steps, mostStep, mostRpm, redSteps, k.redline);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1) return Replay(argv[1]);
    GaugeSettings s;
    SettingsDefaults(s);
    const GaugeScale k = GaugeScaleOf(s); // the truck class: idle 650, red zone from 2200
    const float span = k.redline - k.idle;

    // each gear's top speed
    TruckRaw t = Truck();
    Check(Near(TruckGearTop(t, 1), 11.0f, 0.001f) && Near(TruckGearTop(t, 2), 17.0f, 0.001f) && Near(TruckGearTop(t, 3), 29.0f, 0.001f) && Near(TruckGearTop(t, 4), 37.0f, 0.001f),
          "gears: a gear's wheels are governed to twice its number and five");
    Check(Near(TruckGearTop(t, 5), 10.0f, 0.001f) && Near(TruckGearTop(t, -1), 8.0f, 0.001f) && TruckGearTop(t, 0) == 0.0f && TruckGearTop(t, 6) == 0.0f,
          "gears: the high gear to its number and two, reverse as a gear, none in neutral or past the gearbox");
    t.powerFactor = 0.45f;
    const float low = TruckGearTop(t, 1);
    t.powerFactor = 0.2f;
    const float lowMinus = TruckGearTop(t, 1);
    t.automatic = 1;
    Check(Near(low, 5.5f, 0.001f) && Near(lowMinus, 3.0f, 0.001f) && Near(TruckGearTop(t, 1), 11.0f, 0.001f), "gears: the low range holds the first gear to a share, an automatic's first gear runs out");

    // the driven wheels
    t = Truck();
    t.spin[0] = t.spin[1] = 2.0f;
    t.spin[2] = -10.0f;
    t.spin[3] = 12.0f;
    const float rear = TruckDrivenSpin(t);
    t.awd = 1;
    Check(Near(rear, 11.0f, 0.001f) && Near(TruckDrivenSpin(t), 6.5f, 0.001f), "wheels: the mean of the driven ones whichever way they turn, the front ones with all wheel drive");

    // where the needle stands
    t = Truck();
    TachModel m = {};
    TachStep(m, t, k, kDt);
    Check(Near(m.rpm, k.idle, 0.5f), "needle: at idle on the first step, without a lag");
    t.throttle = -1.0f;
    Run(m, t, k, 2.0f);
    Check(Near(m.rpm, k.idle + kTachFree * span, 2.0f), "needle: the pedal revs the engine freely in neutral");
    t = Truck();
    Drive(t, 1, true, 0.0f, 1.0f);
    m = TachModel{};
    Run(m, t, k, 2.0f);
    const float stuck = m.rpm;
    Drive(t, 1, true, 0.0f, 0.25f);
    Run(m, t, k, 2.0f);
    Check(Near(stuck, k.idle + kTachLoad * span, 2.0f) && Near(m.rpm, k.idle + kTachLoad * 0.5f * span, 2.0f), "needle: a truck that does not move pulls the engine up with the pedal");
    Drive(t, 3, true, 1.0f, 1.0f);
    Run(m, t, k, 2.0f);
    const float runOut = m.rpm;
    Drive(t, 3, true, 0.5f, 0.0f);
    Run(m, t, k, 2.0f);
    const float coasting = m.rpm;
    Drive(t, 3, true, 0.5f, 1.0f);
    Run(m, t, k, 2.0f);
    Check(Near(runOut, k.redline, 2.0f) && Near(coasting, k.idle + 0.5f * span, 2.0f) && m.rpm > coasting + 100.0f && m.rpm < runOut - 100.0f,
          "needle: a gear run out stands at the red zone, half way at half the gear's speed, higher under the pedal");
    Drive(t, 2, true, 1.6f, 1.0f);
    Run(m, t, k, 2.0f);
    Check(Near(m.rpm, k.idle + kTachOverrun * span, 2.0f) && m.rpm > k.redline && m.rpm < k.max, "needle: wheels spinning past the gear's speed go into the red zone and stop there");
    t.powerFactor = 0.45f;
    Drive(t, 1, false, 1.0f, 1.0f);
    Run(m, t, k, 2.0f);
    Check(Near(m.rpm, k.redline, 2.0f) && Near(TruckDrivenSpin(t), 5.5f, 0.001f), "needle: the low range runs out at its own lower speed");

    // the pedal's side
    t = Truck();
    Drive(t, -1, true, 0.0f, 1.0f);
    const float automaticReverse = TruckPedal(t);
    t.throttle = -1.0f;
    const float automaticBrake = TruckPedal(t);
    Drive(t, -1, false, 0.0f, 1.0f);
    const float manualReverse = TruckPedal(t);
    Drive(t, 2, true, 0.5f, 1.0f);
    const float forwards = TruckPedal(t);
    t.throttle = 1.0f;
    Check(automaticReverse == 1.0f && automaticBrake == 0.0f && manualReverse == 1.0f && forwards == 1.0f && TruckPedal(t) == 0.0f,
          "pedal: forwards is the game's negative side, an automatic's reverse the positive one, the brake is no pedal");

    // through the gears: the needle climbs in each gear, falls at each shift, and never jumps
    t = Truck();
    m = TachModel{};
    TachStep(m, t, k, kDt);
    float most = 0.0f, lowest = k.max, highest = 0.0f;
    bool fell = true;
    for (int gear = 1; gear <= 4; gear++)
    {
        const float from = gear == 1 ? 0.0f : TruckGearTop(t, gear - 1) / TruckGearTop(t, gear);
        const float atShift = m.rpm;
        for (float at = 0.0f; at < 4.0f; at += kDt)
        {
            Drive(t, gear, true, from + (1.0f - from) * fminf(at / 3.0f, 1.0f), 1.0f);
            const float before = m.rpm;
            TachStep(m, t, k, kDt);
            most = fmaxf(most, fabsf(m.rpm - before));
            if (gear > 1 && at > 0.2f && at < 0.6f) lowest = fminf(lowest, m.rpm);
            if (gear > 1 && Near(at, 0.5f, 0.01f) && m.rpm > atShift - 150.0f) fell = false;
        }
        highest = fmaxf(highest, m.rpm);
        if (!Near(m.rpm, k.redline, 5.0f)) fell = false;
    }
    Check(fell && lowest > k.idle + 0.3f * span && highest <= k.redline + 1.0f, "gears: each gear runs to the red zone and the next one starts lower, well above idle");
    Check(most < 0.2f * span, "needle: no step of a drive through the gears moves it by a fifth of the way from idle to the red zone");

    // a gear going in dips the engine speed for a moment
    t = Truck();
    Drive(t, 2, false, 0.6f, 0.5f);
    TachModel a = {}, b = {};
    Run(a, t, k, 2.0f);
    b = a;
    const float steady = a.rpm;
    t.gear = 3; // picked, not yet engaged
    Run(a, t, k, 0.15f);
    Check(a.rpm < steady - 0.05f * steady && a.rpm > steady * kTachDip && Near(b.rpm, steady, 1.0f), "shift: the engine speed dips while a gear goes in");
    t.gear = 2;
    Run(a, t, k, 1.5f);
    Check(Near(a.rpm, steady, 2.0f), "shift: and is back when it is in");

    // start, stall, stop
    t = Truck();
    t.flags = 1;
    t.startTimer = kStartTime;
    m = TachModel{};
    TachStep(m, t, k, kDt);
    const float atStart = m.rpm;
    bool rising = true;
    for (float left = kStartTime; left > 0.0f; left -= kDt)
    {
        t.startTimer = left;
        const float before = m.rpm;
        TachStep(m, t, k, kDt);
        if (m.rpm < before) rising = false;
    }
    const float started = m.rpm;
    t.flags = 3;
    t.startTimer = 0.0f;
    Run(m, t, k, 1.0f);
    Check(atStart == 0.0f && rising && started > 0.7f * k.idle && started <= k.idle && Near(m.rpm, k.idle, 2.0f), "start: the needle rises from 0 to idle while the engine starts");
    t.stall = 0.7f;
    Run(m, t, k, 2.0f);
    const float stalling = m.rpm;
    t.stall = 1.4f;
    Run(m, t, k, 2.0f);
    Check(Near(stalling, k.idle * 0.25f, 3.0f) && m.rpm < 2.0f, "stall: the needle falls with the game's stall counter, to 0 where the engine dies");
    t = Truck();
    Drive(t, 3, true, 0.8f, 1.0f);
    m = TachModel{};
    Run(m, t, k, 1.0f);
    const float running = m.rpm;
    t.flags = 0;
    TachStep(m, t, k, kDt);
    const float oneStep = m.rpm;
    Run(m, t, k, 3.0f);
    Check(oneStep > 0.8f * running && m.rpm < 5.0f, "stop: the needle runs down with the engine and does not drop at once");

    // the game's burn rule
    t = Truck();
    const float idle = TruckFuelRate(t);
    t.awd = 1;
    const float idleAwd = TruckFuelRate(t);
    t.awd = 0;
    Drive(t, 4, true, 0.6f, 1.0f);
    t.actionB4 = 0.8f;
    const float pulling = TruckFuelRate(t);
    t.damageEffect = 1.0f;
    const float damaged = TruckFuelRate(t);
    t.flags = 0;
    const float off = TruckFuelRate(t);
    t.burnKnown = false;
    Check(Near(idle, 0.84f, 0.001f) && Near(idleAwd, 1.512f, 0.001f) && pulling > 3.0f && pulling < 4.2f && Near(damaged, pulling * 1.5f, 0.01f) && off == 0.0f &&
          TruckFuelRate(t) < 0.0f, "burn: 0.84 litres a minute at idle, 1.8 times that with all wheel drive, more under load and with a damaged engine, none with the engine off");

    // the range: 2 litres a kilometre at 10 m/s
    t = Truck();
    RangeModel r = {};
    auto drive = [&](float metres, float speed, float litresPerKm) {
        t.speed = speed;
        for (float way = 0.0f; way < metres; way += speed * kDt)
        {
            t.fuel -= litresPerKm * speed * kDt / 1000.0f;
            RangeStep(r, t, kDt);
        }
    };
    auto stand = [&](float seconds, float litresPerMinute) {
        t.speed = 0.0f;
        for (float at = 0.0f; at < seconds; at += kDt)
        {
            t.fuel -= litresPerMinute * kDt / 60.0f;
            RangeStep(r, t, kDt);
        }
    };
    RangeStep(r, t, kDt);
    const bool noneAtFirst = r.rangeKm < 0.0f;
    drive(450.0f, 10.0f, 2.0f);
    const bool noneYet = r.rangeKm < 0.0f;
    drive(100.0f, 10.0f, 2.0f);
    Check(noneAtFirst && noneYet && Near(r.rangeKm, t.fuel / 2.0f, 0.3f), "range: no number until half a kilometre is driven, then the litres over the litres a kilometre");
    const float perKm = r.perKm, before = r.rangeKm, fuelBefore = t.fuel;
    stand(300.0f, 0.84f);
    Check(Near(r.perKm, perKm, 0.001f) && Near(r.rangeKm, before - (fuelBefore - t.fuel) / perKm, 0.05f) && Near(r.standing, 0.84f, 0.01f),
          "range: standing with the engine running takes its litres off the range and leaves the litres a kilometre alone");
    t.fuel = 75.0f;
    RangeStep(r, t, kDt);
    Check(Near(r.perKm, perKm, 0.001f) && Near(r.rangeKm, 75.0f / perKm, 0.05f), "range: a filled tank goes further at the same litres a kilometre");
    t.fuel -= 20.0f;
    t.speed = 10.0f;
    RangeStep(r, t, kDt);
    Check(Near(r.perKm, perKm, 0.01f), "range: fuel that leaves the tank in one go was not burned on the way");
    // the burn doubles: the window takes it in over its three kilometres, never by more than a tenth of the first
    // figure on 50 metres
    float mostChange = 0.0f;
    for (int i = 0; i < 80; i++)
    {
        const float was = r.perKm;
        drive(50.0f, 10.0f, 4.0f);
        mostChange = fmaxf(mostChange, fabsf(r.perKm - was));
    }
    Check(Near(r.perKm, 4.0f, 0.05f) && mostChange < 0.2f && Near(r.rangeKm, t.fuel / 4.0f, 0.3f), "range: a higher burn is taken in over three kilometres, step by step");
    Check(r.marks <= kRangeMarks && r.way - r.mark[0].way >= kRangeWindow - 1.0f && r.way - r.mark[0].way < kRangeWindow + 2.0f * kRangeMark, "range: the window is the last three kilometres");
    t.tank = 200.0f;
    t.fuel = 150.0f;
    RangeStep(r, t, kDt);
    Check(r.rangeKm < 0.0f && r.way < 1.0, "range: a truck with another tank starts over");
    drive(1100.0f, 10.0f, 0.01f);
    Check(r.rangeKm == kRangeMax, "range: the readout stops at 999");

    // the tilt
    {
        GaugeSettings set;
        SettingsDefaults(set);
        TruckRaw c = Truck();
        TiltModel tilt = {};
        // the lamp of a truck that has stood like this for four seconds
        auto lamp = [&](float pitch, float roll) {
            Stand(c, pitch, roll);
            for (int i = 0; i < 80; i++) TiltStep(tilt, c, set, kDt);
            return tilt.lamp;
        };
        Stand(c, 0.0f, 20.0f);
        TiltStep(tilt, c, set, kDt);
        Check(tilt.lamp == 1 && Near(tilt.rollDeg, 20.0f, 0.01f) && Near(tilt.pitchDeg, 0.0f, 0.01f), "tilt: the first step takes the truck as it stands");
        Check(lamp(0.0f, 0.0f) == 0 && lamp(0.0f, 14.0f) == 0 && lamp(0.0f, 16.0f) == 1 && lamp(0.0f, 24.0f) == 1 && lamp(0.0f, 26.0f) == 2,
              "tilt: green on level ground, amber from 15 degrees to a side, red from 25");
        const bool left = lamp(0.0f, -16.0f) == 1 && Near(tilt.rollDeg, -16.0f, 0.1f) && lamp(0.0f, -26.0f) == 2;
        Check(left && lamp(0.0f, 18.0f) == 1 && Near(tilt.rollDeg, 18.0f, 0.1f), "tilt: to the left as to the right, and the lean carries its side");
        const bool gentle = lamp(34.0f, 0.0f) == 0 && lamp(-34.0f, 0.0f) == 0;
        const bool up = lamp(36.0f, 0.0f) == 2 && Near(tilt.pitchDeg, 36.0f, 0.1f);
        Check(gentle && up && lamp(-36.0f, 0.0f) == 2 && Near(tilt.pitchDeg, -36.0f, 0.1f), "tilt: red from 35 degrees up or down a slope, no amber before it");
        // 20 degrees about the truck's own length on a slope of 20 put its left side 18.7 degrees above the horizon
        Check(lamp(20.0f, 20.0f) == 1 && Near(tilt.rollDeg, 18.75f, 0.1f) && Near(tilt.pitchDeg, 20.0f, 0.1f), "tilt: on a slope the lean is how far the truck's side hangs below the horizon");
        // a bump throws the truck to 40 degrees for one pass: the lamp stays green; held there, it goes amber, then red
        lamp(0.0f, 0.0f);
        Stand(c, 0.0f, 40.0f);
        TiltStep(tilt, c, set, kDt);
        const bool bump = tilt.lamp == 0;
        lamp(0.0f, 0.0f);
        bool amberFirst = true, wasAmber = false;
        Stand(c, 0.0f, 40.0f);
        float mostStep = 0.0f;
        for (int i = 0; i < 40; i++)
        {
            const float was = tilt.rollDeg;
            TiltStep(tilt, c, set, kDt);
            mostStep = fmaxf(mostStep, fabsf(tilt.rollDeg - was));
            if (tilt.lamp == 1) wasAmber = true;
            if (tilt.lamp == 2 && !wasAmber) amberFirst = false;
        }
        Check(bump && amberFirst && wasAmber && tilt.lamp == 2 && Near(tilt.rollDeg, 40.0f, 0.1f) && mostStep < 7.0f,
              "tilt: a bump of one pass does not light the lamp, and a lean that stays goes through amber to red");
        // rocking about an angle: amber from 15 stays until 13, red from 25 until 23
        const bool amberHeld = lamp(0.0f, 0.0f) == 0 && lamp(0.0f, 14.0f) == 0 && lamp(0.0f, 16.0f) == 1 && lamp(0.0f, 14.0f) == 1 && lamp(0.0f, 12.5f) == 0;
        const bool redHeld = lamp(0.0f, 26.0f) == 2 && lamp(0.0f, 24.0f) == 2 && lamp(0.0f, 22.5f) == 1 && lamp(36.0f, 0.0f) == 2 && lamp(34.0f, 0.0f) == 2 && lamp(32.5f, 0.0f) == 0;
        Check(amberHeld && redHeld, "tilt: the lamp goes back two degrees below the angle it came on at");
        const bool side = lamp(0.0f, 90.0f) == 2 && Near(tilt.rollDeg, 90.0f, 0.5f);
        const bool past = lamp(0.0f, 120.0f) == 2 && Near(tilt.rollDeg, 120.0f, 0.5f) && lamp(0.0f, -120.0f) == 2 && Near(tilt.rollDeg, -120.0f, 0.5f);
        Check(side && past && lamp(0.0f, 180.0f) == 2 && Near(fabsf(tilt.rollDeg), 180.0f, 0.5f),
              "tilt: on its side and on its roof the truck is in the red, and the lean goes on past 90 degrees");
        set.rollWarnDeg = 8.0f;
        set.rollDangerDeg = 12.0f;
        set.pitchDangerDeg = 20.0f;
        Check(lamp(0.0f, 5.0f) == 0 && lamp(0.0f, 10.0f) == 1 && lamp(0.0f, 14.0f) == 2 && lamp(22.0f, 0.0f) == 2 && lamp(16.0f, 0.0f) == 0, "tilt: the angles are the settings'");
        char line[256];
        lamp(-12.0f, -10.0f);
        TiltProbeText(c, tilt, line, sizeof line);
        Check(strstr(line, "nose down 12.0 deg, leaning left 9.8 deg, lamp amber") && strstr(line, "forward -0.208"), "tilt: the probe's line carries the angles, the lamp and the axes");
        c.left[1] = nanf("");
        TiltStep(tilt, c, set, kDt);
        Check(tilt.lamp < 0 && !tilt.running, "tilt: axes that are no numbers leave the lamp dark");
    }

    // the probe's line
    t = Truck();
    Drive(t, 2, true, 0.5f, 1.0f);
    m = TachModel{};
    TachStep(m, t, k, kDt);
    char text[512];
    ModelProbeText(t, m, r, k, text, sizeof text);
    Check(strstr(text, "wheels 8.50 rad/s of the gear's 17.00") && strstr(text, "pedal 1.00") && strstr(text, "l/min by the game's rule"), "probe: the models' line carries their numbers");

    printf(g_failed ? "MODELS TEST FAILED (%d)\n" : "MODELS TEST OK\n", g_failed);
    return g_failed ? 1 : 0;
}
