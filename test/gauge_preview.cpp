// gauge_preview.cpp: draws the gauge cluster and the Gauges tab without the game, into PNG files, with the mod's own
// code (src\gauge_draw.h, src\settings_page.h) and the mod's own placement of the cluster. Dear ImGui is compiled in
// (its full sources, see test\build_preview.bat) and renders on a Direct3D 11 WARP device: no game, display or
// graphics card needed.
//   gauge_preview.exe <out dir> <font dir> [game screenshot.png]
// <font dir> is the game's Sources\Bin folder (the font files are read from there). With a screenshot the cluster is
// drawn over it, otherwise over a plain backdrop. Writes, in <out dir>:
//   strip_open.png, strip_tinted.png, strip_solid.png   the strip layout in each style on a 1920 x 1080 screen, demo values
//   block_open.png, block_tinted.png, block_solid.png   the block layout, the same
//   strip_swapped.png, block_swapped.png   both layouts with the speedometer and the tachometer swapped (SwapDials)
//   strip_2160p.png    the default settings on a 3840 x 2160 screen
//   strip_states.png   the strip at twice its size: a cold start, the demo values, overheating on a slope
//   block_states.png   the block, the same three states
//   live_states.png    the strip with what this version shows of a truck: parked and cold, automatic, the low range and hot
//   options.png        other settings: scout scales, mph, psi and Fahrenheit, the air dial on, the lamps off, larger, moved
//   oil_dial.png       the strip and the block with the oil on a dial of its own, the oil too hot and its lamp lit
//   fallback.png       the strip with the ImGui font standing in (no game font files)
//   settings.png       the Gauges tab of ReShade's overlay at ReShade FontScale 2 (ReShade's own style differs), with a
//                      truck driven that has scales of its own
//   settings_plain.png, settings_none.png   the same with a truck that shows the plain scales, and with no truck driven
#define IMGUI_DEFINE_MATH_OPERATORS
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <stdio.h>
#include <vector>
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "gauges.h"
#include "gauge_draw.h"
#include "settings_page.h"
#include "checkers_board.h"
#include "imgui_internal.h"
#include <string>
#include <algorithm>
#include <iterator>

// Dear ImGui's test engine hooks (the preview is built with IMGUI_ENABLE_TEST_ENGINE): each widget's label as it is
// drawn, kept while g_items is set, for the list of the Gameplay tab's settings
static std::vector<std::string> *g_items = nullptr;
// a combo and the plot tell no label to the hooks, only their id: it is matched against their labels (settings_page.h's)
// under the ID stack in force when they are added
void ImGuiTestEngineHook_ItemAdd(ImGuiContext *ctx, ImGuiID id, const ImRect &, const ImGuiLastItemData *)
{
    static const char *const quiet[] = { "##aspiration", "##class", "##dampercurve", "##diffaxle1", "##diffaxle2", "##fuel", "##gamegeardisplay", "##heldgearslip", "##input", "##layout", "##oilgauge", "##quickwinch", "##spool", "##springcurve", "##style", "##torquecurve" };
    if (!g_items || !ctx->CurrentWindow || ctx->CurrentWindow->IDStack.Size == 0) return;
    for (const char *l : quiet)
        if (ctx->CurrentWindow->GetID(l) == id) g_items->push_back(l);
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext *, ImGuiID, const char *label, ImGuiItemStatusFlags)
{
    if (g_items && label) g_items->push_back(label);
}
void ImGuiTestEngineHook_Log(ImGuiContext *, const char *, ...) {}
const char *ImGuiTestEngine_FindItemDebugLabel(ImGuiContext *, ImGuiID) { return nullptr; }

// The settings and the view that draw every row the Gameplay tab has: the mod's own gearbox, Damage and the clutch's
// heat, the engine handed over, an engine of its own with a curve of its own, a gearbox, the creep in a held gear, the
// air suspension and the probe (the Reviewer's point 6 on BRIEF_overlay_tabs_and_text.md)
static void PageWidest(GaugeSettings &s, GaugeView &v)
{
    SettingsDefaults(s);
    s.style = kFaceTinted;
    s.damage = s.clutchBurn = s.powertrain = s.roadLoad = s.creep = s.creepManual = s.airSuspension = s.airSync = true;
    s.gearbox = kGearboxOwn;
    s.probe = 1;
    s.soundVolume = 60;
    v = GaugeView{};
    v.show = true;
    strcpy_s(v.truck, "mod_suv/1000001");
    v.truckOwn = true;
    strcpy_s(v.engine, "us_scout_old");
    v.engineOwn = v.engineCustom = true;
    v.enginePeakRpm = 2000.0f;
    v.engineIdle = 800.0f;
    v.engineRedline = 5500.0f;
    v.engineTorque = 1.0f;
    strcpy_s(v.gearbox, "g_scout_highrange");
    v.gearHold = 1.5f;
    v.engineCapacity = 150;
    v.canReverse = true;
    v.captureAction = v.captureSlot = -1;
    // two rows by axle on the Power tab (BRIEF_axle_diff.md): the front waits for all wheel drive
    v.diffRows = 2, v.diffRowAxle[0] = 0, v.diffRowAxle[1] = 1, v.diffRowNow[1] = true, v.diffKind = 0;
    for (int a = 0; a < kViewActions; a++) strcpy_s(v.bindText[a][0], "None"), strcpy_s(v.bindText[a][1], "None");
}

volatile LONG g_pageCapturing = 0; // gauge_overlay.cpp's, which is not part of this program
void DamageTestAsk(int) {}         // gauges.cpp's: the tab's test buttons write nothing here
void BindingAsk(int, int, int) {}  // and the gearbox's bindings are not captured here

static ID3D11Device *g_dev = nullptr;
static ID3D11DeviceContext *g_ctx = nullptr;
static IWICImagingFactory *g_wic = nullptr;
static int g_failed = 0;

template <typename T> static void Release(T *&p) { if (p) { p->Release(); p = nullptr; } }

static ID3D11ShaderResourceView *MakeTexture(const uint8_t *rgba, int w, int h)
{
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA sd = { rgba, (UINT)w * 4, 0 };
    ID3D11Texture2D *tex = nullptr;
    ID3D11ShaderResourceView *srv = nullptr;
    if (SUCCEEDED(g_dev->CreateTexture2D(&td, &sd, &tex))) g_dev->CreateShaderResourceView(tex, nullptr, &srv);
    Release(tex);
    return srv;
}

