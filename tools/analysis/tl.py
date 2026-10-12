import re,sys
L=open(sys.argv[1],errors='replace').read().splitlines()
lo,hi=sys.argv[2],sys.argv[3]
row={}
for s in L:
    t=s[:8]
    if not (lo<=t<=hi): continue
    if 'gearbox:' in s or 'takeover' in s or 'adopt' in s or 'game' in s and 'probe' not in s:
        print('   ',s[:150]); continue
    m=re.search(r'gear (\d) \(engaged (\d)\) automatic (\d) power ([\d.]+).*throttle (-?[\d.]+) \| speed (-?[\d.]+) m/s',s)
    if m: row=dict(t=t,g=m[1],auto=m[3],pw=m[4],thr=m[5],v=m[6]); continue
    m=re.search(r'engine speed (\d+) rpm, heading for (\d+) \(pedal (-?[\d.]+), wheels ([\d.]+) rad/s of the gear.s ([\d.]+)',s)
    if m: row.update(erpm=m[1],head=m[2],ped=m[3],wh=m[4],top=m[5]); continue
    m=re.search(r'nose (up|down) ([\d.]+) deg',s)
    if m: row['nose']=('+' if m[1]=='up' else '-')+m[2]; continue
    m=re.search(r'automatic \(the mod.s.*gear (\d) of \d \(the game.s (\d)\), converter (\w+(?: \w+)?), input (\d+) rpm, engine (\d+) rpm.*?\| wheels ([\d.]+) rad/s[^|]*\|?(.*)',s)
    if m:
        row.update(mg=m[1],conv=m[3][:6],inp=m[4],meng=m[5],grip=m[6],rest=m[7][:60])
        print("{t} g{g}/m{mg} thr{thr:>7} ped{ped:>5} v{v:>5} wh{wh:>6}/{top:>5} game{erpm:>5}->{head:>5} model{meng:>5} in{inp:>5} {conv} nose{nose} grip{grip} {rest}".format(**{k:row.get(k,'?') for k in 'g mg thr ped v wh top erpm head meng inp conv nose grip rest t'.split()}))
