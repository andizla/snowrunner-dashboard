# Part 2 of the Gauges manual: the technical reference. The models are re-run here from the 0.15.7 source's own
# formulas and constants (models.h, gauges.h, maps.h), so every chart and table is the mod's arithmetic.
from math import exp, log, cos, pi, sqrt
from make_manuals import table
from figs import chart, diagram


def clamp(v, a, b):
    return max(a, min(b, v))


def eq(body, n=''):
    return f'<div class="eq">{body}' + (f'<span class="n">({n})</span>' if n else '') + '</div>'


# ---------------------------------------------------------------- scales (gauges.h:194)
CLASSES = {'truck': (650, 2200, 3000, 100, 1.8), 'scout': (800, 5500, 7000, 120, 1.0), 'heavy': (600, 1900, 2500, 80, 2.0)}


# ---------------------------------------------------------------- the game's pull (models.h:1081-1088)
def game_pull(x):
    return clamp(min(2.8 * x - 0.25, 2.1 * (1 - x)), 0, 1)


def gear_pull(x):
    return x * game_pull(x)


def up_share(r):
    if r <= 1.01:
        return 1.0
    x = 0.5
    while x <= 1.0 + 1e-9:
        if gear_pull(x / r) >= 1.05 * gear_pull(x):
            return x
        x += 0.005
    return 1.0


def down_share(r):
    x = 0.6
    while x >= 0.1 - 1e-9:
        if x * r < 1 and gear_pull(x * r) >= 1.05 * gear_pull(x):
            return x
        x -= 0.005
    return None


# ---------------------------------------------------------------- the needle (models.h:153-167)
def needle_n(w, p):
    q = sqrt(p)
    w = clamp(w, 0, 1.1)
    s = 1 - min(w, 1)
    return w + 0.65 * q * s ** 1.5


# ---------------------------------------------------------------- torque curves (models.h:887-961)
CURVES = {'truck': [(0.25, 0.55), (0.45, 0.85), (0.6, 1.0), (0.8, 1.0), (1.0, 0.85)],
          'scout': [(0.15, 0.5), (0.35, 0.75), (0.6, 0.95), (0.75, 1.0), (1.0, 0.85)],
          'heavy': [(0.25, 0.6), (0.4, 0.9), (0.55, 1.0), (0.75, 1.0), (1.0, 0.8)]}


def from_peak(peak, red, aspirated):
    P = clamp(peak / red, 0.15, 0.95)
    pts = [(0.4 * P, 0.7), (P, 1.0), (P + 0.5 * (1 - P), 0.92), (1.0, 0.8)] if aspirated else \
          [(0.5 * P, 0.6), (0.85 * P, 1.0), (min(1.5 * P, 0.95), 1.0), (1.0, 0.8)]
    out = []
    for p in pts:
        if not out or p[0] > out[-1][0] + 0.005:
            out.append(p)
    return out


def torque_at(curve, x, top):
    if x <= curve[0][0]:
        return curve[0][1]
    for (a, ya), (b, yb) in zip(curve, curve[1:]):
        if x <= b:
            return ya + (yb - ya) * (x - a) / (b - a)
    xl, yl = curve[-1]
    return 0.0 if top <= xl else yl * clamp(1 - (x - xl) / (top - xl), 0, 1)


# ---------------------------------------------------------------- coolant and oil (models.h:397-647), defaults
def heat(load):
    return 0.15 + 0.85 * clamp(load, 0, 1)


def thermo(T):
    return 0.15 + 0.85 * clamp((T - 82) / 10, 0, 1)


def fan(T):
    return clamp((T - 98) / 4, 0, 1)


def airflow(kmh, T, full=30):
    return 0.35 + 0.65 * clamp(kmh / max(full, 10), 0, 1) + 0.3 * fan(T)


def cooling(T, tout, kmh, full=30, water=0):
    return (T - tout) / (97 - 15) * thermo(T) * (airflow(kmh, T, full) + water)


def temp_path(a, b, load, kmh, full):
    s, T = 0.0, a + 0.125
    while T < b:
        s += 0.25 / max(heat(load) - cooling(T, 15, kmh, full), 0.001)
        T += 0.25
    return s


R_WARM = temp_path(15, 85, 0.5, 13, 30) / (3 * 60)
R_HOT = temp_path(90, 118, 1, 0, 10) / (1.5 * 60)
R_OFF = log((90 - 15) / (40 - 15)) / (15 * 60)


def temp_step(T, load, kmh, dt, tout=15):
    h, c = heat(load), cooling(T, tout, kmh)
    rate = 1.0 if h < c else R_WARM + (R_HOT - R_WARM) * clamp((T - 85) / 5, 0, 1)
    return min(140, T + rate * (h - c) * dt)


def equilibrium(load, kmh, tout=15):
    lo, hi = tout, 140.0
    for _ in range(60):
        m = (lo + hi) / 2
        if heat(load) > cooling(m, tout, kmh):
            lo = m
        else:
            hi = m
    return (lo + hi) / 2


def oil_reaches(lag, until):
    T, oil, t = 15.0, 15.0, 0.0
    while t < until:
        T = temp_step(T, 0.5, 13, 0.25)
        target = T + 4 * 0.5 * 0.5 + 10 * 0.5
        oil += (target - oil) * (1 - exp(-0.25 / lag))
        t += 0.25
    return oil


def oil_lag(until=300.0):
    lo, hi = 1.0, 3600.0
    for _ in range(40):
        m = sqrt(lo * hi)
        if oil_reaches(m, until) >= 85:
            lo = m
        else:
            hi = m
    return sqrt(lo * hi)


# ---------------------------------------------------------------- boost (models.h:707-869)
# review 12's turbo: each class's (name, fuel, most bar, n0, n1, tau_red s), and the profiles on a truck's (n0, n1, tau_red, most x)
TURBO_CLASSES = [('Truck', 'diesel', 1.8, 0.36, 0.60, 0.045), ('Scout', 'petrol', 1.0, 0.20, 0.30, 0.010), ('Heavy', 'diesel', 2.0, 0.37, 0.68, 0.085)]
PROFILES = {'Standard': (0.36, 0.60, 0.045, 1.0), 'Small and quick': (0.36, 0.48, 0.0225, 1.0), 'Big and slow': (0.504, 0.84, 0.0675, 1.0),
            'Two-stage': (0.36, 0.45, 0.045, 1.3)}


# ---------------------------------------------------------------- the day (maps.h:130-143)
def day_share(h):
    if 5 <= h <= 15:
        return 0.5 - 0.5 * cos(pi * (h - 5) / 10)
    since = h - 15 if h > 15 else h + 9
    return 0.5 + 0.5 * cos(pi * since / 14)


REGIONS = [('Michigan', 12), ('Alaska', -12), ('Taymyr', -20), ('Kola Peninsula', -10), ('Yukon', -15), ('Wisconsin', 8),
           ('Amur', 2), ('Don', 16), ('Maine', 10), ('Tennessee', 20), ('Ontario', 5), ('British Columbia', 3),
           ('Scandinavia', -4), ('North Carolina', 22), ('Almaty Region', 24), ('Austria', 8), ('Quebec', 0), ('Washington', 10),
           ('Chiapas', 26), ('Belozersk Glades', 18), ('Zurdania', 22), ('Switzerland', -10)]


def s_intro():
    return '''
<div class="part break">Part 2. Technical reference</div>
<p class="intro">This part is for readers who want the workings. The game keeps no engine speed, no temperatures, no boost, no
brake air and no range, so most of the cluster is models: this part gives each one as the code has it, with its formulas, every
constant, and where a number comes from when the code says so; then how the mod finds the truck in the game's memory, what it
reads and writes there, and what it cannot do. The numbers are those of version 0.18.4; file and line references
(<code>models.h:153</code>) point into its source. "The Steam build" is the game build of 22 July 2026 (time stamp
<code>6a607c05</code>); an RVA is an offset from the start of <code>SnowRunner.exe</code> in memory, in that build. The charts are
the models re-run with their default settings.</p>
<div class="box"><b>Notation.</b> <i>p</i> is the pedal, 0 to 1, and <i>b</i> the brake. spin is the mean turning speed of the
driven wheels in rad/s. top(<i>g</i>) is gear <i>g</i>'s top wheel speed. idle, red and max are the tachometer's idle, the start of
its red zone and its end. share(rpm) = clamp((rpm − idle)/(red − idle), 0, 1) and speed(rpm) = clamp(rpm/red, 0, 1).
clamp(<i>v</i>, <i>a</i>, <i>b</i>) holds <i>v</i> between <i>a</i> and <i>b</i>. A pass is one loop of the mod's thread.</div>
'''


def s_overview():
    boxes = {'rs': (20, 18, 150, 46, ['ASI loader', 'loads Gauges.asi'], 'other'),
             'thr': (245, 18, 150, 46, ['Gauges thread', '20 passes a second'], 'mod'),
             'ini': (470, 18, 150, 46, ['Gauges.ini and files', 'trucks, gearboxes, engines'], 'file'),
             'mem': (20, 110, 150, 54, ['Game memory', 'truck, wheels, engine,', 'clock, menus'], 'game'),
             'mdl': (245, 110, 150, 54, ['The models', 'engine, heat, boost,', 'air, range, tilt, gears'], 'mod'),
             'save': (470, 110, 150, 54, ['The newest save', 'which map'], 'file'),
             'draw': (20, 206, 150, 50, ['The cluster', 'drawn every frame'], 'other'),
             'gb': (245, 206, 150, 50, ['Gauges block', 'written'], 'mem'),
             'tb': (470, 206, 150, 50, ['Tire and gearbox blocks', 'read'], 'mem')}
    arrows = [('rs', 'thr', 'starts'), ('ini', 'thr', 'settings', {'both': True}), ('mem', 'thr', 'reads; writes a few', {'both': True, 'lx': -6}),
              ('thr', 'mdl', 'steps', {'lx': 20, 'ly': 3}), ('save', 'mdl', 'map', {'lx': 0}), ('mdl', 'draw', 'state', {'lx': 6}),
              ('mdl', 'gb', 'writes', {'lx': 20, 'ly': 3}), ('tb', 'mdl', 'reads', {'lx': 6})]
    return f'''
<h2>How the mod runs</h2>
<p><code>Gauges.asi</code> is loaded into the game by an ASI loader (the release's <code>version.dll</code>) and draws each frame from its own hook on the game's Present (Drawing, below). It acts only
inside <code>SnowRunner.exe</code>, and a mutex <code>Local\\SnowRunnerGauges-&lt;process id&gt;</code> lets one copy run. It starts
one thread that loops with a 50 ms sleep, about 20 passes a second (<code>gauges.cpp:2060</code>). Each pass reads the truck,
steps every model by the time since the last pass, Δ<i>t</i> = clamp(<i>t</i> − <i>t</i><sub>last</sub>, 0.001, 0.2) s (0.05 s after a
gap of a second or more), writes what it writes, and hands the state to the drawing. The drawing runs at the frame rate and lets
the two big needles glide between passes with a 0.05 s time constant.</p>
{diagram(boxes, arrows, h=270, cap='The parts and what flows between them.')}
<p>The models only step while you drive: no menu, map, garage or viewer open, not loading, and the game's simulated clock
(vehicle +0x114) having moved in the last second, so a paused game stops them. The cluster shows after half a second of driving.</p>
'''


def s_scales():
    rows = [[k.title(), f'{v[0]} rpm', f'{v[1]} rpm', f'{v[2]} rpm', f'{v[3]} km/h', f'{v[4]:g} bar'] for k, v in CLASSES.items()]
    return f'''
<h2>Scales and classes</h2>
{table(['Class', 'Idle', 'Red zone from', 'Tachometer to', 'Speedometer to', 'Boost to'], rows, 'The class scales (gauges.h:194); no stated source')}
<p>A nonzero <code>IdleRpm</code>, <code>RedlineRpm</code>, <code>MaxRpm</code>, <code>SpeedMax</code> or <code>BoostMax</code>, from
a truck's own file or from <code>[Gauges]</code>, replaces the class value. The scale is then made to fit
(<code>GaugeScaleOf</code>, <code>gauges.h:198-211</code>): max is rounded up to a step of 1000 rpm (500 below 5000), red is held to
at most max, and idle to at most 0.8 red.</p>
<p>The scales, the automatic and the torque curves go by one class (<code>ClassInForce</code> in <code>models.h</code>,
BRIEF_truck_keys_per_truck.md, ruling 3). It is the class picked for the truck (<code>Class</code>, 0 to 2, in the scales in force
for it: its own in <code>[Truck:&lt;id&gt;]</code>, else its kind's in <code>[Truck]</code>), else the game's type, else the weight,
else the class picked on the plain row, else truck. The plain row's pick never goes over the type. <code>GaugeSettings::tachClass</code>
holds the pick or <code>kClassAuto</code>; the class in force is put into <code>scaleClass</code> by the mod's pass, and
<code>GaugeScaleOf</code> goes by that: one way for the dials, the log and the tab. The weight and the type are read at the top of a
truck's first pass (<code>ClassLook</code>), before the scales are taken, and <code>ScaleClassStep</code> keeps the class of the pass
before until they were tried, so the red zone does not jump a pass later. An old <code>TachClass</code> is never read as a class: one
log line a file (<code>truck class: the file of</code>, <code>truck class: Gauges.ini holds a class saved by an older version</code>), the key goes when that level's
scales are next written, and no read writes a file. A level's scales are whole: a section with any scale key gives all seven over the
plain ones, and without <code>Class</code> it is Auto. An edit with the id read writes all seven into the truck's section
(<code>settings: saved, this truck's scales in [Truck:&lt;id&gt;] of</code> the file); giving up acts at the level in force (<code>SettingsDrop</code>). The class by
the truck's mass (model +0x784, without addons, cargo or trailer): up to 6000 kg a scout, under 16000 kg a
truck, from 16000 kg a heavy truck (<code>models.h:1282-1292</code>; the TUZ 108 weighs 4.1 t, the Tayga 12.0 t, a twin-steer
Western Star 20.3 t). Weight cannot tell every scout from a truck: the Fleetstar (6314 kg) counts as a truck.</p>
<p><b>The game's own truck type.</b> <code>truck_type.h</code>, <code>ClassByType</code> in <code>models.h</code>. Every truck file
names its role in <code>TruckType</code>, and the game keeps it as a text in its list of truck records, "Undefined" where the file has
none. <code>SCOUT</code> is a scout whatever the truck weighs. <code>OFFROAD</code>, <code>HIGHWAY</code>, <code>HEAVY_DUTY</code> and
<code>HEAVY</code> are never a scout: truck or heavy by the 16000 kg border alone. The game's <code>HEAVY</code> is a role and not a
weight, so it does not make a truck heavy here. Any other text, and no text, leaves the weight's rule whole. The order is: the class picked
for the truck, then the type, then the weight. The route: the truck's name is the text at model + 0x588; the list's begin and
end and a record's size come from the game's own lookup by name, the text's place in a record from the parse of the attribute, both
found by pattern and read from the matched bytes (<code>truck type: the game's list of truck records at +</code>). A size outside
0x100 to 0x1000 or a text past the record's end is refused. The list is walked once a truck, on the mod's thread, by guarded reads
alone; nothing is written. A list of more than 4096 records, a name of none or more than 255 characters and a type of 16 characters or
more are not followed. Each failure gives the weight's rule and its reason in the log. One line a truck names the name read, the type,
the class by the type, the class by the weight and the class taken.</p>
'''


def s_gears():
    xs = [k * 0.005 for k in range(0, 201)]
    fig = chart([('The game\'s pull, GamePull(x)', [(v, game_pull(v)) for v in xs]),
                 ('Against other gears, x · GamePull(x)', [(v, gear_pull(v)) for v in xs], {'dash': '5 3'})],
                (0, 1), (0, 1.05), 0.1, 0.1, 'x: the wheels\' speed as a share of the gear\'s top', 'Share of the engine\'s torque',
                'How the game hands out an engine\'s torque by the wheel speed, as the mod models it.', legend='tr',
                yfmt=lambda v: f'{v:.1f}', xfmt=lambda v: f'{v:.1f}')
    rows = []
    for r in [1.2, 1.35, 1.5, 1.75, 2.0, 2.5]:
        d = down_share(r)
        rows.append([f'{r:g}', f'{up_share(r):.3f}', f'{d:.3f}' if d else 'none'])
    t = table(['Step to the next gear, top(g+1)/top(g)', 'Floored up at, share of red', 'Floored down at, share of red'], rows,
              'Where the automatic shifts floored with the game\'s own engine')
    return f'''
<h2>The gearbox as the game has it</h2>
<p>The truck keeps a list of numbers, one per gear (truck action +0x58): reverse, 1 to <i>N</i>, high. The game turns a gear's
number <i>a</i> into the gear's top wheel speed (<code>TruckGearTop</code>, <code>models.h:127-135</code>):</p>
{eq('top(<i>g</i>) = 2<i>a</i> + 5 rad/s,    top(high) = <i>a</i> + 2', 1)}
<p class="where">A numbered gear's top is learned where the game's own pull reaches nothing (review 11, <code>GearTopStep</code>): a
floored plateau, the pedal at 0.9 or more, +0xB4 at 0.02 or under, the power factor 1, the wheels between 0.85 and 1.15 of 2<i>a</i> + 5
and within 1% of their mean for a second, on ground within 2° by the path, gives the wheels' mean as the gear's top. It is kept as
its share of 2<i>a</i> + 5, for each truck kind and gearbox in the truck's file (<code>TopShare</code><i>g</i><code>.&lt;gearbox&gt;</code>),
as one gearbox's name comes with other gear speeds on another truck. A share is used only within 0.85 to 1.15, averaged with the next
plateau's within 15 % and replaced by it past that; until then 2<i>a</i> + 5. A <code>Top</code><i>g</i> in rad/s of an older build in
the gearbox's file is not read. Floored in A at the game's limit (+0xB4 at 0.02 or under, the wheels gaining under 1% for 1.2 s, the
gear's hold past, no hunt guard keeping the gear), the automatic shifts up one, whatever the mod takes the top for. The rule is armed
only once +0xB4 has read over 0.1 since the automatic's model began: a build where it holds no pull reads 0 throughout. The high gear's <i>a</i> + 2 holds (an HX 520 sat at 15.8 to 16.3 rad/s
in H against 16 in 11 floored plateaus). The testers' logs of 0.17.0 at hand had plateaus at the top of the first gear (0.92 to 1.03
of 2<i>a</i> + 5) and of H (1.03) only; the review read 0.90 to 0.99 in some gears from the pull's fall.</p>
<p class="where">In first gear with the automatic off, the low range scales it: top = 1 + (top − 1) · clamp(power factor, 0, 1), with the
power factor at truck action +0x38. The game's low ranges are L 0.45, L− 0.2 and L+ 1; the lamp reads below 0.325 as L−, below 0.725
as L, otherwise L+.</p>
<p>The game does not give an engine's torque by its engine speed. It gives it by how far the wheels are toward the gear's top,
read from truck action +0xB4 with the pedal floored and steady on the Defender and another truck (<code>models.h:1081-1088</code>):</p>
{eq('GamePull(<i>x</i>) = clamp( min(2.8 <i>x</i> − 0.25, 2.1 (1 − <i>x</i>)), 0, 1 ),    <i>x</i> = spin / top(<i>g</i>)', 2)}
<p class="where">All of it from <i>x</i> ≈ 0.45 to 0.52, falling in a line to nothing at the top. In 0.12.5's test a power factor that doubled
a gear's top pulled less, so the game multiplies the torque as a gear's top is lower; at a given road speed a gear pulls
<i>x</i> · GamePull(<i>x</i>) against the others. A gear revved near its top pulls a fraction of the next one's.</p>
{fig}
<p>So the gear that pulls the most has the wheels near half its top. Floored, the automatic shifts up at the first share
<i>x</i> ≥ 0.5 (in steps of 0.005) where the next gear pulls 5% more, and down at the first share from 0.6 down where the gear
below does (<code>GearUpShare</code>, <code>GearDownShare</code>, <code>models.h:1313-1328</code>):</p>
{eq('up: (<i>x</i>/<i>r</i>) · GamePull(<i>x</i>/<i>r</i>) ≥ 1.05 · <i>x</i> · GamePull(<i>x</i>),    <i>r</i> = top(<i>g</i>+1)/top(<i>g</i>)', 3)}
{t}
'''


def s_rpm():
    ws = [k * 0.005 for k in range(0, 221)]
    ser = [(f'pedal {p:g}', [(w, needle_n(w, p)) for w in ws]) for p in [0, 0.25, 1]]
    fig = chart(ser, (0, 1.1), (0, 1.2), 0.1, 0.1, 'Wheels\' speed as a share of the gear\'s top', 'n: the needle from idle (0) to red (1)',
                'Where the needle heads in gear. Floored at a standstill it stands at 0.65 of the way to the red zone; wheelspin past the top takes it to 1.1.',
                legend='br', yfmt=lambda v: f'{v:.1f}', xfmt=lambda v: f'{v:.1f}')
    return f'''
<h2>Engine speed</h2>
<p>The needle heads for a target worked out from the wheels and the pedal (<code>TachTarget</code>, <code>models.h:153-167</code>).
With <i>q</i> = √<i>p</i>:</p>
{eq('in gear:  <i>w</i> = clamp(spin / top(<i>g</i>), 0, 1.1),   <i>s</i> = 1 − min(<i>w</i>, 1),   <i>t</i> = clamp((<i>w</i> red − idle) / (red − idle), 0, 1.1),   <i>n</i> = <i>t</i> + (<i>w</i> − <i>t</i>) <i>q</i> + 0.65 <i>q s</i><sup>1.5</sup>', 4)}
{eq('in neutral:  <i>n</i> = 0.9 <i>q</i>', 5)}
{eq('target = (idle + (red − idle) · <i>n</i>) · alive²,    alive = clamp(1 − 0.714 · stall, 0, 1)', 6)}
<p class="where">stall is the game's stall timer (vehicle +0x11C); the engine dies at 1.4 s, where alive reaches 0. The wheels at the
gear's top put the needle at the red zone; floored at a standstill <i>n</i> = 0.65, floored in neutral 0.9. At zero pedal the wheels turn the engine in proportion, never under idle (<i>t</i>), so a truck that creeps at idle shows idle; the map from idle comes in with <i>q</i>. Starting, the target is
idle × clamp(1 − timer/1.8, 0, 1) on the game's start timer (vehicle +0x730). With the engine off it is 0.</p>
{fig}
<p>The needle follows with a lag (<code>TachStep</code>, <code>models.h:178-197</code>):</p>
{eq('rpm ← rpm + (target − rpm) · (1 − e<sup>−Δ<i>t</i>/τ</sup>),    τ = 0.18 s rising, 0.25 s falling, 0.4 s engine off', 7)}
<p class="where">While the gear changes, and for 0.3 s after, the target is × 0.85, the dip of a shift. The first pass on a truck puts the
needle at the target at once. Under the mod's automatic the needle follows the torque converter's engine speed instead (see the
automatic), with the same lag and no dip.</p>
<p>The probe also logs the game's own engine value, which the game burns fuel by (<code>models.h:236-260</code>): 0.19 at idle, 0.69
floored, scaled by the engine's damage. The cluster does not use it.</p>
'''


def s_torque():
    xs = [k * 0.005 for k in range(0, 281)]
    ser = [(f'{k.title()} class', [(v, torque_at(c, v, CLASSES[k][2] / CLASSES[k][1])) for v in xs]) for k, c in CURVES.items()]
    td = from_peak(1400, 2200, False)
    na = from_peak(4000, 5500, True)
    ser.append(('Turbo diesel, peak at 1400 of 2200', [(v, torque_at(td, v, 3000 / 2200)) for v in xs], {'dash': '5 3'}))
    ser.append(('Aspirated, peak at 4000 of 5500', [(v, torque_at(na, v, 7000 / 5500)) for v in xs], {'dash': '2 3'}))
    fig = chart(ser, (0, 1.4), (0, 1.05), 0.1, 0.1, 'Engine speed over the red zone\'s', 'Torque over its most',
                'The torque curves: the three class curves, and two built from a peak torque speed. Past the red zone each falls to nothing at the tachometer\'s end.',
                legend='bl', yfmt=lambda v: f'{v:.1f}', xfmt=lambda v: f'{v:.1f}', h=270)
    rows = [[k.title(), ', '.join(f'{a:g}:{b:g}' for a, b in c)] for k, c in CURVES.items()]
    return f'''
<h2>Torque curves and engine profiles</h2>
<p>The mod's engine has a torque curve against its engine speed, as points <i>x</i>:<i>y</i> with <i>x</i> the engine speed over the red
zone's and <i>y</i> the torque over its most, straight between points and flat below the first. Past the last point the torque falls in
a line to nothing at the tachometer's end (<code>TorqueAt</code>, <code>models.h:923-931</code>).</p>
{table(['Class', 'Points'], rows, 'The class curves (models.h:887-890): trucks and heavy trucks a diesel\'s, broad and early; scouts a later peak')}
<p>An engine profile's <code>PeakTorqueRpm</code> builds the curve around the maker's peak torque speed (<code>EngineCurveFromPeak</code>,
<code>models.h:938-961</code>). With <i>P</i> = clamp(peak/red, 0.15, 0.95):</p>
{eq('turbo:  (0.5<i>P</i>, 0.6), (0.85<i>P</i>, 1), (min(1.5<i>P</i>, 0.95), 1), (1, 0.8)', 8)}
{eq('aspirated:  (0.4<i>P</i>, 0.7), (<i>P</i>, 1), (<i>P</i> + 0.5(1 − <i>P</i>), 0.92), (1, 0.8)', 9)}
<p class="where">A point closer than 0.005 to the one before is left out. <code>TorqueCurve</code> gives 2 to 8 points of its own, <i>x</i>
above 0 and rising, <i>y</i> from 0 to 1, or is ignored. A hand-written curve wins over the peak, the peak over the class curve.</p>
{fig}
'''


