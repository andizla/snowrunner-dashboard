#define main models_main
#include "../../../../../home/user/snowrunner-dashboard/test/models_test.cpp"
#undef main
int main()
{
    const TorqueCurve &scout = kClassCurves[kClassScout];
    PullShape sh{ &scout, 7000.0f / 5600.0f };
    for (float r : {1.24f, 1.375f, 1.5f, 1.64f, 1.89f}) printf("r %.3f game up %.3f down %.3f | engine up %.3f down %.3f\n", r, GearUpShare(r), GearDownShare(r), GearUpShare(r, sh), GearDownShare(r, sh));
    for (float x : {0.2f, 0.5f, 0.8f, 0.9f, 0.95f, 1.0f}) printf("x %.2f lift %.3f enginepull %.3f T %.3f\n", x, TorqueLift(1.0f, 1.0f, x), EnginePull(scout, 1.25f, x), TorqueAt(scout, x, 1.25f));
    return 0;
}
