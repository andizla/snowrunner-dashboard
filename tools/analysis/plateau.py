import re,sys
rows=[]; cur={}
for l in open(sys.argv[1],encoding='utf-8-sig'):
    m=re.match(r'(\d\d):(\d\d):(\d\d) probe: automatic \(the mod\'s[^)]*\): gear (\d+) of (\d+) \(the game\'s (-?\d+)\).*?input (\d+) rpm, engine (\d+) rpm.*?gaining ([-\d.]+) rad/s each second, the wheels at ([\d.]+) of the game\'s gear\'s top',l)
    if m:
        h,mi,s,g,top,gg,inp,eng,tr,x=m.groups()
        cur=dict(t=int(h)*3600+int(mi)*60+int(s),ts=f'{h}:{mi}:{s}',g=int(g),top=int(top),gg=int(gg),inp=int(inp),eng=int(eng),tr=float(tr),x=float(x))
        continue
    m=re.match(r'(\d\d:\d\d:\d\d) probe: powertrain \(handed to the game: the torque factor ([\d.]+)[^)]*\): pedal ([-\d.]+)',l)
    if m and cur and m.group(1)==cur['ts']:
        cur['f']=float(m.group(2)); cur['p']=float(m.group(3)); rows.append(cur); cur={}
print(len(rows),'rows')
# plateau episodes: floored, below top gear, x 0.7..0.96, trend small, lasting >=2s
ep=[]; start=None
for r in rows:
    cond = r['p']>=0.9 and r['g']<r['top'] and 0.7<=r['x']<0.97 and abs(r['tr'])<0.8 and r['gg']==r['g']
    if cond:
        if start is None: start=r; acc=[r]
        else: acc.append(r)
    else:
        if start is not None and acc[-1]['t']-start['t']>=2:
            ep.append(acc)
        start=None
if start is not None and acc[-1]['t']-start['t']>=2: ep.append(acc)
for a in ep:
    xs=[r['x'] for r in a]; fs=[r['f'] for r in a]
    print(a[0]['ts'],'-',a[-1]['ts'],'gear',a[0]['g'],'x %.2f-%.2f'%(min(xs),max(xs)),'factor %.2f-%.2f'%(min(fs),max(fs)),'input',a[0]['inp'],a[-1]['inp'],'trend %.2f'%a[-1]['tr'])
