// NextGenTuning.asi: a tool for tuning the Next Gen mods while driving (BRIEF_rolling_drag_sliders.md, the lead's head
// note of 2026-11-11 21:39 and the Reviewer's addendum 3). Not part of a release.
//
// It is a mod with a tab and nothing more: the two shared overlay files compiled in as they are, and a tab "Tuning" in
// the NEXT GEN menu. It hooks nothing of the game's, reads and writes none of the game's memory and none of a mod's.
// Each mod that has figures to tune publishes them in a block of named shared memory (ng_tune.h); the tool draws what
// it finds there and writes a wanted value back into the block, which the mod takes at its next pass. A mod that is not
// in the game, or one with no block, has no group on the tab.
//
// Its key (F1 unless NextGenTuning.ini says otherwise; the tab sets it) opens the menu on the Tuning tab and closes it.
// While the menu is open the game gets no keys and no mouse, as with every tab: driving goes on with a pad or a wheel.
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include "imgui.h"
#include "ng_overlay.h"
#include "ng_tune.h"
#include "tune_page.h"

static const char *const kVersion = "1.0.0"; // the tool's own version (CHANGELOG.md); not ng_tune.h's layout or kNgSharedVersion
static std::wstring g_dir; // the game's folder, with the last backslash
static CRITICAL_SECTION g_logLock;
static volatile LONG g_key = VK_F1, g_capture = 0, g_keySave = 0;

