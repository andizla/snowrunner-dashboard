// The game's own TruckType of the driven truck (BRIEF_scout_by_truck_type.md).
//
// Every truck file names its role in the attribute TruckType: SCOUT, OFFROAD, HIGHWAY, HEAVY_DUTY or HEAVY. The game
// keeps it as a text in its list of truck records, one record a truck file, and "Undefined" where the file has none.
// SCOUT tells a scout better than the weight does, so it gives the scout class whatever the truck weighs. The other
// four only say "no scout": truck against heavy stays the weight's (ClassByType in models.h).
//
// The route: the truck's name is the standard text at model + 0x588. The list's begin and end are two pointers found
// by the game's own lookup (TruckTypePlaceFind in game_code.h), which also gives the record's size; the parse of the
// attribute gives the text's place in the record. The list is walked for the record whose name is the truck's.
//
// Nothing here reads the game by itself: the mod's thread hands in its TruckMemory, the tests a made-up one. Nothing
// is ever written. Every step can fail, and each failure leaves the text empty with a reason: the weight then decides
// as before.
#pragma once
#include "game_code.h"
#include "truck.h"
#include <stdint.h>
#include <string.h>

static const uint64_t kTypeRecordsMost = 4096; // more records than this between the list's ends: not walked
static const int kTypeNameMost = 255;          // the longest truck name followed
enum { kTypeTextRoom = 16 };                   // a TruckType text of 16 characters or more sits behind a pointer: not followed

struct TruckTypeRead
{
    char name[kTypeNameMost + 1]; // the truck's name as the model holds it, "" where it was not read
    char text[kTypeTextRoom];     // the TruckType text of its record, "" where it was not read
    bool found;                   // a record with that name was found and its text read
    int records;                  // the records the list held
    const char *why;              // why not, "" where found
};

// The standard text at `at` with 1 to kTypeNameMost characters: in place while its room is under 16, else behind its
// pointer, which is followed only then.
inline bool TruckTypeName(const TruckMemory &m, uint64_t at, char (&out)[kTypeNameMost + 1], size_t &length)
{
    uint8_t text[0x20];
    uint64_t chars = 0, size = 0, room = 0;
    out[0] = 0;
    length = 0;
    if (!m.get(at, text)) return false;
    memcpy(&chars, text, 8);
    memcpy(&size, text + 0x10, 8);
    memcpy(&room, text + 0x18, 8);
    if (size < 1 || size > (uint64_t)kTypeNameMost || room < size) return false;
    if (room < 16) memcpy(out, text, (size_t)size);
    else if (chars < 0x10000 || !m.peek(chars, out, (size_t)size)) return false;
    out[size] = 0;
    length = (size_t)size;
    return true;
}

// Whether the record at `at` has the name `name` of `length` characters. A record's name is the engine's own text: its
// length at + 0, a flag at + 4 (1: the characters in place at + 8, fewer than 16; 0: a pointer to them at + 8).
// Compared by the length first, so the characters of most records are never read. False in `read` where a read failed.
inline bool TruckTypeRecordIs(const TruckMemory &m, uint64_t at, const char *name, size_t length, bool &read)
{
    uint8_t head[0x18];
    int32_t size = 0, flag = 0;
    read = false;
    if (!m.get(at, head)) return false;
    memcpy(&size, head, 4);
    memcpy(&flag, head + 4, 4);
    read = true;
    if (size < 0 || (size_t)size != length) return false;
    if (flag == 1) return length < 16 && !memcmp(head + 8, name, length);
    if (flag != 0) return false;
    uint64_t chars = 0;
    char own[kTypeNameMost + 1];
    memcpy(&chars, head + 8, 8);
    if (chars < 0x10000 || !m.peek(chars, own, length))
    {
        read = false;
        return false;
    }
    return !memcmp(own, name, length);
}

// The TruckType text of the truck whose model is at `model`. False with a reason in out.why where any step failed.
inline bool TruckTypeLook(const TruckMemory &m, const TruckTypePlace &place, uint64_t model, TruckTypeRead &out)
{
    out = TruckTypeRead{};
    out.why = "";
    size_t length = 0;
    if (!place.found)
    {
        out.why = "the list of truck records was not found in this game version";
        return false;
    }
    if (!TruckTypeName(m, model + 0x588, out.name, length))
    {
        out.why = "the name of the truck was not read";
        return false;
    }
    uint64_t begin = 0, end = 0;
    if (!m.get(m.base + place.list, begin) || !m.get(m.base + place.list + 8, end) || begin < 0x10000)
    {
        out.why = "the list of truck records was not read";
        return false;
    }
    if (end < begin)
    {
        out.why = "the list of truck records begins after its end";
        return false;
    }
    const uint64_t count = (end - begin) / place.size;
    if (count > kTypeRecordsMost)
    {
        out.why = "the list of truck records is too long";
        return false;
    }
    out.records = (int)count;
    for (uint64_t i = 0; i < count; i++)
    {
        const uint64_t record = begin + i * place.size;
        bool read = false;
        if (!TruckTypeRecordIs(m, record, out.name, length, read))
        {
            if (read) continue;
            out.why = "a truck record was not read";
            return false;
        }
        uint8_t text[0x20];
        uint64_t size = 0, room = 0;
        if (!m.get(record + place.text, text))
        {
            out.why = "the TruckType of the record was not read";
            return false;
        }
        memcpy(&size, text + 0x10, 8);
        memcpy(&room, text + 0x18, 8);
        if (size >= (uint64_t)kTypeTextRoom || room >= 16)
        {
            out.why = "the TruckType of the record is too long";
            return false;
        }
        memcpy(out.text, text, (size_t)size);
        out.text[size] = 0;
        out.found = true;
        return true;
    }
    out.why = "no truck record has the name";
    return false;
}