def s_powertrain():
    return f'''
<h2>The mod's engine handed to the game (Powertrain=1)</h2>
<p>The mod's engine works out what it would give each pass (<code>PowertrainStep</code>, <code>models.h:1009-1022</code>):</p>
{eq('pedal′ → <i>p</i><sup>1.8</sup>, from 0 to 1 in 0.6 s at the most, from 1 to 0 in 0.15 s', 10)}
{eq('cold = 1 − 0.15 · clamp((60 − <i>T</i><sub>oil</sub>)/60, 0, 1),    boost = limit (equation 34), 1 aspirated', 11)}
{eq('torque = curve(speed(rpm)) · cold · boost,    power = torque · pedal′', 12)}
<p class="where">Cold oil costs up to 15% at 0 °C and nothing from 60 °C. An aspirated engine has boost = 1. Half way down the pedal
gives 0.29.</p>
<p>The game has no place for an engine curve, but it has a torque factor: the share of its torque a damaged engine keeps (truck
action +0xCC). The mod writes that factor so the game's pull matches the mod's engine (<code>models.h:1160-1238</code>). With
<i>x</i> the wheels' share of the gear's top, from the learned grip so that spinning wheels keep the game's fall. The grip is learned
in a gear held by hand at Gearbox=2 too, where it rises only with the pedal under 0.7 (steady wheelspin floored in mud is no grip)
and may always fall; before it is learned <i>x</i> is held to 0.5. In such a gear with the clutch fully in (the clutch model
connected, its grip at 0.99 or more, no travel) the engine is locked to the wheels, so <i>x</i> is the driven wheels' own speed over
the gear's top (<code>SpinShare</code>) and the lift, under the class's roof with its fade, is capped at <code>kSpinLiftMost</code> 2 (<code>SpinRoof</code>; it binds up to about 0.94 of the top): when spinning wheels catch, the
factor written for the spin meets them at the game's full torque until the next pass, so the kick is at most twice the engine's
torque for one pass. The spin is noisy by about 0.15 rad/s a pass, a big share of a low top (L-'s is 2.24 rad/s). So the spin
is the mean of the last 5 passes in rad/s (about 0.25 s, <code>SpinMeanStep</code>, started over at a new gear or truck), <i>x</i> its share of
this pass's top, so a change within the low range mixes no tops, and the cap fades
from 2 to 1 in a straight line over the wider of the top's last 0.8 rad/s and its last fifth (<code>SpinLiftMost</code>,
<code>SpinFadeWidth</code>), not only its last 0.06. With 0.18.4's cut over the last 0.06 a held low gear swayed about once a
second: the factor fell to 1 at the top, the truck sagged, and the rise back was paced at <code>kLiftRise</code>. On this path a
rise is now paced only after the wheels break loose: the mean's <i>x</i> up by more than 0.2 or the spin up by more than 1 rad/s
in one pass (<code>kSpinLooseX</code>, <code>kSpinLooseRad</code>), for <code>kLiftCutWindow</code>. A fall along the fade opens nothing. The
ground path keeps its cut rule. <code>SpinCalm</code> in Gauges.ini is read no more, and the log says so once.
While the torque cut acts (below) this path writes nothing. The probe's <i>x</i> is the mean:</p>
{eq('lift = clamp( curve(<i>x</i>) · ratio · min(1, cold · boost / <i>p</i>) / GameFall(<i>x</i>), 0.05, <i>R</i> ) for <i>x</i> &lt; 1, 1 for <i>x</i> ≥ 1,    GameFall(<i>x</i>) = clamp(2.1(1 − <i>x</i>), 0, 1),    <i>R</i> = 6', 13)}
{eq('factor = game · (1 + clamp(EngineShare/100, 0, 1) · (lift − 1))', 14)}
<p class="where">ratio is the torque converter's multiplication while the mod's automatic holds the gears, otherwise 1. game is the game's
own share, at most 1; it is taken at the first write and again whenever the value read differs from the last one written (the
game wrote its own, as on new engine damage), so a damaged engine still pulls less. The factor is written only when it moves by
0.005 or more, never below 0.05 and never above the roof: 6 times the game's own for a truck or a heavy truck, 3 for a scout (the
class the automatic goes by), flat up to 0.7 of a gear's top and from there falling in a straight line to 1 at the top (6: 3.5 at 0.85,
1.75 at 0.955; 3: 2 at 0.85). With the fall at 2.1(1 − <i>x</i>) the engine's torque is held to about 0.86 of a gear's top (a scout's
0.80), then the push dies down to the game's own at the top, as a diesel's governor cuts the fuel. With a flat roof of 6 it held to
0.92 (at 3 times 0.84), and a vanilla Defender 90 floored on a road went from 27 to 58 km/h in 5 seconds through three gears (27
October). The fade began at 0.8 until 28 October (held to 0.88): over the last fifth the push fell so steeply that a heavy truck held
near its top gear's limit at part throttle overshot into the fade, got the push back in one jump and surged, every two seconds (a
tester's HX 520 at 36 to 39 km/h, the factor swinging 1.0 to 2.4). Within a gear a factor above the last one written climbs toward it
by at most 1 a second (and at least 0.005 a pass, so it is written); a lower one, a new gear (the game's engaged gear) and the first write
after the hand takes the truck are written at once. Near the top a
small error in the fitted fall times a big factor is a big error in the torque, and a roof of 23.8 kicked a d110 at the top of 2nd to 5th
gear (2026-10-25). With the wheels at the
gear's top or past it (rolling downhill) the factor is the game's own: the division would be by nothing. Below about half a gear's
top GameFall is 1 and nothing is divided out. The game's own share goes back once when the setting goes off, the engine stops, the pull test is armed, or another truck is
driven (and to the old truck only if its action is still its own).</p>
<p><b>The chassis push</b> (BRIEF_chassis_push.md). At c42b8a the drive gives a share s of each driven wheel's torque as a direct impulse
on the wheel's parent body along the ground, s = clamp((mean wheel speed − 4)/6, 0.01, 1) · clamp((|forward| − 3)/6, 0, 1), and on the
kick path all of it. That push carries the factor one for one with no tyre between, so the factor times a B4 higher than the one it was
written for shoved the chassis: the wheels catching after a slip, or the factor stepping while B4 was still high. CreepDrive reads each
distinct parent body of the driven wheels (wheel + 0x10) before and after the original drive, its velocity at + 0x230 and its turn at
+ 0x240, skipping an inverse mass of 0 (+ 0x22C), and moves both back by k of their change: v −= k Δv and ω −= k Δω, so the pitch
moment comes off in the same share. F_excess = min(B4, throttle) / assumed, with B4 at action + 0xB4 before the drive and assumed
GameFall at the factor's own x times the pedal; F is the factor over the game's own share, so engine damage is not read as the mod's
part; k = (1 − max(1/F_excess, 1/F)) · (1 − <code>PushShare</code>/100), and 0 where F_excess or F is not above 1
(<code>PushTakeBack</code>). The floor 1/F keeps the game's own push for the game's torque whole. In a held gear B4 is written 1 and the
cut's answer is at most the stock, so nothing comes off there unless the guard bound: then (1 − kept) of the push of each parent that
carries a clamped wheel (<code>PushCapBack</code>). The kick path's torque has no factor in it (c42859: no + 0xCC and no B4), so its push
is the game's own and stays whole: nothing comes off at a forward speed of 3 m/s or under, where s is 0 and the only push is the kick's,
nor on a call that starts with action + 0x70 apart from + 0x74 (the clunk of engaging 1st or R, c41f03) or with + 0xB8 above 1e-4
(c41e35, the kick path on every call); those steps are counted as kicks (<code>PushMayTake</code>). The probe gives a second's
push and the share taken back, k, F_excess, F, B4, the forward speed and the kick path's steps.</p>
<p><b>The diff lock's bite</b> (BRIEF_diff_lock_bite.md). The game's lock (action + 0x4A, one byte for the truck) is a rule in the drive
(c423a4): on each driven wheel's speed cut to whole rad/s, a share max(1 − max(w + 0.5 a − (0.9 slowest + 0.1 mean + 0.01), 0) / width, 0.01)
with width clamp(0.05 fastest, 0.05, 0.5), smoothed 0.8 old; nothing ties the speeds. The mod's thread publishes the driven truck's action,
vehicle and DiffLockBite p (<code>LockGive</code>) while Powertrain is on and p is above 0, at every Gearbox level, with the setter's
pattern (<code>kLockSetterPattern</code>, d7d470 on 6adb8627: it writes + 0x4A only while vehicle + 0xE8 is 0) found once at the start
and the kind at vehicle + 0xE8 read and 0 or 1 (<code>LockPublishes</code>; 2 is always on and takes the bite too, any other kind none). In the
drive's hook, which the bite asks for by itself (<code>DriveHookWanted</code>), after the original drive and while + 0x4A is set and the
handbrake + 0x48 is not, or the truck's type is Locked: each driven wheel's spin about its axle w (as the wheel cap reads it) moves to w + k (mean − w), the mean
weighted by each wheel's inertia about its axle (1 / <code>CapGain</code>), so the momentum about the axles stays (<code>LockStep</code>);
k = 1 − exp(−dt / tau), tau = 10^(−p / 50) s, 1 at p 100, 0 for a dt that is no number or not above 0 (<code>LockK</code>). Nothing
under a spread of 0.01 rad/s, with fewer than two wheels, or for a wheel with no inertia (skipped, counted). With the wheel cap on the
lock comes first and the cap works on the locked speeds, one write a wheel against the drive's result. Gauges.log has
"settings: diff lock bite ..." with the settings, "diff lock bite: the game's diff lock setter at +..." at the code look, "diff lock bite: rests on this truck: kind ..." for a kind not known (it publishes nothing), "diff lock bite: acts on this truck (kind ...)" at the first write,
and with Probe=1 "probe: diff lock bite: kind ..." a second.</p>
<p><b>The differential's type</b> (BRIEF_diff_types.md). DiffType is a key of the truck's own file: 0 Auto, 1 Open, 2 Limited slip, 3 Locked
(<code>DiffTypeOf</code>). Auto is Locked at kind 2, Open at kinds 0 and 1, and Limited slip for a kind not read or not known. The type acts
where the torque cut acts. Each step of the drive reads once what is in force (<code>DiffForceOf</code>): the lock while + 0x4A is set and
the handbrake is not, or the type is Locked; else Open for the type Open; else Limited slip, the cut as it was before the types. Open acts in each
axle by itself (BRIEF_open_by_axle.md). After the drive the driven wheels of an axle share its impulse evenly, J = the sum of
(post &minus; pre) / gain over the axle's n wheels and each speed pre + gain J / n (<code>DiffEvenAxles</code>, <code>DiffEvenStep</code>
an axle); the cut's answer is the same in every type, and where the gear has a wheel cap the step's ceiling (<code>CapCeiling</code>) holds
the mean of each axle's wheels, not each wheel (<code>DiffOpenAxles</code>, <code>DiffOpenStep</code> an axle): what the drive added to each
wheel of the axle in the step is scaled by one factor k in 0 to 1, so that the axle's mean after the step is min(the mean after the drive,
max(the mean before, the ceiling)). No wheel is lowered, a wheel the drive slowed keeps the drive's result, and nothing is handed on
within the axle. With all wheels alike each stays at the ceiling; a free wheel with its partner standing runs to 2 &times; the ceiling and
the partner gets nothing.
<b>The hand-on between axles</b> (the tied shaft; <code>DiffOpenAxles</code> with handOn). What the limit took from an axle, as an impulse
(the speed taken from each wheel over its gain), goes to the axles whose limit took nothing and that the drive did not push back, by the
impulse the drive gave each (evenly where it gave none). Each wheel of the axle that receives gets the same impulse through its own gain,
and that axle's mean stays under the same limit, max(its mean before the drive, the ceiling). What does not fit is dropped in that step
and kept nowhere. Nothing goes to an axle that gave, so nothing to the partner of the free wheel, and no wheel is lowered. In a numbered
gear the ceiling takes nothing, so nothing is handed on. The reason is the game's share: in the mod's model of the drive
(torque_cut_sim.h, <code>TopRun</code>) a wheel slower than the fastest by a whole rad/s gets next to none of the drive, and with nothing
handed on the other axle of a truck standing on one free wheel pulls about 1 percent of what it pulls with no wheel free. With the hand-on
it pulls the same as with no wheel free at 60 steps a second and about two thirds of it at 120. Where nothing was handed on, the truck's
kept share is the axles' factors weighted by what the drive added to each, &Sigma; k &times; added / &Sigma; added (the factor itself
where all axles that got something have the same). Where something was, it is what the wheels gained over what the drive added,
&Sigma; max(final &minus; pre, 0) / &Sigma; max(after the drive &minus; pre, 0), so what was handed on counts as kept. The clamps are
summed and the excess is the largest axle's.
<b>The axles.</b> Two wheels are one axle when they have the same parent body (wheel + 0x10), stand on other sides of it and each is the
other's nearest along it within 0.5 m (<code>AxleGroups</code>; <code>kAxleAlong</code>: tandem axles stand over 1 m apart, so half of
that cannot reach the next axle, and it leaves room for two wheels not quite opposite). A wheel's place is its centre (wheel + 0x13C,
world) less the body's place (body + 0x1A0), along the body's forward (+ 0x170) and left (+ 0x190), so a bent frame, steer and spring
travel do not move a wheel to another axle. All wheels of the truck are grouped, driven or not, and the driven ones find their axle by
the wheel object's pointer. The wheels left alone are paired again by the same rule in the main body's frame (vehicle + 0x60, model + 0x230),
whatever body each hangs on: a tandem on beams, one beam a side, has both wheels of a beam on one side of it, so only the second pairing
gives it two axles of two wheels. A wheel with no partner, one not read or one the table does not hold is an axle of its own. The grouping is
made in the drive's hook, on the physics thread, in the first step with Open in force on a truck: reads only, under the fault guard. It is
kept and made again when the vehicle or one of its wheel pointers is another, and looked at again every 240 Open steps while a wheel stands
alone, where only a result with fewer wheels alone is taken. A read that faults leaves each wheel an axle of its own and is counted.
Gauges.log has "differential: Open by axle: N axles over the M wheels of this truck, K wheels alone ..." once a truck when Open first acts
on it and again when the axles are made anew. The truck's line "differential: the lock kind ..." ends "Open acts in each axle by itself
and hands what the limit took from an axle to the other axles". With Probe=1 and Open in force "probe: differential: Open by axle, the last step ..." gives
each axle's wheels (by their place in the truck's list), its mean after the step, its factor and the speed handed to it, the first 8 axles; the line
"probe: differential: ..." gives J summed over the axles. Locked on a truck of
kind 0 or 1: the mod's thread holds the lock byte (<code>LockHoldStep</code>), never clears a byte the player set, and sets a cleared one
again with one line in the log. A truck left with the mod's byte set keeps it, and back on that truck the byte is the mod's own again and is
cleared once the type no longer acts (the last four such trucks are remembered). With the bite above 0
the lock's step weighs each wheel by what it has behind it (<code>LockStepHeld</code>): W = I + load &times; mass / n &times; r&sup2;, the
load the contact share at wheel + 0x128, so a wheel in the air no longer pulls the mean up; with every load 0 it is <code>LockStep</code>.
While the drive's impulse J = the sum of I &times; (post &minus; pre) is over 0, three rules bound what is written (<code>LockBound</code>, on the
blended speed, so below bite 100 too): final is at least min(post, 0); final is at most the fastest post; the sum of I &times; final is at most
that of I &times; post, the surplus taken from the wheels by their room above the floor. What the rules do not let through is dropped and
kept nowhere. With J at 0 or under (a truck rolling back, engine braking) the step is as before. With Probe=1 the line a second counts the
steps with C under 0 and J over 0, the wheels written under 0, the steps where the rules moved a wheel and the wheels written under 0 and under what
the drive left them with J over 0 (<code>LockStepOut</code>'s breach, 0 while the bounds hold), and the line
"probe: diff lock bite: the last written step by wheel ..." gives the last written step wheel by wheel: before the drive, after the drive,
as the lock wrote it, the contact share, the gain, the rolling radius, the same wheel's speed before the next step's drive, the speed after the
wheel cap, the byte at wheel + 0x1A4, the float at + 0x178 and the float at + 0x1C4, with the cap's ceiling of that step (0 where no cap acts).
Its truck speed is the cut's, and where the cut gave none the main body's forward speed over the mean rolling radius (<code>TruckSpeedInWheels</code>).
Below k 1 a free wheel still runs ahead by about the drive's kick &times; (1 &minus; k) / k. Gauges.log has "differential: the lock kind ..."
for a truck, "differential: Locked on a truck whose lock is not always on ..." as the mod takes or leaves the lock byte,
"differential: the lock byte the mod holds for Locked was 0 again ..." once, and with Probe=1 "probe: differential: ..." a second.</p>
<p><b>The diff types by owned truck</b> (BRIEF_truck_keys_per_truck.md, the diff step). The diff group, DiffType with DiffAxle1
to DiffAxle8, has a level of the owned truck: <code>[Truck:&lt;id&gt;]</code> in the kind's file, beside
[Suspension:&lt;id&gt;] and [Engine:&lt;id&gt;]. It has no Own key. A key of the group in the section makes the whole group the
truck's, and the kind's keys of the group are then not read for it (<code>TruckDiffRead</code>, <code>ReadTruckIni</code>). With
the id read an edit writes the whole group in force into the section (<code>WriteTruckDiffs</code>); with no id read it writes
the kind's keys as before. No read writes a file, so an old file gives every truck of the kind what it gave. The id arrives a
pass or more after the truck: the kind's group is in force until then, and the settings are read again on that pass
(<code>SettingsFor</code>) and at a take-over (<code>DiffFromFiles</code>). The section is the fourth of the sections that
follow a truck, at the end of <code>TruckIdAllPrefixes</code>, so the settings' bits are 1, 2 and 8 and the odometer's is 4. The
take-over, the marks and the prune are those of the other sections. The defaults take the group out at both levels, and a
section left with the parts alone goes. The log says at each truck change where the diff types come from
(<code>DiffLevelLog</code>: "differential: this truck's own diff types (", or the kind's, or the plain Diff row), and at a save
"settings: saved, this truck's diff types in". RoadCdA, AirAxles and OdometerHours stay keys of the kind.</p>
<p><b>The differential by axle</b> (BRIEF_axle_diff.md). DiffAxle1 to DiffAxle8 are keys of the truck's own file, 0 to 3 as DiffType. An axle with no key goes by DiffType.
The mod's thread reads every wheel's parent body and place and groups them as Open by axle does (<code>AxleGroups</code>), ordered front to back as the Drive display's rows
(<code>AxleRowsMake</code>). <code>AxlePlanOf</code> gives each axle's type in force: its set type through <code>DiffTypeOf</code>, or Locked while its lever is on.
The cut's slot carries each wheel's pointer with its axle's type and how it is driven. In the drive's hook <code>SlotForce</code> decides once a step, over the wheels driven in that step.
Alike: the step is the one before the rows, with their type. The lock byte in force: the whole truck's lock. Else <code>DiffAxlesStep</code>: an Open axle is evened within itself,
a Locked axle gets <code>LockStepHeld</code> over its own wheels with the truck's mass times the axle's wheels over the driven wheels read, a Limited slip axle is left as the drive left it.
In a gear with a cap each axle is then held against the same ceiling and what the limit took goes to the other axles by <code>DiffOpenAxles</code>' rule.
The mod holds the lock byte only where every driven axle is Locked. The two levers are kept with the truck's watch by vehicle and never written.
Gauges.log has "differential by axle: ..." for the axles read and for a lever's press, and with Probe=1 "probe: differential by axle: ..." a second while the axles differ.</p>
<h3>The torque cut (a gear held by hand)</h3>
<p>The game's torque per gear is one function, <code>bool(vehicle, int gear, float* torque, float* start, float* a, float* top, float* gearSpeed)</code>, found by
its first bytes once (<code>kGearTorquePattern</code>), and the drive's call of it is the first call in the wheels' drive that lands there (<code>GearTorqueFind</code>,
within 0x400 bytes). Its other callers (the stall, the input, the automatic) keep the game's own. The detour is attached with the other threads held
(<code>GearboxAttach</code>). It makes the stock call, then on the drive's call for the published truck answers torque = <i>T</i><sub>stock</sub> · used with
start −10<sup>9</sup>, a one float step under the top and the top at the governor's end over the power factor, 1.1 · top/pf (<code>TorqueCutCurveOut</code>), so after the
drive's own scaling by pf the curve is 1 under the governor's end, and the game's own overspeed brake acts on each wheel above that top less 0.5, as in the stock
(BRIEF_drive_2108_findings.md F1: with top 10<sup>9</sup> a free wheel ran away to tens of rad/s). used is the bracket paced as the game's B4 rose: used + <i>r</i> ·
(bracket − used) away from 0, with <i>r</i> = EngineResponsiveness (+0xC4) · pf, and at once toward 0 (<code>CutRise</code>, F1b; one step's kick on a free wheel is about
30 times the brake's most). A take-over paces from the game's own pull at that step, clamp01(min(B4, pedal)) read on the mod's pass before the first publish, and from 0
where B4 is no number (<code>TorqueCutGamePull</code>, S1). The governor's fade goes by the truck's own speed: the forward speed of the main body ([[vehicle + 0x60] + 0x230], its + 0x230 along its
rotation's first column at + 0x170, as the drive's d73120) over the driven wheels' rolling radius, n over the sum of 1/<i>r</i>, each wheel's collision cylinder ([[wheel + 0x18] + 0x20] + 0x28 by hkpCylinderShape's vtable, which the tire mod writes with the pressure) where it lies within 0.5 to 1.25 of wheel + 0x94, else + 0x94 (<code>WheelRollRadius</code>, BRIEF_cut_radius_cylinder.md; the vtable rides in the slot, the hook neither looks it up nor logs; "torque cut: the truck's speed by the rolling radius ..." a truck and when a fallback count changes), in the gear's direction (<code>CutSpeedsTruck</code>,
M5 of the wheel cap's addendum); where the guard acts the curve is read at the wheels' speed held to the redline, as its tail past the redline would be a fall in the
wheels' speed under the guard; in a numbered gear it keeps its tail, the one limit on a wheel that runs free there. The wheel cap is the runaway guard (BRIEF_drivetrain_2c_wheel_cap.md): after the original drive returns, each driven wheel's speed in the game's measure,
w = dot(body + 0x240, wheel + 0x154) in the gear's direction (wheel + 0x18 the body, used as activate checks it: + 0x10 set, + 0x160 not 5), is held to
<code>WheelCap</code> percent of the gear's top (150) by omega += (cap - w) · axle / |axle|², so only the spin about the axle changes and the axle need not be a unit
(<code>CapMove</code>). It acts in L-, L, L+ and R alone, not in a numbered gear held by hand (<code>GearboxPosCapped</code>, one condition), on every step (<code>CapArmStep</code> stays as a state for the probe), and hands what a held wheel could not take, as an impulse, to the driven wheels not held and not pushed
against the gear, by their own push, through each one's inverse inertia about the axle (body + 0x220 along the desc's local axle at + 0x100; <code>CapStep</code>,
S1). Since BRIEF_governor_sway.md the limit of a step is a ceiling by the truck's speed (<code>CapCeiling</code>): the cap up to 0.75 of the gear's top (<code>kCapCeilFrom</code>), then down by <code>CutSmooth</code> to the governor's end as the truck goes to it, and the end where no truck speed is read. In L-, L and R the cap is wider by <code>LowRangeSlip</code> (BRIEF_low_range_pull.md; R since BRIEF_reverse_pull.md, by its own top, and where its own cap is no smaller it stays its own; the log: "low range slip: in L-, L and R the wheel cap is the gear's own plus ...". With Probe and the truck in A (the game's automatic flag, or the mod gearbox's A; not L+, L, L- or a gear on the game's shifter, part 194) the log has once a second "probe: pull in A: the mod hands the game ...": the share of the game's own torque, the pedal and the wheels' speed over the gear's top): the game's ground takes a share of a wheel's slip a step, about 0.2 on soft ground and 0.5 on full contact, until the grip ends it, so a ceiling by the gear's own top gave L- about a third of the pull of L+. The wider cap is the gear's own plus <code>LowRangeSlip</code> percent of the way to <code>WheelCap</code> percent of the top of the highest low gear the gearbox has (<code>LowRangeCap</code>, <code>GearboxLowMostPower</code>, <code>TruckGearTopAt</code>); with one low gear or a top that is no number it is the gear's own. The slot carries it beside the gear's own cap (<code>capWide</code>), and the ceiling is <code>CapCeilingWide</code>: the wide part whole while the truck is under 0.25 of the gear's top (<code>kLowWideFrom</code>), down by <code>CutSmooth</code> to nothing at 0.75, and from there <code>CapCeiling</code> of the gear's own cap to the last bit. With the wide part whole down to 0.75 the sim's truck ran 2 to 4 percent of the top past the governor's end on arrival and the needle swung up to 235 rpm at the top, so it ends before the governor's band starts. The log says "low range slip: in L-, L and R the wheel cap is the gear's own plus 100 percent of the way to the cap of the highest low gear the gearbox has ..." once for each value. The needle is held at its stop of 1.1 of the redline by any wheel speed over that, as before. A wheel is set to min(post, max(pre, ceiling)), so the step takes only what the drive added and lowers no wheel under its speed before the drive. Where the guard acts <i>F</i> has no governor's fade: the curve at the wheels' speed held to the redline, and 0 with the truck at or over the governor's end, so the drive does not undo the game's brake on a wheel over it. The fade in <i>F</i> near the gear's top made a loop with the wheels' light inertia, and that loop swung the pull. The stand-in is the paced bracket times the share of the drive's push the driven wheels kept, smoothed 0.8 old (M1). The engine brake is tapered by the slowest driven wheel in the gear's direction, whole from twice the gear's idle speed and 0 at it
(<code>TorqueCutBrakeTaper</code>, F3), and the cut waits for the game's gear to agree in sign with the position (<code>GearboxPosAgrees</code>, F2). The needle in a
locked gear goes by the driven wheels' signed mean in the gear's direction, floored at 0 (<code>TruckDrivenSpinGeared</code>, F4). The mod's thread publishes the slot once a pass (<code>TorqueCutSlotOf</code>) with
Powertrain=1, ModEngineSpeed=1 (else every engine speed reader but the fuel's and the stall's would read B4 = 1), Gearbox=2, a gear held by hand, EngineShare above 0 and the game's gear not 0. The detour reads <i>s</i> from the vehicle's own wheels: the mean of
the driven wheels' float ω in the gear's direction, floored at 0 (<code>TorqueCutSpeeds</code>). A failed read takes the slot's last <i>s</i>.</p>
<p>Where the cut does not act the torque factor carries the lift, and a factor worked out for one gear never stands in another.
The first write after the mod takes a truck is the game's own share, and the lift comes with the second pass. Where the mod
orders a shift, a factor above the game's own share is written down to it before the gear is set (<code>TorqueHandShift</code>;
with the probe on, the line <code>probe: torque factor down for the shift:</code>). While that shift is out and the game's
engaged gear is not yet the one asked, no factor is raised. The new gear's lift is written at once when it is engaged. The
automatic's input and engine figure stop at the tachometer's top, and on a figure that stood there it shifts up one gear, never
two.</p>
<p>With rpm = max(<i>s</i>, 0)/top · redline: <i>F</i> = <i>p</i> · (E(rpm) · min(1, limit/<i>p</i>) − load) · held − (1 − <i>p</i>) · brake · clamp((rpm − idle)/(redline − idle), 0, 1).
Here <i>p</i> is the raw pedal (TruckPedal, linear as the game's pull) and E the curve times the governor (a numbered gear's; where the guard acts the ceiling above governs): 1 up to 1.1 − max(0.1, <code>kGovernWidth</code>/top) of the redline, then a straight line to 0 at 1.1, the needle's stop
(<code>CutGovernBand</code>, <code>kTachOverrun</code>; <code>kGovernWidth</code> 0.8 rad/s by the closed loop at 60 Hz: 0.6 chatters in L- floored in soft mud, 1.0 settles there in 9.4 s at 0.85 of the top, 0.8 in 2.1 s at 0.93; so L- 0.81 to 1.1, 1st and up 1.0 to 1.1). brake is <code>EngineBrake</code>/100. With the
clutch slipping the bracket is the clutch model's torque through it as a share of the gear's (the scalar, held to −1 to 1.5); the creep's share is a floor.
bracket = (1 − share) · pedal · curve<sub>stock</sub>(<i>s</i>) + share · <i>F</i>, with curve<sub>stock</sub> the game's smoothstep from start to a, falling to top,
scaled by the power factor at +0x38. B4 (+0xB4) is written 1 in the hook on the wheels' drive before the call, so the game's pull lag is out. B4's readers get a stand-in instead, the cut's own pull, the bracket held to 0 to 1 (<code>TorqueCutStandIn</code>, one function): the fuel's call of
the engine speed answers stock · stand-in/B4, and the range reads the stand-in. The stall's call adds a wheels' term that is not linear in B4, so it gets the
game's own formula with the stand-in: <i>e</i> = max(<i>p</i> · clamp(<i>w</i> + 0.25 · stand-in, 0, 1), own(vehicle, false, false) · stand-in/B4), 0 at <i>p</i> ≤ 0.01, with
<i>w</i> = 0.75 · (0.25 · mean + 0.75 · max of the driven wheels' whole rad/s in the gear's direction)/the gear's list top (2 × entry + 5, entry + 2 in the high gear),
kept from the drive's step (<code>TorqueCutStallAnswer</code>, <code>TorqueCutStallWheels</code>). B4s = pedal · (1 − D4 + D4 · curve<sub>stock</sub>(<i>s</i>)), the game's
settled pull, is for the probe.
While the cut acts the factor at +0xCC is not written, the gear's top and Air drag's base learn nothing, and a refused ShiftGear(0) from the game (the damage
kick) puts the mod gearbox in N. The detour counts the steps where the bracket falls by 0.3 or more.</p>
<p>The log: <code>torque cut: the game's gear torque at +…, the drive's call returning to +…</code> at the first truck, or why not; <code>torque cut: the game's gear
torque hooked at +…</code> once the detour is in; <code>torque cut: acting in …</code> and <code>torque cut: off, …</code> at each change; <code>gearbox damage: the game
kicked the gearbox to N</code>. With Probe=1 a line a second, <code>probe: torque cut: …</code>: <i>s</i> and its share of the top, rpm, <i>F</i>, the bracket and its
least, the scalar, the share, the game's <i>T</i> and the answer, D4, B4s, the calls answered and passed, the other callers', failed reads and falls.</p>
<h3>The game's engine speed (ModEngineSpeed)</h3>
<p>The game's engine speed is one function, <code>float(vehicle, bool withWheels, bool withMinThrottle)</code>, found by its first bytes
(RVA d73170 in 6adb8627, d73200 in 6ab52f5d). It is worked out on each call from the pedal, the pull at action + 0xB4 and the driven
wheels, 0 to 1, stored nowhere, and 0 with the engine off or the pedal released. Eleven callers read it: the fuel burn, Truck.rpm (the
cockpit's needles), the player update, the stall's near-dead check, a HUD vec4, GetTruckInput, the mud and terrain effects, the lights and an
event. A Detours hook (all other threads suspended) answers with the mod's share <i>s</i> = clamp((rpm − idle) / (top − idle), 0, 1), top the
redline times <code>EngineSpeedTop</code> (<code>EngineSpeedShare</code>). The rpm is the needle's before the stall's dip (<code>TachModel::live</code>, the same
lag), as Truck.rpm multiplies its own stall term in. The mod's thread publishes <i>s</i> with its vehicle in one of two slots each pass; the hook
answers only for that vehicle, with its engine bit (vehicle + 0x768 bit 0) set, a slot at most 1 s old, and a caller other than the fuel's and the
stall's. Those two are told by their return addresses, from the bytes around each call, and each call must land on the function; they keep
the game's value, as both read it as a load (the burn goes from the idle rate to the gear's FuelModifier by it). Otherwise the hook calls the
game's own. A slot is published only while Powertrain hands the torque by its rule and the needle follows the mod's engine (the converter in A,
the clutch in a position of its own); it is cleared out of the world. With <code>Probe=1</code> the hook calls the game's own first on every call and keeps
its last value and count by return address. Gauges.log says "engine speed: the game's engine speed at +..." at the first truck with the
three places (or why it was not found, "so the game's engine speed stays its own"), "engine speed: the game's engine speed hooked at +..."
once hooked, and with the probe "probe: engine speed: the mod's ..." once a second, each caller by its return address with its calls and
the game's last value.</p>
<p><b>The engine sound</b> (BRIEF_drivetrain_1b_sound.md). The sound's script reads TruckInput.velocityFactor (the data block + 0x48),
which the C++ behind Truck.GetTruckInput (b23fa0 on 6adb8627, found once by its first bytes, <code>TruckInputFind</code>) fills from
the chassis speed's place in the stock gear band, and 0.5 in N. A Detours hook, attached with every other thread suspended once Powertrain
and ModEngineSpeed are on with a truck driven, calls the game's own. Then, while the engine speed's slot names the vehicle, is fresh and
the engine is on, it resolves the handle in *out through the game's own helper (the target of the call at + 0x8F2; it knows the object's
kinds, so the mod uses no mask of its own) and writes <code>SoundVelocityFactor</code>: the share × (1 − 0.714 stall)², with the stall
seconds at vehicle + 0x11C, and 0.5 + 0.5 × that in N (the data block's gear 0), so N idles at the game's own 0.5. The spline's x is
G + w T + (1 − w) velocityFactor with w = clamp(0.25 G + 0.35, 0, 0.75), so the share carries 65% of the stretch in 1st, N, R and H
(which the game reports as 1), 40% in 2nd and 25% from 3rd up; the time on the gear T stays the game's. Gauges.log names the function
and the helper at the first truck and the hook once it is in; the probe gives the game's velocityFactor beside the mod's, the gear,
and a second's calls, writes, calls not ours and handles not resolved.</p>
<h3>Air drag (RoadLoad)</h3>
<p>The game already slows a rolling truck by itself, more the faster it goes. The air's drag goes on top of that. With <code>RoadLoad=1</code> and the engine handed to the game, the mod takes it off the lift
(<code>RoadLoadAccel</code>, <code>RoadLoadShare</code>, models.h) as a deceleration over the game's own acceleration in that gear at
factor 1 and full pedal, <i>a</i><sub>base</sub>(<i>g</i>), times the pedal, before the fall's make-up, so it scales with the pull it is
taken from and stays bounded near a gear's top:</p>
{eq('<i>a</i><sub>load</sub> = ½ ρ CdA <i>v</i>² / <i>m</i>,    lift = (curve · ratio · min(1, cold · boost / <i>p</i>) − <i>a</i><sub>load</sub> / (<i>p</i> · <i>a</i><sub>base</sub>(<i>g</i>))) / GameFall(<i>x</i>),    ρ = 1.2', 15)}
<p class="where">The roof, its fade and the least come after it, as without the load. <i>m</i> is the truck's mass by its files, without
cargo or a trailer, so a loaded truck's air part is overstated and a trailer's own drag is missing. CdA by that mass, 1.1 m² under 4 t,
3.5 under 10 t, else 6, unless <code>RoadCdA</code> sets it in <code>[Gauges]</code> or a truck's file. Nothing standing, in reverse,
with the foot off (the game's coasting stays) or before <i>a</i><sub>base</sub>(<i>g</i>) is learned. Only the air: Gameplay reads no
surface, and the game has its own rolling resistance. <i>a</i><sub>base</sub>(<i>g</i>) is learned in ordinary driving
(<code>BaseLearnStep</code>): floored (0.9), the grade by the truck's path within 3° and taken out (<i>a</i> + <i>g</i> sin θ; the path's rise
over its way in the last second, <code>GradeStep</code>, as the chassis squats nose up under power), the grip known and the wheels not
spinning, no brake, the wheels at 0.3 to 0.8 of the gear's top (past it a top's error grows: 10 % at 0.8, 20 % at 0.9), not within 0.5 s
of a shift's end (its stage idle again); each pass gives (<i>a</i> + <i>a</i><sub>damp</sub>) / (factor · <i>p</i>), and the mean is weighted by the pull,
Σ (<i>a</i> + <i>a</i><sub>damp</sub>)/(factor · <i>p</i>) over Σ GamePull(<i>x</i>), with <i>a</i> the truck's acceleration over
the ground smoothed over 0.2 s and the factor as the game has it (the load is in it already). <i>a</i><sub>damp</sub> is the wheels' own
drag added back (<code>TruckWheelDamp</code>): Σ <i>I</i> · <i>c</i> · |ω| / <i>r</i> over every wheel on the ground, driven or not, over the
mass the physics moves (model + 0x784 and each wheel body's 1/<i>w</i> at body + 0x22C), with <i>c</i> the body's angular damping
(body + 0x216, a Havok half), <i>I</i> and ω about the axle (body + 0x220 and + 0x240 by the columns at + 0x170), <i>r</i> the rolling radius (the cylinder within 0.5 to 1.25 of wheel + 0x94, else + 0x94, <code>WheelRollRadius</code>),
smoothed by the same 0.2 s filter. The game's stock damping, a flat tire's and the tire mod's Rolling drag all slow the truck that way.
A wheel off the ground (wheel + 0x1A4) is left out; a wheel not read, or fewer wheels than the vehicle's models, is no pass. Figures
learned before have no <code>BaseDamp.&lt;engine&gt;=1</code> beside them: the read deletes them and writes the mark in one step. Once
per truck the log says "road load: the wheels' drag read from" so many wheels, with the mass taken,
or "road load: the wheels' drag not read (...)" with why; at each learned gear it gives the raw figure, the mean <i>a</i><sub>damp</sub>, the kept one and the file's, "written" or "within 2 % of the file's, not written"; and
2 s of them, gathered per gear over visits (a visit under 0.3 s counts for nothing; the sums start over with another truck or engine),
make the mean, kept as <code>Base1.&lt;engine&gt;</code>, <code>Base2.&lt;engine&gt;</code> ... in the truck's file (it is the
truck's, and an engine upgrade starts over) and followed at 0.3 a time. It is written once the kept figure is more than 2 % off the one last
written or read from the file, whatever one step moved it (<code>GearBaseStep</code>), so a small change, a new mass among them, is saved too. The game multiplies the torque as a gear's top is lower, so
<i>a</i><sub>base</sub> × top is one figure per truck (<code>BaseFold</code>): with two gears or more learned and their products within 15 %
of their mean K, a gear with none of its own goes by K / top, which gives the top gears, floored at speed nearly always past 0.8, their
load; spread wider, each gear by its own only. The log gives the learned products side by side, and with the probe each gear's seconds
gathered, every 10 s at most. A loaded truck learns its slower
pull while the air part still goes by its own weight, so with a load the air part is overstated. For a 3.5 t truck at 100 km/h,
<i>a</i><sub>load</sub> is 0.145 m/s² (0.51 kN).</p>
<p>The probe's "pull:" line (<code>PullBins</code>): with <code>Probe=1</code>, Powertrain off, floored in a numbered gear under the
same conditions with the nose within 1° (the probe takes no grade out), the acceleration in bins of 0.05 of the gear's top from 0.30, logged once the gear passes 0.98 or changes with eight
bins filled, with the engine's MaxDeltaAngVel, the most and where it is, the fall past it fitted as <code>kGamePullFall</code> is, and
the peaks of every gear measured so far, which tell whether the game multiplies the torque by the gear.</p>
<h3>The pull test</h3>
<p>On a long flat road in fourth gear or higher (the game's automatic off), floored, no brakes, at 1 m/s or more and the wheels
under 0.55 of the gear's top, the test holds the gear and runs seven phases of a second each, with the factor × 1, 0.5, 1, 1.5, 1,
0.75, 1 (<code>models.h:1804-1944</code>). In each phase it fits a straight line to the speed over time by least squares (the first
0.3 s apart), slope = (<i>n</i>Σ<i>ty</i> − Σ<i>t</i>Σ<i>y</i>)/(<i>n</i>Σ<i>t</i>² − (Σ<i>t</i>)²), and reports each changed phase against the
mean of its neighbours:</p>
{eq('ratio<sub><i>i</i></sub> = slope<sub><i>i</i></sub> / ½(slope<sub><i>i</i>−1</sub> + slope<sub><i>i</i>+1</sub>),    <i>i</i> = 1, 3, 5', 15)}
<p class="where">A ratio near the phase's factor means the game's pull goes with the factor. Lifting, braking, another gear or the wheels
past 0.95 of the top stop it, and the factor goes back. The power factor (action +0x38) was tested the same way in 0.12.4: a write
counts at once and is kept, but it moves the gear's top speed and gives no torque.</p>
'''


