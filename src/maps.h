// maps.h: the game's maps and regions by the game's own names, and the map it loaded last as its save records it.
// The game names a map level_<region>_<nn>, some with more after it (level_us_01_04_new, level_ru_02_01_crop), and
// its region is the part after level_ (us_01). The English names are the game's own, from its texts
// (strings_english.str in initial.pak): a map's under US_01_01_NAME or LEVEL_US_03_01_NAME, a region's under US_01
// ("Michigan, USA"), here without the country. The save (CompleteSave.cfg) names the map loaded last in
// "lastLoadedLevel". gauges.cpp finds the save and looks up the outside temperature by these names.
#pragma once
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <math.h>

// A region: its id, its name, and its first outside temperature, a guess to be set in Gauges.ini.
struct MapRegion { const char *id, *name; float ambientC; };
static const MapRegion kMapRegions[] = {
    { "us_01", "Michigan", 12.0f }, { "us_02", "Alaska", -12.0f }, { "us_03", "Wisconsin", 8.0f }, { "us_04", "Yukon", -15.0f },
    { "us_06", "Maine", 10.0f }, { "us_07", "Tennessee", 20.0f }, { "us_09", "Ontario", 5.0f }, { "us_10", "British Columbia", 3.0f },
    { "us_11", "Scandinavia", -4.0f }, { "us_12", "North Carolina", 22.0f }, { "us_14", "Austria", 8.0f }, { "us_15", "Quebec", 0.0f },
    { "us_16", "Washington", 10.0f }, { "us_18", "Chiapas", 26.0f }, { "us_19", "Switzerland", -10.0f }, { "ru_02", "Taymyr", -20.0f }, { "ru_03", "Kola Peninsula", -10.0f },
    { "ru_04", "Amur", 2.0f }, { "ru_05", "Don", 16.0f }, { "ru_08", "Belozersk Glades", 18.0f }, { "ru_13", "Almaty Region", 24.0f },
    { "ru_17", "Zurdania", 22.0f } };

// A map: its id without what follows its number, and its name (UTF-8, as the game's texts have it: Grüntal Hollow).
// Wisconsin's second map has its name under its id alone, without _NAME. Switzerland (us_19) came with the game's
// update of 20 October 2026, Season 19.
struct MapLevel { const char *id, *name; };
static const MapLevel kMapLevels[] = {
    { "level_us_01_01", "Black River" }, { "level_us_01_02", "Smithville Dam" }, { "level_us_01_03", "Island Lake" },
    { "level_us_01_04", "Drummond Island" }, { "level_us_02_01", "North Port" }, { "level_us_02_02", "Mountain River" },
    { "level_us_02_03", "White Valley" }, { "level_us_02_04", "Pedro Bay" }, { "level_us_03_01", "Black Badger Lake" }, { "level_us_03_02", "Grainwoods River" },
    { "level_us_04_01", "Flooded Foothills" }, { "level_us_04_02", "Big Salmon Peak" }, { "level_us_06_01", "The Lowland" },
    { "level_us_06_02", "Yellowrock National Forest" }, { "level_us_07_01", "Burning Mill" }, { "level_us_09_01", "The Albany River" },
    { "level_us_09_02", "Burned Forest" }, { "level_us_10_01", "Duncan Bay" }, { "level_us_10_02", "North Peak National Park" },
    { "level_us_11_01", "Mountain Ridge" }, { "level_us_11_02", "By the Lake" }, { "level_us_12_01", "Pineline Bay" },
    { "level_us_12_02", "Reactive Zone" }, { "level_us_12_03", "Flatland" }, { "level_us_12_04", "Oviro Hills" },
    { "level_us_14_01", "Unterfeld" }, { "level_us_14_02", "Foothills" }, { "level_us_15_01", "Harbor" }, { "level_us_15_02", "Lake Ribiere" },
    { "level_us_16_01", "Solace Bay" }, { "level_us_16_02", "Selten River National Park" }, { "level_us_16_03", "Selten River Mountains" },
    { "level_us_18_01", "Costa Atardecer" }, { "level_us_18_02", "Sierra Fuerte" }, { "level_us_19_01", "Grüntal Hollow" },
    { "level_us_19_02", "Schneetal" }, { "level_us_19_03", "Eiswandspitze" }, { "level_ru_02_01", "Quarry" },
    { "level_ru_02_02", "Drowned Lands" }, { "level_ru_02_03", "Zimnegorsk" }, { "level_ru_02_04", "Rift" }, { "level_ru_03_01", "Lake Kovd" },
    { "level_ru_03_02", "Imandra" }, { "level_ru_04_01", "Urska River" }, { "level_ru_04_02", "Cosmodrome" },
    { "level_ru_04_03", "Northern Aegis Installation" }, { "level_ru_04_04", "Chernokamensk" }, { "level_ru_05_01", "Factory Grounds" },
    { "level_ru_05_02", "Antonovskiy Nature Reserve" }, { "level_ru_08_01", "Crossroads" }, { "level_ru_08_02", "The Institute" },
    { "level_ru_08_03", "Heartlands" }, { "level_ru_08_04", "\"HarvestCorp\"" }, { "level_ru_13_01", "Zherbai Quarries" },
    { "level_ru_17_01", "Zurdanian Valley" }, { "level_ru_17_02", "The Valley of Falling Rocks" } };

