// settings_page.h: the Gauges tab in ReShade's overlay, every Gauges.ini setting edited in the game. Shared by the mod
// (gauge_overlay.cpp, Dear ImGui through ReShade's function table) and the offline preview (test\gauge_preview.cpp).
// Include after imgui.h. The tab works on a copy of the settings in force and reports what to do with it; ReShade
// hands the overlay's keyboard to Dear ImGui, so a new toggle key is read from there.
#pragma once
#include "gauges.h"

struct PageState
{
    int capture = -1;             // -1 none, 0 the toggle key, 1 the pad buttons
    uint32_t peak = 0;            // the pad buttons held together so far while capturing
    bool armed = false;           // every pad button was up since the capture began: the press that began it is not read
    unsigned long long began = 0; // GetTickCount64 when the capture began (it gives up after kPageCaptureMs)
    unsigned long long last = 0;  // GetTickCount64 of the last frame the tab was drawn
};
static const unsigned long long kPageCaptureMs = 8000;
enum { kPageChanged = 1, kPageReload = 2 };

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

static void PageTip(const char *text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text);
}

// A label in the left column, the widget after it.
static void PageLabel(const char *text, float x)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
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

// Draws the tab on s (the settings in force) and returns kPageChanged when s was edited, kPageReload for the ini
// button. view: what the cluster shows now; padNow: the pad's buttons this frame, for pad buttons being set.
static int SettingsPage(GaugeSettings &s, const GaugeView &view, PageState &st, uint32_t padNow)
{
    int act = 0;
    auto changed = [&](bool c) { if (c) act |= kPageChanged; };
    const unsigned long long now = GetTickCount64();
    if (now - st.last > 1000) st.capture = -1; // the overlay was closed in between
    st.last = now;
    const float em = ImGui::GetFontSize(), labelX = em * 12.0f, w = em * 14.0f;
    char buf[64];

    ImGui::SeparatorText("Cluster");
    if (view.show) ImGui::TextUnformatted(view.demo ? "On screen, with demo values." : "On screen.");
    else if (view.hidden == kHiddenByToggle) ImGui::TextDisabled("Hidden. The toggle key or the pad buttons show it again.");
    else ImGui::TextDisabled("No truck is being driven. The cluster shows while you drive.");
    static const char *layouts[kLayoutCount] = { "Strip, bottom centre", "Block, bottom right" };
    PageLabel("Layout", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::Combo("##layout", &s.layout, layouts, kLayoutCount));
    static const char *styles[kFaceCount] = { "Solid", "Tinted", "Open" };
    PageLabel("Style", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::Combo("##style", &s.style, styles, kFaceCount));
    PageTip("Solid: dark dial faces with a bezel. Tinted: see-through faces. Open: no faces, only the scales, numbers and needles.");
    ImGui::BeginDisabled(s.style != kFaceTinted);
    PageLabel("Face opacity", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::SliderFloat("##face", &s.faceOpacity, 0.0f, kFaceOpacityMax, "%.2f", ImGuiSliderFlags_AlwaysClamp));
    PageTip("How dark the tinted faces are. 0 = clear.");
    ImGui::EndDisabled();
    PageLabel("Size", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::SliderFloat("##scale", &s.scale, 0.5f, 2.5f, "x%.2f", ImGuiSliderFlags_AlwaysClamp));
    PageLabel("Opacity", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::SliderFloat("##opacity", &s.opacity, kOpacityMin, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp));
    PageLabel("Position", labelX);
    ImGui::SetNextItemWidth(w * 0.5f - ImGui::GetStyle().ItemInnerSpacing.x * 0.5f);
    changed(ImGui::DragFloat("##x", &s.offsetX, 1.0f, -kOffsetMax, kOffsetMax, "%.0f right", ImGuiSliderFlags_AlwaysClamp));
    PageTip("Moves the cluster from its place, in pixels of a 1080p screen. Drag, or double-click to type.");
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(w * 0.5f - ImGui::GetStyle().ItemInnerSpacing.x * 0.5f);
    changed(ImGui::DragFloat("##y", &s.offsetY, 1.0f, -kOffsetMax, kOffsetMax, "%.0f down", ImGuiSliderFlags_AlwaysClamp));
    PageTip("Moves the cluster from its place, in pixels of a 1080p screen. Drag, or double-click to type.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Back###origin"))
    {
        s.offsetX = s.offsetY = 0.0f;
        act |= kPageChanged;
    }
    PageTip("Back to the layout's own place.");

    ImGui::SeparatorText("Dials");
    changed(ImGui::Checkbox("Coolant temperature", &s.showTemp));
    changed(ImGui::Checkbox("Boost pressure", &s.showBoost));
    changed(ImGui::Checkbox("Brake air pressure", &s.showAir));
    changed(ImGui::Checkbox("Lamps", &s.showLamps));
    PageTip("Handbrake, headlights, all wheel drive, differential lock, low gear, engine damage, cold engine and tilt.");

    ImGui::SeparatorText("Tilt lamp");
    {
        // red never comes before amber: the one being dragged takes the other along
        PageLabel("Amber from", labelX);
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("##rollwarn", &s.rollWarnDeg, 5.0f, 45.0f, "%.0f degrees to a side", ImGuiSliderFlags_AlwaysClamp))
        {
            s.rollWarnDeg = floorf(s.rollWarnDeg + 0.5f);
            if (s.rollDangerDeg < s.rollWarnDeg) s.rollDangerDeg = s.rollWarnDeg;
            act |= kPageChanged;
        }
        PageTip("The lamp is green while the truck stands level and amber once it leans to a side by this much.");
        PageLabel("Red from", labelX);
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("##rolldanger", &s.rollDangerDeg, 5.0f, 60.0f, "%.0f degrees to a side", ImGuiSliderFlags_AlwaysClamp))
        {
            s.rollDangerDeg = floorf(s.rollDangerDeg + 0.5f);
            if (s.rollWarnDeg > s.rollDangerDeg) s.rollWarnDeg = s.rollDangerDeg;
            act |= kPageChanged;
        }
        PageTip("Red once the truck leans to a side by this much. The truck in the lamp leans as yours does.");
        PageLabel("Red on a slope from", labelX);
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("##pitchdanger", &s.pitchDangerDeg, 10.0f, 60.0f, "%.0f degrees up or down", ImGuiSliderFlags_AlwaysClamp))
        {
            s.pitchDangerDeg = floorf(s.pitchDangerDeg + 0.5f);
            act |= kPageChanged;
        }
        PageTip("Red once the truck points up or down a slope by this much.");
    }

    ImGui::SeparatorText("Units");
    changed(ImGui::Checkbox("Miles per hour", &s.mph));
    PageTip("Speed in mph and the range in miles. Off: km/h and kilometres.");
    changed(ImGui::Checkbox("Pounds per square inch", &s.psi));
    PageTip("Boost in psi. Off: bar.");
    changed(ImGui::Checkbox("Fahrenheit", &s.fahrenheit));
    PageTip("Coolant temperature in degrees Fahrenheit. Off: Celsius.");

    ImGui::SeparatorText("Scales");
    static const char *classes[kClassCount] = { "Truck", "Scout", "Heavy" };
    PageLabel("Truck class", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::Combo("##class", &s.tachClass, classes, kClassCount));
    PageTip("Sets the four numbers below: trucks idle at 650 rpm with the red zone from 2200 of 3000 and a speedometer to 100 km/h, "
            "scouts 800, 5500 of 7000 and 120 km/h, heavy trucks 600, 1900 of 2500 and 80 km/h.");
    {
        const GaugeScale own = kClassScales[s.tachClass < 0 || s.tachClass >= kClassCount ? kClassTruck : s.tachClass];
        // each number is the class's own until it is changed here; "Class" gives it back to the class
        struct Row { const char *label, *unit, *tip; float *value; float own; const float *range; float speed; };
        const Row rows[4] = {
            { "Idle", "rpm", "Where the needle rests with the engine running.", &s.idleRpm, own.idle, kIdleRange, 5.0f },
            { "Red zone from", "rpm", "Where the tachometer's red zone starts.", &s.redlineRpm, own.redline, kRedlineRange, 10.0f },
            { "Tachometer top", "rpm", "The end of the tachometer's scale, rounded up to a whole step of its numbers.", &s.maxRpm, own.max, kMaxRpmRange, 10.0f },
            { "Speedometer top", "km/h", "The end of the speedometer's scale, in km/h whatever the unit shown.", &s.speedMax, own.speedMax, kSpeedMaxRange, 0.5f } };
        for (int i = 0; i < 4; i++)
        {
            const Row &r = rows[i];
            float shown = *r.value > 0.0f ? *r.value : r.own;
            sprintf_s(buf, *r.value > 0.0f ? "%%.0f %s" : "%%.0f %s (class)", r.unit);
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
    }

    ImGui::SeparatorText("Controls");
    PageLabel("Toggle key", labelX);
    KeyName(s.toggleKey, buf, sizeof buf);
    if (ImGui::Button(st.capture == 0 ? "Press a key (Esc: keep)###key" : (strcat_s(buf, "###key"), buf), ImVec2(w, 0)))
        st.capture = st.capture == 0 ? -1 : 0;
    PageTip("Hides the cluster and shows it again.");
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
    PageLabel("Toggle on the pad", labelX);
    sprintf_s(buf, "%s###bind", s.padToggle);
    if (ImGui::Button(buf, ImVec2(w, 0)) && st.capture < 1)
    {
        st.capture = 1;
        st.peak = 0;
        st.armed = false;
        st.began = now;
        beganNow = true;
    }
    PageTip("Press it, then hold the button or buttons on the pad and let go. The game sees these buttons too, so pick ones it does not use while driving.");
    ImGui::SameLine();
    if (ImGui::SmallButton("None###nopad"))
    {
        strcpy_s(s.padToggle, "None");
        act |= kPageChanged;
    }
    if (st.capture >= 1)
    {
        // The buttons are read in a window of its own: ReShade's overlay can be worked with the pad, and on the tab
        // the buttons being held would walk the widgets and press them (PageCaptureStep has the rest). B closes a
        // popup, so it is opened again each frame it is found closed.
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
    InterlockedExchange(&g_pageCapturing, st.capture >= 1 ? (LONG)GetTickCount() : 0);

    ImGui::SeparatorText("For work on the mod");
    changed(ImGui::Checkbox("Demo values", &s.demo));
    PageTip("Draws the cluster with made up values whenever no truck is driven, in menus and on loading screens too.");
    bool probe = s.probe != 0;
    if (ImGui::Checkbox("Probe log", &probe))
    {
        s.probe = probe ? 1 : 0;
        act |= kPageChanged;
    }
    PageTip("Writes the values read from the game into Gauges.log, every two seconds and whenever one changes.");

    ImGui::Separator();
    if (ImGui::Button("Defaults"))
    {
        SettingsDefaults(s);
        st.capture = -1;
        act |= kPageChanged;
    }
    ImGui::SameLine();
    if (ImGui::Button("Read the ini again")) act |= kPageReload;
    ImGui::TextDisabled("Changes count at once and go into Gauges.ini. Version " GAUGES_VERSION ".");
    return act;
}
