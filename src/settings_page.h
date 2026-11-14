// settings_page.h: the Gameplay tab of the NEXT GEN menu, every Gauges.ini setting edited in the game. Shared by the mod
// (gauge_overlay.cpp, on the mod's own overlay) and the offline previews (test\gauge_preview.cpp, test\menu_preview.cpp).
// Include after imgui.h. The tab works on a copy of the settings in force and reports what to do with it; the menu
// hands every key to Dear ImGui while it is open, so a new toggle key is read from there.
#pragma once
#include "gauges.h"
#include "air_axles.h"
#include "odometer.h"
#include "checkers.h"

struct PageState
{
    int capture = -1;             // -1 none, 0 the toggle key, 1 the pad buttons
    uint32_t peak = 0;            // the pad buttons held together so far while capturing
    bool armed = false;           // every pad button was up since the capture began: the press that began it is not read
    unsigned long long began = 0; // GetTickCount64 when the capture began (it gives up after kPageCaptureMs)
    unsigned long long last = 0;  // GetTickCount64 of the last frame the tab was drawn
    float mapAmbientC = 0.0f;     // the outside temperature the slider set for the map's section in force (kPageMapAmbient)
    int airHeight = 0;            // the air's height the Suspension tab's slider set, cm (kPageAirHeight)
    float gearHold = 0.0f;        // the seconds the slider set for the driven truck's gearbox (kPageGearHold)
    int engRows = 0;              // the rows of the driven truck's engine chain moved this frame: 1 Engine braking (engBrake), 2 the creep (engCreep, engCreepManual)
    float engBrake = 0.0f;
    bool engCreep = false, engCreepManual = false;
    float enginePeak = 0.0f;      // the driven truck's engine's profile as the Engine section set it (kPageEngineSet)
    int engineAspiration = 0;
    int engineFuel = kFuelClass;
    int engineTorque = 100;       // and how much of its torque the truck gets, in percent, as the slider set it (kPageEngineTorque)
    bool odoHours = false;        // the Display tab's Counts picked hours (kPageOdoHours), and Set's figure in hours (kPageOdoSet)
    double odoValue = 0.0;        // Set's figure: metres, or seconds with hours
    char odoText[24] = "";        // Set's field as typed, and the word under it where it was refused
    const char *odoWord = "";
    CkClicks ckClicks = {};       // the clicks on the foot line of the Mod work tab (kPageCheckers)
    ProfileList prof = {};        // the Engine tab's Profiles: the list the pass made (the host copies it in before the page)
    ProfileAsk profAsk = {};      // ... and what a press asks of the pass (kPageProfile)
    char profName[kProfileNameMost + 8] = "", profNotes[kProfileNotesMost + 8] = ""; // Export's fields as typed
    char profOver[kProfileNameMost + 8] = ""; // the name Export was pressed with once though a file has it: the next press writes over it
    char profOther[200] = "";     // the row of another kind a button was pressed on once: the next press of that button takes it
    bool profOtherBackup = false;
    int profOtherWhat = 0;
    bool profEngineToo = false;   // the tick: an Import also writes the engine's own default
    bool profLooked = false;      // the host asked for the list once
    const char *profWord = "";    // the tab's own word, before the pass has answered
    int tab = -1;                 // the open tab (kTab...): the host sets the one Gauges.ini keeps before the first frame, else Display
    int tabAsk = -1;              // a tab to open on the next frame (the previews)
    int tabSettle = 0;            // frames until an asked tab is the shown one
    bool tabShown = false;        // the tab bar was drawn: st.tab opens on its first frame
    bool wholeTab = false;        // each tab at its full height, with no scroll of its own (the previews' pictures)
};
static const unsigned long long kPageCaptureMs = 8000;
enum { kPageChanged = 1, kPageReload = 2, kPageTruckDrop = 4, kPageDefaults = 8, kPageHearHigh = 16, kPageHearMid = 32, kPageHearLow = 64, kPageHearOil = 128,
       kPagePullTest = 256, kPagePullCancel = 512, kPageMapAmbient = 1024, kPageGearHold = 2048, kPageEngineSet = 4096,
       kPageEngineDrop = 8192, kPageTab = 16384, kPageEngineTorque = 32768, kPageSuspOn = 65536, kPageSuspOff = 131072, kPageSuspDefault = 262144, kPageSuspTruck = 524288, kPageSuspKind = 1048576,
       kPageEngOn = 2097152, kPageEngOff = 4194304, kPageEngDefault = 8388608, kPageEngTruck = 16777216, kPageEngKind = 33554432, kPageAirHeight = 67108864,
       kPageOdoSet = 134217728, kPageOdoHours = 268435456, kPageCheckers = 536870912, kPageProfile = 1073741824 };

// The Dear ImGui key as a virtual key, for the keys the toggle key can be; 0 for others.
static int PageVirtualKey(int k)
{
    if (k >= ImGuiKey_F1 && k <= ImGuiKey_F24) return VK_F1 + (k - ImGuiKey_F1);
    if (k >= ImGuiKey_A && k <= ImGuiKey_Z) return 'A' + (k - ImGuiKey_A);
    if (k >= ImGuiKey_0 && k <= ImGuiKey_9) return '0' + (k - ImGuiKey_0);
    if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) return VK_NUMPAD0 + (k - ImGuiKey_Keypad0);
    switch (k)
    {
    case ImGuiKey_Insert: return VK_INSERT;
    case ImGuiKey_Delete: return VK_DELETE;
    case ImGuiKey_Home: return VK_HOME;
    case ImGuiKey_End: return VK_END;
    case ImGuiKey_PageUp: return VK_PRIOR;
    case ImGuiKey_PageDown: return VK_NEXT;
    case ImGuiKey_Pause: return VK_PAUSE;
    case ImGuiKey_ScrollLock: return VK_SCROLL;
    default: return 0;
    }
}

// The row's name in the left column (PageLabel) was hovered, and the line it is on: the row's PageTip shows over it as
// over the widget, only for an item on that line, so a row with no tip of its own does not lend its name to the next.
static bool g_pageLabelHovered = false;
static float g_pageLabelY = 0.0f;

// A tip drawn wrapped at kPageTipWrap font heights (BRIEF_full_pass_gameplay.md part 2, FR7: a plain tooltip has no wrap,
// so a long tip was cut at the screen edge). A short one stays as narrow as its text. Tire's tab wraps at the same width.
static const float kPageTipWrap = 35.0f;
static void PageTipShow(const char *text)
{
    if (!ImGui::BeginTooltip()) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * kPageTipWrap);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
// The four names of the Diff row, for the rows by axle under it (the first is Auto with what it picks on this truck).
static const char *g_pageDiffNames[4] = { "Auto", "Open", "Limited slip", "Locked" };
static void PageTip(const char *text)
{
    const bool label = g_pageLabelHovered && fabsf(ImGui::GetItemRectMin().y - g_pageLabelY) < ImGui::GetFrameHeight() * 0.5f;
    if (label || ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) PageTipShow(text);
    g_pageLabelHovered = false;
}

// A class's name in the tab's text (GaugeSettings::tachClass).
static const char *PageClassWord(int tachClass)
{
    static const char *const words[kClassCount] = { "truck", "scout", "heavy" };
    return words[ClassOr(tachClass, kClassTruck)];
}

// Whose engine or gearbox figure is in force (GaugeView::engTorqueFrom, engHoldFrom: 0 none, 1 the default, 2 the kind's, 3 this truck's).
// The kind's level is followed by the truck's name.
static const char *PageEngLevel(int from, const char *what)
{
    static char text[64];
    if (from == 3) snprintf(text, sizeof text, "this %s in this truck", what);
    else if (from == 2) snprintf(text, sizeof text, "this %s in every ", what);
    else if (from == 1) snprintf(text, sizeof text, "the default of this %s", what);
    else snprintf(text, sizeof text, "none set, the stock figure");
    return text;
}

// A label in the left column, the widget after it.
static void PageLabel(const char *text, float x)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    g_pageLabelHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
    g_pageLabelY = ImGui::GetItemRectMin().y - ImGui::GetStyle().FramePadding.y; // the line's top, where the widget after it starts
    ImGui::SameLine(x);
}

// One frame of the pad buttons being set. padNow: the pad's buttons this frame; beganNow: the capture began on this
// very frame (the button that began it may have been pressed with the pad's A, which is then still down, so this frame
// never counts the pad as let go); escape: Esc was pressed. True when the capture is over: bound then holds the buttons
// to bind, 0 = keep the old ones. The press that began the capture is not read: reading starts once every button is
// up; the buttons then held together are kept when they are let go.
static bool PageCaptureStep(PageState &st, uint32_t padNow, bool beganNow, bool escape, unsigned long long now, uint32_t &bound)
{
    bound = 0;
    if (escape || (!st.peak && now - st.began > kPageCaptureMs)) return true;
    if (!st.armed) { st.armed = !beganNow && padNow == 0; return false; }
    if (padNow) { st.peak |= padNow; return false; }
    if (!st.peak) return false;
    bound = st.peak;
    return true;
}

// The Gameplay tab's own tabs (BRIEF_overlay_tabs_and_text.md): the shown names, the fixed ids Gauges.ini keeps for
// the open one ([Gauges] Tab, so a later rename does not lose it), and a line on what each is for.
enum { kTabDisplay, kTabEngine, kTabGearbox, kTabPower, kTabSuspension, kTabControls, kTabSounds, kTabModWork, kTabCount };
static const char *const kPageTabNames[kTabCount] = { "Display", "Engine", "Gearbox", "Power", "Suspension", "Controls", "Sounds", "Mod work" };
static const char *const kPageTabIds[kTabCount] = { "display", "engine", "gearbox", "power", "suspension", "controls", "sounds", "modwork" };
static const char *const kPageTabHelp[kTabCount] = {
    "How the cluster looks and what it shows.",
    "The engine torque curve, its scales, its temperatures and the damage they do.",
    "Who shifts the gears, the clutch and the creep.",
    "How much of the mod engine the truck pulls with, and the air drag.",
    "Tests of the springs, the dampers, the ride height and the sway bars.",
    "What you drive with, the keys and the bindings.",
    "The turbo sounds and the oil warning.",
    "For work on the mod: demo values, the probe log, the defaults and the ini.",
};

// The tab an id names; Display for an id no tab has.
static int PageTabFromId(const char *id)
{
    for (int i = 0; id && i < kTabCount; i++)
        if (!strcmp(id, kPageTabIds[i])) return i;
    return kTabDisplay;
}

// The tab bar. The tab in st.tab opens on the first frame (and st.tabAsk's when set); a tab picked after that is
// reported with kPageTab, so the host keeps it in Gauges.ini on its own and not with the settings. The open tab.
static int PageTabs(PageState &st, int &act)
{
    if (st.tab < 0 || st.tab >= kTabCount) st.tab = kTabDisplay;
    int ask = st.tabAsk >= 0 && st.tabAsk < kTabCount ? st.tabAsk : -1;
    if (!st.tabShown) ask = ask >= 0 ? ask : st.tab;
    if (ask >= 0)
    {
        st.tab = ask;
        st.tabSettle = 2; // Dear ImGui selects an asked tab from the next frame on
    }
    st.tabAsk = -1;
    st.tabShown = true;
    int shown = -1;
    if (ImGui::BeginTabBar("##gameplaytabs"))
    {
        for (int i = 0; i < kTabCount; i++)
            if (ImGui::BeginTabItem(kPageTabNames[i], nullptr, i == ask ? ImGuiTabItemFlags_SetSelected : 0))
            {
                shown = i;
                ImGui::EndTabItem();
            }
        ImGui::EndTabBar();
    }
    if (st.tabSettle > 0) st.tabSettle--;
    else if (shown >= 0 && shown != st.tab)
    {
        st.tab = shown;
        act |= kPageTab;
    }
    return st.tabSettle > 0 || shown < 0 ? st.tab : shown;
}

