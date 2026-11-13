// air_keep.h: the brake air each owned truck is left with (BRIEF_jobs_1114.md, part 165, with the Reviewer's addendum
// and the lead's rulings of 2026-11-14 00:55). A truck keeps its air while the game runs, by its id with its
// take-overs, and between runs as AirBar in its [Odometer:<id>] of the kind's file (gauges.cpp writes it at the
// odometer's own moments). With the box Empty tanks at a start a truck first driven in a run starts at 0 bar.
// Here only what a pass decides, so the tests can drive it without a game. After models.h (AirModel).
#pragma once
#include <vector>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>

// Where the bar a truck starts its drive with comes from: the log's words at each truck change.
enum { kAirFromFull, kAirFromRun, kAirFromFile, kAirFromEmpty, kAirFromNoId };
inline const wchar_t *AirFromWords(int from)
{
    return from == kAirFromRun ? L"as it was left in this run" : from == kAirFromFile ? L"from its file" : from == kAirFromEmpty ? L"empty by the box Empty tanks at a start"
           : from == kAirFromNoId ? L"full, its id not read" : L"full, a truck not driven before";
}

// The trucks driven in this run, by id: has, the bar it was left with (a truck with air brakes). A take-over or a
// match makes the old id's entry the new id's, so the same truck under a new id is no first drive (must 3).
struct AirRun { char id[40]; bool has; float bar; };
static const size_t kAirRunMost = 512;

inline AirRun *AirRunFind(std::vector<AirRun> &list, const char *id)
{
    if (!id || !id[0]) return nullptr;
    for (auto &e : list)
        if (!strcmp(e.id, id)) return &e;
    return nullptr;
}
// The id's entry, made where it has none. None for no id, and none past kAirRunMost: such a truck starts full.
inline AirRun *AirRunAdd(std::vector<AirRun> &list, const char *id)
{
    if (AirRun *e = AirRunFind(list, id)) return e;
    if (!id || !id[0] || list.size() >= kAirRunMost) return nullptr;
    AirRun e = {};
    strncpy_s(e.id, id, _TRUNCATE);
    list.push_back(e);
    return &list.back();
}
// from's entry becomes to's, over one to had. Nothing where from has none.
inline void AirRunRename(std::vector<AirRun> &list, const char *from, const char *to)
{
    if (!from || !to || !from[0] || !to[0] || !strcmp(from, to) || !AirRunFind(list, from)) return;
    for (size_t i = 0; i < list.size(); i++)
        if (!strcmp(list[i].id, to))
        {
            list.erase(list.begin() + (ptrdiff_t)i);
            break;
        }
    strncpy_s(AirRunFind(list, from)->id, to, _TRUNCATE);
}

// A kept bar as the file has it: a number from 0 to most (the file's two decimals of most itself count as most).
// Anything else, text or a number out of range, is no figure (the truck starts full): a hand edit must not make a
// number the model cannot hold (must 6).
inline bool AirBarRanged(const wchar_t *text, float most, float &bar)
{
    if (!text || !text[0]) return false;
    wchar_t *end = nullptr;
    const double v = wcstod(text, &end);
    while (end && (*end == L' ' || *end == L'\t')) end++;
    if (!end || end == text || *end || !isfinite(v) || v < 0.0 || v > (double)most + 0.005) return false;
    bar = fminf((float)v, most);
    return true;
}

// The bar a truck starts with once its id has decided. idRead: it has an id. run: its entry where it was driven in this
// run (with its take-overs). A truck with no id is never emptied and keeps nothing (must 2). One driven before in this
// run has what it was left with, or full tanks where it had no air brakes then: never emptied (must 5). A first drive
// is empty with the box on, whatever its file says, else its file's figure, else full.
inline int AirAtDecision(bool idRead, const AirRun *run, bool fileKnown, float fileBar, bool emptyAtStart, float full, float &bar)
{
    bar = full;
    if (!idRead) return kAirFromNoId;
    if (run)
    {
        if (!run->has) return kAirFromFull;
        bar = run->bar;
        return kAirFromRun;
    }
    if (emptyAtStart)
    {
        bar = 0.0f;
        return kAirFromEmpty;
    }
    if (!fileKnown) return kAirFromFull;
    bar = fileBar;
    return kAirFromFile;
}

// The decided bar into the model, once: on the first pass the model runs for a truck with air brakes after the
// decision. Until the decision the model runs from full, as before. True on the pass it is put in.
inline bool AirStartStep(AirModel &m, bool &pending, bool decided, bool on, float start, float cutIn)
{
    if (!pending || !decided || !on || !m.running) return false;
    m.bar = start;
    m.pumping = start <= cutIn;
    pending = false;
    return true;
}
// Whether the spring brakes are judged on this pass (must 1): not while the truck's air waits for its id's decision or
// for its first step, so there is no hold on a truck that turns out to have its air and no release of one that has
// none. A truck without air brakes is judged as ever: nothing holds it.
inline bool AirJudged(bool decided, bool pending, bool on) { return !on || (decided && !pending); }
