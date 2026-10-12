import re,sys
# a shift up undone by a foot-on shift down within W seconds, the game's own picks left out
W=float(sys.argv[2]) if len(sys.argv)>2 else 6
truck=None; last=None; tap=False; res={}
def sec(t): h,m,s=map(int,t.split(':')); return h*3600+m*60+s
for l in open(sys.argv[1],encoding='utf-8-sig'):
    m=re.match(r'(\d\d:\d\d:\d\d) truck: this is (\S+)',l)
    if m: truck=m.group(2).split('/')[0].rstrip(','); last=None; continue
    if "game's own automatic picked" in l: tap=True; continue
    if 'takes the gears' in l or 'gives the gears back' in l: last=None; continue
    m=re.match(r'(\d\d:\d\d:\d\d) gearbox: (\d+) to (\d+) .*?pedal at ([-\d.]+);.*?the wheels at ([\d.]+) of the gear',l)
    if not m: continue
    t,a,b,p,x=m.groups(); a,b,p,x=int(a),int(b),float(p),float(x)
    r=res.setdefault(truck,{'ups':0,'undone':[],})
    if tap: tap=False; last=None; continue
    if b>a and p>0.05: r['ups']+=1; last=(sec(t),a,b,t)
    elif b<a and p>0.05 and last and last[2]==a and last[1]==b and sec(t)-last[0]<=W:
        r['undone'].append(f'{last[3]} {b}<->{a} {sec(t)-last[0]}s'); last=None
    else: last=None
for k,v in res.items(): print(f"{k}: {v['ups']} ups, {len(v['undone'])} undone within {W:.0f} s:", ' | '.join(v['undone']))
