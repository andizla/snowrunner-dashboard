import re,sys,collections
by=collections.defaultdict(list)
for f in sys.argv[1:]:
    L=open(f,errors='replace').read().splitlines()
    hist=[]; truck='?'; cur=None
    for s in L:
        m=re.search(r'truck: this is (\S+)',s)
        if m: truck=m[1].split('/')[0]
        m=re.search(r'gear (\d) \(engaged (-?\d)\) automatic (\d) power ([\d.]+).*throttle (-?[\d.]+) \| speed (-?[\d.]+) m/s.*B4 (-?[\d.]+)',s)
        if m:
            cur=dict(g=int(m[2]),fl=float(m[5])<=-0.99,b4=float(m[7]),pw=float(m[4]),v=float(m[6]))
            hist.append(cur); hist=hist[-3:]; continue
        m=re.search(r'wheels \(drive:rad/s\) (.*?) \|',s)
        if m and cur:
            ws=[float(w.split(':')[1]) for w in m[1].split() if not w.startswith('0:')]
            cur['spread']=(max(ws)-min(ws))/max(1e-3,max(ws)) if ws else 1
        m=re.search(r'wheels ([\d.]+) rad/s of the gear.s ([\d.]+)',s)
        if m and cur and len(hist)==3 and all(h['fl'] and h['g']==cur['g'] for h in hist) and cur['g']>=1 and float(m[2])>0 and cur.get('spread',1)<0.15 and cur['pw']>=0.99:
            x=float(m[1])/float(m[2]); by[(truck,min(int(x*20),25))].append(cur['b4']); cur=None
for k in sorted(by):
    v=sorted(by[k]); 
    if len(v)<3: continue
    print(k[0][:12].ljust(12), "x %.2f-%.2f"%(k[1]/20,(k[1]+1)/20), "n%3d"%len(v), "B4 q25 %.2f median %.2f q75 %.2f"%(v[len(v)//4],v[len(v)//2],v[3*len(v)//4]))
