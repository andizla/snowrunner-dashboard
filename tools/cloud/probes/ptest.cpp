#include <stdio.h>
#include "models.h"
int main()
{
    for (int mode = 0; mode < 3; mode++)
    {
        TruckRaw p = {}; p.flags = 3; p.gearSpeeds = 6; float sp[6] = { 1.5f, 3, 6, 12, 16, 8 }; memcpy(p.gearSpeed, sp, sizeof sp);
        p.wheels = 4; p.drive[0] = p.drive[1] = 2; p.drive[2] = p.drive[3] = 1;
        p.gear = p.gearEngaged = 3; p.powerFactor = 1.05f; p.throttle = -1.0f;
        float v = 5.0f, inForce = 1.05f;
        PowerTest t = { kPowerArmed };
        for (float at = 0; at < 10; at += 0.05f)
        {
            if (mode == 2 && at >= 4.0f) p.throttle = 0;
            for (int i = 0; i < 4; i++) p.spin[i] = v * 2.2f;
            p.speed = v;
            PowerOrders o = PowerTestStep(t, p, TruckPedal(p), 0.05f);
            if (o.write) p.powerFactor = o.factor;
            if (mode == 0 || o.shiftTo) inForce = p.powerFactor;
            if (o.ended) { char a[1400], b[320]; PowerTestText(t, a, sizeof a, b, sizeof b); printf("%s\n%s\nfactor after %.3f shift %d\n\n", a, b, p.powerFactor, o.shiftTo); }
            v += 0.5f * inForce / 1.05f * 0.05f;
        }
    }
}
