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
#include "checkers_board.h"

volatile LONG g_pageCapturing = 0;
volatile LONG g_modOff = 0;

// The game's font as a texture on the game's device: baked for the cluster's size on the first frames, again when the
// size changes, freed with the device.
static GaugeFonts g_fonts;
static bool g_fontFiles = false;
static float g_fontFailedAt = 0.0f; // the size a bake failed at: not tried again until the size changes

// The warning boxes' text (gauge_draw.h, kWarnStyles), baked for the HUD stage's size as the tire mod bakes its own.
static GaugeFonts g_warnFonts;
static bool g_warnFontFiles = false;
static float g_warnFontFailedAt = 0.0f;
// The physics rate sign's own atlas: the boxes' font at the sign's size, baked the first time the sign is drawn.
static GaugeFonts g_signFonts;
static bool g_signFontFiles = false;
static float g_signFontFailedAt = 0.0f;

static void FreeFontTexture()
{
    NgTextureFree(g_fonts.tex);
    g_fonts.tex = 0;
    NgTextureFree(g_warnFonts.tex);
    g_warnFonts.tex = 0;
    NgTextureFree(g_signFonts.tex);
    g_signFonts.tex = 0;
}

// The boxes' atlas for u (WarnUnit), again when the picture's size changes.
static void UpdateWarnFontTexture(float u)
{
    if (!g_warnFontFiles || (g_warnFonts.tex && fabsf(g_warnFonts.u - u) < 0.001f)) return;
    if (!g_warnFonts.tex && g_warnFontFailedAt > 0.0f && fabsf(g_warnFontFailedAt - u) < 0.001f) return;
    NgTextureFree(g_warnFonts.tex);
    g_warnFonts.tex = 0;
    if (FontsBake(g_warnFonts, u))
    {
        g_warnFonts.tex = NgTextureMake(g_warnFonts.rgba, g_warnFonts.w, g_warnFonts.h);
        free(g_warnFonts.rgba);
        g_warnFonts.rgba = nullptr;
    }
    g_warnFontFailedAt = g_warnFonts.tex ? 0.0f : u;
    if (!g_warnFonts.tex) GaugeLog(L"overlay: the warning boxes' font did not become a texture at %.3f px per reference px, Dear ImGui's font stands in", u);
}

// The physics rate sign (BRIEF_step_check_always.md): its atlas for u, then the sign on the foreground list, so over the
// cluster, the boxes and the NEXT GEN menu. Where the font does not become a texture Dear ImGui's font stands in.
static void DrawStepSign(float w, float h)
{
    const float u = kStepSignScale * WarnUnit(w, h);
    const bool have = g_signFonts.tex && fabsf(g_signFonts.u - u) < 0.001f, failed = !g_signFonts.tex && g_signFontFailedAt > 0.0f && fabsf(g_signFontFailedAt - u) < 0.001f;
    if (g_signFontFiles && !have && !failed)
    {
        NgTextureFree(g_signFonts.tex);
        g_signFonts.tex = 0;
        if (FontsBake(g_signFonts, u))
        {
            g_signFonts.tex = NgTextureMake(g_signFonts.rgba, g_signFonts.w, g_signFonts.h);
            free(g_signFonts.rgba);
            g_signFonts.rgba = nullptr;
        }
        g_signFontFailedAt = g_signFonts.tex ? 0.0f : u;
    }
    GStepSign(ImGui::GetForegroundDrawList(), g_signFonts, w, h, u);
}

// The clutch's and the engine's warning boxes, below the tire mod's: each fades in over a quarter second while it has
// something to say and out again after, keeping its last words while it fades; not while the NEXT GEN menu is open,
// and on the background list, so never over the menu. Returns the slot the displays under the boxes start at: 3 with
// dials shown, as ever; with the Layout at Off the one under the last box on screen (BRIEF_dials_off_option.md).
static int DrawWarnings(const GaugeView &view, float w, float h, float dt)
{
    static float fade[kWarnBoxes] = {};
    static WarnBox last[kWarnBoxes] = {};
    const float u = WarnUnit(w, h);
    UpdateWarnFontTexture(u);
    const bool menu = NgMenuOpen();
    int under = 1;
    for (int i = 0; i < kWarnBoxes; i++)
    {
        const bool on = view.warn[i].state != kWarnNone && !menu;
        if (view.warn[i].state != kWarnNone) last[i] = view.warn[i];
        fade[i] = fminf(fmaxf(fade[i] + (on ? dt : -dt) / 0.25f, 0.0f), 1.0f);
        if (fade[i] > 0.0f)
        {
            GWarnBox(ImGui::GetBackgroundDrawList(), g_warnFonts, WarnOrigin(w, h, 1 + i), u, fade[i], last[i]);
            under = 2 + i;
        }
    }
    return view.warnStacked ? under : 3;
}

