// a rough truck on the Defender's gears, to see where the model shifts
#include <stdio.h>
#include "models.h"
static GaugeScale k;
struct Sim { TruckRaw t; GearboxModel m; float v; };
static void Setup(Sim &s, int gear, float v)
{
    s = Sim{};
    s.t.flags = 3; s.t.powerFactor = 1.05f; s.t.gearSpeeds = 8;
    const float sp[8] = { 3, 1.5f, 4, 7, 11, 19, 30, 20 };
    memcpy(s.t.gearSpeed, sp, sizeof sp);
    s.t.wheels = 4; s.t.drive[0] = s.t.drive[1] = 1; s.t.drive[2] = s.t.drive[3] = 1; s.t.automatic = 0; s.t.gear = s.t.gearEngaged = gear;
    s.v = v;
    s.m.running = true; s.m.gear = gear; s.m.spin = s.m.by = v * 2.2f; s.m.grip = 2.2f;
}
// one step: pedal, brake, grade as m/s2 against, mud drag m/s2
static void Step(Sim &s, float pedal, float brake, float grade, float mud, float dt, const char *name, float at)
{
    const float spin = s.v * 2.2f;
    for (int i = 0; i < 4; i++) s.t.spin[i] = spin;
    s.t.speed = s.v;
    s.t.throttle = brake > 0 ? brake : -pedal;
    const int before = s.m.gear;
    GearboxStep(s.m, s.t, k, pedal, pedal, dt);
    if (s.m.gear != before) printf("  %s %5.1f s: %d to %d at %4.1f m/s, engine %4.0f rpm after (pedal %.2f) [spin %.1f by %.1f top %.1f]\n", name, at, before, s.m.gear, s.v, s.m.input, pedal, s.m.spin, s.m.by, TruckGearTop(s.t, s.m.gear));
    const int g = s.m.gear;
    const float top = TruckGearTop(s.t, g), share = spin / top;
    const float most = 3.2f * powf(8.0f / top, 0.7f);
    float a = pedal * most * fmaxf(0.0f, 1.0f - share * share * share * share) - 0.04f - 0.0006f * s.v * s.v - grade - mud * (s.v > 0.1f) - brake * 5.0f * (s.v > 0.05f);
    s.v = fmaxf(0.0f, s.v + a * dt);
}
int main()
{
    GaugeSettings st = {}; SettingsDefaults(st); st.tachClass = kClassScout; st.idleRpm = 800; st.redlineRpm = 4850; SettingsClamp(st); k = GaugeScaleOf(st); printf("idle %.0f, red zone %.0f\n", k.idle, k.redline);
    const float dt = 1.0f / 60;
    Sim s;
    for (float p : { 1.0f, 0.5f, 0.25f })
    {
        printf("flat, pedal %.2f from a stand:\n", p);
        Setup(s, 1, 0.0f);
        for (float at = 0; at < 45; at += dt) Step(s, p, 0, 0, 0, dt, "", at);
        printf("  at 45 s %.1f m/s in %d\n", s.v, s.m.gear);
    }
    for (float grade : { 0.6f, 1.2f, 2.0f })
    for (float p : { 1.0f, 0.6f })
    {
        printf("flat floored 30 s, then a climb of %.1f m/s2 at pedal %.2f:\n", grade, p);
        Setup(s, 1, 0.0f);
        int shifts = 0;
        for (float at = 0; at < 30; at += dt) Step(s, 1.0f, 0, 0, 0, dt, "", at);
        shifts = s.m.shifts;
        for (float at = 30; at < 70; at += dt) Step(s, p, 0, grade, 0, dt, "climb", at);
        printf("  on the climb %d shifts, at 70 s %.1f m/s in %d\n", s.m.shifts - shifts, s.v, s.m.gear);
    }
    printf("cruise in 5th at pedal 0.3, then the foot off in mud (1.5 m/s2):\n");
    Setup(s, 5, 15.0f);
    for (float at = 0; at < 5; at += dt) Step(s, 0.3f, 0, 0, 0, dt, "cruise", at);
    for (float at = 5; at < 20; at += dt) Step(s, 0.0f, 0, 0, 1.5f, dt, "coast", at);
    printf("cruise in 5th at pedal 0.3, then braking hard:\n");
    Setup(s, 5, 15.0f);
    for (float at = 0; at < 5; at += dt) Step(s, 0.3f, 0, 0, 0, dt, "cruise", at);
    for (float at = 5; at < 12; at += dt) Step(s, 0.0f, 1.0f, 0, 0, dt, "brake", at);
    printf("floored in 6th from 25 m/s up a 2.5 m/s2 grade:\n");
    Setup(s, 6, 25.0f);
    for (float at = 0; at < 30; at += dt) Step(s, 1.0f, 0, 2.5f, 0, dt, "steep", at);
    printf("  at 30 s %.1f m/s in %d\n", s.v, s.m.gear);
    printf("light 0.3 in 5th at 15 m/s, then pushed to the floor:\n");
    Setup(s, 5, 15.0f);
    for (float at = 0; at < 3; at += dt) Step(s, 0.3f, 0, 0.5f, 0, dt, "light", at);
    for (float at = 3; at < 8; at += dt) Step(s, 1.0f, 0, 0.5f, 0, dt, "push", at);
}
