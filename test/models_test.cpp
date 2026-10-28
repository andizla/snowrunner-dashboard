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
// the game's object for the truck as a hex number, where the table has it. tools\log2drive.py makes such a file from
// the probe's lines of a Gauges.log (python tools\log2drive.py Gauges.log drive.txt); the samples are then a second or
// two apart, and each line also says the automatic's model's gear beside the game's.
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
#include "state.h"
#include "tires.h"

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
// Shifts seen at a replay's samples, and the shifts back the other way within kShiftCountBack seconds, but for those
// made as the truck comes to a stop or pulls away (the driven wheels under kShiftCountStop rad/s).
static const float kShiftCountBack = 5.0f, kShiftCountStop = 1.0f;
struct ShiftCount
{
    int gear, shifts, hunts, way;
    float at;
    void See(int now, float seconds, bool stopping)
    {
        if (gear >= 1 && now != gear)
        {
            const int to = now > gear ? 1 : -1;
            shifts++;
            if (to == -way && seconds - at < kShiftCountBack && !stopping) hunts++;
            way = to;
            at = seconds;
        }
        gear = now;
    }
};

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
    // the powertrain while driving: the pedal as held and as shaped, the power, and the seconds near full power
    PowertrainModel power = {};
    float pedalSum = 0.0f, shapedSum = 0.0f, powerSum = 0.0f, nearFull = 0.0f;
    // the automatic: its shifts against the game's own while the game's automatic drives a numbered gear, both seen at
    // the samples, and the seconds it would have had the converter locked
    GearboxModel box = {};
    ShiftCount gameShifts = {}, boxShifts = {};
    float automaticFor = 0.0f, lockedFor = 0.0f, spinBefore = 0.0f;
    // the brake air: the lowest it fell to, the seconds in the red zone and the governor's cut-outs
    AirModel air = {};
    float airLowest = kAirCutOut, airRed = 0.0f;
    int airCutOuts = 0;
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
            power = PowertrainModel{};
            box = GearboxModel{};
            airCutOuts += air.cutOuts;
            air = AirModel{};
            gameShifts.gear = boxShifts.gear = 0;
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
            PowertrainStep(power, t, kClassCurves[s.tachClass], TachSpeed(tach, k), k.max / k.redline, temp.oil, boost.bar / k.boost, dt);
            GearboxStep(box, t, k, TruckEngineOn(t) ? TruckPedal(t) : 0.0f, power.power, dt);
            AirStep(air, t, TachSpeed(tach, k), dt);
            airLowest = fminf(airLowest, air.bar);
            if (air.bar < kAirWarn) airRed += dt;
            if (t.automatic && t.gearEngaged >= 1 && t.gearEngaged <= TruckTopGear(t) && TruckEngineOn(t))
            {
                automaticFor += dt;
                if (box.locked) lockedFor += dt;
            }
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
                pedalSum += TruckPedal(t) * dt;
                shapedSum += power.pedal * dt;
                powerSum += power.power * dt;
                if (power.power >= 0.9f) nearFull += dt;
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
        if (t.automatic && t.gearEngaged >= 1 && t.gearEngaged <= TruckTopGear(t) && TruckEngineOn(t))
        {
            const bool stopping = fminf(TruckDrivenSpin(t), spinBefore) < kShiftCountStop;
            gameShifts.See(t.gearEngaged, seconds, stopping);
            boxShifts.See(box.gear, seconds, stopping);
        }
        else
            gameShifts.gear = boxShifts.gear = 0;
        spinBefore = TruckDrivenSpin(t);
        last = seconds;
        samples++;
        printf("%8.2f gear %2d %s pedal %.2f wheels %5.2f of %5.2f | %4.0f rpm | %5.1f km/h | fuel %6.2f | range %6.1f km at %.3f l/km over %4.0f m, standing %.2f l/min | "
               "coolant %5.1f C, load %.2f, heat %.2f, %.2f taken away, oil %5.1f C | boost %.2f bar | the automatic's model: gear %d, %s, input %.0f rpm\n", seconds,
               t.gearEngaged, t.automatic ? "A" : " ", TruckPedal(t), TruckDrivenSpin(t), TruckGearTop(t, t.gearEngaged), tach.rpm, t.speed * 3.6f, t.fuel, range.rangeKm,
               range.perKm, range.way - range.mark[0].way, range.standing, temp.temp, temp.load, temp.heat, temp.cooling, temp.oil, boost.bar, box.gear,
               box.locked ? "locked" : "slipping", box.input);
    }
    fclose(f);
    printf("%d samples, %d steps: the largest change of the engine speed in one step %.0f rpm, the highest engine speed %.0f rpm, %d steps in the red zone (from %.0f rpm)\n",
           samples, steps, mostStep, mostRpm, redSteps, k.redline);
    for (const std::string &n : notes) printf("coolant: %s\n", n.c_str());
    printf("oil: the hottest %.1f C\n", hottestOil);
    printf("automatic: in %.0f s of the game's automatic in a numbered gear, seen at the samples, it shifted %d times and back within %.0f s %d times; the model would have "
           "shifted %d times and back %d times, with the converter locked %.0f s\n",
           automaticFor, gameShifts.shifts, kShiftCountBack, gameShifts.hunts, boxShifts.shifts, boxShifts.hunts, lockedFor);
    printf("brake air: at the lowest %.2f bar, %.0f s in the red zone (under %.1f bar), %d cut-outs of the governor\n", airLowest, airRed, kAirWarn, airCutOuts + air.cutOuts);
    printf("powertrain: while driving the pedal was down %.2f on average and %.2f shaped; the power %.2f on average, %.0f s at 0.9 of full or more\n",
           drivingAll > 0.0f ? pedalSum / drivingAll : 0.0f, drivingAll > 0.0f ? shapedSum / drivingAll : 0.0f, drivingAll > 0.0f ? powerSum / drivingAll : 0.0f, nearFull);
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

// ---- the closed-loop drive (review 11, finding 15) ----
// A truck whose speed answers what it gets, stepped at the mod's pass through the chain gauges.cpp runs: the needle
// (TachFollow on the converter's engine speed while the automatic drives, else TachStep), the boost, PowertrainStep,
// GearboxStep with the engine handed over, the game's gear set to TakeoverAutoGear over GearboxLowestFit, and the factor
// from TorqueLift on LiftShare through TorqueHandStep with the pass's dt and the gear. The game's push: push m/s² at the
// pedal floored, GamePull 1 and a factor of 1 in a gear with the first gear's top, times the game's pedal, GamePull at
// the wheels' share of the engaged gear's top, the factor as the game applies it and the first gear's top over the
// gear's; less the drag (drag0 + drag2 v²) and the grade. The game applies a written factor after dead seconds and then
// with a lag of lag seconds: a truck answers a change of push about a second late (review 11). The wheels turn with the
// ground at grip rad/s for each m/s.
struct LoopTruck
{
    TruckRaw t;
    GaugeScale k;
    int cls;
    float grip, push, drag0, drag2, grade, dead, lag;
    float realTop = 1.0f; // the game's own top over the mod's figure for it (TruckGearTop): where the game's pull really ends
    float bumps = 0.0f, bumpPeriod = 2.5f; // the ground's ups and downs: a grade of +- bumps m/s² over bumpPeriod seconds
    int fixedGear;        // 0: the mod's automatic drives the game's gears; else the game's gear held (the player's own, the model follows)
    bool creep = false;   // the creep's share raises the game's throttle (CreepShare: the converter while the automatic drives, else the governor)
    float lowPull = 0.0f; // the game's pull at a standstill, as a share: its target is the throttle times (1 - D4 + D4 times its curve)
    float brakeDecel = 3.0f; // m/s² the brake floored takes off the truck
};
struct LoopState
{
    float v = 0.0f, applied = 1.0f, at = 0.0f; // m/s, the factor as the game applies it, seconds
    float queue[64] = {};                        // the factors written, waiting out the dead time
    int head = 0;
    TachModel tach;
    BoostModel boost;
    PowertrainModel power;
    GearboxModel box;
    TorqueHand hand;
    int gear = 1;        // the game's gear
    int shifts = 0;      // the game's gear changes
    bool paced = false;  // the last write was held back by the pace
    float brake = 0.0f;  // the brake held, 0 to 1 (the pedal then 0)
    float share = 0.0f;  // the creep's share on the last pass
};

// The lift as it was before c9c99ea: the roof fading from 0.8 of the gear's top, written at once.
static float LiftBefore(float curve, float limit, float pedal, float ratio, float x, float most)
{
    if (isfinite(x) && x >= 1.0f) return 1.0f;
    const float roof = !isfinite(x) || x <= 0.8f ? most : 1.0f + (most - 1.0f) * SettingsRange((1.0f - x) / 0.2f, 0.0f, 1.0f);
    const float lift = curve * fminf(1.0f, limit / fmaxf(pedal, 1e-3f)) * ratio / fmaxf(GameFall(x), 1e-3f);
    return SettingsRange(isfinite(lift) ? lift : 1.0f, kTorqueFactorLeast, fmaxf(roof, kTorqueFactorLeast));
}

static void LoopStep(const LoopTruck &c, LoopState &s, const TorqueCurve &curve, float pedal, float dt, bool before)
{
    TruckRaw t = c.t;
    t.flags = 3;
    t.automatic = 0;
    t.gear = t.gearEngaged = c.fixedGear ? c.fixedGear : s.gear;
    t.throttle = s.brake > 0.0f ? s.brake : -pedal;
    t.speed = s.v;
    for (int i = 0; i < t.wheels; i++) t.spin[i] = c.grip * s.v;
    t.torqueFactor = s.hand.on ? s.hand.written : 1.0f;
    const bool driving = !c.fixedGear;
    if (driving) TachFollow(s.tach, t, c.k, s.box.rpm, dt);
    else TachStep(s.tach, t, c.k, dt);
    BoostStep(s.boost, t, TachShare(s.tach, c.k), c.k.boost, 0, dt);
    PowertrainStep(s.power, t, curve, TachSpeed(s.tach, c.k), c.k.max / c.k.redline, 80.0f, c.k.boost > 0.0f ? s.boost.bar / c.k.boost : 0.0f, dt);
    const EngineHand hand{ true, &curve, c.k.max / c.k.redline, 1.0f, 0.0f, 0.0f, EngineLightFloor(curve, c.k.redline), kInputPad, TorqueRoof(c.cls) };
    GearboxStep(s.box, t, c.k, pedal, s.power.power, dt, hand);
    // the factor, as gauges.cpp hands it
    const float x = LiftShare(TruckDrivenSpin(t), s.v, s.box.grip, TruckGearTop(t, t.gearEngaged));
    const float ratio = driving ? s.box.ratio : 1.0f;
    const float limit = s.power.cold * s.power.boost;
    const float lift = before ? LiftBefore(s.power.curve, limit, TruckPedal(t), ratio, x, TorqueRoof(c.cls))
                              : TorqueLift(s.power.curve, limit, TruckPedal(t), ratio, x, TorqueRoof(c.cls));
    const float wanted = s.hand.game * lift;
    TorqueHandStep(s.hand, true, 0x5000, 0x4000, t.torqueFactor, lift, before ? 0.0f : dt, t.gearEngaged);
    s.paced = s.hand.written < wanted - kTorqueFactorStep;
    // the game: the factor after the dead time and the lag, the push on the speed
    const int delay = (int)(c.dead / dt + 0.5f) < 63 ? (int)(c.dead / dt + 0.5f) : 63;
    s.queue[s.head] = s.hand.written;
    const float arrived = s.queue[(s.head + 64 - delay) % 64];
    s.head = (s.head + 1) % 64;
    s.applied += (arrived - s.applied) * (1.0f - expf(-dt / c.lag));
    const float top = TruckGearTop(t, t.gearEngaged), first = TruckGearTop(t, 1);
    const float share = top > 0.0f ? c.grip * s.v / (top * c.realTop) : 0.0f;
    // the hook: the game's throttle raised to the creep's share (the mod hands it on this pass, the game takes it on its steps)
    s.share = c.creep ? CreepShare(t, c.k, driving, 1.0f) : 0.0f;
    const float throttle = s.brake > 0.0f ? 0.0f : fmaxf(pedal, s.share);
    const float pull = c.lowPull + (1.0f - c.lowPull) * GamePull(share);
    const float pushNow = top > 0.0f ? c.push * throttle * pull * s.applied * first / top : 0.0f;
    const float ground = c.grade + c.bumps * sinf(6.2831853f * s.at / c.bumpPeriod);
    const float braking = s.v > 0.0f ? c.brakeDecel * s.brake : 0.0f;
    s.v = fmaxf(0.0f, s.v + (pushNow - c.drag0 - c.drag2 * s.v * s.v - ground - braking) * dt);
    // the game takes the gear the takeover sets, on the next pass
    if (driving)
    {
        const int to = TakeoverAutoGear(t, s.box.gear, GearboxLowestFit(s.box, t, c.k, true));
        GearboxFollowFloor(s.box, to);
        if (to != s.gear) s.shifts++;
        s.gear = to;
    }
    s.at += dt;
}

// What a stretch of a loop drive did: the written factor's and the speed's least and most.
struct LoopSpan { float factorLo = 1e9f, factorHi = 0.0f, vLo = 1e9f, vHi = 0.0f; int pacedPasses = 0; };
static LoopSpan LoopDrive(const LoopTruck &c, LoopState &s, const TorqueCurve &curve, float pedal, float seconds, bool before, bool record = true)
{
    LoopSpan span;
    for (float at = 0.0f; at < seconds; at += kDt)
    {
        LoopStep(c, s, curve, pedal, kDt, before);
        if (!record) continue;
        span.factorLo = fminf(span.factorLo, s.hand.written);
        span.factorHi = fmaxf(span.factorHi, s.hand.written);
        span.vLo = fminf(span.vLo, s.v);
        span.vHi = fmaxf(span.vHi, s.v);
        span.pacedPasses += s.paced;
    }
    return span;
}

