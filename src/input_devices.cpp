// input_devices.cpp: DirectInput 8 for the wheels and shifters (input_devices.h).
#define DIRECTINPUT_VERSION 0x0800
#include "input_devices.h"
#include <dinput.h>
#include <wchar.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
// A wheel's place: its device while it is there, and its instance, so it keeps its number when it comes back.
struct Wheel
{
    IDirectInputDevice8W *device;
    GUID instance;
    bool known; // instance holds one: the place is this wheel's
    int buttons, povs, axes;
};
IDirectInput8W *g_input = nullptr;
HWND g_window = nullptr;
Wheel g_wheels[kInputDevicesMost] = {};
void (*g_log)(const wchar_t *) = nullptr;
ULONGLONG g_lookAt = 0;          // when to list the controllers again while one is missing
const ULONGLONG kLookAgainMs = 5000, kLookLaterMs = 30000; // every 5 s at first, every 30 s after kLookMisses looks that found it not
const int kLookMisses = 3;       // (listing them can take a few hundred milliseconds on the mod's thread; review 8, W8)
int g_lookMisses = 0;

void Say(const wchar_t *fmt, ...)
{
    if (!g_log) return;
    wchar_t line[512];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(line, _TRUNCATE, fmt, args);
    va_end(args);
    g_log(line);
}

// An XInput pad as DirectInput lists it: its device path carries "IG_" (Microsoft's mark for them).
bool IsXInput(IDirectInputDevice8W *d)
{
    DIPROPGUIDANDPATH p = {};
    p.diph.dwSize = sizeof p;
    p.diph.dwHeaderSize = sizeof p.diph;
    p.diph.dwHow = DIPH_DEVICE;
    if (FAILED(d->GetProperty(DIPROP_GUIDANDPATH, &p.diph))) return false;
    for (wchar_t *c = p.wszPath; *c; c++) *c = towupper(*c);
    return wcsstr(p.wszPath, L"IG_") != nullptr;
}

BOOL CALLBACK OnDevice(const DIDEVICEINSTANCEW *found, void *)
{
    int at = -1;
    for (int i = 0; i < kInputDevicesMost && at < 0; i++)
        if (g_wheels[i].known && IsEqualGUID(g_wheels[i].instance, found->guidInstance))
        {
            if (g_wheels[i].device) return DIENUM_CONTINUE; // being read
            at = i;
        }
    const bool back = at >= 0;
    for (int i = 0; i < kInputDevicesMost && at < 0; i++)
        if (!g_wheels[i].known) at = i;
    if (at < 0) return DIENUM_STOP; // no place left
    IDirectInputDevice8W *d = nullptr;
    if (FAILED(g_input->CreateDevice(found->guidInstance, &d, nullptr)) || !d) return DIENUM_CONTINUE;
    if (IsXInput(d))
    {
        d->Release();
        return DIENUM_CONTINUE;
    }
    DIPROPRANGE range = {};
    range.diph.dwSize = sizeof range;
    range.diph.dwHeaderSize = sizeof range.diph;
    range.diph.dwHow = DIPH_DEVICE;
    range.lMin = 0;
    range.lMax = 65535;
    DIDEVCAPS caps = {};
    caps.dwSize = sizeof caps;
    if (FAILED(d->SetDataFormat(&c_dfDIJoystick2)) || FAILED(d->SetCooperativeLevel(g_window, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)) ||
        FAILED(d->GetCapabilities(&caps)))
    {
        d->Release();
        return DIENUM_CONTINUE;
    }
    d->SetProperty(DIPROP_RANGE, &range.diph); // axes from 0 to 65535 where it takes it
    d->Acquire();
    Wheel &w = g_wheels[at];
    w.device = d;
    w.instance = found->guidInstance;
    w.known = true;
    w.buttons = (int)caps.dwButtons;
    w.povs = (int)caps.dwPOVs;
    w.axes = (int)caps.dwAxes;
    if (back) Say(L"input: Wheel %d is back", at + 1);
    else Say(L"input: Wheel %d is \"%s\", %d buttons, %d POV, %d axes", at + 1, found->tszProductName, w.buttons, w.povs, w.axes);
    return DIENUM_CONTINUE;
}

void LookForDevices()
{
    if (g_input) g_input->EnumDevices(DI8DEVCLASS_GAMECTRL, OnDevice, nullptr, DIEDFL_ATTACHEDONLY);
}

bool AnyMissing()
{
    for (const Wheel &w : g_wheels)
        if (w.known && !w.device) return true;
    return false;
}
}