def s_auto():
    return f'''
<h2>The mod's automatic (Gearbox=1)</h2>
<p>The automatic is a model that runs every pass (<code>GearboxStep</code>, <code>models.h:1419-1652</code>); with <code>Gearbox=1</code>
it also drives the game's gears. Its "engine mode" is on while the Powertrain factor is being written.</p>
<h3>What it goes by</h3>
<ul><li><b>The pedal.</b> With <code>Input</code> = keyboard any pedal from 0.05 is taken as floored. A drop of more than 0.05 for up to
0.3 s is ignored: the game cuts the throttle for a moment at each shift. The pedal the shift points follow, feel, falls at most
0.5 a second: feel = max(<i>p</i>, feel − 0.5Δ<i>t</i>).</li>
<li><b>The grip.</b> Learned only when it can be trusted: pedal over 0.2, no brake or handbrake, over 2.5 m/s on the ground, the
wheels over 1 rad/s, steady within 5%, the wheels within 15% of each other, and no hard acceleration. It is the wheels' turning over
the ground speed; it follows a lower value with a 2 s time constant and a higher one with 20 s.</li>
<li><b>The speeds.</b> spin smoothed over 0.25 s; by = grip × ground speed once the grip is known; its trend smoothed over 1 s.
Foot on, the shifts go by pace = min(spin, 1.2 × ground + 1 rad/s): in drives logged in the game the wheels turned within 1.13 times the
learned grip nine times in ten, so wheelspin does not shift up. A shift up with the foot on goes by min(wheels, 1.05 × by), the
wheels unsmoothed. With the engine handed to the game the gears it sets off in (the first, and the start gear) differ: their shift
up, floored point and limiter alike, goes by the wheels as they turn, spinning or not, while the truck goes at least 0.3 of the start
gear's top over the ground. The game's pull falls by the wheel speed, so a spinning first gear pulls almost nothing and gains
nothing from staying (a tester's log of 27 October: a ZIL-157 on dirt sat in first 10 to 40 s). The gear's hold still gives a launch
its seconds there, and a truck stuck where it stands keeps the ground's rule. Foot off they go by the faster of the wheels and the ground.</li>
<li><b>Climbing.</b> Half a second with the pedal at 0.4 or more and the speed falling faster than 0.35 rad/s² starts 4 s in which
the shift points are the floored ones.</li></ul>
<h3>Shift points</h3>
<p>With point(<i>s</i>) = idle + (red − idle) <i>s</i> and input(<i>g</i>, <i>v</i>) = <i>v</i>/top(<i>g</i>) · red, the engine speed a gear would
turn at:</p>
{eq('up(<i>g</i>) = light + (floored − light) · feel,    light = point(0.35),    floored = max(up share · red, light)', 16)}
{eq('down(<i>g</i>) = point(0.12) at a light foot,    max(down share · red, point(0.12)) floored', 17)}
<p class="where">The shares are those of equation (3). In engine mode the points follow the mod's curve instead: the floored point is
where the next gear pulls harder by the mod's curve, EngineShare and the roof (with a truck's fading roof a ZIL-131's at 2816 to 2864 rpm
of a 3200 red zone; with the next gear 43/27 taller, at 0.905 of the gear's top floored with all of the engine, 0.875 at EngineShare 50), or the limiter, point(0.98), where none does (until 0.15.10 never past point(0.95), a
rule from before the fade, which sat just over it); the lightest is the light-foot shift-up point, 0.8 × where the curve first reaches 0.9 of its
most and no later than 0.45 red (a truck's class curve 880 rpm, a scout's 2370; from the curve's 0.98 point a scout's was 3300), and
never under idle, as a car's nine-speed automatic shifts; down is the gear below's up point moved over by the step, / 1.15, and never
under 1.6 idle at a light foot, falling to 1.15 idle floored (a truck's part-throttle points land at 0.55 to 0.75 of its governed
speed and a modern diesel pulls cleanly to about 1000 to 1100 rpm; review 11, from 1.15 at any pedal).</p>
<p>Two more ways up, one gear at a time, in both modes. Cruising: a light foot (0.5 or less) held steady, the speed changing by no
more than 2% of itself a second, for 2 s, goes up when the next gear would turn at max(the light-foot shift-up point, 1.1 × its point
down) or over, short of the light point (a car's nine-speed automatic, filmed on the road on 2026-10-19, cruised at 1200 to 1300 rpm
against a peak at about 1600). A lift at speed: the foot off and no brake, the truck neither gaining more than 2% a second (downhill:
the engine brakes it) nor slowing more than 4% (mud, a climb), goes up when the next gear would turn at max(the light-foot shift-up point, 1.1 ×
point(0.15)) or over. With keys (<code>Input</code> 2) the key has to have been let go for 2 s first, and a lift's
shift up taken back down within 6 s (the next press, the brake) keeps lifts from shifting up for 10 s, twice as long each time again,
up to 60 s: a tester's keyboard drive on 0.15.8 had 19 of 23 lift upshifts taken straight back, 376 of its 412 lifts under a
second. A wheel's pedals and a pad shift up at a lift as before.</p>
<h3>Deciding</h3>
<ol><li>Standing (the first gear would turn under 0.1 red): the start gear at once.</li>
<li>A kickdown (below).</li>
<li>Past the red zone, or past what the engine can take: up, at once, through every gear that would be past it too.</li>
<li>Up: the shift has been called for 0.4 s at a light foot, 0.15 s floored (a short first gear is through in a second), and the
last shift is 0.5 s past if it was also up.</li>
<li>Foot off: down when the gear would turn under point(0.15), under point(0.25) with the brake on (the same car coasted in its gear
down to 1000 to 1300 rpm of a 4500 rpm red zone, 0.07 to 0.15), and the gear below under point(0.75), called for 0.3 s.</li>
<li>Foot on: down when called for 0.5 s.</li></ol>
<p>Every shift pauses decisions for a second; the converter opens for the shift's own pass and may lock again on the next. In engine mode three more rules hold: a gear is kept 2.5 s
of moving after a shift up (1.5 s for a scout, or the gearbox file's <code>GearHold</code>; an empty Fleetstar went 1 to 5 in two
seconds), but on the limiter only 0.9 s (review 11: no truck automatic holds a gear at the governed speed while the truck
accelerates); the start gear is the lowest with a top of at least a seventh of the top gear's, lower while the engine's torque per kilo
is under 1.2 × the gear's top; and a shift up taken back within 6 s with the foot on is not tried again for 8 s, doubling up to 30
s (a tester's Tayga swapped first and second 18 times in six minutes on 0.15.0).</p>
<h3>Kickdown</h3>
<p>A push from part throttle to the floor: the pedal must have stood between 0.2 and 0.9 (0.75 for a pad, whose trigger held near its
end wavers) for 0.3 s, then reach 0.9. A press from the foot off, so every key press, does not kick down. It then steps down at once,
ignoring the pause, while the gear below fits and pulls 5% more by equation (2) (engine mode: while the gear below would turn under
min(point(0.85), 0.92 × its up point)).</p>
<h3>The torque converter</h3>
<p>With input = spin/top(<i>g</i>) · red the speed the gearbox turns the engine at (<code>models.h:1637-1651</code>):</p>
{eq('stall speed = max( min(0.8 red, 2600), idle ),    stall = idle + (stall speed − idle) · <i>p</i>', 18)}
{eq('rpm = max(input, idle) locked;    max(1.04 input, stall + 0.15 min(<i>p</i>/0.05, 1) input, idle) open', 19)}
{eq('<i>s</i> = input / rpm,    ratio = 1 if <i>s</i> ≥ 0.87, else 2 − <i>s</i>/0.87', 20)}
<p class="where">The stall speeds are 1760 rpm for trucks, 2600 for scouts and 1520 for heavy trucks (Allison's converters stall at
1500 to 2000 on a heavy truck): the truck's diesel curve gives all its torque there, the heavy truck's 0.96, a scout's 5500 rpm engine
0.85. Open, the engine climbs from the stall with the input, as the converter's K-factor rises with the speed ratio, so the needle
climbs through a launch; the rise comes in over the first 5% of the pedal, so at idle the engine stays at idle while the truck creeps. The ratio is 2 at a stall and falls in a line to 1 at the coupling point, 0.87. Lock points: 0.5 red with
the game's own engine; in engine mode 1.65 idle at a light foot up to 0.5 red floored. In the gears it sets off in it locks only once
the speed ratio is 0.92 too, so the lock drops the needle at most 8%; a floored launch stays in the converter through first, as
Allison's 1C. A shift made locked opens it for its own pass and locks it again on the next over the lock point, so from there the
gears go lock to lock. It lets go under 0.4 red with the game's own engine; in engine mode under 1.4 idle with the foot on and 1.04
on a coast, never by the pedal, so a press from a low cruise keeps it locked and a press that needs more pull gets a shift down by
the schedule or the kickdown (review 11: the unlock point had risen with the pedal, and a press from a low cruise flared the engine
to the stall). A lift keeps it locked, as a truck's automatic does: the engine follows the wheels down and the game's own engine
braking slows the truck in the gear held. With the wheels spinning free (the model's spinning) the engine follows them down no slower
than its own drag brings it, idle to the red zone in kEngineRevTime (0.8 s) back down, so free wheels in the air or deep mud no
longer hold the needle at the tachometer's top; the lock and the shifts still read the input. With Probe=1 the automatic's line then
ends "the foot off: the engine at its run-down". Before d0940b4 it opened on every lift for a second and at every shift for the shifts'
pause, never locked in first; it stalled at 0.75 red (2400 at most), then at 0.6 (2000 at most, the tight one of d0940b4), coupling at
0.85 and then 0.75: the truck felt like a rubber band (two players' reports, 28 October).</p>
<h3>Creep at idle (Creep=1)</h3>
<p><b>By default only where an automatic picks the gear</b> (the author, 1 November): the creep goes where an automatic picks the gear, the mod's A
(with the reverse and the range it drives there) or the game's own automatic (<code>CreepAutoPicks</code>). A gear picked by hand gets
none, on either shifter and an R included: the game turns its automatic off to shift into R (through AutoOffAndShift), so an R it took
at a stop cannot be told from one picked by hand. With <code>CreepManual=1</code> (<code>CreepGoes</code>, off by default) the governor,
the holding part and the stall below are reached again, as before 53f526d: a gear picked by hand on either shifter, R and the low range
on the game's shifter through the converter. In Sequential the down to D before a stall (0.5 s under 1.1 idle) comes before the
creep's stall (a second stuck), so it stalls only with BackToA 0. Nothing either from one of the mod's shifts, from the moment it is asked until kCreepQuiet (1.5 s) after +0x70 shows the new gear (A shift, step by step); the creep's model keeps the gear and the power it sees then, so it starts no second quiet.</p>
<p>Once a physics step the game makes a drive throttle and a brake from the pedal field (+0x44; d740a0 in the 6ab52f5d image) and
hands the throttle to the wheels' drive, <code>c41da0(action, throttle, dt)</code>, called only at c41d40. The pull +0xB4 moves toward
throttle · (1 − D4 + D4 · curve(the wheels' speed)) and each driven wheel gets min(B4, throttle) · CC · D4 · the gear's torque · its
share (D4 is action +0xD4, CC the torque factor), so at zero pedal every wheel gets nothing. The game's engine speed value (d73200,
TruckGameRevs), its fuel and as far as is known its sound read the pedal field, not this throttle. The mod finds the call by its
bytes (the action in rcx, the throttle in xmm1, the seconds in xmm2; once in the code), checks that its target is a function's start
with the drive's first bytes and not another mod's jump, and detours the drive to a stub (<code>creep_stub.asm</code>) that sets xmm1
to the larger of the throttle and the mod's share for the driven truck's action only; every other call passes as it came. The share
is worked out each pass (<code>CreepShare</code>) and handed over with its action as one slot:</p>
{eq('converter:  <i>r</i> = input / max(1.04 input, idle),    share = 0.15 · (1 − clamp(<i>r</i>/0.87, 0, 1))', '20a')}
{eq('by hand:  most = 0.15 · min(top(<i>g</i>) / top(1), 1),    share = most · clamp(1 − (spin + 0.6 · rise) / idleSpin, 0, 1) · grip,    up at most 0.3 a second', '20b')}
{eq('creep = share · (1 − <i>p</i>);    0 with the engine off or starting, in N, the brake over 0.02 or the handbrake on', '20c')}
<p class="where">Through the converter: the mod's A, the game's automatic flag up, and R, L- and L picked on the game's shifter at
Gearbox 0 and 1 (the game's L+ is its first gear at full power and cannot be told from it). In R and the low range there the
converter's share is calmed as the governor's: times low = min(top(<i>g</i>)/top(1), 1) and rising at most 0.3 a second (the
author, 29 October). On the models test's heavy truck R and L- creep there to 0.69 of the gear's idle speed, no overshoot, no cycle. By hand: a position of the mod's own
gearbox, grip the clutch model's (0 pressed, 1 let up, 0 with its engine stalled), or a manual gear of the game's, grip 1. The
converter's share reaches nothing at 0.87 of the gear's idle speed (idle/red · top). The governor's falls in a line from its most at
a standstill to nothing at it: top(1) is the first gear's top without the low range, so the low range gets less (the game multiplies
the throttle by the gear's torque), and rise is how fast the wheels gain, in rad/s a second over 0.2 s, while they gain. The look
ahead and the paced rise keep it from outrunning the game's pull, which answers about a second late: given at once and whole, from
0.1 of idle to the red zone under idle, it overshot in the low range and cycled (the author's drive, 28 October). Nothing outside the world (a menu, the map, the garage, the pause), while the pull test runs, or for another
truck. 0.15 is a first figure, for the test build's logs to set. On the models test's heavy truck (the game's pull at a standstill
taken as 0.3 of its curve's) the creep holds 1.08 m/s in A and 1.49 m/s in first by hand, the gear's idle speed 1.49 m/s (the holding part below), the
needle at idle both times. In L- with the game's pull half a second late it settles at 1.00 of the idle speed, and with the wheels
free to spin at 1.00, without an overshoot (without the look ahead 2.1 times the idle speed, cycling). Gauges.log has a line a quarter second after each creep begins and one as it ends, four a second with
Probe=1: the share and what it went by, the stub's calls for the truck and in all, the game's pull, the factor, its engine speed
value (0.19 at idle) and its fuel rate.</p>
<p>Stuck in a position of the mod's own gearbox picked by hand (Gearbox=2) the engine stalls (<code>CreepStep</code>), through the
stall seconds' ramp of RequireClutch's stall. Never on the automatic (Gearbox 0 or 1, R and the low range on the game's shifter too)
or in A, and at every level no stuck time counts within a second of a gear change (the author's drive of BF57872B: "on auto, the
truck stalls in R, L-+, when switching gears"). Where no stall may come the governor and its holding part push and the engine idles on:</p>
{eq('stall:  governing, grip ≥ 0.99;  B4 rose ≤ 3% over the last 0.5 s;  spin < 0.25 idleSpin, gained < 0.03 idleSpin over 0.5 s;  for 1 s', '20d')}
<p class="where">B4 is the game's pull (action +0xB4). The grace starts once it has built, so a slow truck pulling away is not
stalled: a Zetros took 7 s to bring its wheels to a quarter of the idle speed, its pull rising all the while. A truck still gaining
from rest is not stuck either. After the stall the creep stays off until the engine is off (3 s at the most), and Gauges.log says
"creep: the engine stalled, stuck in gear N". Nothing through the converter, with the clutch held or on the brake.</p>
<p>The holding part (review 13 with its addendum): a real diesel's idle governor holds the idle speed in every gear with whatever
fuel it takes, up to its full fuel at idle. On top of the line of equation 20b, once the truck rolls in a gear picked by hand:</p>
{eq('hold ← clamp(hold + pull · holdMost / 2.5 s · Δ<i>t</i>, 0, holdMost),    share = min(holdMost, line + hold)', '20e')}
{eq('off = (idleSpin − (spin + rate · 1 s)) / idleSpin;    pull = clamp(((|off| − 0.03)<sub>+</sub> sign(off) / 0.1 + 0.1 clamp(off / 0.03, −1, 1)) · low<sup>2</sup>, −1, 1)', '20f')}
<p class="where">holdMost is the engine's full fuel at idle, a throttle of 1 with the engine handed to the game (the factor makes it
the curve's torque there) and else the torque at idle of the curve the engine pulls by, times low = min(top(<i>g</i>)/top(1), 1) and
(1 − <i>p</i>). It arms once the wheels reach 0.6 of the gear's idle speed in this gear and empties at a standstill (a quarter of
it), on a change of gear, with the clutch held and on the brake, so a truck pulling away in a tall gear gets the line alone and stalls
as before. It looks a second ahead, rising or falling, as a truck answers about a second late; within 3% of the idle speed it trims
at a tenth of its pace, and in the low range its pace goes with low<sup>2</sup> (L- cycled about the idle speed without either).
Armed, with the part at its most, the pull built and the wheels under 0.6 of the idle speed and not gaining for 1 s, the engine
stalls ("lugged under idle"), at Gearbox=2 only. There the clutch model's engine shows the lug on the needle, and a lug ends in
one stall, the clutch model's or the creep's; in the game's own gears, where no stall comes, the needle at zero pedal stays at idle
or over (<code>TachTarget</code>). On the models test's heavy truck in 4th by hand the truck holds 0.98 to 1.03 of the gear's idle speed on the flat (0.98 to
1.03 with Powertrain off, holdMost 0.73); a grade of 1.5 times what holdMost gives there lugs it under 0.6 and it stalls 2.0 s later;
from a standstill in 4th it does not move and stalls unarmed. At Gearbox 0 or 1 the same grade, and a truck stuck in R, L- or L+, get
no stall, the governor pushing and the needle at idle; a shift puts off a stall by a second. The pace, the 0.6 and how the game's
terrain takes it are for the test build's logs to settle.</p>
<h3>Driving the game's gears</h3>
<p>When the game's automatic flag (truck action +0x3C) is up in a forward gear, the mod takes over: it puts the flag down and from
then on calls the game's own shift function, <code>ShiftGear(vehicle, gear)</code>, whenever its gear changes. The gear asked for is
never under the lowest that fits. A gear the game picks (a tap, or A again) is put back, and so is a gear that appears within 2 s
of such a pick. It hands the gears back on R, N or high, on a gear the player picks by hand (after 0.5 s), on a low range picked
(the power factor 0.02 under its highest), with the setting off or the engine off.</p>
<h3>A shift, step by step</h3>
<p>The mod's thread decides each shift on its 50 ms pass, as before, but makes none itself. It posts a job (the truck, the gear,
the automatic flag and the power factor) to the game's truck update, which the mod hooks. On the game's own thread, before the
game's update, the hook puts the flag, calls <code>ShiftGear</code> through its trampoline and sets the power factor after it, the
order the game's own calls show. The result (taken or refused, +0x74 and +0x70) comes back in the job. One job is out at a time,
and a shift while one is out is asked again on the next pass. A job not dealt within a second is dropped and logged whatever
the updates did, and so is one for another truck or with none driven. The game's thread deals none older than a second (the drive
of 6 November 20:52: a shift posted as a truck switch began was dealt 50 s later). The shift's line names the vehicle it was dealt
on. With Probe=1, from the function menu's close until the game sets a truck (15 s at most), <code>probe: menu words</code> lines
give the words around the menu flags the mod reads, each one that changed, and <code>probe: menu words: the watch ends,</code> says
why it ended. The truck switch's own screen sets no flag the mod reads yet, and these lines are to find one. While no truck is read,
<code>game: no truck read:</code> gives the game's words at each change, the garage's pointer as read beside its flag, 100
lines a start at most. A switch to another
truck of the same kind logs <code>truck: another truck of the kind:</code>, not "again". In the drive of 3 November all 104 of the mod's shifts and all 158 of the game's own gearbox calls
ran on that one thread, and +0x70 followed +0x74 one update later.</p>
<p>Each of the mod's shifts then goes through four stages (<code>ShiftStage</code>, models.h): asked (the job posted), set (the
game took it, so +0x74 is the new gear), engaged (+0x70 shows it), and idle again kCreepQuiet (1.5 s) after it engaged. A stage
that stays asked or set for kStageMost (3 s) goes idle, and a refusal or a drop clears it. While a shift is asked, the gearbox
decides on the gear asked, not on the read before it. The creep gives nothing until the stage is idle. A shift without the clutch
owes its stall until the gear is set, so the stall and its gearbox damage come on the new gear and never on the old one. Gauges.log
has a line for the job ("shift N: a to b ... dealt at truck update ... on thread ...: took, +74, +70, power"), one when +0x70 shows
the gear, one when the stage is idle again, and one when an owed stall is ordered.</p>
{s_back()}
'''


def s_back():
    rows = [['The position', 'a numbered gear, shifted into by hand; not held by RequireGearHeld or the game\'s shifter',
             'R, N, H and the low range never return'],
            ['The timer', 'BackToA seconds since the last shift by hand', 'each shift by hand restarts it'],
            ['The pedal', 'under 0.7, unless A\'s gear is the held one', 'coasting included; with A\'s gear the held one no shift happens'],
            ['The grip', 'learned, and the wheels within 1.05 times the ground\'s speed plus 0.5 rad/s', 'no return while it is not learned'],
            ['No climb', 'the nose under 8° while the pedal is at 0.4 or more, and the climb hold not running', 'a shift up on a climb bogs'],
            ['No descent', 'the nose down under 5° while moving forward', 'a low gear held for engine braking'],
            ['Not rolling back', 'speed −0.5 m/s or more', ''],
            ['A\'s gear', 'the held one or next to it; any, standing with the pedal under 0.05', 'one shift, nothing jerks'],
            ['The locks', 'no diff lock or AWD change within 3 s', ''],
            ['The handbrake', 'off', ''],
            ['The engine and the shift', 'running, not dying; no job out and no shift asked or set', 'never on top of a shift']]
    return f'''
<h3>Back to A (BackToA)</h3>
<p>At Gearbox=2 the position goes back to A by itself when <code>BackToA</code> is above 0 (<code>BackToAStep</code>,
gearbox_model.h). The timer starts on a shift by hand into a numbered gear, one that changes the position or the gear, and not
on the game's shifter. Once it has run out, every condition of the table must hold for 1 s without a break; any failure starts
that second over. A's gear is the one the mod's automatic would ask on its first pass back (<code>TakeoverAutoGear</code>), as
its model keeps stepping in a held gear (<code>GearboxModelPass</code>: it decides nothing there and only reads along, the grip
rising only with the pedal under 0.7). The A key pressed from a held gear asks the same gear. The return is the A key pressed on the next pass: one job on the game's thread, with no
power write, and with <code>RequireClutch</code> no stall, as a move to A never stalls. The creep comes back once the shift is
idle again. The log has "sequential: back to A held by" each time what holds it changes, with the pedal, the wheels against the
ground, the nose and A's gear, and "sequential: back to A after N s in gear g, A's gear a, rpm" when it goes.</p>
{table(['Condition', 'Figure', 'Why'], rows, 'Back to A: what must hold for a second')}
<p>Sequential's own shifts (<code>SequentialStep</code>, gearbox_model.h): with <code>BackToA</code> above 0, not with
<code>RequireClutch</code> or <code>RequireGearHeld</code>, never for a gear picked on the game's shifter and never within a
shift's stages (which end 1.5 s after the gear engaged), a held numbered gear goes back to D (D picks the gear, and the line says which speed it went by: <code>by the ground: the wheels spin</code>, else by the wheels; before the grip is learned
a truck at rest whose driven wheels lie their own mean apart, <code>kRestSpread</code>, counts as spinning, so one free wheel gives D no high gear) near the red zone, when the gearbox's input
(<code>ClutchModel::input</code>, the wheels and the gear) has been at 0.9 of the red zone's speed for 0.3 s (a floored held gear settles near 0.95 of its top, where the game gives nothing), below the top gear, with the grip
known, the wheels not spinning (Back to A's own test) and the pedal at 0.05 or more (with the foot off a low gear held downhill is
engine braking), and before a stall, when it has been under 1.1 times idle for 0.5 s, the foot on or off (0.2 s under the brake),
above 1st. By the input, not the engine's speed: the clutch that works by itself slips at low speed and
holds the engine near idle. The return is the A key on that pass, one shift job; Back to A's pedal and lock conditions do not
hold it, as it is protective. The log: "sequential: up at the red zone" or "down before a stall", with the gear, D's gear and
the input's rpm.</p>'''


