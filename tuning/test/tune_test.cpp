// tune_test.cpp: the tuning block (src\ng_tune.h) and the tab's order and Reset (src\tune_page.h without its drawing),
// offline. A mod's side and the tool's side on one buffer and on a real mapping; the layout as bytes; the soft version
// rule both ways; the hand-over with no lock.
#include <windows.h>
#include <stdio.h>
#include <vector>
#include "ng_tune.h"
#include "tune_page.h"

static int g_failed = 0;
static void Check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failed++;
}
static const NgTuneItem kItems[] = {
    { kNgTuneSlider, "Strength", "Rolling drag", "One figure for every ground.", 0.0f, 300.0f, 100.0f, 1.0f, 0 },
    { kNgTuneSlider, "Paved", "Rolling drag", "Rolling resistance on paved ground.", 0.0f, 0.1f, 0.01f, 0.001f, 3 },
    { kNgTuneLine, "Ground", "Rolling drag", "The ground under the wheels as the mod reads it.", 0.0f, 0.0f, 0.0f, 0.0f, 0 },
    { kNgTuneSwitch, "Mud tuning", "Mud", "A switch.", 0.0f, 1.0f, 0.0f, 1.0f, 0 },
    { kNgTuneButton, "Log bodies", "Mud", "A button.", 0.0f, 0.0f, 0.0f, 0.0f, 0 },
};
static const int kCount = (int)(sizeof kItems / sizeof kItems[0]);

// The first 80 + 288 bytes of a block set up by layout 1's first build (the head and the first entry of kItems under
// the name "Tire Inflation", order 2, after one pass at tick 0x1122334455), kept for the day the layout grows: a block
// of today read by a later build must give these fields.
static const char *const kToday =
    "4e47545501000000500000002001000005000000040000005544332211000000020000005469726520496e666c6174696f6e000000000000000000000000000000000000000000000000000000000000"
    "01000000537472656e67746800000000000000000000000000000000000000000000000000000000000000000000000000000000526f6c6c696e67206472616700000000000000000000000000000000"
    "000000004f6e652066696775726520666f722065766572792067726f756e642e000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000096430000c8420000803f000000000000c84200000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

static std::vector<uint8_t> Hex(const char *text)
{
    std::vector<uint8_t> out;
    int have = -1;
    for (const char *p = text; *p; p++)
    {
        const int v = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10 : -1;
        if (v < 0) continue;
        if (have < 0) have = v;
        else out.push_back((uint8_t)(have * 16 + v)), have = -1;
    }
    return out;
}

