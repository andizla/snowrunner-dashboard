#include <windows.h>
#include <stddef.h>
#include "/home/user/snowrunner-manual-transmission/DLL/state_export.h"
#include "/home/user/snowrunner-dashboard/src/smt.h"
#define SAME(a,b) static_assert(offsetof(SmtSharedState,a)==offsetof(SmtBlock,b) && sizeof(((SmtSharedState*)0)->a)==sizeof(((SmtBlock*)0)->b), #a);
SAME(magic,magic) SAME(version,version) SAME(size,size) SAME(sequence,sequence) SAME(tick,tick) SAME(vehicle,vehicle) SAME(gear,gear) SAME(maxGear,maxGear)
SAME(range,range) SAME(flags,flags) SAME(clutch,clutch) SAME(powerCoef,powerCoef) SAME(stalls,stalls) SAME(shifts,shifts)
static_assert(sizeof(SmtSharedState)==sizeof(SmtBlock));
static_assert(SMT_STATE_MAGIC==kSmtMagic && SMT_STATE_VERSION==kSmtVersion);
static_assert(SMT_STATE_VEHICLE==kSmtVehicle && SMT_STATE_AUTO_RANGE==kSmtAutoRange && SMT_STATE_CLUTCH==kSmtClutch && SMT_STATE_REQUIRE_CLUTCH==kSmtRequireClutch &&
  SMT_STATE_REQUIRE_GEAR_HELD==kSmtRequireGearHeld && SMT_STATE_IMMERSIVE==kSmtImmersive && SMT_STATE_GAME_SHIFTING_OFF==kSmtGameShiftingOff &&
  SMT_STATE_CLUTCH_AXIS==kSmtClutchAxis && SMT_STATE_FOCUSED==kSmtFocused);
int main(){}
