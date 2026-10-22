// menu_preview.cpp: Next Gen's overlay without the game. A hidden window gets a swap chain on the WARP device, made
// through this program's imports of CreateDXGIFactory1 and D3D11CreateDevice as the game makes its own, so the overlay
// (src\ng_overlay.cpp, the mod's own file) hooks its Present as it hooks the game's, and the NEXT GEN menu is
// drawn into it with the mod's Gameplay tab (src\settings_page.h) and the cluster (src\gauge_draw.h) on demo values. A
// second tab, as the Next Gen Tire Inflation mod would add, is put into the shared block by hand. What the overlay
// drew is taken from the back buffer right before the real Present and written as PNG files:
//   menu_preview.exe <out dir> [font dir]
// <font dir> holds the game's TTLakesCompressed-*.ttf (the game's Sources\Bin folder); the overlay reads them next to
// the exe, so they are copied there first when they are not there yet. Writes menu_1080p.png and menu_1440p.png.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wincodec.h>
#include <stdio.h>
#include <vector>
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "ng_overlay.h"
#include "gauges.h"
#include "gauge_draw.h"
#include "settings_page.h"

volatile LONG g_pageCapturing = 0; // gauge_overlay.cpp's, which is not part of this program
void DamageTestAsk(int) {}         // gauges.cpp's: the tab's test buttons write nothing here
void GaugeLog(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vwprintf(fmt, ap);
    va_end(ap);
    wprintf(L"\n");
}

// The block's layout as ng_overlay.cpp has it (kNgSharedVersion 2): the test opens the menu and adds a tab through it.
struct NgSharedCopy
{
    volatile LONG version, open, chosen, toggledAt, key, inputSlot, wheelTurn, charCount, chars[32], cursorX, cursorY, placeTurn;
    float x, y, w, h;
    volatile LONG64 table;
    struct Slot
    {
        volatile LONG used, ready;
        LONG order;
        char tab[40];
    } slots[8];
};

typedef HRESULT(STDMETHODCALLTYPE *PresentFn)(IDXGISwapChain *, UINT, UINT);
static PresentFn g_realPresent = nullptr;
static const wchar_t *g_shotPath = nullptr; // where the next Present's picture goes
static int g_failed = 0;

static bool SavePng(const wchar_t *path, const uint8_t *bgra, UINT w, UINT h)
{
    IWICImagingFactory *wic = nullptr;
    IWICStream *st = nullptr;
    IWICBitmapEncoder *enc = nullptr;
    IWICBitmapFrameEncode *fr = nullptr;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) && SUCCEEDED(wic->CreateStream(&st)) &&
              SUCCEEDED(st->InitializeFromFilename(path, GENERIC_WRITE)) && SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
              SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) && SUCCEEDED(fr->Initialize(nullptr)) &&
              SUCCEEDED(fr->SetSize(w, h)) && SUCCEEDED(fr->SetPixelFormat(&fmt)) && IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA) &&
              SUCCEEDED(fr->WritePixels(h, w * 4, w * h * 4, (BYTE *)bgra)) && SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
    if (fr) fr->Release();
    if (enc) enc->Release();
    if (st) st->Release();
    if (wic) wic->Release();
    return ok;
}

// Under the overlay's hook in the chain: it sees the back buffer with the overlay drawn, before the real Present.
static HRESULT STDMETHODCALLTYPE ShotPresent(IDXGISwapChain *sc, UINT sync, UINT flags)
{
    if (g_shotPath)
    {
        ID3D11Texture2D *bb = nullptr, *staging = nullptr;
        ID3D11Device *dev = nullptr;
        ID3D11DeviceContext *ctx = nullptr;
        bool ok = SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb)) && SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), (void **)&dev));
        D3D11_TEXTURE2D_DESC td = {};
        if (ok)
        {
            dev->GetImmediateContext(&ctx);
            bb->GetDesc(&td);
            td.Usage = D3D11_USAGE_STAGING;
            td.BindFlags = 0;
            td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            td.MiscFlags = 0;
            ok = SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &staging));
        }
        if (ok)
        {
            ctx->CopyResource(staging, bb);
            D3D11_MAPPED_SUBRESOURCE map = {};
            ok = SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map));
            if (ok)
            {
                std::vector<uint8_t> bgra((size_t)td.Width * td.Height * 4);
                for (UINT y = 0; y < td.Height; y++)
                {
                    const uint8_t *s = (const uint8_t *)map.pData + (size_t)y * map.RowPitch;
                    uint8_t *d = bgra.data() + (size_t)y * td.Width * 4;
                    for (UINT x = 0; x < td.Width; x++, s += 4, d += 4) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255; }
                }
                ctx->Unmap(staging, 0);
                ok = SavePng(g_shotPath, bgra.data(), td.Width, td.Height);
            }
        }
        wprintf(L"%s %s\n", ok ? L"wrote" : L"FAILED", g_shotPath);
        if (!ok) g_failed++;
        if (staging) staging->Release();
        if (ctx) ctx->Release();
        if (dev) dev->Release();
        if (bb) bb->Release();
        g_shotPath = nullptr;
    }
    return g_realPresent(sc, sync, flags);
}

