// settings_page.h: the Gameplay tab of the NEXT GEN menu, every Gauges.ini setting edited in the game. Shared by the mod
// (gauge_overlay.cpp, on the mod's own overlay) and the offline previews (test\gauge_preview.cpp, test\menu_preview.cpp).
// Include after imgui.h. The tab works on a copy of the settings in force and reports what to do with it; the menu
// hands every key to Dear ImGui while it is open, so a new toggle key is read from there.
#pragma once
#include "gauges.h"

struct PageState
{
    int capture = -1;             // -1 none, 0 the toggle key, 1 the pad buttons
    uint32_t peak = 0;            // the pad buttons held together so far while capturing
    bool armed = false;           // every pad button was up since the capture began: the press that began it is not read
    unsigned long long began = 0; // GetTickCount64 when the capture began (it gives up after kPageCaptureMs)
    unsigned long long last = 0;  // GetTickCount64 of the last frame the tab was drawn
    float mapAmbientC = 0.0f;     // the outside temperature the slider set for the map's section in force (kPageMapAmbient)
    float gearHold = 0.0f;        // the seconds the slider set for the driven truck's gearbox (kPageGearHold)
    float enginePeak = 0.0f;      // the driven truck's engine's profile as the Engine section set it (kPageEngineSet)
    int engineAspiration = 0;
    float engineFullPull = 0.0f;  // and the share of a gear's top its full pull reaches, as the slider set it (kPageEngineFull)
    int engineTorque = 100;       // and how much of its torque the truck gets, in percent, as the slider set it (kPageEngineTorque)
};
static const unsigned long long kPageCaptureMs = 8000;
enum { kPageChanged = 1, kPageReload = 2, kPageTruckDrop = 4, kPageDefaults = 8, kPageHearHigh = 16, kPageHearMid = 32, kPageHearLow = 64, kPageHearOil = 128,
       kPagePullTest = 256, kPagePullCancel = 512, kPageMapAmbient = 1024, kPageGearHold = 2048, kPageEngineSet = 4096,
       kPageEngineDrop = 8192, kPageEngineFull = 16384, kPageEngineTorque = 32768 };

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
// A class's name in the tab's text (GaugeSettings::tachClass).
static const char *PageClassWord(int tachClass)
{
    static const char *const words[kClassCount] = { "truck", "scout", "heavy" };
    return words[tachClass < 0 || tachClass >= kClassCount ? kClassTruck : tachClass];
}

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
// button, kPageTruckDrop for the button that gives the driven truck the plain scales back, kPageDefaults for the
// defaults button. view: what the cluster shows now; padNow: the pad's buttons this frame, for pad buttons being set.
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
    else if (view.hidden == kHiddenSoundsOnly) ImGui::TextDisabled("Not drawn: \"Sounds only\" is ticked under Sounds.");
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
    PageTip("The air in the brakes' tanks. The engine's compressor fills them up to 8.2 bar, faster the higher it revs, and starts again at 6.9; "
            "each press of the brake and letting the handbrake go draw from them. Under 5.5 bar it is in the red. Not drawn for scouts, which brake by hydraulics.");
    changed(ImGui::Checkbox("Lamps", &s.showLamps));
    PageTip("Handbrake, headlights, all wheel drive, differential lock, low gear, engine damage, cold engine, tilt and oil temperature.");
    static const char *oils[kOilGaugeCount] = { "Not shown", "Second needle", "Dial of its own" };
    PageLabel("Oil temperature", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::Combo("##oilgauge", &s.oilGauge, oils, kOilGaugeCount));
    PageTip("Second needle: an amber needle in the coolant temperature dial. Dial of its own: a small dial left of the coolant's. "
            "The oil lamp is in the lamps either way.");

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

    // the tips name temperatures in the unit the dial shows
    auto degrees = [&](float c, char *out, size_t size) { sprintf_s(out, size, s.fahrenheit ? "%.0f F" : "%.0f C", s.fahrenheit ? c * 1.8f + 32.0f : c); };

    ImGui::SeparatorText("Coolant temperature");
    {
        char from[16], to[16], tip[384];
        PageLabel("Warm after", labelX);
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("##warmup", &s.warmUpMinutes, 1.0f, 15.0f, "%.1f minutes of driving", ImGuiSliderFlags_AlwaysClamp))
        {
            s.warmUpMinutes = floorf(s.warmUpMinutes * 10.0f + 0.5f) / 10.0f;
            act |= kPageChanged;
        }
        degrees(kTempOutside, from, sizeof from);
        degrees(kTempWarm, to, sizeof to);
        sprintf_s(tip, "The time a cold engine takes from %s to %s when it is driven as usual. Idling takes much longer.", from, to);
        PageTip(tip);
        PageLabel("Overheats after", labelX);
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("##overheat", &s.overheatMinutes, 0.5f, 10.0f, "%.1f minutes floored", ImGuiSliderFlags_AlwaysClamp))
        {
            s.overheatMinutes = floorf(s.overheatMinutes * 10.0f + 0.5f) / 10.0f;
            act |= kPageChanged;
        }
        degrees(kTempHot, from, sizeof from);
        degrees(kTempRed, to, sizeof to);
        sprintf_s(tip, "The time from %s into the red zone at %s floored at the red zone with the wheels spinning at a standstill. "
                       "The heat follows the fuel the engine burns: the pedal and the engine speed together. Driving cools the engine: the faster "
                       "the truck goes, the more air the radiator gets.", from, to);
        PageTip(tip);
        PageLabel("Cool after", labelX);
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("##cooldown", &s.coolDownMinutes, 2.0f, 60.0f, "%.0f minutes stopped", ImGuiSliderFlags_AlwaysClamp))
        {
            s.coolDownMinutes = floorf(s.coolDownMinutes + 0.5f);
            act |= kPageChanged;
        }
        degrees(kTempCooled, to, sizeof to);
        sprintf_s(tip, "The time a stopped engine takes from %s down to %s.", from, to);
        PageTip(tip);
        // the outside temperature: by the map (the default), the slider shows and sets the temperature of the map's
        // section in force, its own or its region's; where none is, and with the map left out, it sets AmbientC. The
        // ini holds it in C; the slider shows it in the unit the dial shows
        PageLabel("Outside by map", labelX);
        changed(ImGui::Checkbox("##ambientbymap", &s.ambientByMap));
        PageTip("The outside temperature goes by the map you drive on, which the mod reads from the game's save: the map's own or its region's, "
                "as Gauges.ini has them under [Map:<name>]. Off: one temperature on every map.");
        const bool byMap = s.ambientByMap && view.map[0] && view.mapOwn;
        const float shown = byMap ? view.ambientC : s.ambientC;
        float outside = s.fahrenheit ? shown * 1.8f + 32.0f : shown;
        PageLabel("Outside temperature", labelX);
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
        sprintf_s(tip, "A truck that has stood for long is this cold. In colder air the engine takes longer to warm up and is harder to overheat. "
                       "The cold lamp is lit below %s.", to);
        PageTip(tip);
        // dimmed and wrapped: a map's name can be long
        ImGui::BeginDisabled();
        if (byMap)
        {
            char day[32], night[32];
            degrees(view.ambientDay, day, sizeof day);
            degrees(view.ambientNight, night, sizeof night);
            if (view.hour >= 0.0f)
                ImGui::TextWrapped("On %s, from [Map:%s] in Gauges.ini: %s by day, %s by night, now %02d:%02d of the game's day. The slider moves both.", view.map,
                                   view.mapFrom, day, night, (int)view.hour, (int)((view.hour - (int)view.hour) * 60.0f));
            else
                ImGui::TextWrapped("On %s, from [Map:%s] in Gauges.ini: %s by day, %s by night. The game's clock is not known yet (with Probe=1 the mod looks "
                                   "for it while you drive), so the day's mean is in force. The slider moves both.", view.map, view.mapFrom, day, night);
        }
        else if (!s.ambientByMap) ImGui::TextWrapped("The same on every map.");
        else if (view.map[0]) ImGui::TextWrapped("On %s: this temperature, as Gauges.ini has no section for the map or its region.", view.map);
        else ImGui::TextWrapped("This temperature until the mod has found the game's save, which says the map.");
        ImGui::EndDisabled();
        // the ini holds the speed in km/h; the slider shows it in the unit the speedometer shows
        float full = s.mph ? s.airFullKmh / 1.609f : s.airFullKmh;
        PageLabel("Full airflow from", labelX);
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("##airfull", &full, s.mph ? kAirFullRange[0] / 1.609f : kAirFullRange[0], s.mph ? kAirFullRange[1] / 1.609f : kAirFullRange[1],
                               s.mph ? "%.0f mph" : "%.0f km/h", ImGuiSliderFlags_AlwaysClamp))
        {
            full = floorf(full + 0.5f);
            s.airFullKmh = SettingsRange(s.mph ? full * 1.609f : full, kAirFullRange[0], kAirFullRange[1]);
            act |= kPageChanged;
        }
        PageTip("The radiator gets more air the faster the truck drives, and all of it from this speed. Lower keeps a slow, hard pulling engine cooler. "
                "Higher makes it run hotter at a crawl.");
    }

    ImGui::SeparatorText("Oil temperature");
    {
        char from[16], to[16], tip[320];
        PageLabel("Oil warm after", labelX);
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("##oilwarmup", &s.oilWarmUpMinutes, 2.0f, 30.0f, "%.1f minutes of driving", ImGuiSliderFlags_AlwaysClamp))
        {
            s.oilWarmUpMinutes = floorf(s.oilWarmUpMinutes * 10.0f + 0.5f) / 10.0f;
            act |= kPageChanged;
        }
        degrees(kTempOutside, from, sizeof from);
        degrees(kTempWarm, to, sizeof to);
        sprintf_s(tip, "The time the oil takes from %s to %s when the engine is driven as usual, at least a minute after the coolant. "
                       "The oil follows the coolant slowly, keeps its heat longer and runs a few degrees hotter when the engine works hard.", from, to);
        PageTip(tip);
        // the ini holds the temperature in C; the slider shows it in the unit the dial shows
        float warn = s.fahrenheit ? s.oilWarnC * 1.8f + 32.0f : s.oilWarnC;
        PageLabel("Oil warning from", labelX);
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("##oilwarn", &warn, s.fahrenheit ? kOilWarnRange[0] * 1.8f + 32.0f : kOilWarnRange[0],
                               s.fahrenheit ? kOilWarnRange[1] * 1.8f + 32.0f : kOilWarnRange[1], s.fahrenheit ? "%.0f F" : "%.0f C", ImGuiSliderFlags_AlwaysClamp))
        {
            warn = floorf(warn + 0.5f);
            s.oilWarnC = SettingsRange(s.fahrenheit ? (warn - 32.0f) / 1.8f : warn, kOilWarnRange[0], kOilWarnRange[1]);
            act |= kPageChanged;
        }
        PageTip("From this oil temperature the oil lamp blinks and a warning sounds every few seconds, until the oil is two degrees under it again.");
    }

    ImGui::SeparatorText("Engine damage");
    {
        char at[16], from[16], tip[320];
        changed(ImGui::Checkbox("The temperature costs the engine", &s.damage));
        PageTip("On: an engine in the red zone, and a cold engine worked hard, take the game's own engine damage. A garage repairs it. "
                "Off: the temperature is only shown. It has not been tried in co-op: leave it off there.");
        ImGui::BeginDisabled(!s.damage);
        PageLabel("Overheating", labelX);
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("##hotdamage", &s.hotDamage, 0.0f, 100.0f, "%.0f percent a minute", ImGuiSliderFlags_AlwaysClamp))
        {
            s.hotDamage = floorf(s.hotDamage + 0.5f);
            act |= kPageChanged;
        }
        degrees(kTempHotDamage, at, sizeof at);
        degrees(kTempRed, from, sizeof from);
        sprintf_s(tip, "The share of what the engine can take that it loses each minute at %s. It loses nothing below the red zone at %s, "
                       "less than this between the two and more above. 0 = none.", at, from);
        PageTip(tip);
        PageLabel("Cold engine", labelX);
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("##colddamage", &s.coldDamage, 0.0f, 60.0f, "%.0f percent a minute", ImGuiSliderFlags_AlwaysClamp))
        {
            s.coldDamage = floorf(s.coldDamage + 0.5f);
            act |= kPageChanged;
        }
        degrees(0.0f, at, sizeof at);
        degrees(kTempCold, from, sizeof from);
        sprintf_s(tip, "The share it loses each minute floored at the red zone at %s. Less the warmer the engine and the lighter the work: "
                       "nothing from %s up, and nothing while it burns half the fuel it burns floored at the red zone or less. 0 = none.", at, from);
        PageTip(tip);
        ImGui::EndDisabled();
        if (view.engineCapacity > 0) ImGui::TextDisabled("This truck's engine has %d of %d left.", view.engineCapacity - view.engineDamage, view.engineCapacity);
    }

    ImGui::SeparatorText("Units");
    changed(ImGui::Checkbox("Miles per hour", &s.mph));
    PageTip("Speed in mph and the range in miles. Off: km/h and kilometres.");
    changed(ImGui::Checkbox("Pounds per square inch", &s.psi));
    PageTip("Boost in psi. Off: bar.");
    changed(ImGui::Checkbox("Fahrenheit", &s.fahrenheit));
    PageTip("Coolant temperature in degrees Fahrenheit. Off: Celsius.");

    ImGui::SeparatorText("Scales");
    // whose scales these are: the driven truck's own, or the plain ones
    if (!view.truck[0]) ImGui::TextWrapped("No truck is driven. These are the plain scales, which every truck without scales of its own shows.");
    else if (!view.truckOwn)
        ImGui::TextWrapped("Scales for %s. It shows the plain ones. Change one here and this kind of truck keeps all seven as its own.", view.truck);
    else
    {
        ImGui::TextWrapped("Scales for %s. They are its own, kept for this kind of truck in its file in Gauges\\trucks.", view.truck);
        if (ImGui::SmallButton("Back to the plain scales")) act |= kPageTruckDrop;
        PageTip("This kind of truck gives up its own scales and shows the plain ones again, as every truck without its own does.");
    }
    static const char *classes[kClassCount] = { "Truck", "Scout", "Heavy" };
    PageLabel("Truck class", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::Combo("##class", &s.tachClass, classes, kClassCount));
    PageTip("Sets the five numbers below: trucks idle at 650 rpm with the red zone from 2200 of 3000, a speedometer to 100 km/h and 1.8 bar of boost, "
            "scouts 800, 5500 of 7000, 120 km/h and 1 bar, heavy trucks 600, 1900 of 2500, 80 km/h and 2 bar. The mod's automatic shifts by the class of "
            "a truck's own scales; a truck without them shifts by the class of its weight (a scout up to 6 t, a truck under 16 t, a heavy truck from 16 t).");
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
        // the turbo's boost: in bar with two decimals, so it has a row of its own
        float shown = s.boostMax > 0.0f ? s.boostMax : own.boost;
        PageLabel("Boost at full load", labelX);
        ImGui::SetNextItemWidth(w);
        if (ImGui::DragFloat("##boost", &shown, 0.01f, kBoostRange[0], kBoostRange[1], s.boostMax > 0.0f ? "%.2f bar" : "%.2f bar (class)", ImGuiSliderFlags_AlwaysClamp))
        {
            s.boostMax = floorf(shown * 100.0f + 0.5f) / 100.0f;
            act |= kPageChanged;
        }
        PageTip("The boost the turbo reaches with the pedal floored at high engine speed, in bar whatever the unit shown. "
                "The classes' own: trucks 1.8, scouts 1, heavy trucks 2.");
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
        PageTip("How the turbo builds its boost. Standard: from about a fifth of the way to the red zone. Small and quick: early and with little lag. "
                "Big and slow: late, and long to come at low revs. Two stages: a small turbo early and a big one after it.");
    }

    ImGui::SeparatorText("Engine");
    // the driven truck's engine: its peak torque's speed and how it breathes, kept for that engine in its own file
    if (!view.engine[0]) ImGui::TextWrapped("No truck is driven, or its engine is not known yet.");
    else
    {
        if (view.engineOwn) ImGui::TextWrapped("Engine %s. Its own profile, kept in Gauges\\engines.", view.engine);
        else ImGui::TextWrapped("Engine %s. It pulls by the %s class's torque curve%s. Set its peak torque here and this engine keeps a profile of its own.", view.engine,
                                PageClassWord(view.autoClass), view.autoByWeight ? " (the class of the truck's weight)" : "");
        if (view.engineFigure > 0.0f) ImGui::TextWrapped("Its torque in the game's files: %.0f (a stock scout's engine reads about 195000).", view.engineFigure);
        float peak = view.enginePeakRpm;
        int aspiration = view.engineAspiration;
        bool set = false;
        PageLabel("Peak torque at", labelX);
        ImGui::SetNextItemWidth(w);
        if (view.engineCustom) ImGui::BeginDisabled();
        if (ImGui::SliderFloat("##peakrpm", &peak, view.engineIdle, view.engineRedline, "%.0f rpm", ImGuiSliderFlags_AlwaysClamp)) set = true;
        if (view.engineCustom) ImGui::EndDisabled();
        PageTip(view.engineCustom ? "Its file holds a torque curve of its own (TorqueCurve), which takes the place of one built from the peak."
                                  : "The engine speed of the engine's peak torque, as its maker gives it (most engines' figures are published). The torque curve is "
                                    "built around it: a turbo diesel's broad plateau from a little under it to half as much again over it, a naturally aspirated "
                                    "engine's narrower hump. With the mod's automatic and the engine handed over, a light foot shifts up no sooner than 0.8 of it.");
        static const char *aspirations[kAspCount] = { "Turbo", "Naturally aspirated" };
        PageLabel("Aspiration", labelX);
        ImGui::SetNextItemWidth(w);
        if (ImGui::Combo("##aspiration", &aspiration, aspirations, kAspCount)) set = true;
        PageTip("Naturally aspirated: no boost, so no boost dial and no turbo sounds, and all of the torque at once with the pedal.");
        // how far toward a gear's top the engine keeps its full pull, shown in rpm on these scales, kept in percent
        float pullRpm = SettingsRange(view.engineFullPull, kFullPullRange[0], kFullPullRange[1]) * view.engineRedline;
        PageLabel("Full pull up to", labelX);
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("##fullpull", &pullRpm, kFullPullRange[0] * view.engineRedline, kFullPullRange[1] * view.engineRedline, "%.0f rpm", ImGuiSliderFlags_AlwaysClamp) &&
            view.engineRedline > 0.0f)
        {
            st.engineFullPull = floorf(pullRpm / view.engineRedline * 100.0f + 0.5f) / 100.0f;
            act |= kPageEngineFull;
        }
        PageTip("With the engine handed to the game: how close to the red zone the engine keeps all its torque floored, before it fades out "
                "toward the top of each gear as a governor cuts the fuel. 92 percent of the red zone is every engine's at first and suits a "
                "diesel; a petrol engine that makes its power up to its red zone can keep it to 96 to 98. Floored, the mod's automatic shifts "
                "up where the next gear pulls more, so it revs further too. Near a gear's top the mod writes a large torque factor for it "
                "(10 times the game's own at 95, 24 at 98): lower it where a truck surges at the top of a gear.");
        // how much of its torque the truck gets, kept in percent
        int torquePercent = (int)lroundf(view.engineTorque * 100.0f);
        PageLabel("Torque", labelX);
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderInt("##torqueshare", &torquePercent, (int)(kTorqueShareRange[0] * 100.0f), (int)(kTorqueShareRange[1] * 100.0f), "%d %%", ImGuiSliderFlags_AlwaysClamp))
        {
            st.engineTorque = torquePercent;
            act |= kPageEngineTorque;
        }
        PageTip("How much of the engine's torque the truck gets while Engine to the game is on, the game's own pull included. 100 is the engine as it comes. "
                "Lower it for a mod's engine with several times a stock engine's torque, such as RLM's: at 25 an engine with four times the torque pulls like the stock one. "
                "The set-off gear follows it. No effect with Engine to the game off.");
        if (set)
        {
            st.enginePeak = floorf(peak / 50.0f + 0.5f) * 50.0f;
            st.engineAspiration = aspiration;
            act |= kPageEngineSet;
        }
        PageLabel("Curve", labelX);
        ImGui::PlotLines("##torquecurve", view.engineCurve, kEnginePlot, 0, nullptr, 0.0f, 1.05f, ImVec2(w, 60.0f * ImGui::GetFontSize() / 13.0f));
        PageTip("The torque in force from idle to the tachometer's top, as a share of the engine's most.");
        if (view.engineOwn && ImGui::SmallButton("Back to the class's curve")) act |= kPageEngineDrop;
    }

    ImGui::SeparatorText("Gearbox");
    static const char *gearboxes[kGearboxCount] = { "The game's own", "The mod's automatic" };
    PageLabel("Automatic", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::Combo("##gearbox", &s.gearbox, gearboxes, kGearboxCount));
    PageTip("The mod's automatic picks the gears while the game's shifter is in A: a torque converter, shifts up later the further the pedal is down, "
            "a kickdown when it is floored, the gear held when the foot lifts, and the first gear as the truck stops. The tachometer shows its engine "
            "speed. Picking a gear by hand hands the gearbox back to you.");
    static const char *inputs[kInputCount] = { "A wheel's pedals", "A pad", "The keyboard" };
    PageLabel("Driven with", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::Combo("##input", &s.input, inputs, kInputCount));
    PageTip("What the mod's automatic takes the pedal for. A wheel's pedals: a push from part throttle to the floor kicks down. A pad: the same, "
            "but the trigger has to come from under three quarters, as one held near its end wavers. The keyboard: no kickdown, and a key held "
            "shifts as the pedal floored.");
    PageLabel("Engine to the game", labelX);
    changed(ImGui::Checkbox("##powertrain", &s.powertrain));
    PageTip("The truck pulls with the mod's engine: its torque curve against the engine speed the needle shows, less with cold oil and "
            "without boost where the pedal asks for more, and with the mod's automatic doubled by the converter at its stall. The game's own "
            "engine gives all its torque at about half a gear's top speed and less and less toward the top; with this on, a gear revved high "
            "pulls as a real engine's would, and the mod's automatic shifts as a truck's does: floored at the red zone, early at a light "
            "foot. The mod writes the game's torque factor for it.");
    if (s.powertrain)
    {
        PageLabel("How much of it", labelX);
        ImGui::SetNextItemWidth(w);
        changed(ImGui::SliderInt("##engineshare", &s.engineShare, 0, 100, "%d %%", ImGuiSliderFlags_AlwaysClamp));
        PageTip("How much of the mod's engine the game gets: 0 leaves the game's own pull, 100 hands it all of the mod's engine, and between the "
                "two the truck pulls between them. Lower it where a truck pulls too hard.");
        PageLabel("Rev cap 95%", labelX);
        changed(ImGui::Checkbox("##revcap", &s.revCap));
        PageTip("A test switch. With the mod's engine the pull ends at 95 percent of the way from idle to the red zone, as it did until 0.15.10, "
                "and the automatic shifts up there floored. Try it where a heavy truck runs away on its own.");
        // the driven truck's gearbox: how long each gear is held going up, kept for that gearbox
        if (view.gearbox[0] && s.gearbox == kGearboxModel)
        {
            float hold = view.gearHold;
            PageLabel("Each gear going up", labelX);
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderFloat("##gearhold", &hold, kGearHoldRange[0], kGearHoldRange[1], "%.1f s", ImGuiSliderFlags_AlwaysClamp))
            {
                st.gearHold = floorf(hold * 10.0f + 0.5f) / 10.0f;
                act |= kPageGearHold;
            }
            char gearTip[400], whose[160] = "";
            if (!view.gearHoldOwn)
                sprintf_s(whose, "; until it is moved, the %s class's%s: a scout 1.5 s, the others 2.5 s", PageClassWord(view.autoClass),
                          view.autoByWeight ? " (the class of the truck's weight; scales of its own set it instead)" : "");
            sprintf_s(gearTip, "Going up through the gears the mod's automatic holds each gear at least this long, on the limiter if it comes to that. "
                               "Kept for this gearbox (%s)%s.", view.gearbox, whose);
            PageTip(gearTip);
        }
    }
    PageLabel("Pull test", labelX);
    const bool pullOn = view.pullTest == kViewPullArmed || view.pullTest == kViewPullRunning;
    if (ImGui::Button(pullOn ? "Cancel###pulltest" : "Test the engine's pull###pulltest", ImVec2(w, 0))) act |= pullOn ? kPagePullCancel : kPagePullTest;
    PageTip("Measures how the game's torque factor, which carries the engine's torque (Engine to the game), changes the pull. Press it, then on a long flat "
            "road drive in the fourth gear or higher (the mod's automatic, or a gear picked by hand) and floor the pedal for seven seconds. The mod holds "
            "the gear, changes the torque factor for a second at a time and measures the pull, then puts it back. The numbers go into Gauges.log.");
    if (view.pullTest == kViewPullArmed) ImGui::TextDisabled("Armed: floor it in the fourth gear or higher on a long flat road.");
    else if (view.pullTest == kViewPullRunning) ImGui::TextDisabled("Running, step %d of 7: keep it floored.", view.pullPhase + 1);
    else if (view.pullTest == kViewPullDone) ImGui::TextDisabled("Done: the numbers are in Gauges.log.");
    else if (view.pullTest == kViewPullStopped) ImGui::TextDisabled("Stopped: %s. The torque factor is back as it was.", view.pullWhy ? view.pullWhy : "");

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
    InterlockedExchange(&g_pageCapturing, st.capture >= 1 ? (LONG)GetTickCount() : 0);

    ImGui::SeparatorText("Sounds");
    PageLabel("Volume", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::SliderInt("##soundvolume", &s.soundVolume, 0, 100, s.soundVolume > 0 ? "%d" : "off", ImGuiSliderFlags_AlwaysClamp));
    PageTip("How loud the turbo is heard when the pedal comes up quickly under boost: it flutters, longer and louder the more boost there was, "
            "until the pedal goes down again. The oil warning goes by it too. 0 = no sound.");
    ImGui::BeginDisabled(s.soundVolume <= 0);
    PageLabel("Hear it", labelX);
    if (ImGui::Button("Full boost")) act |= kPageHearHigh;
    ImGui::SameLine();
    if (ImGui::Button("Half")) act |= kPageHearMid;
    ImGui::SameLine();
    if (ImGui::Button("Little")) act |= kPageHearLow;
    PageTip("Plays one of the turbo's sounds for a lift of the pedal at that much boost. Each has a few, so press again for another.");
    ImGui::SameLine();
    if (ImGui::Button("Oil warning")) act |= kPageHearOil;
    PageTip("Plays the warning that sounds while the oil is too hot.");
    ImGui::EndDisabled();
    changed(ImGui::Checkbox("Sounds only", &s.soundsOnly));
    PageTip("Plays the sounds and draws no cluster.");

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
    if (s.probe)
    {
        // a test of the write into the game's memory: the points go to the driven truck's engine on the mod's next pass
        static const char *testTip = "A test of the write into the game. Adds to the driven truck's engine damage at once, whatever the switch under "
                                     "Engine damage says. Gauges.log has the numbers.";
        if (ImGui::Button("Add 1 engine damage###test1")) DamageTestAsk(1);
        PageTip(testTip);
        ImGui::SameLine();
        if (ImGui::Button("Add 10###test10")) DamageTestAsk(10);
        PageTip(testTip);
    }

    ImGui::Separator();
    if (ImGui::Button("Defaults"))
    {
        st.capture = -1;
        act |= kPageDefaults;
    }
    PageTip("Every setting back to its default. The truck you drive gives up its own scales; other kinds of truck keep theirs.");
    ImGui::SameLine();
    if (ImGui::Button("Read the ini again")) act |= kPageReload;
    ImGui::TextDisabled("Changes count at once and go into Gauges.ini, a truck's or a gearbox's own into its file in Gauges. Version " GAUGES_VERSION ".");
    return act;
}