def s_coolant():
    scen = [('Half load at 13 km/h', 0.5, 13), ('Full load at 30 km/h', 1, 30), ('Full load at 7 km/h', 1, 7),
            ('Floored, wheels spinning, standing', 1, 0), ('Idle, standing', 0, 0)]
    series = []
    for lab, load, kmh in scen:
        T, pts = 15.0, [(0, 15.0)]
        for k in range(1, 8 * 60 * 4 + 1):
            T = temp_step(T, load, kmh, 0.25)
            if k % 4 == 0:
                pts.append((k / 4 / 60, T))
        series.append((lab, pts))
    fig = chart(series, (0, 8), (0, 145), 1, 10, 'Minutes from a cold start, 15 °C outside', 'Coolant, °C',
                'The coolant from a cold start, default settings.', hlines=[(118, 'red zone'), (60, 'cold lamp off')], legend='out', h=270)
    eqs = [[lab, f'{equilibrium(load, kmh):.1f} °C'] for lab, load, kmh in
           [('Idle, standing', 0, 0), ('Half load at 13 km/h', 0.5, 13), ('Full load at 30 km/h or faster', 1, 30),
            ('Full load at 7 km/h', 1, 7), ('Full load, standing', 1, 0)]]
    eqs[-1][1] = 'none: rises to the 140 °C cap'
    lag = oil_lag()
    return f'''
<h3>The game's gear display (GameGearDisplay)</h3>
<p>The display is the game's script view HudTransmissionUiView. Its UpdateStatusText writes the mode's text, and its
UpdateRunnerAnimation animates the lit dot. The script engine runs a function from the code array its function object holds at
+0x60, read once at each call's start. So one aligned pointer store swaps a function from its next call on (game_script.h).
Each function object gets two arrays of its own, never shared with another object, made with the engine's own allocate on the mod's thread. Dark: the text's
<code>visible</code> false, then return; the dot's <code>visible</code> set to isActive (LB held), then the stock words. Light:
<code>visible</code> true, then the stock words. The stock array stays the engine's. Light stays in place for the rest of the
start, so the game's display is as it was. At the game's exit the function object's destructor frees whichever array is in
place, so nothing is put back then.</p>
<p>Everything is found by name, as the engine finds it. The class registry and the string pool come from signatures of the code
that loads them, and the allocate from its first bytes and the call to it in the code array's reserve. The classes and members
come by the hash of their names, and each name is read back. Before any write the words are checked: UpdateStatusText has 41,
with words 33 to 35 the container's status and word 38 the store into its text. UpdateRunnerAnimation has 18, with its call of
GetRunner at word 13. Our jumps move by the six words we put in front. The count and the pointer are stored in the order that
never pairs a count with a shorter array. A look comes at each truck change and each change of the setting or of Gearbox, and
every 30 s while one was refused. The log says <code>gear display: the script engine's class registry at</code> at the start,
<code>settings: the game's gear display</code> with the setting, one line when the arrays go in or out with the mod's thread,
and one when a check refuses.</p>

<h2>Coolant and oil</h2>
<p>A real engine's coolant takes about a quarter to a third of the fuel's heat, and the fuel goes with the pedal and the engine speed
together. The model counts heat and cooling in one measure, 1 being a full load's heat (<code>models.h:397-647</code>):</p>
{eq('load = <i>p</i> · speed(rpm)  (× 0.25 in neutral),    heat = 0.15 + 0.85 · clamp(load, 0, 1)', 21)}
{eq('thermostat(<i>T</i>) = 0.15 + 0.85 · clamp((<i>T</i> − 82)/10, 0, 1),    fan(<i>T</i>) = clamp((<i>T</i> − 98)/4, 0, 1)', 22)}
{eq('air = 0.35 + 0.65 · clamp(km/h / AirFullKmh, 0, 1) + 0.3 · fan(<i>T</i>),    water = clamp((depth − 0.5)/0.7, 0, 1)', 23)}
{eq('cooling = (<i>T</i> − <i>T</i><sub>out</sub>) / (97 − 15) · thermostat(<i>T</i>) · (air + water)', 24)}
{eq('<i>T</i> ← <i>T</i> + rate · (heat − cooling) · Δ<i>t</i>,    at most 140 °C', 25)}
<p class="where">97 °C is where a fully loaded engine settles with all the air the radiator can get at 15 °C. depth is the mean water depth
at the front wheels (wheel +0x17C): from about axle height (0.5 m) to the engine in it (1.2 m) the water does as much as full airflow.
While the engine makes more heat than it loses, rate is r<sub>warm</sub> up to 85 °C and r<sub>hot</sub> from 90 °C, blended between; while it
loses more, the coolant falls at 1 °C a second for each unit, whatever the settings, so a long overheat time does not make a hot
engine slow to come back.</p>
<p>The three times in the settings set the rates (<code>TempRatesOf</code>, <code>models.h:511-525</code>). The way from one
temperature to another at a rate of 1 is the sum, in quarter degrees, of 0.25/(heat − cooling); a rate is that way over the time it
should take:</p>
{eq('r<sub>warm</sub> = path(15 → 85 °C, load 0.5, 13 km/h) / WarmUpMinutes,    r<sub>hot</sub> = path(90 → 118 °C, load 1, standing) / OverheatMinutes', 26)}
{eq('r<sub>off</sub> = ln( (90 − 15)/(40 − 15) ) / CoolDownMinutes,    stopped: <i>T</i> ← <i>T</i><sub>out</sub> + (<i>T</i> − <i>T</i><sub>out</sub>) e<sup>−r<sub>off</sub>(1 + 4 water) Δ<i>t</i></sup>', 27)}
<p class="where">Load 0.5 at 13 km/h is the mean of a drive logged in the game on 2026-10-08 (0.49 at 13.1 km/h). At the defaults (3, 1.5 and 15
minutes) r<sub>warm</sub> = {R_WARM:.3f}, r<sub>hot</sub> = {R_HOT:.3f} and r<sub>off</sub> = {R_OFF:.5f} a second.</p>
{fig}
{table(['Driving', 'Settles at'], eqs, 'Where the coolant settles, default settings, 15 °C outside')}
<p>The oil heads for the coolant's temperature plus what friction and the pistons add, and follows with a lag:</p>
{eq('<i>T</i><sub>oil</sub> → <i>T</i> + 4 · speed(rpm)² + 10 · clamp(load, 0, 1)', 28)}
<p class="where">The lag is found by bisection over a simulated warm-up, so that the oil reaches 85 °C at max(OilWarmUpMinutes,
WarmUpMinutes + 1 min); at the defaults it is {lag:.0f} s. A stopped engine's oil cools at half the coolant's rate. The cold lamp
lights under 60 °C; the oil warning comes on at <code>OilWarnC</code> (120 °C) and goes off 2 °C under it, with a beep at once and
every 4 s.</p>
<p>The mod remembers 16 kinds of truck. A kind driven again has cooled as a stopped engine for the time others were driven; a new
kind starts at the outside temperature. Two trucks of one kind share a temperature.</p>
'''


def s_damage():
    Ts = [k * 0.5 for k in range(0, 281)]
    hot = [(T, 10 * (T - 118) / 7 if T > 118 else 0) for T in Ts]
    cold = [(T, 5 * (60 - T) / 60 if T < 60 else 0) for T in Ts]
    fig = chart([('Hot, any load', hot), ('Cold, floored at the red zone', cold)], (0, 140), (0, 32), 10, 4, 'Coolant, °C',
                'Engine damage, % a minute', 'Engine damage by coolant temperature, default settings.', legend='tl')
    return f'''
<h2>Engine damage</h2>
<p>Since 0.17.0 the gearbox takes damage too: a stall the mod's own gearbox orders for a shift without the clutch, once the new gear is set, costs
StallDamagePercent of what the gearbox takes (0 to 25, 5 by default, with Damage on), dealt on the game's thread as below under
"Changed in 0.17.0". So does a burning clutch (ClutchBurn, with Damage on): its heat q rises by
P = max(carried, 0) · Torque · slip over kClutchHeatCapacity (1.6e7; slip in rad/s), times the speed's share (1 under 5 km/h, 0.2 from
15 km/h, linear between: a slip while rolling is a short event, a truck pinned at a crawl is the abuse), and falls by kClutchCooling
(0.173) of itself a second, half in 4 s, at any speed. A warning box (amber CLUTCH HOT) shows from kClutchWarnFrom (1.6); past
kClutchBurnFrom (3.2, the box red, CLUTCH DAMAGE) and only while the clutch still slips under load it costs the gearbox
q/3.2 · kClutchBurnRate (0.025) of its capacity a second; locked or open it costs nothing however hot, and what it owed is dropped. A
repair (the gearbox's damage read falling) sets q to 0 and drops a burn's points not dealt yet. On the clutch model's own states
(automatic, floored in third from a standstill on the test truck): amber after 1.6 s, red after 4.0 s; fourth red after 3.4 s, a gear
the truck cannot pull after 2.9 s, first never amber (0.66 at most). On the idealised slip red after 1.80 s and 0.073 in the two seconds
after; three times the torque 0.50 s. From the end of a red slip the box is amber again after about 2 s and gone within about 10.</p>
<p>Two rates, in percent of the engine a minute (<code>models.h:2189-2247</code>):</p>
{eq('hot = HotDamagePercentPerMinute · (<i>T</i> − <i>T</i><sub>from</sub>)/(125 − <i>T</i><sub>from</sub>)   for <i>T</i> &gt; <i>T</i><sub>from</sub>,    <i>T</i><sub>from</sub> = HotDamageFrom (100 to 124 °C, 118 by default)', 29)}
{eq('cold = ColdDamagePercentPerMinute · (60 − <i>T</i>)/60 · ((min(load, 1) − 0.5)/0.5)²   for <i>T</i> &lt; 60 °C and load &gt; 0.5', 30)}
{eq('points = (hot + cold)/100 · capacity · Δ<i>t</i>/60', 31)}
<p class="where">At the defaults (10 and 5): 10% a minute at 125 °C, 31% at the 140 °C cap; a cold engine floored at the red zone at
0 °C loses 5% a minute, and nothing at moderate revs, as it burns little fuel there. Fractions are carried over and whole points
written; nothing counts while the game window is in the background.</p>
<p><b>Over-revving</b> (BRIEF_overrev_downshift.md). A third rate, over = OverRevDamagePercentPerMinute · min((share − governTo)/0.2, 1) for
share &gt; governTo, with share = min(the truck's speed, the driven wheels' mean)/the gear's top from the torque cut's last answer
(<code>OverRevShare</code>; without the truck's speed the wheels' mean) and governTo the map's governor end. Only on a pass where the map
went out (<code>kCutMap</code>: the cut acting, the clutch in, a gear engaged) and the drive's answers rose; never in A. Half a second
(<code>kOverRevGrace</code>) costs nothing and is not owed after; the clock falls to 0 on the first pass out of the conditions. It needs no
coolant temperature. Points: (hot + cold)·Δ<i>t</i> + over·(the time past the half second), then /100 · capacity/60. The cause in the
log line is the largest of the three rates: "over-revving at ... rpm in ..." with the rpm share · redline. The engine braking in
<code>TorqueCutF</code> keeps rising above the redline to its value at 1.3 of the top (<code>kEngineBrakeOverTop</code>). With Probe=1,
once a second while the share is over the end, "probe: over-rev: ...".</p>
{fig}
<p>The damage goes into the game's own engine damage (the block behind vehicle +0x150; the handle with its second byte cleared, its
flags at +0x10 with bit 0 set and bit 2 clear, data at +0x38: damage, capacity, effect). A write goes out at most once a second,
only with the game in front, no menu open and after half a second of driving; control, vehicle and action must be unchanged, the
capacity between 2 and a million and matching the engine's record. It stops one point short of the capacity: the last point stops
the engine, and that stays the game's to do. With the engine's record found, it also writes the effect the game would, by the
game's rule (Steam RVA 0xd6f690):</p>
{eq('<i>s</i> = damage/capacity;   if <i>s</i> &gt; threshold:  <i>e</i> = (<i>s</i> − threshold)/(1 − threshold),   torque factor = (1 − <i>e</i>) · <i>T</i><sub>min</sub> + <i>e</i> · <i>T</i><sub>max</sub>', 32)}
<p class="where">threshold, <i>T</i><sub>min</sub> and <i>T</i><sub>max</sub> are the engine record's (+0x74, +0x78, +0x7C). Without the record only the
damage is written and the effect follows at the game's next damage event. The lamp is steady past half the capacity and blinks
while damage is being added.</p>
'''


def s_boost():
    ns = [k * 0.005 for k in range(0, 201)]
    ser = [(name, [(n, mm * clamp((n - a) / (b - a), 0, 1)) for n in ns]) for name, (a, b, _, mm) in PROFILES.items()]
    fig = chart(ser, (0, 1), (0, 1.4), 0.1, 0.2, 'Engine speed, share of the red zone rpm', 'Full-load boost, share of BoostMax',
                'Where each turbo profile makes its boost floored, on a truck.', legend='br', yfmt=lambda v: f'{v:.1f}', xfmt=lambda v: f'{v:.1f}')
    classes = [[name, fuel, f'{most:g} bar', f'{a:g} to {b:g}', f'{tau:g} s'] for name, fuel, most, a, b, tau in TURBO_CLASSES]
    rows = [[name, f'{a:g} to {b:g}', f'{tau:g} s', f'{mm:g}'] for name, (a, b, tau, mm) in PROFILES.items()]
    return f'''
<h2>Boost and the turbo</h2>
<p>The turbo is review 12's model as its addendum corrected it (<code>BoostStep</code>). It has two states: the rotor's speed <i>w</i>,
as a share of what makes the most boost, and the manifold's pressure <i>b</i> in bar, gauge. <i>n</i> is the engine speed over the red
zone's rpm, past 1 on the governor; <i>p</i> is the pedal and <i>q</i> the fuel the engine burns, as a share of full load at <i>n</i>.</p>
{eq('full(<i>n</i>) = most · clamp((<i>n</i> − <i>n</i><sub>0</sub>)/(<i>n</i><sub>1</sub> − <i>n</i><sub>0</sub>), 0, 1)', 33)}
{eq('<i>q</i> = min(<i>p</i>, limit<sub>last</sub> · cold) · taper(<i>n</i>);    in N or with the clutch pressed 0.25 <i>p</i> · taper(<i>n</i>)', '33a')}
{eq('diesel: <i>w</i><sub>want</sub> = √(full · <i>q</i><sup>1.6</sup> / most);    petrol: MAP = 0.3 + (1 + full − 0.3) <i>p</i>,  <i>w</i><sub>want</sub> = √(max(MAP − 1, 0) / most)', '33b')}
{eq('<i>w</i> ← <i>w</i> + (<i>w</i><sub>want</sub> − <i>w</i>)(1 − e<sup>−Δ<i>t</i>/τ</sup>),    τ = τ<sub>red</sub> / max(<i>q n</i><sup>4</sup>, 0.02) rising, 1.5 s falling', '33c')}
{eq('<i>b</i> → min(full, most <i>w</i><sup>2</sup>) a diesel;  MAP − 1 while ≤ 0, else min(MAP − 1, most <i>w</i><sup>2</sup>) a petrol;  with 0.1 s', '33d')}
{eq('limit = min(1, <i>r</i> (1 + <i>b</i>) / (1 + full · taper<sup>1.6</sup>)),    <i>r</i> = 1.2 a diesel, 1.0 a petrol', 34)}
<p class="where">taper(<i>n</i>) is 1 up to the curve's last point and past it the curve over that point's torque, the governor's cut.
MAP is a petrol's manifold in bar absolute, set by its throttle: 0.7 bar of vacuum at idle, 0 at about half the pedal on 1 bar, the
most floored. With the engine off or starting <i>q</i> = 0 and the manifold heads for 0. The limit is the powertrain's boost in
equation 11. Settled and floored it is 1 at every speed, as the curve is a turbo engine's full-load curve, measured with the boost it
makes there; only the spool costs (review 12, finding 1). On the governor full(<i>n</i>) is taken at the taper's fuel, as the curve
already falls there: compared with full(<i>n</i>) alone, as the addendum writes it, the limit would cut there a second time. Two stages
drop the boost a tenth for 0.3 s as <i>n</i> passes 0.6. The defaults per class are review 12's (the C13 and the 3406E for a truck), its
τ<sub>red</sub> fitted by the addendum's simulation to the review's bounds; <code>BoostMax</code> takes the place of the class's most.</p>
{table(['Class', 'Fuel', 'Most', 'n0 to n1', 'τ_red'], classes, 'The turbo of each class', 'small')}
{table(['BoostProfile', 'n0 to n1 (a truck)', 'τ_red', 'Most ×'], rows, 'The turbo profiles (BoostProfile 0 to 3)', 'small')}
{fig}
<p>What the model gives, as the tests check it. Settled: a truck's diesel 0.00 bar at idle, 0.26 at a light foot (0.3) at 0.6 of the
red zone's rpm, 0.67 floored at 0.45 and 1.80 at 0.8; a petrol scout −0.70, −0.19, 1.00 and 1.00 bar. At 0.6 of the pedal and 0.8 of
the red zone's rpm the limit is 0.77 on a truck and 0.66 on a scout, over the pedal. From a light foot (0.2) floored, 90% of the torque
comes in 2.0 s at 1100 rpm and 0.9 s at 1760 on a truck, 2.9 s at 1000 and 1.5 s at 1520 on a heavy truck, 1.4 s at 2000 and 0.3 s at
4400 on a scout. After a half-second lift at 0.8 a truck has 1.11 bar 0.1 s after the press (the limit 0.90) and all of it within a
second; after three seconds 0.08 bar, then 0.52, 1.29 and 1.80 at 0.5, 1 and 2 s. A petrol scout's manifold is at −0.69 bar on the
lift and 0.22 bar 0.1 s after the press. Floored from idle through the converter the limit dips to 0.44 and is whole again at 1.5 s on
a truck (0.41 and 2.4 s on a heavy truck). Floored in neutral a truck holds 0.20 bar.</p>
<p>The sounds come from the pedal over the last 0.25 s. A lift of half the pedal with at least 0.22 BoostMax built up is a blow-off;
a lift of a quarter, the pedal then still, with at least 0.11 BoostMax, a flutter; then 0.8 s of quiet. Loudness is
clamp(bar at the lift / BoostMax, 0, 1)<sup>1.5</sup>; at 0.5 or more a blow-off plays the high recording and a flutter the middle one.
Gain = volume × (0.2 + 0.8 loud), pitch ±6% at random. A diesel has no throttle plate and no blow-off valve: unless
<code>DieselFlutter=1</code> its lift asks for the whistle (<code>WhistleSoundOf</code>), which holds only the player's own
<code>turbo_whistle_</code> files, at the same gain and at a pitch of 0.5 + 0.5 · clamp(1.5 (<i>w</i> − 1/3), 0, 1) by the rotor at the
lift; without them the lift is quiet (a whistle made by code was dropped after the author's drive of 8ED18842). The pedal going down past half fades a ringing sound out in 0.08 s.</p>
'''


def s_range_air_tilt():
    ts = list(range(0, 241))

    def fill(spd):
        out, bar = [], 0.0
        for t in ts:
            out.append((t, bar))
            for _ in range(20):
                bar = min(8.2, bar + 0.23 * spd * max(1 - bar / 12, 0) * 0.05)
        return out
    idle_spd = CLASSES['truck'][0] / CLASSES['truck'][1]
    fig = chart([('At the red zone', fill(1.0)), (f'At a truck\'s idle ({CLASSES["truck"][0]} of {CLASSES["truck"][1]} rpm)', fill(idle_spd))],
                (0, 240), (0, 9), 30, 1, 'Seconds from an empty tank', 'Brake air, bar', 'The compressor filling the tank, engine running.',
                hlines=[(8.2, 'cut-out 8.2'), (6.9, 'cut-in 6.9'), (5.5, 'red zone')], legend='br')
    t_red = -12 / 0.23 * log(1 - 8.2 / 12)
    return f'''
<h2>Fuel range</h2>
<p>The range is the fuel left over what the last few kilometres cost (<code>RangeStep</code>, <code>models.h:262-332</code>). Moving
at 0.5 m/s or more, the way driven and the fuel used are marked every 50 m, up to 64 marks, the oldest dropped while the window still
spans 3 km. A step that burns more than 0.1 l counts as filled or leaked, not burned. Once the window spans 500 m (a shorter way is
mostly the pulling away):</p>
{eq('per km = (used − used<sub>0</sub>) / (way − way<sub>0</sub>) · 1000,  smoothed with τ = 10 s;    range = min(fuel / per km, 999 km)', 35)}
<p class="where">Fuel burned standing is not charged to distance but lowers the fuel and so the range. Another truck kind or tank size
starts it over; until 500 m it shows "---".</p>
<p>After the range the fuel use: the game's own rule (<code>TruckFuelRate</code>, litres a minute, 0 with the engine off), through a one-pole
filter of τ = 1 s so it does not flicker with the pedal; one decimal, US gallons an hour (× 60 / 3.785) with Mph, "---" while the rule's
numbers are not read. <code>ShowFuelUse=0</code> leaves it out.</p>
<h2>Brake air</h2>
<p>The engine drives a compressor that fills the tanks; a governor runs it from 6.9 bar up to 8.2 and lets it rest in between
(<code>AirStep</code>, <code>models.h:2092-2160</code>):</p>
{eq('gives = 0.23 · speed(rpm) · max(1 − bar/12, 0) · Δ<i>t</i>   while it runs', 36)}
{eq('used = 0.35 · max(<i>b</i> − <i>c</i>, 0) + 0.4 (handbrake let go) + (0.1 + 0.25 while braking) / 60 · Δ<i>t</i>', 37)}
<p class="where"><i>c</i> is the brake chambers' fill: up with the brake at once, and after a release down toward it at 2 a second, so the
chambers are empty half a second after the foot comes off (BRIEF_tester_1114.md, part 193). A press costs what fills them again: a
brake read flickering five times a second costs about a third of a full press each time, slow pumping the full press. While the mod
holds the spring brakes on, a release of the handbrake that it undoes costs nothing (<code>AirStep</code>'s springsHeld). Where the
brake read rises and falls more than three times in a second, Gauges.log has one line with the next second's samples, "brake air: the
brake as the game reads it rose and fell N times in a second", at most once a minute.
A full brake application takes 0.35 bar, letting the handbrake go 0.4 (the air that holds the spring brakes off), and
the system leaks 0.1 bar a minute, 0.35 with the brake held. From empty the tank fills in {t_red:.0f} s at the red zone and about 3
minutes at idle; eight full applications with the engine off take a full tank to {8.2 - 8 * 0.35:.1f} bar, the red zone's edge
(5.5).</p>
{fig}
<p><b>Air brakes by truck and the spring brakes</b> (BRIEF_spring_brakes.md). <code>AirBrakesOn</code> is the truck's own choice where its file holds one
(AirBrakes, 0 or 1, in the suspension's sections, the owned truck's over the kind's), else every class but the scout, by the class the automatic goes by for the truck
(<code>AutoClass</code>: the class picked for it, else the game's truck type, else its weight), the plain row's pick only where
neither is read. The mod's thread works it out once a pass, and not before the truck's type has been tried, so it never flips a pass after
the truck is found (<code>air brakes: %s for vehicle</code>). The draw gets the answer in the state (<code>noAir</code>). Everything of the brake air asks it:
the model (<code>AirStepFor</code>: off, it does not run and holds no pressure), the dial, the HUD's cell, the warning and the purge.
<code>SpringHeldStep</code> holds under 3.0 bar and lets go at 4.5; the 0.4 bar the release takes leaves the needle over the hold.
The truck is held by the game's own handbrake, the byte at action + 0x48, through <code>LockHoldStep</code> with a hold of its own: a handbrake the player set first is never cleared,
the mod's own is set again when a key clears it and cleared at the release. No hook and no write to a wheel or a body. This route is not proved in the game yet.
Gauges.log has "spring brakes: hold at ..." and "spring brakes: release at ...", one line in capitals if a held truck still rolls on flat ground, and with Probe=1 "probe: spring brakes: ..." a second.</p>
<p><b>The air kept by the owned truck</b> (<code>air_keep.h</code>; BRIEF_jobs_1114.md, part 165). The mod's thread keeps a list of the ids driven in this run with the bar each
was left with (<code>AirRun</code>), updated each pass from the model, so a truck away loses nothing by the clock. The list follows the take-over and the match
(<code>AirRunRename</code>), so the same truck under a new id is no first drive. Between runs the bar is <code>AirBar</code> in the truck's <code>[Odometer:&lt;id&gt;]</code>,
written with the count at the odometer's own moments and at a truck change where the bar moved, and read ranged (0 to 8.2, anything else is no figure). The take-over, the
warehouse match, the marks and the prune carry the key with the section, and no profile holds it. The bar a truck starts with is decided where the odometer's count is, once
the take-over and the match have decided (<code>AirAtDecision</code>): as left in this run, else 0 with <code>AirEmptyAtStart</code> (the box Empty tanks at a start), else its
file's figure, else full. A truck whose id is not read starts full and keeps nothing. Until the decision the model runs from full, the spring brakes are not judged
(<code>AirJudged</code>) and the dial keeps the figure it showed. The decided bar goes into the model on its first step for a truck with air brakes
(<code>AirStartStep</code>), with one line: <code>brake air: &lt;kind&gt;, id</code>, the bar and where it came from. A truck without air brakes keeps no figure and its key
goes at the next write.</p>
<p><b>Truck profiles</b> (BRIEF_truck_profile.md, build 1; <code>truck_profile.h</code> and a block in <code>gauges.cpp</code>).
The storage is not changed: a profile is written from the truck's, the engine's and the gearbox's files and goes back into them
through the functions an edit uses. The file is <code>Gauges\\profiles\\&lt;kind&gt; - &lt;name&gt;.ini</code>, UTF-16,
written beside its place and then put there whole. Its sections: <code>[Profile]</code> (Version, Truck, Date, Engine, Gearbox,
Notes, and Backup in a backup), <code>[Truck]</code> (the seven scales together or <code>ScalesOwn=0</code>, RoadCdA, AirAxles,
OdometerHours, AirBrakes), <code>[Drive]</code> (DiffType and DiffAxle by axle as the truck's file has it, DiffAxle2 for the second axle, else <code>DiffOwn=0</code>; a DiffAxle9 is an unknown key),
<code>[Suspension]</code> (the whole set in force), <code>[Engine]</code> (Torque, GearHold, EngineBrake, Creep, CreepManual, PeakTorqueRpm, Aspiration, Fuel,
TorqueCurve with no part name, only what some level holds) and <code>[Tire Inflation]</code> (Mode from the tire mod's file; an Import
writes it back into that file, only where the file is there and the game names the truck: Own=1 and Mode into the truck's own section,
or Mode alone into the kind's; a backup of a level with no mode says <code>ModeOwn=0</code>). The truck's id, [Seen], the odometer's count and the learned figures never go in. A backup holds one level exactly: where the level has nothing of its own it says <code>AirOwn=0</code>, <code>SuspOwn=0</code> or <code>EngineOwn=0</code>, and taking it puts that part out of force again. Import writes the diff group into the truck's own section, whole. A backup of the kind's level also takes RoadCdA, AirAxles and DiffType out where it does not hold them.</p>
<p>All file work is on the mod's pass. The tab only asks (<code>ProfileAskPut</code>) and shows the list the pass made
(<code>ProfilesGet</code>, 50 rows at the most, the rest counted, the driven kind's files first). An edit that waits for its
save is saved before the asking is dealt with. An Import checks the file first: 64 KB at the most, a truck named in its head, a
row name with no path, another kind only after the second press. Then it writes a backup of the levels it will change into
<code>Gauges\\profiles\\backup</code>; the backup is itself a profile. Where the backup cannot be written nothing is taken.
Import writes the truck's own sections (its id must be read). <b>For every truck of this kind</b> writes the kind's level and
the [Truck] keys. The engine's part is taken only for the same engine and the hold only for the same gearbox, compared as the
id's match compares them. The engine's shared file is written only with <b>Also the default of the engine</b>, the gearbox's
never. Each figure goes into its range, unknown keys are counted and left out, and a text of the file is cleaned, cut and never
a format or a path. Gauges.log has <code>profile: exported</code>, <code>profile: Gauges\\profiles\\... could not be
written</code>, <code>profile: the backup Gauges\\profiles\\backup\\... could not be written</code> and a line naming
what was taken. Build 1 does not take the tire mode.</p>
<h2>The tilt lamp</h2>
<p>The world-up parts of the chassis' forward, up and left axes (chassis +0x170, +0x180, +0x190) are smoothed with a 0.3 s time constant,
so a bump does not light the lamp (<code>models.h:334-386</code>):</p>
{eq('pitch = asin(forward<sub>y</sub>),    roll = asin(left<sub>y</sub>)  (on its roof: ±180° − roll)', 38)}
<p class="where">Red from <code>RollDangerDeg</code> (25°) of roll or <code>PitchDangerDeg</code> (35°) of pitch, amber from
<code>RollWarnDeg</code> (15°) of roll; pitch has no amber. Each stays until 2° under its angle, so it does not flicker.</p>
'''