// A map's id as the game writes one: level_ and then lower case letters, digits and _, 64 characters at the most.
inline bool MapIdValid(const char *id)
{
    if (strncmp(id, "level_", 6) || !id[6]) return false;
    size_t n = 0;
    for (const char *p = id; *p; p++, n++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return false;
    return n < 64;
}

// The length of a map's id up to the end of its number, level_us_01_04 of level_us_01_04_new: 14, or 0 when it is
// not of that form (a mod's map).
inline size_t MapIdCore(const char *id)
{
    auto letter = [](char c) { return c >= 'a' && c <= 'z'; };
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (strncmp(id, "level_", 6) || strlen(id) < 14) return 0;
    const char *p = id + 6;
    if (!letter(p[0]) || !letter(p[1]) || p[2] != '_' || !digit(p[3]) || !digit(p[4]) || p[5] != '_' || !digit(p[6]) || !digit(p[7])) return 0;
    return p[8] == 0 || p[8] == '_' ? 14 : 0;
}

// The region of a map, or nullptr for one the game's texts do not know.
inline const MapRegion *MapRegionOf(const char *id)
{
    if (!MapIdCore(id)) return nullptr;
    for (const auto &r : kMapRegions)
        if (!strncmp(id + 6, r.id, 5)) return &r;
    return nullptr;
}

// The English name of a map, or nullptr for one the game's texts do not name.
inline const char *MapNameOf(const char *id)
{
    const size_t core = MapIdCore(id);
    if (!core) return nullptr;
    for (const auto &m : kMapLevels)
        if (!strncmp(id, m.id, core) && strlen(m.id) == core) return m.name;
    return nullptr;
}

// A map in words for the log and the tab: "Zurdanian Valley, Zurdania", its id and its region where the texts give
// it no name, and its id alone for a map of a mod.
inline void MapTitle(const char *id, char *out, size_t size)
{
    const char *name = MapNameOf(id);
    const MapRegion *region = MapRegionOf(id);
    if (name && region) snprintf(out, size, "%s, %s", name, region->name);
    else if (region) snprintf(out, size, "%s, %s", id, region->name);
    else snprintf(out, size, "%s", id);
}

// The map the game loaded last, from the text of its save: the value of "lastLoadedLevel". False when the text has
// none or its value is no map's id (a save being written as it was read).
inline bool MapFromSave(const char *text, size_t length, char *out, size_t size)
{
    static const char key[] = "\"lastLoadedLevel\"";
    const size_t keyLength = sizeof key - 1;
    for (size_t i = 0; i + keyLength < length; i++)
    {
        if (memcmp(text + i, key, keyLength)) continue;
        size_t at = i + keyLength;
        while (at < length && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) at++;
        if (at >= length || text[at++] != ':') return false;
        while (at < length && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) at++;
        if (at >= length || text[at++] != '"') return false;
        char id[64];
        size_t n = 0;
        while (at < length && text[at] != '"' && n + 1 < sizeof id) id[n++] = text[at++];
        if (at >= length || text[at] != '"') return false;
        id[n] = 0;
        if (!MapIdValid(id) || n + 1 > size) return false;
        memcpy(out, id, n + 1);
        return true;
    }
    return false;
}

// Whether the text of a save is an empty slot: its "lastLoadedLevel" is "", a slot never played on a map. The game
// still writes such a slot a few seconds after the one played (found in the game, 2026-10-25). A save cut short as it
// is written is not one.
inline bool SaveSlotEmpty(const char *text, size_t length)
{
    static const char key[] = "\"lastLoadedLevel\"";
    const size_t keyLength = sizeof key - 1;
    for (size_t i = 0; i + keyLength < length; i++)
    {
        if (memcmp(text + i, key, keyLength)) continue;
        size_t at = i + keyLength;
        while (at < length && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) at++;
        if (at >= length || text[at++] != ':') return false;
        while (at < length && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) at++;
        return at + 1 < length && text[at] == '"' && text[at + 1] == '"';
    }
    return false;
}

// ---- the outside temperature over the game's day ----
// A map's air is coldest just before sunrise and warmest in the afternoon. A section gives the day's warmest and the
// night's coldest (DayC and NightC), or only the day's mean (AmbientC), which then swings kDaySwing between them.
static const float kDaySwing = 10.0f;        // C from the night's coldest to the day's warmest where a section gives only the mean
static const float kColdestHour = 5.0f, kWarmestHour = 15.0f;

// The outside temperature at hour (0 to 24) of the game's day: from night at kColdestHour up to day at kWarmestHour,
// and down again through the evening and the night, along half a cosine each way.
inline float DayNightC(float day, float night, float hour)
{
    float h = fmodf(hour, 24.0f);
    if (h < 0.0f) h += 24.0f;
    const float rise = kWarmestHour - kColdestHour, fall = 24.0f - rise;
    float share; // 0 at the coldest, 1 at the warmest
    if (h >= kColdestHour && h <= kWarmestHour) share = 0.5f - 0.5f * cosf(3.14159265f * (h - kColdestHour) / rise);
    else
    {
        const float since = h > kWarmestHour ? h - kWarmestHour : h + 24.0f - kWarmestHour;
        share = 0.5f + 0.5f * cosf(3.14159265f * since / fall);
    }
    return night + (day - night) * share;
}
