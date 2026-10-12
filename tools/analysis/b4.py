import re,sys,collections
pts=[]
for f in sys.argv[1:]:
    L=open(f,errors='replace').read().splitlines()
    cur=None; prevfl=False; truck='?'
    for s in L:
        m=re.search(r'truck: this is (\S+)',s)
        if m: truck=m[1].split('/')[0]
        m=re.search(r'gear (\d) \(engaged (-?\d)\) automatic (\d) power ([\d.]+).*throttle (-?[\d.]+) \| speed (-?[\d.]+) m/s.*B4 (-?[\d.]+)',s)
        if m:
            fl=float(m[5])<=-0.99
            cur=dict(g=int(m[2]),thr=float(m[5]),v=float(m[6]),b4=float(m[7]),pw=float(m[4]),fl=fl and prevfl,t=s[:8])
            prevfl=fl; continue
        m=re.search(r'wheels ([\d.]+) rad/s of the gear.s ([\d.]+)',s)
        if m and cur and cur['fl'] and cur['g']>=1 and float(m[2])>0:
            x=float(m[1])/float(m[2]); pts.append((truck,cur['g'],round(x,2),cur['b4'],cur['pw'],cur['t']))
            cur=None
by=collections.defaultdict(list)
for tr,g,x,b,pw,t in pts:
    by[(tr,min(int(x*10),12))].append(b)
for k in sorted(by):
    v=by[k]; v.sort()
    print(k[0][:22].ljust(22), "x %.1f-%.1f"%(k[1]/10,(k[1]+1)/10), "n%3d"%len(v), "B4 median %.2f  min %.2f max %.2f"%(v[len(v)//2],v[0],v[-1]))
