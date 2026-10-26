// input_devices.h: the keys, the pads and the wheels and shifters as one snapshot a pass (input_bindings.h reads the
// bindings from it). Wheels and shifters go through DirectInput 8: every game controller Windows lists, but for the
// XInput pads (PadButtons reads those), opened without taking it from the game, polled once a pass. One that goes away
// is let go and looked for again every 5 s until it is back. From one thread only.
#pragma once
#include "input_bindings.h"

// DirectInput up and the controllers found, each named in the log through log (a line without its end). False where
// DirectInput cannot start: the keys and the pad are read all the same.
bool InputDevicesStart(void (*log)(const wchar_t *line));
void InputDevicesStop();
// The controllers being read now.
int InputDevicesFound();
// One pass: the keys (only while front: the game's window is in front), the pad's buttons as given, and every
// controller's buttons, POV hats and axes.
void InputSnapshotRead(InputSnapshot &s, bool front, uint32_t pad);
