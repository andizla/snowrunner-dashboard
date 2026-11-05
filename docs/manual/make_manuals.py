# Builds the Next Gen Gameplay manual (English) as HTML and prints it to PDF with Edge, into out\manual\. The version in the
# output's name is the one in src\gauges.h. Numbers in the tables are computed from the mod's own values, not typed by hand.
# The Russian manual: make_manuals_ru.py. The texts: Part 1 below, Part 2 in tech_gauges.py, the Russian in ru_gauges.py.
#   python docs\manual\make_manuals.py
# At a release the two PDFs are copied into docs\ as Next_Gen_Gameplay_Manual.pdf and Next_Gen_Gameplay_Manual_ru.pdf.
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, 'out', 'manual')
EDGE = r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
VERSION = re.search(r'#define GAUGES_VERSION "([^"]+)"', open(os.path.join(ROOT, 'src', 'gauges.h'), encoding='utf-8').read()).group(1)
GAUGE_IMG = 'file:///' + ROOT.replace('\\', '/') + '/out/preview'

def table(cols, rows, cap='', cls='', first_left=True):
    h = f'<table class="{cls}">' + (f'<caption>{cap}</caption>' if cap else '') + '<thead><tr>' + ''.join(f'<th>{c}</th>' for c in cols) + '</tr></thead><tbody>'
    for r in rows:
        h += '<tr>' + ''.join(f'<td>{c}</td>' for c in r) + '</tr>'
    return h + '</tbody></table>'

# ---------------------------------------------------------------- shared look
CSS = '''
@page { size: A4; margin: 16mm 15mm 16mm 15mm; }
body { font-family: "Segoe UI", Arial, sans-serif; font-size: 10pt; line-height: 1.45; color: #1d2329; background: #fff; }
.cover { border-bottom: 3px solid #c6532a; margin-bottom: 12pt; padding-bottom: 6pt; }
.cover h1 { font-size: 22pt; margin: 0; color: #12171c; }
.cover .sub { color: #5a6570; margin: 2pt 0 0; }
.part { font-size: 15pt; color: #fff; background: #2b333b; padding: 5pt 9pt; margin: 18pt 0 6pt; break-after: avoid; }
.part.break { break-before: page; }
h2 { font-size: 13pt; margin: 14pt 0 4pt; color: #c6532a; break-after: avoid; }
h3 { font-size: 10.5pt; margin: 10pt 0 2pt; color: #12171c; break-after: avoid; }
p { margin: 4pt 0; }
ul, ol { margin: 3pt 0 4pt 15pt; padding: 0; }
li { margin: 1.5pt 0; }
code { font-family: Consolas, monospace; font-size: 9pt; background: #f3f1ee; padding: 0 2pt; }
pre { font-family: Consolas, monospace; font-size: 8.8pt; background: #f3f1ee; border-left: 3px solid #c6532a; padding: 5pt 8pt; white-space: pre-wrap; }
.box { background: #eef4f8; border-left: 3px solid #3a7fb0; padding: 5pt 9pt; margin: 6pt 0; }
.warn { background: #fbf4e6; border-left: 3px solid #e0a63a; padding: 5pt 9pt; margin: 6pt 0; }
table { border-collapse: collapse; width: 100%; margin: 7pt 0 6pt; font-size: 9pt; break-inside: avoid; }
caption { text-align: left; font-weight: 600; padding-bottom: 3pt; color: #12171c; }
th { background: #2b333b; color: #fff; font-weight: 600; padding: 3pt 5pt; text-align: left; }
td { padding: 2.5pt 5pt; border-bottom: 1px solid #e3e1dd; vertical-align: top; }
.dim { color: #7a858f; font-size: 8.3pt; }
figure { margin: 8pt 0; break-inside: avoid; }
figure img { max-width: 100%; border: 1px solid #d8d4cd; }
figcaption { font-size: 8.5pt; color: #5a6570; }
.toc { columns: 2; font-size: 9.5pt; }
.toc li { break-inside: avoid; }
.toc { margin-left: 0; } .toc li { list-style: none; }
.toc .grp { font-weight: 600; color: #c6532a; margin-top: 3pt; }
.intro { color: #3d454d; margin: 6pt 0 10pt; }
.sect { font-size: 11.5pt; font-weight: 600; color: #2b333b; border-bottom: 1.5px solid #2b333b; margin: 14pt 0 4pt; padding-bottom: 2pt; break-after: avoid; }
.sect.break { break-before: page; }
.eq { font-family: "Cambria Math", Cambria, "Times New Roman", serif; font-size: 11pt; margin: 5pt 0 5pt 16pt; break-inside: avoid; }
.eq .n { float: right; font-family: "Segoe UI", Arial, sans-serif; font-size: 8.5pt; color: #7a858f; }
.where { font-size: 9pt; margin: 2pt 0 6pt 16pt; color: #3d454d; }
.src { font-size: 8.3pt; color: #7a858f; }
table.k td:first-child { font-family: Consolas, monospace; font-size: 8.6pt; white-space: nowrap; }
table.small { font-size: 8.4pt; }
figure svg { display: block; border: 1px solid #e3e1dd; background: #fff; }
'''
def page(title, body):
    return f'<!doctype html><html lang="en"><head><meta charset="utf-8"><title>{title}</title><style>{CSS}</style></head><body>{body}</body></html>'

