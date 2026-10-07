# Changelog

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