static void Log(const wchar_t *fmt, ...)
{
    wchar_t line[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(line, _countof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    SYSTEMTIME t;
    GetLocalTime(&t);
    EnterCriticalSection(&g_logLock);
    FILE *f = nullptr;
    if (_wfopen_s(&f, (g_dir + L"NextGenTuning.log").c_str(), L"a, ccs=UTF-8") == 0 && f)
    {
        fwprintf(f, L"%02d:%02d:%02d.%03d %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, line);
        fclose(f);
    }
    LeaveCriticalSection(&g_logLock);
}

// ---- the blocks: looked for every 2 s while the tab shows, on the thread that draws ----
struct Block
{
    HANDLE map;
    uint8_t *view;
    size_t mapped;
    bool said;        // its fault, or that it was found, is in the log
    ULONGLONG since;  // when the tab first read it
};
static Block g_blocks[kNgTuneBlocks] = {};
static NgTuneSeen g_seen[kNgTuneBlocks];
static ULONGLONG g_looked = 0;

static void BlocksLook()
{
    const ULONGLONG now = GetTickCount64();
    if (g_looked && now - g_looked < 2000) return;
    g_looked = now ? now : 1;
    for (int n = 0; n < kNgTuneBlocks; n++)
    {
        Block &b = g_blocks[n];
        if (b.view) continue;
        char name[64];
        NgTuneName(name, sizeof name, GetCurrentProcessId(), n);
        const HANDLE map = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
        if (!map) continue;
        uint8_t *view = (uint8_t *)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        MEMORY_BASIC_INFORMATION info = {};
        if (!view || !VirtualQuery(view, &info, sizeof info))
        {
            if (view) UnmapViewOfFile(view);
            CloseHandle(map);
            continue;
        }
        b = Block{ map, view, info.RegionSize, false, 0 };
    }
}

static void SayChange(const char *line) { Log(L"tuning: changed: %S", line); }
static void Page()
{
    BlocksLook();
    const ULONGLONG now = GetTickCount64();
    TuneShown shown[kNgTuneBlocks];
    int count = 0;
    for (int n = 0; n < kNgTuneBlocks; n++)
    {
        Block &b = g_blocks[n];
        if (!b.view) continue;
        const bool ok = NgTuneRead(b.view, b.mapped, g_seen[n]);
        if (!b.since) b.since = now ? now : 1;
        if (!b.said && (ok || now - b.since > 2000))
        {
            // a block its mod has not set up yet reads as none for a moment: a fault is said once it has stayed 2 s
            b.said = true;
            if (ok) Log(L"tuning: block %d is %S, %d figures (its head %zu bytes, an entry %zu)", n, g_seen[n].mod, g_seen[n].count, g_seen[n].headSize, g_seen[n].entrySize);
            else Log(L"tuning: block %d gives no group: %S", n, g_seen[n].why);
        }
        if (ok) shown[count++] = TuneShown{ b.view, &g_seen[n], n };
    }
    char keyName[24];
    TuneKeyName((int)g_key, keyName, sizeof keyName);
    const int asked = TunePageDraw(shown, count, now, keyName, g_capture != 0, SayChange);
    if (asked == kTuneAskKey) InterlockedExchange(&g_capture, 1);
}
static void Frame() {}
static void DeviceGone() {}

static bool GameInFront()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static DWORD WINAPI Run(void *)
{
    DeleteFileW((g_dir + L"NextGenTuning.log").c_str());
    Log(L"Next Gen Tuning %S: it draws the figures the Next Gen mods publish and hooks nothing of the game", kVersion);
    // one tool a game
    char mutexName[64];
    snprintf(mutexName, sizeof mutexName, "Local\\NextGenTuning.%lu", (unsigned long)GetCurrentProcessId());
    const HANDLE mutex = CreateMutexA(nullptr, FALSE, mutexName);
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        Log(L"tuning: a second tuning tool in this game: this one ends and draws nothing");
        if (mutex) CloseHandle(mutex);
        return 0;
    }
    const std::wstring ini = g_dir + L"NextGenTuning.ini";
    // the first start writes the file with the defaults and their comments, so a player can see the key and change it
    const int first = TuneIniFirst(ini.c_str());
    if (first == kTuneIniWritten) Log(L"tuning: NextGenTuning.ini was not there: written with the defaults (%d keys)", kTuneIniCount);
    else if (first == kTuneIniFailed) Log(L"tuning: NextGenTuning.ini was not there and could not be written: the defaults are used");
    const int key = (int)GetPrivateProfileIntW(L"Tuning", L"Key", TuneIniDefault("Key"), ini.c_str());
    InterlockedExchange(&g_key, TuneKeyOk(key) ? key : VK_F1);
    char keyName[24];
    TuneKeyName((int)g_key, keyName, sizeof keyName);
    NgClient c = {};
    c.tab = "Tuning";
    c.version = kVersion;
    c.order = 9;
    c.log = &Log;
    c.frame = &Frame;
    c.page = &Page;
    c.deviceGone = &DeviceGone;
    c.keepOthers = true; // the dials and whatever else the other mods draw stay under the Tuning tab (BRIEF_dials_under_tuning.md)
    if (!NgOverlayStart(c))
    {
        Log(L"tuning: the overlay could not start: no Tuning tab");
        return 0;
    }
    Log(L"tuning: the tab Tuning is in the NEXT GEN menu; %S opens and closes it (NextGenTuning.ini, Key=%d)", keyName, (int)g_key);
    bool was = false, held[256] = {};
    for (;;)
    {
        Sleep(30);
        const bool front = GameInFront();
        if (g_capture)
        {
            // the next key that goes down is the tool's key; Escape leaves it as it is. Mouse buttons are not keys here
            int got = 0;
            for (int vk = 8; vk < 255 && !got; vk++)
            {
                const bool down = front && (GetAsyncKeyState(vk) & 0x8000) != 0;
                if (down && !held[vk]) got = vk;
                held[vk] = down;
            }
            if (got)
            {
                if (got != VK_ESCAPE && TuneKeyOk(got))
                {
                    InterlockedExchange(&g_key, got);
                    wchar_t text[16];
                    swprintf_s(text, L"%d", got);
                    WritePrivateProfileStringW(L"Tuning", L"Key", text, ini.c_str());
                    TuneKeyName(got, keyName, sizeof keyName);
                    Log(L"tuning: the key is now %S (Key=%d, saved)", keyName, got);
                }
                InterlockedExchange(&g_capture, 0);
                was = true; // the press that set it does not also close the menu
            }
            continue;
        }
        for (int vk = 8; vk < 255; vk++) held[vk] = front && (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool now = held[(int)g_key & 255];
        if (now && !was)
        {
            // closed: open on the Tuning tab; open on another tab: go to the Tuning tab; on the Tuning tab: close
            if (TuneKeyDoes(NgMenuOpen(), NgOwnTabShown()) == kTuneKeyCloses) NgMenuShowTab(nullptr);
            else if (!NgMenuShowTab("Tuning")) Log(L"tuning: the menu has no Tuning tab yet (the overlay draws nothing so far)");
        }
        was = now;
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t *name = wcsrchr(path, L'\\');
    if (!name || _wcsicmp(name + 1, L"SnowRunner.exe")) return TRUE; // loaded into another program: do nothing
    g_dir.assign(path, name + 1 - path);
    InitializeCriticalSection(&g_logLock);
    const HANDLE t = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    return TRUE;
}
