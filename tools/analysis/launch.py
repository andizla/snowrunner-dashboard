import re,sys
for path in sys.argv[1:]:
    truck=None; state=None
    for l in open(path,encoding='utf-8-sig'):
        m=re.match(r'(\d\d):(\d\d):(\d\d) truck: this is ([^/, ]+)',l)
        if m: truck=m.group(4); continue
        m=re.match(r'(\d\d):(\d\d):(\d\d) probe: flags .*?gear (-?\d+) \(engaged (-?\d+)\).*?throttle ([-\d.]+) \| speed ([-\d.]+) m/s',l)
        if not m: continue
        h,mi,s,g,ge,thr,sp=m.groups(); t=int(h)*3600+int(mi)*60+int(s); g=int(g); thr=-float(thr); sp=float(sp)
        if state is None:
            if abs(sp)<0.1 and thr>0.5 and g>=1: state=(t,f'{h}:{mi}:{s}',g,[]) 
        else:
            state[3].append(g)
            if sp>=2.0:
                print(path[-20:-10],truck,'launch at',state[1],'gears',sorted(set(state[3])),'start gear',state[2],'to 2 m/s in',t-state[0],'s'); state=None
            elif thr<0.1 or t-state[0]>15:
                print(path[-20:-10],truck,'launch at',state[1],'start gear',state[2],'gears',sorted(set(state[3])),'gave up after',t-state[0],'s, speed',sp); state=None
