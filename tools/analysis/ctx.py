import re,sys
# each shift with the probe state just before it: speed, pitch, trend, climbing
truck=None; spd=None; pitch=None; trend=None; climb=False; stuck=''
out=[]
for l in open(sys.argv[1],encoding='utf-8-sig'):
    m=re.match(r'(\d\d:\d\d:\d\d) truck: this is (\S+)',l)
    if m: truck=m.group(2).split('/')[0].rstrip(','); continue
    if ' probe: flags' in l:
        m=re.search(r'speed ([-\d.]+) m/s',l); spd=float(m.group(1)) if m else None; continue
    if ' probe: tilt' in l:
        m=re.search(r'nose (up|down) ([\d.]+) deg',l); pitch=(1 if m.group(1)=='up' else -1)*float(m.group(2)) if m else None; continue
    if ' probe: automatic' in l:
        m=re.search(r'gaining ([-\d.]+) rad/s',l); trend=float(m.group(1)) if m else None
        climb='climbing' in l; stuck='stuck' if 'stuck' in l else ''; continue
    m=re.match(r'(\d\d:\d\d:\d\d) gearbox: (\d+) to (\d+) at (\d+) rpm.*?pedal at ([-\d.]+); the wheels at ([\d.]+) of the gear\'s top \(the game\'s pull ([\d.]+)\) and ([\d.]+) of the new',l)
    if m:
        t,a,b,inp,p,x,pl,x2=m.groups()
        out.append((truck,t,int(a),int(b),float(p),float(x),float(x2),spd,pitch,trend,climb,stuck))
want=sys.argv[2] if len(sys.argv)>2 else None
for o in out:
    if want and not o[0].startswith(want): continue
    tr,t,a,b,p,x,x2,s,pi,trd,cl,st=o
    print(f'{t} {a}->{b} pedal {p:.2f} x {x:.2f}->{x2:.2f} speed {s} pitch {pi} trend {trd} {"climb" if cl else ""} {st}')
