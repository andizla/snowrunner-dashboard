#define main models_main
#include "../../../../../home/user/snowrunner-dashboard/test/models_test.cpp"
#undef main
int main()
{
    GaugeSettings s; SettingsDefaults(s); SettingsClamp(s);
    const GaugeScale k = GaugeScaleOf(s);
    printf("idle %.0f red %.0f\n", k.idle, k.redline);
    TruckRaw g = Truck(); g.gear = g.gearEngaged = 1; g.automatic = 1;
    for (int i = 1; i <= 4; i++) printf("gear %d top %.1f up(light) %.0f up(floored) %.0f down(light) %.0f down(floored) %.0f\n", i, TruckGearTop(g, i),
        GearboxUpPoint(g, k, i, 0), GearboxUpPoint(g, k, i, 1), GearboxDownPoint(g, k, i, 0), GearboxDownPoint(g, k, i, 1));
    for (float r : {1.24f, 1.31f, 1.46f, 1.53f, 1.59f, 1.625f, 1.706f, 1.89f}) printf("r %.3f up x %.3f down x %.3f\n", r, GearUpShare(r), GearDownShare(r));
    // pull trace
    for (float pedal : {1.0f, 0.3f})
    {
        GearboxModel b = {};
        const float topSpin = TruckGearTop(g, TruckTopGear(g));
        for (float at = 0.0f; at < 40.0f; at += kDt)
        {
            const float spin = topSpin * at / 40.0f;
            for (int i = 0; i < g.wheels; i++) g.spin[i] = spin;
            const int before = b.gear;
            GearboxStep(b, g, k, pedal, pedal, kDt);
            if (b.gear != before) printf("pedal %.1f at %.2f s spin %.2f: %d to %d\n", pedal, at, b.spin, before, b.gear);
        }
    }
    return 0;
}
