// late_test.cpp: the overlay's start against the game's swap chain, without the game (BRIEF_overlay_late_hook.md).
// A window gets a swap chain on the WARP device through this program's imports of CreateDXGIFactory1 and
// D3D11CreateDevice, as the game makes its own, before or after the overlay (src\ng_overlay.cpp) sets its hooks.
// The overlay's state is one a process, so each case is a run of its own:
//   late_test.exe late       the chain before the hooks, one mod: nothing drawn, the line in capitals, then a second chain
//   late_test.exe order      the hooks before the chain: no drawing before the start is done, no asking; then this mod offers
//   late_test.exe handover   the chain before the hooks and a stand-in for a mod that draws: taken inside its Present
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "imgui.h"
#include "ng_overlay.h"

// The menu block's layout as ng_overlay.cpp has it (version 3): other programs read it by its bytes.
struct NgSharedCopy
{
    volatile LONG version, open, chosen, toggledAt, key, inputSlot, wheelTurn, charCount, chars[32], cursorX, cursorY, placeTurn;
    float x, y, w, h;
    volatile LONG64 graphics;
    struct Slot
    {
        volatile LONG used, ready;
        LONG order;
        char tab[40];
    } slots[8];
};

static int g_failed = 0;
static volatile LONG g_frames = 0;
static CRITICAL_SECTION g_lock;
static std::vector<std::wstring> g_lines;

static void Check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failed++;
}

static void Log(const wchar_t *fmt, ...)
{
    wchar_t line[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(line, _TRUNCATE, fmt, ap);
    va_end(ap);
    EnterCriticalSection(&g_lock);
    g_lines.push_back(line);
    LeaveCriticalSection(&g_lock);
}

static int Lines(const wchar_t *part)
{
    int n = 0;
    EnterCriticalSection(&g_lock);
    for (const std::wstring &l : g_lines)
        if (l.find(part) != std::wstring::npos) n++;
    LeaveCriticalSection(&g_lock);
    return n;
}

static void Frame() { InterlockedIncrement(&g_frames); }
static void Page() {}
static void DeviceGone() {}

struct Chain
{
    HWND wnd;
    IDXGISwapChain *sc;
    ID3D11Device *dev;
    ID3D11DeviceContext *ctx;
};

// The game's way: a factory from CreateDXGIFactory1, a device on one of its adapters, the swap chain from the factory.
static bool ChainMake(Chain &c)
{
    c = {};
    c.wnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"late test", WS_POPUP, 0, 0, 320, 200, nullptr, nullptr, nullptr, nullptr);
    if (!c.wnd) return false;
    ShowWindow(c.wnd, SW_SHOWNOACTIVATE);
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 320;
    sd.BufferDesc.Height = 200;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = c.wnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGIFactory1 *f = nullptr;
    IDXGIFactory4 *f4 = nullptr;
    IDXGIAdapter *warp = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&f)) && SUCCEEDED(f->QueryInterface(__uuidof(IDXGIFactory4), (void **)&f4)))
        f4->EnumWarpAdapter(__uuidof(IDXGIAdapter), (void **)&warp);
    const bool made = warp && SUCCEEDED(D3D11CreateDevice(warp, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &c.dev, nullptr, &c.ctx)) &&
                      SUCCEEDED(f->CreateSwapChain(c.dev, &sd, &c.sc));
    if (warp) warp->Release();
    if (f4) f4->Release();
    if (f) f->Release();
    return made;
}

// n presents 20 ms apart; false where one failed.
static bool Presents(Chain &c, int n)
{
    bool ok = true;
    for (int i = 0; i < n; i++)
    {
        if (FAILED(c.sc->Present(0, 0))) ok = false;
        Sleep(20);
    }
    return ok;
}

static bool Start(bool waitReady)
{
    NgClient client = {};
    client.tab = "Gameplay";
    client.version = "test";
    client.order = 1;
    client.log = Log;
    client.frame = Frame;
    client.page = Page;
    client.deviceGone = DeviceGone;
    client.waitReady = waitReady;
    return NgOverlayStart(client);
}

