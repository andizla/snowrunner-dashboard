// models_test.cpp: the engine speed, the range, the tilt, the coolant temperature and the boost (src\models.h) on drives made up
// here: a truck with the gearbox of a scout (wheel speeds 1.5 in reverse, 3, 6, 12 and 16 in its four gears, 8 in the
// high gear), stepped 20 times a second as the mod steps it. It checks each gear's top speed, where the needle stands
// at idle, under load, in neutral and with the wheels spinning, that it never jumps, the start, the stall and the stop
// of the engine, and the range: when it shows, what standing, filling up and a change of the burn do to it, and the
// game's own burn rule. For the tilt it stands the truck on slopes and leans it to either side, over a bump and onto
// its roof. For the coolant it warms a cold engine up, floors it at a crawl and at speed, lets it idle, stops it,
// changes the settings and the outside temperature, and drives one truck after another. For what the temperature
// costs the engine it holds an engine in the red zone, works a cold one hard, and counts the points of damage. For the
// boost it floors the pedal at high and at low engine speed and lifts it fast, a little and slowly.
// build.bat makes out\test\models_test.exe; test.bat runs it.
//
// "spool" prints the turbo's four profiles as numbers to draw (Spool).
//
// With a file as its argument it runs the models over a recorded drive instead and prints what they make of it, on a
// scout's scales, or on a truck's own idle and red zone when two more numbers follow (models_test drive.txt 800 4850). One
// sample a line, numbers with spaces between: seconds, flags, stall, start timer, all wheel drive, gear, gear engaged,
// automatic, power factor, throttle, the action's + 0xB4, speed in m/s, fuel, tank, engine damage and its capacity,
// the number of gear speeds and each, the number of wheels and each one's drive and turning speed, and after those
// the game's object for the truck as a hex number, where the table has it.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "gauges.h"
#include "truck.h"
#include "models.h"
#include "sound.h"

void GaugeLog(const wchar_t *, ...) {} // the sounds say in the mod's log where each comes from; here that goes nowhere

static float SoundRms(const std::vector<float> &x)
{
    double sum = 0.0;
    for (const float v : x) sum += (double)v * v;
    return x.empty() ? 0.0f : (float)sqrt(sum / (double)x.size());
}

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

// The truck with a gear in, the pedal down by pedal and the speedometer at kmh, and its coolant stepped for a while
// with the tachometer's needle at revs of the way from idle to the red zone. Returns the temperature it ends at.
static float Heat(TempModel &m, TruckRaw &t, uint64_t id, const GaugeSettings &s, float seconds, float pedal, float revs, float kmh)
{
    t.throttle = -pedal;
    t.speed = kmh / 3.6f;
    for (float at = 0.0f; at < seconds; at += kDt) TempStep(m, id, t, revs, s, kDt);
    return m.temp;
}

// The same until the coolant passes a temperature, from below or from above. Returns the seconds it took, below 0
// when it did not get there within an hour.
static float Until(TempModel &m, TruckRaw &t, uint64_t id, const GaugeSettings &s, float temp, float pedal, float revs, float kmh)
{
    t.throttle = -pedal;
    t.speed = kmh / 3.6f;
    const bool up = !m.known || m.temp < temp;
    for (float at = kDt; at <= 3600.0f; at += kDt)
    {
        TempStep(m, id, t, revs, s, kDt);
        if (up ? m.temp >= temp : m.temp <= temp) return at;
    }
    return -1.0f;
}

// Sets the driven truck's coolant to a temperature.
static void Coolant(TempModel &m, float temp) { m.truck[m.at].temp = m.temp = temp; }

static int Replay(const char *path, float idleRpm, float redlineRpm)
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
    s.idleRpm = idleRpm;
    s.redlineRpm = redlineRpm;
    SettingsClamp(s);
    s.damage = true; // the replay counts what the drive would have cost the engine
    const GaugeScale k = GaugeScaleOf(s);
    TachModel tach = {};
    RangeModel range = {};
    TempModel temp = {};
    DamageModel wear = {};
    // the boost: the highest, the time above half of what the turbo reaches, the lifts told, and how long it takes
    // from a floored pedal at next to no boost to nine tenths of it
    BoostModel boost = {};
    float mostBoost = 0.0f, boosted = 0.0f, climb = -1.0f, climbSum = 0.0f, climbLongest = 0.0f;
    int climbs = 0, flutters = 0, blowOffs = 0;
    float hotPoints = 0.0f, coldPoints = 0.0f;
    float last = -1.0f, tank = 0.0f, mostStep = 0.0f, mostRpm = 0.0f;
    int samples = 0, steps = 0, redSteps = 0;
    // the coolant: each truck's way to the cold lamp's 60 C and to 85 C, in seconds with the truck and in seconds of
    // driving (the engine on and the pedal down or the truck rolling), and the hottest it gets
    std::vector<std::string> notes;
    unsigned long long truckNow = 0;
    uint64_t kindNow = 0;
    float hottestOil = -1000.0f;
    float withTruck = 0.0f, driving = 0.0f, hottest = -1000.0f, inRed = 0.0f, loadSum = 0.0f, kmhSum = 0.0f, drivingAll = 0.0f;
    bool past60 = false, past85 = false;
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
        // the game's object for the truck; a table without it is one truck throughout
        unsigned long long truck = 1;
        if (sscanf_s(line + at, "%llx", &truck) != 1 || !truck) truck = 1;
        t.flags = (uint8_t)flags;
        t.awd = (uint8_t)awd;
        t.automatic = (uint8_t)automatic;
        t.damageKnown = true;
        // the table has no name for the truck's kind: it goes by its build, as in the mod where the name is not read
        char key[kMaxText];
        TruckBuildKey(t, key);
        const uint64_t kind = TruckKeyNumber(key);
        // another truck (a recovery, the garage) or a break in the drive: the models start over, as in the mod
        if (last < 0.0f || seconds - last > 5.0f || seconds < last || fabsf(t.tank - tank) > 0.01f)
        {
            tach = TachModel{};
            boost = BoostModel{};
            climb = -1.0f;
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
            const int lift = BoostStep(boost, t, TachShare(tach, k), k.boost, s.boostProfile, dt);
            if (lift == kLiftBlowOff) blowOffs++;
            if (lift == kLiftFlutter) flutters++;
            mostBoost = fmaxf(mostBoost, boost.bar);
            if (boost.bar >= 0.5f * k.boost) boosted += dt;
            if (climb < 0.0f && TruckPedal(t) >= 0.95f && boost.bar < 0.1f * k.boost) climb = 0.0f;
            else if (climb >= 0.0f)
            {
                climb += dt;
                if (TruckPedal(t) < 0.95f) climb = -1.0f;
                else if (boost.bar >= 0.9f * k.boost)
                {
                    climbSum += climb;
                    climbLongest = fmaxf(climbLongest, climb);
                    climbs++;
                    climb = -1.0f;
                }
            }
            RangeStep(range, kind, t, dt);
            if (was) mostStep = fmaxf(mostStep, fabsf(tach.rpm - before));
            mostRpm = fmaxf(mostRpm, tach.rpm);
            steps++;
            if (tach.rpm > k.redline) redSteps++;
            TempStep(temp, kind, t, TachSpeed(tach, k), s, dt);
            const int cost = DamageStep(wear, truck, t, temp, TachSpeed(tach, k), s, dt);
            (wear.hot > 0.0f ? hotPoints : coldPoints) += (float)cost;
            char note[200];
            if (truck != truckNow)
            {
                // a new object of the kind driven until now is that truck put back into the world, or another of
                // its kind (TempStep): its way to 85 C goes on
                const bool same = truckNow && kind == kindNow;
                sprintf_s(note, "%8.2f s: truck %llX (%s), coolant %.1f C%s", seconds, truck, key, temp.temp, same ? " (of the kind driven until now)" : "");
                notes.push_back(note);
                truckNow = truck;
                kindNow = kind;
                if (!same) { withTruck = driving = 0.0f; past60 = temp.temp >= kTempCold; past85 = temp.temp >= kTempWarm; }
            }
            withTruck += dt;
            if (TruckEngineOn(t) && (TruckPedal(t) > 0.05f || fabsf(t.speed) > 0.28f))
            {
                driving += dt;
                drivingAll += dt;
                loadSum += temp.load * dt;
                kmhSum += fabsf(t.speed) * 3.6f * dt;
            }
            if (!past60 && temp.temp >= kTempCold)
            {
                past60 = true;
                sprintf_s(note, "%8.2f s: 60 C, the cold lamp goes out, after %.0f s of driving (%.0f s with the truck)", seconds, driving, withTruck);
                notes.push_back(note);
            }
            if (!past85 && temp.temp >= kTempWarm)
            {
                past85 = true;
                sprintf_s(note, "%8.2f s: 85 C after %.0f s of driving (%.0f s with the truck)", seconds, driving, withTruck);
                notes.push_back(note);
            }
            hottest = fmaxf(hottest, temp.temp);
            hottestOil = fmaxf(hottestOil, temp.oil);
            if (temp.temp >= kTempRed) inRed += dt;
        }
        last = seconds;
        samples++;
        printf("%8.2f gear %2d %s pedal %.2f wheels %5.2f of %5.2f | %4.0f rpm | %5.1f km/h | fuel %6.2f | range %6.1f km at %.3f l/km over %4.0f m, standing %.2f l/min | "
               "coolant %5.1f C, load %.2f, heat %.2f, %.2f taken away, oil %5.1f C | boost %.2f bar\n", seconds,
               t.gearEngaged, t.automatic ? "A" : " ", TruckPedal(t), TruckDrivenSpin(t), TruckGearTop(t, t.gearEngaged), tach.rpm, t.speed * 3.6f, t.fuel, range.rangeKm,
               range.perKm, range.way - range.mark[0].way, range.standing, temp.temp, temp.load, temp.heat, temp.cooling, temp.oil, boost.bar);
    }
    fclose(f);
    printf("%d samples, %d steps: the largest change of the engine speed in one step %.0f rpm, the highest engine speed %.0f rpm, %d steps in the red zone (from %.0f rpm)\n",
           samples, steps, mostStep, mostRpm, redSteps, k.redline);
    for (const std::string &n : notes) printf("coolant: %s\n", n.c_str());
    printf("oil: the hottest %.1f C\n", hottestOil);
    printf("coolant: the hottest %.1f C, %.0f s in the red zone (from %.0f C); while driving (%.0f s) the mean load was %.2f and the mean speed %.1f km/h\n", hottest, inRed, kTempRed,
           drivingAll, drivingAll > 0.0f ? loadSum / drivingAll : 0.0f, drivingAll > 0.0f ? kmhSum / drivingAll : 0.0f);
    printf("boost: the highest %.2f bar of the turbo's %.2f, %.0f s above half of it; %d blow-offs and %d flutters; from a floored pedal to nine tenths of it %d times, %.1f s on average, %.1f s the longest\n",
           mostBoost, k.boost, boosted, blowOffs, flutters, climbs, climbs ? climbSum / (float)climbs : 0.0f, climbLongest);
    printf("damage: with Damage on the heat would have cost the engine %.0f points on this drive, and working a cold engine hard %.0f\n", hotPoints, coldPoints);
    return 0;
}

