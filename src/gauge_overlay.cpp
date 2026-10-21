// gauge_overlay.cpp: the render side of the mod. The cluster is drawn on every frame by Next Gen's own overlay
// (ng_overlay.cpp: Dear ImGui compiled into the mod, on the game's DirectX 11 device; no ReShade needed, and ReShade
// where installed is left alone). The drawing is in gauge_draw.h, shared with the offline preview
// (test\gauge_preview.cpp). Its text is the game's font from a glyph atlas that this file makes a texture of on the
// game's device. The overlay also gets the Gameplay tab of the NEXT GEN menu (settings_page.h), which edits Gauges.ini
// in the game; the Next Gen Tire Inflation mod, where installed, has its tab in the same menu.
#include <windows.h>
#include <xinput.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include "ng_overlay.h"
#include "gauges.h"
#include "gauge_draw.h"
#include "settings_page.h"

volatile LONG g_pageCapturing = 0;
volatile LONG g_modOff = 0;

// The game's font as a texture on the game's device: baked for the cluster's size on the first frames, again when the
// size changes, freed with the device.
static GaugeFonts g_fonts;
static bool g_fontFiles = false;
static float g_fontFailedAt = 0.0f; // the size a bake failed at: not tried again until the size changes

static void FreeFontTexture()
{
    NgTextureFree(g_fonts.tex);
    g_fonts.tex = 0;
}

static void UpdateFontTexture(float u)
{
    static float wanted = 0.0f;         // the size asked for on the last frame
    static ULONGLONG wantedSince = 0;   // and since when
    if (!g_fontFiles || (g_fonts.tex && fabsf(g_fonts.u - u) < 0.001f)) return;
    if (!g_fonts.tex && g_fontFailedAt > 0.0f && fabsf(g_fontFailedAt - u) < 0.001f) return;
    // a size being dragged on the settings tab changes on every frame: the atlas is baked again once the size has
    // rested for a quarter of a second, and the text keeps its old size until then
    const ULONGLONG now = GetTickCount64();
    if (fabsf(wanted - u) >= 0.001f) { wanted = u; wantedSince = now; }
    if (g_fonts.tex && now - wantedSince < 250) return;
    FreeFontTexture();
    if (FontsBake(g_fonts, u))
    {
        g_fonts.tex = NgTextureMake(g_fonts.rgba, g_fonts.w, g_fonts.h);
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
        // not again every frame: Dear ImGui's font stands in until the cluster's size changes
        g_fontFailedAt = u;
        GaugeLog(L"overlay: the game font did not become a texture at %.3f px per reference px, Dear ImGui's font stands in", u);
    }
}

static void OnFrame()
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
    UpdateFontTexture(u);
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
    // the background list: under every window, so the cluster never covers the NEXT GEN menu
    LARGE_INTEGER before, after;
    QueryPerformanceCounter(&before);
    GaugeLayout lay = GaugeLayoutOf(s);
    if (shown.natural) lay.showBoost = false; // a naturally aspirated engine has no boost to show
    GaugeDraw(ImGui::GetBackgroundDrawList(), g_fonts, GaugeOrigin(s, io.DisplaySize.x, io.DisplaySize.y), u, alpha * s.opacity, shown, lay);
    QueryPerformanceCounter(&after);
    if (s.probe)
    {
        // what the drawing costs, once a minute: the time GaugeDraw takes to put the cluster into the overlay's draw list
        static LARGE_INTEGER rate = {};
        static double sum = 0.0, longest = 0.0;
        static int frames = 0;
        static ULONGLONG since = 0;
        if (!rate.QuadPart) QueryPerformanceFrequency(&rate);
        const double ms = (double)(after.QuadPart - before.QuadPart) * 1000.0 / (double)rate.QuadPart;
        const ULONGLONG now = GetTickCount64();
        if (!since) since = now;
        sum += ms;
        longest = ms > longest ? ms : longest;
        frames++;
        if (now - since >= 60000)
        {
            GaugeLog(L"probe: the cluster took %.3f ms a frame to draw over the last minute (%d frames, the longest %.3f ms) on a %.0f x %.0f picture", sum / frames, frames,
                     longest, io.DisplaySize.x, io.DisplaySize.y);
            sum = longest = 0.0;
            frames = 0;
            since = now;
        }
    }
    if (loggedW != io.DisplaySize.x || loggedH != io.DisplaySize.y)
    {
        loggedW = io.DisplaySize.x;
        loggedH = io.DisplaySize.y;
        static const wchar_t *layouts[kLayoutCount] = { L"strip", L"block" }, *styles[kFaceCount] = { L"solid", L"tinted", L"open" };
        GaugeLog(L"overlay: the cluster is drawn on a %.0f x %.0f picture (%s layout, %s style, %.3f px per reference px%s)", loggedW, loggedH,
                 layouts[s.layout == kLayoutBlock ? 1 : 0], styles[s.style < 0 || s.style >= kFaceCount ? kFaceOpen : s.style], u, view.demo ? L", demo values" : L"");
    }
}

// The Gameplay tab: the page edits a copy of the settings in force and hands it back when something changed.
static void OnPage()
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
    const LONG turn = SettingsGet(s);
    ViewGet(view);
    // the pad is read on every frame of the tab: the frame the binding's button is pressed on has to know that the
    // pad's A, which pressed it, is still down
    const int act = SettingsPage(s, view, st, PadButtons());
    if (act & kPageChanged) SettingsPut(s, turn);
    if (act & kPageTruckDrop) SettingsTruckDrop();
    if (act & kPagePullTest) PullTestAsk(true);
    if (act & kPagePullCancel) PullTestAsk(false);
    if (act & kPageMapAmbient) MapAmbientSet(st.mapAmbientC);
    if (act & kPageGearHold) GearHoldSet(st.gearHold);
    if (act & kPageEngineSet) EngineProfileSet(st.enginePeak, st.engineAspiration);
    if (act & kPageEngineDrop) EngineProfileDrop();
    if (act & kPageEngineFull) EngineFullPullSet(st.engineFullPull);
    if (act & kPageDefaults) SettingsReset();
    if (act & kPageReload) SettingsReload();
    if (act & kPageHearHigh) SoundHear(kTurboHigh);
    if (act & kPageHearMid) SoundHear(kTurboMid);
    if (act & kPageHearLow) SoundHear(kTurboLow);
    if (act & kPageHearOil) SoundHear(kSoundOil);
}

bool OverlayInit(HMODULE)
{
    // the game's font files sit next to the exe
    wchar_t dir[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
    wchar_t *slash = n && n < MAX_PATH ? wcsrchr(dir, L'\\') : nullptr;
    if (slash) *slash = 0;
    g_fontFiles = slash && FontsLoad(g_fonts, dir, kGaugeStyles, kGsStyleCount);
    GaugeLog(g_fontFiles ? L"overlay: game font read (TT Lakes Compressed)" : L"overlay: game font not found, Dear ImGui's font stands in");
    NgClient client = {};
    client.tab = "Gameplay";
    client.version = GAUGES_VERSION;
    client.order = 1;
    client.log = GaugeLog;
    client.frame = OnFrame;
    client.page = OnPage;
    client.deviceGone = FreeFontTexture;
    return NgOverlayStart(client);
}