def s_suspension():
    return f'''
<h2>Suspension</h2>
<p>Each wheel of a truck is a Havok wheel constraint (<code>hkpWheelConstraintData</code>, found once by its vtable,
<code>suspension_scan.h</code>). The tune writes four of its values: the spring's strength (+0xC4), the damping (+0xC8), the
compression end SuspensionMin (+0xB4), and the attach point the spring hangs from (+0xA0 to +0xA8, in the frame of the body
the constraint is on). A detour in the wheels' drive reads each wheel's place <i>x</i> along the axis and its speed <i>v</i>
every physics step and writes there, so the features act on each step, not on the mod's 50 ms pass
(<code>suspension_step.h</code>). With that hook in, the spring and damping factors are written there too, so a wheel's spring
and damping land in the same step. The hook takes each wheel from the truck's action as the game walks it (the truck at
+0x30, its wheel models from +0x200) and writes only where that wheel, its constraint and the constraint's class are the
ones the mod's thread handed it. Before each write it reads the field. A word the mod wrote there, or the stock, it writes
over. Any other word is the game's own, from an upgrade or a repair: the wheel is left, and the mod's thread takes that word
as the new stock. A constraint at a new address that holds the mod's last word keeps the old stock, so a tune is never
applied twice. The camber, the torque factor and the power stay on the mod's thread: the solve does not read the camber,
and the other two are single words. The stock values are read once a truck. A truck left tuned gets them back when another is
driven, so a truck driven again is scaled once and never twice. Compression is negative <i>x</i>; <i>L</i> is the
compression travel as tuned and <i>u</i> = −<i>x</i> / <i>L</i>, from 0 to 1, how far into it the wheel is.</p>
<h3>Spring, damping and travel</h3>
{eq('<i>k</i> = clamp(<i>k</i><sub>stock</sub> · Spring<sub>end</sub> · <i>f</i>(<i>u</i>), 0.001, 1),    <i>c</i> = clamp(<i>c</i><sub>stock</sub> · Damping<sub>end</sub> · <i>G</i>(<i>v</i>) · <i>z</i>(<i>u</i>), 0.01, 1)', 41)}
<p class="where">The game holds both at 1, so a factor can act in full only while the stock figure times it stays under 1
(the Arocs' front damping is 0.5: a factor above 2 is cut). <i>f</i>, <i>G</i> and <i>z</i> are 1 with their features off.
ExtraTravel moves SuspensionMin by its cm toward the body (SuspensionMin − ExtraTravel / 100 m), from a file's 0.0 too, and
never back.</p>
<h3>Front and rear</h3>
<p>Spring and damping have a factor for each end (SpringFront, SpringRear, DampingFront, DampingRear). A wheel's end is made
once a vehicle (<code>SuspEndsRule</code>, <code>suspension_scan.h</code>), at the first pass where every wheel's place is
read, from the wheel centres in the chassis's frame. A wheel is rear where another wheel is more than 0.2 m ahead of it and
less aside than ahead. It is front where none is. So the first axle is front and every axle behind it rear, a second steered
axle too, and a truck first seen bent or at full steer gets the same ends. The ends are kept until the vehicle or its wheel
count changes. The mod's thread, the step's hook and the tab take a wheel's factor from that one list. Only the driven
truck's own wheels are written, never a trailer's. <code>Gauges.log</code> names the front and the rear wheels once a truck
("suspension: front wheels ..., rear wheels ...").</p>
<p>While both ends of a value are alike it is written without the list, as before. With the ends apart and the list not made
yet nothing new is written, and the log says so once a truck ("suspension: front and rear are set apart and this truck's
front and rear wheels are not known yet ..."). One wheel whose place is not read in 20 passes, on a truck otherwise placed,
is taken as rear and named in the log ("suspension: wheel 3's place was not read in 20 passes: it is taken as rear"). With one end at 1 that end's wheels get their stock each step. The
ride height and the sway bars keep their own rule, the frontmost axle within 0.2 m, which is the same axle on a truck
standing straight.</p>
<p>The older keys SpringScale and DampingScale are read first as both ends, then each new key over them, from
<code>Gauges.ini</code>, a truck's <code>[Suspension]</code> sections and a kind's. Reading an old file writes nothing. A
save writes the four new keys. It writes the old key too while both ends are alike, so an older build reads the same value.
Once they differ it removes the old key, and an older build takes that value from the level below, or 1.</p>
<h3>The damper</h3>
<p>Havok's damper is linear: its force is the damping times the shaft speed. So <i>G</i> is written each step, with
<i>w</i> = |<i>v</i>| / knee, <i>L</i><sub>d</sub> and <i>H</i><sub>d</sub> the low and high speed factors (compression for
<i>v</i> &gt; 0, rebound for <i>v</i> &lt; 0) and <i>n</i> 1 linear, 0.6 digressive, 1.6 progressive (<code>DamperG</code>):</p>
{eq('<i>G</i> = <i>L</i><sub>d</sub>  for <i>w</i> ≤ 1;    <i>G</i> = (<i>L</i><sub>d</sub> + <i>H</i><sub>d</sub> (<i>w</i><sup><i>n</i></sup> − 1)) / <i>w</i>  above it', 42)}
<p class="where">The force is <i>L</i><sub>d</sub> times the stock's up to the knee, then <i>H</i><sub>d</sub> knee
(<i>w</i><sup><i>n</i></sup> − 1) on top. Under 0.005 m/s <i>G</i> is <i>L</i><sub>d</sub>. All four at 1 and linear give
<i>G</i> = 1 at every speed, and the damper does not run.</p>
<h3>Spring curves, bump stops and damping zones</h3>
<p>Havok's spring force is the strength times <i>x</i>, so a feature is given as a rate <i>r</i>(<i>u</i>), a factor of the
stock's slope, and written as its secant, so the force is continuous at every edge:</p>
{eq('<i>f</i>(<i>u</i>) = (∫<sub>0</sub><sup><i>u</i></sup> <i>r</i> d<i>u</i>) / <i>u</i>;    progressive <i>r</i> = 1 + (SpringRise − 1) <i>u</i><sup>2</sup>;    dual rate <i>r</i> = 1, then SpringRise from SpringCross', 43)}
<p class="where">Dual rate blends over 0.05 of the travel. The droop side (<i>x</i> &gt; 0) stays at 1.</p>
{eq('bump force += (BumpForce − 1) · <i>F</i><sub>end</sub> · <i>s</i><sup>3</sup>,    compression damping × (1 + (BumpDamping − 1) <i>s</i><sup>2</sup>)', 44)}
<p class="where"><i>s</i> is the share of the stop the wheel is into, over the last BumpZone m (at most <i>L</i> / 3), and
<i>F</i><sub>end</sub> the spring's own force at the end, so at the end the stop pushes BumpForce times as hard. The stops go
by where the wheel will be a step on (<i>x</i> + <i>v</i> Δ<i>t</i>), as at 2.5 m/s it crosses a stop in two steps. The
damping zones give <i>z</i> = ZoneSoft from ZoneFrom to ZoneTo and ZoneFirm toward either end, blended over 0.1 of the
travel.</p>
<h3>Ride height and sway bars</h3>
<p>The spring's zero and both travel ends are measured from the attach point, so a shift <i>d</i> of it along the body's up
moves the wheel's rest place and its travel by <i>d</i> and the corner rises by about <i>d</i>, as the file's Height does.
Every shift is held to ±0.12 m, the game's own DeviationMax range. RideFront moves the frontmost axle, RideRear the others.
A sway bar works on the two wheels of an axle (within 0.2 m along), with <i>c</i> each side's compression and <i>k</i> its
strength:</p>
{eq('<i>F</i> = <i>K</i> (<i>c</i><sub>L</sub> − <i>c</i><sub>R</sub>),    <i>K</i> = Sway · mean(<i>k</i>);    <i>d</i><sub>L</sub> = <i>F</i> / (2 <i>k</i><sub>L</sub>) toward the droop, <i>d</i><sub>R</sub> the same toward the bump', 45)}
<p class="where">The more compressed side is pushed down and the body is held level. Above 0.3 the bars swing, so the
setting stops there, and a higher value in the ini is held at 0.3 with one log line.</p>
<h3>Air suspension and Level to horizon</h3>
<p>At the first standstill after the truck is taken (1 s under 0.1 m/s), each wheel's place, less the ride height's shift,
is its rest place. From then on, under 10 km/h, the errors from it are fitted by a plane over the wheels' places in the
chassis (least squares: height, pitch and roll), and each wheel follows the plane at its place with a time constant of 3 s,
never its own error. A rigid chassis can follow only those three, so its twist and a middle axle's place are the ground's,
and a cross-axle crawl never winds it up. An axle with a wheel hanging or at its compression end is left out of the fit.
The ride height and the keys' height (2 cm a press, within 10 cm, under 20 km/h, back to 0 on another truck) go on top of
the rest place, so the levelling never takes them back (<code>SuspLevelStep</code>).</p>
<p>Only the ends ticked Air are fitted and shifted (<code>air_axles.h</code>). The ticks are a mask, <code>AirAxleMask</code>
(bit 0 the front axle, bit 1 every other), kept with the suspension set in <code>[Suspension]</code> or
<code>[Suspension:&lt;id&gt;]</code> of the truck's file and never in Gauges.ini. It is written only once the player ticks. A set
saved for any other reason carries it only where it has one, so a table fixed later still reaches a truck never ticked, and
Defaults takes it out. Without one the start order gives it: this truck's ticks, else the kind's, else <code>AirAxles</code> in
<code>[Truck]</code> of its file (all, rear, none, front), else the mod's table of vanilla trucks by kind (none 13, rear 19, all 4;
the sources are the team's, not in the repo), else none. It is read again when the kind or the truck's id changes, and the
ticks come with the set in force. Rear is every axle but the front one, by place, the split RideFront and RideRear use. With
leaf axles beside air ones the plane is held at 0 at the leaf wheels' mean place: the fit goes through that point, and the
height moves with the pitch and roll, so a 4x2 with one air axle still levels its pitch about the front. A leaf axle gets
neither the levelling nor the keys' height. RideFront, RideRear, the sway bars, the springs and the dampers stay on every axle.
A tick changed under a level goes the way AirSuspension off and on goes: the plane and the keys' height fade out with the wheels
the level began with (<code>SuspLevelFade</code>), then the rest places are taken again, so no wheel snaps back or jumps to the
keys' height. The log says once per truck, and again when the mask or its source changes, "suspension: air on every axle but
the front one (rear)", "suspension: air on the front axle only (front)", "suspension: air on every axle (front and rear; ...)" or
"suspension: no axle ticked Air, leaf on every axle", with where it came from. Sync all enabled (<code>AirSync</code>, a view
setting) shows a slider for the keys' own figure, set by the keys' rules; it is never saved and is back at 0 on another truck. Swing axles (101): a wheel's
CamberSuspensionMultiplier at wheel + 0x8C over 1 with its CamberAnglePhysics at + 0x88 at 0, as the five swing trucks have
it; "suspension: swing axles, the camber follows the travel" says so once per truck. It reads the stock <code>camber.h</code>
keeps, so a gain of x0 written there still marks a swing axle.</p>
<p>Camber gain and Camber at rest (<code>camber.h</code>). The game's wheel parser stores CamberAngleRender at wheel + 0x84 and
CamberAnglePhysics at + 0x88 in radians, and CamberSuspensionMultiplier at + 0x8C as written (found in the code of 6ab52f5d and
6a607c05). The look's code reads + 0x84 and + 0x8C from the wheel object on each call and turns the tilt to the wheel's side
itself, so one figure serves both sides. The parser fills the wheel object itself, so the stock is the first read of each wheel
object, kept by its address. Of the 128 places kept, one is given up only once its object holds nothing the mod wrote; with
none to give up a new wheel is neither kept nor written, and "camber: all 128 kept wheels still hold values the mod wrote" says
so once. A value equal to the word the mod last wrote there is the mod's: a rebuild that leaves it never
becomes a stock, and a gain of x2 stays x2 of the file. Any other value is a new stock. A new truck, new wheel objects or new
constraints start a generation. The gain multiplies the stock multiplier. The offset goes onto the stock angle with the stock's
own sign, within the parser's 30 degrees. The front is the front axle, as RideFront splits. At x1 and +0 nothing is written, and
a value the mod wrote goes back to the stock. On the pass after a write the value is read again. One back at the stock is logged
("camber: n values back at the file's the pass after the write") and not written again until a rebuild. Once per generation
"camber: generation n, per wheel the render and physics camber in degrees and the multiplier as read" lists every wheel, and each
write logs "camber: n values written" with what is read back. CamberAnglePhysics is never written. A truck left keeps what its set
wrote.</p>
<p>With <code>AirHorizon</code> the pitch and roll follow the chassis's tilt to the horizon, as the tilt lamp reads it,
instead of the wheels' plane. The 12 cm of shift give about 7 degrees side to side; past that it gets as close as it can.
Over 10 km/h the pitch and roll fade back to 0 and the height is kept.</p>
<p>If the game moves an attach point the mod wrote (a truck with its own active ride height), the ride height, the sway
bars and the air suspension stop for that truck. The log lines start with <code>suspension:</code> (each wheel's stock and
what was written, the stock put back, the attach point moved by the game, the sway bars disconnected or connected) and
<code>air suspension:</code> (the rest places taken, the levelling front, rear and left over right, the keys' height).</p>
'''


def s_lamps():
    rows = [['Handbrake', 'truck action +0x48', 'set', 'red'], ['Headlights', 'vehicle +0x768 bit 2', 'set', 'green'],
            ['All wheel drive', 'truck action +0x49', 'set', 'green'], ['Differential lock', 'truck action +0x4A', 'set', 'amber'],
            ['Low gear', 'gear 1, game automatic off, power factor', 'L−, L, L+', 'amber'],
            ['Engine damage', 'the engine damage block', 'past half; blinks while adding and 2 s after', 'amber'],
            ['Cold engine', 'the coolant model', 'under 60 °C', 'blue'], ['Tilt', 'chassis axes', 'see above', 'amber, red'],
            ['Oil', 'the oil model', 'blinks 400 ms at OilWarnC', 'red'], ['Tire', 'the tire mod\'s block', 'see Shared memory', 'amber, green, red']]
    return f'''
<h2>Lamps and dials</h2>
{table(['Lamp', 'Reads', 'Lit', 'Colour'], rows, 'The lamps (gauge_draw.h:729-777)', 'small')}
<p>Dials: fuel red under 10%; coolant 50 to 130 °C, blue under 60 and red from 118, with the oil as a second, amber needle; the oil
dial 50 to 150 °C, red from <code>OilWarnC</code>; boost from 0 (a diesel) or −1 bar (a petrol engine) to 2 bar, or 3 for a turbo of more than 2 (× 14.5 for psi); brake air 0 to 10 bar, red under 5.5,
hidden on a truck without air brakes (<code>AirBrakesOn</code>: the Air brakes choice, else every class but the scout, by the class the automatic goes by); the clock, twelve hour marks and two hands over the game's
hour (no hands while the clock is not found), with the outside temperature in force as a number in its face. The speed is the chassis velocity along its forward axis,
× 3.6 for km/h. Every dial sweeps from 150° to 390°.</p>
'''


def s_outside():
    hs = [k * 0.1 for k in range(0, 241)]
    ser = [('Alaska, AmbientC −12 (−7 by day, −17 at night)', [(h, -17 + 10 * day_share(h)) for h in hs]),
           ('Michigan, AmbientC 12', [(h, 7 + 10 * day_share(h)) for h in hs]),
           ('A section with DayC 30, NightC 14', [(h, 14 + 16 * day_share(h)) for h in hs], {'dash': '5 3'})]
    fig = chart(ser, (0, 24), (-20, 32), 2, 4, 'Hour of the game\'s day', 'Outside, °C',
                'The daily curve: coldest at 05:00, warmest at 15:00.', legend='out', vlines=[(5, ''), (15, '')])
    third = (len(REGIONS) + 2) // 3
    cols = [REGIONS[i * third:(i + 1) * third] for i in range(3)]
    rows = []
    for i in range(third):
        r = []
        for c in cols:
            r += [c[i][0], f'{c[i][1]} °C'] if i < len(c) else ['', '']
        rows.append(r)
    return f'''
<h2>The outside temperature</h2>
<p>With <code>AmbientByMap=1</code> the mod uses the first section that exists in <code>Gauges.ini</code>: <code>[Map:&lt;map name&gt;]</code>,
then <code>[Map:&lt;region&gt;]</code>, then <code>[Map:&lt;level id&gt;]</code>; otherwise <code>AmbientC</code> of <code>[Gauges]</code>. A
section gives <code>AmbientC</code>, a mean that swings 10 °C (5 either way), or both ends, <code>DayC</code> and <code>NightC</code>. The
day follows the game's clock (<code>DayNightC</code>, <code>maps.h:130-143</code>), with <i>h</i> the hour:</p>
{eq('5 ≤ <i>h</i> ≤ 15:  share = ½ − ½ cos(π(<i>h</i> − 5)/10);    otherwise:  share = ½ + ½ cos(π · since/14),  since = hours after 15:00', 39)}
{eq('<i>T</i> = night + (day − night) · share', 40)}
<p class="where">Without a clock the mean holds.</p>
{fig}
{table(['Region', 'Mean', 'Region', 'Mean', 'Region', 'Mean'], rows, 'The regions\' first values, written into an ini without map sections (maps.h:15-21): a guess for the player to set', 'small')}
<h3>Which map</h3>
<p>The game does not say which map it is on in a place the mod reads, so the mod reads the newest save every 5 seconds for its
<code>"lastLoadedLevel"</code>: Steam's <code>CompleteSave*.cfg</code> under <code>userdata\\&lt;account&gt;\\1465360\\remote</code> (the Steam
folder from the registry, or five folders up from the game), Epic's <code>CompleteSave*.dat</code> under
<code>Documents\\My Games\\SnowRunner\\base\\storage</code>, and the Microsoft Store's files under
<code>%LOCALAPPDATA%\\Packages\\*SnowRunner*\\SystemAppData\\wgs</code> whose first 95 bytes hold "CompleteSave". A save over 64 MB is
skipped. Map names come from the game's <code>strings_english.str</code>.</p>
<h3>The game's clock</h3>
<p>In the Steam build the hour is a float at [exe + 0x2a8eb48] + 0x150, found in the running game on 2026-10-10. On another build a
signature gives the static and the same +0x150 is assumed; a <code>[Clock]</code> section with the build's stamp overrides both. A
reading counts in 0 to 24.5 and is taken modulo 24; unreadable for about 5 minutes of driving, the place is forgotten.</p>
<p>With <code>Probe=1</code> and no clock known, the mod looks for one while you drive (<code>clock_scan.h</code>): it follows the
pointers in the exe's statics from 0x2a80000 to 0x2ac0000 two levels down (0x800 bytes of up to 4096 objects, then 0x200 bytes of up
to 16384) and keeps every float in (0.0001, 24.5) and every double, up to a million slots (about 50 MB). It looks again every 10 s of
the game's own time. A slot is steady after 4 looks at an even pace (within 10%) between 2.3·10<sup>−5</sup> and 0.08 units a second,
a day of 5 minutes to 12 hours; values above 1.05 are hours, others a share of the day. A jump of an hour or more off pace, then two
looks back on pace, marks the clock: skipping the night (T) or waiting in a garage makes that jump. The first jumped steady slot is
the clock, or the only steady one after 180 s; it is saved to <code>[Clock]</code>.</p>
'''


def s_hooks():
    places = [['Control (the truck you drive, +8)', '0x2a8eb78'], ['Wheel class', '0x2258918'], ['Game (menus, loading)', '0x2a8eb90'],
              ['Map screen', '0x2a8eb30'], ['Viewer', '0x2a8eb38'], ['Garage', '0x2a90728'], ['Input', '0x2a8ebe0'],
              ['Engine sets', '0x2a9e8e0'], ['Clock static', '0x2a8eb48']]
    offs = [['vehicle', '+0x768', 'flags: engine on, started, headlights, high beam, reversing, brake lights (bits 0 to 5)'],
            ['vehicle', '+0x114, +0x11C, +0x730', 'simulated seconds, stall seconds, start timer'],
            ['vehicle', '+0x150, +0x148, +0x158', 'engine, gearbox and tank damage handles'],
            ['vehicle', '+0xE38 to +0xE6C', 'fuel burn numbers'],
            ['vehicle', '+0x930, +0x968, +0x9C0 (+0x9A0)', 'engine set names, engine fitted, gearbox fitted (the truck\'s default gearbox)'],
            ['vehicle', '+0x60, +0x68, +0x200', 'model, truck action, wheel models'],
            ['truck action', '+0x38, +0x3C, +0x44', 'power factor, automatic flag (written), throttle (forward is negative)'],
            ['truck action', '+0x48, +0x49, +0x4A', 'handbrake, all wheel drive, differential lock'],
            ['truck action', '+0x58, +0x70, +0x74', 'gear list, gear engaged, gear'],
            ['truck action', '+0xB4, +0xCC', 'the game\'s pull, torque factor (written)'],
            ['model', '+0x5E8, +0x5F0, +0x588, +0x784', 'fuel, tank, kind, mass'],
            ['chassis (model +0x230)', '+0x170, +0x180, +0x190, +0x1A0, +0x230', 'forward, up, left axes, position, velocity'],
            ['wheel (model +0x2C8)', '+0xE8, +0x13C, +0x16C, +0x17C', 'drive type, centre, turning speed, water depth'],
            ['engine record', '+0x60, +0x68, +0x6C, +0x80', 'Torque, MaxDeltaAngVel, FuelConsumption, EngineResponsiveness'],
            ['engine record', '+0x74, +0x78, +0x7C, +0x84', 'damage threshold, torque factors, capacity']]
    return f'''
<h2>Getting into the game</h2>
<p>The loader loads the mod; the overlay draws in the game's Present, and everything else happens on the mod's own thread. The mod
compares the exe's time stamp with the Steam build's. On that build it uses the places it knows; on any other it looks for them
with signatures harvested from the Steam build (harvest 3, <code>game_globals.h:45-66</code>): each must match exactly once, and two
that point to different places count as not found. Control, the wheel class and the game object are needed: a look that misses one is made again every 2 s, 31 looks in all (a mod
loaded before the game's code is ready, as through another mod's early DirectX probe), then the mod stands down and the tab says so; the map, viewer, garage, input, engine sets and clock are optional. The objects' offsets are taken as they are
on other builds.</p>
{table(['Place', 'Steam build RVA'], places, 'The places the mod reads from (truck.h:57-58, game_globals.h:24-29)', 'small')}
{table(['Object', 'Offset', 'What'], offs, 'The fields it reads, and the two it writes (truck.h)', 'small')}
<p>The engine's record is found the way the game finds it: through the engine sets list (entry: next +0, name +0x10, tree +0x30),
a tree walk as the game's (RVA 0xc3c010), a text compare as its RVA 0x4d8230. Its fields were matched against a scout's file
(Torque 195000). The mass was found by a scan of five trucks against their files.</p>
<h3>Guards</h3>
<ul><li>Every read goes through <code>ReadProcessMemory</code> and every write through <code>WriteProcessMemory</code> on the game's own
process, and addresses below 0x10000 are refused: an object freed under the mod is a failed call, not a crash.</li>
<li>The truck is found again every 500 ms and after any failed read; a read fails if control no longer points at the same vehicle. A
truck must look sane: under 200 m/s, a tank under 100000 l with the fuel at most 1.01 of it, gears −1 to 32, throttle within ±2. A
new kind is taken after 2 passes.</li>
<li>The wheels are recognised by their class pointer; at most 32 wheels are read, and a gear list of 32 entries (R, 30 gears and H). A longer list is logged once a truck, and its gears are not read.</li></ul>
<h3>What it writes and calls</h3>
<ul><li>With <code>Damage</code> on, the driven truck's engine damage, and with the record its effect and torque factor.</li>
<li>With <code>Gearbox=1</code>, the automatic flag (truck action +0x3C), and it calls the game's <code>ShiftGear(vehicle, gear)</code>. That
function is found by a pattern, also when another mod has hooked its start with a jump; it is called on the game's own thread, from the truck update the mod hooks (A shift, step by step), inside an exception guard, and a fault turns it off for the session. With <code>ShiftOnGameThread=0</code> (ini only), or without that hook, it is called from the mod's thread as before.</li>
<li>With <code>Powertrain=1</code> and during the pull test, the torque factor (truck action +0xCC).</li>
<li>With <code>Creep=1</code>, nothing in the truck: the game's wheels' drive is detoured (Creep at idle), at every <code>Gearbox</code>
level, the first time a truck is driven with it on, and left in place until the game is started again.</li>
<li>With all of them off it writes nothing.</li></ul>
<h3>Drawing</h3>
<p>Next Gen's overlay (<code>ng_overlay.cpp</code>, the same file in Next Gen Tire Inflation) hooks Present, slot 8, and Present1,
slot 22, on the game's own swap chains through method tables of their own. Where ReShade is installed, ReShade's presents hand it
each swap chain the game makes (<code>ng_reshade.cpp</code>); without ReShade it takes the game's imports of
<code>CreateDXGIFactory1</code> and <code>D3D11CreateDevice</code> and gives each factory and swap chain they bring a method table of
its own. DirectX's own tables are never written, so the Steam overlay, which hooks what those hold, never meets the overlay's
Present. The release brings ReShade 6.8.0 without its start banner, with a <code>ReShade.ini</code> that loads no effects and keeps Home for ReShade's own window: on a tester's PC with RivaTuner, Discord's overlay and two monitors the game crashed without ReShade and not with it. Where ReShade is missing or does not take the overlay as an add-on, the start says so in the log, in a line at the top for 15 s that the first Next Gen mod in order draws, and in the menu above the tab's page (<code>NgReShadeCheck</code>). In the hook the mod draws with Dear ImGui 1.92.1, compiled in, on the game's device, through a render
target view of the back buffer made for that frame; the game's render targets and state are put back before Present goes on. A
16-bit float back buffer (HDR, scRGB) gets the colours in linear light at a paper white of 203 nits.</p>
<p><b>The start against the game's swap chain</b> (BRIEF_overlay_late_hook.md). The game can make its swap chain within a second of its start, and a chain made before the import hooks are in is never seen by them. So the mod's thread sets the hooks first, right after the log's head (<code>OverlayHooks</code>), before the physics rate's look and the fonts: "overlay: the hooks are in: a swap chain the game makes from now on gets the overlay's Present (through its import...". A present that comes before the fonts are read is called on and draws nothing until <code>NgOverlayReady</code> (one interlocked write, with <code>NgClient::waitReady</code>). The start's last overlay line is now "overlay: the overlay is set up for the cluster and the Gameplay tab of the NEXT GEN menu; the line Present hooked says when a swap chain is taken": only "overlay: Present hooked on a swap chain the game made ..." says that it draws.</p>
<p>Where the chain was made first all the same, the overlay's watch thread sees a window of the game up for a second and no chain taken. With Spotter in the game it registers its callback from the mod's own thread, which Spotter's API allows: "overlay: no swap chain came within a second of a window (the game likely made its own before the overlay's hook was in): the overlay draws through Spotter's frame hook, asked for from the mod's own thread". On Spotter's road the overlay has no Present of its own, so its watch thread asks Spotter whether it still draws: when it does not for the wait's length the log has "overlay: THE OVERLAY DRAWS NOTHING: Spotter says it draws no more and the overlay has no swap chain of its own; the other Next Gen mods are asked, and the next swap chain the game makes is taken", once. Else it asks the other Next Gen mods through a block of its own, <code>Local\\NextGen.Chain.&lt;process id&gt;</code> (version 1: <code>want</code>, <code>offered</code>, <code>taken</code>, <code>offer</code>; the menu's block is not changed): "overlay: no swap chain came within a second of a window (the game likely made its own before the overlay's hook was in): asking the other Next Gen mods for it". A mod that draws sees the asking in its own Present, where the chain is alive, writes the chain's pointer to <code>offer</code>, waits at most 100 ms and clears it: "overlay: a Next Gen mod that started after the game's swap chain asked for it: offered in this Present and taken after ..." or "overlay: a Next Gen mod that started after the game's swap chain asked for it: offered in this Present and not taken within ...". The late mod's thread gives the chain its method table as at the making: "overlay: the swap chain taken from another Next Gen mod's Present", or "overlay: another Next Gen mod offered the swap chain but it could not be given the overlay's Present (the lines above say why)". No reference is held on the chain and no pointer is kept past that Present. A mod asks once a session, its asking is answered once (also by a chain it takes at its making after all), and an asking is served once. A block of another version: "overlay: another Next Gen mod hands swap chains over another way (block version ...".</p>
<p>With nobody to offer, the next chain the game makes is taken. Ten seconds with a window up and no chain: "overlay: THE OVERLAY DRAWS NOTHING: no swap chain came while a window was up (the game likely made its own before the overlay's hook was in), and no other Next Gen mod handed one over; the next swap chain the game makes is taken (a change of the screen mode can make one)", once. On ReShade's route: "overlay: THE OVERLAY DRAWS NOTHING: ReShade took the overlay as an add-on but handed it no swap chain of the game's". These replace the line of two minutes. <code>test\\late_test.cpp</code> runs the three cases on a WARP swap chain: the chain before the hooks, the hooks before the chain, and the handover.</p>
<p>The NEXT GEN menu is one window for every Next Gen mod in the game, kept in named shared memory
(<code>Local\\NextGen.Menu.&lt;process id&gt;</code>): the tabs, the one chosen, whether it is open, where it is, and its key
(<code>NextGen.ini</code>, F2 by default). A tab's slot holds <code>ready</code>: 1 for a tab to draw, 2 for a tab whose mod says
the other mods keep drawing under it (<code>NgClient::keepOthers</code>). The cluster hides under another mod's tab unless that
tab's <code>ready</code> is 2. A mod with an older overlay reads 2 as a ready tab and hides as before. The block's layout and its
version 3 are unchanged. The mod whose tab is chosen draws it. Next Gen Graphics offers its switches through <code>NgGraphicsGetApi</code> (<code>ng_graphics.h</code>): the first Next Gen mod to claim it in the block draws a Graphics tab from them, and Next Gen Graphics silences its F keys meanwhile. While it is open the game's imports
<code>GetCursorPos</code>, <code>SetCursorPos</code>, <code>GetAsyncKeyState</code> and <code>GetKeyState</code>, its window's mouse and key
messages, and DirectInput's <code>GetDeviceState</code> and <code>GetDeviceData</code> for mice and keyboards give the game nothing new;
releases still reach it.</p>
<p>The cluster is drawn into ImGui's background list, in the game's own font (TT Lakes Medium and DemiBold,
baked with stb_truetype). One unit is the screen height / 1080 × <code>Scale</code>; it fades in and out over 0.15 s, and hides under
320 × 240 or when toggled off.</p>
'''


