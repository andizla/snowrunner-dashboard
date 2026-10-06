// gauge_overlay.cpp: the render side of Gauges.asi. The cluster is drawn from ReShade's reshade_overlay add-on event,
// which ReShade runs on every frame once an add-on listens to it. ReShade owns the swap chain and Dear ImGui and hands
// add-ons the ImGui functions through a table, so only headers are compiled in (deps\reshade, deps\imgui; their
// versions must match the installed ReShade). Without ReShade nothing is drawn.
// The drawing is in gauge_draw.h, shared with the offline preview (test\gauge_preview.cpp). Its text is the game's
// font from a glyph atlas that this file makes a texture of on ReShade's device. The overlay also gets a Gauges tab
// (settings_page.h) that edits Gauges.ini in the game.
// A second ReShade add-on next to the SnowRunner Tire Inflation System's: each registers on its own.
#include <windows.h>
#include <xinput.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#define ImTextureID ImU64 // ReShade expects 64-bit texture handles
#include <imgui.h>
#include <reshade.hpp>
#include "gauges.h"
#include "gauge_draw.h"
#include "settings_page.h"

// ReShade lists the add-on under these.
extern "C" __declspec(dllexport) const char *NAME = "Gauges";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "A gauge cluster for SnowRunner: speedometer, tachometer, fuel and warning lamps, drawn by Gauges.asi.";

volatile LONG g_pageCapturing = 0;
volatile LONG g_modOff = 0;

// The game's font as a texture on ReShade's device: baked for the cluster's size on the first frames, again when the
// size changes, freed with the device.
static GaugeFonts g_fonts;
static bool g_fontFiles = false;
static float g_fontFailedAt = 0.0f; // the size a bake failed at: not tried again until the size changes
static reshade::api::device *g_fontDevice = nullptr;
static reshade::api::resource g_fontTex = {};
static reshade::api::resource_view g_fontView = {};

static void FreeFontTexture()
{
    if (g_fontDevice)
    {
        if (g_fontView.handle) g_fontDevice->destroy_resource_view(g_fontView);
        if (g_fontTex.handle) g_fontDevice->destroy_resource(g_fontTex);
    }
    g_fontDevice = nullptr;
    g_fontTex = {};
    g_fontView = {};
    g_fonts.tex = 0;
}

static void UpdateFontTexture(reshade::api::effect_runtime *runtime, float u)
{
    using namespace reshade::api;
    static float wanted = 0.0f;         // the size asked for on the last frame
    static ULONGLONG wantedSince = 0;   // and since when
    device *dev = runtime->get_device();
    if (!g_fontFiles || (g_fonts.tex && g_fontDevice == dev && fabsf(g_fonts.u - u) < 0.001f)) return;
    if (!g_fonts.tex && g_fontFailedAt > 0.0f && fabsf(g_fontFailedAt - u) < 0.001f) return;
    // a size being dragged on the settings tab changes on every frame: the atlas is baked again once the size has
    // rested for a quarter of a second, and the text keeps its old size until then
    const ULONGLONG now = GetTickCount64();
    if (fabsf(wanted - u) >= 0.001f) { wanted = u; wantedSince = now; }
    if (g_fonts.tex && g_fontDevice == dev && now - wantedSince < 250) return;
    FreeFontTexture();
    if (FontsBake(g_fonts, u))
    {
        const subresource_data data = { g_fonts.rgba, (uint32_t)g_fonts.w * 4, (uint32_t)g_fonts.w * (uint32_t)g_fonts.h * 4 };
        const resource_desc desc((uint32_t)g_fonts.w, (uint32_t)g_fonts.h, 1, 1, format::r8g8b8a8_unorm, 1, memory_heap::default_, resource_usage::shader_resource);
        if (dev->create_resource(desc, &data, resource_usage::shader_resource, &g_fontTex))
        {
            if (dev->create_resource_view(g_fontTex, resource_usage::shader_resource, resource_view_desc(format::r8g8b8a8_unorm), &g_fontView))
            {
                g_fontDevice = dev;
                g_fonts.tex = g_fontView.handle;
            }
            else
            {
                dev->destroy_resource(g_fontTex);
                g_fontTex = {};
            }
        }
        free(g_fonts.rgba);
        g_fonts.rgba = nullptr;
    }
    if (g_fonts.tex)
    {
        g_fontFailedAt = 0.0f;
        GaugeLog(L"overlay: game font baked at %.3f px per reference px (%dx%d atlas)", u, g_fonts.w, g_fonts.h);
    }
    else
    {
        // not again every frame: ReShade's font stands in until the cluster's size changes
        g_fontFailedAt = u;
        GaugeLog(L"overlay: the game font did not become a texture at %.3f px per reference px, ReShade's font stands in", u);
    }
}

