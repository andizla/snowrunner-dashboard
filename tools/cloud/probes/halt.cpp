#include <stdio.h>
#include "models.h"
int main()
{
    GaugeSettings s = {}; SettingsDefaults(s); s.idleRpm = 650; s.redlineRpm = 2200; SettingsClamp(s);
    const GaugeScale k = GaugeScaleOf(s);
    TruckRaw g = {}; g.flags = 3; g.powerFactor = 1; g.gearSpeeds = 6; float sp[6] = { 1.5f, 3, 6, 12, 16, 8 }; memcpy(g.gearSpeed, sp, sizeof sp);
    g.wheels = 4; g.drive[0] = g.drive[1] = 2; g.drive[2] = g.drive[3] = 1; g.gear = g.gearEngaged = 1; g.automatic = 1;
    printf("idle %.0f red %.0f\n", k.idle, k.redline);
    auto wheels = [&](float v) { for (int i = 0; i < 4; i++) g.spin[i] = v; };
    wheels(TruckGearTop(g, 4) * 0.7f);
    GearboxModel m = {}; m.running = true; m.gear = 4; m.spin = TruckDrivenSpin(g);
    for (float at = 0; at < 3; at += 1.0f / 60)
    {
        wheels(fmaxf(0.0f, TruckGearTop(g, 4) * 0.7f * (1.0f - at / 1.5f)));
        int b = m.gear;
        GearboxStep(m, g, k, 0, 0, 1.0f / 60);
        if (b != m.gear) printf("%.2f s: %d to %d, input %.0f, wheels %.1f smoothed %.1f\n", at, b, m.gear, m.input, g.spin[0], m.spin);
    }
}
