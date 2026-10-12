import re,sys
def rows(f):
    out=[]
    for s in open(f):
        m=re.match(r'\s*([\d.]+) gear\s+(-?\d+)\s+A?\s*pedal\s+(-?[\d.]+) wheels\s+([\d.]+) of\s+([\d.]+).*?km/h.*model: gear (\d+), (\w+), input (\d+)',s)
        if m: out.append(m.groups())
    return out
a=rows(sys.argv[1]); b=rows(sys.argv[2])
print(len(a),len(b))
for x,y in zip(a,b):
    t=float(x[0]); hh=int(t//3600); mm=int(t%3600//60); ss=int(t%60)
    print("%02d:%02d:%02d game %2s pedal %5s wheels %5s/%5s  old %s in %5s  new %s in %5s%s"%(hh,mm,ss,x[1],x[2],x[3],x[4],x[5],x[7],y[5],y[7],'  *' if x[5]!=y[5] else ''))
