#include <stdio.h>
#include "models.h"
int main()
{
    for (int mode = 0; mode < 3; mode++)
    {
        TruckRaw p = {}; p.flags = 3; p.gearSpeeds = 6; float sp[6] = { 1.5f, 3, 6, 12, 16, 8 }; memcpy(p.gearSpeed, sp, sizeof sp);
        p.wheels = 4; p.drive[0] = p.drive[1] = 2; p.drive[2] = p.drive[3] = 1;
        p.gear = p.gearEngaged = 4; p.torqueFactor = 1.0f; p.throttle = -1.0f;
        float v = 5.0f;
        PullTest t = { kPullArmed };
        for (float at = 0; at < 10; at += 0.05f)
        {
            if (mode == 2 && at >= 4.0f) p.throttle = 0;
            for (int i = 0; i < 4; i++) p.spin[i] = v * 2.2f;
            p.speed = v;
            PullOrders o = PullTestStep(t, p, TruckPedal(p), 0.05f);
            if (o.write) p.torqueFactor = o.factor;
            if (o.ended) { char a[1400], b[320]; PullTestText(t, a, sizeof a, b, sizeof b); printf("%s\n%s\nfactor after %.3f state %d\n\n", a, b, p.torqueFactor, t.state); }
            v += 0.5f * p.torqueFactor * 0.05f;
            if (mode == 1) p.torqueFactor = 1.0f;
        }
        printf("mode %d end state %d\n", mode, t.state);
    }
}
