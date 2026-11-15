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
#include <map>
#include "gauges.h"
#include "truck.h"
#include "models.h"
#include "odometer.h"
#include "air_keep.h"
#include "text_cut.h"
#include "gearbox_model.h"
#include "sound.h"
#include "state.h"
#include "warn_list.h"
#include "tires.h"
#include "torque_cut_sim.h"
#include "fault_place.h"

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

// A made-up game memory for the wheels' drag reads: bytes put at addresses, a read of any byte not put fails.
static std::map<uint64_t, uint8_t> g_memBytes;
static bool MemPeek(uint64_t addr, void *out, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        const auto it = g_memBytes.find(addr + i);
        if (it == g_memBytes.end()) return false;
        ((uint8_t *)out)[i] = it->second;
    }
    return true;
}
template <typename T> static void MemPut(uint64_t addr, const T &v)
{
    for (size_t i = 0; i < sizeof v; i++) g_memBytes[addr + i] = ((const uint8_t *)&v)[i];
}
static const TruckMemory g_mem = { MemPeek, 0x140000000ull, nullptr };

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

// One step of the boost at the engine speed n (over the red zone's rpm) with the class's turbo under profile, its
// most (0: the class's) and fuel, the class's curve, warm; the engine free in neutral.
static int Blow(BoostModel &m, const TruckRaw &t, float n, float dt, int cls = kClassTruck, int profile = kSpoolStandard, float most = 0.0f, int fuel = kFuelClass)
{
    const GaugeScale &s = kClassScales[cls];
    const TurboChar k = TurboOf(cls, profile, most > 0.0f ? most : s.boost, fuel);
    return BoostStep(m, t, k, n, kClassCurves[cls], s.max / s.redline, 1.0f, t.gearEngaged == 0, dt);
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
    s.tachClass = s.scaleClass = kClassScout;
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
            const int lift = BoostStep(boost, t, TurboOf(s.scaleClass, s.boostProfile, k.boost, kFuelClass), TachSpeed(tach, k), kClassCurves[s.scaleClass],
                                       k.max / k.redline, 1.0f, t.gearEngaged == 0, dt);
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
            PowertrainStep(power, t, kClassCurves[s.scaleClass], TachSpeed(tach, k), k.max / k.redline, temp.oil, boost.bar / k.boost, dt);
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

// "spool": the turbo's four profiles as numbers to draw, on the truck class's scales and boost: the full-load boost
// at each engine speed (one line for each hundredth of the red zone's rpm), then the boost step by step as a truck
// floored in first gear at a standstill gathers speed to the gear's top in six seconds. Both as comma separated
// lines: what, x, then the four profiles.
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
        for (int p = 0; p < kSpoolCount; p++) printf(",%.4f", TurboFull(TurboOf(kClassTruck, p, k.boost, kFuelClass), i / 100.0f));
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
            Blow(boost[p], t, TachSpeed(tach[p], k), kDt, kClassTruck, p, k.boost);
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
    float pullLag = 0.0f; // seconds the game's pull (action + 0xB4) lags its target by, the engine's responsiveness; 0 at once
    float clutchGrip = 1.0f; // the clutch's grip the creep goes by in a gear picked by hand
    int boostProfile = kSpoolStandard, fuel = kFuelClass; // the turbo: the class's under this profile, and the fuel
    float holdMost = 1.0f; // the creep's holding part's most: 1, the engine handed to the game as the loop has it
    bool mayStall = true;  // the creep may stall the engine: the mod's own gearbox in a position picked by hand (else Gearbox 0 or 1)
    bool shifterConverter = false; // R or the low range on the game's shifter: the creep through the converter, calmed (CreepStep's calm)
    bool spinLift = false; // a held gear with the clutch in: the lift by the wheels' own speed, capped at kSpinLiftMost, as gauges.cpp has it since eeb2ffa
    bool oldSpin = false;  // 0.18.4's locked path, to show the sway (BRIEF_low_gear_sway.md): this pass's spin, the cap of 2, a rise paced after any cut
    float spinNoise = 0.0f; // the wheels' spin read with +- this many rad/s of noise a pass, as the game's frames give it (about 0.15)
    bool gameCut = false;   // the game's pull as THE VERDICT of BRIEF_low_gear_sway.md reads it: flat at the throttle, cut at the top and back only under cutBack of it
    float cutBack = 0.55f;
};
struct LoopState
{
    float v = 0.0f, applied = 1.0f, at = 0.0f; // m/s, the factor as the game applies it, seconds
    float queue[64] = {};                        // the factors written, waiting out the dead time
    int head = 0;
    TachModel tach = {};
    BoostModel boost = {};
    PowertrainModel power = {};
    GearboxModel box = {};
    TorqueHand hand = {};
    int gear = 1;        // the game's gear
    int shifts = 0;      // the game's gear changes
    bool paced = false;  // the last write was held back by the pace
    SpinMean spin = {};  // the held gear's wheel spin, its mean in rad/s (SpinMeanStep), as gauges.cpp keeps it
    float brake = 0.0f;  // the brake held, 0 to 1 (the pedal then 0)
    float share = 0.0f;  // the creep's share on the last pass
    CreepModel creep = {}; // the creep's pace and stall
    float pull = 0.0f;   // the game's pull (action + 0xB4)
    float stallAt = -1.0f; // seconds into the drive the creep stalled the engine, which is off from then on
    uint32_t seed = 12345u; // the spin noise's
    bool cut = false;    // the game's pull is cut (gameCut)
    int cuts = 0;        // the times it was cut
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
    t.flags = s.stallAt < 0.0f ? 3 : 0;
    t.automatic = 0;
    t.actionB4 = s.pull;
    t.gear = t.gearEngaged = c.fixedGear ? c.fixedGear : s.gear;
    t.throttle = s.brake > 0.0f ? s.brake : -pedal;
    t.speed = s.v;
    float noise = 0.0f;
    if (c.spinNoise > 0.0f)
    {
        s.seed = s.seed * 1664525u + 1013904223u;
        noise = ((float)(s.seed >> 8) / 16777216.0f * 2.0f - 1.0f) * c.spinNoise;
    }
    for (int i = 0; i < t.wheels; i++) t.spin[i] = c.grip * s.v + noise;
    t.torqueFactor = s.hand.on ? s.hand.written : 1.0f;
    const bool driving = !c.fixedGear;
    if (driving) TachFollow(s.tach, t, c.k, s.box.rpm, dt);
    else TachStep(s.tach, t, c.k, dt);
    BoostStep(s.boost, t, TurboOf(c.cls, c.boostProfile, c.k.boost, c.fuel), s.tach.rpm / c.k.redline, curve, c.k.max / c.k.redline, s.power.running ? s.power.cold : 1.0f,
              t.gearEngaged == 0, dt);
    PowertrainStep(s.power, t, curve, TachSpeed(s.tach, c.k), c.k.max / c.k.redline, 80.0f, s.boost.limit, dt);
    const EngineHand hand{ true, &curve, c.k.max / c.k.redline, 1.0f, 0.0f, 0.0f, EngineLightFloor(curve, c.k.redline), kInputPad, TorqueRoof(c.cls) };
    GearboxStep(s.box, t, c.k, pedal, s.power.power, dt, hand);
    // the factor, as gauges.cpp hands it
    const bool spinning = c.fixedGear && c.spinLift;
    const float liftTop = TruckGearTop(t, t.gearEngaged);
    if (!spinning) s.spin.n = 0, s.spin.at = 0;
    SpinLift byspin = { 0.0f, kTorqueFactorMost, false };
    if (spinning && c.oldSpin) byspin = SpinLift{ SpinShare(TruckDrivenSpin(t), liftTop), kSpinLiftMost, false };
    else if (spinning) byspin = SpinLiftStep(s.spin, TruckDrivenSpin(t), liftTop, t.gearEngaged, 0x5000);
    const float x = spinning ? byspin.x : LiftShare(TruckDrivenSpin(t), s.v, s.box.grip, liftTop);
    const float ratio = driving ? s.box.ratio : 1.0f;
    const float limit = s.power.cold * s.power.boost;
    const float lift = before ? LiftBefore(s.power.curve, limit, TruckPedal(t), ratio, x, TorqueRoof(c.cls))
                              : spinning ? fminf(TorqueLift(s.power.curve, limit, TruckPedal(t), ratio, x, TorqueRoof(c.cls)), byspin.cap)
                                         : TorqueLift(s.power.curve, limit, TruckPedal(t), ratio, x, TorqueRoof(c.cls));
    const float wanted = s.hand.game * lift;
    TorqueHandStep(s.hand, true, 0x5000, 0x4000, t.torqueFactor, lift, before ? 0.0f : dt, t.gearEngaged, spinning && !c.oldSpin, byspin.loose);
    s.paced = s.hand.written < wanted - kTorqueFactorStep;
    // the game: the factor after the dead time and the lag, the push on the speed
    const int delay = (int)(c.dead / dt + 0.5f) < 63 ? (int)(c.dead / dt + 0.5f) : 63;
    s.queue[s.head] = s.hand.written;
    const float arrived = s.queue[(s.head + 64 - delay) % 64];
    s.head = (s.head + 1) % 64;
    s.applied += (arrived - s.applied) * (1.0f - expf(-dt / c.lag));
    TruckRaw plain = t;
    plain.powerFactor = 1.0f;
    const float top = TruckGearTop(t, t.gearEngaged), first = TruckGearTop(plain, 1); // the low range pushes harder by its top
    const float share = top > 0.0f ? c.grip * s.v / (top * c.realTop) : 0.0f;
    // the hook: the game's throttle raised to the creep's share (the mod hands it on this pass, the game takes it on its steps)
    bool stalls = false;
    s.share = c.creep ? CreepStep(s.creep, t, c.k, driving || c.shifterConverter, c.clutchGrip, dt, nullptr, stalls, c.holdMost, c.mayStall, c.shifterConverter) : 0.0f;
    if (stalls) s.stallAt = s.at;
    const float throttle = s.brake > 0.0f || !TruckEngineOn(t) ? 0.0f : fmaxf(pedal, s.share);
    if (c.gameCut)
    {
        if (share >= 1.0f && !s.cut) s.cut = true, s.cuts++;
        else if (share < c.cutBack) s.cut = false;
    }
    const float target = c.gameCut ? (s.cut ? 0.0f : throttle) : throttle * (c.lowPull + (1.0f - c.lowPull) * GamePull(share));
    s.pull = c.pullLag > 0.0f ? s.pull + (target - s.pull) * (1.0f - expf(-dt / c.pullLag)) : target;
    const float pushNow = top > 0.0f ? c.push * s.pull * s.applied * first / top : 0.0f;
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

// The rpm sway in a held low gear (BRIEF_low_gear_sway.md): a heavy truck held in gear by hand at full pedal, the lift
// by spin, a drag that rises with speed; after warm seconds, the written factor's and the wheels' spin's least and most
// over seconds, and the cycles (a fall of the factor over 0.2 after a rise), as 0.18.4 had it (oldSpin) or now.
struct SwayStats { float factorLo, factorHi, spinLo, spinHi, top; int cycles; float lastSpin, step; };
static LoopTruck LoopHeavy();
static SwayStats SwayRun(bool oldSpin, int gear, float powerFactor, float gearA, float drag0, float drag2, float warm, float seconds, float noise = 0.15f)
{
    LoopTruck c = LoopHeavy();
    c.spinNoise = noise;
    c.fixedGear = gear;
    c.spinLift = true;
    c.oldSpin = oldSpin;
    c.t.powerFactor = powerFactor;
    if (gearA > 0.0f) c.t.gearSpeed[gear] = gearA;
    c.drag0 = drag0;
    c.drag2 = drag2;
    const TorqueCurve curve = kClassCurves[kClassHeavy];
    SwayStats r = { 1e9f, 0.0f, 1e9f, 0.0f, 0.0f, 0, 0.0f, 0.0f };
    TruckRaw at = c.t;
    at.gear = at.gearEngaged = gear;
    r.top = TruckGearTop(at, gear);
    LoopState s;
    s.v = 0.6f * r.top / c.grip; // rolling at 0.6 of the top: the game's pull begins past a standstill
    LoopDrive(c, s, curve, 1.0f, warm, false, false);
    float last = s.hand.written;
    bool rising = false;
    for (float t = 0.0f; t < seconds; t += kDt)
    {
        LoopStep(c, s, curve, 1.0f, kDt, false);
        const float f = s.hand.written, spin = c.grip * s.v;
        r.factorLo = fminf(r.factorLo, f), r.factorHi = fmaxf(r.factorHi, f);
        r.spinLo = fminf(r.spinLo, spin), r.spinHi = fmaxf(r.spinHi, spin);
        r.step = fmaxf(r.step, fabsf(f - last));
        if (f > last + 1e-4f) rising = true;
        else if (f < last - 0.2f && rising) r.cycles++, rising = false;
        last = f;
        r.lastSpin = spin;
    }
    return r;
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
    // the low range's wider wheel cap (BRIEF_low_range_pull.md, M1)
    {
        TruckRaw lr = Truck();
        lr.powerFactor = 0.2f;
        const float own = 1.5f * TruckGearTop(lr, 1), mid = 1.5f * TruckGearTopAt(lr, 1, 0.45f), most = 1.5f * TruckGearTopAt(lr, 1, 1.0f);
        const bool tops = Near(TruckGearTopAt(lr, 1, 0.2f), 3.0f, 0.001f) && Near(TruckGearTopAt(lr, 1, 0.45f), 5.5f, 0.001f) && Near(TruckGearTopAt(lr, 1, 1.0f), 11.0f, 0.001f) &&
                          TruckGearTopAt(lr, 1, lr.powerFactor) == TruckGearTop(lr, 1) && TruckGearTopAt(lr, 2, 0.2f) == TruckGearTop(lr, 2);
        const bool power = GearboxLowMostPower(0) == 1.0f && GearboxLowMostPower(kHasLowPlus) == 0.45f && GearboxLowMostPower(kHasLowPlus | kHasL) == 0.2f &&
                           GearboxLowMostPower(kHasLowPlus | kHasL | kHasLowMinus) == 0.0f && GearboxLowMostPower(kHasLowMinus | kHasL) == 1.0f && GearboxLowMostPower(kHasH) == 1.0f;
        const bool none = LowRangeCap(kPosLowMinus, own, most, 0) == own && LowRangeCap(kPosLow, mid, most, 0) == mid && LowRangeCap(kPosLowMinus, own, most, -5) == own;
        const bool whole = LowRangeCap(kPosLowMinus, own, most, 100) == most && LowRangeCap(kPosLow, mid, most, 100) == most && LowRangeCap(kPosLowMinus, own, most, 150) == most;
        const bool half = Near(LowRangeCap(kPosLowMinus, own, most, 50), 0.5f * (own + most), 1e-5f) && Near(LowRangeCap(kPosLow, mid, most, 50), 0.5f * (mid + most), 1e-5f) &&
                          Near(LowRangeCap(kPosLowMinus, own, most, 25), own + 0.25f * (most - own), 1e-5f);
        const bool others = LowRangeCap(kPosLowPlus, most, 2.0f * most, 100) == most && LowRangeCap(kPosR, own, most, 100) == most && LowRangeCap(kPosGear, own, most, 100) == own &&
                            LowRangeCap(kPosH, own, most, 100) == own && LowRangeCap(kPosA, own, most, 100) == own && LowRangeCap(kPosN, own, most, 100) == own;
        // one low gear (the most is its own), a top that is no number or none: the gear's own cap
        const bool lone = LowRangeCap(kPosLow, mid, mid, 100) == mid && LowRangeCap(kPosLowMinus, own, NAN, 100) == own && LowRangeCap(kPosLowMinus, own, INFINITY, 100) == own &&
                          LowRangeCap(kPosLowMinus, own, 0.0f, 100) == own && LowRangeCap(kPosLowMinus, own, 0.5f * own, 100) == own && LowRangeCap(kPosLowMinus, 0.0f, most, 100) == 0.0f &&
                          LowRangeCap(kPosLowMinus, NAN, most, 100) == 0.0f;
        // the ceiling with the wider cap: the governor's end of the gear's own top once the truck is there, and with no truck speed
        const float top = TruckGearTop(lr, 1), to = 1.1f, end = to * top;
        const bool ceiling = CapCeiling(most, top, to, 0.0f) == most && CapCeiling(most, top, to, kCapCeilFrom * top) == most && Near(CapCeiling(most, top, to, end), end, 1e-5f) &&
                             Near(CapCeiling(most, top, to, 3.0f * end), end, 1e-5f) && Near(CapCeiling(most, top, to, NAN), end, 1e-5f) &&
                             CapCeiling(most, top, to, 0.9f * top) < most && CapCeiling(most, top, to, 0.9f * top) > end;
        // the ceiling with the wider cap: whole under kLowWideFrom of the top, the gear's own law to the last bit from kCapCeilFrom on
        bool wideOk = CapCeilingWide(own, most, top, to, 0.0f) == most && CapCeilingWide(own, most, top, to, kLowWideFrom * top) == most &&
                      Near(CapCeilingWide(own, most, top, to, 0.5f * (kLowWideFrom + kCapCeilFrom) * top), 0.5f * (own + most), 1e-4f) &&
                      CapCeilingWide(own, most, top, to, -1.0f) == most && CapCeilingWide(own, own, top, to, 0.0f) == own && CapCeilingWide(own, 0.0f, top, to, 0.0f) == own &&
                      CapCeilingWide(own, NAN, top, to, 0.0f) == own && CapCeilingWide(0.0f, most, top, to, 0.0f) == 0.0f && CapCeilingWide(own, most, 0.0f, to, 1.0f) == own &&
                      CapCeilingWide(own, most, top, to, NAN) == CapCeiling(own, top, to, NAN) && CapCeilingWide(own, most, top, to, 0.4f * top, kCapCeilFrom) == most;
        for (int k = 0; k <= 60; k++)
        {
            const float v = top * (float)k / 40.0f, c = CapCeilingWide(own, most, top, to, v);
            if (v >= kCapCeilFrom * top && c != CapCeiling(own, top, to, v)) wideOk = false;
            if (k && c > CapCeilingWide(own, most, top, to, top * (float)(k - 1) / 40.0f) + 1e-6f) wideOk = false;
        }
        Check(wideOk, "low range slip, the ceiling: the wider cap whole while the truck is under a quarter of the gear's top, half way between it and the gear's own at half the top, "
                      "the gear's own law to the last bit from three quarters on, never rising with the truck's speed; the gear's own with no wider cap or no truck speed");
        Check(tops && power && none && whole && half && others && lone && ceiling,
              "low range slip: the top of a low gear at a power factor given; the highest low gear a gearbox has; at 0 the gear's own cap to the last bit, at 100 the cap of the "
              "highest low gear in L- and L, at 50 half way; L+, R and every other position their own; one low gear or a top that is no number the gear's own; the ceiling "
              "with the wider cap ends at the governor's end of the gear's own top, and there with no truck speed");
        if (!(tops && power && none && whole && half && others && lone && ceiling)) printf("     %d %d %d %d %d %d %d %d\n", tops, power, none, whole, half, others, lone, ceiling);
        // the truck's speed in the wheels' measure where the cut gave none (the by-wheel line read "nan")
        const float radii[4] = { 0.5f, 0.5f, 0.0f, NAN }, bad[2] = { 0.0f, NAN };
        const bool rOk[4] = { true, true, true, true }, firstOnly[4] = { true, false, false, false }, noneOk[4] = {};
        const float mixed[4] = { 0.4f, 0.6f, 0.6f, 0.6f };
        const bool speed = Near(TruckSpeedInWheels(3.0f, radii, rOk, 4, 1.0f), 6.0f, 1e-5f) && Near(TruckSpeedInWheels(3.0f, radii, rOk, 4, -1.0f), -6.0f, 1e-5f) &&
                           Near(TruckSpeedInWheels(-1.5f, radii, rOk, 4, 1.0f), -3.0f, 1e-5f) && Near(TruckSpeedInWheels(3.0f, mixed, firstOnly, 4, 1.0f), 7.5f, 1e-5f) &&
                           Near(TruckSpeedInWheels(3.0f, mixed, rOk, 4, 1.0f), 3.0f * 0.25f * (1.0f / 0.4f + 3.0f / 0.6f), 1e-4f) && TruckSpeedInWheels(0.0f, radii, rOk, 4, 1.0f) == 0.0f &&
                           isnan(TruckSpeedInWheels(NAN, radii, rOk, 4, 1.0f)) && isnan(TruckSpeedInWheels(3.0f, radii, noneOk, 4, 1.0f)) && isnan(TruckSpeedInWheels(3.0f, bad, rOk, 2, 1.0f)) &&
                           isnan(TruckSpeedInWheels(3.0f, radii, rOk, 0, 1.0f)) && isnan(TruckSpeedInWheels(INFINITY, radii, rOk, 4, 1.0f));
        Check(speed, "truck speed in the wheels' measure: the forward speed over the mean rolling radius of the wheels read whole, in the gear's direction, a radius of 0 or no number "
                     "left out; no number with no forward speed, no wheel read or no radius usable");
    }

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

    // the turbo: review 12's model as its addendum corrected it, at a held engine speed against the addendum's figures
    // (its simulation, turbo_model_sim.py)
    {
        TruckRaw f = Truck();
        f.gear = f.gearEngaged = 1;
        f.automatic = 1;
        // the boost held at n with the pedal at p for half a minute
        auto settled = [&](int cls, float n, float p, int fuel = kFuelClass) {
            BoostModel b = {};
            f.throttle = -p;
            for (float at = 0.0f; at < 30.0f; at += kDt) Blow(b, f, n, kDt, cls, kSpoolStandard, 0.0f, fuel);
            return b;
        };
        // the characters: the class's, under the profiles
        const TurboChar truck = TurboOf(kClassTruck, kSpoolStandard, 1.8f, kFuelClass), scout = TurboOf(kClassScout, kSpoolStandard, 1.0f, kFuelClass),
                        heavy = TurboOf(kClassHeavy, kSpoolStandard, 2.0f, kFuelClass), quick = TurboOf(kClassTruck, kSpoolQuick, 1.8f, kFuelClass),
                        laggy = TurboOf(kClassTruck, kSpoolLaggy, 1.8f, kFuelClass), two = TurboOf(kClassTruck, kSpoolTwoStage, 1.8f, kFuelClass),
                        petrolTruck = TurboOf(kClassTruck, kSpoolStandard, 1.8f, kFuelPetrol), dieselScout = TurboOf(kClassScout, kSpoolStandard, 1.0f, kFuelDiesel),
                        wild = TurboOf(kClassTruck, 7, 1.8f, kFuelClass);
        const bool classes = !truck.petrol && scout.petrol && !heavy.petrol && truck.n0 == 0.36f && truck.n1 == 0.6f && scout.n1 == 0.3f && heavy.n1 == 0.68f &&
                             truck.tauRed == 0.045f && heavy.tauRed == 0.085f && scout.tauRed == 0.01f && truck.r == 1.2f && scout.r == 1.0f && petrolTruck.petrol &&
                             !dieselScout.petrol && wild.profile == kSpoolStandard;
        const bool profiles = Near(quick.n1, 0.48f, 1e-5f) && Near(quick.tauRed, 0.0225f, 1e-5f) && Near(laggy.n0, 0.504f, 1e-5f) && Near(laggy.n1, 0.84f, 1e-5f) &&
                              Near(laggy.tauRed, 0.0675f, 1e-5f) && Near(two.n1, 0.45f, 1e-5f) && Near(two.most, 2.34f, 1e-5f) && two.twoStage;
        GaugeSettings g;
        SettingsDefaults(g);
        g.boostProfile = 7;
        SettingsClamp(g);
        Check(classes && profiles && g.boostProfile == kSpoolStandard,
              "turbo: trucks and heavy trucks a diesel, scouts petrol unless the engine says otherwise, each class's points and spool; the small one full sooner "
              "and quicker, the big one later and slower, two stages sooner and higher; a profile the mod does not have is the standard one");
        // the dial, settled (A4): idle, a light foot of 0.3 at 0.6 of the red zone's rpm, floored at 0.45 and at 0.8
        const float tIdle = settled(kClassTruck, 650.0f / 2200.0f, 0.0f).bar, tLight = settled(kClassTruck, 0.6f, 0.3f).bar, tLow = settled(kClassTruck, 0.45f, 1.0f).bar,
                    tHigh = settled(kClassTruck, 0.8f, 1.0f).bar;
        const float sIdle = settled(kClassScout, 800.0f / 5500.0f, 0.0f).bar, sLight = settled(kClassScout, 0.6f, 0.3f).bar, sLow = settled(kClassScout, 0.45f, 1.0f).bar,
                    sHigh = settled(kClassScout, 0.8f, 1.0f).bar;
        printf("     turbo, the dial settled: a truck %.2f at idle, %.2f at a light foot, %.2f and %.2f floored low and high; a petrol scout %.2f, %.2f, %.2f and %.2f bar\n",
               tIdle, tLight, tLow, tHigh, sIdle, sLight, sLow, sHigh);
        Check(Near(tIdle, 0.0f, 0.05f) && Near(tLight, 0.26f, 0.05f) && Near(tLow, 0.67f, 0.05f) && Near(tHigh, 1.8f, 0.05f) && Near(sIdle, -0.7f, 0.05f) &&
                  Near(sLight, -0.19f, 0.05f) && Near(sLow, 1.0f, 0.05f) && Near(sHigh, 1.0f, 0.05f),
              "turbo: the dial reads a truck diesel's 0 at idle, 0.26 bar at a light foot, 0.67 floored at 0.45 of the red zone's rpm and 1.8 at 0.8; a petrol "
              "scout's vacuum of 0.7 bar at idle and 0.19 at a light foot, 1 bar floored");
        // settled and floored the limit is 1 at every speed (finding 1); at part load it stays over the pedal
        bool whole = true;
        for (int c = 0; c < kClassCount; c++)
            for (float n = 0.3f; n <= 1.001f; n += 0.1f) whole = whole && settled(c, n, 1.0f).limit >= 0.999f;
        const float tPart = settled(kClassTruck, 0.8f, 0.6f).limit, sPart = settled(kClassScout, 0.8f, 0.6f).limit;
        printf("     turbo: at 0.6 of the pedal and 0.8 of the red zone's rpm the limit %.2f on a truck, %.2f on a scout\n", tPart, sPart);
        Check(whole && Near(tPart, 0.77f, 0.02f) && Near(sPart, 0.66f, 0.02f),
              "turbo: settled and floored the air allows all of the curve at every speed; at part load more than the pedal asks");
        // the spool (A3): 90 percent of the torque the air allows after a press from a light foot (0.2) to the floor
        auto spool = [&](int cls, float n) {
            BoostModel b = {};
            f.throttle = -0.2f;
            for (float at = 0.0f; at < 10.0f; at += kDt) Blow(b, f, n, kDt, cls);
            const float from = b.limit;
            BoostModel end = b;
            f.throttle = -1.0f;
            for (float at = 0.0f; at < 20.0f; at += kDt) Blow(end, f, n, kDt, cls);
            f.throttle = -1.0f;
            for (float at = kDt; at < 20.0f; at += kDt)
                if (Blow(b, f, n, kDt, cls), b.limit >= from + 0.9f * (end.limit - from) - 1e-6f) return at;
            return -1.0f;
        };
        const float tSlow = spool(kClassTruck, 1100.0f / 2200.0f), tQuick = spool(kClassTruck, 0.8f), hSlow = spool(kClassHeavy, 1000.0f / 1900.0f),
                    hQuick = spool(kClassHeavy, 0.8f), sSlow = spool(kClassScout, 2000.0f / 5500.0f), sQuick = spool(kClassScout, 0.8f);
        printf("     turbo, 90 percent of the torque from a light foot floored: a truck %.1f s at 1100 rpm and %.1f at 1760, a heavy truck %.1f at 1000 and %.1f at 1520, "
               "a scout %.1f at 2000 and %.1f at 4400\n", tSlow, tQuick, hSlow, hQuick, sSlow, sQuick);
        Check(Near(tSlow, 2.0f, 0.2f) && Near(tQuick, 0.9f, 0.15f) && Near(hSlow, 2.9f, 0.25f) && Near(hQuick, 1.5f, 0.2f) && Near(sSlow, 1.4f, 0.2f) && Near(sQuick, 0.3f, 0.1f),
              "turbo: the spool is slow low down and quick high up: a truck about 2 s at 1100 rpm and under 1 s at 1760, a heavy truck about 3 s at 1000, a scout "
              "about 1.4 s at 2000 and 0.3 s at 4400");
        // a lift at 0.8 of the red zone's rpm, then the pedal back (A4, B)
        auto lift = [&](int cls, float off, float &atPress, float &after, float &limitAfter, float &half, float &one, float &two) {
            BoostModel b = {};
            f.throttle = -1.0f;
            for (float at = 0.0f; at < 20.0f; at += kDt) Blow(b, f, 0.8f, kDt, cls);
            f.throttle = 0.0f;
            for (float at = 0.0f; at < off - 0.001f; at += kDt) Blow(b, f, 0.8f, kDt, cls);
            atPress = b.bar;
            f.throttle = -1.0f;
            for (float at = kDt; at < 2.001f; at += kDt)
            {
                Blow(b, f, 0.8f, kDt, cls);
                if (Near(at, 0.1f, 0.001f)) { after = b.bar; limitAfter = b.limit; }
                if (Near(at, 0.3f, 0.001f) && cls == kClassScout) half = b.bar;
                if (Near(at, 0.5f, 0.001f) && cls != kClassScout) half = b.bar;
                if (Near(at, 1.0f, 0.001f)) one = b.bar;
                two = b.bar;
            }
        };
        float shortPress, shortAfter, shortLimit, shortHalf, shortOne, shortTwo, longPress, longAfter, longLimit, longHalf, longOne, longTwo;
        float scoutPress, scoutAfter, scoutLimit, scoutThird, scoutOne, scoutTwo;
        lift(kClassTruck, 0.5f, shortPress, shortAfter, shortLimit, shortHalf, shortOne, shortTwo);
        lift(kClassTruck, 3.0f, longPress, longAfter, longLimit, longHalf, longOne, longTwo);
        lift(kClassScout, 0.5f, scoutPress, scoutAfter, scoutLimit, scoutThird, scoutOne, scoutTwo);
        printf("     turbo, lifts at 0.8: a truck's half second %.2f bar at the press, %.2f 0.1 s after (the limit %.2f), %.2f at 1 s; three seconds %.2f, %.2f, then %.2f, "
               "%.2f and %.2f at 0.5, 1 and 2 s; a scout's half second %.2f, %.2f 0.1 s after, %.2f at 0.3 s\n",
               shortPress, shortAfter, shortLimit, shortOne, longPress, longAfter, longHalf, longOne, longTwo, scoutPress, scoutAfter, scoutThird);
        Check(shortAfter >= 0.55f * 1.8f && shortLimit >= 0.85f && Near(shortOne, 1.8f, 0.05f) && longPress < 0.1f * 1.8f && longTwo >= 0.9f * 1.8f &&
                  Near(longAfter, 0.07f, 0.05f) && Near(longHalf, 0.5f, 0.1f) && Near(longOne, 1.28f, 0.1f) && Near(scoutPress, -0.69f, 0.05f) &&
                  Near(scoutAfter, 0.22f, 0.07f) && Near(scoutThird, 0.89f, 0.07f),
              "turbo: the rotor keeps turning through a short lift, so the boost is back at once and whole within a second; after three seconds it spools "
              "again, nine tenths within 2 s; a petrol's manifold goes to vacuum the moment the throttle shuts");
        // a floored launch from idle through the converter, the engine flaring to its stall in 0.7 s (A4)
        auto launch = [&](int cls, float &dip, float &whole1) {
            BoostModel b = {};
            const GaugeScale &k = kClassScales[cls];
            const float ni = k.idle / k.redline;
            f.throttle = -1.0f;
            dip = 2.0f;
            whole1 = -1.0f;
            float dipAt = 0.0f;
            for (float at = kDt; at < 6.0f; at += kDt)
            {
                Blow(b, f, ni + (0.8f - ni) * fminf(at / 0.7f, 1.0f), kDt, cls);
                if (b.limit < dip) { dip = b.limit; dipAt = at; }
                if (whole1 < 0.0f && at > dipAt && dip < 0.9f && b.limit >= 0.999f) whole1 = at;
            }
        };
        float tDip, tWhole, hDip, hWhole;
        launch(kClassTruck, tDip, tWhole);
        launch(kClassHeavy, hDip, hWhole);
        printf("     turbo, a floored launch from idle: a truck's limit dips to %.2f and is whole at %.1f s, a heavy truck's %.2f and %.1f s\n", tDip, tWhole, hDip, hWhole);
        Check(Near(tDip, 0.43f, 0.04f) && tWhole > 1.0f && tWhole < 2.2f && Near(hDip, 0.41f, 0.04f) && hWhole > 1.5f && hWhole < 3.0f,
              "turbo: floored from idle the air allows all at first, a dip to about 0.43 as the engine passes the full-load point with no boost yet, and all "
              "again within about 1.6 s (a heavy truck 2.4 s)");
        // the fuel: in neutral a free engine's, past the curve's last point the governor's taper; nothing with the engine off
        TruckRaw free = f;
        free.gear = free.gearEngaged = 0;
        free.throttle = -1.0f;
        BoostModel nb = {};
        for (float at = 0.0f; at < 30.0f; at += kDt) Blow(nb, free, 0.8f, kDt);
        const float topN = kClassScales[kClassTruck].max / kClassScales[kClassTruck].redline;
        const BoostModel governed = settled(kClassTruck, (1.0f + topN) * 0.5f, 1.0f), redZone = settled(kClassTruck, 1.0f, 1.0f);
        TruckRaw off = f;
        off.flags = 0;
        BoostModel ob = settled(kClassTruck, 0.8f, 1.0f);
        for (float at = 0.0f; at < 10.0f; at += kDt) Blow(ob, off, 0.8f, kDt);
        printf("     turbo: floored in neutral %.2f bar; on the governor half way to the top the fuel %.2f and %.2f bar (%.2f at the red zone)\n", nb.bar, governed.q, governed.bar,
               redZone.bar);
        Check(Near(nb.bar, 0.2f, 0.06f) && Near(governed.q, 0.5f, 0.02f) && governed.bar < 0.8f * redZone.bar && governed.limit >= 0.999f && Near(redZone.bar, 1.8f, 0.05f) &&
                  fabsf(ob.bar) < 0.01f && ob.limit == 1.0f,
              "turbo: floored in neutral the free engine's fuel holds about 0.2 bar; on the governor the fuel and the boost taper and the air still allows "
              "all the curve gives there; with the engine off nothing");
        // two stages: a dip of a tenth for 0.3 s at the changeover
        {
            BoostModel b = {};
            f.throttle = -1.0f;
            for (float at = 0.0f; at < 10.0f; at += kDt) Blow(b, f, 0.55f, kDt, kClassTruck, kSpoolTwoStage);
            const float before = b.bar;
            float least = before;
            for (float at = 0.0f; at < 0.3f; at += kDt) { Blow(b, f, 0.65f, kDt, kClassTruck, kSpoolTwoStage); least = fminf(least, b.bar); }
            for (float at = 0.0f; at < 2.0f; at += kDt) Blow(b, f, 0.65f, kDt, kClassTruck, kSpoolTwoStage);
            Check(Near(before, 2.34f, 0.05f) && least < 0.95f * before && least > 0.85f * before && Near(b.bar, before, 0.02f),
                  "turbo: two stages make 1.3 times the boost, with a short dip of about a tenth at the changeover");
        }
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
        // the cold and the air: floored at the plateau with cold oil at a first instant's limit (0.43, review 12), and
        // with the engine off
        PowertrainModel cold = {};
        f.throttle = -1.0f;
        for (float at = 0.0f; at < 1.0f; at += kDt) PowertrainStep(cold, f, truckCurve, 0.7f, top, 0.0f, 0.43f, kDt);
        const bool weak = Near(cold.cold, 1.0f - kColdTorque, 0.001f) && Near(cold.boost, 0.43f, 0.001f) && Near(cold.power, (1.0f - kColdTorque) * 0.43f, 0.001f);
        TruckRaw stopped = f;
        stopped.flags = 0;
        PowertrainStep(cold, stopped, truckCurve, 0.7f, top, 90.0f, 1.0f, kDt);
        Check(weak && cold.power == 0.0f && cold.load == 0.0f,
              "powertrain: cold oil takes 15 percent of the torque and the air takes what its limit leaves; a stopped engine delivers nothing");
        // the author's drive, 28 October: floored at mid revs a truck has all but a little of its torque once the
        // boost has built, as the game's own engine has all of it. Floored and steady with the needle at needle of the
        // way from idle to the red zone, with the class's scale, curve and turbo: what the engine delivers
        auto steady = [&](int tachClass, float needle) {
            GaugeSettings cs;
            SettingsDefaults(cs);
            cs.tachClass = cs.scaleClass = tachClass;
            const GaugeScale k = GaugeScaleOf(cs);
            const float rpm = k.idle + needle * (k.redline - k.idle);
            BoostModel bm = {};
            PowertrainModel pw = {};
            for (float at = 0.0f; at < 6.0f; at += kDt)
            {
                Blow(bm, f, rpm / k.redline, kDt, tachClass);
                PowertrainStep(pw, f, kClassCurves[tachClass], rpm / k.redline, k.max / k.redline, 90.0f, bm.limit, kDt);
            }
            printf("     floored at %.0f rpm (%.2f of the way to the red zone): boost x%.2f, curve %.2f, %.2f of the most\n", rpm, needle, pw.boost, pw.curve, pw.power);
            return pw.power;
        };
        Check(steady(kClassScout, 0.6f) >= 0.9f && steady(kClassTruck, 0.5f) >= 0.9f,
              "powertrain: floored and steady a scout at 0.6 of the way to its red zone and a truck's diesel at half way hand at least 0.9 of their most once the boost has built");
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
        const bool stall = box.gear == 1 && !box.locked && Near(box.rpm, ConverterStall(k), 1.0f) && Near(box.ratio, kStallRatio, 0.001f);
        GearboxStep(box, g, k, 0.0f, 0.0f, kDt);
        // the model's own "to the wheels" figure and its creep are gone (BRIEF_drivetrain_3_a_on_our_chain.md, M1): the creep is CreepStep's alone
        const bool creep = Near(box.rpm, k.idle, 1.0f) && box.drive == 0.0f;
        Check(stall && creep, "automatic: standing, a floored engine runs at the converter's stall with the torque doubled, and an idling one runs at idle; the model gives no pull figure of its own");
        // part B: the model hands the stall's speed at this pedal and its rise, and the detour's law gives the model's own engine speed and ratio from them
        {
            GearboxModel hands = {};
            wheels(0.0f);
            GearboxStep(hands, g, k, 1.0f, 1.0f, kDt);
            TorqueCutMap km = {};
            km.idle = k.idle, km.redline = k.redline, km.top = 10.0f, km.tachTop = 1.3f;
            const ConverterNow stood = ConverterAt(km, 0.0f, hands.stallNow, hands.riseNow);
            const bool stoodFloored = Near(hands.stallNow, ConverterStall(k), 0.5f) && Near(hands.riseNow, kStallRise, 1e-6f) && Near(stood.engine, hands.rpm, 0.5f) &&
                                 Near(stood.ratio, hands.ratio, 1e-4f);
            GearboxStep(hands, g, k, 0.0f, 0.0f, kDt);
            const ConverterNow idling = ConverterAt(km, 0.0f, hands.stallNow, hands.riseNow);
            Check(stoodFloored && Near(hands.stallNow, k.idle, 0.5f) && hands.riseNow == 0.0f && Near(idling.engine, hands.rpm, 0.5f),
                  "torque cut in A, part B: the model hands the stall's speed at this pedal and its rise; from them the detour's law gives the model's own engine speed and torque "
                  "ratio at a standstill, floored and with the foot off");
        }
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
        // a spin that is no number on the first pass (BRIEF_full_pass_2_gameplay.md part 3): the good passes after it
        // give a model whose spin, speed and trend are numbers, and that shifts up
        {
            GearboxModel nan = {};
            wheels(NAN);
            GearboxStep(nan, g, k, 1.0f, 1.0f, kDt);
            GearboxModel poisoned = nan;
            poisoned.trend = NAN; // as one that came in through by
            float spin = 0.0f;
            int topReached = 0;
            for (float at = 0.0f; at < 20.0f; at += kDt)
            {
                wheels(spin);
                GearboxStep(poisoned, g, k, 1.0f, 1.0f, kDt);
                topReached = topReached > poisoned.gear ? topReached : poisoned.gear;
                spin = fminf(spin + 0.5f * TruckGearTop(g, 1) * kDt, TruckGearTop(g, TruckTopGear(g)));
            }
            Check(isfinite(poisoned.spin) && isfinite(poisoned.by) && isfinite(poisoned.trend) && topReached > 1 && isfinite(nan.spin),
                  "automatic: a wheel spin that is no number on the first pass, or a trend that is none, heals with the good passes after it, and the box shifts up");
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
            // the foot off with free wheels (BRIEF_tester_1114.md, part 190): floored to the tachometer's top at 85 rad/s
            // in the first gear, then the foot off with the wheels still at 85: the needle comes down under the red zone in
            // under two seconds while the input stays at the top; gripping wheels at the gear's top keep it with them
            {
                const float top1 = TruckGearTop(g, 1);
                GearboxModel free = spinning;
                TruckRaw air = mud;
                for (int i = 0; i < air.wheels; i++) air.spin[i] = 85.0f;
                air.speed = 2.5f;
                for (float at = 0.0f; at < 1.0f; at += kDt) GearboxStep(free, air, k, 1.0f, 1.0f, kDt);
                const float atTop = free.rpm;
                const bool upThere = free.locked && free.spinning && free.gear == 1 && atTop >= k.redline;
                float under = -1.0f;
                for (float at = 0.0f; at < 3.0f; at += kDt)
                {
                    GearboxStep(free, air, k, 0.0f, 0.0f, kDt);
                    if (under < 0.0f && free.rpm < k.redline) under = at;
                }
                char text[1400];
                GearboxProbeText(air, free, true, text, sizeof text);
                const bool freeOk = upThere && under >= 0.0f && under < 2.0f && Near(free.rpm, k.idle, 1.0f) && free.input >= k.redline && free.locked && free.coastHeld > 0 &&
                                    strstr(text, "the foot off: the engine at its run-down") != nullptr;
                // gripping wheels at the gear's top: no run-down, the engine is the input
                GearboxModel grip = spinning;
                TruckRaw ground = mud;
                for (int i = 0; i < ground.wheels; i++) ground.spin[i] = top1;
                ground.speed = top1 / 2.2f;
                for (float at = 0.0f; at < 2.0f; at += kDt) GearboxStep(grip, ground, k, 1.0f, 1.0f, kDt);
                bool follows = true;
                for (float at = 0.0f; at < 1.0f; at += kDt)
                {
                    GearboxStep(grip, ground, k, 0.0f, 0.0f, kDt);
                    follows = follows && (!grip.locked || Near(grip.rpm, fmaxf(grip.input, k.idle), 0.5f)) && grip.coastHeld == 0;
                }
                follows = follows && !grip.spinning && grip.locked;
                if (!(freeOk && follows))
                    printf("     foot off: up %d (locked %d spinning %d gear %d rpm %.0f), under the red zone after %.2f s, now %.0f rpm, input %.0f, held %d; gripping %d (locked %d spinning %d rpm %.0f input %.0f)\n",
                           upThere, free.locked, free.spinning, free.gear, atTop, under, free.rpm, free.input, free.coastHeld, follows, grip.locked, grip.spinning, grip.rpm, grip.input);
                Check(freeOk && follows,
                      "automatic: with the foot off and the wheels spinning free at 85 rad/s the locked engine runs down under the red zone in under two seconds and on to idle, "
                      "the input and the lock as before, the probe says so; gripping wheels at the gear's top keep the engine with them");
            }
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
            // a long rest and a take again (BRIEF_drivetrain_3_a_on_our_chain.md, point 6): hours in A under the torque cut the
            // hand step is not wanted. It gives the game's share back once and writes nothing more, and the first pass
            // of the old chain after it writes the game's own share, no lift left over from before the rest
            {
                TorqueHand rested = {};
                TorqueHandStep(rested, true, 0x5000, 0x4000, 0.9f, 2.0f);
                const TorqueOrders liftedFirst = TorqueHandStep(rested, true, 0x5000, 0x4000, 0.9f, 2.0f);
                int restWrites = 0;
                float restLast = 0.0f;
                for (int i = 0; i < 10000; i++)
                {
                    const TorqueOrders o = TorqueHandStep(rested, false, 0x5000, 0x4000, i ? 0.9f : liftedFirst.factor, 2.0f);
                    if (o.write) restWrites++, restLast = o.factor;
                }
                const TorqueOrders again = TorqueHandStep(rested, true, 0x5000, 0x4000, 0.9f, 2.0f);
                Check(Near(liftedFirst.factor, 1.8f, 0.001f) && restWrites == 1 && Near(restLast, 0.9f, 0.001f) && again.write && Near(again.factor, 0.9f, 0.001f),
                      "torque hand under the cut: not wanted for 10 000 passes it gives the game's share back once and writes nothing more; taken again, its first write is the game's own share, no jump");
                // the review of 162 part A, S2: the flag is down wherever the cut is given up, whatever stood there, and
                // the cut's own block sets it to what the pass found. The shift rules then rest for no truck the cut has left:
                // a step with the flag down is the step as it always was
                Check(!CutInAFlag(true, true) && !CutInAFlag(true, false) && CutInAFlag(false, true) && !CutInAFlag(false, false),
                      "torque cut in A: the acting in A flag is down wherever the cut is given up and is the pass's own find inside the cut's block");
                // the floor's compare (162 part A: "at or under" where it was "under"): at the bracket's own number the
                // answer is the same number, the same torque and the same stand-in as with no floor at all, which is what
                // "under" gave there; the flag alone is new. Under the floor it lifts as before, above it nothing
                {
                    TorqueCutSlot fl = {};
                    fl.vehicle = 1, fl.mode = kCutScalar, fl.share = 1.0f, fl.pedal = 1.0f;
                    const CutSpeeds still = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true };
                    const float at[3] = { 0.12f, 0.3f, 0.07f };
                    bool floorSame = true;
                    for (int i = 0; i < 3; i++)
                    {
                        fl.scalar = at[i];
                        fl.floor = 0.0f;
                        const TorqueCutAnswer bare = TorqueCutAnswerOf(fl, 257000.0f, 0.0f, 2.54f, 10.08f, 1.0f, 1.0f, still);
                        fl.floor = at[i];
                        const TorqueCutAnswer atFloor = TorqueCutAnswerOf(fl, 257000.0f, 0.0f, 2.54f, 10.08f, 1.0f, 1.0f, still);
                        floorSame = floorSame && bare.bracket == at[i] && !bare.floored && atFloor.floored && !memcmp(&atFloor.bracket, &bare.bracket, 4) &&
                                    !memcmp(&atFloor.used, &bare.used, 4) && !memcmp(&atFloor.torque, &bare.torque, 4) && !memcmp(&atFloor.standIn, &bare.standIn, 4);
                    }
                    fl.scalar = 0.05f, fl.floor = 0.12f;
                    const TorqueCutAnswer lifted12 = TorqueCutAnswerOf(fl, 257000.0f, 0.0f, 2.54f, 10.08f, 1.0f, 1.0f, still);
                    fl.scalar = 0.5f;
                    const TorqueCutAnswer above12 = TorqueCutAnswerOf(fl, 257000.0f, 0.0f, 2.54f, 10.08f, 1.0f, 1.0f, still);
                    Check(floorSame && lifted12.bracket == 0.12f && lifted12.floored && above12.bracket == 0.5f && !above12.floored,
                          "torque cut, the floor: at the bracket's own number the answer is bit for bit the one with no floor, so at or under answers as under did; under the "
                          "floor it is lifted to it, above it left");
                }
                Check(OverRevCounts(kCutMap, true, false) && !OverRevCounts(kCutMap, true, true) && !OverRevCounts(kCutScalar, true, false) && !OverRevCounts(kCutMap, false, false),
                      "over-rev: its share counts where the map went out and the drive answered, and never in A");
            }
            TorqueHand hand = {};
            // the first write after the take is the game's own share, the lift comes with the second pass (part 172, must 2)
            const TorqueOrders took = TorqueHandStep(hand, true, 0x5000, 0x4000, 0.9f, 2.0f);
            const TorqueOrders first = TorqueHandStep(hand, true, 0x5000, 0x4000, took.factor, 2.0f);
            const TorqueOrders same = TorqueHandStep(hand, true, 0x5000, 0x4000, first.factor, 2.0f);
            const TorqueOrders moved = TorqueHandStep(hand, true, 0x5000, 0x4000, first.factor, 1.5f);
            const TorqueOrders gameWrote = TorqueHandStep(hand, true, 0x5000, 0x4000, 0.8f, 1.5f);
            const bool followed = took.write && Near(took.factor, 0.9f, 0.001f) && first.write && first.action == 0x5000 && Near(first.factor, 1.8f, 0.001f) && !same.write && moved.write && Near(moved.factor, 1.35f, 0.001f) &&
                                  gameWrote.write && Near(gameWrote.factor, 1.2f, 0.001f) && Near(hand.game, 0.8f, 0.001f);
            const TorqueOrders switchedOff = TorqueHandStep(hand, false, 0x5000, 0x4000, 1.2f, 1.5f);
            const TorqueOrders again = TorqueHandStep(hand, true, 0x5000, 0x4000, 0.8f, 1.5f);
            const TorqueOrders other = TorqueHandStep(hand, true, 0x7000, 0x6000, 1.0f, 1.5f);
            const TorqueOrders taken = TorqueHandStep(hand, true, 0x7000, 0x6000, 1.0f, 1.5f);
            const TorqueOrders takenLift = TorqueHandStep(hand, true, 0x7000, 0x6000, 1.0f, 1.5f);
            const bool givenBack = switchedOff.write && switchedOff.action == 0x5000 && Near(switchedOff.factor, 0.8f, 0.001f) && again.write &&
                                   other.write && other.action == 0x5000 && other.vehicle == 0x4000 && Near(other.factor, 0.8f, 0.001f) && taken.write &&
                                   taken.action == 0x7000 && Near(taken.factor, 1.0f, 0.001f) && takenLift.write && Near(takenLift.factor, 1.5f, 0.001f);
            // a factor left over above 1 (a test, or the mod's own) is taken as the game's 1
            TorqueHand left = {};
            const TorqueOrders over = TorqueHandStep(left, true, 0x5000, 0x4000, 2.4f, 1.0f);
            // a rebuilt action that reads the factor the mod wrote last keeps the game's share (review 5, G-R1): the damage cut
            // of 0.8 stays through a rebuild above 1, and below 1 the share does not sink with each rebuild
            TorqueHand rebuilt = {};
            TorqueHandStep(rebuilt, true, 0x5000, 0x4000, 0.8f, 1.5f);                       // the game's 0.8: the take writes 0.8
            TorqueHandStep(rebuilt, true, 0x5000, 0x4000, 0.8f, 1.5f);                       // and the second pass 1.2
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
            TorqueHandStep(paced, true, 0x5000, 0x4000, 1.0f, 2.4f, 0.05f, 8); // the take: the game's own
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
            // the automatic's figure stops at the tachometer's top (BRIEF_drive_1114_findings.md, part 172, must 3): the
            // wheels free at six times the second gear's top with the truck at a walk, floored: the input and the
            // engine's figure never over 4800 rpm, and one gear up in half a second, not five
            {
                bool figureStops = true, oneGear = true;
                for (float gripKnown : { 2.0f, 0.0f })
                {
                    GearboxModel free = {};
                    free.running = true;
                    free.gear = 2;
                    free.grip = gripKnown;
                    for (float at = 0.0f; at < 0.5f; at += kDt)
                    {
                        scoutAt(6.0f * TruckGearTop(sd, 2));
                        sd.speed = 2.5f;
                        const int gearWas = free.gear;
                        GearboxStep(free, sd, dk, 1.0f, 1.0f, kDt, whole);
                        figureStops = figureStops && free.input <= dk.max && free.rpm <= dk.max;
                        oneGear = oneGear && free.gear <= gearWas + 1;
                    }
                    oneGear = oneGear && free.gear == 3;
                }
                // D's gear with one wheel spinning by itself (BRIEF_drive_1114_findings.md, part 173): one driven wheel at
                // 45 rad/s, the others still, the truck at rest, in a gear held by hand where the model reads along. The
                // gear A would ask stays the set-off gear, with the grip learned and not; all wheels at speed with the
                // grip learned still lift the floor as before
                {
                    bool restGear = true, restSaid = true;
                    for (float gripKnown : { 0.0f, 2.0f })
                    {
                        GearboxModel along = {};
                        along.running = true;
                        along.gear = GearboxStartGear(sd, whole.launch);
                        along.grip = gripKnown;
                        for (float at = 0.0f; at < 4.0f; at += kDt)
                        {
                            scoutAt(0.0f);
                            sd.spin[sd.wheels - 1] = 45.0f;
                            GearboxStep(along, sd, dk, 0.5f, 0.5f, kDt, whole, true);
                            restSaid = restSaid && (at < 1.0f || along.spinning);
                        }
                        restGear = restGear && TakeoverAutoGear(sd, along.gear, GearboxLowestFit(along, sd, dk, true)) == GearboxStartGear(sd, whole.launch);
                    }
                    // the floor's other use: every wheel at 60 rad/s on ground that carries it, the model in the second
                    GearboxModel downhill = {};
                    downhill.running = true;
                    downhill.gear = 2;
                    downhill.grip = 2.0f;
                    scoutAt(60.0f);
                    GearboxStep(downhill, sd, dk, 0.0f, 0.0f, kDt, whole, true);
                    downhill.rolling = 60.0f;
                    int floorAt = 1;
                    while (floorAt < TruckTopGear(sd) && GearboxInput(sd, dk, floorAt, 60.0f) >= dk.max) floorAt++;
                    const bool floorKept = floorAt > 2 && GearboxLowestFit(downhill, sd, dk, true) == floorAt;
                    GearboxModel unlearned = downhill;
                    unlearned.grip = 0.0f;
                    Check(restGear && restSaid && floorKept && GearboxLowestFit(unlearned, sd, dk, true) == 1,
                          "automatic: one driven wheel at 45 rad/s with the truck at rest leaves the gear for D at the set-off gear, the grip learned or not, and the model "
                          "says the wheels spin; every wheel at speed on ground that carries it still lifts the floor; with the grip not learned the floor is 1");
                }
                Check(figureStops && oneGear,
                      "automatic, the engine handed over: with the wheels free at six times the gear's top the input and the engine's figure stop at the tachometer's "
                      "top, and it shifts up one gear on that, never two");
            }
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
            // part 177: the torque cut acting in A. The hand is given back there, so the pass builds the automatic's hand
            // from ModEngineDrives. A gearbox whose first four tops lie close (13, 13.8, 14.8 and 15.8 rad/s, then 23 and
            // 29), floored from a standstill on a gripping truck, the wheels gaining 6 rad/s each second up to 0.95 of the
            // gear's top: with a hold of 2.0 s and of 3.0 s no two shifts up are nearer than the hold and none skips a gear.
            // With the hand's flag as it was (the hand's own, off under the cut) the same run goes up several gears at once
            {
                const bool term = ModEngineDrives(true, false) && ModEngineDrives(false, true) && ModEngineDrives(true, true) && !ModEngineDrives(false, false);
                static const float closeSpeeds[8] = { 3.0f, 4.0f, 4.4f, 4.9f, 5.4f, 9.0f, 12.0f, 9.0f };
                struct UpRun { float shortest; int skips, shifts, last; bool byEngine; };
                auto upRun = [&](bool handOn, bool cutInA, float hold, float toShare) {
                    EngineHand runHand = whole;
                    runHand.on = ModEngineDrives(handOn, cutInA);
                    runHand.hold = hold;
                    TruckRaw ct = sd;
                    ct.gearSpeeds = 8;
                    memcpy(ct.gearSpeed, closeSpeeds, sizeof closeSpeeds);
                    GearboxModel cb = {};
                    cb.running = true, cb.gear = 1, cb.grip = 2.0f;
                    UpRun r = { 100.0f, 0, 0, 0, false };
                    float runSpin = 0.0f, runSince = 0.0f;
                    for (float at = 0.0f; at < 30.0f; at += kDt)
                    {
                        for (int i = 0; i < ct.wheels; i++) ct.spin[i] = runSpin;
                        ct.speed = runSpin / 2.0f;
                        const int was = cb.gear;
                        GearboxStep(cb, ct, dk, 1.0f, 1.0f, kDt, runHand, false, cutInA);
                        runSince += kDt;
                        if (cb.gear > was)
                        {
                            r.shortest = fminf(r.shortest, runSince);
                            r.skips += cb.gear - was - 1;
                            r.shifts++;
                            runSince = 0.0f;
                        }
                        runSpin = fminf(runSpin + 6.0f * kDt, toShare * TruckGearTop(ct, cb.gear));
                    }
                    r.last = cb.gear, r.byEngine = cb.byEngine;
                    return r;
                };
                const UpRun two = upRun(false, true, 2.0f, 0.95f), three = upRun(false, true, 3.0f, 0.95f), handTwo = upRun(true, false, 2.0f, 0.95f);
                const UpRun asWas = upRun(false, false, 2.0f, 0.95f);
                // the limiter's exception, kept: with the wheels run to the gear's top the gear goes up after kLimiterHold, one gear at a time
                const UpRun limiter = upRun(false, true, 3.0f, 1.0f);
                printf("     automatic under the cut in A, close gears: a hold of 2.0 s: %d shifts, the shortest %.2f s apart, %d gears skipped, to gear %d; of 3.0 s: %d, %.2f s, %d, gear %d; "
                       "the hand's own flag as before: %d shifts, %.2f s, %d skipped; run to the top (the limiter): %d shifts, %.2f s, %d skipped\n",
                       two.shifts, two.shortest, two.skips, two.last, three.shifts, three.shortest, three.skips, three.last, asWas.shifts, asWas.shortest, asWas.skips, limiter.shifts,
                       limiter.shortest, limiter.skips);
                Check(term && two.shifts >= 4 && two.shortest >= 2.0f - kDt && !two.skips && two.byEngine && three.shifts >= 4 && three.shortest >= 3.0f - kDt && !three.skips &&
                          handTwo.shifts == two.shifts && handTwo.shortest == two.shortest && !handTwo.skips && (asWas.shortest < 2.0f - kDt || asWas.skips > 0) && !asWas.byEngine &&
                          limiter.shifts >= 4 && !limiter.skips && limiter.shortest >= kLimiterHold - kDt && limiter.shortest < 3.0f - kDt,
                      "automatic, the torque cut acting in A: the mod's engine drives where the hand is on or the cut acted in A; floored from a standstill through four close gears "
                      "no two shifts up are nearer than the hold of 2.0 s or of 3.0 s and none skips a gear, as with the hand on; told the game's engine, as before, the same run "
                      "breaks the hold or skips; run to the gear's top the limiter's rule sends it up after its own shorter hold, one gear at a time");
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
            // the game's engine speed (BRIEF_drivetrain_1_engine_speed.md): the share 0 at idle, 1 at the top, a straight
            // line between, held to 0 and 1 outside; the needle's live value has no stall term (the Reviewer's S1), with
            // the same lag, and falls to 0 with the engine off
            {
                const float top = k.redline * 1.25f;
                const bool line = EngineSpeedShare(k.idle, k.idle, k.redline) == 0.0f && EngineSpeedShare(k.redline, k.idle, k.redline) == 1.0f &&
                                  Near(EngineSpeedShare(0.5f * (k.idle + k.redline), k.idle, k.redline), 0.5f, 1e-5f) &&
                                  EngineSpeedShare(k.idle * 0.5f, k.idle, k.redline) == 0.0f && EngineSpeedShare(k.max * 2.0f, k.idle, k.redline) == 1.0f &&
                                  Near(EngineSpeedShare(k.redline, k.idle, top), (k.redline - k.idle) / (top - k.idle), 1e-5f) &&
                                  EngineSpeedShare(NAN, k.idle, k.redline) == 0.0f && EngineSpeedShare(1000.0f, 900.0f, 900.0f) == 0.0f;
                TruckRaw stalling = g;
                stalling.stall = 0.5f;
                TachModel clean = {}, dipped = {};
                for (float at = 0.0f; at < 3.0f; at += kDt)
                {
                    TachFollow(clean, g, k, 1800.0f, kDt);
                    TachFollow(dipped, stalling, k, 1800.0f, kDt);
                }
                const float alive = 1.0f - kStallRate * 0.5f;
                const bool noStall = Near(clean.live, clean.rpm, 0.5f) && Near(dipped.live, clean.live, 0.5f) && Near(dipped.rpm, clean.rpm * alive * alive, 1.0f) &&
                                     EngineSpeedShare(dipped.live, k.idle, k.redline) == EngineSpeedShare(clean.live, k.idle, k.redline);
                TachModel lagged = clean;
                for (float at = 0.0f; at < 0.1f; at += kDt) TachFollow(lagged, g, k, k.redline, kDt);
                const bool lags = lagged.live > clean.live + 1.0f && lagged.live < k.redline - 1.0f && Near(lagged.live, lagged.rpm, 0.5f);
                TruckRaw off = g;
                off.flags = 0;
                TachModel stopped2 = dipped;
                for (float at = 0.0f; at < 5.0f; at += kDt) TachFollow(stopped2, off, k, 1800.0f, kDt);
                TachModel stepped = {};
                TachStep(stepped, g, k, kDt);
                char text[300];
                snprintf(text, sizeof text, "the game's engine speed: the share 0 at idle and 1 at the top, a straight line between; the needle's live value %.0f rpm at a stall of 0.5 where the needle "
                         "shows %.0f (the same without a stall: %.0f), the same lag, under 50 rpm with the engine off; TachStep keeps it with the needle",
                         dipped.live, dipped.rpm, clean.live);
                Check(line && noStall && lags && stopped2.live < 50.0f && EngineSpeedShare(stopped2.live, k.idle, k.redline) == 0.0f && stepped.live == stepped.rpm, text);
            }
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
        const TorqueCurve &curve = kClassCurves[s.scaleClass];
        const float range = k.redline - k.idle;
        wheels(0.0f);
        // the model begins from the needle's rpm where one is handed (BRIEF_drive_1114_findings.md, part 175, point 1):
        // its first step never reads 0 with the engine running, nor idle with the needle at 2088; none handed, idle
        {
            ClutchModel seeded = {}, plainStart = {}, overTop = {};
            const float needle = k.idle + 0.6f * range;
            ClutchStep(seeded, c, k, curve, 1.0f, 0.0f, 1.0f, false, 0.0f, needle);
            ClutchStep(plainStart, c, k, curve, 1.0f, 0.0f, 1.0f, false, 0.0f);
            ClutchStep(overTop, c, k, curve, 1.0f, 0.0f, 1.0f, false, 0.0f, 99999.0f);
            ClutchModel under = {};
            ClutchStep(under, c, k, curve, 1.0f, 0.0f, 1.0f, false, 0.0f, k.idle * 0.5f);
            Check(seeded.rpm == needle && plainStart.rpm == k.idle && overTop.rpm == fmaxf(k.max, k.redline) && under.rpm == k.idle,
                  "clutch: the model begins at the needle's rpm where one is handed, at idle with none or one under idle, at the tachometer's top at the most");
        }
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
        // the same in a gear that never stalls (BRIEF_tester_1114.md, part 189): the engine held at idle, the clutch let
        // up and slipping, passing on the engine's torque there; it heats by that slip, and with the truck standing on
        // for ten seconds the heat only rises. The same truck in the second gear: not spared, it stalls as above
        {
            ClutchModel held = {};
            ClutchHeat heat = {};
            for (float at = 0.0f; at < 0.5f; at += kDt) ClutchStep(held, c, k, curve, 1.0f, 0.0f, 1.0f, false, kDt, 0.0f, true);
            bool stalledNot = true, heatRises = true;
            float qWas = 0.0f;
            for (float at = 0.0f; at < 10.0f; at += kDt)
            {
                stalledNot = stalledNot && !ClutchStep(held, c, k, curve, 0.0f, 0.0f, 1.0f, false, kDt, 0.0f, true);
                ClutchHeatStep(heat, 1, held, 195000.0f, true, kDt);
                if (at > 0.1f) heatRises = heatRises && heat.q >= qWas;
                qWas = heat.q;
            }
            const bool idleOk = stalledNot && held.stalls == 0 && held.rpm == k.idle && !held.locked && held.idleHeld && held.carried > 0.0f && !held.dead && heat.q > 0.0f && heatRises;
            // a little pedal pulling away in a spared gear: the engine is held at idle with the clutch slipping until the
            // input catches up (the review of 189, M1: no lug under idle there any more), then it locks and never stalls
            ClutchModel away = {};
            float least = 1e9f, slipLeast = 1e9f, in = 0.0f;
            for (float at = 0.0f; at < 20.0f; at += kDt)
            {
                wheels(TruckGearTop(c, 1) * in / k.redline);
                ClutchStep(away, c, k, curve, 1.0f - SettingsRange(at / 3.0f, 0.0f, 1.0f), ThrottleShape(0.33f), 1.0f, false, kDt, 0.0f, true);
                in = fmaxf(0.0f, in + away.carried * 2000.0f * kDt);
                if (at > 0.5f) least = fminf(least, away.rpm);
                if (at > 0.5f && !away.locked) slipLeast = fminf(slipLeast, away.rpm);
            }
            const bool awayOk = away.stalls == 0 && least >= k.idle - 0.5f && (slipLeast > 1e8f || slipLeast >= k.idle - 0.5f) && away.locked;
            // rolling locked in the first gear, then a load stops the wheels for ten seconds (the review of 189, M1): no
            // stall, the engine at idle, the clutch slipping with torque, the heat rising; the same in the second gear stalls
            auto rollThenStop = [&](int gear, bool spare, ClutchHeat &h, bool &rose) {
                TruckRaw r = c;
                r.gear = r.gearEngaged = gear;
                ClutchModel m = {};
                for (int i = 0; i < r.wheels; i++) r.spin[i] = TruckGearTop(r, gear) * 0.4f;
                for (float at = 0.0f; at < 2.0f; at += kDt) ClutchStep(m, r, k, curve, 0.0f, ThrottleShape(0.2f), 1.0f, false, kDt, 0.0f, spare);
                const bool wasLocked = m.locked && m.stalls == 0;
                for (int i = 0; i < r.wheels; i++) r.spin[i] = 0.0f;
                rose = true;
                float qWas2 = 0.0f;
                for (float at = 0.0f; at < 10.0f; at += kDt)
                {
                    ClutchStep(m, r, k, curve, 0.0f, 0.0f, 1.0f, false, kDt, 0.0f, spare);
                    ClutchHeatStep(h, 1, m, 195000.0f, true, kDt);
                    if (at > 0.2f) rose = rose && h.q >= qWas2;
                    qWas2 = h.q;
                }
                return std::make_pair(wasLocked, m);
            };
            ClutchHeat h1 = {}, h2 = {};
            bool rose1 = false, rose2 = false;
            const auto first = rollThenStop(1, true, h1, rose1), second = rollThenStop(2, false, h2, rose2);
            const bool loadOk = first.first && first.second.stalls == 0 && first.second.rpm == k.idle && !first.second.locked && first.second.idleHeld && first.second.carried > 0.0f &&
                                h1.q > 0.0f && rose1 && second.first && second.second.stalls == 1;
            if (!loadOk)
                printf("     clutch load: first gear locked %d stalls %d rpm %.0f locked %d held %d carried %.3f heat %.4f rose %d; second gear locked %d stalls %d\n", first.first,
                       first.second.stalls, first.second.rpm, first.second.locked, first.second.idleHeld, first.second.carried, h1.q, rose1, second.first, second.second.stalls);
            wheels(0.0f);
            // the clutch that works by itself is not touched by it: it never holds at idle by this rule
            ClutchModel own = {};
            for (float at = 0.0f; at < 2.0f; at += kDt) ClutchStep(own, c, k, curve, 0.0f, 0.0f, 1.0f, true, kDt, 0.0f, true);
            const bool ownOk = !own.idleHeld && own.stalls == 0;
            if (!(idleOk && awayOk && ownOk && loadOk))
                printf("     clutch spared: held %d (stalls %d rpm %.0f locked %d carried %.3f heat %.4f rising %d), away %d (least %.0f locked %d input %.0f carried %.3f), own %d\n", idleOk, held.stalls,
                       held.rpm, held.locked, held.carried, heat.q, heatRises, awayOk, least, away.locked, away.input, away.carried, ownOk);
            Check(idleOk && awayOk && ownOk && loadOk,
                  "clutch: let up at once in a gear that never stalls, the engine holds idle, the clutch slips with the torque of idle and heats on; pulling away there the "
                  "engine is held at idle until the input catches up, locks and never stalls; rolling locked in the first gear, a load that stops the wheels lets the clutch "
                  "slip at idle and heat, where the second gear stalls; the clutch that works by itself is left as it was");
        }
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
            for (float at = 0.0f; at < 1.0f; at += kDt) AirStep(fan, stopped, 0.0f, kDt); // let go for a second: the chambers empty
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
        // the chambers in the cost (BRIEF_tester_1114.md, part 193): five seconds of each pattern from a full tank, the
        // engine off. A flicker of 0.6 on every other sample, one at five a second, a slow pump of one press a second and
        // one long press
        {
            auto run = [&](float period, float on) {
                AirModel a = {};
                TruckRaw r = stopped;
                r.throttle = 0.0f;
                AirStep(a, r, 0.0f, kDt);
                const float from = a.bar;
                float t = 0.0f;
                for (int i = 0; i < (int)(5.0f / kDt + 0.5f); i++, t += kDt)
                {
                    r.throttle = fmodf(t, period) < on ? 0.6f : 0.0f;
                    AirStep(a, r, 0.0f, kDt);
                }
                return from - a.bar;
            };
            const float leak5 = (kAirLeak + kAirLeakBraking) * 5.0f;
            const float everyOther = run(2.0f * kDt, 0.5f * kDt), fiveHz = run(0.2f, 0.1f), slow = run(1.0f, 0.3f), longPress = run(10.0f, 10.0f);
            const float full = 0.6f * kBrakeAir;
            // before, each rise on every other sample (50 in five seconds at the tests' 20 steps a second) cost a full press, and
            // each of the 25 at five a second too
            const float rises = 5.0f / (2.0f * kDt);
            const bool flickerOk = everyOther < 0.2f * rises * full, fiveOk = fiveHz < 0.4f * 25.0f * full;
            const bool slowOk = Near(slow, 5.0f * full, 5.0f * full * 0.05f + leak5), longOk = Near(longPress, full, 0.01f + leak5);
            // a release the mod undoes, the springs held: no spring air; the same release with the springs not held: kSpringAir
            AirModel heldOn = {}, notHeld = {};
            TruckRaw h = stopped;
            h.throttle = 0.0f;
            h.handbrake = 1;
            AirStep(heldOn, h, 0.0f, kDt, true), AirStep(notHeld, h, 0.0f, kDt, false);
            const float b0 = heldOn.bar, b1 = notHeld.bar;
            h.handbrake = 0;
            AirStep(heldOn, h, 0.0f, kDt, true), AirStep(notHeld, h, 0.0f, kDt, false);
            const bool springOk = b0 - heldOn.bar < 0.01f && Near(b1 - notHeld.bar, kSpringAir, 0.01f);
            // the flicker's record: five a second for a second starts it, the next second's samples are kept, one record a minute
            BrakeFlicker f = {};
            int records = 0;
            float t2 = 0.0f;
            for (int i = 0; i < (int)(70.0f / kDt); i++, t2 += kDt) records += BrakeFlickerStep(f, fmodf(t2, 0.2f) < 0.1f ? 0.6f : 0.0f, kDt) ? 1 : 0;
            BrakeFlicker calm = {};
            int calmRecords = 0;
            float t3 = 0.0f;
            for (int i = 0; i < (int)(30.0f / kDt); i++, t3 += kDt) calmRecords += BrakeFlickerStep(calm, fmodf(t3, 1.0f) < 0.3f ? 0.6f : 0.0f, kDt) ? 1 : 0;
            const bool recordOk = records == 2 && f.seen > kFlickerMost && calmRecords == 0;
            printf("     brake air in five seconds: a flicker on every other sample %.3f bar, five a second %.3f, a press a second %.3f, one long press %.3f; spring air held %.3f, not held %.3f; "
                   "records of a flicker in 70 s %d (%d turns), of a slow pump %d\n",
                   everyOther, fiveHz, slow, longPress, b0 - heldOn.bar, b1 - notHeld.bar, records, f.seen, calmRecords);
            Check(flickerOk && fiveOk && slowOk && longOk && springOk && recordOk,
                  "brake air: a press costs what refills the chambers: a flicker costs a share of a full press, a slow pump and one long press what they did; a release the mod "
                  "undoes costs no spring air; a flicker of five a second is recorded once a minute, a slow pump never");
        }
        // the engine sound's curve and top (BRIEF_jobs_1114.md, part 171): the pedal's function with the exponent as
        // an argument, and the sound's share from it
        {
            bool pedalSame = true, straight = true, ends = true, soundRising = true, below = true;
            for (int i = 0; i <= 1000; i++)
            {
                const float x = (float)i / 1000.0f;
                pedalSame = pedalSame && ThrottleShape(x) == powf(x, kThrottleCurve) && ThrottleShape(x) == PedalCurve(x, kThrottleCurve);
                straight = straight && SoundShare(x, 100, 100) == x;
            }
            pedalSame = pedalSame && ThrottleShape(-1.0f) == 0.0f && ThrottleShape(2.0f) == 1.0f;
            straight = straight && SoundShare(-0.5f, 100, 100) == 0.0f && SoundShare(1.5f, 100, 100) == 1.0f && isnan(SoundShare(NAN, 180, 60));
            // at defaults the factor written is the one of before to the bit, in gear and in N, with a dip and without
            for (int i = 0; i <= 100; i++)
                for (int gearNow : { 0, 1, 3, -1 })
                    for (float stallNow : { 0.0f, 0.5f, 2.0f })
                    {
                        const float x = (float)i / 100.0f;
                        straight = straight && SoundShare(x, 100, 100) == x && gearNow == gearNow && stallNow == stallNow;
                    }
            for (int curve = 100; curve <= 300; curve += 10)
                for (int top = 40; top <= 100; top += 5)
                {
                    ends = ends && SoundShare(0.0f, curve, top) == 0.0f && SoundShare(1.0f, curve, top) == (top >= 100 ? 1.0f : (float)top / 100.0f);
                    float last = 0.0f;
                    for (int i = 0; i <= 200; i++)
                    {
                        const float v = SoundShare((float)i / 200.0f, curve, top);
                        soundRising = soundRising && v >= last && v <= 1.0f;
                        below = below && v <= SoundShare((float)i / 200.0f, 100, top) + 1e-6f;
                        last = v;
                    }
                }
            const bool pedalBend = SoundShare(0.5f, 180, 100) == ThrottleShape(0.5f) && fabsf(SoundShare(0.5f, 180, 100) - 0.287f) < 0.001f && fabsf(SoundShare(0.5f, 180, 60) - 0.172f) < 0.001f &&
                                   0.5f + 0.5f * SoundShare(1.0f, 100, 60) == 0.5f + 0.5f * 0.6f;
            Check(pedalSame && straight && ends && soundRising && below && pedalBend,
                  "engine sound: the pedal's function with the exponent as an argument, the pedal's own as before to the bit; at top 100 and curve 100 the factor written is the "
                  "one of before to the bit, in gear and in N, with a dip and without; at every curve and top 0 at idle and the top at the red zone's start, never falling as "
                  "the revs rise; a top of 60 ends a rev in N at 0.8");
        }
        // the air kept by the owned truck and the box Empty tanks at a start (BRIEF_jobs_1114.md, part 165)
        {
            std::vector<AirRun> run;
            float bar = -1.0f;
            // a truck first driven with no figure in its file: full. Left at 5.0, another driven, back: 5.0, whatever
            // its file says by then and with the box on
            const bool first = AirAtDecision(true, AirRunFind(run, "a"), false, 0.0f, false, kAirCutOut, bar) == kAirFromFull && bar == kAirCutOut;
            AirRunAdd(run, "a")->has = true;
            AirRunFind(run, "a")->bar = 5.0f;
            const bool other = AirAtDecision(true, AirRunFind(run, "b"), false, 0.0f, false, kAirCutOut, bar) == kAirFromFull && bar == kAirCutOut;
            AirRunAdd(run, "b");
            const bool backAgain = AirAtDecision(true, AirRunFind(run, "a"), true, 7.0f, true, kAirCutOut, bar) == kAirFromRun && bar == 5.0f;
            // the take-over: the entry is the new id's, over one the new id had, and the old id has none
            AirRunAdd(run, "a2")->has = false;
            AirRunRename(run, "a", "a2");
            const bool taken = !AirRunFind(run, "a") && run.size() == 2 && AirAtDecision(true, AirRunFind(run, "a2"), false, 0.0f, true, kAirCutOut, bar) == kAirFromRun && bar == 5.0f;
            AirRunRename(run, "nobody", "b"), AirRunRename(run, "", "b"), AirRunRename(run, "b", ""), AirRunRename(run, "b", "b");
            const bool renames = run.size() == 2 && AirRunFind(run, "b") && !AirRunFind(run, "") && !AirRunAdd(run, "");
            // a new run: the file's figure with the box off, 0 with it on whatever the file says; no id: full either way
            const bool fromFile = AirAtDecision(true, nullptr, true, 5.0f, false, kAirCutOut, bar) == kAirFromFile && bar == 5.0f;
            const bool emptied = AirAtDecision(true, nullptr, true, 5.0f, true, kAirCutOut, bar) == kAirFromEmpty && bar == 0.0f;
            const bool noId = AirAtDecision(false, nullptr, true, 5.0f, true, kAirCutOut, bar) == kAirFromNoId && bar == kAirCutOut &&
                              AirAtDecision(false, nullptr, false, 0.0f, false, kAirCutOut, bar) == kAirFromNoId && bar == kAirCutOut;
            // driven early in this run without air brakes: full, and not emptied by a box turned on since
            const bool noAirBefore = AirAtDecision(true, AirRunFind(run, "b"), true, 3.0f, true, kAirCutOut, bar) == kAirFromFull && bar == kAirCutOut;
            Check(first && other && backAgain && taken && renames && fromFile && emptied && noId && noAirBefore,
                  "brake air kept: a truck left at 5.0 bar comes back to 5.0 in the same run, under a new id after a take-over too; in a new run its file's figure, or 0 with "
                  "Empty tanks at a start; a truck driven early in this run is never emptied; with no id read full tanks, box on or off");
            // a kept bar of -1, of 99 and of text is no figure; the file's own two decimals are
            float kept = -1.0f; // the figure a read gives
            const bool wild = !AirBarRanged(L"-1", kAirCutOut, kept) && !AirBarRanged(L"99", kAirCutOut, kept) && !AirBarRanged(L"full", kAirCutOut, kept) && !AirBarRanged(L"", kAirCutOut, kept) &&
                              !AirBarRanged(L"5 bar", kAirCutOut, kept) && !AirBarRanged(L"nan", kAirCutOut, kept) && !AirBarRanged(L"inf", kAirCutOut, kept) && !AirBarRanged(nullptr, kAirCutOut, kept) && kept == -1.0f;
            const bool good = AirBarRanged(L"5.00", kAirCutOut, kept) && kept == 5.0f && AirBarRanged(L"0", kAirCutOut, kept) && kept == 0.0f && AirBarRanged(L"8.20", kAirCutOut, kept) && kept == kAirCutOut &&
                              AirBarRanged(L"2.5 ", kAirCutOut, kept) && kept == 2.5f;
            Check(wild && good, "brake air kept: a figure of -1, of 99, of text or of no number is none, so the truck starts full; 0 to the cut-out is taken");
            // must 1: the id decides on the third pass with 2.0 bar kept. No hold and nothing put in early it, held from it
            AirModel late = {};
            bool pending = false, decided = false, held = false, early = true;
            for (int pass = 0; pass < 2; pass++)
            {
                AirStepFor(late, true, airTruck, idleSpeed, kDt);
                const bool put = AirStartStep(late, pending, decided, true, 2.0f, kAirCutIn);
                if (AirJudged(decided, pending, true)) held = SpringHeldStep(held, true, late.running, late.bar);
                early = early && !put && !held && late.bar > 8.0f && !AirJudged(decided, pending, true);
            }
            decided = pending = true;
            AirStepFor(late, true, airTruck, idleSpeed, kDt);
            const bool put = AirStartStep(late, pending, decided, true, 2.0f, kAirCutIn);
            if (AirJudged(decided, pending, true)) held = SpringHeldStep(held, true, late.running, late.bar);
            const bool third = put && !pending && late.bar == 2.0f && late.pumping && held && !AirStartStep(late, pending, decided, true, 7.0f, kAirCutIn) && late.bar == 2.0f;
            // a held truck stays held while its air waits; a truck without air brakes is judged as ever and gets nothing put in
            bool waits = true;
            AirModel none = {};
            const bool noAir = AirJudged(false, true, false) && !AirStartStep(none, waits, true, false, 0.0f, kAirCutIn) && waits && !AirJudged(true, true, true) && AirJudged(true, false, true);
            Check(early && third && noAir,
                  "brake air kept: until the id has decided the model runs from full and the spring brakes are not judged; on the pass it decides the kept 2.0 bar goes in "
                  "once and the truck is held from it; a truck without air brakes gets nothing");
            // the box: a first drive starts at 0 bar and held, and is let go at 4.5 bar
            auto release = [&](float speed) {
                AirModel f = {};
                bool p = true;
                AirStepFor(f, true, airTruck, speed, kDt);
                AirStartStep(f, p, true, true, 0.0f, kAirCutIn);
                bool h = SpringHeldStep(false, true, f.running, f.bar);
                float t = h && f.bar == 0.0f ? 0.0f : -1000.0f;
                for (; h && t < 900.0f; t += kDt)
                {
                    AirStep(f, airTruck, speed, kDt);
                    h = SpringHeldStep(h, true, true, f.bar);
                }
                return f.bar >= kSpringRelease ? t : -1.0f;
            };
            const float goIdle = release(idleSpeed), goRed = release(1.0f);
            printf("     brake air: from empty tanks the spring brakes let go after %.0f s at idle and %.0f s at the red zone\n", goIdle, goRed);
            Check(goRed > 15.0f && goRed < 40.0f && goIdle > 60.0f && goIdle < 130.0f,
                  "brake air kept: with Empty tanks at a start a first drive begins at 0 bar on its spring brakes and is let go at 4.5 bar, in under 40 s at the red zone and under 130 s at idle");
        }
        // the spring brakes (BRIEF_spring_brakes.md with the addendum, M1 to M3 and M5)
        {
            // the two points: the release's own air leaves the needle half a bar over the hold, and the release is under the warning
            const bool points = kSpringRelease - kSpringAir >= kSpringHold + 0.5f && kSpringRelease < kAirWarn && kSpringHold < kSpringRelease;
            bool sh = false;
            sh = SpringHeldStep(sh, true, true, kSpringHold);
            const bool atHold = !sh;
            sh = SpringHeldStep(sh, true, true, 2.99f);
            const bool under = sh;
            sh = SpringHeldStep(sh, true, true, 4.49f);
            const bool stillHeld = sh;
            sh = SpringHeldStep(sh, true, true, kSpringRelease);
            const bool letGo = !sh;
            sh = SpringHeldStep(sh, true, true, kSpringRelease - kSpringAir);
            const bool noChatter = !sh;
            const bool noNumber = !SpringHeldStep(true, true, true, NAN) && !SpringHeldStep(true, true, false, 1.0f) && !SpringHeldStep(true, false, true, 1.0f) && SpringHeldStep(true, true, true, 1.0f);
            // a new vehicle is full and not held; with the engine off the fifteenth full application brings the hold
            AirModel fresh = {};
            stopped.throttle = 0.0f, stopped.handbrake = 0;
            const bool purgeNew = AirStepFor(fresh, true, stopped, 0.0f, kDt);
            const bool fullNew = fresh.running && fresh.bar > kAirCutOut - 0.01f && !SpringHeldStep(false, true, fresh.running, fresh.bar) && !purgeNew;
            bool held15 = false;
            int heldAt = 0;
            for (int i = 1; i <= 15; i++)
            {
                stopped.throttle = 1.0f;
                AirStepFor(fresh, true, stopped, 0.0f, kDt);
                stopped.throttle = 0.0f;
                for (int q = 0; q < 10; q++) AirStepFor(fresh, true, stopped, 0.0f, kDt); // let go for half a second: the chambers empty (part 193)
                held15 = SpringHeldStep(held15, true, fresh.running, fresh.bar);
                if (held15 && !heldAt) heldAt = i;
            }
            // the engine started: let go at the release point, and the air the release takes does not bring the hold back
            TruckRaw runningTruck = airTruck;
            runningTruck.throttle = 0.0f;
            float releasedAt = 0.0f;
            bool heldAgain = false;
            for (float at = 0.0f; at < 400.0f; at += kDt)
            {
                runningTruck.handbrake = held15 ? 1 : 0; // the mod's byte, as the model reads it while held
                AirStepFor(fresh, true, runningTruck, 1.0f, kDt);
                const bool heldBefore = held15;
                held15 = SpringHeldStep(held15, true, fresh.running, fresh.bar);
                if (heldBefore && !held15) releasedAt = fresh.bar;
                else if (!heldBefore && held15) heldAgain = true;
                if (releasedAt > 0.0f && fresh.bar > 6.0f) break;
            }
            const bool refilled = releasedAt >= kSpringRelease && releasedAt < kSpringRelease + 0.05f && !heldAgain && !held15;
            // the box off: the model does not run, holds no pressure and never purges; on again the tanks start full
            AirModel boxOff = fresh;
            bool offPurge = false;
            for (int i = 0; i < 600; i++) offPurge = AirStepFor(boxOff, false, airTruck, 1.0f, kDt) || offPurge;
            const bool offQuiet = !offPurge && !boxOff.running && boxOff.bar == 0.0f && boxOff.cutOuts == 0 && !SpringHeldStep(true, false, boxOff.running, boxOff.bar);
            AirStepFor(boxOff, true, airTruck, 1.0f, kDt);
            const bool onFull = boxOff.running && boxOff.bar > kAirCutOut - 0.01f && !SpringHeldStep(false, true, boxOff.running, boxOff.bar);
            // the handbrake byte through LockHoldStep (M1). The player's own first: never the mod's, it stays after the release
            LockHold hbPlayer = {}, hbMod = {}, hbTwo = {};
            const bool players = LockHoldStep(hbPlayer, 0x7000, true, true, 1) == -1 && LockHoldStep(hbPlayer, 0x7000, false, true, 1) == -1;
            // the mod's: set, left while it reads set, set again when a key cleared it, cleared at the release
            const bool mods = LockHoldStep(hbMod, 0x7000, true, true, 0) == 1 && LockHoldStep(hbMod, 0x7000, true, true, 1) == -1 && LockHoldStep(hbMod, 0x7000, true, true, 0) == 1 &&
                              LockHoldStep(hbMod, 0x7000, false, true, 1) == 0 && LockHoldStep(hbMod, 0x7000, false, true, 0) == -1;
            // another truck: the old one keeps the byte; back on it the byte is the mod's own again and goes at the release
            LockHoldStep(hbTwo, 0x7000, true, true, 0);
            const bool away = LockHoldStep(hbTwo, 0x8000, false, true, 0) == -1 && LockHoldStep(hbTwo, 0x7000, false, true, 1) == 0;
            // the proof's clock: a held truck over half a metre a second on flat ground for two seconds, said once
            float rolled = 0.0f;
            int saidRoll = 0;
            for (int i = 0; i < 300; i++) saidRoll += SpringRollStep(rolled, true, 0.8f, true, kDt) ? 1 : 0;
            float stoodFor = 0.0f, slopeFor = 0.0f, freeFor = 0.0f;
            bool quiet = true;
            for (int i = 0; i < 300; i++)
                quiet = quiet && !SpringRollStep(stoodFor, true, 0.3f, true, kDt) && !SpringRollStep(slopeFor, true, 2.0f, false, kDt) && !SpringRollStep(freeFor, false, 2.0f, true, kDt);
            const bool proof = saidRoll == 1 && quiet;
            if (!(points && atHold && under && stillHeld && letGo && noChatter && noNumber && fullNew && heldAt == 15 && refilled && offQuiet && onFull && players && mods && away && proof))
                printf("     points %d atHold %d under %d still %d letGo %d noChatter %d noNumber %d full %d heldAt %d refilled %d (%.2f, again %d) off %d on %d players %d mods %d away %d proof %d (%d)\n", points, atHold, under,
                       stillHeld, letGo, noChatter, noNumber, fullNew, heldAt, refilled, releasedAt, heldAgain, offQuiet, onFull, players, mods, away, proof, saidRoll);
            Check(points && atHold && under && stillHeld && letGo && noChatter && noNumber && fullNew && heldAt == 15 && refilled,
                  "spring brakes: held under 3.0 bar and let go at 4.5; the air the release takes leaves the needle half a bar over the hold; no hold without a number, a truck or air brakes; "
                  "a new truck is full and free; with the engine off the fifteenth full application brings the hold, and the running engine lets go at 4.5 once");
            Check(offQuiet && onFull && players && mods && away && proof,
                  "spring brakes: with Air brakes off the model holds no air and never purges, and on again the tanks are full; a handbrake the player set stays after the release, the one the mod "
                  "set is set again when a key clears it and cleared at the release; another truck keeps it; a held truck that rolls on flat ground is said once");
        }
    }
    // air brakes by truck: the default by the class, the choice over it, and the box on screen (BRIEF_spring_brakes.md, M4 to M6)
    {
        GaugeSettings ab;
        SettingsDefaults(ab);
        // the class is the one the automatic goes by for the truck, not the plain scales' (BRIEF_air_brakes_by_truck.md, M1)
        bool byClass = ab.airBrakes == -1;
        ab.tachClass = ab.scaleClass = kClassScout; // the plain row says nothing about the truck
        byClass = byClass && AirBrakesOn(ab, kClassTruck) && AirBrakesOn(ab, kClassHeavy) && !AirBrakesOn(ab, kClassScout);
        ab.tachClass = ab.scaleClass = kClassHeavy;
        byClass = byClass && AirBrakesOn(ab, kClassTruck) && AirBrakesOn(ab, kClassHeavy) && !AirBrakesOn(ab, kClassScout);
        ab.airBrakes = 1; // a scout ticked gets air
        bool overClass = AirBrakesOn(ab, kClassScout) && AirBrakesOn(ab, kClassTruck) && AirBrakesOn(ab, kClassHeavy);
        ab.airBrakes = 0; // a heavy truck unticked loses it
        overClass = overClass && !AirBrakesOn(ab, kClassHeavy) && !AirBrakesOn(ab, kClassTruck) && !AirBrakesOn(ab, kClassScout);
        // the order, by the class in force: own scales of the Scout class on a heavy truck by weight: off. No scales, SCOUT at
        // 20 t: off. No scales, OFFROAD at 5 t: on. No type and no weight: the plain row, Scout off and Truck on
        ab.airBrakes = -1;
        const bool order = !AirBrakesOn(ab, ClassInForce(kClassScout, kClassAuto, "HEAVY", 20000.0f)) && !AirBrakesOn(ab, ClassInForce(kClassAuto, kClassTruck, "SCOUT", 20000.0f)) &&
                           AirBrakesOn(ab, ClassInForce(kClassAuto, kClassScout, "OFFROAD", 5000.0f)) && !AirBrakesOn(ab, ClassInForce(kClassAuto, kClassScout, "", 0.0f)) &&
                           AirBrakesOn(ab, ClassInForce(kClassAuto, kClassTruck, "", 0.0f)) && !AirBrakesOn(ab, ClassInForce(kClassAuto, kClassTruck, "", 5000.0f)) &&
                           AirBrakesOn(ab, ClassInForce(kClassHeavy, kClassAuto, "SCOUT", 2000.0f));
        overClass = overClass && order;
        // the flip (M4): a hold of the spring brakes ends on the pass the truck turns out to have no air brakes, and the model holds no air
        AirModel flip = { true, 2.0f };
        const bool heldOn = SpringHeldStep(false, true, flip.running, flip.bar), heldOff = SpringHeldStep(heldOn, false, flip.running, flip.bar);
        TruckRaw still = {};
        AirStepFor(flip, false, still, 0.5f, 0.05f);
        const bool noAirLeft = !flip.running && flip.bar == 0.0f;
        AirStepFor(flip, true, still, 0.5f, 0.05f);
        const bool startsFull = flip.running && flip.bar > kSpringRelease && !SpringHeldStep(false, true, flip.running, flip.bar);
        overClass = overClass && heldOn && !heldOff && noAirLeft && startsFull;
        ab.airBrakes = 7;
        SettingsClamp(ab);
        const bool clamped = ab.airBrakes == -1;
        // the box: red, in As with dials and At my limits with the Layout at Off, in place of LOW BRAKE AIR; none at None; none with dials
        WarnFigures sf = {};
        sf.driving = true, sf.airKnown = true, sf.airBar = 2.5f, sf.springHeld = true;
        sf.clutch = sf.engine = WarnBox{ kWarnNone, "", "", 0 };
        auto boxes = [&](int layout, int choice, const WarnFigures &figs) {
            GaugeSettings ws;
            SettingsDefaults(ws);
            ws.layout = layout, ws.hiddenWarnings = choice;
            WarnMargins wm = {};
            return WarnListMake(ws, figs, wm);
        };
        auto holds = [](const WarnList &wl, int what) {
            for (int i = 0; i < kWarnBoxes; i++)
                if (wl.what[i] == what && wl.box[i].state != kWarnNone) return i;
            return -1;
        };
        const WarnList atLimits = boxes(kLayoutOff, kHwLimits, sf), asDials = boxes(kLayoutOff, kHwAsDials, sf), atNone = boxes(kLayoutOff, kHwNone, sf), withDials = boxes(kLayoutStrip, kHwLimits, sf);
        const int li = holds(atLimits, kWlSpring), di = holds(asDials, kWlSpring);
        const bool shown = li == 0 && di == 0 && atLimits.box[0].state == kWarnRed && !strcmp(atLimits.box[0].title, "SPRING BRAKES ON") && !strcmp(atLimits.box[0].line, "Wait for the tanks to fill") &&
                           !strcmp(atLimits.box[0].figure, "2.5 bar") && holds(atLimits, kWlAir) < 0 && holds(asDials, kWlAir) < 0;
        const bool notShown = holds(atNone, kWlSpring) < 0 && atNone.count == 0 && holds(withDials, kWlSpring) < 0;
        // not held: the low air box alone, as before; no air brakes: neither, whatever the figures say
        WarnFigures lowOnly = sf;
        lowOnly.springHeld = false, lowOnly.airBar = 5.0f;
        const WarnList lowList = boxes(kLayoutOff, kHwLimits, lowOnly);
        WarnFigures noAir = sf;
        noAir.airKnown = false;
        const WarnList without = boxes(kLayoutOff, kHwLimits, noAir);
        const bool others = holds(lowList, kWlAir) >= 0 && holds(lowList, kWlSpring) < 0 && holds(without, kWlAir) < 0 && holds(without, kWlSpring) < 0 && without.count == 0;
        if (!(byClass && overClass && clamped && shown && notShown && others)) printf("     byClass %d overClass %d clamped %d shown %d (%d %d) notShown %d others %d\n", byClass, overClass, clamped, shown, li, di, notShown, others);
        Check(byClass && overClass && clamped && shown && notShown && others,
              "air brakes by truck: with no choice every class but the scout has them, the choice overrules the class both ways; held, a red box SPRING BRAKES ON stands in place of LOW BRAKE AIR "
              "with the Layout at Off, not at None and not with dials; a truck without air brakes gets neither box");
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

        // over-revving (BRIEF_overrev_downshift.md): the rate over the governor's end at 1.1, the full one 0.2 over it
        {
            const float stop = 1.1f;
            Check(DamageOverRev(1.05f, stop, set) == 0.0f && DamageOverRev(1.1f, stop, set) == 0.0f && Near(DamageOverRev(1.2f, stop, set), 5.0f, 1e-4f) &&
                      Near(DamageOverRev(1.3f, stop, set), 10.0f, 1e-4f) && Near(DamageOverRev(1.6f, stop, set), 10.0f, 1e-4f) && DamageOverRev(NAN, stop, set) == 0.0f &&
                      DamageOverRev(1.3f, NAN, set) == 0.0f && DamageOverRev(1.3f, 0.0f, set) == 0.0f && DamageOverRev(INFINITY, stop, set) == 0.0f,
                  "over-rev: nothing at 1.05 or at the governor's end 1.1, half the rate at 1.2, the whole at 1.3 and 1.6, nothing for a share or an end that is no number");
            GaugeSettings revOff = set;
            revOff.overRevDamage = 0.0f;
            Check(DamageOverRev(1.3f, stop, revOff) == 0.0f, "over-rev: OverRevDamagePercentPerMinute 0 is off");
            // the grace: 0.4 s at 1.3 nothing, 0.6 s gives 0.1 s of points; a coolant not known does not stop it (S1)
            TempModel unknown = {};
            auto overFor = [&](float seconds, const GaugeSettings &g, const TempModel &tm, float share, float &owedOut, DamageModel &mOut) {
                DamageModel om = {};
                om.id = 1;
                int pts = 0;
                const int n = (int)(seconds / kDt + 0.5f);
                for (int step = 0; step < n; step++) pts += DamageStep(om, 1, c, tm, 1.0f, g, kDt, share, stop);
                owedOut = (float)pts + om.owed;
                mOut = om;
            };
            float owed4 = 0.0f, owed6 = 0.0f, owedMinute = 0.0f;
            DamageModel m4 = {}, m6 = {}, mMinute = {};
            overFor(0.4f, set, unknown, 1.3f, owed4, m4);
            overFor(0.6f, set, unknown, 1.3f, owed6, m6);
            overFor(60.5f, set, unknown, 1.3f, owedMinute, mMinute);
            const float perSecond = 10.0f / 100.0f * 150.0f / 60.0f; // an engine that takes 150 at 10 percent a minute
            Check(owed4 == 0.0f && m4.over == 0.0f && !DamageAdding(m4, set) && Near(owed6, 0.1f * perSecond, 0.002f) && Near(m6.over, 10.0f, 1e-4f) && DamageAdding(m6, set) &&
                      Near(owedMinute, 15.0f, 0.05f) && m6.hot == 0.0f,
                  "over-rev: none in the first half second at 1.3, 0.6 s gives 0.1 s of points, a minute past the grace costs 15 of 150; the coolant need not be known");
            // the clock falls to 0 at once: 0.4 s over, one step under, 0.4 s over again costs nothing
            DamageModel broken = {};
            broken.id = 1;
            int brokenPts = 0;
            for (int step = 0; step < 8; step++) brokenPts += DamageStep(broken, 1, c, unknown, 1.0f, set, kDt, 1.3f, stop);
            brokenPts += DamageStep(broken, 1, c, unknown, 1.0f, set, kDt, 1.05f, stop);
            const bool reset = broken.overFor == 0.0f;
            for (int step = 0; step < 8; step++) brokenPts += DamageStep(broken, 1, c, unknown, 1.0f, set, kDt, 1.3f, stop);
            Check(reset && brokenPts == 0 && broken.owed == 0.0f, "over-rev: the grace starts again after a step under the end, so two short overshoots cost nothing");
            // nothing where it does not count: no share (the clutch held, N, the cut not acting, A: no map went out), Damage
            // off, the setting at 0, the engine off
            float owedNone = 0.0f, owedOff = 0.0f, owedZero = 0.0f, owedStopped = 0.0f;
            DamageModel mNone = {}, mOff = {}, mZero = {}, mStopped = {};
            overFor(5.0f, set, unknown, NAN, owedNone, mNone);
            GaugeSettings damageOff = set;
            damageOff.damage = false;
            overFor(5.0f, damageOff, unknown, 1.3f, owedOff, mOff);
            overFor(5.0f, revOff, unknown, 1.3f, owedZero, mZero);
            {
                DamageModel om = {};
                om.id = 1;
                int pts = 0;
                for (int step = 0; step < 300; step++) pts += DamageStep(om, 1, stopped, unknown, 1.0f, set, kDt, 1.3f, stop);
                owedStopped = (float)pts + om.owed;
                mStopped = om;
            }
            Check(owedNone == 0.0f && mNone.overFor == 0.0f && owedOff == 0.0f && Near(mOff.over, 10.0f, 1e-4f) && !DamageAdding(mOff, damageOff) && owedZero == 0.0f &&
                      owedStopped == 0.0f && mStopped.overFor == 0.0f,
                  "over-rev: nothing without a share, with Damage off (the rate still worked out), at 0, or with the engine off");
            // the share (M1): the truck's own speed held to the wheels' mean; the low range with the wheels at 1.5 of the
            // top and the truck at 0.9 costs nothing, the truck at 1.3 the full rate; without the truck's speed the mean
            const float top = 2.74f;
            const float lowSpin = OverRevShare(1.5f * top, 0.9f * top, top), lowRun = OverRevShare(1.5f * top, 1.3f * top, top), dragged = OverRevShare(1.0f * top, 1.4f * top, top),
                        noTruck = OverRevShare(1.25f * top, NAN, top);
            Check(Near(lowSpin, 0.9f, 1e-5f) && DamageOverRev(lowSpin, stop, set) == 0.0f && Near(lowRun, 1.3f, 1e-5f) && Near(DamageOverRev(lowRun, stop, set), 10.0f, 1e-3f) &&
                      Near(dragged, 1.0f, 1e-5f) && Near(noTruck, 1.25f, 1e-5f) && isnan(OverRevShare(3.0f, 3.0f, 0.0f)) && isnan(OverRevShare(NAN, 3.0f, top)),
                  "over-rev: the share is the truck's own speed held to the wheels' mean, so wheels spinning at 1.5 of the top under a truck at 0.9 cost nothing and a truck run to 1.3 the whole rate; "
                  "wheels dragged under the truck go by the wheels; without the truck's speed the mean");
            // the cause: the largest of the three rates, and its words
            DamageModel mixed = {};
            mixed.hot = 2.0f;
            mixed.over = 10.0f;
            DamageModel hotter = mixed;
            hotter.hot = 12.0f;
            DamageModel coldOnly = {};
            coldOnly.cold = 1.0f;
            char said[160], heat[160], cold[160];
            DamageCauseText(3, 90.0f, 12.0f, 3120, "L-", said, sizeof said);
            DamageCauseText(1, 121.5f, 3.0f, 0, nullptr, heat, sizeof heat);
            DamageCauseText(2, 10.0f, 1.5f, 0, nullptr, cold, sizeof cold);
            Check(DamageCause(mixed) == 3 && DamageCause(hotter) == 1 && DamageCause(coldOnly) == 2 && DamageCause(DamageModel{}) == 0 &&
                      !strcmp(said, "over-revving at 3120 rpm in L- (12.0 percent a minute)") && !strcmp(heat, "the heat at 121.5 C (3.0 percent a minute)") &&
                      !strcmp(cold, "a cold engine worked hard at 10.0 C (1.5 percent a minute)"),
                  "over-rev: the cause is the largest of the three rates, over-revving said with its rpm and position");
        }

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
        // no damage and the mod's factor of 0.95 handed: the factor read carries it, so as read; not given the factor, not (part 195.1)
        TruckRaw fresh = p;
        fresh.damage = 0;
        TruckEngineEffect(fresh.damage, fresh.damageCapacity, record, fresh.damageEffect, fresh.torqueFactor);
        fresh.torqueFactor *= 0.95f;
        DamageProbeText(fresh, record, wear, set, line, sizeof line, 0.95f);
        const bool handedOk = strstr(line, "(as read)") != nullptr;
        DamageProbeText(fresh, record, wear, set, line, sizeof line);
        const bool handedNot = strstr(line, "(NOT as read)") != nullptr;
        DamageProbeText(p, EngineRecord{}, wear, shown, line, sizeof line);
        Check(asRead && notAsRead && handedOk && handedNot && strstr(line, "looked up once the truck is driven") && strstr(line, "Damage off"),
              "damage: the probe's line carries the engine's damage, what the game's rule makes of it, the record and the model's rates");
    }

    // the boost
    {
        GaugeSettings bs;
        SettingsDefaults(bs);
        const float truckMost = GaugeScaleOf(bs).boost;
        bs.tachClass = bs.scaleClass = kClassScout;
        const float scoutMost = GaugeScaleOf(bs).boost;
        bs.tachClass = bs.scaleClass = kClassHeavy;
        const float heavyMost = GaugeScaleOf(bs).boost;
        bs.boostMax = 1.35f;
        Check(truckMost == 1.8f && scoutMost == 1.0f && heavyMost == 2.0f && GaugeScaleOf(bs).boost == 1.35f,
              "boost: trucks reach 1.8 bar, scouts 1, heavy trucks 2, and a number of the settings takes the class's place");
        const float reach = 1.8f;
        // steps the truck's boost for a while at n of the red zone's rpm; returns the largest change in one step
        BoostModel bm = {};
        int sounds = 0;
        auto blow = [&](TruckRaw &truck, float n, float seconds) {
            float largest = 0.0f;
            for (float at = 0.0f; at < seconds - 0.001f; at += kDt)
            {
                const float before = bm.bar;
                if (Blow(bm, truck, n, kDt) != kLiftNone) sounds++;
                largest = fmaxf(largest, fabsf(bm.bar - before));
            }
            return largest;
        };
        TruckRaw bt = Truck();
        Drive(bt, 3, true, 0.0f, 0.0f);
        blow(bt, 650.0f / 2200.0f, 2.0f);
        const float idling = bm.bar;
        Drive(bt, 3, true, 0.9f, 1.0f);
        const float step = blow(bt, 0.8f, 4.0f);
        const float floored = bm.bar;
        bt.flags = 0;
        blow(bt, 0.8f, 10.0f);
        Check(fabsf(idling) < 0.005f && Near(floored, reach, 0.02f) && step < 0.2f * reach && fabsf(bm.bar) < 0.01f && sounds == 0,
              "boost: none at idle; floored at 0.8 of the red zone's rpm all of it, without a jump; none with the engine off, and no sound without a lift");
        // lifts of the pedal
        auto full = [&]() {
            bt = Truck();
            bm = BoostModel{};
            sounds = 0;
            Drive(bt, 3, true, 0.9f, 1.0f);
            blow(bt, 0.8f, 6.0f);
        };
        full();
        Drive(bt, 3, true, 0.9f, 0.0f);
        const int first = Blow(bm, bt, 0.8f, kDt);
        const float loud = bm.lastVolume, at = bm.lastBar;
        blow(bt, 0.8f, 0.4f);
        const float fallen = bm.bar;
        Drive(bt, 3, true, 0.9f, 1.0f);
        blow(bt, 0.8f, 0.1f);
        Drive(bt, 3, true, 0.9f, 0.0f);
        blow(bt, 0.8f, 0.2f);
        Check(first == kLiftBlowOff && Near(loud, 1.0f, 0.01f) && Near(at, reach, 0.02f) && fallen < 0.8f * reach && fallen > 0.4f * reach && sounds == 0 && bm.blowOffs == 1,
              "lift: the foot off a floored pedal at full boost is a blow-off at full sound, the boost winds down with the rotor, and a second lift right after is silent");
        full();
        Drive(bt, 3, true, 0.9f, 0.7f);
        blow(bt, 0.8f, 0.2f);
        const bool smallLift = sounds == 1 && bm.flutters == 1 && bm.blowOffs == 0;
        full();
        for (int i = 1; i <= 4; i++)
        {
            Drive(bt, 3, true, 0.9f, 1.0f - 0.15f * (float)i);
            blow(bt, 0.8f, kDt);
        }
        const bool quick = sounds == 1 && bm.blowOffs == 1 && bm.flutters == 0;
        full();
        for (int i = 1; i <= 40; i++)
        {
            Drive(bt, 3, true, 0.9f, 1.0f - 0.025f * (float)i);
            blow(bt, 0.8f, kDt);
        }
        blow(bt, 0.8f, 1.0f);
        Check(smallLift && quick && sounds == 0, "lift: a small lift flutters, the pedal let up over 0.2 s is a blow-off, and one let up over two seconds makes no sound");
        bm = BoostModel{};
        bm.running = true;
        bm.limit = 1.0f;
        bm.bar = 0.15f * reach;
        bt = Truck();
        Drive(bt, 3, true, 0.9f, 1.0f);
        Blow(bm, bt, 0.0f, kDt);
        bm.bar = 0.15f * reach;
        Drive(bt, 3, true, 0.9f, 0.0f);
        const int coming = Blow(bm, bt, 0.0f, kDt), weak = Blow(bm, bt, 0.0f, kDt);
        const float quiet = bm.lastVolume;
        bm = BoostModel{};
        bm.running = true;
        bm.limit = 1.0f;
        Drive(bt, 3, true, 0.9f, 1.0f);
        Blow(bm, bt, 0.0f, kDt);
        bm.bar = 0.05f * reach;
        Drive(bt, 3, true, 0.9f, 0.0f);
        Check(coming == kLiftNone && weak == kLiftFlutter && quiet > 0.0f && quiet < 0.1f && Blow(bm, bt, 0.0f, kDt) == kLiftNone && Blow(bm, bt, 0.0f, kDt) == kLiftNone,
              "lift: with little boost a full lift only flutters, quietly, and with next to none it is silent");
        full();
        Drive(bt, 3, true, 0.9f, 0.0f);
        Blow(bm, bt, 0.8f, kDt);
        char line[512];
        BoostProbeText(bt, bm, TurboOf(kClassTruck, kSpoolStandard, reach, kFuelClass), line, sizeof line);
        Check(strstr(line, "of the turbo's 1.80") && strstr(line, "| n 0.80 of the red zone's rpm, pedal 0.00, fuel 0.00, full load 1.80 bar there | the limit ") &&
                  strstr(line, "a standard turbo, diesel (n 0.36 to 0.60, spool 0.045 s)") &&
                  strstr(line, "1 blow-offs, 0 flutters, the last a blow-off at 1.80 bar, 1.00 of its full sound"),
              "boost: the probe's line carries the boost, the rotor, what they go by, the limit, the turbo and the lifts");
    }

    // the probe's line
    t = Truck();
    Drive(t, 2, true, 0.5f, 1.0f);
    m = TachModel{};
    TachStep(m, t, k, kDt);
    char text[512];
    ModelProbeText(t, m, r, k, text, sizeof text);
    Check(strstr(text, "wheels 8.50 rad/s of the gear's 17.00") && strstr(text, "pedal 1.00") && strstr(text, "l/min by the game's rule"), "probe: the models' line carries their numbers");

    // the needle in the slip room (BRIEF_drive_1112_findings.md part 1 with its addendum)
    {
        const float top = 8.0f, ceil = 20.0f, stop = TachStop(k), red = k.redline;
        // M1: under the top and at it, the clutch model's own figure to the bit
        const bool under = SlipRoomRpm(0.5f * top, top, ceil, k) == 0.5f * top / top * red && SlipRoomRpm(top, top, ceil, k) == top / top * red &&
                           SlipRoomRpm(0.0f, top, ceil, k) == 0.0f && SlipRoomRpm(-2.0f, top, ceil, k) == -2.0f / top * red;
        const bool middle = Near(SlipRoomRpm(0.5f * (top + ceil), top, ceil, k), 0.5f * (red + stop), 0.01f);
        const bool atStop = SlipRoomRpm(ceil, top, ceil, k) == stop && SlipRoomRpm(2.0f * ceil, top, ceil, k) == stop;
        // no usable ceiling: as before, the share, which TachFollow holds at the stop
        const float past = 1.5f * top;
        const bool none = SlipRoomRpm(past, top, 0.0f, k) == past / top * red && SlipRoomRpm(past, top, NAN, k) == past / top * red &&
                          SlipRoomRpm(past, top, top, k) == past / top * red && SlipRoomRpm(past, top, 0.5f * top, k) == past / top * red &&
                          SlipRoomRpm(past, top, INFINITY, k) == past / top * red && past / top * red > stop && SlipRoomRpm(past, 0.0f, ceil, k) == 0.0f;
        bool roomRising = true;
        float last = -1.0f;
        for (float wheels = 0.0f; wheels <= 1.2f * ceil; wheels += 0.05f)
        {
            const float n = SlipRoomRpm(wheels, top, ceil, k);
            if (n < last || n > stop) roomRising = false;
            last = n;
        }
        // a reverse gear: the wheels come in the gear's direction, so the same law (the top of a reverse gear is its own)
        TruckRaw rv = Truck();
        Drive(rv, -1, false, 1.5f, 1.0f);
        const float rTop = TruckGearTop(rv, -1), rSpin = TruckDrivenSpinGeared(rv);
        const bool reverse = rTop > 0.0f && Near(rSpin, 1.5f * rTop, 0.001f) && Near(SlipRoomRpm(rSpin, rTop, 2.0f * rTop, k), 0.5f * (red + stop), 0.5f);
        Check(under && middle && atStop && none && roomRising && reverse,
              "slip room: under the gear's top the needle's figure is the clutch's to the bit, past it it goes from the red zone's start to the stop by the wheels' place under the ceiling, and with no usable ceiling as before");

        // the fall and the rise through TachFollow: the wheels from the ceiling to the top over two seconds, pedal 0
        TruckRaw g = Truck();
        Drive(g, 2, false, 1.0f, 0.0f);
        TachModel fall = {};
        for (float at = 0.0f; at < 2.0f; at += kDt) TachFollow(fall, g, k, SlipRoomRpm(ceil, top, ceil, k), kDt);
        const bool startsAtStop = Near(fall.rpm, stop, 1.0f);
        bool falls = true, pinnedBefore = true;
        TachModel old = fall;
        float was = fall.rpm;
        int step = 0;
        for (float at = 0.0f; at < 2.0f; at += kDt, step++)
        {
            const float wheels = ceil + (top - ceil) * (at + kDt) / 2.0f;
            TachFollow(fall, g, k, SlipRoomRpm(wheels, top, ceil, k), kDt);
            TachFollow(old, g, k, wheels / top * red, kDt); // before: the clutch model's engine
            if (!(fall.rpm < was)) falls = false;
            if (at < 1.5f && !Near(old.rpm, stop, 1.0f)) pinnedBefore = false;
            was = fall.rpm;
        }
        const bool endsAtRed = fall.rpm < red + 0.35f * (stop - red) && fall.rpm >= red - 1.0f;
        TachModel rise = fall;
        bool rises = true;
        for (float at = 0.0f; at < 2.0f; at += kDt)
        {
            const float needleWas = rise.rpm, wheels = top + (ceil - top) * (at + kDt) / 2.0f;
            TachFollow(rise, g, k, SlipRoomRpm(wheels, top, ceil, k), kDt);
            if (rise.rpm < needleWas || rise.rpm > stop + 0.01f) rises = false;
        }
        Check(startsAtStop && falls && pinnedBefore && endsAtRed && rises && rise.rpm > fall.rpm,
              "slip room: the needle falls at every step as the wheels fall from the ceiling to the gear's top, where it stood at the stop before, and rises with them, never over the stop");

        // M2: the ceiling only of the driven truck and only fresh
        Check(SlipRoomCeiling(ceil, 0x1000, 0x1000, true) == ceil && SlipRoomCeiling(ceil, 0x2000, 0x1000, true) == 0.0f && SlipRoomCeiling(ceil, 0x1000, 0x1000, false) == 0.0f &&
                  SlipRoomCeiling(ceil, 0, 0, true) == 0.0f && SlipRoomCeiling(NAN, 0x1000, 0x1000, true) == 0.0f && SlipRoomCeiling(0.0f, 0x1000, 0x1000, true) == 0.0f &&
                  SlipRoomRpm(past, top, SlipRoomCeiling(ceil, 0x2000, 0x1000, true), k) == past / top * red,
              "slip room: a ceiling of another vehicle or one not fresh is not used, and the needle then goes as before");

        // M3: the over-rev's rate is the same whatever the needle shows, and the clutch model's engine is not the law's
        GaugeSettings set;
        SettingsDefaults(set);
        TempModel warm = {};
        DamageModel atStopWear = {}, inRoomWear = {};
        TruckRaw c = Truck();
        Drive(c, 2, false, 1.0f, 1.0f);
        for (float at = 0.0f; at < 30.0f; at += kDt)
        {
            DamageStep(atStopWear, 1, c, warm, stop / red, set, kDt, 1.4f, 1.1f);
            DamageStep(inRoomWear, 1, c, warm, SlipRoomRpm(0.5f * (top + ceil), top, ceil, k) / red, set, kDt, 1.4f, 1.1f);
        }
        TorqueCurve curve = kClassCurves[kClassTruck];
        ClutchModel cm = {};
        TruckRaw fast = Truck();
        Drive(fast, 2, false, 1.5f, 1.0f);
        for (float at = 0.0f; at < 2.0f; at += kDt) ClutchStep(cm, fast, k, curve, 0.0f, 1.0f, 1.0f, false, kDt);
        const float engine = cm.rpm, fTop = TruckGearTop(fast, 2);
        const float shown = SlipRoomRpm(cm.input / k.redline * fTop, fTop, 2.0f * fTop, k);
        Check(atStopWear.over > 0.0f && atStopWear.over == inRoomWear.over && cm.locked && cm.rpm == engine && Near(engine, 1.5f * red, 1.0f) && Near(shown, 0.5f * (red + stop), 0.5f),
              "slip room: the over-rev's rate is the same at either needle, and the clutch model's engine stays the wheels' share while the needle shows the room");

        // the probe's line names the rule
        NeedleRule room = { kNeedleSlipRoom, 2050.0f, 12.0f, 8.0f, 20.0f }, clutch = { kNeedleClutch, 1500.0f };
        char roomText[700], clutchText[700];
        ModelProbeText(t, m, r, k, roomText, sizeof roomText, &room);
        ModelProbeText(t, m, r, k, clutchText, sizeof clutchText, &clutch);
        Check(strstr(roomText, "heading for 2050 by the slip room: the wheels 12.00 rad/s between the gear's top 8.00 and the ceiling 20.00 (pedal") &&
                  strstr(clutchText, "heading for 1500 by the clutch (pedal") && strstr(text, " by the game's gears (pedal") && strstr(roomText, "measured standing"),
              "slip room: the probe's line says what the needle heads for and by which rule, with the wheels, the top and the ceiling in the room");
    }

    // what a hook kept, in a probe's line (BRIEF_drive_1112_findings.md part 4, M1 and M2)
    {
        // two trucks in turn: eight wheels written on the first, then a four-wheel truck with no written step yet, then its own
        char eight[96], other[96], four[96], stood[96], noTruck[96];
        LockWheelsText(eight, sizeof eight, 8, 0x1000, 0x1000, true);
        LockWheelsText(other, sizeof other, 8, 0x1000, 0x2000, true);
        LockWheelsText(four, sizeof four, 4, 0x2000, 0x2000, true);
        LockWheelsText(stood, sizeof stood, 4, 0x2000, 0x2000, false);
        LockWheelsText(noTruck, sizeof noTruck, 8, 0, 0, true);
        Check(!strcmp(eight, "8 driven wheels") && !strcmp(other, "no written step yet for this truck") && !strstr(other, "8") && !strcmp(four, "4 driven wheels") &&
                  !strcmp(stood, "4 driven wheels at the last written step, none written in this second") && !strcmp(noTruck, "no written step yet for this truck"),
              "kept counts: the lock's wheel count is printed only for the driven truck, a four-wheel truck after an eight-wheel one reads none yet and then four");
        char none[200], foreign[200], read[200], part[200];
        CutGovernorText(none, sizeof none, NAN, NAN, NAN, 0, 0, 0, 0, 0x2000);
        CutGovernorText(foreign, sizeof foreign, 3.5f, 0.6f, 0.62f, 8, 0, 0, 0x1000, 0x2000);
        CutGovernorText(read, sizeof read, 3.5f, 0.6f, 0.62f, 4, 0, 0, 0x2000, 0x2000);
        CutGovernorText(part, sizeof part, NAN, NAN, 0.62f, 0, 4, 0, 0x2000, 0x2000);
        Check(!strstr(none, "nan") && strstr(none, "not read yet for this truck") && !strstr(foreign, "8 by the cylinder") && strstr(foreign, "not read yet for this truck") &&
                  !strcmp(read, "3.50 rad/s (the rolling radius 0.600 m, wheel + 0x94 0.620 m; 4 by the cylinder, 0 without, 0 out of bounds)") && !strstr(part, "nan") &&
                  strstr(part, "not read (the rolling radius not read, wheel + 0x94 0.620 m; 0 by the cylinder, 4 without") && CutAnswerOurs(0x2000, 0x2000) &&
                  !CutAnswerOurs(0x1000, 0x2000) && !CutAnswerOurs(0, 0),
              "kept counts: the governor's part of the cut's line has no nan before the first answer and none of another truck's figures; the last answer counts only where it is the driven truck's");
    }

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
        // a diesel's lift (the author's drive of 8ED18842: the whistle made by code went): it asks for the whistle's
        // tier, which holds only the player's own files, so without them nothing plays; their pitch goes by the rotor.
        // DieselFlutter on, it flutters; a petrol engine's lift as before
        LiftSound lifted, whistled, slowWhistle, fluttered, petrolLift;
        const bool whistle = LiftSoundOf(kLiftBlowOff, 0.8f, 50, 0.0f, lifted) && LiftSoundFor(false, false, kLiftBlowOff, 0.8f, 1.0f, 50, 0.0f, whistled) &&
                             whistled.tier == kSoundWhistle && Near(whistled.gain, lifted.gain, 1e-5f) && Near(whistled.pitch, 1.0f, 1e-5f) &&
                             WhistleSoundOf(kLiftFlutter, 0.8f, 0.2f, 50, 0.0f, slowWhistle) && Near(slowWhistle.pitch, kWhistleSlowest, 1e-5f) &&
                             !WhistleSoundOf(kLiftNone, 0.8f, 1.0f, 50, 0.0f, slowWhistle) && !WhistleSoundOf(kLiftBlowOff, 0.8f, 1.0f, 0, 0.0f, slowWhistle);
        const bool flutterOn = LiftSoundFor(false, true, kLiftBlowOff, 0.8f, 1.0f, 50, 0.0f, fluttered) && fluttered.tier == lifted.tier &&
                               Near(fluttered.gain, lifted.gain, 1e-5f) && Near(fluttered.pitch, lifted.pitch, 1e-5f);
        const bool petrolSame = LiftSoundFor(true, false, kLiftBlowOff, 0.8f, 1.0f, 50, 0.0f, petrolLift) && petrolLift.tier == lifted.tier &&
                                Near(petrolLift.gain, lifted.gain, 1e-5f) && LiftSoundFor(true, true, kLiftFlutter, 0.2f, 1.0f, 50, 0.0f, petrolLift) && petrolLift.tier == kTurboLow;
        SoundSet noWhistle, flutters;
        SoundLoad(kSoundWhistle, L"C:\\no such folder\\", noWhistle);
        SoundLoad(kTurboMid, L"C:\\no such folder\\", flutters);
        // own files: a short tone written as sounds\turbo_whistle_1.wav in a folder of the test's
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        const std::wstring own = std::wstring(temp) + L"gauges_whistle_test\\";
        CreateDirectoryW(own.c_str(), nullptr);
        CreateDirectoryW((own + L"sounds").c_str(), nullptr);
        {
            const uint32_t rate = 22050, n = rate / 2;
            std::vector<uint8_t> wav(44 + n * 2);
            auto put = [&](size_t at, uint32_t value, int bytes) { for (int i = 0; i < bytes; i++) wav[at + i] = (uint8_t)(value >> (8 * i)); };
            memcpy(&wav[0], "RIFF", 4);
            put(4, (uint32_t)wav.size() - 8, 4);
            memcpy(&wav[8], "WAVEfmt ", 8);
            put(16, 16, 4);
            put(20, 1, 2);
            put(22, 1, 2);
            put(24, rate, 4);
            put(28, rate * 2, 4);
            put(32, 2, 2);
            put(34, 16, 2);
            memcpy(&wav[36], "data", 4);
            put(40, n * 2, 4);
            for (uint32_t i = 0; i < n; i++) put(44 + i * 2, (uint32_t)(uint16_t)(int16_t)(8000.0 * sin(2.0 * 3.14159265 * 3000.0 * i / rate)), 2);
            FILE *f = nullptr;
            if (_wfopen_s(&f, (own + L"sounds\\turbo_whistle_1.wav").c_str(), L"wb") == 0 && f)
            {
                fwrite(wav.data(), 1, wav.size(), f);
                fclose(f);
            }
        }
        SoundSet owned;
        SoundLoad(kSoundWhistle, own, owned);
        DeleteFileW((own + L"sounds\\turbo_whistle_1.wav").c_str());
        RemoveDirectoryW((own + L"sounds").c_str());
        RemoveDirectoryW(own.c_str());
        Check(whistle && flutterOn && petrolSame && noWhistle.count == 0 && flutters.count == 1 && owned.count == 1,
              "sound: a diesel's lift asks for the whistle, which holds no sound of the mod's, so it is quiet; own turbo_whistle files play there, slower "
              "with the rotor slower; with DieselFlutter on it flutters, and a petrol engine's lift is as before");
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
            const int lift = Blow(bm, bt, 1.0f, kDt);
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

    // the automatic's model in a position of the player's own (held): it learns the wheels' grip in a held 4th that
    // grips, so the lift's x follows the truck's speed past kLiftBlind and the factor at 0.8 of the top is above 1 and
    // under the roof; in a held gear floored in mud with the wheels steadily at twice the ground's speed the grip never rises (it
    // does in A), the lift's x stays at the ground's share, and Back to A holds by the wheelspin, floored or with the foot
    // under 0.7 (A's gear the held one, so the pedal holds nothing of its own)
    {
        const float dt = 0.05f;
        TruckRaw t = Truck();
        auto drive = [&](int gear, float share, float pedal, float slip) {
            Drive(t, gear, false, share, pedal);
            t.speed /= slip;
        };
        const GaugeScale &k = kClassScales[kClassTruck];
        GearboxModel held = {};
        drive(4, 0.8f, 0.3f, 1.0f);
        for (int i = 0; i < 400; i++) GearboxStep(held, t, k, 0.3f, 0.3f, dt, EngineHand{}, true);
        const float top = TruckGearTop(t, 4), x = LiftShare(TruckDrivenSpin(t), t.speed, held.grip, top);
        const float roof = LiftRoof(TorqueRoof(kClassTruck), 0.8f), factor = TorqueLift(1.0f, 1.0f, 1.0f, 1.0f, 0.8f, roof);
        const bool learned = held.grip > 0.0f && Near(held.grip, 1.0f / 0.45f, 0.05f) && x > kLiftBlind + 0.1f && factor > 1.0f && factor <= roof;
        // the held 4th floored in mud (fast enough for the grip to be read): the wheels steadily at twice the ground's speed
        GearboxModel mud = {}, mudA = {}, known = held;
        drive(4, 0.9f, 1.0f, 2.0f);
        for (int i = 0; i < 1200; i++)
        {
            GearboxStep(mud, t, k, 1.0f, 1.0f, dt, EngineHand{}, true);
            GearboxStep(mudA, t, k, 1.0f, 1.0f, dt);
            GearboxStep(known, t, k, 1.0f, 1.0f, dt, EngineHand{}, true);
        }
        const float ground = known.grip * t.speed * kSlipUp / TruckGearTop(t, 4), mudX = LiftShare(TruckDrivenSpin(t), t.speed, known.grip, TruckGearTop(t, 4));
        BackInputs bi = {};
        bi.pos = kPosGear;
        bi.heldGear = bi.autoGear = 4;
        bi.pedal = 1.0f;
        bi.grip = known.grip;
        bi.spin = known.spin;
        bi.speed = t.speed;
        bi.pitchKnown = bi.engineOn = true;
        BackModel bm = {};
        bool back = false;
        for (int i = 0; i < 400; i++) back = back || BackToAStep(bm, 5, i == 0, bi, dt);
        const int byPedal = bm.why;
        bi.pedal = 0.5f;
        bm = BackModel{};
        for (int i = 0; i < 400; i++) back = back || BackToAStep(bm, 5, i == 0, bi, dt);
        const bool mudHeld = mud.grip == 0.0f && mudA.grip > 0.0f && known.grip <= held.grip + 0.001f && Near(mudX, ground, 0.01f) && !back &&
                             byPedal == kBackSpin && bm.why == kBackSpin; // A's gear the held one: floored is no hold of its own
        printf("     held gear: grip %.3f, x %.3f factor %.3f roof %.3f | mud %.3f A %.3f known %.3f, x %.3f of %.3f, back %d, by %d then %d\n", held.grip, x, factor, roof,
               mud.grip, mudA.grip, known.grip, mudX, ground, back, byPedal, bm.why);
        Check(learned && mudHeld, "gearbox: in a held position the model learns the grip (the lift's x past 0.5, the factor at 0.8 of the top above 1 and under the roof); "
                                  "steady wheelspin floored never raises it there (it does in A), and Back to A holds by the wheelspin, floored or not");
    }

    // a held gear's lift by the wheels' own speed (the held gear power brief): spinning in mud at 0.8 of the top with the
    // ground at 0 the factor is the curve over GameFall, under LiftRoof and kSpinLiftMost, not 1; when the wheels catch
    // (0.85 down to 0.3 of the top in 0.2 s) and in a chatter (spin and catch every second pass for 2 s) the factor
    // written on a pass times GameFall at the next pass's x, the torque the game gives until the next write, never
    // passes kSpinLiftMost
    {
        // the lift as the pass writes it in a held gear: the class's roof with its fade, then the cap
        auto byspin = [&](float x) { return fminf(TorqueLift(1.0f, 1.0f, 1.0f, 1.0f, x, TorqueRoof(kClassTruck)), kSpinLiftMost); };
        const float mud = byspin(SpinShare(-0.8f * 20.0f, 20.0f)), mud9 = byspin(0.9f);
        const bool inMud = Near(mud, kSpinLiftMost, 0.001f) && Near(mud9, fminf(1.0f / GameFall(0.9f), SpinRoof(TorqueRoof(kClassTruck), 0.9f)), 0.001f) &&
                           Near(SpinRoof(TorqueRoof(kClassTruck), 0.8f), kSpinLiftMost, 0.001f) && mud9 > 1.0f;
        float worst = 0.0f;
        auto run = [&](const float *xs, int n) {
            for (int i = 0; i + 1 < n; i++)
            {
                const float written = byspin(xs[i]);
                worst = fmaxf(worst, written * GameFall(xs[i + 1]));
            }
        };
        const float caught[6] = { 0.85f, 0.85f, 0.7f, 0.55f, 0.4f, 0.3f };
        run(caught, 6);
        float chatter[40];
        for (int i = 0; i < 40; i++) chatter[i] = i % 2 ? 0.3f : 0.85f;
        run(chatter, 40);
        char line[200];
        snprintf(line, sizeof line, "lift by spin: in mud at 0.8 of the top %.2f (the cap, not 1); the catch and the chatter give the game at most %.2f of the engine's torque "
                                    "between writes (kSpinLiftMost %.1f)", mud, worst, kSpinLiftMost);
        Check(inMud && worst <= kSpinLiftMost + 0.001f && worst > 1.0f, line);
    }

    // the rpm sway in a held low gear (BRIEF_low_gear_sway.md with the Reviewer's addendum): a heavy truck held by hand
    // at full pedal against a drag that rises with speed, the wheels read with +-0.15 rad/s of noise a pass. 0.18.4's
    // locked path (this pass's spin, the cap of 2 cut over the top's last 0.06, a rise paced after any cut) cycles near
    // a second with the factor swinging over 0.8; now (the mean, the cap fading over the wider of the top's last fifth and
    // 0.8 rad/s, the pace only after a break loose) no cycle, a pass's change under 0.1, the wheels within 0.05 rad/s and
    // under the top; without the noise, settled, the factor holds within 0.1. The same in a gear with a 10 rad/s top
    {
        const SwayStats oldLow = SwayRun(true, 1, 0.133f, 0.0f, 1.5f, 0.02f, 10.0f, 6.0f), nowLow = SwayRun(false, 1, 0.133f, 0.0f, 1.5f, 0.02f, 10.0f, 6.0f),
                        quietLow = SwayRun(false, 1, 0.133f, 0.0f, 1.5f, 0.02f, 10.0f, 6.0f, 0.0f);
        const SwayStats oldTen = SwayRun(true, 2, 1.0f, 2.5f, 0.02f, 0.002f, 10.0f, 6.0f), nowTen = SwayRun(false, 2, 1.0f, 2.5f, 0.02f, 0.002f, 10.0f, 6.0f),
                        quietTen = SwayRun(false, 2, 1.0f, 2.5f, 0.02f, 0.002f, 10.0f, 6.0f, 0.0f);
        char line[600];
        snprintf(line, sizeof line,
                 "sway: L- (a %.2f rad/s top) held at full pedal: 0.18.4's factor %.2f to %.2f with %d cuts in 6 s (a pass's change up to %.2f); now %.2f to %.2f, "
                 "no cut, a pass's change up to %.3f, the wheels %.3f to %.3f rad/s; without noise %.3f to %.3f. A 10 rad/s top: 0.18.4's %.2f to %.2f with %d "
                 "cuts, now %.2f to %.2f, no cut, up to %.3f a pass; without noise %.3f to %.3f",
                 oldLow.top, oldLow.factorLo, oldLow.factorHi, oldLow.cycles, oldLow.step, nowLow.factorLo, nowLow.factorHi, nowLow.step, nowLow.spinLo, nowLow.spinHi,
                 quietLow.factorLo, quietLow.factorHi, oldTen.factorLo, oldTen.factorHi, oldTen.cycles, nowTen.factorLo, nowTen.factorHi, nowTen.step, quietTen.factorLo,
                 quietTen.factorHi);
        Check(oldLow.cycles >= 4 && oldLow.factorHi - oldLow.factorLo > 0.8f && !nowLow.cycles && nowLow.step < 0.1f && nowLow.spinHi - nowLow.spinLo < 0.05f &&
                  nowLow.spinHi < nowLow.top && quietLow.factorHi - quietLow.factorLo < 0.1f && oldTen.cycles >= 2 && !nowTen.cycles && nowTen.step < 0.1f &&
                  nowTen.spinHi - nowTen.spinLo < 0.05f && nowTen.spinHi < nowTen.top && quietTen.factorHi - quietTen.factorLo < 0.1f,
              line);
    }
    // the locked path's pace (the Reviewer's addendum point 3): the wheels at 0.9 of an L- top, then broken loose at twice
    // the top for three passes, then caught at half of it: the break loose opens the window, the fall is written at once
    // and the rise as they catch climbs at kLiftRise; a fall along the fade (the wheels creeping to 0.99 of the top) opens
    // nothing, so the climb back is written at once; the ground path's cut rule is unchanged
    {
        const float top = 1.8f, dt = 0.05f;
        auto liftAt = [&](const SpinLift &l) { return fminf(TorqueLift(0.9f, 1.0f, 1.0f, 1.0f, l.x, TorqueRoof(kClassHeavy)), l.cap); };
        SpinMean m = {};
        TorqueHand h = {};
        int looseAt = -1;
        float fallAt = 0.0f, worstRise = 0.0f;
        bool held = false;
        for (int i = 0; i < 60; i++)
        {
            const float spin = i < 20 ? 0.9f * top : i < 23 ? 2.0f * top : 0.5f * top;
            const SpinLift l = SpinLiftStep(m, spin, top, 1, 0x5000);
            const float before = h.written;
            TorqueHandStep(h, true, 0x5000, 0x4000, h.on ? h.written : 1.0f, liftAt(l), dt, 1, true, l.loose);
            if (l.loose && looseAt < 0) looseAt = i, fallAt = h.written;
            if (i > 23) worstRise = fmaxf(worstRise, h.written - before), held = held || h.heldBack;
        }
        const bool caught = looseAt == 20 && Near(fallAt, 1.0f, 0.01f) && held && worstRise <= kLiftRise * dt + 0.0001f;
        SpinMean fm = {};
        TorqueHand fh = {};
        bool creptHeld = false;
        float creptRise = 0.0f;
        for (int i = 0; i < 80; i++)
        {
            const float share = i < 20 ? 0.8f : i < 40 ? 0.8f + 0.19f * (i - 20) / 20.0f : 0.75f;
            const SpinLift l = SpinLiftStep(fm, share * top, top, 1, 0x5000);
            const float before = fh.written;
            TorqueHandStep(fh, true, 0x5000, 0x4000, fh.on ? fh.written : 1.0f, liftAt(l), dt, 1, true, l.loose);
            creptHeld = creptHeld || fh.heldBack || l.loose;
            if (i >= 40) creptRise = fmaxf(creptRise, fh.written - before);
        }
        TorqueHand gh = {};
        TorqueHandStep(gh, true, 0x5000, 0x4000, 1.0f, 2.0f, dt, 3); // the take: the game's own
        TorqueHandStep(gh, true, 0x5000, 0x4000, 1.0f, 2.0f, dt, 3);
        TorqueHandStep(gh, true, 0x5000, 0x4000, gh.written, 1.2f, dt, 3);
        const TorqueOrders groundRise = TorqueHandStep(gh, true, 0x5000, 0x4000, gh.written, 2.0f, dt, 3);
        const bool ground = gh.heldBack && Near(groundRise.factor, 1.2f + kLiftRise * dt, 0.0001f);
        char line[300];
        snprintf(line, sizeof line, "sway: a break loose at pass %d opens the pace, the fall to %.2f at once, the catch's rise at most %.3f a pass; a creep along the "
                                    "fade opens none (a rise back of %.2f at once); the ground path's cut still paces", looseAt, fallAt, worstRise, creptRise);
        Check(caught && !creptHeld && creptRise > kLiftRise * dt + 0.01f && ground, line);
    }

    // the factor of one gear never stands in another (BRIEF_drive_1114_findings.md, part 172, musts 1, 2 and 5)
    {
        const float dtp = 0.05f, lift1 = 4.04f, lift2 = 1.0f, liftNew = 1.3f, room = 0.0001f;
        // one pass: what stands at the action is read, and a write puts the new figure there
        auto passOf = [&](TorqueHand &th, float &stands, float lift, int gear, bool shiftOut) {
            const TorqueOrders o = TorqueHandStep(th, true, 0x5000, 0x4000, stands, lift, dtp, gear, false, false, shiftOut);
            if (o.write) stands = o.factor;
        };
        auto orderOf = [&](TorqueHand &th, float &stands, float &stood) {
            const TorqueOrders o = TorqueHandShift(th, &stood);
            if (o.write) stands = o.factor;
            return o.write;
        };
        // L floored at 0.81 of gear 1's top into A, the start gear 2 ordered in the pass the hand takes the truck: gear
        // 1's lift is 4.04, gear 2's own at its share 1.00. From the order on nothing above gear 2's own stands
        TorqueHand intoA = {};
        float at = 1.0f, stood = 0.0f;
        passOf(intoA, at, lift1, 1, false);
        bool walk = at == 1.0f && !orderOf(intoA, at, stood);
        for (int i = 0; i < 6; i++)
        {
            passOf(intoA, at, lift1, 1, true); // the job is out: the engaged gear is still 1, the lift worked out still gear 1's
            walk = walk && at <= 1.0f * lift2 + room;
        }
        passOf(intoA, at, liftNew, 2, false); // engaged: gear 2's lift at once
        walk = walk && Near(at, liftNew, room);
        // the shift a pass late: the take's 1.00, then 4.04 in gear 1, then the order: down to the game's own first
        TorqueHand late = {};
        float atLate = 1.0f, stoodLate = 0.0f;
        passOf(late, atLate, lift1, 1, false);
        passOf(late, atLate, lift1, 1, false);
        bool lateWalk = Near(atLate, lift1, room) && orderOf(late, atLate, stoodLate) && Near(stoodLate, lift1, room) && atLate == 1.0f;
        for (int i = 0; i < 6; i++)
        {
            passOf(late, atLate, lift1, 1, true);
            lateWalk = lateWalk && atLate <= 1.0f * lift2 + room;
        }
        passOf(late, atLate, liftNew, 2, false);
        lateWalk = lateWalk && Near(atLate, liftNew, room);
        // a floored shift up at 0.9 of the top, the game's share 0.8 (a damaged engine): 2.57 times it stood in gear 6;
        // in the first pass of gear 7 the factor is the game's own, then gear 7's lift at once, with no pace
        TorqueHand up = {};
        float atUp = 0.8f, stoodUp = 0.0f;
        passOf(up, atUp, 2.57f, 6, false);
        passOf(up, atUp, 2.57f, 6, false);
        bool upWalk = Near(atUp, 0.8f * 2.57f, room) && orderOf(up, atUp, stoodUp) && Near(atUp, 0.8f, room) && !orderOf(up, atUp, stoodUp);
        passOf(up, atUp, 2.57f, 6, true);
        upWalk = upWalk && Near(atUp, 0.8f, room);
        passOf(up, atUp, 1.1f, 7, false);
        upWalk = upWalk && Near(atUp, 0.88f, room) && !up.heldBack;
        // a shift down: a factor under the game's own is not raised by the rule, and a lower one is still written
        // while the shift is out
        TorqueHand down = {};
        float atDown = 1.0f, stoodDown = 0.0f;
        passOf(down, atDown, 0.6f, 5, false);
        bool downWalk = Near(atDown, 0.6f, room) && !orderOf(down, atDown, stoodDown) && Near(atDown, 0.6f, room);
        passOf(down, atDown, 0.5f, 5, true);
        downWalk = downWalk && Near(atDown, 0.5f, room);
        passOf(down, atDown, 2.0f, 5, true);
        downWalk = downWalk && Near(atDown, 0.5f, room);
        passOf(down, atDown, 2.0f, 4, false);
        downWalk = downWalk && Near(atDown, 2.0f, room);
        Check(walk && lateWalk && upWalk && downWalk,
              "torque hand at a shift: L floored at 0.81 of gear 1's top into A with the start gear 2, the take's first write the game's own and nothing above gear 2's "
              "own lift from the order on, also with the order a pass late (4.04 down to 1.00 first); a floored shift up has the game's own in the new gear's first "
              "pass and its lift at once after; a shift down raises nothing");
    }

    // the torque cut's map (BRIEF_drivetrain_2_torque_cut.md): F over the wheels' speed with the clutch fully in. At idle
    // and floored the curve's own there; at the peak E times the limit; the governor from the band's start (1.0 of the
    // redline at a 10 rad/s top) to nothing at the needle's stop (1.1), L- of 2.74 rad/s from 0.81; the foot off the brake, 0 at idle and under it, the whole share at the redline; the road load
    // taken off; the engine held back; rolling against the gear 0 rpm (M2); the game's curve and B4s by the research's C.1
    {
        TorqueCutMap cm = { 1.0f, 1.0f, 0.0f, 1.0f, 0.15f, 10.0f, 800.0f, 2000.0f, 1.2f, { 5, { 0.25f, 0.45f, 0.6f, 0.8f, 1.0f }, { 0.55f, 0.85f, 1.0f, 1.0f, 0.85f } },
                           CutGovernBand(10.0f, kTachOverrun).from, CutGovernBand(10.0f, kTachOverrun).to };
        const CutBand lowBand = CutGovernBand(2.74f, kTachOverrun), road = CutGovernBand(15.0f, kTachOverrun);
        const float atIdle = TorqueCutF(cm, 4.0f), atPeak = TorqueCutF(cm, 7.0f), atFrom = TorqueCutF(cm, 10.0f * cm.governFrom), atHalf = TorqueCutF(cm, 10.5f), atTo = TorqueCutF(cm, 10.0f * cm.governTo),
                    past = TorqueCutF(cm, 14.0f);
        TorqueCutMap cold = cm;
        cold.limit = 0.6f;
        const float limited = TorqueCutF(cold, 7.0f);
        TorqueCutMap part = cm;
        part.pedal = 0.5f;
        part.limit = 0.6f;
        const float partLimited = TorqueCutF(part, 7.0f); // the turbo without boost gives what a light foot asks, less the
                                                            // half foot footOff: 0.5 x 0.15 x (1400 - 800) / 1200
        TorqueCutMap footOff = cm;
        footOff.pedal = 0.0f;
        const float brakeTop = TorqueCutF(footOff, 10.0f), brakeIdle = TorqueCutF(footOff, 4.0f), brakeUnder = TorqueCutF(footOff, 2.0f), brakeHalf = TorqueCutF(footOff, 7.0f),
                    against = TorqueCutF(footOff, -6.0f);
        TorqueCutMap loaded = cm;
        loaded.load = 0.2f;
        TorqueCutMap heldBack = cm;
        heldBack.held = 0.5f;
        const bool map = Near(atIdle, CutCurveAt(cm.curve, 0.4f, 1.2f), 1e-5f) &&
                         Near(atPeak, 1.0f, 1e-5f) && Near(atFrom, CutCurveAt(cm.curve, 1.0f, 1.2f), 1e-5f) && Near(atHalf, 0.5f * CutCurveAt(cm.curve, 1.05f, 1.2f), 1e-5f) && atTo == 0.0f && past == 0.0f &&
                         Near(cm.governFrom, 1.0f, 1e-6f) && Near(cm.governTo, 1.1f, 1e-6f) && Near(lowBand.from, 1.1f - kGovernWidth / 2.74f, 1e-6f) && road.from == 1.0f &&
                         Near(limited, 0.6f, 1e-5f) && Near(partLimited, 0.5f - 0.5f * 0.15f * 0.5f, 1e-5f) && Near(brakeTop, -0.15f, 1e-5f) && brakeIdle == 0.0f && brakeUnder == 0.0f &&
                         Near(brakeHalf, -0.15f * (1400.0f - 800.0f) / 1200.0f, 1e-5f) && against == 0.0f && Near(TorqueCutF(loaded, 7.0f), 0.8f, 1e-5f) &&
                         Near(TorqueCutF(heldBack, 7.0f), 0.5f, 1e-5f) && TorqueCutRpm(cm, -3.0f) == 0.0f && Near(TorqueCutRpm(cm, 5.0f), 1000.0f, 1e-3f) &&
                         TorqueCutF(cm, NAN) == TorqueCutF(cm, 0.0f);
        // the engine braking above the redline (BRIEF_overrev_downshift.md, point 5): rising to 1.3 of the top, flat above;
        // the pull 0 above the governor's end with the pedal floored
        const float brake100 = TorqueCutF(footOff, 10.0f), brake115 = TorqueCutF(footOff, 11.5f), brake130 = TorqueCutF(footOff, 13.0f), brake160 = TorqueCutF(footOff, 16.0f);
        const bool overBrake = brake115 < brake100 && brake130 < brake115 && Near(brake130, -0.15f * (2600.0f - 800.0f) / 1200.0f, 1e-5f) && brake160 == brake130 &&
                               TorqueCutF(cm, 12.0f) == 0.0f && TorqueCutF(cm, 16.0f) == 0.0f;
        Check(overBrake, "torque cut: the engine braking keeps rising above the redline to its value at 1.3 of the top (1.5 x at the redline here), flat above; the pull 0 above the governor's end");
        // the open converter's F (BRIEF_drivetrain_3_a_on_our_chain.md, M1 and M3): the map's law with the engine ahead of
        // the input. Floored at the stall (the curve whole at 1600 rpm) the ratio doubles it and the bound is the stall's
        // ratio; at the coupling point it is the map's own drive part; the cold limit, the load, the engine held back
        {
            TorqueCutMap half = cm, coldOpen = cm, loadedOpen = cm, heldOpen = cm, offOpen = cm;
            half.pedal = 0.5f, coldOpen.limit = 0.6f, loadedOpen.load = 0.2f, heldOpen.held = 0.5f, offOpen.pedal = 0.0f;
            const float atStall = ConverterOpenF(cm, 0.0f, 1600.0f, kStallRatio, kStallRatio), bounded = ConverterOpenF(cm, 0.0f, 1600.0f, 2.6f, kStallRatio);
            const float coupled = ConverterOpenF(cm, 1200.0f, 1200.0f, 1.0f, kStallRatio), lockedSame = TorqueCutF(cm, 6.0f);
            const float halfOpen = ConverterOpenF(half, 1200.0f, 1200.0f, 1.0f, kStallRatio), halfLocked = TorqueCutF(half, 6.0f);
            const float brakeAtHalf = 0.5f * 0.15f * (1200.0f - 800.0f) / 1200.0f;
            const bool law = Near(atStall, 2.0f, 1e-5f) && Near(bounded, kStallRatio, 1e-5f) && Near(coupled, 1.0f, 1e-5f) && Near(coupled, lockedSame, 1e-5f) && Near(halfOpen, 0.5f, 1e-5f) &&
                             Near(halfOpen - halfLocked, brakeAtHalf, 1e-5f) && Near(ConverterOpenF(coldOpen, 0.0f, 1600.0f, 2.0f, kStallRatio), 1.2f, 1e-5f) &&
                             Near(ConverterOpenF(loadedOpen, 0.0f, 1600.0f, 2.0f, kStallRatio), 1.8f, 1e-5f) && Near(ConverterOpenF(heldOpen, 0.0f, 1600.0f, 2.0f, kStallRatio), 1.0f, 1e-5f) &&
                             ConverterOpenF(offOpen, 900.0f, 1000.0f, 1.3f, kStallRatio) == 0.0f && ConverterOpenF(cm, 2000.0f * cm.governTo, 2300.0f, 1.0f, kStallRatio) == 0.0f &&
                             ConverterOpenF(cm, NAN, 1600.0f, 2.0f, kStallRatio) == 0.0f && ConverterOpenF(cm, 0.0f, 1600.0f, NAN, kStallRatio) == 0.0f &&
                             Near(ConverterOpenF(cm, 1200.0f, 1200.0f, 0.4f, kStallRatio), 1.0f, 1e-5f);
            Check(law, "torque cut in A: the open converter's F is the map's law at the engine's own speed times the torque ratio: 2.0 floored at the stall and never over the stall's ratio, "
                       "the map's own at the coupling point, at half pedal over the locked map by the brake term alone; the cold limit, the load and the engine held back as in the map; "
                       "0 with the foot off, at the governor's end and for a figure that is no number");
            // part B (points 12 to 14): the open converter worked out in the detour from this step's wheels. Against the
            // thread's own formulas (models.h, GearboxStep) at 20 speeds and 5 pedals: one function at two rates
            {
                bool agree = true, falls = true;
                float worst = 0.0f;
                const float pedals[5] = { 0.1f, 0.3f, 0.5f, 0.8f, 1.0f };
                for (int pi = 0; pi < 5; pi++)
                {
                    TorqueCutMap pm = cm;
                    pm.pedal = pedals[pi];
                    const float stallAt = cm.idle + (fminf(cm.redline * kStallSpeed, kStallRpmMost) - cm.idle) * pedals[pi];
                    const float riseAt = kStallRise * (pedals[pi] / kFootOff < 1.0f ? pedals[pi] / kFootOff : 1.0f);
                    float ratioBefore = 9.0f;
                    for (int si = 0; si < 20; si++)
                    {
                        const float sAt = cm.top * (float)si / 19.0f, inputAt = sAt / cm.top * cm.redline;
                        const float openAt = fmaxf(fmaxf(inputAt * (1.0f + kConverterSlip), stallAt + riseAt * inputAt), cm.idle);
                        const float speedRatio = inputAt / openAt;
                        const float ratioAt = speedRatio >= kCouplingPoint ? 1.0f : 1.0f + (kStallRatio - 1.0f) * (1.0f - speedRatio / kCouplingPoint);
                        const float thread = ConverterOpenF(pm, inputAt, openAt, ratioAt, kStallRatio);
                        float engineAt = 0.0f;
                        const float detour = ConverterOpenAt(pm, sAt, stallAt, riseAt, &engineAt);
                        const float apart = fabsf(detour - thread) / fmaxf(fabsf(thread), 0.05f);
                        worst = apart > worst ? apart : worst;
                        agree = agree && apart <= 0.02f && Near(engineAt, openAt, 0.5f);
                        const ConverterNow at = ConverterAt(pm, sAt, stallAt, riseAt);
                        falls = falls && at.ratio <= ratioBefore + 1e-6f && at.ratio >= 1.0f && at.ratio <= kStallRatio;
                        ratioBefore = at.ratio;
                    }
                }
                // in the answer: a wheel that spins up in one step lowers the ratio and F in that same step, the engine's speed
                // rises with it; a step whose wheels were not read goes by the thread's scalar; locked, the map as before
                TorqueCutSlot conv = {};
                conv.vehicle = 1, conv.mode = kCutScalar, conv.share = 1.0f, conv.pedal = 1.0f, conv.map = cm, conv.scalar = 0.77f;
                conv.convOpen = true, conv.convStall = 1600.0f, conv.convRise = kStallRise;
                const CutSpeeds grip = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, true }, spun = { 6.0f, 6.0f, 6.0f, 6.0f, 6.0f, true }, unread = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false };
                const TorqueCutAnswer gripped = TorqueCutAnswerOf(conv, 1.0f, -1.0f, 0.0f, 1e9f, 1.0f, 1.0f, grip), spinning = TorqueCutAnswerOf(conv, 1.0f, -1.0f, 0.0f, 1e9f, 1.0f, 1.0f, spun);
                const TorqueCutAnswer blind = TorqueCutAnswerOf(conv, 1.0f, -1.0f, 0.0f, 1e9f, 1.0f, 1.0f, unread);
                TorqueCutSlot thread = conv, lockedSlot = conv;
                thread.convOpen = false;
                lockedSlot.mode = kCutMap;
                const TorqueCutAnswer byThread = TorqueCutAnswerOf(thread, 1.0f, -1.0f, 0.0f, 1e9f, 1.0f, 1.0f, spun), byMap = TorqueCutAnswerOf(lockedSlot, 1.0f, -1.0f, 0.0f, 1e9f, 1.0f, 1.0f, spun);
                const bool step = spinning.f < gripped.f - 0.2f && spinning.convEngine > gripped.convEngine && gripped.f > 1.5f && gripped.f <= kStallRatio &&
                                  Near(gripped.convEngine, 1600.0f + kStallRise * 200.0f, 0.5f) && blind.f == 0.77f && blind.convEngine == 0.0f && byThread.f == 0.77f &&
                                  byThread.convEngine == 0.0f && byMap.convEngine == 0.0f && Near(byMap.f, TorqueCutF(cm, 6.0f), 1e-5f);
                // the needle: the detour's engine speed while the cut acts in A through the open converter for this truck, else the model's
                const bool needle = ConverterNeedleRpm(true, false, true, 1630.0f, 1500.0f) == 1630.0f && ConverterNeedleRpm(false, false, true, 1630.0f, 1500.0f) == 1500.0f &&
                                    ConverterNeedleRpm(true, true, true, 1630.0f, 1500.0f) == 1500.0f && ConverterNeedleRpm(true, false, false, 1630.0f, 1500.0f) == 1500.0f &&
                                    ConverterNeedleRpm(true, false, true, 0.0f, 1500.0f) == 1500.0f && ConverterNeedleRpm(true, false, true, NAN, 1500.0f) == 1500.0f;
                const ConverterNow bad = ConverterAt(cm, NAN, 1600.0f, kStallRise), noStall = ConverterAt(cm, 0.0f, NAN, NAN), fast = ConverterAt(cm, 50.0f, 1600.0f, kStallRise);
                const bool edges = bad.turbine == 0.0f && bad.engine == cm.idle && bad.ratio == 1.0f && noStall.engine == cm.idle && fast.engine <= cm.tachTop * cm.redline + 0.5f &&
                                   fast.ratio == 1.0f && ConverterRatio(0.0f, 1600.0f) == kStallRatio && ConverterRatio(1500.0f, 1600.0f) == 1.0f && ConverterRatio(100.0f, 0.0f) == 1.0f;
                printf("torque cut in A, part B: the detour against the thread's formulas, the largest difference %.5f of F\n", worst);
                Check(agree && falls && step && needle && edges,
                      "torque cut in A, part B: the open converter's F in the detour is the thread's at 20 speeds and 5 pedals within 2 percent, its ratio never rising with the speed; a "
                      "wheel that spins up in one step lowers the ratio and F in that step and raises the engine's speed; wheels not read go by the thread's scalar; locked is the map; "
                      "the needle goes by the detour's engine speed only while the cut acts in A, open, for this truck; no number and no stall give idle");
                // the review's S3 (M7), a shift through the 60 Hz sketch, floored, the wheels at 5 rad/s: 30 steps in the first
                // gear (top 6), then the second (top 10), each step its own gear's slot, the bracket paced as the detour paces it.
                // Made locked: one gear's map, then the other's, and never the scalar. Made open: the converter in each gear
                float worstStep[2] = { 0.0f, 0.0f };
                bool noFlip = true, neverScalar = true;
                for (int openShift = 0; openShift < 2; openShift++)
                {
                    TorqueCutSlot gearSlots[2];
                    for (int gi = 0; gi < 2; gi++)
                    {
                        gearSlots[gi] = conv;
                        gearSlots[gi].map.top = gi ? 10.0f : 6.0f;
                        const CutBand shiftBand = CutGovernBand(gearSlots[gi].map.top, kTachOverrun);
                        gearSlots[gi].map.governFrom = shiftBand.from, gearSlots[gi].map.governTo = shiftBand.to;
                        gearSlots[gi].mode = openShift ? kCutScalar : kCutMap;
                        gearSlots[gi].convOpen = openShift != 0;
                        gearSlots[gi].scalar = NAN; // a scalar reaching the wheels here would read as 0 and show as a step
                    }
                    const CutSpeeds rolling5 = { 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, true };
                    float usedNow = 0.0f;
                    for (int stepI = 0; stepI < 60; stepI++)
                    {
                        TorqueCutAnswer shiftAnswer = TorqueCutAnswerOf(gearSlots[stepI < 30 ? 0 : 1], 1.0f, -1.0f, 0.0f, 1e9f, 1.0f, 1.0f, rolling5);
                        if (!stepI) usedNow = shiftAnswer.bracket;
                        TorqueCutAnswerPaced(shiftAnswer, 1.0f, usedNow, 0.05f);
                        const float change = fabsf(shiftAnswer.used - usedNow);
                        if (stepI) worstStep[openShift] = change > worstStep[openShift] ? change : worstStep[openShift];
                        noFlip = noFlip && shiftAnswer.used > 0.0f;
                        neverScalar = neverScalar && (openShift ? shiftAnswer.convEngine > 0.0f : shiftAnswer.convEngine == 0.0f);
                        usedNow = shiftAnswer.used;
                    }
                }
                printf("torque cut in A, a shift 1 to 2 floored in the 60 Hz sketch: the largest step of the bracket %.3f made locked, %.3f made open\n", worstStep[0], worstStep[1]);
                Check(worstStep[0] <= 0.15f && worstStep[1] <= 0.15f && noFlip && neverScalar,
                      "torque cut in A, a shift from 1 to 2 floored in the 60 Hz sketch: made locked and made open no step of the bracket is over 0.15, the pull never changes its "
                      "sign, and the thread's scalar never reaches the wheels");
            }
            // the run-down at a lift (BRIEF_drive_1114_findings.md, part 180, the seven musts)
            {
                // the seconds: the tool's figure over 100 times 2.0 s in the first gear, less by the tops' ratio squared above it
                const bool secs = RunDownSeconds(100, 10.0f, 10.0f) == kRunDownMost && RunDownSeconds(50, 10.0f, 10.0f) == 1.0f && Near(RunDownSeconds(100, 20.0f, 10.0f), 0.5f, 1e-6f) &&
                                  RunDownSeconds(100, 5.0f, 10.0f) == kRunDownMost && RunDownSeconds(0, 10.0f, 10.0f) == 0.0f && RunDownSeconds(-3, 10.0f, 10.0f) == 0.0f &&
                                  RunDownSeconds(400, 10.0f, 10.0f) == kRunDownMost && RunDownSeconds(100, NAN, 10.0f) == 0.0f && RunDownSeconds(100, 0.0f, 10.0f) == 0.0f &&
                                  RunDownSeconds(100, 20.0f, NAN) == kRunDownMost;
                const bool gates = RunDownAllowed(0.0f, false, false) && !RunDownAllowed(0.3f, false, false) && !RunDownAllowed(0.0f, true, false) && !RunDownAllowed(0.0f, false, true) &&
                                   !RunDownAllowed(NAN, false, false) && RunDownSpinning(true, 12.0f, true, 4.0f) && !RunDownSpinning(true, 4.5f, true, 4.0f) &&
                                   !RunDownSpinning(false, 12.0f, true, 4.0f) && !RunDownSpinning(true, 12.0f, false, 4.0f) && RunDownSpinning(true, 3.0f, true, -2.0f) &&
                                   !RunDownSpinning(true, 0.4f, true, 0.0f) && !RunDownSpinning(true, NAN, true, 4.0f);
                // the review of 180, S1 and S2: a clutch that is not locked gives no seconds; the weight is nothing at the
                // threshold and whole from twice the truck's speed, and the held part goes by it
                CutRun weighed = {};
                CutRunStep(weighed, 0.9f, 0.9f, 1.0f, 10.0f, 1.0f, 1.0f, 1000, 7);
                const float fromThr = 4.0f * kRunSpinShare + kRunSpinMore;
                const bool shoulds = !RunDownAllowed(0.0f, false, false, true) && RunDownAllowed(0.0f, false, false, false) && RunDownWeight(true, fromThr, true, 4.0f) == 0.0f &&
                                     RunDownWeight(true, 8.5f, true, 4.0f) == 1.0f && RunDownWeight(true, 30.0f, true, 4.0f) == 1.0f &&
                                     Near(RunDownWeight(true, (fromThr + 8.5f) / 2.0f, true, 4.0f), 0.5f, 1e-5f) && RunDownWeight(true, 0.6f, true, 0.0f) == 1.0f &&
                                     RunDownWeight(true, 0.5f, true, 0.0f) == 0.0f && RunDownWeight(false, 30.0f, true, 4.0f) == 0.0f && RunDownWeight(true, 30.0f, false, 4.0f) == 0.0f &&
                                     RunDownWeight(true, NAN, true, 4.0f) == 0.0f && (RunDownWeight(true, 6.0f, true, 4.0f) > 0.0f) == RunDownSpinning(true, 6.0f, true, 4.0f) &&
                                     Near(CutRunStep(weighed, -0.07f, 0.9f, 0.0f, 10.0f, 0.25f, 1.0f, 1016, 7), -0.07f + 0.97f * 0.25f, 1e-5f);
                Check(shoulds, "run-down at a lift: a clutch that is not locked gives it no seconds; the wheels get nothing of it at the spinning threshold, half of it half way and all "
                               "of it from twice the truck's speed, so one wheel slipping a little carries next to nothing on");
                // the step: a lift from 0.9 to the engine's braking at -0.07, 1.0 s: a straight line by the clock
                CutRun line = {};
                bool wasPaced = false;
                bool rule = CutRunStep(line, 0.9f, 0.9f, 1.0f, 10.0f, true, 1.0f, 1000, 7, &wasPaced) == 0.9f && !wasPaced; // floored: nothing
                rule = rule && Near(CutRunStep(line, -0.07f, 0.9f, 0.0f, 10.0f, true, 1.0f, 1016, 7, &wasPaced), 0.9f, 1e-6f) && wasPaced;
                rule = rule && Near(CutRunStep(line, -0.07f, 0.8f, 0.0f, 10.0f, true, 1.0f, 1516, 7, &wasPaced), -0.07f + 0.97f * 0.5f, 1e-4f) && wasPaced;
                rule = rule && CutRunStep(line, -0.07f, 0.4f, 0.0f, 10.0f, true, 1.0f, 2016, 7, &wasPaced) == -0.07f && !wasPaced && !line.at;
                rule = rule && CutRunStep(line, -0.07f, -0.07f, 0.0f, 10.0f, true, 1.0f, 2032, 7, &wasPaced) == -0.07f && !wasPaced; // done: no second start
                // the same at 60 and at 120 steps a second: the line by the clock at the same instants
                bool rates = true;
                {
                    CutRun at60 = {}, at120 = {};
                    CutRunStep(at60, 0.9f, 0.9f, 1.0f, 10.0f, true, 1.0f, 1000, 7);
                    CutRunStep(at120, 0.9f, 0.9f, 1.0f, 10.0f, true, 1.0f, 1000, 7);
                    float used60 = 0.9f, used120 = 0.9f;
                    for (int ms = 1008; ms <= 2100; ms += 8)
                    {
                        used120 = CutRunStep(at120, -0.07f, used120, 0.0f, 10.0f, true, 1.0f, (uint64_t)ms, 7);
                        if ((ms - 1000) % 16 == 0)
                        {
                            used60 = CutRunStep(at60, -0.07f, used60, 0.0f, 10.0f, true, 1.0f, (uint64_t)ms, 7);
                            rates = rates && Near(used60, used120, 0.01f);
                        }
                    }
                    rates = rates && used60 == -0.07f && used120 == -0.07f;
                }
                // what never paces: a figure of 0 (bit for bit, no state), gripping wheels, a rise, a fall with the pedal steady (the governor's)
                CutRun none = {};
                const float asked = -0.0712345f;
                bool never = CutRunStep(none, 0.9f, 0.9f, 1.0f, 10.0f, true, 0.0f, 1000, 7) == 0.9f && CutRunStep(none, asked, 0.9f, 0.0f, 10.0f, true, 0.0f, 1016, 7, &wasPaced) == asked &&
                             !wasPaced && !none.at;
                CutRun grips = {};
                CutRunStep(grips, 0.9f, 0.9f, 1.0f, 10.0f, false, 1.0f, 1000, 7);
                never = never && CutRunStep(grips, asked, 0.9f, 0.0f, 10.0f, false, 1.0f, 1016, 7, &wasPaced) == asked && !wasPaced && !grips.at;
                CutRun rise = {};
                CutRunStep(rise, 0.2f, 0.2f, 0.0f, 10.0f, true, 1.0f, 1000, 7);
                never = never && CutRunStep(rise, 0.5f, 0.2f, 0.0f, 10.0f, true, 1.0f, 1016, 7, &wasPaced) == 0.5f && !wasPaced;
                CutRun pedalHeld = {};
                CutRunStep(pedalHeld, 0.9f, 0.9f, 1.0f, 10.0f, true, 1.0f, 1000, 7);
                never = never && CutRunStep(pedalHeld, 0.1f, 0.9f, 1.0f, 10.0f, true, 1.0f, 1016, 7, &wasPaced) == 0.1f && !wasPaced;
                // what ends it at once: the pedal back, the wheels gripping, a shift (another top), 0 seconds (the brake, the handbrake), another truck
                bool ends = true;
                for (int how = 0; how < 5; how++)
                {
                    CutRun e = {};
                    CutRunStep(e, 0.9f, 0.9f, 1.0f, 10.0f, true, 1.0f, 1000, 7);
                    const bool going = CutRunStep(e, -0.07f, 0.9f, 0.0f, 10.0f, true, 1.0f, 1100, 7, &wasPaced) > 0.5f && wasPaced;
                    const float target = how == 0 ? 0.3f : -0.07f;
                    const float got = CutRunStep(e, target, 0.8f, how == 0 ? 0.3f : 0.0f, how == 2 ? 14.0f : 10.0f, how != 1, how == 3 ? 0.0f : 1.0f, 1116, how == 4 ? 8 : 7, &wasPaced);
                    ends = ends && going && got == target && !wasPaced && !e.at;
                }
                // a paced step feeds no sample to the learner (must 5)
                TorqueCutSlot learnSlot = {};
                learnSlot.mode = kCutMap;
                TorqueCutAnswer fed = {};
                fed.bracket = fed.used = 0.5f;
                const bool feeds = CutLearnStepOk(learnSlot, fed, 1.0f);
                fed.running = true;
                Check(secs && gates && rule && rates && never && ends && feeds && !CutLearnStepOk(learnSlot, fed, 1.0f),
                      "run-down at a lift: 2.0 s in the first gear at 100, less by the tops' ratio squared above it, the low range as the first; a lift's fall runs in a straight line by "
                      "the clock, the same at 60 and at 120 steps a second; at a figure of 0 the bracket is the step's own bit for bit; gripping wheels, a rise and a fall with the pedal "
                      "steady are never paced; the pedal back, grip, a shift, the brake's 0 seconds and another truck end it at once; a paced step feeds no sample");
                // the sketch (must 7): one driven wheel spinning in place, floored, then the foot off. The wheel's drive is 60 rad/s^2
                // at a bracket of 1 and the ground takes 3 /s of its speed. The seconds until it is under a quarter of its speed at the lift
                auto runDown = [&](int percent, float gearTop, float firstTop, int hz, float truckShare) {
                    TorqueCutSlot rs = {};
                    rs.vehicle = 7, rs.mode = kCutMap, rs.share = 1.0f, rs.pedal = 1.0f, rs.map = cm;
                    rs.map.top = gearTop;
                    const CutBand rb = CutGovernBand(gearTop, kTachOverrun);
                    rs.map.governFrom = rb.from, rs.map.governTo = rb.to;
                    rs.runSeconds = RunDownSeconds(percent, gearTop, firstTop);
                    CutRun rr = {};
                    float wheel = 0.0f, used = 0.0f, atLift = 0.0f, quarterAt = -1.0f;
                    const float dt = 1.0f / (float)hz;
                    for (int stepI = 0; stepI < 8 * hz; stepI++)
                    {
                        const float at = (float)stepI * dt;
                        const bool lifted = at >= 3.0f;
                        rs.pedal = rs.map.pedal = lifted ? 0.0f : 1.0f;
                        CutSpeeds rsp = { wheel, wheel, wheel, wheel, wheel, true };
                        rsp.truck = wheel * truckShare, rsp.truckOk = true;
                        TorqueCutAnswer ra = TorqueCutAnswerOf(rs, 1.0f, -1.0f, 0.0f, 1e9f, 1.0f, 1.0f, rsp);
                        ra.bracket = CutRunStep(rr, ra.bracket, used, rs.pedal, rs.map.top, RunDownWeight(rsp.ok, rsp.mean, rsp.truckOk, rsp.truck), rs.runSeconds,
                                                (uint64_t)(1000.0f + at * 1000.0f), 7, &ra.running);
                        TorqueCutAnswerPaced(ra, 1.0f, used, 1.0f);
                        used = ra.used;
                        if (lifted && atLift == 0.0f) atLift = wheel;
                        wheel = fmaxf(wheel + (60.0f * used - 3.0f * wheel) * dt, 0.0f);
                        if (lifted && quarterAt < 0.0f && wheel < 0.25f * atLift) quarterAt = at - 3.0f;
                    }
                    return quarterAt;
                };
                const float today = runDown(0, 30.0f, 30.0f, 60, 0.0f), first60 = runDown(100, 30.0f, 30.0f, 60, 0.0f), first120 = runDown(100, 30.0f, 30.0f, 120, 0.0f);
                const float atFifty = runDown(50, 30.0f, 30.0f, 60, 0.0f), topGear = runDown(100, 90.0f, 30.0f, 60, 0.0f), topToday = runDown(0, 90.0f, 30.0f, 60, 0.0f);
                const float gripping = runDown(100, 30.0f, 30.0f, 60, 1.0f), grippingToday = runDown(0, 30.0f, 30.0f, 60, 1.0f);
                printf("     run-down at a lift, the sketch: a wheel spinning in place is under a quarter of its speed %.2f s after the lift today, %.2f s at 100 in the first gear (%.2f s at "
                       "120 steps a second), %.2f s at 50; in a gear of three times the top %.2f s at 100 against %.2f s today; a gripping truck %.2f s at 100 and %.2f s today\n",
                       today, first60, first120, atFifty, topGear, topToday, gripping, grippingToday);
                Check(today > 0.0f && first60 > today + 0.5f && fabsf(first60 - first120) <= 0.05f && atFifty > today && atFifty < first60 && topGear < first60 && topGear - topToday < first60 - today &&
                          gripping == grippingToday,
                      "run-down at a lift, the sketch: a wheel spinning in place runs on longer with Engine inertia than today, the same at 60 and at 120 steps a second, atFifty as long "
                      "again at 50, and far less in a high gear than in the first; a gripping truck's lift is today's");
            }
            // the fall at a lock, paced by the clock (the lead's point 17): from the open converter's 1.34 to the map's 1.0
            // over 0.3 s, the same at 60 and at 120 steps a second, no step over the line's own
            bool sameAtBoth = true, smallSteps = true;
            float last60 = 1.34f, last120 = 1.34f;
            for (int i = 0; i <= 18; i++)
            {
                const float secs = (float)i / 60.0f, f60 = CutLockFall(1.0f, 1.34f, 1.0f, 1.0f, secs), f120 = CutLockFall(1.0f, 1.34f, 1.0f, 1.0f, (float)(2 * i) / 120.0f);
                const float mid = CutLockFall(1.0f, 1.34f, 1.0f, 1.0f, (float)(2 * i + 1) / 120.0f);
                sameAtBoth = sameAtBoth && Near(f60, f120, 1e-6f);
                smallSteps = smallSteps && last60 - f60 <= 0.34f / 18.0f + 1e-5f && last120 - f120 <= 0.34f / 18.0f + 1e-5f && mid <= f120 + 1e-6f && mid >= 1.0f;
                last60 = f60, last120 = f120;
            }
            const bool fall = sameAtBoth && smallSteps && Near(CutLockFall(1.0f, 1.34f, 1.0f, 1.0f, 0.0f), 1.34f, 1e-6f) && Near(CutLockFall(1.0f, 1.34f, 1.0f, 1.0f, 0.15f), 1.17f, 1e-5f) &&
                              CutLockFall(1.0f, 1.34f, 1.0f, 1.0f, 0.3f) == 1.0f && CutLockFall(1.0f, 1.34f, 1.0f, 1.0f, 5.0f) == 1.0f;
            // a lift is the driver's: the foot off drops to the map's at once, half the pedal takes half of what was held;
            // a rise is never held, nor a time that is no number
            const bool lift = CutLockFall(-0.1f, 1.34f, 1.0f, 0.0f, 0.05f) == -0.1f && Near(CutLockFall(0.5f, 1.34f, 1.0f, 0.5f, 0.0f), 0.67f, 1e-6f) &&
                              CutLockFall(1.2f, 1.0f, 1.0f, 1.0f, 0.05f) == 1.2f && CutLockFall(1.0f, 1.34f, 1.0f, 1.0f, NAN) == 1.0f && CutLockFall(1.0f, 1.34f, 1.0f, 1.0f, -1.0f) == 1.0f &&
                              CutLockFall(1.0f, NAN, 1.0f, 1.0f, 0.05f) == 1.0f && CutLockFall(1.0f, 1.34f, 0.0f, 1.0f, 0.05f) == 1.0f;
            // in the answer: the map's slot with a fall carries it and says so, the scalar's never, and a slot without one is as before
            const float omegaA[4] = { 6.0f, 6.0f, 6.0f, 6.0f };
            const bool drivenA[4] = { true, true, true, true };
            const CutSpeeds spA = TorqueCutSpeeds(omegaA, drivenA, 4, 1);
            TorqueCutSlot fs = {};
            fs.mode = kCutMap, fs.share = 1.0f, fs.pedal = 1.0f, fs.map = cm;
            const TorqueCutAnswer plainAnswer = TorqueCutAnswerOf(fs, 3.0f, 0.25f, 4.0f, 13.0f, 1.0f, 1.0f, spA, 5000);
            fs.fallFrom = 1.34f, fs.fallPedal = 1.0f, fs.fallAt = 5000;
            const TorqueCutAnswer falling = TorqueCutAnswerOf(fs, 3.0f, 0.25f, 4.0f, 13.0f, 1.0f, 1.0f, spA, 5150), done = TorqueCutAnswerOf(fs, 3.0f, 0.25f, 4.0f, 13.0f, 1.0f, 1.0f, spA, 5300);
            const TorqueCutAnswer noClock = TorqueCutAnswerOf(fs, 3.0f, 0.25f, 4.0f, 13.0f, 1.0f, 1.0f, spA);
            TorqueCutSlot scalarSlot = fs;
            scalarSlot.mode = kCutScalar, scalarSlot.scalar = 1.5f;
            const TorqueCutAnswer scalarAnswer = TorqueCutAnswerOf(scalarSlot, 3.0f, 0.25f, 4.0f, 13.0f, 1.0f, 1.0f, spA, 5150);
            const bool inAnswer = Near(plainAnswer.f, 1.0f, 1e-5f) && !plainAnswer.falling && Near(falling.f, 1.17f, 1e-4f) && falling.falling && Near(falling.torque, 3.0f * 1.17f, 1e-3f) &&
                                  Near(done.f, 1.0f, 1e-5f) && !done.falling && Near(noClock.f, 1.0f, 1e-5f) && !noClock.falling && scalarAnswer.f == 1.5f && !scalarAnswer.falling;
            if (!(fall && lift && inAnswer)) printf("     the fall: same %d steps %d fall %d lift %d inAnswer %d (%.4f %.4f %.4f)\n", sameAtBoth, smallSteps, fall, lift, inAnswer, plainAnswer.f, falling.f, done.f);
            Check(fall && lift && inAnswer,
                  "torque cut in A: the fall at a lock runs in a straight line from the open converter's F to the map's over 0.3 s by the clock, the same at 60 and at 120 steps a second, "
                  "no step over the line's; a lift drops at once and a rise is never held; the detour's answer carries it in the map alone and says so");
            // the learner under the cut (the lead's addendum of 14:23): which steps may feed a sample, and the divisor as
            // the mean of what the detour used between two passes of the mod's thread
            TorqueCutAnswer settled = plainAnswer;
            settled.used = settled.bracket;
            TorqueCutAnswer stillRising = settled, atFloor = settled;
            stillRising.used = settled.bracket * 0.9f, atFloor.floored = true;
            TorqueCutAnswer nearly = settled;
            nearly.used = settled.bracket * 0.99f;
            fs.fallAt = 0;
            const bool stepOk = CutLearnStepOk(fs, settled, 1.0f) && CutLearnStepOk(fs, nearly, 1.0f) && !CutLearnStepOk(fs, stillRising, 1.0f) && !CutLearnStepOk(fs, atFloor, 1.0f) &&
                                !CutLearnStepOk(fs, falling, 1.0f) && !CutLearnStepOk(scalarSlot, settled, 1.0f) && !CutLearnStepOk(fs, settled, 0.9f) && !CutLearnStepOk(fs, settled, NAN);
            const CutLearnTotals was = { 10.0, 100, 3, 7 };
            const CutLearnTotals three = { 11.5, 103, 3, 7 }, none = { 10.0, 100, 3, 7 }, bad = { 11.5, 103, 4, 7 }, other = { 11.5, 103, 3, 8 };
            const bool divisor = Near(CutLearnDivisor(was, three, 7), 0.5f, 1e-6f) && !isfinite(CutLearnDivisor(was, none, 7)) && !isfinite(CutLearnDivisor(was, bad, 7)) &&
                                 !isfinite(CutLearnDivisor(was, other, 8)) && !isfinite(CutLearnDivisor(was, other, 7)) && !isfinite(CutLearnDivisor(was, three, 0));
            Check(stepOk && divisor,
                  "torque cut, the learning: a step feeds a sample only from the map, settled within 2 percent, not in the lock's fall, not at the floor and with the wheels keeping all the push; "
                  "the divisor is the mean of the brackets the detour used since the last pass, and no number for a count of 0, a step that may not feed, or another truck");
        }
        // the game's curve, as the drive scales it by the power factor: 0 below the start, 1 at a, falling to 0 at the top
        const bool stock = TorqueCutStockCurve(0.25f, 4.0f, 13.0f, 1.0f, 0.1f) == 0.0f && TorqueCutStockCurve(0.25f, 4.0f, 13.0f, 1.0f, 4.0f) == 1.0f &&
                           Near(TorqueCutStockCurve(0.25f, 4.0f, 13.0f, 1.0f, 8.5f), 0.5f, 1e-5f) && TorqueCutStockCurve(0.25f, 4.0f, 13.0f, 1.0f, 13.5f) == 0.0f &&
                           TorqueCutStockCurve(0.25f, 4.0f, 13.0f, 0.2f, 2.6f) == 0.0f && TorqueCutStockCurve(0.25f, 4.0f, 13.0f, 0.2f, 0.8f) == 1.0f &&
                           TorqueCutStockCurve(-1.0f, 0.0f, 1e9f, 0.2f, 3.0f) > 0.999f;
        // B4s, the blend at 0, 50 and 100 (S3: the raw pedal, the game's curve at the float s), and the answer's floor
        const float curveAt = TorqueCutStockCurve(0.25f, 4.0f, 13.0f, 1.0f, 9.0f);
        const bool b4s = Near(TorqueCutB4s(0.3f, 1.0f, curveAt), 0.3f * curveAt, 1e-6f) && Near(TorqueCutB4s(0.3f, 0.5f, curveAt), 0.3f * (0.5f + 0.5f * curveAt), 1e-6f) &&
                         TorqueCutB4s(0.0f, 1.0f, 1.0f) == 0.0f;
        const bool blend = Near(TorqueCutBlend(0.0f, 0.4f, curveAt, 0.9f), 0.4f * curveAt, 1e-6f) && Near(TorqueCutBlend(0.5f, 0.4f, curveAt, 0.9f), 0.2f * curveAt + 0.45f, 1e-6f) &&
                           Near(TorqueCutBlend(1.0f, 0.4f, curveAt, 0.9f), 0.9f, 1e-6f) && Near(TorqueCutBlend(1.0f, 0.0f, 0.0f, -0.1f), -0.1f, 1e-6f);
        // M2: R (the gear's direction -1) rolling forward and 2nd rolling back read 0 rpm, so the foot footOff never pushes with the roll
        const float omegaF[4] = { 5.0f, 5.2f, 4.8f, 5.0f }, omegaB[4] = { -5.0f, -5.2f, -4.8f, -5.0f };
        const bool driven[4] = { true, true, false, true };
        const CutSpeeds rForward = TorqueCutSpeeds(omegaF, driven, 4, -1), secondBack = TorqueCutSpeeds(omegaB, driven, 4, 1), rBack = TorqueCutSpeeds(omegaB, driven, 4, -1);
        TorqueCutSlot slot = {};
        slot.mode = kCutMap;
        slot.share = 1.0f;
        slot.map = footOff;
        const TorqueCutAnswer aF = TorqueCutAnswerOf(slot, 3.0f, 0.25f, 4.0f, 13.0f, 1.0f, 1.0f, rForward), aB = TorqueCutAnswerOf(slot, 2.0f, 0.25f, 4.0f, 13.0f, 1.0f, 1.0f, secondBack);
        const bool direction = rForward.ok && Near(rForward.mean, -5.0667f, 1e-3f) && rForward.mid == 0.0f && Near(rBack.mean, 5.0667f, 1e-3f) &&
                               Near(rBack.mid, 5.1f, 1e-5f) && aF.f >= 0.0f && aF.torque >= 0.0f && aB.f >= 0.0f && aB.torque >= 0.0f;
        // the burn under the hold goes by the stand-in, the cut's own pull (the lead's decision of the second review): in
        // the middle of a gear, where the game's curve is whole, the same as the game's settled pull at that pedal; near
        // the gear's top (0.92 of it) floored, the pull's own burn, where the game's settled pull, and its burn, fall
        // toward idle
        TruckRaw ft = Truck();
        ft.burnKnown = true;
        ft.burnIdle = 1.0f;
        ft.gearBurns = 3;
        ft.gearBurn[0] = ft.gearBurn[1] = ft.gearBurn[2] = 4.0f;
        ft.burnShare[0] = 1.0f;
        ft.burnShare[1] = 0.0f;
        ft.damageEffect = ft.gearboxEffect = ft.tankEffect = 0.0f;
        ft.awd = 0;
        ft.burnAwd = 1.0f;
        bool fuel = TorqueCutStandIn(1.4f) == 1.0f && TorqueCutStandIn(-0.2f) == 0.0f && Near(TorqueCutStandIn(0.37f), 0.37f, 1e-6f);
        char fuelText[200] = "";
        const float midCurve = TorqueCutStockCurve(0.25f, 4.0f, 13.0f, 1.0f, 4.0f), nearTop = TorqueCutStockCurve(0.25f, 4.0f, 13.0f, 1.0f, 12.0f);
        for (float pedal : { 0.3f, 1.0f })
        {
            ft.throttle = -pedal;
            TruckRaw game = ft, cut = ft;
            game.actionB4 = pedal * midCurve; // the game's own pull, settled, in the middle of the gear
            cut.actionB4 = TorqueCutStandIn(pedal);
            fuel = fuel && Near(TruckFuelRate(cut), TruckFuelRate(game), 1e-5f);
            snprintf(fuelText + strlen(fuelText), sizeof fuelText - strlen(fuelText), " mid gear %.1f: %.3f;", pedal, TruckFuelRate(cut));
        }
        ft.throttle = -1.0f;
        TruckRaw gameTop = ft, cutTop = ft;
        gameTop.actionB4 = nearTop;
        cutTop.actionB4 = TorqueCutStandIn(1.0f);
        fuel = fuel && TruckFuelRate(cutTop) > TruckFuelRate(gameTop) + 0.5f;
        snprintf(fuelText + strlen(fuelText), sizeof fuelText - strlen(fuelText), " floored at 0.92 of the top %.3f, the game's %.3f;", TruckFuelRate(cutTop), TruckFuelRate(gameTop));
        // the stall's call (the second review's M1): the game's own formula with the stand-in in B4's place. The wheels'
        // term by whole rad/s over the gear's list top (13 for an entry of 4); the base the game's no-wheels value
        // (min(p^0.25 + 0.25, 1) x B4, B4 1 under the hold) x the stand-in
        const float whole[3] = { 13.7f, 12.2f, 13.9f };
        const bool wholeDriven[3] = { true, true, true };
        const CutSpeeds wh = TorqueCutSpeeds(whole, wholeDriven, 3, 1);
        auto noWheels = [](float p) { return fminf(powf(p, 0.25f) + 0.25f, 1.0f); };
        const float atTop = TorqueCutStallAnswer(1.0f, TorqueCutStallWheels(13.0f, 13.0f, 13.0f), 0.0f, noWheels(1.0f), 1.0f);
        const float midStall = TorqueCutStallAnswer(0.5f, TorqueCutStallWheels(6.0f, 6.0f, 13.0f), 0.5f, noWheels(0.5f), 1.0f);
        const float lowStall = TorqueCutStallAnswer(1.0f, TorqueCutStallWheels(2.0f, 2.0f, 13.0f), 0.5f, noWheels(1.0f), 1.0f);
        const float tiny = TorqueCutStallAnswer(0.005f, 0.75f, 1.0f, noWheels(0.005f), 1.0f);
        const bool stall = wh.wholeMean == 13.0f - 1.0f / 3.0f && wh.wholeMax == 13.0f && Near(TorqueCutStallWheels(12.0f, 13.0f, 13.0f), 0.75f * (3.0f + 9.75f) / 13.0f, 1e-6f) &&
                           TorqueCutStallWheels(5.0f, 5.0f, 0.0f) == 0.0f && Near(atTop, 0.75f, 1e-6f) && Near(midStall, noWheels(0.5f) * 0.5f, 1e-6f) &&
                           Near(lowStall, noWheels(1.0f) * 0.5f, 1e-6f) && tiny == 0.0f && TorqueCutStallAnswer(1.0f, 0.2f, 0.4f, 1.0f, 0.0f) == CutClamp01(0.2f + 0.1f);
        snprintf(fuelText + strlen(fuelText), sizeof fuelText - strlen(fuelText), " the stall's call at a gear's top floored %.3f, mid gear at 0.5 %.3f (the base), L- at its top %.3f (the base), at 0.005 %.3f",
                 atTop, midStall, lowStall, tiny);
        char line[1400];
        snprintf(line, sizeof line, "torque cut's map: idle %.3f, the peak %.3f, the governor %.3f at %.2f and 0 at %.2f of the redline (L- from %.3f), the cold %.3f, the foot off %.3f at the "
                                    "redline and %.3f at idle; R rolling forward F %.3f, 2nd rolling back %.3f; the game's curve by C.1; B4s and the blend at 0, 50, 100; the "
                                    "burn by the cut's pull:%s",
                 atIdle, atPeak, atFrom, cm.governFrom, cm.governTo, lowBand.from, limited, brakeTop, brakeIdle, aF.f, aB.f, fuelText);
        Check(map && stock && b4s && blend && direction && fuel && stall, line);
    }

    // the torque cut in the closed loop (the Reviewer's M4, test\torque_cut_sim.h): the game's drive sketched at its 60
    // steps a second with its own terms (the cut to whole rad/s, w + 0x118, the clamp, the share), L- of a 2.74 rad/s top
    // floored in soft and in firmer mud, MaxDeltaAngVel 0.05 (the 18:21 log has 0.02 and 0.05), a wheel's push of 80
    // rad/s^2 at a bracket of 1 (the log's 10 to 30 at the hold's factors of 0.05 to 0.5). From rest and from 0.55 of the
    // top: no fall of 0.3 or more in one step once settled, and s no more spread than with F held flat at its mean (the
    // game's own terms and the mud alone; the Reviewer's last point: their own spread is said with the figure). The
    // counter-cases document the design: the needle's narrow governor (the redline to 1.1 of it) chatters step to step at
    // 60 Hz in the cut, and fed from the mod's thread every 100 ms (and 50 to 120 ms) cycles; the game's own drive (the
    // stock curve and B4) the control. Then 3rd on a road to the redline and held, and the foot off down a grade in 2nd.
    {
        SimSetup u = {};
        u.drive = 80.0f;
        u.mdav = 0.05f;
        u.pf = 0.2f;
        u.gameA = 2.35f;
        u.gameStart = 0.25f * 2.35f - 2.0f;
        u.gameTop = 2.0f * 2.35f + 5.0f;
        u.response = 0.05f;
        u.wheels = 4;
        u.seconds = 20.0f;
        u.seed = 7;
        TorqueCutSlot &sl = u.slot;
        sl.vehicle = 1;
        sl.mode = kCutMap;
        sl.share = 1.0f;
        sl.pedal = 1.0f;
        sl.map = TorqueCutMap{ 1.0f, 1.0f, 0.0f, 1.0f, 0.15f, 2.74f, 800.0f, 2500.0f, 1.2f, { 5, { 0.25f, 0.45f, 0.6f, 0.8f, 1.0f }, { 0.55f, 0.85f, 1.0f, 1.0f, 0.85f } },
                               CutGovernBand(2.74f, kTachOverrun).from, CutGovernBand(2.74f, kTachOverrun).to };
        const SimGround grounds[2] = { { 5.0f, 0.3f, 1.0f, 1.5f, 0.0f }, { 30.0f, 0.15f, 0.5f, 1.0f, 0.0f } };
        const char *groundNames[2] = { "soft mud", "firm mud" };
        char line[2400];
        int len = snprintf(line, sizeof line, "torque cut in the loop at 60 Hz, L- floored:");
        bool ok = true;
        for (int g = 0; g < 2; g++)
            for (int from = 0; from < 2; from++)
            {
                u.ground = grounds[g];
                u.s0 = from ? 0.55f * 2.74f : 0.0f;
                u.v0 = from ? 0.3f : 0.0f;
                u.mode = kSimCut;
                const SimResult cut = SimRun(u);
                u.mode = kSimFlat;
                u.flat = (cut.fMin + cut.fMax) * 0.5f;
                const SimResult flat = SimRun(u);
                u.mode = kSimGame;
                const SimResult game = SimRun(u);
                len += snprintf(line + len, sizeof line - len, " %s from %.2f: settled %.2f s (F held flat %.2f s), x %.3f, sd %.4f of the top (F held flat %.4f, the game's own %.4f at x %.3f), "
                                                               "falls %d after, the brief's 0.02 of the top %s;",
                                groundNames[g], from ? 0.55f : 0.0f, cut.settledAt, flat.settledAt, cut.mean / 2.74f, cut.sd / 2.74f, flat.sd / 2.74f, game.sd / 2.74f, game.mean / 2.74f,
                                cut.cutsAfter, cut.sd / 2.74f < 0.02f ? "met" : "missed");
                ok = ok && cut.settledAt >= 0.0f && cut.settledAt <= 8.0f && cut.cutsAfter == 0 && cut.sd <= 1.25f * flat.sd + 0.002f * 2.74f;
            }
        // the counter-cases, soft mud from 0.55: the narrow governor of the needle's overrun, in the step and fed late
        u.ground = grounds[0];
        u.s0 = 0.55f * 2.74f;
        u.v0 = 0.3f;
        TorqueCutSlot wide = sl;
        sl.map.governFrom = 1.0f;
        sl.map.governTo = kTachOverrun;
        u.mode = kSimCut;
        const SimResult narrow = SimRun(u);
        u.mode = kSimHeld;
        u.passFrom = u.passTo = 0.1f;
        const SimResult late = SimRun(u);
        u.passFrom = 0.05f;
        u.passTo = 0.12f;
        const SimResult jitter = SimRun(u);
        sl = wide;
        u.passFrom = u.passTo = 0.1f;
        const SimResult wideLate = SimRun(u);
        len += snprintf(line + len, sizeof line - len, " the narrow governor in the step %.1f falls a second, fed every 100 ms %.1f a second (one every %.2f s), every 50 to "
                                                       "120 ms %.1f (%.2f s); the wide one fed every 100 ms %.1f a second;",
                        narrow.cutsPerSecond, late.cutsPerSecond, late.period, jitter.cutsPerSecond, jitter.period, wideLate.cutsPerSecond);
        ok = ok && narrow.cutsPerSecond >= 1.0f && late.cutsPerSecond >= 1.0f && jitter.cutsPerSecond >= 1.0f;
        // 3rd on a road floored: a 15 rad/s top, the clamp's + 0x170 term whole, the truck free to run
        SimSetup road = u;
        road.mode = kSimCut;
        road.w170 = 12.0f;
        road.s0 = road.v0 = 2.0f;
        road.ground = SimGround{ 60.0f, 0.05f, 0.05f, 0.3f, 0.0f };
        road.slot.map.top = 15.0f;
        road.slot.map.governFrom = CutGovernBand(15.0f, kTachOverrun).from;
        road.slot.map.governTo = CutGovernBand(15.0f, kTachOverrun).to;
        road.seconds = 30.0f;
        const SimResult third = SimRun(road);
        len += snprintf(line + len, sizeof line - len, " 3rd on a road: x %.3f at the end, %.3f over the last 10 s (sd %.4f), falls %d after;", third.sEnd / 15.0f,
                        third.mean / 15.0f, third.sd / 15.0f, third.cutsAfter);
        ok = ok && third.mean / 15.0f >= 1.0f && third.mean / 15.0f <= kTachOverrun && third.sEnd / 15.0f <= kTachOverrun && third.cutsAfter == 0;
        // the foot off down a 10 % grade in 2nd (an 8 rad/s top) from 0.8 of its top: held back, slowing or holding
        SimSetup hill = road;
        hill.slot.pedal = 0.0f;
        hill.slot.map.pedal = 0.0f;
        hill.slot.map.top = 8.0f;
        hill.slot.map.governFrom = CutGovernBand(8.0f, kTachOverrun).from;
        hill.slot.map.governTo = CutGovernBand(8.0f, kTachOverrun).to;
        hill.s0 = hill.v0 = 6.4f;
        hill.ground = SimGround{ 60.0f, 0.05f, 0.05f, 0.3f, 0.6f };
        hill.seconds = 15.0f;
        const SimResult held = SimRun(hill);
        SimSetup rolls = hill;
        rolls.slot.map.brake = 0.0f;
        const SimResult free = SimRun(rolls);
        len += snprintf(line + len, sizeof line - len, " the foot off down the grade in 2nd: from %.2f rad/s to %.2f held, to %.2f with EngineBrake 0", hill.v0, held.vEnd, free.vEnd);
        ok = ok && held.vEnd <= hill.v0 + 0.05f && free.vEnd > held.vEnd + 1.0f;
        // over-revving down a grade (BRIEF_overrev_downshift.md, R2): L- of 2.74 rad/s with the foot off at EngineBrake 30,
        // the truck pushed past the top by a grade, the brake rising to its value at 1.3 of the top; the guard at 150 and
        // the governor by the truck's speed as in the game: no fall of F by 0.15 or more in a step over 10 s
        SimSetup steep = hill;
        steep.slot.map.brake = kEngineBrakeMost / 100.0f;
        steep.slot.map.top = 2.74f;
        steep.slot.map.governFrom = CutGovernBand(2.74f, kTachOverrun).from;
        steep.slot.map.governTo = CutGovernBand(2.74f, kTachOverrun).to;
        steep.cap = 1.5f;
        steep.governTruck = true;
        steep.s0 = steep.v0 = 1.1f * 2.74f;
        steep.ground = SimGround{ 60.0f, 0.05f, 0.05f, 0.3f, 3.8f };
        steep.seconds = 12.0f;
        const SimResult down = SimRun(steep);
        len += snprintf(line + len, sizeof line - len, "; L- foot off down a grade at EngineBrake 30 from 1.1 of the top: s %.3f of the top (least %.3f), F %.3f to %.3f, falls of 0.15 in a step %d",
                        down.mean / 2.74f, down.sMin / 2.74f, down.fMin, down.fMax, down.smallFalls);
        ok = ok && down.smallFalls == 0 && down.mean / 2.74f > 1.3f;
        Check(ok, line);
    }

    // the free wheel (BRIEF_drive_2108_findings.md, F1 and F1b): L- of a 2.9 rad/s top floored, a kick of 50 rad/s a step
    // at a bracket of 1 on a slow wheel (the 21:08 log: T 257 000, f 0.2, a 10 kg m^2 wheel), MaxDeltaAngVel 0.01, one
    // wheel of four with no grip, the game's overspeed brake on 10 kg m^2. The counter-case, the old outputs (top 1e9) and
    // the bracket whole at once: the needle pinned at its stop most of the time. F1 alone (the finite top): the brake acts,
    // but one step's kick is about 30 times its most. F1 with F1b (the bracket paced at EngineResponsiveness 0.05 x pf
    // 0.2 a step): the wheels' fastest under 5 x the top and s at the stop under 5 % of the time. Then deep mud with all
    // four gripping, and the grounded cases of the loop above again with the brake and the pace, the figures in the line.
    {
        SimSetup u = {};
        u.drive = 3000.0f;
        u.mdav = 0.01f;
        u.wheels = 4;
        u.seconds = 30.0f;
        u.seed = 11;
        u.inertia = 10.0f;
        u.freeWheels = 1;
        u.ground = SimGround{ 5.0f, 0.3f, 1.0f, 1.5f, 0.0f };
        TorqueCutSlot &sl = u.slot;
        sl.vehicle = 1;
        sl.mode = kCutMap;
        sl.share = 1.0f;
        sl.pedal = 1.0f;
        const CutBand band = CutGovernBand(2.9f, kTachOverrun);
        sl.map = TorqueCutMap{ 1.0f, 1.0f, 0.0f, 1.0f, 0.15f, 2.9f, 800.0f, 2500.0f, 1.2f, { 5, { 0.25f, 0.45f, 0.6f, 0.8f, 1.0f }, { 0.55f, 0.85f, 1.0f, 1.0f, 0.85f } },
                               band.from, band.to };
        const float rise = 0.04f * 0.2f; // the Reviewer's toy: EngineResponsiveness 0.04 x pf 0.2 (0.2 x 0.2 the quicker case)
        u.mode = kSimCut;
        u.cutTop = false;
        u.rise = 0.0f;
        const SimResult old = SimRun(u);
        u.cutTop = true;
        const SimResult brakeOnly = SimRun(u);
        u.rise = rise;
        const SimResult paced = SimRun(u);
        u.rise = 0.04f;
        const SimResult quick = SimRun(u);
        u.rise = rise;
        u.freeWheels = 0;
        const SimResult gripping = SimRun(u);
        u.cutTop = false;
        u.rise = 0.0f;
        const SimResult grippingOld = SimRun(u);
        char line[1600];
        int len = snprintf(line, sizeof line, "the free wheel in L- floored (a kick of 50 rad/s a step): the old outputs pinned %.0f %% of the time, the fastest wheel %.0f rad/s, "
                                              "%.1f falls a second; the finite top alone %.0f %%, %.0f rad/s, %.1f a second; with the pace %.3f a step %.0f %%, %.1f rad/s "
                                              "(%.1f x the top), %.1f a second (at 0.04 a step %.0f %%, %.1f rad/s); all gripping %.0f %% at %.2f m/s on the ground (the old outputs %.0f %% at %.2f);",
                           100.0f * old.pinned, old.wMax, old.cutsPerSecond, 100.0f * brakeOnly.pinned, brakeOnly.wMax, brakeOnly.cutsPerSecond, rise, 100.0f * paced.pinned,
                           paced.wMax, paced.wMax / 2.9f, paced.cutsPerSecond, 100.0f * quick.pinned, quick.wMax, 100.0f * gripping.pinned, gripping.vEnd, 100.0f * grippingOld.pinned, grippingOld.vEnd);
        bool ok = old.pinned > 0.5f && paced.wMax < 5.0f * 2.9f && paced.pinned < 0.05f;
        // the loop's grounded cases with the brake and the pace (drive 80, MaxDeltaAngVel 0.05, L- of 2.74)
        SimSetup g = {};
        g.drive = 80.0f;
        g.mdav = 0.05f;
        g.wheels = 4;
        g.seconds = 20.0f;
        g.seed = 7;
        g.inertia = 10.0f;
        g.cutTop = true;
        g.rise = rise;
        g.mode = kSimCut;
        g.slot = sl;
        const CutBand lowBand = CutGovernBand(2.74f, kTachOverrun);
        g.slot.map.top = 2.74f;
        g.slot.map.redline = 2500.0f;
        g.slot.map.governFrom = lowBand.from;
        g.slot.map.governTo = lowBand.to;
        const SimGround grounds[2] = { { 5.0f, 0.3f, 1.0f, 1.5f, 0.0f }, { 30.0f, 0.15f, 0.5f, 1.0f, 0.0f } };
        const char *names[2] = { "soft mud", "firm mud" };
        for (int gk = 0; gk < 2; gk++)
            for (int from = 0; from < 2; from++)
            {
                g.ground = grounds[gk];
                g.s0 = from ? 0.55f * 2.74f : 0.0f;
                g.v0 = from ? 0.3f : 0.0f;
                g.used0 = from ? 1.0f : 0.0f; // S1: rolling, the take-over starts at the game's own pull, floored 1
                const SimResult c = SimRun(g);
                SimSetup flatSetup = g;
                flatSetup.mode = kSimFlat;
                flatSetup.rise = 0.0f;
                flatSetup.flat = (c.fMin + c.fMax) * 0.5f;
                const SimResult flat = SimRun(flatSetup);
                len += snprintf(line + len, sizeof line - len, " %s from %.2f%s with both: settled %.2f s (the lead's 8 s %s), x %.3f, sd %.4f of the top (F held flat %.4f), falls %d "
                                                               "after;", names[gk], from ? 0.55f : 0.0f, from ? " (the pace from the game's pull)" : "", c.settledAt, c.settledAt >= 0.0f && c.settledAt <= 8.0f ? "met" : "missed",
                                c.mean / 2.74f, c.sd / 2.74f, flat.sd / 2.74f, c.cutsAfter);
                // the lead's bounds: no falls, settled within 8 s, the spread
                ok = ok && c.cutsAfter == 0 && c.settledAt >= 0.0f && c.settledAt <= 8.0f && c.sd <= 1.25f * flat.sd + 0.002f * 2.74f;
            }
        Check(ok, line);
    }

    // the wheel speed cap (BRIEF_drivetrain_2c_wheel_cap.md, the Reviewer's addendum, the lead's S1): the pieces, then the
    // sketch (R4)
    {
        auto nearly = [](float x, float y) { return fabsf(x - y) <= 1e-4f * fmaxf(1.0f, fabsf(y)); };
        // the axle: one of length 2 and a unit one, each wheel's spin about it in the game's measure (dot(omega, axle), as
        // + 0x174) brought to the cap 3 from 4.2: both by the same 1.2, the parts across the axle as they were
        CapVec o1 = { 4.2f, 0.3f, -0.2f }, o2 = { 2.1f, 0.3f, -0.2f };
        const CapVec a1 = { 1.0f, 0.0f, 0.0f }, a2 = { 2.0f, 0.0f, 0.0f };
        const float b1 = CapSpeed(o1, a1, 1.0f), b2 = CapSpeed(o2, a2, 1.0f);
        const bool moved = CapMove(o1, a1, 1.0f, 3.0f - b1) && CapMove(o2, a2, 1.0f, 3.0f - b2);
        const bool axle = moved && nearly(b1, 4.2f) && nearly(b2, 4.2f) && nearly(CapSpeed(o1, a1, 1.0f), 3.0f) && nearly(CapSpeed(o2, a2, 1.0f), 3.0f) && o1.y == 0.3f && o1.z == -0.2f &&
                          o2.y == 0.3f && o2.z == -0.2f && nearly(o2.x, 1.5f) && !CapMove(o1, CapVec{ 0.0f, 0.0f, 0.0f }, 1.0f, 1.0f);
        // R: the gear's direction is the sign
        CapVec r1 = { -4.2f, 0.0f, 0.0f };
        CapMove(r1, a1, -1.0f, 3.0f - CapSpeed(r1, a1, -1.0f));
        const bool reverseAxle = nearly(r1.x, -3.0f);
        // CapStep (M1 and S1): two wheels over the cap 3 after a push of 1 each, one under it; the spread hands the two's
        // excess impulse (0.5 / 0.2 + 0.5 / 0.1) to the third through its gain 0.1: 2.75. Without the spread the third
        // keeps 2. A wheel the drive pushed back (against the gear) takes nothing
        const float pre[3] = { 2.5f, 2.5f, 1.0f }, post[3] = { 3.5f, 3.5f, 2.0f }, gain[3] = { 0.2f, 0.1f, 0.1f };
        const bool all[3] = { true, true, true };
        const CapStepOut sp = CapStep(pre, post, gain, all, 3, 3.0f, true), no = CapStep(pre, post, gain, all, 3, 3.0f, false);
        const float preB[3] = { 2.5f, 1.0f, 1.0f }, postB[3] = { 3.5f, 0.5f, 2.0f };
        const CapStepOut against = CapStep(preB, postB, gain, all, 3, 3.0f, true);
        // no wheel pushed with the gear: nothing spread to those pushed back
        const float preD[3] = { 2.5f, 1.0f, 1.0f }, postD[3] = { 3.5f, 0.5f, 0.5f };
        const CapStepOut noTaker = CapStep(preD, postD, gain, all, 3, 3.0f, true);
        const bool spread = noTaker.final[1] == 0.5f && noTaker.final[2] == 0.5f && noTaker.spread == 0.0f && sp.final[0] == 3.0f && sp.final[1] == 3.0f && nearly(sp.final[2], 2.75f) && sp.clamped[0] && sp.clamped[1] && !sp.clamped[2] && nearly(sp.kept, 2.75f / 3.0f) &&
                            nearly(sp.keptEach[0], 0.5f) && sp.keptEach[2] == 1.0f && nearly(sp.excess, 0.5f) && no.final[2] == 2.0f && nearly(no.kept, 2.0f / 3.0f) &&
                            against.final[1] == 0.5f && nearly(against.final[2], 2.25f);
        // a wheel the spread would push past the cap is held at it; with no push the kept share is 1
        const float preC[2] = { 2.5f, 2.9f }, postC[2] = { 4.5f, 2.95f }, gainC[2] = { 0.1f, 0.1f };
        const CapStepOut over = CapStep(preC, postC, gainC, all, 2, 3.0f, true);
        const float still[2] = { 2.0f, 2.0f };
        const bool held = over.final[0] == 3.0f && over.final[1] == 3.0f && over.clamped[1] && CapStep(still, still, gainC, all, 2, 3.0f, true).kept == 1.0f;
        // M3: a downshift at 2 x the cap waits, arms at the guard, stays armed, and waits again after a shift
        CapArm arm = { 0x7FFFFFFF, false };
        const bool waits = !CapArmStep(arm, 1, 6.0f, true, 3.0f) && !CapArmStep(arm, 1, 3.2f, true, 3.0f) && CapArmStep(arm, 1, 3.0f, true, 3.0f) && CapArmStep(arm, 1, 6.0f, true, 3.0f) &&
                           !CapArmStep(arm, 2, 6.0f, true, 3.0f) && !CapArmStep(arm, 2, 1.0f, false, 3.0f) && !CapArmStep(arm, 0, 1.0f, true, 3.0f);
        // M5: the governor's fade by the truck's own speed: the wheels at 1.05 of the top with the truck at 0.9 of it pull
        // whole, the truck at 1.05 too gets the governed 0.5, no truck read goes by the wheels; the stand-in by what the
        // wheels kept (M1)
        TorqueCutSlot cs = {};
        cs.vehicle = 1;
        cs.mode = kCutMap;
        cs.share = 1.0f;
        cs.pedal = 1.0f;
        cs.map = TorqueCutMap{ 1.0f, 1.0f, 0.0f, 1.0f, 0.15f, 10.0f, 800.0f, 2000.0f, 1.6f, { 2, { 0.1f, 1.5f }, { 1.0f, 1.0f } }, 1.0f, 1.1f };
        const bool d4[4] = { true, true, true, true };
        const float fast[4] = { 10.5f, 10.5f, 10.5f, 10.5f };
        const float governed = TorqueCutAnswerOf(cs, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, TorqueCutSpeeds(fast, d4, 4, 1)).f;
        CutSpeeds slowTruck = TorqueCutSpeeds(fast, d4, 4, 1), fastTruck = slowTruck, backTruck = slowTruck, noTruck = slowTruck;
        CutSpeedsTruck(slowTruck, 4.5f, 0.5f, 1);  // 9 rad/s, 0.9 of the top
        CutSpeedsTruck(fastTruck, 5.25f, 0.5f, 1); // 10.5
        CutSpeedsTruck(backTruck, 4.5f, 0.5f, -1); // -9 in R's direction: under the top
        CutSpeedsTruck(noTruck, NAN, 0.5f, 1);
        TorqueCutAnswer capped = TorqueCutAnswerOf(cs, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, slowTruck);
        const float truckFast = TorqueCutAnswerOf(cs, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, fastTruck).f;
        const float truckNone = TorqueCutAnswerOf(cs, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, noTruck).f;
        TorqueCutAnswerPaced(capped, 2.0f, 1.0f, 1.0f, 0.25f);
        const bool answer = nearly(governed, 0.5f) && nearly(capped.f, 1.0f) && nearly(truckFast, 0.5f) && nearly(truckNone, 0.5f) && !noTruck.truckOk && nearly(backTruck.truck, -9.0f) &&
                            nearly(capped.torque, 2.0f) && nearly(capped.standIn, 0.25f) && nearly(CapKeptSmooth(1.0f, 0.5f), 0.9f);
        // the gate, one condition (the user: "L gears and R"): R and the low range; not a numbered gear, A, N or H
        const bool gate = !GearboxPosCapped(kPosGear) && GearboxPosCapped(kPosR) && GearboxPosCapped(kPosLowMinus) && GearboxPosCapped(kPosLow) && GearboxPosCapped(kPosLowPlus) &&
                          !GearboxPosCapped(kPosA) && !GearboxPosCapped(kPosN) && !GearboxPosCapped(kPosH);
        // the curve's tail (the review of bc224bf, M1): the wheels at 1.55 of the top, half way down the tail from the
        // curve's last point (1.5) to the needle's top (1.6), the truck slow: with no guard (a numbered gear) F 0.5, the tail
        // kept; with the guard in the slot F 1, the curve held to the redline
        const float tailWheels[4] = { 15.5f, 15.5f, 15.5f, 15.5f };
        CutSpeeds tailSp = TorqueCutSpeeds(tailWheels, d4, 4, 1);
        CutSpeedsTruck(tailSp, 4.5f, 0.5f, 1);
        const float tailNoGuard = TorqueCutAnswerOf(cs, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, tailSp).f;
        TorqueCutSlot guarded = cs;
        guarded.cap = 15.0f;
        const float tailGuard = TorqueCutAnswerOf(guarded, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, tailSp).f;
        const bool tail = nearly(tailNoGuard, 0.5f) && nearly(tailGuard, 1.0f);
        char capLine[800];
        snprintf(capLine, sizeof capLine, "wheel cap: an axle of length 2 and a unit one both from 4.2 to 3 in the game's measure, the parts across them kept, R by the sign; held wheels "
                                          "3 and 3, the third 2.75 with the spread (2 without), kept %.3f (%.3f), nothing to a wheel pushed against the gear; a downshift at 2 x the "
                                          "cap waits until the guard, a shift waits again; the wheels at 1.05 of the top: F %.2f by them, %.2f with the truck at 0.9, %.2f at 1.05; "
                                          "the stand-in %.2f at kept 0.25; the guard in R and the low range alone, not a numbered gear; the wheels on the curve's tail F %.2f "
                                          "with no guard, %.2f with it",
                 sp.kept, no.kept, governed, capped.f, truckFast, capped.standIn, tailNoGuard, tailGuard);
        Check(axle && reverseAxle && spread && held && waits && answer && gate && tail, capLine);

        // R4, the sketch: L- with a top of 2.9 floored, the brake (F1) and the pace (F1b) as above, today (the governor by
        // the wheels, no cap) against M5 (the governor by the truck's own speed, the guard at 1.5); the scout (a kick of 50 rad/s a step) with a wheel free, in deep mud, on a road; the heavy (6 a step) with a
        // wheel free; the foot off down a grade entered at the top; a downshift floored on a road at 2 x the new top with
        // M3, with the cap armed at once and today (armed at once was the counter-case while the step set a wheel over
        // the cap down to it; since BRIEF_governor_sway.md it lowers none, so the two are the same)
        SimSetup u = {};
        u.mdav = 0.01f;
        u.wheels = 4;
        u.seconds = 30.0f;
        u.seed = 11;
        u.inertia = 10.0f;
        u.cutTop = true;
        u.rise = 0.04f * 0.2f;
        u.mode = kSimCut;
        u.governTruck = true;
        TorqueCutSlot &sl = u.slot;
        sl.vehicle = 1;
        sl.mode = kCutMap;
        sl.share = 1.0f;
        sl.pedal = 1.0f;
        const CutBand band = CutGovernBand(2.9f, kTachOverrun);
        const float guard = (float)kCapDefault / 100.0f;
        sl.map = TorqueCutMap{ 1.0f, 1.0f, 0.0f, 1.0f, 0.15f, 2.9f, 800.0f, 2500.0f, 1.2f, { 5, { 0.25f, 0.45f, 0.6f, 0.8f, 1.0f }, { 0.55f, 0.85f, 1.0f, 1.0f, 0.85f } },
                               band.from, band.to };
        const SimGround mud = { 5.0f, 0.3f, 1.0f, 1.5f, 0.0f }, road = { 60.0f, 0.1f, 0.05f, 0.05f, 0.0f };
        struct Case
        {
            const char *name;
            float drive;
            int free;
            SimGround ground;
        };
        const Case cases[4] = { { "the scout with a wheel free", 3000.0f, 1, mud }, { "the scout in deep mud", 3000.0f, 0, mud }, { "the scout on a road", 3000.0f, 0, road },
                                { "the heavy with a wheel free", 360.0f, 1, mud } };
        char line[2400];
        int len = snprintf(line, sizeof line, "wheel cap, the sketch (x the top 2.9, the last 20 s of 30):");
        bool ok = true;
        for (int c = 0; c < 4; c++)
        {
            u.drive = cases[c].drive;
            u.freeWheels = cases[c].free;
            u.ground = cases[c].ground;
            u.cap = 0.0f;
            u.governTruck = false;
            const SimResult today = SimRun(u);
            u.governTruck = true;
            const SimResult fadeOnly = SimRun(u);
            u.cap = guard;
            const SimResult cap = SimRun(u);
            len += snprintf(line + len, sizeof line - len, " %s: today the fastest wheel %.2f, x %.2f, sd %.3f, the truck %.2f; the fade on the truck alone: the fastest %.2f; with the "
                                                           "guard: into the ground at most %.3f, x %.2f, sd %.3f, the truck %.2f, kept %.2f, falls %d;",
                            cases[c].name, today.wMax / 2.9f, today.mean / 2.9f, today.sd / 2.9f, today.vEnd, fadeOnly.wMax / 2.9f, cap.wIn / 2.9f, cap.mean / 2.9f, cap.sd / 2.9f,
                            cap.vEnd, cap.kept, cap.cutsAfter);
            ok = ok && cap.wIn <= 2.9f * guard + 1e-4f && cap.cutsAfter == 0;
        }
        // the gear ratio pull (BRIEF_jobs_1114.md, 161): the law's figures, the roof, the detour's step and the sketch
        {
            const float refA = 14.35f;
            bool roofed = true;
            const float f1 = GearRatioFactor(2.54f, refA, 1.0f, kRatioPullRoof, &roofed), f3 = GearRatioFactor(8.62f, refA, 1.0f), fTop = GearRatioFactor(refA, refA, 1.0f);
            const bool figures = Near(f1, 1.406f, 0.002f) && !roofed && Near(f3, 1.1745f, 0.002f) && fTop == 1.0f && Near(GearRatioFactor(1.675f, refA, 1.0f), 1.507f, 0.002f);
            const bool half = Near(GearRatioFactor(2.54f, refA, 0.5f), 1.0f + (f1 - 1.0f) * 0.5f, 1e-5f) && Near(GearRatioFactor(2.54f, refA, 7.0f), f1, 1e-6f);
            // a gear so low that the law asks more than the roof: a of 0.5 against a reference of 30 asks 3.2
            const float deep = GearRatioFactor(0.5f, 30.0f, 1.0f, kRatioPullRoof, &roofed);
            const bool roof = deep == kRatioPullRoof && roofed && Near(GearRatioFactor(0.5f, 30.0f, 0.5f), 1.5f, 1e-5f) && GearRatioFactor(0.5f, 30.0f, 1.0f, 1.2f) == 1.2f;
            // never under 1: H's own a above the reference's, and every a from 0.2 to 60 against every reference
            bool never = GearRatioFactor(20.0f, refA, 1.0f, kRatioPullRoof, &roofed) == 1.0f && !roofed;
            for (float gearA = 0.2f; gearA < 60.0f; gearA += 0.37f)
                for (float topA = 0.2f; topA < 60.0f; topA += 1.3f)
                    for (int sh = 0; sh <= 4; sh++)
                    {
                        const float f = GearRatioFactor(gearA, topA, sh / 4.0f);
                        never = never && f >= 1.0f && f <= kRatioPullRoof && (sh || f == 1.0f);
                    }
            const bool none = GearRatioFactor(NAN, refA, 1.0f) == 1.0f && GearRatioFactor(2.54f, NAN, 1.0f) == 1.0f && GearRatioFactor(2.54f, refA, NAN) == 1.0f &&
                              GearRatioFactor(0.0f, refA, 1.0f) == 1.0f && GearRatioFactor(2.54f, 0.0f, 1.0f) == 1.0f && GearRatioFactor(-3.0f, refA, 1.0f) == 1.0f &&
                              GearRatioFactor(2.54f, refA, -1.0f) == 1.0f && GearRatioFactor(INFINITY, refA, 1.0f) == 1.0f;
            Check(figures && half && roof && never && none,
                  "gear ratio pull: the law's figures against a top gear of 14.35 (x1.406 at 2.54, x1.175 at 8.62, 1 at the reference), half way at 50, the roof and the flag, never "
                  "under 1 over every a, reference and share, exactly 1 at a share of 0, and 1 where a figure is no number or not above 0");
            // the law itself: with the game's T as the engine's over the root of max(0.5 a, 1), the torque times the game's top is the same in every gear at 100
            bool law = true;
            const float gears[4] = { 4.2f, 6.1f, 8.62f, 11.0f };
            for (int i = 0; i < 4; i++)
            {
                const float tGame = 1000.0f / sqrtf(fmaxf(0.5f * gears[i], 1.0f)), tRef = 1000.0f / sqrtf(0.5f * refA);
                law = law && Near(tGame * GearRatioFactor(gears[i], refA, 1.0f) * (2.0f * gears[i] + 5.0f), tRef * (2.0f * refA + 5.0f), 0.05f);
            }
            // must 1: at 0 the detour's step writes neither the torque nor the slot
            TorqueCutSlot base = u.slot;
            base.map.load = 0.08f, base.floor = 0.12f, base.ratioRefA = refA, base.ratioShare = 0.0f;
            TorqueCutSlot atZero = base;
            float tOff = 257000.37f;
            const float fOff = GearRatioApply(atZero, tOff, 2.54f);
            const bool asBefore = fOff == 1.0f && tOff == 257000.37f && !memcmp(&atZero, &base, sizeof atZero);
            // must 2: at 100 the air drag and the creep's floor are the same force, and the engine's braking grows by the factor
            TorqueCutSlot on = base;
            on.ratioShare = 1.0f;
            float tOn = 257000.0f;
            const float fOn = GearRatioApply(on, tOn, 2.54f, &roofed);
            const bool forces = Near(fOn, f1, 1e-6f) && Near(tOn, 257000.0f * f1, 1.0f) && Near(on.map.load * tOn, base.map.load * 257000.0f, 1.0f) &&
                                Near(on.floor * tOn, base.floor * 257000.0f, 1.0f);
            TorqueCutSlot lift = base, liftOn = base;
            lift.map.load = liftOn.map.load = 0.0f, lift.floor = liftOn.floor = 0.0f, lift.map.pedal = liftOn.map.pedal = 0.0f, lift.pedal = liftOn.pedal = 0.0f;
            liftOn.ratioShare = 1.0f;
            float tLift = 257000.0f;
            GearRatioApply(liftOn, tLift, 2.54f);
            const CutSpeeds rolling = { 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, true };
            const TorqueCutAnswer brake0 = TorqueCutAnswerOf(lift, 257000.0f, 0.0f, 2.54f, 10.08f, 1.0f, 1.0f, rolling);
            const TorqueCutAnswer brake1 = TorqueCutAnswerOf(liftOn, tLift, 0.0f, 2.54f, 10.08f, 1.0f, 1.0f, rolling);
            const bool braking = brake0.torque < 0.0f && Near(brake1.torque, brake0.torque * f1, fabsf(brake0.torque) * 1e-4f);
            // a shift 2 to 3 at 100: each step is its own gear's, by the a the game answers for
            TorqueCutSlot shift = on;
            float t2 = 1000.0f / sqrtf(0.5f * 6.1f), t3 = 1000.0f / sqrtf(0.5f * 8.62f);
            const float g2 = GearRatioApply(shift, t2, 6.1f);
            shift = on;
            const float g3 = GearRatioApply(shift, t3, 8.62f);
            const bool shiftOk = g2 > g3 && Near(g3, f3, 1e-6f) && Near(t2 * (2.0f * 6.1f + 5.0f), t3 * (2.0f * 8.62f + 5.0f), 0.05f);
            Check(law && asBefore && forces && braking && shiftOk,
                  "gear ratio pull: at 100 the torque times the game's top is the same in every gear; at 0 the detour's step leaves the torque and the slot bit for bit; at 100 the "
                  "air drag and the creep's floor stay the same force and the engine's braking grows by the factor; a shift from 2 to 3 takes each step's factor from its own gear");
            // 161b: R and the low positions get no more than gear 1 gets, by the smaller of two factors
            {
                const float firstA = 2.54f;
                bool lowAll = true, numbered = true;
                const float lowA[2] = { 1.0f, 0.5f };
                for (int i = 0; i < 2; i++)
                    for (int sh = 1; sh <= 2; sh++)
                    {
                        bool isRoofed = true, isHeld = false;
                        const float share = sh / 2.0f, got = GearRatioHeld(lowA[i], refA, firstA, true, share, kRatioPullRoof, &isRoofed, &isHeld);
                        lowAll = lowAll && got == GearRatioFactor(firstA, refA, share) && got < kRatioPullRoof && isHeld && !isRoofed;
                    }
                // without the hold the same figures read their own, the roof among them: the numbered gears are not touched
                bool ownRoofed = false, ownHeld = true;
                lowAll = lowAll && GearRatioHeld(0.5f, refA, firstA, false, 1.0f, kRatioPullRoof, &ownRoofed, &ownHeld) == kRatioPullRoof && ownRoofed && !ownHeld;
                const float everyA[5] = { 2.54f, 4.2f, 6.1f, 8.62f, refA };
                for (int i = 0; i < 5; i++)
                {
                    TorqueCutSlot gearSlot = base;
                    gearSlot.ratioShare = 1.0f, gearSlot.ratioFirstA = firstA, gearSlot.ratioHold = false;
                    float tg = 1000.0f;
                    numbered = numbered && GearRatioApply(gearSlot, tg, everyA[i]) == GearRatioFactor(everyA[i], refA, 1.0f) && tg == 1000.0f * GearRatioFactor(everyA[i], refA, 1.0f);
                }
                // a low figure above gear 1's with a smaller factor keeps its own; one between 2.0 and 2.5 against a gear 1 of 2.0 has
                // the larger factor though the larger figure, and is held; gear 1's own figure is not "held"; no gear 1: a factor of 1
                bool hOwn = true, hRise = false, hSame = true, hNone = false;
                const bool edges = GearRatioHeld(4.2f, refA, firstA, true, 1.0f, kRatioPullRoof, nullptr, &hOwn) == GearRatioFactor(4.2f, refA, 1.0f) && !hOwn &&
                                   GearRatioFactor(2.5f, refA, 1.0f) > GearRatioFactor(2.0f, refA, 1.0f) &&
                                   GearRatioHeld(2.5f, refA, 2.0f, true, 1.0f, kRatioPullRoof, nullptr, &hRise) == GearRatioFactor(2.0f, refA, 1.0f) && hRise &&
                                   GearRatioHeld(firstA, refA, firstA, true, 1.0f, kRatioPullRoof, nullptr, &hSame) == f1 && !hSame &&
                                   GearRatioHeld(1.0f, refA, 0.0f, true, 1.0f, kRatioPullRoof, nullptr, &hNone) == 1.0f && hNone &&
                                   GearRatioHeld(1.0f, refA, NAN, true, 1.0f) == 1.0f && GearRatioHeld(1.0f, refA, firstA, true, 0.0f) == 1.0f;
                // the detour's step with the hold: the slot's own shares go by the factor handed, and at 0 nothing is written
                TorqueCutSlot lowSlot = base, lowZero = base;
                lowSlot.ratioShare = 1.0f, lowSlot.ratioFirstA = lowZero.ratioFirstA = firstA, lowSlot.ratioHold = lowZero.ratioHold = true;
                const TorqueCutSlot lowZeroWas = lowZero;
                float tLow = 257000.0f, tLowZero = 257000.37f;
                bool lowRoofed = true, lowHeld = false;
                const float fLow = GearRatioApply(lowSlot, tLow, 0.5f, &lowRoofed, &lowHeld);
                const bool step = fLow == f1 && lowHeld && !lowRoofed && Near(lowSlot.map.load * tLow, base.map.load * 257000.0f, 1.0f) && Near(lowSlot.floor * tLow, base.floor * 257000.0f, 1.0f) &&
                                  GearRatioApply(lowZero, tLowZero, 0.5f) == 1.0f && tLowZero == 257000.37f && !memcmp(&lowZero, &lowZeroWas, sizeof lowZero);
                Check(lowAll && numbered && edges && step,
                      "gear ratio pull, R and the low positions: with an a of 1.0 and of 0.5 they read gear 1's factor at 50 and at 100, never the roof, and its flag is not set; "
                      "the numbered gears from 1 to the top read their own as before; a held figure with the smaller factor keeps its own and one with the larger is held though "
                      "its figure is the larger; no gear 1 gives 1; at 0 the detour's step writes nothing");
                // T1, the learner under the factor: the detour's sum carries the bracket times the factor, so a row learned at 100 lands on the base learned at 0
                CutLearnTotals was0 = { 0.0, 0, 0, 7 }, now0 = was0, was100 = was0, now100 = was0;
                const float bracket = 0.8f, accel0 = 3.0f; // rad/s^2 at a bracket of 0.8 with the game's own torque
                for (int stepI = 0; stepI < 6; stepI++)
                {
                    now0.sum = CutLearnAdd(now0.sum, bracket, 1.0f), now0.n++;
                    now100.sum = CutLearnAdd(now100.sum, bracket, f1), now100.n++;
                }
                const float d0 = CutLearnDivisor(was0, now0, 7), d100 = CutLearnDivisor(was100, now100, 7);
                Check(Near(d0, bracket, 1e-5f) && Near(d100, bracket * f1, 1e-5f) && Near(accel0 * f1 / d100, accel0 / d0, 1e-4f) && CutLearnAdd(1.5, NAN, 1.2f) == 1.5 &&
                          CutLearnAdd(1.5, 0.5f, NAN) == 1.5,
                      "gear ratio pull, the learner: the detour's sum carries the bracket times the factor, so the acceleration over the divisor at 100 is the same base as at 0; a "
                      "bracket or a factor that is no number adds nothing");
            }
            // the review of 162 part A, S1: the slot's cap is GearboxPosCap of the position, and in A that is 0 at every
            // Held gear slip choice (All gears is the default), every cap and every top, so the wheel cap never acts in A
            {
                bool aNoCap = true;
                const float capTops[3] = { 2.9f, 10.0f, 40.0f }, capShares[3] = { 1.1f, 1.5f, 3.0f };
                for (int ch = 0; ch < kSlipCount; ch++)
                    for (int ti = 0; ti < 3; ti++)
                        for (int ci = 0; ci < 3; ci++) aNoCap = aNoCap && GearboxPosCap(kPosA, ch, capShares[ci], capTops[ti], 2.9f) == 0.0f;
                Check(aNoCap && !GearboxPosCapped(kPosA), "torque cut in A: the slot's cap is 0 in A at every Held gear slip choice, at every cap and top");
            }
            // must 5, the sketch: the scout with a wheel free and on a road in L- under the guard, at 0 and at the roof. The
            // wheel handed to the ground never passes the ceiling, and on a free wheel the cap keeps a smaller share of the larger push
            char pullLine[900];
            int pl = snprintf(pullLine, sizeof pullLine, "gear ratio pull, the sketch (L-, the guard at %.2f x the top 2.9):", guard);
            bool pullOk = true;
            for (int c = 0; c < 3; c += 2)
            {
                SimSetup p = u;
                p.freeWheels = cases[c].free;
                p.ground = cases[c].ground;
                p.cap = guard;
                p.governTruck = true;
                p.drive = cases[c].drive;
                const SimResult at0 = SimRun(p);
                p.slot.ratioRefA = 30.0f, p.slot.ratioShare = 1.0f;
                const float pf = GearRatioApply(p.slot, p.drive, 0.5f);
                const SimResult at100 = SimRun(p);
                pl += snprintf(pullLine + pl, sizeof pullLine - pl, " %s: at 0 into the ground at most %.3f, kept %.3f, the truck %.2f; at x%.1f: %.3f, kept %.3f, the truck %.2f;",
                               cases[c].name, at0.wIn / 2.9f, at0.kept, at0.vEnd, pf, at100.wIn / 2.9f, at100.kept, at100.vEnd);
                pullOk = pullOk && pf == kRatioPullRoof && at100.wIn <= 2.9f * guard + 1e-4f && at0.wIn <= 2.9f * guard + 1e-4f && at100.cutsAfter == 0;
                if (cases[c].free) pullOk = pullOk && at100.kept < at0.kept;
            }
            // the stand-in for B4's readers follows the kept share (the fuel's side stays the game's rule)
            TorqueCutAnswer keptMore = {}, keptLess = {};
            keptMore.bracket = keptLess.bracket = 1.0f;
            TorqueCutAnswerPaced(keptMore, 1000.0f, 1.0f, 1.0f, 0.8f);
            TorqueCutAnswerPaced(keptLess, 2000.0f, 1.0f, 1.0f, 0.4f);
            pullOk = pullOk && keptLess.standIn < keptMore.standIn && Near(keptLess.standIn, keptMore.standIn * 0.5f, 1e-4f);
            puts(pullLine);
            Check(pullOk, "gear ratio pull, the sketch: with the push doubled the wheel handed to the ground never passes the guard's ceiling, a free wheel keeps a smaller share, "
                          "the stand-in follows the kept share, and no fall of the bracket");
        }
        // the open converter in the detour through the sketch (162 part B; the brief's drive step 2, A in mud with the
        // wheels spinning): floored in A, the first gear of a top of 6 rad/s, no cap in A. The scout with a wheel free, in
        // deep mud and on a road: no fall of the bracket by 0.3 in a step, and the pull stays up
        {
            char convLine[700];
            int cl = snprintf(convLine, sizeof convLine, "torque cut in A, part B, the sketch (the open converter on this step's wheels, top 6):");
            bool convOk = true;
            for (int c = 0; c < 3; c++)
            {
                SimSetup inA = u;
                inA.drive = cases[c].drive, inA.freeWheels = cases[c].free, inA.ground = cases[c].ground;
                inA.cap = 0.0f, inA.governTruck = true;
                inA.slot.mode = kCutScalar, inA.slot.scalar = NAN, inA.slot.convOpen = true, inA.slot.convStall = 1600.0f, inA.slot.convRise = kStallRise;
                inA.slot.map.top = 6.0f, inA.slot.map.idle = 800.0f, inA.slot.map.redline = 2000.0f;
                const CutBand aBand = CutGovernBand(6.0f, kTachOverrun);
                inA.slot.map.governFrom = aBand.from, inA.slot.map.governTo = aBand.to;
                const SimResult open = SimRun(inA);
                cl += snprintf(convLine + cl, sizeof convLine - cl, " %s: the bracket %.2f to %.2f, falls of 0.3 in a step %d, of 0.15 %d, the wheels x %.2f of the top, sd %.3f, the truck %.2f;",
                               cases[c].name, open.fMin, open.fMax, open.cutsAfter, open.smallFalls, open.mean / 6.0f, open.sd / 6.0f, open.vEnd);
                convOk = convOk && open.cutsAfter == 0 && open.smallFalls == 0 && open.fMin >= 0.0f && open.fMax <= kStallRatio + 1e-4f;
            }
            puts(convLine);
            Check(convOk, "torque cut in A, part B, the sketch: with the open converter worked out on each step's wheels a free wheel, deep mud and a road give no fall of the bracket "
                          "by 0.15 in a step; the pull is never under 0 and never over the stall's ratio, and at the gear's top it is the governor's 0");
        }
        // a numbered gear (no guard), a free wheel, floored, the fade on the truck's speed: the curve's tail is the limit
        // (the review of bc224bf, M1). 2nd with a top of 10 rad/s and the scout's kick; against the governor on the wheels
        // (618410d's). The bound: no worse than 618410d by a tenth, no falls; the review's 1.2 x the needle's stop is said
        // met or missed (the scout's one-step kick runs a free wheel past it in this sketch with either governor: the peak
        // before the game's brake takes it back)
        {
            SimSetup num = u;
            num.drive = 3000.0f;
            num.freeWheels = 1;
            num.ground = mud;
            num.cap = 0.0f;
            const CutBand numBand = CutGovernBand(10.0f, kTachOverrun);
            num.slot.map.top = 10.0f;
            num.slot.map.governFrom = numBand.from;
            num.slot.map.governTo = numBand.to;
            num.governTruck = false;
            const SimResult wheelsGov = SimRun(num);
            num.governTruck = true;
            const SimResult nr = SimRun(num);
            len += snprintf(line + len, sizeof line - len, " 2nd (a top of 10) with a wheel free, no guard: the fastest wheel %.2f of the top with the tail kept (the governor on the wheels "
                                                           "%.2f; 1.2 x the needle's stop %s), x %.2f (%.2f), falls %d;",
                            nr.wMax / 10.0f, wheelsGov.wMax / 10.0f, nr.wMax < 1.2f * kTachOverrun * 10.0f ? "met" : "missed", nr.mean / 10.0f, wheelsGov.mean / 10.0f, nr.cutsAfter);
            ok = ok && nr.wMax <= 1.1f * wheelsGov.wMax && nr.cutsAfter == 0;
        }
        // floored up a grade on hard ground (the Reviewer's note: the guard's margin at the top is (WheelCap - 100) % x the
        // top x the wheels' inertia; at 110 a grade sags there): today, the guard at 110 and at 150
        SimSetup climb = u;
        climb.drive = 3000.0f;
        climb.freeWheels = 0;
        climb.seconds = 30.0f;
        float climbV[3] = {}, climbX[3] = {};
        const float climbCaps[3] = { 0.0f, 1.1f, guard };
        const float grades[2] = { -0.3f, -0.6f };
        for (int g2 = 0; g2 < 2; g2++)
        {
            climb.ground = SimGround{ 60.0f, 0.1f, 0.05f, 0.05f, grades[g2] };
            for (int ck = 0; ck < 3; ck++)
            {
                climb.cap = climbCaps[ck];
                climb.governTruck = ck > 0;
                const SimResult cr = SimRun(climb);
                climbV[ck] = cr.vEnd / 2.9f;
                climbX[ck] = cr.mean / 2.9f;
                ok = ok && (ck == 0 || cr.cutsAfter == 0);
            }
            len += snprintf(line + len, sizeof line - len, " the scout floored up a grade of %.1f: the truck at %.2f of the top today (x %.2f), %.2f with the guard at 110 (x %.2f), %.2f at 150 "
                                                           "(x %.2f);",
                            -grades[g2], climbV[0], climbX[0], climbV[1], climbX[1], climbV[2], climbX[2]);
        }
        // the foot off down a grade, entered at the top
        SimSetup hill = u;
        hill.drive = 360.0f;
        hill.freeWheels = 0;
        hill.ground = SimGround{ 60.0f, 0.1f, 0.05f, 0.05f, 0.6f };
        hill.slot.pedal = hill.slot.map.pedal = 0.0f;
        hill.s0 = hill.v0 = 2.9f;
        hill.seconds = 20.0f;
        hill.cap = 0.0f;
        hill.governTruck = false;
        const SimResult hillToday = SimRun(hill);
        hill.governTruck = true;
        hill.cap = guard;
        const SimResult hillCap = SimRun(hill);
        // a downshift floored on a road at 2 x the new top
        SimSetup down = u;
        down.drive = 360.0f;
        down.freeWheels = 0;
        down.ground = road;
        down.s0 = down.v0 = 2.0f * 2.9f;
        down.seconds = 5.0f;
        down.s0 = down.v0 = 2.0f * guard * 2.9f;
        down.cap = 0.0f;
        down.governTruck = false;
        const SimResult downToday = SimRun(down);
        down.governTruck = true;
        down.cap = guard;
        const SimResult downWait = SimRun(down);
        down.capAtOnce = true;
        const SimResult downOnce = SimRun(down);
        len += snprintf(line + len, sizeof line - len, " the heavy foot off down a grade from the top: %.2f today, %.2f with M5; a downshift at 2 x the guard, the truck at 0.5 s %.2f "
                                                       "today, %.2f with the guard waiting (M3), %.2f armed at once (the same: the step lowers no wheel)",
                        hillToday.vEnd / 2.9f, hillCap.vEnd / 2.9f, downToday.vHalf, downWait.vHalf, downOnce.vHalf);
        ok = ok && hillCap.vEnd <= hillToday.vEnd + 0.05f && fabsf(downWait.vHalf - downOnce.vHalf) < 1e-3f && downWait.vHalf > downToday.vHalf - 0.01f;
        Check(ok, line);
    }

    // the governor's sway (BRIEF_governor_sway.md): the top of a low gear held by a ceiling on each driven wheel, by the
    // truck's speed, in place of the governor's factor on F. TopRun (torque_cut_sim.h) with the law and, as the counter
    // case, the law before it. Speeds over the gear's top.
    {
        TopSetup u = {};
        u.slot.vehicle = 1, u.slot.mode = kCutMap, u.slot.share = 1.0f, u.slot.pedal = 1.0f;
        u.cap = 1.5f, u.wheels = 2, u.kick = 500.0f, u.mdav = 0.01f, u.inertia = 10.0f, u.flat = -1.0f, u.holdV = -1.0f;
        u.seconds = 20.0f, u.rise = 0.008f, u.seed = 7, u.gripNoise = 0.3f, u.dragNoise = 0.3f, u.couple = 1.0f / 60.0f;
        static char line[6000];
        auto mapOf = [](float top) {
            const CutBand band = CutGovernBand(top, kTachOverrun);
            return TorqueCutMap{ 1.0f, 1.0f, 0.0f, 1.0f, 0.15f, top, 800.0f, 2500.0f, 1.2f, { 5, { 0.25f, 0.45f, 0.6f, 0.8f, 1.0f }, { 0.55f, 0.85f, 1.0f, 1.0f, 0.85f } }, band.from, band.to };
        };
        // the pure parts: the ceiling, the step's guard, F's switch
        {
            const float cap = 3.0f, top = 2.0f, to = 1.1f, end = to * top, from = kCapCeilFrom * top;
            const bool ceiling = CapCeiling(0.0f, top, to, 1.0f) == 0.0f && CapCeiling(cap, top, to, 0.0f) == cap && CapCeiling(cap, top, to, from) == cap &&
                                 Near(CapCeiling(cap, top, to, 0.5f * (from + end)), 0.5f * (cap + end), 1e-5f) && Near(CapCeiling(cap, top, to, end), end, 1e-6f) &&
                                 Near(CapCeiling(cap, top, to, 2.0f * end), end, 1e-6f) && Near(CapCeiling(cap, top, to, NAN), end, 1e-6f) && CapCeiling(cap, 0.0f, to, 1.0f) == cap &&
                                 CapCeiling(cap, top, 0.0f, 1.0f) == cap && CapCeiling(2.0f, top, to, 2.5f) == 2.0f && CapCeiling(cap, top, to, 1.8f) < CapCeiling(cap, top, to, 1.7f);
            const float pre[3] = { 4.0f, 4.0f, 2.0f }, post[3] = { 5.0f, 3.5f, 5.0f }, gain[3] = { 1.0f, 1.0f, 1.0f };
            const bool ok[3] = { true, true, true };
            const CapStepOut o = CapStep(pre, post, gain, ok, 3, 3.0f, false);
            const bool guard = Near(o.final[0], 4.0f, 1e-6f) && Near(o.final[1], 3.5f, 1e-6f) && Near(o.final[2], 3.0f, 1e-6f);
            TorqueCutMap cm = mapOf(10.0f);
            const float full = CutCurveAt(cm.curve, 1.0f, cm.tachTop);
            const bool sw = Near(TorqueCutF(cm, 10.5f, 1.0f, NAN, true), full, 1e-5f) && Near(TorqueCutF(cm, 12.0f, 1.0f, 10.9f, true), full, 1e-5f) &&
                            TorqueCutF(cm, 10.5f, 1.0f, 11.0f, true) == 0.0f && TorqueCutF(cm, 9.0f, 1.0f, 12.0f, true) == 0.0f && TorqueCutF(cm, 11.5f, 1.0f, NAN, true) == 0.0f &&
                            Near(TorqueCutF(cm, 10.5f), 0.5f * CutCurveAt(cm.curve, 1.05f, cm.tachTop), 1e-5f);
            Check(ceiling && guard && sw, "governor ceiling, pure: the cap under 0.75 of the top, down to the governor's end at the truck's speed there and past it, halfway at the middle, the end with no "
                                          "truck speed, the cap with no top, no end or an end over it, 0 with no cap; the step leaves a wheel over the ceiling at its speed before the drive, keeps "
                                          "the drive's own slowing, holds one under it at the ceiling; F where the guard acts whole under the governor's end and 0 from it, by the truck's speed when "
                                          "given; elsewhere the fade as before");
            if (!(ceiling && guard && sw)) printf("     %d %d %d\n", ceiling, guard, sw);
        }
        // floored at the top, three low gears, the grip of 5 and 1 with half and twice each (N1), a load of half the push
        // the ground can give: after 3 s the needle within 150 rpm, the truck within 2 percent of the top around its
        // middle, the push from step to step without a jump. The law before: the needle and the push swing. The tops and
        // the start keep the wheels off a whole rad/s (the sim's limit, the last case here).
        {
            const float tops[3] = { 2.4f, 3.4f, 4.5f }, grips[6] = { 0.5f, 1.0f, 2.0f, 2.5f, 5.0f, 10.0f };
            int len = snprintf(line, sizeof line, "governor ceiling, floored at the top of a low gear for 20 s (top, grip: truck band, needle band rpm, push jump over push; the law before):");
            bool calmAll = true, swayed = true;
            for (int tI = 0; tI < 3; tI++)
                for (int g = 0; g < 6; g++)
                {
                    const float top = tops[tI];
                    TopSetup c = u;
                    c.slot.map = mapOf(top);
                    c.grip = grips[g], c.s0 = c.v0 = 0.9f * top;
                    c.drag = 0.5f * 2.0f * c.couple * fminf(c.grip, 0.45f * top);
                    const TopResult q = TopRun(c);
                    c.oldLaw = true;
                    const TopResult law0 = TopRun(c);
                    const bool good = q.needleMax - q.needleMin <= 150.0f && 0.5f * (q.vMax - q.vMin) <= 0.02f * top && q.pushJump <= 0.35f * q.push && q.vMin >= kCapCeilFrom * top &&
                                      q.vMax <= c.slot.map.governTo * top && q.lowered == 0;
                    const bool bad = law0.needleMax - law0.needleMin > 2.0f * 150.0f && law0.pushJump > q.pushJump;
                    calmAll = calmAll && good, swayed = swayed && bad;
                    len += snprintf(line + len, sizeof line - len, "%s %.1f, %.1f: %.3f to %.3f, %.0f, %.2f; %.3f to %.3f, %.0f, %.2f%s", tI || g ? ";" : "", top, c.grip, q.vMin / top, q.vMax / top,
                                    q.needleMax - q.needleMin, q.pushJump / q.push, law0.vMin / top, law0.vMax / top, law0.needleMax - law0.needleMin, law0.pushJump / law0.push, good && bad ? "" : " <<<");
                }
            Check(calmAll && swayed, line);
        }
        // a climb that takes 0.15 g (0.049 rad/s a step at a rolling radius of 0.5 m), four wheels, from rest: the truck
        // holds 0.75 of the top or more with a steady needle
        {
            TopSetup c = u;
            const float top = 2.0f;
            c.slot.map = mapOf(top);
            c.wheels = 4, c.couple = 1.0f / 30.0f, c.grip = 5.0f, c.drag = 0.0f, c.grade = -0.049f;
            const TopResult q = TopRun(c);
            c.oldLaw = true;
            const TopResult law0 = TopRun(c);
            snprintf(line, sizeof line, "governor ceiling, a climb of 0.15 g in a low gear from rest: the truck holds %.3f to %.3f of the top, the needle within %.0f rpm, no wheel lowered; the law "
                                        "before %.3f to %.3f, the needle over %.0f rpm",
                     q.vMin / top, q.vMax / top, q.needleMax - q.needleMin, law0.vMin / top, law0.vMax / top, law0.needleMax - law0.needleMin);
            Check(q.vMin >= kCapCeilFrom * top && q.needleMax - q.needleMin <= 150.0f && q.lowered == 0 && law0.needleMax - law0.needleMin > 300.0f, line);
        }
        // downhill past the top, the pedal down and then up: the cap's step lowers no wheel under what the road and the
        // drive left it (the counter case is the hook's test, where wheels over the cap were set down to it)
        {
            bool none = true, past = true;
            float reach[2] = {};
            for (int foot = 0; foot < 2; foot++)
            {
                TopSetup c = u;
                const float top = 2.0f;
                c.slot.map = mapOf(top);
                c.grip = 5.0f, c.drag = 0.005f, c.grade = 0.02f, c.s0 = c.v0 = 0.9f * top, c.footUpAt = foot ? 10.0f : 0.0f;
                const TopResult q = TopRun(c);
                none = none && q.lowered == 0;
                past = past && q.vMost > (c.slot.map.governTo + 0.5f) * top;
                reach[foot] = q.vMost / top;
            }
            snprintf(line, sizeof line, "governor ceiling, downhill past the top with the pedal down (to %.2f of the top) and with it let up at 10 s (%.2f): the cap's step lowers no wheel", reach[0],
                     reach[1]);
            Check(none && past, line);
        }
        // M4 and S1: floored from rest, into the gear at the top with the pull already whole, and after a shift down at
        // 1.6 of the top, a light load: the truck is not past the governor's end by more than 2 percent of the top once
        // the drive has it, and over that the wheels give it under 2 percent of the top in all
        {
            int len = snprintf(line, sizeof line, "governor ceiling, floored with a light load (top: from rest the most; into the gear at the top with the pull whole the most; shifted down at 1.6 "
                                                  "the end and the wheels' gift over the end, all over the top):");
            bool held = true;
            const float tops[3] = { 2.0f, 3.4f, 4.6f };
            for (int tI = 0; tI < 3; tI++)
            {
                const float top = tops[tI];
                TopSetup c = u;
                c.slot.map = mapOf(top);
                c.grip = 5.0f, c.drag = 0.0005f;
                const float limit = (c.slot.map.governTo + 0.02f) * top;
                const TopResult rest = TopRun(c);
                c.s0 = c.v0 = top, c.used0 = 1.0f;
                const TopResult into = TopRun(c);
                c.s0 = c.v0 = 1.6f * top, c.used0 = 0.0f;
                const TopResult down = TopRun(c);
                const bool good = rest.vMost <= limit && rest.vEnd > top && into.vMost <= limit && down.vEnd <= limit && down.gainOver <= 0.02f * top && rest.lowered + into.lowered + down.lowered == 0;
                held = held && good;
                len += snprintf(line + len, sizeof line - len, "%s %.1f: %.3f; %.3f; %.3f, %.4f%s", tI ? ";" : "", top, rest.vMost / top, into.vMost / top, down.vEnd / top, down.gainOver / top,
                                good ? "" : " <<<");
            }
            Check(held, line);
        }
        // S4: one wheel of four in the air, the truck held at half the top: the free wheel is held at the cap, and the
        // other three pull no less than under the law before
        {
            TopSetup c = u;
            const float top = 2.0f;
            c.slot.map = mapOf(top);
            c.wheels = 4, c.freeWheels = 1, c.grip = 1.0f, c.holdV = 0.5f * top;
            const TopResult q = TopRun(c);
            c.oldLaw = true;
            const TopResult law0 = TopRun(c);
            snprintf(line, sizeof line, "governor ceiling, one wheel of four free, the truck held at %.2f of the top: the free wheel to %.3f of the top, the cap; the three's pull %.1f, "
                                        "%.1f under the law before",
                     c.holdV / top, q.freeMost / top, q.pull, law0.pull);
            Check(q.pull > 0.0f && q.freeMost <= c.cap * top + 1e-4f && q.freeMost > 1.4f * top && q.pull >= 0.999f * law0.pull && q.lowered == 0, line);
        }
        // the sim's own limit, under both laws: the game's traction term cuts the drive for some steps when a wheel's
        // whole rad/s rises, so with a high grip and a heavy load a truck that starts under a whole number below the
        // band stays at it with the needle swinging. The cases above keep clear of it. Whether the game does this is not
        // known from the sim
        {
            TopSetup c = u;
            const float top = 3.4f;
            c.slot.map = mapOf(top);
            c.grip = 10.0f, c.s0 = c.v0 = 0.6f * top;
            c.drag = 0.5f * 2.0f * c.couple * fminf(c.grip, 0.45f * top);
            const TopResult q = TopRun(c);
            c.oldLaw = true;
            const TopResult law0 = TopRun(c);
            snprintf(line, sizeof line, "governor ceiling, the sim's limit under both laws: a heavy load and a high grip from under a whole 3 rad/s, top 3.4: the truck stays at %.2f to %.2f rad/s, "
                                        "the needle over %.0f rpm; the law before %.2f to %.2f, %.0f",
                     q.vMin, q.vMax, q.needleMax - q.needleMin, law0.vMin, law0.vMax, law0.needleMax - law0.needleMin);
            Check(q.vMax < 3.05f && law0.vMax < 3.05f && q.needleMax - q.needleMin > 500.0f && law0.needleMax - law0.needleMin > 500.0f, line);
        }
        // the differential by axle (BRIEF_axle_diff.md, M3 and M4; torque_cut.h, DiffAxlesStep). Four wheels, the front axle
        // wheels 0 and 1, the rear 2 and 3, every gain 1
        {
            const int axle4[4] = { 0, 0, 2, 2 };
            const float gain4[4] = { 1.0f, 1.0f, 1.0f, 1.0f }, radius4[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
            const bool ok4[4] = { true, true, true, true };
            // a free front wheel: the drive gives it nearly all
            const float pre4[4] = { 2.0f, 2.0f, 2.0f, 2.0f }, post4[4] = { 9.0f, 2.1f, 2.05f, 2.02f }, load4[4] = { 0.0f, 1.0f, 1.0f, 1.0f };
            const int open4[4] = { kDiffOpen, kDiffOpen, kDiffOpen, kDiffOpen };
            const float mass = 4000.0f, kFull = LockK(100, 1.0f / 60.0f);
            auto lowered = [&](const DiffAxlesOut &x, const float *pre, const float *post, int n) {
                for (int i = 0; i < n; i++)
                    if (!(x.r.o.final[i] >= fminf(pre[i], post[i]) - 1e-4f) || !isfinite(x.r.o.final[i])) return true;
                return false;
            };
            // every type Open, and one axle: DiffEvenAxles then DiffOpenAxles, to the bit, with a ceiling and with none
            bool alike = true;
            for (float ceiling : { 3.0f, 0.0f })
            {
                const DiffEvenOut e = DiffEvenAxles(pre4, post4, gain4, ok4, axle4, 4);
                const OpenAxlesOut was = DiffOpenAxles(pre4, e.final, gain4, ok4, axle4, 4, ceiling, true);
                const DiffAxlesOut is = DiffAxlesStep(pre4, post4, gain4, load4, radius4, ok4, axle4, open4, 4, kFull, mass, ceiling, true);
                alike = alike && !memcmp(is.r.o.final, was.o.final, 4 * sizeof(float)) && !memcmp(is.work, e.final, 4 * sizeof(float)) && is.r.o.kept == was.o.kept &&
                        is.r.o.spread == was.o.spread && is.r.axles == was.axles && is.evenJ == e.j && is.evenUsed == e.used && is.tied == 0;
                const int one[2] = { 0, 0 };
                const DiffEvenOut e1 = DiffEvenAxles(pre4, post4, gain4, ok4, one, 2);
                const OpenAxlesOut was1 = DiffOpenAxles(pre4, e1.final, gain4, ok4, one, 2, ceiling, true);
                const DiffAxlesOut is1 = DiffAxlesStep(pre4, post4, gain4, load4, radius4, ok4, one, open4, 2, kFull, mass, ceiling, true);
                alike = alike && !memcmp(is1.r.o.final, was1.o.final, 2 * sizeof(float)) && is1.r.o.kept == was1.o.kept && is1.r.axles == 1;
            }
            // Open front and Locked rear, the free front wheel, a capped gear: the front axle's mean at the ceiling, the rear gets
            // the hand-on, its two wheels are tied, no wheel lowered
            const int frontOpen[4] = { kDiffOpen, kDiffOpen, kDiffLocked, kDiffLocked };
            const DiffAxlesOut capped = DiffAxlesStep(pre4, post4, gain4, load4, radius4, ok4, axle4, frontOpen, 4, kFull, mass, 3.0f, true);
            const float *f = capped.r.o.final;
            const bool mixed = Near(0.5f * (f[0] + f[1]), 3.0f, 1e-4f) && capped.r.got[1] > 0.5f && capped.r.got[0] == 0.0f && Near(f[2], f[3], 1e-3f) && 0.5f * (f[2] + f[3]) <= 3.0f + 1e-4f &&
                               f[2] > 2.5f && !lowered(capped, pre4, post4, 4) && capped.tied == 1 && capped.type[0] == kDiffOpen && capped.type[1] == kDiffLocked && capped.lockBreach == 0 &&
                               capped.lockUnder == 0 && capped.r.o.kept > 0.0f && capped.r.o.kept <= 1.0f;
            // the same with no cap: the front evened, the rear tied, nothing passes between the axles
            const DiffAxlesOut free = DiffAxlesStep(pre4, post4, gain4, load4, radius4, ok4, axle4, frontOpen, 4, kFull, mass, 0.0f, true);
            const bool noCap = Near(free.r.o.final[0], 5.55f, 1e-4f) && Near(free.r.o.final[1], 5.55f, 1e-4f) && Near(free.r.o.final[2], free.r.o.final[3], 1e-3f) && free.r.o.final[2] < 2.1f &&
                               free.r.got[0] == 0.0f && free.r.got[1] == 0.0f && free.r.o.spread == 0.0f && !memcmp(free.r.o.final, free.work, 4 * sizeof(float));
            // Locked front and Limited slip rear, a free rear wheel: the front tied, the rear held wheel by wheel at the ceiling
            const int frontLocked[4] = { kDiffLocked, kDiffLocked, kDiffLimited, kDiffLimited };
            const float postRear[4] = { 2.3f, 2.1f, 9.0f, 2.05f }, loadRear[4] = { 1.0f, 1.0f, 0.0f, 1.0f };
            const DiffAxlesOut limRear = DiffAxlesStep(pre4, postRear, gain4, loadRear, radius4, ok4, axle4, frontLocked, 4, kFull, mass, 3.0f, true);
            const bool lockedFront = Near(limRear.r.o.final[0], limRear.r.o.final[1], 1e-3f) && limRear.r.o.final[2] <= 3.0f + 1e-4f && limRear.work[2] == 9.0f && limRear.work[3] == 2.05f &&
                                     !lowered(limRear, pre4, postRear, 4) && limRear.type[0] == kDiffLocked && limRear.type[1] == kDiffLimited;
            // the mass share: two wheels of eight get a quarter of the mass. The Locked axle's part is LockStepHeld over its two
            // wheels with that mass, to the bit
            bool share = true;
            {
                int axle8[8], type8[8];
                float pre8[8], post8[8], gain8[8], load8[8], radius8[8];
                bool ok8[8];
                for (int i = 0; i < 8; i++)
                {
                    axle8[i] = i & ~1, type8[i] = i < 2 ? kDiffLocked : i < 4 ? kDiffOpen : kDiffLimited, pre8[i] = 2.0f, post8[i] = 2.0f + 0.3f * (float)(i % 3), gain8[i] = 1.0f,
                    load8[i] = i == 1 ? 0.2f : 1.0f, radius8[i] = 0.5f, ok8[i] = true;
                }
                post8[1] = 4.0f;
                pre8[1] = 1.5f; // the two wheels apart before the drive, so the mean C goes by the mass (part 197: R itself is one speed)
                const DiffAxlesOut x8 = DiffAxlesStep(pre8, post8, gain8, load8, radius8, ok8, axle8, type8, 8, 0.5f, mass, 0.0f, true);
                const LockStepOut own = LockStepHeld(pre8, post8, gain8, load8, radius8, ok8, 2, 0.5f, mass * 2.0f / 8.0f), whole = LockStepHeld(pre8, post8, gain8, load8, radius8, ok8, 2, 0.5f, mass);
                share = x8.work[0] == own.final[0] && x8.work[1] == own.final[1] && (own.final[0] != whole.final[0] || own.final[1] != whole.final[1]) && x8.r.axles == 4 && x8.tied == 1 &&
                        x8.type[0] == kDiffLocked && x8.type[1] == kDiffOpen && x8.type[2] == kDiffLimited && x8.type[3] == kDiffLimited && !lowered(x8, pre8, post8, 8);
                // four axles of three types against a ceiling: numbers, a share kept, no wheel lowered
                const DiffAxlesOut c8 = DiffAxlesStep(pre8, post8, gain8, load8, radius8, ok8, axle8, type8, 8, 0.5f, mass, 2.2f, true);
                share = share && !lowered(c8, pre8, post8, 8) && c8.r.o.kept >= 0.0f && c8.r.o.kept <= 1.0f && c8.r.axles == 4;
                for (int ax8 = 0; ax8 < 4; ax8++) share = share && isfinite(c8.r.mean[ax8]) && c8.r.mean[ax8] <= 2.2f + 1e-3f;
            }
            // M4: at Diff lock bite 0 a Locked axle among others is not tied at all; at 100 its two wheels are equal after the step
            const DiffAxlesOut noBite = DiffAxlesStep(pre4, post4, gain4, load4, radius4, ok4, axle4, frontOpen, 4, LockK(0, 1.0f / 60.0f), mass, 0.0f, true);
            const bool bite = noBite.tied == 0 && noBite.work[2] == post4[2] && noBite.work[3] == post4[3] && free.work[2] == free.work[3] && free.tied == 1 && LockK(0, 1.0f / 60.0f) == 0.0f;
            if (!(alike && mixed && noCap && lockedFront && share && bite))
                printf("     alike %d mixed %d noCap %d lockedFront %d share %d bite %d; capped %.3f %.3f %.3f %.3f got %.3f; free %.3f %.3f %.3f %.3f\n", alike, mixed, noCap, lockedFront, share, bite,
                       f[0], f[1], f[2], f[3], capped.r.got[1], free.r.o.final[0], free.r.o.final[1], free.r.o.final[2], free.r.o.final[3]);
            Check(alike && mixed && noCap && lockedFront && share && bite,
                  "differential by axle: every axle Open and one axle are the evening and the limit by axle to the bit; Open front and Locked rear with a free front wheel: the front mean at the "
                  "ceiling, the rear gets the hand-on and is tied, no wheel lowered; with no cap nothing passes; Locked front with Limited slip rear; two wheels of eight weigh a quarter of the mass; "
                  "at bite 0 a Locked axle is not tied, at 100 its wheels are equal");
        }
        // the hook's decision over the slot's wheels (M1, M2, M5): alike is the step before the axles, the lock byte in
        // force is the whole truck's lock, only driven axles that differ go by axle
        {
            TorqueCutSlot sl = {};
            sl.diff = kDiffLimited;
            bool slotOk = SlotForce(sl, true, false, false, true) == kDiffForceLimited && SlotForce(sl, true, true, false, true) == kDiffForceLock && !SlotAxleTypes(sl, true).differ;
            sl.axleN = 4;
            for (int i = 0; i < 4; i++) sl.axleWheel[i] = 0x1000u + (uint64_t)i * 0x100u, sl.axleDrive[i] = 1;
            for (int type = kDiffOpen; type <= kDiffLocked; type++)
            {
                for (int i = 0; i < 4; i++) sl.axleType[i] = (uint8_t)type;
                for (int bytes = 0; bytes < 4; bytes++)
                    slotOk = slotOk && SlotForce(sl, true, (bytes & 1) != 0, (bytes & 2) != 0, false) == DiffForceOf(type, (bytes & 1) != 0, (bytes & 2) != 0) && SlotAxleTypes(sl, false).type == type;
            }
            // the front axle with all wheel drive Locked, the rear Open
            sl.axleType[0] = sl.axleType[1] = kDiffLocked, sl.axleType[2] = sl.axleType[3] = kDiffOpen;
            sl.axleDrive[0] = sl.axleDrive[1] = 2;
            const bool differ = SlotForce(sl, true, false, false, true) == kDiffForceAxles && SlotAxleTypes(sl, true).differ && SlotAxleTypes(sl, true).type == kDiffLimited;
            const bool awdOff = SlotForce(sl, true, false, false, false) == kDiffForceOpen && !SlotAxleTypes(sl, false).differ; // the differing axle is not driven: the step before the axles
            const bool byteOn = SlotForce(sl, true, true, false, true) == kDiffForceLock && SlotForce(sl, true, true, true, true) == kDiffForceAxles;
            const bool notRead = SlotForce(sl, false, false, false, true) == kDiffForceLimited;
            sl.axleDrive[2] = sl.axleDrive[3] = 0; // no wheel driven: the slot's own type
            const bool noneDriven = SlotForce(sl, true, false, false, false) == kDiffForceLimited;
            bool found = false, lost = true;
            const bool byPointer = SlotWheelType(sl, 0x1100u, &found) == kDiffLocked && found && SlotWheelType(sl, 0x1300u, &found) == kDiffOpen && found &&
                                   SlotWheelType(sl, 0x9900u, &lost) == kDiffLimited && !lost;
            sl.axleType[3] = 9;
            const bool badType = SlotWheelType(sl, 0x1300u, &found) == kDiffLimited && found;
            if (!(slotOk && differ && awdOff && byteOn && notRead && noneDriven && byPointer && badType))
                printf("     slot %d differ %d awdOff %d byteOn %d notRead %d noneDriven %d byPointer %d badType %d\n", slotOk, differ, awdOff, byteOn, notRead, noneDriven, byPointer, badType);
            Check(slotOk && differ && awdOff && byteOn && notRead && noneDriven && byPointer && badType,
                  "differential by axle, the hook: no table and axles alike give the force as before for each type and each byte; a Locked front with an Open rear goes by axle; with all wheel "
                  "drive off the front is not driven and the step is the one before; the lock byte set is the whole truck locked, with the handbrake by axle again; a wheel missing from the "
                  "slot is Limited slip and counted");
        }
        // the slip room of the numbered gears (BRIEF_held_gear_slip.md with the Reviewer's addendum): the one rule, the
        // choice in force at a switch, and the closed loop in gear 1 against L+ on the same figures. The first gear's top
        // is 8.06, as the low range's test below has L+.
        {
            const float capShare = 1.5f, firstTop = 8.06f, capLow = capShare * firstTop;
            // M1 and M2: the rule over every position and choice
            bool rule = true;
            for (int choice = -1; choice <= kSlipCount; choice++)
            {
                // L-, L, L+ and R the same at every choice, to the bit; A and N never
                rule = rule && GearboxPosCap(kPosLowPlus, choice, capShare, firstTop, firstTop) == capShare * firstTop && GearboxPosCap(kPosLowMinus, choice, capShare, 2.23f, firstTop) == capShare * 2.23f &&
                       GearboxPosCap(kPosLow, choice, capShare, 4.18f, firstTop) == capShare * 4.18f && GearboxPosCap(kPosR, choice, capShare, 3.1f, firstTop) == capShare * 3.1f &&
                       GearboxPosCap(kPosA, choice, capShare, firstTop, firstTop) == 0.0f && GearboxPosCap(kPosN, choice, capShare, firstTop, firstTop) == 0.0f;
                // what is no choice is choice 0: no cap in a numbered gear or in H
                if (choice != kSlipLowGears && choice != kSlipAllGears)
                    rule = rule && GearboxPosCap(kPosGear, choice, capShare, firstTop, firstTop) == 0.0f && GearboxPosCap(kPosH, choice, capShare, 40.0f, firstTop) == 0.0f;
            }
            // gear 1 at choices 1 and 2: the cap of L+, to the bit
            const float capPlus = GearboxPosCap(kPosLowPlus, kSlipEngineHeld, capShare, firstTop, firstTop);
            rule = rule && GearboxPosCap(kPosGear, kSlipLowGears, capShare, firstTop, firstTop) == capPlus && GearboxPosCap(kPosGear, kSlipAllGears, capShare, firstTop, firstTop) == capPlus;
            // choice 1: the cap of L+ where it is at least 1.10 of the gear's own top, else none
            const float edge = capLow / kSlipLeast;
            rule = rule && GearboxPosCap(kPosGear, kSlipLowGears, capShare, 0.999f * edge, firstTop) == capLow && GearboxPosCap(kPosGear, kSlipLowGears, capShare, 1.001f * edge, firstTop) == 0.0f &&
                   GearboxPosCap(kPosGear, kSlipLowGears, capShare, 10.5f, firstTop) == capLow && GearboxPosCap(kPosGear, kSlipLowGears, capShare, 20.0f, firstTop) == 0.0f;
            // at WheelCap 110 choice 1 caps gear 1 alone where the second gear's top is over the first's
            rule = rule && GearboxPosCap(kPosGear, kSlipLowGears, 1.1f, firstTop, firstTop) == 1.1f * firstTop && GearboxPosCap(kPosGear, kSlipLowGears, 1.1f, 1.01f * firstTop, firstTop) == 0.0f;
            // choice 2: the cap by the gear's own top, in the highest gear and in H; H at choice 1 is far past the rule
            rule = rule && GearboxPosCap(kPosGear, kSlipAllGears, capShare, 40.0f, firstTop) == capShare * 40.0f && GearboxPosCap(kPosH, kSlipAllGears, capShare, 40.0f, firstTop) == capShare * 40.0f &&
                   GearboxPosCap(kPosH, kSlipLowGears, capShare, 40.0f, firstTop) == 0.0f && GearboxPosNumbered(kPosGear) && GearboxPosNumbered(kPosH) && !GearboxPosNumbered(kPosA) &&
                   !GearboxPosNumbered(kPosN) && !GearboxPosNumbered(kPosLowPlus) && !GearboxPosNumbered(kPosR);
            // figures that are no number give no cap
            rule = rule && GearboxPosCap(kPosGear, kSlipAllGears, capShare, 0.0f, firstTop) == 0.0f && GearboxPosCap(kPosGear, kSlipAllGears, capShare, NAN, firstTop) == 0.0f &&
                   GearboxPosCap(kPosGear, kSlipLowGears, capShare, firstTop, NAN) == 0.0f && GearboxPosCap(kPosGear, kSlipAllGears, NAN, firstTop, firstTop) == 0.0f;
            // no wide part in a numbered gear at any LowRangeSlip
            for (int share : { 0, 50, 100 })
                rule = rule && LowRangeCap(kPosGear, capLow, 2.0f * capLow, share) == capLow && LowRangeCap(kPosH, capLow, 2.0f * capLow, share) == capLow;
            rule = rule && !strcmp(HeldSlipName(0), "Engine held") && !strcmp(HeldSlipName(1), "Low gears") && !strcmp(HeldSlipName(2), "All gears") && !strcmp(HeldSlipName(9), "Engine held");
            Check(rule, "held gear slip: the one rule: L-, L, L+ and R the same at every choice, A and N never; gear 1 at Low gears and All gears has the cap of L+ to the bit; Low gears caps "
                        "where the cap of L+ is at least 1.10 of the gear top and nowhere past it; All gears caps by the gear top, H too; no wide part in a numbered gear");

            // M3: the choice in force at a switch at speed
            bool sw = true;
            {
                const float from = CutGovernBand(firstTop, kTachOverrun).from;
                TorqueCutMap cm = mapOf(firstTop);
                const float fCap = TorqueCutF(cm, 1.8f * firstTop, 1.0f, 0.3f * firstTop, true), fGovern = TorqueCutF(cm, 1.8f * firstTop, 1.0f, 0.3f * firstTop, false);
                // a cap comes (0 to 2), floored with the wheels at the governor: in force at once
                HeldSlip st = { kSlipEngineHeld, kPosGear, 1 };
                sw = sw && HeldSlipStep(st, kSlipAllGears, kPosGear, 1, capShare, firstTop, firstTop, 1.0f, 1.1f * firstTop, 0.3f * firstTop, from) == kSlipAllGears;
                // the cap's step lowers no wheel in that step, and the pull rises by the cut's own pacing
                const float ceiling = CapCeiling(capLow, firstTop, cm.governTo, 0.3f * firstTop);
                const float pre[4] = { 1.12f * firstTop, 1.08f * firstTop, 0.4f * firstTop, 1.9f * firstTop }, post[4] = { 1.13f * firstTop, 1.2f * firstTop, 0.45f * firstTop, 1.95f * firstTop },
                            gain[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                const bool ok[4] = { true, true, true, true };
                const CapStepOut o = CapStep(pre, post, gain, ok, 4, ceiling, false);
                for (int i = 0; i < 4; i++) sw = sw && o.final[i] >= fminf(pre[i], post[i]) - 1e-5f;
                const float usedWas = TorqueCutF(cm, 1.1f * firstTop, 1.0f, 0.3f * firstTop, false), bracketNow = TorqueCutF(cm, 1.1f * firstTop, 1.0f, 0.3f * firstTop, true);
                const float paced = CutRise(usedWas, bracketNow, 0.008f);
                sw = sw && bracketNow > usedWas && paced > usedWas && paced < bracketNow;
                // a cap goes (2 to 0) with the wheels at 1.8 of the top and the foot down: the old choice stays, so the pull
                // does not fall in that step (it would, from the whole curve to nothing)
                sw = sw && fCap > 0.5f && fGovern == 0.0f;
                sw = sw && HeldSlipStep(st, kSlipEngineHeld, kPosGear, 1, capShare, firstTop, firstTop, 1.0f, 1.8f * firstTop, 0.3f * firstTop, from) == kSlipAllGears && st.inForce == kSlipAllGears;
                char words[80];
                HeldSlipProbe(words, sizeof words, kSlipEngineHeld, st.inForce);
                sw = sw && !strcmp(words, "set 0, in force 2 until the wheels are under the top");
                // the wheels under the top with the truck still over the governor's start, or the truck speed not read: it stays
                sw = sw && HeldSlipStep(st, kSlipEngineHeld, kPosGear, 1, capShare, firstTop, firstTop, 1.0f, 0.9f * firstTop, 1.05f * from * firstTop, from) == kSlipAllGears &&
                     HeldSlipStep(st, kSlipEngineHeld, kPosGear, 1, capShare, firstTop, firstTop, 1.0f, 0.9f * firstTop, NAN, from) == kSlipAllGears;
                // the pedal lifted: the next pass has no cap
                sw = sw && HeldSlipStep(st, kSlipEngineHeld, kPosGear, 1, capShare, firstTop, firstTop, 0.0f, 1.8f * firstTop, 0.3f * firstTop, from) == kSlipEngineHeld &&
                     GearboxPosCap(kPosGear, st.inForce, capShare, firstTop, firstTop) == 0.0f;
                // or the wheels at the top and the truck under the governor's start
                st = HeldSlip{ kSlipAllGears, kPosGear, 1 };
                sw = sw && HeldSlipStep(st, kSlipEngineHeld, kPosGear, 1, capShare, firstTop, firstTop, 1.0f, firstTop, 0.9f * from * firstTop, from) == kSlipEngineHeld;
                // 2 to 1 in a gear that keeps a cap: at once, the cap smaller, and its step lowers no wheel
                const float second = 9.0f;
                st = HeldSlip{ kSlipAllGears, kPosGear, 2 };
                sw = sw && HeldSlipStep(st, kSlipLowGears, kPosGear, 2, capShare, second, firstTop, 1.0f, 1.45f * second, 0.3f * second, from) == kSlipLowGears &&
                     GearboxPosCap(kPosGear, kSlipLowGears, capShare, second, firstTop) == capLow && capLow < capShare * second;
                {
                    const float lower = CapCeiling(capLow, second, cm.governTo, 0.3f * second);
                    const float pre2[4] = { 1.45f * second, 1.3f * second, 0.4f * second, 1.48f * second }, post2[4] = { 1.47f * second, 1.4f * second, 0.45f * second, 1.44f * second };
                    const CapStepOut o2 = CapStep(pre2, post2, gain, ok, 4, lower, false);
                    for (int i = 0; i < 4; i++) sw = sw && o2.final[i] >= fminf(pre2[i], post2[i]) - 1e-5f;
                }
                // 1 to 2 there: the cap grows, at once
                sw = sw && HeldSlipStep(st, kSlipAllGears, kPosGear, 2, capShare, second, firstTop, 1.0f, 1.3f * second, 0.3f * second, from) == kSlipAllGears;
                // 2 to 1 in a gear past the rule: the cap goes, so it waits as to 0 does
                st = HeldSlip{ kSlipAllGears, kPosGear, 5 };
                sw = sw && HeldSlipStep(st, kSlipLowGears, kPosGear, 5, capShare, 20.0f, firstTop, 1.0f, 1.4f * 20.0f, 6.0f, from) == kSlipAllGears;
                // a shift ends the wait, and so does another position
                sw = sw && HeldSlipStep(st, kSlipLowGears, kPosGear, 6, capShare, 26.0f, firstTop, 1.0f, 1.4f * 20.0f, 6.0f, from) == kSlipLowGears;
                st = HeldSlip{ kSlipAllGears, kPosGear, 5 };
                sw = sw && HeldSlipStep(st, kSlipEngineHeld, kPosGear, 5, capShare, 20.0f, firstTop, 1.0f, 30.0f, 6.0f, from) == kSlipAllGears &&
                     HeldSlipStep(st, kSlipEngineHeld, kPosH, 0, capShare, 40.0f, firstTop, 1.0f, 30.0f, 6.0f, from) == kSlipEngineHeld;
                // the same choice set and in force: nothing to do
                sw = sw && HeldSlipStep(st, kSlipEngineHeld, kPosH, 0, capShare, 40.0f, firstTop, 1.0f, 30.0f, 6.0f, from) == kSlipEngineHeld;
                HeldSlipProbe(words, sizeof words, 1, 1);
                sw = sw && !strcmp(words, "1 (Low gears)");
            }
            Check(sw, "held gear slip: a cap that comes or grows is in force at once, lowers no wheel in its step and the pull rises by the pacing; a cap that goes waits with the foot down "
                      "and the wheels past the top, so the pull does not fall in one step; the pedal lifted, the wheels and the truck slow, a shift or another position ends the wait");

            // the closed loop: four wheels, 120 steps a second, as the low range's test below
            auto held = [&](float top, float capAbs) {
                TopSetup c = u;
                c.slot.map = mapOf(top);
                c.cap = capAbs > 0.0f ? capAbs / top : 0.0f, c.wheels = 4, c.rate = 120, c.gripNoise = 0.0f, c.dragNoise = 0.0f, c.grip = 6.0f;
                return c;
            };
            const float capOf[kSlipCount] = { GearboxPosCap(kPosGear, 0, capShare, firstTop, firstTop), GearboxPosCap(kPosGear, 1, capShare, firstTop, firstTop),
                                              GearboxPosCap(kPosGear, 2, capShare, firstTop, firstTop) };
            // standing against soft ground, floored: gear 1 at choices 1 and 2 is L+ to the last figure; at choice 0 it pulls less
            float push[kSlipCount] = {}, pushPlus = 0.0f;
            bool same = true;
            {
                TopSetup p = held(firstTop, capPlus);
                p.holdV = 0.0f, p.slipShare = 0.2f;
                const TopResult plus = TopRun(p);
                pushPlus = plus.push;
                for (int choice = 0; choice < kSlipCount; choice++)
                {
                    TopSetup c = held(firstTop, capOf[choice]);
                    c.holdV = 0.0f, c.slipShare = 0.2f;
                    const TopResult q = TopRun(c);
                    push[choice] = q.push;
                    if (choice) same = same && q.push == plus.push && q.wMax == plus.wMax && q.freeMost == plus.freeMost && q.needleMax == plus.needleMax && q.lowered == 0;
                }
            }
            // the top of the gear is held as L+ holds its own: the free run to the top, the same truck speed and needle band
            bool tops = true;
            float vPlus = 0.0f, vSecond = 0.0f, vHigh = 0.0f, vGovern = 0.0f;
            {
                const TopResult plus = TopRun(held(firstTop, capPlus));
                vPlus = plus.vMax;
                for (int choice = 1; choice < kSlipCount; choice++)
                {
                    const TopResult q = TopRun(held(firstTop, capOf[choice]));
                    tops = tops && q.vMax == plus.vMax && q.vMin == plus.vMin && q.needleMin == plus.needleMin && q.needleMax == plus.needleMax && q.lowered == 0;
                }
                vGovern = TopRun(held(firstTop, 0.0f)).vMax;
                // choice 1 in the second gear (top 10.5, the cap of L+ under its own) and choice 2 in a high gear (top 30): the
                // top held under the governor's end, no wheel lowered
                const TopResult second = TopRun(held(10.5f, GearboxPosCap(kPosGear, kSlipLowGears, capShare, 10.5f, firstTop)));
                const TopResult high = TopRun(held(30.0f, GearboxPosCap(kPosGear, kSlipAllGears, capShare, 30.0f, firstTop)));
                vSecond = second.vMax, vHigh = high.vMax;
                tops = tops && second.vMax <= 1.12f * 10.5f && high.vMax <= 1.12f * 30.0f && second.lowered == 0 && high.lowered == 0 && plus.vMax <= 1.12f * firstTop;
            }
            // a wheel in the air, the truck held standing: it stops at the ceiling in each
            bool air = true;
            float freeAt[3] = {};
            {
                const float topsOf[3] = { firstTop, 10.5f, 30.0f };
                const float caps[3] = { capOf[kSlipLowGears], GearboxPosCap(kPosGear, kSlipLowGears, capShare, 10.5f, firstTop), GearboxPosCap(kPosGear, kSlipAllGears, capShare, 30.0f, firstTop) };
                for (int i = 0; i < 3; i++)
                {
                    TopSetup c = held(topsOf[i], caps[i]);
                    c.holdV = 0.0f, c.freeWheels = 1;
                    const TopResult q = TopRun(c);
                    freeAt[i] = q.freeMost;
                    air = air && caps[i] > 0.0f && q.freeMost <= caps[i] * 1.001f && q.freeMost >= 0.9f * caps[i];
                }
            }
            // the lock's bounds in a capped numbered gear (the test of 130's rules): nothing under 0, no breach
            bool lock = true;
            {
                // the setup of that test: 60 steps a second, a ground that grips hard, one wheel free, the truck held standing
                TopSetup c = u;
                c.slot.map = mapOf(firstTop);
                c.cap = capOf[kSlipAllGears] / firstTop, c.wheels = 4, c.freeWheels = 1, c.holdV = 0.0f, c.grip = 1000.0f;
                c.lockK = 1.0f, c.lockMass = 4000.0f, c.lockRadius = 0.5f;
                const TopResult q = TopRun(c);
                lock = q.lockUnder == 0 && q.lockBreach == 0 && q.lockOver <= 1e-3f && q.lockAdded <= 1e-2f;
            }
            snprintf(line, sizeof line, "held gear slip in the closed loop: standing floored on soft ground gear 1 gives the truck %.4f a step at Engine held, %.4f at Low gears and %.4f at All gears, "
                                        "L+ %.4f (Engine held is %.2f of L+); the top: L+ %.2f, gear 1 governed %.2f, the second gear at Low gears %.2f of 10.5, a high gear at All gears %.2f of 30; "
                                        "a wheel in the air stops at %.2f, %.2f and %.2f rad/s; the lock keeps its bounds",
                     push[0], push[1], push[2], pushPlus, pushPlus > 0.0f ? push[0] / pushPlus : 0.0f, vPlus, vGovern, vSecond, vHigh, freeAt[0], freeAt[1], freeAt[2]);
            if (!(same && tops && air && lock && push[0] < pushPlus)) printf("     same %d tops %d air %d lock %d\n", same, tops, air, lock);
            Check(same && tops && air && lock && push[0] < pushPlus, line);
        }
        // the low range's pull (BRIEF_low_range_pull.md, M3), on the ground by the slip (slipShare): 0.2 of the slip a step
        // at 120 on soft ground, 0.5 on hard, at most 3 rad/s a step. Three low gears with tops of 2.23, 4.18 and 8.06,
        // four wheels, 120 steps a second. The wider cap is LowRangeCap's, as the mod's slot gets it.
        {
            const float tops[3] = { 2.23f, 4.18f, 8.06f };
            const int poses[3] = { kPosLowMinus, kPosLow, kPosLowPlus };
            float wideFrom = kLowWideFrom;
            auto low = [&](int g, float cap, int slipPercent) {
                TopSetup c = u;
                c.slot.map = mapOf(tops[g]);
                c.cap = cap, c.wheels = 4, c.rate = 120, c.gripNoise = 0.0f, c.dragNoise = 0.0f, c.grip = 6.0f;
                c.capWide = LowRangeCap(poses[g], cap * tops[g], cap * tops[2], slipPercent), c.wideFrom = wideFrom;
                return c;
            };
            // (a) and (b): the truck held standing, floored for 20 s. What a wheel gives the ground a step, after 3 s
            {
                int len = snprintf(line, sizeof line, "low range pull, standing and floored, rad/s a step and wheel given to the ground in L-, L and L+ (cap, ground: at LowRangeSlip 0; at 100):");
                bool fault = true, fixedAll = true;
                const float caps[2] = { 1.5f, 2.0f }, grounds[2] = { 0.2f, 0.5f };
                for (int cI = 0; cI < 2; cI++)
                    for (int gI = 0; gI < 2; gI++)
                    {
                        float was[3], now[3];
                        for (int g = 0; g < 3; g++)
                            for (int m = 0; m < 2; m++)
                            {
                                TopSetup c = low(g, caps[cI], m ? 100 : 0);
                                c.holdV = 0.0f, c.slipShare = grounds[gI];
                                const TopResult q = TopRun(c);
                                (m ? now : was)[g] = q.push / c.couple / 4.0f;
                            }
                        const float most = fmaxf(now[0], fmaxf(now[1], now[2])), least = fminf(now[0], fminf(now[1], now[2]));
                        // within 2 percent. At a cap of 1.5 on the soft ground within 4: the game's overspeed brake (from 0.5 rad/s under the governor's end of the
                        // gear's own top) slows L-'s and L's wheels on the steps the game's traction term gives no drive, and L+'s only on some
                        const float tol = cI == 0 && gI == 0 ? 0.04f : 0.02f;
                        const bool same = most - least <= tol * most && now[0] >= (1.0f - tol) * now[1] && now[1] >= (1.0f - tol) * now[2] && now[2] >= 0.999f * was[2] && now[2] <= 1.001f * was[2];
                        // the fault, on the soft ground at a cap of 2.0: the share of the slip at the ceiling is 0.89, 1.67 and 3.0 (the grip). The sim's wheels
                        // give 0.85 to 1 of it, since the game's traction term leaves out the drive on the steps a wheel's whole rad/s rises. L- under L under L+
                        if (cI == 1 && gI == 0)
                        {
                            fault = was[0] < 0.98f * was[1] && was[1] < 0.98f * was[2];
                            for (int g = 0; g < 3; g++)
                            {
                                const float whole = fminf(0.2f * 2.0f * tops[g], 3.0f);
                                fault = fault && was[g] >= 0.85f * whole && was[g] <= 1.001f * whole;
                            }
                        }
                        fixedAll = fixedAll && same;
                        len += snprintf(line + len, sizeof line - len, "%s %.1f, %.1f: %.2f, %.2f, %.2f; %.2f, %.2f, %.2f%s", cI || gI ? ";" : "", caps[cI], grounds[gI], was[0], was[1], was[2], now[0],
                                        now[1], now[2], same ? "" : " <<<");
                    }
                Check(fault && fixedAll, line);
                if (!fault) printf("     the fault's case does not show the fall\n");
            }
            // (c): the top holds. L- with the wider cap, free to run: from rest on the flat with a light load, and downhill
            // from 0.9 of the top, on both grounds. The truck's largest speed is the one at LowRangeSlip 0. The counter
            // case: the wide cap whole down to the governor's band (wideFrom at kCapCeilFrom) runs past the end on the flat
            {
                int len = snprintf(line, sizeof line, "low range pull, the top of L- holds (ground, case: the truck's largest speed over the top at LowRangeSlip 0; at 100; with the wide cap whole to 0.75 of the top):");
                bool held = true, counter = false;
                for (int gI = 0; gI < 2; gI++)
                    for (int hill = 0; hill < 2; hill++)
                    {
                        float most[3], end[3];
                        int lowered = 0;
                        float limit = 0.0f;
                        for (int m = 0; m < 3; m++)
                        {
                            wideFrom = m == 2 ? kCapCeilFrom : kLowWideFrom;
                            TopSetup c = low(0, 2.0f, m ? 100 : 0);
                            c.slipShare = gI ? 0.2f : 0.0f, c.grip = gI ? 6.0f : 5.0f;
                            c.drag = hill ? 0.005f : 0.0005f, c.grade = hill ? 0.02f : 0.0f;
                            if (hill) c.s0 = c.v0 = 0.9f * tops[0];
                            limit = (c.slot.map.governTo + 0.02f) * tops[0];
                            const TopResult q = TopRun(c);
                            most[m] = q.vMost, end[m] = q.vEnd;
                            if (m < 2) lowered += q.lowered;
                        }
                        wideFrom = kLowWideFrom;
                        const bool good = lowered == 0 && fabsf(most[1] - most[0]) <= 0.002f * tops[0] && (hill || (most[1] <= limit && end[1] > tops[0]));
                        held = held && good;
                        if (!hill && most[2] > most[0] + 0.02f * tops[0]) counter = true;
                        len += snprintf(line + len, sizeof line - len, "%s %s, %s: %.3f; %.3f; %.3f%s", gI || hill ? ";" : "", gI ? "by the slip" : "the grip whole", hill ? "downhill" : "the flat",
                                        most[0] / tops[0], most[1] / tops[0], most[2] / tops[0], good ? "" : " <<<");
                    }
                Check(held && counter, line);
            }
            // (d): no sway. The first case above (floored at the top for 20 s, two wheels, 60 steps, the six grips, a load
            // of half the push the ground can give) on L- and L with the wider cap of 1.5 and 2.0 times L+'s top. At the
            // top the law is the one at LowRangeSlip 0, so the figures are the same. In L some of these cases sit on
            // the sim's own limit (the last case above) at LowRangeSlip 0 too: there the wider cap must not be the worse.
            // The counter case, the wide cap whole to 0.75 of the top: the needle swings over 150 rpm in some
            {
                int len = snprintf(line, sizeof line, "low range pull, no sway at the top of L- and L with the wider cap (top, cap, grip: truck band, needle band rpm, push jump over push; the needle band at "
                                                      "LowRangeSlip 0; with the wide cap whole to 0.75 of the top):");
                bool calmAll = true;
                int counterBad = 0, limitCases = 0;
                const float caps[2] = { 1.5f, 2.0f }, grips[6] = { 0.5f, 1.0f, 2.0f, 2.5f, 5.0f, 10.0f };
                for (int g = 0; g < 2; g++)
                    for (int cI = 0; cI < 2; cI++)
                        for (int gr = 0; gr < 6; gr++)
                        {
                            const float top = tops[g];
                            TopResult q[3];
                            bool good[3];
                            for (int m = 0; m < 3; m++)
                            {
                                TopSetup c = u;
                                c.slot.map = mapOf(top);
                                c.cap = caps[cI], c.capWide = LowRangeCap(poses[g], caps[cI] * top, caps[cI] * tops[2], m ? 100 : 0), c.wideFrom = m == 2 ? kCapCeilFrom : kLowWideFrom;
                                c.grip = grips[gr], c.s0 = c.v0 = 0.9f * top;
                                c.drag = 0.5f * 2.0f * c.couple * fminf(c.grip, 0.45f * top);
                                q[m] = TopRun(c);
                                good[m] = q[m].needleMax - q[m].needleMin <= 150.0f && 0.5f * (q[m].vMax - q[m].vMin) <= 0.02f * top && q[m].pushJump <= 0.35f * q[m].push &&
                                          q[m].vMin >= kCapCeilFrom * top && q[m].vMax <= c.slot.map.governTo * top && q[m].lowered == 0;
                            }
                            // L-: calm in every case. L: calm, or on the sim's limit as at LowRangeSlip 0 and then no worse
                            const bool fine = good[1] || (g == 1 && !good[0] && q[1].needleMax - q[1].needleMin <= q[0].needleMax - q[0].needleMin + 1.0f);
                            calmAll = calmAll && fine;
                            limitCases += !good[1];
                            counterBad += g == 0 && !good[2];
                            len += snprintf(line + len, sizeof line - len, "%s %.2f, %.1f, %.1f: %.3f to %.3f, %.0f, %.2f; %.0f; %.0f%s", g || cI || gr ? ";" : "", top, caps[cI], grips[gr], q[1].vMin / top,
                                            q[1].vMax / top, q[1].needleMax - q[1].needleMin, q[1].pushJump / q[1].push, q[0].needleMax - q[0].needleMin, q[2].needleMax - q[2].needleMin,
                                            fine ? "" : " <<<");
                        }
                len += snprintf(line + len, sizeof line - len, "; %d of the 24 on the sim's limit as at LowRangeSlip 0; the counter case swings in %d of L-'s 12", limitCases, counterBad);
                Check(calmAll && counterBad > 0, line);
            }
            // moving under a load: half of what the four wheels give the ground standing with the wider cap, on the soft
            // ground, 30 s from rest. With the wider cap the truck keeps moving, at LowRangeSlip 0 it stands
            {
                TopSetup s = low(0, 2.0f, 100);
                s.holdV = 0.0f, s.slipShare = 0.2f;
                const float stand = TopRun(s).push * 2.0f; // a step at 120 into the drag's measure, a step at 60
                TopResult q[2];
                for (int m = 0; m < 2; m++)
                {
                    TopSetup c = low(0, 2.0f, m ? 100 : 0);
                    c.slipShare = 0.2f, c.gripNoise = 0.3f, c.dragNoise = 0.3f, c.seconds = 30.0f, c.drag = 0.5f * stand;
                    q[m] = TopRun(c);
                }
                snprintf(line, sizeof line, "low range pull, L- from rest against a load of half the standing pull with the wider cap, soft ground, 30 s: the truck runs at %.2f to %.2f of the top, "
                                            "at LowRangeSlip 0 at %.2f to %.2f",
                         q[1].vMin / tops[0], q[1].vMax / tops[0], q[0].vMin / tops[0], q[0].vMax / tops[0]);
                Check(q[1].vMin > kLowWideFrom * tops[0] && q[1].vMax < kCapCeilFrom * tops[0] && q[0].vMax < 0.05f * tops[0] && q[1].lowered == 0, line);
            }
            // (e): one wheel of four in the air in L-: with the truck held standing the free wheel's fastest speed is the
            // wider cap and not over it; held at half the top it is the ceiling there (CapCeilingWide), half way down to
            // the gear's own; the other three pull no less than at LowRangeSlip 0
            {
                TopSetup c = low(0, 2.0f, 100);
                c.freeWheels = 1, c.holdV = 0.0f, c.slipShare = 0.2f;
                const TopResult q = TopRun(c);
                c.holdV = 0.5f * tops[0];
                const TopResult half = TopRun(c);
                const float halfCeil = CapCeilingWide(2.0f * tops[0], c.capWide, tops[0], c.slot.map.governTo, c.holdV);
                c.capWide = 0.0f, c.holdV = 0.0f;
                const TopResult own = TopRun(c);
                const float wide = 2.0f * tops[2];
                snprintf(line, sizeof line, "low range pull, one wheel of four free in L-, the truck held standing: the free wheel to %.2f rad/s, the wider cap %.2f (%.2f at LowRangeSlip 0, "
                                            "the gear's own %.2f); held at half the top to %.2f, the ceiling there %.2f; the three's pull standing %.0f against %.0f",
                         q.freeMost, wide, own.freeMost, 2.0f * tops[0], half.freeMost, halfCeil, q.pull, own.pull);
                Check(q.freeMost <= wide + 1e-3f && q.freeMost > 0.95f * wide && own.freeMost <= 2.0f * tops[0] + 1e-3f && half.freeMost <= halfCeil + 1e-3f && half.freeMost > 0.95f * halfCeil &&
                          halfCeil < 0.75f * wide && q.pull >= own.pull && q.lowered + half.lowered == 0,
                      line);
            }
            // R (BRIEF_reverse_pull.md): the same wider cap by R's own top, 3.1 here, between L-'s and L's. The sim's speeds are
            // in the gear's direction, as the cap's step has them, so R is a gear of that top with LowRangeCap's kPosR
            {
                const float topR = 3.1f;
                auto rev = [&](float cap, int slipPercent, float from) {
                    TopSetup c = u;
                    c.slot.map = mapOf(topR);
                    c.cap = cap, c.wheels = 4, c.rate = 120, c.gripNoise = 0.0f, c.dragNoise = 0.0f, c.grip = 6.0f;
                    c.capWide = LowRangeCap(kPosR, cap * topR, cap * tops[2], slipPercent), c.wideFrom = from;
                    return c;
                };
                // M1: the cap itself. 0, 50 and 100; R's top over L+'s: its own; no low gear (a most of 0): its own
                const float ownR = 2.0f * topR, mostR = 2.0f * tops[2];
                const bool capOk = LowRangeCap(kPosR, ownR, mostR, 0) == ownR && Near(LowRangeCap(kPosR, ownR, mostR, 50), 0.5f * (ownR + mostR), 1e-5f) && LowRangeCap(kPosR, ownR, mostR, 100) == mostR &&
                                   LowRangeCap(kPosR, 2.0f * 9.5f, mostR, 100) == 2.0f * 9.5f && LowRangeCap(kPosR, ownR, 0.0f, 100) == ownR && LowRangeCap(kPosR, ownR, NAN, 100) == ownR;
                // the ceiling: whole under a quarter of R's top and for a truck rolling forward in R (a speed under 0 in the
                // gear's direction), half way at the middle of the fade, the gear's own from three quarters on
                const float gTo = mapOf(topR).governTo;
                const bool fadeOk = CapCeilingWide(ownR, mostR, topR, gTo, 0.0f) == mostR && CapCeilingWide(ownR, mostR, topR, gTo, -2.0f) == mostR &&
                                    CapCeilingWide(ownR, mostR, topR, gTo, 0.2f * topR) == mostR && Near(CapCeilingWide(ownR, mostR, topR, gTo, 0.5f * topR), 0.5f * (ownR + mostR), 1e-3f) &&
                                    CapCeilingWide(ownR, mostR, topR, gTo, 0.75f * topR) == CapCeiling(ownR, topR, gTo, 0.75f * topR) &&
                                    CapCeilingWide(ownR, mostR, topR, gTo, 0.9f * topR) == CapCeiling(ownR, topR, gTo, 0.9f * topR);
                snprintf(line, sizeof line, "reverse pull: R's wider cap at LowRangeSlip 0, 50 and 100 is %.2f, %.2f and %.2f rad/s (its own %.2f, the highest low gear's %.2f), its own with a top over L+'s and with no low gear; "
                                            "the ceiling whole at a standstill, rolling forward and at 0.2 of the top, half way at 0.5, the gear's own from 0.75",
                         LowRangeCap(kPosR, ownR, mostR, 0), LowRangeCap(kPosR, ownR, mostR, 50), LowRangeCap(kPosR, ownR, mostR, 100), ownR, mostR);
                Check(capOk && fadeOk, line);
                // the pull standing and floored on the soft ground: at 100 as L+'s within 4 percent, at 0 under it
                {
                    float give[3];
                    for (int m = 0; m < 3; m++)
                    {
                        TopSetup c = m == 2 ? low(2, 2.0f, 100) : rev(2.0f, m ? 100 : 0, kLowWideFrom);
                        c.holdV = 0.0f, c.slipShare = 0.2f;
                        give[m] = TopRun(c).push / c.couple / 4.0f;
                    }
                    snprintf(line, sizeof line, "reverse pull, standing and floored on soft ground, rad/s a step and wheel given to the ground in R: %.2f at LowRangeSlip 0, %.2f at 100, L+ %.2f", give[0], give[1], give[2]);
                    Check(give[0] < 0.9f * give[1] && fabsf(give[1] - give[2]) <= 0.04f * give[2], line);
                }
                // M2 (c): the top of R holds, on the flat and downhill, on both grounds
                {
                    int len = snprintf(line, sizeof line, "reverse pull, the top of R holds (ground, case: the truck's largest speed over the top at LowRangeSlip 0; at 100):");
                    bool held = true;
                    for (int gI = 0; gI < 2; gI++)
                        for (int hill = 0; hill < 2; hill++)
                        {
                            float most[2], end[2];
                            int lowered = 0;
                            float limit = 0.0f;
                            for (int m = 0; m < 2; m++)
                            {
                                TopSetup c = rev(2.0f, m ? 100 : 0, kLowWideFrom);
                                c.slipShare = gI ? 0.2f : 0.0f, c.grip = gI ? 6.0f : 5.0f;
                                c.drag = hill ? 0.005f : 0.0005f, c.grade = hill ? 0.02f : 0.0f;
                                if (hill) c.s0 = c.v0 = 0.9f * topR;
                                limit = (c.slot.map.governTo + 0.02f) * topR;
                                const TopResult q = TopRun(c);
                                most[m] = q.vMost, end[m] = q.vEnd, lowered += q.lowered;
                            }
                            const bool good = lowered == 0 && fabsf(most[1] - most[0]) <= 0.002f * topR && (hill || (most[1] <= limit && end[1] > topR));
                            held = held && good;
                            len += snprintf(line + len, sizeof line - len, "%s %s, %s: %.3f; %.3f%s", gI || hill ? ";" : "", gI ? "by the slip" : "the grip whole", hill ? "downhill" : "the flat", most[0] / topR,
                                            most[1] / topR, good ? "" : " <<<");
                        }
                    Check(held, line);
                }
                // M2 (d): the needle at the top of R, no worse than at LowRangeSlip 0
                {
                    int len = snprintf(line, sizeof line, "reverse pull, no sway at the top of R with the wider cap (cap, grip: the needle band rpm at LowRangeSlip 100; at 0):");
                    bool calmAll = true;
                    const float caps[2] = { 1.5f, 2.0f }, grips[6] = { 0.5f, 1.0f, 2.0f, 2.5f, 5.0f, 10.0f };
                    for (int cI = 0; cI < 2; cI++)
                        for (int gr = 0; gr < 6; gr++)
                        {
                            TopResult q[2];
                            for (int m = 0; m < 2; m++)
                            {
                                TopSetup c = u;
                                c.slot.map = mapOf(topR);
                                c.cap = caps[cI], c.capWide = LowRangeCap(kPosR, caps[cI] * topR, caps[cI] * tops[2], m ? 100 : 0), c.wideFrom = kLowWideFrom;
                                c.grip = grips[gr], c.s0 = c.v0 = 0.9f * topR;
                                c.drag = 0.5f * 2.0f * c.couple * fminf(c.grip, 0.45f * topR);
                                q[m] = TopRun(c);
                            }
                            const bool fine = q[1].needleMax - q[1].needleMin <= q[0].needleMax - q[0].needleMin + 1.0f && q[1].vMax <= q[0].vMax + 0.002f * topR && q[1].lowered == 0;
                            calmAll = calmAll && fine;
                            len += snprintf(line + len, sizeof line - len, "%s %.1f, %.1f: %.0f; %.0f%s", cI || gr ? ";" : "", caps[cI], grips[gr], q[1].needleMax - q[1].needleMin, q[0].needleMax - q[0].needleMin,
                                            fine ? "" : " <<<");
                        }
                    Check(calmAll, line);
                }
                // one wheel of four in the air in R: it stops at the wider cap standing, at the ceiling at half the top
                {
                    TopSetup c = rev(2.0f, 100, kLowWideFrom);
                    c.freeWheels = 1, c.holdV = 0.0f, c.slipShare = 0.2f;
                    const TopResult q = TopRun(c);
                    c.holdV = 0.5f * topR;
                    const TopResult half = TopRun(c);
                    const float halfCeil = CapCeilingWide(ownR, c.capWide, topR, c.slot.map.governTo, c.holdV);
                    c.capWide = 0.0f, c.holdV = 0.0f;
                    const TopResult own = TopRun(c);
                    snprintf(line, sizeof line, "reverse pull, one wheel of four free in R, the truck held standing: the free wheel to %.2f rad/s, the wider cap %.2f (%.2f at LowRangeSlip 0, the gear's own %.2f); "
                                                "held at half the top to %.2f, the ceiling there %.2f; the three's pull standing %.0f against %.0f",
                             q.freeMost, mostR, own.freeMost, ownR, half.freeMost, halfCeil, q.pull, own.pull);
                    Check(q.freeMost <= mostR + 1e-3f && q.freeMost > 0.95f * mostR && own.freeMost <= ownR + 1e-3f && half.freeMost <= halfCeil + 1e-3f && half.freeMost > 0.95f * halfCeil &&
                              q.pull >= own.pull && q.lowered + half.lowered == 0,
                          line);
                }
            }
        }
    }

    // the diff lock's bite, pure (BRIEF_diff_lock_bite.md with the Reviewer's addendum and the lead's of 14:09): k from the
    // setting and the step's seconds; the step on the inertia-weighted mean with the momentum kept; what writes nothing;
    // who publishes; the drive's hook asked for by the bite alone
    {
        auto nearly = [](float a, float b, float e) { return fabsf(a - b) <= e; };
        const float k50 = LockK(50, 1.0f / 60.0f), kHalf = LockK(50, 1.0f / 120.0f);
        const bool ks = LockK(0, 1.0f / 60.0f) == 0.0f && LockK(100, 1.0f / 60.0f) == 1.0f && nearly(k50, 0.1535f, 1e-4f) &&
                        nearly((1.0f - kHalf) * (1.0f - kHalf), 1.0f - k50, 1e-5f) && LockK(50, 0.0f) == 0.0f && LockK(50, -0.01f) == 0.0f && LockK(50, NAN) == 0.0f &&
                        LockK(100, NAN) == 0.0f && LockK(100, 0.0f) == 0.0f; // no step, no write, at any setting
        const bool okAll[kCapWheels] = { true, true, true, true };
        const float equal[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        const float two[2] = { 10.0f, 20.0f };
        const LockStepOut s1 = LockStep(two, equal, okAll, 2, 1.0f), sh = LockStep(two, equal, okAll, 2, 0.5f);
        const bool pairs = s1.final[0] == 15.0f && s1.final[1] == 15.0f && s1.wrote && s1.spreadBefore == 10.0f && s1.spreadAfter == 0.0f && sh.final[0] == 12.5f &&
                           sh.final[1] == 17.5f && nearly(sh.spreadAfter, 5.0f, 1e-5f);
        // unequal inertia (gains 0.1 and 0.2: 10 and 5): the mean (100 + 200) / 15 = 20, the momentum the same at any k
        const float speeds[2] = { 10.0f, 40.0f }, gains[2] = { 0.1f, 0.2f };
        const LockStepOut u = LockStep(speeds, gains, okAll, 2, 0.3f), u1 = LockStep(speeds, gains, okAll, 2, 1.0f);
        const bool weighted = nearly(u1.final[0], 20.0f, 1e-4f) && nearly(u1.final[1], 20.0f, 1e-4f) && nearly(10.0f * u.final[0] + 5.0f * u.final[1], 300.0f, 1e-4f) &&
                              nearly(u.final[0], 13.0f, 1e-4f);
        // four wheels, one free at 60: one mean
        const float four[4] = { 60.0f, 10.0f, 10.0f, 10.0f };
        const LockStepOut f = LockStep(four, equal, okAll, 4, 1.0f);
        const bool oneMean = f.final[0] == 22.5f && f.final[1] == 22.5f && f.final[3] == 22.5f && f.used == 4;
        // nothing: a gain of 0 skipped and counted, one wheel left; a spread under 0.01; no k; a NaN k
        const float noGain[2] = { 0.0f, 1.0f }, close[2] = { 10.0f, 10.005f };
        const LockStepOut g = LockStep(two, noGain, okAll, 2, 1.0f), c = LockStep(close, equal, okAll, 2, 1.0f), z = LockStep(two, equal, okAll, 2, 0.0f),
                          nk = LockStep(two, equal, okAll, 2, NAN);
        const bool nothing = !g.wrote && g.skipped == 1 && g.final[1] == 20.0f && !c.wrote && c.final[1] == 10.005f && !z.wrote && !nk.wrote && nk.final[0] == 10.0f;
        // who publishes (the diff types' addendum, A3): kinds 0, 1 and 2, never another kind, never without the read or the pattern
        const bool publishes = LockPublishes(true, true, 0) && LockPublishes(true, true, 1) && LockPublishes(true, true, 2) && !LockPublishes(true, false, 0) &&
                               !LockPublishes(false, true, 0) && !LockPublishes(false, true, 2) && !LockPublishes(true, true, 3) && !LockPublishes(true, true, -1);
        // the drive's hook: the bite alone asks for it (Powertrain on, DiffLockBite above 0), at 0 or with Powertrain off it does not
        // and the drive display alone asks for it, whatever else is off (131's M2)
        const bool hook = DriveHookWanted(false, false, false, false, true, 50) && !DriveHookWanted(false, false, false, false, true, 0) &&
                          !DriveHookWanted(false, false, false, false, false, 50) && DriveHookWanted(true, false, false, false, false, 0) &&
                          DriveHookWanted(false, false, false, false, false, 0, true) && !DriveHookWanted(false, false, false, false, false, 0, false);
        if (!(ks && pairs && weighted && oneMean && nothing && publishes && hook))
            printf("     lock pure: k %d (k50 %.5f), pairs %d, weighted %d (%.4f %.4f), one mean %d, nothing %d, publishes %d, hook %d\n", ks, k50, pairs, weighted, u.final[0],
                   u.final[1], oneMean, nothing, publishes, hook);
        Check(ks && pairs && weighted && oneMean && nothing && publishes && hook,
              "diff lock bite: k is 0 at 0 and 1 at 100, 0.1535 at 50 for 1/60 s and the same over two half steps, 0 for no step; the wheels on their inertia-weighted mean "
              "with the momentum kept; a wheel with no inertia skipped, a spread under 0.01 or no k writes nothing; published at kinds 0, 1 and 2, "
              "never at another kind, never without the read or the setter's pattern; the bite alone asks for the drive's hook");
    }
    // the sketch at 60 Hz (the brief's): one driven wheel on ice and one on grip, floored for 5 s, the game's locked rule
    // (the Reviewer's reading of c423a4: whole speeds, the threshold over the slowest, the share smoothed 0.8 old) alone
    // against the bite at 50, 75 and 100: the spread of the two and the truck's speed at the end
    {
        struct Run { float spread, speed; };
        auto run = [](int bite) {
            const float dt = 1.0f / 60.0f, inertia = 20.0f, radius = 0.5f, mass = 2000.0f, torque = 800.0f, load = 10000.0f, stiff = 2000.0f;
            const float mu[2] = { 0.05f, 0.8f };
            float w[2] = { 0.0f, 0.0f }, share[2] = { 1.0f, 1.0f }, accel[2] = { 0.0f, 0.0f }, v = 0.0f, spreadSum = 0.0f;
            int counted = 0;
            for (int step = 0; step < 300; step++)
            {
                // the game's rule on the speeds as they came into the step
                const float lo = fminf(floorf(w[0]), floorf(w[1])), hi = fmaxf(floorf(w[0]), floorf(w[1])), mean = (floorf(w[0]) + floorf(w[1])) / 2.0f;
                const float threshold = 0.9f * lo + 0.1f * mean + 0.01f, width = fminf(fmaxf(0.05f * hi, 0.05f), 0.5f);
                float force = 0.0f, was[2] = { w[0], w[1] };
                for (int i = 0; i < 2; i++)
                {
                    const float target = fmaxf(1.0f - fmaxf(floorf(w[i]) + 0.5f * accel[i] - threshold, 0.0f) / width, 0.01f);
                    share[i] = 0.8f * share[i] + 0.2f * target;
                    const float slip = w[i] * radius - v, f = fminf(fmaxf(stiff * slip, -mu[i] * load), mu[i] * load);
                    w[i] += (torque / 2.0f * share[i] - f * radius) / inertia * dt;
                    force += f;
                }
                v += force / mass * dt;
                // the bite after the drive
                const float gain[2] = { 1.0f / inertia, 1.0f / inertia };
                const bool ok[2] = { true, true };
                const LockStepOut o = LockStep(w, gain, ok, 2, LockK(bite, dt));
                w[0] = o.final[0], w[1] = o.final[1];
                for (int i = 0; i < 2; i++) accel[i] = 0.8f * accel[i] + 0.2f * (w[i] - was[i]);
                if (step >= 60) spreadSum += fabsf(w[0] - w[1]), counted++;
            }
            return Run{ spreadSum / (float)counted, v };
        };
        const Run game = run(0), halfway = run(50), firm = run(75), shaft = run(100);
        char line[400];
        snprintf(line, sizeof line, "diff lock bite, the sketch: ice and grip floored 5 s at 60 Hz; the spread over the last 4 s %.2f rad/s by the game rule alone, %.2f at 50, %.2f at 75, "
                                    "%.3f at 100; the truck at %.2f, %.2f, %.2f and %.2f m/s",
                 game.spread, halfway.spread, firm.spread, shaft.spread, game.speed, halfway.speed, firm.speed, shaft.speed);
        Check(shaft.spread < 0.01f && firm.spread < halfway.spread && halfway.spread < game.spread && shaft.speed > game.speed, line);
    }

    // the differential's types (BRIEF_diff_types.md with the Reviewer's addendum of 2026-11-11): the type by the setting and
    // the kind, what is in force in a step, Open's limit on the driven mean, the held lock byte
    {
        const bool types = DiffTypeOf(kDiffAuto, true, 0) == kDiffOpen && DiffTypeOf(kDiffAuto, true, 1) == kDiffOpen && DiffTypeOf(kDiffAuto, true, 2) == kDiffLocked &&
                           DiffTypeOf(kDiffAuto, true, 3) == kDiffLimited && DiffTypeOf(kDiffAuto, false, 2) == kDiffLimited && DiffTypeOf(kDiffAuto, true, -1) == kDiffLimited &&
                           DiffTypeOf(kDiffOpen, true, 2) == kDiffOpen && DiffTypeOf(kDiffLimited, true, 0) == kDiffLimited && DiffTypeOf(kDiffLocked, false, 0) == kDiffLocked &&
                           DiffTypeOf(9, true, 2) == kDiffLocked && DiffTypeOf(-1, true, 1) == kDiffOpen;
        const bool force = DiffForceOf(kDiffOpen, false, false) == kDiffForceOpen && DiffForceOf(kDiffOpen, true, false) == kDiffForceLock &&
                           DiffForceOf(kDiffOpen, true, true) == kDiffForceOpen && DiffForceOf(kDiffLimited, false, false) == kDiffForceLimited &&
                           DiffForceOf(kDiffLimited, true, false) == kDiffForceLock && DiffForceOf(kDiffLimited, true, true) == kDiffForceLimited &&
                           DiffForceOf(kDiffLocked, false, false) == kDiffForceLock && DiffForceOf(kDiffLocked, false, true) == kDiffForceLock &&
                           DiffLockInForce(kDiffLocked, false, true) && !DiffLockInForce(kDiffOpen, true, true);
        // Open's limit (DiffOpenStep), wheels with a gain of 1 and the evening done: all alike are held at the ceiling
        // each; one free at 56 with three standing, the drive adding 5 and 1, 1, 1: the mean from 14 to the ceiling of
        // 15 by half of each addition; a mean over the ceiling before the drive gets nothing and loses nothing; the
        // engine's braking passes whole; under the ceiling nothing is touched; one wheel slowed by 2 and one pushed by
        // 10 around a mean of 15: the slowing whole, 0.2 of the push; no ceiling (0, no number): as the drive left it
        bool ceiling = true;
        {
            auto close = [](float a, float b) { return fabsf(a - b) <= 1e-4f; };
            const bool okW[4] = { true, true, true, true };
            const float one[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            const float aPre[4] = { 8.0f, 8.0f, 8.0f, 8.0f }, aEven[4] = { 9.0f, 9.0f, 9.0f, 9.0f };
            const CapStepOut oa = DiffOpenStep(aPre, aEven, one, okW, 4, 8.5f), aCap = CapStep(aPre, aEven, one, okW, 4, 8.5f, true);
            const float bPre[4] = { 0.0f, 0.0f, 0.0f, 56.0f }, bEven[4] = { 1.0f, 1.0f, 1.0f, 61.0f };
            const CapStepOut ob = DiffOpenStep(bPre, bEven, one, okW, 4, 15.0f);
            const float cPre[2] = { 20.0f, 20.0f }, cEven[2] = { 22.0f, 22.0f }, dEven[2] = { 18.0f, 18.0f };
            const CapStepOut c = DiffOpenStep(cPre, cEven, one, okW, 2, 15.0f), d = DiffOpenStep(cPre, dEven, one, okW, 2, 15.0f);
            const float ePre[2] = { 1.0f, 1.0f }, eEven[2] = { 2.0f, 2.0f };
            const CapStepOut e = DiffOpenStep(ePre, eEven, one, okW, 2, 15.0f);
            const float fPre[2] = { 10.0f, 20.0f }, fEven[2] = { 8.0f, 30.0f };
            const CapStepOut f = DiffOpenStep(fPre, fEven, one, okW, 2, 15.0f), g0 = DiffOpenStep(fPre, fEven, one, okW, 2, 0.0f), gn = DiffOpenStep(fPre, fEven, one, okW, 2, NAN);
            for (int i = 0; i < 4; i++) ceiling = ceiling && close(oa.final[i], 8.5f) && close(oa.final[i], aCap.final[i]) && oa.clamped[i];
            ceiling = ceiling && oa.clamps == 4 && close(oa.kept, 0.5f) && close(ob.final[0], 0.5f) && close(ob.final[1], 0.5f) && close(ob.final[2], 0.5f) && close(ob.final[3], 58.5f) &&
                      close(ob.kept, 0.5f) && close(ob.excess, 1.0f) && ob.spread == 0.0f && c.final[0] == 20.0f && c.final[1] == 20.0f && c.kept == 0.0f && c.clamps == 2 &&
                      d.final[0] == 18.0f && d.final[1] == 18.0f && d.clamps == 0 && d.kept == 1.0f && e.final[0] == 2.0f && e.clamps == 0 && e.kept == 1.0f && f.final[0] == 8.0f &&
                      close(f.final[1], 22.0f) && f.clamps == 1 && !f.clamped[0] && close(f.keptEach[1], 0.2f) && g0.final[1] == 30.0f && g0.clamps == 0 && gn.final[1] == 30.0f &&
                      gn.clamps == 0;
        }
        // the hold: set where the byte is 0, left where it is set, cleared once the type no longer acts, set again when
        // the game cleared it; a byte the player set first is never cleared; no read and no action do nothing
        LockHold h = {};
        const int set = LockHoldStep(h, 0x1000, true, true, 0), stays = LockHoldStep(h, 0x1000, true, true, 1), again = LockHoldStep(h, 0x1000, true, true, 0);
        const bool oursNow = h.ours;
        const int clear = LockHoldStep(h, 0x1000, false, true, 1), after = LockHoldStep(h, 0x1000, false, true, 1);
        LockHold p = {};
        const int players = LockHoldStep(p, 0x1000, true, true, 1), playersOff = LockHoldStep(p, 0x1000, false, true, 1);
        // a truck change: the new action starts with nothing held, and the old truck's byte is not touched from there;
        // back on the old truck its byte is the mod's own again and is cleared once the type no longer acts (G1), also
        // after three more trucks, and a byte the game cleared meanwhile is not written
        LockHold c = {};
        LockHoldStep(c, 0x1000, true, true, 0);
        const int other = LockHoldStep(c, 0x2000, false, true, 1);
        const bool otherNotOurs = !c.ours && c.action == 0x2000;
        const int back = LockHoldStep(c, 0x1000, false, true, 1);
        const bool backDone = !c.ours;
        const int backAfter = LockHoldStep(c, 0x1000, false, true, 1);
        LockHold c4 = {};
        LockHoldStep(c4, 0x1000, true, true, 0);
        LockHoldStep(c4, 0x2000, true, true, 0);
        LockHoldStep(c4, 0x3000, true, true, 1); // the player's
        LockHoldStep(c4, 0x4000, true, true, 0);
        const int players3 = LockHoldStep(c4, 0x3000, false, true, 1), back2 = LockHoldStep(c4, 0x2000, false, true, 1), back1 = LockHoldStep(c4, 0x1000, false, true, 0);
        const int back4 = LockHoldStep(c4, 0x4000, true, true, 1), off4 = LockHoldStep(c4, 0x4000, false, true, 1);
        const bool forgot = otherNotOurs && back == 0 && backDone && backAfter == -1 && players3 == -1 && back2 == 0 && back1 == -1 && back4 == -1 && off4 == 0;
        LockHold n = {};
        const int noRead = LockHoldStep(n, 0x1000, true, false, 0), noAction = LockHoldStep(n, 0, true, true, 0);
        LockHold g = {};
        LockHoldStep(g, 0x1000, true, true, 0);
        const int gone = LockHoldStep(g, 0x1000, false, true, 0); // ours, and the game cleared it already: nothing to write
        const bool hold = set == 1 && stays == -1 && again == 1 && oursNow && clear == 0 && after == -1 && players == -1 && !p.ours && playersOff == -1 && other == -1 && forgot &&
                          noRead == -1 && noAction == -1 && gone == -1 && !g.ours;
        if (!(types && force && ceiling && hold)) printf("     diff types: types %d, force %d, ceiling %d, hold %d (%d %d %d %d %d %d %d %d)\n", types, force, ceiling, hold, set, stays, again, clear, after, players, playersOff, other);
        Check(types && force && ceiling && hold,
              "diff types: Auto is Open at kinds 0 and 1 and Locked at kind 2, Limited slip with the kind not read or not known, a set type is itself; the lock is in force "
              "with the byte set and the handbrake off on any type and always on Locked; Open's limit is the step's ceiling on the driven wheels' mean, one factor on what "
              "the drive added, no wheel lowered and the engine's braking whole; the held lock byte is set where it is 0, "
              "cleared only where the mod set it, also on the way back to a truck left with it set");
    }

    // Open (A1, A2, with C5 of BRIEF_governor_sway.md): the evening shares the drive's impulse, the limit is the step's
    // ceiling on the driven wheels' mean with nothing handed on, the cut's answer is the same in every type (no governor by
    // the mean); the lock set over a free wheel does not lurch (F1 of the review)
    {
        auto nearly = [](float a, float b, float tol) { return fabsf(a - b) <= tol; };
        const bool okAll[kCapWheels] = { true, true, true, true };
        const float one[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        // two wheels, the free one took the whole kick: half each
        const float pre2[2] = { 0.0f, 0.0f }, post2[2] = { 10.0f, 0.0f };
        const DiffEvenOut e2 = DiffEvenStep(pre2, post2, one, okAll, 2);
        // unequal gains (inertia 2 and 0.5): J = 0 / 0.5 + 20 / 2 = 10, 5 each: 1 + 0.5 x 5 and 1 + 2 x 5, the momentum kept
        const float pre3[2] = { 1.0f, 1.0f }, post3[2] = { 1.0f, 21.0f }, gain3[2] = { 0.5f, 2.0f };
        const DiffEvenOut e3 = DiffEvenStep(pre3, post3, gain3, okAll, 2);
        const bool evens = e2.final[0] == 5.0f && e2.final[1] == 5.0f && e2.wrote && e2.used == 2 && nearly(e2.j, 10.0f, 1e-5f) && nearly(e3.final[0], 3.5f, 1e-5f) &&
                           nearly(e3.final[1], 11.0f, 1e-5f) && nearly(e3.final[0] / 0.5f + e3.final[1] / 2.0f, 1.0f / 0.5f + 1.0f / 2.0f + 10.0f, 1e-4f);
        // all four gripping, the drive even already: nothing written
        const float pre4[4] = { 8.0f, 8.0f, 8.0f, 8.0f }, post4[4] = { 8.5f, 8.5f, 8.5f, 8.5f };
        const DiffEvenOut e4 = DiffEvenStep(pre4, post4, one, okAll, 4);
        // one wheel, a gain of 0 beside one wheel, a NaN: as the drive left them
        const float nanPost[2] = { NAN, 4.0f }, noGain[2] = { 0.0f, 1.0f };
        const DiffEvenOut e1 = DiffEvenStep(pre2, post2, one, okAll, 1), eg = DiffEvenStep(pre2, post2, noGain, okAll, 2), en = DiffEvenStep(pre2, nanPost, one, okAll, 2);
        const bool leaves = !e4.wrote && e4.final[2] == 8.5f && e4.used == 4 && !e1.wrote && e1.final[0] == 10.0f && !eg.wrote && eg.final[0] == 10.0f && eg.used == 1 && !en.wrote &&
                            en.final[1] == 4.0f;
        // at the limit (two wheels, a ceiling of 13.75 on the mean: the free one at 27.5 with the other standing) the free
        // wheel is held and the standing one gets nothing of it; the cap's step with the spread would hand it on
        const float capPre[2] = { 0.0f, 26.0f }, capPost[2] = { 0.0f, 30.0f };
        const CapStepOut atCap = DiffOpenStep(capPre, capPost, one, okAll, 2, 13.75f), handed = CapStep(capPre, capPost, one, okAll, 2, 27.5f, true);
        const bool noHand = nearly(atCap.final[1], 27.5f, 1e-4f) && atCap.final[0] == 0.0f && atCap.spread == 0.0f && atCap.clamps == 1 && handed.final[0] > 0.0f;
        // F1: a gear's top of 10 and a cap of 15; in Open one wheel of four runs free at 4 x the top, the drive's kick 150 on
        // it and 3 on each of the others. The step before: evened (39.75 each), the mean from 10 to the ceiling of 15, so 5
        // each. Then the lock byte is set with DiffLockBite at 0: the cap's step with the ceiling 15 and the spread. The free
        // wheel keeps its 40, no wheel gains more than the ceiling, and all four gain no more than the drive gave. (With
        // the mean's limit a standing wheel gains more under the lock than in the Open step before, up to the ceiling:
        // that is the lock's push on the wheels that grip.)
        const float fPre[4] = { 0.0f, 0.0f, 0.0f, 40.0f }, fPost[4] = { 3.0f, 3.0f, 3.0f, 190.0f };
        const DiffEvenOut fEven = DiffEvenStep(fPre, fPost, one, okAll, 4);
        const CapStepOut inOpen = DiffOpenStep(fPre, fEven.final, one, okAll, 4, 15.0f), lockSet = CapStep(fPre, fPost, one, okAll, 4, 15.0f, true);
        bool noLurch = lockSet.final[3] == 40.0f && nearly(inOpen.final[3], 45.0f, 1e-3f) && nearly(inOpen.final[0], 5.0f, 1e-3f);
        float gained = 0.0f, gainMost = 0.0f;
        for (int i = 0; i < 4; i++)
        {
            noLurch = noLurch && lockSet.final[i] >= fPre[i] && lockSet.final[i] <= fmaxf(fPre[i], 15.0f) + 1e-4f;
            gained += lockSet.final[i] - fPre[i];
            gainMost = fmaxf(gainMost, lockSet.final[i] - fPre[i]);
        }
        noLurch = noLurch && gained <= 159.0f + 1e-3f && gainMost <= 15.0f + 1e-4f;
        if (!(evens && leaves && noHand && noLurch))
            printf("     open: evens %d, leaves %d, no hand %d, no lurch %d (the lock set: %.3f %.3f %.3f %.3f, in Open %.3f %.3f %.3f %.3f)\n", evens, leaves, noHand, noLurch, lockSet.final[0],
                   lockSet.final[1], lockSet.final[2], lockSet.final[3], inOpen.final[0], inOpen.final[1], inOpen.final[2], inOpen.final[3]);
        Check(evens && leaves && noHand && noLurch,
              "open differential: the drive's impulse is shared evenly over the driven wheels with the momentum kept, an even drive is not written; at the limit (the "
              "ceiling on the driven mean) nothing is handed to the standing wheel; the lock set over a wheel free at 4 x the top leaves that wheel its speed and "
              "takes no wheel past the ceiling");

        // S1 anew on the top's sim (TopRun, torque_cut_sim.h), by axle (BRIEF_open_by_axle.md): four wheels in two
        // axles, the truck held standing, 20 s, in L- (a cap of 1.5). One front wheel free:
        // the free wheel's partner gets next to nothing, the free wheel stays at or under 2 x the cap and the front
        // mean at the cap. As one axle (the law before, the open centre) the same run breaks those bounds. One wheel
        // free on each axle: the truck barely pulls, hand-on or not (no axle can take). The rear axle (F1 of the
        // review of 122, the tied shaft): the game's own share gives a wheel slower than the fastest by a whole rad/s
        // next to none of the drive, so what the front axle's limit took from the drive is handed to the rear axle. It
        // then pulls as with no wheel free at 60 steps a second and at least 0.6 of that at 120 (the halved drive has less
        // a step to hand on there than the ground takes), and its wheels end at or under the cap. With nothing handed on (noHandOn, as
        // before F1) the same run leaves the rear axle under 0.05 of it: the bound must fail there. The partner of
        // the free wheel gets nothing from the hand-on. In a numbered gear (no cap) nothing limits
        TopSetup u = {};
        u.slot.vehicle = 1, u.slot.mode = kCutMap, u.slot.share = 1.0f, u.slot.pedal = 1.0f;
        u.wheels = 4, u.freeWheels = 1, u.kick = 500.0f, u.mdav = 0.01f, u.inertia = 10.0f, u.flat = -1.0f, u.holdV = 0.0f, u.grip = 1000.0f;
        u.seconds = 20.0f, u.rise = 0.008f, u.seed = 7, u.gripNoise = 0.3f, u.dragNoise = 0.3f, u.couple = 1.0f / 60.0f;
        auto run = [&](float top, float cap, bool open, uint32_t freeMask, bool oneAxle, int rate, bool shareOne = false, bool noHandOn = false) {
            TopSetup c = u;
            c.shareOne = shareOne;
            c.noHandOn = noHandOn;
            const CutBand band = CutGovernBand(top, kTachOverrun);
            c.slot.map = TorqueCutMap{ 1.0f, 1.0f, 0.0f, 1.0f, 0.15f, top, 800.0f, 2500.0f, 1.2f, { 5, { 0.25f, 0.45f, 0.6f, 0.8f, 1.0f }, { 0.55f, 0.85f, 1.0f, 1.0f, 0.85f } }, band.from, band.to };
            c.cap = cap;
            c.open = open;
            c.freeWheels = 0, c.freeMask = freeMask, c.oneAxle = oneAxle, c.rate = rate;
            return TopRun(c);
        };
        const float lowTop = 2.4f, highTop = 12.3f, capAt = 1.5f * lowTop;
        static char line[4800];
        bool axleSim = true;
        int len = 0;
        for (int rate = 60; rate <= 120; rate += 60)
        {
            const TopResult oneFree = run(lowTop, 1.5f, true, 1u, false, rate), none = run(lowTop, 1.5f, true, 0u, false, rate), asOne = run(lowTop, 1.5f, true, 1u, true, rate);
            const TopResult limited = run(lowTop, 1.5f, false, 1u, false, rate), both = run(lowTop, 1.5f, true, 5u, false, rate);
            const float rearPull = oneFree.pullEach[2] + oneFree.pullEach[3], rearNone = none.pullEach[2] + none.pullEach[3], rearBefore = asOne.pullEach[2] + asOne.pullEach[3];
            const float frontMean = 0.5f * (oneFree.wEnd[0] + oneFree.wEnd[1]), frontBefore = 0.5f * (asOne.wEnd[0] + asOne.wEnd[1]);
            const TopResult oneS = run(lowTop, 1.5f, true, 1u, false, rate, true), noneS = run(lowTop, 1.5f, true, 0u, false, rate, true), beforeS = run(lowTop, 1.5f, true, 1u, true, rate, true);
            const float rearS = oneS.pullEach[2] + oneS.pullEach[3], rearNoneS = noneS.pullEach[2] + noneS.pullEach[3], rearBeforeS = beforeS.pullEach[2] + beforeS.pullEach[3];
            // as before F1 of the review of 122: nothing handed from one axle to another
            const TopResult asBefore = run(lowTop, 1.5f, true, 1u, false, rate, false, true), bothBefore = run(lowTop, 1.5f, true, 5u, false, rate, false, true);
            const float rearAsBefore = asBefore.pullEach[2] + asBefore.pullEach[3], rearLimited = limited.pullEach[2] + limited.pullEach[3];
            len += snprintf(line + len, sizeof line - len,
                            "%sopen by axle on the top's sim at %d steps a second: four wheels, the truck held standing, 20 s in L- (the cap %.2f rad/s); one front wheel free: it ends at %.2f "
                            "x the cap (%.2f at the most), its partner pulls %.1f (%.1f with no wheel free), the front mean ends at %.2f x the cap; the rear axle, handed what the front axle's limit "
                            "took, pulls %.1f (%.1f with no wheel free, %.1f with nothing handed on, %.1f as one axle; Limited slip: its rear axle %.1f, its three wheels %.1f) and its wheels "
                            "end at %.2f and %.2f x the cap; the free wheel's partner with nothing handed on pulls %.1f; "
                            "with every share held at 1 (not the game) the rear axle pulls %.1f against %.1f with no wheel free, and %.1f as one axle; "
                            "as one axle, the law before: the free wheel to %.2f x the cap, the front mean %.2f x the cap; one wheel "
                            "free on each axle: all pull %.1f (%.1f with nothing handed on) against %.1f with no wheel free; no wheel lowered (%d %d %d)",
                            len ? "; " : "", rate, capAt, oneFree.freeEnd / capAt, oneFree.freeMost / capAt, oneFree.pullEach[1], none.pullEach[1], frontMean / capAt, rearPull, rearNone, rearAsBefore,
                            rearBefore, rearLimited, limited.pull, oneFree.wEnd[2] / capAt, oneFree.wEnd[3] / capAt, asBefore.pullEach[1], rearS, rearNoneS,
                            rearBeforeS, asOne.freeMost / capAt, frontBefore / capAt, both.pull, bothBefore.pull, none.pull, oneFree.lowered, both.lowered, oneS.lowered);
            // the hand-on: the rear axle pulls on, its wheels under the cap, the partner as with nothing handed on; with nothing handed on the rear axle's bound fails
            const bool handsOn = rearPull >= (rate == 60 ? 0.95f : 0.6f) * rearNone && rearPull <= 1.05f * rearNone && oneFree.wEnd[2] <= capAt + 1e-3f && oneFree.wEnd[3] <= capAt + 1e-3f &&
                                 nearly(oneFree.pullEach[1], asBefore.pullEach[1], 0.01f * none.pullEach[1]) && nearly(both.pull, bothBefore.pull, 0.01f * none.pull) &&
                                 asBefore.lowered == 0 && rearAsBefore < 0.05f * rearNone;
            const bool good = oneFree.freeMost <= 2.0f * capAt + 1e-3f && nearly(rearS, rearNoneS, 0.05f * rearNoneS) && oneFree.pullEach[1] <= 0.05f * none.pullEach[1] && nearly(frontMean, capAt, 0.03f * capAt) &&
                              both.pull <= 0.05f * none.pull && oneFree.lowered == 0 && both.lowered == 0 && oneS.lowered == 0 && oneFree.pull > 0.0f && oneS.freeMost <= 2.0f * capAt + 1e-3f;
            // the law before must break them: the free wheel past 2 x the cap, the front mean not at the cap, and with the shares at 1 the rear axle short
            const bool beforeBreaks = asOne.freeMost > 2.5f * capAt && !nearly(frontBefore, capAt, 0.03f * capAt) && rearBeforeS < 0.5f * rearNoneS;
            axleSim = axleSim && good && beforeBreaks && handsOn;
            if (!(good && beforeBreaks && handsOn)) printf("     open by axle at %d: the bounds %d, the law before breaks them %d, the hand-on %d\n", rate, good, beforeBreaks, handsOn);
        }
        printf("     %s\n", line);
        Check(axleSim, line);
        const TopResult highOpen = run(highTop, 0.0f, true, 1u, false, 60), highNot = run(highTop, 0.0f, false, 1u, false, 60);
        static char high[600];
        snprintf(high, sizeof high,
                 "open by axle in a numbered gear (no cap), one front wheel of four free, the truck held standing: the free wheel to %.2f of the top and %.2f not Open, the pull %.1f and "
                 "%.1f, the rear axle %.1f and %.1f; no wheel lowered (%d)",
                 highOpen.freeMost / highTop, highNot.freeMost / highTop, highOpen.pull, highNot.pull, highOpen.pullEach[2] + highOpen.pullEach[3], highNot.pullEach[2] + highNot.pullEach[3],
                 highOpen.lowered);
        printf("     %s\n", high);
        Check(highOpen.pull > 0.0f && highOpen.lowered == 0 && isfinite(highOpen.freeMost), high);
    }

    // Open by axle, pure (BRIEF_open_by_axle.md with the Reviewer's addendum): the grouping (M1), a wheel alone (M2), the
    // truck's kept (M3), the release from the lock (S2), and the step that must fail under the law before
    {
        auto nearly = [](float a, float b, float tol) { return fabsf(a - b) <= tol; };
        struct Frame
        {
            float at[3], f[3], l[3];
        };
        auto frame = [](float x, float z, float yawDeg) {
            const float y = yawDeg * 3.14159265f / 180.0f;
            Frame b = { { x, 0.0f, z }, { cosf(y), 0.0f, sinf(y) }, { -sinf(y), 0.0f, cosf(y) } };
            return b;
        };
        struct Wheels
        {
            uint64_t body[kCapWheels];
            float along[kCapWheels], side[kCapWheels], c[kCapWheels][3];
            bool ok[kCapWheels];
            int n;
        };
        // a wheel at along, side and up of its body, placed in the world and read back in that body's frame
        auto add = [](Wheels &w, uint64_t id, const Frame &b, float along, float side, float up) {
            float *c = w.c[w.n];
            for (int k = 0; k < 3; k++) c[k] = b.at[k] + along * b.f[k] + side * b.l[k];
            c[1] += up;
            w.body[w.n] = id;
            w.ok[w.n] = AxlePlace(c, b.at, b.f, b.l, w.along[w.n], w.side[w.n]);
            w.n++;
        };
        auto groups = [](const Wheels &w) { return AxleGroups(w.body, w.along, w.side, w.ok, w.n); };
        const Frame straight = frame(3.0f, -7.0f, 25.0f);
        // 4, 6 and 8 wheels on one body, a tandem 1.3 m apart
        Wheels w4 = {}, w6 = {}, w8 = {};
        const float at8[4] = { 2.6f, 1.2f, -1.6f, -2.9f };
        for (int at = 0; at < 4; at++)
        {
            if (at == 0 || at == 3) add(w4, 1, straight, at8[at], 1.0f, 0.0f), add(w4, 1, straight, at8[at], -1.0f, 0.0f);
            if (at != 1) add(w6, 1, straight, at8[at], 1.0f, 0.0f), add(w6, 1, straight, at8[at], -1.0f, 0.0f);
            add(w8, 1, straight, at8[at], 1.0f, 0.0f), add(w8, 1, straight, at8[at], -1.0f, 0.0f);
        }
        const AxleGroupsOut g4 = groups(w4), g6 = groups(w6), g8 = groups(w8);
        bool counts = g4.axles == 2 && g4.alone == 0 && g6.axles == 3 && g6.alone == 0 && g8.axles == 4 && g8.alone == 0;
        for (int i = 0; i < 8; i++) counts = counts && g8.axle[i] == (i & ~1) && (i >= 6 || g6.axle[i] == (i & ~1)) && (i >= 4 || g4.axle[i] == (i & ~1));
        // a bent frame: the front axle on one body, a tandem on another turned 40 degrees. In its own body's frame each
        // pair is found. In the front body's frame (the old pairing) the rear wheels are over 1 m apart along and none pairs
        const Frame front = frame(0.0f, 0.0f, 0.0f), rearB = frame(-1.5f, 0.0f, 40.0f);
        Wheels bent = {}, oneFrame = {};
        add(bent, 1, front, 1.5f, 1.0f, 0.0f), add(bent, 1, front, 1.5f, -1.0f, 0.0f);
        for (int at = 0; at < 2; at++) add(bent, 2, rearB, -1.5f - 1.3f * (float)at, 1.0f, 0.0f), add(bent, 2, rearB, -1.5f - 1.3f * (float)at, -1.0f, 0.0f);
        const AxleGroupsOut gBent = groups(bent);
        oneFrame = bent;
        for (int i = 0; i < oneFrame.n; i++) oneFrame.body[i] = 1, AxlePlace(bent.c[i], front.at, front.f, front.l, oneFrame.along[i], oneFrame.side[i]);
        const AxleGroupsOut gOne = groups(oneFrame);
        const bool bends = gBent.axles == 3 && gBent.alone == 0 && gBent.axle[2] == 2 && gBent.axle[3] == 2 && gBent.axle[4] == 4 && gBent.axle[5] == 4 &&
                           fabsf(oneFrame.along[2] - oneFrame.along[3]) > 1.0f && gOne.alone >= 2;
        // two wheels opposite on two bodies are no axle
        Wheels two = {};
        add(two, 1, front, 0.0f, 1.0f, 0.0f), add(two, 2, front, 0.0f, -1.0f, 0.0f);
        const AxleGroupsOut gTwo = groups(two);
        // the front wheels at full steer (45 degrees about a pin 0.1 m inboard: 0.07 m along, one forward and one back),
        // and one rear wheel 0.3 m up on its spring
        Wheels pose = {};
        add(pose, 1, straight, 2.6f + 0.07f, 0.97f, 0.0f), add(pose, 1, straight, 2.6f - 0.07f, -0.97f, 0.0f);
        add(pose, 1, straight, -1.6f, 1.0f, 0.3f), add(pose, 1, straight, -1.6f, -1.0f, 0.0f);
        const AxleGroupsOut gPose = groups(pose);
        const bool poses = gPose.axles == 2 && gPose.alone == 0 && gPose.axle[1] == 0 && gPose.axle[3] == 2 && gTwo.axles == 2 && gTwo.alone == 2;
        // a wheel with no partner (the last of the 6 gone), a wheel in the middle (side 0), and a wheel not read: it and
        // its partner are each alone
        Wheels five = w6, mid = w4, unread = w6;
        five.n = 5;
        add(mid, 1, straight, 0.0f, 0.0f, 0.0f);
        unread.ok[2] = false;
        const AxleGroupsOut gFive = groups(five), gMid = groups(mid), gUnread = groups(unread);
        // the mutual nearest: a left at 0, a right at 0.3 and a left at 0.4: the right one and the second left are the
        // pair (0.1 m), the first left is alone though it has a wheel within the bound; and a pair 0.6 m apart is none
        Wheels near3 = {}, wide = {};
        add(near3, 1, straight, 0.0f, 1.0f, 0.0f), add(near3, 1, straight, 0.3f, -1.0f, 0.0f), add(near3, 1, straight, 0.4f, 1.0f, 0.0f);
        add(wide, 1, straight, 0.0f, 1.0f, 0.0f), add(wide, 1, straight, 0.6f, -1.0f, 0.0f);
        const AxleGroupsOut gNear = groups(near3), gWide = groups(wide);
        const bool lone = gFive.axles == 3 && gFive.alone == 1 && gFive.axle[4] == 4 && gMid.axles == 3 && gMid.alone == 1 && gMid.axle[4] == 4 && gUnread.axles == 4 && gUnread.alone == 2 &&
                          gUnread.axle[2] == 2 && gUnread.axle[3] == 3 && gNear.axle[0] == 0 && gNear.axle[1] == 1 && gNear.axle[2] == 1 && gNear.alone == 1 && gWide.alone == 2;
        // all wheel drive off on the 6 wheel truck: the hook's list holds the four rear wheels in an order of its own, and
        // a wheel the table does not hold; by the pointer each finds its axle, the stranger is its own
        uint64_t ptr[6];
        for (int i = 0; i < 6; i++) ptr[i] = 0x1000 + 0x100 * (uint64_t)i;
        const uint64_t driven[5] = { ptr[5], ptr[2], ptr[4], ptr[3], 0x9999 };
        int ax[5], place[5];
        for (int i = 0; i < 5; i++) ax[i] = AxleOfWheel(ptr, g6.axle, 6, driven[i], kCapWheels + i, &place[i]);
        const bool byPointer = ax[0] == 4 && ax[2] == 4 && ax[1] == 2 && ax[3] == 2 && ax[4] == kCapWheels + 4 && place[0] == 5 && place[1] == 2 && place[4] == -1;

        // the steps. One front wheel of four free, the others standing on ground that takes all they get each step; a
        // ceiling of 15; the drive's kick 150 on the free wheel and 3 on each other. By axle: the free wheel ends at 2 x
        // the ceiling, the front mean at the ceiling, its partner gets nothing in the end, each rear wheel gets its 3
        // each step as with no wheel free. As one axle (the law before) the free wheel ends at 4 x the ceiling and the
        // rear wheels get nothing in the end
        const bool okAll[kCapWheels] = { true, true, true, true };
        const float one[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        const int pairs[4] = { 0, 0, 2, 2 }, single[4] = { 0, 0, 0, 0 };
        float end[2][4] = {}, got[2][4] = {};
        int lowered = 0;
        for (int law = 0; law < 2; law++)
        {
            const int *axle = law ? single : pairs;
            float freeW = 0.0f;
            for (int step = 0; step < 400; step++)
            {
                const float pre[4] = { freeW, 0.0f, 0.0f, 0.0f }, post[4] = { freeW + 150.0f, 3.0f, 3.0f, 3.0f };
                const DiffEvenOut e = DiffEvenAxles(pre, post, one, okAll, axle, 4);
                const OpenAxlesOut o = DiffOpenAxles(pre, e.final, one, okAll, axle, 4, 15.0f, false);
                for (int i = 0; i < 4; i++)
                {
                    end[law][i] = o.o.final[i], got[law][i] = o.o.final[i] - pre[i];
                    if (o.o.final[i] < pre[i]) lowered++;
                }
                freeW = o.o.final[0];
            }
        }
        const bool byAxle = nearly(end[0][0], 30.0f, 0.01f) && nearly(got[0][1], 0.0f, 0.01f) && nearly(0.5f * (end[0][0] + end[0][1]), 15.0f, 0.01f) && got[0][2] == 3.0f && got[0][3] == 3.0f &&
                            lowered == 0;
        const bool beforeFails = nearly(end[1][0], 60.0f, 0.01f) && nearly(got[1][2], 0.0f, 0.01f) && !nearly(0.5f * (end[1][0] + end[1][1]), 15.0f, 1.0f);
        // M2: a wheel alone is held at the ceiling itself; a wheel whose partner is not read this step the same, and
        // the one not read is not touched
        const int own[2] = { kCapWheels, kCapWheels + 1 }, pair2[2] = { 0, 0 };
        const float aPre[2] = { 10.0f, 10.0f }, aPost[2] = { 20.0f, 12.0f };
        const bool half[2] = { true, false };
        const DiffEvenOut aEven = DiffEvenAxles(aPre, aPost, one, okAll, own, 2), hEven = DiffEvenAxles(aPre, aPost, one, half, pair2, 2);
        const OpenAxlesOut aOut = DiffOpenAxles(aPre, aEven.final, one, okAll, own, 2, 15.0f, false), hOut = DiffOpenAxles(aPre, hEven.final, one, half, pair2, 2, 15.0f, false);
        const bool aloneHeld = !aEven.wrote && aEven.used == 0 && aOut.o.final[0] == 15.0f && aOut.o.final[1] == 12.0f && aOut.axles == 2 && aOut.o.clamps == 1 && !hEven.wrote &&
                               hOut.o.final[0] == 15.0f && hOut.o.final[1] == 12.0f && hOut.axles == 1;
        // M3: two axles the drive added 2 to each, one kept whole and one at 0.1: the truck's kept is 0.55, the clamps the
        // sum and the excess the largest; with all axles alike the figures are the one step's over all four, to the bit
        const float mPre[4] = { 0.0f, 0.0f, 14.9f, 14.9f }, mEven[4] = { 1.0f, 1.0f, 15.9f, 15.9f };
        const OpenAxlesOut mOut = DiffOpenAxles(mPre, mEven, one, okAll, pairs, 4, 15.0f, false);
        const float sPre[4] = { 8.0f, 8.0f, 8.0f, 8.0f }, sEven[4] = { 9.0f, 9.0f, 9.0f, 9.0f };
        const OpenAxlesOut sOut = DiffOpenAxles(sPre, sEven, one, okAll, pairs, 4, 8.3f, false);
        const CapStepOut sOne = DiffOpenStep(sPre, sEven, one, okAll, 4, 8.3f);
        const float xPre[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, xEven[4] = { 20.0f, 20.0f, 16.0f, 16.0f };
        const OpenAxlesOut xOut = DiffOpenAxles(xPre, xEven, one, okAll, pairs, 4, 15.0f, false);
        bool kept = nearly(mOut.o.kept, 0.55f, 1e-3f) && mOut.o.clamps == 2 && mOut.axles == 2 && nearly(mOut.k[0], 1.0f, 0.0f) && nearly(mOut.k[1], 0.1f, 1e-3f) && mOut.wheels[0] == 3u &&
                    mOut.wheels[1] == 12u && nearly(mOut.mean[1], 15.0f, 1e-4f) && sOut.o.kept == sOne.kept && sOut.o.clamps == sOne.clamps && sOut.o.excess == sOne.excess &&
                    xOut.o.clamps == 4 && nearly(xOut.o.excess, 5.0f, 1e-4f) && nearly(xOut.o.kept, (0.75f * 40.0f + 0.9375f * 32.0f) / 72.0f, 1e-4f);
        for (int i = 0; i < 4; i++) kept = kept && sOut.o.final[i] == sOne.final[i] && sOut.o.keptEach[i] == sOne.keptEach[i];
        // S2, the release: the step after the lock, Open by axle with the free wheel at 40 against a ceiling of 15. No
        // wheel is lowered, the free wheel keeps its 40, its partner gets nothing of the kick and the rear axle its own
        const float rPre[4] = { 40.0f, 0.0f, 0.0f, 0.0f }, rPost[4] = { 190.0f, 3.0f, 3.0f, 3.0f };
        const DiffEvenOut rEven = DiffEvenAxles(rPre, rPost, one, okAll, pairs, 4);
        const OpenAxlesOut rOut = DiffOpenAxles(rPre, rEven.final, one, okAll, pairs, 4, 15.0f, false);
        const bool release = rOut.o.final[0] == 40.0f && rOut.o.final[1] == 0.0f && rOut.o.final[2] == 3.0f && rOut.o.final[3] == 3.0f && nearly(rEven.final[1], 76.5f, 1e-3f);
        // one wheel free on each axle, both at 2 x the ceiling: the standing wheels get nothing
        const float bPre[4] = { 30.0f, 0.0f, 30.0f, 0.0f }, bPost[4] = { 180.0f, 3.0f, 180.0f, 3.0f };
        const DiffEvenOut bEven = DiffEvenAxles(bPre, bPost, one, okAll, pairs, 4);
        const OpenAxlesOut bOut = DiffOpenAxles(bPre, bEven.final, one, okAll, pairs, 4, 15.0f, false);
        const bool bothFree = bOut.o.final[1] == 0.0f && bOut.o.final[3] == 0.0f && bOut.o.final[0] == 30.0f && bOut.o.kept == 0.0f;
        // F1 of the review of 122, the hand-on between axles (handOn). The free front wheel at 2 x the ceiling, the
        // drive's 153 all on the front axle: its limit takes it, the rear axle is handed it and ends with its mean at the
        // ceiling, the rest is dropped; the partner of the free wheel gets nothing; the same step with nothing handed
        // on leaves the rear wheels their 3
        const float hPre[4] = { 30.0f, 0.0f, 0.0f, 0.0f }, hPost[4] = { 180.0f, 3.0f, 3.0f, 3.0f };
        const DiffEvenOut hE = DiffEvenAxles(hPre, hPost, one, okAll, pairs, 4);
        const OpenAxlesOut hOn = DiffOpenAxles(hPre, hE.final, one, okAll, pairs, 4, 15.0f, true), hOff = DiffOpenAxles(hPre, hE.final, one, okAll, pairs, 4, 15.0f, false);
        bool handed = nearly(hOn.o.final[0], 30.0f, 1e-3f) && nearly(hOn.o.final[1], 0.0f, 1e-3f) && nearly(hOn.o.final[2], 15.0f, 1e-3f) && nearly(hOn.o.final[3], 15.0f, 1e-3f) &&
                      nearly(hOn.mean[1], 15.0f, 1e-3f) && nearly(hOn.got[1], 24.0f, 1e-3f) && hOn.got[0] == 0.0f && nearly(hOn.o.spread, 24.0f, 1e-3f) &&
                      nearly(hOn.o.kept, 30.0f / 159.0f, 1e-3f) && hOff.o.final[2] == 3.0f && hOff.o.final[3] == 3.0f && hOff.o.spread == 0.0f && hOff.got[1] == 0.0f;
        // all of it placed where there is room: each wheel of the axle that receives gets the same impulse through its
        // own gain, and the truck's kept counts what was handed on (1 where the gains are alike)
        const float lEven[4] = { 30.6f, 0.6f, 0.2f, 0.2f }, gEven[4] = { 30.6f, 0.6f, 0.2f, 0.1f }, gGain[4] = { 1.0f, 1.0f, 1.0f, 0.5f };
        const OpenAxlesOut lOn = DiffOpenAxles(hPre, lEven, one, okAll, pairs, 4, 15.0f, true), gOn = DiffOpenAxles(hPre, gEven, gGain, okAll, pairs, 4, 15.0f, true);
        handed = handed && nearly(lOn.o.final[2], 0.8f, 1e-4f) && nearly(lOn.o.final[3], 0.8f, 1e-4f) && nearly(lOn.o.final[1], 0.0f, 1e-4f) && nearly(lOn.o.kept, 1.0f, 1e-3f) &&
                 nearly(gOn.o.final[2], 0.8f, 1e-4f) && nearly(gOn.o.final[3], 0.4f, 1e-4f) && nearly(gOn.got[1], 0.9f, 1e-4f);
        // M3 with the hand-on: what the rear axle's limit took (0.9 a wheel) goes to the front axle and the truck's kept is 1
        const OpenAxlesOut mOn = DiffOpenAxles(mPre, mEven, one, okAll, pairs, 4, 15.0f, true);
        handed = handed && nearly(mOn.o.final[0], 1.9f, 1e-3f) && nearly(mOn.o.final[1], 1.9f, 1e-3f) && nearly(mOn.o.final[2], 15.0f, 1e-3f) && nearly(mOn.o.kept, 1.0f, 1e-3f);
        // no axle to take: one wheel free on each axle, both axles at their limit, and an axle the drive pushes back
        // get nothing, to the bit as with nothing handed on
        const OpenAxlesOut bOn = DiffOpenAxles(bPre, bEven.final, one, okAll, pairs, 4, 15.0f, true), xOn = DiffOpenAxles(xPre, xEven, one, okAll, pairs, 4, 15.0f, true);
        const float backPre[4] = { 30.0f, 0.0f, 5.0f, 5.0f }, backEven[4] = { 30.6f, 0.6f, 4.8f, 4.8f };
        const OpenAxlesOut backOn = DiffOpenAxles(backPre, backEven, one, okAll, pairs, 4, 15.0f, true);
        handed = handed && bOn.o.spread == 0.0f && xOn.o.spread == 0.0f && bOn.o.kept == bOut.o.kept && xOn.o.kept == xOut.o.kept && backOn.o.spread == 0.0f && backOn.o.final[2] == 4.8f &&
                 backOn.o.final[3] == 4.8f;
        for (int i = 0; i < 4; i++) handed = handed && bOn.o.final[i] == bOut.o.final[i] && xOn.o.final[i] == xOut.o.final[i];
        // 6 and 8 wheels with one free: the axles that take share by what the drive gave each, evenly where alike, and
        // none goes over the ceiling
        const bool ok8[8] = { true, true, true, true, true, true, true, true };
        const float one8[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
        const int six[6] = { 0, 0, 2, 2, 4, 4 }, eight[8] = { 0, 0, 2, 2, 4, 4, 6, 6 };
        const float wPre[8] = { 30.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, wPost[8] = { 180.0f, 3.0f, 3.0f, 3.0f, 3.0f, 3.0f, 3.0f, 3.0f };
        const float wLow[8] = { 30.6f, 0.6f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f }, wSkew[6] = { 30.6f, 0.6f, 0.3f, 0.3f, 0.1f, 0.1f };
        const DiffEvenOut e6 = DiffEvenAxles(wPre, wPost, one8, ok8, six, 6), e8 = DiffEvenAxles(wPre, wPost, one8, ok8, eight, 8);
        const OpenAxlesOut full6 = DiffOpenAxles(wPre, e6.final, one8, ok8, six, 6, 15.0f, true), full8 = DiffOpenAxles(wPre, e8.final, one8, ok8, eight, 8, 15.0f, true);
        const OpenAxlesOut low6 = DiffOpenAxles(wPre, wLow, one8, ok8, six, 6, 15.0f, true), low8 = DiffOpenAxles(wPre, wLow, one8, ok8, eight, 8, 15.0f, true);
        const OpenAxlesOut skew6 = DiffOpenAxles(wPre, wSkew, one8, ok8, six, 6, 15.0f, true);
        handed = handed && full6.axles == 3 && full8.axles == 4 && nearly(full6.o.final[0], 30.0f, 1e-3f) && nearly(full8.o.final[0], 30.0f, 1e-3f) && nearly(full6.o.final[1], 0.0f, 1e-3f) &&
                 nearly(full8.o.final[1], 0.0f, 1e-3f) && nearly(skew6.o.final[2], 0.75f, 1e-4f) && nearly(skew6.o.final[3], 0.75f, 1e-4f) && nearly(skew6.o.final[4], 0.25f, 1e-4f) &&
                 nearly(skew6.o.final[5], 0.25f, 1e-4f) && nearly(skew6.o.final[1], 0.0f, 1e-4f);
        for (int i = 2; i < 8; i++)
            handed = handed && nearly(full8.o.final[i], 15.0f, 1e-3f) && nearly(low8.o.final[i], 0.4f, 1e-4f) && (i >= 6 || (nearly(full6.o.final[i], 15.0f, 1e-3f) && nearly(low6.o.final[i], 0.5f, 1e-4f)));
        // no wheel is lowered by any of them, and nothing is kept for a later step: the step after, with no wheel at a
        // limit, leaves what the drive gave to the bit
        const OpenAxlesOut *all[] = { &hOn, &lOn, &gOn, &mOn, &bOn, &xOn, &backOn };
        const float *alPre[] = { hPre, hPre, hPre, mPre, bPre, xPre, backPre }, *alEven[] = { hE.final, lEven, gEven, mEven, bEven.final, xEven, backEven };
        for (int which = 0; which < 7; which++)
            for (int i = 0; i < 4; i++) handed = handed && all[which]->o.final[i] >= fminf(alPre[which][i], alEven[which][i]);
        const float nPre[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, nEven[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        const OpenAxlesOut after = DiffOpenAxles(nPre, nEven, one, okAll, pairs, 4, 15.0f, true);
        for (int i = 0; i < 4; i++) handed = handed && after.o.final[i] == 1.0f;
        handed = handed && after.o.spread == 0.0f && after.o.kept == 1.0f;
        if (!handed)
            printf("     open by axle, the hand-on: %.3f %.3f %.3f %.3f, got %.3f, spread %.3f, kept %.4f; low %.3f %.3f kept %.4f; gains %.3f %.3f; M3 %.3f %.3f kept %.4f; back %.3f; six %.3f %.3f "
                   "%.3f, eight %.3f %.3f, skew %.3f %.3f\n",
                   hOn.o.final[0], hOn.o.final[1], hOn.o.final[2], hOn.o.final[3], hOn.got[1], hOn.o.spread, hOn.o.kept, lOn.o.final[2], lOn.o.final[3], lOn.o.kept, gOn.o.final[2], gOn.o.final[3],
                   mOn.o.final[0], mOn.o.final[2], mOn.o.kept, backOn.o.final[2], full6.o.final[2], full6.o.final[4], low6.o.final[2], full8.o.final[6], low8.o.final[6], skew6.o.final[2],
                   skew6.o.final[4]);
        Check(handed,
              "open by axle, the hand-on between axles: what the limit of the free wheel's axle took from the drive goes to the other axles, evenly within each and by what the drive "
              "gave each among them; the axle that receives stays with its mean at the ceiling and the rest is dropped; the partner of the free wheel gets nothing; with room all of "
              "it is placed and the truck's kept is 1; an axle at its own limit and one the drive pushes back take nothing; 6 and 8 wheels with one free; no wheel lowered and "
              "nothing kept for the step after");
        if (!(counts && bends && poses && lone && byPointer && byAxle && beforeFails && aloneHeld && kept && release && bothFree))
            printf("     open by axle: counts %d, bends %d, poses %d, lone %d, by pointer %d, by axle %d (%.3f %.3f %.3f %.3f), the law before fails %d (%.3f, %.3f), alone held %d, kept %d "
                   "(%.4f, %.4f), release %d, both free %d\n",
                   counts, bends, poses, lone, byPointer, byAxle, end[0][0], end[0][1], got[0][2], got[0][3], beforeFails, end[1][0], got[1][2], aloneHeld, kept, mOut.o.kept, xOut.o.kept, release,
                   bothFree);
        Check(counts && bends && poses && lone && byPointer,
              "open by axle, the grouping: two wheels are one axle on the same parent body, on other sides and each the other's nearest along it within 0.5 m; 4, 6 and 8 wheels give "
              "2, 3 and 4 axles; a frame bent by 40 degrees, full steer and a wheel 0.3 m up change nothing; a wheel with no partner, one in the middle and one not read are alone; "
              "the driven wheels find their axle by the pointer with all wheel drive off");
        Check(byAxle && beforeFails && aloneHeld && kept && release && bothFree,
              "open by axle, the steps: one front wheel of four free ends at 2 x the ceiling with the front mean at the ceiling, and the rear wheels get what the drive gives them (as "
              "one axle, the law before, it ends at 4 x and they get nothing); a wheel alone is held at the ceiling; the truck's kept is the axles' weighted by what each added, "
              "0.55 for 1 and 0.1, and one step's figure to the bit where all are alike; from the lock to Open over a free wheel no wheel is lowered; one free wheel on each axle "
              "leaves the standing ones nothing");
    }

    // Open with every wheel on the ground (F1 of the review of 118 on 121): the governor ceiling's floored table once more
    // with Open in force, two and four driven wheels. The same bounds: after 3 s the needle within 150 rpm and the truck
    // within 2 percent of the top around its middle, and the truck never past the governor's end by more than 2 percent
    // of the top. With a limit of n times the ceiling on each wheel nothing ended the pull before F's switch
    {
        TopSetup u = {};
        u.slot.vehicle = 1, u.slot.mode = kCutMap, u.slot.share = 1.0f, u.slot.pedal = 1.0f;
        u.cap = 1.5f, u.kick = 500.0f, u.mdav = 0.01f, u.inertia = 10.0f, u.flat = -1.0f, u.holdV = -1.0f;
        u.seconds = 20.0f, u.rise = 0.008f, u.seed = 7, u.gripNoise = 0.3f, u.dragNoise = 0.3f, u.couple = 1.0f / 60.0f;
        u.open = true;
        static char line[9000];
        const float tops[3] = { 2.4f, 3.4f, 4.5f }, grips[6] = { 0.5f, 1.0f, 2.0f, 2.5f, 5.0f, 10.0f };
        int len = snprintf(line, sizeof line, "open differential, floored at the top of a low gear for 20 s with every wheel on the ground (wheels, top, grip: truck band, needle band rpm, the truck's most):");
        bool calm = true;
        float needleMost = 0.0f, truckMost = 0.0f;
        for (int wheels = 2; wheels <= 4; wheels += 2)
            for (int tI = 0; tI < 3; tI++)
                for (int g = 0; g < 6; g++)
                {
                    const float top = tops[tI];
                    TopSetup c = u;
                    const CutBand band = CutGovernBand(top, kTachOverrun);
                    c.slot.map = TorqueCutMap{ 1.0f, 1.0f, 0.0f, 1.0f, 0.15f, top, 800.0f, 2500.0f, 1.2f, { 5, { 0.25f, 0.45f, 0.6f, 0.8f, 1.0f }, { 0.55f, 0.85f, 1.0f, 1.0f, 0.85f } }, band.from, band.to };
                    c.wheels = wheels, c.grip = grips[g], c.s0 = c.v0 = 0.9f * top;
                    c.drag = 0.5f * (float)wheels * c.couple * fminf(c.grip, 0.45f * top);
                    const TopResult q = TopRun(c);
                    const bool good = q.needleMax - q.needleMin <= 150.0f && 0.5f * (q.vMax - q.vMin) <= 0.02f * top && q.vMost <= (c.slot.map.governTo + 0.02f) * top && q.lowered == 0;
                    calm = calm && good;
                    needleMost = fmaxf(needleMost, q.needleMax - q.needleMin), truckMost = fmaxf(truckMost, q.vMost / top);
                    len += snprintf(line + len, sizeof line - len, "%s %d, %.1f, %.1f: %.3f to %.3f, %.0f, %.3f%s", wheels > 2 || tI || g ? ";" : "", wheels, top, c.grip, q.vMin / top, q.vMax / top,
                                    q.needleMax - q.needleMin, q.vMost / top, good ? "" : " <<<");
                }
        printf("     open, floored: the widest needle band %.0f rpm, the truck's most %.3f of the top\n", needleMost, truckMost);
        Check(calm, line);
    }

    // the physics rate, stage 1 (BRIEF_physics_rate_stage1.md, section 7 and the Reviewer's S3): the governor ceiling's
    // floored table and Open's once more with the sim at 120 steps a second and stage 1's corrections (TopSetup's rate),
    // the same bounds as at 60
    {
        TopSetup u = {};
        u.slot.vehicle = 1, u.slot.mode = kCutMap, u.slot.share = 1.0f, u.slot.pedal = 1.0f;
        u.cap = 1.5f, u.kick = 500.0f, u.mdav = 0.01f, u.inertia = 10.0f, u.flat = -1.0f, u.holdV = -1.0f;
        u.seconds = 20.0f, u.rise = 0.008f, u.seed = 7, u.gripNoise = 0.3f, u.dragNoise = 0.3f, u.couple = 1.0f / 60.0f;
        u.rate = 120;
        static char line[14000];
        const float tops[3] = { 2.4f, 3.4f, 4.5f }, grips[6] = { 0.5f, 1.0f, 2.0f, 2.5f, 5.0f, 10.0f };
        for (int open = 0; open < 2; open++)
        {
            int len = snprintf(line, sizeof line, "physics rate 120, %s, floored at the top of a low gear for 20 s (wheels, top, grip: truck band, needle band rpm, push jump over push, the truck's most; "
                                                  "the needle band at 60):", open ? "open differential" : "governor ceiling");
            bool calm = true;
            float needleMost = 0.0f, truckMost = 0.0f, jumpMost = 0.0f, needle60 = 0.0f;
            int failing = 0, cases = 0;
            for (int wheels = 2; wheels <= (open ? 4 : 2); wheels += 2)
                for (int tI = 0; tI < 3; tI++)
                    for (int g = 0; g < 6; g++)
                    {
                        const float top = tops[tI];
                        TopSetup c = u;
                        const CutBand band = CutGovernBand(top, kTachOverrun);
                        c.slot.map = TorqueCutMap{ 1.0f, 1.0f, 0.0f, 1.0f, 0.15f, top, 800.0f, 2500.0f, 1.2f, { 5, { 0.25f, 0.45f, 0.6f, 0.8f, 1.0f }, { 0.55f, 0.85f, 1.0f, 1.0f, 0.85f } }, band.from, band.to };
                        c.open = open != 0, c.wheels = wheels, c.grip = grips[g], c.s0 = c.v0 = 0.9f * top;
                        c.drag = 0.5f * (float)wheels * c.couple * fminf(c.grip, 0.45f * top);
                        const TopResult q = TopRun(c);
                        c.rate = 60;
                        const TopResult at60 = TopRun(c);
                        bool good = q.needleMax - q.needleMin <= 150.0f && 0.5f * (q.vMax - q.vMin) <= 0.02f * top && q.lowered == 0;
                        if (open) good = good && q.vMost <= (c.slot.map.governTo + 0.02f) * top;
                        else good = good && q.pushJump <= 0.35f * q.push && q.vMin >= kCapCeilFrom * top && q.vMax <= c.slot.map.governTo * top;
                        calm = calm && good, cases++, failing += good ? 0 : 1;
                        needleMost = fmaxf(needleMost, q.needleMax - q.needleMin), truckMost = fmaxf(truckMost, q.vMost / top), jumpMost = fmaxf(jumpMost, q.pushJump / q.push);
                        needle60 = fmaxf(needle60, at60.needleMax - at60.needleMin);
                        len += snprintf(line + len, sizeof line - len, "%s %d, %.1f, %.1f: %.3f to %.3f, %.0f, %.2f, %.3f; %.0f%s", wheels > 2 || tI || g ? ";" : "", wheels, top, c.grip, q.vMin / top,
                                        q.vMax / top, q.needleMax - q.needleMin, q.pushJump / q.push, q.vMost / top, at60.needleMax - at60.needleMin, good ? "" : " <<<");
                    }
            printf("     physics rate 120, %s: %d of %d cases failing, the widest needle band %.0f rpm (%.0f at 60), the truck's most %.3f of the top, the largest push jump %.2f of the push\n",
                   open ? "open" : "the ceiling", failing, cases, needleMost, needle60, truckMost, jumpMost);
            Check(calm, line);
        }
    }

    // C6's first step (C3 of BRIEF_governor_sway.md, F1 of the governor ceiling's review): the push the ground takes a
    // step against the truck's speed lies on the cap's line whatever the bracket. The law before the ceiling (the cap
    // flat at 1.5 of the top), four wheels in L-, the bracket held at 0.39 and at 0.012 (flat), the truck's speed held
    // (holdV) at 0 to 1.25 of the top, a ground that takes all a wheel has over it. The cap's line: each wheel leaves the
    // step at the cap and the ground takes its speed over the truck's, so the truck gains
    // wheels x couple x (the cap - the truck's speed) a step, straight down to 0 at the cap
    {
        TopSetup u = {};
        u.slot.vehicle = 1, u.slot.mode = kCutMap, u.slot.share = 1.0f, u.slot.pedal = 1.0f;
        u.cap = 1.5f, u.wheels = 4, u.kick = 500.0f, u.mdav = 0.01f, u.inertia = 10.0f, u.grip = 1000.0f;
        u.seconds = 10.0f, u.rise = 0.008f, u.seed = 7, u.couple = 1.0f / 60.0f, u.oldLaw = true;
        const float top = 2.4f, speeds[6] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.25f }, brackets[2] = { 0.39f, 0.012f };
        const CutBand band = CutGovernBand(top, kTachOverrun);
        u.slot.map = TorqueCutMap{ 1.0f, 1.0f, 0.0f, 1.0f, 0.15f, top, 800.0f, 2500.0f, 1.2f, { 5, { 0.25f, 0.45f, 0.6f, 0.8f, 1.0f }, { 0.55f, 0.85f, 1.0f, 1.0f, 0.85f } }, band.from, band.to };
        static char line[1600];
        int len = snprintf(line, sizeof line, "the cap's line (C3): the truck's gain a step from four wheels in L-, the law before the ceiling, the truck's speed held (speed over the top: the line, at a "
                                              "bracket of 0.39 with its clamp share, at 0.012 with its clamp share):");
        bool onLine = true;
        float offMost = 0.0f;
        for (int i = 0; i < 6; i++)
        {
            const float v = speeds[i] * top, want = 4.0f * u.couple * (1.5f * top - v);
            float got[2], clamp[2];
            for (int at = 0; at < 2; at++)
            {
                TopSetup c = u;
                c.flat = brackets[at], c.holdV = v, c.s0 = c.v0 = v;
                const TopResult q = TopRun(c);
                got[at] = q.push, clamp[at] = q.clampShare;
                offMost = fmaxf(offMost, fabsf(q.push - want) / want);
                onLine = onLine && fabsf(q.push - want) <= 0.005f * want && q.clampShare > 0.99f && q.pushJump <= 0.005f * want;
            }
            len += snprintf(line + len, sizeof line - len, "%s %.2f: %.4f, %.4f (%.2f), %.4f (%.2f)", i ? ";" : "", speeds[i], want, got[0], clamp[0], got[1], clamp[1]);
        }
        printf("     the cap's line: the push is off the line by %.2f percent at most\n", 100.0f * offMost);
        Check(onLine, line);
    }

    // Locked (M3, S2): the lock's step with the ground behind the wheels
    {
        auto nearly = [](float a, float b, float tol) { return fabsf(a - b) <= tol; };
        const bool okAll[kCapWheels] = { true, true, true, true };
        const float one[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        // one wheel in the air and one held by the ground, the kick of 150 on the free one: W 1 and 1 + 1000 / 2 x 0.25 =
        // 126, C 5, aHeld 150 / 127, the rest to the held wheel
        const float pre[2] = { 5.0f, 5.0f }, post[2] = { 155.0f, 5.0f }, load[2] = { 0.0f, 1.0f }, radius[2] = { 0.5f, 0.5f };
        const LockStepOut l = LockStepHeld(pre, post, one, load, radius, okAll, 2, 1.0f, 1000.0f), half = LockStepHeld(pre, post, one, load, radius, okAll, 2, 0.5f, 1000.0f);
        const float aHeld = 150.0f / 127.0f;
        const bool held = nearly(l.final[0], 5.0f + aHeld, 1e-3f) && nearly(l.final[1], 5.0f + aHeld + (150.0f - 2.0f * aHeld), 1e-2f) && nearly(l.final[0] + l.final[1], 2.0f * 5.0f + 150.0f, 1e-2f) &&
                          l.wrote && l.used == 2 && nearly(l.c, 5.0f, 1e-4f) && nearly(l.r, 150.0f - 2.0f * aHeld, 1e-2f) && nearly(l.over, 150.0f, 1e-3f) &&
                          nearly(half.final[0], 155.0f + 0.5f * (l.final[0] - 155.0f), 1e-3f) && nearly(half.final[1], 5.0f + 0.5f * (l.final[1] - 5.0f), 1e-3f);
        // no mass or no radius: the stand-in weight I x (1 + 30 x load); every wheel in the air: LockStep itself
        const float noRadius[2] = { 0.0f, 0.0f }, air[2] = { 0.0f, 0.0f };
        const LockStepOut m0 = LockStepHeld(pre, post, one, load, radius, okAll, 2, 1.0f, 0.0f), r0 = LockStepHeld(pre, post, one, load, noRadius, okAll, 2, 1.0f, 1000.0f);
        const LockStepOut inAir = LockStepHeld(pre, post, one, air, radius, okAll, 2, 1.0f, 1000.0f), plain = LockStep(post, one, okAll, 2, 1.0f);
        const float a30 = 150.0f / 32.0f;
        const bool standIn = nearly(m0.final[0], 5.0f + a30, 1e-3f) && m0.final[0] == r0.final[0] && m0.final[1] == r0.final[1] && inAir.final[0] == plain.final[0] &&
                             inAir.final[1] == plain.final[1] && inAir.final[0] == 80.0f && inAir.wrote;
        // nothing: one wheel, no k, a NaN before, both spreads under the least
        const float nanPre[2] = { NAN, 5.0f }, same[2] = { 5.0f, 5.0f };
        const bool nothing = !LockStepHeld(pre, post, one, load, radius, okAll, 1, 1.0f, 1000.0f).wrote && !LockStepHeld(pre, post, one, load, radius, okAll, 2, 0.0f, 1000.0f).wrote &&
                             !LockStepHeld(nanPre, post, one, load, radius, okAll, 2, 1.0f, 1000.0f).wrote && !LockStepHeld(same, same, one, load, radius, okAll, 2, 1.0f, 1000.0f).wrote;
        // S2, the drive's case as a sequence: 60 steps, three wheels on the ground that a stand-in solver sets back to the
        // ground's 5 rad/s each step, one in the air, kicks of 150 on it and 3 on the others; a truck of 4000 kg on 0.5 m
        auto run = [&](bool heldStep, float k) {
            float w[4] = { 5.0f, 5.0f, 5.0f, 5.0f }, worst = 0.0f, identity = 0.0f;
            const float loads[4] = { 1.0f, 1.0f, 1.0f, 0.0f }, radii[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
            for (int step = 0; step < 60; step++)
            {
                float p[4], q[4];
                for (int i = 0; i < 4; i++) p[i] = w[i], q[i] = w[i] + (i == 3 ? 150.0f : 3.0f);
                const LockStepOut o = heldStep ? LockStepHeld(p, q, one, loads, radii, okAll, 4, k, 4000.0f) : LockStep(q, one, okAll, 4, k);
                if (heldStep) identity = fmaxf(identity, fabsf(o.final[0] + o.final[1] + o.final[2] + o.final[3] - (4.0f * o.c + 159.0f)));
                for (int i = 0; i < 3; i++) w[i] = 5.0f;
                w[3] = o.final[3];
                worst = fmaxf(worst, w[3] - 5.0f);
            }
            struct R { float ahead, worst, identity; };
            return R{ w[3] - 5.0f, worst, identity };
        };
        const auto old = run(false, 1.0f), now = run(true, 1.0f), blend = run(true, 0.5f);
        char line[500];
        snprintf(line, sizeof line,
                 "locked differential, the drive's case: 60 steps, three wheels set back to the ground's speed and one in the air, kicks of 150; the step of before leaves "
                 "the free wheel %.1f rad/s ahead, the step with the ground behind the wheels %.2f (at most %.2f on the way), at k 0.5 %.1f; the momentum off by %.4f at most",
                 old.ahead, now.ahead, now.worst, blend.ahead, now.identity);
        printf("     %s\n", line);
        if (!(held && standIn && nothing)) printf("     locked: held %d (%.4f %.4f c %.4f r %.4f over %.4f), stand-in %d (%.4f), nothing %d\n", held, l.final[0], l.final[1], l.c, l.r, l.over, standIn, m0.final[0], nothing);
        Check(held && standIn && nothing && now.worst < 2.0f && old.ahead > 20.0f * now.ahead && now.identity < 0.05f && blend.ahead > now.ahead && blend.ahead < old.ahead * 4.0f, line);
    }

    // the lock's bounds (BRIEF_lock_wheels_backward.md, M2; LockBound): with the drive's impulse over 0 no wheel leaves
    // the lock under min(post, 0), none over the fastest wheel the drive left, and the lock adds no spin. Six wheels of
    // inertia 34.5 on 0.586 m under 12000 kg, each case with the law of before (bound false) beside it. A spread of
    // 0.02 rad/s before the drive is in the cases with equal speeds, since under kLockSpreadMin nothing is written.
    {
        const bool bOk[kCapWheels] = { true, true, true, true, true, true };
        float bGain[6], bRadius[6];
        for (int i = 0; i < 6; i++) bGain[i] = 1.0f / 34.5f, bRadius[i] = 0.586f;
        const float bMass = 12000.0f;
        struct BSeen
        {
            float least, most, added;
            bool rules, lowered;
        };
        auto seen = [](const LockStepOut &o, const float *p6) {
            BSeen b = { o.final[0], o.final[0], 0.0f, true, false };
            float fastest = p6[0];
            for (int i = 0; i < 6; i++)
            {
                b.least = fminf(b.least, o.final[i]), b.most = fmaxf(b.most, o.final[i]), fastest = fmaxf(fastest, p6[i]);
                b.added += o.final[i] - p6[i];
                if (o.final[i] < fminf(p6[i], 0.0f) - 1e-4f) b.rules = false;
                if (o.final[i] < p6[i] - 1e-4f) b.lowered = true;
            }
            if (b.most > fastest + 1e-4f || b.added > 1e-3f) b.rules = false;
            return b;
        };
        int breachBefore = 0, breachNow = 0, underNow = 0, lockCalls = 0;
        auto both = [&](const float *pre6, const float *add6, const float *load6, float k, LockStepOut &before, LockStepOut &after, float *p6) {
            for (int i = 0; i < 6; i++) p6[i] = pre6[i] + add6[i];
            before = LockStepHeld(pre6, p6, bGain, load6, bRadius, bOk, 6, k, bMass, false);
            after = LockStepHeld(pre6, p6, bGain, load6, bRadius, bOk, 6, k, bMass);
            breachBefore += before.breach, breachNow += after.breach, underNow += after.under, lockCalls++;
        };
        const float nine[6] = { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f };
        LockStepOut b0, a0;
        float p[6];
        // 1. standing, the drive leaves 9 on each, two wheels carry the truck: before 21.0 on those and 3.0 on the rest
        const float pre1[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.02f }, load1[6] = { 1.0f, 1.0f, 0.1f, 0.1f, 0.1f, 0.1f };
        both(pre1, nine, load1, 1.0f, b0, a0, p);
        const BSeen s1b = seen(b0, p), s1 = seen(a0, p);
        // (since part 197 the lock itself leaves the six at one speed, 9.00, so the bounds need move none)
        const bool case1 = !s1b.rules && s1b.most > 20.5f && s1b.most < 21.5f && s1b.least > 2.5f && s1b.least < 3.5f && s1.rules && s1.most <= 9.02f + 1e-4f && s1.least >= 0.0f &&
                           s1.most - s1.least < 1e-3f && a0.wrote && a0.under == 0 && a0.j > 0.0f;
        // in L: a ceiling just over the drive's 9 cuts two wheels of the lock's before and none of them now
        const CapStepOut cap1b = CapStep(pre1, b0.final, bGain, bOk, 6, 9.05f, true), cap1 = CapStep(pre1, a0.final, bGain, bOk, 6, 9.05f, true), cap1d = CapStep(pre1, p, bGain, bOk, 6, 9.05f, true);
        const bool inL = cap1b.clamps == 2 && cap1.clamps == 0 && cap1d.clamps == 0;
        const float most1b = s1b.most, least1b = s1b.least, most1 = s1.most, least1 = s1.least;
        // 2. the wheels turning back before the drive, which adds 9: before C is -1.95 and the lock writes 16.3 and 2.4
        const float pre2[6] = { -2.5f, -0.8f, -0.8f, -2.5f, -0.8f, -0.8f }, load2[6] = { 1.0f, 0.2f, 0.2f, 1.0f, 0.2f, 0.2f };
        both(pre2, nine, load2, 1.0f, b0, a0, p);
        const BSeen s2b = seen(b0, p), s2 = seen(a0, p);
        const float c2 = b0.c, most2b = s2b.most, least2b = s2b.least, most2 = s2.most, least2 = s2.least;
        LockStepOut b2h, a2h;
        both(pre2, nine, load2, 0.5f, b2h, a2h, p);
        const bool case2 = !s2b.rules && c2 < -1.9f && c2 > -2.0f && most2b > 15.5f && least2b < 3.0f && s2.rules && seen(a2h, p).rules && !seen(b2h, p).rules;
        // 3. one wheel in the air and C under 0: before that wheel is written to about -1.5
        const float pre3[6] = { -2.0f, -2.0f, -2.0f, -2.0f, -2.02f, -2.0f }, load3[6] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f };
        both(pre3, nine, load3, 1.0f, b0, a0, p);
        const float air3b = b0.final[5], air3 = a0.final[5];
        const bool case3 = air3b < -1.0f && b0.under == 1 && b0.breach == 1 && air3 >= 0.0f && a0.under == 0 && a0.breach == 0 && seen(a0, p).rules;
        // 4. rolling back at 5 rad/s with the engine pulling forward: before the lightly loaded wheels are lowered, now none
        const float pre4[6] = { -5.0f, -5.0f, -5.0f, -5.0f, -5.0f, -5.02f }, two[6] = { 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f };
        both(pre4, two, load1, 1.0f, b0, a0, p);
        const BSeen s4b = seen(b0, p), s4 = seen(a0, p);
        const bool case4 = s4b.lowered && !s4.lowered && s4.rules;
        // the engine braking (the drive's impulse not over 0): no bound moves a wheel; since part 197 the loaded wheels
        // leave it at one speed, as a locked shaft slows, where the law of before slowed the two carrying the truck the most
        const float less[6] = { -1.0f, -1.0f, -0.5f, -0.5f, -0.5f, -0.5f }, pre5[6] = { 6.0f, 6.0f, 7.0f, 7.0f, 7.0f, 7.0f };
        both(pre5, less, load1, 1.0f, b0, a0, p);
        bool braking = a0.wrote && a0.bounded == 0 && a0.j < 0.0f && b0.final[0] < b0.final[2] - 0.5f;
        for (int i = 1; i < 6; i++) braking = braking && Near(a0.final[i], a0.final[0], 1e-3f);
        // every wheel in the air, one turning back: no wheel on the ground: the mean of post as it was, and with one wheel turning back fast no other wheel is taken under 0
        const float pre6[6] = { 9.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, one6[6] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, air6[6] = {};
        const float pre7[6] = { -30.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        both(pre6, one6, air6, 1.0f, b0, a0, p);
        bool inAir6 = seen(a0, p).rules && a0.bounded == 0;
        for (int i = 0; i < 6; i++) inAir6 = inAir6 && a0.final[i] == b0.final[i];
        both(pre7, one6, air6, 1.0f, b0, a0, p);
        inAir6 = inAir6 && !seen(b0, p).rules && seen(a0, p).rules && a0.bounded > 0 && a0.final[1] >= 0.0f && b0.final[1] < -3.0f;
        char bLine[700];
        snprintf(bLine, sizeof bLine,
                 "locked differential, the bounds: the drive leaves 9 rad/s on six standing wheels of which two carry the truck: the lock wrote %.1f and %.1f, now %.2f and %.2f, "
                 "none over the drive's fastest, none under 0, no spin added, and the ceiling in L cuts nothing of the lock's; wheels turning back at C %.2f: %.1f and %.1f "
                 "before, now %.2f and %.2f; a wheel in the air with C under 0: %.2f before, now %.2f; rolling back with the engine pulling no wheel is lowered; "
                 "engine braking and wheels all in the air as they were",
                 most1b, least1b, most1, least1, c2, most2b, least2b, most2, least2, air3b, air3);
        printf("     %s\n", bLine);
        if (!(case1 && inL && case2 && case3 && case4 && braking && inAir6))
            printf("     bounds: case1 %d, inL %d (%d %d %d, %.3f %.3f %.3f), case2 %d, case3 %d, case4 %d, braking %d, in air %d\n", case1, inL, cap1b.clamps, cap1.clamps, cap1d.clamps,
                   cap1b.excess, cap1.excess, cap1d.excess, case2, case3, case4, braking, inAir6);
        Check(case1 && inL && case2 && case3 && case4 && braking && inAir6, bLine);
        // the lock's hand-on as one change of speed (BRIEF_tester_1114.md, parts 188 and 197), on two steps of a tester's
        // 6x6 of 8269 kg (the probe's by-wheel lines): 13:02:45, both front wheels written to 8.21 and the rear to 4.93 to
        // 6.59 by the law of before; 17:05:13, a front wheel in the air and the spread 3.23 after the drive, 6.37 after the lock
        {
            const float kGain[6] = { 0.02486f, 0.02486f, 0.01420f, 0.01420f, 0.01420f, 0.01420f };
            const float preA[6] = { 2.44f, 1.08f, 5.04f, 5.92f, 5.93f, 4.38f }, postA[6] = { 2.69f, 25.97f, 4.42f, 5.15f, 5.16f, 4.29f };
            const float loadA[6] = { 1.0f, 1.0f, 0.56f, 1.0f, 1.0f, 0.23f }, radA[6] = { 0.564f, 0.571f, 0.567f, 0.571f, 0.567f, 0.567f };
            const float preB[6] = { 0.0f, -1.60f, -1.07f, -1.00f, -1.04f, -0.86f }, postB[6] = { 6.37f, 5.79f, 3.16f, 3.23f, 3.19f, 3.36f };
            const float loadB[6] = { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, radB[6] = { 0.564f, 0.549f, 0.539f, 0.549f, 0.529f, 0.539f };
            const LockStepOut nowA = LockStepHeld(preA, postA, kGain, loadA, radA, bOk, 6, 1.0f, 8269.0f), oldA = LockStepHeld(preA, postA, kGain, loadA, radA, bOk, 6, 1.0f, 8269.0f, false);
            const LockStepOut nowB = LockStepHeld(preB, postB, kGain, loadB, radB, bOk, 6, 1.0f, 8269.0f), oldB = LockStepHeld(preB, postB, kGain, loadB, radB, bOk, 6, 1.0f, 8269.0f, false);
            // the impulse handed on to the loaded wheels, the ones that push the ground (a wheel in the air: the bound
            // lifts it from C + a under 0 to 0, which the law of before without the bounds does not)
            auto handed = [&](const LockStepOut &o, const float *post, const float *load) {
                float s = 0.0f;
                for (int i = 0; i < 6; i++)
                    if (load[i] > kLockLoaded) s += (o.final[i] - post[i]) / kGain[i];
                return s;
            };
            float loLoadedB = 1e9f, hiLoadedB = -1e9f;
            for (int i = 1; i < 6; i++) loLoadedB = fminf(loLoadedB, nowB.final[i]), hiLoadedB = fmaxf(hiLoadedB, nowB.final[i]);
            const float hA = handed(nowA, postA, loadA), hOldA = handed(oldA, postA, loadA), hB = handed(nowB, postB, loadB), hOldB = handed(oldB, postB, loadB);
            // the law of before reproduces the log's figures (8.21 on the fronts, 6.59 and 4.93 on the rear)
            const bool logged = Near(oldA.final[0], 8.21f, 0.02f) && Near(oldA.final[1], 8.21f, 0.02f) && Near(oldA.final[3], 6.59f, 0.02f) && Near(oldA.final[5], 4.93f, 0.02f);
            const bool spreadA = nowA.spreadAfter <= nowA.spreadBefore && nowA.spreadAfter < 0.01f;
            // 17:05:13: the loaded five at one speed and the spread under the law of before's; the wheel in the air takes none of R
            const bool spreadB = hiLoadedB - loLoadedB < 0.01f && nowB.spreadAfter < oldB.spreadAfter && nowB.final[0] < loLoadedB && nowB.final[0] >= 0.0f;
            const bool pull = fabsf(hA - hOldA) <= 0.01f * fabsf(hOldA) + 0.01f && fabsf(hB - hOldB) <= 0.01f * fabsf(hOldB) + 0.01f;
            char oneLine[700];
            snprintf(oneLine, sizeof oneLine,
                     "locked differential, one change of speed: 13:02:45 the spread %.2f after the drive, %.2f after the lock (%.2f by the law of before), the six at %.2f to %.2f; "
                     "17:05:13 %.2f after the drive, %.2f after the lock (%.2f before), the loaded five at %.2f to %.2f, the wheel in the air at %.2f; "
                     "the impulse handed on to the loaded wheels %.1f and %.1f (%.1f and %.1f by the law of before)",
                     nowA.spreadBefore, nowA.spreadAfter, oldA.spreadAfter, nowA.final[5], nowA.final[0], nowB.spreadBefore, nowB.spreadAfter, oldB.spreadAfter, loLoadedB, hiLoadedB,
                     nowB.final[0], hA, hB, hOldA, hOldB);
            printf("     %s\n", oneLine);
            if (!(logged && spreadA && spreadB && pull)) printf("     one speed: logged %d spreadA %d spreadB %d pull %d\n", logged, spreadA, spreadB, pull);
            Check(logged && spreadA && spreadB && pull, oneLine);
        }
        // the new count (BRIEF_low_range_pull.md): a wheel written under 0 and under what the drive left it while the
        // drive's impulse is over 0. 0 over every case above with the bounds; the law of before breaks it. The engine
        // braking with wheels turning back is counted as under 0 (the old count) and is no breach
        {
            const int callsAbove = lockCalls, breachAbove = breachNow, beforeAbove = breachBefore;
            const float pre8[6] = { -1.0f, -1.0f, -3.0f, -3.0f, -3.0f, -3.02f };
            LockStepOut b8, a8;
            both(pre8, less, load1, 1.0f, b8, a8, p);
            char cLine[400];
            snprintf(cLine, sizeof cLine,
                     "locked differential, the count of wheels under 0 and under the drive's with the drive's impulse over 0: %d over the %d steps of the bounds' cases, %d with "
                     "the law of before; engine braking with the wheels turning back: %d under 0, %d of them counted",
                     breachAbove, callsAbove, beforeAbove, a8.under, a8.breach);
            Check(breachAbove == 0 && callsAbove == 8 && beforeAbove > 0 && a8.wrote && a8.j < 0.0f && a8.under > 0 && a8.breach == 0 && b8.breach == 0 && underNow >= 0, cLine);
        }

        // S1: the lock's step in the top's sim (TopRun), four wheels of which one in the air, the truck held standing,
        // 20 s, in a numbered gear (no cap) and in L (a cap of 1.5): the law of before and with the bounds
        TopSetup lu = {};
        lu.slot.vehicle = 1, lu.slot.mode = kCutMap, lu.slot.share = 1.0f, lu.slot.pedal = 1.0f;
        lu.wheels = 4, lu.freeWheels = 1, lu.kick = 500.0f, lu.mdav = 0.01f, lu.inertia = 10.0f, lu.flat = -1.0f, lu.holdV = 0.0f, lu.grip = 1000.0f;
        lu.seconds = 20.0f, lu.rise = 0.008f, lu.seed = 7, lu.gripNoise = 0.3f, lu.dragNoise = 0.3f, lu.couple = 1.0f / 60.0f;
        lu.lockK = 1.0f, lu.lockMass = 4000.0f, lu.lockRadius = 0.5f;
        auto lockRun = [&](float top, float cap, bool noBound, float lockK) {
            TopSetup c = lu;
            const CutBand band = CutGovernBand(top, kTachOverrun);
            c.slot.map = TorqueCutMap{ 1.0f, 1.0f, 0.0f, 1.0f, 0.15f, top, 800.0f, 2500.0f, 1.2f, { 5, { 0.25f, 0.45f, 0.6f, 0.8f, 1.0f }, { 0.55f, 0.85f, 1.0f, 1.0f, 0.85f } }, band.from, band.to };
            c.cap = cap, c.lockNoBound = noBound, c.lockK = lockK;
            return TopRun(c);
        };
        static char sLine[1500];
        int sLen = 0;
        bool simOk = true;
        for (int gearCase = 0; gearCase < 2; gearCase++)
        {
            const float top = gearCase ? 2.4f : 12.3f, cap = gearCase ? 1.5f : 0.0f;
            const TopResult none = lockRun(top, cap, false, 0.0f), was = lockRun(top, cap, true, 1.0f), is = lockRun(top, cap, false, 1.0f);
            const float pullNone = none.pullEach[1] + none.pullEach[2] + none.pullEach[3], pullWas = was.pullEach[1] + was.pullEach[2] + was.pullEach[3],
                        pullIs = is.pullEach[1] + is.pullEach[2] + is.pullEach[3];
            sLen += snprintf(sLine + sLen, sizeof sLine - sLen,
                             "%s%s: the gripping wheels give the ground %.0f rad/s without the lock, %.0f with the lock of before, %.0f with the bounds; the free wheel at most %.1f, "
                             "%.1f, %.1f rad/s; the lock over the drive's fastest by %.2f before and %.4f now, spin added %.2f and %.4f",
                             gearCase ? "; " : "", gearCase ? "in L" : "in a numbered gear", pullNone, pullWas, pullIs, none.freeMost, was.freeMost, is.freeMost, was.lockOver, is.lockOver,
                             was.lockAdded, is.lockAdded);
            // the cost: in a numbered gear the gripping wheels keep at least 0.95 of what the lock of before gave them and the free
            // wheel stays under a tenth of the unlocked one's speed; in L the cap holds the free wheel either way
            simOk = simOk && is.lockOver <= 1e-3f && is.lockAdded <= 1e-2f && is.lockUnder == 0 && is.lockBreach == 0 && is.freeMost <= was.freeMost + 0.01f && pullIs >= 0.95f * pullWas &&
                    pullIs >= pullNone && (gearCase || (is.freeMost < 0.1f * none.freeMost && was.lockOver > 10.0f && pullIs > 100.0f * pullNone));
        }
        printf("     %s\n", sLine);
        Check(simOk, "locked differential, the bounds in the sim: nothing over the drive's fastest, no spin added, no wheel under 0, the free wheel no faster than with the lock of before and the gripping wheels given at least 0.95 of it");
        // M3 of BRIEF_reverse_pull.md: the lock in R with the wider cap, four wheels of which one in the air, the truck held
        // standing. The speeds are in the gear's direction (CapSpeed by the gear's sign), so the drive's impulse in R is over
        // 0 as in a forward gear and the three bounds hold there too
        {
            const float topR = 3.1f, wideR = LowRangeCap(kPosR, 1.5f * topR, 1.5f * 8.06f, 100);
            auto lockR = [&](bool noBound, float lockK, float wide) {
                TopSetup c = lu;
                const CutBand band = CutGovernBand(topR, kTachOverrun);
                c.slot.map = TorqueCutMap{ 1.0f, 1.0f, 0.0f, 1.0f, 0.15f, topR, 800.0f, 2500.0f, 1.2f, { 5, { 0.25f, 0.45f, 0.6f, 0.8f, 1.0f }, { 0.55f, 0.85f, 1.0f, 1.0f, 0.85f } }, band.from, band.to };
                c.cap = 1.5f, c.capWide = wide, c.wideFrom = kLowWideFrom, c.lockNoBound = noBound, c.lockK = lockK;
                return TopRun(c);
            };
            const TopResult none = lockR(false, 0.0f, wideR), was = lockR(true, 1.0f, wideR), is = lockR(false, 1.0f, wideR), narrow = lockR(false, 1.0f, 0.0f);
            const float pullNone = none.pullEach[1] + none.pullEach[2] + none.pullEach[3], pullIs = is.pullEach[1] + is.pullEach[2] + is.pullEach[3],
                        pullNarrow = narrow.pullEach[1] + narrow.pullEach[2] + narrow.pullEach[3];
            static char rLine[900];
            snprintf(rLine, sizeof rLine, "locked differential in R with the wider cap (%.2f rad/s, the gear's own %.2f), one wheel of four free, standing: the gripping wheels give the ground %.0f rad/s without the lock, "
                                          "%.0f with it, %.0f with it at LowRangeSlip 0; the free wheel at most %.2f rad/s (%.2f without the bounds); the lock over the drive's fastest by %.4f (%.2f without the bounds), "
                                          "spin added %.4f (%.2f), wheels under 0 %d, breaches %d",
                     wideR, 1.5f * topR, pullNone, pullIs, pullNarrow, is.freeMost, was.freeMost, is.lockOver, was.lockOver, is.lockAdded, was.lockAdded, is.lockUnder, is.lockBreach);
            Check(is.lockOver <= 1e-3f && is.lockAdded <= 1e-2f && is.lockUnder == 0 && is.lockBreach == 0 && is.freeMost <= wideR + 1e-3f && pullIs >= pullNone && pullIs >= pullNarrow, rLine);
        }
    }

    // the chassis push (BRIEF_chassis_push.md with the Reviewer's addendum and the lead's): k = 1 - max(1 / F_excess, 1 / F)
    // where F_excess = min(B4, throttle) over the pull the factor assumed is above 1 and F (the factor over the game's own
    // share) above 1; the push of a factor stepping from 1 to 3 at a GameFall of 0.4 with B4 still 0.9 held to the mod's
    // pull (0.4 x 3 = 1.2, not 2.7); steady, nothing; the game's own push the floor; the raw factor of a damaged engine
    // not read as F; PushShare's back scales it; the cap's part by (1 - kept)
    {
        auto nearly = [](float a, float b) { return fabsf(a - b) <= 1e-4f * fmaxf(1.0f, fabsf(b)); };
        const float shove = PushTakeBack(0.9f, 1.0f, 0.4f, 3.0f, 1.0f);
        const float pushAfter = 0.9f * 3.0f * (1.0f - shove);
        const float pSteady = PushTakeBack(0.4f, 1.0f, 0.4f, 3.0f, 1.0f);
        const float floorBinds = PushTakeBack(0.9f, 1.0f, 0.25f, 3.0f, 1.0f); // F_excess 3.6 above F 3: down to the game's own push 0.9
        const float leadsK = PushTakeBack(0.9f, 1.0f, 0.25f, 6.0f, 1.0f);    // F 6: 1 - 1 / 3.6
        const float pDamaged = PushTakeBack(0.9f, 1.0f, 0.25f, 0.75f / 0.5f, 1.0f); // own share 0.5 written 0.75: F 1.5
        const float atOwn = PushTakeBack(0.9f, 1.0f, 0.25f, 1.0f, 1.0f);
        const float half = PushTakeBack(0.9f, 1.0f, 0.4f, 3.0f, 0.5f);
        const float none = PushTakeBack(0.9f, 1.0f, 0.4f, 3.0f, 0.0f);
        const float throttleLow = PushTakeBack(0.9f, 0.4f, 0.4f, 3.0f, 1.0f); // min(B4, throttle) 0.4: pSteady
        PushBody fixedBody = {};
        fixedBody.v1 = { 1.0f, 0.0f, 0.0f };
        CapVec pv, pw;
        const bool skipped = !PushBodyBack(fixedBody, 0.5f, pv, pw);
        PushBody body = {};
        body.v0 = { 10.0f, 0.0f, 0.0f };
        body.v1 = { 11.0f, 0.0f, 0.2f };
        body.w0 = { 0.0f, 0.0f, 0.0f };
        body.w1 = { 0.0f, 0.5f, 0.0f };
        body.invMass = 0.001f;
        const bool moved = PushBodyBack(body, 0.25f, pv, pw) && nearly(pv.x, 10.75f) && nearly(pv.z, 0.15f) && nearly(pw.y, 0.375f);
        const bool gate = PushMayTake(10.0f, 3, 3, 0.0f) && PushMayTake(-10.0f, -1, -1, 0.0f) && !PushMayTake(3.0f, 3, 3, 0.0f) && !PushMayTake(2.0f, 3, 3, 0.0f) &&
                          !PushMayTake(NAN, 3, 3, 0.0f) && !PushMayTake(10.0f, 1, 3, 0.0f) && !PushMayTake(10.0f, 3, 3, 0.5f) && !PushMayTake(10.0f, 3, 3, NAN);
        const bool ok = gate && nearly(shove, 1.0f - 1.0f / 2.25f) && nearly(pushAfter, 1.2f) && pSteady == 0.0f && nearly(floorBinds, 1.0f - 1.0f / 3.0f) &&
                        nearly(0.9f * 3.0f * (1.0f - floorBinds), 0.9f) && nearly(leadsK, 1.0f - 1.0f / 3.6f) && nearly(pDamaged, 1.0f - 1.0f / 1.5f) && atOwn == 0.0f &&
                        nearly(half, 0.5f * shove) && none == 0.0f && throttleLow == 0.0f && PushTakeBack(NAN, 1.0f, 0.4f, 3.0f, 1.0f) == 0.0f &&
                        nearly(PushCapBack(true, 0.25f, 1.0f), 0.75f) && PushCapBack(false, 0.25f, 1.0f) == 0.0f && nearly(PushCapBack(true, 0.25f, 0.5f), 0.375f) && skipped && moved;
        char line[600];
        snprintf(line, sizeof line, "chassis push: the factor stepping to 3 at a GameFall of 0.4 with B4 0.9: k %.3f, the push %.2f (the mod's pull 1.20, not 2.70); steady k %.1f; F_excess 3.6 "
                                    "over F 3: k %.3f, down to the game's own push 0.90; F 6: k %.3f (1 - 1 / 3.6); a damaged engine's 0.75 over 0.5: k %.3f; at the game's own 0; "
                                    "PushShare 50 halves it; the cap's part 0.75 at kept 0.25; a fixed body skipped; v and the turn moved back by k; nothing at 3 m/s or under, on the clunk or the stutter",
                 shove, pushAfter, pSteady, floorBinds, leadsK, pDamaged);
        Check(ok, line);
    }

    // the brief's sketch of the push (60 Hz): the Researcher's share s = clamp((wheels - 4) / 6, 0.01, 1) x clamp((|v| - 3)
    // / 6, 0, 1) of the drive's pull min(B4, throttle) x the factor goes to the chassis; the factor steps from 1 to 3 at 10
    // m/s while B4 falls from 0.9 to the factor's assumption of 0.4 by 0.1 a step. Today the chassis gets 2.25 times the
    // mod's steady pull at once; with the take-back never more than the mod's pull, and the steady end the same. At 2 m/s s
    // is 0: nothing to take back either way
    {
        auto sketch = [](float v0, float pushShare, float &peak, float &end) {
            float v = v0, b4 = 0.9f;
            const float assumed = 0.4f, factor = 3.0f, radius = 0.5f, dt = 1.0f / 60.0f;
            peak = 0.0f, end = 0.0f;
            for (int i = 0; i < 60; i++)
            {
                const float wheels = v / radius;
                const float s = fminf(fmaxf((wheels - 4.0f) / 6.0f, 0.01f), 1.0f) * fminf(fmaxf((fabsf(v) - 3.0f) / 6.0f, 0.0f), 1.0f);
                const float push = s * fminf(b4, 1.0f) * factor;
                const float k = PushTakeBack(b4, 1.0f, assumed, factor, 1.0f - pushShare / 100.0f);
                const float a = push * (1.0f - k);
                peak = fmaxf(peak, a), end = a;
                v += a * dt;
                b4 = fmaxf(b4 - 0.1f, assumed);
            }
        };
        float todayPeak, todayEnd, backPeak, backEnd, slowToday, slowEnd, slowBack, slowBackEnd;
        sketch(10.0f, 100.0f, todayPeak, todayEnd);
        sketch(10.0f, 0.0f, backPeak, backEnd);
        sketch(2.0f, 100.0f, slowToday, slowEnd);
        sketch(2.0f, 0.0f, slowBack, slowBackEnd);
        const bool sk = todayPeak > 2.0f * todayEnd && backPeak <= backEnd * 1.0001f + 1e-6f && Near(todayEnd, backEnd, 1e-4f) && slowToday == 0.0f && slowBack == 0.0f;
        char line[400];
        snprintf(line, sizeof line, "the push sketch: the factor stepping 1 to 3 at 10 m/s, B4 falling 0.9 to 0.4: the chassis's push peaks at %.2f against a steady %.2f today (PushShare 100), "
                                    "%.2f against %.2f with the take-back; at 2 m/s %.2f and %.2f",
                 todayPeak, todayEnd, backPeak, backEnd, slowToday, slowBack);
        Check(sk, line);
    }

    // F3 and F4 of BRIEF_drive_2108_findings.md: the engine brake only while every driven wheel turns in the gear's
    // direction over the gear's idle speed, fading from twice it; the needle by the driven wheels' signed mean in the
    // gear's direction. F2: the game's gear agrees in sign with the mod's position, else the stock passes.
    {
        TorqueCutSlot bs = {};
        bs.vehicle = 1;
        bs.mode = kCutMap;
        bs.share = 1.0f;
        bs.pedal = 0.0f;
        bs.map = TorqueCutMap{ 0.0f, 1.0f, 0.0f, 1.0f, 0.15f, 10.0f, 800.0f, 2000.0f, 1.2f, { 2, { 0.1f, 1.5f }, { 1.0f, 1.0f } }, CutGovernBand(10.0f, 1.1f).from,
                               CutGovernBand(10.0f, 1.1f).to };
        const bool d2[2] = { true, true }, d4[4] = { true, true, true, true };
        const float apart[2] = { -75.0f, 81.0f }, back[4] = { -12.0f, -12.0f, -12.0f, -12.0f }, oneForward[4] = { -12.0f, -12.0f, -12.0f, 2.0f },
                    slow[4] = { -6.0f, -6.0f, -6.0f, -6.0f };
        const float fApart = TorqueCutAnswerOf(bs, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, TorqueCutSpeeds(apart, d2, 2, -1)).f;
        const float fBack = TorqueCutAnswerOf(bs, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, TorqueCutSpeeds(back, d4, 4, -1)).f;
        const float fOne = TorqueCutAnswerOf(bs, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, TorqueCutSpeeds(oneForward, d4, 4, -1)).f;
        const float fSlow = TorqueCutAnswerOf(bs, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, TorqueCutSpeeds(slow, d4, 4, -1)).f;
        const bool brake = fApart >= 0.0f && Near(fBack, -0.15f * (2400.0f - 800.0f) / 1200.0f, 1e-5f) && // 1.2 of the top: over the redline the braking still rises (PLANNED 67)
                            fOne >= 0.0f && Near(fSlow, -0.15f * (1200.0f - 800.0f) / 1200.0f * 0.5f, 1e-5f) &&
                           TorqueCutBrakeTaper(8.0f, bs.map) == 1.0f && TorqueCutBrakeTaper(4.0f, bs.map) == 0.0f && Near(TorqueCutBrakeTaper(6.0f, bs.map), 0.5f, 1e-6f);
        TruckRaw tw = Truck();
        tw.wheels = 2;
        tw.drive[0] = tw.drive[1] = 1;
        tw.spin[0] = -75.0f;
        tw.spin[1] = 81.0f;
        tw.gearEngaged = 1;
        const float forward = TruckDrivenSpinGeared(tw);
        tw.gearEngaged = -1;
        const float reverse = TruckDrivenSpinGeared(tw);
        tw.spin[0] = tw.spin[1] = -9.0f;
        const float backing = TruckDrivenSpinGeared(tw);
        const bool needle = Near(forward, 3.0f, 1e-5f) && reverse == 0.0f && Near(backing, 9.0f, 1e-5f);
        GearboxPosModel pm = {};
        pm.pos = kPosR;
        const bool rAgrees = GearboxPosAgrees(pm, -1) && !GearboxPosAgrees(pm, 2);
        pm.pos = kPosLowMinus;
        const bool lowAgrees = GearboxPosAgrees(pm, 1) && !GearboxPosAgrees(pm, -1) && !GearboxPosAgrees(pm, 0);
        pm.pos = kPosN;
        const bool nNever = !GearboxPosAgrees(pm, 0) && !GearboxPosAgrees(pm, 1);
        char brakeLine[500];
        snprintf(brakeLine, sizeof brakeLine, "torque cut's brake and needle: foot off in R with the driven pair at -75 and +81 F %.3f, all four at -12 F %.3f, one of them at "
                                              "+2 F %.3f, all at -6 (1.5 x the idle speed) F %.3f; the needle's spin forward %.1f, in R %.1f, backing at -9 %.1f; R agrees only "
                                              "with a gear below 0, L- above 0, N never",
                 fApart, fBack, fOne, fSlow, forward, reverse, backing);
        Check(brake && needle && rAgrees && lowAgrees && nNever, brakeLine);
    }

    // L- is jerky (BRIEF_low_minus_jerky.md): by spin, the mean of the last passes and the cap's fade over the wider of
    // the top's last 0.8 rad/s and its last fifth (BRIEF_low_gear_sway.md). A trace with +-0.15 rad/s of noise around
    // 0.95 (and 1.0) of L-'s 2.24 rad/s top keeps the factor's change from one pass to the next under 0.15 (0.18.4's jumps
    // by up to 1); a 15 and a 10 rad/s top fade over their last fifth (2 at 0.8, 1.5 at 0.9, 1 at the top); a catch from 2
    // to 0.5 of the top in one pass stays under kSpinLiftMost; a new gear or truck starts the mean over
    {
        const float curve = 0.9f;
        auto today = [&](float x) { return fminf(TorqueLift(curve, 1.0f, 1.0f, 1.0f, x, TorqueRoof(kClassTruck)), kSpinLiftMost); };
        auto calm = [&](float x, float top) { return fminf(TorqueLift(curve, 1.0f, 1.0f, 1.0f, x, TorqueRoof(kClassTruck)), SpinLiftMost(x, top)); };
        auto noisy = [&](float top, float centre, float &worstToday) {
            uint32_t seed = 12345u;
            SpinMean m = {};
            float worst = 0.0f, last = -1.0f, lastToday = -1.0f;
            worstToday = 0.0f;
            for (int i = 0; i < 2000; i++)
            {
                seed = seed * 1664525u + 1013904223u;
                const float spin = centre * top + ((float)(seed >> 8) / 16777216.0f * 2.0f - 1.0f) * 0.15f;
                const float f = calm(SpinShare(SpinMeanStep(m, spin, 1, 0x5000), top), top), ft = today(SpinShare(spin, top));
                if (last >= 0.0f) worst = fmaxf(worst, fabsf(f - last)), worstToday = fmaxf(worstToday, fabsf(ft - lastToday));
                last = f, lastToday = ft;
            }
            return worst;
        };
        float t95 = 0.0f, t100 = 0.0f;
        const float j95 = noisy(2.24f, 0.95f, t95), j100 = noisy(2.24f, 1.0f, t100);
        // noise free: a top of 15 and of 10 rad/s fade over their last fifth, a straight line; under 0.8 they are 0.18.4's
        bool same15 = true, near10 = true;
        for (int i = 0; i <= 600; i++)
        {
            const float x = i * 0.002f;
            const float line15 = fminf(TorqueLift(curve, 1.0f, 1.0f, 1.0f, x, TorqueRoof(kClassTruck)), fminf(2.0f, x >= 1.0f ? 1.0f : 1.0f + (1.0f - x) / 0.2f));
            same15 = same15 && Near(calm(x, 15.0f), line15, 0.0001f) && Near(calm(x, 10.0f), line15, 0.0001f) && (x > 0.8f || Near(calm(x, 15.0f), today(x), 0.0001f));
        }
        near10 = Near(SpinLiftMost(0.8f, 10.0f), 2.0f, 0.0001f) && Near(SpinLiftMost(0.9f, 10.0f), 1.5f, 0.0001f) && SpinLiftMost(1.0f, 10.0f) == 1.0f &&
                 Near(SpinLiftMost(0.9f, 2.24f), 1.0f + 0.1f * 2.24f / 0.8f, 0.0001f);
        // a catch: 2 of the top for five passes, then 0.5 of it each pass after: what is written, and what the game gives
        // with it at the next pass's spin, stays under kSpinLiftMost
        SpinMean m = {};
        float caughtWorst = 0.0f, written = 1.0f;
        for (int i = 0; i < 20; i++)
        {
            const float raw = i < 5 ? 2.0f : 0.5f;
            if (i > 0) caughtWorst = fmaxf(caughtWorst, written * GameFall(raw));
            written = calm(SpinShare(SpinMeanStep(m, raw * 2.24f, 1, 0x5000), 2.24f), 2.24f);
            caughtWorst = fmaxf(caughtWorst, written);
        }
        // a new gear, or another truck, starts the mean over
        SpinMean r = {};
        SpinMeanStep(r, 0.2f, 1, 0x5000), SpinMeanStep(r, 0.2f, 1, 0x5000);
        const bool gearNew = Near(SpinMeanStep(r, 0.9f, 2, 0x5000), 0.9f, 0.0001f) && Near(SpinMeanStep(r, 0.5f, 2, 0x6000), 0.5f, 0.0001f) &&
                             Near(SpinMeanStep(r, 0.7f, 2, 0x6000), 0.6f, 0.0001f);
        // a change of range in the same gear (L- to 1st, the top 2.24 to 7.2 rad/s) with the wheels' speed steady: the
        // next pass's factor is the one at that spin alone, as the mean is in rad/s (review M1)
        SpinMean rg = {};
        for (int i = 0; i < 5; i++) SpinMeanStep(rg, 0.95f * 2.24f, 1, 0x5000);
        const float afterRange = calm(SpinShare(SpinMeanStep(rg, 0.95f * 2.24f, 1, 0x5000), 7.2f), 7.2f);
        const bool rangeSteady = Near(afterRange, calm(SpinShare(0.95f * 2.24f, 7.2f), 7.2f), 0.05f);
        char line[300];
        snprintf(line, sizeof line, "lift by spin in L-: +-0.15 rad/s of noise at 0.95 and 1.0 of a 2.24 rad/s top moves the factor at most %.2f and %.2f a pass (today's %.2f and %.2f); "
                                    "a 15 and a 10 rad/s top fade over their last fifth; a catch gives at most %.2f; a change of range keeps the factor at its spin (%.2f); a new gear or truck starts the mean over",
                 j95, j100, t95, t100, caughtWorst, afterRange);
        Check(j95 < 0.15f && j100 < 0.15f && t95 > 0.5f && same15 && near10 && caughtWorst <= kSpinLiftMost + 0.001f && gearNew && rangeSteady, line);

        // SpinLiftStep as gauges.cpp has it, always (SpinCalm is read no more, BRIEF_low_gear_sway.md): the same noisy L-
        // trace gives the calm's figures above, with no break loose on it; a noisy trace (+-0.15 rad/s) around 0.95 of a
        // 1.8 rad/s top keeps the factor's change from one pass to the next under 0.1 without more than the mean
        auto stepped = [&](float top, float centre, float &worstStep, int &looses) {
            uint32_t seed = 12345u;
            SpinMean m = {};
            float last = -1.0f;
            worstStep = 0.0f;
            looses = 0;
            for (int i = 0; i < 2000; i++)
            {
                seed = seed * 1664525u + 1013904223u;
                const float spin = centre * top + ((float)(seed >> 8) / 16777216.0f * 2.0f - 1.0f) * 0.15f;
                const SpinLift l = SpinLiftStep(m, spin, top, 1, 0x5000);
                const float f = fminf(TorqueLift(curve, 1.0f, 1.0f, 1.0f, l.x, TorqueRoof(kClassTruck)), l.cap);
                if (last >= 0.0f) worstStep = fmaxf(worstStep, fabsf(f - last));
                last = f;
                looses += l.loose;
            }
        };
        float on95 = 0.0f, low95 = 0.0f;
        int loose95 = 0, looseLow = 0;
        stepped(2.24f, 0.95f, on95, loose95);
        stepped(1.8f, 0.95f, low95, looseLow);
        snprintf(line, sizeof line, "lift by spin, always the calm: the noisy L- trace is the calm's (%.2f a pass), no break loose on it; around 0.95 of a 1.8 rad/s top at most %.2f a pass",
                 on95, low95);
        Check(Near(on95, j95, 0.0001f) && !loose95 && !looseLow && low95 < 0.1f, line);
    }

    // D slowing under the brake (the sequential to D brief): from 6th at 60 km/h braked to 20 km/h in 3 s it comes down
    // to the speed's gear (the one a long coast at 20 km/h settles in), never more than 0.5 s behind each speed's gear and
    // with no more shifts than gears; slowing gently without
    // the brake it still goes a gear at a time; with the grip unknown and the wheels half locked under the brake no pass
    // drops more than one gear; and the pedal after the brake's skips finds no hunt kept
    {
        const float dt = 0.05f;
        TruckRaw t = Truck();
        t.gearSpeeds = 8;
        static const float six[8] = { 3.0f, 6.0f, 10.0f, 15.0f, 22.0f, 30.0f, 42.0f, 20.0f };
        memcpy(t.gearSpeed, six, sizeof six);
        t.gear = t.gearEngaged = 6;
        auto at = [&](float v, float pedal, float brake, float lock = 1.0f) {
            for (int i = 0; i < t.wheels; i++) t.spin[i] = v / 0.45f * lock;
            t.speed = v;
            t.throttle = brake > 0.0f ? brake : -pedal;
        };
        const GaugeScale &k = kClassScales[kClassTruck];
        GearboxModel m = {};
        at(16.7f, 0.3f, 0.0f);
        for (int i = 0; i < 80; i++) GearboxStep(m, t, k, 0.3f, 0.3f, dt);
        m.gear = 6;
        m.pause = m.wanted = 0.0f;
        const GearboxModel learned = m;
        GearboxModel ref = learned;
        at(5.56f, 0.0f, 0.0f);
        for (int i = 0; i < 400; i++) GearboxStep(ref, t, k, 0.0f, 0.0f, dt);
        // the gear a long coast settles in at speed v: the speed's gear
        auto settled = [&](float v) {
            GearboxModel c = learned;
            at(v, 0.0f, 0.0f);
            for (int i = 0; i < 200; i++) GearboxStep(c, t, k, 0.0f, 0.0f, dt);
            return c.gear;
        };
        const int shiftsWere = m.shifts;
        int worstDrop = 0;
        float behind = 0.0f, worstBehind = 0.0f;
        for (int i = 0; i <= 70; i++)
        {
            const float v = i < 60 ? 16.7f - (16.7f - 5.56f) * i / 60.0f : 5.56f;
            const int speeds = settled(v);
            at(v, 0.0f, 0.5f);
            const int was = m.gear;
            GearboxStep(m, t, k, 0.0f, 0.0f, dt);
            worstDrop = was - m.gear > worstDrop ? was - m.gear : worstDrop;
            behind = m.gear > speeds ? behind + dt : 0.0f;
            worstBehind = fmaxf(worstBehind, behind);
        }
        const int brakeShifts = m.shifts - shiftsWere;
        const bool braked = learned.grip > 0.0f && m.gear == ref.gear && brakeShifts >= 1 && brakeShifts <= 6 - ref.gear && worstBehind <= 0.5f;
        const int huntsWere = m.hunts;
        at(5.56f, 0.5f, 0.0f);
        for (int i = 0; i < 40; i++) GearboxStep(m, t, k, 0.5f, 0.5f, dt);
        const bool noHunt = m.hunts == huntsWere && m.keepFor <= 0.0f;
        GearboxModel gentle = learned;
        int gentleDrop = 0;
        for (int i = 0; i < 400; i++)
        {
            at(16.7f - 0.3f * i * dt, 0.0f, 0.0f);
            const int was = gentle.gear;
            GearboxStep(gentle, t, k, 0.0f, 0.0f, dt);
            gentleDrop = was - gentle.gear > gentleDrop ? was - gentle.gear : gentleDrop;
        }
        GearboxModel blind = {};
        at(16.7f, 0.0f, 0.5f, 0.5f);
        GearboxStep(blind, t, k, 0.0f, 0.0f, dt);
        blind.gear = 6;
        int blindDrop = 0;
        for (int i = 0; i < 60; i++)
        {
            at(16.7f - (16.7f - 5.56f) * i / 60.0f, 0.0f, 0.5f, 0.5f);
            const int was = blind.gear;
            GearboxStep(blind, t, k, 0.0f, 0.0f, dt);
            blindDrop = was - blind.gear > blindDrop ? was - blind.gear : blindDrop;
        }
        char line[220];
        snprintf(line, sizeof line, "automatic: braked from 6th at 60 to 20 km/h in 3 s it lands in %d (a coast settles in %d) in %d shifts, never %.2f s behind the speed's gear; gently a gear at a time (%d); "
                                    "the grip unknown, at most %d a pass; no hunt after", m.gear, ref.gear, brakeShifts, worstBehind, gentleDrop, blindDrop);
        Check(braked && noHunt && gentleDrop <= 1 && blind.grip == 0.0f && blindDrop <= 1, line);
    }

    // road load (the game pull and road load brief, part 2): the air's drag only, nothing standing and none rolling;
    // four times at twice the speed; the F350 at 3515 kg and 100 km/h about 0.15 m/s2 (0.51 kN); CdA by the mass read;
    // nothing with a gear's pull not learned or the foot off; taken off before the fall's make-up, so near a gear's top
    // the lift stays bounded (under the roof, at least the least) and a load past the pull leaves the least; a gear's
    // pull learned from a synthetic run within 5 %
    {
        const float cda = RoadCdA(3515.0f, 0.0f);
        const float at100 = RoadLoadAccel(cda, 27.8f, 3515.0f), at50 = RoadLoadAccel(cda, 13.9f, 3515.0f);
        const bool air = RoadLoadAccel(cda, 0.0f, 3515.0f) == 0.0f && RoadLoadAccel(cda, 0.5f, 3515.0f) < 0.001f && Near(at100 / at50, 4.0f, 0.01f) &&
                         Near(at100 * 3515.0f, 510.0f, 15.0f) && RoadLoadAccel(cda, 20.0f, 0.0f) == 0.0f;
        const bool areas = cda == 1.1f && RoadCdA(8000.0f, 0.0f) == 3.5f && RoadCdA(15000.0f, 0.0f) == 6.0f && RoadCdA(3515.0f, 2.0f) == 2.0f;
        const bool none = RoadLoadShare(0.2f, 1.0f, 0.0f) == 0.0f && RoadLoadShare(0.2f, 0.0f, 1.0f) == 0.0f && Near(RoadLoadShare(0.2f, 0.5f, 1.0f), 0.4f, 0.001f);
        const float roof = TorqueRoof(kClassTruck);
        const float plain = TorqueLift(1.0f, 1.0f, 1.0f, 1.0f, 0.95f, roof), loaded = TorqueLift(1.0f, 1.0f, 1.0f, 1.0f, 0.95f, roof, 0.3f);
        const float past = TorqueLift(1.0f, 1.0f, 1.0f, 1.0f, 0.5f, roof, 1.5f), mid = TorqueLift(1.0f, 1.0f, 1.0f, 1.0f, 0.5f, roof, 0.2f);
        const bool bounded = loaded <= plain && loaded <= LiftRoof(roof, 0.95f) && loaded >= kTorqueFactorLeast && past == kTorqueFactorLeast &&
                             Near(mid, 0.8f / GameFall(0.5f), 0.001f);
        BaseLearn l = {};
        float learned = 0.0f, got = 0.0f;
        // the game's acceleration is a_base × the factor in the game × pedal × its pull: with the road load on, the factor
        // is lower by the load's share and the truck slower by as much, and the same a_base is learned (review F1)
        const float base = 1.2f, factor = 1.3f, pedal = 1.0f, x = 0.5f, onFactor = factor - RoadLoadShare(0.1f, pedal, base);
        float gotOn = 0.0f;
        BaseLearn on = {};
        for (int i = 0; i < 60; i++)
        {
            if (BaseLearnStep(l, 3, true, base * factor * pedal * GamePull(x), 0.0f, factor, pedal, x, 0.0f, 9.0f, 0.05f, learned)) got = learned;
            if (BaseLearnStep(on, 3, true, base * onFactor * pedal * GamePull(x), 0.0f, onFactor, pedal, x, 0.0f, 9.0f, 0.05f, learned)) gotOn = learned;
        }
        BaseLearn off = {};
        bool offLearned = false;
        for (int i = 0; i < 60; i++) offLearned = offLearned || BaseLearnStep(off, 3, true, 1.0f, 0.0f, factor, 0.5f, x, 0.0f, 9.0f, 0.05f, learned);
        // the review of 162 part A, S3: the base learner's long rest. 10 000 passes in A under the cut with nothing to feed
        // (not ok, as a step of the open converter or one that may not feed) learn nothing and write nothing; the visit
        // that was open when the rest began is dropped; and what is learned after the rest is what a fresh learner learns
        {
            BaseLearn rested = {}, never = {};
            float restLearned = -1.0f, gotRested = 0.0f, gotFresh = 0.0f;
            for (int i = 0; i < 3; i++) BaseLearnStep(rested, 3, true, 2.0f * 0.8f, 0.0f, 99.0f, 1.0f, 0.5f, 0.0f, 9.0f, 0.05f, restLearned, nullptr, nullptr, 0.8f);
            int restWrites = 0;
            for (int i = 0; i < 10000; i++) restWrites += BaseLearnStep(rested, 3, false, NAN, 0.0f, 99.0f, 1.0f, 0.5f, 0.0f, 9.0f, 0.05f, restLearned, nullptr, nullptr, NAN) != 0;
            const bool restClean = !restWrites && restLearned == -1.0f && rested.visitSum == 0.0f && rested.visitTime == 0.0f && rested.visitPull == 0.0f;
            int doneRested = 0, doneFresh = 0;
            for (int i = 0; i < 4000 && !(doneRested && doneFresh); i++)
            {
                if (!doneRested && BaseLearnStep(rested, 3, true, 1.4f * 0.8f, 0.0f, 99.0f, 1.0f, 0.5f, 0.0f, 9.0f, 0.05f, gotRested, nullptr, nullptr, 0.8f)) doneRested = i + 1;
                if (!doneFresh && BaseLearnStep(never, 3, true, 1.4f * 0.8f, 0.0f, 99.0f, 1.0f, 0.5f, 0.0f, 9.0f, 0.05f, gotFresh, nullptr, nullptr, 0.8f)) doneFresh = i + 1;
            }
            Check(restClean && doneRested && doneRested == doneFresh && gotRested == gotFresh && Near(gotRested, 1.4f, 0.01f),
                  "base learning, a long rest under the cut: 10 000 passes with nothing to feed learn and write nothing and drop the open visit; the first row after it is the one a "
                  "fresh learner learns, in the same number of passes, with nothing of the passes before the rest in it");
        }
        const bool learns = Near(got, base, base * 0.05f) && Near(gotOn, got, 0.001f) && !offLearned;
        // under the cut (the lead's addendum of 14:23 to BRIEF_drivetrain_3_a_on_our_chain.md): the acceleration is the
        // base times the bracket the cut used, and the same base is learned; a row begun without the cut and ended with it
        // keeps what it has and lands on the same base; a bracket of 0.2 or less feeds nothing
        {
            BaseLearn byCut = {}, mixed = {}, lowBracket = {};
            float gotCut = 0.0f, gotMixed = 0.0f;
            bool lowLearned = false;
            for (int i = 0; i < 60; i++)
            {
                const float bracket = 0.6f + 0.004f * i; // it moves with the wheels' speed
                if (BaseLearnStep(byCut, 3, true, base * bracket, 0.0f, 99.0f, pedal, x, 0.0f, 9.0f, 0.05f, learned, nullptr, nullptr, bracket)) gotCut = learned;
                const bool cutNow = i >= 20;
                if (BaseLearnStep(mixed, 3, true, cutNow ? base * bracket : base * factor * pedal * GamePull(x), 0.0f, factor, pedal, x, 0.0f, 9.0f, 0.05f, learned, nullptr, nullptr,
                                  cutNow ? bracket : NAN))
                    gotMixed = learned;
                lowLearned = lowLearned || BaseLearnStep(lowBracket, 3, true, base * 0.15f, 0.0f, factor, pedal, x, 0.0f, 9.0f, 0.05f, learned, nullptr, nullptr, 0.15f);
            }
            Check(Near(gotCut, base, base * 0.001f) && Near(gotMixed, base, base * 0.05f) && Near(gotMixed, got, 0.01f) && !lowLearned,
                  "road load under the torque cut: a gear's pull learned by the bracket the cut used is the base learned without the cut, a row fed both ways lands on it too, "
                  "and a bracket of 0.2 or less feeds nothing");
        }
        char line[220];
        snprintf(line, sizeof line, "road load: the air only, %.3f m/s2 at 100 km/h for 3515 kg; near the top %.2f under %.2f; a gear's pull learned %.3f of %.2f, %.3f with the load on",
                 at100, loaded, plain, got, base, gotOn);
        Check(air && areas && none && bounded && learns, line);
    }
    // the wheels' drag added back (BRIEF_air_drag_wheel_damping.md with the Reviewer's addendum): a_damp 0 learns
    // today's figure; a_damp 0.2 at a = 0.8 learns 1.0 / (f p) per pull; NAN is no pass; the sum over four wheels with a
    // known I, c, ω and r over the mass with the wheels' own; a wheel off the ground left out with the pass still on; a
    // failed read and fewer wheels than models no pass; a_damp's mean given with the learned gear
    {
        const float factor = 1.25f, pedal = 1.0f, x = 0.5f, dt = 0.05f;
        auto learnWith = [&](float accel, float damp, float *mean = nullptr) {
            BaseLearn l = {};
            float learned = 0.0f, got = 0.0f, m = 0.0f;
            for (int i = 0; i < 60; i++)
                if (BaseLearnStep(l, 3, true, accel * GamePull(x), damp * GamePull(x), factor, pedal, x, 0.0f, 9.0f, dt, learned, nullptr, &m)) got = learned, mean ? *mean = m : 0.0f;
            return got;
        };
        float mean = 0.0f;
        const bool zero = Near(learnWith(0.8f, 0.0f), 0.8f / (factor * pedal), 1e-4f);
        const bool added = Near(learnWith(0.8f, 0.2f, &mean), 1.0f / (factor * pedal), 1e-4f) && Near(mean, 0.2f * GamePull(x), 1e-4f);
        BaseLearn nan = {};
        float learned = 0.0f;
        bool nanLearned = false;
        for (int i = 0; i < 80; i++) nanLearned |= BaseLearnStep(nan, 3, true, 0.8f, NAN, factor, pedal, x, 0.0f, 9.0f, dt, learned) != 0;
        // four wheels: a cylinder's inertia 40 about its axle (body x), 25 across; the axle along the world's x; spinning
        // 10 rad/s about it; c 0.5 (a half: 0x3F00); r 0.5; each 100 kg; the truck's bodies 4000 kg
        WheelDamp ws[4] = {};
        for (WheelDamp &wd : ws)
        {
            wd.ok = wd.grounded = true;
            wd.damping = 0x3F00;
            wd.inv[0] = 1.0f / 40.0f, wd.inv[1] = wd.inv[2] = 1.0f / 25.0f, wd.inv[3] = 1.0f / 100.0f;
            wd.cols[0][0] = wd.cols[1][1] = wd.cols[2][2] = 1.0f;
            wd.angular[0] = -10.0f;
            wd.radius = 0.5f;
        }
        int grounded = 0;
        const float four = WheelDampAccel(ws, 4, 4000.0f, &grounded); // 4 × 40 × 0.5 × 10 / 0.5 = 1600 N over 4400 kg
        const bool sum = Near(four, 1600.0f / 4400.0f, 1e-5f) && grounded == 4 && Near(WheelDampMass(ws, 4, 4000.0f), 4400.0f, 0.01f);
        WheelDamp up[4];
        memcpy(up, ws, sizeof up);
        up[3].grounded = false;
        up[3].damping = 0x7F80; // an airborne wheel's figures are not looked at (infinity here)
        const bool airborne = Near(WheelDampAccel(up, 4, 4000.0f, &grounded), 1200.0f / 4400.0f, 1e-5f) && grounded == 3;
        WheelDamp bad[4];
        memcpy(bad, ws, sizeof bad);
        bad[2].ok = false;
        WheelDamp wild[4];
        memcpy(wild, ws, sizeof wild);
        wild[1].damping = 0x4500; // 2048, over the 1000 a damping may be
        WheelDamp round[4];
        memcpy(round, ws, sizeof round);
        round[0].inv[0] = round[0].inv[1] = round[0].inv[2] = 1.0f / 25.0f; // a sphere: no axle, read by |w| (BRIEF_gear_tops_per_truck.md)
        const bool refused = isnan(WheelDampAccel(bad, 4, 4000.0f)) && isnan(WheelDampAccel(wild, 4, 4000.0f)) && isfinite(WheelDampAccel(round, 4, 4000.0f)) &&
                             isnan(WheelDampAccel(ws, 4, 0.0f));
        // through the game's memory: the reads as the layout gives them, and a wheel the class check left out
        const uint64_t cyl = 0x7000;
        TruckRefs r = {};
        r.wheels = r.models = 4;
        for (int i = 0; i < 4; i++)
        {
            const uint64_t wheel = 0x20000000ull + (uint64_t)i * 0x1000, body = wheel + 0x400, shape = wheel + 0x800;
            r.wheel[i] = wheel;
            MemPut(wheel + 0x18, body);
            MemPut(body + 0x20, shape);
            MemPut(shape, cyl);
            MemPut(wheel + 0x1A4, (uint8_t)1);
            MemPut(wheel + 0x94, 0.5f);
            MemPut(body + 0x216, (uint16_t)0x3F00);
            MemPut(body + 0x220, ws[0].inv);
            MemPut(body + 0x170, ws[0].cols);
            MemPut(body + 0x240, ws[0].angular);
        }
        float mass = 0.0f;
        const float read = TruckWheelDamp(g_mem, r, cyl, 4000.0f, &grounded, &mass);
        TruckRefs short_ = r;
        short_.models = 5;
        const bool memory = Near(read, four, 1e-6f) && Near(mass, 4400.0f, 0.01f) && isnan(TruckWheelDamp(g_mem, short_, cyl, 4000.0f)) &&
                            isnan(TruckWheelDamp(g_mem, r, cyl + 8, 4000.0f)) && isnan(TruckWheelDamp(g_mem, r, 0, 4000.0f));
        // part 2 of BRIEF_cut_radius_cylinder.md: the drag at the ground over the rolling radius, the cylinder of 0.4 under
        // + 0x94 of 0.5 (the force goes by 1 over it: 1.25 times); one out of bounds (0.2, under half) keeps + 0x94
        for (int i = 0; i < 4; i++) MemPut(0x20000000ull + (uint64_t)i * 0x1000 + 0x800 + 0x28, 0.4f);
        const float byCylinder = TruckWheelDamp(g_mem, r, cyl, 4000.0f);
        for (int i = 0; i < 4; i++) MemPut(0x20000000ull + (uint64_t)i * 0x1000 + 0x800 + 0x28, 0.2f);
        const float outOfBounds = TruckWheelDamp(g_mem, r, cyl, 4000.0f);
        for (int i = 0; i < 4; i++) MemPut(0x20000000ull + (uint64_t)i * 0x1000 + 0x800 + 0x28, 0.0f);
        const bool rolling = Near(byCylinder, four * 0.5f / 0.4f, 1e-5f) && Near(outOfBounds, four, 1e-6f);
        char line[260];
        snprintf(line, sizeof line, "wheels' drag: added back, a_damp 0 learns %.3f, 0.2 at 0.8 learns %.3f; four wheels %.4f m/s2; one off the ground %.4f; a bad read, a wild damping, "
                 "fewer wheels than models: no pass; a sphere reads", learnWith(0.8f, 0.0f), learnWith(0.8f, 0.2f), four, WheelDampAccel(up, 4, 4000.0f));
        Check(zero && added && !nanLearned && sum && airborne && refused && memory && rolling, line);
    }
    // the rolling radius (BRIEF_cut_radius_cylinder.md with the Reviewer's addendum): the cylinder where it is read and
    // within 0.5 to 1.25 of + 0x94, else + 0x94 by its kind; a broken wheel with both equal as before, under the tire
    // mod's flat the cylinder; the truck's speed by it equals the wheels' mean and the over-rev share their mean over the
    // top; two sizes by n over the sum of 1 / r (S2)
    {
        const RollRadius good = WheelRollRadius(0.51f, 0.45f, true), notRead = WheelRollRadius(0.51f, 0.45f, false), nan = WheelRollRadius(0.51f, NAN, true),
                         zero = WheelRollRadius(0.51f, 0.0f, true), under = WheelRollRadius(0.51f, 0.25f, true), over = WheelRollRadius(0.51f, 0.64f, true),
                         edgeLow = WheelRollRadius(0.5f, 0.25f, true), edgeHigh = WheelRollRadius(0.5f, 0.625f, true), broken = WheelRollRadius(0.3f, 0.3f, true),
                         brokenFlat = WheelRollRadius(0.3f, 0.22f, true), noRadius = WheelRollRadius(NAN, 0.45f, true);
        const bool choice = good.kind == kRollCylinder && good.r == 0.45f && notRead.kind == kRollNoCylinder && notRead.r == 0.51f && nan.kind == kRollOutOfBounds && nan.r == 0.51f &&
                            zero.kind == kRollOutOfBounds && under.kind == kRollOutOfBounds && under.r == 0.51f && over.kind == kRollOutOfBounds && over.r == 0.51f &&
                            edgeLow.kind == kRollCylinder && edgeHigh.kind == kRollCylinder && broken.kind == kRollCylinder && broken.r == 0.3f && brokenFlat.r == 0.22f &&
                            noRadius.kind == kRollOutOfBounds;
        // a truck at 10 m/s on cylinders of 0.45 under + 0x94 of 0.51, the wheels at 10 / 0.45 rad/s, a top of 20
        const float spins[4] = { 10.0f / 0.45f, 10.0f / 0.45f, 10.0f / 0.45f, 10.0f / 0.45f };
        const bool all[4] = { true, true, true, true };
        RollMean used = {}, plain = {};
        for (int i = 0; i < 4; i++) RollMeanAdd(used, WheelRollRadius(0.51f, 0.45f, true).r), RollMeanAdd(plain, 0.51f);
        CutSpeeds sp = TorqueCutSpeeds(spins, all, 4, 1), old = sp;
        CutSpeedsTruck(sp, 10.0f, RollMeanOf(used), 1);
        CutSpeedsTruck(old, 10.0f, RollMeanOf(plain), 1);
        const bool truck = Near(sp.truck, sp.mean, 1e-4f) && Near(OverRevShare(sp.mean, sp.truck, 20.0f), sp.mean / 20.0f, 1e-5f) && Near(old.truck / old.mean, 0.45f / 0.51f, 1e-4f);
        // two sizes, 0.5 and 0.6, rolling without slip at 10 m/s
        const float two[4] = { 20.0f, 20.0f, 10.0f / 0.6f, 10.0f / 0.6f };
        RollMean sizes = {};
        RollMeanAdd(sizes, 0.5f), RollMeanAdd(sizes, 0.5f), RollMeanAdd(sizes, 0.6f), RollMeanAdd(sizes, 0.6f), RollMeanAdd(sizes, NAN), RollMeanAdd(sizes, 0.0f);
        CutSpeeds sp2 = TorqueCutSpeeds(two, all, 4, 1);
        CutSpeedsTruck(sp2, 10.0f, RollMeanOf(sizes), 1);
        const RollMean empty = {};
        const bool twoSizes = sizes.n == 4 && Near(sp2.truck, sp2.mean, 1e-4f) && isnan(RollMeanOf(empty));
        char line[300];
        snprintf(line, sizeof line, "rolling radius: the cylinder where read and within 0.5 to 1.25 of + 0x94, else + 0x94 by its kind; the truck by it %.3f against the wheels' %.3f "
                                    "(by + 0x94 %.3f); two sizes by n over the sum of 1 / r %.3f against %.3f",
                 sp.truck, sp.mean, old.truck, sp2.truck, sp2.mean);
        Check(choice && truck && twoSizes, line);
    }
    // the learning over visits (BRIEF_road_load_learning.md, with the Reviewer's addendum): gears 3 and 4 alternating every
    // 1.5 s floored at x 0.5 to 0.75 both learn (each 2 s summed over visits; before, never); a visit of 0.2 s adds
    // nothing; a 2.5° climb by the path learns within 5 %; a nose-up squat changes nothing, as the path has no pitch in
    // it; a shift's dip in the acceleration, skipped kBaseLearnSkip from the shift's end, learns within 5 %; a truck
    // change clears every gear's sums
    {
        const float dt = 0.05f, factor = 1.2f;
        const float base[kMaxGears] = { 0, 0, 0, 1.0f, 0.8f };
        auto accelAt = [&](int g, float x, float grade) { return base[g] * factor * GamePull(x) - kGravity * grade; };
        // alternating: x climbs from 0.5 to 0.75 over each 1.5 s visit; a shift's end restarts settled
        BaseLearn l = {};
        float learnt[kMaxGears] = {}, when[kMaxGears] = {};
        float settled = 0.0f, gathered3 = 0.0f;
        for (int i = 0; i < 400; i++)
        {
            const float t = i * dt, inVisit = fmodf(t, 1.5f);
            const int g = ((int)(t / 1.5f)) % 2 ? 4 : 3;
            settled = inVisit < dt * 0.5f ? 0.0f : settled + dt;
            const float x = 0.5f + 0.25f * inVisit / 1.5f;
            float got = 0.0f, added = 0.0f;
            const int done = BaseLearnStep(l, g, true, accelAt(g, x, 0.0f), 0.0f, factor, 1.0f, x, 0.0f, settled, dt, got, &added);
            if (g == 3) gathered3 += added;
            if (done && !learnt[done]) learnt[done] = got, when[done] = t;
        }
        const bool alternating = Near(learnt[3], base[3], 0.05f * base[3]) && Near(learnt[4], base[4], 0.05f * base[4]) && when[3] > 0.0f && when[3] < 7.0f &&
                                 when[4] > 0.0f && when[4] < 8.0f;
        // a visit of 0.2 s adds nothing
        BaseLearn shortVisit = {};
        float got = 0.0f, added = 0.0f, shortAdded = 0.0f;
        for (int i = 0; i < 4; i++) BaseLearnStep(shortVisit, 3, true, accelAt(3, 0.5f, 0.0f), 0.0f, factor, 1.0f, 0.5f, 0.0f, 9.0f, dt, got, &added), shortAdded += added;
        BaseLearnStep(shortVisit, 3, false, 0.0f, 0.0f, factor, 1.0f, 0.5f, 0.0f, 9.0f, dt, got, &added);
        const bool shortNothing = shortAdded == 0.0f && shortVisit.time[3] == 0.0f;
        // a 2.5° climb by the path: the game's pull lifts the truck too, and a + g sin θ gives a_base back
        const float climb = sinf(2.5f * 3.14159265f / 180.0f);
        BaseLearn c = {};
        float onClimb = 0.0f;
        for (int i = 0; i < 80 && !onClimb; i++)
            if (BaseLearnStep(c, 3, true, accelAt(3, 0.55f, climb), 0.0f, factor, 1.0f, 0.55f, climb, 9.0f, dt, got)) onClimb = got;
        // 4° by the path is no learning
        BaseLearn steep = {};
        bool steepLearned = false;
        const float four = sinf(4.0f * 3.14159265f / 180.0f);
        for (int i = 0; i < 80; i++) steepLearned |= BaseLearnStep(steep, 3, true, accelAt(3, 0.55f, four), 0.0f, factor, 1.0f, 0.55f, four, 9.0f, dt, got) != 0;
        // the grade by the path: a truck at 10 m/s up 2.5° through a 0.7° nose-up squat of the chassis (no part of the
        // path) reads 2.5°; under kGradeWay in the span it is unknown
        GradeTrack track = {}, slow = {};
        float grade = NAN, slowGrade = 0.0f;
        for (int i = 0; i <= 30; i++)
        {
            const float t = i * dt, along = 10.0f * t;
            const float at[3] = { along * cosf(2.5f * 3.14159265f / 180.0f), along * climb, 3.0f };
            grade = GradeStep(track, at, t);
            const float crawl[3] = { 0.5f * t, 0.0f, 0.0f };
            slowGrade = GradeStep(slow, crawl, t);
        }
        const bool byPath = Near(grade, climb, 0.002f) && !isfinite(slowGrade) && Near(onClimb, base[3], 0.05f * base[3]) && !steepLearned;
        // a shift's dip: the smoothed acceleration holds a share of it for a while after the shift's end; skipped
        // kBaseLearnSkip from the end it learns within 5 %
        BaseLearn d = {};
        float dipLearned = 0.0f, smooth = 0.0f, set = 0.0f;
        for (int i = 0; i < 200 && !dipLearned; i++)
        {
            const float t = i * dt, phase = fmodf(t, 1.5f);
            const bool shifting = phase < 0.2f;
            if (phase < dt * 0.5f) set = 0.0f;
            if (!shifting) set += dt; // the shift's stage idle again after 0.2 s
            const float raw = shifting ? 0.0f : accelAt(3, 0.6f, 0.0f);
            smooth += (raw - smooth) * (1.0f - expf(-dt / kAccelSmooth));
            const int done = BaseLearnStep(d, 3, !shifting, smooth, 0.0f, factor, 1.0f, 0.6f, 0.0f, set, dt, got);
            if (done) dipLearned = got;
        }
        const bool dip = Near(dipLearned, base[3], 0.05f * base[3]);
        // a truck change clears every gear's sums
        BaseLearn cleared = l;
        cleared = BaseLearn{};
        bool empty = true;
        for (int g = 0; g < kMaxGears; g++) empty = empty && cleared.time[g] == 0.0f && cleared.sum[g] == 0.0f;
        char line[300];
        snprintf(line, sizeof line, "road load learning: 3 and 4 alternating every 1.5 s learn %.3f (of %.2f, at %.1f s) and %.3f (of %.2f, at %.1f s); a 0.2 s visit adds "
                 "nothing; a 2.5° climb by the path %.3f; 4° none; a shift's dip skipped %.3f; a truck change clears",
                 learnt[3], base[3], when[3], learnt[4], base[4], when[4], onClimb, dipLearned);
        Check(alternating && shortNothing && byPath && dip && empty && gathered3 > 0.0f, line);
    }
    // the kept pull saved against the file's figure (BRIEF_air_drag_save.md): passes 10 % below a kept 2.0 that is the
    // file's write whenever the kept figure is more than 2 % off the last written, four times, then stop for good (the old
    // rule, a single 30 % step over 5 %, wrote none of them); a pass within 2 % writes nothing; a gear with none in the
    // file writes on its first pass
    {
        float kept = 2.0f, file = 2.0f;
        int writes = 0, lastWrite = -1;
        bool rule = true;
        for (int i = 0; i < 100; i++)
        {
            const float fileBefore = file;
            const bool wrote = GearBaseStep(kept, file, 1.8f);
            rule &= wrote == (fabsf(kept - fileBefore) > 0.02f * fileBefore) && (!wrote || file == kept) && (wrote || file == fileBefore);
            if (wrote) writes++, lastWrite = i;
        }
        float kept2 = 2.0f, file2 = 2.0f;
        const bool within = !GearBaseStep(kept2, file2, 1.9f) && Near(kept2, 1.97f, 1e-4f) && file2 == 2.0f;
        float kept3 = 0.0f, file3 = 0.0f;
        const bool firstPass = GearBaseStep(kept3, file3, 1.6f) && kept3 == 1.6f && file3 == 1.6f;
        char line[300];
        snprintf(line, sizeof line, "air drag saved: passes 10 %% below a kept 2.0 write %d times (the last on pass %d), the file then %.3f with the kept %.3f; "
                 "within 2 %% nothing; none in the file writes on the first pass", writes, lastWrite + 1, file, kept);
        Check(rule && writes == 4 && lastWrite == 8 && fabsf(file - kept) <= 0.02f * file && within && firstPass, line);
    }
    // one figure per truck: two gears learned with equal a_base × top give an unlearned gear K / top; spread 20 % by
    // gear only; one gear learned alone fills nothing
    {
        float base[kMaxGears] = {}, top[kMaxGears] = {}, use[kMaxGears] = {};
        for (int g = 1; g <= 10; g++) top[g] = 2.0f * g;
        base[4] = 1.5f;
        base[5] = 1.2f; // 1.5 × 8 = 12 = 1.2 × 10
        float k = 0.0f, spread = 0.0f;
        bool folded = false;
        const int two = BaseFold(base, top, 10, use, k, spread, folded);
        const bool fills = two == 2 && folded && Near(k, 12.0f, 0.001f) && Near(use[10], 12.0f / 20.0f, 0.001f) && use[4] == 1.5f && use[5] == 1.2f;
        base[5] = 1.2f * 1.2f; // 20 % apart
        BaseFold(base, top, 10, use, k, spread, folded);
        const bool apart = !folded && spread > 0.15f && use[10] == 0.0f && use[4] == 1.5f;
        base[5] = 0.0f;
        BaseFold(base, top, 10, use, k, spread, folded);
        const bool alone = !folded && use[10] == 0.0f && use[4] == 1.5f;
        // L- and L floored (the game's gear 1 at 0.20 and 0.45) add nothing to gear 1; L+ and the 1st (1.00) do (review F1)
        const bool plain = !BaseLearnPlainPower(1, 0.2f) && !BaseLearnPlainPower(1, 0.45f) && BaseLearnPlainPower(1, 1.0f) && BaseLearnPlainPower(1, 0.0f) &&
                           BaseLearnPlainPower(2, 0.2f);
        BaseLearn low = {};
        float lowGot = 0.0f, lowAdded = 0.0f, lowSum = 0.0f;
        for (int i = 0; i < 80; i++)
            BaseLearnStep(low, 1, BaseLearnPlainPower(1, 0.2f), 0.5f * 1.2f * GamePull(0.5f), 0.0f, 1.2f, 1.0f, 0.5f, 0.0f, 9.0f, 0.05f, lowGot, &lowAdded), lowSum += lowAdded;
        const bool lowNothing = lowSum == 0.0f && low.time[1] == 0.0f;
        Check(fills && apart && alone && plain && lowNothing, "road load fold: two gears with the same a_base × top give the others K / top; 20 % apart, or one gear alone, by gear only; L- floored adds nothing to gear 1");
    }
    // part 1's probe: a synthetic acceleration of the game's own pull shape gives its most where the pull peaks and
    // its fall past it about kGamePullFall
    {
        PullBins b = {};
        for (float x = 0.30f; x < 0.98f; x += 0.005f) PullBinsAdd(b, x, 1.5f * GamePull(x), 0.05f);
        float most = 0.0f, mostX = 0.0f, fall = 0.0f;
        const int bins = PullBinsFit(b, most, mostX, fall);
        char line[160];
        snprintf(line, sizeof line, "pull probe: %d bins, the most %.2f at %.2f, the fall fitted %.2f (the curve's %.1f)", bins, most, mostX, fall, kGamePullFall);
        Check(bins == kPullBins && Near(most, 1.5f, 0.02f) && mostX >= 0.4f && mostX <= 0.55f && Near(fall, kGamePullFall, 0.15f), line);
    }

    // the shift's state (review 17, step 2): asked, set, engaged and idle kCreepQuiet later, in that order; the stall of a
    // shift without the clutch comes once, on the first pass after it is set, never while it is only asked; a shift
    // not taken goes idle with its stall; one that never engages goes idle after kStageMost
    {
        const float dt = 0.05f;
        ShiftStage st = {};
        const bool idleFirst = !ShiftStageStep(st, 3, dt) && st.stage == kStageIdle;
        ShiftStageAsk(st, 1, true);
        const bool askedNoStall = !ShiftStageStep(st, -1, dt) && !ShiftStageStep(st, -1, dt) && st.stage == kStageAsked && st.stallOwed;
        ShiftStageResult(st, true);
        const bool set = st.stage == kStageSet;
        const bool stallOnSet = ShiftStageStep(st, -1, dt) && st.stage == kStageSet && !st.stallOwed;
        const bool once = !ShiftStageStep(st, 1, dt) && st.stage == kStageEngaged;
        int passes = 0;
        while (st.stage == kStageEngaged && passes < 100) ShiftStageStep(st, 1, dt), passes++;
        const bool quiet = st.stage == kStageIdle && passes * dt >= kCreepQuiet - 0.001f && passes * dt < kCreepQuiet + dt + 0.001f;
        ShiftStage refused = {};
        ShiftStageAsk(refused, -1, true);
        ShiftStageResult(refused, false);
        const bool notTaken = refused.stage == kStageIdle && !refused.stallOwed && !ShiftStageStep(refused, 0, dt);
        ShiftStage stuck = {};
        ShiftStageAsk(stuck, 2, false);
        ShiftStageResult(stuck, true);
        passes = 0;
        while (stuck.stage != kStageIdle && passes < 1000) ShiftStageStep(stuck, 1, dt), passes++;
        const bool givesUp = passes * dt >= kStageMost - 0.001f && passes * dt < kStageMost + dt + 0.001f;
        ShiftStage late = {};
        ShiftStageResult(late, true); // an answer with nothing asked changes nothing
        Check(idleFirst && askedNoStall && set && stallOnSet && once && quiet && notTaken && givesUp && late.stage == kStageIdle,
              "shift state: asked, set, engaged and idle kCreepQuiet after it engaged; a clutchless shift's stall once, as it is set; a shift not taken "
              "goes idle with its stall; one that never engages goes idle after kStageMost");
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
            // version 2 and 256 bytes, which the tire mod's reader takes (version >= 1, size >= 64, 64 bytes copied)
            read = u32(0) == 0x47554147u && u32(4) == 2 && u32(8) == 256 && !(sequence & 1) && u64(16) == 777000 && u64(24) == 0x1234567890ull && f32(32) == -12.5f &&
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

        // version 2, read as the header's example reader does: the whole mapping (size 0), a copy while the sequence holds
        values = StateValues{};
        values.vehicle = 0xABCDull;
        values.flags = kStateVehicle | kStateEngineOn;
        values.speedKmh = -7.5f;
        values.speedMax = 100.0f;
        values.rpmRedline = 2400.0f;
        values.rpmMax = 3000.0f;
        values.gear = 3;
        values.gearMode = StateGearMode("A");
        values.top = 16;
        values.gearboxLevel = 2;
        values.tireLamp = SRG_TIRE_REDUCED;
        values.oilC = 104.0f;
        values.oilWarnC = 120.0f;
        values.airBar = 6.2f;
        values.boostTop = 2.5f;
        values.fuelFrac = 0.5f;
        values.fuelL = 150.0f;
        values.tankL = 300.0f;
        values.burnPerMin = NAN;
        values.rangeKm = INFINITY;
        values.coolantC = NAN;
        values.lamps = SRG_LAMP_HANDBRAKE | SRG_LAMP_OIL | (2u << SRG_LAMP_TILT_SHIFT);
        values.blinks = SRG_LAMP_OIL;
        values.options = SRG_OPT_MPH | SRG_OPT_CLUSTER;
        StateWrite(writer, values, 778000);
        const HANDLE whole = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        const SrgState *view = whole ? (const SrgState *)MapViewOfFile(whole, FILE_MAP_READ, 0, 0, 0) : nullptr;
        bool v2 = false, oldBlock = false;
        if (view)
        {
            SrgState copy = {};
            const long before = view->sequence;
            memcpy(&copy, (const void *)view, view->size < sizeof copy ? view->size : sizeof copy);
            v2 = !(before & 1) && view->sequence == before && copy.version == 2 && copy.size == 256 && copy.vehicle == 0xABCDull && copy.speedKmh == -7.5f &&
                 copy.speedMax == 100.0f && copy.rpmRedline == 2400.0f && copy.rpmMax == 3000.0f && copy.gear == 3 && copy.gearMode == SRG_GEAR_A &&
                 copy.top == 16 && copy.gearboxLevel == 2 && copy.tireLamp == SRG_TIRE_REDUCED && copy.oilC == 104.0f && copy.oilWarnC == 120.0f &&
                 copy.airBar == 6.2f && copy.boostTop == 2.5f && copy.fuelFrac == 0.5f && copy.fuelL == 150.0f && copy.tankL == 300.0f &&
                 copy.burnPerMin == -1.0f && copy.rangeKm == -1.0f && copy.coolantC == 0.0f && SRG_LAMP_TILT(copy.lamps) == 2 &&
                 (copy.lamps & SRG_LAMP_OIL) && (copy.blinks & SRG_LAMP_OIL) && !(copy.blinks & SRG_LAMP_HANDBRAKE) && copy.options == (SRG_OPT_MPH | SRG_OPT_CLUSTER) &&
                 SRG_STATE_HAS(copy, options) && SRG_STATE_HAS(copy, reserved2);
            // an older Gauges' block of 64 bytes: a version 2 reader finds the version 1 fields and none of the new ones
            SrgState old = {};
            memcpy(&old, (const void *)view, 64);
            old.version = 1;
            old.size = 64;
            oldBlock = SRG_STATE_HAS(old, flags) && SRG_STATE_HAS(old, boostBar) && !SRG_STATE_HAS(old, speedKmh) && !SRG_STATE_HAS(old, options);
            UnmapViewOfFile(view);
        }
        if (whole) CloseHandle(whole);
        const bool modes = StateGearMode("") == SRG_GEAR_NUMBER && StateGearMode(nullptr) == SRG_GEAR_NUMBER && StateGearMode("H") == SRG_GEAR_H &&
                           StateGearMode("L-") == SRG_GEAR_L_MINUS && StateGearMode("L") == SRG_GEAR_L && StateGearMode("L+") == SRG_GEAR_L_PLUS;
        // the warnings with the dials off (warn_list.h, BRIEF_dials_off_option.md with addendum 2)
        {
            GaugeSettings ws;
            SettingsDefaults(ws);
            const WarnBox noBox = { kWarnNone, "", "", 0 }, clutchAmber = { kWarnAmber, "CLUTCH HOT", "Ease off or shift down", 60 },
                          clutchRed = { kWarnRed, "CLUTCH DAMAGE", "Shift down, the clutch is wearing", 100 }, engineAmber = { kWarnAmber, "ENGINE HOT", "Ease off, the coolant is in the red", 10 },
                          engineRed = { kWarnRed, "ENGINE DAMAGE", "Ease off, the engine is wearing", 80 };
            // a truck with nothing wrong: every figure read and on its good side
            WarnFigures fine = {};
            fine.driving = true;
            fine.clutch = fine.engine = noBox;
            fine.fuelKnown = fine.engineKnown = fine.gearboxKnown = fine.coolantKnown = fine.airKnown = true;
            fine.fuelPercent = 80.0f, fine.enginePercent = 10.0f, fine.gearboxPercent = 10.0f, fine.coolantC = 90.0f, fine.oilC = 95.0f, fine.airBar = 8.0f;
            auto list = [&](int layout, int choice, const WarnFigures &f, WarnMargins &wmar) {
                GaugeSettings wset = ws;
                wset.layout = layout, wset.hiddenWarnings = choice;
                return WarnListMake(wset, f, wmar);
            };
            auto is = [](const WarnList &l, int at, int what, int state) { return l.what[at] == what && l.box[at].state == state; };
            auto empty = [&](const WarnList &l) {
                bool e = l.count == 0;
                for (int i = 0; i < kWarnBoxes; i++) e = e && l.box[i].state == kWarnNone && l.what[i] < 0;
                return e;
            };
            // every figure at its worst
            WarnFigures bad = fine;
            bad.clutch = clutchAmber, bad.engine = engineAmber;
            bad.fuelPercent = 3.0f, bad.enginePercent = 90.0f, bad.gearboxPercent = 95.0f, bad.coolantC = 119.0f, bad.oilHot = true, bad.oilC = 130.0f, bad.tilt = 1, bad.rollDeg = -18.0f,
            bad.airBar = 3.0f;

            // M2: each choice with the same figures. With dials, whatever the key holds: today's two boxes in their slots
            bool choices = true;
            for (int layout = kLayoutStrip; layout <= kLayoutHudDigital; layout++)
                for (int choice = kHwNone; choice < kHwCount; choice++)
                {
                    WarnMargins wmar = { true, true, true };
                    const WarnList l = list(layout, choice, bad, wmar);
                    choices = choices && !l.stacked && l.count == 2 && is(l, 0, kWlClutch, kWarnAmber) && is(l, 1, kWlEngine, kWarnAmber) && l.box[2].state == kWarnNone &&
                              !strcmp(l.box[1].line, "Ease off, the coolant is in the red") && !wmar.fuel && !wmar.coolant && !wmar.air;
                    // the engine's alone stays in the second slot
                    WarnFigures one = bad;
                    one.clutch = noBox;
                    const WarnList o = list(layout, choice, one, wmar);
                    choices = choices && o.box[0].state == kWarnNone && is(o, 1, kWlEngine, kWarnAmber);
                }
            WarnMargins mm = {};
            const WarnList offNone = list(kLayoutOff, kHwNone, bad, mm), offDials = list(kLayoutOff, kHwAsDials, bad, mm);
            choices = choices && offNone.stacked && empty(offNone) && offDials.stacked && offDials.count == 2 && is(offDials, 0, kWlClutch, kWarnAmber) && is(offDials, 1, kWlEngine, kWarnAmber) &&
                      !mm.fuel && !mm.coolant && !mm.air;
            {
                // as with dials in Off the boxes stack: the engine's alone takes the first place
                WarnFigures one = bad;
                one.clutch = noBox;
                const WarnList o = list(kLayoutOff, kHwAsDials, one, mm);
                choices = choices && o.count == 1 && is(o, 0, kWlEngine, kWarnAmber);
            }
            // None asks for no warning sound, and only in Off; the other two and every layout with dials do
            bool sounds = true;
            for (int layout = 0; layout < kLayoutCount; layout++)
                for (int choice = kHwNone; choice < kHwCount; choice++)
                {
                    GaugeSettings wset = ws;
                    wset.layout = layout, wset.hiddenWarnings = choice;
                    sounds = sounds && WarnSoundsOn(wset) == !(layout == kLayoutOff && choice == kHwNone);
                }
            {
                // the oil over OilWarnC with the engine running: the model asks for its beep, and in None none is played
                GaugeSettings wset = ws;
                wset.layout = kLayoutOff, wset.hiddenWarnings = kHwNone;
                OilWarning ow = {};
                TempModel tm = {};
                tm.known = true, tm.oil = wset.oilWarnC + 5.0f;
                TruckRaw tr = {};
                tr.flags = 1; // the engine runs
                const bool asked = OilWarnStep(ow, tm, tr, wset, 0.05f);
                sounds = sounds && asked && ow.on && !(asked && WarnSoundsOn(wset));
                wset.hiddenWarnings = kHwAsDials;
                sounds = sounds && WarnSoundsOn(wset);
            }

            // M3: five warnings at once, three shown, the red first; one goes and the fourth comes
            bool stack = true;
            {
                WarnFigures f = bad;
                f.engine = engineRed;
                f.tilt = 2, f.rollDeg = 27.0f;
                WarnMargins wmar = {};
                const WarnList l = list(kLayoutOff, kHwLimits, f, wmar);
                stack = stack && l.stacked && l.count == 3 && is(l, 0, kWlEngine, kWarnRed) && is(l, 1, kWlTilt, kWarnRed) && is(l, 2, kWlClutch, kWarnAmber) &&
                        !strcmp(l.box[0].title, "ENGINE DAMAGE") && !strcmp(l.box[1].figure, "27 deg") && wmar.fuel && wmar.air;
                f.tilt = 0, f.rollDeg = 2.0f;
                const WarnList l2 = list(kLayoutOff, kHwLimits, f, wmar);
                stack = stack && l2.count == 3 && is(l2, 0, kWlEngine, kWarnRed) && is(l2, 1, kWlClutch, kWarnAmber) && is(l2, 2, kWlFuel, kWarnAmber);
                f.clutch = noBox, f.engine = noBox, f.coolantC = 90.0f;
                const WarnList l3 = list(kLayoutOff, kHwLimits, f, wmar);
                stack = stack && l3.count == 3 && is(l3, 0, kWlFuel, kWarnAmber) && is(l3, 1, kWlEngineWorn, kWarnAmber) && is(l3, 2, kWlGearboxWorn, kWarnAmber);
                f.fuelPercent = 80.0f, f.enginePercent = 10.0f;
                const WarnList l4 = list(kLayoutOff, kHwLimits, f, wmar);
                stack = stack && l4.count == 3 && is(l4, 0, kWlGearboxWorn, kWarnAmber) && is(l4, 1, kWlOil, kWarnAmber) && is(l4, 2, kWlAir, kWarnAmber) &&
                        !strcmp(l4.box[1].figure, "130 C") && !strcmp(l4.box[2].figure, "3.0 bar") && l4.box[0].figure[0] == 0 && l4.box[0].percent == 95;
                // the figures in the player's units
                GaugeSettings wset = ws;
                wset.layout = kLayoutOff, wset.hiddenWarnings = kHwLimits, wset.fahrenheit = true, wset.psi = true;
                const WarnList l5 = WarnListMake(wset, f, wmar);
                stack = stack && !strcmp(l5.box[1].figure, "266 F") && !strcmp(l5.box[2].figure, "44 psi");
            }

            // M1: every limit at its off end with every figure at its worst: the red boxes of damage, the tilt's and the brake
            // air's still come, and the oil's, which has no off end of its own here; no fuel, wear or coolant box
            bool offEnds = true;
            {
                GaugeSettings wset = ws;
                wset.layout = kLayoutOff, wset.hiddenWarnings = kHwLimits, wset.warnFuel = 0, wset.warnEngine = 100, wset.warnGearbox = 100, wset.warnCoolant = 100;
                WarnFigures f = bad;
                f.fuelPercent = 0.0f, f.enginePercent = 100.0f, f.gearboxPercent = 100.0f, f.coolantC = 124.0f, f.oilHot = false;
                f.clutch = noBox;
                WarnMargins wmar = {};
                const WarnList amberOnly = WarnListMake(wset, f, wmar);
                offEnds = offEnds && amberOnly.count == 2 && is(amberOnly, 0, kWlTilt, kWarnAmber) && is(amberOnly, 1, kWlAir, kWarnAmber) && !wmar.fuel && !wmar.coolant && wmar.air;
                f.clutch = clutchRed, f.engine = engineRed;
                const WarnList reds = WarnListMake(wset, f, wmar);
                offEnds = offEnds && reds.count == 3 && is(reds, 0, kWlClutch, kWarnRed) && is(reds, 1, kWlEngine, kWarnRed) && is(reds, 2, kWlTilt, kWarnAmber);
                f.tilt = 0, f.airBar = 8.0f, f.clutch = noBox, f.engine = engineAmber;
                offEnds = offEnds && empty(WarnListMake(wset, f, wmar)); // the engine's amber at 118 C is not shown either: the coolant's limit is off
                f.oilHot = true;
                const WarnList oil = WarnListMake(wset, f, wmar);
                offEnds = offEnds && oil.count == 1 && is(oil, 0, kWlOil, kWarnAmber);
            }

            // M4: each limit just under, just over and back inside its margin
            bool margins = true;
            {
                GaugeSettings wset = ws;
                wset.layout = kLayoutOff, wset.hiddenWarnings = kHwLimits; // fuel under 15, damage over 50, coolant over 90 percent of 85 to 118 C: 114.7 C
                WarnMargins wmar = {};
                WarnFigures f = fine;
                auto has = [&](int what) {
                    const WarnList l = WarnListMake(wset, f, wmar);
                    for (int i = 0; i < l.count; i++)
                        if (l.what[i] == what) return true;
                    return false;
                };
                f.fuelPercent = 15.1f;
                margins = margins && !has(kWlFuel);
                f.fuelPercent = 14.9f;
                margins = margins && has(kWlFuel);
                f.fuelPercent = 16.9f;
                margins = margins && has(kWlFuel) && has(kWlFuel); // inside the margin of 2 points: no flicker
                f.fuelPercent = 17.1f;
                margins = margins && !has(kWlFuel);
                f.fuelPercent = 16.0f;
                margins = margins && !has(kWlFuel); // and not on again inside the margin
                f.fuelPercent = 80.0f;
                f.enginePercent = 50.0f;
                margins = margins && !has(kWlEngineWorn);
                f.enginePercent = 50.5f;
                margins = margins && has(kWlEngineWorn);
                f.enginePercent = 49.9f; // after a repair: gone at once, there is no margin
                margins = margins && !has(kWlEngineWorn);
                f.gearboxPercent = 50.0f;
                margins = margins && !has(kWlGearboxWorn);
                f.gearboxPercent = 51.0f;
                margins = margins && has(kWlGearboxWorn);
                f.gearboxPercent = 10.0f;
                margins = margins && !has(kWlGearboxWorn);
                const float at90 = kTempWarm + (kTempRed - kTempWarm) * 0.90f, at87 = kTempWarm + (kTempRed - kTempWarm) * 0.87f;
                f.coolantC = at90 - 0.2f;
                margins = margins && !has(kWlEngine);
                f.coolantC = at90 + 0.2f;
                margins = margins && has(kWlEngine);
                f.coolantC = at87 + 0.2f;
                margins = margins && has(kWlEngine); // inside the margin of 3 points
                {
                    const WarnList l = WarnListMake(wset, f, wmar);
                    margins = margins && l.count == 1 && !strcmp(l.box[0].title, "ENGINE HOT") && !strcmp(l.box[0].line, "The coolant is over your limit") && l.box[0].percent == 87;
                }
                f.coolantC = at87 - 0.2f;
                margins = margins && !has(kWlEngine);
                f.coolantC = at87 + 0.5f;
                margins = margins && !has(kWlEngine);
                f.coolantC = 90.0f;
                f.airBar = kAirWarn + 0.05f;
                margins = margins && !has(kWlAir);
                f.airBar = kAirWarn - 0.05f;
                margins = margins && has(kWlAir);
                f.airBar = kAirWarn + 0.25f;
                margins = margins && has(kWlAir);
                f.airBar = kAirWarn + 0.35f;
                margins = margins && !has(kWlAir);
                f.airBar = kAirWarn + 0.25f;
                margins = margins && !has(kWlAir);
                // the margins are let go when the choice is left, the layout shows dials again or no truck is driven
                f.fuelPercent = 14.0f, f.coolantC = 117.0f, f.airBar = 3.0f;
                margins = margins && has(kWlFuel) && wmar.fuel && wmar.coolant && wmar.air;
                WarnFigures parkedNow = f;
                parkedNow.driving = false;
                margins = margins && empty(WarnListMake(wset, parkedNow, wmar)) && !wmar.fuel && !wmar.coolant && !wmar.air;
            }

            // a figure not read gives no box, never a box from a zero; no truck, no box; a figure that is no number neither
            bool notRead = true;
            {
                GaugeSettings wset = ws;
                wset.layout = kLayoutOff, wset.hiddenWarnings = kHwLimits;
                WarnFigures f = {};
                f.driving = true;
                f.clutch = f.engine = noBox;
                f.tilt = -1;
                WarnMargins wmar = {};
                notRead = notRead && empty(WarnListMake(wset, f, wmar)); // nothing read: fuel 0, air 0, damage 0 of 0, and no box
                f.fuelKnown = f.engineKnown = f.gearboxKnown = f.coolantKnown = f.airKnown = true;
                f.fuelPercent = f.enginePercent = f.gearboxPercent = f.coolantC = f.airBar = NAN;
                f.oilHot = true, f.oilC = NAN;
                notRead = notRead && empty(WarnListMake(wset, f, wmar));
                WarnFigures parked = bad;
                parked.driving = false;
                parked.clutch = clutchRed, parked.engine = engineRed;
                for (int layout = 0; layout < kLayoutCount; layout++)
                    for (int choice = kHwNone; choice < kHwCount; choice++)
                    {
                        const WarnList l = list(layout, choice, parked, wmar);
                        for (int i = 0; i < kWarnBoxes; i++) notRead = notRead && l.box[i].state == kWarnNone;
                    }
                // a scout has no brake air to warn of
                WarnFigures scout = fine;
                scout.airKnown = false, scout.airBar = 0.0f;
                notRead = notRead && empty(WarnListMake(wset, scout, wmar));
            }
            Check(choices, "warnings with the dials off: with dials the two boxes of today in their slots whatever the choice; in Off None gives none, As with dials the two as a stack");
            Check(sounds, "warnings with the dials off: only Off with None asks for no warning sound, and there the oil over its limit plays none");
            Check(stack, "warnings with the dials off: at most three at once, the red first, then in the list's order; one goes and the next comes; degrees, bar and psi at the right");
            Check(offEnds, "warnings with the dials off: every limit at its off end never warns, while the damage, tilt, brake air and oil boxes still come");
            Check(margins, "warnings with the dials off: each limit just under, just over and back inside its margin without a flicker; damage has no margin; the margins are let go with the truck");
            Check(notRead, "warnings with the dials off: a figure not read or no number gives no box, no truck gives none, a scout has no brake air box");
        }
        // the Off layout (BRIEF_dials_off_option.md): the block takes one thing from the layout and the toggle, the bit that
        // says the cluster is on screen. Off gives the options a cluster hidden by the key gives, to the bit
        {
            bool same = true;
            for (int combo = 0; combo < 32; combo++)
            {
                const bool mph = (combo & 1) != 0, psi = (combo & 2) != 0, fahrenheit = (combo & 4) != 0, vacuum = (combo & 8) != 0, natural = (combo & 16) != 0;
                const uint32_t strip = StateOptions(mph, psi, fahrenheit, ClusterHidden(kLayoutStrip, true, false, true, false) == kShown, vacuum, natural);
                const uint32_t byKey = StateOptions(mph, psi, fahrenheit, ClusterHidden(kLayoutStrip, false, false, true, false) == kShown, vacuum, natural);
                const uint32_t noDials = StateOptions(mph, psi, fahrenheit, ClusterHidden(kLayoutOff, true, false, true, false) == kShown, vacuum, natural);
                same = same && noDials == byKey && (strip ^ noDials) == SRG_OPT_CLUSTER && !(noDials & SRG_OPT_CLUSTER);
            }
            Check(same, "state: with the Off layout the options are a hidden cluster's, and differ from the strip's on screen by the cluster bit alone");
        }
        Check(v2 && oldBlock && modes, "state: version 2 carries what the cluster shows at the header's places, an unknown burn or range as -1 and never NaN, "
                                       "the gear letters as codes, and a reader of version 2 sees an older 64-byte block's new fields as absent");
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
        // review 12's closed loop: the same truck floored from a standstill in A, the converter flaring its engine (the
        // limit dips while the rotor spools, then the air allows all of it); and a climb held at mid revs in second by
        // hand (settled, the limit 1 and the engine gives its curve)
        {
            LoopTruck launch = drive;
            LoopState ls;
            float dip = 2.0f, dipAt = 0.0f, whole = -1.0f;
            for (float at = kDt; at < 10.0f; at += kDt)
            {
                LoopStep(launch, ls, hc, 1.0f, kDt, false);
                if (ls.boost.limit < dip) { dip = ls.boost.limit; dipAt = at; }
                if (whole < 0.0f && at > dipAt && dip < 0.9f && ls.boost.limit >= 0.999f) whole = at;
            }
            LoopTruck climb = drive;
            climb.fixedGear = 2;
            climb.grade = 1.0f;
            LoopState cl;
            LoopDrive(climb, cl, hc, 1.0f, 30.0f, false, false);
            const float climbRpm = cl.tach.rpm;
            printf("     loop, the HX 520 floored from a standstill in A: the limit dips to %.2f at %.1f s and is whole at %.1f s; climbing in second at %.0f rpm "
                   "settled: the boost %.2f bar, the limit %.2f, the engine %.2f of its curve's %.2f\n", dip, dipAt, whole, climbRpm, cl.boost.bar, cl.boost.limit, cl.power.power,
                   cl.power.curve);
            Check(dip < 0.7f && whole > 0.0f && whole < 4.0f && climbRpm > 0.5f * climb.k.redline && climbRpm < 0.95f * climb.k.redline && cl.boost.limit >= 0.999f &&
                      Near(cl.power.power, cl.power.curve, 0.005f),
                  "loop: floored from a standstill the turbo costs torque only while it spools, then the air allows all of it; climbing at mid revs settled the "
                  "engine gives its whole curve");
        }

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
            Drive(c, 1, false, 0.5f * idleAt, 0.0f);
            const float govHalf = CreepShare(c, ck, false, 1.0f);
            Drive(c, 1, false, idleAt - 0.01f, 0.0f);
            const float govNear = CreepShare(c, ck, false, 1.0f);
            Drive(c, 1, false, idleAt, 0.0f);
            const float govIdle = CreepShare(c, ck, false, 1.0f);
            // the low range: the governor's most by the first gear's top in it over the plain first's
            TruckRaw plainFirst = c;
            c.powerFactor = 0.2f;
            Drive(c, 1, false, 0.0f, 0.0f);
            const float govLow = CreepShare(c, ck, false, 1.0f), lowMost = kIdleShare * TruckGearTop(c, 1) / TruckGearTop(plainFirst, 1);
            Drive(c, 2, false, 0.0f, 0.0f);
            const float govSecond = CreepShare(c, ck, false, 1.0f);
            c.powerFactor = 1.0f;
            Check(Near(convStand, kCreepShare, 1e-5f) && Near(govStand, kIdleShare, 1e-5f) && Near(govHalfGrip, 0.5f * kIdleShare, 1e-5f) && govPressed == 0.0f &&
                      Near(convHalfPedal, 0.5f * kCreepShare, 1e-5f) && convHalf > 0.2f * kCreepShare && convHalf < 0.8f * kCreepShare && convCoupled == 0.0f &&
                      Near(govHalf, 0.5f * kIdleShare, 1e-4f) && govNear > 0.0f && govNear < 0.1f * kIdleShare && govIdle == 0.0f && Near(govLow, lowMost, 1e-4f) &&
                      govLow < 0.5f * kIdleShare && Near(govSecond, kIdleShare, 1e-5f),
                  "creep: through the converter its share at a standstill fades to nothing at the coupling point; the governor's falls in a line from its most at a "
                  "standstill to nothing at the gear's idle speed, by the clutch's grip, its most less in the low range by the gear's top, whole in first and up; "
                  "both less as the pedal goes down");
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
            const LoopSpan handStart = LoopDrive(byHand, hs, hc, 0.0f, 30.0f, false);
            const LoopSpan handHeld = LoopDrive(byHand, hs, hc, 0.0f, 10.0f, false);
            LoopTruck none = crawl;
            none.creep = false;
            LoopState ns;
            LoopDrive(none, ns, hc, 0.0f, 10.0f, false, false);
            printf("     creep, the HX 520 in first on hard flat ground: in A %.2f to %.2f m/s (the gear's idle speed %.2f m/s), the needle %.0f rpm (idle %.0f); "
                   "on the brake %.2f m/s; by hand %.2f to %.2f m/s, the needle %.0f rpm; with Creep off %.2f m/s\n",
                   held.vLo, held.vHi, idleMs, creepRpm, crawl.k.idle, stopV, handHeld.vLo, handHeld.vHi, hs.tach.rpm, ns.v);
            Check(held.vLo > 0.3f * idleMs && held.vHi < kCouplingPoint * idleMs && held.vHi - held.vLo < 0.02f && creepGear == 1.0f && Near(creepRpm, crawl.k.idle, 15.0f) &&
                      stopV == 0.0f && stopShare == 0.0f && handHeld.vLo > 0.95f * idleMs && handHeld.vHi < 1.05f * idleMs && handHeld.vHi - handHeld.vLo < 0.02f &&
                      handStart.vHi < 1.3f * idleMs &&
                      Near(hs.tach.rpm, crawl.k.idle, 30.0f) && ns.v == 0.0f,
                  "loop: a heavy truck in first at zero pedal creeps in A under the gear's idle speed and holds it, the needle at idle, stops on the brake; "
                  "in first by hand from a standstill it holds the gear's idle speed with no overshoot past 1.3 of it (review 13's holding part); with Creep "
                  "off it stands");

            // CreepManual (the gearbox rebuild's step 4): off, a gear picked by hand gets no creep and R on the game's
            // shifter neither (its automatic flag is down); on, both, and A as ever. A held 4th rolling at idle on the
            // flat under today's lift by spin holds the gear's idle speed; on a grade past what the holding part gives it
            // lugs and stalls at Gearbox=2 and idles on at Gearbox 1; in Sequential the down to D comes first
            {
                const bool gateOff = !CreepGoes(false, false, false, kPosA, 0) && !CreepGoes(false, true, true, kPosGear, 0) && CreepGoes(false, true, true, kPosA, 0) &&
                                     CreepGoes(false, false, false, kPosA, 1);
                const bool gateOn = CreepGoes(true, false, false, kPosA, 0) && CreepGoes(true, true, true, kPosGear, 0) && CreepGoes(true, true, true, kPosLow, 0);
                LoopTruck fourth = crawl;
                fourth.fixedGear = 4;
                fourth.spinLift = true;
                TruckRaw at4 = fourth.t;
                at4.gear = at4.gearEngaged = 4;
                const float idle4 = fourth.k.idle / fourth.k.redline * TruckGearTop(at4, 4) / fourth.grip; // m/s at the 4th gear's idle speed
                LoopState fs;
                fs.v = idle4;
                LoopDrive(fourth, fs, hc, 0.0f, 10.0f, false, false);
                const LoopSpan rolling = LoopDrive(fourth, fs, hc, 0.0f, 10.0f, false);
                LoopTruck uphill = fourth;
                uphill.grade = 0.8f;
                LoopState us;
                us.v = idle4;
                LoopDrive(uphill, us, hc, 0.0f, 20.0f, false, false);
                LoopTruck uphill1 = uphill;
                uphill1.mayStall = false;
                LoopState u1;
                u1.v = idle4;
                LoopDrive(uphill1, u1, hc, 0.0f, 20.0f, false, false);
                // Sequential's down before a stall, by the gearbox's input stuck under 1.1 idle with the foot off
                SeqInputs si = {};
                si.backToA = 5;
                si.pos = kPosGear;
                si.gear = 4;
                si.top = 6;
                si.input = 0.5f * fourth.k.idle;
                si.redline = fourth.k.redline;
                si.idle = fourth.k.idle;
                si.engineOn = true;
                SeqModel sm = {};
                float seqAt = -1.0f;
                for (int i = 0; i < 60 && seqAt < 0.0f; i++)
                    if (SequentialStep(sm, si, kDt) < 0) seqAt = i * kDt;
                printf("     creep by hand: a held 4th at idle on the flat %.3f to %.3f of its idle speed; up a grade it stalls at %.1f s (Gearbox 1: %.1f); "
                       "Sequential goes to D after %.2f s\n", rolling.vLo / idle4, rolling.vHi / idle4, us.stallAt, u1.stallAt, seqAt);
                Check(gateOff && gateOn && rolling.vLo > 0.98f * idle4 && rolling.vHi < 1.03f * idle4 && us.stallAt > 0.0f && u1.stallAt < 0.0f && seqAt >= 0.0f &&
                          seqAt < 1.0f,
                      "creep in a hand-picked gear: off, none by hand and none for R on the game's shifter; on, both; a held 4th holds its idle speed under the "
                      "lift by spin, stalls stuck up a grade at Gearbox 2 and not at 1; in Sequential the down to D comes before the creep's stall");
            }

            // the brief's part 1, kept as a guard: the hook never writes the pedal field, so creeping at a standstill in A
            // (stuck: a grade nothing climbs) the needle stays at idle, the powertrain's pedal at 0 and nothing stalls; a
            // light press reads as the press alone
            LoopTruck wall = crawl;
            wall.grade = 100.0f;
            wall.pullLag = 0.5f;
            LoopState as;
            LoopDrive(wall, as, hc, 0.0f, 10.0f, false, false);
            const float wallRpm = as.tach.rpm, wallPedal = as.power.pedal, wallShare = as.share;
            LoopDrive(wall, as, hc, 0.1f, 3.0f, false, false);
            printf("     creep, stuck in A: the share %.3f, the needle %.0f rpm, the powertrain's pedal %.2f; pressed to 0.1 it reads %.3f (shaped 0.1 is %.3f)\n", wallShare,
                   wallRpm, wallPedal, as.power.pedal, ThrottleShape(0.1f));
            LoopState creepBoost;
            LoopDrive(wall, creepBoost, hc, 0.0f, 10.0f, false, false);
            Check(wallShare > 0.0f && Near(wallRpm, wall.k.idle, 1.0f) && wallPedal == 0.0f && Near(as.power.pedal, ThrottleShape(0.1f), 0.001f) && as.stallAt < 0.0f &&
                      creepBoost.boost.q == 0.0f && fabsf(creepBoost.boost.bar) < 0.005f,
                  "creep: stuck in A the needle stands at idle and the powertrain's pedal at 0, a press reads as the press alone, the turbo burns nothing and "
                  "makes no boost, and the converter never stalls it");

            // the governor in the low range, the game's pull half a second late (review 11's addendum to this brief): from
            // a standstill it settles under the gear's idle speed, no overshoot past 1.3 times it and no cycle; again with
            // the wheels free to spin, as in mud (ten times the push, little drag)
            auto governed = [&](float push, float drag0, float &most, float &spanLate, float &settled) {
                LoopTruck low = crawl;
                low.fixedGear = 1;
                low.t.powerFactor = 0.2f; // L-
                low.push = push;
                low.drag0 = drag0;
                low.pullLag = 0.5f;
                const float lowIdle = low.k.idle / low.k.redline * TruckGearTop(low.t, 1) / low.grip;
                LoopState ls;
                const LoopSpan early = LoopDrive(low, ls, hc, 0.0f, 30.0f, false);
                const LoopSpan late = LoopDrive(low, ls, hc, 0.0f, 10.0f, false);
                most = fmaxf(early.vHi, late.vHi) / lowIdle;
                spanLate = (late.vHi - late.vLo) / lowIdle;
                settled = ls.v / lowIdle;
                return ls.stallAt < 0.0f;
            };
            float firmMost, firmSpan, firmAt, mudMost, mudSpan, mudAt;
            const bool firmAlive = governed(8.0f, 0.1f, firmMost, firmSpan, firmAt), mudAlive = governed(80.0f, 0.02f, mudMost, mudSpan, mudAt);
            printf("     creep, by hand in L-: on firm ground at most %.2f of the idle speed, %.2f at the end, the last 10 s within %.3f; the wheels free to spin: at most "
                   "%.2f, %.2f at the end, within %.3f\n", firmMost, firmAt, firmSpan, mudMost, mudAt, mudSpan);
            Check(firmAlive && mudAlive && firmMost < 1.3f && mudMost < 1.3f && firmSpan < 0.05f && mudSpan < 0.05f && Near(firmAt, 1.0f, 0.05f) && Near(mudAt, 1.0f, 0.05f),
                  "loop: the governor in the low range settles at the gear's idle speed with the game's pull half a second late, no overshoot past 1.3 times "
                  "it and no cycle, on firm ground and with the wheels free to spin");

            // stuck in first by hand the engine stalls a second after the game's pull has built; not with the clutch held,
            // not in A (above), not at the gear's idle speed (byHand above), and not for a slow truck pulling away (a Zetros:
            // its pull three seconds late)
            LoopTruck stuckHand = crawl;
            stuckHand.fixedGear = 1;
            stuckHand.grade = 100.0f;
            stuckHand.pullLag = 0.5f;
            LoopState ss;
            float builtAt = -1.0f;
            for (float at = 0.0f; at < 10.0f && ss.stallAt < 0.0f; at += kDt)
            {
                LoopStep(stuckHand, ss, hc, 0.0f, kDt, false);
                if (builtAt < 0.0f && ss.pull >= 0.95f * kIdleShare * stuckHand.lowPull) builtAt = ss.at;
            }
            LoopTruck heldClutch = stuckHand;
            heldClutch.clutchGrip = 0.0f;
            LoopState cl;
            LoopDrive(heldClutch, cl, hc, 0.0f, 10.0f, false, false);
            LoopTruck zetros = crawl;
            zetros.fixedGear = 1;
            zetros.pullLag = 3.0f;
            LoopState zs;
            LoopDrive(zetros, zs, hc, 0.0f, 20.0f, false, false);
            printf("     creep, stuck in first by hand: the pull built at %.2f s, the engine stalled at %.2f s; with the clutch held no stall (%.2f); a slow truck "
                   "pulling away at %.2f m/s after 20 s, no stall (%.2f), its rise over the lead at most %.3f of the idle speed (the look-ahead's weight from 0.05); at the gear's "
                   "idle speed none (%.2f)\n",
                   builtAt, ss.stallAt, cl.stallAt, zs.v, zs.stallAt, zs.creep.riseMost, hs.stallAt);
            Check(builtAt > 0.0f && ss.stallAt > builtAt + 0.5f && ss.stallAt < builtAt + 2.0f && cl.stallAt < 0.0f && zs.stallAt < 0.0f && zs.v > 0.5f * idleMs &&
                      hs.stallAt < 0.0f && as.stallAt < 0.0f,
                  "creep: stuck in a gear picked by hand the engine stalls about a second after the game's pull has built; not with the clutch held, in A, "
                  "at the gear's idle speed or for a slow truck pulling away");
            // the creep holds, it does not switch (BRIEF_drive_1115_findings.md, 204, the Reviewer's addendum of 02:30). The
            // plant: the truck's wheel speed v rises by the game's pull times a full-share acceleration against a steady
            // resistance; the game's pull follows the share through a first-order lag; the wheels the creep reads carry
            // white noise of 1.5 rad/s from end to end on an idle wheel speed of 0.93 rad/s (L), at 60 and at 120 passes
            // a second. The old law (the pass's own speed, the governor's 0.6 s look-ahead unheld, its fall at once, the
            // converter at once) is run beside it in the same plant and relays; the new one holds.
            {
                TruckRaw lt = Truck();
                Drive(lt, 1, false, 0.0f, 0.0f);
                GaugeScale lk = ck;
                lk.idle = lk.redline * 0.93f / TruckGearTop(lt, 1); // the idle wheel speed of L in the drive's log
                const float idleL = CreepIdleSpin(lt, lk);
                struct Run { float relays, worstSwing, worstSpeed, backIn, held, mean; bool fastDrop; };
                auto run = [&](int rate, bool converter, bool old, float stepAt) {
                    const float dt = 1.0f / (float)rate, full = 1.0f * idleL, resist = 0.25f * full, lag = 0.3f;
                    CreepModel cm = {};
                    float v = 0.0f, pull = 0.0f, oldShare = 0.0f, oldRate = 0.0f, oldWas = -1.0f, lastHigh = -10.0f, winLo = 1.0f, winHi = 0.0f, vLo = 1e9f, vHi = -1e9f, winAt = 0.0f;
                    float heldSum = 0.0f;
                    int heldN = 0;
                    unsigned seed = 12345u;
                    Run out = { 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, false };
                    float meanSum = 0.0f;
                    int meanN = 0;
                    bool high = false;
                    const float most = converter ? kCreepShare : kIdleShare * 1.0f, pass = 0.05f; // the mod's thread: 20 passes a second
                    float share = 0.0f, nextPass = 0.0f;
                    for (int i = 0; i < rate * 16; i++)
                    {
                        const float at = (float)i * dt;
                        if (at + 1e-5f >= nextPass)
                        {
                        nextPass += pass;
                        seed = seed * 1664525u + 1013904223u;
                        const float noise = ((float)(seed >> 8) / 16777216.0f - 0.5f) * 1.5f;
                        for (int w = 0; w < lt.wheels; w++) lt.spin[w] = v + noise;
                        lt.speed = v * 0.45f;
                        if (old)
                        {
                            // the old law, as 9084d63 had it
                            const float raw = TruckDrivenSpin(lt);
                            if (oldWas >= 0.0f) oldRate += ((raw - oldWas) / pass - oldRate) * (1.0f - expf(-pass / kIdleRateLag));
                            oldWas = raw;
                            const float target = CreepShare(lt, lk, converter, 1.0f, nullptr, oldRate * 0.6f, false);
                            oldShare = converter || target <= oldShare ? target : fminf(target, oldShare + 0.3f * pass);
                            share = oldShare;
                        }
                        else
                        {
                            bool stall = false;
                            share = CreepStep(cm, lt, lk, converter, 1.0f, pass, nullptr, stall, 0.0f, false);
                        }
                        }
                        // the plant
                        pull += (share / most - pull) * (1.0f - expf(-dt / lag));
                        const float load = resist + (stepAt > 0.0f && at >= stepAt ? 0.05f * full : 0.0f);
                        v = fmaxf(0.0f, v + (pull * full - load) * dt);
                        // the measures, from 3 s
                        // a relay: from over half its most to under a tenth of it (the second answer, 02:49), counted after 3 s
                        if (share > 0.5f * most) lastHigh = at, high = true;
                        if (share < 0.1f * most && high)
                        {
                            if (at >= 3.0f) out.relays += 1.0f;
                            high = false;
                            if (at - lastHigh < 0.5f && at >= 3.0f) out.fastDrop = true;
                        }
                        if (at >= 3.0f && (stepAt <= 0.0f || at < stepAt)) meanSum += v, meanN++;
                        if (at >= 3.0f && (stepAt <= 0.0f || at < stepAt))
                        {
                            winLo = fminf(winLo, share), winHi = fmaxf(winHi, share), vLo = fminf(vLo, v), vHi = fmaxf(vHi, v);
                            if (at - winAt >= 1.0f)
                            {
                                out.worstSwing = fmaxf(out.worstSwing, winHi - winLo), out.worstSpeed = fmaxf(out.worstSpeed, vHi - vLo);
                                winLo = 1.0f, winHi = 0.0f, vLo = 1e9f, vHi = -1e9f, winAt = at;
                            }
                            if (at >= stepAt - 2.0f) heldSum += v, heldN++;
                        }
                        else if (at < 3.0f) winAt = at;
                        if (stepAt > 0.0f && at >= stepAt && out.backIn < 0.0f && heldN > 0)
                        {
                            const float held = heldSum / (float)heldN;
                            out.held = held;
                            if (at > stepAt + 0.2f && fabsf(v - held) <= 0.2f * held) out.backIn = at - stepAt;
                        }
                    }
                    out.relays /= 13.0f; // a second, from 3 s to the end
                    out.mean = meanN ? meanSum / (float)meanN / idleL : 0.0f;
                    out.worstSwing /= most;
                    out.worstSpeed /= idleL;
                    return out;
                };
                bool holds = true, oldFails = true;
                char creepLine[900] = "";
                int at = 0;
                for (int rate : { 60, 120 })
                    for (bool converter : { false, true })
                    {
                        const Run now = run(rate, converter, false, 0.0f), was = run(rate, converter, true, 0.0f), grade = run(rate, converter, false, 12.0f);
                        holds = holds && now.mean >= 0.5f && now.relays <= 1.0f && now.worstSpeed < 0.3f && grade.backIn >= 0.0f && grade.backIn <= 1.5f; // the share's swing printed
                        oldFails = oldFails && (was.mean < 0.5f || was.relays > 1.0f);
                        at += snprintf(creepLine + at, sizeof creepLine - (size_t)at, "%s%d %s: mean %.2f of idle, %.2f relays a second, swing %.2f, share swing %.2f, grade back in %.2f s (old: mean %.2f, %.1f relays, swing %.2f)",
                                       at ? "; " : "", rate, converter ? "converter" : "governor", now.mean, now.relays, now.worstSpeed, now.worstSwing, grade.backIn, was.mean, was.relays, was.worstSpeed);
                        if (at >= (int)sizeof creepLine) at = (int)sizeof creepLine - 1;
                    }
                // a brake past kCreepBrake gives 0 on the same pass
                CreepModel bm = {};
                bool bs = false;
                for (int w = 0; w < lt.wheels; w++) lt.spin[w] = 0.0f;
                lt.speed = 0.0f;
                for (int i = 0; i < 40; i++) CreepStep(bm, lt, lk, true, 1.0f, 0.05f, nullptr, bs, 0.0f, false);
                const float givenBefore = bm.given;
                lt.throttle = 0.5f; // the brake (TruckBrake: a throttle over 0 in a forward gear)
                const float brakedNow = CreepStep(bm, lt, lk, true, 1.0f, 0.05f, nullptr, bs, 0.0f, false);
                lt.throttle = 0.0f;
                printf("     creep on noisy wheels: %s; braked %.3f from %.3f\n", creepLine, brakedNow, givenBefore);
                Check(holds && oldFails && brakedNow == 0.0f && givenBefore > 0.0f,
                      "creep holds on wheels with 1.5 rad/s of noise on 0.93 rad/s idle, at 60 and at 120, both branches: the truck creeps at half the idle speed or more, at most one relay "
                      "a second, its speed within 0.3 of the idle speed each second, back within 20 percent 1.5 s after a grade; the old law stands or relays; the brake gives 0 at once");
            }
            // the stall in the model: once, the creep off until the engine is off
            {
                CreepModel cm = {};
                TruckRaw st = Truck();
                Drive(st, 1, false, 0.0f, 0.0f);
                st.actionB4 = 0.01f;
                bool stalled = false, again = false, any = false;
                float after = 1.0f;
                for (float at = 0.0f; at < 3.0f; at += kDt)
                {
                    bool now = false;
                    const float sh = CreepStep(cm, st, ck, false, 1.0f, kDt, nullptr, now);
                    if (now && stalled) again = true;
                    if (now) stalled = true;
                    if (stalled && !now) after = fminf(after, sh);
                }
                st.flags = 0;
                CreepStep(cm, st, ck, false, 1.0f, kDt, nullptr, any);
                st.flags = 3;
                CreepStep(cm, st, ck, false, 1.0f, kDt, nullptr, any);
                Check(stalled && !again && after == 0.0f && !cm.stalled && cm.share > 0.0f,
                      "creep: its stall comes once and the creep is off until the engine is off; running again it creeps");
            }

            // review 13: the holding part in a tall gear. The HX 520 in 4th by hand (its top 33 rad/s) rolling at the gear's
            // idle speed on the flat holds it; a step grade past its torque at idle lugs it under 0.6 of it and it stalls
            // about a second later; from a standstill it never moves off (the game's pull at a standstill 0.3 of its
            // curve's, as the crawl) and stalls as before (the addendum's A)
            {
                LoopTruck tall = crawl;
                tall.fixedGear = 4;
                tall.pullLag = 0.5f;
                const float tallIdle = tall.k.idle / tall.k.redline * TruckGearTop(tall.t, 4) / tall.grip;
                LoopState fl;
                fl.v = tallIdle;
                LoopDrive(tall, fl, hc, 0.0f, 10.0f, false, false);
                const LoopSpan flat = LoopDrive(tall, fl, hc, 0.0f, 20.0f, false);
                const bool flatArmed = fl.creep.armed && fl.stallAt < 0.0f;
                // with Powertrain off the holding part's most is the curve's torque at idle
                LoopTruck tallOff = tall;
                tallOff.holdMost = TorqueAt(hc, tall.k.idle / tall.k.redline, tall.k.max / tall.k.redline);
                LoopState fo;
                fo.v = tallIdle;
                LoopDrive(tallOff, fo, hc, 0.0f, 10.0f, false, false);
                const LoopSpan flatOff = LoopDrive(tallOff, fo, hc, 0.0f, 20.0f, false);
                // the grade: how much the holding part's most gives in 4th at its idle speed, and half as much again
                LoopTruck uphill = tall;
                uphill.grade = 1.5f * tall.push * GamePull(tall.k.idle / tall.k.redline) * TruckGearTop(tall.t, 1) / TruckGearTop(tall.t, 4);
                LoopState gr = fl;
                float under = -1.0f;
                for (float at = 0.0f; at < 15.0f && gr.stallAt < 0.0f; at += kDt)
                {
                    LoopStep(uphill, gr, hc, 0.0f, kDt, false);
                    if (under < 0.0f && gr.v < kHoldStall * tallIdle) under = gr.at;
                }
                const bool armedStall = gr.stallAt > 0.0f && under > 0.0f && gr.creep.stallArmed;
                // from a standstill in 4th
                LoopState st;
                float moved = 0.0f;
                for (float at = 0.0f; at < 15.0f && st.stallAt < 0.0f; at += kDt)
                {
                    LoopStep(tall, st, hc, 0.0f, kDt, false);
                    moved = fmaxf(moved, st.v);
                }
                printf("     creep, the HX 520 in 4th by hand: on the flat %.3f to %.3f of its idle speed (%.2f m/s), armed %d; a grade of %.2f m/s2: under 0.6 at %.1f s, "
                       "stalled at %.1f s (armed %d); from a standstill at most %.3f of the idle speed, stalled at %.1f s (armed %d)\n",
                       flat.vLo / tallIdle, flat.vHi / tallIdle, tallIdle, fl.creep.armed, uphill.grade, under, gr.stallAt, gr.creep.stallArmed,
                       moved / tallIdle, st.stallAt, st.creep.stallArmed);
                printf("     creep, 4th with Powertrain off (the most %.2f): %.3f to %.3f of the idle speed\n", tallOff.holdMost, flatOff.vLo / tallIdle, flatOff.vHi / tallIdle);
                Check(flatArmed && flat.vLo > 0.95f * tallIdle && flat.vHi < 1.05f * tallIdle && flat.vHi - flat.vLo < 0.05f * tallIdle && flatOff.vLo > 0.95f * tallIdle &&
                          flatOff.vHi < 1.05f * tallIdle && fo.stallAt < 0.0f,
                      "creep: rolling in a tall gear by hand on the flat the truck holds the gear's idle speed, with no cycle over 20 s; with Powertrain off too");
                Check(armedStall && gr.stallAt - under > 0.5f && gr.stallAt - under < 2.5f,
                      "creep: in the mod's own gearbox a grade past the engine's torque at idle lugs the truck under 0.6 of the gear's idle speed, and the engine "
                      "stalls about a second later");
                Check(moved < kCreepStuck * tallIdle && st.stallAt > 0.0f && !st.creep.stallArmed,
                      "creep: from a standstill in a tall gear the truck does not move off at idle, the holding part stays unarmed, and the engine stalls as before");
                // the author's drive of BF57872B: at Gearbox 0 and 1 (the game's gears) no stall comes. The same grade in 4th,
                // and stuck in R, L- and L+ picked on the game's shifter: the governor pushes, the engine idles on, the
                // needle (the game's gears' TachTarget) stays at idle
                LoopTruck gameUphill = uphill;
                gameUphill.mayStall = false;
                LoopState gg = fl;
                float gameNeedle = 1e9f, gameShare = 0.0f;
                for (float at = 0.0f; at < 15.0f; at += kDt)
                {
                    LoopStep(gameUphill, gg, hc, 0.0f, kDt, false);
                    gameNeedle = fminf(gameNeedle, gg.tach.rpm);
                    gameShare = fmaxf(gameShare, gg.share);
                }
                bool stuckQuiet = true;
                float stuckNeedle = 1e9f;
                for (int which = 0; which < 3; which++)
                {
                    LoopTruck stuck = crawl;
                    stuck.mayStall = false;
                    stuck.grade = 100.0f;
                    stuck.fixedGear = which == 0 ? -1 : 1;
                    stuck.t.powerFactor = which == 1 ? 0.2f : 1.0f; // R, L-, L+ (the game's L+ is its first gear: the governor)
                    stuck.shifterConverter = which < 2;
                    stuck.pullLag = 0.5f;
                    LoopState ss2;
                    for (float at = 0.0f; at < 10.0f; at += kDt)
                    {
                        LoopStep(stuck, ss2, hc, 0.0f, kDt, false);
                        if (at > 2.0f) stuckNeedle = fminf(stuckNeedle, ss2.tach.rpm);
                    }
                    stuckQuiet = stuckQuiet && ss2.stallAt < 0.0f && ss2.share > 0.0f;
                }
                printf("     creep at Gearbox 0 and 1: the grade in 4th, no stall (%.1f), the governor at most %.3f, the needle at least %.0f rpm; stuck in R, L- and L+: "
                       "no stall %d, the needle at least %.0f rpm (idle %.0f)\n", gg.stallAt, gameShare, gameNeedle, stuckQuiet, stuckNeedle, tall.k.idle);
                Check(gg.stallAt < 0.0f && gameShare > 0.5f && gameNeedle >= tall.k.idle - 1.0f && stuckQuiet && stuckNeedle >= tall.k.idle - 1.0f,
                      "creep: in the game's gears (Gearbox 0 and 1) no stall comes: lugged in a tall gear, or stuck in R and L- (through the converter) or L+, the "
                      "creep pushes, the engine idles on and the needle stays at idle");
                // part E: R and L- on the game's shifter from a standstill creep through the converter, calmed: no overshoot
                // past 1.3 of the gear's idle speed, no cycle over 10 s, under the coupling point
                auto shifterCreep = [&](int gear, float powerFactor, float &most, float &span, float &settled, float &share) {
                    LoopTruck sc = crawl;
                    sc.mayStall = false;
                    sc.fixedGear = gear;
                    sc.t.powerFactor = powerFactor;
                    sc.shifterConverter = true;
                    sc.pullLag = 0.5f;
                    const float idleSpeed = sc.k.idle / sc.k.redline * TruckGearTop(sc.t, gear) / sc.grip;
                    LoopState cs2;
                    const LoopSpan early = LoopDrive(sc, cs2, hc, 0.0f, 30.0f, false);
                    const LoopSpan late = LoopDrive(sc, cs2, hc, 0.0f, 10.0f, false);
                    most = fmaxf(early.vHi, late.vHi) / idleSpeed;
                    span = (late.vHi - late.vLo) / idleSpeed;
                    settled = cs2.v / idleSpeed;
                    share = cs2.share;
                    return cs2.stallAt < 0.0f && !cs2.creep.armed;
                };
                float rMost, rSpan, rAt, rShare, lMost, lSpan, lAt, lShare;
                const bool rQuiet = shifterCreep(-1, 1.0f, rMost, rSpan, rAt, rShare), lQuiet = shifterCreep(1, 0.2f, lMost, lSpan, lAt, lShare);
                printf("     creep through the converter on the game's shifter: R at most %.2f of its idle speed, %.2f at the end, within %.3f; L- at most %.2f, %.2f, "
                       "within %.3f\n", rMost, rAt, rSpan, lMost, lAt, lSpan);
                Check(rQuiet && lQuiet && rMost < 1.3f && lMost < 1.3f && rSpan < 0.05f && lSpan < 0.05f && rAt > 0.3f && lAt > 0.3f && rAt < kCouplingPoint + 0.02f &&
                          lAt < kCouplingPoint + 0.02f,
                      "creep: R and L- on the game's shifter creep through the converter from a standstill, under the coupling point, no overshoot past 1.3 of "
                      "the gear's idle speed and no cycle, with no holding part and no stall");
            }
            // a shift: no stuck time counts within a second of it, at every level; at Gearbox 2 still stuck after it, the
            // engine stalls. Stuck in 3rd from the start (the game's pull steady), then a shift into 4th at 0.4 s
            {
                const GaugeScale sk = kClassScales[kClassTruck];
                auto stallAt = [&](bool shift, bool mayStall) {
                    CreepModel sm = {};
                    TruckRaw sh = Truck();
                    Drive(sh, 3, false, 0.0f, 0.0f);
                    sh.actionB4 = 0.01f;
                    for (float at = kDt; at < 5.0f; at += kDt)
                    {
                        if (shift && at >= 0.4f && sh.gearEngaged == 3) Drive(sh, 4, false, 0.0f, 0.0f);
                        bool now = false;
                        CreepStep(sm, sh, sk, false, 1.0f, kDt, nullptr, now, 0.6f, mayStall);
                        if (now) return at;
                    }
                    return -1.0f;
                };
                const float plain = stallAt(false, true), shifted = stallAt(true, true), game = stallAt(true, false);
                printf("     creep, stuck at Gearbox 2: stalled at %.2f s; with a shift at 0.4 s at %.2f s; at Gearbox 1 none (%.2f)\n", plain, shifted, game);
                Check(plain > 0.0f && shifted >= 0.4f + kShiftQuiet + kCreepGrace - kDt - 0.01f && shifted > plain && shifted < 3.5f && game < 0.0f,
                      "creep: no stall within a second of a shift; still stuck after it in the mod's own gearbox the engine stalls, and at Gearbox 1 never");
            }
            // 0.18.2's hotfix: the walk of the author's repro, L+ to L to L- (gear 1 at three powers) with the truck
            // rolling at L+'s idle speed, then N. Each change of range is a shift: nothing for kCreepQuiet after it,
            // and in L- (the wheels at about four times its idle speed) the holding part never arms or pushes
            {
                const GaugeScale wk = kClassScales[kClassTruck];
                CreepModel wm = {};
                TruckRaw wt = Truck();
                Drive(wt, 1, false, wk.idle / wk.redline, 0.0f);
                wt.powerFactor = 1.0f;
                bool any = false, quietHeld = true, overHeld = true, armedOnce = false;
                float at = 0.0f, quietUntil = -1.0f;
                auto run = [&](float until) {
                    for (; at < until; at += kDt)
                    {
                        const float sh = CreepStep(wm, wt, wk, false, 1.0f, kDt, nullptr, any, 0.6f, true);
                        if (at < quietUntil && sh > 0.0f) quietHeld = false;
                        armedOnce = armedOnce || wm.armed;
                        if (wt.powerFactor < 0.3f && at >= quietUntil && (sh > 0.0f || wm.armed)) overHeld = false;
                    }
                };
                run(3.0f);
                const bool heldInLPlus = armedOnce;
                wt.powerFactor = 0.5f; // L
                quietUntil = at + kCreepQuiet - kDt;
                run(4.0f);
                wt.powerFactor = 0.27f; // L-
                quietUntil = at + kCreepQuiet - kDt;
                run(6.0f);
                Drive(wt, 0, false, 0.0f, 0.0f); // N
                wt.powerFactor = 1.0f;
                const float inN = CreepStep(wm, wt, wk, false, 1.0f, kDt, nullptr, any, 0.6f, true);
                // round 2: A standing on the game's gear 0, then R, then back to A moving off in gear 1, at Gearbox 2
                // and 1. R goes through the converter at both (gauges.cpp), with no stall. Nothing within kCreepQuiet
                // of each change, the converter's creep in R after it, and in A again
                bool walkHeld = true;
                float reverseShare = 1.0f;
                for (int level = 1; level <= 2; level++)
                {
                    CreepModel am = {};
                    TruckRaw atr = Truck();
                    Drive(atr, 0, true, 0.0f, 0.0f);
                    atr.powerFactor = 1.0f;
                    float clock = 0.0f, changedAt = -100.0f, afterShare = 0.0f, inReverse = 0.0f;
                    bool stall = false;
                    auto step = [&](bool converter, float until) {
                        for (; clock < until; clock += kDt)
                        {
                            const float sh = CreepStep(am, atr, wk, converter, 1.0f, kDt, nullptr, stall, 0.6f, level == 2 && !converter);
                            if (atr.gearEngaged < 0 && clock > changedAt + kCreepQuiet + kDt) inReverse = fmaxf(inReverse, sh);
                            if (clock < changedAt + kCreepQuiet - kDt && sh != 0.0f) walkHeld = false;
                            if (atr.gearEngaged == 1 && clock > changedAt + kCreepQuiet + kDt) afterShare = fmaxf(afterShare, sh);
                            if (stall) walkHeld = false;
                        }
                    };
                    step(true, 2.0f);
                    Drive(atr, -1, level == 1, 0.0f, 0.0f); // R
                    changedAt = clock;
                    step(true, 6.0f);
                    Drive(atr, 1, true, 0.0f, 0.0f); // back to A, moving off in gear 1
                    changedAt = clock;
                    step(true, 10.0f);
                    walkHeld = walkHeld && afterShare > 0.0f;
                    reverseShare = fminf(reverseShare, inReverse);
                }
                walkHeld = walkHeld && reverseShare > 0.0f;
                Check(heldInLPlus && quietHeld && overHeld && inN == 0.0f && !wm.armed && walkHeld,
                      "creep (0.18.2): L+ to L to L- and N each a shift, nothing for 1.5 s after each; in L- at four times its idle speed the holding part "
                      "never arms or pushes; A on gear 0 to R and back at Gearbox 2 and 1: nothing within 1.5 s of a change, then the converter's creep in R and in A");
            }
            // the needle at zero pedal in a gear of the game's own (Gearbox 0 and 1): never under idle, as no stall comes
            // there; over idle with the wheels and with the pedal down as before
            {
                const GaugeScale nk = kClassScales[kClassTruck];
                TruckRaw nt = Truck();
                auto needleAt = [&](float ofIdle, bool automatic, float pedal) {
                    Drive(nt, 4, automatic, ofIdle * nk.idle / nk.redline, pedal);
                    return TachTarget(nt, nk);
                };
                const float lug = needleAt(0.8f, false, 0.0f), deep = needleAt(0.4f, false, 0.0f), standing = needleAt(0.1f, false, 0.0f),
                            automaticNeedle = needleAt(0.8f, true, 0.0f), pressed = needleAt(0.8f, false, 0.3f), over = needleAt(1.2f, false, 0.0f);
                Check(Near(lug, nk.idle, 1.0f) && Near(deep, nk.idle, 1.0f) && Near(standing, nk.idle, 1.0f) && Near(automaticNeedle, nk.idle, 1.0f) &&
                          pressed > nk.idle && Near(over, 1.2f * nk.idle, 1.0f),
                      "needle: at zero pedal in a gear of the game's own it stays at idle with the wheels under it, as no stall comes there; over idle with the "
                      "wheels and with the pedal down as before");
            }
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
            Check(ramp && !slip.locked && Near(slip.rpm, slip.input * (1.0f + kConverterSlip), 1.0f) && slip.rpm > slip.input && creep.ratio < creepStand.ratio &&
                      Near(creepStand.ratio, kStallRatio, 0.001f),
                  "converter: the multiplication falls in a line from its stall to 1 at the coupling point; open and rolling the engine runs a little ahead of the "
                  "input; at idle the converter multiplies less as the truck rolls");
            // the converter as the cut reads it (M4): a shift made locked opens the model for its own pass alone, and the
            // cut still reads locked there, so the slot never goes to the scalar and back within two passes
            {
                GearboxModel sb = boxIn(2, rollAt(2, tk.redline * 1.04f), 1.0f);
                sb.locked = true;
                bool shifted = false, openSeen = false;
                for (int i = 0; i < 80 && !shifted; i++)
                {
                    GearboxStep(sb, lt, tk, 1.0f, 1.0f, kDt, handOf(kInputPad));
                    shifted = sb.gear != 2;
                    openSeen = openSeen || !ConverterLockedForCut(sb);
                }
                const bool onPass = shifted && sb.gear == 3 && !sb.locked && sb.relock && ConverterLockedForCut(sb) && !openSeen;
                rollAt(3, GearboxInput(lt, tk, 3, sb.spin));
                GearboxStep(sb, lt, tk, 1.0f, 1.0f, kDt, handOf(kInputPad));
                const bool after = sb.locked && !sb.relock && ConverterLockedForCut(sb);
                GearboxModel openBox = boxIn(1, rollAt(1, 0.0f), 1.0f);
                GearboxStep(openBox, lt, tk, 1.0f, 1.0f, kDt, handOf(kInputPad));
                if (!(onPass && after)) printf("     M4: shifted %d gear %d locked %d relock %d openSeen %d after %d\n", shifted, sb.gear, sb.locked, sb.relock, openSeen, after);
                Check(onPass && after && !ConverterLockedForCut(openBox),
                      "converter under the cut: on the pass of a shift made locked the model is open and the cut reads it locked, so no scalar goes out; it is locked again on the next pass; "
                      "a launch from a standstill is open");
            }
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
        // review 12, finding 1 (in place of review 11's finding 16): the engine has the torque the air allows, the
        // turbo's limit as BoostStep gives it; a limit that is no number is all of it
        {
            TruckRaw on = lt;
            on.flags = 3;
            PowertrainModel none = {}, full = {}, wild = {};
            PowertrainStep(none, on, tc, 0.6f, tk.max / tk.redline, 80.0f, 0.43f, kDt);
            PowertrainStep(full, on, tc, 0.6f, tk.max / tk.redline, 80.0f, 1.0f, kDt);
            PowertrainStep(wild, on, tc, 0.6f, tk.max / tk.redline, 80.0f, nanf(""), kDt);
            Check(Near(none.boost, 0.43f, 0.001f) && Near(full.boost, 1.0f, 0.001f) && wild.boost == 1.0f,
                  "powertrain: the engine has the torque the air allows, at a first instant's 0.43 that much, settled all of it");
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
                for (float at = 0.0f; at < seconds && !got; at += kDt) got = GearTopStep(l, t, kDt, 0.0f, gear, top);
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
            learned.gearTop[3] = top / formula; // kept as its share of 2a + 5 (BRIEF_gear_tops_per_truck.md)
            const float upBefore = GearboxUpPoint(pt, tk, 3, 1.0f, handOf(kInputPad)), upAfter = GearboxUpPoint(learned, tk, 3, 1.0f, handOf(kInputPad));
            const bool used = Near(TruckGearTop(learned, 3), top, 0.001f) && Near(TruckGearTop(learned, 2), TruckGearTop(pt, 2), 0.001f) && upAfter != upBefore &&
                              Near(GearTopMerge(0.93f, 0.97f), 0.95f, 0.001f) && Near(GearTopMerge(0.0f, 0.97f), 0.97f, 0.001f);
            printf("     finding 8: a plateau in third at %.2f rad/s (2a + 5 gives %.2f): learned; the floored point up from %.0f to %.0f rpm\n", top, formula, upBefore, upAfter);
            Check(seen && refused && used,
                  "gearbox: a floored plateau gives a gear's top, which the limiter and the floored points go by; none with the pull on, a light pedal, the low range, "
                  "in H or under a second; a second plateau gives the mean");
        }

        // BRIEF_gear_tops_per_truck.md: a top kept as its share of 2a + 5, used only inside 0.85 to 1.15; a plateau far from
        // the kept share replaces it, near it the mean; none down a slope; the stuck rule; the drag read's reasons
        {
            TruckRaw ga = lt, gb = lt;
            ga.gearSpeed[4] = 8.0f;  // one truck's gear 4 (2a + 5 = 21.0)
            gb.gearSpeed[4] = 2.8f;  // another's of the same gearbox name (10.6)
            ga.gearTop[4] = gb.gearTop[4] = 1.005f;
            const bool byShare = Near(TruckGearTop(ga, 4), 21.105f, 0.01f) && Near(TruckGearTop(gb, 4), 10.653f, 0.01f);
            TruckRaw wild = gb;
            wild.gearTop[4] = 1.99f; // 21.1 over 10.6, as an absolute top read as a share would be
            const bool notUsed = Near(TruckGearTop(wild, 4), 10.6f, 0.001f) && !GearTopShareUsable(1.99f) && !GearTopShareUsable(0.0f) &&
                                 !GearTopShareUsable(nanf("")) && GearTopShareUsable(0.85f) && GearTopShareUsable(1.15f);
            Check(byShare && notUsed,
                  "gear tops: a share learned at a = 8.0 (x1.005) gives 10.65 rad/s at a = 2.8, not 21.1; a share outside 0.85 to 1.15 (1.99) is not used: 2a + 5");
            // the merge by shares: 21.1 kept against an 11.0 plateau at 2a + 5 = 10.6 gives 11.0; within 15 % the mean
            const float kept = 21.1f / 10.6f, plateau = 11.0f / 10.6f;
            const bool replaced = Near(GearTopMerge(kept, plateau) * 10.6f, 11.0f, 0.001f) && GearTopFar(1.10f, 0.90f) &&
                                  Near(GearTopMerge(1.10f, 0.90f), 0.90f, 0.0001f);
            const bool meaned = !GearTopFar(1.00f, 1.10f) && Near(GearTopMerge(1.00f, 1.10f), 1.05f, 0.0001f) && Near(GearTopMerge(0.0f, 0.97f), 0.97f, 0.0001f);
            Check(replaced && meaned, "gear tops: a plateau more than 15 % from the kept share replaces it (21.1 kept, 11.0 found: 11.0), within 15 % the mean, the first as it is");
        }
        {
            // the plateau's grade: on the flat it learns, down or up a 3 % slope and with the grade unknown it does not
            TruckRaw pt = lt;
            pt.flags = 3;
            pt.gear = pt.gearEngaged = 3;
            pt.throttle = -1.0f;
            pt.actionB4 = 0.0f;
            const float formula = TruckGearFormulaTop(pt, 3);
            for (int i = 0; i < pt.wheels; i++) pt.spin[i] = 1.03f * formula;
            auto learns = [&](float grade) {
                GearTopLearn l = {};
                int gear = 0;
                float top = 0.0f;
                bool got = false;
                for (float at = 0.0f; at < 1.5f && !got; at += kDt) got = GearTopStep(l, pt, kDt, grade, gear, top);
                return got;
            };
            Check(learns(0.0f) && learns(0.03f) && !learns(-0.05f) && !learns(0.05f) && !learns(nanf("")),
                  "gear tops: a plateau is learned on ground within 2 degrees by the path, not down or up a steeper slope and not with the grade unknown");
        }
        {
            // the stuck rule: in A, floored in 3rd, the game's pull at nothing and the wheels steady at 0.8 of what the mod
            // takes the top for (a wrong top), it goes up one after kTopStuckFor; not with the pull on, not under the gear's
            // hold, not in the top gear
            const EngineHand h = handOf(kInputPad);
            auto stuckRun = [&](int gear, float pull, float moving, float seconds, int &ups, bool armed = true, bool pullHeld = false, bool *seen = nullptr) {
                TruckRaw st = lt;
                st.flags = 3;
                st.gear = st.gearEngaged = gear;
                st.throttle = -1.0f;
                st.actionB4 = pull;
                const float spin = 0.8f * TruckGearTop(st, gear);
                for (int i = 0; i < st.wheels; i++) st.spin[i] = spin;
                st.speed = spin / 2.0f;
                GearboxModel b = boxIn(gear, spin, 1.0f);
                b.moving = moving;
                b.holdUp = true;
                b.pullSeen = armed;
                for (float at = 0.0f; at < seconds && b.gear == gear; at += kDt) GearboxStep(b, st, tk, 1.0f, 1.0f, kDt, h, false, pullHeld);
                ups = b.topUps;
                if (seen) *seen = b.pullSeen;
                return b.gear;
            };
            int u1 = 0, u2 = 0, u3 = 0, u4 = 0, u5 = 0, u6 = 0;
            const int up = stuckRun(3, 0.0f, 10.0f, 3.0f, u1), early = stuckRun(3, 0.0f, 10.0f, 1.0f, u2), pulling = stuckRun(3, 0.3f, 10.0f, 3.0f, u3);
            const int heldGear = stuckRun(3, 0.0f, 0.0f, 1.5f, u4), topGear = stuckRun(TruckTopGear(lt), 0.0f, 10.0f, 3.0f, u5);
            const int unarmed = stuckRun(3, 0.0f, 10.0f, 3.0f, u6, false);
            printf("     stuck rule: up %d (%d), early %d (%d), pulling %d (%d), held %d (%d), top gear %d of %d (%d), unarmed %d (%d)\n", up, u1, early, u2, pulling, u3,
                   heldGear, u4, topGear, TruckTopGear(lt), u5, unarmed, u6);
            Check(up == 4 && u1 == 1 && early == 3 && u3 == 0 && heldGear == 3 && u4 == 0 && topGear == TruckTopGear(lt) && u5 == 0 && unarmed == 3 && u6 == 0,
                  "gear tops: floored in A at the game's top (its pull at nothing, the wheels not gaining) under a wrong top, the automatic goes up one after 1.2 s; "
                  "not sooner, not with the pull on (the taper rule's case then), not within the gear's hold, not in the top gear, not before the game's pull has "
                  "read over 0.1 once");
            (void)pulling;
            // the cut in A holds the game's pull at 1 (M2, the lead's point 16): the net rests and is not armed by that pull
            int u7 = 0, u8 = 0;
            bool seenHeld = true;
            const int restNet = stuckRun(3, 0.0f, 10.0f, 3.0f, u7, true, true), restArm = stuckRun(3, 1.0f, 10.0f, 3.0f, u8, false, true, &seenHeld);
            Check(restNet == 3 && u7 == 0 && restArm == 3 && u8 == 0 && !seenHeld,
                  "gear tops: with the torque cut acting in A the net under a wrong top rests: floored at a steady 0.8 of the top it stays in the gear, and a pull held at 1 does not arm it");
        }
        {
            // the taper rule (BRIEF_auto_stuck_under_taper.md F1, the Reviewer's point 11 and R1): in A, floored in 3rd at
            // 0.80 of its top, the game pulling 0.58: the trend gathered over a second whatever its sign, up one where its
            // mean is at 0 or under, past the gear's hold. The new gear measured from 1 s to 3 s after the shift: holding its
            // speed it stays (no hunting), and the shift's own loss in the first second does not count; losing faster than
            // the old gear's mean (a grade) it comes back and is held off 30 s. None with a mean over 0, at part throttle,
            // within the hold after a shift down, under 0.65 of the top, or where the next gear lands under its point down
            // (at 0.74 this fixture's 4th lands at 2,204 rpm, under its floored 2,292; the drive's truck had 1,731)
            const EngineHand h = handOf(kInputPad);
            // the drive's engine: 2,800 rpm at 0.74 of 3rd's top, so its red zone near 3,800 (the scale the gears go by)
            GaugeScale tq = tk;
            tq.idle = 700.0f;
            tq.redline = 3800.0f;
            tq.max = 4200.0f;
            struct Run { int gear, ups, backs, watches, most; float trend0, rate; };
            // in3(at): rad/s each second in 3rd at the run's time; in4(since): in 4th since the shift
            auto taperRun = [&](float share, float pedal, float moving, bool holdUp, float seconds, auto in3, auto in4, bool pullHeld = false) {
                TruckRaw st = lt;
                st.flags = 3;
                st.throttle = -pedal;
                st.actionB4 = 0.58f;
                float spin = share * TruckGearTop(st, 3), since = 0.0f;
                GearboxModel b = boxIn(3, spin, pedal);
                b.moving = moving;
                b.holdUp = holdUp;
                b.pullSeen = true;
                Run r = { 3, 0, 0, 0, 3, 0.0f, 0.0f };
                for (float at = 0.0f; at < seconds; at += kDt)
                {
                    spin += (b.gear == 4 ? in4(since) : b.gear == 3 ? in3(at) : 0.0f) * kDt;
                    since = b.gear == 4 ? since + kDt : 0.0f;
                    st.gear = st.gearEngaged = b.gear;
                    for (int i = 0; i < st.wheels; i++) st.spin[i] = spin;
                    st.speed = spin / 2.0f;
                    st.actionB4 = pullHeld ? 1.0f : 0.58f; // under the cut the game's pull reads 1
                    GearboxStep(b, st, tq, pedal, 1.0f, kDt, h, false, pullHeld);
                    if (b.gear > r.most) r.most = b.gear;
                }
                r.gear = b.gear;
                r.ups = b.taperUps;
                r.backs = b.taperBacks;
                r.watches = b.taperWatches;
                r.trend0 = b.taperTrend0;
                r.rate = b.taperRate;
                return r;
            };
            auto flat = [](float) { return 0.0f; };
            auto alternating = [](float at) { return fmodf(at, 1.0f) < 0.5f ? 1.0f : -1.2f; }; // a bumpy flat road: mean -0.1
            auto gapThen = [](float since) { return since < 0.8f ? -0.6f : 0.1f; };           // the shift's own loss, then gaining a little
            auto grade = [](float) { return -0.2f; };
            auto plus = [](float at) { return fmodf(at, 1.0f) < 0.5f ? 1.0f : -0.9f; };       // mean +0.05
            auto gaining = [](float) { return 0.3f; };
            const Run holds = taperRun(0.80f, 1.0f, 10.0f, true, 12.0f, flat, flat);
            // the cut in A (M2): floored at the taper's share and not gaining for kTaperFor and far more, a climb too: no shift, no hunt
            const Run restFlat = taperRun(0.80f, 1.0f, 10.0f, true, 12.0f, flat, flat, true), restClimb = taperRun(0.80f, 1.0f, 10.0f, true, 30.0f, [](float) { return -0.2f; }, flat, true);
            Check(restFlat.gear == 3 && restFlat.ups == 0 && restFlat.most == 3 && restClimb.gear <= 3 && restClimb.ups == 0 && restClimb.most == 3,
                  "taper rule: with the torque cut acting in A it rests: floored at 0.80 of 3rd's top and not gaining, on the flat and losing on a climb, no shift up by it");
            const Run bumpy = taperRun(0.80f, 1.0f, 10.0f, true, 6.0f, alternating, flat);
            const Run gap = taperRun(0.80f, 1.0f, 10.0f, true, 8.0f, flat, gapThen);
            const Run climb = taperRun(0.80f, 1.0f, 10.0f, true, 30.0f, flat, grade);
            const Run over = taperRun(0.80f, 1.0f, 10.0f, true, 4.0f, plus, flat);
            const Run gains = taperRun(0.80f, 1.0f, 10.0f, true, 3.0f, gaining, flat);
            const Run part = taperRun(0.80f, 0.6f, 10.0f, true, 3.0f, flat, flat);
            const Run afterDown = taperRun(0.80f, 1.0f, 0.0f, false, 2.0f, flat, flat);
            const Run under = taperRun(0.60f, 1.0f, 10.0f, true, 3.0f, flat, flat);
            const Run nextLow = taperRun(0.74f, 1.0f, 10.0f, true, 3.0f, flat, flat);
            printf("     taper rule: holds %d (ups %d, backs %d), bumpy %d (%d, mean %.2f), gap %d (%d, %d, rate %.2f), climb %d (%d, %d, most %d, rate %.2f), "
                   "mean over 0 %d (%d), gaining %d (%d), part (%d), after a shift down %d (%d), under 0.65 %d (%d), next under its point down %d (%d)\n",
                   holds.gear, holds.ups, holds.backs, bumpy.gear, bumpy.ups, bumpy.trend0, gap.gear, gap.ups, gap.backs, gap.rate, climb.gear, climb.ups, climb.backs,
                   climb.most, climb.rate, over.gear, over.ups, gains.gear, gains.ups, part.ups, afterDown.gear, afterDown.ups, under.gear, under.ups, nextLow.gear,
                   nextLow.ups);
            Check(holds.gear == 4 && holds.ups == 1 && holds.backs == 0 && holds.watches == 1 && holds.most == 4,
                  "taper rule: floored at 0.80 of 3rd's top, the game still pulling, not gaining over a second: up to 4th, and holding its speed there it stays "
                  "(no hunting)");
            Check(bumpy.ups == 1 && bumpy.most == 4 && bumpy.trend0 < 0.0f && bumpy.trend0 > -0.5f,
                  "taper rule: the trend alternating +1 and -1.2 rad/s each second (a mean of -0.1): up, by the second's mean, not one pass");
            Check(gap.gear == 4 && gap.ups == 1 && gap.backs == 0 && gap.watches == 1,
                  "taper rule: the shift's own loss in the first 0.8 s, then gaining a little: kept, the watch going from 1 s to 3 s");
            Check(climb.gear == 3 && climb.ups == 1 && climb.backs == 1 && climb.most == 4,
                  "taper rule: 4th losing speed faster than 3rd's mean (a grade): back to 3rd at 3 s, and no shift up by the rule from 3rd for 30 s");
            Check(over.gear == 3 && over.ups == 0, "taper rule: a second's mean of +0.05 rad/s each second: no shift");
            Check(gains.gear == 3 && gains.ups == 0 && part.ups == 0 && afterDown.gear == 3 && afterDown.ups == 0 && under.gear == 3 && under.ups == 0 &&
                      nextLow.gear == 3 && nextLow.ups == 0,
                  "taper rule: none while gaining, at part throttle (the light foot's own points decide there), within the gear's hold after a shift down, "
                  "under 0.65 of the gear's top, or where the next gear would land under its own point down");
        }
        {
            // the drag read's reasons, one per figure, and a sphere-like wheel read by |w|
            WheelDamp ok = {};
            ok.ok = ok.grounded = true;
            ok.damping = 0x3F00; // 0.5
            ok.inv[0] = 1.0f / 40.0f, ok.inv[1] = 1.0f / 25.0f, ok.inv[2] = 1.0f / 25.0f, ok.inv[3] = 1.0f / 100.0f;
            ok.cols[0][0] = 1.0f, ok.cols[1][1] = 1.0f, ok.cols[2][2] = 1.0f;
            ok.angular[0] = 10.0f;
            ok.radius = 0.5f;
            auto why = [&](WheelDamp w1) {
                WheelDamp ws[2] = { ok, w1 };
                WheelDampWhy y = {};
                const float a = WheelDampAccel(ws, 2, 5000.0f, nullptr, &y);
                return std::make_pair(a, y);
            };
            WheelDamp damping = ok, radius = ok, mass = ok, axle = ok, sphere = ok;
            damping.damping = 0x449A;  // 1232
            radius.radius = 0.0f;
            mass.inv[3] = 0.0f;
            axle.inv[1] = 0.0f;        // no inverse: neither an axle nor a sphere
            sphere.inv[0] = 1.0f / 41.2f, sphere.inv[1] = 1.0f / 41.5f, sphere.inv[2] = 1.0f / 41.3f;
            sphere.angular[0] = 6.0f, sphere.angular[1] = 8.0f; // |w| 10
            const auto good = why(ok), d = why(damping), r = why(radius), m = why(mass), x = why(axle), s = why(sphere);
            const float sphereA = (40.0f * 0.5f * 10.0f / 0.5f + 41.3333f * 0.5f * 10.0f / 0.5f) / 5200.0f;
            Check(isfinite(good.first) && good.second.what == kDampOk && d.second.what == kDampDamping && d.second.wheel == 1 && Near(d.second.value, 1232.0f, 1.0f) &&
                      r.second.what == kDampRadius && m.second.what == kDampMass && m.second.wheel == 1 && x.second.what == kDampAxle && Near(x.second.inertia[0], 40.0f, 0.01f) &&
                      isfinite(s.first) && Near(s.first, sphereA, 0.002f) && !isfinite(d.first) && !isfinite(r.first) && !isfinite(m.first) && !isfinite(x.first),
                  "drag read: the failed figure named, one reason per figure (damping 1232, radius, mass, no axle with its inertias); a sphere-like wheel read by its mean inertia and |w|");
            // when it is logged: the first good read, a failure, a changed reason 10 s on, nothing more in between, the
            // first good read after a failure with its seconds; a new truck starts over
            DampSay say = {};
            const WheelDampWhy fine = { kDampOk, -1, 0.0f, {} }, bad3 = { kDampDamping, 3, 1232.0f, {} }, bad2 = { kDampRadius, 2, 0.0f, {} };
            const int a1 = DampSayStep(say, 1, bad3, 100.0f), a2 = DampSayStep(say, 1, bad3, 101.0f), a3 = DampSayStep(say, 1, bad2, 105.0f),
                      a4 = DampSayStep(say, 1, bad2, 111.0f), a5 = DampSayStep(say, 1, fine, 115.0f);
            const float took = 121.5f - say.failSince; // as the log takes it, before the step
            const int a6 = DampSayStep(say, 1, fine, 121.5f), a7 = DampSayStep(say, 1, fine, 140.0f), a8 = DampSayStep(say, 2, fine, 141.0f);
            Check(a1 == kDampSayFail && a2 == kDampSayNone && a3 == kDampSayNone && a4 == kDampSayFail && a5 == kDampSayNone && a6 == kDampSayBack && Near(took, 21.5f, 0.01f) &&
                      a7 == kDampSayNone && a8 == kDampSayGood,
                  "drag read: logged at a failure, again when its reason changes (10 s apart at least), and at the first good read after with the seconds it took; a new truck starts over");
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

    // the creep's line at its longest (the fast fail of 2026-11-06 21:41, F0 of BRIEF_drive_2052_fixes.md): every text at
    // its longest ("unarmed (pulling away)", " (under it)", ", a shift just now") and each float at 1e6, into the real
    // 512 of gauges.cpp: cut and ended, never fatal; the holding part whole in a buffer with room
    {
        TruckRaw t = {};
        t.gearEngaged = 1000000;
        t.speed = 1.0e6f;
        t.actionB4 = t.torqueFactor = t.throttle = 1.0e6f;
        t.handbrake = 1;
        CreepCase c = {};
        c.converter = false;
        c.ratio = c.input = 1.0e6f;
        c.idleSpin = 1.0e6f; // the wheels at 0 of it: under the stuck mark
        CreepModel m = {};
        m.armed = false;
        m.mayStall = true;
        m.hold = m.holdMost = m.share = m.stuck = 1.0e6f;
        m.sinceShift = 0.0f;
        char real[512], wide[2048];
        memset(real, 'x', sizeof real);
        CreepProbeText(t, 1.0e6f, c, 2147483647L, -2147483647L - 1, real, sizeof real, &m);
        CreepProbeText(t, 1.0e6f, c, 2147483647L, -2147483647L - 1, wide, sizeof wide, &m);
        const size_t cut = strnlen(real, sizeof real), whole = strlen(wide);
        const bool ended = cut < sizeof real && !strncmp(real, wide, cut);
        const char *hold = strstr(wide, " | holding unarmed");
        const bool holdWhole = hold && strstr(hold, "unarmed (pulling away)") && strstr(hold, " (under it)") && whole >= 18 &&
                               !strcmp(wide + whole - 18, ", a shift just now");
        Check(ended && holdWhole, "creep line: at its longest (every text, each float 1e6) cut and ended in the real 512, the holding part whole where there is room, no fatal call");
    }

    // FmtCut (text_cut.h): a line built piece by piece from a game text longer than its buffer stays in it, the length
    // never -1, and a whole text gives its length
    {
        char line[48];
        int used = 0;
        const char *name = "a_truck_kind_name_of_eighty_characters_as_a_mod_can_give_it_to_the_game_0123456";
        for (int i = 0; i < 4; i++) used += FmtCut(line + used, sizeof line - used, "%s%s", i ? " | " : "", name);
        wchar_t wline[16];
        const int wn = FmtCut(wline, 16, L"%S", name);
        const bool ok = used == (int)sizeof line - 1 && strlen(line) == sizeof line - 1 && wn == 15 && wcslen(wline) == 15 && FmtCut(line, sizeof line, "%d", 42) == 2;
        Check(ok, "FmtCut: a line of game texts longer than its buffer is cut, its length the buffer's less one and never -1; a whole one gives its length");
    }

    // the odometer (BRIEF_odometer.md): metres above 0.5 m/s only, reverse too, and real seconds with the engine on only;
    // the readout's text; Set's figure
    {
        double m = 0.0, sec = 0.0;
        OdoCount(m, sec, 0.4f, 1.0f, true);                 // the physics' jitter: no metres, the engine's second
        OdoCount(m, sec, 0.5f, 2.0f, false);                // at the threshold, engine off: no second
        OdoCount(m, sec, -10.0f, 0.5f, true);               // reverse counts
        OdoCount(m, sec, 20.0f, 0.0f, true);                // no seconds passed: nothing
        const bool count = fabs(m - 6.0) < 1e-6 && fabs(sec - 1.5) < 1e-6;
        char t[6][24];
        OdoText(t[0], 24, 0.0, 0.0, false, false);
        OdoText(t[1], 24, 99900.0, 0.0, false, false);
        OdoText(t[2], 24, 100000.0, 0.0, false, false);
        OdoText(t[3], 24, 123456000.0, 0.0, false, false);
        OdoText(t[4], 24, 16093.44, 0.0, false, true);
        OdoText(t[5], 24, 0.0, 12.3 * 3600.0, true, true);
        const bool text = !strcmp(t[0], "0.0 km") && !strcmp(t[1], "99.9 km") && !strcmp(t[2], "100 km") && !strcmp(t[3], "123456 km") && !strcmp(t[4], "10.0 mi") &&
                          !strcmp(t[5], "12.3 h");
        double v = 0.0;
        const bool set = OdoSetValue("12.5", false, false, v) && v == 12500.0 && OdoSetValue(" 0 ", false, true, v) && v == 0.0 && OdoSetValue("2", true, false, v) &&
                         v == 7200.0 && OdoSetValue("1", false, true, v) && fabs(v - kOdoMile) < 1e-9 && !OdoSetValue("", false, false, v) &&
                         !OdoSetValue("abc", false, false, v) && !OdoSetValue("12 km", false, false, v) && !OdoSetValue("-1", false, false, v) &&
                         !OdoSetValue("10000001", false, false, v) && OdoSetValue("10000000", false, false, v) && !OdoSetValue("1000001", true, false, v) &&
                         !OdoSetValue("nan", false, false, v) && !OdoSetValue("inf", false, false, v);
        Check(count && text && set, "odometer: metres above 0.5 m/s, reverse too, seconds with the engine on; 0.0 km, 99.9 km, 100 km, 123456 km, mi with Mph, 12.3 h; "
                                    "Set's figure in km, mi or h within its range, anything else refused");
    }

    // the odometer's writes by a made-up clock (the Reviewer's musts 3 and 4, should 6): nothing before the decision and
    // the count then on top of the figure loaded; a minute; a stop after 2 s standing, at most one every 10 s; when the
    // models stop stepping; nothing while nothing moved; the owed write at a change
    {
        OdoState o = {};
        unsigned long long now = 1000;
        bool early = true;
        for (int i = 0; i < 200; i++, now += 50) early &= OdoPass(o, 1.0, 0.05, true, true, now) == kOdoNoWrite;  // 10 s in the match's wait
        const bool pending = early && fabs(o.metres - 200.0) < 1e-6 && !OdoOwed(o);
        OdoDecide(o, true, true, 5000.0, 100.0, now);
        const bool decided = fabs(o.metres - 5200.0) < 1e-6 && fabs(o.seconds - 110.0) < 1e-6 && OdoOwed(o);
        // driving on: the minute's write
        int due = kOdoNoWrite;
        unsigned long long minuteAt = 0;
        for (int i = 0; i < 1300 && due == kOdoNoWrite; i++, now += 50)
            if ((due = OdoPass(o, 1.0, 0.05, true, true, now)) != kOdoNoWrite) minuteAt = now;
        const bool minute = due == kOdoMinute && minuteAt - 10950 >= kOdoEvery - 100 && minuteAt - 10950 <= kOdoEvery + 100;
        OdoWritten(o, now);
        // a stop: moving, then 2 s standing (the engine idles: its seconds change)
        int stopAt = -1;
        for (int i = 0; i < 60; i++, now += 50)
            if (OdoPass(o, 0.0, 0.05, false, true, now) == kOdoStop && stopAt < 0) stopAt = i;
        const bool stop = stopAt >= 39 && stopAt <= 41;
        OdoWritten(o, now);
        // stop and go: a second stop 5 s later writes nothing, the next after 10 s does
        int stops = 0;
        for (int round = 0; round < 3; round++)
        {
            for (int i = 0; i < 20; i++, now += 50) stops += OdoPass(o, 1.0, 0.05, true, true, now) == kOdoStop;
            for (int i = 0; i < 60; i++, now += 50) stops += OdoPass(o, 0.0, 0.05, false, true, now) == kOdoStop;
        }
        const bool gap = stops == 1;
        // the game held (a menu): one write, then nothing while nothing steps
        OdoPass(o, 1.0, 0.05, true, true, now);
        now += 50;
        const bool held = OdoPass(o, 0.0, 0.0, false, false, now) == kOdoHeld;
        OdoWritten(o, now);
        bool still = true;
        for (int i = 0; i < 2000; i++, now += 50) still &= OdoPass(o, 0.0, 0.0, false, false, now) == kOdoNoWrite;
        // a truck with no id: counted, never written
        OdoState x = {};
        OdoDecide(x, false, false, 0.0, 0.0, now);
        bool unsaved = true;
        for (int i = 0; i < 2000; i++, now += 50) unsaved &= OdoPass(x, 1.0, 0.05, true, true, now) == kOdoNoWrite;
        unsaved &= !OdoOwed(x) && x.metres > 1999.0;
        // Set to: the figure, the other kept
        OdoSet(o, false, 0.0);
        const bool setZero = o.metres == 0.0 && o.seconds > 0.0 && OdoOwed(o);
        OdoChange(o);
        const bool changed = !o.decided && o.metres == 0.0 && !OdoOwed(o);
        Check(pending && decided && minute && stop && gap && held && still && unsaved && setZero && changed,
              "odometer writes: nothing before the decision and the count added to the figure loaded; a write a minute; one at a stop after 2 s standing, at most one "
              "every 10 s; one when the game holds the models, then none; an id not read never written; Set to keeps the other figure; another truck starts over");
    }

    // the driveline inertia (BRIEF_drive_1114_findings.md, part 185, the Reviewer's addendum of 14:29): T' = T - c (T - L)
    {
        const uint64_t veh = 0x1000;
        // must 1: at 0 the torque keeps its bits, whatever else is read
        DriveRun d0 = {};
        bool bits = true;
        const float asks[4] = { 1234.5678f, 0.1f, -77.25f, 3.0e7f };
        for (int i = 0; i < 4; i++)
        {
            const DriveOut o = DriveInertiaStep(d0, asks[i], 5.0f + (float)i, true, 1.0f / 60.0f, (uint64_t)i + 1, 0.0f, 10.0f, 185.0f, veh, 9.0f, 16u * (uint64_t)i);
            bits = bits && !memcmp(&o.torque, &asks[i], sizeof(float)) && !o.acted;
        }
        // the loop: one wheel of 10 kg m2, 185 kg m2 of truck behind it by the grip, at 60 and at 120, with the build's Is
        // (the wheel's own, whatever the grip: the review of 185 part 1, M1). mode 0: the wheel turns the truck (195 in
        // truth); 1: it spins alone against a load of 0.6 of the pull; 2: the one then the other mid run; 3: the edge of
        // grip, the ground's push growing with the slip up to 0.3 of the pull and the truck behind it, the wheel at 1.5
        // times the truck's speed and over. The law reads no truck speed now, so a truck speed not read is the same run.
        struct Loop { float seconds, worstLate, worstEarly, ratio; bool swing; };
        // the swing: the pull's change from step to step in the last second is not under half its largest in the quarter
        // second after the start or after the change of grip (it dies away, or it grows)
        auto loop = [&](int rate, float share, int mode, float pull, float target, float isScale) {
            DriveRun d = {};
            const float dt = 1.0f / (float)rate;
            float omega = 0.0f, last = 0.0f, truckV = 0.0f;
            Loop out = { -1.0f, 0.0f, 0.0f, 0.0f, false };
            const int steps = rate * 8;
            for (int st = 0; st < steps; st++)
            {
                const bool gripping = mode == 0 || (mode == 2 && st < steps / 2);
                // isScale: the wheel's inertia fed too large on purpose
                const DriveOut o = DriveInertiaStep(d, pull, omega, true, dt, (uint64_t)st + 1, share, 10.0f * isScale, 185.0f, veh, 9.0f, (uint64_t)(st * 1000 / rate));
                const float t = o.torque;
                if (mode == 3)
                {
                    const float ground = fminf(0.3f * pull, fmaxf(0.0f, 100.0f * (omega - truckV)));
                    omega += (t - ground) * dt / 10.0f;
                    truckV += ground * dt / 185.0f;
                    out.ratio = truckV > 0.0f ? omega / truckV : 0.0f;
                }
                else omega += (t - (gripping ? 0.0f : 0.6f * pull)) * dt / (gripping ? 195.0f : 10.0f);
                if (st > 0)
                {
                    const float c2 = fabsf(t - last);
                    if (st < rate / 4 || (mode == 2 && st >= steps / 2 && st < steps / 2 + rate / 4)) out.worstEarly = fmaxf(out.worstEarly, c2);
                    else if (st > steps - rate) out.worstLate = fmaxf(out.worstLate, c2);
                }
                last = t;
                if (out.seconds < 0.0f && omega >= target) out.seconds = (float)(st + 1) * dt;
                if (!isfinite(omega)) out.swing = true;
            }
            out.swing = out.swing || out.worstLate > 0.5f * out.worstEarly + 1.0f;
            return out;
        };
        bool loopSteady = true, rates = true;
        char loopLine[640] = "";
        float g60 = 0, g120 = 0, s60 = 0, s120 = 0, e60 = 0, e120 = 0, edgeRatio = 0;
        for (int mode = 0; mode < 4; mode++)
            for (float share : { 0.5f, 1.0f })
            {
                const float target = mode == 1 || mode == 3 ? 10.0f : 8.0f;
                const Loop at60 = loop(60, share, mode, 1000.0f, target, 1.0f), at120 = loop(120, share, mode, 1000.0f, target, 1.0f);
                loopSteady = loopSteady && !at60.swing && !at120.swing;
                rates = rates && at60.seconds > 0.0f && at120.seconds > 0.0f && fabsf(at60.seconds - at120.seconds) <= 0.05f * at60.seconds;
                if (mode == 0 && share == 1.0f) g60 = at60.seconds, g120 = at120.seconds;
                if (mode == 1 && share == 1.0f) s60 = at60.seconds, s120 = at120.seconds;
                if (mode == 3 && share == 1.0f) e60 = at60.seconds, e120 = at120.seconds, edgeRatio = fminf(at60.ratio, at120.ratio);
            }
        // Is taken 18.5 times too large (the wheel spinning alone fed as turning the truck) swings; the build's Is is the wheel's own
        const Loop wrong = loop(60, 1.0f, 1, 1000.0f, 1e9f, 18.5f);
        DriveRun di = {};
        DriveInertiaStep(di, 100.0f, 1.0f, true, 1.0f / 60.0f, 1, 1.0f, 10.0f, 185.0f, veh, 9.0f, 0);
        const DriveOut spinIs = DriveInertiaStep(di, 100.0f, 1.5f, true, 1.0f / 60.0f, 2, 1.0f, 10.0f, 185.0f, veh, 9.0f, 16);
        const DriveOut halfIs = DriveInertiaStep(di, 100.0f, 2.0f, true, 1.0f / 60.0f, 3, 1.0f, 10.0f, 185.0f, veh, 9.0f, 33);
        const bool isLow = spinIs.is == 10.0f && halfIs.is == 10.0f && Near(spinIs.ie, 185.0f, 1e-3f) && spinIs.c <= kDriveShareMost && wrong.swing && edgeRatio >= 1.5f;
        // a steady spin in mud (the load equal to the pull): the term 0 within a hundredth
        DriveRun dm = {};
        float mudTook = 1.0f;
        for (int st = 0; st < 120; st++) mudTook = DriveInertiaStep(dm, 800.0f, 30.0f, true, 1.0f / 60.0f, (uint64_t)st + 1, 1.0f, 10.0f, 185.0f, veh, 9.0f, (uint64_t)st * 16).share;
        const bool mud = fabsf(mudTook) < 0.01f;
        // the bound: at a lift (the ask falls) nothing is given; under the engine's braking (T under 0) T as asked
        DriveRun db = {};
        DriveInertiaStep(db, 1000.0f, 1.0f, true, 1.0f / 60.0f, 1, 1.0f, 10.0f, 185.0f, veh, 9.0f, 0);
        const DriveOut lift = DriveInertiaStep(db, 200.0f, 1.0f, true, 1.0f / 60.0f, 2, 1.0f, 10.0f, 185.0f, veh, 9.0f, 16);
        const DriveOut brake = DriveInertiaStep(db, -300.0f, 0.9f, true, 1.0f / 60.0f, 3, 1.0f, 10.0f, 185.0f, veh, 9.0f, 33);
        const bool bound = lift.torque <= 200.0f && lift.torque >= 0.0f && brake.torque == -300.0f;
        // the resets (must 5): another truck, another gear's top, a gap of three steps, a gap in time, the share at 0, no mean
        auto fresh = [&](DriveRun &d) {
            d = {};
            DriveInertiaStep(d, 500.0f, 1.0f, true, 1.0f / 60.0f, 10, 1.0f, 10.0f, 185.0f, veh, 9.0f, 1000);
        };
        DriveRun dr = {};
        fresh(dr);
        bool resets = !DriveInertiaStep(dr, 500.0f, 3.0f, true, 1.0f / 60.0f, 11, 1.0f, 10.0f, 185.0f, veh + 8, 9.0f, 1016).acted;
        fresh(dr);
        resets = resets && !DriveInertiaStep(dr, 500.0f, 3.0f, true, 1.0f / 60.0f, 11, 1.0f, 10.0f, 185.0f, veh, 12.0f, 1016).acted;
        fresh(dr);
        resets = resets && !DriveInertiaStep(dr, 500.0f, 3.0f, true, 1.0f / 60.0f, 13, 1.0f, 10.0f, 185.0f, veh, 9.0f, 1016).acted;
        fresh(dr);
        resets = resets && !DriveInertiaStep(dr, 500.0f, 3.0f, true, 1.0f / 60.0f, 12, 1.0f, 10.0f, 185.0f, veh, 9.0f, 1016).acted; // one step skipped (S1)
        fresh(dr);
        resets = resets && !DriveInertiaStep(dr, 500.0f, 3.0f, true, 1.0f / 60.0f, 11, 1.0f, 10.0f, 185.0f, veh, 9.0f, 1200).acted;
        fresh(dr);
        resets = resets && !DriveInertiaStep(dr, 500.0f, 3.0f, true, 1.0f / 60.0f, 11, 0.0f, 10.0f, 185.0f, veh, 9.0f, 1016).acted;
        fresh(dr);
        resets = resets && !DriveInertiaStep(dr, 500.0f, NAN, false, 1.0f / 60.0f, 11, 1.0f, 10.0f, 185.0f, veh, 9.0f, 1016).acted;
        fresh(dr);
        resets = resets && DriveInertiaStep(dr, 500.0f, 3.0f, true, 1.0f / 60.0f, 11, 1.0f, 10.0f, 185.0f, veh, 9.0f, 1016).acted; // and on the same truck it acts
        // the shares by the gear: 0 at 0, the first gear whole at 100, a gear of twice the top a quarter, the low range as the first
        const bool shares = DriveIeShare(0, 9.0f, 9.0f) == 0.0f && DriveIeShare(100, 9.0f, 9.0f) == 1.0f && DriveIeShare(100, 18.0f, 9.0f) == 0.25f &&
                            DriveIeShare(50, 4.0f, 9.0f) == 0.5f && DriveIeShare(150, 9.0f, 9.0f) == 1.0f;
        // the drive-off on grip: a pull that takes the truck to 8 rad/s in 3.0 s at 0, at 50 and 100 (the truck's share and the
        // wheels' own at 5 percent of it, the scout, and 15 percent, the 8x8)
        auto driveOff = [&](float share, float wheelShare) {
            DriveRun d = {};
            const float part = 185.0f, wheel = part * wheelShare, inertia = part + wheel, pull = 8.0f * inertia / 3.0f;
            float omega = 0.0f;
            for (int st = 0; st < 60 * 30; st++)
            {
                const float tq = DriveInertiaStep(d, pull, omega, true, 1.0f / 60.0f, (uint64_t)st + 1, share, wheel, part, veh, 9.0f, (uint64_t)st * 16).torque;
                omega += tq / 60.0f / inertia;
                if (omega >= 8.0f) return (float)(st + 1) / 60.0f;
            }
            return -1.0f;
        };
        const float sc0 = driveOff(0.0f, 0.05f), sc50 = driveOff(0.5f, 0.05f), sc100 = driveOff(1.0f, 0.05f), ex0 = driveOff(0.0f, 0.15f), ex50 = driveOff(0.5f, 0.15f),
                    ex100 = driveOff(1.0f, 0.15f);
        snprintf(loopLine, sizeof loopLine,
                 "driveline inertia: at 0 the bits; the loop at 60 and 120 settles with Is the wheel's own (gripping %.2f s and %.2f s to 8 rad/s, spinning %.2f s and %.2f s and at the "
                 "edge of grip %.2f s and %.2f s, %.1f x the truck, to 10 rad/s at 100), Is 18.5 times too large swings; steady spin in mud takes %.3f; the bound and the resets, a step "
                 "skipped too; the drive-off of 3.0 s on grip at 50 and 100: %.2f and %.2f s (the scout), %.2f and %.2f s (the 8x8)",
                 g60, g120, s60, s120, e60, e120, edgeRatio, mudTook, sc50, sc100, ex50, ex100);
        printf("     %s\n", loopLine);
        const bool driveOk = sc0 > 2.9f && sc0 < 3.1f && sc50 > sc0 && sc100 > sc50 && ex0 > 2.9f && ex100 > ex50 && ex50 > ex0;
        if (!(bits && loopSteady && rates && isLow && mud && bound && resets && shares && driveOk))
            printf("     driveline: bits %d steady %d rates %d isLow %d (wrong swing %d) mud %d bound %d (lift %.1f brake %.1f) resets %d shares %d driveOff %d\n", bits, loopSteady, rates, isLow,
                   wrong.swing, mud, bound, lift.torque, brake.torque, resets, shares, driveOk);
        Check(bits && loopSteady && rates && isLow && mud && bound && resets && shares && driveOk, loopLine);
    }

    // the crash line's words of a fault's place (BRIEF_tester_1114.md, part 196): the memory there, and a fault on
    // another thread after ExitProcess named at the quit, not a crash
    {
        char memA[200], memB[200], memC[200], memD[200];
        FaultMemoryText(true, MEM_COMMIT, MEM_PRIVATE, (const void *)0x7fff0aaa0000, true, memA, sizeof memA);
        FaultMemoryText(true, MEM_COMMIT, MEM_IMAGE, (const void *)0x140000000, false, memB, sizeof memB);
        FaultMemoryText(true, MEM_FREE, 0, nullptr, false, memC, sizeof memC);
        FaultMemoryText(false, 0, 0, nullptr, false, memD, sizeof memD);
        const bool words = strstr(memA, "committed, private") && strstr(memA, "the mod's hook trampolines") && strstr(memB, "committed, image") &&
                           strstr(memB, "no region of the mod's") && strstr(memC, "free, none") && !strcmp(memD, "memory: not read");
        const bool quit = !FaultAtQuit(0, 5) && !FaultAtQuit(5, 5) && FaultAtQuit(5, 7);
        char shortText[24];
        const int cut = FaultMemoryText(true, MEM_COMMIT, MEM_PRIVATE, (const void *)0x1000, true, shortText, sizeof shortText);
        const bool cutOk = cut < 0 && strlen(shortText) == sizeof shortText - 1;
        if (!(words && quit && cutOk)) printf("     fault place: \"%s\" \"%s\" \"%s\" \"%s\" quit %d cut %d\n", memA, memB, memC, memD, quit, cutOk);
        Check(words && quit && cutOk, "crash line: the memory at a fault named by state, type, allocation base and whether it is the mod's hooks; a fault on "
                                      "another thread after ExitProcess is at the quit, the thread that called it and before it not; a short buffer cuts the words");
    }

    printf(g_failed ? "MODELS TEST FAILED (%d)\n" : "MODELS TEST OK\n", g_failed);
    return g_failed ? 1 : 0;
}