# ---------------------------------------------------------------- gauges manual
def gt(cols, rows, cap=''): return table(cols, rows, cap)
GAUGES = f'''
<div class="cover"><h1>Next Gen Gameplay</h1><p class="sub">Manual for version 0.18.0 (beta), October 2026</p></div>
<!--TOC-->

<p class="intro">Part 1, for dummies, says what the mod does and how to use it, first the basics and then feature by feature. Part 2, the technical reference, gives the models behind it: the formulas and constants and where each number comes from, how the mod gets into the game and reads and writes its memory, and what it cannot do.</p>
<div class="part">Part 1. For dummies</div>
<div class="sect">Getting started</div>

<h2>1. What it is</h2>
<p>A gauge cluster for the truck you drive, drawn over the game in the game's own font: speedometer, tachometer with the gear, fuel with the range, coolant and oil temperature, boost, brake air and up to ten lamps. The game has no engine speed, no range, no temperatures, no boost and no brake air, so the mod works those out from what the truck does, and the temperatures can cost the engine, as real ones do. Two switches go further: an automatic gearbox of the mod's own, and an engine model that changes how the truck pulls.</p>

<h2>2. What you need</h2>
<ul><li>SnowRunner for Windows: Steam, Epic Games Store or Microsoft Store. It is made on the Steam build; on other builds it finds its places by the game's code, looks again for a minute where it cannot, and stands down where it still cannot.</li>
<li>An ASI loader, which loads the mod: the <code>version.dll</code> in the release, the same file as Next Gen Tire Inflation's.</li>
<li>ReShade 6.8.0, which comes in the zip as <code>dxgi.dll</code> and <code>ReShade.ini</code>, the same files as Next Gen Tire Inflation's, for compatibility with overlays such as RivaTuner, Discord, Nvidia and Steam. It shows nothing of its own; Home opens its window. A ReShade already in the game works as it is (any ReShade from 6.8.0 on with add-on support). Without ReShade the game can crash with the RivaTuner or Discord overlay or with multiple monitors, and the mod says so at the start.</li></ul>

<h2>3. Install</h2>
<ol><li>Close the game.</li><li>Copy <code>Gauges.asi</code>, <code>version.dll</code>, <code>dxgi.dll</code> and <code>ReShade.ini</code> into the game's <code>Sources\\Bin</code> folder, next to <code>SnowRunner.exe</code>. A <code>version.dll</code> already there from Next Gen Tire Inflation is the same loader: keep it, and its <code>dxgi.dll</code> and <code>ReShade.ini</code> are the same files as this release's.</li>
<li>Start the game. F2 opens the NEXT GEN menu, which has a Gameplay tab.</li></ol>
<p>On Linux (Proton, the Steam Deck) set SnowRunner's launch options in Steam (right click, Properties, General) to <code>WINEDLLOVERRIDES="version=n,b" %command%</code>. Then the game takes this <code>version.dll</code> instead of Wine's own. The mod there is not yet tested by a player.</p>
<div class="box"><b>Already using ReShade?</b> Choose Skip for <code>dxgi.dll</code> and <code>ReShade.ini</code> when Windows asks, and yours stays. The mods draw without ReShade too. It is in the zip for RivaTuner users, whose game does not boot without it. With Spotter installed, the overlay draws through Spotter's frame hook; <code>NextGen.ini</code> <code>[Overlay] Spotter=0</code> turns it off. To update, copy the new file over the old one. Up to 0.15.11 the mod was <code>Gauges.addon64</code>, loaded by ReShade: delete it when you install <code>Gauges.asi</code>; with two files of the mod only one runs, and the log says which. <code>Gauges.ini</code> keeps working.</div>

<h2>4. What you see</h2>
<figure><img src="{GAUGE_IMG}/strip_states.png"><figcaption>The default layout and style, in three states: parked with the engine cold, cruising in automatic, and a hot, tilted truck in L+.</figcaption></figure>
<p>From the left: coolant (with the oil as an amber needle), fuel with the range under it, speed, the lamps, the engine speed with the gear, boost, brake air when it is switched on, and a clock with the game's time of day and the outside temperature in its face. The cluster shows only while you drive: not on loading screens, in the pause menu, on the map or in the garage.</p>

<h2>5. The settings worth knowing</h2>
<p>All in the Gameplay tab of the NEXT GEN menu, in eight tabs, and they save themselves into <code>Gauges.ini</code>. F2 opens and closes the menu; to give it another key, click the key's name at the menu's top right and press the new one. The key is kept in <code>NextGen.ini</code>, which the Next Gen mods share, and while the menu is open the game gets no mouse and no keys, and the cluster's toggle key and pad toggle rest. With Next Gen Graphics installed the menu also has a Graphics tab. It switches Graphics' effects while you play. Its F keys are off then; KeysWithMenu=1 in SnowRunnerShadows.ini keeps them.</p>
<ul><li><b>Display</b>: the cluster's layout, style, size and opacity, which dials show, the tilt lamp's angles and the units (km/h or mph, bar or psi, Celsius or Fahrenheit).</li>
<li><b>Engine</b>: this engine's profile and Engine torque; the Truck class and the scales (truck, scout or heavy), kept for this kind of truck when set while driving it, with Class beside a number to give it back; the coolant and the oil; Map climate, which makes the temperature follow the map and the hour of the game's day; and Damage, on by default: overheating or working a cold engine hard adds to the game's own engine damage. Switch it off to only watch.</li>
<li><b>Gearbox</b> (<code>Gearbox</code>, Game's gearbox by default): Mod automatic is smarter shifting with a torque converter and a kickdown. Mod gearbox is the mod's own gearbox on the keyboard, a pad, a wheel or a shifter, with a clutch if you want one (section 14). Creep (<code>Creep=1</code>, on by default): an idling truck in D or the game's automatic moves at walking pace; hold the brake to stand. A gear picked by hand creeps only with Creep in held gear (<code>CreepManual=1</code>, off by default).</li>
<li><b>Power</b>: Real engine (<code>Powertrain=1</code>, off by default) makes the truck pull with the mod's engine. Engine share sets how much; lower it if a truck pulls too hard. Air drag and the pull test are here too.</li>
<li><b>Suspension</b>: tests of the springs, the dampers, the ride height and the sway bars.</li>
<li><b>Controls</b>: Driven with (wheel pedals, a pad or the keyboard), the toggle key and the pad toggle, and every binding.</li>
<li><b>Sounds</b>: how loud the turbo and the oil warning are.</li>
<li><b>Mod work</b>: demo values, the probe log, Defaults and Reload ini.</li></ul>
<p>The menu opens on the tab open last (<code>Tab</code> in the ini).</p>

<h2>6. When something is wrong</h2>
<ul><li><code>Gauges.log</code> in <code>Sources\\Bin</code> says what the mod found and did. The newest 20 earlier starts' logs are kept in <code>Gauges\\logs</code> (<code>KeepLogs</code>).</li>
<li>When another program holds the game's swap chain in a way the mod cannot draw on, <code>Gauges.log</code> names that program.</li>
<li>The game crashes without ReShade with the RivaTuner or Discord overlay or with multiple monitors: put ReShade in from the release (Install, step 2). With ReShade the same PCs ran fine.</li>
<li>Support, discussion and ideas: the author's channel on EnthusiastRunner's Discord, <a href="https://discord.com/invite/MssDb2Pvcj" target="_blank" rel="noopener noreferrer">discord.com/invite/MssDb2Pvcj</a>, or an issue on GitHub for a bug report; <code>GrabDumps.bat</code> from the zip gathers the logs, the ini files and the crash dump into one zip for it.</li>
<li>To remove the mod, delete <code>Gauges.asi</code>, <code>Gauges.ini</code>, <code>Gauges.log</code> and the <code>Gauges</code> folder from <code>Sources\\Bin</code>. <code>NextGen.ini</code> and <code>version.dll</code> go too when no other Next Gen mod is left. ReShade from the release goes the same way when nothing else uses it: <code>dxgi.dll</code>, <code>ReShade.ini</code> and <code>ReShade.log</code>.</li>
<li>Leave <code>Damage</code> off in co-op; it has not been tried there.</li></ul>

<div class="sect break">Feature by feature</div>

<h2>7. The dials one by one</h2>
<ul><li><b>Speed</b>, forwards and backwards.</li>
<li><b>Gear</b>: its number with a D in automatic, H for the high gear, L, L+ and L- for the low range, R and N.</li>
<li><b>Engine speed.</b> Worked out from the driven wheels, the gear and the pedal. The needle climbs through each gear to the red zone and falls at the shift, rises under the pedal when the truck is stuck, revs freely in neutral, and spinning wheels take it into the red zone. It rises from 0 while the engine starts and runs down when it stalls or stops.</li>
<li><b>Fuel</b> as a share of the tank, and the <b>range</b>: how far the fuel goes at the litres per kilometre burned over the last three kilometres. It shows <code>---</code> until the truck has driven half a kilometre; fuel burned standing still comes off the range without spoiling the rate. SnowRunner's trucks are thirsty: a scout burning 10 litres a kilometre off road gets about 7 km from a 75 litre tank. After the range the <b>fuel use</b>: the litres a minute the game burns now, smoothed over a second (gallons an hour with Mph; <code>ShowFuelUse</code>, the tab's Fuel use).</li>
<li><b>Coolant and oil</b> (section 9), <b>boost</b> (section 10), and <b>brake air</b>: the engine's compressor fills the tanks to 8.2 bar and starts again at 6.9; each press of the brake and letting the handbrake go draw from them. Scouts brake by hydraulics: the air dial is hidden on scout scales.</li></ul>
<figure><img src="{GAUGE_IMG}/closeup_open.png"><figcaption>A close look at the open style.</figcaption></figure>

<h2>8. The lamps</h2>
{gt(['Lamp', 'Lights when'], [['Handbrake, headlights, all wheel drive, differential lock, low range', 'The truck\'s own state'],
 ['Engine damage', 'The game\'s engine damage is above half of what the engine takes; blinks while the mod adds damage and for two seconds after'],
 ['Cold engine', 'Coolant below 60 °C'], ['Oil', 'Blinks from 120 °C oil, with a warning every four seconds'],
 ['Tilt', 'A small truck that leans as yours does: green level, amber from 15° to a side, red from 25°, or from 35° up or down a slope; a bump does not light it'],
 ['Tire (with the tire mod)', 'The pressure mode\'s letter; amber at Low and Reduced, green at Increased, blinking while the pressure moves, red while tires take damage or one is worn out']], 'Table 1. Lamps')}

<h2>9. Coolant, oil and engine damage</h2>
<h3>Coolant</h3>
<p>Worked out from the fuel the engine burns and how fast the truck drives. A real engine's coolant takes about a quarter to a third of the fuel's heat; the fuel goes with the pedal and the engine speed together, so floored at the red zone makes the most heat and coasting with the foot off next to none. In neutral the engine makes a quarter of the heat.</p>
<ul><li>A cold engine starts at the outside temperature and reaches 85 °C after about three minutes of driving. The thermostat then holds about 86 °C at idle and near 100 °C working hard at speed.</li>
<li>The red zone starts at 118 °C, reached only working hard at a crawl: floored at a standstill with the wheels spinning it takes a minute and a half from 90 °C. From about 7 km/h up the needle stays out of the red zone.</li>
<li>Water cools it: from about half a metre at the truck's front, and with the engine in it (1.2 m and more) a hot engine is back at 100 °C within half a minute.</li>
<li>A stopped engine cools from 90 °C to 40 °C in a quarter of an hour, five times as fast in deep water. Each kind of truck keeps its own temperature while the game runs. After a loading screen the truck that drives on keeps its heat (a travel, a border crossing, a reload); a game start, the garage, a recovery or another truck makes every engine cold again.</li></ul>
<h3>Oil</h3>
<p>The oil takes the heat of friction in the bearings and pistons and gives it to the coolant through the oil cooler, so it follows the coolant slowly and runs a few degrees over it: about 7 over at cruise and 14 floored at the red zone. Driven as usual it is at 85 °C after about five minutes, never before the coolant. It warns from 120 °C (<code>OilWarnC</code>).</p>
<h3>Engine damage</h3>
<ul><li>In the red zone an engine loses 10% of what it can take every minute at 125 °C, nothing at 118 °C (<code>HotDamageFrom</code>, the Engine tab's Damage start, moves that start between 100 and 124 °C): floored at a standstill with the wheels spinning, an engine is used up about five minutes after it enters the red zone.</li>
<li>Below 60 °C, working hard (burning more than half the fuel it burns floored at the red zone) costs up to 5% a minute, less the warmer and the lighter the work. A cold start floored at the red zone costs 2 to 3%; driving about at moderate revs costs nothing.</li>
<li>The Engine tab's Hot damage and Cold damage set both rates: up to 100% a minute for overheating and up to 60% for a cold engine. Gauges.ini takes up to 100 for both.</li>
<li>All of it is the game's own engine damage: a garage repairs it, and past the engine's threshold the game takes torque away and burns more fuel. The mod stops one point short of what the engine takes and never stops the engine itself.</li></ul>
{gt(['Setting', 'Default', 'What it changes'], [['WarmUpMinutes', '3', 'Minutes from 15 °C to 85 °C driven as usual'], ['OverheatMinutes', '1.5', 'Minutes from 90 °C into the red zone, floored and stuck'],
 ['CoolDownMinutes', '15', 'Minutes a stopped engine takes from 90 °C to 40 °C'], ['AirFullKmh', '30', 'Speed from which the radiator gets all the air driving gives'],
 ['OilWarmUpMinutes, OilWarnC', '5, 120', 'Oil warm-up time; too hot from'], ['HotDamagePercentPerMinute, ColdDamagePercentPerMinute', '10, 5', 'Damage rates; 0 switches each off'],
 ['Damage', '1', 'The temperature costs the engine']], 'Table 2. Heat settings')}
<p>Each of the three times moves one thing only; the temperatures the engine settles at stay. The outside temperature moves those: in frost the engine warms slowly and cannot be overheated, on a hot day it overheats sooner.</p>

<h2>10. Boost and the turbo</h2>
<p>The mod runs a turbo of its own. Its rotor spools by the fuel the engine burns and how fast it turns: slowly low down and at a light foot, quickly high up. A truck's diesel takes about 2 s to its boost at 1100 rpm and under a second at 1760. Floored, a truck's diesel makes all its boost from about 1300 rpm and a scout's petrol from about 1650; at part load the boost goes by the fuel. On a short lift the rotor keeps turning, so the boost is back at once; it runs down over a second and a half. While the turbo spools the engine has the torque the air allows, about 0.43 of it at the first instant on a 1.8 bar truck; once the boost has built it has all of its curve. In neutral the free engine makes little boost, and on the governor it falls with the fuel.</p>
<p>Trucks and heavy trucks are diesels and scouts petrol, unless an engine's profile says otherwise (<code>Fuel</code>, section 15). The dial reads the manifold's pressure: a diesel's from 0, a petrol engine's from -1 bar (-15 psi), as its throttle holds the manifold in vacuum at idle (about -0.7 bar) and at a light foot. It ends at 2 bar (30 psi), or at 3 bar (45 psi) for a turbo of more than 2.</p>
{gt(['BoostProfile', 'Character'], [['0 standard', 'The class\'s own, as above'], ['1 small and quick', 'Full sooner, half the lag'], ['2 big and slow', 'Full later, half as much lag again'], ['3 two stages', 'Full sooner with 1.3 times the boost, a short dip at the changeover']], 'Table 3. Turbo profiles')}
<p><code>BoostMax</code> sets how much boost the turbo reaches (0.3 to 3 bar). A naturally aspirated engine (section 16) has no boost, no dial and no turbo sounds.</p>

<h2>11. Scales by truck class and per truck</h2>
{gt(['', 'Truck', 'Scout', 'Heavy'], [['Idle', '650 rpm', '800 rpm', '600 rpm'], ['Red zone from', '2200 rpm', '5500 rpm', '1900 rpm'], ['Tachometer top', '3000 rpm', '7000 rpm', '2500 rpm'], ['Speedometer top', '100 km/h', '120 km/h', '80 km/h'], ['Boost', '1.8 bar', '1 bar', '2 bar']], 'Table 4. Scales by class')}
<p>Each kind of truck can have its own seven: the class, idle, the red zone, the tachometer's top, the speedometer's top, the boost and the turbo profile. Change one on the Engine tab while you drive a truck and all seven are kept for that kind of truck, in a file of its own in <code>Gauges\\trucks</code>, named as the game's files name the truck (a modded truck's <code>name/mod id</code> becomes <code>name@mod id.ini</code>). The file can be shared and dropped into another player's folder. "Plain scales" gives them up. Every other truck shows the plain scales of <code>[Gauges]</code>.</p>
<pre>[Truck]
Kind=chevrolet_ck1500
TachClass=1
RedlineRpm=4500
BoostProfile=2</pre>
<p>A truck without its own class shifts, in the mod's automatic, by the class of its weight from the game's files: a scout up to 6 tonnes, a truck under 16, a heavy truck above. Some modded trucks carry a weight that is too high and land in the wrong class: set their class by hand.</p>

<h2>12. The outside temperature by map</h2>
<p>The mod reads which map you are on from the game's save (Steam, Epic and Microsoft Store saves) and takes the temperature from a section named after the map or its region, using the game's English names; a modded map goes by its id.</p>
<pre>[Map:Alaska]
AmbientC=-12

[Map:Scandinavia]
DayC=3
NightC=-12</pre>
<p>The air follows the game's day: coldest just before sunrise, warmest in the afternoon. A section with only <code>AmbientC</code> swings 10 °C around it. Without a section, or before the save is read, <code>AmbientC</code> of <code>[Gauges]</code> is in force; <code>AmbientByMap=0</code> uses it on every map. While the map's temperature is in force, the tab's slider sets it in the section it comes from. The tire mod reads the same temperature.</p>

<h2>13. The mod's automatic (Gearbox=1)</h2>
<p>The game's automatic shifts up at fixed speeds whatever the pedal, never shifts down under load until the engine bogs, and then drops to first. The mod's automatic takes the gears while the game's shifter is in A:</p>
<ul><li>A torque converter: floored at a standstill the engine stops at the stall speed (0.8 of the red zone, at most 2600 rpm) and climbs from there through the launch; on the move it follows the road speed with a little slip. A press never opens a locked converter: one that needs more pull gets a shift down. At idle in gear it creeps to about walking pace with the needle at idle (<code>Creep</code>, section 14). It locks up from first gear above its lock point and stays locked on a lift, so the engine brakes the truck; it opens near idle, at a stop and for the launch.</li>
<li>Shifts up later the further the pedal is down. A light foot held steady cruises: it shifts up as soon as the next gear lands at 0.8 of the engine's peak torque speed or over, so the engine settles low. Lifting at speed shifts up a gear at a time; rolling downhill or slowing hard it keeps the gear. Coasting holds the gear down to 0.15 of the way from idle to the red zone (0.25 with the brake on, so the engine brakes), then shifts down, gears at a time when the truck slows hard. Under the brake it comes down to the gear for the speed at once,
skipping gears, with no pause between the shifts. Floored it takes the gear that pulls the most in the game.</li>
<li>On a climb it shifts down where floored would and holds the gear a few seconds, so it does not hunt.</li>
<li>A kickdown is a push from part throttle to the floor: it drops at once to the gear that pulls the most, as far as the engine can take. Wheelspin and locked wheels do not shift it.</li>
<li><code>Input</code> fits the kickdown to what you drive with: on a pad the push has to come from under three quarters; the keyboard has no part throttle, so a held key shifts as floored and kicks nothing down. With the keyboard a lift shifts up only once the key has been let go for 2 seconds, and if the next press takes that shift back, lifts shift nothing up for 10 seconds (then 20, up to a minute), so tapping the key to hold a speed keeps the gear.</li>
<li>Picking a position by hand, L+ among them, gives the gearbox back to you until A is picked, which takes it again. So the automatic never shifts out of L+, and the game keeps the diff lock there.</li></ul>

<h2>14. The mod's own gearbox (Gearbox=2)</h2>
<p>With <code>Gearbox=2</code> the mod's gearbox takes the shifter from the moment you drive a truck, and the game's own shifts are refused. Its positions are D (the automatic of section 13; the game's shifter and the ini call it A), the numbered gears, N, R, H and the low range L-, L and L+. Up and down walk the row R, N, L-, L, L+, 1, 2 and on to the top gear, as SMT's did (L+ is the first gear at full power). Up from L+ goes to the 1st a second after an up press got there, so a quick run of presses up from R stops in L+. A 1st reached from the low range (by up, RangeHigh or the key 1) stays held until your next shift, so the low range never goes on to D by itself. That 1st is L+ in all but its name and the low gear lamp: what it gives is the walk on to 2nd. On a pad RangeHigh has no button, so Gear up (Pad X) or D (Pad B) leaves it forward, and a held gear comes from D by Gear up. Back from the map, the garage or the viewer the position stays while the game still has the gear and the power the mod set. A gear key sets its gear; A hands the choice to the automatic until another position is picked. A or a gear picked from R while the truck still rolls back waits in N until it stands.</p>
<ul><li><b>Bindings</b>: each action has two, set on the Controls tab by a click and then a press. A control already held or a hat already pointing when you click is left alone until you let it go. A binding is a key, a pad's buttons together or a stick's direction with a button (LB and the right stick up is <code>Pad LB+RSUp</code>), a wheel's or shifter's button or hat, or a pedal's axis for the clutch. The game steers with the left stick and turns the camera with the right, so bind a direction with a button held. The defaults are on the number pad: Num 8 up, Num 2 down, Num 5 N, Num 0 R and Num 7 D. A quick tap shifts. Gear1 to Gear16 cover gearboxes of up to 16 gears; a gear key over the truck's top gear picks its top gear. The first time the mod finds <code>SMT.ini</code> and no binding of its own, it takes SMT's keyboard bindings and options over once (its IMMERSIVE MODE as DownStaysForward).</li>
<li><b>Locks</b> (Locks and ride on the Controls tab): the differential lock and all-wheel drive on a key, on every truck, the upgrade fitted or not, at every <code>Gearbox</code> level. No key by default.</li>
<li><b>Options</b>, off by default: <code>RequireClutch</code> (a shift into a gear with the clutch not held: at about 5 km/h or more the gear grinds and stays out in N, standing or crawling it stalls the engine), <code>RequireGearHeld</code> (N the moment no gear key is held, for an H shifter), <code>SkipNeutral</code> (from L- to reverse and back without stopping in N), <code>DownStaysForward</code> (Gear down stops at L-, the lowest forward gear, N and R only on their own keys or the game's shifter, as SMT's immersive mode did; it wins over SkipNeutral), <code>IgnoreShifter</code> (the game's shifter picks nothing, with any <code>Input</code>: only the bindings shift, and until one of them reaches reverse, 2 runs as 1).</li>
<li><b>Back to D</b> (<code>BackToA</code>, in seconds: 0 never, the default, else 3 to 30). After you shift by hand
into a numbered gear (Gear up, Gear down or a gear key), the gearbox goes back to D by itself once that many seconds pass with
no other shift by hand, and then only once everything below has held for a second without a break:
<ul><li>the pedal under 0.7, unless D's gear is the one you hold (then the return is no shift, and the foot may stay down);</li>
<li>the wheels gripping, not spinning (the mod learns their grip in the held gear as you drive; floored, steady wheelspin does not
count as grip);</li>
<li>no climb under load (the nose up more than 8° with the pedal at 0.4 or more) and no slope down (the nose down more than 5°
while moving), so a low gear held for engine braking stays;</li>
<li>not rolling back;</li>
<li>D's gear the held one or next to it, so the return is one shift (standing with the foot off, any gear);</li>
<li>no diff lock or AWD switched in the last 3 s, and the handbrake off;</li>
<li>the engine running and no shift under way.</li></ul>
Each shift by hand starts the time again, and the D key (<code>GearA</code>) still goes back at once. Either way D starts in the gear it would pick, not in 1st; Back to D keeps that next to the held one. The grip is learned while
you drive with the pedal under 0.7, and on the keyboard a key held counts as floored: a truck taken straight into a held gear
learns it once it has driven a moment in D, or with a pad's lighter trigger, and until then the log says "the wheels' grip not
learned yet". It never returns from R, N, H or the low range,
never with <code>RequireGearHeld</code>, and never for a gear picked on the game's shifter, which stay where you put them. The
<b>Sequential</b> button on the Gearbox tab sets <code>RequireClutch</code> off, <code>RequireGearHeld</code> off and
<code>BackToA</code> 10: shift up and down by hand on a pad, and the gearbox goes back to D by itself. <code>Gauges.log</code>
says what holds the return and when it comes. With <code>BackToA</code> on, and not with <code>RequireClutch</code> or
<code>RequireGearHeld</code>, a held gear also goes back to D by itself, as a real automatic's manual mode does, and D picks the
gear: near the red zone with the foot on (after 0.3 s, not while the wheels spin and not before the mod knows their grip), and
before a stall (the gearbox turning under 1.1 times idle for 0.5 s, the foot on or off, 0.2 s under the brake). So braking hard
in a held 6th drops straight to the gear for the speed. With the foot off at the red zone it stays, so a low gear held down a hill
keeps its engine braking. It never moves a gear picked on the game's shifter, and the log says "sequential: up at the red zone"
or "down before a stall", with D's gear.</li>
<li><b>Damage</b>, with <code>Damage</code> on: a stall from a shift without the clutch (it comes once the new gear is in, on the new gear) costs the gearbox <code>StallDamagePercent</code> of what it takes (5 by default). A clutch slipping under load heats up and, once red, costs the gearbox too (<code>ClutchBurn</code>, on by default; its pace is <code>ClutchHeatPercent</code>, 60). A box at the top left warns: amber CLUTCH HOT, then red CLUTCH DAMAGE. The clutch heats fully only at a crawl; from 15 km/h up a lugging engine stalls the game's own way instead. A repair clears the heat. A box of the same shape warns for the engine: amber ENGINE HOT, red ENGINE DAMAGE.</li>
<li><b>Creep in a hand-picked gear</b> (<code>CreepManual</code>, the Gearbox tab's "Creep in held gear", off by default). Off, as since
1 November (the author's call), the creep goes only where an automatic picks the gear, the mod's D (with the reverse and the range it
drives there) or the game's own automatic; a gear picked by hand gets none, on either shifter and an R included, as the game turns
its automatic off to shift into R. On, a gear picked by hand creeps too, as the next bullet says: the governor, the holding and at
<code>Gearbox=2</code> the stall; R, L- and L on the game's shifter at Gearbox 0 and 1 through the converter. In Sequential (Back to D
above 0) a held gear stuck goes back to D before it can stall, so the stall comes only with Back to D at 0. Either way it does not
push through one of the mod's shifts, from the shift until 1.5 s after the new gear is in. It is off while it is tried out: if the
game closes with it on, the log's ExitProcess or TerminateProcess line names what closed it.</li>
<li><b>Creep</b> (<code>Creep=1</code>, on by default, at every <code>Gearbox</code> level): an idling truck in gear moves and stands only on the brake or the handbrake. Through a converter (the game's automatic, D, and R, L- and L on the game's shifter at Gearbox 0 and 1) it creeps to about walking pace, in R and the low range with a push smaller by the gear and paced so it does not surge, and nothing from 0.87 of the gear's idle speed (the wheels' speed with the engine at idle). In a gear picked by hand, the game's or a position here with the clutch up, an idle governor holds about the gear's idle speed, less as the clutch is pressed; a gear too tall to pull at idle lugs and stalls. The governor comes in over about half a second, gives less in the low range and eases off before the wheels reach the idle speed, so in a low gear on soft ground the wheels and the needle stay steady. Rolling, it holds the gear's idle speed in any gear on the flat, up to the engine's torque at idle. In the mod's own gearbox (Gearbox=2), in a position picked by hand, the engine stalls as a manual one's does: stuck, the wheels under a quarter of the gear's idle speed for a second once the game's pull has built, or lugged under 0.6 of it by a climb or mud. Hold the clutch, pick neutral or brake to keep it running. On the automatic (Gearbox 0 or 1, R and the low range on the game's shifter too) and within a second of a shift no stall comes: the governor pushes and the engine idles on. In D the converter takes it and the engine never stalls. The mod raises only the throttle the game hands the wheels, for the truck you drive: the pedal, the game's engine speed, fuel and sound stay at idle. Nothing in N, with the engine off or starting, in a menu or for another truck. Its strength (0.15 of the throttle at a standstill) is a first figure; <code>Gauges.log</code> has a line as each creep begins and ends.</li>
<li><b>Levels</b>: 0 is the game's own gearbox, none of its gearbox functions hooked; 1 the mod's automatic in A with the game's shifter yours, where only the reverse the game's automatic puts in on the brake is refused; 2 the mod's own gearbox. Driven with a pad or the keyboard (<code>Input</code> 1 or 2) and <code>IgnoreShifter</code> off, the game's shifter picks the position too: moved to R, N, H, A, a gear or the low range, it counts as that position's key (L+ reads as the first gear). With a wheel's pedals (<code>Input</code> 0), or with <code>IgnoreShifter</code>, the game's shifts stay refused and the bindings pick the position; until a binding that can reach reverse is there, 2 runs as 1. While the NEXT GEN menu is open the gearbox stands where it is.</li>
<li><b>Gears</b>: gearboxes of up to 30 gears are read, with their reverse and high gear, so a 16-speed drives in every
position and shows its fuel a minute. A longer list is logged once a truck, and its gears are not read.</li>
<li>The manual transmission mod (SMT) is no longer needed and not compatible: remove <code>SMT.addon64</code> or <code>SMT.asi</code> and <code>SMT.ini</code>. The mod says so when it finds it installed.</li>
<li>Not tested on a wheel or shifter yet; the keyboard and a pad were driven. A downshift at speed does not yet over-rev the engine, and in a manual position the engine pulls by the mod's curve at the revs it is at, so a high gear at low revs pulls less than A does.</li></ul>

<h2>14. The mod's engine (Powertrain=1)</h2>
<p>The game gives all of an engine's torque with the wheels at about half the gear's top speed and less and less to nothing at the top, whatever the engine speed. With <code>Powertrain=1</code> the truck pulls with the mod's engine instead: the torque curve against the engine speed the needle shows, doubled by the converter at its stall with the automatic. Cold oil (up to 15% at 0 °C) and a turbo without boost (35% of the torque comes from it) cost torque only where the pedal asks for more than they leave. The torque holds to about 0.86 of a gear's top speed (a scout's 0.8), then falls to nothing at the top, as a diesel's governor cuts the fuel; a gear's top speed stays the game's. This holds in a gear held by hand at <code>Gearbox=2</code> too, so a held gear pulls to the red zone. There, with the clutch fully
in, the engine is locked to the wheels and the torque goes by their own speed: spinning wheels in mud get the engine's torque and
spin up toward the red zone. By the wheels' speed it is held to twice the engine's torque, so wheels that catch grip do not lurch.</p>
<p><b>Air drag</b> (<code>RoadLoad</code>, the Power tab's "Air drag", off by default, with <code>Powertrain=1</code>). The game has no air drag,
so a light truck runs to its gear's top as if in a vacuum. With this on the mod takes the air's drag off the engine's pull, more the
faster the truck goes: about 0.5 kN at 100 km/h for a pickup. It needs each gear's pull at the game's own torque, which the mod learns
by itself when you drive that gear floored for a couple of seconds in all, over as many visits as it takes, with the wheels up to 0.8 of
the gear's top and on grades up to 3°. Until then that gear has none, unless two learned gears agree within 15 % (their pull times
their top): then it goes by them, which gives the top gears their drag. The drag's area goes
by the truck's weight (1.1 m² under 4 t, 3.5 under 10 t, else 6) unless <code>RoadCdA</code> sets it, in <code>[Gauges]</code> or a
truck's file. The weight is the truck's own, without cargo or a trailer, so with a load the drag taken off is too much. Rolling resistance is the game's on soft ground, and on firm ground the game has next to none.</p>
<p><code>EngineShare</code> (50) sets how much of the mod's engine the game gets: at 50 the truck pulls half way between the game's engine and the mod's, at 100 all the mod's. With the engine handed over, the automatic shifts as a truck automatic does:</p>
<ul><li>Floored, each gear to the red zone, or sooner where the next gear already pulls harder. At a light foot it shifts up about as far along as the pedal is down, never under 0.8 of the engine's peak torque speed.</li>
<li>Down where the gear below comes in clear of its own point up, so it does not shift straight back.</li>
<li>One gear up at a time, each held at least 2.5 seconds of moving (a scout 1.5). The Gearbox tab's "Upshift delay" slider (1 to 3 s) sets it per gearbox, in <code>Gauges\\gearboxes\\&lt;name&gt;.ini</code>. If a truck accelerates too hard, raise it, and check the truck's class.</li>
<li>It sets off in the lowest gear with at least a seventh of the top gear's speed, lower where the engine is weak for the truck's mass, and takes the crawling gears only when stuck.</li>
<li>A shift up that the next gear cannot hold, up a grade or through mud, is not tried again for eight seconds, then each failed try doubles the wait, up to 30.</li></ul>
<p>It works through the game's torque factor, the share of torque the game leaves a damaged engine: written every pass, at most six times the game's own (the game's own with the wheels at a gear's top or past it), times the engine's Torque share where one is set, and handed back when the setting goes off, the engine stops or another truck is driven. A damaged engine still pulls less.</p>

<h2>15. Engine profiles</h2>
<p>Each engine can have its own file, <code>Gauges\\engines\\&lt;name&gt;.ini</code>, set on the Engine tab with a plot of the curve in force:</p>
{gt(['Key', 'What it does'], [['PeakTorqueRpm', 'Engine speed of peak torque, as the maker gives it. A turbo diesel gets a broad plateau from 0.85 to 1.5 times it, three fifths of the torque at half of it and four fifths left at the red zone'],
 ['Aspiration', '0 turbo, 1 naturally aspirated: no boost, all of its torque at once'], ['Fuel', 'diesel or petrol in place of the class\'s (trucks diesel, scouts petrol): a petrol engine\'s boost dial shows vacuum'], ['TorqueCurve', 'A whole curve as x:y points, x the engine speed over the red zone\'s, y the torque over its most, such as 0.3:0.7,0.5:1,1:0.75'],
 ['Torque', "How much of its torque the truck gets with the engine handed to the game, the game's own pull included, 10 to 100 percent (100 when not set; the Engine tab's Engine torque slider). For an RLM-like engine with several times a stock engine's torque: at 25 an engine with four times the torque pulls like the stock one"]], 'Table 5. Engine profile keys')}
<p>The profile goes with the engine, so a truck fitted with another engine in the garage pulls by that one's. The mod also reads the engine's own record from the game: torque, responsiveness, its fastest rev change and its fuel consumption.</p>

<h2>16. The pull test</h2>
<p>The Power tab's Pull test has a Start button: on a long flat road, in fourth gear or higher, floor the pedal for seven seconds. The mod holds the gear, changes the torque factor for a second at a time (halved, raised by half, at three quarters), measures the pull and puts the factor back. Lifting the foot, braking or another gear stops it. The numbers go into the log.</p>

<h2>Suspension (tests)</h2>
<p>The Suspension tab changes how each wheel's spring and damper work, while you drive. Everything in it is a test:
off by default, for every truck at once (not per truck). Set a slider back, or drive another truck, and the truck has its own
suspension again. Under the sliders the tab shows the truck's own figures from its file, front and rear, and what is in force
now.</p>
<ul><li><b>Spring and Damping</b> (<code>SpringScale</code>, <code>DampingScale</code>, x0.25 to x4): the truck's own spring
and damping times a factor. Above 1 the spring is stiffer and the truck sits higher, below 1 softer and lower. More damping
settles the body sooner, less lets it bounce longer. The game takes a wheel's spring and damping only up to 1, so where a
truck's own figure is 0.5 (the Arocs' front damping), a factor above 2 cannot act in full. The tab says so.</li>
<li><b>Extra travel</b> (<code>ExtraTravel</code>, 0 to 30 cm): each wheel can rise further into the body than the truck's
file lets it (its SuspensionMin), on a file that sets 0 too. It never shortens the travel; the bump stops do that. Past the
file's end a wheel can go into the body, and the tab warns.</li>
<li><b>Ride height</b> (<code>RideFront</code>, <code>RideRear</code>, -10 to 10 cm, in whole cm): raises or lowers each
end, as the file's Height does. The middle axles of a long truck go with the rear. The tab shows how far into the travel the
wheels sit at rest.</li>
<li><b>Sway bars</b> (<code>SwayFront</code>, <code>SwayRear</code>, 0 to 0.3 of the axle's spring rate): they hold the body
level in a turn and on a side slope, and let less of the wheels' twist through on a crossing. 0.1 to 0.2 works well. Above
0.3 the bars swing, so a higher value in the ini is held at 0.3. The Sway bars binding (under Locks) disconnects both for a
crawl, and a lamp shows while they are off.</li>
<li><b>Air suspension</b> (<code>AirSuspension</code>): at the first standstill after you take the truck, each wheel's place
is kept as its rest place, so cargo no longer makes the truck sag or lean. It levels only the height, the pitch and the roll,
so a twist over rough ground stays the ground's. It works under 10 km/h and settles in about 3 s. An axle with a wheel
hanging or on its stop keeps its height. Ride up and Ride down (under Locks) raise and lower it 2 cm a press, within 10 cm,
under 20 km/h; the height goes back to 0 when you take another truck. Turned off, it eases back over a few seconds.</li>
<li><b>Level to horizon</b> (<code>AirHorizon</code>, with the air suspension): the body is levelled to the horizon instead
of to the wheels, standing and crawling under 10 km/h, at most about 7 degrees side to side. Over 10 km/h it goes back to the
wheels and keeps the height.</li>
<li><b>Display</b> (<code>SuspDisplay</code>): a row for each axle under the warning boxes. Each wheel's travel is a bar, red
when the wheel hit the compression end in the last second and hollow while it hangs, with the articulation in cm.</li>
<li><b>Damper</b>: the curve (<code>DamperCurve</code>: linear, digressive or progressive) and four factors, compression and
rebound at low and high speed (<code>LowCompression</code>, <code>HighCompression</code>, <code>LowRebound</code>,
<code>HighRebound</code>, x0.25 to x4), split at the knee (<code>DamperKnee</code>, 0.02 to 0.5 m/s, 0.1). Low speed is body
roll, squat and dive; high speed is bumps, rocks and curbs. The presets are starting points: Stock (the truck's own), Trail
(soft at low speed, a firmer rebound), Rock crawl (soft all through, progressive) and Road (firm at low speed, a flat
body).</li>
<li><b>Spring curve</b> (<code>SpringCurve</code>: linear, progressive or dual rate): how the spring stiffens deep in the
travel, up to <code>SpringRise</code> times the truck's own at the end (1 to 3, 2). Dual rate changes over at
<code>SpringCross</code> (0.2 to 0.8 of the travel, 0.5). Near rest it feels the same.</li>
<li><b>Bump stops</b> (<code>BumpStops</code>): a cushion over the last <code>BumpZone</code> metres of the travel (0.03 to
0.2, 0.12, at most a third of it). At the end it pushes back <code>BumpForce</code> times as hard as the spring (1 to 6, 3),
with more compression damping there (<code>BumpDamping</code>, 1 to 4, 2).</li>
<li><b>Damping zones</b> (<code>DampingZones</code>): softer damping in the middle of the travel for small bumps
(<code>ZoneSoft</code>, 0.5 to 1, 0.7, from <code>ZoneFrom</code> 0.1 to <code>ZoneTo</code> 0.6 of the travel) and firmer
toward both ends (<code>ZoneFirm</code>, 1 to 3, 1.5), like a bypass shock.</li>
<li>On a truck whose game moves the spring's attach point by itself (its own active ride height), the ride height, the sway
bars and the air suspension stop for that truck, and the log says so.</li></ul>

<h2>17. Sounds</h2>
<p>The turbo on a lift: on a petrol engine the throttle shuts against the boost and the air beats back through the compressor wheel, slower and fainter until it is gone; the more boost, the longer and louder, never the same twice in a row. A diesel has no throttle plate and no blow-off valve, so its turbo coasts down and its lift is quiet; <code>DieselFlutter=1</code> (Flutter on a diesel) has it flutter, and your own <code>turbo_whistle_1.wav</code> to <code>_4</code> play there. Three strengths with three recordings each. Your own WAV files in <code>Sources\\Bin\\sounds</code> as <code>turbo_high_1.wav</code> to <code>_4</code>, and <code>turbo_mid_</code> and <code>turbo_low_</code> likewise, replace a strength's recordings; <code>oil_beep_1.wav</code> replaces the oil warning. Each time the governor stops the compressor at the cut-out the air dryer purges with a short hiss; <code>air_purge_1.wav</code> to <code>_4</code> replace it. <code>SoundVolume</code> 0 silences them, <code>SoundsOnly</code> plays them without the cluster.</p>

<h2>18. With the tire mod</h2>
<p>Next Gen Tire Inflation and this mod run side by side, each with its own ini and log, their tabs in the one NEXT GEN menu; the same <code>version.dll</code> loads both <code>.asi</code> files. Gauges publishes the truck, the outside temperature and everything its cluster shows in shared memory (<code>Local\\SnowRunnerGauges.State.&lt;process id&gt;</code>); the tire mod takes the temperature from it, and another mod's speedo can draw from it. From the tire mod's version that publishes its own state, Gauges shows the tenth lamp with the pressure mode.</p>

<h2>19. Every setting</h2>
{gt(['Setting', 'In the ini', 'Default'], [
 ['Layout: strip along the bottom, block in the corner, Spotter dials or Spotter digital (left of Spotter\'s gear panel)', 'Layout 0 to 3', 'strip'], ['The speedometer and the tachometer change places, except in Spotter digital (Swap dials)', 'SwapDials 0 or 1', '0'],['Style of the strip and the block: solid, tinted, or open', 'Style 0, 1 or 2', 'open'],
 ['How dark tinted faces are', 'FaceOpacity 0 to 0.85', '0.35'], ['Position in 1080p pixels from the layout\'s place', 'OffsetX, OffsetY', '0, 0'],
 ['Size, opacity', 'Scale, Opacity 0.1 to 1', '1, 1'], ['mph, psi, Fahrenheit', 'Mph, Psi, Fahrenheit', 'off'],
 ['Temperature, boost, air dials, lamps', 'ShowTemp, ShowBoost, ShowAir, ShowLamps', 'on, on, off, on'], ['Clock with the outside temperature (Clock)', 'ShowClock', 'on'], ['Fuel used a minute after the range (Fuel use)', 'ShowFuelUse', 'on'],['Oil: none, second needle, own dial', 'OilGauge 0, 1 or 2', 'second needle'],
 ['Truck class', 'TachClass 0 truck, 1 scout, 2 heavy', 'truck'], ['Own idle, red zone, tach top, speedo top', 'IdleRpm, RedlineRpm, MaxRpm, SpeedMax', '0: the class\'s'],
 ['Boost reached, 0.3 to 3 bar', 'BoostMax', '0: the class\'s'], ['Turbo profile', 'BoostProfile 0 to 3', 'standard'],
 ['Tilt lamp angles', 'RollWarnDeg, RollDangerDeg, PitchDangerDeg', '15, 25, 35'], ['Coolant times', 'WarmUpMinutes, OverheatMinutes, CoolDownMinutes', '3, 1.5, 15'],
 ['Outside temperature, -40 to 50', 'AmbientC', '15'], ['Outside temperature by map', 'AmbientByMap', '1'], ['Radiator full air from, 10 to 80 km/h', 'AirFullKmh', '30'],
 ['Oil warm-up, too hot from', 'OilWarmUpMinutes, OilWarnC', '5, 120'], ['Damage: heat and a cold engine cost the engine', 'Damage', 'on'], ['The gearbox: the game\'s own, the mod\'s automatic, the mod\'s own', 'Gearbox 0, 1 or 2', '0'], ['A shift without the clutch stalls; neutral while no gear key is held; L- to reverse without N', 'RequireClutch, RequireGearHeld, SkipNeutral', 'off, off, off'], ["The game's shifter picks nothing at Gearbox=2 (Ignore the game's shifter)", 'IgnoreShifter', 'off'], ["The mod's shifts on the game's thread (ini only, for work on the mod)", 'ShiftOnGameThread 0 or 1', '1'], ['Back to A after a shift by hand at Gearbox=2, in seconds (the Sequential button sets 10)', 'BackToA 0, or 3 to 30', '0 (never)'], ['The gearbox\'s bindings, two an action', '[Gearbox] Bind&lt;Action&gt;, Bind&lt;Action&gt;2', 'Num 8, 2, 5, 0, 7 for up, down, N, R, A'], ['Stall damage, percent of the gearbox, 0 to 25', 'StallDamagePercent', '5'], ['The clutch burns; its heating pace, 10 to 300', 'ClutchBurn, ClutchHeatPercent', 'on, 60'], ["Spring and damping as factors of the truck's own (tests)", 'SpringScale, DampingScale 0.25 to 4', '1, 1'], ["Travel past the file's compression end, cm", 'ExtraTravel 0 to 30', '0'], ['Ride height front and rear, cm', 'RideFront, RideRear -10 to 10', '0, 0'], ["Sway bars, a share of the axle's spring rate", 'SwayFront, SwayRear 0 to 0.3', '0, 0'], ['Air suspension, and Level to horizon with it', 'AirSuspension, AirHorizon', 'off, off'], ['The suspension display', 'SuspDisplay', 'off'], ['Damper curve: linear, digressive, progressive', 'DamperCurve 0, 1 or 2', 'linear'], ['Damping under and over the knee, 0.25 to 4; the knee, 0.02 to 0.5 m/s', 'LowCompression, HighCompression, LowRebound, HighRebound; DamperKnee', '1; 0.1'], ["Spring curve: linear, progressive, dual rate; its end rate 1 to 3; dual rate's crossover 0.2 to 0.8", 'SpringCurve 0, 1 or 2; SpringRise; SpringCross', 'linear; 2; 0.5'], ['Bump stops: length 0.03 to 0.2 m, force 1 to 6, damping 1 to 4', 'BumpStops; BumpZone, BumpForce, BumpDamping', 'off; 0.12, 3, 2'], ['Damping zones: the middle 0.5 to 1, the ends 1 to 3, where the middle runs', 'DampingZones; ZoneSoft, ZoneFirm, ZoneFrom, ZoneTo', 'off; 0.7, 1.5, 0.1, 0.6'], ['Sway bars off and on; the air suspension 2 cm up and down (Locks)', 'BindSwayDisconnect, BindRideUp, BindRideDown', 'none'],
 ['Driven with: wheel pedals, pad, keyboard', 'Input 0, 1 or 2', 'wheel pedals'], ['The mod\'s engine', 'Powertrain', 'off'], ['Share of the mod\'s engine, 0 to 100', 'EngineShare', '50'], ['Air drag off the engine\'s pull', 'RoadLoad', 'off'], ['The drag\'s area in m², 0 by the truck\'s weight', 'RoadCdA', '0'], ['An idling truck in gear moves (Creep at idle)', 'Creep', 'on'], ['A gear picked by hand creeps too', 'CreepManual', 'off'],
 ['Damage rates, 0 to 100', 'HotDamagePercentPerMinute, ColdDamagePercentPerMinute', '10, 5'], ['Where the hot damage starts, 100 to 124 °C (Damage start); the rate is reached at 125 °C', 'HotDamageFrom', '118'],['Sound volume, 0 to 100', 'SoundVolume', '50'], ['Sounds without the cluster', 'SoundsOnly', 'off'], ['A diesel\'s lift flutters (Flutter on a diesel)', 'DieselFlutter', 'off'],
 ['Key and pad buttons that hide the cluster', 'ToggleKey, PadToggle', 'none'], ['Mod work: demo values, probe log', 'Demo, Probe', 'off'], ['The tab it opens on: the one open last', 'Tab', 'display'], ['Start logs kept in Gauges\\logs (at most 500 MB; 0 all)', 'KeepLogs', '20']], 'Table 6. Gauges.ini, [Gauges]')}

<h2>20. What the mod writes, and the logs</h2>
<ul><li>No game file is changed. Into the game's memory it writes only: with <code>Damage</code> on, the driven truck's engine damage, and its gearbox damage on a stall from a shift without the clutch or from a burning clutch (on the game's thread, through a hook on its truck update, with the game's damage update after); with <code>Gearbox=1</code> or <code>2</code>, the game's automatic flag, the gears through the game's own shift function, the low range's power factor and the stall seconds of a shift without the clutch; with <code>Powertrain=1</code>, the torque factor every pass; with <code>Creep=1</code> nothing in the truck: the throttle the game hands the driven truck's wheels at idle is raised on its way, through a hook on the game's wheels' drive; during the pull test, the torque factor for seven seconds. With all of them off it writes nothing. The hooks in the game's code go by <code>Gearbox</code>, and the creep's by <code>Creep</code> alone, at every level: at 0 none of the gearbox's; at 1 and 2 the eight gearbox functions, calling through; at 2 with <code>Damage</code> on also the game's truck update. Each is put in the first time the level needs it and stays until the game is started again.</li>
<li><code>Gauges.log</code> is the last start's log; at each start the previous one moves to <code>Gauges\\logs</code>, named by its date and time. With <code>Probe=1</code> a long drive's log runs to 20 or 30 MB. The log stays open while the game runs and its probe lines go out every half second; every other line is on disk at once. The folder keeps the newest 20 logs and at most 500 MB of them (<code>KeepLogs</code>, 0 keeps all); older ones go at the next start, and only the mod's own date-named logs are ever removed. With <code>Probe=1</code> and
<code>Powertrain</code> off, a "pull:" line follows each gear driven floored on flat ground to its top: the game's own pull by the
share of the gear's top, for fitting the mod's lift to an engine.</li>
<li>The game's own HUD stays; its "Immersive Mode" option hides it while not in use.</li></ul>

<!--TECH-->
<h2>Licence and credits</h2>
<p>GNU General Public License v3.0 (GPL-3.0-only). The cluster and the menu are drawn with Dear ImGui by Omar Cornut, and their text is baked with stb_truetype by Sean Barrett; ReShade by Patrick Mours (crosire), which the release brings along without its start banner (BSD 3-Clause licence), hands the mod the game's swap chains through its add-on API. The turbo recordings are used with their author's permission. SnowRunner is a game by Saber Interactive; this project is not affiliated with Saber Interactive or Focus Entertainment and contains no game files.</p>
<p>A big thank you to the testers who gave their time to the early builds: Milos_Mk6, Globalhacker and Zenosxy. Your runs, logs, ideas and patience shaped this release, and it would not be what it is without you.</p>
'''

