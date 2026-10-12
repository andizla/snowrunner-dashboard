import re,sys
L=open(sys.argv[1],errors='replace').read().splitlines()
lo,hi=sys.argv[2],sys.argv[3]
row={}
for s in L:
    t=s[:8]
    if not (lo<=t<=hi): continue
    if 'gearbox:' in s or 'powertrain:' in s and 'probe' not in s:
        print('   ',s[9:170]); continue
    m=re.search(r'gear (-?\d) \(engaged (-?\d)\) automatic (\d) power ([\d.]+).*throttle (-?[\d.]+) \| speed (-?[\d.]+) m/s.*B4 (-?[\d.]+)',s)
    if m: row=dict(t=t,g=m[2],thr=m[5],v=float(m[6]),b4=m[7]); continue
    m=re.search(r'wheels ([\d.]+) rad/s of the gear.s ([\d.]+)',s)
    if m: row['x']=float(m[1])/max(float(m[2]),1e-3); continue
    m=re.search(r'handed to the game: the torque factor ([\d.]+).*?torque ([\d.]+) of the most at (\d+) rpm \(curve ([\d.]+), cold x([\d.]+), boost x([\d.]+)',s)
    if m: row.update(f=m[1],tq=m[2],rpm=m[3],boost=m[6]); continue
    m=re.search(r'automatic \(the mod.s.*gear (\d) of \d \(the game.s (-?\d)\), (converter slipping|locked up), input (\d+) rpm, engine (\d+) rpm, torque x([\d.]+).*gaining (-?[\d.]+)',s)
    if m:
        row.update(mg=m[1],cv='slip' if m[3][0]=='c' else 'lock',ratio=m[6],gain=m[7])
        print("{t} g{g}/m{mg} thr{thr:>7} v{v:5.2f} x{x:5.2f} B4 {b4:>5} | f {f:>4} tq {tq:>4} @{rpm:>4} boost{boost:>5} | {cv} r{ratio} gain{gain:>6}".format(**{**{k:'?' for k in 'g mg thr v x b4 f tq rpm boost cv ratio gain t'.split()},**row}))
