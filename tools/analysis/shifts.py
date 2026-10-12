import re,sys
L=open(sys.argv[1],errors='replace').read().splitlines()
lo=sys.argv[2] if len(sys.argv)>2 else '00'; hi=sys.argv[3] if len(sys.argv)>3 else '99'
def sec(t): h,m,s=map(int,t.split(':')); return h*3600+m*60+s
ev=[]
for s in L:
    t=s[:8]
    if not (lo<=t<=hi): continue
    m=re.search(r'gearbox: (\d+) to (\d+) at (\d+) rpm.*pedal at (-?[\d.]+); the wheels at ([\d.]+) of the gear.s top \(the game.s pull ([\d.]+)\) and ([\d.]+) of the new one.s \(([\d.]+)\)',s)
    if m: ev.append((sec(t),t,'shift',int(m[1]),int(m[2]),float(m[4]),float(m[5]),float(m[6]),float(m[7]),float(m[8]))); continue
    m=re.search(r'picked gear (\d+)',s)
    if m: ev.append((sec(t),t,'pick',int(m[1])))
    if 'takes the gears' in s or 'gives the gears back' in s: ev.append((sec(t),t,'take',s[9:90]))
prev=None
for e in ev:
    if e[2]=='shift':
        _,t,_,a,b,p,xf,pf,xt,pt=e
        back = prev and prev[2]=='shift' and prev[3]==b and prev[4]==a and e[0]-prev[0]<=3
        print("%s %d->%d ped %.2f  x %.2f (pull %.2f) -> %.2f (pull %.2f)  gain %+.2f%s"%(t,a,b,p,xf,pf,xt,pt,(xt*pt)/(xf*pf+1e-6)-1 if xf>0 else 0,'  BACK' if back else ''))
        prev=e
    else:
        print(e[1],e[2],e[3]); 
        if e[2]!='pick': prev=None