def s_shared():
    gauges = [['0', 'u32', 'magic 0x47554147, "GAUG"'], ['4', 'u32', 'version 2'], ['8', 'u32', 'size 256'],
              ['12', 'i32', 'sequence, odd while being written'], ['16', 'u64', 'tick: GetTickCount64 at the write, ms'],
              ['24', 'u64', 'vehicle (control +8), 0: none'], ['32', 'f32', 'outside temperature in force, °C (always written)'],
              ['36', 'f32', 'coolant, °C'], ['40', 'f32', 'engine speed (the needle), rpm'], ['44', 'f32', 'boost, bar'],
              ['48', 'u32', 'flags: bit 0 vehicle, 1 temperature from a map section, 2 engine on'], ['52', 'u32 × 3', 'reserved'],
              ['64', 'f32', 'speed, km/h (below 0 backwards)'], ['68', 'f32', 'the speedometer\'s top, km/h'],
              ['72, 76', 'f32', 'the tachometer\'s red zone and its end, rpm'], ['80', 'i32', 'gear: below 0 R, 0 N, 1 and up forward'],
              ['84', 'u8', 'the letter: 0 a number, 1 A, 2 H, 3 L-, 4 L, 5 L+'], ['85', 'u8', 'the gearbox\'s numbered gears, 0 not known'],
              ['86', 'u8', 'the Gearbox level in force: 0 the game\'s, 1 the automatic, 2 the own gearbox'],
              ['87', 'u8', 'the tire lamp\'s mode: 0 none, 1 Low, 2 Reduced, 3 Normal, 4 Increased'],
              ['88, 92', 'f32', 'oil, °C, and where its red zone starts'], ['96', 'f32', 'brake air, bar'], ['100', 'f32', 'the turbo\'s most, bar'],
              ['104', 'f32', 'fuel, 0 to 1'], ['108, 112', 'f32', 'fuel and tank, litres'], ['116', 'f32', 'burn, litres a minute (below 0 not known)'],
              ['120', 'f32', 'range, km (below 0 not known)'], ['124', 'u32', 'lamps lit (bits 16 and 17: the tilt lamp)'],
              ['128', 'u32', 'lamps blinking, the same bits'], ['132', 'u32', 'options: mph, psi, °F, the cluster on screen, vacuum, no boost'],
              ['136', 'u32 × 30', 'reserved']]
    tire = [['16', 'u64', 'tick'], ['24', 'u64', 'vehicle'], ['32', 'i32', 'mode: 0 Low, 1 Reduced, 2 Normal, 3 Increased'],
            ['56', 'u32', 'flags: bit 0 vehicle, 1 changing, 2 held, 3 too fast, 4 worn']]
    smt = [['32', 'i32', 'gear (−1 R, 0 N, N+1 high)'], ['36', 'i32', 'highest gear'], ['40', 'i32', 'range (−1 low, 0 none, 1 high)'],
           ['44', 'u32', 'flags: vehicle, A range, clutch held, clutch required, gear held, immersive, game shifting off, clutch on an axis, focused'],
           ['48', 'f32', 'clutch 0 to 1'], ['52', 'f32', 'power coefficient'], ['56, 60', 'u32', 'stalls, shifts']]
    return f'''
<h2>Shared memory</h2>
<p>Named blocks, one per game process (Gauges' of 256 bytes since version 2, the others of 64), read without locks: the writer makes the sequence odd, writes, and makes it even
again; a reader copies the block and keeps the copy only if the sequence was even and unchanged (up to 8 tries). A tick older than
the limit means the writer is gone. Later versions only add fields at the end.</p>
<pre>writer:  seq += 1 (odd)   barrier   fields   barrier   seq += 1 (even)
reader:  s = seq; if s odd, retry;   copy;   barrier;   if seq != s, retry</pre>
{table(['Offset', 'Type', 'Field'], gauges, 'Written: Local\\SnowRunnerGauges.State.&lt;process id&gt; (state.h), every pass', 'small')}
<p>Every truck value is written only while you drive, otherwise 0; the block itself is written on every pass, in the menus too, so a
stale tick means only that Gauges is gone. The values are the cluster's as it shows them, metric whatever the units picked; the lamps
that blink (engine damage, oil, tire) are flagged in their own word, so a reader blinks them at its own rate. No value is ever NaN.
The tire mod reads the outside temperature from here. Other mods' authors get the layout and the read rule in
<code>docs\\SnowRunnerGauges_State.h</code> and its guide <code>SnowRunnerGauges_State.md</code> (CC0); a reader maps the whole block
(size 0) and checks each field against the size, so it reads version 1 and 2 alike.</p>
<p><b>Winch out</b> (<code>winch_out.h</code>; BRIEF_winch_out_probe.md, BRIEF_jobs_1114.md part 163). The game makes the rope's
constraint anew on every step while a winch is active, and it has a function of its own for a rope of a given length. While the
key is held the mod reads the rope's length by the game's function in the driven truck's update and asks for that length plus
the rate times the step (<code>WinchOutFrame</code>; the step is held to 0.05 s, so 60 and 120 steps give the same metres a
second). The mod keeps no length and no latch: with no key held it makes no call, so a released winch, a truck change and a
recovery leave nothing of the mod behind. No call while the game's pull or its second active byte is on, on a crane's winch,
with no rope of this truck, and where the ask would pass the winch's own length (vehicle + 0x8F0) or that length is not read.
The truck update's hook is asked for only once the action is bound (<code>WinchOutWanted</code>); there is no default binding.
The three places in the game's code must each be found exactly once, else the row is greyed and reads "Winch out: not in this
game version." After a call faulted the log has one line in capitals (<code>WINCH OUT: the game's</code>) and the row says so
until the game is started again. The first hold of a session gives one line (<code>winch out: the key lets the winch wire out
at</code>), the hook one (<code>winch out: the truck update is hooked (Winch out is bound)</code>; while it was a probe the line read
<code>winch out: the truck update is hooked for the test</code>). The action is WinchOut in the list of bindings, so its
key is BindWinchOut. The slider was Winch out rate on the Mod work tab while it was a probe and is Winch out speed on the
Controls tab now. The lines of each hold with their counts stay behind Probe=1. The step mode of the test builds, the row Winch out step, is dropped: WinchOutStep is
neither written nor read.</p>
<p><b>The quick winch.</b> <code>quick_winch.h</code>, <code>QuickWinchFind</code> in <code>game_code.h</code>. The game's quick winch
key ends in one method of 29 bytes that only raises a request byte on the player's controller; the player's truck update makes the
winch a frame later. The method is found in three steps: the hash of the script native's name once in the code, the thunk named just
before it, the call at thunk + 0x5D, and the method's shape, which holds the store of the request byte
(<code>quick winch: the game's request at +</code>). Any miss gives one line, no hook and a greyed row. The hook is attached alone in its
own transaction the first time <code>QuickWinch</code> is not 0 (<code>quick winch: the game's request is hooked</code>, or
<code>quick winch: the game's request could not be hooked</code>, and then the row is greyed too). A refusal is a
return without the call, so nothing is raised and nothing is pending. The mod's thread posts the driven truck, its speed and the
setting once a pass; the hook reads only those. It refuses under Off, and under Standing only at 0.5 m/s and above, for the driven
truck alone and only while the last pass is no older than 250 ms. Each refusal is one line, at most one a second
(<code>quick winch: refused</code>). The settings line is <code>settings: quick winch</code>.</p>
<p><b>A lock limits the steering.</b> <code>lock_steer.h</code>. The game steers a wheel to its most angle times the stick, and
keeps that most angle as a float at wheel + 0xB8. One pattern of the game's code proves the place, found once
(<code>steering: the game's steering product at +</code>); where it is not found nothing is ever written. While an axle is locked
and a row is under 100 the mod keeps each steered wheel's stock figure and writes stock times one share, the same for every wheel
(<code>steering: a lock limits the steering of vehicle</code>). The share moves at 0.8 a second and a pass counts as 0.1 s at the most.
The stick is not read: at the middle the product is 0 at any share. Before every write the wheel must hold its stock figure or the
mod's own last one, to the bit. Another figure there ends the writing for that truck (<code>steering: wheel</code>). The stock figure
goes back when the truck is left, when Real engine goes off and when both rows are at 100
(<code>steering: the stock most angle put back into</code>). A wheel that cannot be put back, because the game has moved or rebuilt it,
is let go with the mod's figure in it. Its stock figure and that figure are kept as a pair for the session, 64 pairs at the most, with
the truck's kind (not its id, which the game renews at a recovery). A figure about to be taken as a stock figure is looked up among them first. Met again, the stock is the pair's and the
share in force goes on from figure over stock, so a rebuilt truck is never lowered twice (<code>steering: %ld wheels of vehicle</code>). The settings line is
<code>settings: the steering left with a lock in:</code>, and with <code>Probe=1</code> a line a second
(<code>probe: steering: the target</code>) gives the target, the share in force and the first wheel's figures.</p>
<p><b>The tuning block.</b> A second block, <code>Local&#92;NextGen.Tune.&lt;process id&gt;.&lt;n&gt;</code> (<code>ng_tune.h</code>,
the same file in every Next Gen mod; <code>tune_figures.h</code>), holds figures of the Powertrain for a tuning tool that is
not part of a release: <code>DiffLockBite</code>, <code>WheelCap</code>, <code>PushShare</code>, <code>LowRangeSlip</code>,
<code>HeldGearSlip</code>, <code>LockSteerFront</code> and <code>LockSteerRear</code>, each
with its name, tip, range, default and the value the tab shows. The tool writes a wanted value with a count. The mod's thread
takes it at its next pass (the count, the value, the count again), held to the range and to whole percent, and hands it in by
<code>SettingsPut</code> with the turn it read the settings at. That is the road an edit on the tab takes: in force on that pass,
saved a second after the last change. <code>HeldGearSlip</code> is a figure of the tool alone: test builds had it as the row Held
gear slip on the Power tab, and the row is gone. Its default is 2, All gears: a numbered gear held by hand has the wheel cap in
every gear. An ini that holds 0 or 1 keeps it, whether a test build or the tool wrote it: both are a player's choice, so no
warning comes (part 195.3); the settings line says the figure in force. <code>EngineSoundTop</code> (40 to 100) and <code>EngineSoundCurve</code> (100 to 300) are
two more figures of the tool alone, for the engine sound. The mod's thread takes the engine speed share through the pedal's curve
with the exponent <code>EngineSoundCurve</code> / 100, then times <code>EngineSoundTop</code> / 100, and hands the result to the
sound's hook beside the share. The share the game's fuel and stall read is never changed by them. At 100 and 100 the sound is as
it was. The probe's line <code>probe: engine sound: the engine speed share</code> names the share, the shaped and topped figure,
the two keys, the factor written and the game's own. It takes the place of <code>probe: engine sound: the game's
velocityFactor</code>. While either key is not 100 the settings have a line <code>settings: the engine sound's top</code>. With Real engine off the figures are published greyed. Only the mod's thread touches the
block, and no hook does. Without the tool nothing reads it. The log's start says "tuning:" with the block's name, or that it
could not be made.</p>
{table(['Offset', 'Type', 'Field'], tire, 'Read: Local\\SnowRunnerTirePressure.State.&lt;process id&gt;, the tire mod\'s (tires.h); the fields used', 'small')}
<p>Looked for every 5 s, stale after 2 s. The tire lamp shows only while the block's vehicle is the driven one: the letter L, R, N
or +; red while the tires are too fast or one is worn out; otherwise blinking (400 ms) while the pressure changes or waits for the
truck to stop, lit amber at Low and Reduced, green at Increased, dark at Normal.</p>
{table(['Offset', 'Type', 'Field'], smt, 'Read: a manual transmission mod\'s block, Local\\SnowRunnerMT.State.&lt;process id&gt; (smt.h)', 'small')}
<p>Stale after 1 s. While that mod drives the truck in a manual gear, the mod's automatic stands down and a clutch model runs
(engine speed from the clutch: free past 0.85 of the travel, full grip under 0.35, stalls under half idle); it is logged, not
yet shown.</p>
'''


def s_files():
    return '''
<h2>Files and what wins</h2>
<ul><li><code>Gauges.ini</code>, section <code>[Gauges]</code>: every setting. A missing ini is written with help text and the defaults;
values that are not finite fall back to the defaults; tab edits are saved a second after the last one.</li>
<li><code>Gauges\\trucks\\&lt;kind&gt;.ini</code>, <code>[Truck]</code> for the kind and <code>[Truck:&lt;id&gt;]</code> for one
truck: the seven scales (<code>Class IdleRpm RedlineRpm MaxRpm SpeedMax BoostMax BoostProfile</code>; <code>Class</code> only where a
class is picked); a key present wins over <code>[Gauges]</code>. Written when one is changed on the tab while that truck
is driven. The kind is the game's (model +0x588).</li>
<li><code>Gauges\\gearboxes\\&lt;name&gt;.ini</code>, <code>[Gearbox]</code>: <code>GearHold</code>, 1 to 3 s, and <code>Creep</code>
with <code>CreepManual</code> (0 or 1, read only beside Creep). The engine's file holds <code>EngineBrake</code> (0 to 30). These
are the default's level of the same chain: <code>[Engine:&lt;id&gt;]</code>, the kind's <code>[Engine]</code>, these files, then
<code>[Gauges]</code>. The pass reads the figure in force in one place each (EngineBrakeOf, CreepOf, CreepManualOf). The drive's
hook goes in by the Creep of <code>[Gauges]</code> and by Real engine, as before, and a truck change never touches it. The log
lines: "engine braking by truck:" and "creep by truck:", and "engine: every" where a row's first move makes the kind's set. The name is the gearbox
fitted (vehicle +0x9C0), the truck's default (+0x9A0) only when none is read there.</li>
<li><code>Gauges\\engines\\&lt;name&gt;.ini</code>, <code>[Engine]</code>: <code>PeakTorqueRpm</code> (100 to 20000), <code>Aspiration</code>,
<code>Fuel</code> (diesel or petrol), <code>TorqueCurve</code>: the default. The truck's file holds the same four as
<code>PeakTorqueRpm.&lt;engine&gt;</code> and so on, in <code>[Engine]</code> for the kind and <code>[Engine:&lt;id&gt;]</code> for one
truck. The most specific section in force that holds any of them gives all four (EngineFromFiles), a missing key the stock one.</li>
<li><code>[Map:...]</code> and <code>[Clock]</code> in <code>Gauges.ini</code>. Old <code>[Truck:...]</code>, <code>[Gearbox:...]</code> and
<code>[Engine:...]</code> sections are moved into the files at load.</li></ul>
<p>Scales: truck file over <code>[Gauges]</code>. The automatic's class: the truck file's if it sets a scale, else by mass. Gear hold: the
gearbox file over the class (scout 1.5 s, others 2.5). Torque curve: <code>TorqueCurve</code> over <code>PeakTorqueRpm</code> over the class.
The tab's sliders cover narrower ranges than the ini for some keys (Scale 0.5 to 2.5, warm-up 1 to 15 min, overheat 0.5 to 10).</p>
<p>The tab draws its settings in eight tabs (<code>PageTabs</code> in <code>settings_page.h</code>), each in a child window with a scroll of its own, as the host draws the page in a scrolling child the bar would scroll away with. The open one is kept as its fixed id in <code>[Gauges] Tab</code>, apart from the settings: a tab picked is no setting edit, Defaults leaves it, and the write goes to the mod's thread, where the settings are saved. An id no tab has opens Display.</p>
'''


def s_rate():
    return '''
<h2>The physics rate test mode</h2>
<p><b>The automatic on the mod drivetrain, part A</b> (BRIEF_drivetrain_3_a_on_our_chain.md; <code>CutInA</code>, the Mod work tab's
"Mod engine in A", off by default). The torque cut answers in A too, at Gearbox=1 and 2, wherever the mod's automatic has the truck
in A (TorqueCutInA, TorqueCutActs); a held gear still needs Gearbox=2, and the game's gear agrees with the position by its sign
alone. Locked, the slot is the map of a held gear, with its engine braking and the governor by the truck's own speed. A shift made
locked reads locked on its own pass too (ConverterLockedForCut), so no scalar goes out between two maps. Open, the slot is the scalar
from one function, ConverterOpenF: the map's law at the open converter's engine speed times its torque ratio, bounded at the stall's
ratio of 2, with no braking. Since part B that function runs in the game's own torque call, on that step's wheels
(ConverterAt, ConverterOpenAt): the mod's thread hands the stall's speed at this pedal and its rise, and the call works out the
turbine's speed, the engine's and the torque ratio, so a wheel that spins up lowers the ratio in the same step. A step whose
wheels were not read goes by the thread's own figure. The needle goes by the engine speed the call worked out
(ConverterNeedleRpm), through the tachometer's own smoothing, so the needle, the sound and the pull are one number. The lock
state stays the thread's. At a lock above the start gear the pull would fall about a quarter in one step, so the detour runs it
down to the map's in a straight line over 0.3 s by its own clock (CutLockFall), the same at 60 and at 120 steps a second; a lifted
pedal is never held up. In A there is no wheel cap, no slip room and no over-rev damage (OverRevCounts). While the cut acts in A the
automatic's net under a wrong top and its taper rule rest, as both read the game's pull, which the cut holds at 1. No gear top is
learned under the cut; the log says once a truck what it goes by: "gear tops: with the mod engine in A (CutInA=1) no gear top is learned on this truck". The air drag's base goes on
learning under the cut: a sample is the acceleration over the mean of the brackets the detour used since the thread's last pass
(CutLearnDivisor), only from steps of the map that were settled, not at the floor and not in the lock's fall (CutLearnStepOk), into
the same rows as without the cut. The learned line ends with which of the two fed it. The differential's types and the held lock
byte act wherever the cut acts, so with <code>CutInA=1</code> in A as well. The model's own "to the wheels" figure is gone: the
automatic's probe line shows the F the slot carries, 0 with no slot. Log lines: "torque cut: acting in A (the game's gear", paced to
one a second at a change of locked and open, and "probe: torque cut in A: the slot is" beside the cut's probe line; at a start with the key on, "settings: CutInA=1, a test: in A the torque cut answers too".</p>
<p><b>What it is</b> (BRIEF_physics_rate_stage1.md, <code>physics_rate.h</code>). <code>PhysicsRate=120</code> steps the game's physics 120 times a second in place of 60. A test: off by default, single player only, no co-op check. <code>PhysicsRate</code> (60 or 120) and <code>PhysicsIterations</code> (0, 2 or 4; ini only; 0 is the mod's choice, 2) are read once at the start, before the overlay. Any other value reads as the default with one line: "physics rate: PhysicsRate= ..." or "physics rate: PhysicsIterations= ...". The tab's row (Mod work, Test: Physics rate) saves the key and says that it runs after a restart and which rate runs now. At 60 the mod reads the key and does nothing else: no page, no hook for the rate, no write.</p>
<p><b>The places</b>, all or nothing. Six operands in the game's code. Each is found by its pattern exactly once and checked to read the game's own float: the step (1/60 s), the drive's wheel impulse (1/60), its chassis push (1/120), the overspeed brake (-1/60) and the two shares of the readout at wheel + 0x16C (0.6 old, 0.4 new). Each disp32 is pointed at a float of the mod's on one page of its own within 2 GB of the code. The page is filled once and then read only: 1/120, 1/120, 1/240, -1/120, the root of 0.6 (0.7746) and its rest (0.2254), so two steps blend as one of the game's. The game's own floats are never written: the 1/60 alone has 20 other readers. The solver's iteration count is an immediate in the code (4), and with <code>PhysicsIterations</code> 0 or 2 it becomes 2: as many solver passes a second as the game's. One more pattern finds the static of the world's owner; the world is its + 0x20, the world's step its + 0x2A8 and its iterations its + 0x3DC. A place not found, found twice or holding something else means no write at all, and "physics rate: 120 asked (PhysicsRate=120) and off: ..." names it.</p>
<p><b>The order</b> (<code>RateStart</code>, first after the log's head). All is found and checked on a copy of the exe. Then the wheels' drive is hooked whatever the settings are ("creep: the game's wheels' drive at +..." says it was hooked for the rate). Then the writes go in, four bytes each, the step last. Each has its protection changed, the instruction cache flushed and is read back. A write that does not read back is named in the log and the earlier ones are put back. "physics rate: 120 steps a second ..." names each place. After it: "physics rate: no step had come before the last write went in ..." or "physics rate: the writes were late: the drive had run ...", then "physics rate: no world was there yet at the start ..." or "physics rate: a world was there already at the start ..." (then the iterations written reach only a world made later). At 4 iterations: "physics rate: the suspension is not corrected at 4 iterations ...". Then the second group and the brakes' hook (part B2 below). Always: "physics rate: left as they are on purpose at 120: ...".</p>
<p><b>The drive.</b> Around each call of the drive the hook halves MaxDeltaAngVel (action + 0x4C) and EngineResponsiveness (action + 0xC4), and after the call puts them back if the half still stands there. It keeps no memory between calls. The torque the mod hands the drive stays as it is: the operands do the correction. The mod's own blend of the wheel cap's kept share (<code>kCapKeptOld</code> 0.8) is taken to the power dt x 60 with the call's own dt: 0.894 at 1/120 s and 0.8 to the bit at 1/60 s.</p>
<p><b>The world.</b> At the first drive call in each new world the hook reads its step and iterations, and the mod's thread writes "physics rate: world N (...) at its first drive call: ...". The line gives the step as the world holds it with its bits. The step is good within 20 percent of 1/120 s (<code>RateStepGood</code>, <code>kRateStepNear</code>): the world's figure comes out a hair off 1/120 (3c089000 was seen against 3c088889), while a world still at 60 is twice the step. The iterations are checked exactly. Where the step is outside that or the iterations are not the ones asked, a line in capitals follows ("PHYSICS RATE: ...") and the tab's row says the same. A read that faulted in the hook is counted: "physics rate: a read in the drive's hook faulted (...)".</p>
<p><b>Stage 2, part A: the substance action</b> (BRIEF_physics_rate_stage2.md). In water and in mud the game's substance action builds two vectors a step, the lift with the drag and a second drag, multiplies each by four 60s it loads once, and hands each to the body's motion as an impulse at a point. Nothing in the two vectors holds the step's seconds, so the amount is one a step and at 120 it would be given twice a second over. The load's operand is pointed at four 30s on the mod's page, 16 bytes aligned for the movaps, in the same plan as stage 1's places: all or nothing. The action's own 1/60 (a time ahead) and its scalar 60 (a move over that time into a speed) are a pair and are left: they give the right speed at any step. Its 1/600 is a size over a count under a root, not a time. With Probe on at 120 the log has once a second "probe: physics rate: the substance action reads 30.00 30.00 30.00 30.00 for its lift and drag a step, ...", read through the game's own operand, or "probe: physics rate: the substance action's four floats could not be read back". At the start the log has "physics rate: corrected at 120: the step, the drive's wheel impulse and chassis push, ...", which also names what is left as it is on purpose, before the line of what is not corrected yet. The brakes, the handbrake, the winch, the blends and the height move are part B and wait for their traces.</p>
<p><b>Stage 2, part B1: the wheels in the substance</b> (addendum 2, M1). The game's four 60s have three readers and no more: part A's load, which serves the chassis, and two multiplies in the action's part for a wheel. Each of the two is followed by the body's activate and the motion's impulse call with one vector, an impulse a step as the chassis's. With the load alone corrected the chassis got the right lift and drag at 120 and each wheel twice it. Now the two operands point at the same four 30s on the mod's page. The plan (<code>kRateQuadSites</code>, <code>RatePlanMake</code>) takes all three or none: each found once, each at the very four 60s the load reads ("it does not read the four 60s the substance action's load reads" otherwise), ten writes at 2 iterations with the step last. With Probe at 120 a second line a second reads the two back: "probe: physics rate: the substance action's two impulses on a wheel read 30.00 30.00 30.00 30.00 and 30.00 30.00 30.00 30.00 a step, ...", or "probe: physics rate: the substance action's two wheel operands could not be read back". The start's line reads "physics rate: corrected at 120: the step, the drive's wheel impulse and chassis push, the overspeed brake, the readout's shares, MaxDeltaAngVel and EngineResponsiveness, and since stage 2 the substance action's lift and drag in water and in mud, on the chassis and on each wheel ...".</p>
<p><b>Stage 2, part B2: the brakes, two blends and the fixed force</b> (addendum 2, M2 to M4). Three things, each by itself.</p>
<p><b>The brakes.</b> The game's brakes take a share of each wheel's spin a step: the wheel body's angular velocity times (1 - brake), with no step's seconds in it. Two steps of that leave the square, so at 120 the factor is the root of (1 - brake). The share is the input and no constant, so there is no operand to point elsewhere. The brakes' function is found from the drive's call (<code>BrakePlaceFind</code>: the next call in the same caller, so it runs once for each drive call) and hooked as the drive is (<code>BrakeHook</code>), only when 120 goes in force. Each call reads every wheel body's angular velocity before the game's function and after it. Only where the velocity after is the velocity before times (1 - brake) on all three parts, to the bit, is it written as before times the root (<code>BrakeRootOf</code>). The lock's zero vector, a wheel the function did not touch, a brake at or under the game's 0.0001 and anything else are left. The brake is the one the function used: the first of the action's delayed inputs, read after the call. The input is not changed: it also sets the lock's threshold. The write is the cap's: three floats of the body, no call into the game, a body that is in no world or fixed refused. No lock, no heap and no log on the physics threads. The start says "physics rate: the brakes hooked at +... (called at +..., right after the drive's call) ..." or, in capitals, "physics rate: THE BRAKES ARE NOT CORRECTED at 120: ...". With Probe a line a second: "probe: physics rate: the brakes' hook in the last second: 240 calls, 12 wheels written by the root ..." or "probe: physics rate: the brakes are not hooked".</p>
<p><b>The second group</b> (<code>kRate2</code>, <code>Rate2PlanMake</code>): six operands, all or nothing in itself, written after the first group. The blend at w + 0x118: the old's weight 0.3 becomes its root 0.5477 and the new's 0.7 becomes 0.4523. The blend at w + 0x110: 0.8 becomes 0.8944 and 0.2 becomes 0.1056. Each pair sums to 1 to the bit and two steps of the new blend give one of the game's. The fixed 1/60 force in its two places: a body that lies under a surface has its velocities set to zero that step and is pushed up by a force for a fixed 1/60 s; at 120 the time is 1/120 s. The game's own constants are not written: each operand points at a float on the mod's page. The start says "physics rate: the second group is in: the blend at w + 0x118 at +..." or, in capitals, "physics rate: THE SECOND GROUP IS NOT IN: ...", and then 120 runs with the first group alone. With Probe: "probe: physics rate: the second group reads w + 0x118 old 0.5477 new 0.4523, ...", "probe: physics rate: the second group's floats could not be read back" or "probe: physics rate: the second group is not in".</p>
<p><b>The winch's pull</b> (<code>kRate3</code>, <code>Rate3PlanMake</code>): a third group, all or nothing in itself. The pull is a spring whose strength the pull action's step governs by how far the rope closed since the step before, in metres a step: over 0.02 the strength is cut to 0, and it builds towards its target by a weight of 0.01 a step that eases off from 0.005 up. At 120 the same metres a step are twice the speed, so the winch hauled at up to 2.4 m/s for 1.2 with the same force. Four operands read three of the game's floats; each is pointed at a float on the mod's page: the cut 0.01, the easing 0.0025 (two reads) and the weight 1 less the root of 0.99, 0.0050126. The thresholds are right to the bit; the weight is right with the truck standing and within a third of a percent at any speed under the cut (<code>WinchPullStep</code> is the game's arithmetic for the tests). The four writes go in as a group of their own before the first group, whose last write is the step (<code>RateStartWrites</code>), so no step of 1/120 s runs with the winch's figures of 60; where the first group then fails they are put back (<code>RateBack</code>). The start says "physics rate: the winch's pull is in: its cut at +..." or, in capitals, "physics rate: THE WINCH'S PULL IS NOT CORRECTED at 120: ...", and then 120 runs without it. The tab's row adds "Winch pull: right at 120." or "Winch pull: too fast at 120 (not corrected, see the log).", and where a write failed and the game's code did not go back (<code>kRateWinchBroken</code>) "Winch pull: part changed. Start the game again." With Probe: "probe: physics rate: the winch's pull reads its cut ..." a second ("probe: physics rate: the winch's floats could not be read back" or "probe: physics rate: the winch's pull is not in" otherwise). The reel out probe reads the rope's length under the game's pull without setting it, and its line then ends "under the game's pull the rope ... m, closing at ... m/s since the last line" (<code>WinchClosing</code>): about 1.2 at either rate when the pull is right.</p>
<p><b>Left on purpose</b>, named at each start in "physics rate: left as they are on purpose at 120: ...": the blend at w + 0x128 (its old weight comes in a register), the mean of two steps at w + 0x170, the adjustable suspension's height move, w + 0x190 and w + 0x18C, action + 0xC8, the handbrake, the winch's rope and its reel. With the second group or the brakes' hook missing the tab's row reads "Running at 120 with a part of the corrections missing. Gauges.log says which."</p>
<p><b>The step as the drive gets it</b> (BRIEF_step_check_always.md, <code>StepJudge</code>). At both rates the drive's hook holds each call's step seconds against the rate in force: within 20 percent it is right. At 120 a step near 1/60 is the mod's own miss (a world made before the write), anything else is another mod's. A verdict counts after 8 calls in a row and not before the first second of drive calls in a world; a step that is no number, 0 or under is counted and not judged. While the step is right this is one compare of a float a call. The drive's thread sets a flag and nothing more. The mod's thread writes "physics step: 0.016667 s as the drive gets it, the rate in force 60", and for a foreign step a line in capitals and the sign in the middle of the screen (<code>GStepSign</code>, on the foreground list, until the game ends). A step's float that is not the game's own at the write for 120 turns the sign on too. Where the drive's hook is not in (Creep, Probe, Powertrain, the suspension features and the drive display all off) or the drive is not found, the log says once "physics step: not checked: ...".</p>
<p><b>Half as long at 120.</b> Counts of calls and steps stay counts. The step threads' window (<code>kStepThreadsCalls</code>, 600 calls) is 5 s in place of 10. The step trace's ring (2048) holds 17 s in place of 34, the history (4096) half its time, a bounce (512 steps at the most) 4.3 s in place of 8.5 and its rest (20 steps) 0.17 s. "falls of 0.3 or more in one step" in the torque cut's probe line means something else: the same fall lies over two steps. The probe lines of the torque cut, the differential and the chassis push say the rate at their end. The shifter's ask lives two passes of the mod's thread, not steps, and does not change. The drive's hook and the brakes' run twice a frame at 60 fps and up to eight times at 30.</p>
<p><b>Not corrected in this build:</b> the brakes, the water's lift and drag, the substance force, the blends at wheel + 0x110, 0x118, 0x128 and 0x170, the adjustable suspension's height move, wheel + 0x190 and 0x18C, action + 0xC8. The suspension at 4 iterations is not corrected: springs, dampers, the handbrake and the winch are likely four times stronger.</p>
<p>The idea of a higher physics rate, and the first map of what has to be scaled, come from True 240Hz Physics Patch by alounana on mod.io.</p>
'''

