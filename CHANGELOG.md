# Changelog

## [0.12.7] 2026-10-10

- The mod's automatic never sets a gear the engine cannot take at the speed the wheels turn, whatever gear its model holds. At the end of both power tests in the third gear it set the second at about 6500 rpm: the model had shifted down by itself while the test held the game's gear, and set its own once the test let go. The model now holds the test's gear too.
- The tab's power test is a pull test now: it changes the game's torque factor (the share of its torque the game leaves a damaged engine) in place of the power factor, halved, raised by half and at three quarters, in the fourth gear or higher. The power factor's answer is in: a write counts at once and is kept, but it moves the gear's top speed, the way the game makes its low gears, so halved the truck braked hard and doubled the gear pulled less. It cannot carry the engine's torque.

## [0.12.6] 2026-10-10

- The outside temperature follows the game's day: coldest just before sunrise, warmest in the afternoon. A map's section can give the day's warmest and the night's coldest, `DayC` and `NightC` (Scandinavia's asphalt has frost at night and none by day: `DayC=3`, `NightC=-12`); a section with only `AmbientC` swings 10 C around it. The tab shows both and the hour, and its slider moves them together.
- The mod finds the game's clock by itself: with `Probe=1` it looks through the game's main objects for a value from 0 to 24 that rises at a day's pace while you drive, and takes the one that jumps when the night is skipped on the map (or time passes in a garage), or the only one left after three minutes. `Gauges.log` tells the search; the place is kept in `Gauges.ini` under `[Clock]` for this version of the game and read from then on. Until it is found the day's mean is in force, as before.

## [0.12.5] 2026-10-10

- The outside temperature by the map is a setting of its own, on by default: `AmbientByMap` (on the tab, "Outside by map"). While the map's temperature is in force, the tab's slider shows it and sets it in the map's or its region's section of `Gauges.ini`; before, the slider showed and set `AmbientC`, which is only in force where the map is not known, so the tab looked as if the temperature were set by hand. Off, `AmbientC` is in force on every map.

## [0.12.4] 2026-10-10

- A power test on the tab (Gearbox, "Test the power factor"): floored in the third gear or higher on flat ground, the mod holds the gear for seven seconds and changes the game's power factor a second at a time (as it is, halved by a write alone, halved with the gear set again, doubled with the gear set again), measures the pull in each, and puts the factor back. `Gauges.log` gets the numbers: whether the factor changes the pull at once, only once the gear is set again, or not at all, which decides how the engine's torque reaches the game next. Lifting the foot, the brake, another gear or the game's automatic stops it.

## [0.12.3] 2026-10-10

- The mod's automatic goes by the pedal where your foot holds it. It went by the pedal after the powertrain's curve, under which a fifth of the pedal reads as a twentieth, so a light foot sat on the line between coasting and driving and it shifted 4 to 5 and back every few seconds.
- A tap of the shift modifier with the foot off no longer drops the truck into first at speed. The game's own automatic picks first then whatever the speed (at 23 km/h, about 8400 rpm), and the mod took that gear on; it now takes the lowest gear the engine can take at the speed the wheels turn.
- Floored, a shift up comes 0.15 s after it is called for, where it was 0.4 s: a short first gear went 300 to 800 rpm past its shift point in that time.

## [0.12.2] 2026-10-10

- The mod's automatic no longer shifts down into a gear that races the engine. In 0.12.1 it learned how fast the wheels turn for the truck's speed from any moment with the foot on, so a bump or the truck sliding back down a slope could teach it the truck went at half its speed: it then shifted down as far as first at up to 6800 rpm, and held first on the limiter for half a minute. It learns now only while the wheels turn steadily and alike, the truck going forwards with no brake or handbrake, and follows a lower reading over two seconds. It goes by the wheels again and weighs them against the ground only to tell wheelspin and locked wheels, and no shift down takes the engine past nine tenths of the way to the red zone at the speed the wheels turn.
- A climb is told by the truck losing speed for half a second with the foot on, where a moment of it was enough, so the shift points no longer jump to floored's on a bump.
- A tap of the shift modifier the game goes back on within two seconds (it picked 3rd from 1st and fell back to 1st) is taken on as well; before, it gave the gears back as if picked by hand.

## [0.12.1] 2026-10-10

- The mod's automatic shifts sooner. Floored it shifts up at about four fifths of the way from idle to the red zone, where it waited until nearly the red zone (on the Defender 4040 rpm, where it was about 4600 from 4th to 5th), and at a light foot lower still. The shift points are measured from idle, so a slow revving diesel and a fast revving petrol engine both shift in their band.
- With the foot off it no longer shifts up (only past the red zone, rolling downhill), and it shifts down early, at a quarter of the way from idle to the red zone, gears at a time when the truck slows hard, never so far that the engine races. Before, a lift shifted up and the shifts down came late, the last one as the truck stopped.
- Climbing (the truck slowing with the foot on) it shifts down where floored would and holds those points four seconds after, so it does not shift up and straight back down. Before, it barely shifted on a climb: wheels slipping held every shift.
- It goes by the truck's speed over the ground, learned against the wheels while they grip, so wheelspin in mud does not shift it up and wheels locked under the brake do not throw it into first; the shifts no longer wait while the wheels slip.
- A push of the pedal to the floor kicks down at once, once; held floored it steps down a gear at a time as the truck slows.
- Tapping the shift modifier while the mod drives lets the game's own automatic pick a gear, which the mod takes on and holds for two seconds. Before, the mod set its own gear straight back, and a tap for 5th went back to 4th.
- Another mod raising the power factor in A (a manual transmission mod sets 1.05) no longer reads as a low gear picked by hand, which gave the gears back a few seconds after they were taken.

## [0.12.0] 2026-10-10

- The mod's automatic drives the game's gears. `Gearbox=1` (the tab's new Gearbox section) hands the gears to the mod's automatic while the game's shifter is in A: a torque converter (the engine at its stall speed with the pedal floored against a standing truck, at most 2400 rpm, following the road speed with a little slip on the move, locked up from the second gear), shift points that rise with the pedal, a kickdown when floored, the gear held when the foot lifts and rolling downhill, no shift in wheelspin, and the first gear by the time the truck stops. The tachometer and everything that goes by it (the turbo, the heat) take the converter's engine speed, and the cluster shows A. The gears are set through the game's own shift function with the game's automatic put down; picking a gear by hand gives the gearbox back, A takes it again. The truck still pulls with the game's own torque. `Gearbox=0`, the default, leaves the game's automatic as it was.
- The converter's stall speed is at most 2400 rpm: a fast revving engine stops well short of its red zone floored against a standing truck.

## [0.11.3] 2026-10-09

- The outside temperature goes by the map. The mod reads which map you are on from the game's save (the map it loaded last), and the `[Map:<name>]` sections now carry the game's own names: one for each of its 21 regions (`[Map:Michigan]`, `[Map:Zurdania]`), and a map can have one apart from its region's (`[Map:Black River]`). A map of a mod goes by its id. `Gauges.log` says the map, its region and the section in force when it changes, and the Gauges tab names them under the outside temperature.
- The mod is a ReShade add-on now, `Gauges.addon64`: ReShade loads it, so no ASI loader is needed. Delete `Gauges.asi` when you install it; with both in the folder only one runs, and the log says which. `Gauges.asi` is the same file for a game set up with an ASI loader, and the log says which of the two loaded it.
- Once a truck is driven, the log says where the game's gearbox functions are in its code, or that another mod holds them. Nothing is written: this is the ground for the gearbox, the clutch and the engine that come with 0.12.0.
- The clutch's model in the log (`Probe=1`, while the manual transmission mod drives): a stalled engine stays dead until the clutch is pressed or the gearbox is in neutral, where it started over every two seconds before and stalled again at once while the truck stood in gear. Without a clutch pedal on an axis (a clutch button, or none) the clutch works by itself: it lets go at idle, takes hold as the engine speeds up, and never stalls the engine.
- The look for the game's gearbox functions tells a function behind another mod's hook also where the hook moved more than one instruction, and no longer looks for ShiftToNeutral where its neighbours are not both found.
- The automatic's model (`Probe=1`) runs beside the game's automatic also while the manual transmission mod is loaded; it stands down only while that mod drives a manual gear with the game's automatic off. Before, it stood down whenever the mod was loaded.
- The automatic's model is in the first gear as the truck all but stands, also after a hard stop that left no time to shift down gear by gear.
- The placeholder sections of 0.11.0 to 0.11.2 are renamed to the game's names with the temperatures they hold, and the four regions they lacked (Quebec, Washington, Chiapas, Zurdania) are added.

## [0.11.2] 2026-10-08

- The brake air dial shows the air in the brakes' tanks. The engine's compressor fills them, faster the higher it revs (about three minutes from empty at idle, one at the red zone); its governor stops it at 8.2 bar and starts it again at 6.9. Each press of the brake takes air by how far it goes down, letting the handbrake go takes the air that holds the spring brakes off, and the system leaks a little. Eight full presses with the engine off take a full tank into the red zone. Scouts brake by hydraulics and have no air dial. `ShowAir` is still off at first. With `Probe=1` the log has a line for it.
- With `Probe=1` the log has a line for the powertrain that comes with 0.12.0: the pedal as it will be shaped (a progressive curve, and a ramp so a keyboard's pedal comes in over 0.6 s), the torque from the truck class's torque curve at the engine speed, less with cold oil and more with boost, and the power the engine would deliver. Nothing of it reaches the game yet.
- With `Probe=1` the log has a line for an automatic gearbox with a torque converter as well, and a line at each of its shifts beside the game's own gear: the gear it would be in, the converter's slip and lock-up, and how much it multiplies the torque. It shifts up later the further the pedal is down, kicks down when floored, holds its gear rolling downhill, and does not shift up at a lift of the foot or in wheelspin. Nothing of it reaches the game yet.
- A manual transmission mod that publishes its state (a build with the state export) is followed: the log says when it comes and goes, and while it drives the truck the automatic's model stands down. With `Probe=1` the log has a line for it (its gear by its own names, the clutch held and how far, its stalls) and a line at each stall it causes for a shift without the clutch. Without such a build nothing changes.
- While that mod drives, the log with `Probe=1` also has a line for the engine behind its clutch pedal: free with the pedal pressed, slipping in the bite, turning with the gearbox let up, and a stall when the clutch comes up too fast for the engine's speed, said in the log as it would happen. Nothing of it reaches the game yet.
- `WarmUpMinutes` is measured on driving as it really is: the mean load and speed of a 25 minute drive (half of what the engine burns floored at the red zone, at 13 km/h), where it was a heavier foot before. On that drive the coolant was at 85 C after 4 minutes of driving with the setting at 3, where it took nearly 5 with 0.11.1.

## [0.11.1] 2026-10-08

- The heat comes from the fuel the engine burns: the pedal and the engine speed together, as in a real engine. Floored at the red zone makes the most heat, floored at low revs little, and coasting or rolling downhill with the foot off next to none. In 0.11.0 a truck coasting at high revs heated as if floored.
- The cold engine damage goes by the fuel burned too: only a cold engine burning more than half of what it burns floored at the red zone suffers. Pulling away and driving about at moderate revs cost nothing, also on a keyboard.
- The oil takes its own heat: the friction, which grows with the engine speed, and the pistons' cooling, which grows with the load. It runs about 7 degrees over the coolant at cruise and 14 floored at the red zone, and it is warm a minute after the coolant at the least, whatever `OilWarmUpMinutes` says.
- New defaults: `OilWarmUpMinutes` 5 (was 8) and `OilWarnC` 120 C (was 125), the warning one maker puts on its truck diesels. An ini that holds the old values keeps them.
- `models_test.exe <drive> <idle> <red zone>` replays a recorded drive on a truck's own scales.

## [0.11.0] 2026-10-08

- The coolant temperature follows the engine speed, not the pedal. An engine revved high heats whatever the foot does, one pulling at low revs heats little, and in neutral it makes a quarter of that heat.
- The cold engine damage goes by the engine speed too: below 60 C with the needle more than half way from idle to the red zone. A keyboard's pedal, which is either up or floored, costs no more than a gentle foot that revs as high. The damage from overheating comes from the temperature, as before.
- The oil temperature. The oil follows the coolant slowly: driven as usual it is at 85 C after about eight minutes, and a stopped engine's oil keeps its heat longer. It runs about 2 degrees over the coolant at idle and 14 at full load. Each kind of truck keeps its own.
- `OilGauge` shows it as a second, amber needle in the coolant's dial (the default), on a dial of its own left of the coolant's, or not at all.
- An oil lamp, the ninth. It blinks while the oil is too hot, from `OilWarnC` (125 C at first), and a warning of two short beeps sounds every four seconds until the oil is two degrees under that or the engine stops. `SoundVolume` sets how loud; `sounds\oil_beep_1.wav` plays in its place. The Gauges tab has a button to hear it.
- New settings `OilWarmUpMinutes` (8) and `OilWarnC` (125), on the tab under Oil temperature.
- `BoostProfile`: how the turbo builds its boost. Standard, small and quick (early, little lag), big and slow (late, long to come at low revs), or two stages (a small turbo early and a big one after it). It is on the tab under Scales, and each kind of truck can have its own, as its seventh scale.
- Sections `[Map:<name>]` with an outside temperature for each of the game's regions are written into the ini. Their names are placeholders: the mod does not read which map a truck is on yet, so `AmbientC` is in force everywhere until it does.
- With `Probe=1` the coolant's line has the oil, and the log says when the oil warning comes and goes.

## [0.10.0] 2026-10-07

- The turbo is heard. Take your foot off the pedal under boost and the turbo flutters: the air beats back through the compressor wheel, slower and fainter until it is gone.
- The more boost the lift had, the longer and louder the flutter: there is one for a lift at full boost, one at half and one at little, each in three recordings, of which one is played and never the same twice running. Each also plays a little faster or slower than the last.
- Put the pedal down again and a flutter that still sounds stops.
- `SoundVolume` (0 to 100, 50 at first) sets how loud, and 0 switches the sounds off. The Gauges tab has the slider and three buttons to hear the sounds.
- `SoundsOnly` plays the sounds and draws no cluster.
- WAV files `sounds\turbo_high_1.wav` to `_4.wav` next to `Gauges.asi` (and `turbo_mid_`, `turbo_low_`) are played in place of the mod's own recordings of that strength.
- The sounds go through XAudio2, the game's own copy or Windows'. The mod holds the audio device only while a sound plays.

## [0.9.0] 2026-10-07

- The cluster is drawn anew. Solid and tinted dials sit in a bevelled metal ring with a soft shadow, and the solid face is a little lighter towards its middle. In the open style the scale arcs have a soft and a sharp shadow.
- The needles are blades with a point, one half bright and one shaded, over a hub with a lit ring. Major ticks taper towards the middle, and the red and blue zones have a soft inner edge.
- Every lamp has a new symbol, drawn from curves: parking brake, headlights, engine, frost, the truck of the tilt lamp, and on the dials the fuel pump, the thermometer, the turbo and brake air. All wheel drive, the differential lock and the low range are symbols now, in place of the texts AWD, DIFF and LOW. A lit lamp glows.
- Still drawn at the screen's own resolution at any size, with no image files.
- With `Probe=1` the log says once a minute what the cluster takes to draw.

## [0.8.0] 2026-10-07

- The boost dial shows a boost pressure, worked out from the pedal and the engine speed. The turbo makes nothing near idle and all it can from about two thirds of the way to the red zone with the pedal floored. In neutral it makes a quarter of that at most.
- The needle lags behind the pedal, longer at low engine speed: floored at the red zone the boost is there in under a second, pulling away from a standstill it takes a second or more. It is gone within half a second after the foot comes off.
- New setting `BoostMax`: the boost in bar the turbo reaches, from 0.3 to 2. At 0 it is the truck class's own: trucks 1.8 bar, scouts 1, heavy trucks 2. It is on the tab under Scales, and each kind of truck can have its own, kept with its other scales in its section of `Gauges.ini`.
- With `Probe=1` the log has a line for the boost, and one for each quick lift of the pedal under boost: a blow-off or a flutter, with the boost it came at.

## [0.7.0] 2026-10-07

- Each kind of truck can have its own scales. Change the truck class, idle, the start of the red zone, the tachometer's top or the speedometer's top on the Gauges tab while you drive a truck, and all five are kept for that kind of truck. They are back whenever you drive one: after a recovery, out of the garage, after a change of trucks and after a new start of the game.
- A truck that was never set up shows the scales of `[Gauges]`, as before, and an ini from an earlier version works as it is. On the tab those plain scales are the ones shown while no truck is driven: in the game's main menu and in the garage.
- The Scales section of the tab names the truck it sets, says whether the scales are its own, and has a button that gives the truck the plain scales back.
- A truck's scales are saved in `Gauges.ini` in a section named after the truck as the game's files name it, such as `[Truck:chevrolet_ck1500]`. `Gauges.log` names the truck each time you take another one.
- The coolant temperature and the range go by the kind of truck as well. They went by the tank, the wheels and the gearbox before, so two different trucks with the same three shared a temperature. Two trucks of one kind still do.
- The Defaults button takes the scales of the truck you drive back to the defaults too. Other kinds of truck keep theirs.

## [0.6.1] 2026-10-07

- Water cools the engine. From about half a metre of water at the truck's front the coolant loses heat to it, and with the engine standing in the water (1.2 m and more) a hot engine is back at 100 C within half a minute, even with the pedal floored. A stopped engine cools five times as fast in deep water. Nothing cools below the outside temperature, and an idling engine in a ford stays warm enough to keep the cold lamp dark.
- `Damage` is on by default: an ini without the key now has the engine damage from the temperature. `Damage=0` switches it off.
- The dials wait while the game does not simulate the truck, so nothing heats, cools or wears on values the game has left standing.
- The write of engine damage checks more before it writes, and for an engine whose record it cannot find it writes the damage alone and leaves the loss of torque to the game.
- The tab, the ini help and the readme say that a keyboard's pedal counts as floored, so a cold start always costs there unless `ColdDamagePercentPerMinute` is 0, and that `Damage` has not been tried in co-op.

## [0.6.0] 2026-10-07

- The temperature can cost the engine. With `Damage` on, an engine in the red zone takes the game's own engine damage: 10 percent of what it can take every minute at 125 C, nothing at 118 C, less between the two and more above. Floored at a standstill with the wheels spinning, an engine is used up about five minutes after it enters the red zone.
- A cold engine suffers under a heavy pedal. Below 60 C, with the pedal more than half down, it loses up to 5 percent a minute (floored at 0 C), and less the warmer it is. A cold start driven with the pedal floored costs 2 to 3 percent of the engine. Half pedal or less costs nothing.
- The damage is the game's own: a garage repairs it, and past the engine's threshold the game takes torque away and burns more fuel. The mod stops one point short of what the engine takes, so it never stops the engine itself.
- The engine damage lamp blinks while the mod adds damage.
- `Damage` is off by default, and the temperature is then only shown, as before. The Gauges tab has a new Engine damage section with the switch and the two rates, `HotDamagePercentPerMinute` and `ColdDamagePercentPerMinute`.
- The radiator gets all its air from 30 km/h, where it took 50 km/h before. A truck in the low range at its own top speed now runs hot and settles just under the red zone, and the needle stays out of the red zone from about 7 km/h up. Slower crawls and a truck that spins its wheels without moving still overheat. The new setting `AirFullKmh` moves that speed: lower keeps a slow, hard pulling engine cooler.
- With `Probe=1` the log has a sixth line with the engine's damage, the engine's record and what the mod adds.

## [0.5.1] 2026-10-07

- A truck you take out of the garage again is as warm as you left it, less what it cooled while you drove another truck. In 0.5.0 it came back cold, as the game makes a new truck of it. The mod now knows a truck by its tank, its wheels and its gearbox.

## [0.5.0] 2026-10-06

- The coolant temperature dial works. The mod works the temperature out from how hard the engine works and how fast the truck drives. A cold engine starts at the outside temperature and is at 85 C after about three minutes of driving. The thermostat then holds it: about 86 C at idle, near 100 C when the truck works hard at speed.
- The needle goes into the red zone from 118 C only when the pedal stays floored at a crawl. At a standstill with the engine revving out that takes a minute and a half from 90 C. From about 15 km/h up the needle stays out of the red zone, and off the pedal the engine is back at 100 C within a minute. The temperature is only shown and does nothing to the truck.
- The cold lamp is lit while the coolant is below 60 C.
- A stopped engine cools from 90 C to 40 C in a quarter of an hour. Each truck keeps its own temperature while the game runs, and a truck left standing cools while you drive another.
- Four new settings on the Gauges tab and in `Gauges.ini`: `WarmUpMinutes`, `OverheatMinutes` and `CoolDownMinutes` set the three times, and `AmbientC` is the outside temperature.
- With `Probe=1` the log has a fifth line with the coolant temperature, the engine's load and the air through the radiator.

## [0.4.0] 2026-10-06

- The tilt lamp works. It is green while the truck stands level, amber when the truck leans 15 degrees to a side and red from 25 degrees, or when the truck points 35 degrees up or down a slope. A truck on its side or its roof is in the red.
- The small truck in the lamp leans the way your truck does, by the same angle.
- The lean is smoothed over a third of a second, so a bump does not light the lamp, and the lamp goes back two degrees below the angle it came on at, so it does not flicker on the edge.
- Three new settings on the Gauges tab and in `Gauges.ini` set the angles: `RollWarnDeg`, `RollDangerDeg` and `PitchDangerDeg`.
- With `Probe=1` the log has a fourth line with the slope, the lean and the lamp.

## [0.3.0] 2026-10-06

- The tachometer shows an engine speed, worked out from the driven wheels, the gear and the pedal. It climbs through each gear to the red zone and falls at the shift, it rises under the pedal when the truck is stuck, and the pedal revs the engine freely in neutral. Wheels that spin faster than the gear allows take the needle into the red zone.
- The needle rises from 0 while the engine starts, dips for a moment when a gear goes in, and runs down when the engine stalls or is switched off.
- The range readout shows how far the fuel in the tank goes, from the litres per kilometre the truck burned on the last three kilometres it drove. It shows `---` until the truck has driven half a kilometre, and a tenth of a kilometre below ten.
- Fuel burned while standing comes off the range with the litres it takes and does not change the litres per kilometre. A truck put back into the world keeps its figure.
- The speedometer and tachometer needles glide between the 20 readings a second.
- With `Probe=1` the log has a third line with the engine speed, the range and the fuel burn.

## [0.2.0] 2026-10-06

- The cluster shows the driven truck's own values: speed, the gear, fuel, and the lamps for the handbrake, the headlights, all wheel drive, the differential lock, the low range and engine damage. The tachometer rests at idle while the engine runs.
- The gear is written as its number with an A in automatic, H for the high gear, L, L+ and L- for the low range, R and N.
- The cluster is on screen only while a truck is driven in the world. It is hidden on loading screens, in the pause menu, on the map and in the garage.
- The dials and lamps that have no source yet rest: coolant temperature, boost, brake air, the range, the cold lamp and the tilt lamp.
- `Demo` is off by default. It is a switch for work on the mod and draws demo values whenever no truck is driven.
- `Probe=1` writes the values read from the game into `Gauges.log`, every two seconds and whenever one changes.

## [0.1.0] 2026-10-05

- `Gauges.asi` draws a gauge cluster over the game through ReShade's add-on overlay, in the game's own font: speedometer, tachometer with the gear, fuel with the range, coolant temperature, boost pressure, brake air pressure and eight lamps. The cluster shows demo values.
- Two layouts (a strip along the bottom centre, a block in the bottom right corner) and three styles (solid dial faces, tinted see-through faces, open scales without faces).
- A Gauges tab in ReShade's overlay edits every setting while the game runs and saves it to `Gauges.ini`: layout, style, position, size, opacity, units, which dials and lamps are shown, the scales by truck class, and a key and pad buttons that hide and show the cluster.
- The mod runs next to the SnowRunner Tire Inflation System, loaded by the same `version.dll`.
- On any build of the game but the Steam build of 22 July 2026 the mod stands down and says so in `Gauges.log`.