static bool LoadImageRgba(const wchar_t *path, std::vector<uint8_t> &out, UINT &w, UINT &h)
{
    IWICBitmapDecoder *dec = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *conv = nullptr;
    bool ok = SUCCEEDED(g_wic->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
              SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(g_wic->CreateFormatConverter(&conv)) &&
              SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) &&
              SUCCEEDED(conv->GetSize(&w, &h));
    if (ok)
    {
        out.resize((size_t)w * h * 4);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, (UINT)out.size(), out.data()));
    }
    Release(conv);
    Release(frame);
    Release(dec);
    return ok;
}

static bool SavePng(const wchar_t *path, const uint8_t *bgra, UINT w, UINT h)
{
    IWICStream *st = nullptr;
    IWICBitmapEncoder *enc = nullptr;
    IWICBitmapFrameEncode *fr = nullptr;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    bool ok = SUCCEEDED(g_wic->CreateStream(&st)) && SUCCEEDED(st->InitializeFromFilename(path, GENERIC_WRITE)) &&
              SUCCEEDED(g_wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) && SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) &&
              SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) && SUCCEEDED(fr->Initialize(nullptr)) && SUCCEEDED(fr->SetSize(w, h)) &&
              SUCCEEDED(fr->SetPixelFormat(&fmt)) && IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA) &&
              SUCCEEDED(fr->WritePixels(h, w * 4, w * h * 4, (BYTE *)bgra)) && SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
    Release(fr);
    Release(enc);
    Release(st);
    return ok;
}

// frames ImGui frames of w x h: draw() fills the draw lists or makes windows; the last frame goes to path as PNG (a
// window sized by its content needs a frame to find its size).
template <typename F> static bool RenderPng(const wchar_t *path, int w, int h, F draw, int frames = 1)
{
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *rt = nullptr, *staging = nullptr;
    ID3D11RenderTargetView *rtv = nullptr;
    bool ok = SUCCEEDED(g_dev->CreateTexture2D(&td, nullptr, &rt)) && SUCCEEDED(g_dev->CreateRenderTargetView(rt, nullptr, &rtv));
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ok = ok && SUCCEEDED(g_dev->CreateTexture2D(&td, nullptr, &staging));
    if (ok)
    {
        ImGuiIO &io = ImGui::GetIO();
        const float grey[4] = { 0.27f, 0.30f, 0.26f, 1.0f };
        const D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
        for (int i = 0; i < frames; i++)
        {
            io.DisplaySize = ImVec2((float)w, (float)h);
            io.DeltaTime = 1.0f / 60.0f;
            ImGui_ImplDX11_NewFrame();
            ImGui::NewFrame();
            draw(ImGui::GetForegroundDrawList(), ImGui::GetBackgroundDrawList());
            ImGui::Render();
            g_ctx->OMSetRenderTargets(1, &rtv, nullptr);
            g_ctx->RSSetViewports(1, &vp);
            g_ctx->ClearRenderTargetView(rtv, grey);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        g_ctx->CopyResource(staging, rt);
        D3D11_MAPPED_SUBRESOURCE map = {};
        ok = SUCCEEDED(g_ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map));
        if (ok)
        {
            std::vector<uint8_t> bgra((size_t)w * h * 4);
            for (int y = 0; y < h; y++)
            {
                const uint8_t *s = (const uint8_t *)map.pData + (size_t)y * map.RowPitch;
                uint8_t *d = bgra.data() + (size_t)y * w * 4;
                for (int x = 0; x < w; x++, s += 4, d += 4) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255; }
            }
            g_ctx->Unmap(staging, 0);
            ok = SavePng(path, bgra.data(), (UINT)w, (UINT)h);
        }
    }
    Release(staging);
    Release(rtv);
    Release(rt);
    wprintf(L"%s %s\n", ok ? L"wrote" : L"FAILED", path);
    if (!ok) g_failed++;
    return ok;
}

// Bakes the font atlas for u and makes it the texture the drawing uses.
static ID3D11ShaderResourceView *g_fontSrv = nullptr;
static bool UseFonts(GaugeFonts &f, float u)
{
    Release(g_fontSrv);
    if (!FontsBake(f, u)) return false;
    g_fontSrv = MakeTexture(f.rgba, f.w, f.h);
    free(f.rgba);
    f.rgba = nullptr;
    f.tex = (ImTextureID)(intptr_t)g_fontSrv;
    printf("font atlas %dx%d for %.3f px per reference px\n", f.w, f.h, u);
    return g_fontSrv != nullptr;
}

// The game screenshot when there is one, else a plain backdrop with a horizon, so the cluster is judged against
// something like the game image.
static ID3D11ShaderResourceView *g_shot = nullptr;
static void Backdrop(ImDrawList *bg, float w, float h)
{
    if (g_shot)
    {
        bg->AddImage((ImTextureID)(intptr_t)g_shot, ImVec2(0, 0), ImVec2(w, h));
        return;
    }
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(w, h * 0.55f), IM_COL32(118, 132, 140, 255));
    bg->AddRectFilled(ImVec2(0, h * 0.55f), ImVec2(w, h), IM_COL32(74, 70, 58, 255));
    bg->AddRectFilled(ImVec2(0, h * 0.72f), ImVec2(w, h), IM_COL32(62, 58, 48, 255));
}

