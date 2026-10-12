# Log analysis scripts

Python 3 scripts that read a `Gauges.log` written with `Probe=1`. Run them from outside the folder holding the log, with `python3 -I tools/analysis/<script> Gauges.log`, as a log from someone else is data, not code.

## The gearbox

- `shifts2.py`: per truck, the shifts up and down, the share of the gear's top the floored and the part-pedal shifts up came at, and the shifts taken back within 4 s with the foot on (the first dozen). `shifts_all.py` is the same with every one listed.
- `hunt.py [seconds]`: per truck, the shifts up that a foot-on shift down took back within the seconds given (6 by default), leaving out the game's own picks after a takeover. The measure for the hunt guard of 0.15.2.
- `ctx.py [truck]`: every shift with the state just before it: the speed, the pitch, the trend (rad/s each second) and whether the model was climbing or stuck.
- `kick.py`: per truck, the shifts down with the pedal floored, how many came after a fresh press, how many skipped gears and how many came climbing. The measure for the keyboard kickdowns.
- `shifts.py log [from] [to]`: the shift lines between two times, with the game's own picks.
- `launch.py`: each launch from a standstill: the start gear, the gears used and the time to 2 m/s.
- `plateau.py`: floored stretches at a gear's top with the torque factor and the pedal, for the EngineShare 50 plateau of 0.13.4.
- `cmp.py old new`: two replays of the same drive (`tools/log2drive.py` output run through two builds of the model) side by side, gear by gear.

## The game's values

- `b4.py`, `b4b.py`, `b4c.py`: the action's `B4` (the game's pull times the throttle) against the wheels' share of the gear's top, by gear: how the game's pull curve was found.
- `slip.py`: the driven wheels against the speed over the ground: the grip the gearbox learns.
- `tl.py log from to`, `tl2.py log from to`: a timeline of the gear, the throttle, the speed (and `B4`) between two times, with the other lines in between.