static NgChain *ChainBlock()
{
    wchar_t name[64];
    swprintf_s(name, L"Local\\NextGen.Chain.%lu", GetCurrentProcessId());
    const HANDLE map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
    return map ? (NgChain *)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(NgChain)) : nullptr;
}

static void MenuBlockCheck()
{
    wchar_t name[64];
    swprintf_s(name, L"Local\\NextGen.Menu.%lu", GetCurrentProcessId());
    const HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    const NgSharedCopy *s = map ? (const NgSharedCopy *)MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof(NgSharedCopy)) : nullptr;
    Check(sizeof(NgSharedCopy) == 616 && offsetof(NgSharedCopy, graphics) == 192, "the menu block: 616 bytes with graphics at byte 192");
    Check(s && s->version == 3 && s->slots[0].used == 1 && strcmp(s->slots[0].tab, "Gameplay") == 0, "the menu block: version 3 and the mod's tab in slot 0, read by that layout");
}

// The chain before the hooks, one mod.
static void CaseLate()
{
    Chain a;
    if (!ChainMake(a)) { Check(false, "a WARP swap chain"); return; }
    NgLateWaitSet(1500, 300);
    Check(Start(true), "the overlay starts after the swap chain was made");
    NgChain *c = ChainBlock();
    Check(c && c->version == kNgChainVersion && c->want == 0, "the chain block is there, version 1, nothing asked yet");
    if (!c) return;
    Presents(a, 3);
    NgOverlayReady();
    const ULONGLONG from = GetTickCount64();
    while (!Lines(L"THE OVERLAY DRAWS NOTHING") && GetTickCount64() - from < 5000) Presents(a, 1);
    const ULONGLONG took = GetTickCount64() - from;
    Presents(a, 10);
    Check(g_frames == 0, "nothing is drawn on the chain made before the hooks");
    Check(Lines(L"THE OVERLAY DRAWS NOTHING: no swap chain came while a window was up (the game likely made its own before the overlay's hook was in)") == 1 && took >= 1000 && took < 3000,
          "the line in capitals with its reason, once, after the wait (1.5 s here)");
    Check(c->want == 1 && c->offered == 0 && c->taken == 0 && Lines(L"asking the other Next Gen mods") == 1, "the chain was asked for once and nobody offered");
    Check(Lines(L"Present hooked") == 0, "no line says a Present is hooked");
    Chain b;
    if (!ChainMake(b)) { Check(false, "a second WARP swap chain"); return; }
    Check(Presents(b, 6) && g_frames > 0 && Lines(L"Present hooked on a swap chain the game made") == 1, "the next chain the game makes is taken and drawn on");
    Sleep(300);
    Check(c->want == 1 && Lines(L"THE OVERLAY DRAWS NOTHING") == 1, "no second asking and no second line");
    MenuBlockCheck();
}

struct Taker
{
    NgChain *c;
    void *got;
};

// A stand-in for a late mod's thread: takes the next offer and answers it.
static DWORD WINAPI TakerThread(void *p)
{
    Taker *t = (Taker *)p;
    const LONG offered = t->c->offered;
    for (int i = 0; i < 3000 && t->c->offered == offered; i++) Sleep(1);
    t->got = InterlockedExchangePointer(&t->c->offer, nullptr);
    InterlockedIncrement(&t->c->taken);
    return 0;
}