def s_sounds():
    return '''
<h2>Sounds</h2>
<p>Five kinds: the turbo on a lift (high, middle, low), a diesel turbo's whistle on a lift (the player's own files only), the oil warning's beep, the air dryer's purge at each cut-out of the compressor
(with the engine running, at 0.8 of the oil warning's gain), and the tab's buttons to hear the first two. For each the mod takes the
player's own <code>sounds\\turbo_high_1..4.wav</code> (and <code>_mid_</code>, <code>_low_</code>, <code>oil_beep_</code>, <code>air_purge_</code>, <code>turbo_whistle_</code>)
next to the exe, else its built-in recordings (used with their author's permission; none for the beep or the purge), else sounds it makes; the whistle has neither, so without own files a diesel's lift is quiet:</p>
<ul><li>Blow-off: 0.7 s of noise through band-passes at 4.2 kHz (Q 0.7) and 1.9 kHz (Q 0.9), 6 ms attack, decaying with 0.13 s after 70 ms,
high-passed at 700 Hz.</li>
<li>Flutter: 0.6 s of puffs slowing from 26 to 13 a second, decaying with 0.22 s, band-passed at 1.3 kHz plus 0.35 × 3.4 kHz,
high-passed at 400 Hz.</li>
<li>Beep: two 0.13 s beeps at 2.2 kHz with a quarter of the third harmonic, 0.09 s apart, as a dashboard's buzzer gives them.</li>
<li>Purge: 0.6 s of noise through a one-pole low pass at 4 kHz and a one-pole high pass at 600 Hz, a 3 ms attack, full for 0.1 s, then
decaying with 0.15 s.</li></ul>
<p>Every clip is brought to the same loudness (the loudest 0.1 s at RMS 0.1, peaks at most 0.6) and played at min(gain, 1) × 0.45 so two
ringing together stay under full scale, never the same variant twice in a row, through XAudio2 2.9. No engine sound
plays.</p>
'''


def s_limits():
    return '''
<h2>Limits and known issues</h2>
<h3>By design</h3>
<ul><li>The game has no engine speed, temperatures, boost, brake air or range: all of them are models, tuned by drives logged in the game where
the sections say so and otherwise the mod's own reasoning.</li>
<li>The mod knows a truck by its kind: two trucks of one kind share a temperature, and 16 kinds are remembered.
After a loading screen the truck that drives on under the game's id for it keeps its heat; with another truck, none before the load or
none for a minute after it, every engine is cold again (see Changed in 0.17.0).</li>
<li>The class by mass cannot tell every scout from a truck (the Fleetstar at 6314 kg is a truck; a modded scout of 9007 kg shifts as a
truck until a truck file names it a scout). Mass is without addons, cargo or trailer.</li>
<li>With <code>Gearbox=1</code> alone the pull is the game's; a gear's top speed stays the game's, and past it the game gives nothing.</li>
<li><code>Damage</code> has not been tried in co-op; leave it off there.</li>
<li>On a build other than Steam's the clock is unknown until found with <code>Probe=1</code>, and the objects' offsets are taken as
they are. A wrong clock place that still reads 0 to 24.5 is not noticed.</li>
<li>A change of the game's engine damage that falls between the mod's read and write is lost.</li>
<li>Probe logs run to 20 or 30 MB; the logs folder keeps the newest <code>KeepLogs</code> (20) and at most 500 MB, the newest always (<code>LogsCap</code>). The pad's toggle buttons reach the game too.</li></ul>
<h3>Fixed after this manual's first version (0.15.8)</h3>
<ul><li>Under the mod's automatic the needle followed the converter's engine speed without a cap and could run to the dial's end with the wheels spinning; now it goes no further past the red zone than wheelspin takes it elsewhere (1.1 of the way from idle to red).</li>
<li>The brake air dial's red zone is drawn from the model's own figure (5.5 bar, as before).</li>
<li>The README now says where another build stands down, how far a diesel comes down at a light foot, that the damage lamp also blinks after a write, and that the air dial is hidden on scout scales; the source comments say the engine and the automatic are handed to the game.</li></ul>
<h3>Fixed in 0.15.9, after a tester's keyboard drive</h3>
<ul><li>Driven with keys, a lift shifted up at once and the next tap shifted back down: a key tapped to hold the speed swapped two
close gears every second or two (19 of 23 lift upshifts taken straight back). Keys now wait 2 s and block lifts after one taken back,
as above; wheel pedals and pads are unchanged.</li></ul>
<h3>Changed in 0.15.10</h3>
<ul><li>Floored, the automatic shifts up where the next gear pulls more, or on the limiter: the cap at point(0.95) is gone. With
the roof of 6 it sat just over where the next gear pulls more anyway (a ZIL-131: 3024 to 3056 rpm against 3072).</li>
<li>An engine's own FullPull (the tab's Full pull up to): how far toward a gear's top it keeps its full pull, the roof following,
so a petrol engine that makes its power up to its red zone can rev there (the ZIL-131 at 97: about 3150 rpm). Gone again in 0.17.0.</li></ul>
<h3>Changed in 0.16.0</h3>
<ul><li>The mod is <code>Gauges.asi</code>, loaded by the ASI loader, and draws with Next Gen's overlay (Drawing, above); the release
brings ReShade without its start banner, which hands the overlay the game's swap chains. The Gameplay tab is in the NEXT GEN
menu, on F2, shared with Next Gen Tire Inflation. The mod is called Next Gen Gameplay; its files keep the name Gauges.</li></ul>
<h3>Changed in 0.15.11</h3>
<ul><li>A look for the game's places that misses a needed one is made again every 2 s for a minute before the mod stands
down. Loaded early through another mod's DirectX probe (a minimap mod), it looked before the game's code was ready and stood down.</li></ul>
<h3>Changed in 0.16.1</h3>
<ul><li>RevCap (the tab's Rev cap 95%), off by default: with the mod's engine the pull ends at 95 percent of the way
from idle to the red zone and the automatic shifts up there floored. For a truck that shoots from 40 to
80 km/h on its own, a game bug that RLM-like engines and this mod make worse. Gone again in 0.17.0.</li>
<li>An engine's own Torque (the tab's Torque slider under Engine): how much of its torque the truck gets with the engine handed to
the game, 10 to 100 percent (100 when not set). Lower it for an RLM-like engine with several times a stock
engine's torque: at 25 an engine with four times the torque pulls like the stock one. The set-off
gear follows it. The shift points stay: a constant share does not move where the next gear pulls more. The tab's torque plot is
labelled Curve now.</li>
<li>On another map every truck's engine is meant to start as cold as the air outside: the coolant and oil the mod remembers for each kind of
truck are dropped when the game's save names another map. A reload of the same map keeps them. Known: this does not take effect yet.
A drive on 25 October kept the engine warm over a map change. The code stays in and a fix follows.</li></ul>
<h3>Changed in 0.17.0</h3>
<ul><li>A truck's gearbox file and gear hold go by the gearbox fitted (vehicle +0x9C0), not the truck's default gearbox (+0x9A0,
which the mod read before): a truck with an upgraded gearbox was read as its stock one (a d110 on its highway gearbox, 26 October).
A gearbox file named after the stock gearbox still applies while that one is fitted. The vehicle holds one block of 0x58 bytes a
socket (the engine at +0x930, the gearbox at +0x988, the winch at +0x9E0): the list of the socket's types, the default part's name at
+0x18 and the fitted one's at +0x38.</li>
<li>The overlay copies a swap chain's method table that another program has put at the start of its memory. Before, it drew
nothing there (no cluster, no panel and no NEXT GEN menu on two players' PCs). When a table still cannot be copied,
<code>Gauges.log</code> says in whose code the table and its Present lie, so the program can be named.</li>
<li>After a loading screen the truck that drives on under the id the game gave it keeps its engine's heat, and every other engine is
as cold as the air outside: a travel between regions, a border crossing or a reload is no cold start, a game start, the garage, a
recovery or a change of truck is. The cold start of 0.16.1 went by the save, which names the new map seconds before a travel ends and
minutes after a border crossing, so a travelled truck arrived cold and took cold-engine damage at once (a tester's UAZ, 26 October);
the known issue is closed.</li>
<li>The torque factor's roof is 6 for every engine again. An engine's FullPull and the tab's Full pull up to are gone: at 98 percent
the roof was 23.8, and a d110 got a kick at the top of 2nd to 5th gear (25 October). RevCap and the tab's Rev cap 95% are gone too:
the cap went by the engine's modelled speed, the kick by the wheels' share of the gear, and it never stopped one. For an engine with
several times a stock engine's torque, lower its Torque.</li>
<li>The mod finds its places on the Microsoft Store and Game Pass build the way Next Gen Tire Inflation does. Where its signatures
miss, control is found by the truck update's three calls (way 2) or the same with the stack frame left open (way 3), the game's
state beside control and the wheels' class by its type information (way 2 each). The log names the way. An optional place that is
not found is left out with a line in the log that says what goes without it.</li>
<li>SwapDials and the tab's Swap dials: the speedometer and the tachometer change places, in both layouts (asked for in the comments).</li>
<li>HotDamageFrom and the tab's Damage from: where the hot engine damage starts, 100 to 124 °C, 118 as before; the rate is still reached
at 125 °C (equation 29; from the mod.io list).</li>
<li>The tab's Truck class keeps the scale numbers set by hand when the class changes; only the ones still at the old class's own
follow the new class. The peak torque slider's tip says it ends at the red zone and the curve's peak sits at most at 0.95 of it.</li>
<li>A clock on the cluster, ShowClock and the tab's Clock: an analog dial with the game's time of day, the outside temperature in its
face, after the air dial in both layouts (from the mod.io list).</li>
<li>The pad toggle (PadToggle) rests while the NEXT GEN menu is open, as the toggle key does; a toggle held as the menu opens or closes
is no press.</li>
<li>The air dryer's purge: a short hiss each time the governor stops the compressor at the cut-out, made by the mod's code, or the
player's own sounds\\air_purge_1.wav to _4.wav next to the mod; it follows SoundVolume.</li>
<li>The fuel used a minute, after the range under the fuel dial, ShowFuelUse and the tab's Fuel use: the game's own burn smoothed over a
second, litres a minute, gallons an hour with Mph (asked for in the comments).</li>
<li>The game's gearbox functions are hooked through Microsoft Detours, calling through, as the ground for the mod's own gearbox
(below); a probe line counts their calls. The manual transmission mod (SMT) is no longer needed and not compatible: the mod says
so in the log and the tab when it finds it installed. The hooks go by the level: at Gearbox=0 none (the mod changes nothing in the game's
code and runs as 0.16.x did); at 1 and 2 the eight gearbox functions; at 2 with Damage on also the game's truck update. Each is attached the
first time a level needs it, also when the tab turns it on mid-game, through the same transaction and thread suspension, and is never
detached while the game runs. On them the mod's own gearbox, in three levels of Gearbox: 0 the game's own;
1 the mod's automatic in A, the game's shifter the player's (a position picked there hands the truck back by the door it came
through, AutoOffAndShift, ShiftToHigh, ShiftToNeutral or ShiftToReverse, whatever gear it names, so L+ is told from the automatic's own
first gear and the automatic never shifts out of it; A takes it again) and only the
game's reverse while the truck stands in A with the brake down and the game's automatic flag up refused (the flag is up only in the
moment after the shift modifier is tapped or A picked again, before the mod puts it down; the hook reads it at the call), at
ShiftToReverse or a gear below 0 through AutoOffAndShift or ShiftGear; with the flag down the shifter's R passes; 2 the mod's own gearbox, which takes the shifter: A, the numbered gears, N, R, H and the low range L-, L, L+ (0.2, 0.45, 1)
as positions of its own, with a clutch (a button, or a pedal axis that the engine model runs behind; in these positions the needle
and the torque curve handed to the game follow the clutch model's engine speed, TachFollow, as A follows the converter's; with the clutch locked, a wheel cap in the
slot and the wheels past the gear's top the needle goes by <code>SlipRoomRpm</code> instead: from the red zone's start to the stop
(<code>TachStop</code>) by the driven wheels' place between the gear's top and the cap's ceiling of the hook's last step
(<code>WheelCapCeilingGet</code>; only for the driven vehicle and only where the hook stepped since the last pass,
<code>SlipRoomCeiling</code>, else as before); the clutch model's own engine speed is not changed, and the probe's "engine speed" line
names the rule; its pedal
is a torque between the idle governor and the high-speed one, the curve's fall past the red zone, as a road truck's min-max governor;
a stall of it with the clutch on an axis goes to the game's stall seconds as the gearbox's own does, a clutch on a button never
stalls), a stall for a shift without it standing or crawling, under 1.5 m/s (RequireClutch: 5 written into the truck's stall
seconds; at 1.5 m/s or more the gear grinds and stays out, N, with a log line; review 11), a held-gear mode for H shifters with every gear bound (RequireGearHeld) and
a choice to skip neutral (SkipNeutral, from L- to R), and down stopping at L- (DownStaysForward, SMT's immersive mode; it wins over
SkipNeutral). Up and down walk an index: R -1, N 0, L- 1, L 2, L+ 3, gear g at g + 3, A at the game's gear + 3, H at the top + 4. <code>GearboxWalkNext</code> moves a press past the places <code>GearboxWalkStops</code> passes: N with SkipNeutral, and L-, L and L+ with WalkSkipsLow unless the press starts inside the low range (BRIEF_sequential_no_low.md), so the walk leaves the low range but never enters it. DownStaysForward refuses a down press whose target is N or R, and a down press from A whose target is N or R gives the 1st held instead. PositionsByGearbox (BRIEF_positions_by_gearbox.md) puts the positions the fitted gearbox lacks on <code>GearboxTruck::missing</code>: a missing low place is no stop of the walk, a key for a missing position or Range low without L does nothing, and a truck standing in one goes to L or the 1st held (H to the top) unless the game's shifter gave it. The read (gearbox_positions.h) is a job on the game's thread in the truck update's hook, hooked for it at Gearbox=2: <code>d7d550(vehicle, vehicle + 0x9C0)</code> (found by its first bytes, <code>GearboxDescFind</code>) gives the desc after the name and the socket's block (+ 0x988 .. + 0x990) check out, then the tagged ref at desc + 0x58, the object's bit 0 at + 0x10, the native at + 0x38, its pugi node at [+ 8], a node named GameData, and GearboxParams among its children (name + 0x10, first child + 0x20, next + 0x30, first attribute + 0x38; an attribute's name + 0x08, value + 0x10, next + 0x20), every read a peek, at most 64 siblings and 64 attributes. L by IsLowerGearExists, L+ by IsLowerPlusGearExists and L- by IsLowerMinusGearExists, each with L, H by IsHighGearExists, a missing attribute true; no GearboxParams none; IsManualLowGear takes nothing and goes to the log (<code>kManualDropsEnds</code> in <code>GearboxHasFromFlags</code> is false; true would take L- and L+, as the game's shifter shows). The flags in force are those the game hands its own shifter (shifter_read.h): the HUD's script parses the fitted gearbox at every truck change and asks a native for each flag by its name, <code>bool(node, {text, length}, default, flag)</code>, the answer in al. The mod hooks that native (<code>XmlBoolFind</code>: its first bytes once and the script thunk's call once, the call landing on it), at Gearbox=2 with Positions by gearbox, in a transaction of its own. In the hook the game's answer comes first and is always returned as it is; then the name by its length and bytes against the five; no log, no lock, no allocation and no read of game memory but the name itself. <code>ShifterNote</code> writes into a ring of four captures (the node, the flags asked, their answers, the tick), a count odd while one is written; its own fault guard, and a fault turns the route off for the session with a line in capitals. <code>ShifterPick</code> takes the newest whole capture no older than a second before the last pass on which the mod saw the truck before (<code>ShifterSeenStep</code>; for the first truck of a session any whole capture), and never one taken before: the parse has one caller, the truck change, so while the mod sees no truck every parse is of the truck to come. The hook goes in on the first pass that has Gearbox=2 with Positions by gearbox, with or without a truck; where a truck was driven already its parse is past, and the desc's read is in force at once with "the shifter's read was hooked after this truck was taken". <code>PositionsRouteStep</code>: the shifter's read is in force as soon as there is one; else the desc's read, once there is no hook, no GearboxParams (the script then asks no flag), a fault, or two seconds gone; until then every position is there and <code>PositionsWaiting</code> waits. A late capture replaces the read with a second line. Another fitted name on the same vehicle starts anew. The line "gearbox positions: ... by the game shifter's own read (node ...)" or "... by the desc's read (node ...; the reason)" names the route and the five flags; where the two reads differ, both sets and both nodes after "THE DESC'S READ DIFFERS". With Probe: "probe: gearbox positions: the script's bool read was called ..." a second. Nothing of the game's is kept: the mask counts for its vehicle and the name the mod's thread peeks, once a second, and a new name reads anew. A fault in the lookup turns the read off for the game. Gauges.log has "gearbox positions: the game's gearbox lookup at +..." at the code look, a line for each read with the five flags and the positions the truck gets, "gearbox positions: ...: the desc's read failed at ..." with the step, "gearbox positions: the script's bool read of an XML node at +..." at the code look, "gearbox positions: the game shifter's read of the five flags is hooked" or "gearbox positions: the game shifter's read could not be hooked (error ...)" at the first pass that wants it, "gearbox: L- asked, this gearbox has no L-" once a position and truck, "gearbox: this gearbox has no ..." for a position the truck stood in, and "gearbox: the game shifter gave ..." where the shifter gave one the read lacks. Up from L+ waits <code>kLowRangeUpHold</code> (1 s) after an up press from L brought it there (<code>lowPlusFor</code>, by the pass's dt; L+ reached any other way lets up go at once), its refusal logged once a visit to L+ (lowUpSay). A 1st reached from the low range is marked <code>heldFromLow</code> until the next shift, and <code>GearboxPosHeld</code> keeps Back to A and Sequential off it. Back in the world after the map, the garage or the viewer, <code>GearboxPosKeeps</code> keeps the position while the game's gear is the one the mod set last and its power the position's; otherwise <code>GearboxPosTake</code> reads it again (review 7, R6): read again, L+ is a first gear, and A, which runs with the game's automatic flag down, a held gear. At 2 the game's own ShiftGear, ShiftToAutoGear, ShiftToHigh, ShiftToReverse, ShiftToNeutral,
AutoOffAndShift and SetPowerFactor are refused for the held truck (SetCurrentVehicle passes, and everything passes out of the world).
Driven with a pad or the keyboard (Input 1 or 2) the first call of each door is read as a request for a position (R, N, H, A, gear g,
or the low range once a SetPowerFactor under 0.725 follows a first gear; L+ reads as the first gear): the hook packs it into an atomic
the mod's thread takes on its next pass, a request older than two passes is dropped, and the model applies it as that position's key
under its own rules (RequireClutch stalls it too). With a wheel's pedals (Input 0) no request is made, and it runs as 1 until a
binding that can be pressed reaches reverse (a key always, a wheel's while it is there, a pad's while a pad answers). While the NEXT GEN menu is open no action goes down and what was held stands; the reader steps every pass, and the pad and the
wheels count only with the game in front. A or a gear picked from R while the truck still rolls back (speed under -0.5 m/s) waits in N
until it stands, and the probe's letter is the model's while the mod's gearbox has the truck. The doors, as the first drives found them
(2026-10-26, the d110 with Gearbox=0, every position by hand): H is ShiftToHigh, then AutoOffAndShift(7), then ShiftGear(7); R is
ShiftToReverse, then AutoOffAndShift(-1), then ShiftGear(-1); N is ShiftToNeutral, then AutoOffAndShift(0), then ShiftGear(0); L is
AutoOffAndShift(1), then ShiftGear(1), with no Shift-To function of its own; A is ShiftToAutoGear, then ShiftGear(1); each is followed by
SetPowerFactor, 0.450 in L and 1.000 in A, H, N and R (L- and L+ wait for a truck that has them). With the game's automatic flag down
the game makes no gearbox call at a standstill on the brake (8 s held, every count 0), so the brake-reverse is the game's automatic's and
can only come while the flag is up; the shifter's R comes through the same door, which is why the flag and not the door tells them apart.
Two bindings an action in Gauges.ini's [Gearbox] (Bind&lt;Action&gt;, Bind&lt;Action&gt;2, for Gear1 the second is BindGear1Second: a key,
a pad chord, a wheel's button or POV, or an axis for the clutch), bound in the tab by a press: the capture's first pass takes a snapshot
of every control and it binds the first thing that changes from it, a control held or a POV pointing at the start left alone until let
go and named in the log (closing the menu ends a capture; the
menu's key, ToggleKey and the PadToggle buttons are never bound); defaults on the number pad, Num 8, Num 2,
Num 5, Num 0 and Num 7 for up, down, N, R and A. SMT.ini's keyboard bindings (DirectInput scan codes, the number pad's 0x47 to 0x53 named as number pad keys) and options are taken
over once, its DIFF LOCK and AWD too, and a default on a key it took is dropped. The manual
transmission mod's state is no longer read.</li>
<li>The differential lock and all-wheel drive on a key for every truck, the upgrade fitted or not, at every level of Gearbox (0 too):
DiffLock and Awd in the bindings (no default), each press flipping one byte of the truck's action as SMT did, the lock at +0x4A and
all-wheel drive at +0x49; the cluster's lamps read the same bytes. A key held is one press; nothing is written while no truck is driven
or the NEXT GEN menu is open. A byte the game puts back to 0 on the next pass after a write of 1 is said once a truck in the log
and left (<code>LocksStep</code>, <code>truck.h</code>).</li>
<li>A shift without the clutch costs the gearbox: on each stall the model orders (RequireClutch), round(capacity · StallDamagePercent/100)
points go into the gearbox's damage block (behind vehicle +0x148, laid out as the engine's), at most up to its capacity; 0 to 25, 5 by
default, nothing with Damage off; a stall of the game's own costs nothing. As the tire mod deals tire damage, it is dealt on the game's
own thread: the mod's thread posts a job (<code>StallJob</code>, <code>truck.h</code>); a detour on the game's truck update (Steam RVA
0xa56970, the update of 20 October 0xa58220; found by its call of GetForegroundWindow, then its first bytes before it; first argument
an object with the vehicle at +0x20) deals it on its next frame for that truck, then runs the game's damage update (RVA 0xd6f690 and
0xd710b0, found by its first bytes), which sums the parts at vehicle +0x108 and brings up the truck card. The log gives the points and the
sum the game reports after; a job no frame takes within 5 s is taken back. The probe's damage line names the gearbox's damage beside
the engine's.</li>
<li>A slipping clutch under load heats up and costs the gearbox, faster the stronger the engine (ClutchBurn, default 1, with Damage;
the tab's "Clutch burns"): <code>ClutchHeat</code> in <code>models.h</code>, stepped from the clutch model each pass with the engine's
Torque figure times the profile's share, the heat kept per kind of truck as the temperatures are; the burn's whole points go through
the stall damage's job. The log says "clutch: burning, the gearbox pays" and, when it cools under the burn point, the points it cost;
the probe's clutch line gives the heat and its share of the burn point. The figures are in the damage section. Every clutch heats, a
pedal's or the one that works by itself (on a button, or none). The automatic clutch rides as a driver's foot would: while it slips the
engine goes to kAutoTakeUp (0.9) of the pedal's governor speed at the free engine's pace and the clutch passes on what the engine gives
there, from none at idle to all of it a tenth of the way to the red zone, until the gearbox's input has come up to the engine; then it
locks, and it lets go again when the input falls under idle. From kAutoLockKmh (15 km/h) up it stays locked and never slips; with the
engine held under its idle speed in gear (lugging) the mod raises the game's stall seconds (vehicle +0x11C) by the pass's time and puts
them back to 0 when the engine recovers, so the game's own "engine stalling" notice shows and it kills the engine at 1.4 by its rule (a
drive confirms the notice). A stall under RequireClutch ramps the seconds from 0 to 1.5 over 0.3 s (StallStep) instead of writing 5 at
once. The controls are read by a poll thread about 200 times a second and latched (InputLatch), so a tap between two passes still gives
its press. Floored from a standstill (a truck of gears 3, 6, 12 and 16, the input
gaining 2000 rpm a second a share in first and by the square of the gears' speeds in the others) first reaches 0.57, the box never shows,
third goes amber after 1.6 s and red after 4.0 s, fourth red after 3.4 s and a gear the truck cannot pull after 2.9 s; it heats fully
only under 5 km/h and a fifth as fast from 15 km/h, and cools to half in 4 s. Those figures are at ClutchHeatPercent 100; the tab's
"Clutch heat rate" (ClutchHeatPercent, 10 to 300, 60 by default) scales the heating only: at 60 first reaches 0.40, third goes amber after
2.9 s and peaks at 2.16 (no red), fourth amber after 2.6 s (2.72, no red), and a gear the truck cannot pull amber after 2.4 s and red
after 6.0 s. The cost only
while it slips past the burn point; a repair clears the heat ("clutch: repaired, the heat is gone"). A stall's
job waits while the same truck is driven and goes only when another truck is.</li>
<li>Warning boxes in the shape of the tire mod's TIRE DAMAGE box (800 by 148 of its reference pixels, a stripe down the left, the title,
a line of advice, the number at the right and a bar along the bottom), stacked below it at stage 52, 205 plus a slot of 148 + 16
(<code>WarnOrigin</code> in <code>gauge_draw.h</code>), drawn by the mod's own overlay with its own atlas of the game's font baked for
the stage's scale, fading in and out over a quarter second, on the background list and never while the NEXT GEN menu is open. The
clutch's (ClutchBurn and Damage on, slot 1): amber CLUTCH HOT "Ease off or shift down" from kClutchWarnFrom, red CLUTCH DAMAGE "Shift
down, the clutch is wearing" from kClutchBurnFrom, the number q in percent of the burn point. The engine's (Damage on, slot 2): amber
ENGINE HOT "Ease off, the coolant is in the red" from kTempRed (118 °C), red ENGINE DAMAGE "Ease off, the engine is wearing" while
DamageHot is above 0, the number the coolant's degrees over 118 in percent of the way to 125. With HotDamageFrom at its default (118)
the red follows the amber at once. At the turbo's full boost with the pedal floored the red title reads DANGER TO MANIFOLD with "Keep an
eye on the floor pan", and once a point of the heat's damage lands under it, "The floor pan fell off" for the rest of that red spell.</li>
<li>Known: the own gearbox is not tested on a wheel or shifter yet; a test on one follows. In a manual position the engine pulls by the mod's
torque curve at the revs it is at, so a high gear at low revs pulls less than A keeps it pulling. A downshift at speed does not yet send
the revs over the red zone: no engine braking from it and no over-rev damage. Lugging the engine in too high a gear at speed raises the
game's own stall seconds; whether the game's stalling notice shows for it is unconfirmed. The low range's L- and L+ power factors are
taken as 0.2 and 1.0 until a truck with them is driven at Gearbox=0; L is 0.45 as driven. On Windows 10 the NEXT GEN menu may not draw
while ReShade loads the .asi files as add-ons: turn that off in ReShade's Add-ons tab (one player's report, under investigation).</li></ul>
<h3>Changed in 0.18.4</h3>
<ul><li>Each gear's top is learned for each truck kind as a share of the game's gear speed, and kept in the truck's file. Before, it was kept per gearbox and shared by every truck with that gearbox's name, so another truck's figure could hold the automatic in a gear at the game's own limit. A learned top far off the one kept replaces it, and none is learned on a slope. Floored in A at the game's limit, the automatic now shifts up. When the wheels' drag can't be read, the log names the wheel and the figure, and it says when the read works again.</li>
<li>Air drag learns each gear's pull with the wheels' own drag left out, so Rolling drag and a flat tire don't make it stronger.</li>
<li>Air drag saves a gear's pull once it is more than 2 % off the figure in the truck's file. Before, one learned step had to move it over 5 %,
so a smaller change, a truck's new mass among them, was learned again every drive and never saved.</li>
<li>Air suspension and Level to horizon act only on the ends you tick. The game's airbags are only a look, so the Suspension tab asks:
a tick Air beside Ride front (the front axle) and beside Ride rear (every other axle). The ticks are kept with the suspension set, per
kind or per truck. Until a truck has ticks they come from <code>AirAxles=all</code>, <code>rear</code>, <code>front</code> or
<code>none</code> in <code>[Truck]</code> of its file in <code>Gauges\\trucks</code>, then from the mod's table, else none. So a truck the
mod knows nothing about, mod trucks too, starts with none ticked: if it had air before, tick its ends. With none ticked the two settings
are greyed and Ride up and Ride down do nothing. A tick changed while you drive fades the levelling out first. Sync all enabled shows one
slider for the air's height. Defaults takes the ticks out. The tab and the log say where the ticks came from.</li>
<li>Camber gain and Camber at rest in the Suspension tab, for the front axle and every other. Camber gain scales how much each wheel
tilts as it travels, x0 to x3 of its file's figure. Camber at rest adds -10 to +10 degrees to the wheel's tilt in its file. On a swing
axle they line the drawn wheel up with the half-axle. Only the look changes: the physics wheel stays as the game has it. They are kept
with the suspension set, per kind or per truck. At x1 and +0 nothing is written. <code>Gauges.log</code> names each wheel's three
camber figures once a truck.</li>
<li>Engine torque and Upshift delay can be kept per truck, as the suspension is. One scout's slider no longer moves every scout with
that engine. The Engine tab has Own settings, Make this the engine's default and For, as the Suspension tab has. A truck keeps a figure
for each engine and gearbox fitted. A figure it has none for comes from the engine's or the gearbox's default, then 100 % and the
class's hold. The line In force under each slider says which. An old <code>Torque=</code> or <code>GearHold=</code> is that default, so
nothing is lost. When the game rebuilds the truck it keeps its engine set too.</li>
<li>Frame flex and Twist range in the Suspension tab: how stiff the frame is against twisting and how far it twists, as factors of the
truck's own. They act on trucks whose frame twists, about two in three of the game's. Below 1 Frame flex lets the frame twist more
easily, so the wheels keep the ground longer. They are kept with the suspension set, per kind or per truck. <code>Gauges.log</code> names
the truck's own figures once a truck.</li>
<li>The suspension settings can be kept per truck: tick Own settings in the Suspension tab. A truck without its own uses the default.
It ticks itself when you change a setting there while you drive. Unticked, the truck's set is kept for later, and changes meanwhile
edit the default. Make these the default copies the truck's set to every truck without its own. Under the box, For picks This truck
only (this one truck, by the game's id of it) or every truck of its kind; the tab says which is in force. When the game rebuilds the
truck it keeps its set. Every truck of its kind keeps the truck's own set aside when the kind has one, and This truck only brings it
back. A truck taken out of the warehouse starts on its kind's settings.</li>
<li>A truck back from the garage keeps its own suspension set however long the visit took. The game gives it a new id there. On the same truck with no loading screen between, the new id takes the set over. Before, a visit over 20 s lost it. A switch through the map to another truck of the kind takes nothing over while the first is still in the game, so each keeps its own.</li>
<li>Floored in A where the game's own pull tapers off, from about 0.7 of a gear's top, a truck that stops gaining short of its point up gets the next gear after a second. Before, a truck a little heavier stayed in its gear. The next gear has to land over its own point down. It is watched from 1 s to 3 s after the shift: if it loses speed faster than the gear it left, the automatic goes back and keeps that gear for 30 s. <code>Gauges.log</code> has a line for each.</li>
<li>The game's closing now leaves a line in <code>Gauges.log</code> naming who closed it and from where, and the creep gives nothing on a
pass the mod shifts in (the author's drive of 30 October, the game still closing on a shift into or out of N).</li>
<li>The gear keys and the other bindings wait while the game's function menu is up and 1.5 s after it closes, so picking Recover or the garage with Pad A no longer shifts down. A shift's line in <code>Gauges.log</code> gives the time it was dealt, and says how much later the line came where the game held the mod's pass off.</li>
<li>A shift the mod's gearbox asks for lives a second at most and goes with its truck, so picking another truck with Pad A no longer shifts down 50 s later.</li>
<li>The crash line in <code>Gauges.log</code> also names an invalid parameter in the C runtime and abort, which can end the game at once (0xC0000409). The crash line is armed as the mod starts, so a stop at the game's boot is named too. It is armed again at the first truck, after the game has set its own handlers.</li>
<li>The ASI loader starts on Linux (Proton and the Steam Deck) with the launch option <code>WINEDLLOVERRIDES="version=n,b" %command%</code>. Before, the mods never loaded there, and with that option the game would not start.</li>
<li>The Gameplay tab in eight tabs (Display, Engine, Gearbox, Power, Suspension, Controls, Sounds, Mod work), the open one kept in <code>Tab</code>; Driven with and every binding on Controls. The settings have shorter names, and each help text says what it does and its default. The ini keys are unchanged.</li>
<li>Air drag (<code>RoadLoad</code>, on by default, with Powertrain): the air's drag taken off the engine's pull against each gear's learned pull at the game's own torque, on top of the game's own slowing, which grows with speed. Each gear's pull is learned floored up to 0.8 of its top, on grades up to 3° by the path, over several visits; the gears not learned go by the learned ones once two agree within 15 %. With the probe, a "pull:" line measures the game's own pull by x.</li>
<li>Creep in a hand-picked gear (<code>CreepManual</code>, off by default): the governor, the holding part and at Gearbox=2 the stall are reached again, R and the low range on the game's shifter through the converter; in Sequential the gearbox goes back to D first.</li>
<li>The log stays open and is written out every half second, so <code>Probe=1</code> no longer opens the file 70 times a second:
one append handle that shares writing (so CrashLineOut's own handle still gets in), a 64 KB buffer flushed on every line that is not a probe line
and from the mod's loop half a second after the last flush; the crash and exit writers flush first where the lock is free. An outside kill loses
at most half a second of probe lines.</li>
<li>Up from L+ goes to 1st a second after the truck got to L+, so a run of presses up from R stops in the low range. A 1st reached from the low range stays held until your next shift, so the low range never goes on to D by itself.
Back from the map, the garage or the viewer the gearbox keeps its position (L+ came back as a 1st, D as a held gear).</li>
<li>Sequential's own shifts go back to D, which picks the gear: near the red zone with the foot on, and before a
stall with the foot on or off (0.2 s under the brake). Under the brake D comes down to the speed's gear at once, skipping gears.</li>
<li><code>Gauges\\logs</code> keeps the newest 20 start logs and at most 500 MB of them (<code>KeepLogs</code>, 0 keeps all); only the mod's own date-named logs are removed.</li>
<li>The automatic's position shows as D on the cluster and in the tab ("Back to D", the D binding); the ini keys, the bindings, the state block and the log keep A (<code>GearLetterShown</code>).</li>
<li>A gear held by hand at Gearbox=2 with the clutch fully in gets the lift by the wheels' own speed, held to twice the
engine's torque, so spinning wheels in mud spin up toward the red zone and wheels that catch do not lurch. Back to A comes with
the foot down when A's gear is the held one. With Back to A on, a held gear shifts up by itself at the red zone and down before a
stall, unless RequireClutch or RequireGearHeld is on or the gear was picked on the game's shifter.</li>
<li>In a gear held by hand at Gearbox=2 the mod's automatic keeps reading along: the wheels' grip stays learned, so
Back to A goes back (it waited for the grip forever), A comes back in the gear A would pick, not in 1st (Back to A keeps it next
to the held one), and the lift
pulls to the red zone instead of fading past half the gear's top. There the grip rises only with the pedal under 0.7.</li>
<li>Gearboxes of up to 30 gears are read, with their reverse and high gear, so a mod's 16-speed drives in every position and shows
its fuel a minute; a longer list is logged once a truck and not read. The gear keys go to Gear16 (they went to Gear14).</li>
<li>Each of the mod's shifts goes through four stages (asked, set, engaged, idle 1.5 s later, A shift, step by step). The creep
gives nothing until the shift is idle, and a shift without the clutch stalls once the new gear is set, so the stall never lands on
the old gear.</li>
<li><code>IgnoreShifter</code> (the tab's "Ignore shifter", at Gearbox=2): the game's shifter picks nothing, with any
<code>Input</code>; only the bindings shift, and until one of them reaches reverse, 2 runs as 1, as with a wheel's pedals.</li>
<li><code>BackToA</code> (the tab's "Back to D", 0 by default) and the tab's Sequential button: after a shift by hand into a
numbered gear the gearbox goes back to A by itself once its conditions hold (Back to A).</li>
<li>The state for other mods is version 2: 256 bytes with everything the cluster shows, for another mod's speedo (Shared
memory).</li>
<li>The mod's own gearbox shifts on the game's thread: each shift is handed to the game's truck update and made there, with the automatic flag before it and the power factor after it, instead of from the mod's own thread. <code>ShiftOnGameThread=0</code> (ini only) goes back to the old way. Each shift writes one line to the log.</li><li>Two layouts in the style of Spotter's HUD: Spotter dials (<code>Layout=2</code>, rpm and speed dials with the temperatures, boost and air in cells) and Spotter digital (<code>Layout=3</code>, the speed and rpm in figures over a segmented rpm bar). Both sit left of Spotter's gear panel at its height, show the gear Next Gen Gameplay has in force, and have the mod's own lamps only (cold, oil, engine, tilt, lights, tire), as Spotter's panel shows the handbrake, AWD and diff lock. Style and FaceOpacity are for the strip and the block.</li><li>With Spotter installed (its draw API, version 1 or later), the overlay draws through Spotter's frame hook instead of a hook of its own, where ReShade has not taken it as an add-on. <code>NextGen.ini</code> <code>[Overlay] Spotter=0</code> turns it off. While the NEXT GEN menu is open, the game gets no mouse or keys and Spotter's hotkeys rest. While Spotter's own menu or big map is open, the menu's key does nothing.</li><li>Nothing about ReShade is shown on screen any more: the notice in the NEXT GEN menu and the line at the top for 15 seconds are gone. The log says which route the overlay takes. ReShade's <code>dxgi.dll</code> and <code>ReShade.ini</code> stay in the zip for RivaTuner users, whose game does not boot without it. The mods draw without it.</li><li>A test, on the Suspension tab: air suspension (<code>AirSuspension</code>). Each wheel is held at its rest place of the first standstill after the truck is taken, so cargo no longer makes the truck sag or lean to a side. It levels only the height, pitch and roll, so a twist over rough ground stays the ground's. Level to horizon (<code>AirHorizon</code>) levels the body's lean and slope to the horizon instead, under 10 km/h (over it, it goes back to the wheels), about 7 degrees at most. It levels under 10 km/h (about 3 s), and an axle with a wheel hanging or on its stop keeps its height. Turned off, it goes back over a few seconds. <code>BindRideUp</code> and <code>BindRideDown</code> raise and lower it 2 cm a press, within 10 cm, under 20 km/h, for the truck driven, and a box shows the height for a few seconds. It goes on top of the ride height and the sway bars, the three held to 12 cm together, and stops on a truck whose game moves the attach point itself.</li>
<li>Tests, on the Suspension tab: a ride height for each axle (<code>RideFront</code>, <code>RideRear</code>, -10 to 10 cm) and a sway bar on each end (<code>SwayFront</code>, <code>SwayRear</code>, 0 to 0.3 of the axle's spring rate; above it the bars swing), with a binding that disconnects the bars and a lamp while they are off. The ride height goes in whole cm and shows 0 as "0 cm". Both move where each wheel's spring hangs from, and stop on a truck whose game moves that point itself. The suspension display (<code>SuspDisplay</code>) shows each wheel's travel, its end hits, a hanging wheel and the articulation.</li>
<li>A test: with <code>Probe=1</code> each wheel's travel and speed are read every physics step and logged every 10 s (steps a second, a
histogram of the speeds, the range, a bounce), and <code>SuspStepTest</code> (ini only) shows whether a spring or damping write takes on
the next step. The creep's hook on the wheels' drive is now C++ and is attached for <code>Creep</code> or <code>Probe</code> (review 14,
phase 1).</li>
<li>A test, on the Suspension tab: Spring and Damping as factors of the truck's own, x0.25 to x4 (<code>SpringScale</code>, <code>DampingScale</code>), with the truck's file figures front and rear and what is in force shown under each, and a note where the game's limit of 1 holds. Extra travel adds 0 to 30 cm past the file's compression end (<code>ExtraTravel</code>), from a file's 0.0 too, and never shortens; past the file's end a wheel may go into the body. SuspensionMax does nothing in the game, so it has no slider. The damper's curve, spring curves, bump stops and damping zones are there as tests too, all off by default.</li>
<li>The creep only with an automatic, for now: the mod's A and the game's own automatic creep as before; a gear picked by hand,
on either shifter and an R included, gets none (the author, 1 November). A gearbox whose name is not read keeps its gear hold: the
same gears within 2 % keep the first gearbox's file, where each small drift of the speeds was a new gearbox before.</li></ul>
<h3>Changed in 0.18.1</h3>
<ul><li>The creep holds the gear's idle speed once the truck rolls in a gear picked by hand, up to the engine's torque at idle
(review 13; the author, 29 October): in 4th or 5th on a flat road the truck rolls on at idle. A load past the idle torque lugs it, and in the mod's
own gearbox (Gearbox=2) under 0.6 of the idle speed it stalls a second later, the needle following the clutch's engine down. On the
automatic and the game's shifter, in A and within a second of a shift no stall comes and the engine idles on (the author's drive of
BF57872B: "on auto, the truck stalls in R, L-+, when switching gears"). R, L- and L on the game's shifter creep through the
converter, its share there smaller by the gear and paced; the game's L+ creeps as its first gear. From a standstill a tall gear still does
not move off. Until now the governor gave nothing at the idle speed, so a tall gear slowed to a crawl with the needle at idle (5th:
18 to 14 km/h in a second, 28 October).</li></ul>
<h3>Changed in 0.18.0</h3>
<ul><li>The turbo of review 12 with its addendum: a rotor that spools by the fuel and the engine speed (τ = τ<sub>red</sub>/max(<i>q
n</i><sup>4</sup>, 0.02), down with 1.5 s), the boost by the fuel along each class's own full-load line, a limit that is 1 settled
and floored (the air's share of the torque, kBoostTorque, is gone), a diesel or a petrol engine per class and per engine
(<code>Fuel</code>). The author's drive and review 12, 28 October: the boost's share cut the torque at every speed below full
boost even settled, and the dial followed the engine speed.</li>
<li>The boost dial from vacuum on a petrol engine (−1 bar) to 2 or 3 bar by <code>BoostMax</code>, which now goes to 3; a diesel's
lift is quiet as its turbo coasts down (own <code>turbo_whistle_</code> files play there; one made by code was tried and dropped), the
flutter on a diesel a setting (<code>DieselFlutter</code>, off by default).</li>
<li>Creep at idle (<code>Creep=1</code>, on by default; asked for on mod.io, 27 October): an idling truck in gear moves, through
the converter to about walking pace, in a gear picked by hand at the gear's idle speed, through a detour of the game's wheels'
drive that raises its throttle for the driven truck only. The pedal, the game's engine speed, fuel and sound stay at idle. At zero
pedal the needle maps the wheels in proportion, never under idle, and the converter's stall rise needs the foot on. The governor
falls in a line to the gear's idle speed, gives less in the low range, rises at most 0.3 a second and looks 0.6 s ahead while the
wheels gain: at once and whole it overshot in the low gears, and on soft ground the wheels and the needle jerked (the author's
drive, 28 October). Stuck in a gear picked by hand, at any Gearbox level, the engine stalls a second after the game's pull has
built; in A it never does.</li>
<li>The probe lines (review 11): "powertrain" ends with the factor's own terms (LiftShare's x, LiftRoof there, the pace) and marks
its power and fuel as the model's own; "automatic" ends with what its rules decide by (feel, the points up and down in force, the
lock and unlock points, the pause, wanted, the hold, the kickdown's arm, the cruise, lift and stuck timers, the start gear) and x
against the model's own gear. The own positions' shift lines carry the clutch model's figures instead of zeros, and the hunt line is
written only while the model drives. The fields the log tools read are kept where they were.</li>
<li>Each gear's top is learned where the game's own pull reaches nothing (review 11): a floored plateau is logged and from then on
gives that gear's top, kept in the gearbox's file, so the limiter, the floored points, the needle and the lift go by the gear's real
top. The high gear's a + 2 holds.</li>
<li>At Gearbox=2 in the gearbox's own positions there is one engine (review 11): the needle and the torque curve the game gets go
by the clutch model's engine speed, not by the wheels' share and a pedal lift that worked with the clutch pressed too. The clutch
model's governor is a road truck's min-max one, the pedal a torque between the idle and the high-speed governors. A stall with the
clutch on a pedal's axis is handed to the game; a clutch on a button never stalls the engine as the truck moves off, as before (stuck
at idle with the creep it does).</li>
<li>RequireClutch: a shift without the clutch at 1.5 m/s (about 5 km/h) or more grinds and stays out, N with a log line, instead of
stalling the engine; standing or crawling it still stalls (review 11: it stalled at 60 km/h too).</li>
<li>With Powertrain=1 the lift's fade starts at 0.7 of a gear's top (from 0.8), so the push comes down over the last three
tenths, less steeply. Within two seconds of a cut a push that grows comes back at about 1 a second; an ordinary rise through a gear,
a push that falls, a new gear and the first write after the hand takes the truck come at once. A tester's log of 28 October: a heavy truck held near its top gear's limit
at part throttle lost and regained its push every two seconds, the factor swinging 1.0 to 2.4 and the speed 36 to 39 km/h, since the
fade was steep and the push came back in one jump. The top speed stays the game's own; the floored shift up moves with the fade, to
about 0.905 of a gear's top with a truck's roof (from about 0.92).</li>
<li>The automatic's torque converter is a truck's (review 11). It stalls at 0.8 of the red zone (at most 2600 rpm; 0.75 and 2400
in 0.17.1), the engine climbing from there through a launch, and couples at 0.87 with 4 percent slip. A launch locks once the
converter has all but coupled, a shift made locked locks again on the next pass, and it locks in first gear too. Locked, it stays
locked on a lift, so the engine follows the wheels and brakes the truck, and in engine mode a press never opens it: it lets go only
under 1.4 idle with the foot on and 1.04 on a coast. The truck felt like a CVT or a rubber band: the converter opened on every lift
and every shift, never locked in first, and the engine flared against its stall speed at each press (two players' reports and the
author's own drives, 28 October; review 11 found a press from a low cruise still flared it).</li>
<li>In engine mode the automatic cruises where a diesel pulls cleanly (review 11): at a light foot the converter locks from 1.65 idle
(from 1.3) and the gearbox shifts down at 1.6 idle (from 1.15). A scout without a profile cruises from about 2370 rpm instead of
3300 (the light-foot shift-up point from the curve's 0.9 point, at most 0.45 red). On the limiter a gear going up is held 0.9 s, not the hold's 2.5
s. When the floor lifts the game's gear past the model's held one, the model's gear follows. The coast shift down with the brake on lands low for an engine brake, and slowing hard with the foot off and the grip known (the
brake, or the wheels' trend in the ground's terms under -1.5 m/s² × grip, <code>kBrakeSlowing</code>) it goes as far as the
speed's gear at once, with no <code>kShiftPause</code> and no coast wait before it; with the grip unknown under the brake, one gear
at a time, as wheels locked by it read nothing; it matters once the
mod has one.</li>
<li>A binding's capture in the tab binds the first thing that changes. A control already held or a hat already pointing when the
slot is clicked is left alone until it is let go, and Gauges.log names it. Two players could bind nothing in the tab, and only
Escape worked: the capture waited for every control to be at rest, and a controller reporting a hat or a switch as pressed kept it
waiting.</li>
<li>With Powertrain=1 the automatic's shift up out of first (and out of the gear it sets off in) goes by the wheels as they
turn, spinning or not. A tester's log of 27 October: a ZIL-157 on dirt sat in first for 10 to 40 s with its wheels spinning near the
gear's top, since the shift up went by the ground speed, and the game's pull falls by the wheel speed. From the second gear up a
shift up goes by the ground as before. A truck stuck where it stands, under 0.3 of the start gear's top over the ground, keeps the
ground's rule, so it does not shift up and back.</li></ul>
<h3>Changed in 0.17.1</h3>
<ul><li>At Gearbox=1 a position picked on the game's shifter hands the truck back by the door it came through, whatever gear it
names, so the automatic never shifts out of a player's L+ and the game keeps the diff lock. Before, the hand-back went by the gear
number. L+ and the automatic's first are the same gear, so the automatic shifted on out of L+ and the game dropped the diff lock (a
tester's report, 28 October). L and L- picked from the automatic's first gear were missed the same way.</li>
<li>The engine's lift fades over each gear's last fifth: its roof is flat up to 0.8 of the gear's top and falls in a straight
line to the game's own at the top. The roof is 6 for a truck or a heavy truck and 3 for a scout. A vanilla Defender 90 floored on a
road shot from 27 to 58 km/h in 5 seconds through three gears (27 October). The floored upshift now comes earlier: a ZIL-131's at
2880 to 2928 rpm (was 3024 to 3056).</li>
<li>A pad's stick directions bind as buttons: LSUp, LSDown, LSLeft, LSRight, RSUp, RSDown, RSLeft and RSRight, bits 16 to 23 of the
pad's mask above XInput's 16 buttons, each down while its axis is past half its travel from the dead zone (8689, the right stick's,
for both: 20728 of 32767). The capture and the poll thread's latch take them as buttons, so <code>Pad LB+RSUp</code> binds and a flick
between two passes counts (asked for in the comments). A pad binding with an unknown button in Gauges.ini ended the game at its start
(a 99-character reason copied with strcpy_s into 96 bytes); the reason is now cut to its room.</li>
<li>At Gearbox=2 up and down walk the low range too: R, N, L-, L, L+, 1, 2 and on to the top, as SMT's did, on every truck (L+ is
the first gear at 1.0, so it and the 1st are the same gear). SkipNeutral now steps from L- to R and back.</li>
<li>DownStaysForward, off by default: at Gearbox=2 Gear down stops at L-; from L-, N or R it does nothing, and up from R still walks
N and L-. It wins over SkipNeutral, and the once-only import of SMT.ini takes SMT's IMMERSIVE MODE over as it (asked for in the
comments).</li>
<li>At Gearbox=2 driven with a pad or the keyboard (Input 1 or 2) the game's shifter picks the position: the calls stay refused, and
the first door of each shift is a request the model takes as that position's key (L+ reads as the first gear). With a key bound the
slider used to move while nothing shifted. A request equal to the shifter's last one is not followed (<code>ShifterAskStep</code>): the game sends the shifter's position again when its button is let go. The last request is kept from the take (the position the truck is taken in) to the let go, and a binding does not change it. The log says "gearbox: the game's shifter sent A again, not followed" the first time for each position ("gearbox: the game's shifter sent gear 3 again, not followed" for a gear) and a minute after "gearbox: the game's shifter sent its last position again 2 times in the last minute, none followed". The repeat told at once is not counted, so that line comes only for more. At the take the log names what the shifter is taken as standing at: "gearbox: the shifter taken as at A: what it sends is measured against that", or "gearbox: the shifter taken as at gear 3: what it sends is measured against that" for a gear. Where the mod was handed back and takes the truck again, the game's shifter may stand elsewhere, and its first send then reads as a pick. With a wheel's pedals (Input 0) the game's shifts stay refused and level 2 still runs as 1
until a binding reaches reverse (asked for in the comments and seen in the author's own drive).</li></ul>
<h3>Not changed</h3>
<ul><li>The oil's lag is fitted with the engine speed set equal to the load (0.5).</li>
<li>The light-foot downshift floor is 1.15 × idle: 748, 920 or 690 rpm by class.</li></ul>
'''


