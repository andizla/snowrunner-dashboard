// truck_profile.h: a truck's settings as one file a player can keep, take back and hand on
// (BRIEF_truck_profile.md with the Reviewer's addendum, build 1). The storage stays as it is: a profile is written
// from the truck's, the engine's and the gearbox's files (Export) and goes back into them through the functions an
// edit uses (Import). This header holds what has no file and no game in it: the names, the texts a stranger's file may
// hold, the list the tab shows and what the tab asks of the mod's pass. The file work is gauges.cpp's, on the pass.
//
// A profile is a stranger's text: a friend's file, or one from the net. Nothing of it is ever a format, a path or a
// number out of its range; a text shown is cut and has no control character.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static const int kProfileNameMost = 40;             // characters of the name a player types
static const int kProfileNotesMost = 100;           // characters of the notes shown
static const int kProfileRowsMost = 50;             // files the tab lists; the rest are counted
static const unsigned kProfileFileMost = 64 * 1024; // a file larger than this is no profile and is not read

// A text of a profile as it may be shown or written: no control character, cut to most characters, no space at
// either end. Bytes of 128 and over stay (UTF-8). True where something is left.
inline bool ProfileTextClean(const char *in, char *out, size_t size, int most)
{
    size_t n = 0;
    if (in)
        for (; *in && n + 1 < size && (int)n < most; in++)
        {
            const unsigned char c = (unsigned char)*in;
            if (c < 32 || c == 127) continue;
            if (!n && c == ' ') continue;
            out[n++] = (char)c;
        }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
    return n > 0;
}

// Whether a file's name without its ending is one of the devices Windows keeps: CON, PRN, AUX, NUL, COM1 to COM9 and
// LPT1 to LPT9, whatever the case, and with anything after a dot.
inline bool ProfileDeviceName(const char *name)
{
    char base[8] = {};
    size_t n = 0;
    for (; name[n] && name[n] != '.' && n < 7; n++) base[n] = (char)toupper((unsigned char)name[n]);
    if (name[n] && name[n] != '.') return false; // longer than any of them
    while (n && base[n - 1] == ' ') base[--n] = 0;
    if (!strcmp(base, "CON") || !strcmp(base, "PRN") || !strcmp(base, "AUX") || !strcmp(base, "NUL")) return true;
    return n == 4 && (!strncmp(base, "COM", 3) || !strncmp(base, "LPT", 3)) && base[3] >= '1' && base[3] <= '9';
}

// The name a player typed, as a part of a file's name: what a file's name cannot hold becomes "_" (a stroke "@", as
// the mod's other files have it), no control character, no dot or space at the end, no space at the start, 40
// characters at the most. False, with why (a text for the tab), where nothing is left or it is a device's name.
inline bool ProfileNameClean(const char *typed, char *out, size_t size, const char **why = nullptr)
{
    size_t n = 0;
    if (typed)
        for (; *typed && n + 1 < size && (int)n < kProfileNameMost; typed++)
        {
            const unsigned char c = (unsigned char)*typed;
            if (c < 32 || c == 127) continue;
            if (!n && c == ' ') continue;
            out[n++] = c == '/' ? '@' : strchr("\\:*?\"<>|", c) ? '_' : (char)c;
        }
    while (n && (out[n - 1] == ' ' || out[n - 1] == '.')) n--;
    out[n] = 0;
    if (!n)
    {
        if (why) *why = "Type a name first.";
        return false;
    }
    if (ProfileDeviceName(out))
    {
        if (why) *why = "Windows keeps that name. Type another.";
        return false;
    }
    if (why) *why = "";
    return true;
}

// A profile's file name: "<kind> - <name>.ini", the kind as the mod's files have it.
inline void ProfileFileName(const char *kindFile, const char *name, char *out, size_t size) { snprintf(out, size, "%s - %s.ini", kindFile, name); }

// Whether a file's name ends as a profile may: .ini or .txt, whatever the case.
inline bool ProfileFileEnding(const char *file)
{
    const size_t n = strlen(file);
    return n > 4 && (!_stricmp(file + n - 4, ".ini") || !_stricmp(file + n - 4, ".txt"));
}

// The name a row shows: the file's name without its ending, and without "<kind> - " where it begins with this kind.
inline void ProfileShownName(const char *file, const char *kindFile, char *out, size_t size)
{
    char whole[200];
    strncpy_s(whole, file, _TRUNCATE);
    if (ProfileFileEnding(whole)) whole[strlen(whole) - 4] = 0;
    const size_t k = kindFile ? strlen(kindFile) : 0;
    const char *from = k && !_strnicmp(whole, kindFile, k) && !strncmp(whole + k, " - ", 3) && whole[k + 3] ? whole + k + 3 : whole;
    ProfileTextClean(from, out, size, 60);
}