// The page and the cluster, on demo values.
static GaugeSettings g_set;
static GaugeView g_viewDemo;
static PageState g_page;
static GaugeFonts g_fonts;
static bool g_fontFiles = false;
static int g_act = 0;

static void Frame()
{
    ImGuiIO &io = ImGui::GetIO();
    ImDrawList *bg = ImGui::GetBackgroundDrawList();
    // a backdrop in the game's colours: sky, hills, mud
    bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(io.DisplaySize.x, io.DisplaySize.y * 0.55f), IM_COL32(118, 140, 158, 255), IM_COL32(118, 140, 158, 255),
                                IM_COL32(170, 176, 170, 255), IM_COL32(170, 176, 170, 255));
    bg->AddRectFilledMultiColor(ImVec2(0, io.DisplaySize.y * 0.55f), io.DisplaySize, IM_COL32(86, 92, 64, 255), IM_COL32(86, 92, 64, 255), IM_COL32(58, 52, 40, 255),
                                IM_COL32(58, 52, 40, 255));
    const float u = GaugeUnit(io.DisplaySize.y, g_set);
    if (g_fontFiles && (!g_fonts.tex || fabsf(g_fonts.u - u) > 0.001f))
    {
        NgTextureFree(g_fonts.tex);
        g_fonts.tex = 0;
        if (FontsBake(g_fonts, u))
        {
            g_fonts.tex = NgTextureMake(g_fonts.rgba, g_fonts.w, g_fonts.h);
            free(g_fonts.rgba);
            g_fonts.rgba = nullptr;
        }
    }
    GaugeDraw(bg, g_fonts, GaugeOrigin(g_set, io.DisplaySize.x, io.DisplaySize.y), u, g_set.opacity, g_viewDemo.st, GaugeLayoutOf(g_set));
}

static void Page() { g_act |= SettingsPage(g_set, g_viewDemo, g_page, 0); }

static void DeviceGone()
{
    NgTextureFree(g_fonts.tex);
    g_fonts.tex = 0;
}

static void Log(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    wprintf(L"  log: ");
    vwprintf(fmt, ap);
    va_end(ap);
    wprintf(L"\n");
}