// The hooks before the chain, as at a good start; then this mod as the one that offers.
static void CaseOrder()
{
    NgLateWaitSet(1200, 300);
    Check(Start(true), "the overlay starts before the swap chain is made");
    Chain a;
    if (!ChainMake(a)) { Check(false, "a WARP swap chain"); return; }
    NgChain *c = ChainBlock();
    if (!c) { Check(false, "the chain block"); return; }
    Check(Lines(L"Present hooked on a swap chain the game made") == 1, "the chain is taken at its making");
    Check(Presents(a, 4) && g_frames == 0, "presents before the start is done are called on and draw nothing");
    NgOverlayReady();
    Check(Presents(a, 4) && g_frames > 0, "after the word that the start is done the presents draw");
    for (int i = 0; i < 100; i++) Presents(a, 1); // past the wait
    Check(c->want == 0 && c->offered == 0 && Lines(L"THE OVERLAY") == 0 && Lines(L"asking") == 0, "the order hooks then chain: no asking and no line");

    // a late mod that never answers: the present goes on at its bound
    InterlockedIncrement(&c->want);
    ULONGLONG from = GetTickCount64();
    const bool went = SUCCEEDED(a.sc->Present(0, 0));
    ULONGLONG took = GetTickCount64() - from;
    Check(went && took >= 80 && took < 1000 && c->offered == 1 && c->offer == nullptr && Lines(L"not taken within") == 1,
          "an asking nobody answers: offered once, the present goes on after about 100 ms, the offer cleared");
    from = GetTickCount64();
    Presents(a, 3);
    Check(GetTickCount64() - from < 500 && c->offered == 1, "the same asking is not served again");
    // a late mod that answers: the wait ends at its answer
    Taker t = { c, nullptr };
    const HANDLE th = CreateThread(nullptr, 0, TakerThread, &t, 0, nullptr);
    Sleep(50);
    InterlockedIncrement(&c->want);
    from = GetTickCount64();
    const bool went2 = SUCCEEDED(a.sc->Present(0, 0));
    took = GetTickCount64() - from;
    WaitForSingleObject(th, 5000);
    Check(went2 && t.got == (void *)a.sc && took < 80 && c->offered == 2 && c->offer == nullptr && Lines(L"and taken after") == 1,
          "an asking answered: the chain being presented was offered and the wait ended at the answer");
    const LONG frames = g_frames;
    Check(Presents(a, 3) && g_frames > frames, "the mod that offered draws on");
    MenuBlockCheck();
}

// The chain before the hooks, and a mod that draws (this program stands in for its Present).
static void CaseHandover()
{
    Chain a;
    if (!ChainMake(a)) { Check(false, "a WARP swap chain"); return; }
    NgLateWaitSet(2000, 200);
    Check(Start(false), "the second mod starts after the swap chain was made");
    NgChain *c = ChainBlock();
    if (!c) { Check(false, "the chain block"); return; }
    ULONGLONG from = GetTickCount64();
    while (!c->want && GetTickCount64() - from < 3000) Presents(a, 1);
    Check(c->want == 1 && g_frames == 0, "it asks for the chain and draws nothing yet");
    // the first mod's Present: the offer, the wait of at most 100 ms, the offer cleared
    InterlockedExchangePointer(&c->offer, a.sc);
    InterlockedIncrement(&c->offered);
    from = GetTickCount64();
    while (!c->taken && GetTickCount64() - from < 100) Sleep(1);
    const ULONGLONG took = GetTickCount64() - from;
    const void *left = InterlockedExchangePointer(&c->offer, nullptr);
    Check(c->taken == 1 && took < 100 && !left, "the late mod took the offer within the first mod's wait");
    Check(Presents(a, 6) && g_frames > 0, "the late mod draws after the handover");
    Check(Lines(L"the swap chain taken from another Next Gen mod's Present") == 1 && Lines(L"Present hooked on a swap chain the game made") == 1, "its log says where the chain came from");
    for (int i = 0; i < 120; i++) Presents(a, 1); // past the wait
    Check(Lines(L"THE OVERLAY") == 0 && c->want == 1 && c->offered == 1, "no line in capitals, no second asking, and it offers its own asking to nobody");
}

int wmain(int argc, wchar_t **argv)
{
    InitializeCriticalSection(&g_lock);
    const std::wstring mode = argc > 1 ? argv[1] : L"";
    if (mode == L"late") CaseLate();
    else if (mode == L"order") CaseOrder();
    else if (mode == L"handover") CaseHandover();
    else
    {
        printf("late_test.exe late | order | handover\n");
        return 2;
    }
    if (g_failed)
    {
        EnterCriticalSection(&g_lock);
        for (const std::wstring &l : g_lines) wprintf(L"  log: %s\n", l.c_str());
        LeaveCriticalSection(&g_lock);
    }
    printf(g_failed ? "LATE TEST %ls FAILED (%d)\n" : "LATE TEST %ls OK\n", mode.c_str(), g_failed);
    return g_failed ? 1 : 0;
}