// The suspension display, the sway bars' lamp (build B) and the air suspension's height box (build C), on the warning
// boxes' stage below their slots; not while the NEXT GEN menu is open. The drive display (131) in the suspension
// display's place, or beside it on its right while both are on.
static void DrawSuspension(const GaugeView &view, float w, float h, int slot)
{
    if (NgMenuOpen() || (view.susp.wheels <= 0 && !view.driveShown && !view.swayOff && !view.airShown)) return;
    const float u = WarnUnit(w, h);
    UpdateWarnFontTexture(u);
    ImVec2 at = WarnOrigin(w, h, slot);
    if (view.driveShown) GDriveDisplay(ImGui::GetBackgroundDrawList(), g_warnFonts, ImVec2(at.x + (view.susp.wheels > 0 ? (kSuspW + kWarnGap) * u : 0.0f), at.y), u, 1.0f, view.drive);
    if (view.susp.wheels > 0)
    {
        GSuspDisplay(ImGui::GetBackgroundDrawList(), g_warnFonts, at, u, 1.0f, view.susp);
        at.y += (GSuspHeight(view.susp) + kWarnGap) * u;
    }
    else if (view.driveShown) at.y += (GDriveHeight(view.drive) + kWarnGap) * u;
    if (view.swayOff)
    {
        GSwayLamp(ImGui::GetBackgroundDrawList(), g_warnFonts, at, u, 1.0f);
        at.y += (kSwayH + kWarnGap) * u;
    }
    if (view.airShown) GRideBox(ImGui::GetBackgroundDrawList(), g_warnFonts, at, u, 1.0f, view.airHeight);
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

// ---- the checkers board (BRIEF_checkers.md with the Reviewer's addendum): hidden, and no line of it runs until its
// binding or the five clicks ask. It shows only with the NEXT GEN menu open on the mod's tab, in place of the tab's body.
static CkBoard g_ck = {};
static CkWindow g_ckWin = {};
static volatile LONG g_ckAsk = 0;
static uint64_t g_ckTruck = 0;
void CheckersAsk() { InterlockedExchange(&g_ckAsk, 1); }

// What the board asks of the menu. NgMenuShowTab opens the menu on the mod's tab, and with it the menu's own take of
// the keys and the mouse; NgMenuShowTab(nullptr) closes it and gives them back. The board takes nothing by itself.
static void CkMenuDo(int what)
{
    if (what == kCkMenuShow) NgMenuShowTab("Gameplay");
    else if (what == kCkMenuClose) NgMenuShowTab(nullptr);
}

static void CheckersOpen(const GaugeView &view)
{
    if (g_ckWin.open) return;
    g_ckTruck = view.truckVehicle;
    CkBoardOpened(g_ck, (uint32_t)GetTickCount()); // the opponent's seed from the clock, its own from there on
    CkMenuDo(CkWindowOpen(g_ckWin, NgMenuOpen()));
}

// Every frame, and nothing where the board is closed and not asked for: the binding's press, and what closes the board
// besides the player (the menu gone, the game's window not in front, another truck).
static void CheckersFrame(const GaugeSettings &s, const GaugeView &view)
{
    if (!g_ckWin.open && !g_ckAsk) return;
    if (InterlockedExchange(&g_ckAsk, 0) && s.checkers)
    {
        if (g_ckWin.open) CkMenuDo(CkWindowClose(g_ckWin));
        else CheckersOpen(view);
        return;
    }
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    CkMenuDo(CkWindowFrame(g_ckWin, NgMenuOpen(), process == GetCurrentProcessId(), view.truckVehicle == g_ckTruck));
}

// The board in place of the tab's body: the pad's d-pad or left stick, A and B, and the arrow keys, Enter and Escape,
// which the open menu hands to Dear ImGui. The pad is only read: the game hears it too.
static void CheckersPage()
{
    const uint32_t pad = PadButtons();
    const ImGuiIO &io = ImGui::GetIO();
    const unsigned held = CkHeldOf((pad & (XINPUT_GAMEPAD_DPAD_LEFT | kPadLSLeft)) || ImGui::IsKeyDown(ImGuiKey_LeftArrow),
                                   (pad & (XINPUT_GAMEPAD_DPAD_RIGHT | kPadLSRight)) || ImGui::IsKeyDown(ImGuiKey_RightArrow),
                                   (pad & (XINPUT_GAMEPAD_DPAD_UP | kPadLSUp)) || ImGui::IsKeyDown(ImGuiKey_UpArrow),
                                   (pad & (XINPUT_GAMEPAD_DPAD_DOWN | kPadLSDown)) || ImGui::IsKeyDown(ImGuiKey_DownArrow),
                                   (pad & XINPUT_GAMEPAD_A) || ImGui::IsKeyDown(ImGuiKey_Enter) || ImGui::IsKeyDown(ImGuiKey_KeypadEnter),
                                   (pad & XINPUT_GAMEPAD_B) || ImGui::IsKeyDown(ImGuiKey_Escape));
    if (CkBoardDraw(g_ck, held, fminf(fmaxf(io.DeltaTime, 0.0f), 0.1f))) CkMenuDo(CkWindowClose(g_ckWin));
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
    CheckersFrame(s, view);
    // Off is no place (BRIEF_dials_off_option.md): the font keeps the strip's unit there, and the cluster's own part
    // below is left before anything places it
    const bool off = s.layout == kLayoutOff;
    GaugeSettings place = s;
    if (off) place.layout = kLayoutStrip;
    const float u = GaugeUnit(io.DisplaySize.x, io.DisplaySize.y, place);
    UpdateFontTexture(u);
    const float dt = fminf(fmaxf(io.DeltaTime, 0.0f), 0.1f);
    const int under = DrawWarnings(view, io.DisplaySize.x, io.DisplaySize.y, dt);
    DrawSuspension(view, io.DisplaySize.x, io.DisplaySize.y, under);
    // the physics rate sign: on the foreground list, so over the cluster, the boxes and the NEXT GEN menu; before every
    // switch below (the toggle key, the truck, the other tabs, the opacity), so none of them reaches it
    if (StepSignShown(view)) DrawStepSign(io.DisplaySize.x, io.DisplaySize.y);
    // the Off layout: no dials, at once and with no fade. The boxes, the displays and the sign above are not the cluster's
    if (off)
    {
        alpha = 0.0f;
        return;
    }
    // hidden while the menu shows a tab this mod does not draw (BRIEF_full_pass_gameplay.md part 3): it would lie over
    // that tab where the two overlap; on the Gameplay tabs it stays, so Size and the layout show as they are set
    const bool shows = view.show && !NgOtherTabShown();
    alpha = fminf(fmaxf(alpha + (shows ? dt : -dt) / 0.15f, 0.0f), 1.0f);
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
    if (shown.noAir) lay.showAir = false;     // a truck without air brakes has no air to show (the mod's thread worked it out)
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
        static const wchar_t *styles[kFaceCount] = { L"solid", L"tinted", L"open" };
        GaugeLog(L"overlay: the cluster is drawn on a %.0f x %.0f picture (%s layout, %s style, %.3f px per reference px%s)", loggedW, loggedH,
                 LayoutName(s.layout),
                 GaugeOnStage(s) ? L"the HUD's" : styles[s.style < 0 || s.style >= kFaceCount ? kFaceOpen : s.style], u, view.demo ? L", demo values" : L"");
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
        GaugeLog(L"overlay: the Gameplay tab was opened");
    }
    GaugeSettings s;
    GaugeView view;
    const LONG turn = SettingsGet(s);
    ViewGet(view);
    // the tab it opens on: the one Gauges.ini keeps
    if (st.tab < 0)
    {
        char id[32];
        PageTabRead(id, sizeof id);
        st.tab = PageTabFromId(id);
    }
    // the board in place of the tab's body, so a click beside it reaches nothing
    if (g_ckWin.open)
    {
        CheckersPage();
        return;
    }
    // the pad is read on every frame of the tab: the frame the binding's button is pressed on has to know that the
    // pad's A, which pressed it, is still down
    // the Engine tab's Profiles: the list the pass made, and the first look at the folder when the tab is first shown
    ProfilesGet(st.prof);
    int act = SettingsPage(s, view, st, PadButtons());
    if (!(act & kPageProfile) && st.tab == kTabEngine && !st.profLooked)
    {
        st.profAsk = ProfileAsk{};
        st.profAsk.what = kProfAskLook;
        act |= kPageProfile;
    }
    if (act & kPageProfile)
    {
        st.profLooked = true;
        ProfileAskPut(st.profAsk);
    }
    if (act & kPageCheckers)
    {
        if (!s.checkers)
        {
            s.checkers = true;
            act |= kPageChanged;
            GaugeLog(L"checkers: the board was opened for the first time; its row with a binding is on the Mod work tab from now on");
        }
        CheckersOpen(view);
    }
    if (act & kPageTab) PageTabWrite(kPageTabIds[st.tab]);
    if (act & kPageChanged) SettingsPut(s, turn);
    if (act & kPageTruckDrop) SettingsTruckDrop();
    if (act & kPageSuspOn) SettingsSuspOwn(true);
    if (act & kPageSuspOff) SettingsSuspOwn(false);
    if (act & kPageSuspDefault) SettingsSuspToDefault();
    if (act & kPageSuspTruck) SettingsSuspLevel(true);
    if (act & kPageSuspKind) SettingsSuspLevel(false);
    if (act & kPageEngOn) EngineOwnAsk(true);
    if (act & kPageEngOff) EngineOwnAsk(false);
    if (act & kPageEngDefault) EngineToDefaultAsk();
    if (act & kPageEngTruck) EngineLevelAsk(true);
    if (act & kPageEngKind) EngineLevelAsk(false);
    if (act & kPagePullTest) PullTestAsk(true);
    if (act & kPagePullCancel) PullTestAsk(false);
    if (act & kPageMapAmbient) MapAmbientSet(st.mapAmbientC);
    if (act & kPageAirHeight) AirHeightAsk(st.airHeight);
    if (act & kPageGearHold) GearHoldSet(st.gearHold);
    if (act & kPageEngineSet) EngineProfileSet(st.enginePeak, st.engineAspiration, st.engineFuel);
    if (act & kPageEngineDrop) EngineProfileDrop();
    if (act & kPageEngineTorque) EngineTorqueSet(st.engineTorque);
    if (act & kPageOdoHours) OdometerHoursAsk(st.odoHours);
    if (act & kPageOdoSet) OdometerSetAsk(st.odoHours, st.odoValue);
    if (act & kPageDefaults) SettingsReset();
    if (act & kPageReload) SettingsReload();
    if (act & kPageHearHigh) SoundHear(kTurboHigh);
    if (act & kPageHearMid) SoundHear(kTurboMid);
    if (act & kPageHearLow) SoundHear(kTurboLow);
    if (act & kPageHearOil) SoundHear(kSoundOil);
}

