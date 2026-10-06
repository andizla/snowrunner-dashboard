# Changelog

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