// A heavy truck with the gears of a tester's International HX 520 (0.7 1 3 8 14 14: tops 7, 11, 21 and 33 rad/s, its
// high gear 16, slower than its fourth as in the game), wheels of 0.68 m; drag set so three quarters of the pedal in H
// settles in the gear's fade, near 39 km/h, as the tester's did.
static LoopTruck LoopHeavy()
{
    LoopTruck c = {};
    c.t = Truck();
    static const float hx[6] = { 0.7f, 1.0f, 3.0f, 8.0f, 14.0f, 14.0f };
    c.t.gearSpeeds = 6;
    memcpy(c.t.gearSpeed, hx, sizeof hx);
    c.t.powerFactor = 1.0f;
    c.k = kClassScales[kClassHeavy];
    c.cls = kClassHeavy;
    c.grip = 1.48f;
    c.push = 1.6f;
    c.drag0 = 0.12f;
    c.drag2 = 0.0016f;
    c.dead = 0.3f;
    c.lag = 0.5f;
    return c;
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
    Check(Near(runOut, k.redline, 2.0f) && Near(coasting, 0.5f * k.redline, 2.0f) && m.rpm > coasting + 100.0f && m.rpm < runOut - 100.0f,
          "needle: a gear run out stands at the red zone, at half its speed with the wheels at half the gear's with the foot off, higher under the pedal");
    // at zero pedal the wheels turn the engine in proportion, never under idle: a truck creeping in first at a third of
    // the gear's top shows idle (review 11's addendum), and the pedal brings the map from idle in
    Drive(t, 1, true, 0.25f, 0.0f);
    Run(m, t, k, 2.0f);
    const float creepNeedle = m.rpm;
    Drive(t, 1, true, 0.25f, 0.04f);
    Run(m, t, k, 2.0f);
    Check(Near(creepNeedle, k.idle, 2.0f) && m.rpm > creepNeedle + 50.0f && m.rpm < k.idle + 0.25f * span + kTachLoad * span,
          "needle: creeping at idle in first at a quarter of the gear's top it shows idle, and a touch of the pedal lifts it toward the map from idle");
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
        // driven as usual: floored at half the red zone's engine speed, a load of 0.5, at 13 km/h
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
        const float coastingHeat = Heat(temp, c, 1, set, 1800.0f, 0.0f, 1.0f, 60.0f), lugging = Heat(temp, c, 1, set, 1800.0f, 1.0f, 0.0f, 0.0f);
        const float half = Heat(temp, c, 1, set, 1800.0f, 0.5f, 1.0f, 60.0f), halfSpeed = Heat(temp, c, 1, set, 1800.0f, 1.0f, 0.5f, 60.0f);
        Check(Near(coastingHeat, rolling, 0.5f) && Near(lugging, idling, 0.5f) && Near(half, halfSpeed, 0.2f) && half > rolling + 3.0f && half < kTempCruise - 3.0f,
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
            // a load: the game's loading screen ends, and the mod drops the model's memory
            fleet = TempModel{};
            TempStep(fleet, 0x2000, hauler, 0.0f, set, 0.0f);
            Check(leftHauler > set.ambientC + 20.0f && Near(fleet.temp, set.ambientC, 1.0f) && Near(fleet.oil, set.ambientC, 1.0f),
                  "coolant: after a load a kind driven warm before starts as cold as the air outside, the coolant and the oil, as on a first drive");
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
        TempModel settled = {};
        TempStep(settled, 1, c, 0.0f, set, kDt);
        Heat(settled, c, 1, set, 3600.0f, 1.0f, 1.0f, 60.0f);
        const float hard = settled.oil - settled.temp;
        Heat(settled, c, 1, set, 3600.0f, 0.0f, 0.0f, 0.0f);
        const float idleOver = settled.oil - settled.temp;
        Check(Near(hard, OilOver(1.0f, 1.0f), 0.3f) && Near(OilOver(1.0f, 1.0f), 14.0f, 0.01f) && Near(idleOver, OilOver(0.0f, 0.0f), 0.3f) && Near(OilOver(0.7f, 0.7f), 8.96f, 0.01f) &&
              Near(OilOver(0.0f, 1.0f), kOilFriction, 0.01f), "oil: it settles 14 degrees over the coolant floored at the red zone, 9 driven as usual, 4 coasting at the red zone and none at idle");

        // a stopped engine: the oil keeps its heat longer than the coolant
        TruckRaw halted = c;
        halted.flags = 0;
        Coolant(settled, kTempHot);
        settled.truck[settled.at].oil = settled.oil = kTempHot;
        Heat(settled, halted, 1, set, 900.0f, 0.0f, 0.0f, 0.0f);
        const float slower = set.ambientC + (kTempHot - set.ambientC) * sqrtf((kTempCooled - set.ambientC) / (kTempHot - set.ambientC));
        Check(Near(settled.temp, kTempCooled, 0.3f) && Near(settled.oil, slower, 0.5f), "oil: when a stopped engine's coolant has cooled from 90 C to 40 C, its oil is still at 58 C");

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
        const bool dark = !ow.on && !OilLamp(ow, 0);
        const int stopped = beeps(set.oilWarnC + 5.0f, halted, 10.0f);
        Check(under == 0 && over == 3 && blinks && band == 1 && held && below == 0 && dark && stopped == 0,
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
        const float mostBar = 1.8f;
        // the boost held at one engine speed, and the seconds to nine tenths of it from none
        auto held = [&](int profile, float revs) { return BoostTarget(f, revs, mostBar, profile); };
        auto rise = [&](int profile, float revs) {
            BoostModel b = {};
            const float to = 0.9f * held(profile, revs);
            for (float at = kDt; at < 20.0f; at += kDt)
                if (BoostStep(b, f, revs, mostBar, profile, kDt), b.bar >= to) return at;
            return -1.0f;
        };
        bool full = true;
        for (int p = 0; p < kSpoolCount; p++) full = full && Near(held(p, 1.0f), mostBar, 0.001f) && held(p, 0.0f) == 0.0f;
        const bool early = held(kSpoolQuick, 0.3f) > 2.0f * held(kSpoolStandard, 0.3f) && held(kSpoolLaggy, 0.5f) < 0.5f * held(kSpoolStandard, 0.5f);
        const bool stages = Near(held(kSpoolTwoStage, 0.35f), 0.4f * mostBar, 0.01f) && Near(held(kSpoolTwoStage, 0.45f), 0.4f * mostBar, 0.01f) &&
                            held(kSpoolTwoStage, 0.6f) > 0.6f * mostBar;
        Check(full && early && stages, "spool: every turbo makes all its boost at the red zone and none at idle; the small one comes in early, the big one late, two stages one after the other");
        const float standard = rise(kSpoolStandard, 1.0f), quick = rise(kSpoolQuick, 1.0f), laggy = rise(kSpoolLaggy, 1.0f), lowLaggy = rise(kSpoolLaggy, 0.85f);
        const float firstStage = rise(kSpoolTwoStage, 0.4f);
        Check(Near(standard, 0.35f * logf(10.0f), 0.06f) && quick < 0.6f * standard && laggy > standard && lowLaggy > laggy + 0.1f && firstStage > 0.0f && firstStage < 1.0f,
              "spool: at the red zone the standard boost is there in 0.8 s, the small turbo's sooner and the big one's later, longer still lower down, and the first of two stages within a second");
        GaugeSettings g;
        SettingsDefaults(g);
        g.boostProfile = 7;
        SettingsClamp(g);
        Check(g.boostProfile == kSpoolStandard && &SpoolOf(-1) == &kSpoolProfiles[kSpoolStandard], "spool: a profile the mod does not have is the standard one");
    }

    // the powertrain: the shaped pedal, the torque curves, the cold and the boost
    {
        const bool shape = ThrottleShape(0.0f) == 0.0f && ThrottleShape(1.0f) == 1.0f && Near(ThrottleShape(0.5f), 0.287f, 0.002f) && ThrottleShape(0.3f) < ThrottleShape(0.31f) &&
                           ThrottleShape(1.5f) == 1.0f && ThrottleShape(-1.0f) == 0.0f;
        Check(shape, "powertrain: the pedal is shaped progressively: half way down gives 0.29, and up and floored stay as they are");
        // a keyboard's pedal: floored at once, held, then let go
        TruckRaw f = Truck();
        f.gear = f.gearEngaged = 1;
        f.automatic = 1;
        PowertrainModel pm = {};
        const TorqueCurve &truckCurve = kClassCurves[kClassTruck];
        const float top = 3000.0f / 2200.0f;
        f.throttle = 0.0f;
        PowertrainStep(pm, f, truckCurve, 0.7f, top, 90.0f, 1.0f, kDt);
        f.throttle = -1.0f;
        float half = -1.0f, full = -1.0f;
        for (float at = kDt; at < 2.0f; at += kDt)
        {
            PowertrainStep(pm, f, truckCurve, 0.7f, top, 90.0f, 1.0f, kDt);
            if (half < 0.0f && pm.pedal >= 0.5f) half = at;
            if (full < 0.0f && pm.pedal >= 1.0f) full = at;
        }
        const bool atFull = Near(pm.power, 1.0f, 0.001f) && Near(pm.load, 0.7f, 0.001f);
        f.throttle = 0.0f;
        float gone = -1.0f;
        for (float at = kDt; at < 1.0f && gone < 0.0f; at += kDt)
        {
            PowertrainStep(pm, f, truckCurve, 0.7f, top, 90.0f, 1.0f, kDt);
            if (pm.pedal <= 0.0f) gone = at;
        }
        Check(Near(half, 0.3f, 0.06f) && Near(full, kThrottleUp, 0.06f) && atFull && gone > 0.0f && gone <= kThrottleDown + 0.06f,
              "powertrain: a pedal floored at once comes in over 0.6 s and goes in 0.15 s; floored warm at full boost on the plateau it is all the power, and burns 0.7 at 0.7 of the red zone's speed");
        // the curves
        const bool truck = Near(TorqueAt(truckCurve, 0.1f, top), 0.55f, 0.001f) && Near(TorqueAt(truckCurve, 0.35f, top), 0.7f, 0.001f) &&
                           Near(TorqueAt(truckCurve, 0.7f, top), 1.0f, 0.001f) && Near(TorqueAt(truckCurve, 1.0f, top), 0.85f, 0.001f) &&
                           Near(TorqueAt(truckCurve, (1.0f + top) * 0.5f, top), 0.425f, 0.001f) && TorqueAt(truckCurve, top, top) == 0.0f && TorqueAt(truckCurve, 2.0f, top) == 0.0f;
        bool peaks = true;
        for (int c = 0; c < kClassCount; c++)
        {
            float mostBar = 0.0f;
            for (float x = 0.0f; x <= 1.0f; x += 0.01f) mostBar = fmaxf(mostBar, TorqueAt(kClassCurves[c], x, top));
            peaks = peaks && Near(mostBar, 1.0f, 0.001f) && TorqueAt(kClassCurves[c], 1.0f, top) >= 0.8f && TorqueAt(kClassCurves[c], 0.0f, top) >= 0.5f;
        }
        Check(truck && peaks && TorqueAt(truckCurve, nanf(""), top) == 0.0f,
              "powertrain: each class's curve peaks at its most, keeps 0.8 of it at the red zone and half at idle, and falls to nothing at the tachometer's top");
        TorqueCurve own = {}, kept = kClassCurves[kClassScout];
        const bool read = TorqueCurveParse("0.3:0.6, 0.6:1 ,1:0.8", own) && own.n == 3 && own.x[1] == 0.6f && own.y[2] == 0.8f;
        const bool refused = !TorqueCurveParse("0.6:1,0.3:0.5", kept) && !TorqueCurveParse("abc", kept) && !TorqueCurveParse("0.3:1.2,0.6:1", kept) &&
                             !TorqueCurveParse("1:1", kept) && !TorqueCurveParse("0.3:0.6,0.6:1,", kept) && !TorqueCurveParse("0:0.5,0.5:1", kept) && kept.n == kClassCurves[kClassScout].n;
        Check(read && refused, "powertrain: a truck's own curve is read from \"x:y,x:y\", and text that is no rising curve of 2 to 8 points is refused");
        // the cold and the boost: floored at the plateau with cold oil and no boost yet, and with the engine off
        PowertrainModel cold = {};
        f.throttle = -1.0f;
        for (float at = 0.0f; at < 1.0f; at += kDt) PowertrainStep(cold, f, truckCurve, 0.7f, top, 0.0f, 0.0f, kDt);
        const bool weak = Near(cold.cold, 1.0f - kColdTorque, 0.001f) && Near(cold.boost, 1.0f - kBoostTorque, 0.001f) &&
                          Near(cold.power, (1.0f - kColdTorque) * (1.0f - kBoostTorque), 0.001f);
        TruckRaw stopped = f;
        stopped.flags = 0;
        PowertrainStep(cold, stopped, truckCurve, 0.7f, top, 90.0f, 1.0f, kDt);
        Check(weak && cold.power == 0.0f && cold.load == 0.0f, "powertrain: cold oil takes 15 percent of the torque and no boost 35 percent; a stopped engine delivers nothing");
    }

    // the automatic gearbox: the converter, the shift points, the hysteresis, the kickdown, a descent, a climb and wheelspin
    {
        TruckRaw g = Truck();
        g.gear = g.gearEngaged = 1;
        g.automatic = 1;
        // all driven wheels at one speed
        auto wheels = [&](float spin) { for (int i = 0; i < g.wheels; i++) g.spin[i] = spin; };
        // a model already driving in a gear at the wheels' speed
        auto driving = [&](int gear) {
            GearboxModel m = {};
            m.running = true;
            m.gear = gear;
            m.spin = m.by = TruckDrivenSpin(g);
            return m;
        };
        // standing: the converter at the stall floored, and creeping at idle
        GearboxModel box = {};
        wheels(0.0f);
        GearboxStep(box, g, k, 1.0f, 1.0f, kDt);
        const bool stall = box.gear == 1 && !box.locked && Near(box.rpm, ConverterStall(k), 1.0f) && Near(box.ratio, kStallRatio, 0.001f) && Near(box.drive, kStallRatio, 0.001f);
        GearboxStep(box, g, k, 0.0f, 0.0f, kDt);
        const bool creep = Near(box.rpm, k.idle, 1.0f) && Near(box.drive, kCreep * kStallRatio, 0.001f);
        Check(stall && creep, "automatic: standing, a floored engine runs at the converter's stall with the torque doubled, and an idling one creeps");
        // a truck's converter (review 11): the stall at 0.8 of the red zone, at most 2600 rpm (a truck 1760, a heavy truck
        // 1520, a scout's 5500 rpm engine 2600), where the truck's curve gives all its torque and the heavy truck's 0.96 (a
        // scout's 0.85); a floored launch from a standstill, gaining, lifts the engine from the stall through the launch and
        // never drops it by more than the lock's speed ratio from one pass to the next
        {
            const float truckStall = ConverterStall(kClassScales[kClassTruck]), heavyStall = ConverterStall(kClassScales[kClassHeavy]),
                        scoutStall = ConverterStall(kClassScales[kClassScout]);
            auto share = [](int c, float rpm) {
                const GaugeScale &cs = kClassScales[c];
                return TorqueAt(kClassCurves[c], rpm / cs.redline, cs.max / cs.redline);
            };
            const bool torque = Near(share(kClassTruck, truckStall), 1.0f, 0.001f) && share(kClassHeavy, heavyStall) > 0.95f && share(kClassScout, scoutStall) > 0.8f;
            GearboxModel launch = {};
            float spin = 0.0f, startRpm = 0.0f, lastRpm = 0.0f, mostDrop = 0.0f, rpmBeforeLock = 0.0f;
            bool climbed = false;
            for (float at = 0.0f; at < 4.0f && launch.gear <= 1; at += kDt)
            {
                wheels(spin);
                GearboxStep(launch, g, k, 1.0f, 1.0f, kDt);
                if (launch.gear != 1) break;
                if (at == 0.0f) startRpm = launch.rpm;
                if (!launch.locked) rpmBeforeLock = launch.rpm;
                if (lastRpm > 0.0f) mostDrop = fmaxf(mostDrop, 1.0f - launch.rpm / lastRpm);
                climbed = climbed || launch.rpm > startRpm + 50.0f;
                lastRpm = launch.rpm;
                spin += 0.5f * TruckGearTop(g, 1) * kDt;
            }
            printf("     the converter: stalls %.0f, %.0f and %.0f rpm; a floored launch from %.0f rpm, %.0f before the lock, the most drop a pass %.3f\n", truckStall,
                   heavyStall, scoutStall, startRpm, rpmBeforeLock, mostDrop);
            Check(Near(truckStall, 1760.0f, 0.5f) && Near(heavyStall, 1520.0f, 0.5f) && Near(scoutStall, 2600.0f, 0.5f) && torque && Near(startRpm, ConverterStall(k), 1.0f) &&
                      climbed && mostDrop <= 1.0f - kLockRatio + 0.01f,
                  "automatic: a truck's converter stalls at 0.8 of the red zone, at most 2600 rpm, on the diesels' plateau; a floored launch lifts the "
                  "engine from the stall and the lock drops it no more than its speed ratio allows");
        }
        // a floored pull from a standstill to the top gear's top in 40 seconds, and the same at a light foot
        auto pull = [&](float pedal, float &firstShiftAt, float &shortestPause, bool &lockedOnce) {
            GearboxModel b = {};
            firstShiftAt = -1.0f;
            shortestPause = 100.0f;
            lockedOnce = false;
            float lastShift = -100.0f;
            const float topSpin = TruckGearTop(g, TruckTopGear(g));
            for (float at = 0.0f; at < 40.0f; at += kDt)
            {
                const float spin = topSpin * at / 40.0f;
                wheels(spin);
                const int before = b.gear;
                GearboxStep(b, g, k, pedal, pedal, kDt);
                if (b.gear != before && before >= 1) // its first gear is no shift
                {
                    if (firstShiftAt < 0.0f) firstShiftAt = b.spin; // the speed the gearbox goes by
                    shortestPause = fminf(shortestPause, at - lastShift);
                    lastShift = at;
                }
                lockedOnce = lockedOnce || b.locked;
            }
            return b;
        };
        float floorShift, floorPause, lightShift, lightPause;
        bool floorLocked, lightLocked;
        const GearboxModel floored = pull(1.0f, floorShift, floorPause, floorLocked), light = pull(0.3f, lightShift, lightPause, lightLocked);
        // floored, the first shift is called for where the second gear pulls more in the game (and lands over its own
        // point down), and made kShiftUpAfterFloored later
        const float floorPoint = fmaxf(TruckGearTop(g, 1) * GearboxUpPoint(g, k, 1, 1.0f) / k.redline, TruckGearTop(g, 2) * GearboxDownPoint(g, k, 2, 1.0f) / k.redline);
        const float gaining = TruckGearTop(g, TruckTopGear(g)) / 40.0f;
        Check(floored.gear == 4 && floored.shifts == 3 && light.shifts == 3 && floorShift >= floorPoint && floorShift <= floorPoint + gaining * kShiftUpAfterFloored + 0.1f && lightShift < floorShift - 0.2f &&
                  floorPause >= kShiftPause && floorLocked && lightLocked,
              "automatic: pulling away it shifts through each gear once, late when floored and early at a light foot, a second apart, and locks the converter up");
        // a steady speed between the shift points with the pedal wandering: the gear holds
        wheels(10.0f);
        GearboxModel hold = driving(2);
        uint32_t seed = 7;
        for (float at = 0.0f; at < 60.0f; at += kDt) GearboxStep(hold, g, k, 0.45f + 0.15f * LiftChance(seed), 0.5f, kDt);
        Check(hold.shifts <= 1, "automatic: at a steady speed with the pedal wandering between 0.3 and 0.6 the gear holds");
        // floored in the second gear at 0.73 of its top (short of the floored point), then the foot lifted: no shift up
        // with the foot off, however long; one once a light foot is back on
        wheels(TruckGearTop(g, 2) * 0.73f);
        GearboxModel lift = driving(2);
        for (float at = 0.0f; at < 2.0f; at += kDt) GearboxStep(lift, g, k, 1.0f, 1.0f, kDt);
        const int floorGear = lift.gear;
        for (float at = 0.0f; at < 4.0f; at += kDt) GearboxStep(lift, g, k, 0.0f, 0.0f, kDt);
        const int liftedGear = lift.gear;
        for (float at = 0.0f; at < 1.0f; at += kDt) GearboxStep(lift, g, k, 0.2f, 0.2f, kDt);
        Check(floorGear == 2 && liftedGear == 2 && lift.gear == 3, "automatic: with the foot lifted it never shifts up short of the red zone; a light foot back on shifts up");
        // the foot off at a low speed in the top gear: down at once to the gear that keeps the engine between the
        // points, skipping one
        wheels(10.0f);
        GearboxModel coast = driving(4);
        for (float at = 0.0f; at < kCoastDownAfter + 0.1f; at += kDt) GearboxStep(coast, g, k, 0.0f, 0.0f, kDt);
        Check(coast.gear == 2 && coast.shifts == 1 && GearboxInput(g, k, 2, 10.0f) < GearboxPoint(k, kCoastDownTo),
              "automatic: with the foot off at a low speed it shifts down early, gears at a time, the engine kept under the coast point's top");
        // a push of the pedal past kKickdown at a low speed kicks down at once, skipping a gear, to the gear that pulls
        // the most in the game, and holds it while the pedal stays down until the next gear pulls more; held there
        // without a push it shifts down the same way once the shift down has been called for kShiftDownAfter
        wheels(12.0f);
        GearboxModel kick = driving(4);
        for (float at = 0.0f; at < kKickArm + 0.05f; at += kDt) GearboxStep(kick, g, k, 0.3f, 0.3f, kDt);
        const int beforeKick = kick.gear;
        GearboxStep(kick, g, k, 1.0f, 1.0f, kDt);
        GearboxModel held4 = driving(4);
        held4.pedal = held4.held = 1.0f;
        GearboxStep(held4, g, k, 1.0f, 1.0f, kDt);
        const int heldFirst = held4.gear;
        for (float at = 0.0f; at < kShiftDownAfter + 0.1f; at += kDt) GearboxStep(held4, g, k, 1.0f, 1.0f, kDt);
        float kickUpAt = -1.0f;
        GearboxModel kickHold = kick;
        for (float at = 0.0f; at < 4.0f && kickUpAt < 0.0f; at += kDt)
        {
            wheels(12.0f + 2.0f * at);
            GearboxStep(kickHold, g, k, 1.0f, 1.0f, kDt);
            if (kickHold.gear == 3) kickUpAt = GearboxInput(g, k, 2, TruckDrivenSpin(g));
        }
        wheels(12.0f);
        Check(beforeKick == 4 && kick.gear == 2 && kick.shifts == 1 && heldFirst == 4 && held4.gear == 2 && held4.shifts == 1 && kickUpAt >= GearboxUpPoint(g, k, 2, 1.0f) - 1.0f &&
                  kickUpAt < GearboxUpPoint(g, k, 2, 1.0f) + 200.0f,
              "automatic: a push of the pedal kicks down at once, gears at a time, to the gear that pulls the most in the game, and holds it while floored until "
              "the next gear pulls more; a pedal held down shifts down as far, a little later");
        // a kickdown is a push from part throttle. A press from the foot off, as every press of a key is, arms none,
        // however long the foot was off; nor does a pedal that passes through part throttle on its way down in one
        // pass, or one that rested under kKickFrom. From half throttle held kKickArm the push kicks down at once
        {
            wheels(12.0f);
            GearboxModel key = driving(4);
            for (float at = 0.0f; at < kFootGap + kKickArm + 0.1f; at += kDt) GearboxStep(key, g, k, 0.0f, 0.0f, kDt);
            GearboxModel through = key, light = driving(4), half = driving(4);
            const int keyGear = key.gear, keyShifts = key.shifts;
            GearboxStep(key, g, k, 1.0f, 1.0f, kDt);
            const bool fromOff = key.kick <= 0.0f && key.gear == keyGear && key.shifts == keyShifts;
            GearboxStep(through, g, k, 0.5f, 0.5f, kDt);
            GearboxStep(through, g, k, 1.0f, 1.0f, kDt);
            for (float at = 0.0f; at < kKickArm + 0.05f; at += kDt) GearboxStep(light, g, k, kKickFrom - 0.05f, kKickFrom - 0.05f, kDt);
            const int lightGear = light.gear;
            GearboxStep(light, g, k, 1.0f, 1.0f, kDt);
            for (float at = 0.0f; at < kKickArm + 0.05f; at += kDt) GearboxStep(half, g, k, 0.5f, 0.5f, kDt);
            const int halfGear = half.gear;
            GearboxStep(half, g, k, 1.0f, 1.0f, kDt);
            Check(fromOff && through.kick <= 0.0f && light.kick <= 0.0f && light.gear == lightGear && halfGear == 4 && half.gear == 2 && half.shifts == 1,
                  "automatic: a kickdown is a push from part throttle; a press from the foot off, a pedal on its way through and one that rested under a fifth kick nothing down");
        }
        // the input in use. Keys: no kickdown from any pedal, and a key held shifts as floored, however far the game
        // has ramped it. A pad: a trigger wavering near its end (0.85, then full) kicks nothing down, a push from half
        // does, also through a pass between; a wheel's pedal kicks down from 0.85 as well
        {
            wheels(12.0f);
            auto pushed = [&](int input, float from) {
                EngineHand how = {};
                how.input = input;
                GearboxModel m = driving(4);
                for (float at = 0.0f; at < kKickArm + 0.05f; at += kDt) GearboxStep(m, g, k, from, from, kDt, how);
                const int was = m.gear;
                GearboxStep(m, g, k, 1.0f, 1.0f, kDt, how);
                return was == 4 && m.gear < 4 && m.shifts == 1;
            };
            EngineHand pad = {}, keys = {};
            pad.input = kInputPad;
            keys.input = kInputKeys;
            GearboxModel slow = driving(4);
            for (float at = 0.0f; at < kKickArm + 0.1f; at += kDt) GearboxStep(slow, g, k, 0.5f, 0.5f, kDt, pad);
            GearboxStep(slow, g, k, 0.8f, 0.8f, kDt, pad);
            GearboxStep(slow, g, k, 1.0f, 1.0f, kDt, pad);
            // keys ramped to 0.3 shift as a wheel's pedal floored does, and not as one at 0.3
            GearboxModel ramped = driving(4), light = driving(4), floored = driving(4);
            for (float at = 0.0f; at < kShiftDownAfter + 0.3f; at += kDt)
            {
                GearboxStep(ramped, g, k, 0.3f, 0.3f, kDt, keys);
                GearboxStep(light, g, k, 0.3f, 0.3f, kDt);
                GearboxStep(floored, g, k, 1.0f, 1.0f, kDt);
            }
            const bool wheel = pushed(kInputWheel, 0.5f) && pushed(kInputWheel, 0.85f), trigger = pushed(kInputPad, 0.5f) && !pushed(kInputPad, 0.85f),
                       through = slow.gear < 4 && slow.shifts == 1, key = !pushed(kInputKeys, 0.5f) && ramped.gear == floored.gear && ramped.shifts == floored.shifts && ramped.gear < light.gear && ramped.kick <= 0.0f;
            if (!(wheel && trigger && through && key))
                printf("     wheel %d, trigger %d, through %d (gear %d, %d shifts), key %d (light %d, ramped %d)\n", wheel, trigger, through, slow.gear, slow.shifts, key, light.gear,
                       ramped.gear);
            Check(wheel && trigger && through && key,
                  "automatic: by the input in use: a pad's trigger kicks down only from under three quarters, keys kick nothing down and shift as floored");
        }
        // floored with the game's throttle cut to nothing for a pass now and then (as at its shifts): no kickdown, no
        // shift down with the foot off; a lift that lasts is one
        wheels(TruckGearTop(g, 2) * 0.6f);
        GearboxModel cuts = driving(2);
        cuts.pedal = cuts.held = 1.0f;
        int cutShifts = 0;
        for (int i = 0; i < 120; i++)
        {
            const int before = cuts.gear;
            GearboxStep(cuts, g, k, i % 6 == 3 ? 0.0f : 1.0f, 1.0f, kDt);
            cutShifts += cuts.gear < before;
        }
        GearboxModel lifted = cuts;
        for (float at = 0.0f; at < kFootGap + 0.05f; at += kDt) GearboxStep(lifted, g, k, 0.0f, 0.0f, kDt);
        Check(cutShifts == 0 && cuts.kick <= 0.0f && lifted.held == 0.0f,
              "automatic: the game's throttle cut for a moment is no lift and no push to kick down; a lift that lasts kFootGap is one");
        // a wheel spinning by itself at a standstill: no floor under the gears before the grip is learned, nor after it
        // while the ground's speed is nothing
        {
            TruckRaw one = g;
            for (int i = 0; i < one.wheels; i++) one.spin[i] = 0.0f;
            one.spin[3] = 20.0f;
            one.speed = 0.0f;
            GearboxModel standing = driving(1);
            GearboxStep(standing, one, k, 1.0f, 1.0f, kDt);
            const int blindFit = GearboxLowestFit(standing, one, k);
            standing.grip = 2.2f;
            for (float at = 0.0f; at < 1.0f; at += kDt) GearboxStep(standing, one, k, 1.0f, 1.0f, kDt);
            Check(blindFit == 1 && GearboxLowestFit(standing, one, k) == 1 && standing.gear == 1,
                  "automatic: a wheel spinning by itself at a standstill sets no floor under the gears and shifts nothing up");
        }
        // rolling downhill with the foot off the gear holds until the engine would pass the red zone, on the flat too
        wheels(9.0f);
        GearboxModel down = driving(2);
        GearboxStep(down, g, k, 0.0f, 0.0f, kDt);
        bool held = true;
        for (float at = 0.0f; at < 10.0f; at += kDt)
        {
            wheels(9.0f + 1.0f * at);
            GearboxStep(down, g, k, 0.0f, 0.0f, kDt);
            if (GearboxInput(g, k, 2, g.spin[0]) <= k.redline && down.gear != 2) held = false;
        }
        wheels(14.0f);
        GearboxModel flat = driving(2);
        for (float at = 0.0f; at < 2.0f; at += kDt) GearboxStep(flat, g, k, 0.0f, 0.0f, kDt);
        Check(held && down.gear == 3 && flat.gear == 3 && flat.shifts == 1,
              "automatic: rolling downhill with the foot off it holds the gear for the engine's braking and shifts up only past the red zone; on the flat a lift at "
              "speed shifts up one gear, where the next lands over the coast point");
        // a lift slowing hard (mud) keeps the gear; coasting holds it down to kCoastDown, and with the brake on it shifts down
        // from kCoastDownBraking, for the engine's braking, and never up
        {
            GearboxModel mud = driving(2);
            for (float at = 0.0f; at < 2.0f; at += kDt)
            {
                wheels(14.0f - 3.0f * at);
                GearboxStep(mud, g, k, 0.0f, 0.0f, kDt);
            }
            const float third = 1000.0f / k.redline * TruckGearTop(g, 3); // the third at 1000 rpm: between the two coast points
            wheels(third);
            GearboxModel coasting = driving(3), braked = driving(3);
            for (float at = 0.0f; at < 1.0f; at += kDt) GearboxStep(coasting, g, k, 0.0f, 0.0f, kDt);
            g.throttle = 0.5f; // the brake: the pedal's other side
            for (float at = 0.0f; at < 1.0f; at += kDt) GearboxStep(braked, g, k, 0.0f, 0.0f, kDt);
            g.throttle = 0.0f;
            wheels(14.0f);
            GearboxModel brakedFast = driving(2);
            g.throttle = 0.5f;
            for (float at = 0.0f; at < 2.0f; at += kDt) GearboxStep(brakedFast, g, k, 0.0f, 0.0f, kDt);
            g.throttle = 0.0f;
            Check(mud.gear <= 2 && coasting.gear == 3 && coasting.shifts == 0 && GearboxInput(g, k, 3, third) < GearboxPoint(k, kCoastDownBraking) &&
                      GearboxInput(g, k, 3, third) > GearboxPoint(k, kCoastDown) && braked.gear == 2 && brakedFast.gear == 2,
                  "automatic: a lift slowing hard keeps the gear; coasting holds it down to 0.15 of the way from idle to the red zone, with the brake on "
                  "it shifts down from 0.25 and never up");
        }
        // a lift keeps the converter locked, the engine at the gearbox's input (no flare, the engine braking with the
        // gear held), while the input stays over the unlock point; slowing under it opens the converter, and a stop
        // keeps it open (two players' reports, 28 October: the truck felt like a rubber band)
        {
            const float lockSpeed = 1200.0f / k.redline * TruckGearTop(g, 3);
            wheels(lockSpeed);
            GearboxModel tip = driving(3);
            for (float at = 0.0f; at < 2.0f; at += kDt) GearboxStep(tip, g, k, 0.5f, 0.5f, kDt);
            const bool lockedBefore = tip.locked;
            bool stayed = true;
            for (float at = 0.0f; at < 2.0f; at += kDt)
            {
                GearboxStep(tip, g, k, 0.0f, 0.0f, kDt);
                stayed = stayed && tip.locked && Near(tip.rpm, fmaxf(tip.input, k.idle), 1.0f);
            }
            const bool liftKept = lockedBefore && stayed && tip.shifts == 0 && tip.gear == 3;
            // slowing with the foot off in the first gear (in a higher one the coast shift down comes before the unlock
            // point): locked down past the unlock point (kUnlockBelow of the lock point), open under it
            const float firstLocked = 1000.0f / k.redline * TruckGearTop(g, 1);
            wheels(firstLocked);
            GearboxModel slow = driving(1);
            slow.locked = true;
            float openedAt = -1.0f;
            for (float at = 0.0f; at < 10.0f; at += kDt)
            {
                wheels(firstLocked * (1.0f - at / 10.0f));
                GearboxStep(slow, g, k, 0.0f, 0.0f, kDt);
                if (!slow.locked && openedAt < 0.0f) openedAt = slow.input;
            }
            const float unlockAt = k.redline * kUnlockBelow; // with the game's own engine the lock point is kLockFrom at any pedal
            Check(liftKept && slow.gear == 1 && openedAt > 0.0f && openedAt < unlockAt + 1.0f && openedAt > unlockAt - 60.0f && !slow.locked,
                  "automatic: a lift keeps the converter locked, the engine at the gearbox's input; slowing under the unlock point opens it");
        }
        // after a shift up made locked the converter locks again on the next pass, the input over the lock point; the
        // first gear locks too, past the lock point once the speed ratio reaches kLockRatio, and a launch from a
        // standstill stays open under it
        {
            wheels(0.95f * TruckGearTop(g, 2));
            GearboxModel up = driving(2);
            up.locked = true;
            up.feel = up.held = up.pedal = 1.0f;
            up.moving = 10.0f;
            bool shifted = false, openOnShift = false;
            for (int i = 0; i < 200 && !shifted; i++)
            {
                const int was = up.gear;
                GearboxStep(up, g, k, 1.0f, 1.0f, kDt);
                shifted = up.gear > was;
                openOnShift = shifted && !up.locked;
            }
            // the wheels past the floored lock point in the new gear: locked on the next pass, within the shifts' pause
            wheels(0.6f * TruckGearTop(g, up.gear));
            GearboxStep(up, g, k, 1.0f, 1.0f, kDt);
            const bool relocked = shifted && openOnShift && up.gear == 3 && up.locked && up.pause > 0.0f;
            // the launch in first at half the pedal: from a standstill, gaining; open under the lock point, locked past it
            // once the speed ratio is kLockRatio
            GearboxModel launch = {};
            float spin = 0.0f, lockedFrom = -1.0f, ratioAtLock = 0.0f, lastInput = 0.0f, lastRpm = 0.0f;
            bool openUnder = true;
            for (float at = 0.0f; at < 6.0f && launch.gear <= 1; at += kDt)
            {
                wheels(spin);
                const bool was = launch.locked;
                GearboxStep(launch, g, k, 0.5f, 0.5f, kDt);
                if (launch.gear != 1) break;
                if (launch.locked && !was && lockedFrom < 0.0f)
                {
                    lockedFrom = launch.input;
                    ratioAtLock = lastRpm > 0.0f ? lastInput / lastRpm : 0.0f;
                }
                if (launch.locked && launch.input < k.redline * kLockFrom) openUnder = false;
                lastInput = launch.input;
                lastRpm = launch.rpm;
                spin += 0.2f * TruckGearTop(g, 1) * kDt;
            }
            GearboxModel standing = {};
            wheels(0.0f);
            for (float at = 0.0f; at < 1.0f; at += kDt) GearboxStep(standing, g, k, 1.0f, 1.0f, kDt);
            Check(relocked && lockedFrom >= k.redline * kLockFrom - 1.0f && ratioAtLock >= kLockRatio - 0.02f && openUnder && !standing.locked &&
                      Near(standing.ratio, kStallRatio, 0.001f),
                  "automatic: after a shift up made locked the converter locks again on the next pass; the first gear locks past the lock point once the "
                  "speed ratio is 0.92, and a launch from a standstill goes through the open converter under it");
        }
        // keys tapped to hold the speed (a tester's ZIL-131 on 0.15.8, 2026-10-19: the fourth's and the fifth's tops 20
        // and 23 rad/s, 650 to 3200 rpm, the key held a fraction of a second at a time, 0.15.8 swapping the two at the
        // taps): the taps keep the gear, as a lift shifts up only once the foot has been off kLiftAfter; a lift that lasts
        // shifts up, and the next tap taking it back keeps lifts from shifting up kLiftBlock, twice as long the next time
        {
            TruckRaw z = g;
            static const float zilSpeeds[7] = { 0.7f, 1.0f, 2.2f, 4.0f, 7.5f, 9.0f, 7.0f };
            z.gearSpeeds = 7;
            memcpy(z.gearSpeed, zilSpeeds, sizeof zilSpeeds);
            z.speed = 0.0f;
            const float zs = 0.7f * TruckGearTop(z, 4); // short of 4th's floored point up, which the lift's fade brought to about 0.8 of its top
            for (int i = 0; i < z.wheels; i++) z.spin[i] = zs;
            GaugeScale zk = k;
            zk.idle = 650.0f;
            zk.redline = 3200.0f;
            zk.max = 3400.0f;
            EngineHand zh{ true, &kClassCurves[kClassTruck], 3400.0f / 3200.0f, 1.0f };
            zh.least = kLightPeak * 1800.0f;
            zh.input = kInputKeys;
            GearboxModel zb = {};
            zb.running = true;
            zb.gear = 4;
            zb.pedal = zb.held = zb.feel = 1.0f;
            zb.spin = zb.by = zb.rolling = zs;
            // the key held on seconds and let go off seconds by turns
            auto keyed = [&](float seconds, float on, float off) {
                for (float at = 0.0f; at < seconds; at += kDt) GearboxStep(zb, z, zk, fmodf(at, on + off) < on ? 1.0f : 0.0f, 1.0f, kDt, zh);
            };
            keyed(30.0f, 0.3f, 0.7f);
            const int tapShifts = zb.shifts;
            keyed(4.0f, 0.0f, 1.0f);
            const int liftedTo = zb.gear;
            keyed(1.0f, 1.0f, 0.0f);
            const int tappedTo = zb.gear;
            const float block = zb.liftBlock;
            keyed(4.0f, 0.0f, 1.0f);
            const int blockedIn = zb.gear;
            keyed(10.0f, 0.0f, 1.0f);
            const int afterBlock = zb.gear;
            keyed(1.0f, 1.0f, 0.0f);
            // the same taps on a wheel's pedals: as 0.15.8, a lift shifts up at once (only keys wait)
            GearboxModel wb = {};
            wb.running = true;
            wb.gear = 4;
            wb.pedal = wb.held = wb.feel = 1.0f;
            wb.spin = wb.by = wb.rolling = zs;
            EngineHand wheel = zh;
            wheel.input = kInputWheel;
            for (float at = 0.0f; at < 1.0f; at += kDt) GearboxStep(wb, z, zk, at < 0.3f ? 1.0f : 0.0f, 1.0f, kDt, wheel);
            const int wheelLifted = wb.gear;
            printf("    keys: %d shifts in 30 s of taps; a lift to %d, a tap to %d, blocked %.1f s, in %d after 4 s, %d after 14 s; then %d, the next block %.0f s; "
                   "a wheel's pedal lifted 0.7 s: in %d\n",
                   tapShifts, liftedTo, tappedTo, block, blockedIn, afterBlock, zb.gear, zb.liftBlockWait, wheelLifted);
            Check(tapShifts == 0 && liftedTo == 5 && tappedTo == 4 && Near(block, kLiftBlock, 0.6f) && blockedIn == 4 && afterBlock == 5 && zb.gear == 4 &&
                      Near(zb.liftBlockWait, 2.0f * kLiftBlock, 0.001f) && wheelLifted == 5,
                  "automatic, keys: taps of the key to hold the speed keep the gear; a lift of 2 s shifts up, the next tap takes it back and lifts "
                  "shift nothing up for 10 s, then 20; a wheel's pedal lifted shifts up as before");
        }
        // floored, the point up goes where the next gear pulls more at a truck's roof of 6 as it fades over the gear's last
        // three tenths (a tester's ZIL-131 with its own profile, a naturally aspirated peak at 1800 of a 3200 red zone: 2816
        // to 2864 rpm; fading over the last fifth 2880 to 2928; with the flat roof of 6 it was 3024 to 3056, under
        // kEngineUpFloored's 3072)
        {
            TruckRaw z = g;
            static const float zilSpeeds[7] = { 0.7f, 1.0f, 2.2f, 4.0f, 7.5f, 9.0f, 7.0f };
            z.gearSpeeds = 7;
            memcpy(z.gearSpeed, zilSpeeds, sizeof zilSpeeds);
            GaugeScale zk = k;
            zk.idle = 650.0f;
            zk.redline = 3200.0f;
            zk.max = 3400.0f;
            const TorqueCurve zil = EngineCurveFromPeak(1800.0f / zk.redline, true);
            EngineHand six{ true, &zil, zk.max / zk.redline, 1.0f };
            float sixLow = 1e9f, sixHigh = 0.0f;
            for (int gear = 1; gear < 5; gear++)
            {
                const float at6 = GearboxUpPoint(z, zk, gear, 1.0f, six);
                sixLow = fminf(sixLow, at6);
                sixHigh = fmaxf(sixHigh, at6);
            }
            printf("    roof of 6: the ZIL's floored points up %.0f to %.0f rpm (the limiter %.0f)\n", sixLow, sixHigh, GearboxPoint(zk, kEngineLimit));
            Check(sixLow > 2780.0f && sixHigh < 2900.0f && sixHigh < GearboxPoint(zk, kEngineUpFloored),
                  "powertrain: at a truck's roof of 6 fading over the gear's last three tenths a ZIL-131's floored points up lie where the next gear pulls "
                  "more, about 2840 rpm");
            Check(TorqueHeld(6.0f, 0.25f) == 1.5f && TorqueHeld(1.0f, 0.25f) == 0.25f && TorqueHeld(0.1f, 0.25f) == kTorqueFactorLeast && TorqueHeld(3.0f, 0.0f) == 3.0f &&
                      TorqueHeld(3.0f, 1.0f) == 3.0f && TorqueHeld(3.0f, 1.5f) == 3.0f,
                  "torque held: a quarter of the push for the roof of 6 is 1.5 and for the game's own 1 is 0.25, never under the least; none set or a whole share leaves it");
        }
        // cruising, as a car's nine-speed automatic did on the road (2026-10-19): a light foot held steady shifts up as
        // soon as the next gear lands over kLightPeak of the peak torque's speed and kShiftRoom over its point down, short
        // of the light point up; not while gaining, nor at a heavier foot
        {
            TruckRaw c = g;
            static const float closeSpeeds[7] = { 1.5f, 3.0f, 5.0f, 7.5f, 10.5f, 14.5f, 8.0f }; // tops 11, 15, 20, 26 and 34 rad/s
            c.gearSpeeds = 7;
            memcpy(c.gearSpeed, closeSpeeds, sizeof closeSpeeds);
            GaugeScale car = k;
            car.idle = 750.0f;
            car.redline = 4500.0f;
            car.max = 5000.0f;
            EngineHand peak = {};
            peak.least = kLightPeak * 1600.0f;
            const float from = 2200.0f / car.redline * TruckGearTop(c, 3); // the third at 2200 rpm
            auto cruiseFrom = [&](float pedal, float gain, float rpm) {
                GearboxModel m = {};
                m.running = true;
                m.gear = 3;
                float spin = rpm / car.redline * TruckGearTop(c, 3);
                m.spin = m.by = spin;
                for (float at = 0.0f; at < 3.0f; at += kDt)
                {
                    for (int i = 0; i < c.wheels; i++) c.spin[i] = spin;
                    GearboxStep(m, c, car, pedal, pedal, kDt, peak);
                    spin += gain * kDt;
                }
                return m;
            };
            // gaining from 2000 rpm, short of the light point up in the three seconds
            const GearboxModel steady = cruiseFrom(0.3f, 0.0f, 2200.0f), gaining = cruiseFrom(0.3f, 0.25f, 2000.0f), heavier = cruiseFrom(0.6f, 0.0f, 2200.0f);
            const float landed = GearboxInput(c, car, 4, from);
            if (!(GearboxUpPoint(c, car, 3, 0.3f) > 2200.0f && steady.gear == 4 && steady.shifts == 1 && landed >= peak.least && gaining.gear == 3 && heavier.gear == 3))
                printf("     up point %.0f, steady %d (%d shifts), landed %.0f, gaining %d, heavier %d\n", GearboxUpPoint(c, car, 3, 0.3f), steady.gear, steady.shifts, landed,
                       gaining.gear, heavier.gear);
            Check(GearboxUpPoint(c, car, 3, 0.3f) > 2200.0f && steady.gear == 4 && steady.shifts == 1 && landed >= peak.least && gaining.gear == 3 && heavier.gear == 3,
                  "automatic: a light foot held steady cruises a gear up where the next lands over 0.8 of the peak torque's speed, short of the light point up; "
                  "gaining or at a heavier foot it keeps the gear");
        }
        // climbing: the wheels losing speed with the foot half down. It shifts down where floored would, and does not
        // shift up while the climb's hold lasts after the slowing stops
        GearboxModel climb = driving(3), flatter = driving(3);
        float climbDownAt = -1.0f, flatterDownAt = -1.0f;
        for (float at = 0.0f; at < 6.0f; at += kDt)
        {
            wheels(16.0f - at);
            GearboxStep(climb, g, k, 0.5f, 0.5f, kDt);
            if (climb.gear == 2 && climbDownAt < 0.0f) climbDownAt = at;
        }
        const int climbShifts = climb.shifts;
        for (float at = 0.0f; at < kClimbHold - 0.5f; at += kDt) GearboxStep(climb, g, k, 0.5f, 0.5f, kDt);
        // the same speeds without the climb: a light foot's lower point
        for (float at = 0.0f; at < 6.0f; at += kDt)
        {
            wheels(16.0f - at);
            flatter.trend = 0.0f;
            flatter.climb = 0.0f;
            GearboxStep(flatter, g, k, 0.5f, 0.5f, kDt);
            if (flatter.gear == 2 && flatterDownAt < 0.0f) flatterDownAt = at;
        }
        Check(climbDownAt > 0.0f && (flatterDownAt < 0.0f || climbDownAt < flatterDownAt - 0.3f) && climb.gear == 2 && climb.shifts == climbShifts,
              "automatic: climbing it shifts down where floored would, and holds the gear while the climb's hold lasts");
        // wheelspin: with the ground's speed learned, wheels spinning up in the first gear while the truck crawls do
        // not shift up; without the ground's speed they would
        {
            TruckRaw mud = g;
            auto mudWheels = [&](float spin) { for (int i = 0; i < mud.wheels; i++) mud.spin[i] = spin; };
            mudWheels(6.0f);
            mud.speed = 6.0f / 2.2f;
            GearboxModel spinning = driving(1);
            spinning.spin = spinning.by = 6.0f;
            for (float at = 0.0f; at < 1.0f; at += kDt) GearboxStep(spinning, mud, k, 0.5f, 0.5f, kDt);
            const bool learned = Near(spinning.grip, 2.2f, 0.01f) && !spinning.spinning;
            // a bump: the wheels slowed to half for a moment while the truck rolls on; and the handbrake held with the
            // foot on. Neither is learned
            GearboxModel bumped = spinning;
            TruckRaw bump = mud;
            for (int i = 0; i < bump.wheels; i++) bump.spin[i] = 3.0f;
            for (float at = 0.0f; at < 0.3f; at += kDt) GearboxStep(bumped, bump, k, 0.5f, 0.5f, kDt);
            bump.handbrake = 1;
            for (float at = 0.0f; at < 2.0f; at += kDt) GearboxStep(bumped, bump, k, 0.5f, 0.5f, kDt);
            // sliding back down a slope with the wheels turning forwards steadily: the truck goes nowhere forwards
            TruckRaw sliding = mud;
            sliding.speed = -2.0f * mud.speed;
            for (float at = 0.0f; at < 3.0f; at += kDt) GearboxStep(bumped, sliding, k, 0.5f, 0.5f, kDt);
            // a lower reading held steadily is followed
            GearboxModel lowered = spinning;
            TruckRaw firmer = mud;
            firmer.speed = 6.0f / 2.0f;
            for (float at = 0.0f; at < 6.0f; at += kDt) GearboxStep(lowered, firmer, k, 0.5f, 0.5f, kDt);
            const bool robust = Near(bumped.grip, 2.2f, 0.01f) && lowered.grip < 2.05f && lowered.grip >= 2.0f;
            mudWheels(10.5f);
            mud.speed = 2.5f;
            for (float at = 0.0f; at < 3.0f; at += kDt) GearboxStep(spinning, mud, k, 1.0f, 1.0f, kDt);
            TruckRaw unknown = mud;
            unknown.speed = 0.0f;
            GearboxModel blind = driving(1);
            for (float at = 0.0f; at < 3.0f; at += kDt) GearboxStep(blind, unknown, k, 1.0f, 1.0f, kDt);
            Check(learned && robust && spinning.spinning && spinning.gear == 1 && blind.gear == 2 && spinning.input > blind.input - 1.0f,
                  "automatic: wheels spinning faster than the ground goes do not shift up; the engine still turns with them; the wheels' speed against the "
                  "ground's is learned where they turn steadily, not from a bump, the handbrake or the truck sliding back");
            // fast in the top gear, the grip learned: floored, and a push of the pedal after a light foot, never shift
            // down into a gear the engine cannot take at the speed the truck goes
            TruckRaw fast = g;
            for (int i = 0; i < fast.wheels; i++) fast.spin[i] = 30.0f;
            fast.speed = 30.0f / 2.2f;
            GearboxModel misled = driving(4);
            misled.spin = misled.by = 30.0f;
            misled.grip = 2.2f;
            for (float at = 0.0f; at < 2.0f; at += kDt) GearboxStep(misled, fast, k, 1.0f, 1.0f, kDt);
            for (float at = 0.0f; at < kFootGap + kKickArm + 0.1f; at += kDt) GearboxStep(misled, fast, k, 0.3f, 0.3f, kDt);
            GearboxStep(misled, fast, k, 1.0f, 1.0f, kDt);
            Check(misled.gear == 4 && GearboxInput(fast, k, 3, 30.0f) > GearboxPoint(k, kDownMost),
                  "automatic: no shift down, kickdown or not, puts the engine near the red zone at the speed the wheels turn");
        }
        // the game's pull (action + 0xB4, read floored and steady on the Defender and the sooty): all of the torque at
        // half the gear's top, falling in a line to nothing at the top, and less under 0.45
        Check(Near(GamePull(0.5f), 1.0f, 0.001f) && Near(GamePull(0.725f), 0.58f, 0.03f) && Near(GamePull(0.875f), 0.26f, 0.03f) && GamePull(1.02f) == 0.0f &&
                  Near(GamePull(0.325f), 0.66f, 0.03f) && GamePull(nanf("")) == 0.0f && GearUpShare(1.0f) == 1.0f && GearDownShare(1.0f) == 0.0f,
              "automatic: the game gives all of the torque with the wheels at half the gear's top and nothing at the top, as action + 0xB4 read");
        // the sooty's gears (tops 9, 17, 26, 34, 42 and 55 rad/s, 800 to 5600 rpm, a drive logged in the game at 18:23) floored up a
        // climb at a steady speed, the grip learned
        {
            TruckRaw so = g;
            static const float sootySpeeds[8] = { 2.0f, 2.0f, 6.0f, 10.5f, 14.5f, 18.5f, 25.0f, 21.0f };
            so.gearSpeeds = 8;
            memcpy(so.gearSpeed, sootySpeeds, sizeof sootySpeeds);
            GaugeScale sk = k;
            sk.idle = 800.0f;
            sk.redline = 5600.0f;
            sk.max = 7000.0f;
            auto sootyAt = [&](float spin) {
                for (int i = 0; i < so.wheels; i++) so.spin[i] = spin;
                so.speed = spin / 1.93f;
            };
            auto sootyDriving = [&](int gear) {
                GearboxModel sb = {};
                sb.running = true;
                sb.gear = gear;
                sb.spin = sb.by = sb.rolling = TruckDrivenSpin(so);
                sb.grip = 1.93f;
                sb.pedal = sb.held = sb.feel = 1.0f;
                return sb;
            };
            // the second at 0.47 of its top, the pedal pushed: the first, at 0.89 of its own, pulls under half of what
            // the second does, so no kickdown, and floored no shift down (at 18:35:20 it went 1 to 2 and back six times)
            sootyAt(0.47f * 17.0f);
            GearboxModel second = sootyDriving(2);
            for (float at = 0.0f; at < kFootGap + kKickArm + 0.1f; at += kDt) GearboxStep(second, so, sk, 0.3f, 0.3f, kDt);
            for (float at = 0.0f; at < 2.0f; at += kDt) GearboxStep(second, so, sk, 1.0f, 1.0f, kDt);
            // the fifth at 0.40 of its top: down to the fourth (at 0.49), not on to the third (at 0.65, which pulls less)
            sootyAt(0.40f * 42.0f);
            GearboxModel fifth = sootyDriving(5);
            for (float at = 0.0f; at < 3.0f; at += kDt) GearboxStep(fifth, so, sk, 1.0f, 1.0f, kDt);
            // the third at 0.69 of its top: up to the fourth (at 0.53), which pulls a sixth more (from 18:36:39 the third
            // was held 13 s at 0.57 to 0.69, waiting for 0.83)
            sootyAt(0.69f * 26.0f);
            GearboxModel third = sootyDriving(3);
            for (float at = 0.0f; at < 1.0f; at += kDt) GearboxStep(third, so, sk, 1.0f, 1.0f, kDt);
            Check(second.gear == 2 && second.shifts == 0 && fifth.gear == 4 && fifth.shifts == 1 && third.gear == 4 && third.shifts == 1,
                  "automatic: floored it takes the gear that pulls the most in the game: no kickdown into a first near its top, the fifth at 0.4 of its top "
                  "down to the fourth, the third at 0.69 of its top up to the fourth");
            // the second floored with the ground at 0.56 of its top and the wheels spinning 1.35 times as fast: no shift
            // up (the third pulls less at the truck's speed); wheels spinning pull no harder in a taller gear
            GearboxModel spinUp = sootyDriving(2);
            for (int i = 0; i < so.wheels; i++) so.spin[i] = 0.56f * 17.0f * 1.35f;
            so.speed = 0.56f * 17.0f / 1.93f;
            spinUp.spin = TruckDrivenSpin(so);
            for (float at = 0.0f; at < 1.0f; at += kDt) GearboxStep(spinUp, so, sk, 1.0f, 1.0f, kDt);
            Check(spinUp.gear == 2 && spinUp.shifts == 0, "automatic: wheels spinning faster than the ground goes shift nothing up, floored or not");
        }
        // the Tacoma's gears (tops 8, 11, 17, 25, 41 and 53 rad/s, 800 to 5600 rpm), the grip learned: floored from a
        // standstill its first gear is through in about a second, and the shift up comes near its point all the same
        // (0.67, and kShiftUpAfterFloored on: 0.79), not as late as the smoothed wheels had it (0.88 of the top at
        // 21:33:54); and up a climb
        // that slows the truck from the fifth hard, it is down in the gear that pulls the most within a second of
        // the slowing's end, gears at a time
        {
            TruckRaw ta = g;
            static const float tacomaSpeeds[8] = { 3.0f, 1.5f, 3.0f, 6.0f, 10.0f, 18.0f, 24.0f, 10.0f };
            ta.gearSpeeds = 8;
            memcpy(ta.gearSpeed, tacomaSpeeds, sizeof tacomaSpeeds);
            GaugeScale tk2 = k;
            tk2.idle = 800.0f;
            tk2.redline = 5600.0f;
            tk2.max = 7000.0f;
            auto tacomaAt = [&](float spin) {
                for (int i = 0; i < ta.wheels; i++) ta.spin[i] = spin;
                ta.speed = spin / 2.1f;
            };
            GearboxModel launch = {};
            launch.running = true;
            launch.gear = 1;
            launch.grip = 2.1f;
            float firstUpAt = -1.0f;
            for (float at = 0.0f; at < 1.5f && firstUpAt < 0.0f; at += kDt)
            {
                tacomaAt(7.0f * at);
                GearboxStep(launch, ta, tk2, 1.0f, 1.0f, kDt);
                if (launch.gear > 1) firstUpAt = 7.0f * at / TruckGearTop(ta, 1);
            }
            GearboxModel slowing = {};
            slowing.running = true;
            slowing.gear = 5;
            slowing.grip = 2.1f;
            tacomaAt(20.0f);
            slowing.spin = slowing.by = slowing.rolling = 20.0f;
            slowing.pedal = slowing.held = slowing.feel = 1.0f;
            for (float at = 0.0f; at < 2.0f; at += kDt)
            {
                tacomaAt(20.0f - 7.0f * at);
                GearboxStep(slowing, ta, tk2, 1.0f, 1.0f, kDt);
            }
            for (float at = 0.0f; at < 1.0f; at += kDt) GearboxStep(slowing, ta, tk2, 1.0f, 1.0f, kDt);
            Check(firstUpAt > 0.6f && firstUpAt < 0.82f && slowing.gear == 2 && slowing.shifts <= 3,
                  "automatic: floored from a standstill the first gear is left near its point however fast it goes by; slowing hard up a climb it is "
                  "down in the gear that pulls the most within a second, gears at a time");
        }
        // the engine handed to the game (Powertrain=1): the factor lifts the game's curve where it falls away toward the
        // gear's top, by the engine's torque and the converter's ratio, at the most 6 times, and the game's own at the
        // gear's top and past it; it stands on the game's own
        // share, follows the game where it writes its own (engine damage), and gives it back when the switch goes off and
        // to the last truck when another is driven
        {
            // the roof by class (a scout's 3, a truck's and a heavy truck's 6) and its fade over a gear's last three tenths:
            // flat to 0.7 of the top, then down to 1 at the top (a vanilla Defender's runaway, 27 October; from 0.8 until a
            // tester's HX 520 pulsed at its top gear's limit, 28 October)
            const float scoutRoof = TorqueRoof(kClassScout), truckRoof = TorqueRoof(kClassTruck);
            const bool roofs = scoutRoof == 3.0f && truckRoof == 6.0f && TorqueRoof(kClassHeavy) == 6.0f &&
                               LiftRoof(truckRoof, 0.5f) == 6.0f && LiftRoof(truckRoof, 0.7f) == 6.0f && Near(LiftRoof(truckRoof, 0.8f), 13.0f / 3.0f, 0.001f) &&
                               Near(LiftRoof(truckRoof, 0.85f), 3.5f, 0.001f) && Near(LiftRoof(truckRoof, 0.9f), 8.0f / 3.0f, 0.001f) &&
                               Near(LiftRoof(truckRoof, 0.955f), 1.75f, 0.001f) && Near(LiftRoof(truckRoof, 1.0f), 1.0f, 0.001f) &&
                               LiftRoof(scoutRoof, 0.5f) == 3.0f && LiftRoof(scoutRoof, 0.7f) == 3.0f && Near(LiftRoof(scoutRoof, 0.85f), 2.0f, 0.001f) &&
                               Near(LiftRoof(scoutRoof, 0.9f), 5.0f / 3.0f, 0.001f) && Near(LiftRoof(scoutRoof, 0.95f), 4.0f / 3.0f, 0.001f) && Near(LiftRoof(scoutRoof, 1.0f), 1.0f, 0.001f) &&
                               Near(TorqueLift(1.0f, 1.0f, 1.0f, 1.0f, 0.8f, scoutRoof), fminf(1.0f / (kGamePullFall * 0.2f), 7.0f / 3.0f), 0.01f) &&
                               Near(TorqueLift(1.0f, 1.0f, 1.0f, 1.0f, 0.9f, scoutRoof), 5.0f / 3.0f, 0.01f);
            printf("     the lift's roof at 0.5, 0.8, 0.85, 0.9, 0.95 and 1.0 of a gear's top: a truck's %.2f %.2f %.2f %.2f %.2f %.2f, a scout's %.2f %.2f %.2f %.2f %.2f %.2f\n",
                   LiftRoof(truckRoof, 0.5f), LiftRoof(truckRoof, 0.8f), LiftRoof(truckRoof, 0.85f), LiftRoof(truckRoof, 0.9f), LiftRoof(truckRoof, 0.95f), LiftRoof(truckRoof, 1.0f),
                   LiftRoof(scoutRoof, 0.5f), LiftRoof(scoutRoof, 0.8f), LiftRoof(scoutRoof, 0.85f), LiftRoof(scoutRoof, 0.9f), LiftRoof(scoutRoof, 0.95f), LiftRoof(scoutRoof, 1.0f));
            const bool lifts = roofs && Near(TorqueLift(1.0f, 1.0f, 1.0f, 1.0f, 0.5f), 1.0f, 0.001f) &&
                               Near(TorqueLift(1.0f, 1.0f, 1.0f, 1.0f, 0.8f), 1.0f / (kGamePullFall * 0.2f), 0.01f) &&
                               Near(TorqueLift(1.0f, 1.0f, 1.0f, 1.0f, 0.97f), 1.5f, 0.01f) &&
                               Near(TorqueLift(1.0f, 1.0f, 1.0f, 1.0f, 0.9f), fminf(1.0f / (kGamePullFall * 0.1f), 8.0f / 3.0f), 0.01f) && TorqueLift(1.0f, 1.0f, 1.0f, 1.0f, 1.0f) == 1.0f &&
                               TorqueLift(1.0f, 1.0f, 1.0f, 1.0f, 1.3f) == 1.0f && Near(TorqueLift(1.0f, 1.0f, 1.0f, kStallRatio, 0.1f), kStallRatio, 0.001f) &&
                               Near(TorqueLift(0.8f, 0.55f, 1.0f, 1.0f, 0.3f), 0.8f * 0.55f, 0.001f) && Near(TorqueLift(0.8f, 0.55f, 0.4f, 1.0f, 0.3f), 0.8f, 0.001f) &&
                               TorqueLift(0.0f, 1.0f, 1.0f, 1.0f, 0.5f) == kTorqueFactorLeast && TorqueLift(nanf(""), 1.0f, 1.0f, 1.0f, 0.5f) == 1.0f;
            TorqueHand hand = {};
            const TorqueOrders first = TorqueHandStep(hand, true, 0x5000, 0x4000, 0.9f, 2.0f);
            const TorqueOrders same = TorqueHandStep(hand, true, 0x5000, 0x4000, first.factor, 2.0f);
            const TorqueOrders moved = TorqueHandStep(hand, true, 0x5000, 0x4000, first.factor, 1.5f);
            const TorqueOrders gameWrote = TorqueHandStep(hand, true, 0x5000, 0x4000, 0.8f, 1.5f);
            const bool followed = first.write && first.action == 0x5000 && Near(first.factor, 1.8f, 0.001f) && !same.write && moved.write && Near(moved.factor, 1.35f, 0.001f) &&
                                  gameWrote.write && Near(gameWrote.factor, 1.2f, 0.001f) && Near(hand.game, 0.8f, 0.001f);
            const TorqueOrders switchedOff = TorqueHandStep(hand, false, 0x5000, 0x4000, 1.2f, 1.5f);
            const TorqueOrders again = TorqueHandStep(hand, true, 0x5000, 0x4000, 0.8f, 1.5f);
            const TorqueOrders other = TorqueHandStep(hand, true, 0x7000, 0x6000, 1.0f, 1.5f);
            const TorqueOrders taken = TorqueHandStep(hand, true, 0x7000, 0x6000, 1.0f, 1.5f);
            const bool givenBack = switchedOff.write && switchedOff.action == 0x5000 && Near(switchedOff.factor, 0.8f, 0.001f) && again.write &&
                                   other.write && other.action == 0x5000 && other.vehicle == 0x4000 && Near(other.factor, 0.8f, 0.001f) && taken.write &&
                                   taken.action == 0x7000 && Near(taken.factor, 1.5f, 0.001f);
            // a factor left over above 1 (a test, or the mod's own) is taken as the game's 1
            TorqueHand left = {};
            const TorqueOrders over = TorqueHandStep(left, true, 0x5000, 0x4000, 2.4f, 1.0f);
            // a rebuilt action that reads the factor the mod wrote last keeps the game's share (review 5, G-R1): the damage cut
            // of 0.8 stays through a rebuild above 1, and below 1 the share does not sink with each rebuild
            TorqueHand rebuilt = {};
            TorqueHandStep(rebuilt, true, 0x5000, 0x4000, 0.8f, 1.5f);                       // the game's 0.8: writes 1.2
            const TorqueOrders r1 = TorqueHandStep(rebuilt, true, 0x9000, 0x4000, 1.2f, 1.5f); // the rebuilt action reads 1.2: the old one gets 0.8 back
            const TorqueOrders r2 = TorqueHandStep(rebuilt, true, 0x9000, 0x4000, 1.2f, 1.5f); // and 1.2 stands, not 1.5
            TorqueHand sinking = {};
            TorqueHandStep(sinking, true, 0x5000, 0x4000, 1.0f, 0.6f);                       // the game's 1: writes 0.6
            TorqueHandStep(sinking, true, 0xA000, 0x4000, 0.6f, 0.6f);
            const TorqueOrders s2 = TorqueHandStep(sinking, true, 0xA000, 0x4000, 0.6f, 0.6f); // 0.6 stands, not 0.36
            const bool carried = r1.write && r1.action == 0x5000 && Near(r1.factor, 0.8f, 0.001f) && !r2.write && Near(rebuilt.game, 0.8f, 0.001f) &&
                                 Near(rebuilt.written, 1.2f, 0.001f) && !s2.write && Near(sinking.game, 1.0f, 0.001f) && Near(sinking.written, 0.6f, 0.001f);
            // the push comes back at a set pace (a tester's HX 520, 28 October): the first write after the take at once
            // (2.4), a fall at once (1.0), then within the gear a rise of kLiftRise a second (1.0 to 2.4 in 1.4 s at passes
            // of 0.05 s); a fall again at once; a new gear at once
            TorqueHand paced = {};
            const TorqueOrders p0 = TorqueHandStep(paced, true, 0x5000, 0x4000, 1.0f, 2.4f, 0.05f, 8);
            const TorqueOrders p1 = TorqueHandStep(paced, true, 0x5000, 0x4000, paced.written, 1.0f, 0.05f, 8);
            const TorqueOrders p2 = TorqueHandStep(paced, true, 0x5000, 0x4000, paced.written, 2.4f, 0.05f, 8);
            int risePasses = 1;
            while (risePasses < 100 && paced.written < 2.4f - 0.001f)
            {
                TorqueHandStep(paced, true, 0x5000, 0x4000, paced.written, 2.4f, 0.05f, 8);
                risePasses++;
            }
            const TorqueOrders dropped = TorqueHandStep(paced, true, 0x5000, 0x4000, paced.written, 1.0f, 0.05f, 8);
            const TorqueOrders shifted = TorqueHandStep(paced, true, 0x5000, 0x4000, paced.written, 2.4f, 0.05f, 9);
            const bool pace = p0.write && Near(p0.factor, 2.4f, 0.001f) && p1.write && Near(p1.factor, 1.0f, 0.001f) && p2.write &&
                              p2.factor < 1.1f && risePasses * 0.05f > 1.0f && risePasses * 0.05f < 2.0f && dropped.write && Near(dropped.factor, 1.0f, 0.001f) &&
                              shifted.write && Near(shifted.factor, 2.4f, 0.001f);
            printf("     the push back at a set pace: 1.0 to 2.4 in %d passes of 0.05 s\n", risePasses);
            // EngineShare: none of it leaves the game's own, all of it the whole lift, half of it half way
            const bool blends = TorqueBlend(2.0f, 0.0f) == 1.0f && TorqueBlend(2.0f, 1.0f) == 2.0f && Near(TorqueBlend(2.0f, 0.5f), 1.5f, 0.001f) &&
                                Near(TorqueBlend(0.4f, 0.5f), 0.7f, 0.001f) && TorqueBlend(2.0f, 3.0f) == 2.0f;
            Check(lifts && followed && givenBack && over.write && Near(over.factor, 1.0f, 0.001f) && blends && carried && pace,
                  "powertrain: the torque factor lifts the game's curve by the engine's torque (the cold and the boost only where the pedal asks more) and the "
                  "converter's ratio, at the most 6 times and the game's own at the gear's top, as far as EngineShare hands it, stands on the "
                  "game's own share and follows it, keeps it over a rebuilt action that reads the mod's own factor, and gives it back when it goes off and to the last truck; "
                  "within a gear a rise comes back at 1 a second, a fall, a new gear and the first write at once");
        }
        // with the engine handed over, a truck automatic's schedule, on a scout's road gearbox with a crawling first
        // (tops 8, 13, 19, 27, 43 and 65 rad/s, 750 to 4000 rpm): it sets off in the second; floored it revs each gear
        // to near the red zone, at a light foot it shifts up early, and neither takes a shift back; stuck floored it
        // takes the first after a second, and the second again once the foot comes off
        {
            TruckRaw sd = g;
            static const float scoutSpeeds[8] = { 3.0f, 1.5f, 4.0f, 7.0f, 11.0f, 19.0f, 30.0f, 20.0f };
            sd.gearSpeeds = 8;
            memcpy(sd.gearSpeed, scoutSpeeds, sizeof scoutSpeeds);
            GaugeScale dk = k;
            dk.idle = 750.0f;
            dk.redline = 4000.0f;
            dk.max = 4800.0f;
            auto scoutAt = [&](float spin) {
                for (int i = 0; i < sd.wheels; i++) sd.spin[i] = spin;
                sd.speed = spin / 2.0f;
            };
            const EngineHand whole{ true, &kClassCurves[kClassScout], 4800.0f / 4000.0f, 1.0f }, half{ true, &kClassCurves[kClassScout], 4800.0f / 4000.0f, 0.5f };
            // from a standstill to 0.9 of the top gear's top at gain rad/s each second: the share of each gear's top it
            // left them at, the most and the least, and the shifts down on the way
            auto scoutPull = [&](float pedal, float gain, int &firstGear, float &leftMost, float &leftLeast, int &downs) {
                GearboxModel sb = {};
                sb.running = true;
                sb.gear = 1;
                sb.grip = 2.0f;
                firstGear = 0;
                leftMost = 0.0f;
                leftLeast = 1.0f;
                downs = 0;
                for (float at = 0.0f; at < 120.0f && gain * at < 0.9f * TruckGearTop(sd, 6); at += kDt)
                {
                    scoutAt(gain * at);
                    const int was = sb.gear;
                    GearboxStep(sb, sd, dk, pedal, pedal, kDt, whole);
                    if (!firstGear) firstGear = sb.gear;
                    if (was >= 1 && sb.gear > was && at > 0.0f)
                    {
                        const float x = gain * at / TruckGearTop(sd, was);
                        leftMost = fmaxf(leftMost, x);
                        leftLeast = fminf(leftLeast, x);
                    }
                    if (was >= 1 && sb.gear < was) downs++;
                }
                return sb.gear;
            };
            int flooredFirst = 0, lightFirst = 0, flooredDowns = 0, lightDowns = 0;
            float flooredMost = 0.0f, flooredLeast = 0.0f, lightMost = 0.0f, lightLeast = 0.0f;
            const int flooredLast = scoutPull(1.0f, 2.0f, flooredFirst, flooredMost, flooredLeast, flooredDowns);
            const int lightLast = scoutPull(0.3f, 1.0f, lightFirst, lightMost, lightLeast, lightDowns);
            const bool schedule = GearboxStartGear(sd) == 2 && GearboxStartGear(g) == 1 && flooredFirst == 2 && lightFirst == 2 && flooredLast == 6 &&
                                  lightLast == 6 && flooredLeast > 0.9f && flooredMost < 0.99f && lightMost < 0.7f && flooredDowns == 0 && lightDowns == 0;
            // stuck at a standstill, floored: the second, the first after kStuckAfter, the second with the foot off
            GearboxModel stuckBox = {};
            stuckBox.running = true;
            stuckBox.gear = 1;
            stuckBox.grip = 2.0f;
            scoutAt(0.0f);
            int stuckAtHalf = 0, stuckAfter = 0;
            for (float at = 0.0f; at < 1.6f; at += kDt)
            {
                GearboxStep(stuckBox, sd, dk, 1.0f, 1.0f, kDt, whole);
                if (at < 0.5f) stuckAtHalf = stuckBox.gear;
            }
            stuckAfter = stuckBox.gear;
            for (float at = 0.0f; at < kFootGap + 0.2f; at += kDt) GearboxStep(stuckBox, sd, dk, 0.0f, 0.0f, kDt, whole);
            // and without the mod's engine the first gear as before
            GearboxModel gameOwn = {};
            gameOwn.running = true;
            gameOwn.gear = 1;
            gameOwn.grip = 2.0f;
            GearboxStep(gameOwn, sd, dk, 1.0f, 1.0f, kDt);
            // floored in the fourth at 0.89 of its top and gaining nothing: at EngineShare 50 the fifth pulls more there, and
            // it shifts up; with all of the engine the fourth still pulls more (with a truck's roof of 6 fading over the
            // gear's last three tenths the two cross at 0.875 and 0.905; fading over the last fifth at 0.89 and 0.92; with
            // the flat roof of 6 at 0.915 and 0.955; at 3 they crossed at 0.86 and 0.89, and the drive of 11:51 at
            // EngineShare 50 shifted at 0.87)
            auto heldAt = [&](const EngineHand &hand) {
                GearboxModel hb = {};
                hb.running = true;
                hb.gear = 4;
                hb.grip = 2.0f;
                scoutAt(0.89f * TruckGearTop(sd, 4));
                hb.spin = hb.by = hb.rolling = sd.spin[0];
                hb.pedal = hb.held = hb.feel = 1.0f;
                hb.moving = kEngineGearHold; // in the gear a while
                for (float at = 0.0f; at < 2.0f; at += kDt) GearboxStep(hb, sd, dk, 1.0f, 1.0f, kDt, hand);
                return hb.gear;
            };
            const int heldHalf = heldAt(half), heldWhole = heldAt(whole);
            // a kickdown at EngineShare 50 from the fifth: not into a fourth it would land in at 0.86 of its top, past
            // its point up (0.915 or so) less kKickRoom, but into one it lands in at 0.7
            auto kickFrom5 = [&](float fourthShare) {
                GearboxModel kb = {};
                kb.running = true;
                kb.gear = 5;
                kb.grip = 2.0f;
                scoutAt(fourthShare * TruckGearTop(sd, 4));
                kb.spin = kb.by = kb.rolling = sd.spin[0];
                for (float at = 0.0f; at < 0.6f; at += kDt) GearboxStep(kb, sd, dk, 0.5f, 0.5f, kDt, half);
                for (float at = 0.0f; at < 0.3f; at += kDt) GearboxStep(kb, sd, dk, 1.0f, 1.0f, kDt, half);
                return kb.gear;
            };
            const int kickHigh = kickFrom5(0.86f), kickLow = kickFrom5(0.7f);
            // the start gear by the engine's Torque over the truck's mass: 195000 over 9007 kg the second, 100000 the first
            EngineHand strong = whole, weak = whole;
            strong.launch = 195000.0f / 9007.0f;
            weak.launch = 100000.0f / 9007.0f;
            const bool starts = GearboxStartGear(sd, strong.launch) == 2 && GearboxStartGear(sd, weak.launch) == 1 && GearboxStartGear(sd, 0.0f) == 2;
            GearboxModel weakStart = {};
            weakStart.running = true;
            weakStart.gear = 2;
            weakStart.grip = 2.0f;
            scoutAt(0.0f);
            GearboxStep(weakStart, sd, dk, 1.0f, 1.0f, kDt, weak);
            // a light foot (0.15) losing speed is no climb; a heavy one (0.8) is
            auto slowingAt = [&](float pedal) {
                GearboxModel cb = {};
                cb.running = true;
                cb.gear = 5;
                cb.grip = 2.0f;
                cb.pedal = cb.held = cb.feel = pedal;
                for (float at = 0.0f; at < 1.5f; at += kDt)
                {
                    scoutAt(0.45f * TruckGearTop(sd, 5) - 0.8f * at);
                    if (at == 0.0f) cb.spin = cb.by = cb.rolling = sd.spin[0];
                    GearboxStep(cb, sd, dk, pedal, pedal, kDt, whole);
                }
                return cb.climb > 0.0f;
            };
            const bool climbs = !slowingAt(0.15f) && slowingAt(0.8f);
            // an empty truck through close gears (tops 8, 13, 17, 21 and 25 rad/s) floored from a standstill, gaining
            // 6 rad/s each second up to its gear's top, where the game governs it (the drive of 14:12:05: 1 to 5 in
            // two seconds): each gear held kEngineGearHold of moving, on the limiter if it comes to that
            float quickShortest = 100.0f, scoutShortest = 100.0f;
            int quickLast = 0, scoutLast = 0;
            for (int scoutKind = 0; scoutKind < 2; scoutKind++)
            {
                EngineHand quickHand = whole;
                quickHand.hold = scoutKind ? kEngineGearHoldScout : 0.0f;
                float &shortest = scoutKind ? scoutShortest : quickShortest;
                int &last = scoutKind ? scoutLast : quickLast;
                TruckRaw qt = sd;
                static const float quickSpeeds[7] = { 3.0f, 1.5f, 4.0f, 6.0f, 8.0f, 10.0f, 7.5f };
                qt.gearSpeeds = 7;
                memcpy(qt.gearSpeed, quickSpeeds, sizeof quickSpeeds);
                GearboxModel qb = {};
                qb.running = true;
                qb.gear = 1;
                qb.grip = 2.0f;
                float spin = 0.0f, since = 0.0f;
                for (float at = 0.0f; at < 20.0f; at += kDt)
                {
                    for (int i = 0; i < qt.wheels; i++) qt.spin[i] = spin;
                    qt.speed = spin / 2.0f;
                    const int was = qb.gear;
                    GearboxStep(qb, qt, dk, 1.0f, 1.0f, kDt, quickHand);
                    since += kDt;
                    if (qb.gear > was)
                    {
                        if (was >= 1) shortest = fminf(shortest, since);
                        since = 0.0f;
                    }
                    spin = fminf(spin + 6.0f * kDt, TruckGearTop(qt, qb.gear));
                }
                last = qb.gear;
            }
            EngineHand scoutWhole = whole, scoutHalf = half;
            scoutWhole.roof = scoutHalf.roof = TorqueRoof(kClassScout);
            printf("     engine handed over, the next gear 43/27 taller: floored shift up at %.3f of the gear's top at EngineShare 100, %.3f at 50 (a truck's roof); "
                   "%.3f and %.3f with a scout's\n",
                   EngineUpShare(whole, 43.0f / 27.0f), EngineUpShare(half, 43.0f / 27.0f), EngineUpShare(scoutWhole, 43.0f / 27.0f), EngineUpShare(scoutHalf, 43.0f / 27.0f));
            // the fade from 0.7 brings the floored point up to 0.905 of the top for a truck's roof and 0.865 for a scout's
            // (from 0.8 it was under 0.92 and under 0.9; the flat roof of 6 had it at 0.955 and 0.915)
            const bool crossing = EngineUpShare(half, 43.0f / 27.0f) < EngineUpShare(whole, 43.0f / 27.0f) && EngineUpShare(half, 43.0f / 27.0f) < 0.9f &&
                                  EngineUpShare(whole, 43.0f / 27.0f) > 0.9f && EngineUpShare(whole, 43.0f / 27.0f) < 0.915f &&
                                  EngineUpShare(scoutWhole, 43.0f / 27.0f) < 0.9f && EngineUpShare(scoutHalf, 43.0f / 27.0f) < EngineUpShare(scoutWhole, 43.0f / 27.0f);
            Check(schedule && stuckAtHalf == 2 && stuckAfter == 1 && stuckBox.gear == 2 && gameOwn.gear == 1 && crossing && heldHalf == 5 && heldWhole == 4 &&
                      kickHigh == 5 && kickLow == 4 && starts && weakStart.gear == 1 && climbs && quickLast == 5 && quickShortest >= kLimiterHold - kDt &&
                      quickShortest < kEngineGearHoldScout - kDt && scoutLast == 5 && scoutShortest >= kLimiterHold - kDt && scoutShortest < kEngineGearHoldScout - kDt,
                  "automatic, the engine handed over: it sets off in the second of a crawling first; floored it revs each gear toward the red zone and shifts "
                  "where the next gear pulls more if that comes first, at a light foot early, no shift taken back; held short of the point floored, the next "
                  "gear taken where it pulls more; a kickdown only into a gear short of its point up; stuck floored it takes the first after a second, the "
                  "second again with the foot off; a weaker engine for the truck's mass sets off in the first; a light foot losing speed is no climb; a "
                  "quick truck through close gears floored, governed at each gear's top, holds each gear on the limiter 0.9 s, not the hold's 2.5 (a scout's 1.5)");
            // the wheels spinning at 0.95 of the gear's top while the ground says 0.6 (the grip known), floored: out of the
            // gear it set off in the shift up goes by the wheels, so the first shifts up once its hold has passed (and
            // not before); from the second gear up the ground guard holds (a tester's log, 27 October: a ZIL-157 sat in
            // first 10 to 40 s on dirt)
            auto spinningIn = [&](int gear, const EngineHand &hand, float moved) {
                GearboxModel sb = {};
                sb.running = true;
                sb.gear = gear;
                sb.grip = 2.0f;
                const float top = TruckGearTop(sd, gear);
                for (int i = 0; i < sd.wheels; i++) sd.spin[i] = 0.95f * top;
                sd.speed = 0.6f * top / sb.grip;
                sb.spin = sb.by = sb.rolling = sd.spin[0];
                sb.pedal = sb.held = sb.feel = 1.0f;
                sb.holdUp = true;
                sb.moving = moved;
                for (float at = 0.0f; at < 0.5f; at += kDt) GearboxStep(sb, sd, dk, 1.0f, 1.0f, kDt, hand);
                return sb.gear;
            };
            // stuck where it stands, the ground at nothing and the wheels at 0.95 of the first's top, floored 12 s: no
            // shift up and back (without the ground's rule while slow it went up and down every 9 s)
            int stuckShifts = 0;
            {
                GearboxModel sb = {};
                sb.running = true;
                sb.gear = 1;
                sb.grip = 2.0f;
                for (int i = 0; i < sd.wheels; i++) sd.spin[i] = 0.95f * TruckGearTop(sd, 1);
                sd.speed = 0.0f;
                sb.spin = sb.by = sb.rolling = sd.spin[0];
                sb.pedal = sb.held = sb.feel = 1.0f;
                for (float at = 0.0f; at < 12.0f; at += kDt)
                {
                    const int was = sb.gear;
                    GearboxStep(sb, sd, dk, 1.0f, 1.0f, kDt, weak);
                    stuckShifts += sb.gear != was;
                }
            }
            const int firstAfter = spinningIn(1, weak, kEngineGearHold), firstBefore = spinningIn(1, weak, 0.0f), secondWeak = spinningIn(2, weak, kEngineGearHold),
                      startGear = spinningIn(2, whole, kEngineGearHold), third = spinningIn(3, whole, kEngineGearHold);
            Check(firstAfter == 2 && firstBefore == 1 && secondWeak == 2 && startGear == 3 && third == 3 && stuckShifts == 0,
                  "automatic, the engine handed over: wheels spinning at 0.95 of the gear's top on ground that says 0.6, floored: the first shifts up "
                  "once its hold has passed, not before; the start gear (the second of a crawling first) shifts up the same; the third holds, and "
                  "the second when it is not the start gear; stuck where it stands the first holds");
            // an engine's own curve from its peak torque's speed: a turbo diesel's plateau from 0.85 to 1.5 times it, three
            // fifths at half of it, four fifths at the red zone; a naturally aspirated engine's hump at the peak
            const TorqueCurve td = EngineCurveFromPeak(1950.0f / 4850.0f, false), na = EngineCurveFromPeak(0.6f, true);
            const float tdPeak = 1950.0f / 4850.0f;
            const bool curves = Near(TorqueAt(td, tdPeak, 1.25f), 1.0f, 0.001f) && Near(TorqueAt(td, tdPeak * 0.5f, 1.25f), 0.6f, 0.001f) &&
                                Near(TorqueAt(td, tdPeak * 1.4f, 1.25f), 1.0f, 0.001f) && Near(TorqueAt(td, 1.0f, 1.25f), 0.8f, 0.001f) &&
                                Near(CurvePeak(td), tdPeak * kPeakFrom, 0.001f) && Near(TorqueAt(na, 0.6f, 1.25f), 1.0f, 0.001f) && Near(CurvePeak(na), 0.6f, 0.001f) &&
                                TorqueAt(na, 0.3f, 1.25f) < 0.9f && TorqueAt(na, 0.9f, 1.25f) < 0.95f && EngineCurveFromPeak(0.97f, false).n >= 2;
            // at a light foot the scout (idle 800, red zone from 4850) with a peak at 1950 rpm: up about as far along as the
            // pedal is down, not under 0.8 of the peak's speed; down where the gear below comes in 1.15 under its point up,
            // and at a light foot not under 1.6 times idle (1280 rpm; review 11, from 1.15), so a light shift up waits till
            // it lands over that
            GaugeScale tk = dk;
            tk.idle = 800.0f;
            tk.redline = 4850.0f;
            tk.max = 6000.0f;
            EngineHand diesel = whole;
            diesel.top = 6000.0f / 4850.0f;
            diesel.least = kLightPeak * 1950.0f;
            const float upFifth = GearboxUpPoint(sd, tk, 3, 0.2f, diesel), upHalf = GearboxUpPoint(sd, tk, 3, 0.5f, diesel), upFull = GearboxUpPoint(sd, tk, 3, 1.0f, diesel);
            const float downFifth = GearboxDownPoint(sd, tk, 4, 0.2f, diesel);
            const float landFifth = upFifth * TruckGearTop(sd, 3) / TruckGearTop(sd, 4);
            const bool lightPoints = upFifth >= 1560.0f && upFifth < 1580.0f && upHalf > 2400.0f && upHalf < 2800.0f && upFull > 4000.0f &&
                                     Near(downFifth, tk.idle * (kEngineDownIdle + (kEngineDownIdleFloored - kEngineDownIdle) * 0.2f), 1.0f) &&
                                     landFifth < downFifth * kShiftRoom && GearboxDownPoint(sd, tk, 1, 0.2f, diesel) == 0.0f;
            // and so through the gears at a fifth of the pedal, gaining slowly: each shift up once it lands over the down
            // point (about 1900 to 2200 rpm across this box's steps; at 1560 it would land under it), none taken back, the
            // converter locked after each one
            float lightLow = 1e9f, lightHigh = 0.0f;
            int lightDownsDiesel = 0, lightLastDiesel = 0, unlockedAfter = 0;
            {
                GearboxModel lb = {};
                lb.running = true;
                lb.gear = 1;
                lb.grip = 2.0f;
                for (float at = 0.0f; at < 120.0f; at += kDt)
                {
                    scoutAt(fminf(0.5f * at, 0.9f * TruckGearTop(sd, 6)));
                    const int was = lb.gear;
                    const float spinNow = sd.spin[0];
                    GearboxStep(lb, sd, tk, 0.2f, 0.2f, kDt, diesel);
                    if (was >= 2 && lb.gear > was)
                    {
                        const float rpm = GearboxInput(sd, tk, was, spinNow);
                        lightLow = fminf(lightLow, rpm);
                        lightHigh = fmaxf(lightHigh, rpm);
                    }
                    if (lb.gear < was && was >= 1) lightDownsDiesel++;
                    // past the lock-up's speed at a light foot (kLockIdle times idle) and clear of a shift, locked
                    if (lb.gear == was && lb.gear >= 2 && lb.pause <= 0.0f && lb.input > tk.idle * kLockIdle + 30.0f && !lb.locked) unlockedAfter++;
                }
                lightLastDiesel = lb.gear;
            }
            // floored it shifts up by the engine's power at the wheels (the pull, the torque times the speed): a curve whose
            // power falls before the red zone (from 1 at 0.6 to 0.4 at the red zone) shifts up sooner than a flat one; the
            // diesel's from its peak (0.8 at the red zone) still gains power up to it, and shifts with the flat one, near the top
            EngineHand fading = whole, flat = whole, dieselFloored = whole;
            static const TorqueCurve flatCurve = { 2, { 0.2f, 1.0f }, { 1.0f, 1.0f } }, fallingCurve = { 3, { 0.2f, 0.6f, 1.0f }, { 1.0f, 1.0f, 0.4f } };
            fading.curve = &fallingCurve;
            flat.curve = &flatCurve;
            dieselFloored.curve = &td;
            printf("     floored point up, the next gear 1.3 taller: a falling curve %.3f, a flat one %.3f, the diesel's %.3f\n", EngineUpShare(fading, 1.3f),
                   EngineUpShare(flat, 1.3f), EngineUpShare(dieselFloored, 1.3f));
            // (with the lift's fade the flat curve's point up is 0.905, the flat roof had it past 0.93)
            const bool fades = EngineUpShare(fading, 1.3f) < 0.85f && EngineUpShare(flat, 1.3f) > 0.88f && EngineUpShare(fading, 1.3f) < EngineUpShare(flat, 1.3f) - 0.05f &&
                               fabsf(EngineUpShare(dieselFloored, 1.3f) - EngineUpShare(flat, 1.3f)) < 0.02f;
            printf("     a fifth of the pedal on the scout's diesel: shifts up at %.0f to %.0f rpm, the light down point %.0f\n", lightLow, lightHigh, downFifth);
            Check(curves && fades && lightPoints && lightLow > 1850.0f && lightHigh < 2250.0f && lightDownsDiesel == 0 && lightLastDiesel == 6 && unlockedAfter == 0,
                  "automatic, the engine handed over: an engine's curve from its peak torque's speed, floored one whose power falls before the red zone shifting up sooner than a flat one; at a light foot up about as far along as the pedal "
                  "is down, not under 0.8 of the peak's speed (a fifth of the pedal: 1560 rpm), down where the gear below comes in clear of its point up and, "
                  "at a light foot, not under 1.6 times idle, so a light shift up lands over it (1850 to 2250 rpm here), through the gears without a shift taken "
                  "back, the converter locked");
            // a climb the second gear cannot take (the tester's drive of 22:02: gears' tops 9, 17, 21 and 29 rad/s, the
            // plain scales a scout's, 800 to 5600 of 7000 rpm): floored, the first gains 1.1 rad/s each second, less near
            // its top, where the game governs it, and the second loses 0.5. The drive swapped them every few seconds; now
            // the first shift up taken back keeps the first kHuntKeep on its limiter, the next try twice as long. With
            // the grade over (the second gaining from easeAt), the next try holds and the wait is let go; a lift of
            // the foot lets it go too
            TruckRaw ht = g;
            static const float huntSpeeds[6] = { 0.5f, 2.0f, 6.0f, 8.0f, 12.0f, 8.0f };
            ht.gearSpeeds = 6;
            memcpy(ht.gearSpeed, huntSpeeds, sizeof huntSpeeds);
            GaugeScale hk = dk;
            hk.idle = 800.0f;
            hk.redline = 5600.0f;
            hk.max = 7000.0f;
            EngineHand huntHand = whole;
            huntHand.top = 7000.0f / 5600.0f;
            huntHand.hold = kEngineGearHoldScout;
            huntHand.least = kLightPeak * CurvePeak(kClassCurves[kClassScout]) * 5600.0f;
            struct HuntRun { int ups, downs, hunts, last; float firstLongest, firstMost, waitAfter; bool liftLetGo; };
            auto huntRun = [&](float seconds, float easeAt, float liftAt) {
                HuntRun run = {};
                GearboxModel hb = {};
                hb.running = true;
                hb.gear = 2;
                hb.grip = 2.0f;
                hb.moving = kEngineGearHold;
                hb.pedal = hb.held = hb.feel = 1.0f;
                float spin = 0.48f * TruckGearTop(ht, 2), inFirst = 0.0f;
                hb.spin = hb.by = hb.rolling = spin;
                for (float at = 0.0f; at < seconds; at += kDt)
                {
                    for (int i = 0; i < ht.wheels; i++) ht.spin[i] = spin;
                    ht.speed = spin / 2.0f;
                    const bool footUp = liftAt >= 0.0f && at >= liftAt && at < liftAt + 1.0f;
                    const float pedal = footUp ? 0.0f : 1.0f;
                    const int was = hb.gear;
                    const bool keptBefore = hb.keepFor > 0.0f;
                    GearboxStep(hb, ht, hk, pedal, pedal, kDt, huntHand);
                    if (footUp && keptBefore && hb.keepFor <= 0.0f && hb.keep == 0) run.liftLetGo = true;
                    if (hb.gear > was) run.ups++;
                    if (hb.gear < was) run.downs++;
                    inFirst = hb.gear == 1 ? inFirst + kDt : 0.0f;
                    run.firstLongest = fmaxf(run.firstLongest, inFirst);
                    const float x = spin / TruckGearTop(ht, hb.gear);
                    if (hb.gear == 1) run.firstMost = fmaxf(run.firstMost, x);
                    // the first gains up to its top, the game's pull falling to nothing there; above it the climb takes
                    // speed, or gives it once the grade is over; coasting, it slows
                    const float gain = footUp ? -1.0f : hb.gear == 1 ? 1.1f * SettingsRange((0.99f - x) / 0.09f, 0.0f, 1.0f) : easeAt >= 0.0f && at >= easeAt ? 0.3f : -0.5f;
                    spin = fminf(fmaxf(spin + gain * kDt, 0.5f), TruckGearTop(ht, hb.gear));
                }
                run.hunts = hb.hunts;
                run.last = hb.gear;
                run.waitAfter = hb.keepWait;
                return run;
            };
            const HuntRun grade = huntRun(40.0f, -1.0f, -1.0f), eased = huntRun(55.0f, 20.0f, -1.0f), liftRun = huntRun(12.0f, -1.0f, 9.0f);
            printf("    hunt: on the grade %d up, %d down, %d taken back, the first held %.1f s at most, up to %.3f of its top; eased: %d up, "
                   "%d taken back, in %d at the end, the wait %.1f; lifted: let go %d\n",
                   grade.ups, grade.downs, grade.hunts, grade.firstLongest, grade.firstMost, eased.ups, eased.hunts, eased.last, eased.waitAfter, liftRun.liftLetGo);
            Check(grade.ups <= 3 && grade.hunts == 3 && grade.firstLongest >= 2.0f * kHuntKeep - 0.5f && grade.firstMost > kEngineLimit &&
                      eased.last >= 2 && eased.hunts == 2 && eased.waitAfter == 0.0f && liftRun.liftLetGo,
                  "automatic, the engine handed over: up a climb the second cannot take, a shift up taken back keeps the first on its limiter "
                  "eight seconds, the next try sixteen, so it no longer swaps them every few seconds; once the grade is over the next try holds "
                  "and the wait is let go; a lift of the foot lets it go");
            // the class a truck without scales of its own shifts by, from its weight by its files (the tester's trucks)
            Check(ClassByMass(4091.0f, kClassTruck) == kClassScout && ClassByMass(6501.0f, kClassScout) == kClassTruck && ClassByMass(12022.0f, kClassScout) == kClassTruck &&
                      ClassByMass(20308.0f, kClassScout) == kClassHeavy && ClassByMass(0.0f, kClassScout) == kClassScout && ClassByMass(NAN, kClassHeavy) == kClassHeavy,
                  "automatic: a truck without scales of its own shifts by the class of its weight, a scout's up to 6 t, a truck's under 16 t, a heavy "
                  "truck's from there; the plain scales' class where the weight is not read");
            // the share of the gear's top the lift goes by: by the ground once the grip is known, the wheels at most
            // kSlipUp over it; before that the wheels, at most kLiftBlind
            Check(Near(LiftShare(10.0f, 4.0f, 2.0f, 20.0f), 0.42f, 0.001f) && Near(LiftShare(6.0f, 4.0f, 2.0f, 20.0f), 0.3f, 0.001f) &&
                      Near(LiftShare(24.0f, 0.2f, 0.0f, 20.0f), kLiftBlind, 0.001f) && Near(LiftShare(4.0f, 0.2f, 0.0f, 20.0f), 0.2f, 0.001f) &&
                      LiftShare(4.0f, 1.0f, 2.0f, 0.0f) == 0.0f,
                  "powertrain: the lift goes by the ground once the grip is known; wheels spun up before that get no more than at half the gear's top");
        }
        // a close-ratio box (the Scania's ten gears, tops 7.5 to 30.5 rad/s, 600 to 1930 rpm) floored from a standstill
        // with the grip learned: through the gears faster than a shift a second, skipping gears, never held to its top;
        // and a gear past what the engine can take is left at once, whatever the pause
        {
            TruckRaw sc = g;
            static const float scaniaSpeeds[12] = { 1.0f, 1.268f, 1.563f, 2.054f, 2.839f, 3.625f, 4.901f, 5.981f, 7.453f, 10.103f, 12.754f, 8.0f };
            sc.gearSpeeds = 12;
            memcpy(sc.gearSpeed, scaniaSpeeds, sizeof scaniaSpeeds);
            GaugeScale ck = k;
            ck.idle = 600.0f;
            ck.redline = 1930.0f;
            ck.max = 2500.0f;
            GearboxModel close = {};
            close.running = true;
            close.gear = 1;
            close.grip = 2.2f;
            int closeShifts = 0;
            float closeMost = 0.0f;
            for (float at = 0.0f; at < 12.0f; at += kDt)
            {
                const float spin = 2.5f * at;
                for (int i = 0; i < sc.wheels; i++) sc.spin[i] = spin;
                sc.speed = spin / 2.2f;
                const int closeWas = close.gear;
                const float x = closeWas >= 1 ? spin / TruckGearTop(sc, closeWas) : 0.0f;
                GearboxStep(close, sc, ck, 1.0f, 1.0f, kDt);
                if (closeWas >= 1 && close.gear > closeWas)
                {
                    closeShifts++;
                    closeMost = fmaxf(closeMost, x);
                }
            }
            // in the fourth (top 10.7) at 0.95 of its top just after a shift: up at once
            GearboxModel past = {};
            past.running = true;
            past.gear = 4;
            past.grip = 2.2f;
            past.pause = kShiftPause;
            for (int i = 0; i < sc.wheels; i++) sc.spin[i] = 0.95f * TruckGearTop(sc, 4);
            sc.speed = sc.spin[0] / 2.2f;
            past.spin = past.by = past.rolling = sc.spin[0];
            past.pedal = past.held = past.feel = 1.0f;
            GearboxStep(past, sc, ck, 1.0f, 1.0f, kDt);
            Check(close.gear == 10 && closeShifts < 9 && closeMost < 0.85f && past.gear > 4 && GearboxInput(sc, ck, past.gear, past.rolling) < GearboxPoint(ck, kDownMost),
                  "automatic: a close-ratio box floored skips gears and is never held to a gear's top; a gear past what the engine can take is left at once");
        }
        // braked hard from the top gear to a stop in a second and a half: in the first gear by the time the wheels
        // stop, not left in a high one for the next pull away, and never a shift down that would race the engine
        // with the ground's speed known (wheels of 0.45 m), and without it, when the shifts go by the wheels' speed
        // smoothed, which lags behind
        auto haltFrom = [&](bool groundKnown, float &firstAt, float &race) {
            TruckRaw h = g;
            const float from = TruckGearTop(h, 4) * 0.7f;
            auto set = [&](float spin) {
                for (int i = 0; i < h.wheels; i++) h.spin[i] = spin;
                h.speed = groundKnown ? spin * 0.45f : 0.0f;
            };
            set(from);
            GearboxModel hm = driving(4);
            hm.spin = hm.by = TruckDrivenSpin(h);
            GearboxStep(hm, h, k, 0.3f, 0.3f, kDt); // the ground's speed learned on the way
            int shifts = 0;
            firstAt = -1.0f;
            race = 0.0f;
            for (float at = 0.0f; at < 3.0f; at += kDt)
            {
                set(fmaxf(0.0f, from * (1.0f - at / 1.5f)));
                const int gearBefore = hm.gear;
                GearboxStep(hm, h, k, 0.0f, 0.0f, kDt);
                if (hm.gear != gearBefore)
                {
                    shifts++;
                    race = fmaxf(race, GearboxInput(h, k, hm.gear, TruckDrivenSpin(h)));
                }
                if (hm.gear == 1 && firstAt < 0.0f) firstAt = at;
            }
            return hm.gear == 1 && shifts <= 3;
        };
        float firstAt, haltRace, blindAt, blindRace;
        const bool halted = haltFrom(true, firstAt, haltRace), blindHalted = haltFrom(false, blindAt, blindRace);
        Check(halted && firstAt > 0.5f && firstAt <= 1.6f && haltRace < GearboxPoint(k, kCoastDownTo) + 50.0f && blindHalted && blindAt < 2.5f,
              "automatic: braked hard to a stop it is in the first gear by the time the wheels stop, the engine never raced, and within a second after without the ground's speed");
        // the mod's gearbox in the game: it takes a truck the moment it is driven with its engine running, in any gear,
        // and lets it go when the setting goes off or the engine stops; A's gear is the model's, never lower than the
        // engine can take, within the gears
        {
            TruckRaw tk = g;
            tk.gear = tk.gearEngaged = -1; // reverse: taken all the same
            TakeoverModel take = {};
            const TakeoverOrders disabled = TakeoverStep(take, tk, false);
            const TakeoverOrders took = TakeoverStep(take, tk, true);
            const TakeoverOrders stays = TakeoverStep(take, tk, true);
            const TakeoverOrders off = TakeoverStep(take, tk, false);
            const bool offWhy = take.lastHandBack == kTakeOff;
            TakeoverStep(take, tk, true);
            tk.flags = 0;
            const TakeoverOrders stopped = TakeoverStep(take, tk, true);
            tk.flags = g.flags;
            GearboxModel rollingAt = {};
            rollingAt.rolling = 20.0f;
            rollingAt.grip = 2.2f;
            TruckRaw tf = g;
            tf.gear = tf.gearEngaged = 1;
            const int fit = GearboxLowestFit(rollingAt, tf, k);
            Check(!disabled.took && took.took && take.handBacks == 1 && !stays.took && !stays.gaveBack && off.gaveBack && offWhy && !stopped.gaveBack &&
                      take.lastHandBack == kTakeOff && take.driving && fit == 3 && TakeoverAutoGear(tf, 2, fit) == 3 && TakeoverAutoGear(tf, 4, 1) == 4 &&
                      TakeoverAutoGear(tf, 0, 1) == 1 && TakeoverAutoGear(tf, 40, 1) == TruckTopGear(tf),
                  "gearbox in the game: taken the moment a truck is driven, in any gear; let go when the setting goes off, kept through an engine stop (a "
                  "stall); A's gear the "
                  "model's, never lower than the engine can take, within the gears");
            // the needle follows the converter's engine speed with its lag; with the engine off it falls to nothing
            TachModel follow = {};
            TachFollow(follow, g, k, 1500.0f, kDt);
            const bool followPlaced = Near(follow.rpm, 1500.0f, 0.5f);
            for (float at = 0.0f; at < 0.2f; at += kDt) TachFollow(follow, g, k, 2000.0f, kDt);
            const bool followLagging = follow.rpm > 1500.0f && follow.rpm < 2000.0f;
            for (float at = 0.0f; at < 3.0f; at += kDt) TachFollow(follow, g, k, 2000.0f, kDt);
            const bool followThere = Near(follow.rpm, 2000.0f, 5.0f);
            // a converter speed past the red zone (wheels spinning in a low gear) takes the needle no further than wheelspin does
            TachModel over = {};
            for (float at = 0.0f; at < 3.0f; at += kDt) TachFollow(over, g, k, k.max * 2.0f, kDt);
            const bool followCapped = Near(over.rpm, k.idle + (k.redline - k.idle) * kTachOverrun, 5.0f);
            TruckRaw dead = g;
            dead.flags = 0;
            const float gearBefore = follow.rpm;
            for (float at = 0.0f; at < 5.0f; at += kDt) TachFollow(follow, dead, k, 3000.0f, kDt);
            Check(followPlaced && followLagging && followThere && followCapped && Near(gearBefore, 2000.0f, 5.0f) && follow.rpm < 50.0f,
                  "automatic in the game: the needle follows the converter's engine speed with its lag, no further past the red zone than wheelspin takes it, and falls "
                  "with the engine off");
        }
        TruckRaw stopped = g;
        stopped.flags = 0;
        GearboxStep(box, stopped, k, 1.0f, 1.0f, kDt);
        Check(box.drive == 0.0f && box.rpm == 0.0f && TruckBrake(g) == 0.0f, "automatic: a stopped engine drives nothing");
        TruckRaw braking = g;
        braking.throttle = 0.6f;
        TruckRaw reversing = g;
        reversing.gear = reversing.gearEngaged = -1;
        reversing.throttle = -0.4f;
        Check(Near(TruckBrake(braking), 0.6f, 0.001f) && TruckPedal(braking) == 0.0f && Near(TruckBrake(reversing), 0.4f, 0.001f) && TruckPedal(reversing) == 0.0f,
              "automatic: the brake is the side of the pedal that does not drive, in an automatic's reverse too");
    }

    // the pull test: on a made up truck whose pull follows the torque factor, and on one whose game puts the factor
    // back every frame; it starts floored in the fourth gear, measures the three changes, and puts the factor back;
    // lifting the foot stops it and puts the factor back too
    {
        auto run = [&](bool kept, float liftAt, PullTest &test, PullOrders &last, float &factorAfter) {
            TruckRaw p = Truck();
            p.gear = p.gearEngaged = 4;
            p.automatic = 0;
            p.torqueFactor = 1.0f;
            p.throttle = -1.0f;
            float v = 5.0f;
            test = PullTest{ kPullArmed };
            last = PullOrders{};
            for (float at = 0.0f; at < 10.0f; at += 0.05f)
            {
                if (liftAt >= 0.0f && at >= liftAt) p.throttle = 0.0f;
                for (int i = 0; i < p.wheels; i++) p.spin[i] = v * 2.2f;
                p.speed = v;
                const PullOrders o = PullTestStep(test, p, TruckPedal(p), 0.05f);
                if (o.write) p.torqueFactor = o.factor;
                if (!kept) p.torqueFactor = 1.0f; // the game puts its own back before its physics reads it
                if (o.ended) last = o;
                v += 0.5f * p.torqueFactor * 0.05f;
            }
            factorAfter = p.torqueFactor;
        };
        PullTest follows, overwritten, lifted;
        PullOrders followEnd, overEnd, liftEnd;
        float followAfter, overAfter, liftAfter;
        run(true, -1.0f, follows, followEnd, followAfter);
        run(false, -1.0f, overwritten, overEnd, overAfter);
        run(true, 4.0f, lifted, liftEnd, liftAfter);
        const bool measured = follows.state == kPullDone && Near(PullRatio(follows, 1), 0.5f, 0.05f) && Near(PullRatio(follows, 3), 1.5f, 0.08f) &&
                              Near(PullRatio(follows, 5), 0.75f, 0.05f) && Near(followAfter, 1.0f, 0.001f) && Near(follows.seen[1], 0.5f, 0.001f) &&
                              overwritten.state == kPullDone && Near(PullRatio(overwritten, 1), 1.0f, 0.05f) && Near(overwritten.seen[1], 1.0f, 0.001f);
        const bool stopped = lifted.state == kPullStopped && !strcmp(lifted.why, "the pedal was lifted") && Near(liftAfter, 1.0f, 0.001f) && liftEnd.write;
        // not in the third gear, nor with the game's automatic flag up
        TruckRaw low = Truck();
        low.gear = low.gearEngaged = 3;
        low.throttle = -1.0f;
        low.torqueFactor = 1.0f;
        low.speed = 3.0f;
        PullTest waiting = { kPullArmed };
        PullTestStep(waiting, low, 1.0f, 0.05f);
        TruckRaw flagUp = low;
        flagUp.gear = flagUp.gearEngaged = 4;
        flagUp.automatic = 1;
        PullTestStep(waiting, flagUp, 1.0f, 0.05f);
        char first[1400], second[320];
        PullTestText(follows, first, sizeof first, second, sizeof second);
        Check(measured && stopped && waiting.state == kPullArmed && strstr(second, "x0.50") && strstr(second, "x1.50") && strstr(second, "x0.75"),
              "pull test: measures a pull that follows the torque factor and one the game puts back, puts the factor back, stops at a lift, "
              "and waits for the fourth gear and the game's automatic flag down");
    }

    // the clutch: free with the pedal pressed, a stall when it is let up at once with the truck standing, none when it
    // is let up slowly as the truck pulls away, locked at speed, and the engine back to idle with it pressed
    {
        TruckRaw c = Truck();
        c.gear = c.gearEngaged = 1;
        auto wheels = [&](float spin) { for (int i = 0; i < c.wheels; i++) c.spin[i] = spin; };
        const TorqueCurve &curve = kClassCurves[s.tachClass];
        const float range = k.redline - k.idle;
        wheels(0.0f);
        ClutchModel free = {};
        for (float at = 0.0f; at < 2.0f; at += kDt) ClutchStep(free, c, k, curve, 1.0f, 1.0f, 1.0f, false, kDt);
        // pressed, floored: the pedal is a torque (a road truck's min-max governor, review 11), so the free engine runs up
        // past the red zone to where the curve's fall, the high-speed governor, meets its friction, short of the top
        ClutchModel idling = {};
        for (float at = 0.0f; at < 2.0f; at += kDt) ClutchStep(idling, c, k, curve, 1.0f, 0.0f, 1.0f, false, kDt);
        Check(!free.locked && free.stalls == 0 && free.rpm > k.redline && free.rpm < k.max && free.carried == 0.0f && Near(idling.rpm, k.idle, range * 0.03f),
              "clutch: pressed, a floored engine runs free to its high-speed governor, past the red zone and short of the top; with the pedal up the idle "
              "governor holds idle");
        (void)range;
        // let up at once at idle, the truck standing: the engine dies within a few tenths of a second
        ClutchModel dump = {};
        for (float at = 0.0f; at < 0.5f; at += kDt) ClutchStep(dump, c, k, curve, 1.0f, 0.0f, 1.0f, false, kDt);
        float diedAfter = -1.0f;
        for (float at = 0.0f; at < 2.0f && diedAfter < 0.0f; at += kDt)
            if (ClutchStep(dump, c, k, curve, 0.0f, 0.0f, 1.0f, false, kDt)) diedAfter = at;
        Check(diedAfter >= 0.0f && diedAfter < 0.5f && dump.stalls == 1 && dump.rpm == 0.0f, "clutch: let up at once at idle with the truck standing, the engine stalls");
        // let up over three seconds with a third of the pedal, the truck pulling away with what the clutch passes on
        // (the gearbox's input gaining 2000 rpm a second for each share of the most torque, as in a first gear)
        ClutchModel pull = {};
        float dip = 1e9f, input = 0.0f;
        for (float at = 0.0f; at < 5.0f; at += kDt)
        {
            wheels(TruckGearTop(c, 1) * input / k.redline);
            ClutchStep(pull, c, k, curve, 1.0f - SettingsRange(at / 3.0f, 0.0f, 1.0f), ThrottleShape(0.33f), 1.0f, false, kDt);
            input = fmaxf(0.0f, input + pull.carried * 2000.0f * kDt);
            if (at > 0.5f) dip = fminf(dip, pull.rpm);
        }
        // with the min-max governor the third of the pedal is a torque, so the engine dips further than an all-speed one let
        // it (0.71 of idle, from 0.75 and over): the idle governor's full torque under idle catches it short of the stall
        printf("     clutch: a third of the pedal pulling away dips the engine to %.0f rpm (idle %.0f, the stall under %.0f) and locks at %.0f\n", dip, k.idle,
               k.idle * kClutchStall, pull.rpm);
        Check(pull.stalls == 0 && dip > k.idle * kClutchStall + 50.0f && dip < k.idle && pull.locked && Near(pull.rpm, pull.input, 1.0f),
              "clutch: let up slowly with a little pedal as the truck pulls away, the engine dips under its idle without stalling and the clutch locks");
        // the clutch's heat driven from ClutchStep's own states (review 10, D7), for a stock engine (Torque 195000): the
        // same start in first stays cold; floored in third, revved with the clutch pressed and then held in its bite (the
        // truck gaining a third as fast as in first), it burns within a few seconds; let up and locked, it only cools
        {
            const float stock = 195000.0f;
            ClutchModel firstGear = {};
            ClutchHeat firstHeat = {};
            float in = 0.0f, firstMost = 0.0f;
            for (float at = 0.0f; at < 5.0f; at += kDt)
            {
                wheels(TruckGearTop(c, 1) * in / k.redline);
                ClutchStep(firstGear, c, k, curve, 1.0f - SettingsRange(at / 3.0f, 0.0f, 1.0f), ThrottleShape(0.33f), 1.0f, false, kDt);
                in = fmaxf(0.0f, in + firstGear.carried * 2000.0f * kDt);
                ClutchHeatStep(firstHeat, 1, firstGear, stock, true, kDt);
                firstMost = fmaxf(firstMost, firstHeat.q);
            }
            TruckRaw third = c;
            third.gear = third.gearEngaged = 3;
            ClutchModel thirdGear = {};
            ClutchHeat thirdHeat = {};
            float thirdBegan = -1.0f, thirdMost = 0.0f, thirdCarried = 0.0f;
            in = 0.0f;
            // floored with the clutch pressed for a second, then held in its bite (0.65 of the pedal's travel)
            for (float at = 0.0f; at < 1.0f; at += kDt) ClutchStep(thirdGear, third, k, curve, 1.0f, ThrottleShape(1.0f), 1.0f, false, kDt);
            for (float at = 0.0f; at < 6.0f && thirdBegan < 0.0f; at += kDt)
            {
                for (int i = 0; i < third.wheels; i++) third.spin[i] = TruckGearTop(third, 3) * in / k.redline;
                ClutchStep(thirdGear, third, k, curve, 0.65f, ThrottleShape(1.0f), 1.0f, false, kDt);
                in = fmaxf(0.0f, in + thirdGear.carried * 2000.0f / 3.0f * kDt);
                ClutchHeatStep(thirdHeat, 1, thirdGear, stock, true, kDt);
                if (thirdHeat.burning) thirdBegan = at;
                thirdMost = fmaxf(thirdMost, thirdHeat.q);
                if (thirdGear.carried > thirdCarried) thirdCarried = thirdGear.carried;
            }
            // let up fully at speed: locked, so the heat only falls
            const float hot = thirdHeat.q;
            wheels(TruckGearTop(c, 1) * 0.6f);
            ClutchModel locked = {};
            for (float at = 0.0f; at < 2.0f; at += kDt)
            {
                ClutchStep(locked, c, k, curve, 0.0f, ThrottleShape(0.5f), 1.0f, false, kDt);
                ClutchHeatStep(thirdHeat, 1, locked, stock, true, kDt);
            }
            // as tuned (the figures over the constants take the clutch carrying 1.0 at 1600 rpm), the clutch model held in
            // its bite at its governor carries about 0.53 at 1900 rpm, and the heat of this start stays short of the burn
            // point: what it reaches is printed for the retune the drives will ask for
            printf("     clutch heat from ClutchStep: first gear at most %.2f of %.2f; third floored in the bite carries at most %.2f and heats to %.2f (%s)\n",
                   firstMost, kClutchBurnFrom, thirdCarried, thirdMost, thirdBegan >= 0.0f ? "burns" : "no burn");
            Check(firstMost < kClutchBurnFrom * 0.1f && thirdMost > firstMost * 10.0f && thirdMost > kClutchBurnFrom * 0.6f && thirdGear.stalls == 0 && locked.locked &&
                      thirdHeat.q < hot,
                  "clutch heat from ClutchStep's states: a start in first stays under a tenth of the burn point; floored in third with the clutch held in its "
                  "bite it heats more than ten times as much, past 0.6 of the burn point; let up and locked it only cools");
        }
        // the automatic clutch at speed (the user's hill test, 27 October): from kAutoLockKmh up it stays locked, and with
        // the engine held under its idle speed in a high gear it lugs (the game's stall seconds rise, gauges.cpp); back
        // above idle it does not; in the slow band it slips as before
        {
            auto at = [&](float kmh, float inputShare) {
                TruckRaw t = c;
                t.gear = t.gearEngaged = 4;
                t.speed = kmh / 3.6f;
                for (int i = 0; i < t.wheels; i++) t.spin[i] = TruckGearTop(t, 4) * inputShare * k.idle / k.redline;
                ClutchModel cm = {};
                for (int i = 0; i < 10; i++) ClutchStep(cm, t, k, curve, 0.0f, ThrottleShape(1.0f), 1.0f, true, kDt);
                return cm;
            };
            const ClutchModel lugging = at(20.0f, 0.6f), pulling = at(20.0f, 1.5f), crawling = at(3.0f, 0.3f);
            Check(lugging.locked && lugging.lugging && Near(lugging.rpm, lugging.input, 1.0f) && pulling.locked && !pulling.lugging && !crawling.locked &&
                      !crawling.lugging && crawling.dead == false,
                  "clutch, the automatic one at speed: from 15 km/h it stays locked, lugging with the engine under idle in a high gear and not once above "
                  "it; at a crawl it slips as before");
        }
        // the automatic clutch (no pedal axis: keyboard, pad, a button clutch) burns as a pedal does (the user, 26 October:
        // "it should do it if trying to use a too high gear even when using the sequential mode"): it slips until the
        // gearbox's input has come up to the engine's speed. Floored from a standstill, a start in first stays under a
        // third of the burn point; a start in too high a gear burns. The truck gains 2000 rpm of the gearbox's input a
        // second for each share of the most torque in first; in a taller gear by the square of the gears' speeds, as the
        // wheels gain more slowly and each turn of them is fewer turns of the gearbox (this truck's 3, 6, 12 and 16: a
        // quarter of first's pace in second, a sixteenth in third, 1/28 in fourth)
        {
            const float stock = 195000.0f;
            auto gain = [&](int gear) { const float r = TruckGearTop(c, 1) / TruckGearTop(c, gear); return 2000.0f * r * r; };
            float warned = -1.0f; // when the last start's heat came to the amber warning
            float rate = 1.0f;    // ClutchHeatPercent / 100: the model's own pace unless set
            auto start = [&](int gear, float gain, float seconds, float &most, float &began) {
                TruckRaw t = c;
                t.gear = t.gearEngaged = gear;
                ClutchModel cm = {};
                ClutchHeat h = {};
                float input = 0.0f;
                most = 0.0f;
                began = -1.0f;
                for (float at = 0.0f; at < seconds; at += kDt)
                {
                    for (int i = 0; i < t.wheels; i++) t.spin[i] = TruckGearTop(t, gear) * input / k.redline;
                    ClutchStep(cm, t, k, curve, 0.0f, ThrottleShape(1.0f), 1.0f, true, kDt);
                    input = fmaxf(0.0f, input + cm.carried * gain * kDt);
                    ClutchHeatStep(h, 1, cm, stock, true, kDt, 0.0f, rate);
                    most = fmaxf(most, h.q);
                    if (h.burning && began < 0.0f) began = at;
                    if (h.q >= kClutchWarnFrom && warned < 0.0f) warned = at;
                }
            };
            float most[5] = {}, began[5] = {}, stuckMost = 0.0f, stuckBegan = 0.0f, thirdWarned = -1.0f;
            for (int g = 1; g <= 4; g++)
            {
                warned = -1.0f;
                start(g, gain(g), 8.0f, most[g], began[g]);
                if (g == 3) thirdWarned = warned;
            }
            start(3, 0.0f, 8.0f, stuckMost, stuckBegan); // a gear it cannot pull at all: the truck does not move
            printf("     automatic clutch floored from a standstill: first at most %.2f of %.2f (amber from %.2f), second %.2f; third amber after %.1f s, %.2f%s%.1f s; "
                   "fourth %.2f%s%.1f s; a gear it cannot pull %.2f%s%.1f s\n",
                   most[1], kClutchBurnFrom, kClutchWarnFrom, most[2], thirdWarned, most[3], began[3] >= 0.0f ? ", red after " : ", no red ", began[3], most[4],
                   began[4] >= 0.0f ? ", red after " : ", no red ", began[4], stuckMost, stuckBegan >= 0.0f ? ", red after " : ", no red ", stuckBegan);
            Check(most[1] < kClutchWarnFrom && began[1] < 0.0f && thirdWarned > 1.4f && thirdWarned < 2.6f && began[3] > 3.4f && began[3] < 4.6f && began[4] >= 0.0f &&
                      began[4] <= began[3] && stuckBegan >= 0.0f && stuckBegan <= began[3],
                  "clutch heat, the automatic clutch slips until the truck catches up: floored from a standstill a start in first never warns; one in third "
                  "warns amber after about two seconds and goes red after about four, in fourth sooner, and in a gear the engine cannot pull soonest");
            // ClutchHeatPercent at its default 60: the same starts at 0.6 of the heating, the cooling as it is
            rate = 0.6f;
            float m60[5] = {}, b60[5] = {}, stuck60Most = 0.0f, stuck60Began = 0.0f, third60Warned = -1.0f, fourth60Warned = -1.0f, stuck60Warned = -1.0f;
            for (int g = 1; g <= 4; g++)
            {
                warned = -1.0f;
                start(g, gain(g), 12.0f, m60[g], b60[g]);
                if (g == 3) third60Warned = warned;
                if (g == 4) fourth60Warned = warned;
            }
            warned = -1.0f;
            start(3, 0.0f, 12.0f, stuck60Most, stuck60Began);
            stuck60Warned = warned;
            rate = 1.0f;
            printf("     at ClutchHeatPercent 60: first at most %.2f; third amber after %.1f s, at most %.2f%s%.1f s; fourth amber after %.1f s, %.2f%s%.1f s; "
                   "a gear it cannot pull amber after %.1f s, %.2f%s%.1f s\n",
                   m60[1], third60Warned, m60[3], b60[3] >= 0.0f ? ", red after " : ", no red ", b60[3], fourth60Warned, m60[4],
                   b60[4] >= 0.0f ? ", red after " : ", no red ", b60[4], stuck60Warned, stuck60Most, stuck60Began >= 0.0f ? ", red after " : ", no red ", stuck60Began);
            Check(m60[1] < m60[3] && m60[1] < kClutchWarnFrom && third60Warned > thirdWarned && stuck60Began > stuckBegan && stuck60Began >= 0.0f,
                  "clutch heat at ClutchHeatPercent 60: every start heats more slowly than at 100, first never warns, and a gear the engine cannot pull "
                  "still burns");
        }
        // let up at speed: the engine turns with the gearbox; pressed again with the foot off, back to idle
        wheels(TruckGearTop(c, 1) * 0.6f);
        ClutchModel cruise = {};
        for (float at = 0.0f; at < 2.0f; at += kDt) ClutchStep(cruise, c, k, curve, 0.0f, ThrottleShape(0.5f), 1.0f, false, kDt);
        const bool turning = cruise.locked && Near(cruise.rpm, k.redline * 0.6f, 1.0f);
        for (float at = 0.0f; at < 3.0f; at += kDt) ClutchStep(cruise, c, k, curve, 1.0f, 0.0f, 1.0f, false, kDt);
        Check(turning && !cruise.locked && Near(cruise.rpm, k.idle, range * 0.03f),
              "clutch: let up at speed the engine turns with the gearbox, and pressed with the foot off it is back at idle within three seconds");
        // a stalled engine stays dead for kStallRest, then the model's engine starts over; neutral is never a stall
        for (float at = 0.0f; at < kStallRest + 0.1f; at += kDt) ClutchStep(dump, c, k, curve, 1.0f, 0.0f, 1.0f, false, kDt);
        TruckRaw neutral = c;
        neutral.gear = neutral.gearEngaged = 0;
        wheels(0.0f);
        ClutchModel neutralRun = {};
        for (float at = 0.0f; at < 1.0f; at += kDt) ClutchStep(neutralRun, neutral, k, curve, 0.0f, 0.0f, 1.0f, false, kDt);
        Check(Near(dump.rpm, k.idle, range * 0.03f) && !dump.dead && neutralRun.stalls == 0 && !neutralRun.connected && Near(neutralRun.rpm, k.idle, range * 0.03f),
              "clutch: a stalled engine starts over after its rest with the clutch pressed, and in neutral the clutch takes nothing");
        // stalled and left in gear with the clutch up, the engine stays dead past its rest; in neutral it starts again
        ClutchModel left = {};
        for (float at = 0.0f; at < 0.5f; at += kDt) ClutchStep(left, c, k, curve, 1.0f, 0.0f, 1.0f, false, kDt);
        int leftStalls = 0;
        for (float at = 0.0f; at < kStallRest * 3.0f; at += kDt) leftStalls += ClutchStep(left, c, k, curve, 0.0f, 0.0f, 1.0f, false, kDt) ? 1 : 0;
        const bool stillDead = left.dead && left.rpm == 0.0f && leftStalls == 1;
        for (float at = 0.0f; at < 1.0f; at += kDt) ClutchStep(left, neutral, k, curve, 0.0f, 0.0f, 1.0f, false, kDt);
        char deadLine[200];
        ClutchModel shown = {};
        shown.dead = true;
        ClutchProbeText(shown, 0.0f, deadLine, sizeof deadLine);
        Check(stillDead && !left.dead && Near(left.rpm, k.idle, range * 0.03f) && strstr(deadLine, "waiting for the clutch pressed or neutral"),
              "clutch: stalled and left in gear with the clutch up, the engine stays dead, once, and starts again in neutral");

        // without a pedal the clutch works by itself: standing in gear at idle it takes nothing and nothing stalls;
        // a little pedal pulls the truck away and it locks; slowing under idle it lets go again
        wheels(0.0f);
        ClutchModel byItself = {};
        int autoStalls = 0;
        for (float at = 0.0f; at < 3.0f; at += kDt) autoStalls += ClutchStep(byItself, c, k, curve, 0.0f, 0.0f, 1.0f, true, kDt) ? 1 : 0;
        const bool standing = autoStalls == 0 && Near(byItself.rpm, k.idle, range * 0.02f) && fabsf(byItself.carried) < 0.1f && byItself.automatic;
        float autoInput = 0.0f, autoDip = 1e9f;
        for (float at = 0.0f; at < 8.0f; at += kDt)
        {
            wheels(TruckGearTop(c, 1) * autoInput / k.redline);
            autoStalls += ClutchStep(byItself, c, k, curve, 0.0f, ThrottleShape(0.33f), 1.0f, true, kDt) ? 1 : 0;
            autoInput = fmaxf(0.0f, autoInput + byItself.carried * 2000.0f * kDt);
            autoDip = fminf(autoDip, byItself.rpm);
        }
        const bool pulled = autoStalls == 0 && byItself.locked && byItself.input > k.idle && autoDip >= k.idle * kClutchStall;
        for (float at = 0.0f; at < 3.0f; at += kDt)
        {
            autoInput = fmaxf(0.0f, autoInput - 3000.0f * kDt); // braked to a stop in gear
            wheels(TruckGearTop(c, 1) * autoInput / k.redline);
            autoStalls += ClutchStep(byItself, c, k, curve, 0.0f, 0.0f, 1.0f, true, kDt) ? 1 : 0;
        }
        Check(standing && pulled && autoStalls == 0 && !byItself.locked && Near(byItself.rpm, k.idle, range * 0.03f),
              "clutch: without a pedal it works by itself: in gear at idle it takes nothing, a little pedal pulls away and locks, braked to a stop it lets go, "
              "and the engine never stalls");
        Check(Near(ClutchGrip(0.0f), 1.0f, 0.001f) && ClutchGrip(1.0f) == 0.0f && ClutchGrip(kClutchFree) == 0.0f && Near(ClutchGrip(kClutchBite), 1.0f, 0.001f) &&
                  Near(ClutchGrip(0.5f * (kClutchFree + kClutchBite)), 0.5f, 0.001f),
              "clutch: the grip is all of it let up, none pressed, and the bite between");
    }

    // the brake air: filled by the engine's speed, held between the governor's cut-in and cut-out, drawn by the brake
    // and the handbrake, and leaking with the engine off
    {
        TruckRaw airTruck = Truck();
        airTruck.gear = airTruck.gearEngaged = 1;
        const float idleSpeed = k.idle / k.redline;
        // the time to fill an empty tank to the cut-out at idle and at the red zone's speed
        auto fill = [&](float speed) {
            AirModel m = {};
            AirStep(m, airTruck, speed, kDt);
            m.bar = 0.0f;
            m.pumping = true;
            float at = 0.0f;
            while (m.pumping && at < 600.0f)
            {
                AirStep(m, airTruck, speed, kDt);
                at += kDt;
            }
            return at;
        };
        const float atIdle = fill(idleSpeed), atRed = fill(1.0f);
        Check(atIdle > 140.0f && atIdle < 220.0f && atRed > 45.0f && atRed < 75.0f, "brake air: an empty tank fills in about three minutes at idle and one at the red zone");
        // ten minutes at idle with a half press of the brake every ten seconds: the compressor rests until the cut-in,
        // and the needle stays between the two
        AirModel gov = {};
        float airLow = 100.0f, airHigh = 0.0f, startedAt = -1.0f;
        int presses = 0;
        for (int step = 0; step < (int)(600.0f / kDt); step++)
        {
            const int inTen = step % (int)(10.0f / kDt);
            const bool press = inTen < (int)(2.0f / kDt);
            airTruck.throttle = press ? 0.5f : 0.0f;
            presses += inTen == 0 ? 1 : 0;
            const bool wasPumping = gov.pumping;
            AirStep(gov, airTruck, idleSpeed, kDt);
            if (gov.pumping && !wasPumping && startedAt < 0.0f) startedAt = gov.bar;
            airLow = fminf(airLow, gov.bar);
            airHigh = fmaxf(airHigh, gov.bar);
        }
        airTruck.throttle = 0.0f;
        Check(presses == 60 && startedAt <= kAirCutIn + 0.001f && startedAt > kAirCutIn - kBrakeAir && airLow > kAirCutIn - kBrakeAir - 0.05f && airHigh <= kAirCutOut + 0.001f &&
                  gov.cutOuts >= 1,
              "brake air: the governor starts the compressor at the cut-in and stops it at the cut-out, and the needle stays between them while braking at idle");
        // with the engine off: seven full brake applications leave it over the red zone, the eighth takes it in
        TruckRaw stopped = airTruck;
        stopped.flags = 0;
        AirModel fan = {};
        AirStep(fan, stopped, 0.0f, kDt);
        auto press = [&](float how) {
            stopped.throttle = how;
            AirStep(fan, stopped, 0.0f, kDt);
            stopped.throttle = 0.0f;
            AirStep(fan, stopped, 0.0f, kDt);
        };
        for (int i = 0; i < 7; i++) press(1.0f);
        const float afterSeven = fan.bar;
        press(1.0f);
        Check(afterSeven > kAirWarn && fan.bar < kAirWarn, "brake air: with the engine off seven full brake applications leave it over the red zone, the eighth takes it in");
        // the handbrake let go takes the spring brakes' air; ten minutes standing with the engine off lose a bar
        AirModel spring = {};
        stopped.handbrake = 1;
        AirStep(spring, stopped, 0.0f, kDt);
        stopped.handbrake = 0;
        const float charged = spring.bar;
        AirStep(spring, stopped, 0.0f, kDt);
        const float released = charged - spring.bar;
        for (float at = 0.0f; at < 600.0f; at += kDt) AirStep(spring, stopped, 0.0f, kDt);
        Check(Near(released, kSpringAir, 0.01f) && Near(charged - released - spring.bar, 1.0f, 0.02f), "brake air: letting the handbrake go takes the spring brakes' air, and it leaks a bar in ten minutes standing");
    }

    // what the temperature costs the engine
    {
        GaugeSettings set;
        SettingsDefaults(set);
        set.damage = true;
        Check(DamageHot(kTempRed, set) == 0.0f && DamageHot(100.0f, set) == 0.0f && Near(DamageHot(121.5f, set), 5.0f, 0.001f) &&
              Near(DamageHot(kTempHotDamage, set), 10.0f, 0.001f) && Near(DamageHot(132.0f, set), 20.0f, 0.001f),
              "damage: nothing up to the red zone at 118 C, 10 percent a minute at 125 C, half of that half way there and more above");
        GaugeSettings early = set;
        early.hotDamageFrom = 110.0f;
        Check(DamageHot(kTempHotDamage, set) == set.hotDamage && DamageHot(112.0f, early) > 0.0f && DamageHot(108.0f, early) == 0.0f &&
                  Near(DamageHot(kTempHotDamage, early), 10.0f, 0.001f),
              "damage: from 110 C an engine at 112 C takes damage and one at 108 C none, the rate still reached at 125 C as at the default");
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

    // the tire inflation mod's block, made here as that mod makes it and read as the cluster reads it, and the lamp
    {
        TiresReader reader = {};
        const bool noneYet = !TiresRead(reader, 1000).present; // looked for, not there: looked for again from 6000
        wchar_t name[64];
        swprintf(name, 64, L"Local\\SnowRunnerTirePressure.State.%lu", GetCurrentProcessId());
        const HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 64, name);
        TiresBlock *block = mapping ? (TiresBlock *)MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 64) : nullptr;
        bool read = false, lamps = false, gone = false;
        if (block)
        {
            block->magic = kTiresMagic;
            block->version = 1;
            block->size = 64;
            block->sequence = 2;
            block->tick = 6900;
            block->vehicle = 0x55;
            block->mode = 0;
            block->pressure = 0.45f;
            block->flags = kTiresVehicle;
            const bool tooSoon = !TiresRead(reader, 5000).present;
            TiresView v = TiresRead(reader, 7000);
            read = tooSoon && v.present && v.driving && v.vehicle == 0x55 && v.mode == 0 && v.pressure == 0.45f && !v.changing && !v.tooFast;
            int lamp = -1;
            bool lit = false, warn = false;
            TiresLamp(v, 0x55, 0, lamp, lit, warn);
            const bool low = lamp == 1 && lit && !warn;
            TiresLamp(v, 0x66, 0, lamp, lit, warn);
            const bool otherTruck = lamp == 0 && !lit;
            block->mode = 2;
            v = TiresRead(reader, 7000);
            TiresLamp(v, 0x55, 0, lamp, lit, warn);
            const bool normal = lamp == 3 && !lit && !warn;
            block->flags = kTiresVehicle | kTiresChanging;
            v = TiresRead(reader, 7000);
            TiresLamp(v, 0x55, 0, lamp, lit, warn);
            const bool blinkOn = lamp == 3 && lit;
            TiresLamp(v, 0x55, 400, lamp, lit, warn);
            const bool blinkOff = !lit && !warn;
            block->mode = 3;
            block->flags = kTiresVehicle;
            v = TiresRead(reader, 7000);
            TiresLamp(v, 0x55, 400, lamp, lit, warn);
            const bool increased = lamp == 4 && lit && !warn;
            block->flags = kTiresVehicle | kTiresTooFast;
            v = TiresRead(reader, 7000);
            TiresLamp(v, 0x55, 400, lamp, lit, warn);
            lamps = low && otherTruck && normal && blinkOn && blinkOff && increased && lamp == 4 && lit && warn;
            // a block that stands still, one in the middle of a write, one that is not the tire mod's
            const bool stale = !TiresRead(reader, 6900 + kTiresStaleMs + 1).present;
            block->sequence = 3;
            const bool writing = !TiresRead(reader, 7000).present;
            block->sequence = 4;
            block->magic = 0x12345678;
            gone = stale && writing && !TiresRead(reader, 7000).present;
            UnmapViewOfFile(block);
        }
        if (mapping) CloseHandle(mapping);
        Check(noneYet && read && lamps && gone, "tires: the tire mod's block is found once it is there and read while it is fresh and whole; the lamp shows the mode, lit away from Normal, "
                                                 "blinking while the pressure changes, red while the tires take damage, and is not there for another truck");
    }

    // the state for other mods: written as the mod writes it, and read back through the name as another mod reads it
    {
        StateWriter writer = {};
        wchar_t name[64];
        StateName(name, 64);
        wchar_t expected[64];
        swprintf(expected, 64, L"Local\\SnowRunnerGauges.State.%lu", GetCurrentProcessId());
        const bool opened = StateOpen(writer) && !wcscmp(name, expected);
        StateValues values = {};
        values.vehicle = 0x1234567890ull;
        values.ambientC = -12.5f;
        values.coolantC = 88.0f;
        values.rpm = 1850.0f;
        values.boostBar = 1.2f;
        values.flags = kStateVehicle | kStateByMap | kStateEngineOn;
        StateWrite(writer, values, 777000);
        const HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        const uint8_t *seen = mapping ? (const uint8_t *)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 64) : nullptr;
        bool read = false, again = false;
        if (seen)
        {
            // by the offsets of version 1, not through the writer's struct
            auto u32 = [&](size_t at) { uint32_t v; memcpy(&v, seen + at, 4); return v; };
            auto u64 = [&](size_t at) { uint64_t v; memcpy(&v, seen + at, 8); return v; };
            auto f32 = [&](size_t at) { float v; memcpy(&v, seen + at, 4); return v; };
            const uint32_t sequence = u32(12);
            read = u32(0) == 0x47554147u && u32(4) == 1 && u32(8) == 64 && !(sequence & 1) && u64(16) == 777000 && u64(24) == 0x1234567890ull && f32(32) == -12.5f &&
                   f32(36) == 88.0f && f32(40) == 1850.0f && f32(44) == 1.2f && u32(48) == 7 && u32(52) == 0 && u32(56) == 0 && u32(60) == 0;
            values = StateValues{};
            values.ambientC = 15.0f;
            StateWrite(writer, values, 777050);
            again = u32(12) == sequence + 2 && u64(16) == 777050 && u64(24) == 0 && f32(32) == 15.0f && f32(36) == 0.0f && u32(48) == 0 && u32(0) == 0x47554147u;
            UnmapViewOfFile(seen);
        }
        if (mapping) CloseHandle(mapping);
        StateWriter none = {};
        StateWrite(none, values, 1); // a writer without a block writes nowhere
        Check(opened && read && again, "state: the block for other mods carries its magic, version and size, each write leaves the sequence even and two further, and the values stand at their places");
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
        // the air dryer's purge: about 0.6 s, loudest in its first tenth, brought to the common loudness there
        SoundClip purge;
        SoundMakePurge(purge);
        const size_t tenth = purge.rate / 10;
        auto rmsAt = [&](size_t from) {
            double sum = 0.0;
            for (size_t i = from; i < from + tenth && i < purge.x.size(); i++) sum += (double)purge.x[i] * purge.x[i];
            return sqrt(sum / (double)tenth);
        };
        const double firstRaw = rmsAt(0), lastRaw = rmsAt(purge.x.size() - tenth);
        SoundLevel(purge);
        Check(!purge.x.empty() && Near((float)purge.x.size() / purge.rate, 0.6f, 0.01f) && firstRaw > 4.0 * lastRaw && Near((float)rmsAt(0), kSoundRms, 0.01f),
              "sound: the purge made by code is a hiss of 0.6 s, loudest in its first tenth, at the common loudness there");
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

    // the clutch's heat, at the figures over its constants: a slipping clutch carrying `carried` at `slip` rpm over the
    // gearbox for a stock engine (Torque 195000) or three times that, stepped at 20 a second
    {
        const float stock = 195000.0f, dt = 0.05f;
        auto slipping = [](float carried, float slip) {
            ClutchModel c = {};
            c.running = c.connected = true;
            c.input = 800.0f;
            c.rpm = 800.0f + slip;
            c.carried = carried;
            return c;
        };
        // seconds to the burn point (-1 never within `seconds`), and the share of the gearbox owed in the two after
        auto run = [&](ClutchHeat &h, uint64_t id, float carried, float slipFrom, float slipTo, float seconds, float torque, bool on, float &began, float &owedAfter) {
            began = -1.0f;
            float paid = 0.0f;
            for (float t = 0.0f; t < seconds; t += dt)
            {
                const float slip = slipFrom + (slipTo - slipFrom) * t / seconds;
                ClutchHeatStep(h, id, slipping(carried, slip), torque, on, dt);
                if (h.burning && began < 0.0f) began = t;
                if (began >= 0.0f && t < began + 2.0f) paid += h.owed;
                h.owed = 0.0f; // dealt each step
            }
            owedAfter = paid;
        };
        float began = 0.0f, owed = 0.0f;
        ClutchHeat first = {};
        run(first, 1, 1.0f, 1200.0f, 0.0f, 1.5f, stock, true, began, owed);
        const bool firstCold = began < 0.0f && first.q < kClutchWarnFrom;
        ClutchHeat third = {};
        run(third, 1, 1.0f, 1600.0f, 1600.0f, 6.0f, stock, true, began, owed);
        const float thirdBegan = began, thirdOwed = owed;
        ClutchHeat strong = {};
        run(strong, 1, 1.0f, 1600.0f, 1600.0f, 6.0f, stock * 3.0f, true, began, owed);
        const float strongBegan = began, strongOwed = owed;
        ClutchHeat hill = {};
        run(hill, 1, 0.5f, 1300.0f, 1300.0f, 4.5f, stock, true, began, owed);
        const bool hillNotYet = began < 0.0f;
        run(hill, 1, 0.5f, 1300.0f, 1300.0f, 3.0f, stock, true, began, owed);
        const bool hillBurns = began >= 0.0f;
        printf("     clutch heat: third floored burns after %.2f s and costs %.3f in the two after; three times the torque %.2f s and %.3f\n", thirdBegan, thirdOwed,
               strongBegan, strongOwed);
        // as retuned after the user's first test (the idealised slip of the first tuning: carrying 1.0 at 1600 rpm): the
        // first gear never warns; third goes red after about two seconds and costs about a twentieth in the two after,
        // half the first tuning's tenth; three times the torque goes red three times as soon and costs more
        Check(firstCold && thirdBegan > 1.5f && thirdBegan < 2.6f && thirdOwed > 0.04f && thirdOwed < 0.09f && strongBegan * 2.5f < thirdBegan &&
                  strongOwed > thirdOwed && hillNotYet && hillBurns,
              "clutch heat: a first-gear start never warns; a floored start in third burns after about two seconds and costs about a twentieth of the "
              "gearbox in the two after, three times the torque burns three times as soon; a hill held at half throttle burns after about six seconds");
        // no cost from stored heat: past the burn point and then locked, the clutch owes nothing and only cools, the
        // warning red until it falls under the burn point; a repair clears the heat
        {
            ClutchHeat hot = {};
            for (int i = 0; i < 200; i++) ClutchHeatStep(hot, 3, slipping(1.0f, 1600.0f), stock, true, dt); // ten seconds slipping
            const bool paying = hot.dealing && hot.owed > 0.0f && ClutchLamp(hot) == kClutchLampDealing && ClutchWarn(hot, true).state == kWarnRed;
            ClutchModel locked = slipping(1.0f, 0.0f);
            locked.locked = true;
            ClutchHeatStep(hot, 3, locked, stock, true, dt);
            const bool stopped = !hot.dealing && hot.owed == 0.0f && hot.q > kClutchBurnFrom && ClutchLamp(hot) == kClutchLampRed;
            const float qLocked = hot.q;
            float owedLater = 0.0f;
            for (int i = 0; i < 100; i++)
            {
                ClutchHeatStep(hot, 3, locked, stock, true, dt);
                owedLater += hot.owed;
            }
            const bool cooling = owedLater == 0.0f && hot.q < qLocked * 0.8f;
            ClutchHeatRepair(hot);
            const bool repaired = hot.q == 0.0f && hot.owed == 0.0f && ClutchLamp(hot) == kClutchLampOff && ClutchWarn(hot, true).state == kWarnNone;
            // a burn's job no frame took is dropped by the repair; a stall's stays
            StallJob job = {};
            TruckRefs refs = {};
            refs.vehicle = 0x1000;
            const bool burnDropped = StallJobPost(job, refs, 2.0f, 0, true) && StallJobDropBurn(job) && job.state == kStallJobIdle;
            const bool stallKept = StallJobPost(job, refs, 5.0f, 0, false) && !StallJobDropBurn(job) && job.state == kStallJobPosted;
            Check(paying && stopped && cooling && repaired && burnDropped && stallKept,
                  "clutch heat: it costs only while it slips past the burn point, nothing once it grips however hot, and a repair clears the heat and "
                  "drops a burn's points not dealt yet, a stall's kept");
        }
        // the warning boxes: the clutch's off, amber from half the burn point, red from it, the number its share of the
        // burn point; the engine's amber from the coolant's red mark, red while the heat costs, its film title at the
        // turbo's full with the pedal floored and the floor pan's line once a point lands under it
        {
            ClutchHeat wh = {};
            wh.known = true;
            auto at = [&](float q) { wh.q = wh.truck[0].q = q; wh.dealing = false; return ClutchWarn(wh, true); };
            const WarnBox none = at(kClutchWarnFrom * 0.9f), amber = at(kClutchWarnFrom * 1.1f), red = at(kClutchBurnFrom * 1.5f), off = ClutchWarn(wh, false);
            const bool clutchBoxes = none.state == kWarnNone && amber.state == kWarnAmber && !strcmp(amber.title, "CLUTCH HOT") &&
                                     !strcmp(amber.line, "Ease off or shift down") && amber.percent == (int)(kClutchWarnFrom * 1.1f / kClutchBurnFrom * 100.0f) &&
                                     red.state == kWarnRed && !strcmp(red.title, "CLUTCH DAMAGE") && !strcmp(red.line, "Shift down, the clutch is wearing") &&
                                     red.percent == 100 && off.state == kWarnNone;
            bool pan = false;
            const WarnBox cool = EngineWarn(110.0f, 0.0f, true, false, false, pan), hotAmber = EngineWarn(kTempRed + 1.0f, 0.0f, true, false, false, pan),
                          hotRed = EngineWarn(121.5f, 3.0f, true, false, true, pan), noDamage = EngineWarn(130.0f, 5.0f, false, true, true, pan);
            const bool engineBoxes = cool.state == kWarnNone && hotAmber.state == kWarnAmber && !strcmp(hotAmber.title, "ENGINE HOT") &&
                                     !strcmp(hotAmber.line, "Ease off, the coolant is in the red") && hotRed.state == kWarnRed && !strcmp(hotRed.title, "ENGINE DAMAGE") &&
                                     !strcmp(hotRed.line, "Ease off, the engine is wearing") && hotRed.percent == 50 && noDamage.state == kWarnNone && !pan;
            // the film: DANGER TO MANIFOLD only at the turbo's full with the pedal floored, the floor pan watched, then
            // fallen once a point lands, for the rest of the red spell, and the plain lines again once it is amber
            const bool fullBoost = BoostFull(1.0f, 1.0f, 1.0f) && !BoostFull(0.9f, 1.0f, 1.0f) && !BoostFull(1.0f, 1.0f, 0.8f);
            const WarnBox manifold = EngineWarn(122.0f, 3.0f, true, true, false, pan);
            const bool watched = !strcmp(manifold.title, "DANGER TO MANIFOLD") && !strcmp(manifold.line, "Keep an eye on the floor pan") && !pan;
            const WarnBox fell = EngineWarn(122.0f, 3.0f, true, true, true, pan);
            const WarnBox stillRed = EngineWarn(122.0f, 3.0f, true, false, false, pan);
            const bool fallen = !strcmp(fell.title, "DANGER TO MANIFOLD") && !strcmp(fell.line, "The floor pan fell off") && pan && !strcmp(stillRed.title, "ENGINE DAMAGE") &&
                                !strcmp(stillRed.line, "The floor pan fell off");
            const WarnBox backAmber = EngineWarn(kTempRed + 1.0f, 0.0f, true, false, false, pan);
            const WarnBox againRed = EngineWarn(122.0f, 3.0f, true, false, false, pan);
            const bool plainAgain = backAmber.state == kWarnAmber && !pan && !strcmp(againRed.line, "Ease off, the engine is wearing");
            Check(clutchBoxes && engineBoxes && fullBoost && watched && fallen && plainAgain,
                  "warnings: the clutch's box amber from half the burn point and red from it with its number; the engine's amber from the red mark and red "
                  "while the heat costs; at the turbo's full the film's title, the floor pan watched and then fallen for the rest of that red spell");
        }
        // cooling with the clutch open, kept per kind of truck, and nothing owed with ClutchBurn or Damage off
        ClutchHeat h = {};
        run(h, 7, 1.0f, 1600.0f, 1600.0f, 4.0f, stock, true, began, owed);
        const float hot = h.q;
        for (int i = 0; i < 80; i++) ClutchHeatStep(h, 7, ClutchModel{}, stock, true, dt); // four seconds open
        const bool cooled = h.q < hot * 0.53f && h.q > hot * 0.47f;
        const float coolShare = h.q / hot;
        const float leftAt = h.q;
        for (int i = 0; i < 200; i++) ClutchHeatStep(h, 8, ClutchModel{}, stock, true, dt); // ten seconds in another truck
        ClutchHeatStep(h, 7, ClutchModel{}, stock, true, dt);
        const bool kept = h.q < leftAt * 0.21f && h.q > leftAt * 0.15f; // e^(-0.173 x 10) = 0.18
        const float keptShare = h.q / leftAt;
        // the heating by the truck's speed: full standing and at a crawl, about three quarters at 8 km/h, a fifth from
        // 15 km/h up; the same slip at 20 km/h heats five times slower, and cooling is the same at any speed
        ClutchHeat still = {}, rolling = {};
        ClutchHeatStep(still, 9, slipping(1.0f, 1600.0f), stock, true, dt, 0.0f);
        ClutchHeatStep(rolling, 9, slipping(1.0f, 1600.0f), stock, true, dt, 20.0f);
        const bool bySpeed = ClutchHeatShare(0.0f) == 1.0f && ClutchHeatShare(4.0f) == 1.0f && fabsf(ClutchHeatShare(8.0f) - 0.76f) < 0.01f &&
                             fabsf(ClutchHeatShare(15.0f) - 0.2f) < 1e-5f && fabsf(ClutchHeatShare(40.0f) - 0.2f) < 1e-5f &&
                             fabsf(rolling.power * 5.0f - still.power) < still.power * 0.01f;
        printf("     clutch heat: %.3f of it left after four seconds open, %.3f after ten seconds away; the heating's share at 0, 8 and 20 km/h %.2f, %.2f, %.2f\n",
               coolShare, keptShare, ClutchHeatShare(0.0f), ClutchHeatShare(8.0f), ClutchHeatShare(20.0f));
        ClutchHeat off = {};
        run(off, 1, 1.0f, 1600.0f, 1600.0f, 6.0f, stock, false, began, owed);
        GaugeSettings gs;
        SettingsDefaults(gs);
        const bool defaults = gs.clutchBurn && gs.damage;
        Check(cooled && kept && bySpeed && began >= 0.0f && owed == 0.0f && off.owed == 0.0f && defaults,
              "clutch heat: an open clutch cools to half in four seconds; each kind of truck keeps its heat and cools while away; it heats fully only "
              "under 5 km/h, a fifth as fast from 15 km/h; burning with ClutchBurn or Damage off owes nothing");
    }

    // ---- review 11: the loop, and the paths the parts' tests did not reach ----
    {
        // the HX 520 in H at three quarters of the pedal, the mod's model only following (the tester's drive of 28
        // October): the game's own top 1.06 of the mod's figure, as his log has it (the wheels at 1.02 to 1.06 of the
        // gear's 16 rad/s with the game's pull still 0.1 at it), a strong push against a dirt road's drag. On smooth ground
        // both fades settle; on ground that rises and falls by 0.3 m/s² every 2.5 s the factor answers it, and since
        // c9c99ea it swings a third less with the speed no wider. No setting tried sustains a cycle on its own: the push
        // under the roof is the engine's curve over the game's fall, flat against the speed, so the drag alone sets the
        // speed there; the pulse the tester felt is the factor's steep answer near the top
        const TorqueCurve hc = kClassCurves[kClassHeavy];
        LoopTruck hx = LoopHeavy();
        hx.push = 8.0f;
        hx.drag0 = 0.7f;
        hx.drag2 = 0.0005f;
        hx.realTop = 1.06f;
        hx.fixedGear = 5;
        LoopSpan smooth[2], uneven[2];
        for (int b = 0; b < 2; b++)
        {
            LoopState st;
            st.v = 9.0f;
            LoopDrive(hx, st, hc, 0.75f, 30.0f, b != 0, false);
            smooth[b] = LoopDrive(hx, st, hc, 0.75f, 10.0f, b != 0);
            LoopTruck bumpy = hx;
            bumpy.bumps = 0.3f;
            LoopState sb;
            sb.v = 9.0f;
            LoopDrive(bumpy, sb, hc, 0.75f, 30.0f, b != 0, false);
            uneven[b] = LoopDrive(bumpy, sb, hc, 0.75f, 10.0f, b != 0);
        }
        const float swingNow = uneven[0].factorHi - uneven[0].factorLo, swingBefore = uneven[1].factorHi - uneven[1].factorLo;
        printf("     loop, the HX 520 in H at 0.75: smooth %.2f to %.2f (before %.2f to %.2f) at %.1f km/h; on uneven ground the factor %.2f to %.2f, %.1f to %.1f km/h "
               "(before %.2f to %.2f, %.1f to %.1f km/h)\n", smooth[0].factorLo, smooth[0].factorHi, smooth[1].factorLo, smooth[1].factorHi, smooth[0].vHi * 3.6f,
               uneven[0].factorLo, uneven[0].factorHi, uneven[0].vLo * 3.6f, uneven[0].vHi * 3.6f, uneven[1].factorLo, uneven[1].factorHi, uneven[1].vLo * 3.6f,
               uneven[1].vHi * 3.6f);
        Check(smooth[0].factorHi - smooth[0].factorLo < 0.02f && smooth[1].factorHi - smooth[1].factorLo < 0.02f && swingBefore > 0.5f && swingNow < 0.75f * swingBefore &&
                  uneven[0].vHi - uneven[0].vLo <= uneven[1].vHi - uneven[1].vLo + 0.05f,
              "loop: a heavy truck in H at three quarters of the pedal settles on smooth ground; on uneven ground the factor swings a third less than with "
              "the fade from 0.8 and no pace, the speed no wider");
        // the automatic driving the same truck floored from a rolling start (2 m/s: the game's pull of GamePull is nothing
        // under a tenth of a gear's top) on the level: through the gears with the speed fed back, no shift taken back,
        // the factor under the roof, the needle under the tachometer's top
        LoopTruck drive = LoopHeavy();
        drive.push = 8.0f;
        drive.drag0 = 0.3f;
        drive.drag2 = 0.0005f;
        LoopState ds;
        ds.v = 2.0f;
        float vAt20 = 0.0f, rpmMost = 0.0f, factorMost = 0.0f;
        for (float at = 0.0f; at < 40.0f; at += kDt)
        {
            LoopStep(drive, ds, hc, 1.0f, kDt, false);
            if (at < 20.0f) vAt20 = ds.v;
            rpmMost = fmaxf(rpmMost, ds.tach.rpm);
            factorMost = fmaxf(factorMost, ds.hand.written);
        }
        printf("     loop, the HX 520 floored from 2 m/s: gear %d after 40 s at %.1f km/h (%.1f at 20 s), %d shifts, %d taken back, the factor at most %.2f, "
               "the needle at most %.0f rpm\n", ds.gear, ds.v * 3.6f, vAt20 * 3.6f, ds.shifts, ds.box.hunts, factorMost, rpmMost);
        Check(ds.gear >= 3 && ds.v > vAt20 && ds.box.hunts == 0 && factorMost <= TorqueRoof(kClassHeavy) + 0.001f && rpmMost <= drive.k.max,
              "loop: the automatic takes a heavy truck floored from a rolling start up through the gears with its speed fed back, no shift taken back, "
              "the factor under the roof and the needle under the tachometer's top");

        // the creep (review 11's addendum): the share by its rules, then the loop with the share raising the game's throttle
        {
            const GaugeScale ck = kClassScales[kClassTruck];
            const float idleAt = ck.idle / ck.redline; // the wheels' share of the gear's top at the gear's idle speed
            TruckRaw c = Truck();
            Drive(c, 1, false, 0.0f, 0.0f);
            const float convStand = CreepShare(c, ck, true, 1.0f), govStand = CreepShare(c, ck, false, 1.0f), govHalfGrip = CreepShare(c, ck, false, 0.5f),
                        govPressed = CreepShare(c, ck, false, 0.0f);
            Drive(c, 1, false, 0.0f, 0.5f);
            const float convHalfPedal = CreepShare(c, ck, true, 1.0f);
            Drive(c, 1, false, 0.5f * idleAt, 0.0f);
            const float convHalf = CreepShare(c, ck, true, 1.0f);
            Drive(c, 1, false, kCouplingPoint * idleAt + 0.001f, 0.0f);
            const float convCoupled = CreepShare(c, ck, true, 1.0f);
            Drive(c, 1, false, (ck.idle - (ck.redline - ck.idle) * kGovernorBand) / ck.redline - 0.001f, 0.0f);
            const float govBand = CreepShare(c, ck, false, 1.0f);
            Drive(c, 1, false, idleAt - 0.01f, 0.0f);
            const float govNear = CreepShare(c, ck, false, 1.0f);
            Drive(c, 1, false, idleAt, 0.0f);
            const float govIdle = CreepShare(c, ck, false, 1.0f);
            Check(Near(convStand, kCreepShare, 1e-5f) && Near(govStand, kIdleShare, 1e-5f) && Near(govHalfGrip, 0.5f * kIdleShare, 1e-5f) && govPressed == 0.0f &&
                      Near(convHalfPedal, 0.5f * kCreepShare, 1e-5f) && convHalf > 0.2f * kCreepShare && convHalf < 0.8f * kCreepShare && convCoupled == 0.0f &&
                      Near(govBand, kIdleShare, 1e-4f) && govNear > 0.0f && govNear < 0.5f * kIdleShare && govIdle == 0.0f,
                  "creep: through the converter its share at a standstill fades to nothing at the coupling point, the governor's is whole a band under the gear's "
                  "idle speed and nothing at it, by the clutch's grip; both less as the pedal goes down");
            Drive(c, 1, false, 0.0f, 0.0f);
            c.throttle = 0.3f; // the brake
            const float braked = CreepShare(c, ck, true, 1.0f);
            c.throttle = 0.0f;
            c.handbrake = 1;
            const float handbraked = CreepShare(c, ck, true, 1.0f);
            c.handbrake = 0;
            Drive(c, 0, false, 0.0f, 0.0f);
            const float neutral = CreepShare(c, ck, true, 1.0f);
            Drive(c, 1, false, 0.0f, 0.0f);
            c.flags = 0;
            const float off = CreepShare(c, ck, true, 1.0f);
            c.flags = 1;
            c.startTimer = 1.0f;
            const float starting = CreepShare(c, ck, true, 1.0f);
            c.flags = 3;
            c.startTimer = 0.0f;
            Drive(c, -1, true, 0.0f, 0.0f);
            const float reverse = CreepShare(c, ck, true, 1.0f);
            c.throttle = -0.3f; // the brake in reverse with the automatic up is the other side
            const float reverseBraked = CreepShare(c, ck, true, 1.0f);
            Check(braked == 0.0f && handbraked == 0.0f && neutral == 0.0f && off == 0.0f && starting == 0.0f && Near(reverse, kCreepShare, 1e-5f) && reverseBraked == 0.0f,
                  "creep: nothing on the brake, the handbrake, in neutral, with the engine off or starting; in reverse with the automatic up as forward, its brake "
                  "on the other side");

            // the heavy truck on hard flat ground, standing in first at zero pedal: the game's pull at a standstill 0.3 of
            // its curve's (1 - D4, not known: the test build logs it). In A through the converter it creeps up and holds
            // under the gear's idle speed with the needle at idle; the brake stops it and holds it; in first picked by hand
            // the governor holds it near the gear's idle speed; with Creep off it stands
            const TorqueCurve hc = kClassCurves[kClassHeavy];
            LoopTruck crawl = LoopHeavy();
            crawl.push = 8.0f;
            crawl.drag0 = 0.1f;
            crawl.drag2 = 0.0005f;
            crawl.lowPull = 0.3f;
            crawl.creep = true;
            const float idleMs = crawl.k.idle / crawl.k.redline * TruckGearTop(crawl.t, 1) / crawl.grip; // m/s at the first gear's idle speed
            LoopState cs;
            LoopDrive(crawl, cs, hc, 0.0f, 30.0f, false, false);
            const LoopSpan held = LoopDrive(crawl, cs, hc, 0.0f, 10.0f, false);
            const float creepRpm = cs.tach.rpm, creepGear = (float)cs.gear;
            cs.brake = 0.5f;
            LoopDrive(crawl, cs, hc, 0.0f, 5.0f, false, false);
            const float stopV = cs.v, stopShare = cs.share;
            LoopTruck byHand = crawl;
            byHand.fixedGear = 1;
            LoopState hs;
            LoopDrive(byHand, hs, hc, 0.0f, 30.0f, false, false);
            const LoopSpan handHeld = LoopDrive(byHand, hs, hc, 0.0f, 10.0f, false);
            LoopTruck none = crawl;
            none.creep = false;
            LoopState ns;
            LoopDrive(none, ns, hc, 0.0f, 10.0f, false, false);
            printf("     creep, the HX 520 in first on hard flat ground: in A %.2f to %.2f m/s (the gear's idle speed %.2f m/s), the needle %.0f rpm (idle %.0f); "
                   "on the brake %.2f m/s; by hand %.2f to %.2f m/s, the needle %.0f rpm; with Creep off %.2f m/s\n",
                   held.vLo, held.vHi, idleMs, creepRpm, crawl.k.idle, stopV, handHeld.vLo, handHeld.vHi, hs.tach.rpm, ns.v);
            Check(held.vLo > 0.3f * idleMs && held.vHi < kCouplingPoint * idleMs && held.vHi - held.vLo < 0.02f && creepGear == 1.0f && Near(creepRpm, crawl.k.idle, 15.0f) &&
                      stopV == 0.0f && stopShare == 0.0f && handHeld.vLo > 0.85f * idleMs && handHeld.vHi < idleMs && handHeld.vHi - handHeld.vLo < 0.02f &&
                      Near(hs.tach.rpm, crawl.k.idle, 15.0f) && ns.v == 0.0f,
                  "loop: a heavy truck in first at zero pedal creeps in A under the gear's idle speed and holds it, the needle at idle, stops on the brake; "
                  "in first by hand it holds the gear's idle speed; with Creep off it stands");
        }

        // a truck's scales and the mod's engine for the unit paths below
        const GaugeScale tk = kClassScales[kClassTruck];
        const TorqueCurve tc = kClassCurves[kClassTruck];
        auto handOf = [&](int input) {
            return EngineHand{ true, &tc, tk.max / tk.redline, 1.0f, 0.0f, 0.0f, kLightPeak * CurvePeak(tc) * tk.redline, input, TorqueRoof(kClassTruck) };
        };
        TruckRaw lt = Truck();
        lt.automatic = 0;
        auto rollAt = [&](int gear, float input) {
            lt.gear = lt.gearEngaged = gear;
            const float spin = input / tk.redline * TruckGearTop(lt, gear);
            for (int i = 0; i < lt.wheels; i++) lt.spin[i] = spin;
            lt.speed = spin / 2.0f;
            return spin;
        };
        auto boxIn = [&](int gear, float spin, float pedal) {
            GearboxModel b = {};
            b.running = true;
            b.gear = gear;
            b.grip = 2.0f;
            b.spin = b.by = b.rolling = spin;
            b.pedal = b.held = b.feel = pedal;
            b.moving = 10.0f;
            return b;
        };
        // finding 1: a converter locked at a light foot (third gear, 1200 rpm on the input, over kLockIdle's 1070), slowed
        // in the lock to 950 (over kUnlockIdle's 910), then a press to 0.85 (short of a kickdown) keeps it locked in the
        // gear, the engine at the input, and the schedule's shift down to the second gives the pull (before, the press
        // lifted the unlock point over the input and flared the engine to 1220 rpm first); with keys, whose every press
        // is floored, the same from a coast
        auto pressFrom = [&](const EngineHand &h, float cruise, float pressTo, bool &openedInGear, float &mostAhead) {
            GearboxModel b = boxIn(3, rollAt(3, 1200.0f), cruise);
            for (float at = 0.0f; at < 1.0f; at += kDt) GearboxStep(b, lt, tk, cruise, cruise, kDt, h);
            const bool lockedThen = b.locked;
            for (float at = 0.0f; at < 1.0f; at += kDt)
            {
                rollAt(3, 1200.0f - 250.0f * at);
                GearboxStep(b, lt, tk, cruise, cruise, kDt, h);
            }
            const bool stillThen = b.locked && b.gear == 3;
            openedInGear = false;
            mostAhead = 0.0f;
            for (float at = 0.0f; at < 1.5f; at += kDt)
            {
                GearboxStep(b, lt, tk, pressTo, pressTo, kDt, h);
                if (b.gear != 3) break;
                openedInGear = openedInGear || !b.locked;
                mostAhead = fmaxf(mostAhead, b.rpm - b.input);
            }
            return lockedThen && stillThen;
        };
        bool padOpened = true, keyOpened = true;
        float padAhead = 0.0f, keyAhead = 0.0f;
        const bool padLocked = pressFrom(handOf(kInputPad), 0.2f, 0.85f, padOpened, padAhead);
        const bool keyLocked = pressFrom(handOf(kInputKeys), 0.0f, 1.0f, keyOpened, keyAhead);
        printf("     finding 1: locked at a light foot %d, a press to 0.85 opened it in the gear %d (the engine at most %.0f rpm over the input); a key press from a "
               "coast: locked %d, opened %d (%.0f over)\n", padLocked, padOpened, padAhead, keyLocked, keyOpened, keyAhead);
        Check(padLocked && !padOpened && padAhead < 1.0f && keyLocked && !keyOpened && keyAhead < 1.0f,
              "finding 1: a press from a light foot's lock, a pad's to 0.85 or a key's, keeps the converter locked in the gear, the engine at the input: no flare");
        // GearboxLowestFit with the engine on, as the game's call has it: by the tachometer's top, so a first gear the
        // engine turns between kDownMost's point and the top still fits; with the game's own engine it does not
        GearboxModel fit = boxIn(1, rollAt(1, 0.5f * (GearboxPoint(tk, kDownMost) + tk.max)), 1.0f);
        Check(GearboxPoint(tk, kDownMost) < tk.max && GearboxLowestFit(fit, lt, tk, true) == 1 && GearboxLowestFit(fit, lt, tk, false) == 2,
              "GearboxLowestFit: with the engine on a first gear past kDownMost's point but under the tachometer's top still fits; with the game's own it does not");
        // kShiftAgain: with the game's own engine a shift up after a shift up waits half the pause (close gears, floored,
        // the wheels gaining fast); a shift the other way waits all of it
        {
            TruckRaw qt = lt;
            static const float close[7] = { 1.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 4.0f };
            qt.gearSpeeds = 7;
            memcpy(qt.gearSpeed, close, sizeof close);
            GearboxModel qb = {};
            float spin = 0.0f, since = 0.0f, shortest = 100.0f;
            for (float at = 0.0f; at < 8.0f; at += kDt)
            {
                for (int i = 0; i < qt.wheels; i++) qt.spin[i] = spin;
                qt.speed = spin / 2.0f;
                qt.gear = qt.gearEngaged = qb.gear >= 1 ? qb.gear : 1;
                const int was = qb.gear;
                GearboxStep(qb, qt, tk, 1.0f, 1.0f, kDt);
                since += kDt;
                if (qb.gear > was && was >= 1)
                {
                    shortest = fminf(shortest, since);
                    since = 0.0f;
                }
                spin += 8.0f * kDt;
            }
            Check(shortest >= kShiftPause - kShiftAgain - kDt && shortest < kShiftPause - kDt,
                  "automatic: a shift up after a shift up waits half the pause with the game's own engine (kShiftAgain), not all of it");
        }
        // overRed while kept: a gear kept by the hunt guard still shifts up halfway into the red zone, and holds on the
        // limiter short of it
        {
            const EngineHand h = handOf(kInputPad);
            GearboxModel kr = boxIn(3, rollAt(3, 0.99f * tk.redline), 1.0f);
            kr.keep = 3;
            kr.keepFor = 10.0f;
            for (float at = 0.0f; at < 0.3f; at += kDt) GearboxStep(kr, lt, tk, 1.0f, 1.0f, kDt, h);
            const int onLimiter = kr.gear;
            GearboxModel ko = boxIn(3, rollAt(3, 0.5f * (tk.redline + tk.max) + 20.0f), 1.0f);
            ko.keep = 3;
            ko.keepFor = 10.0f;
            for (float at = 0.0f; at < 0.3f; at += kDt) GearboxStep(ko, lt, tk, 1.0f, 1.0f, kDt, h);
            Check(onLimiter == 3 && ko.gear == 4, "automatic: a gear kept holds on the limiter, but halfway into the red zone it goes up all the same (overRed)");
        }
        // the hunt guard let go by a stop and by a kickdown; and its wait doubles to kHuntKeepMost and no further
        {
            const EngineHand h = handOf(kInputPad);
            GearboxModel st = boxIn(3, rollAt(3, 0.0f), 0.5f);
            st.keep = 3;
            st.keepFor = st.keepWait = 8.0f;
            st.upFrom = 2;
            GearboxStep(st, lt, tk, 0.5f, 0.5f, kDt, h);
            GearboxModel kd = boxIn(4, rollAt(4, 900.0f), 0.5f);
            kd.keep = 4;
            kd.keepFor = kd.keepWait = 8.0f;
            for (float at = 0.0f; at < 0.5f; at += kDt) GearboxStep(kd, lt, tk, 0.5f, 0.5f, kDt, h);
            for (float at = 0.0f; at < 0.2f; at += kDt) GearboxStep(kd, lt, tk, 1.0f, 1.0f, kDt, h);
            // a shift up from 2 taken back within the window with the foot on, the last wait 16 s: the next 30, not 32
            GearboxModel hm = boxIn(3, rollAt(3, 700.0f), 0.5f);
            hm.upFrom = 2;
            hm.sinceUp = 1.0f;
            hm.keepWait = 16.0f;
            int huntsBefore = hm.hunts;
            for (float at = 0.0f; at < 2.0f && hm.gear == 3; at += kDt) GearboxStep(hm, lt, tk, 0.5f, 0.5f, kDt, h);
            Check(st.keepFor == 0.0f && st.upFrom == 0 && kd.keepFor == 0.0f && kd.gear < 4 && hm.gear == 2 && hm.hunts == huntsBefore + 1 &&
                      Near(hm.keepWait, kHuntKeepMost, 0.001f) && Near(hm.keepFor, kHuntKeepMost, kDt * 2.0f),
                  "automatic: a stop and a kickdown let the hunt guard go; its wait doubles up to 30 s and no further (kHuntKeepMost)");
        }
        // the converter's multiplication falls with the speed ratio from its stall to 1 at the coupling point; open and
        // rolling the engine runs a little ahead of the input, and an idling one creeps less as the truck rolls
        {
            auto ratioAt = [&](float share) {
                const float stall = ConverterStall(tk);
                GearboxModel cb = boxIn(1, rollAt(1, share * stall), 1.0f);
                GearboxStep(cb, lt, tk, 1.0f, 1.0f, kDt);
                return cb;
            };
            // the engine rises from the stall with the input (kStallRise), the ratio falls in a line with the speed ratio
            const GearboxModel r0 = ratioAt(0.0f), r1 = ratioAt(0.25f), r2 = ratioAt(0.5f), r3 = ratioAt(1.1f); // 1.1: coupled, still a gear the engine takes
            auto lineAt = [](const GearboxModel &m) { return 1.0f + (kStallRatio - 1.0f) * (1.0f - m.input / m.rpm / kCouplingPoint); };
            const bool ramp = Near(r0.ratio, kStallRatio, 0.001f) && r1.ratio < r0.ratio && r2.ratio < r1.ratio && r2.ratio > 1.0f && Near(r3.ratio, 1.0f, 0.001f) &&
                              Near(r1.ratio, lineAt(r1), 0.001f) && Near(r2.ratio, lineAt(r2), 0.001f) && r1.rpm > r0.rpm && r2.rpm > r1.rpm;
            GearboxModel slip = boxIn(2, rollAt(2, 1000.0f), 0.1f);
            GearboxStep(slip, lt, tk, 0.1f, 0.1f, kDt);
            GearboxModel creep = boxIn(2, rollAt(2, 400.0f), 0.0f);
            GearboxStep(creep, lt, tk, 0.0f, 0.0f, kDt);
            GearboxModel creepStand = boxIn(1, rollAt(1, 0.0f), 0.0f);
            GearboxStep(creepStand, lt, tk, 0.0f, 0.0f, kDt);
            Check(ramp && !slip.locked && Near(slip.rpm, slip.input * (1.0f + kConverterSlip), 1.0f) && slip.rpm > slip.input && creep.drive > 0.0f &&
                      creep.drive < creepStand.drive,
                  "converter: the multiplication falls in a line from its stall to 1 at the coupling point; open and rolling the engine runs a little ahead of the "
                  "input; an idling engine creeps less as the truck rolls");
        }
        // finding 4: the light foot's floor from where the curve first reaches 0.9 of its most, at the most 0.45 of the red
        // zone: a scout's class curve about 2370 rpm (it was 3300, from its 0.98 point at 0.75), a truck's 880
        {
            const GaugeScale sk = kClassScales[kClassScout];
            const float scoutFloor = EngineLightFloor(kClassCurves[kClassScout], sk.redline), truckFloor = EngineLightFloor(tc, tk.redline);
            static const TorqueCurve late = { 2, { 0.2f, 0.9f }, { 0.5f, 1.0f } };
            printf("     the light foot's floor: a scout %.0f rpm (from %.0f), a truck %.0f\n", scoutFloor, kLightPeak * CurvePeak(kClassCurves[kClassScout]) * sk.redline, truckFloor);
            Check(scoutFloor > 2200.0f && scoutFloor < 2500.0f && truckFloor > 850.0f && truckFloor < 910.0f && Near(EngineLightFloor(late, 1000.0f), kLightMost * 1000.0f, 0.5f),
                  "automatic: a light foot's floor from the curve's 0.9 point, at most 0.45 of the red zone (a scout cruises from about 2370 rpm, not 3300)");
        }
        // finding 9: an ordinary rise of the factor, with no cut in the last two seconds, is written at once; one after a
        // cut is paced
        {
            TorqueHand free = {};
            TorqueHandStep(free, true, 0x5000, 0x4000, 1.0f, 1.0f, 0.05f, 3);
            for (int i = 0; i < 50; i++) TorqueHandStep(free, true, 0x5000, 0x4000, free.written, 1.0f, 0.05f, 3);
            const TorqueOrders rise = TorqueHandStep(free, true, 0x5000, 0x4000, free.written, 2.4f, 0.05f, 3);
            const TorqueOrders cut = TorqueHandStep(free, true, 0x5000, 0x4000, free.written, 1.2f, 0.05f, 3);
            const TorqueOrders after = TorqueHandStep(free, true, 0x5000, 0x4000, free.written, 2.4f, 0.05f, 3);
            Check(rise.write && Near(rise.factor, 2.4f, 0.001f) && cut.write && Near(cut.factor, 1.2f, 0.001f) && after.write && after.factor < 1.3f,
                  "powertrain: a rise of the factor with no cut before it is written at once; within two seconds of a cut it climbs at its pace");
        }
        // finding 10: the floor lifting the game's gear past the model's held one moves the model's gear with it
        {
            GearboxModel held = boxIn(2, rollAt(2, 1500.0f), 1.0f);
            held.holdUp = true;
            held.moving = 0.5f;
            held.locked = true;
            GearboxFollowFloor(held, 3);
            GearboxModel same = held;
            GearboxFollowFloor(same, 3);
            GearboxFollowFloor(same, 2);
            Check(held.gear == 3 && held.relock && held.moving == 0.0f && same.gear == 3 && same.shifts == held.shifts,
                  "automatic: the game's gear lifted by the floor past the model's held gear takes the model's gear with it; a lower or the same one changes nothing");
        }
        // finding 16: without boost the engine has 55 percent of its torque (kBoostTorque 0.45), with all of it all
        {
            TruckRaw on = lt;
            on.flags = 3;
            PowertrainModel none = {}, full = {};
            PowertrainStep(none, on, tc, 0.6f, tk.max / tk.redline, 80.0f, 0.0f, kDt);
            PowertrainStep(full, on, tc, 0.6f, tk.max / tk.redline, 80.0f, 1.0f, kDt);
            Check(Near(none.boost, 0.55f, 0.001f) && Near(full.boost, 1.0f, 0.001f), "powertrain: an engine without boost has 0.55 of its torque, with full boost all of it");
        }
        // finding 8: a floored plateau (the game's pull at nothing, the wheels steady) in a numbered gear at 0.93 of 2a + 5
        // gives that gear's top, which TruckGearTop then goes by, and with it the limiter and the floored point up; not
        // with the pull still on, a light pedal, the low range or in H; a second plateau gives the mean
        {
            TruckRaw pt = lt;
            pt.flags = 3;
            pt.automatic = 0;
            pt.powerFactor = 1.0f;
            pt.gear = pt.gearEngaged = 3;
            pt.throttle = -1.0f;
            pt.actionB4 = 0.0f;
            const float formula = TruckGearFormulaTop(pt, 3);
            auto plateauAt = [&](TruckRaw t, float share, float seconds, int &gear, float &top) {
                GearTopLearn l = {};
                for (int i = 0; i < t.wheels; i++) t.spin[i] = share * formula;
                bool got = false;
                for (float at = 0.0f; at < seconds && !got; at += kDt) got = GearTopStep(l, t, kDt, gear, top);
                return got;
            };
            int gear = 0;
            float top = 0.0f;
            const bool seen = plateauAt(pt, 0.93f, 1.5f, gear, top) && gear == 3 && Near(top, 0.93f * formula, 0.01f);
            TruckRaw pulling = pt, light = pt, low = pt, high = pt;
            pulling.actionB4 = 0.2f;
            light.throttle = -0.5f;
            low.gear = low.gearEngaged = 1;
            low.powerFactor = 0.45f;
            high.gear = high.gearEngaged = TruckTopGear(high) + 1;
            int g2 = 0;
            float t2 = 0.0f;
            const bool refused = !plateauAt(pulling, 0.93f, 1.5f, g2, t2) && !plateauAt(light, 0.93f, 1.5f, g2, t2) && !plateauAt(low, 0.93f, 1.5f, g2, t2) &&
                                 !plateauAt(high, 0.95f, 1.5f, g2, t2) && !plateauAt(pt, 0.93f, 0.9f, g2, t2);
            TruckRaw learned = pt;
            learned.gearTop[3] = top;
            const float upBefore = GearboxUpPoint(pt, tk, 3, 1.0f, handOf(kInputPad)), upAfter = GearboxUpPoint(learned, tk, 3, 1.0f, handOf(kInputPad));
            const bool used = Near(TruckGearTop(learned, 3), top, 0.001f) && Near(TruckGearTop(learned, 2), TruckGearTop(pt, 2), 0.001f) && upAfter != upBefore &&
                              Near(GearTopMerge(top, 0.97f * formula), 0.95f * formula, 0.01f) && Near(GearTopMerge(0.0f, 20.0f), 20.0f, 0.001f);
            printf("     finding 8: a plateau in third at %.2f rad/s (2a + 5 gives %.2f): learned; the floored point up from %.0f to %.0f rpm\n", top, formula, upBefore, upAfter);
            Check(seen && refused && used,
                  "gearbox: a floored plateau gives a gear's top, which the limiter and the floored points go by; none with the pull on, a light pedal, the low range, "
                  "in H or under a second; a second plateau gives the mean");
        }
        // the probe texts carry what they say they do
        {
            GearboxModel pb = boxIn(3, rollAt(3, 1500.0f), 0.5f);
            pb.locked = true;
            pb.keep = 3;
            pb.keepFor = 5.0f;
            char text[1024];
            GearboxProbeText(lt, pb, true, text, sizeof text);
            const bool gearText = strstr(text, "gear 3 of 4 (the game's 3)") && strstr(text, "locked up") && strstr(text, "kept in gear 3") && strstr(text, "setting the game's gears");
            PowertrainModel pm = {};
            pm.running = true;
            pm.curve = 0.9f;
            char power[512];
            PowertrainProbeText(lt, pm, 1500.0f, true, 2.4f, 0.8f, power, sizeof power);
            const bool powerText = strstr(power, "the torque factor 2.40, the game's own 0.80") && strstr(power, "1500 rpm");
            PowertrainProbeText(lt, pm, 1500.0f, false, 2.4f, 0.8f, power, sizeof power);
            const bool offText = strstr(power, "not handed to the game") != nullptr;
            // review 11: the factor's own terms and what the rules decide by, after the fields the overseer's tools read
            // ("...the game's own Y): pedal P, shaped S | torque T of the most at R rpm", "gear N of M (the game's G)" and
            // "the wheels at X of the game's gear's top (its pull P)"), which stay as they were
            PowertrainProbeText(lt, pm, 1500.0f, true, 2.4f, 0.8f, power, sizeof power, 0.93f, 2.17f, true);
            const char *terms = strstr(power, "the factor's terms");
            const bool factorTerms = terms && strstr(terms, "0.93 of the gear's top") && strstr(terms, "the roof 2.17") && strstr(terms, "paced") &&
                                     strstr(power, "the model's own, by the shaped pedal") && strstr(power, "the game's own 0.80): pedal ") && strstr(power, ", shaped ") &&
                                     strstr(power, " | torque ") && strstr(power, " of the most at 1500 rpm") < terms;
            pb.feel = 0.6f;
            pb.startGear = 1;
            GearboxProbeText(lt, pb, true, text, sizeof text);
            const char *rules = strstr(text, " | rules: ");
            const bool ruleText = rules && strstr(rules, "feel 0.60") && strstr(rules, "start gear 1") && strstr(rules, "of the model's gear's top") && strstr(rules, "unlock at") &&
                                  strstr(text, "of the game's gear's top (its pull ") < rules;
            Check(gearText && powerText && offText && factorTerms && ruleText,
                  "probe: the automatic's and the powertrain's lines carry the gear, the lock, the keep, the factor and the engine speed, the factor's own terms and "
                  "what the rules decide by, after the fields the log tools read");
        }
    }

    printf(g_failed ? "MODELS TEST FAILED (%d)\n" : "MODELS TEST OK\n", g_failed);
    return g_failed ? 1 : 0;
}