// The overlay's hooks, first thing at the mod's start (BRIEF_overlay_late_hook.md): the game can make its swap chain
// within a second. Nothing is drawn before OverlayInit says the start is done.
bool OverlayHooks()
{
    NgClient client = {};
    client.tab = "Gameplay";
    client.version = GAUGES_VERSION;
    client.order = 1;
    client.log = GaugeLog;
    client.frame = OnFrame;
    client.page = OnPage;
    client.deviceGone = FreeFontTexture;
    client.waitReady = true;
    return NgOverlayStart(client);
}

void OverlayInit(HMODULE)
{
    // the game's font files sit next to the exe
    wchar_t dir[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
    wchar_t *slash = n && n < MAX_PATH ? wcsrchr(dir, L'\\') : nullptr;
    if (slash) *slash = 0;
    g_fontFiles = slash && FontsLoad(g_fonts, dir, kGaugeStyles, kGsStyleCount);
    g_warnFontFiles = slash && FontsLoad(g_warnFonts, dir, kWarnStyles, kWsStyleCount);
    g_signFontFiles = slash && FontsLoad(g_signFonts, dir, kWarnStyles, kWsStyleCount);
    GaugeLog(g_fontFiles ? L"overlay: game font read (TT Lakes Compressed)" : L"overlay: game font not found, Dear ImGui's font stands in");
    NgOverlayReady();
}