def finish(body, tech):
    body = body.replace('<!--TECH-->', tech)
    n = [0]
    def h2(m):
        n[0] += 1
        return f'<h2 id="s{n[0]}">{n[0]}. '
    body = re.sub(r'<h2>(?:\d+\. )?', h2, body)
    for tag, word in [('caption', 'Table'), ('figcaption', 'Figure')]:
        k = [0]
        def cap(m, word=word, tag=tag):
            k[0] += 1
            return f'<{tag}>{word} {k[0]}. '
        body = re.sub(rf'<{tag}>(?:{word} \d+\. )?', cap, body)
    toc = []
    for m in re.finditer(r'<div class="(part|sect)[^"]*">([^<]+)</div>|<h2 id="s(\d+)">([^<]+)</h2>', body):
        toc.append(f'<li class="grp">{m.group(2)}</li>' if m.group(1) else f'<li>{m.group(4)}</li>')
    return body.replace('<!--TOC-->', '<ol class="toc">' + ''.join(toc) + '</ol>')

def print_pdf(name, html_text):
    os.makedirs(OUT, exist_ok=True)
    html = os.path.join(OUT, name + '.html')
    pdf = os.path.join(OUT, name + '.pdf')
    with open(html, 'w', encoding='utf-8') as f:
        f.write(html_text)
    subprocess.run([EDGE, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--allow-file-access-from-files', f'--print-to-pdf={pdf}',
                    'file:///' + html.replace('\\', '/')], capture_output=True, text=True, timeout=180)
    print(pdf, os.path.getsize(pdf) if os.path.exists(pdf) else 'NO PDF')

if __name__ == '__main__':
    import tech_gauges
    print_pdf('Gauges_manual_' + VERSION, page('Next Gen Gameplay manual', finish(GAUGES, tech_gauges.body())))