// Draws the tab on s (the settings in force) and returns kPageChanged when s was edited, kPageReload for the ini
// button, kPageTruckDrop for the button that gives the driven truck the plain scales back, kPageDefaults for the
// defaults button, kPageTab when another tab was picked (st.tab). view: what the cluster shows now; padNow: the pad's buttons this frame, for pad buttons being set.
static int SettingsPage(GaugeSettings &s, const GaugeView &view, PageState &st, uint32_t padNow)
{
    int act = 0;
    auto changed = [&](bool c) { if (c) act |= kPageChanged; };
    const unsigned long long now = GetTickCount64();
    if (now - st.last > 1000) st.capture = -1; // the overlay was closed in between
    st.last = now;
    g_pageLabelHovered = false;
    const float em = ImGui::GetFontSize(), labelX = em * 12.0f, w = em * 14.0f;
    char buf[64];

    // the tab bar, a line on the open tab, and the tab in a child of its own that keeps its own scroll (the host draws
    // the page in a scrolling child, which the bar would scroll away with)
    const int tab = PageTabs(st, act);
    // a capture left armed on another tab would bind the next key or button unseen (the Reviewer's N4)
    if (act & kPageTab)
    {
        st.capture = -1;
        if (view.captureAction >= 0) BindingAsk(kBindAskCancel, view.captureAction, view.captureSlot);
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", kPageTabHelp[tab]);
    ImGui::PopTextWrapPos();
    char child[32];
    _snprintf_s(child, _TRUNCATE, "##tab_%s", kPageTabIds[tab]);
    const bool open = ImGui::BeginChild(child, ImVec2(0.0f, 0.0f), ImGuiChildFlags_NavFlattened | (st.wholeTab ? ImGuiChildFlags_AutoResizeY : 0));

    // the tips name temperatures in the unit the dial shows
    auto degrees = [&](float c, char *out, size_t size) { _snprintf_s(out, size, _TRUNCATE, s.fahrenheit ? "%.0f F" : "%.0f C", s.fahrenheit ? c * 1.8f + 32.0f : c); };

    if (open && tab == kTabDisplay)
    {
        ImGui::SeparatorText("Cluster");
        const bool off = s.layout == kLayoutOff; // no dials: the rows that only shape them are drawn disabled
        if (off) ImGui::TextDisabled("Not drawn: Layout is Off. Every feature still acts.");
        else if (view.show) ImGui::TextUnformatted(view.demo ? "On screen, with demo values." : "On screen.");
        else if (view.hidden == kHiddenByToggle) ImGui::TextDisabled("Hidden. The toggle key or the pad buttons show it again.");
        else if (view.hidden == kHiddenSoundsOnly) ImGui::TextDisabled("Not drawn: \"Sounds only\" is ticked under Sounds.");
        else ImGui::TextDisabled("No truck is being driven. The cluster shows while you drive.");
        static const char *layouts[kLayoutCount] = { "Strip, bottom centre", "Block, bottom right", "Spotter dials", "Spotter digital", "Off" };
        PageLabel("Layout", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::Combo("##layout", &s.layout, layouts, kLayoutCount));
        PageTip("Where the cluster sits. Spotter dials and Spotter digital draw it in the HUD style of Spotter. Off draws no dials: the features act as before. Default: Strip.");
        if (off)
        {
            // the warnings with the dials off (warn_list.h): the choice, and with "At my limits" a slider a limit, each with an off end
            static const char *warns[kHwCount] = { "None", "As with dials", "At my limits" };
            PageLabel("Warnings", labelX);
            ImGui::SetNextItemWidth(w);
            changed(ImGui::Combo("##hiddenwarnings", &s.hiddenWarnings, warns, kHwCount));
            PageTip("What still warns with the dials off. None: no box and no lamp. As with dials: the clutch and engine boxes. At my limits: a box when a figure passes its limit below.");
            if (s.hiddenWarnings == kHwLimits)
            {
                PageLabel("Fuel under", labelX);
                ImGui::SetNextItemWidth(w);
                changed(ImGui::SliderInt("##warnfuel", &s.warnFuel, 0, 50, s.warnFuel > 0 ? "%d %%" : "off", ImGuiSliderFlags_AlwaysClamp));
                PageTip("A LOW FUEL box while the tank holds less than this share. 0 is off. Default 15.");
                PageLabel("Engine damage over", labelX);
                ImGui::SetNextItemWidth(w);
                changed(ImGui::SliderInt("##warnengine", &s.warnEngine, 0, 100, s.warnEngine < 100 ? "%d %%" : "off", ImGuiSliderFlags_AlwaysClamp));
                PageTip("An ENGINE WORN box while the engine damage is over this share of what the engine takes. 100 is off. Default 50.");
                PageLabel("Gearbox damage over", labelX);
                ImGui::SetNextItemWidth(w);
                changed(ImGui::SliderInt("##warngearbox", &s.warnGearbox, 0, 100, s.warnGearbox < 100 ? "%d %%" : "off", ImGuiSliderFlags_AlwaysClamp));
                PageTip("A GEARBOX WORN box while the gearbox damage is over this share of what the gearbox takes. 100 is off. Default 50.");
                char warm[16], red[16], tip[200];
                degrees(kTempWarm, warm, sizeof warm);
                degrees(kTempRed, red, sizeof red);
                _snprintf_s(tip, _TRUNCATE, "An ENGINE HOT box from this share of the way from warm (%s) to the red mark (%s). 100 is off. Default 90.", warm, red);
                PageLabel("Coolant over", labelX);
                ImGui::SetNextItemWidth(w);
                changed(ImGui::SliderInt("##warncoolant", &s.warnCoolant, 50, 100, s.warnCoolant < 100 ? "%d %%" : "off", ImGuiSliderFlags_AlwaysClamp));
                PageTip(tip);
                ImGui::TextDisabled("Oil, tilt and brake air warn at their own points.");
            }
        }
        ImGui::BeginDisabled(s.layout == kLayoutHudDigital || off);
        PageLabel("Swap dials", labelX);
        changed(ImGui::Checkbox("##swapdials", &s.swapDials));
        PageTip("The speedometer and the tachometer change places. Off by default.");
        ImGui::EndDisabled();
        // Style and Face opacity are the strip's and the block's; the HUD layouts keep them for a way back
        const bool hud = GaugeOnStage(s) || off;
        ImGui::BeginDisabled(hud);
        static const char *styles[kFaceCount] = { "Solid", "Tinted", "Open" };
        PageLabel("Style", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::Combo("##style", &s.style, styles, kFaceCount));
        PageTip("Solid: dark faces with a bezel. Tinted: see-through faces. Open: only the scales, numbers and needles. The strip and the block only. Default: Open.");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(hud || s.style != kFaceTinted);
        PageLabel("Face tint", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##face", &s.faceOpacity, 0.0f, kFaceOpacityMax, "%.2f", ImGuiSliderFlags_AlwaysClamp));
        PageTip("How dark the tinted faces are. 0 is clear. Default 0.35.");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(off);
        PageLabel("Size", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##scale", &s.scale, 0.5f, 2.5f, "x%.2f", ImGuiSliderFlags_AlwaysClamp));
        PageTip("The cluster size. Default x1.00.");
        PageLabel("Opacity", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##opacity", &s.opacity, kOpacityMin, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp));
        PageTip("How solid the whole cluster is drawn. Default 1.");
        PageLabel("Position", labelX);
        ImGui::SetNextItemWidth(w * 0.5f - ImGui::GetStyle().ItemInnerSpacing.x * 0.5f);
        changed(ImGui::DragFloat("##x", &s.offsetX, 1.0f, -kOffsetMax, kOffsetMax, "%.0f right", ImGuiSliderFlags_AlwaysClamp));
        PageTip("Moves the cluster from its place in pixels of a 1080p screen. Drag or double-click to type. Default 0, 0.");
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(w * 0.5f - ImGui::GetStyle().ItemInnerSpacing.x * 0.5f);
        changed(ImGui::DragFloat("##y", &s.offsetY, 1.0f, -kOffsetMax, kOffsetMax, "%.0f down", ImGuiSliderFlags_AlwaysClamp));
        PageTip("Moves the cluster from its place in pixels of a 1080p screen. Drag or double-click to type. Default 0, 0.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset###origin"))
        {
            s.offsetX = s.offsetY = 0.0f;
            act |= kPageChanged;
        }
        PageTip("Back to where the layout puts it.");

        ImGui::SeparatorText("Dials");
        PageLabel("Coolant temperature", labelX);
        changed(ImGui::Checkbox("##showtemp", &s.showTemp));
        PageTip("The coolant temperature dial. On by default.");
        PageLabel("Boost pressure", labelX);
        changed(ImGui::Checkbox("##showboost", &s.showBoost));
        PageTip("The turbo boost dial. On by default.");
        PageLabel("Brake air pressure", labelX);
        changed(ImGui::Checkbox("##showair", &s.showAir));
        PageTip("The air in the brake tanks: filled up to 8.2 bar and red under 5.5. Not drawn for a truck without Air brakes on the Engine tab. Off by default.");
        PageLabel("Clock", labelX);
        changed(ImGui::Checkbox("##showclock", &s.showClock));
        PageTip("The time of day in the game, with the outside temperature in its face. On by default.");
        PageLabel("Fuel use", labelX);
        changed(ImGui::Checkbox("##showfueluse", &s.showFuelUse));
        PageTip(s.mph ? "The fuel used in gallons an hour, under the range of the fuel dial. On by default." : "The fuel used in litres a minute, under the range of the fuel dial. On by default.");
        PageLabel("Lamps", labelX);
        changed(ImGui::Checkbox("##showlamps", &s.showLamps));
        PageTip("Handbrake, lights, AWD, diff lock, low gear, engine damage, cold engine, tilt and oil. On by default.");
        PageLabel("Odometer", labelX);
        changed(ImGui::Checkbox("##showodometer", &s.showOdometer));
        PageTip(s.mph ? "Miles driven under the speed, or engine hours where Counts says Hours. Each truck keeps its own count. On by default."
                      : "Kilometres driven under the speed, or engine hours where Counts says Hours. Each truck keeps its own count. On by default.");
        static const char *oils[kOilGaugeCount] = { "Not shown", "Second needle", "Own dial" };
        PageLabel("Oil gauge", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::Combo("##oilgauge", &s.oilGauge, oils, kOilGaugeCount));
        PageTip("Second needle: an amber needle in the coolant dial. Own dial: a small dial beside it. The oil lamp shows either way. Default: Second needle.");

        ImGui::EndDisabled();

        ImGui::SeparatorText("Odometer");
        {
            // the driven truck's count (BRIEF_odometer.md): the kind's choice of hours, and Set to for this truck only
            if (!view.odoKnown) ImGui::TextDisabled("No truck is being driven, or its id is still being looked up.");
            else ImGui::Text("This truck: %s", view.odoText);
            static const char *counts[2] = { "Distance", "Hours" };
            int count = view.odoHours ? 1 : 0;
            PageLabel("Counts", labelX);
            ImGui::SetNextItemWidth(w);
            ImGui::BeginDisabled(!view.odoKnown);
            if (ImGui::Combo("##odocounts", &count, counts, 2))
            {
                st.odoHours = count == 1;
                act |= kPageOdoHours;
            }
            ImGui::EndDisabled();
            PageTip("Hours: the running hours of the engine instead of the distance, for machinery that works standing. Kept for this kind of truck.");
            PageLabel("Set to", labelX);
            ImGui::BeginDisabled(!view.odoCanSet);
            ImGui::SetNextItemWidth(w * 0.5f);
            ImGui::InputText("##odoset", st.odoText, sizeof st.odoText);
            PageTip("Writes this figure as the count of this truck. For a truck bought used. 0 starts it anew.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Set###odosetbutton"))
            {
                if (OdoSetValue(st.odoText, view.odoHours, s.mph, st.odoValue))
                {
                    st.odoHours = view.odoHours;
                    st.odoWord = "";
                    act |= kPageOdoSet;
                }
                else st.odoWord = view.odoHours ? "Not set: a number from 0 to 1 000 000 h." : s.mph ? "Not set: a number from 0 to 10 000 000 mi." : "Not set: a number from 0 to 10 000 000 km.";
            }
            ImGui::EndDisabled();
            if (st.odoWord[0]) ImGui::TextDisabled("%s", st.odoWord);
        }

        ImGui::SeparatorText("Tilt lamp");
        {
            // red never comes before amber: the one being dragged takes the other along
            PageLabel("Amber tilt", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderFloat("##rollwarn", &s.rollWarnDeg, 5.0f, 45.0f, "%.0f degrees to a side", ImGuiSliderFlags_AlwaysClamp))
            {
                s.rollWarnDeg = floorf(s.rollWarnDeg + 0.5f);
                if (s.rollDangerDeg < s.rollWarnDeg) s.rollDangerDeg = s.rollWarnDeg;
                act |= kPageChanged;
            }
            PageTip("The tilt lamp turns amber once the truck leans this far to a side. Default 15 degrees.");
            PageLabel("Red tilt", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderFloat("##rolldanger", &s.rollDangerDeg, 5.0f, 60.0f, "%.0f degrees to a side", ImGuiSliderFlags_AlwaysClamp))
            {
                s.rollDangerDeg = floorf(s.rollDangerDeg + 0.5f);
                if (s.rollWarnDeg > s.rollDangerDeg) s.rollWarnDeg = s.rollDangerDeg;
                act |= kPageChanged;
            }
            PageTip("The lamp turns red once the truck leans this far. The truck in the lamp leans as yours does. Default 25 degrees.");
            PageLabel("Red on slopes", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderFloat("##pitchdanger", &s.pitchDangerDeg, 10.0f, 60.0f, "%.0f degrees up or down", ImGuiSliderFlags_AlwaysClamp))
            {
                s.pitchDangerDeg = floorf(s.pitchDangerDeg + 0.5f);
                act |= kPageChanged;
            }
            PageTip("The lamp turns red once the truck points this far up or down a slope. Default 35 degrees.");
        }

        ImGui::SeparatorText("Units");
        PageLabel("Miles per hour", labelX);
        changed(ImGui::Checkbox("##mph", &s.mph));
        PageTip("Speed in mph and the range in miles. Off (default): km/h and km.");
        PageLabel("PSI", labelX);
        changed(ImGui::Checkbox("##psi", &s.psi));
        PageTip("Boost in psi. Off (default): bar.");
        PageLabel("Fahrenheit", labelX);
        changed(ImGui::Checkbox("##fahrenheit", &s.fahrenheit));
        PageTip("Every temperature in Fahrenheit. Off (default): Celsius.");
    }
    else if (open && tab == kTabEngine)
    {
        ImGui::SeparatorText("Engine");
        // the driven truck's engine: its peak torque's speed and how it breathes, kept for that engine in its own file
        if (!view.engine[0]) ImGui::TextWrapped("No truck is driven, or its engine is not known yet.");
        else
        {
            // the profile's level is in the "In force:" line under Fuel (BRIEF_engine_profile_per_truck.md)
            if (view.engineOwn) ImGui::TextWrapped("Engine %s. It has a profile of its own.", view.engine);
            else ImGui::TextWrapped("Engine %s. It pulls by the torque curve of the %s class%s.%s", view.engine, PageClassWord(view.autoClass),
                                    view.autoByType ? " (the class of the truck by its type in the game files)" : view.autoByWeight ? " (the class of the truck by its weight)" : "", view.engProfileFrom ? "" : " Set its peak torque here to give it a profile of its own.");
            if (view.engineFigure > 0.0f) ImGui::TextWrapped("Its torque in the game files: %.0f (the engine of a stock scout reads about 195000).", view.engineFigure);
            // whose Engine torque and Upshift delay these are (BRIEF_engine_per_truck.md): this truck's, the kind's, or
            // the engine's default; the box, the For choice and the kept texts are the Suspension tab's
            if (view.truck[0])
            {
                PageLabel("Own settings", labelX);
                bool own = view.engSetOwn;
                if (ImGui::Checkbox("##engown", &own)) act |= own ? kPageEngOn : kPageEngOff;
                PageTip("This kind of truck keeps its own engine profile, Engine torque and Upshift delay in its file in Gauges\\trucks, by the engine and gearbox fitted. Ticks itself "
                        "when one is changed while it is driven. Off: the engine default is in force and its own are kept.");
                if (view.engSetOwn)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Make this the engine default")) act |= kPageEngDefault;
                    PageTip("Copies the engine profile and Engine torque of this truck into the file of the engine, and its Upshift delay into the file of the gearbox, for every truck without "
                            "its own. This truck keeps its own.");
                    PageLabel("For", labelX);
                    ImGui::BeginDisabled(!view.suspIdKnown);
                    if (ImGui::RadioButton("This truck only###engtruck", view.engSetLevel == 3) && view.engSetLevel != 3) act |= kPageEngTruck;
                    ImGui::EndDisabled();
                    PageTip("This truck alone keeps the set, by the id the game gives it. A truck taken out of the warehouse gets a new id. It gets its set back where exactly "
                            "one waiting truck of the kind has the same engine and gearbox. "
                            "The id can also change at a load. Then the truck loses its set until the mod gives trucks an id of its own.");
                    ImGui::SameLine();
                    char every[128];
                    snprintf(every, sizeof every, "Every %s###engkind", view.truck);
                    if (ImGui::RadioButton(every, view.engSetLevel == 2) && view.engSetLevel != 2) act |= kPageEngKind;
                    PageTip(view.engSetKindSet ? "Every truck of this kind uses the set of the kind. The set of this truck is kept, not in use: This truck only brings it back."
                                               : "Every truck of this kind keeps this set. The kind has no set yet, so the set of this truck goes up to it.");
                }
                else if (view.engSetKept) ImGui::TextDisabled("Kept, not in use. Changes here edit the engine default.");
                if (view.engSetOwn && view.engSetLevel != 3 && view.engSetTruckKept) ImGui::TextDisabled("The set of this truck is kept, not in use: This truck only brings it back.");
                if (view.idWords[0]) ImGui::TextDisabled("%s", view.idWords);
            }
            float peak = view.enginePeakRpm;
            int aspiration = view.engineAspiration, fuel = view.engineFuel;
            bool set = false;
            PageLabel("Peak torque", labelX);
            ImGui::SetNextItemWidth(w);
            if (view.engineCustom) ImGui::BeginDisabled();
            if (ImGui::SliderFloat("##peakrpm", &peak, view.engineIdle, view.engineRedline, "%.0f rpm", ImGuiSliderFlags_AlwaysClamp)) set = true;
            if (view.engineCustom) ImGui::EndDisabled();
            PageTip(view.engineCustom ? "Its file has a torque curve of its own (TorqueCurve), which is used instead."
                                  : "The engine speed of its peak torque, as its maker gives it; the curve and the shifts of the mod automatic are built around it. Default: the class curve.");
            static const char *aspirations[kAspCount] = { "Turbo", "Naturally aspirated" };
            PageLabel("Aspiration", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::Combo("##aspiration", &aspiration, aspirations, kAspCount)) set = true;
            PageTip("Naturally aspirated: no boost, so no boost dial and no turbo sounds. Default: Turbo.");
            const char *fuels[kFuelCount] = { view.autoClass == kClassScout ? "Class (petrol)" : "Class (diesel)", "Diesel", "Petrol" };
            PageLabel("Fuel", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::Combo("##fuel", &fuel, fuels, kFuelCount)) set = true;
            PageTip("The boost of a diesel rises from nothing at idle. The dial of a petrol engine reads under 0 at idle and a light foot. Default: by the class, petrol for scouts and diesel for the rest.");
            ImGui::TextDisabled("In force: %s%s", PageEngLevel(view.engProfileFrom, "engine"), view.engProfileFrom == 2 ? view.truck : "");
            // how much of its torque the truck gets, kept in percent
            int torquePercent = (int)lroundf(view.engineTorque * 100.0f);
            PageLabel("Engine torque", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderInt("##torqueshare", &torquePercent, (int)(kTorqueShareRange[0] * 100.0f), (int)(kTorqueShareRange[1] * 100.0f), "%d %%", ImGuiSliderFlags_AlwaysClamp))
            {
                st.engineTorque = torquePercent;
                act |= kPageEngineTorque;
            }
            PageTip("Percent of the torque of this engine the truck gets with Real engine on; lower it for an engine that pulls far too hard. Engine share on the Power tab mixes the result with the game pull. Default 100 %.");
            ImGui::TextDisabled("In force: %s%s", PageEngLevel(view.engTorqueFrom, "engine"), view.engTorqueFrom == 2 ? view.truck : "");
            if (set)
            {
                st.enginePeak = floorf(peak / 50.0f + 0.5f) * 50.0f;
                st.engineAspiration = aspiration;
                st.engineFuel = fuel;
                act |= kPageEngineSet;
            }
            PageLabel("Curve", labelX);
            ImGui::PlotLines("##torquecurve", view.engineCurve, kEnginePlot, 0, nullptr, 0.0f, 1.05f, ImVec2(w, 60.0f * ImGui::GetFontSize() / 13.0f));
            PageTip("The torque in force from idle to the top of the tachometer, as a share of its peak.");
            if (view.engineOwn)
            {
                if (ImGui::SmallButton("Class curve")) act |= kPageEngineDrop;
                PageTip("This engine pulls by the curve of its class again, at the level in force: this truck, every truck of its kind, or the engine default.");
            }
        }

        ImGui::SeparatorText("Scales");
        // whose scales these are: the driven truck's own, or the plain ones
        const char *const keeper = view.scalesIdKnown ? "this truck" : "this kind of truck";
        if (!view.truck[0]) ImGui::TextWrapped("No truck is driven. These are the plain scales, which every truck without scales of its own shows.");
        else if (view.scalesLevel == 0 && !view.truckOwn)
            ImGui::TextWrapped("Scales for %s. It shows the plain ones. Change one here and %s keeps all seven as its own.", view.truck, keeper);
        else if (view.scalesLevel == 2)
        {
            ImGui::TextWrapped("Scales for %s. They are its own, kept for this truck alone in the file of its kind in Gauges\\trucks.", view.truck);
            if (ImGui::SmallButton("Scales of the kind")) act |= kPageTruckDrop;
            PageTip("This truck gives up its own scales. It shows the scales of its kind again, or the plain ones where the kind has none.");
        }
        else
        {
            ImGui::TextWrapped("Scales for %s. They are the scales of its kind, kept in its file in Gauges\\trucks.%s", view.truck,
                               view.scalesIdKnown ? " Change one here and this truck keeps all seven as its own." : "");
            if (ImGui::SmallButton("Plain scales")) act |= kPageTruckDrop;
            PageTip("This kind of truck gives up its scales. Every truck of the kind without scales of its own shows the plain ones again.");
        }
        // Auto first: the class by the truck itself. A picked class is index 1 to 3 (kClassTruck is 0, kClassAuto -1)
        static const char *const autos[kClassCount + 1] = { "Auto (Truck)", "Auto (Scout)", "Auto (Heavy)", "Auto" };
        const char *const classes[kClassCount + 1] = { view.truck[0] ? autos[ClassOr(view.classAuto, kClassTruck)] : autos[kClassCount], "Truck", "Scout", "Heavy" };
        PageLabel("Truck class", labelX);
        ImGui::SetNextItemWidth(w);
        const int classWas = ClassOr(s.tachClass, view.classAuto);
        int classRow = ClassOr(s.tachClass, kClassAuto) + 1;
        if (ImGui::Combo("##class", &classRow, classes, kClassCount + 1))
        {
            s.tachClass = classRow - 1;
            if (ClassOr(s.tachClass, view.classAuto) != classWas) ScalesClassChange(s, classWas);
            act |= kPageChanged;
        }
        PageTip("Sets the idle, the red zone, the tops of both dials and the boost for a truck, a scout or a heavy truck. Auto goes by the type of the truck in the game files and by its weight. Pick a class to overrule that. The mod automatic and Air brakes go by the same class. Class beside a number set by hand gives it back. Default: Auto.");
        // the brakes of the driven truck (BRIEF_spring_brakes.md): its own choice in its file, by its class until it has one
        {
            ImGui::BeginDisabled(!view.truck[0]);
            PageLabel("Air brakes", labelX);
            bool airOn = AirBrakesOn(s, view.autoClass);
            if (ImGui::Checkbox("##airbrakes", &airOn))
            {
                s.airBrakes = airOn ? 1 : 0;
                act |= kPageChanged;
            }
            PageTip("This truck has air brakes: the brake air dial, the low air warning, the air sounds and spring brakes. Under 3 bar the spring brakes hold the truck as the handbrake does, until the air is back at 4.5 bar. A truck keeps the air it was left with, between drives and between sessions. Saved for this truck. Until it is set, every truck but a scout has them.");
            ImGui::EndDisabled();
        }
        // empty tanks at the first drive of a session, for every truck with air brakes (BRIEF_jobs_1114.md, part 165)
        {
            PageLabel("Empty tanks at a start", labelX);
            if (ImGui::Checkbox("##airempty", &s.airEmptyAtStart)) act |= kPageChanged;
            PageTip("The first time you drive a truck with air brakes after the game starts, its tanks are empty. It stands on its spring brakes until the compressor has filled them to 4.5 bar: about a minute and a half at idle, under half a minute at the red zone. A truck you come back to in the same session keeps its air. A truck left while its spring brakes held comes back with the handbrake on: let it go yourself. For every truck. Default: off.");
        }
        {
            const GaugeScale own = kClassScales[ClassOr(ClassOr(s.tachClass, view.classAuto), kClassTruck)];
            // each number is the class's own until it is changed here; "Class" gives it back to the class
            struct Row { const char *label, *unit, *tip; float *value; float own; const float *range; float speed; };
            const Row rows[4] = {
                { "Idle", "rpm", "Where the needle rests with the engine running. Default: by the class.", &s.idleRpm, own.idle, kIdleRange, 5.0f },
                { "Red zone", "rpm", "Where the red zone of the tachometer starts. Default: by the class.", &s.redlineRpm, own.redline, kRedlineRange, 10.0f },
                { "Tachometer top", "rpm", "The end of the tachometer scale. Default: by the class.", &s.maxRpm, own.max, kMaxRpmRange, 10.0f },
                { "Speedometer top", "km/h", "The end of the speedometer scale, in km/h. Default: by the class.", &s.speedMax, own.speedMax, kSpeedMaxRange, 0.5f } };
            for (int i = 0; i < 4; i++)
            {
                const Row &r = rows[i];
                float shown = *r.value > 0.0f ? *r.value : r.own;
                _snprintf_s(buf, _TRUNCATE, *r.value > 0.0f ? "%%.0f %s" : "%%.0f %s (class)", r.unit);
                ImGui::PushID(i);
                PageLabel(r.label, labelX);
                ImGui::SetNextItemWidth(w);
                if (ImGui::DragFloat("##scale", &shown, r.speed, r.range[0], r.range[1], buf, ImGuiSliderFlags_AlwaysClamp))
                {
                    *r.value = floorf(shown + 0.5f);
                    act |= kPageChanged;
                }
                PageTip(r.tip);
                ImGui::SameLine();
                if (ImGui::SmallButton("Class###own"))
                {
                    *r.value = 0.0f;
                    act |= kPageChanged;
                }
                ImGui::PopID();
            }
            // the turbo's boost: in bar with two decimals, so it has a row of its own
            float shown = s.boostMax > 0.0f ? s.boostMax : own.boost;
            PageLabel("Top boost", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::DragFloat("##boost", &shown, 0.01f, kBoostRange[0], kBoostRange[1], s.boostMax > 0.0f ? "%.2f bar" : "%.2f bar (class)", ImGuiSliderFlags_AlwaysClamp))
            {
                s.boostMax = floorf(shown * 100.0f + 0.5f) / 100.0f;
                act |= kPageChanged;
            }
            PageTip("The boost floored at high engine speed, in bar. Default: by the class (trucks 1.8, scouts 1, heavy trucks 2).");
            ImGui::SameLine();
            if (ImGui::SmallButton("Class###ownboost"))
            {
                s.boostMax = 0.0f;
                act |= kPageChanged;
            }
            static const char *profiles[kSpoolCount] = { "Standard", "Small and quick", "Big and slow", "Two stages" };
            PageLabel("Turbo", labelX);
            ImGui::SetNextItemWidth(w);
            changed(ImGui::Combo("##spool", &s.boostProfile, profiles, kSpoolCount));
            PageTip("How the turbo builds boost. Standard (default): by the class. Small and quick: sooner with half the lag. Big and slow: later with more lag. Two stages: sooner with 1.3 times the boost.");
        }

        ImGui::SeparatorText("Coolant");
        {
            char from[16], to[16], tip[384];
            PageLabel("Warm-up", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderFloat("##warmup", &s.warmUpMinutes, 1.0f, 15.0f, "%.1f minutes of driving", ImGuiSliderFlags_AlwaysClamp))
            {
                s.warmUpMinutes = floorf(s.warmUpMinutes * 10.0f + 0.5f) / 10.0f;
                act |= kPageChanged;
            }
            degrees(kTempOutside, from, sizeof from);
            degrees(kTempWarm, to, sizeof to);
            _snprintf_s(tip, _TRUNCATE, "The time a cold engine takes from %s to %s driven as usual. Idling takes much longer. Default 3 minutes.", from, to);
            PageTip(tip);
            PageLabel("Overheat", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderFloat("##overheat", &s.overheatMinutes, 0.5f, 10.0f, "%.1f minutes floored", ImGuiSliderFlags_AlwaysClamp))
            {
                s.overheatMinutes = floorf(s.overheatMinutes * 10.0f + 0.5f) / 10.0f;
                act |= kPageChanged;
            }
            degrees(kTempHot, from, sizeof from);
            degrees(kTempRed, to, sizeof to);
            _snprintf_s(tip, _TRUNCATE, "The time from %s into the red zone at %s floored with the wheels spinning at a standstill. Driving faster cools it. Default 1.5 minutes.", from, to);
            PageTip(tip);
            PageLabel("Cool-down", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderFloat("##cooldown", &s.coolDownMinutes, 2.0f, 60.0f, "%.0f minutes stopped", ImGuiSliderFlags_AlwaysClamp))
            {
                s.coolDownMinutes = floorf(s.coolDownMinutes + 0.5f);
                act |= kPageChanged;
            }
            degrees(kTempCooled, to, sizeof to);
            _snprintf_s(tip, _TRUNCATE, "The time a stopped engine takes from %s down to %s. Default 15 minutes.", from, to);
            PageTip(tip);
            // the outside temperature: by the map (the default), the slider shows and sets the temperature of the map's
            // section in force, its own or its region's; where none is, and with the map left out, it sets AmbientC. The
            // ini holds it in C; the slider shows it in the unit the dial shows
            PageLabel("Map climate", labelX);
            changed(ImGui::Checkbox("##ambientbymap", &s.ambientByMap));
            PageTip("The outside temperature goes by the map you drive on, as Gauges.ini sets it under [Map:<name>]. Off: one temperature on every map. On by default.");
            const bool byMap = s.ambientByMap && view.map[0] && view.mapOwn;
            const float shown = byMap ? view.ambientC : s.ambientC;
            float outside = s.fahrenheit ? shown * 1.8f + 32.0f : shown;
            PageLabel("Outside", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderFloat("##ambient", &outside, s.fahrenheit ? kAmbientRange[0] * 1.8f + 32.0f : kAmbientRange[0],
                                   s.fahrenheit ? kAmbientRange[1] * 1.8f + 32.0f : kAmbientRange[1], s.fahrenheit ? "%.0f F" : "%.0f C", ImGuiSliderFlags_AlwaysClamp))
            {
                outside = floorf(outside + 0.5f);
                const float c = s.fahrenheit ? (outside - 32.0f) / 1.8f : outside;
                if (byMap)
                {
                    st.mapAmbientC = c;
                    act |= kPageMapAmbient;
                }
                else
                {
                    s.ambientC = c;
                    act |= kPageChanged;
                }
            }
            degrees(kTempCold, to, sizeof to);
            _snprintf_s(tip, _TRUNCATE, "How cold a truck that stood long is; in colder air the engine warms up slower. The cold lamp lights under %s. Default %s.", to, (degrees(15.0f, from, sizeof from), from));
            PageTip(tip);
            // dimmed and wrapped: a map's name can be long
            ImGui::BeginDisabled();
            if (byMap)
            {
                char day[32], night[32];
                degrees(view.ambientDay, day, sizeof day);
                degrees(view.ambientNight, night, sizeof night);
                if (view.hour >= 0.0f)
                    ImGui::TextWrapped("On %s, from [Map:%s] in Gauges.ini: %s by day, %s by night, now %02d:%02d of the game day. The slider moves both.", view.map,
                                       view.mapFrom, day, night, (int)view.hour, (int)((view.hour - (int)view.hour) * 60.0f));
                else
                    ImGui::TextWrapped("On %s, from [Map:%s] in Gauges.ini: %s by day, %s by night. The game clock is not known yet (with Probe=1 the mod looks "
                                       "for it while you drive), so the mean of the day is in force. The slider moves both.", view.map, view.mapFrom, day, night);
            }
            else if (!s.ambientByMap) ImGui::TextWrapped("The same on every map.");
            else if (view.map[0]) ImGui::TextWrapped("On %s: this temperature, as Gauges.ini has no section for the map or its region.", view.map);
            else ImGui::TextWrapped("This temperature until the mod has found the game save, which says the map.");
            ImGui::EndDisabled();
            // the ini holds the speed in km/h; the slider shows it in the unit the speedometer shows
            float full = s.mph ? s.airFullKmh / 1.609f : s.airFullKmh;
            PageLabel("Full airflow", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderFloat("##airfull", &full, s.mph ? kAirFullRange[0] / 1.609f : kAirFullRange[0], s.mph ? kAirFullRange[1] / 1.609f : kAirFullRange[1],
                                   s.mph ? "%.0f mph" : "%.0f km/h", ImGuiSliderFlags_AlwaysClamp))
            {
                full = floorf(full + 0.5f);
                s.airFullKmh = SettingsRange(s.mph ? full * 1.609f : full, kAirFullRange[0], kAirFullRange[1]);
                act |= kPageChanged;
            }
            PageTip(s.mph ? "The radiator gets all its air from this speed on. Lower keeps a slow hard-pulling engine cooler. Default 19 mph."
                       : "The radiator gets all its air from this speed on. Lower keeps a slow hard-pulling engine cooler. Default 30 km/h.");
        }

        ImGui::SeparatorText("Oil");
        {
            char from[16], to[16], tip[320];
            PageLabel("Oil warm-up", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderFloat("##oilwarmup", &s.oilWarmUpMinutes, 2.0f, 30.0f, "%.1f minutes of driving", ImGuiSliderFlags_AlwaysClamp))
            {
                s.oilWarmUpMinutes = floorf(s.oilWarmUpMinutes * 10.0f + 0.5f) / 10.0f;
                act |= kPageChanged;
            }
            degrees(kTempOutside, from, sizeof from);
            degrees(kTempWarm, to, sizeof to);
            _snprintf_s(tip, _TRUNCATE, "The time the oil takes from %s to %s driven as usual, at least a minute after the coolant. Default 5 minutes.", from, to);
            PageTip(tip);
            // the ini holds the temperature in C; the slider shows it in the unit the dial shows
            float warn = s.fahrenheit ? s.oilWarnC * 1.8f + 32.0f : s.oilWarnC;
            PageLabel("Oil warning", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderFloat("##oilwarn", &warn, s.fahrenheit ? kOilWarnRange[0] * 1.8f + 32.0f : kOilWarnRange[0],
                                   s.fahrenheit ? kOilWarnRange[1] * 1.8f + 32.0f : kOilWarnRange[1], s.fahrenheit ? "%.0f F" : "%.0f C", ImGuiSliderFlags_AlwaysClamp))
            {
                warn = floorf(warn + 0.5f);
                s.oilWarnC = SettingsRange(s.fahrenheit ? (warn - 32.0f) / 1.8f : warn, kOilWarnRange[0], kOilWarnRange[1]);
                act |= kPageChanged;
            }
            PageTip((degrees(120.0f, to, sizeof to), _snprintf_s(tip, _TRUNCATE, "From this oil temperature the oil lamp blinks and a warning sounds. Default %s.", to), tip));
        }

        ImGui::SeparatorText("Engine damage");
        {
            char at[16], from[16], tip[320];
            PageLabel("Damage", labelX);
            changed(ImGui::Checkbox("##damage", &s.damage));
            PageTip("Overheating and working a cold engine hard add to the engine damage of the game, and the stalls and the clutch to the gearbox damage. A garage repairs it. On by default; leave it off in co-op.");
            ImGui::BeginDisabled(!s.damage);
            // where the hot damage starts, shown in the units in force, kept in C
            float fromShown = s.fahrenheit ? s.hotDamageFrom * 1.8f + 32.0f : s.hotDamageFrom;
            const float fromLo = s.fahrenheit ? kHotDamageFromRange[0] * 1.8f + 32.0f : kHotDamageFromRange[0],
                        fromHi = s.fahrenheit ? kHotDamageFromRange[1] * 1.8f + 32.0f : kHotDamageFromRange[1];
            PageLabel("Damage start", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderFloat("##hotdamagefrom", &fromShown, fromLo, fromHi, s.fahrenheit ? "%.0f F" : "%.0f C", ImGuiSliderFlags_AlwaysClamp))
            {
                s.hotDamageFrom = SettingsRange(floorf((s.fahrenheit ? (fromShown - 32.0f) / 1.8f : fromShown) + 0.5f), kHotDamageFromRange[0], kHotDamageFromRange[1]);
                act |= kPageChanged;
            }
            degrees(kTempHotDamage, at, sizeof at);
            _snprintf_s(tip, _TRUNCATE, "Where the hot damage starts. Default %s, the red zone.", (degrees(118.0f, from, sizeof from), from));
            PageTip(tip);
            PageLabel("Hot damage", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderFloat("##hotdamage", &s.hotDamage, 0.0f, 100.0f, "%.0f percent a minute", ImGuiSliderFlags_AlwaysClamp))
            {
                s.hotDamage = floorf(s.hotDamage + 0.5f);
                act |= kPageChanged;
            }
            degrees(kTempHotDamage, at, sizeof at);
            degrees(s.hotDamageFrom, from, sizeof from);
            _snprintf_s(tip, _TRUNCATE, "The share of the engine lost each minute at %s, none under %s (Damage start). 0 is none. Default 10 %%.", at, from);
            PageTip(tip);
            PageLabel("Cold damage", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderFloat("##colddamage", &s.coldDamage, 0.0f, 60.0f, "%.0f percent a minute", ImGuiSliderFlags_AlwaysClamp))
            {
                s.coldDamage = floorf(s.coldDamage + 0.5f);
                act |= kPageChanged;
            }
            degrees(0.0f, at, sizeof at);
            degrees(kTempCold, from, sizeof from);
            _snprintf_s(tip, _TRUNCATE, "The share lost each minute floored at the red zone at %s; less when warmer or working lighter, none from %s. 0 is none. Default 5 %%.", at, from);
            PageTip(tip);
            PageLabel("Over-rev damage", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderFloat("##overrevdamage", &s.overRevDamage, 0.0f, 60.0f, "%.0f percent a minute", ImGuiSliderFlags_AlwaysClamp))
            {
                s.overRevDamage = floorf(s.overRevDamage + 0.5f);
                act |= kPageChanged;
            }
            PageTip("Damage a minute while a held gear is driven far over the red zone, as after a downshift at speed or a descent in a low gear. 0 is off. Needs Damage. "
                    "With Engine braking at 0 the engine does not hold the truck and a descent in a held gear over-revs. Default 10 %.");
            ImGui::EndDisabled();
            if (view.engineCapacity > 0) ImGui::TextDisabled("The engine of this truck has %d of %d left.", view.engineCapacity - view.engineDamage, view.engineCapacity);
        }
        // the truck's settings as one file (truck_profile.h): the mod's pass does the file work, the tab asks (st.profAsk
        // with kPageProfile) and shows the list the pass made (st.prof). A profile's texts are a stranger's: each is
        // shown as it stands, never as a format
        {
            const ProfileList &pl = st.prof;
            auto ask = [&](int what) {
                st.profAsk = ProfileAsk{};
                st.profAsk.what = what;
                st.profWord = "";
                act |= kPageProfile;
            };
            auto wrapped = [&](const char *text, bool dim) {
                if (dim) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(text);
                ImGui::PopTextWrapPos();
                if (dim) ImGui::PopStyleColor();
            };
            ImGui::SeparatorText("Profiles");
            wrapped("The settings of this truck as one file in Gauges\\profiles: to keep, to take back later or to hand to a friend. A file from a friend goes into that folder.", false);
            ImGui::BeginDisabled(!view.truck[0]);
            PageLabel("Name", labelX);
            ImGui::SetNextItemWidth(w);
            ImGui::InputText("##profname", st.profName, kProfileNameMost + 1);
            PageTip("The name of the profile. The file is named by the truck and this name.");
            PageLabel("Notes", labelX);
            ImGui::SetNextItemWidth(w);
            ImGui::InputText("##profnotes", st.profNotes, kProfileNotesMost + 1);
            PageTip("A line for whoever takes the profile: what it is tuned for.");
            if (ImGui::SmallButton("Export###profexport"))
            {
                const bool there = ProfileNameThere(pl, st.profName);
                if (there && strcmp(st.profOver, st.profName))
                {
                    strncpy_s(st.profOver, st.profName, _TRUNCATE);
                    st.profWord = "A profile of that name is there. Press Export again to write over it.";
                }
                else
                {
                    ask(kProfAskExport);
                    strncpy_s(st.profAsk.name, st.profName, _TRUNCATE);
                    strncpy_s(st.profAsk.notes, st.profNotes, _TRUNCATE);
                    st.profAsk.over = there;
                    st.profOver[0] = 0;
                }
            }
            PageTip("Writes the settings in force for this truck into a profile: the class and scales, the diff types, the suspension, Air brakes and the engine settings. No count of the odometer and nothing the mod learned goes in.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Look again###proflook")) ask(kProfAskLook);
            PageTip("Reads the folder again, after a file was put in it.");
            ImGui::SameLine();
            ImGui::Checkbox("Also the default of the engine###profengine", &st.profEngineToo);
            PageTip("An Import also writes the engine profile as the default of this engine, for every truck with it. Off, only this truck or this kind gets it.");
            ImGui::EndDisabled();
            if (st.profWord[0]) wrapped(st.profWord, false);
            else if (pl.say[0]) wrapped(pl.say, false);
            if (pl.truck && !pl.idRead) wrapped("The id of this truck is not read, so it has no settings of its own: only the second button works.", true);
            else
                wrapped("Import takes what this truck keeps as its own: the class and scales, the diff types, the suspension, Air brakes and the engine settings. The drag area, the air axles and the Counts choice belong to the kind and are left. "
                        "The second button takes all of it for every truck of this kind. A backup is written first, and it is in this list.", true);
            int others = 0;
            auto row = [&](int i, bool other) {
                const ProfileRow &r = pl.row[i];
                char text[420];
                ImGui::PushID(i);
                _snprintf_s(text, _TRUNCATE, "%s%s   %s", r.name, r.backup ? " (backup)" : "", !r.valid ? "not a profile" : r.backup ? "" : r.date);
                ImGui::TextUnformatted(text);
                if (r.valid)
                {
                    if (other)
                    {
                        _snprintf_s(text, _TRUNCATE, "For %s.", r.truck);
                        wrapped(text, true);
                    }
                    if (r.notes[0]) wrapped(r.notes, true);
                    if (!r.sameEngine) wrapped("Made with another engine: its engine settings are left.", true);
                    if (!r.sameGearbox) wrapped("Made with another gearbox: its gear hold is left.", true);
                    if (r.otherVersion)
                    {
                        _snprintf_s(text, _TRUNCATE, "Made with version %s.", r.version);
                        wrapped(text, true);
                    }
                    const bool armed = other && !strcmp(st.profOther, r.file) && st.profOtherBackup == r.backup;
                    auto press = [&](int what) {
                        if (other && !(armed && st.profOtherWhat == what))
                        {
                            strncpy_s(st.profOther, r.file, _TRUNCATE);
                            st.profOtherBackup = r.backup, st.profOtherWhat = what;
                            return;
                        }
                        ask(what);
                        strncpy_s(st.profAsk.file, r.file, _TRUNCATE);
                        st.profAsk.backup = r.backup, st.profAsk.engineToo = st.profEngineToo, st.profAsk.otherKind = other;
                        st.profOther[0] = 0;
                    };
                    ImGui::BeginDisabled(!pl.idRead);
                    if (ImGui::SmallButton("Import")) press(kProfAskImportTruck);
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("For every truck of this kind")) press(kProfAskImportKind);
                    if (armed)
                    {
                        _snprintf_s(text, _TRUNCATE, "This profile is for %s. This truck is %s. Press again to take it.", r.truck, pl.kind);
                        wrapped(text, false);
                    }
                }
                ImGui::Spacing();
                ImGui::PopID();
            };
            for (int i = 0; i < pl.count && i < kProfileRowsMost; i++)
                if (pl.row[i].sameKind || !pl.row[i].valid) row(i, false);
                else others++;
            if (pl.turn && pl.count == others) ImGui::TextDisabled("No profile of this truck yet.");
            if (others && ImGui::TreeNode("Other trucks###profother"))
            {
                for (int i = 0; i < pl.count && i < kProfileRowsMost; i++)
                    if (pl.row[i].valid && !pl.row[i].sameKind) row(i, true);
                ImGui::TreePop();
            }
            if (pl.more > 0) ImGui::TextDisabled("And %d more files.", pl.more);
        }
    }
    else if (open && tab == kTabGearbox)
    {
        ImGui::SeparatorText("Gearbox");
        if (view.smtInstalled)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.68f, 0.15f, 1.0f));
            ImGui::TextWrapped("The manual transmission mod (SMT) is installed. It is not compatible with this version and no longer needed: remove "
                               "SMT.addon64 (or SMT.asi) and SMT.ini from Sources\\Bin and start the game again. The mod gearbox stands down until then.");
            ImGui::PopStyleColor();
        }
        else if (view.gearboxHeld)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.68f, 0.15f, 1.0f));
            ImGui::TextWrapped("Another mod holds the gearbox functions of the game, so the mod gearbox stands down.");
            ImGui::PopStyleColor();
        }
        // the three levels of Gearbox
        changed(ImGui::RadioButton("Game gearbox", &s.gearbox, kGearboxGame));
        PageTip("The gearbox and automatic of the game, as without the mod. Default.");
        changed(ImGui::RadioButton("Mod automatic", &s.gearbox, kGearboxModel));
        PageTip("The mod automatic shifts while the game shifter is in A, shown as D: a torque converter, later shifts the further the pedal is down and a kickdown floored. A gear picked on the game shifter takes over until A again. The reverse the game puts in on the brake while standing is refused.");
        changed(ImGui::RadioButton("Mod gearbox", &s.gearbox, kGearboxOwn));
        PageTip("The mod takes the shifter: D, the gears, N, R, H and the low range on the bindings of the Controls tab, with a clutch and the options below. Driven with a pad or the keyboard the game shifter picks too.");
        // the automatic on the mod drivetrain, part A, on its own tab (BRIEF_release_answers_1115.md, 203): at every level,
        // drawn as off where the settings do not let it act (CutInAMet, the same test as the drive's)
        {
            const bool met = CutInAMet(s);
            ImGui::BeginDisabled(!met);
            PageLabel("Mod engine in A", labelX);
            changed(ImGui::Checkbox("##cutina", &s.cutInA));
            PageTip("In A the truck pulls with the mod engine alone, through the converter, as a held gear already does. The Diff rows then act in A too. On by default.");
            ImGui::EndDisabled();
            if (!met) ImGui::TextDisabled("Not in force now. It acts with the mod automatic or the mod gearbox, Real engine, Mod engine speed and an engine share above 0.");
        }
        PageLabel("Game gear display", labelX);
        {
            static const char *const displays[3] = { "As in the game", "Off at Mod gearbox", "Off always" };
            ImGui::SetNextItemWidth(w);
            changed(ImGui::Combo("##gamegeardisplay", &s.gameGearDisplay, displays, 3));
        }
        PageTip("The gear display of the game at the bottom right. Off at Mod gearbox: it lights nothing while the mod gearbox drives, since the cluster shows the gear. "
                "The dot of the shifter still shows while LB is held, so the stick can pick a gear. Default.");
        if (s.gearbox == kGearboxOwn && (s.input == kInputWheel || s.ignoreShifter) && !view.canReverse)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.68f, 0.15f, 1.0f));
            ImGui::TextWrapped(s.ignoreShifter ? "The game shifter is ignored: bind Gear down or Reverse on the Controls tab before the mod gearbox takes the shifter. Until then it runs as the mod automatic."
                                               : "Driven with wheel pedals: bind Gear down or Reverse on the Controls tab before the mod gearbox takes the shifter. Until then it runs as the mod automatic.");
            ImGui::PopStyleColor();
        }
        if (s.gearbox == kGearboxOwn)
        {
            PageLabel("Require clutch", labelX);
            changed(ImGui::Checkbox("##requireclutch", &s.requireClutch));
            PageTip("A shift into a gear without the clutch: standing it stalls the engine once, rolling it grinds out to N. Off by default.");
            PageLabel("H shifter", labelX);
            changed(ImGui::Checkbox("##requiregearheld", &s.requireGearHeld));
            PageTip("N the moment no gear key is held, as an H shifter with every gear bound. Off by default.");
            PageLabel("Skip neutral", labelX);
            changed(ImGui::Checkbox("##skipneutral", &s.skipNeutral));
            PageTip("Up and down step between R and the lowest forward place past N. Off by default.");
            PageLabel("Down stays forward", labelX);
            changed(ImGui::Checkbox("##downstaysforward", &s.downStaysForward));
            PageTip("Gear down stops at the lowest forward place: the lowest of the low range the gearbox has, or 1 with Skip low range. N and R only on their own keys or the game shifter. Wins over Skip neutral. Off by default.");
            PageLabel("Skip low range", labelX);
            changed(ImGui::Checkbox("##walkskipslow", &s.walkSkipsLow));
            PageTip("Gear up and Gear down step R, N, 1, 2 and on. L-, L and L+ come only from their own bindings, Range low or the game shifter. Off by default.");
            PageLabel("Positions by gearbox", labelX);
            changed(ImGui::Checkbox("##positionsbygearbox", &s.positionsByGearbox));
            PageTip("L-, L, L+ and H are there only when the fitted gearbox has them in the game. Off: every gearbox has all four. On by default.");
            if (ImGui::Button("Sequential", ImVec2(w, 0)))
            {
                // a pad's sequential box: no clutch, no gear held, back to A by itself
                s.requireClutch = s.requireGearHeld = false;
                s.backToA = kBackToASequential;
                act |= kPageChanged;
            }
            PageTip("Sets Require clutch and H shifter off and Back to D to 10 s: shift by hand, and it goes back to D by itself.");
            PageLabel("Back to D", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderInt("##backtoa", &s.backToA, 0, 30, s.backToA > 0 ? "%d s" : "never", ImGuiSliderFlags_AlwaysClamp))
            {
                s.backToA = BackToARange(s.backToA);
                act |= kPageChanged;
            }
            PageTip("Seconds after a shift by hand until the gearbox goes back to D, once the wheels grip and the foot is light. Not with H shifter, for a gear picked on the game shifter or a 1st from the low range, or from R, N, the low range or H. Default never.");
            PageLabel("Ignore shifter", labelX);
            changed(ImGui::Checkbox("##ignoreshifter", &s.ignoreShifter));
            PageTip("A position picked on the game shifter does nothing; only the bindings shift. Off by default.");
            PageLabel("Stall damage", labelX);
            ImGui::SetNextItemWidth(w);
            ImGui::BeginDisabled(!s.damage);
            if (ImGui::SliderFloat("##stalldamage", &s.stallDamage, kStallDamageRange[0], kStallDamageRange[1], "%.0f %%", ImGuiSliderFlags_AlwaysClamp))
            {
                s.stallDamage = floorf(s.stallDamage + 0.5f);
                act |= kPageChanged;
            }
            ImGui::EndDisabled();
            PageTip("What a shift without the clutch costs the gearbox. Needs Damage. Default 5 %.");
            ImGui::BeginDisabled(!s.damage);
            PageLabel("Clutch burns", labelX);
            changed(ImGui::Checkbox("##clutchburn", &s.clutchBurn));
            ImGui::EndDisabled();
            PageTip("A clutch slipping under load heats up and costs the gearbox. Off, it slips at no cost. Needs Damage. On by default.");
            PageLabel("Clutch heat", labelX);
            ImGui::SetNextItemWidth(w);
            ImGui::BeginDisabled(!s.damage || !s.clutchBurn);
            if (ImGui::SliderFloat("##clutchheat", &s.clutchHeat, kClutchHeatRange[0], kClutchHeatRange[1], "%.0f %%", ImGuiSliderFlags_AlwaysClamp))
            {
                s.clutchHeat = floorf(s.clutchHeat + 0.5f);
                act |= kPageChanged;
            }
            ImGui::EndDisabled();
            PageTip("How fast a slipping clutch heats up; 100 is the pace of the model. Lower gives more time to react. Default 60 %.");
            if (view.gearShifts || view.gearStalls) ImGui::TextDisabled("Since the truck was taken: %d shifts, %d stalls.", view.gearShifts, view.gearStalls);
        }
        // the creep: with a truck driven the two rows set the gearbox of this truck on the levels of the Engine tab
        // (167), the two as one group; with none they set the figure every truck without its own goes by
        {
            bool creep = view.creepBy ? view.engCreep : s.creep, creepManual = view.creepBy ? view.engCreepManual : s.creepManual;
            bool moved = false;
            char creepTip[400], whose[200] = "";
            if (view.creepBy) _snprintf_s(whose, _TRUNCATE, " With a truck driven it is kept for the gearbox of this truck (%s) by Own settings on the Engine tab.", view.gearbox);
            PageLabel("Creep", labelX);
            moved |= ImGui::Checkbox("##creep", &creep);
            _snprintf_s(creepTip, _TRUNCATE, "An idling truck in D or the game automatic creeps to walking pace, as a real automatic does. Hold the brake to stand. On by default.%s", whose);
            PageTip(creepTip);
            if (creep)
            {
                PageLabel("Creep in held gear", labelX);
                moved |= ImGui::Checkbox("##creepmanual", &creepManual);
                _snprintf_s(creepTip, _TRUNCATE, "A gear picked by hand creeps too. Stuck in the mod gearbox the engine stalls; in Sequential it goes back to D first. Off by default.%s", whose);
                PageTip(creepTip);
            }
            if (moved && view.creepBy) st.engCreep = creep, st.engCreepManual = creepManual, st.engRows |= 2;
            else if (moved) s.creep = creep, s.creepManual = creepManual, act |= kPageChanged;
            if (view.engCreepFrom) ImGui::TextDisabled("Creep in force: %s%s", PageEngLevel(view.engCreepFrom, "gearbox"), view.engCreepFrom == 2 ? view.truck : "");
            if (view.creepNoHook) ImGui::TextDisabled("Creep cannot act for this truck yet. Real engine on the Power tab lets it.");
        }
        // the driven truck's gearbox: how long each gear is held going up, kept for that gearbox; it works with Real engine
        // on, a switch of the Power tab, so here it is greyed without it (the Reviewer's N5)
        {
            if (view.gearbox[0] && s.gearbox >= kGearboxModel)
            {
                ImGui::BeginDisabled(!s.powertrain);
                float hold = view.gearHold;
                PageLabel("Upshift delay", labelX);
                ImGui::SetNextItemWidth(w);
                if (ImGui::SliderFloat("##gearhold", &hold, kGearHoldRange[0], kGearHoldRange[1], "%.1f s", ImGuiSliderFlags_AlwaysClamp))
                {
                    st.gearHold = floorf(hold * 10.0f + 0.5f) / 10.0f;
                    act |= kPageGearHold;
                }
                char gearTip[400], whose[160] = "";
                if (!view.gearHoldOwn)
                    _snprintf_s(whose, _TRUNCATE, " Until it is moved: by the %s class%s, a scout 1.5 s and the others 2.5 s.", PageClassWord(view.autoClass),
                          view.autoByType ? " (by the type of the truck in the game files)" : view.autoByWeight ? " (by the weight of the truck)" : "");
                _snprintf_s(gearTip, _TRUNCATE, "Going up, the mod automatic waits in each gear at least this long. Kept for this gearbox (%s) by Own settings on the Engine tab.%s Needs Real engine (Power tab).", view.gearbox, whose);
                ImGui::EndDisabled();
                if (view.engHoldFrom) ImGui::TextDisabled("In force: %s%s", PageEngLevel(view.engHoldFrom, "gearbox"), view.engHoldFrom == 2 ? view.truck : "");
                PageTip(gearTip);
            }
        }
    }
    else if (open && tab == kTabPower)
    {
        PageLabel("Real engine", labelX);
        changed(ImGui::Checkbox("##powertrain", &s.powertrain));
        PageTip("The truck pulls with the mod engine: its torque curve at the engine speed shown, and the mod automatic shifts by it. In a gear held by hand the truck pulls with the mod engine alone: no cut at the top, engine braking with the foot off. The engine holds the truck at the top speed of the gear. In L+ no driven wheel runs more than half again past it. L-, L and R get the room of L+ (Low range slip). The pedal is the fuel, as the game has it. Off by default.");
        if (s.powertrain)
        {
            PageLabel("Engine share", labelX);
            ImGui::SetNextItemWidth(w);
            changed(ImGui::SliderInt("##engineshare", &s.engineShare, 0, 100, "%d %%", ImGuiSliderFlags_AlwaysClamp));
            PageTip("How much of the mod engine the game gets: 0 is the pull of the game alone and 100 all of the mod engine. Engine torque on the Engine tab sets the torque of the engine itself. Default 50 %.");
            PageLabel("Air drag", labelX);
            changed(ImGui::Checkbox("##roadload", &s.roadLoad));
            PageTip("The game already slows a rolling truck by itself, more the faster it goes. This adds the share of the air on top, taken off the engine pull in each gear once it is measured floored on flat ground. The drag of the wheels is left out of that measure, so Rolling drag and a flat tire do not make it stronger. Acts with Real engine on. On by default.");
            // with a truck driven the row sets the engine of this truck on the levels of the Engine tab (167)
            {
                float brake = view.engBrakeBy ? view.engBrake : s.engineBrake;
                PageLabel("Engine braking", labelX);
                ImGui::SetNextItemWidth(w);
                if (ImGui::SliderFloat("##enginebrake", &brake, 0.0f, 30.0f, "%.0f %%", ImGuiSliderFlags_AlwaysClamp))
                {
                    if (view.engBrakeBy) st.engBrake = floorf(brake + 0.5f), st.engRows |= 1;
                    else s.engineBrake = brake, act |= kPageChanged;
                }
                char brakeTip[480], whose[200] = "";
                if (view.engBrakeBy) _snprintf_s(whose, _TRUNCATE, " With a truck driven it is kept for the engine of this truck (%s) by Own settings on the Engine tab.", view.engine);
                _snprintf_s(brakeTip, _TRUNCATE, "How hard the engine holds the truck back with the foot off in a gear held by hand, as a share of the gear torque at the redline. 0 lets it roll. Needs the mod gearbox. Default 15 %%.%s", whose);
                if (view.engBrakeFrom) ImGui::TextDisabled("In force: %s%s", PageEngLevel(view.engBrakeFrom, "engine"), view.engBrakeFrom == 2 ? view.truck : "");
                PageTip(brakeTip);
            }
            PageLabel("Diff lock bite", labelX);
            ImGui::SetNextItemWidth(w);
            changed(ImGui::SliderInt("##difflockbite", &s.diffLockBite, 0, 100, s.diffLockBite > 0 ? "%d %%" : "game lock", ImGuiSliderFlags_AlwaysClamp));
            PageTip("How firmly the diff lock ties the driven wheels together. 0 leaves the game lock as it is. 100 turns them as one shaft. A locked truck then scrubs in a turn on hard ground, as a real one does. It acts on a truck whose lock is always on too.");
            PageLabel("Low range slip", labelX);
            ImGui::SetNextItemWidth(w);
            changed(ImGui::SliderInt("##lowrangeslip", &s.lowRangeSlip, 0, 100, "%d %%", ImGuiSliderFlags_AlwaysClamp));
            PageTip("How much wheel slip L-, L and R may use. 100 gives them the same room as L+. 0 holds each wheel near the top speed of its gear.");
            PageLabel("Drive display", labelX);
            changed(ImGui::Checkbox("##drivedisplay", &s.driveDisplay));
            PageTip("Each wheel: its spin against the truck speed and its share of the drive.");
            // drawn as off where the types do not act (BRIEF_tester_1114.md, part 192): by the drive's own test (DiffTypesAct)
            ImGui::BeginDisabled(!view.diffActs);
            PageLabel("Diff", labelX);
            ImGui::SetNextItemWidth(w);
            {
                // 0 Auto, 1 Open, 2 Limited slip, 3 Locked (torque_cut.h); Auto names what it picks while a truck is driven
                static const char *const autos[] = { "Auto", "Auto (Open)", "Auto (Limited slip)", "Auto (Locked)" };
                const char *const diffs[] = { view.diffKnown && view.diffAuto >= 1 && view.diffAuto <= 3 ? autos[view.diffAuto] : autos[0], "Open", "Limited slip", "Locked" };
                // "By axle" where the set types of the rows shown differ (M6). A pick here sets every axle: the keys by axle go
                bool byAxle = false;
                for (int r = 1; r < view.diffRows; r++) byAxle = byAxle || AxleSetOf(s.diffAxle, view.diffRowAxle[r], s.diffType) != AxleSetOf(s.diffAxle, view.diffRowAxle[0], s.diffType);
                if (ImGui::BeginCombo("##difftype", byAxle ? "By axle" : diffs[s.diffType]))
                {
                    for (int d = 0; d < 4; d++)
                        if (ImGui::Selectable(diffs[d], !byAxle && s.diffType == d))
                        {
                            s.diffType = d;
                            for (int a = 0; a < kAxleRowsMost; a++) s.diffAxle[a] = -1;
                            changed(true);
                        }
                    ImGui::EndCombo();
                }
                g_pageDiffNames[0] = diffs[0];
            }
            PageTip("The differential of the driven truck, kept for this truck alone. A truck that never had it set goes by its kind. Auto picks Locked where the lock of the truck is always on, else Open. Open lets a free wheel spin. That wheel takes the pull from the other wheel of its axle. The other axles take over what it cannot use. Limited slip holds a spinning wheel, only in L-, L, L+ and R. Locked ties the wheels, and holds a free wheel only with Diff lock bite above 0. While the diff lock is on the truck is locked whatever the type, so on a truck whose lock is always on Open and Limited slip may change nothing. The type acts in the gear positions of the mod gearbox, not in A.");
            // a row for each axle of the driven truck that can be driven, front to back (BRIEF_axle_diff.md, M8)
            {
                bool lockedOne = false, differ = false;
                int firstForce = -1;
                for (int r = 0; r < view.diffRows && r < kAxleRowsMost; r++)
                {
                    const int a = view.diffRowAxle[r];
                    char label[16], id[24];
                    _snprintf_s(label, _TRUNCATE, "Axle %d", a + 1);
                    _snprintf_s(id, _TRUNCATE, "##diffaxle%d", a + 1);
                    PageLabel(label, labelX);
                    ImGui::SetNextItemWidth(w);
                    int set = AxleSetOf(s.diffAxle, a, s.diffType);
                    if (ImGui::Combo(id, &set, g_pageDiffNames, 4))
                    {
                        s.diffAxle[a] = set;
                        changed(true);
                    }
                    PageTip("The differential of this axle. The Diff row above sets all axles at once. While the diff lock of the truck is on, every axle is locked.");
                    if (view.diffRowKey[r] || !view.diffRowNow[r])
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", view.diffRowKey[r] ? "Locked by the key" : "not driven now");
                    }
                    const int force = view.diffRowKey[r] ? kDiffLocked : DiffTypeOf(set, view.diffKnown, view.diffKind);
                    lockedOne = lockedOne || force == kDiffLocked;
                    if (firstForce < 0) firstForce = force;
                    else differ = differ || force != firstForce;
                }
                if (view.diffRows > 0 && view.diffKind == 2) ImGui::TextDisabled("The lock of this truck is always on. It is locked whatever the rows say.");
                if (lockedOne && differ && (s.diffLockBite <= 0 || !view.diffBiteActs)) ImGui::TextDisabled("Locked on one axle needs Diff lock bite above 0.");
                if (view.diffNoPairs) ImGui::TextDisabled("The axles of this truck are not read as pairs.");
            }
            ImGui::EndDisabled();
            if (!view.diffActs) ImGui::TextDisabled("Not in force now. The types act with the mod gearbox (Gearbox=2) in a held gear.");
            // a locked differential limits the steering (BRIEF_lock_steering.md, M7): the share of the steering that is left
            PageLabel("Steering with front lock", labelX);
            ImGui::SetNextItemWidth(w);
            changed(ImGui::SliderInt("##locksteerfront", &s.lockSteerFront, 30, 100, "%d %%", ImGuiSliderFlags_AlwaysClamp));
            PageTip("How much steering is left while an axle that steers is locked. 100 leaves it all. A truck whose lock is always on has this all the time. Default 60 %.");
            PageLabel("Steering with rear lock", labelX);
            ImGui::SetNextItemWidth(w);
            changed(ImGui::SliderInt("##locksteerrear", &s.lockSteerRear, 30, 100, "%d %%", ImGuiSliderFlags_AlwaysClamp));
            PageTip("How much steering is left while another driven axle is locked. 100 leaves it all. With both locked the smaller figure counts. A truck whose lock is always on has this all the time. Default 85 %.");
            if (view.steerNoRoute) ImGui::TextDisabled("The steering is not found in this game version. Nothing is changed.");
            else if (view.steerPercent) ImGui::TextDisabled("Steering now %d %%", view.steerPercent);
            PageLabel("Mod engine speed", labelX);
            changed(ImGui::Checkbox("##modenginespeed", &s.modEngineSpeed));
            PageTip("The game tachometer, cockpit needles, rev lights, engine sound, effects and exhaust follow the mod engine while Real engine is on. Fuel use and the stall keep the game rules, and in a gear held by hand they follow the pull of the mod engine. Off: the game computes its own, and the mod engine never pulls alone in a gear held by hand, as that needs this on. On by default.");
        }
        PageLabel("Pull test", labelX);
        const bool pullOn = view.pullTest == kViewPullArmed || view.pullTest == kViewPullRunning;
        if (ImGui::Button(pullOn ? "Cancel###pulltest" : "Start###pulltest", ImVec2(w, 0))) act |= pullOn ? kPagePullCancel : kPagePullTest;
        PageTip("Measures how the torque factor of the game changes the pull. Press Start, then floor it in 4th or higher on a long flat road for seven seconds; Gauges.log gets the numbers.");
        if (view.pullTest == kViewPullArmed) ImGui::TextDisabled("Armed: floor it in the fourth gear or higher on a long flat road.");
        else if (view.pullTest == kViewPullRunning) ImGui::TextDisabled("Running, step %d of 7: keep it floored.", view.pullPhase + 1);
        else if (view.pullTest == kViewPullDone) ImGui::TextDisabled("Done: the numbers are in Gauges.log.");
        else if (view.pullTest == kViewPullStopped) ImGui::TextDisabled("Stopped: %s. The torque factor is back as it was.", view.pullWhy ? view.pullWhy : "");
    }
    else if (open && tab == kTabSuspension)
    {
        ImGui::SeparatorText("Suspension");
        // whose suspension this is (BRIEF_suspension_per_truck.md step 1): the driven kind's own, or the default
        if (!view.truck[0]) ImGui::TextWrapped("No truck is driven. These are the default settings, which every truck without its own uses.");
        else
        {
            ImGui::TextWrapped("Settings for: %s", view.truck);
            PageLabel("Own settings", labelX);
            bool own = view.suspOwn;
            if (ImGui::Checkbox("##suspown", &own)) act |= own ? kPageSuspOn : kPageSuspOff;
            PageTip("This kind of truck keeps its own suspension in its file in Gauges\\trucks. Ticks itself when a setting here is changed while it is driven. Off: "
                    "the default is in force and its own are kept.");
            if (view.suspOwn)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Make these the default")) act |= kPageSuspDefault;
                PageTip("Copies the suspension of this truck into the default, for every truck without its own. This truck keeps its own.");
                // which level: this owned truck (by the game's id of it), or every truck of its kind (step 2)
                PageLabel("For", labelX);
                ImGui::BeginDisabled(!view.suspIdKnown);
                if (ImGui::RadioButton("This truck only", view.suspLevel == 2) && view.suspLevel != 2) act |= kPageSuspTruck;
                ImGui::EndDisabled();
                PageTip("This truck alone keeps the set, by the id the game gives it. A truck taken out of the warehouse gets a new id. It gets its set back where exactly one "
                        "waiting truck of the kind has the same engine and gearbox. Else it starts on the settings of its kind.");
                ImGui::SameLine();
                char every[128];
                snprintf(every, sizeof every, "Every %s###suspkind", view.truck);
                if (ImGui::RadioButton(every, view.suspLevel == 1) && view.suspLevel != 1) act |= kPageSuspKind;
                PageTip(view.suspKindSet ? "Every truck of this kind uses the set of the kind. The set of this truck is kept, not in use: This truck only brings it back."
                                         : "Every truck of this kind keeps this set. The kind has no set yet, so the set of this truck goes up to it.");
            }
            if (view.idWords[0]) ImGui::TextDisabled("%s", view.idWords);
            else if (view.suspKept) ImGui::TextDisabled("Kept, not in use. Changes here edit the default.");
            if (view.suspOwn && view.suspLevel != 2 && view.suspTruckKept) ImGui::TextDisabled("The set of this truck is kept, not in use: This truck only brings it back.");
            ImGui::TextDisabled("In force: %s%s", view.suspLevel == 2 ? "this truck" : view.suspLevel == 1 ? "every " : "the default", view.suspLevel == 1 ? view.truck : "");
        }
        // under each slider the driven truck's file for that slider's end and what is in force now (the addendum of
        // 2 November, 13:18; its own end only, BRIEF_suspension_front_rear.md N1): a spring or a damping the game holds at 1
        // is said so
        auto fileLine = [&](int k, int end, float factor) {
            if (!view.suspKnown)
            {
                ImGui::TextDisabled("No truck driven: the figures of the file show here.");
                return;
            }
            const float f = view.suspFile[k][end];
            ImGui::TextDisabled("File %.3g, now %.3g%s", f, fminf(1.0f, f * factor), f * factor > 1.0f ? ", held at the limit of 1 the game sets" : "");
        };
        // Spring and Damping, front and rear apart (BRIEF_suspension_front_rear.md): the ends as Ride front and Ride rear
        auto scaleRow = [&](const char *label, const char *id, float &value, int k, int end, const char *tip) {
            PageLabel(label, labelX);
            ImGui::SetNextItemWidth(w);
            changed(ImGui::SliderFloat(id, &value, kSuspScaleRange[0], kSuspScaleRange[1], "x%.2f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic));
            PageTip(tip);
            fileLine(k, end, value);
        };
        scaleRow("Spring front", "##springfront", s.springFront, 0, 0, "A test: the spring of each front wheel as a factor of its own. Above 1 stiffer and higher. Default 1.");
        scaleRow("Spring rear", "##springrear", s.springRear, 0, 1,
                 "A test: the spring of each rear wheel as a factor of its own. Rear is every axle behind the first. Above 1 stiffer and higher. Default 1.");
        scaleRow("Damping front", "##dampingfront", s.dampingFront, 1, 0, "A test: the damping of each front wheel as a factor of its own. Above 1 the body settles faster. Default 1.");
        scaleRow("Damping rear", "##dampingrear", s.dampingRear, 1, 1,
                 "A test: the damping of each rear wheel as a factor of its own. Rear is every axle behind the first. Above 1 the body settles faster. Default 1.");
        // the frame's twist (BRIEF_frame_flex.md part 2): its spring and its limits, from the driven truck's own joint
        auto frameLine = [&](bool limits, float factor) {
            if (!view.frameKnown && !view.frameRigid) ImGui::TextDisabled("No truck driven: the figures of the file show here.");
            else if (view.frameRigid) ImGui::TextDisabled("Takes effect only on trucks with a flexing frame.");
            else if (!limits) ImGui::TextDisabled("File %.0f, now %.0f", view.frameFile[0], view.frameFile[0] * factor);
            else
            {
                const float most = fmaxf(view.frameMost, fmaxf(-view.frameFile[2], view.frameFile[3]));
                const float lo = fmaxf(-most, view.frameFile[2] * factor), hi = fminf(most, view.frameFile[3] * factor);
                ImGui::TextDisabled("File %.1f to %.1f degrees, now %.1f to %.1f%s", view.frameFile[2], view.frameFile[3], lo, hi,
                                    lo != view.frameFile[2] * factor || hi != view.frameFile[3] * factor ? ", held at the widest" : "");
            }
        };
        PageLabel("Frame flex", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##frameflex", &s.frameFlex, kFrameFlexRange[0], kFrameFlexRange[1], "x%.2f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic));
        PageTip("How stiff the frame is against twisting, as a factor of its own. Below 1 it twists more. Default 1.");
        frameLine(false, s.frameFlex);
        PageLabel("Twist range", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##twistrange", &s.twistRange, kTwistRange[0], kTwistRange[1], "x%.2f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic));
        PageTip("How far the frame can twist, as a factor of its own. Default 1.");
        frameLine(true, s.twistRange);
        PageLabel("Extra travel", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##extratravel", &s.extraTravel, kExtraTravelRange[0], kExtraTravelRange[1], s.extraTravel > 0.0f ? "%.0f cm" : "off", ImGuiSliderFlags_AlwaysClamp));
        PageTip("A test: how much further each wheel can rise into the body past the end in its file. Off by default.");
        if (!view.suspKnown) ImGui::TextDisabled("No truck driven: the figures of the file show here.");
        else
        {
            const float f = view.suspFile[2][0], r = view.suspFile[2][1], extra = s.extraTravel / 100.0f;
            ImGui::TextDisabled("File %.2f / %.2f m (front / rear), now %.2f / %.2f m", f, r, f - extra, r - extra);
        }
        if (s.extraTravel > 0.0f) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Past the end in the file a wheel can go into the body.");
        // the air's ends (BRIEF_air_axle_ticks.md, 8jk3's layout): a tick "Air" beside Ride front and beside Ride rear, the
        // mask in force shown (the truck's own or the kind's ticks, else the start order's), and above them the air's
        // height for every ticked end, the keys' own figure
        const int airMask = s.airAxleMask >= 0 ? s.airAxleMask : view.airKnown ? view.airAxles : 0;
        const bool airTicks = view.airKnown && s.airSuspension;
        PageLabel("Sync all enabled", labelX);
        changed(ImGui::Checkbox("##airsync", &s.airSync));
        PageTip("Shows one slider for the air height: every end ticked Air moves together, as Ride up and Ride down move it. The keys work without it. Off by default.");
        if (s.airSync)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(w - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
            int height = view.airHeight;
            ImGui::BeginDisabled(!airTicks || !airMask);
            if (ImGui::SliderInt("##airheight", &height, -kRideKeyMost, kRideKeyMost, height ? "%+d cm" : "0 cm", ImGuiSliderFlags_AlwaysClamp) && height != view.airHeight)
            {
                st.airHeight = height;
                act |= kPageAirHeight;
            }
            ImGui::EndDisabled();
            PageTip("The air height on every end ticked Air, -10 to +10 cm, under 20 km/h. Back at 0 when another truck is taken.");
        }
        // a tick beside a slider: the end's air, edited as the whole mask
        auto airTick = [&](const char *id, int bit, const char *tip) {
            ImGui::SameLine();
            bool on = (airMask & bit) != 0;
            ImGui::BeginDisabled(!view.airKnown);
            if (ImGui::Checkbox(id, &on))
            {
                s.airAxleMask = on ? airMask | bit : airMask & ~bit;
                act |= kPageChanged;
            }
            ImGui::EndDisabled();
            PageTip(tip);
        };
        // the attach point's features (a test, build B): the ride height and the sway bars move each wheel's spring zero
        PageLabel("Ride front", labelX);
        ImGui::SetNextItemWidth(w);
        // on the slider's step before it is shown and written, so near 0 is exactly 0 and the text keeps one form
        changed(ImGui::SliderFloat("##ridefront", &s.rideFront, kRideRange[0], kRideRange[1], RideFormat(s.rideFront), ImGuiSliderFlags_AlwaysClamp));
        s.rideFront = SettingsStep(s.rideFront, kRideStep);
        PageTip("A test: the ride height of the front axle in cm, above 0 higher. It stops on a truck with an active ride height. Default 0.");
        airTick("Air##airfront", kAirMaskFront, "Tick: the front axle gets air. The airbags of the game are only a look, so the mod asks. Only ticked ends level and move with the air height.");
        PageLabel("Ride rear", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##riderear", &s.rideRear, kRideRange[0], kRideRange[1], RideFormat(s.rideRear), ImGuiSliderFlags_AlwaysClamp));
        s.rideRear = SettingsStep(s.rideRear, kRideStep);
        PageTip("A test: the ride height of the rear axles in cm, above 0 higher. It stops on a truck with an active ride height. Default 0.");
        airTick("Air##airrear", kAirMaskRear, "Tick: every axle but the front one gets air. The airbags of the game are only a look, so the mod asks. Only ticked ends level and move with the air height.");
        if (view.airKnown)
        {
            const char *from = view.airFrom == kAirFromTruck ? "the ticks of this truck" : view.airFrom == kAirFromKind ? "the ticks of the kind"
                             : view.airFrom == kAirFromTicked ? "ticked, not kept: no suspension set in force" : view.airFrom == kAirFromKey ? "AirAxles of the truck"
                             : view.airFrom == kAirFromTable ? "the table of the mod" : "not in the table of the mod";
            const char *ends = airMask == kAirMaskAll ? "front and rear" : airMask == kAirMaskRear ? "rear, every axle but the front one" : airMask == kAirMaskFront ? "front axle only" : "none ticked";
            ImGui::TextDisabled("Air: %s (%s).", ends, from);
        }
        // always one line, so nothing below it moves while the slider passes 0
        if (view.rideRestKnown) ImGui::TextDisabled("At rest %.0f / %.0f cm into the travel (front / rear)", -view.rideRest[0], -view.rideRest[1]);
        else ImGui::TextDisabled("At rest: not read yet on this truck");
        PageLabel("Sway front", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##swayfront", &s.swayFront, kSwayRange[0], kSwayRange[1], s.swayFront > 0.0f ? "%.2f" : "none", ImGuiSliderFlags_AlwaysClamp));
        s.swayFront = SettingsStep(s.swayFront, kSwayStep);
        PageTip("A test: a front sway bar as a share of the spring rate, to keep the body level in turns. It stops on a truck with an active ride height. Default none.");
        PageLabel("Sway rear", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##swayrear", &s.swayRear, kSwayRange[0], kSwayRange[1], s.swayRear > 0.0f ? "%.2f" : "none", ImGuiSliderFlags_AlwaysClamp));
        s.swayRear = SettingsStep(s.swayRear, kSwayStep);
        PageTip("A test: a rear sway bar; 0.1 to 0.2 works well, and it ends at 0.3. It stops on a truck with an active ride height. Default none.");
        if (view.swayOff) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "The sway bars are disconnected.");
        // the camber (BRIEF_swing_camber.md, camber.h): a gain on the file's multiplier and an offset on the file's render
        // angle, for the front axle and every other, each with the file's figure and the one in force. The look only
        auto camberPair = [&](const char *idF, const char *idR, float *f, float *r, const float range[2], float step, const char *fmtF, const char *fmtR, const char *tip) {
            const float half = (w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            ImGui::SetNextItemWidth(half);
            changed(ImGui::SliderFloat(idF, f, range[0], range[1], fmtF, ImGuiSliderFlags_AlwaysClamp));
            *f = SettingsStep(*f, step);
            PageTip(tip);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(half);
            changed(ImGui::SliderFloat(idR, r, range[0], range[1], fmtR, ImGuiSliderFlags_AlwaysClamp));
            *r = SettingsStep(*r, step);
            PageTip(tip);
        };
        // an end's file figure and the one in force, as text ("-" for an end with no wheel read)
        auto camberNow = [&](int end, bool rest, float v, char *file, char *now, size_t size) {
            if (!view.camberEnd[end])
            {
                snprintf(file, size, "-");
                snprintf(now, size, "-");
                return;
            }
            const float f = view.camberFile[end][rest ? 0 : 1];
            const float n = rest ? fmaxf(-30.0f, fminf(30.0f, f + (f < 0.0f ? -v : v))) : f * v;
            snprintf(file, size, rest ? "%.1f" : "x%.2f", f);
            snprintf(now, size, rest ? "%.1f" : "x%.2f", n);
        };
        auto camberLine = [&](bool rest, float front, float rear) {
            if (!view.camberKnown)
            {
                ImGui::TextDisabled("No truck driven: the figures of the file show here.");
                return;
            }
            char f0[16], n0[16], f1[16], n1[16];
            camberNow(0, rest, front, f0, n0, sizeof f0);
            camberNow(1, rest, rear, f1, n1, sizeof f1);
            ImGui::TextDisabled("File %s / %s%s, now %s / %s (front / rear)", f0, f1, rest ? " degrees" : "", n0, n1);
        };
        PageLabel("Camber gain", labelX);
        camberPair("##cambergainfront", "##cambergainrear", &s.camberGainFront, &s.camberGainRear, kCamberGainRange, kCamberGainStep, "front x%.2f", "rear x%.2f",
                   "How much each wheel tilts as it travels, as a factor of the figure in its file. 0 keeps the tilt at rest. An end at File x0 has nothing to scale. Default x1.");
        camberLine(false, s.camberGainFront, s.camberGainRear);
        PageLabel("Camber at rest", labelX);
        camberPair("##camberrestfront", "##camberrestrear", &s.camberRestFront, &s.camberRestRear, kCamberRestRange, kCamberRestStep, "front %+.1f", "rear %+.1f",
                   "The tilt of the wheel at rest, in degrees added to the figure in its file. Default +0.");
        camberLine(true, s.camberRestFront, s.camberRestRear);
        ImGui::TextDisabled("Swing axles: match the tilt of the wheel to that of the half-axle. The look only.");
        // the air suspension (a test, build C): it levels each wheel on top of the ride height, on the ends ticked Air
        // only (BRIEF_air_axle_ticks.md); a truck with none ticked has it greyed
        const bool noAir = view.airKnown && airMask == 0;
        if (noAir) ImGui::TextDisabled("No end ticked Air: tick Air beside Ride front or Ride rear.");
        if (noAir) ImGui::BeginDisabled();
        PageLabel("Air suspension", labelX);
        changed(ImGui::Checkbox("##airsuspension", &s.airSuspension));
        PageTip("A test: each axle with air held at its rest height, so cargo no longer makes the truck sag or lean. Ride up and Ride down move it 2 cm a press under 20 km/h. Only the ends ticked Air level; the others stay on their springs. Off by default.");
        PageLabel("Level to horizon", labelX);
        changed(ImGui::Checkbox("##airhorizon", &s.airHorizon));
        PageTip("A test with Air suspension: under 10 km/h the body is levelled to the horizon, not the wheels. Off by default.");
        if (noAir) ImGui::EndDisabled();
        if (s.airSuspension && !noAir) // a truck with no air: the greyed text above says why (review N1)
        {
            if (!view.air) ImGui::TextDisabled("Height %+d cm; no truck it runs on", view.airHeight);
            else if (!view.airTaken) ImGui::TextDisabled("Height %+d cm; the rest place is taken at the first standstill", view.airHeight);
            else ImGui::TextDisabled("Height %+d cm; levelling %+.1f / %+.1f cm (front / rear), left %+.1f cm over right", view.airHeight, view.airLevel[0],
                                     view.airLevel[1], view.airSide);
        }
        PageLabel("Travel display", labelX);
        changed(ImGui::Checkbox("##suspdisplay", &s.suspDisplay));
        PageTip("The travel of each wheel as a bar, with a lamp at the end and a mark while it hangs. Off by default.");

        // the damper (a test, BRIEF_suspension_damper.md): its curve, the four factors and the knee, or a preset of them
        PageLabel("Damper", labelX);
        static const char *const curves[kDamperCurves] = { "Linear", "Digressive", "Progressive" };
        ImGui::SetNextItemWidth(w);
        changed(ImGui::Combo("##dampercurve", &s.damperCurve, curves, kDamperCurves));
        PageTip("A test: how the damping grows over the knee. Linear (default): as the truck has it. Digressive: softer bumps. Progressive: against bottoming out.");
        const DamperPreset *presets = kDamperPresets;
        PageLabel("Preset", labelX);
        for (int i = 0; i < kDamperPresetCount; i++)
        {
            if (i) ImGui::SameLine();
            if (ImGui::SmallButton(presets[i].name))
            {
                const DamperPreset &d = presets[i];
                s.lowCompression = d.lowC, s.highCompression = d.highC, s.lowRebound = d.lowR, s.highRebound = d.highR, s.damperKnee = d.knee, s.damperCurve = d.curve;
                changed(true);
            }
        }
        PageTip("Starting points: Stock is as the truck has it. Trail is soft with a firmer rebound. Rock crawl is soft and progressive. Road is firm.");
        PageLabel("Slow bump", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##lowcompression", &s.lowCompression, kDamperRange[0], kDamperRange[1], "x%.2f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic));
        PageTip("A test: the damping as the wheel rises slowly: roll, squat and dive. Default 1.");
        PageLabel("Fast bump", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##highcompression", &s.highCompression, kDamperRange[0], kDamperRange[1], "x%.2f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic));
        PageTip("A test: the damping as the wheel rises fast: bumps, rocks and curbs. Default 1.");
        PageLabel("Slow rebound", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##lowrebound", &s.lowRebound, kDamperRange[0], kDamperRange[1], "x%.2f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic));
        PageTip("A test: the damping as the wheel drops slowly: the body coming back after a roll. Default 1.");
        PageLabel("Fast rebound", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##highrebound", &s.highRebound, kDamperRange[0], kDamperRange[1], "x%.2f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic));
        PageTip("A test: the damping as the wheel drops fast: how quickly it follows the ground. Default 1.");
        PageLabel("Knee", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##damperknee", &s.damperKnee, kKneeRange[0], kKneeRange[1], "%.2f m/s", ImGuiSliderFlags_AlwaysClamp));
        PageTip("A test: the wheel speed where slow turns into fast. Rough ground is 0.1 to 1 m/s. Default 0.1.");

        // the spring's features and the damping by position (a test, BRIEF_suspension_extras.md, build A)
        PageLabel("Spring curve", labelX);
        static const char *const springCurves[kSpringCurves] = { "Linear", "Progressive", "Dual rate" };
        ImGui::SetNextItemWidth(w);
        changed(ImGui::Combo("##springcurve", &s.springCurve, springCurves, kSpringCurves));
        PageTip("A test: how the spring stiffens deep in the travel. Linear (default): as the truck has it. Progressive: stiffer and stiffer. Dual rate: stiffer from the crossover.");
        ImGui::BeginDisabled(s.springCurve == 0);
        PageLabel("Stiffest", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##springrise", &s.springRise, kSpringRiseRange[0], kSpringRiseRange[1], "x%.2f", ImGuiSliderFlags_AlwaysClamp));
        PageTip("A test: the spring rate at the end of the travel as a factor. Default 2.");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(s.springCurve != 2);
        PageLabel("Crossover", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##springcross", &s.springCross, kSpringCrossRange[0], kSpringCrossRange[1], "%.2f", ImGuiSliderFlags_AlwaysClamp));
        PageTip("A test: where dual rate turns stiffer, as a share of the travel. Default 0.5.");
        ImGui::EndDisabled();
        PageLabel("Bump stops", labelX);
        changed(ImGui::Checkbox("##bumpstops", &s.bumpStops));
        PageTip("A test: a cushion over the last part of the travel of each wheel instead of the hard limit. Off by default.");
        ImGui::BeginDisabled(!s.bumpStops);
        PageLabel("Stop length", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##bumpzone", &s.bumpZone, kBumpZoneRange[0], kBumpZoneRange[1], "%.2f m", ImGuiSliderFlags_AlwaysClamp));
        PageTip("A test: how much of the travel the stop takes, at most a third. Default 0.12 m.");
        PageLabel("Stop force", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##bumpforce", &s.bumpForce, kBumpForceRange[0], kBumpForceRange[1], "x%.1f", ImGuiSliderFlags_AlwaysClamp));
        PageTip("A test: how hard the stop pushes back at the end, as a multiple of the spring. Default 3.");
        PageLabel("Stop damping", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##bumpdamping", &s.bumpDamping, kBumpDampingRange[0], kBumpDampingRange[1], "x%.1f", ImGuiSliderFlags_AlwaysClamp));
        PageTip("A test: the damping going into the stop. The rebound stays. Default 2.");
        ImGui::EndDisabled();
        PageLabel("Damping zones", labelX);
        changed(ImGui::Checkbox("##dampingzones", &s.dampingZones));
        PageTip("A test: softer damping mid-travel for small bumps and firmer at the ends, as a bypass shock. Off by default.");
        ImGui::BeginDisabled(!s.dampingZones);
        PageLabel("Middle", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##zonesoft", &s.zoneSoft, kZoneSoftRange[0], kZoneSoftRange[1], "x%.2f", ImGuiSliderFlags_AlwaysClamp));
        PageTip("A test: the damping in the soft middle as a factor. Default 0.7.");
        PageLabel("Ends", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##zonefirm", &s.zoneFirm, kZoneFirmRange[0], kZoneFirmRange[1], "x%.2f", ImGuiSliderFlags_AlwaysClamp));
        PageTip("A test: the damping at the ends as a factor. Default 1.5.");
        PageLabel("Middle start", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##zonefrom", &s.zoneFrom, 0.0f, s.zoneTo - 0.05f, "%.2f", ImGuiSliderFlags_AlwaysClamp));
        PageTip("A test: where the soft middle begins as a share of the travel. Default 0.1.");
        PageLabel("Middle end", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderFloat("##zoneto", &s.zoneTo, s.zoneFrom + 0.05f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp));
        PageTip("A test: where the soft middle ends. Default 0.6.");
        ImGui::EndDisabled();
    }
    else if (open && tab == kTabControls)
    {
        ImGui::SeparatorText("Controls");
        PageLabel("Toggle key", labelX);
        KeyName(s.toggleKey, buf, sizeof buf);
        if (ImGui::Button(st.capture == 0 ? "Press a key (Esc: keep)###key" : (strncat_s(buf, "###key", _TRUNCATE), buf), ImVec2(w, 0)))
            st.capture = st.capture == 0 ? -1 : 0;
        PageTip("Hides the cluster and shows it again. None by default.");
        ImGui::SameLine();
        if (ImGui::SmallButton("None###nokey"))
        {
            s.toggleKey = 0;
            if (st.capture == 0) st.capture = -1;
            act |= kPageChanged;
        }
        if (st.capture == 0)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) st.capture = -1;
            else
                for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++)
                {
                    const int vk = PageVirtualKey(k);
                    if (vk && ImGui::IsKeyPressed((ImGuiKey)k, false)) { s.toggleKey = vk; st.capture = -1; act |= kPageChanged; break; }
                }
        }
        bool beganNow = false;
        PageLabel("Pad toggle", labelX);
        _snprintf_s(buf, _TRUNCATE, "%s###bind", s.padToggle);
        if (ImGui::Button(buf, ImVec2(w, 0)) && st.capture < 1)
        {
            st.capture = 1;
            st.peak = 0;
            st.armed = false;
            st.began = now;
            beganNow = true;
        }
        PageTip("Press it, then hold the button or buttons of the pad and let go. Pick ones the game does not use while driving. None by default.");
        ImGui::SameLine();
        if (ImGui::SmallButton("None###nopad"))
        {
            strncpy_s(s.padToggle, "None", _TRUNCATE);
            act |= kPageChanged;
        }
        if (st.capture >= 1)
        {
            // The buttons are read in a popup of its own, so held buttons never reach the tab's widgets (PageCaptureStep
            // has the rest). B closes a popup, so it is opened again each frame it is found closed.
            uint32_t bound = 0;
            const bool done = PageCaptureStep(st, padNow, beganNow, ImGui::IsKeyPressed(ImGuiKey_Escape, false), now, bound);
            if (bound)
            {
                PadName(bound, s.padToggle, sizeof s.padToggle);
                act |= kPageChanged;
            }
            if (!ImGui::IsPopupOpen("###padbind", 0)) ImGui::OpenPopup("###padbind", 0);
            const ImVec2 display = ImGui::GetIO().DisplaySize;
            ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Pad buttons###padbind", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
            {
                ImGui::TextUnformatted("Hide and show the cluster");
                if (!st.armed) ImGui::TextUnformatted("Let go of the pad.");
                else if (!st.peak) ImGui::TextUnformatted("Hold the button or buttons on the pad, then let go.");
                else
                {
                    PadName(st.peak, buf, sizeof s.padToggle);
                    ImGui::Text("%s", buf);
                }
                if (!st.peak)
                {
                    const long long left = (long long)kPageCaptureMs - (long long)(now - st.began);
                    ImGui::TextDisabled("Keeps the old ones in %lld s, or with Esc.", left > 0 ? (left + 999) / 1000 : 0);
                }
                if (done) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            if (done)
            {
                st.capture = -1;
                st.peak = 0;
            }
        }
        static const char *inputs[kInputCount] = { "Wheel pedals", "Pad", "Keyboard" };
        PageLabel("Driven with", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::Combo("##input", &s.input, inputs, kInputCount));
        PageTip("What the mod automatic reads the pedal from. Wheel pedals (default): a push to the floor kicks down. Pad: the trigger has to come from under three quarters. Keyboard: no kickdown.");

        // the bindings: two slots an action; a click binds the next key, pad chord, button or pedal moved, x clears (the
        // order is input_bindings.h's GearAction). The shifter's at Gearbox=2, the locks' at every level
        // the last name, Checkers, is not a row of this tab: its row is drawn on the Mod work tab alone, once the board
        // was opened. No loop here runs over the whole list
        static const char *actions[kViewActions] = { "Gear 1", "Gear 2", "Gear 3", "Gear 4", "Gear 5", "Gear 6", "Gear 7", "Gear 8", "Gear 9", "Gear 10",
                                                     "Gear 11", "Gear 12", "Gear 13", "Gear 14", "Gear 15", "Gear 16", "H", "L-", "L", "L+", "Neutral", "Reverse", "Gear up", "Gear down", "Clutch",
                                                     "Range high", "Range low", "D", "Diff lock", "AWD", "Sway bars", "Ride up", "Ride down", "Lock front axle", "Lock rear axles", "Winch out", "Checkers" };
        auto bindingRow = [&](int a) {
            ImGui::PushID(1000 + a);
            PageLabel(actions[a], labelX);
            for (int slot = 0; slot < 2; slot++)
            {
                ImGui::PushID(slot);
                if (slot) ImGui::SameLine();
                const bool waiting = view.captureAction == a && view.captureSlot == slot;
                char text[64];
                _snprintf_s(text, _TRUNCATE, "%s###bind", waiting ? "press a key, a button or move the pedal..." : view.bindText[a][slot]);
                if (ImGui::Button(text, ImVec2(w * 0.42f, 0.0f))) BindingAsk(waiting ? kBindAskCancel : kBindAskCapture, a, slot);
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) BindingAsk(kBindAskClear, a, slot);
                ImGui::PopID();
            }
            ImGui::PopID();
        };
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Bindings: click a slot, then press a key, a side button of the mouse, buttons on a pad or a button on a wheel, or move the clutch "
                            "pedal. A control already held is left alone. Escape cancels. The game may have the same key or button bound: outside this menu both act.");
        ImGui::PopTextWrapPos();
        if (s.gearbox == kGearboxOwn) for (int a = 0; a < kViewShifts; a++) bindingRow(a);
        ImGui::TextDisabled("Locks and ride");
        PageTip("The diff lock and AWD on a key on every truck. Sway bars connects and disconnects the sway bars. Ride up and Ride down move the air suspension 2 cm a press under 20 km/h. Lock front axle and Lock rear axles hold those axles Locked until pressed again, over the Axle rows of the Power tab.");
        for (int a = kViewShifts; a < kViewActions - 2; a++) bindingRow(a);
        // Winch out (input_bindings.h, kActWinchOut; winch_out.h): the row, and under it the one figure of that key.
        // Greyed with its reason where the game's functions are not found or a call faulted (163, must 3)
        {
            ImGui::BeginDisabled(view.winchOutOff != 0);
            bindingRow(kViewActions - 2);
            ImGui::EndDisabled();
            PageTip("Lets the winch wire out while held. The wire looks tight while it is let out. It goes slack once the truck moves off. The next pull of the winch takes the slack back. Pick a key or button the game does not use.");
            ImGui::BeginDisabled(view.winchOutOff != 0);
            PageLabel("Winch out speed", labelX);
            ImGui::SetNextItemWidth(w);
            changed(ImGui::SliderFloat("##winchrate", &s.winchOutRate, 0.1f, 2.0f, "%.2f m/s", ImGuiSliderFlags_AlwaysClamp));
            ImGui::EndDisabled();
            PageTip("How fast the wire is let out while Winch out is held. It stops at the full length of the winch. Default 0.50 m/s.");
            if (view.winchOutOff == 1) ImGui::TextDisabled("Winch out: not in this game version.");
            else if (view.winchOutOff == 3) ImGui::TextDisabled("Winch out: the game could not be hooked.");
            else if (view.winchOutOff == 2) ImGui::TextDisabled("Winch out: off until the game is started again. Hook the winch again.");
        }
        // the game's quick winch on its key (BRIEF_quick_winch.md): acts at once; greyed where the game's method is not found
        {
            static const char *quick[3] = { "As the game", "Standing only", "Off" };
            ImGui::BeginDisabled(view.quickWinchNoRoute);
            PageLabel("Quick winch", labelX);
            ImGui::SetNextItemWidth(w);
            changed(ImGui::Combo("##quickwinch", &s.quickWinch, quick, 3));
            ImGui::EndDisabled();
            PageTip("The quick winch of the game on its key. Standing only: it attaches only while the truck stands, so on the move the key does its other function alone. Off: never from that key.");
            if (view.quickWinchNoRoute) ImGui::TextDisabled("Quick winch: not in this build. The game does as it does.");
        }
        if (view.controllers) ImGui::TextDisabled("%d wheel or shifter read through DirectInput.", view.controllers);
    }
    else if (open && tab == kTabSounds)
    {
        ImGui::SeparatorText("Sounds");
        PageLabel("Volume", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderInt("##soundvolume", &s.soundVolume, 0, 100, s.soundVolume > 0 ? "%d" : "off", ImGuiSliderFlags_AlwaysClamp));
        PageTip("How loud the lift of the turbo and the oil warning are. 0 is no sound. Default 50.");
        ImGui::BeginDisabled(s.soundVolume <= 0);
        PageLabel("Hear it", labelX);
        if (ImGui::Button("Full boost")) act |= kPageHearHigh;
        ImGui::SameLine();
        if (ImGui::Button("Half")) act |= kPageHearMid;
        ImGui::SameLine();
        if (ImGui::Button("Little")) act |= kPageHearLow;
        PageTip("Plays a turbo sound for a lift at that much boost. Press again for another.");
        ImGui::SameLine();
        if (ImGui::Button("Oil warning")) act |= kPageHearOil;
        PageTip("Plays the oil warning.");
        ImGui::EndDisabled();
        PageLabel("Diesel flutter", labelX);
        changed(ImGui::Checkbox("##dieselflutter", &s.dieselFlutter));
        PageTip("The lift of a diesel is quiet. On: it flutters and blows off as a petrol engine does. Off by default.");
        PageLabel("Sounds only", labelX);
        changed(ImGui::Checkbox("##soundsonly", &s.soundsOnly));
        PageTip("Plays the sounds and draws no cluster. Off by default.");
    }
    else if (open && tab == kTabModWork)
    {
        ImGui::SeparatorText("Mod work");
        PageLabel("Demo values", labelX);
        changed(ImGui::Checkbox("##demo", &s.demo));
        PageTip("Draws the cluster with made-up values whenever no truck is driven. Off by default.");
        bool probe = s.probe != 0;
        PageLabel("Probe log", labelX);
        if (ImGui::Checkbox("##probe", &probe))
        {
            s.probe = probe ? 1 : 0;
            act |= kPageChanged;
        }
        PageTip("Writes the values read from the game into Gauges.log. Off by default.");
        if (s.probe)
        {
            // a test of the write into the game's memory: the points go to the driven truck's engine on the mod's next pass
            static const char *testTip = "A test of the write into the game: adds to the engine damage of the driven truck at once. Gauges.log has the numbers.";
            if (ImGui::Button("Add 1 engine damage###test1")) DamageTestAsk(1);
            PageTip(testTip);
            ImGui::SameLine();
            if (ImGui::Button("Add 10###test10")) DamageTestAsk(10);
            PageTip(testTip);
        }

        ImGui::SeparatorText("Drive");
        PageLabel("Physics rate", labelX);
        changed(ImGui::RadioButton("60###physicsrate60", &s.physicsRate, 60));
        ImGui::SameLine();
        changed(ImGui::RadioButton("120###physicsrate120", &s.physicsRate, 120));
        PageTip("Steps the physics 120 times a second. 60 turns it off. Needs a restart. For co-op set 60 on every machine.");
        {
            const char *note = view.rateNote == kRateNoteOff    ? "120 was asked and is not running. Gauges.log says why."
                               : view.rateNote == kRateNoteIter ? "Running at 120 with other solver iterations than asked. Gauges.log has the figures."
                               : view.rateNote == kRateNoteStep ? "The step is not 1/120 s. Gauges.log has the figures."
                               : view.rateNote == kRateNotePart ? "Running at 120 with a part of the corrections missing. Gauges.log says which."
                               : s.physicsRate != view.rateNow  ? (s.physicsRate == 120 ? "Saved. 120 runs after a restart." : "Saved. 60 runs after a restart.")
                               : view.rateNow == 120            ? "Running at 120."
                                                                : nullptr;
            if (note) ImGui::TextDisabled("%s", note);
            // the winch at 120 (BRIEF_winch_pull_120.md, M4)
            if (view.rateWinch == kRateWinchBroken) ImGui::TextDisabled("Winch pull: part changed. Start the game again.");
            else if (view.rateNow == 120 && view.rateWinch == kRateWinchIn) ImGui::TextDisabled("Winch pull: right at 120.");
            else if (view.rateNow == 120 && view.rateWinch == kRateWinchRefused) ImGui::TextDisabled("Winch pull: too fast at 120 (not corrected, see the log).");
        }

        ImGui::Separator();
        if (ImGui::Button("Defaults"))
        {
            st.capture = -1;
            act |= kPageDefaults;
        }
        PageTip("Every setting back to its default. The truck you drive gives up its own scales. The profile of an engine stays: Class curve on the Engine tab resets it.");
        ImGui::SameLine();
        if (ImGui::Button("Reload ini")) act |= kPageReload;
        PageTip("Reads Gauges.ini again, after an edit by hand.");
        if (s.checkers)
        {
            // the board's own row, the last action (input_bindings.h, kActCheckers): there once the board was opened
            const int a = kViewActions - 1;
            ImGui::PushID(1000 + a);
            PageLabel("Checkers", labelX);
            for (int slot = 0; slot < 2; slot++)
            {
                ImGui::PushID(slot);
                if (slot) ImGui::SameLine();
                const bool waiting = view.captureAction == a && view.captureSlot == slot;
                char text[64];
                _snprintf_s(text, _TRUNCATE, "%s###bind", waiting ? "press a key, a button or move the pedal..." : view.bindText[a][slot]);
                if (ImGui::Button(text, ImVec2(w * 0.42f, 0.0f))) BindingAsk(waiting ? kBindAskCancel : kBindAskCapture, a, slot);
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) BindingAsk(kBindAskClear, a, slot);
                ImGui::PopID();
            }
            ImGui::PopID();
            PageTip("Opens and closes the board. The game still hears the pad.");
        }
        ImGui::TextDisabled("Changes count at once and go into Gauges.ini, and the settings of a truck or a gearbox into its file in Gauges. Version " GAUGES_VERSION ".");
        // five clicks on that line within three seconds (checkers.h): no hover, no cursor, no tip
        if (ImGui::IsItemClicked() && CkClick(st.ckClicks, ImGui::GetTime())) act |= kPageCheckers;
    }
    ImGui::EndChild();
    InterlockedExchange(&g_pageCapturing, st.capture >= 1 ? (LONG)GetTickCount() : 0);
    return act;
}