// One file of the list, as the pass read it. Every text is cleaned and cut.
struct ProfileRow
{
    char file[200];   // the file's name in Gauges\profiles (or in its backup folder), as found
    char name[64];    // what the row shows
    char truck[96], engine[96], gearbox[96], date[20], version[16], notes[104];
    bool valid;       // a profile: not over 64 KB and with a truck named in it
    bool backup;      // from the backup folder
    bool sameKind, sameEngine, sameGearbox; // against the driven truck, when the list was made
    bool otherVersion; // made with another version of the mod
};
// The list and what the tab needs beside it. turn changes with every new list.
struct ProfileList
{
    long turn;
    int count, more;     // rows, and the files beyond them
    bool truck, idRead;  // a truck is driven; its own id is read (so it has a level of its own)
    bool canExport;      // the engine and the gearbox are known too
    char kind[96], kindFile[96];
    char say[480];       // what the last Export or Import did, or why not
    ProfileRow row[kProfileRowsMost];
};

// What the tab asks of the pass. Never a path: a row's file name as the list had it, checked against the folder again.
enum { kProfAskNone, kProfAskLook, kProfAskExport, kProfAskImportTruck, kProfAskImportKind };
struct ProfileAsk
{
    int what;
    char name[kProfileNameMost + 8], notes[kProfileNotesMost + 8]; // Export: as typed
    bool over;                // Export: a file of that name may be written over
    char file[200];           // Import: the row's file
    bool backup;              // ... from the backup folder
    bool engineToo;           // Import: also the engine's own default profile
    bool otherKind;           // Import: the second press for a profile of another kind was made
};

// Whether a name typed is a file the list has already, for the one question before it is written over.
inline bool ProfileNameThere(const ProfileList &list, const char *typed)
{
    char name[64], file[200];
    if (!ProfileNameClean(typed, name, sizeof name)) return false;
    ProfileFileName(list.kindFile, name, file, sizeof file);
    for (int i = 0; i < list.count; i++)
        if (!list.row[i].backup && !_stricmp(list.row[i].file, file)) return true;
    return false;
}

// What an Import took and what it left, for the log's line and the tab's.
struct ProfileTaken
{
    bool susp, air, torque, hold, engine, engineOff, scales, scalesOff, diff, diffOff, cda, airAxles, odo, engineDefault;
    bool engineOther, gearboxOther; // made with another engine or gearbox: that part was left
    bool kindLeft;                  // the first button: the kind's keys were left
    bool truckOver;                 // the second button: this truck has its own in force over a part taken
    int unknown;                    // keys the mod does not know, left out
};
inline void ProfileTakenText(const ProfileTaken &t, char *out, size_t size)
{
    char took[200] = "";
    auto add = [&](bool on, const char *what) {
        if (!on) return;
        if (took[0]) strncat_s(took, ", ", _TRUNCATE);
        strncat_s(took, what, _TRUNCATE);
    };
    add(t.scales, "the class and scales"), add(t.scalesOff, "the plain scales"), add(t.cda, "the drag area"),
        add(t.airAxles, "the air axles"), add(t.odo, "the Counts choice"), add(t.diff || t.diffOff, "the diff types"), add(t.susp, "the suspension"), add(t.air, "Air brakes"), add(t.torque, "Engine torque"),
        add(t.hold, "the gear hold"), add(t.engine, "the engine profile"), add(t.engineOff, "the engine settings out of force"), add(t.engineDefault, "the default of the engine");
    int n = snprintf(out, size, "Taken: %s.", took[0] ? took : "nothing");
    auto more = [&](const char *text) {
        if (n > 0 && (size_t)n < size) n += snprintf(out + n, size - n, " %s", text);
    };
    if (t.kindLeft) more("The settings of the kind were left.");
    if (t.truckOver) more("This truck has settings of its own in force, and those stay.");
    if (t.engineOther) more("Made with another engine: its engine settings were left.");
    if (t.gearboxOther) more("Made with another gearbox: its gear hold was left.");
    more("The tire mode is not taken yet.");
    if (t.unknown && n > 0 && (size_t)n < size) snprintf(out + n, size - n, " %d unknown keys were left out.", t.unknown);
}
