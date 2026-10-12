#include "models.h"
int main()
{
    const float tops[6] = { 8, 13, 19, 27, 43, 65 };
    for (float share : { 0.0f, 0.5f, 1.0f })
    {
        EngineHand e{ true, &kClassCurves[kClassScout], 6000.0f / 4850.0f, share };
        printf("share %.1f:", share);
        for (int g = 0; g < 5; g++)
        {
            const float r = tops[g + 1] / tops[g], x = EngineUpShare(e, r);
            const float idle = 800, red = 4850;
            const float sched = (idle + (red - idle) * 0.95f) / red;
            const float up = fminf(x, sched), land = up / r;
            printf("  %d>%d up %.3f land %.3f (in %.0f)", g + 1, g + 2, up, land, land * red);
        }
        printf("\n");
    }
    printf("down floored 0.4: %.0f, 0.3: %.0f; room 1.1: %.0f %.0f\n", 800 + 0.4f * 4050, 800 + 0.3f * 4050, 1.1f * (800 + 0.4f * 4050), 1.1f * (800 + 0.3f * 4050));
}