// "sounds <folder>": the turbo's sounds as the mod plays them (files in the folder's sounds\, else what this test's
// file carries, which is nothing, so made by code), written into the folder as turbo_high_1_heard.wav and so on, and
// played on this machine's default device, as loud as SoundVolume=50: each strength once, the oil warning, then the
// strong one again, cut short after 0.3 s as by the pedal going down, which leaves the oil warning alone.
static int Sounds(const char *folder)
{
    std::wstring dir;
    for (const char *p = folder; *p; p++) dir += (wchar_t)(unsigned char)*p;
    if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
    for (int t = 0; t < kSoundTierCount; t++)
    {
        SoundSet set;
        SoundLoad(t, dir, set);
        for (int v = 0; v < set.count; v++)
        {
            const SoundClip &c = set.clip[v];
            std::vector<uint8_t> wav(44 + c.x.size() * 2);
            auto put = [&](size_t at, uint32_t value, int bytes) { for (int i = 0; i < bytes; i++) wav[at + i] = (uint8_t)(value >> (8 * i)); };
            memcpy(&wav[0], "RIFF", 4);
            put(4, (uint32_t)wav.size() - 8, 4);
            memcpy(&wav[8], "WAVEfmt ", 8);
            put(16, 16, 4);
            put(20, 1, 2);
            put(22, 1, 2);
            put(24, c.rate, 4);
            put(28, c.rate * 2, 4);
            put(32, 2, 2);
            put(34, 16, 2);
            memcpy(&wav[36], "data", 4);
            put(40, (uint32_t)c.x.size() * 2, 4);
            for (size_t i = 0; i < c.x.size(); i++) put(44 + i * 2, (uint32_t)(uint16_t)(int16_t)(SettingsRange(c.x[i], -1.0f, 1.0f) * 32767.0f), 2);
            wchar_t name[64];
            swprintf_s(name, L"%s_%d_heard.wav", kSoundTiers[t].file, v + 1);
            FILE *f = nullptr;
            if (_wfopen_s(&f, (dir + name).c_str(), L"wb") == 0 && f)
            {
                fwrite(wav.data(), 1, wav.size(), f);
                fclose(f);
                printf("wrote %ls (%.2f s)\n", (dir + name).c_str(), (double)c.x.size() / c.rate);
            }
        }
    }
    if (!SoundStart(dir)) { printf("FAIL the sound thread did not start\n"); return 1; }
    static const float loud[kTurboTiers] = { 1.0f, 0.55f, 0.25f };
    LiftSound ls;
    for (int t = 0; t < kTurboTiers; t++)
    {
        LiftSoundOf(kLiftBlowOff, loud[t], 50, 0.0f, ls);
        SoundPlay(t, ls.gain, ls.pitch);
        Sleep(4500);
    }
    SoundPlay(kSoundOil, OilBeepGain(50), 1.0f);
    Sleep(1500);
    LiftSoundOf(kLiftBlowOff, 1.0f, 50, 0.0f, ls);
    SoundPlay(kTurboHigh, ls.gain, ls.pitch);
    SoundPlay(kSoundOil, OilBeepGain(50), 1.0f);
    Sleep(150);
    SoundQuiet();
    Sleep(800);
    const bool played = g_soundOpens == 1 && g_soundStarts[kTurboHigh] == 2 && g_soundStarts[kTurboMid] == 1 && g_soundStarts[kTurboLow] == 1 && g_soundStarts[kSoundOil] == 2 &&
                        g_soundHushed == 1;
    printf("%s sounds: XAudio2 opened %ld time(s); started: full boost %ld, half %ld, little %ld, the oil warning %ld; cut short by the pedal %ld\n", played ? "ok  " : "FAIL",
           g_soundOpens, g_soundStarts[kTurboHigh], g_soundStarts[kTurboMid], g_soundStarts[kTurboLow], g_soundStarts[kSoundOil], g_soundHushed);
    return played ? 0 : 1;
}

