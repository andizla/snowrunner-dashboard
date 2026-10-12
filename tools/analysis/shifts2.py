import re,sys
truck=None; ev=[]
for l in open(sys.argv[1],encoding='utf-8-sig'):
    m=re.match(r'(\d\d):(\d\d):(\d\d) truck: this is (\S+)',l)
    if m: truck=m.group(4).split('/')[0]; continue
    m=re.match(r'(\d\d):(\d\d):(\d\d) gearbox: (\d+) to (\d+) at (\d+) rpm.*?pedal at ([-\d.]+); the wheels at ([\d.]+) of the gear\'s top \(the game\'s pull ([\d.]+)\) and ([\d.]+) of the new',l)
    if m:
        h,mi,s,a,b,inp,p,x,pl,x2=m.groups(); ev.append((truck,int(h)*3600+int(mi)*60+int(s),f'{h}:{mi}:{s}',int(a),int(b),float(p),float(x),float(x2)))
from collections import defaultdict
by=defaultdict(list)
for e in ev: by[e[0]].append(e)
for t,es in by.items():
    ups=[e for e in es if e[4]>e[3]]; downs=[e for e in es if e[4]<e[3]]
    fl=[e for e in ups if e[5]>=0.9]; pt=[e for e in ups if 0.1<e[5]<0.9]
    rev=0; revs=[]
    for i in range(1,len(es)):
        a,b=es[i-1],es[i]
        if b[1]-a[1]<=4 and a[4]==b[3] and (a[4]>a[3])!=(b[4]>b[3]) and b[5]>0.05 and a[5]>0.05: rev+=1; revs.append((a[2],a[3],a[4],a[5],b[2],b[3],b[4],b[5]))
    print(f'{t}: {len(es)} shifts, {len(ups)} up ({len(fl)} floored: x {min([e[6] for e in fl],default=0):.2f}-{max([e[6] for e in fl],default=0):.2f}), part {len(pt)} x {min([e[6] for e in pt],default=0):.2f}-{max([e[6] for e in pt],default=0):.2f}; {len(downs)} down; reversals foot on within 4 s: {rev}')
    for r in revs[:12]: print('   ',r)
