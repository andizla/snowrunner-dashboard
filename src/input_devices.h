// input_devices.h: the keys, the pads and the wheels and shifters as one snapshot a pass (input_bindings.h reads the
// bindings from it). Wheels and shifters go through DirectInput 8: every game controller Windows lists, but for the
// XInput pads (PadButtons reads those), opened without taking it from the game. A poll thread of its own reads all of
// them about 200 times a second and latches what was down since the last pass (InputLatch), so a tap between two
// passes is not lost; it owns the hidden window DirectInput wants and the controllers, and one that goes away is let
// go and looked for again every 5 s (every 30 s after three misses) until it is back.
#pragma once
#include "input_bindings.h"

// The poll thread up, with DirectInput and the controllers found, each named in the log through log (a line without
// its end); pad: the buttons of every pad together (PadButtons), nullptr for none. Returns once the first reading is
// in. False where DirectInput cannot start: the keys and the pad are read all the same.
bool InputDevicesStart(void (*log)(const wchar_t *line), uint32_t (*pad)() = nullptr);
void InputDevicesStop();
// The controllers being read now.
int InputDevicesFound();
// One pass: everything down since the last pass (InputLatch), the keys only while front (the game's window is in
// front), and every controller's buttons, POV hats and axes; the latch starts again from the controls as they are.
void InputSnapshotRead(InputSnapshot &s, bool front);
