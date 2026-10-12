#define main models_main
#include "../../../../../home/user/snowrunner-dashboard/test/models_test.cpp"
#undef main
int main()
{
    GaugeSettings s; SettingsDefaults(s); SettingsClamp(s);
    const GaugeScale k = GaugeScaleOf(s);
    TruckRaw g = Truck(); g.gear = g.gearEngaged = 1; g.automatic = 1;
    TruckRaw sc = g;
    static const float scaniaSpeeds[12] = { 1.0f, 1.268f, 1.563f, 2.054f, 2.839f, 3.625f, 4.901f, 5.981f, 7.453f, 10.103f, 12.754f, 8.0f };
    sc.gearSpeeds = 12; memcpy(sc.gearSpeed, scaniaSpeeds, sizeof scaniaSpeeds);
    GaugeScale ck = k; ck.idle = 600; ck.redline = 1930; ck.max = 2500;
    for (int i = 1; i <= 10; i++) printf("gear %d top %.2f up %.0f down %.0f\n", i, TruckGearTop(sc, i), GearboxUpPoint(sc, ck, i, 1), GearboxDownPoint(sc, ck, i, 1));
    GearboxModel close = {}; close.running = true; close.gear = 1; close.grip = 2.2f;
    for (float at = 0.0f; at < 12.0f; at += kDt)
    {
        const float spin = 2.5f * at;
        for (int i = 0; i < sc.wheels; i++) sc.spin[i] = spin;
        sc.speed = spin / 2.2f;
        const int before = close.gear;
        const float x = before >= 1 ? spin / TruckGearTop(sc, before) : 0.0f;
        GearboxStep(close, sc, ck, 1.0f, 1.0f, kDt);
        if (close.gear != before) printf("%.2f s spin %.2f by %.2f rolling %.2f: %d -> %d at x %.2f, lands %.2f grip %.2f\n", at, spin, close.by, close.rolling, before, close.gear, x, spin / TruckGearTop(sc, close.gear), close.grip);
    }
    return 0;
}