def s_constants():
    rows = [['Pass', '50 ms sleep; Δt 0.001 to 0.2 s', '', 'the mod'],
            ['Class scales', 'truck 650/2200/3000 rpm, 100 km/h, 1.8 bar; scout 800/5500/7000, 120, 1.0; heavy 600/1900/2500, 80, 2.0', '', 'not stated'],
            ['Gear top', '2a + 5 rad/s, or its share learned per truck kind on a floored plateau (pedal 0.9, B4 0.02, wheels 0.85 to 1.15 of it, 1% for 1 s, the grade within 2°); high a + 2', '', 'the game\'s, as read; review 11'],
            ['Game pull', 'rise 2.8x − 0.25, fall 2.1(1 − x)', '', 'action +0xB4, floored, two trucks'],
            ['Shift gain', '5% more pull', '', 'not stated'],
            ['Needle', 'floored standing 0.65, neutral 0.9, overrun 1.1; lag 0.18/0.25/0.4 s; dip 0.85 for 0.3 s', '', 'not stated'],
            ['Stall, start', 'dies at 1.4 s (0.714/s); start 1.8 s', '', 'the game\'s timers'],
            ['Pedal shape', 'p^1.8; up 0.6 s, down 0.15 s', '', '"half way down gives 0.29"'],
            ['Cold oil torque', '15% at 0 °C, none from 60 °C', '', 'not stated'],
            ['Boost limit', 'min(1, r (1 + b)/(1 + full · taper^1.6)), r 1.2 a diesel, 1 a petrol', '', 'review 12, finding 1, and its addendum'],
            ['Torque factor', '0.05 to 6 × the game\'s (a scout 3), the roof fading from 0.7 of a gear\'s top to the game\'s own at the top, a rise at most 1 a second within a gear within 2 s of a cut, times the engine\'s Torque share where set; step 0.005', '', 'a tester\'s log: the roof of 3 in 1133 readings; the roof of 6: a d110\'s kick at 23.8, 2026-10-25'],
            ['Converter', 'stall 0.8 red, at most 2600 rpm, open + 0.15 input with the foot on; slip 1.04; coupling 0.87; ratio 2 at stall; lock 0.5 red (engine mode 1.65 idle light to 0.5 red floored), in the set-off gears at a speed ratio of 0.92; open 0.4 red (engine mode 1.4 idle, 1.04 on a coast); locked on a lift', '', 'Allison 3000/4000 data sheets (review 11); two players\' reports, 28 October'],
            ['Creep', 'converter 0.15 at a standstill to nothing at a speed ratio of 0.87; governor 0.15 × top/top(1) in a line to nothing at the idle speed, up 0.3/s, look ahead 0.6 s; holding part to holdMost in 2.5 s, armed from 0.6 of the idle speed, look ahead 1 s, still within 3%; brake 0.02; stall a quarter of the idle speed (unarmed) or 0.6 at the holding part\'s most (armed) for 1 s once the pull rose ≤ 3% in 0.5 s, only at Gearbox 2 by hand, none within 1 s of a shift', '', 'a first figure (review 11\'s addendum; the addendum to the creep and stall brief)'],
            ['Shift timing', 'pause 1 s; up 0.4 s (0.15 floored); down 0.5 s; coast 0.3 s', '', '"a short first gear is through in a second"'],
            ['Cruise, lift, coast', 'cruise: pedal 0.5 or less, steady within 2%/s for 2 s; lift: gaining 2%/s at most, slowing 4%/s at most; coast point(0.15), braking point(0.25); with keys a lift waits 2 s, and one taken back within 6 s blocks lifts 10 s, doubling up to 60 s', '', 'a car\'s nine-speed automatic filmed on the road, 2026-10-19; the keyboard drive of a tester, 2026-10-19'],
            ['Kickdown', 'part throttle 0.2 to 0.9 (pad 0.75) for 0.3 s, then 0.9', '', '"a pad\'s trigger held near its end dips"'],
            ['Gear hold', '2.5 s, scout 1.5 s; on the limiter 0.9 s', '', 'Fleetstar 1 to 5 in 2 s; driven in the game; review 11'],
            ['Light foot', 'up no lower than 0.8 × the curve\'s 0.9 point, at most 0.45 red; down not under 1.6 idle light, 1.15 floored', '', 'truck automatics\' part-throttle points (review 11)'],
            ['Hunt guard', '6 s window, 8 s kept, up to 30 s', '', 'a tester\'s Tayga, 0.15.0'],
            ['Class by mass', 'scout ≤ 6000 kg, heavy ≥ 16000 kg', '', 'TUZ 108, Tayga, Western Star'],
            ['Coolant', 'cruise 97 °C; thermostat 82 to 92; fan 98 to 102; idle heat 0.15; still air 0.35; neutral 0.25', '', 'cruise by design; rest not stated'],
            ['Warm-up path', 'load 0.5 at 13 km/h', '', 'a drive logged in the game, 2026-10-08'],
            ['Defaults', 'warm-up 3 min, overheat 1.5, cool-down 15, air full 30 km/h, oil warm-up 5, OilWarnC 120', '', 'not stated'],
            ['Water', 'from 0.5 m to 1.2 m; stopped × 4', '', '"about axle height", "engine in it"'],
            ['Oil over coolant', '4 °C × speed², 10 °C × load; at least 60 s after the coolant', '', 'not stated'],
            ['Engine damage', 'hot 10%/min at 125 °C from HotDamageFrom (118 by default, 100 to 124); cold 5%/min at 0 °C above load 0.5', '', 'not stated'],
            ['Boost', 'truck n 0.36 to 0.60, τ_red 0.045 s; scout petrol 0.20 to 0.30, 0.010 s; heavy 0.37 to 0.68, 0.085 s; fuel^1.6; rotor down 1.5 s; manifold 0.1 s; petrol throttle shut 0.3 bar abs', '', 'review 12 (the C13 and the 3406E); τ_red fitted by its addendum\'s simulation'],
            ['Turbo sounds', 'window 0.25 s; blow-off lift 0.5 at 0.22 BoostMax; flutter 0.25 at 0.11; quiet 0.8 s; a diesel\'s lift quiet, own whistle files pitched 0.5 to 1 by the rotor', '', 'not stated; a diesel has no blow-off valve (review 12, finding 6)'],
            ['Range', 'marks 50 m, window 3 km, first 500 m, gulp 0.1 l, smooth 10 s, at most 999 km', '', '"a shorter way is mostly the pulling away"'],
            ['Brake air', 'cut-out 8.2, cut-in 6.9, flow 0.23 bar/s, most 12, brake 0.35, spring 0.4, leak 0.1 and 0.25 a minute', '', 'fill in 1 min at red, 3 at idle; 8 applications to the red zone'],
            ['Tilt', 'smooth 0.3 s; back 2°; warn 15°, roll 25°, pitch 35°', '', '"so a bump does not light the lamp"'],
            ['Day', 'swing 10 °C; coldest 05:00, warmest 15:00', '', '"coldest just before sunrise"'],
            ['Clock search', '10 s of game time a look; 4 steady looks; pace 2.3e-5 to 0.08/s; jump 1 h; 180 s alone', '', '"a day of 5 minutes to 12 hours"'],
            ['Shared blocks', 'Gauges\' 256 bytes, the others 64; tire stale after 2 s, gearbox mod after 1 s; looked for every 5 s', '', 'the mods\' layouts'],
            ['Sounds', 'RMS 0.1, peaks 0.6, voice × 0.45, pitch 0.25 to 2', '', '"two that ring together stay below 1"']]
    rows += [['Gear list', '32 entries: R, 30 gears, H; gear keys Gear1 to Gear16', '', 'the game has R, 16 gears and H at most (the author)'],
             ['Shift job', 'one out at a time; dropped after 1 s, at once on another truck or none', '', 'review 17'],
             ['Shift stages', 'idle 1.5 s after engaged (kCreepQuiet); 3 s at most asked or set (kStageMost)', 's', 'review 17'],
             ['Back to A', 'BackToA 0 or 3 to 30 s (Sequential 10); steady 1 s; pedal 0.7; spin 1.05 × ground + 0.5 rad/s; climb 8° at pedal 0.4; descent 5°; locks 3 s; foot off 0.05', '', "review 17; 10 s the author's"],
             ['Suspension values written', 'spring 0.001 to 1, damping 0.01 to 1', '', "Havok's range; the game holds both at 1"],
             ['Damper curve', 'n 1, 0.6, 1.6; G = L under 0.005 m/s', 'm/s', 'not stated'],
             ['Spring and zone blends', '0.05 and 0.1 of the travel', '', 'not stated'],
             ['Attach point shift', '±0.12', 'm', "the game's DeviationMax range"],
             ['Sway bars', "0 to 0.3 of the spring rate, steps of 0.01; an axle's two wheels within 0.2 m", '', 'above 0.3 they swing'],
             ['Camber', 'gain x0 to x3, steps of 0.05; at rest -10 to 10 degrees, steps of 0.5, held to ±30', '', "the game's parser holds the angle to ±30"],
             ['Air suspension', 'rest taken after 1 s under 0.1 m/s; τ 3 s; under 10 km/h; keys 2 cm, within 10 cm, under 20 km/h', '', "the author's 10 km/h"]]
    return f'<h2>Constants at a glance</h2>{table(["Constant", "Value", "Unit", "Where it comes from"], rows, "The models\' constants", "small")}'


def body():
    return ''.join([s_intro(), s_overview(), s_scales(), s_gears(), s_rpm(), s_torque(), s_powertrain(), s_auto(), s_coolant(),
                    s_damage(), s_boost(), s_range_air_tilt(), s_suspension(), s_lamps(), s_outside(), s_hooks(), s_shared(), s_files(), s_rate(), s_sounds(),
                    s_limits(), s_constants()])
