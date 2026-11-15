// tune_page.h: the Tuning tab's body, drawn from the blocks the mods publish (ng_tune.h). The mods in their order, each
// under its name, its figures under their group headings, one Reset a group. Every text of a block is drawn as text,
// never as a format. A figure is asked of the mod (NgTuneWant) and shown as the mod has it in force: the tool keeps no
// figure of its own beyond the slider in the hand.
#pragma once
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "imgui.h"
#include "ng_tune.h"

struct TuneShown
{
    uint8_t *block;
    const NgTuneSeen *seen;
    int n; // the block's number, for the widgets' ids
};
enum { kTuneAskNone, kTuneAskKey };

// A key the tool may have: a keyboard key, not a mouse button, not Escape (it leaves the capture), not the menu's own F2.
inline bool TuneKeyOk(int vk) { return vk >= 8 && vk < 255 && vk != VK_ESCAPE && vk != VK_F2; }

// ---- NextGenTuning.ini (BRIEF_tester_1114.md, part 191) ----
// The keys of the tool's ini, their defaults and their comments: the one table the tool reads by (tuning.cpp gives each
// key's default from here) and writes its first file from, so the two never differ.
struct TuneIniKey
{
    const char *section, *key;
    int def;
    const char *comment;
};
static const TuneIniKey kTuneIni[] = {
    { "Tuning", "Key", VK_F1, "the key that opens and closes the Tuning tab, as a key code (112 is F1). The tab can set it" },
};
static const int kTuneIniCount = (int)(sizeof kTuneIni / sizeof kTuneIni[0]);
inline int TuneIniDefault(const char *key)
{
    for (int i = 0; i < kTuneIniCount; i++)
        if (!strcmp(kTuneIni[i].key, key)) return kTuneIni[i].def;
    return 0;
}
// The text of a first file: a head line, then each section once with its keys, each key under its comment. Returns the
// characters written, 0 where out is too small.
inline size_t TuneIniText(char *out, size_t size)
{
    if (!out || !size) return 0;
    size_t used = 0;
    bool fits = true;
    auto put = [&](const char *a, const char *b, const char *c) {
        const int w = fits ? _snprintf_s(out + used, size - used, _TRUNCATE, "%s%s%s\r\n", a, b, c) : -1;
        if (w < 0) fits = false;
        else used += (size_t)w;
    };
    put("; NextGenTuning.ini: the Next Gen Tuning tool. Written once with the defaults; the tool reads it at each start.", "", "");
    const char *last = "";
    for (int i = 0; i < kTuneIniCount && fits; i++)
    {
        const TuneIniKey &k = kTuneIni[i];
        char section[64], comment[256], value[64];
        _snprintf_s(section, sizeof section, _TRUNCATE, "[%s]", k.section);
        _snprintf_s(comment, sizeof comment, _TRUNCATE, "; %s: %s.", k.key, k.comment);
        _snprintf_s(value, sizeof value, _TRUNCATE, "%s=%d", k.key, k.def);
        if (strcmp(last, k.section)) put(section, "", "");
        last = k.section;
        put(comment, "", "");
        put(value, "", "");
    }
    if (!fits) return out[0] = 0, (size_t)0;
    return used;
}
// Written once: at a start that finds no file, never over one that is there, and whole or not at all (the text to a
// file of its own beside it, then moved to the name, which fails where a file has come there since).
enum { kTuneIniThere, kTuneIniWritten, kTuneIniFailed };
inline int TuneIniFirst(const wchar_t *ini)
{
    if (GetFileAttributesW(ini) != INVALID_FILE_ATTRIBUTES) return kTuneIniThere;
    char text[2048];
    const size_t n = TuneIniText(text, sizeof text);
    if (!n) return kTuneIniFailed;
    wchar_t part[MAX_PATH + 8];
    if (_snwprintf_s(part, _countof(part), _TRUNCATE, L"%s.new", ini) < 0) return kTuneIniFailed;
    const HANDLE f = CreateFileW(part, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return kTuneIniFailed;
    DWORD wrote = 0;
    const bool whole = WriteFile(f, text, (DWORD)n, &wrote, nullptr) && wrote == (DWORD)n && FlushFileBuffers(f);
    CloseHandle(f);
    if (whole && MoveFileExW(part, ini, MOVEFILE_WRITE_THROUGH)) return kTuneIniWritten;
    DeleteFileW(part); // the tool's own part file, never the ini
    return GetFileAttributesW(ini) != INVALID_FILE_ATTRIBUTES ? kTuneIniThere : kTuneIniFailed;
}

// What the tool's key does: with the menu closed it opens it on the Tuning tab, with the menu open on another tab it
// goes to the Tuning tab, and on the Tuning tab it closes the menu.
enum { kTuneKeyOpens, kTuneKeyGoes, kTuneKeyCloses };
inline int TuneKeyDoes(bool menuOpen, bool tuningTabShown) { return !menuOpen ? kTuneKeyOpens : tuningTabShown ? kTuneKeyCloses : kTuneKeyGoes; }
inline void TuneKeyName(int vk, char *out, size_t size)
{
    if (vk >= VK_F1 && vk <= VK_F24)
    {
        snprintf(out, size, "F%d", vk - VK_F1 + 1);
        return;
    }
    UINT scan = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
    if (vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN)
        scan |= 0x100; // the extended keys
    char name[24] = "";
    if (scan && GetKeyNameTextA((LONG)(scan << 16), name, sizeof name) > 0) snprintf(out, size, "%s", name);
    else snprintf(out, size, "Key %d", vk);
}
// The order the mods are drawn in: by the order each gives, then by block number.
inline void TuneOrder(const TuneShown *shown, int count, int *order)
{
    for (int i = 0; i < count; i++) order[i] = i;
    for (int i = 1; i < count; i++)
        for (int k = i; k > 0; k--)
        {
            const TuneShown &a = shown[order[k - 1]], &b = shown[order[k]];
            if (a.seen->order < b.seen->order || (a.seen->order == b.seen->order && a.n < b.n)) break;
            const int t = order[k];
            order[k] = order[k - 1], order[k - 1] = t;
        }
}
// ---- the log of what was changed on the tab (BRIEF_drive_1112_findings.md part 4, S2) ----
// One line a change the tool asked for: the figure, its value before and after. The line goes out when the figure has
// stood still for a second, so a drag gives one line with its last value and a figure never more than one a second.
// A value that moves without the tool asking (the mod loaded another truck's figures) gives no line and is taken as
// the new "before". A Reset gives one line for the group and none for its figures.
static const uint64_t kTuneStillMs = 1000;
struct TuneTrack
{
    bool known, asked;  // seen before; the tool asked for a value since the last line
    float said, last;   // the value the last line ended on (or the first seen); the value at the last look
    uint64_t movedAt;   // when it last moved or was asked
};
inline void TuneTrackAsk(TuneTrack &t, uint64_t now) { t.asked = true, t.movedAt = now; }
inline void TuneTrackQuiet(TuneTrack &t, uint64_t now) { t.asked = false, t.movedAt = now; }
// One look at a figure's value in force. True with before where a line is due.
inline bool TuneTrackStep(TuneTrack &t, float value, uint64_t now, float &before)
{
    if (!t.known)
    {
        t = TuneTrack{ true, t.asked, value, value, t.asked ? t.movedAt : now };
        return false;
    }
    if (value != t.last) t.last = value, t.movedAt = now;
    if (now - t.movedAt < kTuneStillMs) return false;
    const bool due = t.asked && value != t.said;
    before = t.said;
    t.said = value;
    t.asked = false;
    return due;
}
inline TuneTrack g_tuneTrack[kNgTuneBlocks][kNgTuneMost] = {};
typedef void (*TuneSay)(const char *line);
// The line of a change: a switch as on and off, a slider with its own decimals.
inline void TuneChangeLine(char *out, size_t size, const char *mod, const NgTuneSeenEntry &e, float before, float after)
{
    if (e.kind == kNgTuneSwitch)
        snprintf(out, size, "%s: %s%s%s: %s, was %s", mod[0] ? mod : "a mod", e.group, e.group[0] ? " / " : "", e.name, after >= 0.5f ? "on" : "off", before >= 0.5f ? "on" : "off");
    else
        snprintf(out, size, "%s: %s%s%s: %.*f, was %.*f", mod[0] ? mod : "a mod", e.group, e.group[0] ? " / " : "", e.name, e.decimals, after, e.decimals, before);
}

// Reset: each slider and switch of one group asked for its default. Returns how many were asked.
inline int TuneReset(const TuneShown &s, const char *group, uint64_t now)
{
    int asked = 0;
    for (int i = 0; i < s.seen->count; i++)
    {
        const NgTuneSeenEntry &e = s.seen->e[i];
        if (strcmp(e.group, group) || (e.kind != kNgTuneSlider && e.kind != kNgTuneSwitch)) continue;
        if (NgTuneWant(s.block, *s.seen, e, e.def, now)) asked++;
    }
    return asked;
}

inline int TunePageDraw(const TuneShown *shown, int count, uint64_t now, const char *keyName, bool capturing, TuneSay say = nullptr)
{
    int asked = kTuneAskNone;
    ImGui::TextUnformatted("Figures the mods publish for tuning. A change acts at once and the mod saves it.");
    ImGui::TextUnformatted("Key");
    ImGui::SameLine();
    if (ImGui::Button(capturing ? "Press a key" : keyName, ImVec2(140.0f, 0.0f))) asked = kTuneAskKey;
    ImGui::SetItemTooltip("%s", "Opens and closes this tab. Click, then press the key to use. While the menu is open the game gets no keys, so drive with a pad or a wheel.");
    if (!count)
    {
        ImGui::Spacing();
        ImGui::TextUnformatted("No mod in the game publishes tuning figures.");
        return asked;
    }
    int order[kNgTuneBlocks];
    TuneOrder(shown, count > kNgTuneBlocks ? kNgTuneBlocks : count, order);
    for (int m = 0; m < count && m < kNgTuneBlocks; m++)
    {
        const TuneShown &s = shown[order[m]];
        const NgTuneSeen &seen = *s.seen;
        ImGui::PushID(s.n);
        ImGui::Spacing();
        ImGui::SeparatorText(seen.mod[0] ? seen.mod : "A mod");
        const bool stale = NgTuneStale(seen.tick, now);
        if (stale) ImGui::TextUnformatted("This mod does not answer.");
        TuneTrack *track = s.n >= 0 && s.n < kNgTuneBlocks ? g_tuneTrack[s.n] : nullptr;
        const auto tracked = [&](const NgTuneSeenEntry &entry) -> TuneTrack * { return track && entry.index >= 0 && entry.index < kNgTuneMost ? &track[entry.index] : nullptr; };
        ImGui::BeginDisabled(stale);
        for (int i = 0; i < seen.count; i++)
        {
            const NgTuneSeenEntry &e = seen.e[i];
            const bool first = i == 0 || strcmp(seen.e[i - 1].group, e.group), last = i + 1 == seen.count || strcmp(seen.e[i + 1].group, e.group);
            if (first && e.group[0]) ImGui::TextDisabled("%s", e.group);
            ImGui::PushID(e.index);
            const bool off = (e.flags & kNgTuneDisabled) != 0;
            ImGui::BeginDisabled(off);
            if (e.kind == kNgTuneSlider)
            {
                char format[8];
                snprintf(format, sizeof format, "%%.%df", e.decimals);
                float v = e.value;
                ImGui::SetNextItemWidth(280.0f);
                if (ImGui::SliderFloat(e.name, &v, e.least, e.most, format, ImGuiSliderFlags_AlwaysClamp) && NgTuneWant(s.block, seen, e, v, now))
                    if (TuneTrack *t = tracked(e)) TuneTrackAsk(*t, now);
            }
            else if (e.kind == kNgTuneSwitch)
            {
                bool on = e.value >= 0.5f;
                if (ImGui::Checkbox(e.name, &on) && NgTuneWant(s.block, seen, e, on ? 1.0f : 0.0f, now))
                    if (TuneTrack *t = tracked(e)) TuneTrackAsk(*t, now);
            }
            else if (e.kind == kNgTuneButton)
            {
                if (ImGui::Button(e.name) && NgTuneWant(s.block, seen, e, 0.0f, now) && say)
                {
                    char line[200];
                    snprintf(line, sizeof line, "%s: %s%s%s pressed", seen.mod[0] ? seen.mod : "a mod", e.group, e.group[0] ? " / " : "", e.name);
                    say(line);
                }
            }
            else
            {
                char line[160];
                if (e.text[0]) snprintf(line, sizeof line, "%s: %s", e.name, e.text);
                else snprintf(line, sizeof line, "%s: %.*f", e.name, e.decimals, e.value);
                ImGui::TextUnformatted(line);
            }
            if (e.tip[0]) ImGui::SetItemTooltip("%s", e.tip);
            ImGui::EndDisabled();
            if (off && e.kind != kNgTuneLine && e.text[0])
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(e.text);
            }
            ImGui::PopID();
            if (last)
            {
                bool any = false;
                for (int k = 0; k <= i; k++) any = any || (!strcmp(seen.e[k].group, e.group) && (seen.e[k].kind == kNgTuneSlider || seen.e[k].kind == kNgTuneSwitch));
                if (any)
                {
                    ImGui::PushID(e.group);
                    if (ImGui::Button("Reset"))
                    {
                        const int resetAsked = TuneReset(s, e.group, now);
                        // one line for the group, none for its figures
                        for (int k = 0; k < seen.count; k++)
                            if (!strcmp(seen.e[k].group, e.group))
                                if (TuneTrack *t = tracked(seen.e[k])) TuneTrackQuiet(*t, now);
                        if (say)
                        {
                            char line[200];
                            snprintf(line, sizeof line, "%s: Reset of %s: %d figures asked for their defaults", seen.mod[0] ? seen.mod : "a mod", e.group[0] ? e.group : "its figures", resetAsked);
                            say(line);
                        }
                    }
                    ImGui::SetItemTooltip("%s", "Puts this block back to its defaults.");
                    ImGui::PopID();
                }
                ImGui::Spacing();
            }
        }
        ImGui::EndDisabled();
        ImGui::PopID();
        // the changes that have stood still for a second
        for (int i = 0; i < seen.count; i++)
        {
            const NgTuneSeenEntry &e = seen.e[i];
            if (e.kind != kNgTuneSlider && e.kind != kNgTuneSwitch) continue;
            TuneTrack *t = tracked(e);
            float before = 0.0f;
            if (t && !stale && TuneTrackStep(*t, e.value, now, before) && say)
            {
                char line[240];
                TuneChangeLine(line, sizeof line, seen.mod, e, before, e.value);
                say(line);
            }
        }
    }
    return asked;
}
