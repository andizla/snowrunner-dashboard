import re,sys
from collections import deque
truck=None; thr=deque(maxlen=10); trend=0; climb=False
stats={}
for l in open(sys.argv[1],encoding='utf-8-sig'):
    m=re.match(r'(\d\d:\d\d:\d\d) truck: this is (\S+)',l)
    if m: truck=m.group(2).split('/')[0].rstrip(','); continue
    if ' probe: flags' in l:
        m=re.search(r'throttle ([-\d.]+)',l); thr.append(-float(m.group(1))); continue
    if ' probe: automatic' in l:
        m=re.search(r"gaining ([-\d.]+) rad/s",l); trend=float(m.group(1)) if m else trend; climb="climbing" in l; continue
    m=re.match(r'(\d\d:\d\d:\d\d) gearbox: (\d+) to (\d+) .*?pedal at ([-\d.]+); the wheels at ([\d.]+) of the gear\'s top \(the game\'s pull ([\d.]+)\) and ([\d.]+) of the new',l)
    if not m: continue
    t,a,b,p,x,pl,x2=m.groups(); a,b,p=int(a),int(b),float(p)
    s=stats.setdefault(truck,{'down_on':0,'press':0,'skip':0,'climb':0})
    if b<a and p>=0.9:
        s['down_on']+=1
        pressed=min(thr)<0.5 if thr else False
        if pressed: s['press']+=1
        if a-b>=2: s['skip']+=1
        if climb: s['climb']+=1
for k,v in stats.items(): print(k,v)