// "spool": the turbo's four profiles as numbers to draw, on the truck class's scales and boost: the boost held at each
// engine speed with the pedal floored (one line for each hundredth of the way from idle to the red zone), then the
// boost second by second as a truck floored in first gear at a standstill gathers speed to the gear's top in six
// seconds. Both as comma separated lines: what, x, then the four profiles.
static int Spool()
{
    GaugeSettings s;
    SettingsDefaults(s);
    const GaugeScale k = GaugeScaleOf(s);
    TruckRaw t = Truck();
    Drive(t, 1, true, 0.0f, 1.0f);
    printf("what,x,standard,quick,laggy,twostage\n");
    for (int i = 0; i <= 100; i++)
    {
        printf("held,%.2f", i / 100.0f);
        for (int p = 0; p < kSpoolCount; p++) printf(",%.4f", BoostTarget(t, i / 100.0f, k.boost, p));
        printf("\n");
    }
    TachModel tach[kSpoolCount] = {};
    BoostModel boost[kSpoolCount] = {};
    for (int step = 0; step <= 200; step++)
    {
        const float at = step * kDt;
        Drive(t, 1, true, fminf(at / 6.0f, 1.0f), 1.0f);
        printf("pull,%.2f", at);
        for (int p = 0; p < kSpoolCount; p++)
        {
            TachStep(tach[p], t, k, kDt);
            BoostStep(boost[p], t, TachShare(tach[p], k), k.boost, p, kDt);
            printf(",%.4f", boost[p].bar);
        }
        printf("\n");
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "sounds")) return Sounds(argv[2]);
    if (argc > 1 && !strcmp(argv[1], "spool")) return Spool();
    if (argc > 1) return Replay(argv[1], argc > 3 ? (float)atof(argv[2]) : 0.0f, argc > 3 ? (float)atof(argv[3]) : 0.0f);
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
    uint64_t kind = 1;
    auto drive = [&](float metres, float speed, float litresPerKm) {
        t.speed = speed;
        for (float way = 0.0f; way < metres; way += speed * kDt)
        {
            t.fuel -= litresPerKm * speed * kDt / 1000.0f;
            RangeStep(r, kind, t, kDt);
        }
    };
    auto stand = [&](float seconds, float litresPerMinute) {
        t.speed = 0.0f;
        for (float at = 0.0f; at < seconds; at += kDt)
        {
            t.fuel -= litresPerMinute * kDt / 60.0f;
            RangeStep(r, kind, t, kDt);
        }
    };
    RangeStep(r, kind, t, kDt);
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
    RangeStep(r, kind, t, kDt);
    Check(Near(r.perKm, perKm, 0.001f) && Near(r.rangeKm, 75.0f / perKm, 0.05f), "range: a filled tank goes further at the same litres a kilometre");
    t.fuel -= 20.0f;
    t.speed = 10.0f;
    RangeStep(r, kind, t, kDt);
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
    RangeStep(r, kind, t, kDt);
    Check(r.rangeKm < 0.0f && r.way < 1.0, "range: a truck with another tank starts over");
    drive(1100.0f, 10.0f, 0.01f);
    Check(r.rangeKm == kRangeMax, "range: the readout stops at 999");
    // the same tank under another kind of truck, and back
    const double wayBefore = r.way;
    kind = 2;
    RangeStep(r, kind, t, kDt);
    const bool another = wayBefore > 1000.0 && r.rangeKm < 0.0f && r.way < 1.0;
    drive(600.0f, 10.0f, 2.0f);
    const float secondKind = r.rangeKm;
    RangeStep(r, kind, t, kDt);
    Check(another && secondKind > 0.0f && Near(r.rangeKm, secondKind, 0.05f), "range: a truck of another kind starts over, with the same tank too, and a truck of the same kind goes on");

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

    // the coolant temperature
    {
        GaugeSettings set;
        SettingsDefaults(set);
        // a truck in gear with its engine running; each step is given the pedal, the needle and the speed
        TruckRaw c = Truck();
        c.gear = c.gearEngaged = 1;
        c.automatic = 1;
        TempModel temp = {};
        TempStep(temp, 1, c, 0.0f, set, kDt);
        Check(temp.known && Near(temp.temp, 15.0f, 0.05f) && TempCold(temp) && TempZone(temp) == 0,
              "coolant: a truck driven for the first time is as cold as the air outside, and the cold lamp is lit");
        // driven as usual: the needle at 0.7 of the way to the red zone, a load of 0.7, at 15 km/h
        const float usualRevs = kUsualLoad;
        const float lampOut = Until(temp, c, 1, set, kTempCold, 1.0f, usualRevs, kUsualKmh);
        const bool lampIsOut = !TempCold(temp) && TempZone(temp) == 1;
        const float warm = lampOut + Until(temp, c, 1, set, kTempWarm, 1.0f, usualRevs, kUsualKmh);
        Check(Near(warm, 180.0f, 3.0f) && lampOut > 60.0f && lampOut < warm - 30.0f && lampIsOut,
              "coolant: driven as usual a cold engine is at 85 C after the three minutes of WarmUpMinutes, and the cold lamp goes out at 60 C on the way");

        // where the engine settles
        const float idling = Heat(temp, c, 1, set, 1800.0f, 0.0f, 0.0f, 0.0f);
        const float rolling = Heat(temp, c, 1, set, 1800.0f, 0.0f, 0.0f, 60.0f);
        Check(idling > 84.0f && idling < 90.0f && rolling > kThermostatShut - 0.5f && rolling < idling,
              "coolant: the thermostat holds an idling engine at about 86 C, and a little lower with the truck rolling");
        Check(Near(Heat(temp, c, 1, set, 1800.0f, 1.0f, 1.0f, 60.0f), kTempCruise, 1.0f), "coolant: at full load with all the air the radiator can get the engine settles at 97 C");
        const float coasting = Heat(temp, c, 1, set, 1800.0f, 0.0f, 1.0f, 60.0f), lugging = Heat(temp, c, 1, set, 1800.0f, 1.0f, 0.0f, 0.0f);
        const float half = Heat(temp, c, 1, set, 1800.0f, 0.5f, 1.0f, 60.0f), halfSpeed = Heat(temp, c, 1, set, 1800.0f, 1.0f, 0.5f, 60.0f);
        Check(Near(coasting, rolling, 0.5f) && Near(lugging, idling, 0.5f) && Near(half, halfSpeed, 0.2f) && half > rolling + 3.0f && half < kTempCruise - 3.0f,
              "coolant: the heat goes by the fuel burned, the pedal times the engine speed: coasting at the red zone with the foot off makes the heat of idle, "
              "and so does the pedal floored at idle revs");
        bool outOfRed = true;
        float at10 = 0.0f, slower = 200.0f;
        for (float kmh : { 30.0f, 20.0f, 15.0f, 10.0f })
        {
            at10 = Heat(temp, c, 1, set, 1800.0f, 1.0f, 1.0f, kmh);
            if (at10 >= kTempRed - 3.0f || at10 < kTempCruise - 1.0f) outOfRed = false;
            if (slower < 200.0f && at10 <= slower) outOfRed = false;
            slower = at10;
        }
        Check(outOfRed && at10 > 105.0f, "coolant: floored from 10 km/h up the engine runs hotter the slower the truck is, and stays out of the red zone");
        const float at7 = Heat(temp, c, 1, set, 1800.0f, 1.0f, 1.0f, 7.0f), at5 = Heat(temp, c, 1, set, 1800.0f, 1.0f, 1.0f, 5.0f);
        Check(at7 > kTempRed - 3.0f && at7 < kTempRed && at5 > kTempRed, "coolant: floored at 7 km/h the engine settles just under the red zone, and at 5 km/h in it");

        // floored at a crawl
        Coolant(temp, kTempHot);
        const float boil = Until(temp, c, 1, set, kTempRed, 1.0f, 1.0f, 0.0f);
        const bool red = TempZone(temp) == 2;
        const float stopsAt = Heat(temp, c, 1, set, 1800.0f, 1.0f, 1.0f, 0.0f);
        Check(Near(boil, 90.0f, 2.0f) && red && stopsAt == kTempMost,
              "coolant: floored at a standstill the engine goes from 90 C into the red zone in the minute and a half of OverheatMinutes, and the model stops at 140 C");
        Coolant(temp, kTempHot);
        const float crawl = Until(temp, c, 1, set, kTempRed, 1.0f, 1.0f, 5.0f);
        Check(crawl > boil + 10.0f && crawl < 240.0f, "coolant: floored at walking pace it takes longer and still gets there");
        Coolant(temp, kTempRed);
        const float back = Until(temp, c, 1, set, 100.0f, 0.0f, 0.0f, 0.0f);
        Check(back > 10.0f && back < 60.0f, "coolant: off the pedal a standing engine comes back from the red zone to 100 C within a minute");

        // each of the three settings moves its own time and leaves the other two, and none of them the way back from
        // the red zone with the engine running
        auto times = [&](const GaugeSettings &g, float &warmUp, float &overheat, float &coolDown, float &wayBack) {
            TempModel m = {};
            TruckRaw e = c;
            warmUp = Until(m, e, 1, g, kTempWarm, 1.0f, usualRevs, kUsualKmh);
            Coolant(m, kTempHot);
            overheat = Until(m, e, 1, g, kTempRed, 1.0f, 1.0f, 0.0f);
            wayBack = Until(m, e, 1, g, 100.0f, 0.0f, 0.0f, 0.0f);
            Coolant(m, kTempHot);
            e.flags = 0;
            coolDown = Until(m, e, 1, g, kTempCooled, 0.0f, 0.0f, 0.0f);
        };
        float w0, o0, c0, b0, w1, o1, c1, b1;
        times(set, w0, o0, c0, b0);
        GaugeSettings other = set;
        other.warmUpMinutes = 6.0f;
        times(other, w1, o1, c1, b1);
        const bool warmOnly = Near(w1, 360.0f, 6.0f) && Near(o1, o0, 0.11f) && Near(c1, c0, 0.11f) && Near(b1, b0, 0.11f);
        other = set;
        other.overheatMinutes = 3.0f;
        times(other, w1, o1, c1, b1);
        const bool hotOnly = Near(o1, 180.0f, 4.0f) && Near(w1, w0, 0.11f) && Near(c1, c0, 0.11f) && Near(b1, b0, 0.11f);
        other = set;
        other.coolDownMinutes = 30.0f;
        times(other, w1, o1, c1, b1);
        Check(Near(c0, 900.0f, 2.0f) && warmOnly && hotOnly && Near(c1, 1800.0f, 4.0f) && Near(w1, w0, 0.11f) && Near(o1, o0, 0.11f) && Near(b1, b0, 0.11f),
              "coolant: a stopped engine cools from 90 C to 40 C in the 15 minutes of CoolDownMinutes, and each of the three settings moves its own time only");

        // the speed of the radiator's full airflow moves where a crawl settles, and none of the three times
        {
            GaugeSettings early = set, late = set;
            early.airFullKmh = 15.0f;
            late.airFullKmh = 50.0f;
            TempModel withEarly = {}, withLate = {};
            TruckRaw e = c;
            const float cooler = Heat(withEarly, e, 1, early, 1800.0f, 1.0f, 1.0f, 7.0f), hotter = Heat(withLate, e, 1, late, 1800.0f, 1.0f, 1.0f, 7.0f);
            times(early, w1, o1, c1, b1);
            const bool earlyTimes = Near(w1, w0, 2.0f) && Near(o1, o0, 0.11f) && Near(c1, c0, 0.11f) && Near(b1, b0, 0.11f);
            times(late, w1, o1, c1, b1);
            Check(cooler < 110.0f && hotter > kTempRed && earlyTimes && Near(w1, w0, 2.0f) && Near(o1, o0, 0.11f) && Near(c1, c0, 0.11f) && Near(b1, b0, 0.11f),
                  "coolant: with all the air from 15 km/h a crawl at 7 km/h stays cool, from 50 km/h it overheats, and the three times stay as they are");
        }

        // the outside temperature
        {
            GaugeSettings frost = set, heatwave = set;
            frost.ambientC = -20.0f;
            heatwave.ambientC = 35.0f;
            TempModel cold = {}, hot = {};
            TruckRaw e = c;
            TempStep(cold, 1, e, 0.0f, frost, kDt);
            const float frostStart = cold.temp;
            const float frostWarm = Until(cold, e, 1, frost, kTempWarm, 1.0f, usualRevs, kUsualKmh);
            Coolant(cold, kTempHot);
            const float frostBoil = Until(cold, e, 1, frost, kTempRed, 1.0f, 1.0f, 0.0f);
            TempStep(hot, 1, e, 0.0f, heatwave, kDt);
            Coolant(hot, kTempHot);
            const float heatBoil = Until(hot, e, 1, heatwave, kTempRed, 1.0f, 1.0f, 0.0f);
            Check(Near(frostStart, -20.0f, 0.05f) && frostWarm > warm * 1.3f && frostBoil < 0.0f && heatBoil > 0.0f && heatBoil < boil - 5.0f,
                  "coolant: in frost the engine starts colder, takes longer to warm up and cannot be overheated, and on a hot day it overheats sooner");
        }

        // in neutral nothing pulls against the engine
        {
            TruckRaw n = c;
            n.gear = n.gearEngaged = 0;
            TempModel freely = {};
            const float revved = Heat(freely, n, 1, set, 1800.0f, 1.0f, kTachFree, 0.0f);
            c.throttle = -1.0f;
            Check(revved > 90.0f && revved < 105.0f && Near(TruckLoad(n, 1.0f), kLoadNeutral, 0.001f) && Near(TruckLoad(c, 1.0f), 1.0f, 0.001f) &&
                  TruckLoad(c, 0.0f) == 0.0f, "coolant: revved out in neutral the engine warms and does not overheat");
        }

        // a stopped engine
        {
            TruckRaw halted = c;
            halted.flags = 0;
            TempModel stopped = {};
            TempStep(stopped, 1, halted, 0.0f, set, kDt);
            Coolant(stopped, kTempHot);
            const float later = Heat(stopped, halted, 1, set, 3.0f * 3600.0f, 1.0f, 1.0f, 0.0f);
            Check(later >= set.ambientC && later < set.ambientC + 0.1f, "coolant: a stopped engine cools to the outside temperature and no further, whatever the pedal does");
        }

        // one truck after another: the model knows a truck by its kind
        {
            TempModel fleet = {};
            TruckRaw scout = c, hauler = c;
            hauler.tank = 200.0f;
            hauler.fuel = 150.0f;
            const float left = Heat(fleet, scout, 0x1000, set, 900.0f, 1.0f, usualRevs, kUsualKmh);
            TempStep(fleet, 0x2000, hauler, 0.0f, set, 0.0f);
            const bool startsCold = Near(fleet.temp, 15.0f, 0.01f);
            const float leftHauler = Heat(fleet, hauler, 0x2000, set, 600.0f, 1.0f, usualRevs, kUsualKmh);
            // back in the first kind: ten minutes went by with its engine stopped
            TempStep(fleet, 0x1000, scout, 0.0f, set, 0.0f);
            const float scoutBack = fleet.temp;
            const bool cooled = Near(fleet.temp, 15.0f + (left - 15.0f) * expf(-TempRatesOf(set).off * 600.0f), 0.3f) && fleet.temp < left - 20.0f;
            // and straight back into the second: nothing was driven in between
            TempStep(fleet, 0x2000, hauler, 0.0f, set, 0.0f);
            Check(left > kTempWarm && startsCold && cooled && Near(fleet.temp, leftHauler, 0.01f),
                  "coolant: each kind of truck keeps its own temperature, and one left standing cools for as long as others are driven");
            // the kind is all that counts: a truck with the first one's build under a third kind starts cold, and a
            // truck of the second kind with another tank and two more wheels is as warm as that kind was left
            TempStep(fleet, 0x3000, scout, 0.0f, set, 0.0f);
            const bool newKind = Near(fleet.temp, 15.0f, 0.01f);
            TruckRaw rebuilt = hauler;
            rebuilt.tank = 300.0f;
            rebuilt.wheels = 6;
            TempStep(fleet, 0x2000, rebuilt, 0.0f, set, 0.0f);
            Check(newKind && Near(fleet.temp, leftHauler, 0.01f),
                  "coolant: a kind not driven before starts cold, and a truck of a kind driven before is as warm as the kind was left, whatever its build");
            // the first kind taken out of the garage again, after five minutes in the third
            Heat(fleet, scout, 0x3000, set, 300.0f, 1.0f, usualRevs, kUsualKmh);
            TempStep(fleet, 0x1000, scout, 0.0f, set, 0.0f);
            Check(Near(fleet.temp, 15.0f + (scoutBack - 15.0f) * expf(-TempRatesOf(set).off * 300.0f), 0.3f) && fleet.temp > 30.0f,
                  "coolant: a truck taken out of the garage again is as warm as it was left, less the cooling");
            // more kinds than the model remembers, four minutes floored in each
            TempModel many = {};
            TruckRaw x = c;
            for (int i = 0; i < kTempTrucks + 3; i++) Heat(many, x, 0x100 + (uint64_t)i, set, 240.0f, 1.0f, 1.0f, 30.0f);
            TempStep(many, 0x100, x, 0.0f, set, 0.0f);
            const bool forgotten = Near(many.temp, 15.0f, 0.01f);
            TempStep(many, 0x100 + (uint64_t)(kTempTrucks + 1), x, 0.0f, set, 0.0f);
            Check(forgotten && many.temp > kTempCold && many.temp < kTempWarm, "coolant: of more kinds than the model remembers, the one left longest ago starts cold again and the others are still warm");
        }

        // the probe's line, and a needle that is no number
        {
            TempModel p = {};
            TruckRaw q = c;
            char line[640];
            TempProbeText(q, p, set, line, sizeof line);
            const bool notYet = strstr(line, "not known yet") != nullptr;
            Heat(p, q, 1, set, 10.0f, 1.0f, 1.0f, 18.0f);
            TempProbeText(q, p, set, line, sizeof line);
            Check(notYet && strstr(line, "coolant ") && strstr(line, "cold lamp on") && strstr(line, "(load 1.00)") && strstr(line, "at 18.0 km/h") && strstr(line, "15 C outside"),
                  "coolant: the probe's line carries the temperature, the lamp, the load and the air, and says so before a truck was driven");
            // a needle that is no number counts as one at idle, and a step that gives no number changes nothing
            Heat(p, q, 1, set, 300.0f, 1.0f, 1.0f, 18.0f);
            const float held = p.temp;
            Heat(p, q, 1, set, 1.0f, 1.0f, nanf(""), 18.0f);
            const bool needle = isfinite(p.temp) && Near(p.temp, held, 1.0f) && TruckLoad(q, nanf("")) == 0.0f;
            q.speed = nanf("");
            TempStep(p, 1, q, 1.0f, set, kDt);
            TachModel broken = {};
            broken.running = true;
            broken.rpm = nanf("");
            Check(needle && isfinite(p.temp) && Near(p.temp, held, 1.0f) && TachShare(broken, k) == 0.0f,
                  "coolant: a needle or a speed that is no number leaves a warm engine as warm as it was");
        }
    }

    // water around the truck, and the game's own clock
    {
        GaugeSettings set;
        SettingsDefaults(set);
        // a truck in gear with its engine running, its nose along x: the front pair of wheels 1.5 m ahead of the
        // chassis, the rear pair 1.5 m behind it
        TruckRaw wet = Truck();
        wet.gear = wet.gearEngaged = 1;
        wet.automatic = 1;
        wet.forward[0] = 1.0f;
        wet.waterKnown = true;
        for (int i = 0; i < 4; i++)
        {
            wet.wheelAt[i][0] = i < 2 ? 1.5f : -1.5f;
            wet.wheelAt[i][2] = (i % 2) ? -0.8f : 0.8f;
        }
        auto depths = [&](float front, float rear) {
            wet.wheelWater[0] = wet.wheelWater[1] = front;
            wet.wheelWater[2] = wet.wheelWater[3] = rear;
        };
        depths(1.6f, 0.2f);
        const float noseIn = TruckWaterDepth(wet);
        depths(0.2f, 1.6f);
        const float tailIn = TruckWaterDepth(wet);
        wet.waterKnown = false;
        const float notRead = TruckWaterDepth(wet);
        wet.waterKnown = true;
        Check(Near(noseIn, 1.6f, 0.001f) && Near(tailIn, 0.2f, 0.001f) && notRead == 0.0f && TempWater(0.5f) == 0.0f && Near(TempWater(0.85f), 0.5f, 0.001f) &&
              TempWater(1.2f) == 1.0f && TempWater(3.0f) == 1.0f,
              "water: the depth that counts is the one at the truck's front; it cools from half a metre and does all it can from 1.2 m");
        depths(1.6f, 1.6f);
        TempModel ford = {};
        TempStep(ford, 1, wet, 1.0f, set, kDt);
        Coolant(ford, kTempRed);
        const float back = Until(ford, wet, 1, set, 100.0f, 1.0f, 1.0f, 0.0f);
        const float settles = Heat(ford, wet, 1, set, 1800.0f, 1.0f, 1.0f, 0.0f);
        const float idles = Heat(ford, wet, 1, set, 1800.0f, 0.0f, 0.0f, 0.0f);
        Check(back > 5.0f && back < 30.0f && settles > kThermostatShut && settles < kThermostatOpen + 2.0f && idles > kTempCold + 5.0f && idles < settles,
              "water: floored in deep water an engine in the red zone is back at 100 C within half a minute and settles at the thermostat, and an idling one stays well above the cold lamp");
        depths(0.85f, 0.85f);
        Coolant(ford, kTempRed);
        const float half = Until(ford, wet, 1, set, 105.0f, 1.0f, 1.0f, 0.0f);
        depths(1.6f, 1.6f);
        TruckRaw stopped = wet;
        stopped.flags = 0;
        Coolant(ford, kTempHot);
        const float cools = Until(ford, stopped, 1, set, kTempCooled, 0.0f, 0.0f, 0.0f);
        const float coldest = Heat(ford, stopped, 1, set, 3.0f * 3600.0f, 0.0f, 0.0f, 0.0f);
        Check(half > 20.0f && half < 90.0f && Near(cools, 180.0f, 3.0f) && coldest >= set.ambientC && coldest < set.ambientC + 0.1f,
              "water: at axle height it does half as much, a stopped engine in deep water cools five times as fast, and nothing cools below the air outside");
        char line[640];
        depths(0.85f, 0.85f);
        TempStep(ford, 1, wet, 1.0f, set, kDt);
        TempProbeText(wet, ford, set, line, sizeof line);
        Check(strstr(line, "water 0.50 at 0.85 m)") != nullptr, "water: the probe's line carries the depth and what the water does");

        ClockWatch watch = {};
        TruckRaw counted = Truck();
        counted.clockKnown = true;
        counted.clock = 1.0f;
        const bool starts = !ClockStands(watch, counted, 1000), soon = !ClockStands(watch, counted, 1900), stands = ClockStands(watch, counted, 2000);
        counted.clock = 1.0333f;
        const bool moves = !ClockStands(watch, counted, 2050);
        counted.clockKnown = false;
        Check(starts && soon && stands && moves && !ClockStands(watch, counted, 9000),
              "clock: the models wait once the game's count of the truck's seconds has stood still for a second, go on when it moves, and never wait for a truck without the count");
    }

    // the oil: it follows the coolant slowly, runs a little over it, keeps its heat longer, and warns when it is too hot
    {
        GaugeSettings set;
        SettingsDefaults(set);
        TruckRaw c = Truck();
        c.gear = c.gearEngaged = 1;
        c.automatic = 1;
        // driven as usual from cold: the coolant at 85 C after WarmUpMinutes, the oil after OilWarmUpMinutes
        auto warmUp = [&](const GaugeSettings &g, float &water85, float &oil85) {
            TempModel m = {};
            TruckRaw e = c;
            e.throttle = -1.0f;
            e.speed = kUsualKmh / 3.6f;
            water85 = oil85 = -1.0f;
            for (float at = kDt; at <= 3600.0f && oil85 < 0.0f; at += kDt)
            {
                TempStep(m, 1, e, kUsualLoad, g, kDt);
                if (water85 < 0.0f && m.temp >= kTempWarm) water85 = at;
                if (oil85 < 0.0f && m.oil >= kTempWarm) oil85 = at;
            }
        };
        TempModel first = {};
        TempStep(first, 1, c, 0.0f, set, kDt);
        float water85, oil85, water85b, oil85b, water85c, oil85c;
        warmUp(set, water85, oil85);
        GaugeSettings slow = set, quick = set;
        slow.oilWarmUpMinutes = 15.0f;
        warmUp(slow, water85b, oil85b);
        Check(Near(first.oil, set.ambientC, 0.05f) && Near(water85, 180.0f, 3.0f) && Near(oil85, 300.0f, 6.0f) && Near(water85b, water85, 0.11f) && Near(oil85b, 900.0f, 8.0f),
              "oil: a cold engine's oil starts at the air outside and is at 85 C after the five minutes of OilWarmUpMinutes, and the setting moves the oil only");
        quick.oilWarmUpMinutes = 2.0f;
        warmUp(quick, water85c, oil85c);
        Check(Near(water85c, water85, 0.11f) && Near(oil85c, water85c + kOilAfter, 6.0f), "oil: set to warm before the coolant, the oil is warm a minute after it");

        // where it settles: over the coolant by the heat
        TempModel m = {};
        TempStep(m, 1, c, 0.0f, set, kDt);
        Heat(m, c, 1, set, 3600.0f, 1.0f, 1.0f, 60.0f);
        const float hard = m.oil - m.temp;
        Heat(m, c, 1, set, 3600.0f, 0.0f, 0.0f, 0.0f);
        const float idle = m.oil - m.temp;
        Check(Near(hard, OilOver(1.0f, 1.0f), 0.3f) && Near(OilOver(1.0f, 1.0f), 14.0f, 0.01f) && Near(idle, OilOver(0.0f, 0.0f), 0.3f) && Near(OilOver(0.7f, 0.7f), 8.96f, 0.01f) &&
              Near(OilOver(0.0f, 1.0f), kOilFriction, 0.01f), "oil: it settles 14 degrees over the coolant floored at the red zone, 9 driven as usual, 4 coasting at the red zone and none at idle");

        // a stopped engine: the oil keeps its heat longer than the coolant
        TruckRaw halted = c;
        halted.flags = 0;
        Coolant(m, kTempHot);
        m.truck[m.at].oil = m.oil = kTempHot;
        Heat(m, halted, 1, set, 900.0f, 0.0f, 0.0f, 0.0f);
        const float slower = set.ambientC + (kTempHot - set.ambientC) * sqrtf((kTempCooled - set.ambientC) / (kTempHot - set.ambientC));
        Check(Near(m.temp, kTempCooled, 0.3f) && Near(m.oil, slower, 0.5f), "oil: when a stopped engine's coolant has cooled from 90 C to 40 C, its oil is still at 58 C");

        // a truck left standing keeps its oil, cooling the oil's way
        TempModel fleet = {};
        TempStep(fleet, 0x10, c, 0.0f, set, 0.0f);
        Heat(fleet, c, 0x10, set, 1200.0f, 1.0f, kUsualLoad, kUsualKmh);
        const float leftOil = fleet.oil;
        Heat(fleet, c, 0x20, set, 600.0f, 1.0f, kUsualLoad, kUsualKmh);
        TempStep(fleet, 0x10, c, 0.0f, set, 0.0f);
        Check(Near(fleet.oil, set.ambientC + (leftOil - set.ambientC) * expf(-TempRatesOf(set).off * kOilCooling * 600.0f), 0.3f),
              "oil: a truck taken again has its oil as it was left, less the oil's cooling");

        // the warning: at once from OilWarnC, again every four seconds, over two degrees under it; not with the engine stopped
        TempModel w = {};
        TempStep(w, 1, c, 0.0f, set, kDt);
        OilWarning ow = {};
        auto beeps = [&](float oil, const TruckRaw &e, float seconds) {
            w.truck[w.at].oil = w.oil = oil;
            int n = 0;
            for (float at = 0.0f; at < seconds - 0.001f; at += kDt) n += OilWarnStep(ow, w, e, set, kDt) ? 1 : 0;
            return n;
        };
        const int under = beeps(set.oilWarnC - 0.5f, c, 10.0f);
        const int over = beeps(set.oilWarnC, c, 10.0f);
        const bool blinks = OilLamp(ow, 0) && !OilLamp(ow, kOilBlinkMs) && OilLamp(ow, 2 * kOilBlinkMs);
        const int band = beeps(set.oilWarnC - kOilBand + 0.2f, c, 4.0f);
        const bool held = ow.on;
        const int below = beeps(set.oilWarnC - kOilBand - 0.2f, c, 10.0f);
        const bool off = !ow.on && !OilLamp(ow, 0);
        const int stopped = beeps(set.oilWarnC + 5.0f, halted, 10.0f);
        Check(under == 0 && over == 3 && blinks && band == 1 && held && below == 0 && off && stopped == 0,
              "oil: the warning sounds at once from OilWarnC and every four seconds, the lamp blinks, and both end two degrees under it or with the engine stopped");
        Check(Near(OilBeepGain(50), 0.4f, 0.001f) && OilBeepGain(0) == 0.0f && Near(OilBeepGain(150), kOilBeepLoud, 0.001f), "oil: the warning is as loud as SoundVolume has it");

        // the probe's line, and a speed that is no number
        Heat(w, c, 1, set, 1.0f, 1.0f, 1.0f, 20.0f);
        w.truck[w.at].oil = w.oil = set.oilWarnC + 1.0f;
        char line[640];
        TempProbeText(c, w, set, line, sizeof line);
        const bool told = strstr(line, "| oil 121.0 C, heading for ") && strstr(line, ", too hot |");
        c.speed = nanf("");
        TempStep(w, 1, c, 1.0f, set, kDt);
        Check(told && isfinite(w.oil), "oil: the probe's line carries the oil, and a step that gives no number leaves it as it was");
    }

    // the turbo's profiles: where the boost comes in and how long it lags
    {
        TruckRaw f = Truck();
        f.gear = f.gearEngaged = 1;
        f.automatic = 1;
        f.throttle = -1.0f;
        const float most = 1.8f;
        // the boost held at one engine speed, and the seconds to nine tenths of it from none
        auto held = [&](int profile, float revs) { return BoostTarget(f, revs, most, profile); };
        auto rise = [&](int profile, float revs) {
            BoostModel b = {};
            const float to = 0.9f * held(profile, revs);
            for (float at = kDt; at < 20.0f; at += kDt)
                if (BoostStep(b, f, revs, most, profile, kDt), b.bar >= to) return at;
            return -1.0f;
        };
        bool full = true;
        for (int p = 0; p < kSpoolCount; p++) full = full && Near(held(p, 1.0f), most, 0.001f) && held(p, 0.0f) == 0.0f;
        const bool early = held(kSpoolQuick, 0.3f) > 2.0f * held(kSpoolStandard, 0.3f) && held(kSpoolLaggy, 0.5f) < 0.5f * held(kSpoolStandard, 0.5f);
        const bool stages = Near(held(kSpoolTwoStage, 0.35f), 0.4f * most, 0.01f) && Near(held(kSpoolTwoStage, 0.45f), 0.4f * most, 0.01f) &&
                            held(kSpoolTwoStage, 0.6f) > 0.6f * most;
        Check(full && early && stages, "spool: every turbo makes all its boost at the red zone and none at idle; the small one comes in early, the big one late, two stages one after the other");
        const float standard = rise(kSpoolStandard, 1.0f), quick = rise(kSpoolQuick, 1.0f), laggy = rise(kSpoolLaggy, 1.0f), lowLaggy = rise(kSpoolLaggy, 0.85f);
        const float small = rise(kSpoolTwoStage, 0.4f);
        Check(Near(standard, 0.35f * logf(10.0f), 0.06f) && quick < 0.6f * standard && laggy > standard && lowLaggy > laggy + 0.1f && small > 0.0f && small < 1.0f,
              "spool: at the red zone the standard boost is there in 0.8 s, the small turbo's sooner and the big one's later, longer still lower down, and the first of two stages within a second");
        GaugeSettings g;
        SettingsDefaults(g);
        g.boostProfile = 7;
        SettingsClamp(g);
        Check(g.boostProfile == kSpoolStandard && &SpoolOf(-1) == &kSpoolProfiles[kSpoolStandard], "spool: a profile the mod does not have is the standard one");
    }

    // what the temperature costs the engine
    {
        GaugeSettings set;
        SettingsDefaults(set);
        set.damage = true;
        Check(DamageHot(kTempRed, set) == 0.0f && DamageHot(100.0f, set) == 0.0f && Near(DamageHot(121.5f, set), 5.0f, 0.001f) &&
              Near(DamageHot(kTempHotDamage, set), 10.0f, 0.001f) && Near(DamageHot(132.0f, set), 20.0f, 0.001f),
              "damage: nothing up to the red zone at 118 C, 10 percent a minute at 125 C, half of that half way there and more above");
        Check(Near(DamageCold(0.0f, 1.0f, set), 5.0f, 0.001f) && Near(DamageCold(15.0f, 1.0f, set), 3.75f, 0.001f) && Near(DamageCold(15.0f, 0.75f, set), 0.9375f, 0.001f) &&
              DamageCold(15.0f, 0.5f, set) == 0.0f && DamageCold(kTempCold, 1.0f, set) == 0.0f && Near(DamageCold(-20.0f, 1.0f, set), 6.667f, 0.01f) &&
              Near(DamageCold(0.0f, 1.1f, set), 5.0f, 0.001f) && DamageCold(0.0f, nanf(""), set) == 0.0f,
              "damage: a cold engine floored at the red zone at 0 C loses 5 percent a minute, less the warmer it is and the lighter the work, nothing at half of that or from 60 C");

        // a minute in the red zone at 125 C, the engine running in gear with the pedal down
        TruckRaw c = Truck();
        c.gear = c.gearEngaged = 1;
        c.automatic = 1;
        c.throttle = -1.0f;
        TempModel temp = {};
        TempStep(temp, 1, c, 0.0f, set, kDt);
        Coolant(temp, kTempHotDamage);
        DamageModel wear = {};
        int points = 0, atOnce = 0;
        for (float at = 0.0f; at < 60.0f; at += kDt)
        {
            const int got = DamageStep(wear, 1, c, temp, 1.0f, set, kDt);
            points += got;
            if (got > atOnce) atOnce = got;
        }
        Check(Near((float)points + wear.owed, 15.0f, 0.02f) && atOnce == 1 && wear.owed >= 0.0f && wear.owed < 1.0f && Near(wear.hot, 10.0f, 0.001f) && wear.cold == 0.0f &&
              DamageAdding(wear, set), "damage: a minute at 125 C costs an engine that takes 150 fifteen points, handed over one whole point at a time");

        // what costs nothing
        TruckRaw stopped = c, unread = c;
        stopped.flags = 0;
        unread.damageKnown = false;
        DamageModel none = {};
        int stoppedPoints = 0;
        for (float at = 0.0f; at < 60.0f; at += kDt) stoppedPoints += DamageStep(none, 1, stopped, temp, 1.0f, set, kDt);
        const bool stoppedCostsNothing = stoppedPoints == 0 && none.hot == 0.0f && !DamageAdding(none, set);
        GaugeSettings shown = set;
        shown.damage = false;
        DamageModel only = {};
        only.id = 1;
        only.owed = 0.9f;
        const int shownPoints = DamageStep(only, 1, c, temp, 1.0f, shown, 60.0f);
        Check(stoppedCostsNothing && DamageStep(none, 1, unread, temp, 1.0f, set, 60.0f) == 0 && shownPoints == 0 && only.owed == 0.0f && Near(only.hot, 10.0f, 0.001f) &&
              !DamageAdding(only, shown), "damage: none for a stopped engine or a truck whose damage is not read; with Damage off nothing comes together and the rate is still worked out");
        wear.owed = 0.7f;
        DamageStep(wear, 2, c, temp, 1.0f, set, 0.0f);
        Check(wear.id == 2 && wear.owed == 0.0f, "damage: the share of a point that is owed stays with its truck");

        // a cold start at 15 km/h: the needle at the red zone, and half way to it; the pedal floored in both, as on a keyboard
        auto coldStart = [&](float revs, float &coldFor) {
            TempModel coolant = {};
            DamageModel cost = {};
            TruckRaw e = c;
            e.throttle = -1.0f;
            e.speed = 15.0f / 3.6f;
            float total = 0.0f;
            coldFor = 0.0f;
            for (float at = 0.0f; at < 900.0f; at += kDt)
            {
                TempStep(coolant, 1, e, revs, set, kDt);
                total += (float)DamageStep(cost, 1, e, coolant, revs, set, kDt);
                if (TempCold(coolant)) coldFor = at;
            }
            return total + cost.owed;
        };
        float revvedFor = 0.0f, gentleFor = 0.0f;
        const float revved = coldStart(1.0f, revvedFor), gentle = coldStart(0.5f, gentleFor);
        Check(revved > 1.5f && revved < 6.0f && revvedFor > 40.0f && revvedFor < 150.0f && gentle == 0.0f && gentleFor > revvedFor,
              "damage: a cold start floored at the red zone costs a few points until the cold lamp goes out, and none floored at half the red zone's engine speed");
        // a parking lot: a cold engine at 2000 rpm of a scout's 4850, floored, and a light foot at the red zone
        TruckRaw lot = c;
        lot.throttle = -1.0f;
        const float floored2000 = TruckLoad(lot, 2000.0f / 4850.0f);
        lot.throttle = -0.45f;
        Check(DamageCold(0.0f, floored2000, set) == 0.0f && DamageCold(0.0f, TruckLoad(lot, 1.0f), set) == 0.0f,
              "damage: driving about cold costs nothing, floored at 2000 rpm of a scout's 4850 or with a light foot at the red zone");

        Check(DamageLamp(true, false, 0) && !DamageLamp(false, false, 0) && DamageLamp(false, true, 0) && !DamageLamp(true, true, kDamageBlinkMs) &&
              DamageLamp(true, true, 2 * kDamageBlinkMs), "damage: the lamp is lit above half the engine's damage, and blinks while damage is added");

        // the probe's line: the engine past its record's threshold, with the effect and the torque factor as the game's rule gives them
        EngineRecord record = {};
        record.found = true;
        strcpy_s(record.name, "us_scout_modern_engine");
        record.threshold = 0.7f;
        record.torqueMin = 1.0f;
        record.torqueMax = 0.6f;
        record.capacity = 150;
        TruckRaw p = c;
        p.damage = 120;
        p.damageSum = 140;
        p.damageSumCapacity = 840;
        TruckEngineEffect(p.damage, p.damageCapacity, record, p.damageEffect, p.torqueFactor);
        char line[768];
        DamageProbeText(p, record, wear, set, line, sizeof line);
        const bool asRead = strstr(line, "engine damage 120 of 150, effect 0.333, torque x0.867") && strstr(line, "(as read)") && strstr(line, "all parts 140 of 840") &&
                            strstr(line, "record us_scout_modern_engine: takes 150") && strstr(line, "Damage on");
        p.torqueFactor = 1.0f;
        DamageProbeText(p, record, wear, set, line, sizeof line);
        const bool notAsRead = strstr(line, "(NOT as read)") != nullptr;
        DamageProbeText(p, EngineRecord{}, wear, shown, line, sizeof line);
        Check(asRead && notAsRead && strstr(line, "looked up once the truck is driven") && strstr(line, "Damage off"),
              "damage: the probe's line carries the engine's damage, what the game's rule makes of it, the record and the model's rates");
    }

    // the boost
    {
        GaugeSettings bs;
        SettingsDefaults(bs);
        const float truckMost = GaugeScaleOf(bs).boost;
        bs.tachClass = kClassScout;
        const float scoutMost = GaugeScaleOf(bs).boost;
        bs.tachClass = kClassHeavy;
        const float heavyMost = GaugeScaleOf(bs).boost;
        bs.boostMax = 1.35f;
        Check(truckMost == 1.8f && scoutMost == 1.0f && heavyMost == 2.0f && GaugeScaleOf(bs).boost == 1.35f,
              "boost: trucks reach 1.8 bar, scouts 1, heavy trucks 2, and a number of the settings takes the class's place");
        const float reach = 1.8f;
        // steps the boost for a while with the needle at revs; returns the largest change in one step
        BoostModel bm = {};
        int sounds = 0;
        auto blow = [&](TruckRaw &truck, float revs, float seconds) {
            float largest = 0.0f;
            for (float at = 0.0f; at < seconds - 0.001f; at += kDt)
            {
                const float before = bm.bar;
                if (BoostStep(bm, truck, revs, reach, kSpoolStandard, kDt) != kLiftNone) sounds++;
                largest = fmaxf(largest, fabsf(bm.bar - before));
            }
            return largest;
        };
        TruckRaw bt = Truck();
        Drive(bt, 3, true, 0.0f, 0.0f);
        blow(bt, 0.0f, 1.0f);
        const float idling = bm.bar;
        Drive(bt, 3, true, 0.9f, 1.0f);
        const float step = blow(bt, 1.0f, 0.8f);
        const float soon = bm.bar;
        blow(bt, 1.0f, 3.0f);
        Check(idling == 0.0f && soon > 0.85f * reach && soon < reach && Near(bm.bar, reach, 0.01f) && step < 0.2f * reach,
              "boost: none at idle; floored at the red zone it is nearly all there after 0.8 s, without a jump");
        bm = BoostModel{};
        blow(bt, 0.3f, 0.8f);
        const float slow = bm.bar;
        blow(bt, 0.3f, 6.0f);
        const float lowEnd = bm.bar;
        blow(bt, 0.1f, 3.0f);
        Check(slow < 0.6f * lowEnd && Near(lowEnd, reach * (0.3f - kBoostRevsFrom) / (kBoostRevsFull - kBoostRevsFrom), 0.01f) && bm.bar < 0.01f,
              "boost: at low engine speed there is less of it and it comes slower, and near idle there is none");
        bm = BoostModel{};
        Drive(bt, 3, true, 0.9f, 0.5f);
        blow(bt, 1.0f, 4.0f);
        const float half = bm.bar;
        Drive(bt, 0, false, 0.0f, 1.0f);
        blow(bt, 1.0f, 4.0f);
        const float neutral = bm.bar;
        bt.flags = 0;
        blow(bt, 1.0f, 2.0f);
        Check(Near(half, reach * powf(0.5f, kBoostPedal), 0.01f) && Near(neutral, reach * kLoadNeutral, 0.01f) && bm.bar < 0.01f && sounds == 0,
              "boost: less than half of it at half pedal, a quarter in neutral, none with the engine off, and no sound without a lift");
        // lifts of the pedal
        auto full = [&]() {
            bt = Truck();
            bm = BoostModel{};
            sounds = 0;
            Drive(bt, 3, true, 0.9f, 1.0f);
            blow(bt, 1.0f, 4.0f);
        };
        full();
        Drive(bt, 3, true, 0.9f, 0.0f);
        const int first = BoostStep(bm, bt, 1.0f, reach, kSpoolStandard, kDt);
        const float loud = bm.lastVolume, at = bm.lastBar;
        blow(bt, 1.0f, 0.4f);
        const float fallen = bm.bar;
        Drive(bt, 3, true, 0.9f, 1.0f);
        blow(bt, 1.0f, 0.1f);
        Drive(bt, 3, true, 0.9f, 0.0f);
        blow(bt, 1.0f, 0.2f);
        Check(first == kLiftBlowOff && Near(loud, 1.0f, 0.01f) && Near(at, reach, 0.01f) && fallen < 0.05f * reach && sounds == 0 && bm.blowOffs == 1,
              "lift: the foot off a floored pedal at full boost is a blow-off at full sound, the boost is gone in 0.4 s, and a second lift right after is silent");
        full();
        Drive(bt, 3, true, 0.9f, 0.7f);
        blow(bt, 1.0f, 0.2f);
        const bool smallLift = sounds == 1 && bm.flutters == 1 && bm.blowOffs == 0;
        full();
        for (int i = 1; i <= 4; i++)
        {
            Drive(bt, 3, true, 0.9f, 1.0f - 0.15f * (float)i);
            blow(bt, 1.0f, kDt);
        }
        const bool quick = sounds == 1 && bm.blowOffs == 1 && bm.flutters == 0;
        full();
        for (int i = 1; i <= 40; i++)
        {
            Drive(bt, 3, true, 0.9f, 1.0f - 0.025f * (float)i);
            blow(bt, 1.0f, kDt);
        }
        blow(bt, 1.0f, 1.0f);
        Check(smallLift && quick && sounds == 0, "lift: a small lift flutters, the pedal let up over 0.2 s is a blow-off, and one let up over two seconds makes no sound");
        bm = BoostModel{};
        bm.running = true;
        bm.bar = 0.15f * reach;
        bt = Truck();
        Drive(bt, 3, true, 0.9f, 1.0f);
        BoostStep(bm, bt, 0.0f, reach, kSpoolStandard, kDt);
        bm.bar = 0.15f * reach;
        Drive(bt, 3, true, 0.9f, 0.0f);
        const int coming = BoostStep(bm, bt, 0.0f, reach, kSpoolStandard, kDt), weak = BoostStep(bm, bt, 0.0f, reach, kSpoolStandard, kDt);
        const float quiet = bm.lastVolume;
        bm = BoostModel{};
        bm.running = true;
        Drive(bt, 3, true, 0.9f, 1.0f);
        BoostStep(bm, bt, 0.0f, reach, kSpoolStandard, kDt);
        bm.bar = 0.05f * reach;
        Drive(bt, 3, true, 0.9f, 0.0f);
        Check(coming == kLiftNone && weak == kLiftFlutter && quiet > 0.0f && quiet < 0.1f && BoostStep(bm, bt, 0.0f, reach, kSpoolStandard, kDt) == kLiftNone &&
              BoostStep(bm, bt, 0.0f, reach, kSpoolStandard, kDt) == kLiftNone, "lift: with little boost a full lift only flutters, quietly, and with next to none it is silent");
        full();
        Drive(bt, 3, true, 0.9f, 0.0f);
        BoostStep(bm, bt, 1.0f, reach, kSpoolStandard, kDt);
        char line[400];
        BoostProbeText(bt, bm, 1.0f, reach, line, sizeof line);
        Check(strstr(line, "of the turbo's 1.80") && strstr(line, "1 blow-offs, 0 flutters, the last a blow-off at 1.80 bar, 1.00 of its full sound"), "boost: the probe's line carries the boost and the lifts");
    }

    // the probe's line
    t = Truck();
    Drive(t, 2, true, 0.5f, 1.0f);
    m = TachModel{};
    TachStep(m, t, k, kDt);
    char text[512];
    ModelProbeText(t, m, r, k, text, sizeof text);
    Check(strstr(text, "wheels 8.50 rad/s of the gear's 17.00") && strstr(text, "pedal 1.00") && strstr(text, "l/min by the game's rule"), "probe: the models' line carries their numbers");

    // the sound of a lift: how loud and how fast
    {
        LiftSound ls;
        const bool none = !LiftSoundOf(kLiftNone, 1.0f, 50, 0.0f, ls) && !LiftSoundOf(kLiftBlowOff, 1.0f, 0, 0.0f, ls) && ls.gain == 0.0f;
        const bool full = LiftSoundOf(kLiftBlowOff, 1.0f, 100, 0.0f, ls) && Near(ls.gain, 1.0f, 0.001f) && Near(ls.pitch, 1.0f, 0.001f);
        const bool half = LiftSoundOf(kLiftFlutter, 1.0f, 50, 0.0f, ls) && Near(ls.gain, 0.5f, 0.001f);
        const bool faint = LiftSoundOf(kLiftFlutter, 0.0f, 100, 0.0f, ls) && Near(ls.gain, 0.2f, 0.001f);
        const bool between = LiftSoundOf(kLiftBlowOff, 0.5f, 100, 0.0f, ls) && Near(ls.gain, 0.6f, 0.001f) && LiftSoundOf(kLiftBlowOff, 7.0f, 400, 0.0f, ls) && Near(ls.gain, 1.0f, 0.001f);
        Check(none && full && half && faint && between, "sound: none without a lift or with the volume at 0; as loud as the setting at full boost, a fifth of that with next to none");
        const bool fullLift = LiftSoundOf(kLiftBlowOff, 1.0f, 50, 0.0f, ls) && ls.tier == kTurboHigh && LiftSoundOf(kLiftBlowOff, 0.5f, 50, 0.0f, ls) && ls.tier == kTurboHigh &&
                              LiftSoundOf(kLiftBlowOff, 0.3f, 50, 0.0f, ls) && ls.tier == kTurboMid;
        const bool smallLift = LiftSoundOf(kLiftFlutter, 0.8f, 50, 0.0f, ls) && ls.tier == kTurboMid && LiftSoundOf(kLiftFlutter, 0.2f, 50, 0.0f, ls) && ls.tier == kTurboLow;
        Check(fullLift && smallLift, "sound: a full lift with much boost is the strong sound and with less the middle one, a small lift the middle and the weak one");
        const bool fast = LiftSoundOf(kLiftBlowOff, 1.0f, 50, 1.0f, ls) && Near(ls.pitch, 1.06f, 0.001f), slow = LiftSoundOf(kLiftBlowOff, 1.0f, 50, -9.0f, ls) && Near(ls.pitch, 0.94f, 0.001f);
        uint32_t seed = 0;
        float least = 1.0f, widest = -1.0f, sum = 0.0f;
        bool same = false;
        for (int i = 0; i < 2000; i++)
        {
            const float sumWas = sum, c = LiftChance(seed);
            least = fminf(least, c);
            widest = fmaxf(widest, c);
            sum += c;
            same = same || (i > 0 && sum == sumWas);
        }
        Check(fast && slow && least >= -1.0f && least < -0.9f && widest <= 1.0f && widest > 0.9f && fabsf(sum) < 100.0f && !same,
              "sound: each lift plays up to six percent faster or slower, by a number that differs from lift to lift");
        // the model's own lifts, as the mod plays them: a full lift at full boost is the strong sound at the setting's
        // loudness, and nothing is played again within 0.8 s
        TruckRaw bt = Truck();
        bt.gear = bt.gearEngaged = 2;
        BoostModel bm = {};
        const float reach = 1.8f;
        int played = 0, first = kLiftNone;
        float firstGain = 0.0f, lastAt = -10.0f, closest = 10.0f;
        for (int i = 0; i < 400; i++)
        {
            // floored for three seconds, then up and down every 0.3 s: more lifts than may sound
            const float at = (float)i * kDt;
            bt.throttle = at < 3.0f || fmodf(at, 0.6f) < 0.3f ? -1.0f : 0.0f;
            const int lift = BoostStep(bm, bt, 1.0f, reach, kSpoolStandard, kDt);
            if (LiftSoundOf(lift, bm.lastVolume, 50, 0.0f, ls))
            {
                if (!played) { first = lift; firstGain = ls.gain; }
                else closest = fminf(closest, at - lastAt);
                lastAt = at;
                played++;
            }
        }
        Check(first == kLiftBlowOff && Near(firstGain, 0.5f, 0.02f) && played >= 2 && closest >= 0.8f - 0.001f,
              "sound: a full lift at full boost plays at the setting's loudness, and no two sounds come within 0.8 s");
    }

    // the sounds themselves: a WAV file read, the two made by code, and every sound brought to one loudness
    {
        // half a second of a 440 Hz tone in 16 bits on two channels, the left one silent
        std::vector<uint8_t> wav(44 + 22050 * 4);
        auto put = [&](size_t at, uint32_t v, int bytes) { for (int i = 0; i < bytes; i++) wav[at + i] = (uint8_t)(v >> (8 * i)); };
        memcpy(&wav[0], "RIFF", 4);
        put(4, (uint32_t)wav.size() - 8, 4);
        memcpy(&wav[8], "WAVEfmt ", 8);
        put(16, 16, 4);
        put(20, 1, 2);
        put(22, 2, 2);
        put(24, 44100, 4);
        put(28, 44100 * 4, 4);
        put(32, 4, 2);
        put(34, 16, 2);
        memcpy(&wav[36], "data", 4);
        put(40, 22050 * 4, 4);
        for (int i = 0; i < 22050; i++) put(44 + (size_t)i * 4 + 2, (uint32_t)(uint16_t)(int16_t)(16000.0 * sin(6.2831853 * 440.0 * i / 44100.0)), 2);
        SoundClip clip;
        std::wstring why;
        const bool read = SoundDecode(wav, clip, why) && clip.rate == 44100 && clip.x.size() == 22050 && Near(SoundRms(clip.x), 16000.0f / 32768.0f / 2.0f * 0.7071f, 0.002f);
        std::vector<uint8_t> bad = wav;
        put(20, 2, 2); // Microsoft ADPCM: not read
        SoundClip none;
        const bool adpcm = !SoundDecode(wav, none, why) && !why.empty();
        bad.resize(30);
        const bool cut = !SoundDecode(bad, none, why);
        memcpy(&bad[0], "OggS", 4);
        Check(read && adpcm && cut && !SoundDecode(bad, none, why), "sound: a WAV file is read as one channel, and what is no such file or of another format is refused with a reason");
        SoundClip blow, flutter;
        SoundMakeBlowOff(blow);
        SoundMakeFlutter(flutter);
        const bool lengths = Near((float)blow.x.size() / blow.rate, 0.7f, 0.01f) && Near((float)flutter.x.size() / flutter.rate, 0.6f, 0.01f);
        // the blow-off is over quickly: nine tenths of it in its first 0.3 s; the flutter comes in puffs: its loudness
        // over 5 ms goes up and down at least eight times in its first 0.4 s
        double early = 0.0, all = 0.0;
        for (size_t i = 0; i < blow.x.size(); i++) { all += (double)blow.x[i] * blow.x[i]; if (i < 13230) early += (double)blow.x[i] * blow.x[i]; }
        int puffs = 0;
        bool up = false;
        double mean = 0.0;
        for (size_t i = 0; i < 17640; i++) mean += fabs(flutter.x[i]) / 17640.0;
        for (size_t i = 0; i + 220 <= 17640; i += 220)
        {
            double level = 0.0;
            for (size_t j = 0; j < 220; j++) level += fabs(flutter.x[i + j]) / 220.0;
            if (!up && level > mean * 1.3) { up = true; puffs++; }
            else if (up && level < mean * 0.8) up = false;
        }
        Check(lengths && early > 0.9 * all && puffs >= 8, "sound: the blow-off made by code is a burst that is over in a third of a second, the flutter a row of puffs");
        // the oil warning: two beeps of 0.13 s with a gap between them, and quiet at both ends of each
        SoundClip beep;
        SoundMakeBeep(beep);
        int beeps = 0;
        bool sounding = false;
        for (size_t i = 0; i + 441 <= beep.x.size(); i += 441)
        {
            double level = 0.0;
            for (size_t j = 0; j < 441; j++) level += fabs(beep.x[i + j]) / 441.0;
            if (!sounding && level > 0.3) { sounding = true; beeps++; }
            else if (sounding && level < 0.05) sounding = false;
        }
        Check(beep.rate == 44100 && Near((float)beep.x.size() / beep.rate, 0.4f, 0.01f) && beeps == 2 && fabsf(beep.x.front()) < 0.01f && fabsf(beep.x.back()) < 0.01f,
              "sound: the oil warning made by code is two short beeps");
        SoundClip loud = clip, quiet = clip;
        for (float &v : quiet.x) v *= 0.01f;
        SoundLevel(loud);
        SoundLevel(quiet);
        SoundLevel(blow);
        SoundLevel(flutter);
        auto peak = [](const SoundClip &c) { float p = 0.0f; for (const float v : c.x) p = fmaxf(p, fabsf(v)); return p; };
        Check(Near(SoundRms(loud.x), SoundRms(quiet.x), 0.001f) && peak(loud) <= kSoundPeak + 0.001f && peak(blow) <= kSoundPeak + 0.001f && peak(flutter) <= kSoundPeak + 0.001f &&
              peak(blow) > 0.2f && peak(flutter) > 0.2f && fabsf(loud.x.back()) < 0.001f,
              "sound: every sound is brought to one loudness with its peaks held, and its end fades out");
    }

    printf(g_failed ? "MODELS TEST FAILED (%d)\n" : "MODELS TEST OK\n", g_failed);
    return g_failed ? 1 : 0;
}