// The cluster on a w x h screen, placed and sized as the mod does it for these settings.
static void Screen(const wchar_t *out, const wchar_t *name, GaugeFonts &fonts, int w, int h, const GaugeSettings &s, const GaugeState &st)
{
    const float u = GaugeUnit((float)w, (float)h, s);
    if (fonts.tex) UseFonts(fonts, u);
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\%s", out, name);
    RenderPng(path, w, h, [&](ImDrawList *, ImDrawList *bg) {
        Backdrop(bg, (float)w, (float)h);
        const ImVec2 o = GaugeOrigin(s, (float)w, (float)h);
        // the HUD layouts: Spotter's gear panel as an outline where it stands on the stage (about 340 x 166, 10 to the right)
        if (GaugeOnStage(s))
            bg->AddRect(ImVec2(o.x + 10.0f * u, o.y - 166.0f * u), ImVec2(o.x + 350.0f * u, o.y), IM_COL32(168, 90, 50, 200), 6.0f * u, 0, 1.2f * u);
        GaugeDraw(bg, fonts, o, u, s.opacity, st, GaugeLayoutOf(s));
    });
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 3)
    {
        fwprintf(stderr, L"usage: gauge_preview <out dir> <font dir> [game screenshot.png]\n");
        return 2;
    }
    const wchar_t *out = argv[1], *fontDir = argv[2], *shotPath = argc > 3 ? argv[3] : nullptr;
    CreateDirectoryW(out, nullptr);
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic))))
    {
        fwprintf(stderr, L"no WIC\n");
        return 1;
    }
    D3D_FEATURE_LEVEL fl;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &g_dev, &fl, &g_ctx)))
    {
        fwprintf(stderr, L"no D3D11 WARP device\n");
        return 1;
    }
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    static GaugeFonts fonts;
    const bool gameFont = FontsLoad(fonts, fontDir, kGaugeStyles, kGsStyleCount) && UseFonts(fonts, 1.0f);
    if (!gameFont) wprintf(L"game font not found in %s: the ImGui font stands in\n", fontDir);

    std::vector<uint8_t> shot;
    UINT sw = 0, sh = 0;
    if (shotPath && LoadImageRgba(shotPath, shot, sw, sh)) g_shot = MakeTexture(shot.data(), (int)sw, (int)sh);
    if (shotPath && !g_shot) wprintf(L"screenshot not read: %s (a plain backdrop stands in)\n", shotPath);

    GaugeSettings def;
    SettingsDefaults(def);
    // three states: a cold start in the yard, the mod's demo values (cruising), and an overheating engine on a slope
    GaugeState drive;
    GaugeDemo(drive, def);
    strcpy_s(drive.odometer, "123456 km"); // the widest figure (the Reviewer's point 8 on BRIEF_odometer.md)
    GaugeState cold = drive;
    cold.speedKmh = 0.0f;
    cold.rpm = 720.0f;
    cold.gear = 0;
    cold.fuelFrac = 0.71f;
    cold.rangeKm = -1.0f;
    cold.tempC = 14.0f;
    cold.oilC = 14.0f;
    cold.boostBar = 0.0f;
    cold.airBar = 7.6f;
    cold.handbrake = true;
    cold.cold = true;
    cold.awd = cold.diffLock = cold.headlights = false;
    cold.rollDeg = 0.0f;
    GaugeState hot = drive;
    hot.speedKmh = 11.0f;
    hot.rpm = 2450.0f;
    hot.gear = 1;
    hot.gearMode = "L+";
    hot.lowGear = true;
    hot.fuelFrac = 0.18f;
    hot.rangeKm = 90.0f;
    strcpy_s(hot.odometer, "99999.9 h");
    hot.tempC = 121.0f;
    hot.oilC = 131.0f;
    hot.oilHot = true;
    hot.boostBar = 2.4f;
    hot.boostTop = 2.6f; // a turbo of more than 2 bar: the dial to 3
    hot.airBar = 6.0f;
    hot.damage = true;
    hot.tilt = 2;
    hot.rollDeg = -27.0f;
    hot.headlights = false;
    const GaugeState states[3] = { cold, drive, hot };

    // both layouts in the three styles, where the mod puts them on a 1080p screen
    static const struct { const wchar_t *name; int layout, style; bool swap; } looks[8] = {
        { L"strip_open.png", kLayoutStrip, kFaceOpen }, { L"strip_tinted.png", kLayoutStrip, kFaceTinted }, { L"strip_solid.png", kLayoutStrip, kFaceSolid },
        { L"block_open.png", kLayoutBlock, kFaceOpen }, { L"block_tinted.png", kLayoutBlock, kFaceTinted }, { L"block_solid.png", kLayoutBlock, kFaceSolid },
        { L"strip_swapped.png", kLayoutStrip, kFaceOpen, true }, { L"block_swapped.png", kLayoutBlock, kFaceOpen, true } };
    for (const auto &l : looks)
    {
        GaugeSettings s = def;
        s.layout = l.layout;
        s.style = l.style;
        s.swapDials = l.swap;
        Screen(out, l.name, fonts, 1920, 1080, s, drive);
    }
    Screen(out, L"strip_2160p.png", fonts, 3840, 2160, def, drive);

    // the HUD layouts (BRIEF_cluster_hud_layouts.md): docked left of the gear panel's outline at 16:9, 21:9 and 16:10,
    // all four cells and the tire lamp shown; then each at x2.5 in the three states, to hold against the mockups
    {
        GaugeSettings s = def;
        s.showAir = true;
        s.oilGauge = kOilDial;
        static const struct { const wchar_t *name; int w, h; } screens[3] = { { L"1920x1080", 1920, 1080 }, { L"2560x1080", 2560, 1080 }, { L"1920x1200", 1920, 1200 } };
        GaugeState st = drive;
        st.tireLamp = 3;
        for (int layout = kLayoutHudDials; layout <= kLayoutHudDigital; layout++)
        {
            s.layout = layout;
            for (const auto &sc : screens)
            {
                wchar_t name[64];
                swprintf_s(name, L"%s_%s.png", layout == kLayoutHudDials ? L"hud_dials" : L"hud_digital", sc.name);
                Screen(out, name, fonts, sc.w, sc.h, s, st);
            }
            GaugeSettings z = s;
            z.scale = 2.5f;
            static const wchar_t *states3[3] = { L"cold", L"drive", L"hot" };
            for (int i = 0; i < 3; i++)
            {
                GaugeState zs = states[i];
                zs.tireLamp = 3;
                wchar_t name[64];
                swprintf_s(name, L"%s_zoom_%s.png", layout == kLayoutHudDials ? L"hud_dials" : L"hud_digital", states3[i]);
                Screen(out, name, fonts, 1920, 1080, z, zs);
            }
        }
        // Style is the strip's and the block's: a tinted or open style changes nothing on the HUD layouts
        {
            GaugeSettings t = s;
            bool ignored = true;
            for (int style = 0; style < kFaceCount; style++)
            {
                t.style = style;
                t.faceOpacity = 0.3f;
                const GaugeLayout lay = GaugeLayoutOf(t);
                ignored &= lay.faceAlpha == 1.0f && !lay.openArc;
            }
            printf("%s   HUD layouts: Style and FaceOpacity are ignored\n", ignored ? "ok" : "FAIL");
            if (!ignored) g_failed++;
            // the cells close up: oil off and air off leave coolant and boost, in that order; a naturally aspirated
            // engine has no boost cell
            GaugeSettings c2 = s;
            c2.oilGauge = kOilOff;
            c2.showAir = false;
            HudCell cells[4];
            GaugeState nat = st;
            const int two = HudCells(GaugeLayoutOf(c2), st, cells);
            const bool order = two == 2 && cells[0].kind == kHudTemp && cells[1].kind == kHudBoost;
            nat.natural = true;
            const int one = HudCells(GaugeLayoutOf(c2), nat, cells);
            const bool closes = order && one == 1 && cells[0].kind == kHudTemp && HudCells(GaugeLayoutOf(s), st, cells) == 4;
            printf("%s   HUD layouts: hidden cells close up (%d of 4, %d with a naturally aspirated engine)\n", closes ? "ok" : "FAIL", two, one);
            if (!closes) g_failed++;
            // the air cell goes by the state's noAir, which the mod's thread works out for the truck, not by the plain
            // scales' class (BRIEF_air_brakes_by_truck.md, M2)
            GaugeSettings airSet = s;
            airSet.showAir = true, airSet.tachClass = airSet.scaleClass = kClassScout, airSet.airBrakes = -1;
            GaugeState noAir = st;
            noAir.noAir = true;
            auto hasAir = [&](const GaugeState &state) {
                HudCell all[8];
                const int n = HudCells(GaugeLayoutOf(airSet), state, all);
                for (int i = 0; i < n; i++)
                    if (all[i].kind == kHudAir) return true;
                return false;
            };
            const bool airByState = hasAir(st) && !hasAir(noAir) && GaugeLayoutOf(airSet).showAir;
            printf("%s   HUD layouts: the air cell is there with the plain scales at Scout and goes by the state's no air brakes alone\n", airByState ? "ok" : "FAIL");
            if (!airByState) g_failed++;
        }
        // hidden cells close up: no oil, no air
        s.oilGauge = kOilOff;
        s.showAir = false;
        s.layout = kLayoutHudDials;
        Screen(out, L"hud_dials_two_cells.png", fonts, 1920, 1080, s, st);
        s.layout = kLayoutHudDigital;
        Screen(out, L"hud_digital_two_cells.png", fonts, 1920, 1080, s, st);
    }

    // other settings: the scout's scales, the other units, the air dial, no lamps, larger, moved up and to the left
    {
        GaugeSettings s = def;
        s.tachClass = s.scaleClass = kClassScout;
        s.mph = s.psi = s.fahrenheit = true;
        s.showAir = true;
        s.showLamps = false;
        s.scale = 1.25f;
        s.offsetX = -300.0f;
        s.offsetY = -40.0f;
        s.opacity = 0.9f;
        GaugeState st;
        GaugeDemo(st, s);
        st.boostVacuum = true; // a petrol scout at a light foot: the dial from -15 psi
        st.boostBar = -0.45f;
        Screen(out, L"options.png", fonts, 1920, 1080, s, st);
    }

    // the oil on a dial of its own, too hot, in both layouts at twice the size
    {
        ID3D11ShaderResourceView *keep = g_shot;
        g_shot = nullptr;
        if (gameFont) UseFonts(fonts, 2.0f);
        GaugeSettings s = def;
        s.oilGauge = kOilDial;
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\oil_dial.png", out);
        RenderPng(path, 1800, 420 + 580, [&](ImDrawList *, ImDrawList *bg) {
            Backdrop(bg, 1800, 420 + 580);
            GaugeDraw(bg, fonts, ImVec2(900.0f, 420.0f - 24.0f), 2.0f, 1.0f, hot, GaugeLayoutOf(s));
            GaugeSettings b = s;
            b.layout = kLayoutBlock;
            GaugeDraw(bg, fonts, ImVec2(1300.0f, 1000.0f - 24.0f), 2.0f, 1.0f, hot, GaugeLayoutOf(b));
        });
        if (gameFont) UseFonts(fonts, 1.0f);
        g_shot = keep;
    }

    // the three states at twice the size, on the plain backdrop: the strip one above the other, the block side by side
    {
        ID3D11ShaderResourceView *keep = g_shot;
        g_shot = nullptr;
        if (gameFont) UseFonts(fonts, 2.0f);
        GaugeSettings s = def;
        s.showAir = true;
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\strip_states.png", out);
        RenderPng(path, 1560, 3 * 420, [&](ImDrawList *, ImDrawList *bg) {
            Backdrop(bg, 1560, 3 * 420);
            for (int i = 0; i < 3; i++) GaugeDraw(bg, fonts, ImVec2(780.0f, 420.0f * (i + 1) - 24.0f), 2.0f, 1.0f, states[i], GaugeLayoutOf(s));
        });
        s.layout = kLayoutBlock;
        swprintf_s(path, L"%s\\block_states.png", out);
        RenderPng(path, 3 * 700, 580, [&](ImDrawList *, ImDrawList *bg) {
            Backdrop(bg, 3 * 700, 580);
            for (int i = 0; i < 3; i++) GaugeDraw(bg, fonts, ImVec2(700.0f * (i + 1) - 24.0f, 580.0f - 24.0f), 2.0f, 1.0f, states[i], GaugeLayoutOf(s));
        });
        g_shot = keep;
    }

    // close looks at four times the size, for judging the drawing: the tachometer, the lamps and three small dials in
    // each style, with half of the lamps lit
    {
        ID3D11ShaderResourceView *keep = g_shot;
        g_shot = nullptr;
        if (gameFont) UseFonts(fonts, 4.0f);
        GaugeState look = drive;
        look.rpm = 1650.0f;
        look.handbrake = look.lowGear = look.cold = true;
        look.diffLock = false;
        look.tilt = 1;
        look.rollDeg = 17.0f;
        look.tempC = 104.0f;
        look.oilC = 112.0f;
        look.oilHot = true;
        look.rangeKm = 6.4f;
        static const struct { const wchar_t *name; int style; } closeups[3] = { { L"closeup_open.png", kFaceOpen }, { L"closeup_tinted.png", kFaceTinted }, { L"closeup_solid.png", kFaceSolid } };
        for (const auto &l : closeups)
        {
            GaugeSettings s = def;
            s.style = l.style;
            wchar_t path[MAX_PATH];
            swprintf_s(path, L"%s\\%s", out, l.name);
            RenderPng(path, 2400, 760, [&](ImDrawList *, ImDrawList *bg) {
                Backdrop(bg, 2400, 760);
                // the dials are drawn one by one here, so the style is set as GaugeDraw sets it
                const GaugeLayout lay = GaugeLayoutOf(s);
                const float u = 4.0f;
                const ImVec2 c(1200.0f, 380.0f);
                s_open = lay.openArc;
                s_faceAlpha = s_open ? 0.0f : lay.faceAlpha;
                GTacho(bg, fonts, ImVec2(c.x - 196.0f * u, c.y), 72.0f * u, u, 1.0f, look);
                GLamps(bg, fonts, ImVec2(c.x - 76.0f * u, c.y), 2, 20.0f * u, 3.5f * u, u, 1.0f, look);
                GTemp(bg, fonts, ImVec2(c.x + 12.0f * u, c.y + 14.0f * u), 42.0f * u, u, 1.0f, look, true);
                GFuel(bg, fonts, ImVec2(c.x + 104.0f * u, c.y + 14.0f * u), 42.0f * u, u, 1.0f, look);
                GBoost(bg, fonts, ImVec2(c.x + 196.0f * u, c.y + 14.0f * u), 42.0f * u, u, 1.0f, look);
                GRange(bg, fonts, ImVec2(c.x + 104.0f * u, c.y - 46.0f * u), u, 1.0f, look, lay.showFuelUse);
            });
        }
        // what the drawing costs at 2160p, in the solid style with every dial, which draws the most: the strip once
        // on the picture and 200 times beside it, timed
        if (gameFont) UseFonts(fonts, 2.0f);
        GaugeSettings s = def;
        s.style = kFaceSolid;
        s.showAir = true;
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\solid_2160p_size.png", out);
        RenderPng(path, 1560, 420, [&](ImDrawList *, ImDrawList *bg) {
            Backdrop(bg, 1560, 420);
            GaugeDraw(bg, fonts, ImVec2(780.0f, 396.0f), 2.0f, 1.0f, look, GaugeLayoutOf(s));
            LARGE_INTEGER rate, t0, t1;
            QueryPerformanceFrequency(&rate);
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 200; i++) GaugeDraw(bg, fonts, ImVec2(780.0f, 396.0f + 5000.0f), 2.0f, 1.0f, look, GaugeLayoutOf(s));
            QueryPerformanceCounter(&t1);
            const double solidMs = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)rate.QuadPart / 200.0;
            GaugeSettings open = s;
            open.style = kFaceOpen;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 200; i++) GaugeDraw(bg, fonts, ImVec2(780.0f, 396.0f + 5000.0f), 2.0f, 1.0f, look, GaugeLayoutOf(open));
            QueryPerformanceCounter(&t1);
            wprintf(L"drawing the cluster at the size of a 2160p screen takes %.3f ms in the solid style and %.3f ms in the open style (the mean of 200)\n", solidMs,
                    (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)rate.QuadPart / 200.0);
        });
        if (gameFont) UseFonts(fonts, 1.0f);
        g_shot = keep;
    }

    // what this version shows of a real truck, at twice the size: parked on level ground with the engine off and
    // cold (the cold lamp lit, the temperature needle below its scale), third gear in automatic with the gear half
    // run out at 88 C, leaning 18 degrees to the left, and the low range run out with the differential locked on a
    // side slope of 31 degrees to the right, the coolant in the red zone at 121 C. The range has no number before
    // half a kilometre is driven. The boost dial has no source yet and rests.
    {
        ID3D11ShaderResourceView *keep = g_shot;
        g_shot = nullptr;
        if (gameFont) UseFonts(fonts, 2.0f);
        const GaugeScale k = GaugeScaleOf(def);
        GaugeState parked = {};
        GaugeStateSettings(parked, def);
        parked.gearMode = "";
        parked.fuelFrac = 0.97f;
        parked.rangeKm = -1.0f;
        parked.tilt = 0;
        parked.handbrake = true;
        parked.tempC = parked.oilC = def.ambientC;
        parked.cold = true;
        GaugeState automatic = parked;
        automatic.engineOn = true;
        automatic.tempC = 88.0f;
        automatic.oilC = 81.0f;
        automatic.cold = false;
        automatic.rpm = k.idle + (k.redline - k.idle) * 0.6f;
        automatic.rangeKm = 31.0f;
        automatic.boostBar = 0.9f;
        automatic.speedKmh = 24.0f;
        automatic.gear = 3;
        automatic.gearMode = "A";
        automatic.fuelFrac = 0.88f;
        automatic.handbrake = false;
        automatic.headlights = automatic.awd = true;
        automatic.tilt = 1;
        automatic.rollDeg = -18.0f;
        GaugeState low = automatic;
        low.speedKmh = 6.0f;
        low.gear = 1;
        low.gearMode = "L+";
        low.lowGear = low.diffLock = low.damage = true;
        low.fuelFrac = 0.31f;
        low.rpm = k.redline;
        low.rangeKm = 6.4f;
        low.tilt = 2;
        low.tireLamp = 1; // a tire mod at Low: the tenth lamp, lit
        low.tireLit = true;
        low.rollDeg = 31.0f;
        low.tempC = 121.0f;
        low.oilC = 127.0f;
        low.oilHot = true;
        const GaugeState live[3] = { parked, automatic, low };
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\live_states.png", out);
        RenderPng(path, 1560, 3 * 420, [&](ImDrawList *, ImDrawList *bg) {
            Backdrop(bg, 1560, 3 * 420);
            for (int i = 0; i < 3; i++) GaugeDraw(bg, fonts, ImVec2(780.0f, 420.0f * (i + 1) - 24.0f), 2.0f, 1.0f, live[i], GaugeLayoutOf(def));
        });
        g_shot = keep;
    }

    // the warning boxes at 1080p, as the tire mod's TIRE DAMAGE box (slot 0, drawn here in its shape for the stack), the
    // clutch's amber below it and the engine's red with its film title below that, over the backdrop
    {
        static GaugeFonts warnFonts;
        ID3D11ShaderResourceView *warnSrv = nullptr;
        const float wu = WarnUnit(1920.0f, 1080.0f);
        if (FontsLoad(warnFonts, fontDir, kWarnStyles, kWsStyleCount) && FontsBake(warnFonts, wu))
        {
            warnSrv = MakeTexture(warnFonts.rgba, warnFonts.w, warnFonts.h);
            free(warnFonts.rgba);
            warnFonts.rgba = nullptr;
            warnFonts.tex = (ImTextureID)(intptr_t)warnSrv;
        }
        const WarnBox boxes[3] = { { kWarnRed, "TIRE DAMAGE", "Slow down or raise the pressure", 34 },
                                   { kWarnAmber, "CLUTCH HOT", "Ease off or shift down", 62 },
                                   { kWarnRed, "DANGER TO MANIFOLD", "Keep an eye on the floor pan", 71 } };
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\warnings.png", out);
        RenderPng(path, 1920, 1080, [&](ImDrawList *, ImDrawList *bg) {
            Backdrop(bg, 1920, 1080);
            for (int i = 0; i < 3; i++) GWarnBox(bg, warnFonts, WarnOrigin(1920.0f, 1080.0f, i), wu, 1.0f, boxes[i]);
        });
        // the physics rate sign over them (BRIEF_step_check_always.md): its lines are the text asked for, word for word
        {
            std::string all;
            for (int i = 0; i < kStepSignCount; i++) all += i ? " " : "", all += kStepSignLines[i];
            const bool text = all == "Another mod changes the physics rate. Turn that mod off and start the game again. Next Gen Gameplay can run the physics at 120 itself: "
                                     "Physics rate on the Gameplay tab." &&
                              all.find((char)39) == std::string::npos;
            printf("%s physics rate sign: its four lines are the text asked for, word for word, with no apostrophe\n", text ? "ok  " : "FAIL");
            if (!text) g_failed++;
            GaugeFonts signFonts = {};
            ID3D11ShaderResourceView *signSrv = nullptr;
            if (FontsLoad(signFonts, fontDir, kWarnStyles, kWsStyleCount) && FontsBake(signFonts, kStepSignScale * wu))
            {
                signSrv = MakeTexture(signFonts.rgba, signFonts.w, signFonts.h);
                free(signFonts.rgba);
                signFonts.rgba = nullptr;
                signFonts.tex = (ImTextureID)(intptr_t)signSrv;
            }
            swprintf_s(path, L"%s\\step_sign.png", out);
            RenderPng(path, 1920, 1080, [&](ImDrawList *, ImDrawList *bg) {
                Backdrop(bg, 1920, 1080);
                for (int i = 0; i < 3; i++) GWarnBox(bg, warnFonts, WarnOrigin(1920.0f, 1080.0f, i), wu, 1.0f, boxes[i]);
                GStepSign(bg, signFonts, 1920.0f, 1080.0f, kStepSignScale * wu);
            });
            Release(signSrv);
        }
        // the suspension display below them (build B): an Arocs on a twisted crossing, its front left deep and hitting the
        // end, its front right hanging, the bump stops on; then the sway bars' lamp
        SuspDisplayView d = {};
        d.wheels = 8, d.axles = 4, d.stops = true, d.zone = 0.12f;
        const float x[8] = { -0.62f, 0.02f, -0.35f, -0.1f, -0.2f, -0.12f, -0.15f, -0.18f }, travel[8] = { 0.6f, 0.6f, 0.6f, 0.6f, 0.44f, 0.44f, 0.44f, 0.44f };
        for (int i = 0; i < 8; i++)
        {
            d.x[i] = x[i], d.travel[i] = travel[i], d.travelMax[i] = 0.4f, d.rest[i] = i < 4 ? -0.32f : -0.1f;
            d.placed[i] = true, d.isLeft[i] = i % 2 == 0, d.axle[i] = i / 2;
        }
        d.hit[0] = true, d.hanging[1] = true;
        d.articulationKnown = true, d.articulation = (0.62f - (-0.02f) - (0.15f - 0.18f)) * 100.0f;
        swprintf_s(path, L"%s\\suspension.png", out);
        RenderPng(path, 1920, 1080, [&](ImDrawList *, ImDrawList *bg) {
            Backdrop(bg, 1920, 1080);
            const ImVec2 at = WarnOrigin(1920.0f, 1080.0f, 3);
            GSuspDisplay(bg, warnFonts, at, wu, 1.0f, d);
            GSwayLamp(bg, warnFonts, ImVec2(at.x, at.y + (GSuspHeight(d) + kWarnGap) * wu), wu, 1.0f);
            GRideBox(bg, warnFonts, ImVec2(at.x, at.y + (GSuspHeight(d) + kSwayH + 2.0f * kWarnGap) * wu), wu, 1.0f, 4); // the air suspension raised 4 cm
        });
        // the drive display beside it (131): a made up truck of four axles, the two rear ones driven, at 2 m/s in a forward
        // gear: the third axle's left wheel spinning in the air and clamped by the cap, the rear right one turning back
        DdShown dd = {};
        dd.n = 8, dd.rows = 4, dd.force = kDiffForceLimited, dd.gear = 1, dd.kept = 0.82f;
        const float spd[8] = { 2.0f, 2.0f, 2.1f, 1.9f, 9.5f, 2.6f, 2.2f, -0.6f }, shr[8] = { -1, -1, -1, -1, 0.08f, 0.46f, 0.40f, 0.06f },
                    was[8] = { -1, -1, -1, -1, 0.55f, 0.20f, 0.15f, 0.10f };
        for (int i = 0; i < 8; i++)
        {
            dd.row[i] = (uint8_t)(i / 2), dd.right[i] = i % 2 == 1, dd.ok[i] = true, dd.driven[i] = i >= 4;
            dd.speed[i] = spd[i], dd.truck[i] = 2.0f, dd.share[i] = shr[i], dd.shareBefore[i] = was[i];
        }
        dd.air[4] = true, dd.capLamp[4] = true, dd.lockLamp[5] = dd.lockLamp[7] = true;
        swprintf_s(path, L"%s\\drive.png", out);
        RenderPng(path, 1920, 1080, [&](ImDrawList *, ImDrawList *bg) {
            Backdrop(bg, 1920, 1080);
            const ImVec2 at = WarnOrigin(1920.0f, 1080.0f, 3);
            GSuspDisplay(bg, warnFonts, at, wu, 1.0f, d);
            GDriveDisplay(bg, warnFonts, ImVec2(at.x + (kSuspW + kWarnGap) * wu, at.y), wu, 1.0f, dd);
        });
        Release(warnSrv);
    }

    // without the game's font
    {
        const ImTextureID keep = fonts.tex;
        fonts.tex = 0;
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\fallback.png", out);
        RenderPng(path, 800, 240, [&](ImDrawList *, ImDrawList *bg) {
            bg->AddRectFilled(ImVec2(0, 0), ImVec2(800, 240), IM_COL32(74, 70, 58, 255));
            GaugeDraw(bg, fonts, ImVec2(400.0f, 240.0f - 16.0f), 1.0f, 1.0f, drive, GaugeLayoutOf(def));
        });
        fonts.tex = keep;
    }

    // the Gauges tab, as ReShade's overlay shows it with FontScale=2 (ReShade's own style differs): the page must
    // report no change when nothing is touched
    {
        GaugeSettings s = def;
        s.style = kFaceTinted;
        s.redlineRpm = 2400.0f;
        s.toggleKey = VK_F9;
        strcpy_s(s.padToggle, "RB+DPadUp");
        s.damage = true;
        s.probe = 1; // the test buttons show with the probe log on
        GaugeView view = {};
        view.show = view.demo = true;
        view.st = drive;
        view.engineDamage = 14;
        view.engineCapacity = 150;
        ImGui::GetStyle().FontScaleMain = 2.0f;
        // a truck with scales of its own, a truck that shows the plain ones, and no truck
        static const struct { const wchar_t *file; const char *truck; bool own; } tabs[3] = {
            { L"settings.png", "mod_suv/1000001", true }, { L"settings_plain.png", "chevrolet_ck1500", false }, { L"settings_none.png", "", false } };
        for (const auto &tab : tabs)
        {
            strcpy_s(view.truck, tab.truck);
            view.truckOwn = tab.own;
            PageState st;
            int act = 0;
            wchar_t path[MAX_PATH];
            swprintf_s(path, L"%s\\%s", out, tab.file);
            RenderPng(path, 1100, 2300, [&](ImDrawList *, ImDrawList *) {
                ImGui::SetNextWindowPos(ImVec2(10, 10));
                ImGui::SetNextWindowSize(ImVec2(1080, 0));
                ImGui::Begin("Gauges", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
                act |= SettingsPage(s, view, st, 0);
                ImGui::End();
            }, 3);
            printf("%s settings tab (%ls): three frames untouched report no change\n", act ? "FAIL" : "ok  ", tab.file);
            if (act) g_failed++;
        }
        // the Suspension section with a truck's file read (the Arocs' front and rear): the spring past the game's 1 and
        // travel past the file's end, so the notes show (the addendum of 2 November, 13:18)
        {
            GaugeSettings t = s;
            t.springFront = t.springRear = 4.0f;
            t.dampingFront = t.dampingRear = 3.0f;
            t.extraTravel = 12.0f;
            GaugeView v = view;
            strcpy_s(v.truck, "arocs/1180732");
            v.suspOwn = true; // its own suspension: the box ticked and the button beside it
            v.suspLevel = 1;    // at its kind's level, the choice under the box
            v.suspIdKnown = true;
            v.suspKnown = true;
            const float file[3][2] = { { 0.028f, 0.032f }, { 0.5f, 0.3f }, { -0.6f, -0.44f } };
            memcpy(v.suspFile, file, sizeof file);
            // the camber's lines with made up figures: a gain and an offset on each end (BRIEF_swing_camber.md)
            t.camberGainFront = 2.0f;
            t.camberRestRear = 1.5f;
            v.camberKnown = v.camberEnd[0] = v.camberEnd[1] = true;
            const float camber[2][2] = { { 4.0f, 1.5f }, { 4.0f, 0.0f } };
            memcpy(v.camberFile, camber, sizeof camber);
            PageState st;
            st.tabAsk = kTabSuspension;
            st.wholeTab = true;
            int act = 0;
            wchar_t path[MAX_PATH];
            swprintf_s(path, L"%s\\settings_suspension.png", out);
            RenderPng(path, 1100, 7000, [&](ImDrawList *, ImDrawList *) {
                ImGui::SetNextWindowPos(ImVec2(10, 10));
                ImGui::SetNextWindowSize(ImVec2(1080, 0));
                ImGui::Begin("Gauges", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
                act |= SettingsPage(t, v, st, 0);
                ImGui::End();
            }, 3);
            printf("%s settings tab (settings_suspension.png): three frames untouched report no change\n", act ? "FAIL" : "ok  ");
            if (act) g_failed++;
        }
    }

    // the Gameplay tab in its tabs (BRIEF_overlay_tabs_and_text.md): each tab whole with the widest settings, as a
    // picture (settings_tab_<id>.png), and every setting the one page drew before the tabs (test\page_items.txt, its
    // labels) drawn on exactly one tab: the labels of all the tabs together are that list, none twice and none missing
    {
        GaugeSettings s;
        GaugeView v;
        PageWidest(s, v);
        std::vector<std::string> all;
        bool quiet = true;
        for (int t = 0; t < kTabCount; t++)
        {
            std::vector<std::string> items;
            float height = 0.0f;
            wchar_t path[MAX_PATH];
            swprintf_s(path, L"%s\\settings_tab_%S.png", out, kPageTabIds[t]);
            // the first pass measures the tab and takes its labels, the second draws it at its height
            for (int pass = 0; pass < 2; pass++)
            {
                PageState st;
                st.tabAsk = t;
                st.wholeTab = true;
                int frame = 0, act = 0;
                RenderPng(path, 1100, pass ? (int)height + 20 : 9000, [&](ImDrawList *, ImDrawList *) {
                    ImGui::SetNextWindowPos(ImVec2(10, 10));
                    ImGui::SetNextWindowSize(ImVec2(1080, 0));
                    ImGui::Begin("Gauges", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
                    g_items = pass == 0 && ++frame == 4 ? &items : nullptr;
                    ImGui::GetCurrentContext()->TestEngineHookItems = g_items != nullptr;
                    act |= SettingsPage(s, v, st, 0);
                    g_items = nullptr;
                    ImGui::GetCurrentContext()->TestEngineHookItems = false;
                    height = ImGui::GetWindowHeight();
                    ImGui::End();
                }, 4);
                if (act) quiet = false;
            }
            for (const std::string &i : items)
            {
                bool tabName = i == "Gauges" || i.find("##tab_") != std::string::npos;
                for (const char *n : kPageTabNames) tabName = tabName || i == n;
                if (!tabName) all.push_back(i);
            }
        }
        std::sort(all.begin(), all.end());
        std::vector<std::string> before;
        FILE *f = nullptr;
        if (!fopen_s(&f, "test\\page_items.txt", "rb") && f)
        {
            char line[256];
            while (fgets(line, sizeof line, f))
            {
                line[strcspn(line, "\r\n")] = 0;
                if (line[0]) before.push_back(line);
            }
            fclose(f);
        }
        std::sort(before.begin(), before.end());
        std::vector<std::string> twice, missing;
        std::set_difference(all.begin(), all.end(), before.begin(), before.end(), std::back_inserter(twice));
        std::set_difference(before.begin(), before.end(), all.begin(), all.end(), std::back_inserter(missing));
        for (const std::string &i : twice) printf("     on a tab more than the page had it, or new: %s\n", i.c_str());
        for (const std::string &i : missing) printf("     on no tab: %s\n", i.c_str());
        const bool same = !before.empty() && twice.empty() && missing.empty();
        printf("%s settings tabs: the %d settings the one page drew are each on exactly one of the eight tabs (%d labels on the tabs)\n", same ? "ok  " : "FAIL",
               (int)before.size(), (int)all.size());
        if (!same) g_failed++;
        printf("%s settings tabs: each tab drawn untouched reports no change\n", quiet ? "ok  " : "FAIL");
        if (!quiet) g_failed++;
        // the Engine tab's Profiles with a list (BRIEF_truck_profile.md), as a picture (settings_profiles.png): a profile
        // of this kind, its backup, a file that is no profile and one of another kind with its second press asked for;
        // texts a stranger wrote (a %s, a %n) are drawn as they stand; drawn untouched it asks nothing of the pass
        {
            static PageState pst;
            pst = PageState{};
            pst.tabAsk = kTabEngine;
            pst.wholeTab = true;
            ProfileList &pl = pst.prof;
            pl.turn = 1, pl.truck = pl.idRead = pl.canExport = true;
            strcpy_s(pl.kind, "truck_kind"), strcpy_s(pl.kindFile, "truck_kind");
            strcpy_s(pl.say, "Taken: the suspension, Air brakes. The settings of the kind were left. The tire mode is not taken yet.");
            auto add = [&](const char *file, const char *name, const char *truck, const char *notes, bool valid, bool backup, bool same) {
                ProfileRow &r = pl.row[pl.count++];
                strcpy_s(r.file, file), strcpy_s(r.name, name), strcpy_s(r.truck, truck), strcpy_s(r.notes, notes), strcpy_s(r.date, "2026-11-13 13:30"), strcpy_s(r.version, "9.9.9");
                r.valid = valid, r.backup = backup, r.sameKind = same, r.sameEngine = r.sameGearbox = same, r.otherVersion = !same;
            };
            add("truck_kind - Mud.ini", "Mud", "truck_kind", "Soft springs %s%n%s for the swamp", true, false, true);
            add("truck_kind 2026-11-13 13.24.51.ini", "2026-11-13 13.24.51", "truck_kind", "Before Mud, this truck", true, true, true);
            add("truck_kind - Broken.ini", "Broken", "", "", false, false, false);
            add("other_kind - Road %d.ini", "other_kind - Road %d", "other_kind %s", "%n%n%n", true, false, false);
            pl.more = 3;
            strcpy_s(pst.profOther, "other_kind - Road %d.ini");
            pst.profOtherWhat = kProfAskImportKind;
            std::vector<std::string> items;
            int frame = 0, act = 0;
            wchar_t path[MAX_PATH];
            swprintf_s(path, L"%s\\settings_profiles.png", out);
            RenderPng(path, 1100, 2600, [&](ImDrawList *, ImDrawList *) {
                ImGui::SetNextWindowPos(ImVec2(10, 10));
                ImGui::SetNextWindowSize(ImVec2(1080, 0));
                ImGui::Begin("Gauges", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
                g_items = ++frame == 4 ? &items : nullptr;
                ImGui::GetCurrentContext()->TestEngineHookItems = g_items != nullptr;
                act |= SettingsPage(s, v, pst, 0);
                g_items = nullptr;
                ImGui::GetCurrentContext()->TestEngineHookItems = false;
                ImGui::End();
            }, 4);
            int imports = 0, kinds = 0, others = 0;
            for (const std::string &i : items) imports += i == "Import", kinds += i == "For every truck of this kind", others += i == "Other trucks###profother";
            const bool rows = imports >= 2 && kinds >= 2 && others == 1 && !act && pst.profAsk.what == kProfAskNone;
            printf("%s profiles: the list drawn with a profile, a backup, a file that is no profile and one of another kind has the two buttons on each profile and none on the other file, and asks nothing untouched (%d, %d, %d)\n",
                   rows ? "ok  " : "FAIL", imports, kinds, others);
            if (!rows) g_failed++;
        }
        // the board (BRIEF_checkers.md, M5): every tab above was drawn and no line of the board's ran; then the board as a
        // picture (checkers.png), a game a few moves in with a piece picked
        {
            const long before = g_ckBoardFrames;
            CkBoard board = {};
            CkBoardOpened(board, 7);
            board.two = true;
            CkBoardStep(board, 0, 2 * 8 + 2, 0.016f), CkBoardStep(board, 0, 3 * 8 + 3, 0.016f);
            CkBoardStep(board, 0, 5 * 8 + 5, 0.016f), CkBoardStep(board, 0, 4 * 8 + 6, 0.016f);
            CkBoardStep(board, 0, 2 * 8 + 4, 0.016f);
            bool closed = false;
            wchar_t path[MAX_PATH];
            swprintf_s(path, L"%s\\checkers.png", out);
            RenderPng(path, 760, 760, [&](ImDrawList *, ImDrawList *) { closed = CkBoardDraw(board, 0, 0.016f) || closed; }, 4);
            const bool off = before == 0 && g_ckBoardFrames == 4 && !closed && board.game.plies == 2 && board.pick.n == 1;
            printf("%s checkers: no frame of the board ran while the eight tabs were drawn; drawn by itself it runs, closes nothing and keeps its game\n", off ? "ok  " : "FAIL");
            if (!off) g_failed++;
        }
        const bool ids = PageTabFromId("power") == kTabPower && PageTabFromId("modwork") == kTabModWork && PageTabFromId("Power") == kTabDisplay &&
                         PageTabFromId("") == kTabDisplay && PageTabFromId(nullptr) == kTabDisplay && PageTabFromId("tyres") == kTabDisplay;
        printf("%s settings tabs: an id opens its tab; an unknown one, another case or none opens Display\n", ids ? "ok  " : "FAIL");
        if (!ids) g_failed++;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    Release(g_shot);
    Release(g_fontSrv);
    Release(g_ctx);
    Release(g_dev);
    Release(g_wic);
    CoUninitialize();
    printf(g_failed ? "PREVIEW FAILED (%d)\n" : "PREVIEW OK\n", g_failed);
    return g_failed ? 1 : 0;
}
