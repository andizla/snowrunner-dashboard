# log2drive.py <Gauges.log> <drive.txt>: the probe's samples of a Gauges.log as the replay of models_test reads them
import re, sys
flags_rx = re.compile(r'^(\d+):(\d+):(\d+) probe: flags ([0-9A-F]+) .*? stall ([-\d.]+) start ([-\d.]+) \| handbrake \d awd (\d) diff \d \| gear (-?\d+) \(engaged (-?\d+)\) automatic (\d) power ([\d.]+) = \S* \| throttle (-?[\d.]+) \| speed (-?[\d.]+) m/s .*?\| fuel ([\d.]+) of ([\d.]+) l \| engine damage (\d+) of (\d+).*\| B4 (-?[\d.]+)')
wheels_rx = re.compile(r'^(\d+):(\d+):(\d+) probe: wheels \(drive:rad/s\) (.*?) \| gear speeds (.*?) \|')
out = []
pending = None
for line in open(sys.argv[1], encoding='utf-8-sig', errors='replace'):
    m = flags_rx.match(line)
    if m:
        pending = m
        continue
    w = wheels_rx.match(line)
    if w and pending:
        g = pending.groups()
        sec = int(g[0]) * 3600 + int(g[1]) * 60 + int(g[2])
        wheels = [x.split(':') for x in w.group(4).split()]
        speeds = w.group(5).split()
        row = [str(sec), str(int(g[3], 16)), g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[17], g[12], g[13], g[14], g[15], g[16], str(len(speeds))] + speeds
        row += [str(len(wheels))] + [v for pair in wheels for v in pair]
        out.append(' '.join(row))
        pending = None
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
print(len(out), 'samples')