static bool Run(const wchar_t *out, int w, int h)
{
    const HWND wnd = CreateWindowExW(0, L"STATIC", L"menu preview", WS_OVERLAPPEDWINDOW, 0, 0, w, h, nullptr, nullptr, nullptr, nullptr);
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = (UINT)w;
    sd.BufferDesc.Height = (UINT)h;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = wnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    // the game's way: a factory from CreateDXGIFactory1, a device on one of its adapters, the swap chain from the factory
    IDXGIFactory1 *f = nullptr;
    IDXGIFactory4 *f4 = nullptr;
    IDXGIAdapter *warp = nullptr;
    IDXGISwapChain *sc = nullptr;
    ID3D11Device *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    if (wnd && SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&f)) && SUCCEEDED(f->QueryInterface(__uuidof(IDXGIFactory4), (void **)&f4)))
        f4->EnumWarpAdapter(__uuidof(IDXGIAdapter), (void **)&warp);
    const bool made = warp && SUCCEEDED(D3D11CreateDevice(warp, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx)) &&
                      SUCCEEDED(f->CreateSwapChain(dev, &sd, &sc));
    if (warp) warp->Release();
    if (f4) f4->Release();
    if (f) f->Release();
    if (!made)
    {
        printf("FAIL no WARP swap chain\n");
        return false;
    }
    for (int i = 0; i < 8; i++)
    {
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\menu_%dp.png", out, h);
        if (i == 7) g_shotPath = path;
        sc->Present(0, 0);
        Sleep(20);
    }
    ctx->Release();
    dev->Release();
    sc->Release();
    DestroyWindow(wnd);
    return true;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 2)
    {
        printf("menu_preview.exe <out dir> [font dir]\n");
        return 2;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    *wcsrchr(exe, L'\\') = 0;
    if (argc > 2)
        for (const wchar_t *f : { L"TTLakesCompressed-Medium.ttf", L"TTLakesCompressed-DemiBold.ttf" })
        {
            wchar_t from[MAX_PATH], to[MAX_PATH];
            swprintf_s(from, L"%s\\%s", argv[2], f);
            swprintf_s(to, L"%s\\%s", exe, f);
            CopyFileW(from, to, TRUE);
        }
    g_fontFiles = FontsLoad(g_fonts, exe, kGaugeStyles, kGsStyleCount);
    SettingsDefaults(g_set);
    g_set.style = kFaceTinted;
    g_viewDemo.show = g_viewDemo.demo = true;
    GaugeDemo(g_viewDemo.st, g_set);
    strcpy_s(g_viewDemo.truck, "chevrolet_ck1500");
    g_viewDemo.engineDamage = 14;
    g_viewDemo.engineCapacity = 150;

    // the chain the game would have: the real Present, this test's picture taker, then the overlay over both
    {
        const HWND wnd = CreateWindowExW(0, L"STATIC", L"probe", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, nullptr, nullptr);
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 1;
        sd.BufferDesc.Width = sd.BufferDesc.Height = 64;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = wnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        IDXGISwapChain *sc = nullptr;
        ID3D11Device *dev = nullptr;
        ID3D11DeviceContext *ctx = nullptr;
        if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, nullptr, &ctx)))
        {
            printf("FAIL no WARP swap chain\n");
            return 1;
        }
        void **table = *(void ***)sc;
        DWORD old = 0;
        VirtualProtect(&table[8], sizeof(void *), PAGE_READWRITE, &old);
        g_realPresent = (PresentFn)table[8];
        table[8] = (void *)&ShotPresent;
        VirtualProtect(&table[8], sizeof(void *), old, &old);
        sc->Release();
        ctx->Release();
        dev->Release();
        DestroyWindow(wnd);
    }
    NgClient client = {};
    client.tab = "Gameplay";
    client.version = GAUGES_VERSION;
    client.order = 1;
    client.log = Log;
    client.frame = Frame;
    client.page = Page;
    client.deviceGone = DeviceGone;
    if (!NgOverlayStart(client))
    {
        printf("FAIL the overlay did not start\n");
        return 1;
    }
    // the menu open, a second mod's tab beside this one
    wchar_t name[64];
    swprintf_s(name, L"Local\\NextGen.Menu.%lu", GetCurrentProcessId());
    const HANDLE map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
    NgSharedCopy *sh = map ? (NgSharedCopy *)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(NgSharedCopy)) : nullptr;
    if (!sh || sh->version != 2)
    {
        printf("FAIL the shared block is not there\n");
        return 1;
    }
    sh->slots[1].used = 1;
    sh->slots[1].order = 2;
    strcpy_s(sh->slots[1].tab, "Tire Inflation");
    sh->slots[1].ready = 1;
    sh->open = 1;
    Run(argv[1], 1920, 1080);
    // the window's place is the shared one: a new size starts it in the middle again. The overlay keeps to the swap
    // chain it drew on until that one has not presented for 3 s, as when the game makes its own anew
    Sleep(3100);
    sh->w = sh->h = 0.0f;
    InterlockedIncrement(&sh->placeTurn);
    Run(argv[1], 2560, 1440);
    printf("%s the Gameplay tab reports no change when nothing is touched\n", g_act ? "FAIL" : "ok  ");
    if (g_act) g_failed++;
    CoUninitialize();
    printf(g_failed ? "MENU PREVIEW FAILED (%d)\n" : "MENU PREVIEW OK\n", g_failed);
    return g_failed ? 1 : 0;
}
