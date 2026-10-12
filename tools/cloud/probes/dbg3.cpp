#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "gauges.h"
#include "truck.h"
#include "models.h"
void GaugeLog(const wchar_t *, ...) {}
int main(int argc, char**argv){
  GaugeSettings s; SettingsDefaults(s); s.tachClass=kClassScout; s.idleRpm=800; s.redlineRpm=4850; SettingsClamp(s); GaugeScale k=GaugeScaleOf(s);
  FILE*f=fopen(argv[1],"r"); char line[2048]; float last=-1; TachModel tach={}; BoostModel boost={}; TempModel temp={}; PowertrainModel pw={}; GearboxModel box={};
  int prevGear=0; float lastShiftAt=-100; int lastDir=0; int hunts=0, shifts=0; int printed=0;
  int gameGear=0, oursGear=0; float oursLastAt=-100; int oursLastDir=0, oursHunts=0, oursShifts=0, oursStopHunts=0, gameStopHunts=0; float lastSpin=0; float gameLastAt=-100; int gameLastDir=0, gameHunts=0, gameShifts=0;
  while(fgets(line,sizeof line,f)){
    TruckRaw t={}; int flags,awd,automatic,used,at; float sec;
    if(sscanf(line,"%f %d %f %f %d %d %d %d %f %f %f %f %f %f %d %d %d%n",&sec,&flags,&t.stall,&t.startTimer,&awd,&t.gear,&t.gearEngaged,&automatic,&t.powerFactor,&t.throttle,&t.actionB4,&t.speed,&t.fuel,&t.tank,&t.damage,&t.damageCapacity,&t.gearSpeeds,&at)<17) continue;
    for(int i=0;i<t.gearSpeeds;i++){sscanf(line+at,"%f%n",&t.gearSpeed[i],&used);at+=used;}
    sscanf(line+at,"%d%n",&t.wheels,&used);at+=used; for(int i=0;i<t.wheels;i++){sscanf(line+at,"%d %f%n",&t.drive[i],&t.spin[i],&used);at+=used;}
    t.flags=flags; t.awd=awd; t.automatic=automatic;
    if(last<0||sec-last>5){tach={};pw={};box={};last=sec-0.05f;}
    for(float left=sec-last;left>0.0005f;left-=0.05f){float dt=fminf(left,0.05f); TachStep(tach,t,k,dt); BoostStep(boost,t,TachShare(tach,k),k.boost,0,dt); TempStep(temp,1,t,TachSpeed(tach,k),s,dt);
      PowertrainStep(pw,t,kClassCurves[1],TachSpeed(tach,k),k.max/k.redline,temp.oil,boost.bar/k.boost,dt);
      int g0=box.gear; GearboxStep(box,t,k,pw.pedal,pw.power,dt);
      bool inAuto = t.automatic && t.gearEngaged>=1 && t.gearEngaged<=TruckTopGear(t) && (t.flags&1);
      if(inAuto && g0>=1 && box.gear!=g0){ int dir=box.gear>g0?1:-1; shifts++; if(dir==-lastDir && sec-lastShiftAt<5) hunts++; lastDir=dir; lastShiftAt=sec;
        if(printed<0){printed++; printf("%.0f ours %d->%d raw %.1f smooth %.1f input %.0f pedal %.2f game %d\n",sec,g0,box.gear,TruckDrivenSpin(t),box.spin,box.input,pw.pedal,t.gearEngaged);} }
    }
    bool inAuto = t.automatic && t.gearEngaged>=1 && t.gearEngaged<=TruckTopGear(t) && (t.flags&1);
    float spinNow=TruckDrivenSpin(t);
    if(inAuto){ if(oursGear>=1 && box.gear!=oursGear){int dir=box.gear>oursGear?1:-1; oursShifts++; if(dir==-oursLastDir && sec-oursLastAt<5){ if(fminf(spinNow,lastSpin)<1.0f) oursStopHunts++; else oursHunts++;} oursLastDir=dir; oursLastAt=sec;} oursGear=box.gear; } else oursGear=0;
    if(inAuto){ if(gameGear>=1 && t.gearEngaged!=gameGear && fminf(spinNow,lastSpin)<1.0f && t.gearEngaged>gameGear==(gameLastDir<0) && sec-gameLastAt<5) gameStopHunts++; }
    lastSpin=spinNow;
    if(inAuto){ if(gameGear>=1 && t.gearEngaged!=gameGear){int dir=t.gearEngaged>gameGear?1:-1; gameShifts++; if(dir==-gameLastDir && sec-gameLastAt<5) gameHunts++; gameLastDir=dir; gameLastAt=sec;} gameGear=t.gearEngaged; } else gameGear=0;
    last=sec;
  }
  printf("every step: ours %d shifts %d hunts | at the samples: ours %d shifts %d hunts (+%d near a stop) | game %d shifts %d hunts (%d of them near a stop)\n",shifts,hunts,oursShifts,oursHunts,oursStopHunts,gameShifts,gameHunts,gameStopHunts);
}
