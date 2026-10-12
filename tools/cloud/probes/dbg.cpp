#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "gauges.h"
#include "truck.h"
#include "models.h"
void GaugeLog(const wchar_t *, ...) {}
int main(){
  GaugeSettings s; SettingsDefaults(s); GaugeScale k = GaugeScaleOf(s);
  TruckRaw g = {}; g.flags=3; g.powerFactor=1; g.gearSpeeds=6; float sp[6]={1.5f,3,6,12,16,8}; memcpy(g.gearSpeed,sp,sizeof sp); g.wheels=4; g.drive[0]=g.drive[1]=2; g.drive[2]=g.drive[3]=1;
  g.gear=g.gearEngaged=1; g.automatic=1;
  auto wheels=[&](float x){for(int i=0;i<4;i++) g.spin[i]=x;};
  for (float pedal : {1.0f, 0.3f}) {
    GearboxModel b={}; float topSpin=TruckGearTop(g,TruckTopGear(g));
    for(float at=0;at<40;at+=0.05f){ float spin=topSpin*at/40; wheels(spin); int before=b.gear; GearboxStep(b,g,k,pedal,pedal,0.05f);
      if(b.gear!=before) printf("pedal %.1f at %.2f spin %.2f gear %d->%d input %.0f locked %d\n",pedal,at,spin,before,b.gear,b.input,b.locked);}
    printf("end gear %d shifts %d locked %d\n",b.gear,b.shifts,b.locked);
  }
  GearboxModel d={}; d.running=true; d.gear=2; wheels(8); GearboxStep(d,g,k,0,0,0.05f);
  for(float at=0;at<10;at+=0.05f){ wheels(8+0.8f*at); int before=d.gear; GearboxStep(d,g,k,0,0,0.05f); if(d.gear!=before) printf("down at %.2f spin %.2f gear %d->%d input2 %.0f\n",at,g.spin[0],before,d.gear,GearboxInput(g,k,2,g.spin[0]));}
  printf("down end gear %d\n", d.gear);
}