int main()
{
    // the layout: the sizes and offsets are asserted in the header; here a block of today against its kept bytes
    std::vector<uint8_t> block(kNgTuneBlockMost, 0xEE);
    {
        const bool made = NgTuneSetUp(block.data(), block.size(), "Tire Inflation", 2, kItems, kCount);
        NgTuneBegin(block.data());
        NgTuneEnd(block.data(), 0x1122334455ull);
        const std::vector<uint8_t> kept = Hex(kToday);
        const bool same = kept.size() == 80 + 288 && memcmp(kept.data(), block.data(), kept.size()) == 0;
        if (!same)
        {
            printf("     the block's first %zu bytes today (kept %zu):\n     ", (size_t)(80 + 288), kept.size());
            for (int i = 0; i < 80 + 288; i++) printf("%02x", block[i]);
            printf("\n");
        }
        const NgTuneHead *h = (const NgTuneHead *)block.data();
        Check(made && same && h->magic == kNgTuneMagic && h->layout == 1 && h->headSize == 80 && h->entrySize == 288 && h->count == (uint32_t)kCount && h->sequence == 4 &&
                  kNgTuneBlockMost == 80 + 64 * 288,
              "tuning block, the layout: a head of 80 bytes and entries of 288, a block of today equal to the bytes kept in the test, the sequence even after a pass");
        Check(!NgTuneSetUp(block.data(), 100, "x", 0, kItems, kCount) && !NgTuneSetUp(nullptr, block.size(), "x", 0, kItems, kCount) &&
                  !NgTuneSetUp(block.data(), block.size(), "x", 0, kItems, 65),
              "tuning block, the set up: no block in memory too small for it, with no memory or with 65 figures");
    }
    // the tool's read of it
    NgTuneSeen seen;
    {
        NgTuneSetUp(block.data(), block.size(), "Tire Inflation", 2, kItems, kCount);
        NgTuneBegin(block.data());
        NgTuneValue(block.data(), 0, 150.0f);
        NgTuneValue(block.data(), 2, 0.0f, "gravel");
        NgTuneValue(block.data(), 1, 0.02f, "Rolling drag is off.", true);
        NgTuneEnd(block.data(), 5000);
        const bool read = NgTuneRead(block.data(), block.size(), seen);
        Check(read && seen.ok && seen.count == kCount && !strcmp(seen.mod, "Tire Inflation") && seen.order == 2 && seen.tick == 5000 && seen.e[0].kind == kNgTuneSlider &&
                  !strcmp(seen.e[0].name, "Strength") && !strcmp(seen.e[0].group, "Rolling drag") && !strcmp(seen.e[0].tip, "One figure for every ground.") &&
                  seen.e[0].least == 0.0f && seen.e[0].most == 300.0f && seen.e[0].def == 100.0f && seen.e[0].step == 1.0f && seen.e[0].value == 150.0f &&
                  seen.e[1].decimals == 3 && (seen.e[1].flags & kNgTuneDisabled) && !strcmp(seen.e[1].text, "Rolling drag is off.") && seen.e[2].kind == kNgTuneLine &&
                  !strcmp(seen.e[2].text, "gravel") && seen.e[3].kind == kNgTuneSwitch && seen.e[4].kind == kNgTuneButton && seen.e[4].index == 4,
              "tuning block, the read: the mod's name, order and tick, each figure's kind, name, group, tip, range, default, step, decimals, value, text and flags");
        // a text that fills its field has no zero at its end in the block: the read cuts it at the field
        NgTuneItem full = kItems[0];
        full.name = "0123456789012345678901234567890123456789012345678901234567890123456789";
        std::vector<uint8_t> b2(kNgTuneBlockMost);
        NgTuneSetUp(b2.data(), b2.size(), "A name of more than forty letters for the head of the block", 1, &full, 1);
        NgTuneSeen s2;
        Check(NgTuneRead(b2.data(), b2.size(), s2) && strlen(s2.e[0].name) == 48 && strlen(s2.mod) == 40 && !memcmp(s2.e[0].name, full.name, 48),
              "tuning block, a text longer than its field: cut at the field on both sides, read with a zero at its end");
    }
    // M3: the soft version rule
    {
        // a later mod: a head 16 longer and entries of 320 bytes, the fields of today where they are
        const size_t head = 96, entry = 320;
        std::vector<uint8_t> later(head + kCount * entry, 0x5A);
        memcpy(later.data(), block.data(), 80);
        for (int i = 0; i < kCount; i++) memcpy(later.data() + head + i * entry, block.data() + 80 + i * 288, 288);
        NgTuneHead *h = (NgTuneHead *)later.data();
        h->headSize = (uint32_t)head, h->entrySize = (uint32_t)entry;
        NgTuneSeen s;
        const bool grown = NgTuneRead(later.data(), later.size(), s) && s.count == kCount && s.e[0].value == 150.0f && !strcmp(s.e[2].text, "gravel") && s.headSize == head &&
                           s.entrySize == entry;
        // the tool's ask lands in the right entry of the grown block
        const bool asked = NgTuneWant(later.data(), s, s.e[3], 1.0f, 5000);
        const NgTuneEntry *third = (const NgTuneEntry *)(later.data() + head + 3 * entry);
        Check(grown && asked && third->wanted == 1.0f && third->wantedCount == 1 && ((const NgTuneEntry *)(later.data() + head + 2 * entry))->wantedCount == 0,
              "tuning block, a later mod beside this tool: a head 16 bytes longer and entries of 320 are read right, and an ask lands in its entry");
        // a kind this tool does not know among known ones
        std::vector<uint8_t> odd = block;
        ((NgTuneEntry *)(odd.data() + 80 + 288))->kind = 9;
        const bool known = NgTuneRead(odd.data(), odd.size(), s) && s.count == kCount - 1 && s.e[0].index == 0 && s.e[1].index == 2 && !strcmp(s.e[1].name, "Ground");
        Check(known, "tuning block, a kind the tool does not know: left out, the others show and keep their places in the block");
        // no group
        std::vector<uint8_t> two = block, many = block, tiny = block, uneven = block, shortHead = block, magic = block;
        ((NgTuneHead *)two.data())->layout = 2;
        ((NgTuneHead *)many.data())->count = 65;
        ((NgTuneHead *)tiny.data())->entrySize = 100;
        ((NgTuneHead *)uneven.data())->entrySize = 292;
        ((NgTuneHead *)shortHead.data())->headSize = 72;
        ((NgTuneHead *)magic.data())->magic = 0;
        NgTuneSeen n1, n2, n3, n4, n5, n6, n7, n8;
        const bool none = !NgTuneRead(two.data(), two.size(), n1) && strstr(n1.why, "another layout") && !NgTuneRead(many.data(), many.size(), n2) && strstr(n2.why, "more entries") &&
                          !NgTuneRead(tiny.data(), tiny.size(), n3) && strstr(n3.why, "entry size") && !NgTuneRead(uneven.data(), uneven.size(), n4) &&
                          !NgTuneRead(shortHead.data(), shortHead.size(), n5) && strstr(n5.why, "head") && !NgTuneRead(magic.data(), magic.size(), n6) &&
                          !n1.ok && !n1.count && !n6.count;
        // a mapping shorter than the block says: no group, and nothing past its end is read (the page after it is not there)
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        uint8_t *pages = (uint8_t *)VirtualAlloc(nullptr, 2 * si.dwPageSize, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(pages, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        const size_t fits = 80 + 288 * (size_t)kCount;
        uint8_t *edge = pages + si.dwPageSize - 400;
        memcpy(edge, block.data(), 400);
        const bool cut = !NgTuneRead(edge, 400, n7) && strstr(n7.why, "smaller than its sizes say") && !NgTuneRead(edge, 40, n8) && strstr(n8.why, "shorter than a head") &&
                         !NgTuneRead(nullptr, 4096, n8) && fits > 400;
        VirtualFree(pages, 0, MEM_RELEASE);
        Check(none && cut, "tuning block, no group: layout 2, a count of 65, an entry size of 100 or 292, a head of 72, no magic, a mapping shorter than the block says "
                           "(nothing is read past its end); each with its reason for the tool's log");
    }
    // M4: the hand-over
    {
        NgTuneSetUp(block.data(), block.size(), "Tire Inflation", 2, kItems, kCount);
        NgTuneBegin(block.data());
        NgTuneEnd(block.data(), 1000);
        NgTuneRead(block.data(), block.size(), seen);
        float held = -1.0f;
        const bool quiet = NgTuneTake(block.data(), 0, held) == 0 && held == -1.0f;
        // wanted past the range: in force the range's end; the count it took follows
        const bool asked = NgTuneWant(block.data(), seen, seen.e[0], 500.0f, 1500);
        const int taken = NgTuneTake(block.data(), 0, held);
        const bool most = asked && taken == 1 && held == 300.0f && NgTuneAt(block.data(), 0)->takenCount == 1 && NgTuneTake(block.data(), 0, held) == 0;
        // the tool holds a typed figure to the range before it writes; the mod holds what it finds once more, and to the step
        NgTuneAt(block.data(), 1)->wanted = 0.0336f;
        InterlockedIncrement(&NgTuneAt(block.data(), 1)->wantedCount);
        float stepped = 0.0f;
        const bool step = NgTuneTake(block.data(), 1, stepped) == 1 && stepped == 0.034f;
        NgTuneAt(block.data(), 1)->wanted = 5.0f;
        InterlockedIncrement(&NgTuneAt(block.data(), 1)->wantedCount);
        const bool modHolds = NgTuneTake(block.data(), 1, stepped) == 1 && stepped == 0.1f;
        // not a number: the tool does not write it; written all the same, the mod drops it and nothing changes
        float nan = NAN, kept = 77.0f;
        const bool toolNot = !NgTuneWant(block.data(), seen, seen.e[0], nan, 1500) && NgTuneAt(block.data(), 0)->wantedCount == 1;
        NgTuneAt(block.data(), 0)->wanted = nan;
        InterlockedIncrement(&NgTuneAt(block.data(), 0)->wantedCount);
        const bool dropped = NgTuneTake(block.data(), 0, kept) == -1 && kept == 77.0f && NgTuneTake(block.data(), 0, kept) == 0;
        // a wanted written between the mod's two reads of the count: taken a pass later, whole
        NgTuneEntry *e = NgTuneAt(block.data(), 0);
        e->wanted = 40.0f;
        InterlockedIncrement(&e->wantedCount);
        const LONG first = e->wantedCount;
        const float half = e->wanted;
        e->wanted = 60.0f; // the tool writes again while the mod is between its reads
        InterlockedIncrement(&e->wantedCount);
        float got = -5.0f;
        const bool notHalf = NgTuneTaken(e, first, half, e->wantedCount, got) == 0 && got == -5.0f && e->takenCount != e->wantedCount;
        const bool later = NgTuneTake(block.data(), 0, got) == 2 && got == 60.0f;
        // a disabled entry takes nothing, from the tool or written all the same; a line takes nothing
        NgTuneBegin(block.data());
        NgTuneValue(block.data(), 1, 0.02f, "Rolling drag is off.", true);
        NgTuneEnd(block.data(), 1000);
        NgTuneRead(block.data(), block.size(), seen);
        const LONG before = NgTuneAt(block.data(), 1)->wantedCount;
        const bool off = !NgTuneWant(block.data(), seen, seen.e[1], 0.05f, 1500) && NgTuneAt(block.data(), 1)->wantedCount == before;
        NgTuneAt(block.data(), 1)->wanted = 0.05f;
        InterlockedIncrement(&NgTuneAt(block.data(), 1)->wantedCount);
        float no = 9.0f;
        const bool offMod = NgTuneTake(block.data(), 1, no) == -1 && no == 9.0f;
        const bool line = !NgTuneWant(block.data(), seen, seen.e[2], 1.0f, 1500);
        // a tick 3 s old: stale, nothing written; at 2 s still fresh
        const bool stale = NgTuneStale(1000, 4000) && !NgTuneStale(1000, 3000) && !NgTuneStale(5000, 1000) && !NgTuneWant(block.data(), seen, seen.e[0], 10.0f, 4000) &&
                           NgTuneWant(block.data(), seen, seen.e[0], 10.0f, 3000);
        // a switch is 0 or 1; a button counts presses
        float sw = -1.0f, press = -1.0f;
        NgTuneWant(block.data(), seen, seen.e[3], 0.7f, 1500);
        const bool isSwitch = NgTuneTake(block.data(), 3, sw) == 1 && sw == 1.0f;
        NgTuneWant(block.data(), seen, seen.e[4], 0.0f, 1500);
        NgTuneWant(block.data(), seen, seen.e[4], 0.0f, 1500);
        const bool button = NgTuneTake(block.data(), 4, press) == 2 && press == -1.0f && NgTuneTake(block.data(), 4, press) == 0;
        if (!(quiet && most && step && modHolds && toolNot && dropped && notHalf && later && off && offMod && line && stale && isSwitch && button))
            printf("     quiet %d most %d step %d modHolds %d toolNot %d dropped %d notHalf %d later %d off %d offMod %d line %d stale %d switch %d button %d\n", quiet, most, step,
                   modHolds, toolNot, dropped, notHalf, later, off, offMod, line, stale, isSwitch, button);
        Check(quiet && most && step && modHolds && toolNot && dropped,
              "tuning block, the hand-over: nothing asked is nothing taken; wanted 500 with a range of 0 to 300 is 300 in force; a figure is held to its step; no number is "
              "not written by the tool and dropped by the mod, once");
        Check(notHalf && later, "tuning block, a wanted written between the mod's two reads of the count: not taken half, taken whole a pass later");
        Check(off && offMod && line && stale && isSwitch && button,
              "tuning block: a disabled entry and a line take nothing; a tick 3 s old is stale and nothing is written into it; a switch is 0 or 1; a button counts its presses");
    }
    // the sequence: a read while the mod writes takes the last copy and does not hang
    {
        NgTuneBegin(block.data());
        NgTuneSeen s;
        const bool odd = NgTuneRead(block.data(), block.size(), s) && s.ok;
        NgTuneEnd(block.data(), 1000);
        Check(odd, "tuning block, a read while the mod is in its write: two tries, then the last copy");
    }
    // a real mapping, two mods and the tool's open by name
    {
        NgTuneMod a = {}, b = {};
        const bool first = NgTuneOpen(a, "Gameplay", 1, kItems, 2), second = NgTuneOpen(b, "Tire Inflation", 2, kItems, kCount);
        bool found = first && second && a.n != b.n && a.block && b.block;
        int groups = 0;
        for (int n = 0; n < kNgTuneBlocks && found; n++)
        {
            char name[64];
            NgTuneName(name, sizeof name, GetCurrentProcessId(), n);
            const HANDLE map = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
            if (!map) continue;
            uint8_t *view = (uint8_t *)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
            MEMORY_BASIC_INFORMATION info = {};
            NgTuneSeen s;
            if (view && VirtualQuery(view, &info, sizeof info) && NgTuneRead(view, info.RegionSize, s))
            {
                groups++;
                found = found && s.count == (n == a.n ? 2 : kCount) && !strcmp(s.mod, n == a.n ? "Gameplay" : "Tire Inflation");
                if (n == b.n) NgTuneWant(view, s, s.e[0], 250.0f, s.tick);
            }
            if (view) UnmapViewOfFile(view);
            CloseHandle(map);
        }
        float held = 0.0f;
        const bool crossed = found && groups == 2 && NgTuneTake(b.block, 0, held) == 1 && held == 250.0f && NgTuneTake(a.block, 0, held) == 0;
        // the block lives while either side holds it, and a mod's n is free again once both let go
        if (a.block) UnmapViewOfFile(a.block), CloseHandle(a.map);
        NgTuneMod c = {};
        const bool again = NgTuneOpen(c, "Third", 3, kItems, 1) && c.n == a.n;
        if (c.block) UnmapViewOfFile(c.block), CloseHandle(c.map);
        if (b.block) UnmapViewOfFile(b.block), CloseHandle(b.map);
        Check(crossed && again, "tuning block, two mods in one process: each makes the first free number, the tool finds both by name, an ask crosses the mapping to its mod alone");
    }
    // the tab: the mods in their order, and Reset (M6): each slider and switch of one group asked for its default, no other group
    {
        std::vector<uint8_t> first(kNgTuneBlockMost), second(kNgTuneBlockMost);
        NgTuneSetUp(first.data(), first.size(), "Tire Inflation", 2, kItems, kCount);
        NgTuneSetUp(second.data(), second.size(), "Gameplay", 1, kItems, 2);
        static NgTuneSeen s1, s2;
        NgTuneRead(first.data(), first.size(), s1), NgTuneRead(second.data(), second.size(), s2);
        const TuneShown shown[3] = { { first.data(), &s1, 0 }, { second.data(), &s2, 1 }, { first.data(), &s1, 5 } };
        int order[3] = {};
        TuneOrder(shown, 3, order);
        const bool ordered = order[0] == 1 && order[1] == 0 && order[2] == 2;
        // values in force away from the defaults, then Reset of "Rolling drag"
        float held = 0.0f;
        const int asked = TuneReset(shown[0], "Rolling drag", s1.tick);
        const bool reset = asked == 2 && NgTuneTake(first.data(), 0, held) == 1 && held == 100.0f && NgTuneTake(first.data(), 1, held) == 1 && held == 0.01f &&
                           NgTuneTake(first.data(), 2, held) == 0 && NgTuneTake(first.data(), 3, held) == 0 && NgTuneTake(first.data(), 4, held) == 0 &&
                           NgTuneTake(second.data(), 0, held) == 0;
        const bool mud = TuneReset(shown[0], "Mud", s1.tick) == 1 && NgTuneTake(first.data(), 3, held) == 1 && held == 0.0f && NgTuneTake(first.data(), 4, held) == 0;
        char f1[24], ins[24], f12[24];
        TuneKeyName(VK_F1, f1, sizeof f1), TuneKeyName(VK_INSERT, ins, sizeof ins), TuneKeyName(VK_F12, f12, sizeof f12);
        const bool keys = !strcmp(f1, "F1") && !strcmp(f12, "F12") && ins[0] && TuneKeyOk(VK_F1) && TuneKeyOk(VK_INSERT) && !TuneKeyOk(VK_ESCAPE) && !TuneKeyOk(VK_F2) &&
                          !TuneKeyOk(VK_LBUTTON) && !TuneKeyOk(0) && !TuneKeyOk(300);
        // the key: closed it opens on the Tuning tab, open on another tab it goes there, on the Tuning tab it closes
        const bool keyDoes = TuneKeyDoes(false, false) == kTuneKeyOpens && TuneKeyDoes(false, true) == kTuneKeyOpens && TuneKeyDoes(true, false) == kTuneKeyGoes &&
                             TuneKeyDoes(true, true) == kTuneKeyCloses;
        Check(keyDoes, "tuning key: with the menu closed it opens it on the Tuning tab, with the menu open on another tab it goes to the Tuning tab, on the Tuning tab it closes the menu");
        // the log of changes: a drag gives one line a second after it stopped, with its first and last value
        {
            TuneTrack t = {};
            float before = -1.0f;
            int lines = 0;
            bool firstSight = !TuneTrackStep(t, 50.0f, 1000, before);
            // a drag of 40 frames from 50 to 90, asked each frame
            float v = 50.0f;
            for (int f = 1; f <= 40; f++)
            {
                v = 50.0f + (float)f;
                TuneTrackAsk(t, 1000 + 16 * (uint64_t)f);
                if (TuneTrackStep(t, v, 1000 + 16 * (uint64_t)f, before)) lines++;
            }
            const uint64_t end = 1000 + 16 * 40;
            const bool notYet = !TuneTrackStep(t, 90.0f, end + 999, before) && lines == 0;
            const bool oneLine = TuneTrackStep(t, 90.0f, end + 1000, before) && before == 50.0f && !TuneTrackStep(t, 90.0f, end + 3000, before);
            // a value that moves with no ask (another truck's figures loaded): no line, and it is the next line's "before"
            const bool load = !TuneTrackStep(t, 70.0f, end + 4000, before) && !TuneTrackStep(t, 70.0f, end + 6000, before);
            TuneTrackAsk(t, end + 7000);
            const bool afterLoad = !TuneTrackStep(t, 75.0f, end + 7000, before) && TuneTrackStep(t, 75.0f, end + 8000, before) && before == 70.0f;
            // a Reset: quiet, whatever was asked before it
            TuneTrackAsk(t, end + 9000);
            TuneTrackStep(t, 80.0f, end + 9000, before);
            TuneTrackQuiet(t, end + 9100);
            const bool resetQuiet = !TuneTrackStep(t, 100.0f, end + 9100, before) && !TuneTrackStep(t, 100.0f, end + 11000, before);
            // asked for the value it has: no line
            TuneTrackAsk(t, end + 12000);
            const bool same = !TuneTrackStep(t, 100.0f, end + 14000, before);
            // asked before the first sight: the first value seen is the "before"
            TuneTrack early = {};
            TuneTrackAsk(early, 500);
            const bool earlyOk = !TuneTrackStep(early, 1.0f, 500, before) && !TuneTrackStep(early, 2.0f, 600, before) && TuneTrackStep(early, 2.0f, 1600, before) && before == 1.0f;
            NgTuneSeenEntry slider = {}, sw = {};
            slider.kind = kNgTuneSlider, slider.decimals = 2;
            snprintf(slider.name, sizeof slider.name, "Mud scale"), snprintf(slider.group, sizeof slider.group, "Rolling drag");
            sw.kind = kNgTuneSwitch;
            snprintf(sw.name, sizeof sw.name, "Probe");
            char a[240], b[240];
            TuneChangeLine(a, sizeof a, "Tire Inflation", slider, 0.5f, 0.65f);
            TuneChangeLine(b, sizeof b, "", sw, 0.0f, 1.0f);
            Check(firstSight && notYet && oneLine && load && afterLoad && resetQuiet && same && earlyOk && !strcmp(a, "Tire Inflation: Rolling drag / Mud scale: 0.65, was 0.50") &&
                      !strcmp(b, "a mod: Probe: on, was off"),
                  "tuning log: a drag gives one line a second after it stopped, with the value before and the last value; a value loaded by the mod and a Reset give no line; "
                  "asked for the value in force gives none");
        }
        Check(ordered && reset && mud && keys,
              "tuning tab: the mods by their order then their block number; Reset asks each slider and switch of its group for the default the mod published, no line, no "
              "button, no other group and no other mod; the key is a keyboard key, never Escape, the menu key or a mouse button");
    }
    // the first ini (BRIEF_tester_1114.md, part 191): written from the table, read back to the table's figures; never over
    // a file that is there; no part file left. In the temp folder under a name of this run's own, both files removed after
    {
        char text[2048];
        const size_t n = TuneIniText(text, sizeof text);
        bool textOk = n > 0 && strstr(text, "[Tuning]\r\n") && strstr(text, "Key=112\r\n") && strstr(text, "; Key: ") && !strchr(text, '\'');
        char tiny[8];
        textOk = textOk && TuneIniText(tiny, sizeof tiny) == 0 && tiny[0] == 0;
        wchar_t dir[MAX_PATH], ini[MAX_PATH], other[MAX_PATH], part[MAX_PATH + 8];
        GetTempPathW(MAX_PATH, dir);
        swprintf_s(ini, L"%sNextGenTuningTest_%lu.ini", dir, GetCurrentProcessId());
        swprintf_s(other, L"%sNextGenTuningTest_%lu_kept.ini", dir, GetCurrentProcessId());
        swprintf_s(part, L"%s.new", ini);
        DeleteFileW(ini), DeleteFileW(other); // this run's own names only
        const int first = TuneIniFirst(ini), again = TuneIniFirst(ini);
        bool back = first == kTuneIniWritten && again == kTuneIniThere && GetFileAttributesW(part) == INVALID_FILE_ATTRIBUTES;
        for (int i = 0; i < kTuneIniCount; i++)
        {
            wchar_t section[64], key[64];
            swprintf_s(section, L"%S", kTuneIni[i].section);
            swprintf_s(key, L"%S", kTuneIni[i].key);
            back = back && (int)GetPrivateProfileIntW(section, key, -12345, ini) == kTuneIni[i].def;
        }
        // a file that is there keeps its figure
        WritePrivateProfileStringW(L"Tuning", L"Key", L"113", other);
        const bool kept = TuneIniFirst(other) == kTuneIniThere && GetPrivateProfileIntW(L"Tuning", L"Key", 0, other) == 113;
        DeleteFileW(ini), DeleteFileW(other);
        if (!(textOk && back && kept)) printf("     ini: text %d (%zu chars), written and read back %d (first %d again %d), kept %d\n", textOk, n, back, first, again, kept);
        Check(textOk && back && kept && TuneIniDefault("Key") == VK_F1,
              "tuning ini: the first file is written from the table the tool reads by and reads back to its figures, whole, with no part file left; a file that is there "
              "is never written over");
    }
    printf(g_failed ? "TUNE TEST FAILED (%d)\n" : "TUNE TEST OK\n", g_failed);
    return g_failed ? 1 : 0;
}