bool InputDevicesStart(void (*log)(const wchar_t *line))
{
    g_log = log;
    if (g_input) return true;
    const HINSTANCE self = (HINSTANCE)&__ImageBase;
    // DirectInput wants a window of the process for a device's cooperative level: a hidden one of its own
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = self;
    wc.lpszClassName = L"NextGenGameplayInput";
    RegisterClassW(&wc);
    g_window = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, self, nullptr);
    if (!g_window || FAILED(DirectInput8Create(self, DIRECTINPUT_VERSION, IID_IDirectInput8W, (void **)&g_input, nullptr)) || !g_input)
    {
        Say(L"input: DirectInput could not start, so wheels and shifters are not read (keys and the pad are)");
        g_input = nullptr;
        return false;
    }
    LookForDevices();
    if (!InputDevicesFound()) Say(L"input: no wheel or shifter found (the XInput pads are read on their own)");
    g_lookAt = GetTickCount64() + kLookAgainMs;
    return true;
}

void InputDevicesStop()
{
    for (Wheel &w : g_wheels)
        if (w.device)
        {
            w.device->Unacquire();
            w.device->Release();
        }
    memset(g_wheels, 0, sizeof g_wheels);
    if (g_input) g_input->Release();
    g_input = nullptr;
    if (g_window) DestroyWindow(g_window);
    g_window = nullptr;
}

int InputDevicesFound()
{
    int n = 0;
    for (const Wheel &w : g_wheels) n += w.device != nullptr;
    return n;
}

void InputSnapshotRead(InputSnapshot &s, bool front, uint32_t pad)
{
    memset(&s, 0, sizeof s);
    s.front = front;
    s.pad = pad;
    // the hidden window's messages: Windows waits on a top-level window's answer to some of them (a broadcast), so
    // its queue is emptied once a pass (review 8, W4)
    if (g_window)
    {
        MSG msg;
        while (PeekMessageW(&msg, g_window, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    }
    if (front)
        for (int vk = 1; vk < 256; vk++) s.key[vk] = (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
    if (!g_input) return;
    const ULONGLONG now = GetTickCount64();
    if (now >= g_lookAt)
    {
        if (AnyMissing())
        {
            LookForDevices();
            g_lookMisses = AnyMissing() ? g_lookMisses + 1 : 0;
        }
        else g_lookMisses = 0;
        g_lookAt = now + (g_lookMisses >= kLookMisses ? kLookLaterMs : kLookAgainMs);
    }
    for (int i = 0; i < kInputDevicesMost; i++)
    {
        Wheel &w = g_wheels[i];
        if (!w.device) continue;
        DIJOYSTATE2 js = {};
        HRESULT hr = w.device->Poll();
        if (SUCCEEDED(hr) || hr == DI_NOEFFECT) hr = w.device->GetDeviceState(sizeof js, &js);
        if (FAILED(hr))
        {
            // lost to another program or gone: taken again, else let go until it is found again
            w.device->Acquire();
            hr = w.device->Poll();
            if (SUCCEEDED(hr) || hr == DI_NOEFFECT) hr = w.device->GetDeviceState(sizeof js, &js);
        }
        if (FAILED(hr))
        {
            w.device->Unacquire();
            w.device->Release();
            w.device = nullptr;
            Say(L"input: Wheel %d went away; it is looked for again every %llu s", i + 1, kLookAgainMs / 1000);
            continue;
        }
        InputDevice &d = s.device[i];
        d.present = true;
        d.buttons = w.buttons < kInputButtonsMost ? w.buttons : kInputButtonsMost;
        d.povs = w.povs < kInputPovsMost ? w.povs : kInputPovsMost;
        d.axes = w.axes < kInputAxesMost ? w.axes : kInputAxesMost;
        for (int b = 0; b < kInputButtonsMost; b++) d.button[b] = (js.rgbButtons[b] & 0x80) ? 1 : 0;
        for (int p = 0; p < kInputPovsMost; p++) d.pov[p] = LOWORD(js.rgdwPOV[p]) == 0xFFFF ? 0xFFFF : js.rgdwPOV[p];
        const LONG axes[kInputAxesMost] = { js.lX, js.lY, js.lZ, js.lRx, js.lRy, js.lRz, js.rglSlider[0], js.rglSlider[1] };
        for (int a = 0; a < kInputAxesMost; a++) d.axis[a] = (float)axes[a] / 65535.0f;
    }
}
