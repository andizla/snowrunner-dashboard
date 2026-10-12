import re,sys,collections
vals=collections.defaultdict(list)
for f in sys.argv[1:]:
    cur=None
    for s in open(f,errors='replace'):
        m=re.search(r'throttle (-?[\d.]+) \| speed (-?[\d.]+) m/s',s)
        if m and 'probe: flags' in s: cur=dict(thr=-float(m[1]),v=float(m[2])); continue
        m=re.search(r'probe: wheels \(drive:rad/s\) (.*?) \|',s)
        if m and cur:
            ws=[float(w.split(':')[1]) for w in m[1].split() if not w.startswith('0:')]
            cur['w']=sum(ws)/len(ws) if ws else 0; continue
        m=re.search(r'wheels ([\d.]+) rad/s for each m/s \|',s)
        if m and cur and 'w' in cur:
            g=float(m[1])
            if g>0 and cur['v']>3:
                r=cur['w']/(g*cur['v'])
                key='floored' if cur['thr']>0.9 else 'part' if cur['thr']>0.2 else 'off'
                vals[key].append(r)
            cur=None
for k,v in vals.items():
    v.sort(); n=len(v)
    print(k,n,' '.join('%.2f'%v[int(q*(n-1))] for q in (0.05,0.25,0.5,0.75,0.9,0.95,0.98,0.99)))