static void OnDestroyDevice(reshade::api::device *device)
{
    if (device == g_fontDevice) FreeFontTexture();
}

static void OnOverlay(reshade::api::effect_runtime *runtime)
{
    static float alpha = 0.0f;
    static GaugeState shown = {};       // the last state that was on screen: the cluster fades out with it
    static float loggedW = 0.0f, loggedH = 0.0f;
    if (g_modOff) return;
    ImGuiIO &io = ImGui::GetIO();
    // no cluster before the game has a picture to draw on
    if (io.DisplaySize.x < 320.0f || io.DisplaySize.y < 240.0f) return;
    GaugeSettings s;
    GaugeView view;
    SettingsGet(s);
    ViewGet(view);
    const float u = GaugeUnit(io.DisplaySize.y, s);
    UpdateFontTexture(runtime, u);
    const float dt = fminf(fmaxf(io.DeltaTime, 0.0f), 0.1f);
    alpha = fminf(fmaxf(alpha + (view.show ? dt : -dt) / 0.15f, 0.0f), 1.0f);
    // the mod's thread leaves a new state 20 times a second: the two big needles glide from one to the next
    const float speedWas = shown.speedKmh, rpmWas = shown.rpm;
    const bool wasShown = alpha > 0.0f;
    if (view.show)
    {
        shown = view.st;
        if (wasShown)
        {
            const float follow = 1.0f - expf(-dt / 0.05f);
            shown.speedKmh = speedWas + (view.st.speedKmh - speedWas) * follow;
            shown.rpm = rpmWas + (view.st.rpm - rpmWas) * follow;
        }
    }
    if (alpha <= 0.0f) return;
    // the background list: under ReShade's own windows, so the cluster never covers the settings tab
    GaugeDraw(ImGui::GetBackgroundDrawList(), g_fonts, GaugeOrigin(s, io.DisplaySize.x, io.DisplaySize.y), u, alpha * s.opacity, shown, GaugeLayoutOf(s));
    if (loggedW != io.DisplaySize.x || loggedH != io.DisplaySize.y)
    {
        loggedW = io.DisplaySize.x;
        loggedH = io.DisplaySize.y;
        static const wchar_t *layouts[kLayoutCount] = { L"strip", L"block" }, *styles[kFaceCount] = { L"solid", L"tinted", L"open" };
        GaugeLog(L"overlay: the cluster is drawn on a %.0f x %.0f picture (%s layout, %s style, %.3f px per reference px%s)", loggedW, loggedH,
                 layouts[s.layout == kLayoutBlock ? 1 : 0], styles[s.style < 0 || s.style >= kFaceCount ? kFaceOpen : s.style], u, view.demo ? L", demo values" : L"");
    }
}

// The Gauges tab: the page edits a copy of the settings in force and hands it back when something changed.
static void OnSettingsTab(reshade::api::effect_runtime *)
{
    if (g_modOff)
    {
        ImGui::TextWrapped("The mod is made for another version of the game and is not running. Gauges.log in the game folder has the details.");
        return;
    }
    static PageState st;
    static bool logged = false;
    if (!logged)
    {
        logged = true;
        GaugeLog(L"overlay: the Gauges tab was opened");
    }
    GaugeSettings s;
    GaugeView view;
    SettingsGet(s);
    ViewGet(view);
    // the pad is read on every frame of the tab: the frame the binding's button is pressed on has to know that the
    // pad's A, which pressed it, is still down
    const int act = SettingsPage(s, view, st, PadButtons());
    if (act & kPageChanged) SettingsPut(s);
    if (act & kPageReload) SettingsReload();
}

bool OverlayInit(HMODULE self)
{
    if (!reshade::register_addon(self)) return false;
    // the game's font files sit next to the exe
    wchar_t dir[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
    wchar_t *slash = n && n < MAX_PATH ? wcsrchr(dir, L'\\') : nullptr;
    if (slash) *slash = 0;
    g_fontFiles = slash && FontsLoad(g_fonts, dir, kGaugeStyles, kGsStyleCount);
    GaugeLog(g_fontFiles ? L"overlay: game font read (TT Lakes Compressed)" : L"overlay: game font not found, ReShade's font stands in");
    reshade::register_event<reshade::addon_event::reshade_overlay>(&OnOverlay);
    reshade::register_event<reshade::addon_event::destroy_device>(&OnDestroyDevice);
    reshade::register_overlay("Gauges", &OnSettingsTab);
    return true;
}
