// Gauges.asi for SnowRunner (Next Gen Gameplay): a gauge cluster for the truck being driven, drawn by Next Gen's own
// overlay (gauge_overlay.cpp, ng_overlay.cpp; no ReShade needed). This file is the mod's own thread: it owns the settings (Gauges.ini next to the exe, written
// with the defaults on the first start), the log (Gauges.log, rewritten at each game start), the toggle key and pad
// buttons, and the state the cluster shows, which it leaves for the render thread 20 times a second.
//
// The state comes from the truck being driven, read out of the game's memory (truck.h): speed, gear, fuel and the
// lamps. The engine speed, the range, the tilt and the coolant temperature are worked out from those values
// (models.h). With the ini's Damage on, what the temperature costs the engine goes into the game's own engine
// damage. With Gearbox=1 the mod's automatic sets the gears while the game's shifter is in A: it puts the game's
// automatic flag down and calls the game's own shift function (models.h, TakeoverStep). With Powertrain=1, and for
// the pull test's seven seconds, it writes the game's torque factor (TorqueLift, PullTestStep). Those are all the
// mod writes into the game and all it calls there. The cluster is on screen only while a truck is driven in the world. On another build than the one its addresses are from (kGaugesExeStamp) the mod finds them by the code that uses them (game_globals.h), and stands down where it cannot. The map
// a truck is on comes from the game's save (maps.h), for the map's outside temperature.
#include <windows.h>
#include <xinput.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <limits.h>
#include <string>
#include <vector>
#include <algorithm>
#include "gauges.h"
#include "truck.h"
#include "models.h"
#include "gearbox_hooks.h"
#include "gearbox_model.h"
#include "input_bindings.h"
#ifndef GAUGES_SETTINGS_TEST
#include "input_devices.h"
#endif
bool NgMenuOpen(); // ng_overlay.cpp (its header wants Dear ImGui): the toggle key rests while the NEXT GEN menu has the keys
int NgMenuKeyFromIni(); // the menu's key, which a capture never binds
#include "tires.h"
#include "maps.h"
#include "game_code.h"
#include "game_globals.h"
#include "clock_scan.h"
#include "suspension_scan.h"
#include "suspension_step.h"
#include "air_axles.h"
#include "frame_flex.h"
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#ifndef GAUGES_SETTINGS_TEST
#include "sound.h"
#include "state.h"
#endif

static HMODULE g_self = nullptr;
static std::wstring g_dir; // the game's folder, with the last backslash
static uint64_t g_base = 0;
static bool g_overlayOk = false;
static bool g_addon = false;  // loaded by ReShade under the name from before 0.16 (Gauges.addon64), not by an ASI loader (Gauges.asi)
static bool g_second = false; // another copy of the mod runs in this game, and this one stands down

// The ini's settings in force. The Gameplay tab of the NEXT GEN menu reads them and hands back edits (SettingsPut,
// render thread); the mod's thread takes those on its next pass and saves the ini a second later. g_setTurn counts
// the times the mod's thread put settings in force itself (SettingsPublish).
static SRWLOCK g_setLock = SRWLOCK_INIT;
static GaugeSettings g_set;
static LONG g_setTurn = 0;
static volatile LONG g_setDirty = 0, g_reloadAsked = 0, g_truckDropAsked = 0, g_resetAsked = 0;
static volatile LONG g_suspOwnAsked = 0, g_suspDefaultAsked = 0; // the Suspension tab's box (1 on, 2 off) and its button
static volatile LONG g_suspLevelAsked = 0; // the Suspension tab's choice: 1 this truck only, 2 every truck of the kind
static volatile LONG g_engOwnAsked = 0, g_engDefaultAsked = 0, g_engLevelAsked = 0; // the Engine tab's box (1 on, 2 off), its button, its choice (1 this truck, 2 every kind)
static volatile LONG g_pullTestAsked = 0; // 1 arm the pull test, 2 cancel it
static volatile LONG g_mapAmbientAsked = 0, g_mapAmbientTenths = 0; // the tab set the map's outside temperature, in tenths of a degree C
static volatile LONG g_gearHoldAsked = 0, g_gearHoldTenths = 0;     // the tab set the driven gearbox's hold, in tenths of a second
static volatile LONG g_engineAsked = 0, g_enginePeak = 0, g_engineAspiration = 0, g_engineFuel = 0; // the tab set (1) or dropped (2) the driven engine's profile: the peak torque's rpm, the aspiration, the fuel
static volatile LONG g_engineTorqueAsked = 0, g_engineTorquePercent = 0; // the tab set the driven engine's torque share, in percent

// What the cluster shows, written by the mod's thread and read by the render thread.
static SRWLOCK g_viewLock = SRWLOCK_INIT;
static GaugeView g_view = {};

// The log is rewritten at each game start. The mod's thread and the game's threads (the overlay, the truck update) all
// write it: one at a time, into one buffer and one handle (LogV).
static SRWLOCK g_logLock = SRWLOCK_INIT;
static bool g_logStarted = false; // true once this start's first line is in
static std::wstring g_logKept;    // where the last start's log was kept, "" for nowhere

// The last start's log kept: Gauges.log in dir moves into Gauges\logs beside it, named by the date and time that
// start began, so every start's log is kept and a log passed on says when it was. The time is its first line's; the
// date the day it was last written, or the day before where that is later in the day than its last write (a drive
// past midnight). The name taken, "" where there was no log or it could not be moved.
static std::wstring LogKeepLast(const std::wstring &dir)
{
    const std::wstring path = dir + L"Gauges.log";
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a) || (a.nFileSizeHigh == 0 && a.nFileSizeLow == 0)) return L"";
    FILETIME local;
    SYSTEMTIME last, began;
    if (!FileTimeToLocalFileTime(&a.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &last)) return L"";
    began = last;
    char head[24] = {};
    FILE *f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f)
    {
        fread(head, 1, sizeof head - 1, f);
        fclose(f);
    }
    const char *line = head + (memcmp(head, "\xEF\xBB\xBF", 3) ? 0 : 3);
    int h = 0, m = 0, sec = 0;
    if (sscanf_s(line, "%2d:%2d:%2d", &h, &m, &sec) == 3 && h >= 0 && h < 24 && m >= 0 && m < 60 && sec >= 0 && sec < 60)
    {
        if (h * 3600 + m * 60 + sec > last.wHour * 3600 + last.wMinute * 60 + last.wSecond)
        {
            ULARGE_INTEGER day;
            day.LowPart = local.dwLowDateTime;
            day.HighPart = local.dwHighDateTime;
            day.QuadPart -= 24ULL * 3600ULL * 10000000ULL;
            const FILETIME before = { day.LowPart, day.HighPart };
            FileTimeToSystemTime(&before, &began);
        }
        began.wHour = (WORD)h;
        began.wMinute = (WORD)m;
        began.wSecond = (WORD)sec;
    }
    const std::wstring folder = dir + L"Gauges\\logs\\";
    CreateDirectoryW((dir + L"Gauges").c_str(), nullptr);
    CreateDirectoryW(folder.c_str(), nullptr);
    wchar_t stem[48];
    swprintf_s(stem, L"Gauges %04u-%02u-%02u %02u-%02u-%02u", began.wYear, began.wMonth, began.wDay, began.wHour, began.wMinute, began.wSecond);
    std::wstring name = std::wstring(stem) + L".log";
    for (int i = 2; i < 100 && GetFileAttributesW((folder + name).c_str()) != INVALID_FILE_ATTRIBUTES; i++)
        name = std::wstring(stem) + L" (" + std::to_wstring(i) + L").log";
    return MoveFileExW(path.c_str(), (folder + name).c_str(), MOVEFILE_COPY_ALLOWED) ? name : L"";
}

// The kept start logs capped (KeepLogs, the log cap brief): the newest keep of them stay, and at most most bytes of
// them, whichever is less; the newest one always stays, so the log a player is asked for is there. Older ones go,
// oldest first, by their last write. Only the names LogKeepLast gives are touched; keep 0 removes nothing.
static const unsigned long long kLogsMostBytes = 500ULL << 20;
// A name LogKeepLast gives: "Gauges YYYY-MM-DD HH-MM-SS.log", or with " (N)" before ".log".
static bool LogKeptName(const wchar_t *n)
{
    static const wchar_t pattern[] = L"Gauges 0000-00-00 00-00-00";
    for (int i = 0; i < 26; i++)
        if (pattern[i] == L'0' ? !(n[i] >= L'0' && n[i] <= L'9') : n[i] != pattern[i]) return false;
    const wchar_t *tail = n + 26;
    if (!wcscmp(tail, L".log")) return true;
    if (tail[0] != L' ' || tail[1] != L'(' || !(tail[2] >= L'0' && tail[2] <= L'9')) return false;
    const wchar_t *p = tail + 2;
    while (*p >= L'0' && *p <= L'9') p++;
    return !wcscmp(p, L").log");
}
struct LogsCapResult { int removed, kept, failed; };
static LogsCapResult LogsCap(const std::wstring &folder, int keep, unsigned long long most)
{
    LogsCapResult r = {};
    if (keep <= 0) return r;
    struct One { std::wstring name; unsigned long long size, at; };
    std::vector<One> logs;
    WIN32_FIND_DATAW fd;
    const HANDLE find = FindFirstFileW((folder + L"Gauges *.log").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return r;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && LogKeptName(fd.cFileName))
            logs.push_back({ fd.cFileName, ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow,
                             ((unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime });
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    std::sort(logs.begin(), logs.end(), [](const One &a, const One &b) { return a.at != b.at ? a.at > b.at : a.name > b.name; });
    unsigned long long total = 0;
    for (size_t i = 0; i < logs.size(); i++)
    {
        total += logs[i].size;
        if (i == 0 || ((int)i < keep && total <= most)) r.kept++;
        else if (DeleteFileW((folder + logs[i].name).c_str())) r.removed++;
        else r.failed++, r.kept++;
    }
    return r;
}

// The log kept open (BRIEF_log_kept_open.md): with Probe=1 the mod logs about 70 lines a second, and an open, a write
// and a close for each, in Program Files where a scanner may look at each close, is the likeliest hitch of the 4
// November drive. The first line truncates as before; then one handle appends (FILE_APPEND_DATA, so another handle's
// line is never written over) and shares writing, so CrashLineOut's own handle still gets in (the Reviewer's point 1).
// Lines gather in a 64 KB buffer: a line that is not a probe line goes out at once, probe lines at the latest
// kLogFlushMs later, from LogV or the mod's loop (LogTick). A write that fails closes the handle, and each line after
// it opens, appends and closes as before. What an outside kill or a fault inside LogV loses: at most kLogFlushMs of
// probe lines.
static const size_t kLogBuffer = 64 * 1024;
static const ULONGLONG kLogFlushMs = 500;
static HANDLE g_logFile = INVALID_HANDLE_VALUE;
static char g_logBuf[kLogBuffer];
static size_t g_logUsed = 0;
static ULONGLONG g_logFlushedAt = 0;
static bool g_logOneByOne = false; // a write failed: open, append and close for each line

static HANDLE LogOpenAppend()
{
    return CreateFileW((g_dir + L"Gauges.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}

static bool LogWriteAll(HANDLE f, const char *data, size_t size)
{
    while (size)
    {
        DWORD wrote = 0;
        if (!WriteFile(f, data, (DWORD)size, &wrote, nullptr) || wrote == 0) return false;
        data += wrote;
        size -= wrote;
    }
    return true;
}

// Under g_logLock: the buffer to the file.
// Under g_logLock: data to the file through the handle kept open, or where there is none (it failed, or this start's
// first line came before it, as the second copy's does: review F1) through an open, an append and a close, which from
// then on every line takes.
static void LogOut(const char *data, size_t size)
{
    if (g_logFile != INVALID_HANDLE_VALUE && LogWriteAll(g_logFile, data, size)) return;
    if (g_logFile != INVALID_HANDLE_VALUE) CloseHandle(g_logFile);
    g_logFile = INVALID_HANDLE_VALUE;
    g_logOneByOne = true;
    const HANDLE f = LogOpenAppend();
    if (f == INVALID_HANDLE_VALUE) return;
    LogWriteAll(f, data, size);
    CloseHandle(f);
}

// Under g_logLock: the buffer to the file.
static void LogFlushHeld()
{
    g_logFlushedAt = GetTickCount64();
    if (g_logUsed) LogOut(g_logBuf, g_logUsed);
    g_logUsed = 0;
}

// The buffer out where the lock is free, for the crash, fault and exit lines and the unload: a fault inside LogV, or
// a thread ended holding the lock, must not hang them. Held, nothing is flushed, and the line still lands.
static void LogFlushTry()
{
    if (!TryAcquireSRWLockExclusive(&g_logLock)) return;
    LogFlushHeld();
    ReleaseSRWLockExclusive(&g_logLock);
}

// From the mod's loop (every 50 ms): probe lines followed by silence (a menu, the map) go out kLogFlushMs later.
static void LogTick()
{
    AcquireSRWLockExclusive(&g_logLock);
    if (g_logUsed && GetTickCount64() - g_logFlushedAt >= kLogFlushMs) LogFlushHeld();
    ReleaseSRWLockExclusive(&g_logLock);
}

static void LogV(const wchar_t *fmt, va_list ap)
{
    AcquireSRWLockExclusive(&g_logLock);
#ifndef GAUGES_SETTINGS_TEST
    if (!g_logStarted) g_logKept = LogKeepLast(g_dir);
#endif
    if (!g_logStarted)
    {
        // a fresh log each start, with the byte order mark as the "w, ccs=UTF-8" open wrote it
        const HANDLE f = CreateFileW((g_dir + L"Gauges.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE)
        {
            LogWriteAll(f, "\xEF\xBB\xBF", 3);
            CloseHandle(f);
            g_logStarted = true;
            g_logFile = LogOpenAppend();
            g_logOneByOne = g_logFile == INVALID_HANDLE_VALUE;
            g_logFlushedAt = GetTickCount64();
        }
    }
    if (g_logStarted)
    {
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t room[2048];
        std::wstring big;
        int n = _snwprintf_s(room, _countof(room), _TRUNCATE, L"%02u:%02u:%02u ", t.wHour, t.wMinute, t.wSecond);
        const int head = n < 0 ? 0 : n;
        // a copy of the arguments for each further pass over them (review N1)
        va_list count, again;
        va_copy(count, ap);
        va_copy(again, ap);
        n = _vsnwprintf_s(room + head, _countof(room) - head - 2, _TRUNCATE, fmt, ap);
        const wchar_t *text = room;
        int length = 0;
        if (n >= 0) length = head + n;
        else
        {
            // longer than the stack's room
            const int need = _vscwprintf(fmt, count);
            big.assign(room, head);
            big.resize(head + (need > 0 ? need : 0) + 1);
            if (need > 0) _vsnwprintf_s(&big[head], need + 1, _TRUNCATE, fmt, again);
            length = head + (need > 0 ? need : 0);
            text = big.c_str();
        }
        va_end(count);
        va_end(again);
        char line[6144];
        std::string wide;
        char *out = line;
        int bytes = WideCharToMultiByte(CP_UTF8, 0, text, length, line, (int)sizeof line - 2, nullptr, nullptr);
        if (bytes <= 0 && length > 0)
        {
            const int need = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
            wide.resize((size_t)(need > 0 ? need : 0) + 2);
            bytes = need > 0 ? WideCharToMultiByte(CP_UTF8, 0, text, length, &wide[0], need, nullptr, nullptr) : 0;
            out = &wide[0];
        }
        if (bytes < 0) bytes = 0;
        out[bytes++] = '\r';
        out[bytes++] = '\n';
        if (!g_logOneByOne && g_logUsed + (size_t)bytes > kLogBuffer) LogFlushHeld();
        // one by one (a pre-flush that failed included), or longer than the buffer: straight out
        if (g_logOneByOne || g_logFile == INVALID_HANDLE_VALUE || (size_t)bytes > kLogBuffer) LogOut(out, (size_t)bytes);
        else
        {
            memcpy(g_logBuf + g_logUsed, out, (size_t)bytes);
            g_logUsed += (size_t)bytes;
            // a line that is not a probe line goes out at once, so a crash or an exit after it never loses it
            if (wcsncmp(fmt, L"probe:", 6) || GetTickCount64() - g_logFlushedAt >= kLogFlushMs) LogFlushHeld();
        }
    }
    ReleaseSRWLockExclusive(&g_logLock);
}

// The crash, fault and exit lines' file (CrashLineSet), and their writer. CrashLineOut's handle shares writing with
// the log's own, which stays open.
static wchar_t g_crashLog[MAX_PATH + 16];
// One line into Gauges.log, written with no heap and no lock: the time, then text.
static void CrashLineOut(const char *text)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[1400];
    const int n = _snprintf_s(line, sizeof line, _TRUNCATE, "%02u:%02u:%02u %s\r\n", st.wHour, st.wMinute, st.wSecond, text);
    const HANDLE f = CreateFileW(g_crashLog, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    if (n > 0) WriteFile(f, line, (DWORD)n, &wrote, nullptr);
    FlushFileBuffers(f);
    CloseHandle(f);
}

// A crash, fault or exit line after the probe lines still in the buffer, where the log's lock is free (the Reviewer's
// point 3): held, the line lands first and the probe lines after it.
static void CrashLineFlushed(const char *text)
{
    LogFlushTry();
    CrashLineOut(text);
}

void GaugeLog(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    LogV(fmt, ap);
    va_end(ap);
}
#define Log GaugeLog

#ifndef GAUGES_SETTINGS_TEST
// A crash's line (0.18.2's hotfix, the reviewer's item 5): a fault nothing handled writes one line to Gauges.log, its
// code, its place as module + RVA and its thread, then goes on to the filter that was there before (the game's crash
// reporter). Set once a truck is first driven, after the game set its own. The line is written without the log's lock
// and without the heap, as the fault may have come while either was held.
// Round 2: the game closed with no such line, so the fault, if any, is handled inside the game. A vectored handler,
// first in the chain, also names the first kFaultLines faults of the kinds that end a program on any thread, as they
// are raised, and lets them go on as they would (EXCEPTION_CONTINUE_SEARCH): it changes nothing.
// Round 3: it named nothing, so the game ends itself. ExitProcess, TerminateProcess on this process and
// RaiseFailFastException are hooked, and each call writes an exit line with kExitFrames return addresses. The vectored
// handler also names C++ throws (0xE06D7363) and fast fails (0xC0000409). A game may throw as part of its work, so each
// throw's return addresses are kept (the last one only, in static memory) and an exit line names the last throw too.
// Round 4: the game ended 0xC0000409 at 19:39:07 on 2026-11-05 and no line named it: a fast fail goes past the
// vectored handler and the filter. The C runtime's invalid-parameter handler and SIGABRT are set in the game's
// ucrtbase.dll (Gauges owns those; Tire Inflation sets them only without Gauges) and in the mod's own runtime
// (crt_line.h). A /GS failure ends the same way but runs no code of the process: it cannot be caught here, only a
// dump names it.
#include "crt_line.h"
static LPTOP_LEVEL_EXCEPTION_FILTER g_crashBefore = nullptr;
static DWORD g_crashModThread = 0;
static const LONG kFaultLines = 3;
static const int kExitFrames = 8;
static const DWORD kCodeCppThrow = 0xE06D7363, kCodeFastFail = 0xC0000409;
static void *g_lastThrow[kExitFrames];
static volatile LONG g_lastThrowFrames = 0, g_throws = 0;
static volatile DWORD g_lastThrowThread = 0;

// at as module + RVA ("SnowRunner.exe+a58220") into out, with no heap.
static int CrashPlace(const void *at, char *out, size_t size)
{
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)at, &module) && module)
        GetModuleFileNameW(module, path, MAX_PATH);
    const wchar_t *name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _snprintf_s(out, size, _TRUNCATE, "%ls+%llx", name, module ? (unsigned long long)((const uint8_t *)at - (const uint8_t *)module) : (unsigned long long)at);
}

// Return addresses as places, joined by " < ", appended at out + used.
static int CrashFrames(void *const *frames, int count, char *out, size_t size, int used)
{
    for (int i = 0; i < count && used + 8 < (int)size; i++)
    {
        if (i) used += _snprintf_s(out + used, size - used, _TRUNCATE, " < ");
        const int n = CrashPlace(frames[i], out + used, size - used);
        if (n > 0) used += n;
    }
    return used;
}

static const char *CrashThreadWord(DWORD thread) { return thread == g_crashModThread ? " (the mod's thread)" : ""; }

// A fault's line. what: "crash" or "fault"; the place it was raised at, its thread and its callers.
static void CrashWrite(const EXCEPTION_POINTERS *e, const char *what, const char *after)
{
    char text[1200], at[320];
    void *frames[kExitFrames];
    CrashPlace(e->ExceptionRecord->ExceptionAddress, at, sizeof at);
    const DWORD thread = GetCurrentThreadId();
    int used = _snprintf_s(text, sizeof text, _TRUNCATE, "%s: code %08lX at %s, thread %lu%s; %s; called from ", what, e->ExceptionRecord->ExceptionCode, at, thread,
                           CrashThreadWord(thread), after);
    if (used < 0) used = (int)strlen(text);
    CrashFrames(frames, CaptureStackBackTrace(1, kExitFrames, frames, nullptr), text, sizeof text, used);
    CrashLineFlushed(text);
}

// An exit's line: the call, its code, its callers, and the last C++ throw's callers where there was one.
static void ExitWrite(const char *call, DWORD code)
{
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) return;
    char text[1400];
    void *frames[kExitFrames];
    const DWORD thread = GetCurrentThreadId();
    int used = _snprintf_s(text, sizeof text, _TRUNCATE, "exit: %s(%lu), thread %lu%s, called from ", call, code, thread, CrashThreadWord(thread));
    if (used < 0) used = (int)strlen(text);
    used = CrashFrames(frames, CaptureStackBackTrace(1, kExitFrames, frames, nullptr), text, sizeof text, used);
    const LONG thrown = g_lastThrowFrames;
    if (thrown > 0 && used + 64 < (int)sizeof text)
    {
        const int n = _snprintf_s(text + used, sizeof text - used, _TRUNCATE, "; the last of %ld C++ throws, thread %lu, from ", (long)g_throws, (unsigned long)g_lastThrowThread);
        if (n > 0) CrashFrames(g_lastThrow, (int)thrown, text, sizeof text, used + n);
    }
    CrashLineFlushed(text);
}

static LONG WINAPI CrashLine(EXCEPTION_POINTERS *e)
{
    static volatile LONG once = 0;
    if (e && e->ExceptionRecord && InterlockedExchange(&once, 1) == 0) CrashWrite(e, "crash", "passed on to the game's handler");
    return g_crashBefore ? g_crashBefore(e) : EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI FaultLine(EXCEPTION_POINTERS *e)
{
    static volatile LONG lines = 0, throwLines = 0;
    if (!e || !e->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code == kCodeCppThrow)
    {
        // kept for the exit's line; the first kFaultLines named at once
        InterlockedIncrement(&g_throws);
        g_lastThrowFrames = 0;
        g_lastThrowFrames = CaptureStackBackTrace(1, kExitFrames, g_lastThrow, nullptr);
        g_lastThrowThread = GetCurrentThreadId();
        if (InterlockedIncrement(&throwLines) <= kFaultLines) CrashWrite(e, "fault", "a C++ throw, before any catch (it may be caught)");
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW &&
        code != kCodeFastFail)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&lines) <= kFaultLines) CrashWrite(e, "fault", "as raised, before any handler (it may be handled)");
    return EXCEPTION_CONTINUE_SEARCH;
}

static VOID(WINAPI *g_exitProcess)(UINT) = ExitProcess;
static BOOL(WINAPI *g_terminateProcess)(HANDLE, UINT) = TerminateProcess;
static VOID(WINAPI *g_failFast)(PEXCEPTION_RECORD, PCONTEXT, DWORD) = RaiseFailFastException;
static VOID WINAPI ExitProcessHook(UINT code)
{
    ExitWrite("ExitProcess", code);
    g_exitProcess(code);
}
static BOOL WINAPI TerminateProcessHook(HANDLE process, UINT code)
{
    if (process == GetCurrentProcess() || GetProcessId(process) == GetCurrentProcessId()) ExitWrite("TerminateProcess", code);
    return g_terminateProcess(process, code);
}
static VOID WINAPI FailFastHook(PEXCEPTION_RECORD record, PCONTEXT context, DWORD flags)
{
    ExitWrite("RaiseFailFastException", record ? record->ExceptionCode : 0);
    g_failFast(record, context, flags);
}

static void CrashLineSet()
{
    static bool set = false;
    if (set) return;
    set = true;
    wcsncpy_s(g_crashLog, (g_dir + L"Gauges.log").c_str(), _TRUNCATE);
    g_crashModThread = GetCurrentThreadId();
    g_crashBefore = SetUnhandledExceptionFilter(CrashLine);
    const bool first = AddVectoredExceptionHandler(1, FaultLine) != nullptr;
    void **targets[3] = { (void **)&g_exitProcess, (void **)&g_terminateProcess, (void **)&g_failFast };
    void *const hooks[3] = { (void *)ExitProcessHook, (void *)TerminateProcessHook, (void *)FailFastHook };
    const long hooked = GearboxAttach(targets, hooks, 3);
    // the C runtime's handlers, after the game set its own: the earlier ones are read now and called after the line
    wchar_t crt[320];
    crtline::Set(CrashLineFlushed, true, crt, sizeof crt / sizeof crt[0]);
    Log(L"crash line: %s", crt);
    Log(L"crash line: a fault nothing handles will be named in this log, then passed on to %s; %s; %s", g_crashBefore ? L"the game's own handler" : L"Windows",
        first ? L"the first three faults and C++ throws of any thread are named as they are raised" : L"the line for faults as they are raised could not be added",
        hooked == NO_ERROR ? L"ExitProcess, TerminateProcess and RaiseFailFastException name their caller" : L"the exit calls could not be hooked");
}

// Says where the last start's log went, once this start's first line is in.
static void LogKeptSay()
{
    if (!g_logKept.empty()) Log(L"log: the last start's log is kept as Gauges\\logs\\%s", g_logKept.c_str());
}
#endif

// ---- Gauges.ini ----

static const wchar_t *const kIniSection = L"Gauges";
static std::wstring IniPath() { return g_dir + L"Gauges.ini"; }

// A number from the ini; def for a missing key and for anything that is not a finite number ("nan" would pass every
// range check after it).
static float IniFloatIn(const wchar_t *section, const wchar_t *key, float def, const std::wstring &ini)
{
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(section, key, L"", buf, 64, ini.c_str());
    if (!buf[0]) return def;
    const float v = (float)_wtof(buf);
    return isfinite(v) ? v : def;
}

static float IniFloat(const wchar_t *key, float def, const std::wstring &ini) { return IniFloatIn(kIniSection, key, def, ini); }
static int IniInt(const wchar_t *key, int def, const std::wstring &ini) { return (int)GetPrivateProfileIntW(kIniSection, key, def, ini.c_str()); }
static bool IniBool(const wchar_t *key, bool def, const std::wstring &ini) { return IniInt(key, def ? 1 : 0, ini) != 0; }

// Text as UTF-8 (the game's names, such as Grüntal Hollow), or byte by byte where it is not valid UTF-8.
static std::wstring Wide(const char *s)
{
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, nullptr, 0);
    if (n > 0)
    {
        std::wstring w((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, &w[0], n);
        w.resize((size_t)n - 1);
        return w;
    }
    std::wstring w;
    while (*s) w += (wchar_t)(unsigned char)*s++;
    return w;
}

// A number into a section of the ini, with the fewest digits that read back as the same float.
static void IniPutFloat(const wchar_t *section, const wchar_t *key, float v, const std::wstring &ini)
{
    wchar_t b[32];
    swprintf_s(b, L"%g", v);
    for (int digits = 7; digits <= 9 && (float)_wtof(b) != v; digits++) swprintf_s(b, L"%.*g", digits, v);
    WritePrivateProfileStringW(section, key, b, ini.c_str());
}

static void IniPutInt(const wchar_t *section, const wchar_t *key, int v, const std::wstring &ini)
{
    wchar_t b[16];
    swprintf_s(b, L"%d", v);
    WritePrivateProfileStringW(section, key, b, ini.c_str());
}

// The Gameplay tab's open tab, its fixed id in [Gauges] Tab: read and written on its own, not with the settings, so a
// tab picked is no setting edit and Defaults leaves it (the Reviewer's point 4 on BRIEF_overlay_tabs_and_text.md).
static void IniTabPut(const char *id, const std::wstring &ini) { WritePrivateProfileStringW(kIniSection, L"Tab", Wide(id).c_str(), ini.c_str()); }
void PageTabRead(char *out, size_t size)
{
    wchar_t b[32] = {};
    GetPrivateProfileStringW(kIniSection, L"Tab", L"", b, 32, IniPath().c_str());
    if (!size || !WideCharToMultiByte(CP_UTF8, 0, b, -1, out, (int)size, nullptr, nullptr)) out[0] = 0;
}
// the write goes to the mod's thread, where the settings are saved, so the two never write the ini at once
static char g_tabPut[32] = {};
static volatile LONG g_tabPutAsked = 0;
void PageTabWrite(const char *id)
{
    strcpy_s(g_tabPut, id);
    InterlockedExchange(&g_tabPutAsked, 1);
}

// ---- setups of their own: a file each in the folder Gauges\ beside Gauges.ini ----
// A truck's own scales stand in Gauges\trucks\<kind>.ini, a gearbox's own hold in Gauges\gearboxes\<name>.ini, each
// under one section ([Truck], [Gearbox]) with the kind or the name written in it: a file for each, so that a setup
// can be passed on and dropped in as it is. A file's name cannot hold a stroke, which a modded truck's kind does
// ("<name>/<mod id>"): it is written "@" there, and any other character a file's name cannot hold "_". Up to 0.13.9
// they stood in Gauges.ini as [Truck:<kind>] and [Gearbox:<name>]: SetupsMove takes them out into the files.
enum { kSetupTruck, kSetupGearbox, kSetupEngine, kSetupKinds };
static const wchar_t *const kSetupFolders[kSetupKinds] = { L"trucks", L"gearboxes", L"engines" };
static const wchar_t *const kSetupSections[kSetupKinds] = { L"Truck", L"Gearbox", L"Engine" };
static const wchar_t *const kSetupNameKeys[kSetupKinds] = { L"Kind", L"Name", L"Name" };
static const wchar_t *const kSetupIniPrefixes[kSetupKinds] = { L"Truck:", L"Gearbox:", L"Engine:" };

// The file's name for a key, without the folder and ".ini".
static std::wstring SetupFileName(const std::wstring &key)
{
    std::wstring name;
    for (wchar_t c : key) name += c == L'/' ? L'@' : (c < 32 || wcschr(L"\\:*?\"<>|", c)) ? L'_' : c;
    return name;
}

static std::wstring SetupFolder(int kind) { return g_dir + L"Gauges\\" + kSetupFolders[kind]; }
static std::wstring SetupFile(int kind, const std::wstring &key) { return SetupFolder(kind) + L"\\" + SetupFileName(key) + L".ini"; }
static std::wstring SetupFile(int kind, const char *key) { return SetupFile(kind, Wide(key)); }

// The folder made where it is not (WritePrivateProfileString makes a file, not a folder).
static void SetupFolderMake(int kind)
{
    CreateDirectoryW((g_dir + L"Gauges").c_str(), nullptr);
    CreateDirectoryW(SetupFolder(kind).c_str(), nullptr);
}

// A setup's file begun: the folder, and the kind or the name written in it.
static void SetupBegin(int kind, const std::wstring &key)
{
    SetupFolderMake(kind);
    WritePrivateProfileStringW(kSetupSections[kind], kSetupNameKeys[kind], key.c_str(), SetupFile(kind, key).c_str());
}

// The setups Gauges.ini still holds, [Truck:<kind>] and [Gearbox:<name>], moved into their files: each key the file
// does not hold yet goes in, and the section leaves Gauges.ini. Their count, by kind.
static void SetupsMove(int (&moved)[kSetupKinds])
{
    const std::wstring ini = IniPath();
    for (int &n : moved) n = 0;
    std::vector<wchar_t> names(65536, L'\0');
    const DWORD got = GetPrivateProfileSectionNamesW(names.data(), (DWORD)names.size(), ini.c_str());
    if (!got) return;
    std::vector<std::wstring> sections;
    for (const wchar_t *n = names.data(); *n && n < names.data() + got; n += wcslen(n) + 1) sections.push_back(n);
    std::vector<wchar_t> pairs(32768, L'\0');
    for (const std::wstring &section : sections)
        for (int kind = 0; kind < kSetupKinds; kind++)
        {
            const size_t prefix = wcslen(kSetupIniPrefixes[kind]);
            if (section.size() <= prefix || _wcsnicmp(section.c_str(), kSetupIniPrefixes[kind], prefix)) continue;
            const std::wstring key = section.substr(prefix), file = SetupFile(kind, key);
            SetupBegin(kind, key);
            const DWORD length = GetPrivateProfileSectionW(section.c_str(), pairs.data(), (DWORD)pairs.size(), ini.c_str());
            for (const wchar_t *pair = pairs.data(); length && *pair && pair < pairs.data() + length; pair += wcslen(pair) + 1)
            {
                const wchar_t *eq = wcschr(pair, L'=');
                if (!eq || eq == pair) continue;
                const std::wstring name(pair, eq);
                wchar_t there[8] = {};
                if (!GetPrivateProfileStringW(kSetupSections[kind], name.c_str(), L"", there, 8, file.c_str()))
                    WritePrivateProfileStringW(kSetupSections[kind], name.c_str(), eq + 1, file.c_str());
            }
            WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, ini.c_str());
            moved[kind]++;
        }
}

// Every key of [Gauges] into the ini (WritePrivateProfileString keeps the comments, the keys it is not given and the
// other sections).
static void WriteIni(const GaugeSettings &s)
{
    const std::wstring ini = IniPath();
    auto num = [&](const wchar_t *key, float v) { IniPutFloat(kIniSection, key, v, ini); };
    auto whole = [&](const wchar_t *key, int v) { IniPutInt(kIniSection, key, v, ini); };
    whole(L"Layout", s.layout);
    whole(L"SwapDials", s.swapDials ? 1 : 0);
    whole(L"Style", s.style);
    num(L"FaceOpacity", s.faceOpacity);
    num(L"OffsetX", s.offsetX);
    num(L"OffsetY", s.offsetY);
    num(L"Scale", s.scale);
    num(L"Opacity", s.opacity);
    whole(L"Mph", s.mph ? 1 : 0);
    whole(L"Psi", s.psi ? 1 : 0);
    whole(L"Fahrenheit", s.fahrenheit ? 1 : 0);
    whole(L"ShowTemp", s.showTemp ? 1 : 0);
    whole(L"ShowBoost", s.showBoost ? 1 : 0);
    whole(L"ShowAir", s.showAir ? 1 : 0);
    whole(L"ShowClock", s.showClock ? 1 : 0);
    whole(L"ShowFuelUse", s.showFuelUse ? 1 : 0);
    whole(L"ShowLamps", s.showLamps ? 1 : 0);
    whole(L"OilGauge", s.oilGauge);
    whole(L"TachClass", s.tachClass);
    num(L"IdleRpm", s.idleRpm);
    num(L"RedlineRpm", s.redlineRpm);
    num(L"MaxRpm", s.maxRpm);
    num(L"SpeedMax", s.speedMax);
    num(L"BoostMax", s.boostMax);
    whole(L"BoostProfile", s.boostProfile);
    whole(L"Gearbox", s.gearbox);
    whole(L"RequireClutch", s.requireClutch ? 1 : 0);
    whole(L"RequireGearHeld", s.requireGearHeld ? 1 : 0);
    whole(L"SkipNeutral", s.skipNeutral ? 1 : 0);
    whole(L"DownStaysForward", s.downStaysForward ? 1 : 0);
    whole(L"IgnoreShifter", s.ignoreShifter ? 1 : 0);
    whole(L"BackToA", s.backToA);
    whole(L"Input", s.input);
    whole(L"Powertrain", s.powertrain ? 1 : 0);
    whole(L"RoadLoad", s.roadLoad ? 1 : 0);
    num(L"RoadCdA", s.roadCdA);
    whole(L"EngineShare", s.engineShare);
    whole(L"Creep", s.creep ? 1 : 0);
    whole(L"CreepManual", s.creepManual ? 1 : 0);
    num(L"RollWarnDeg", s.rollWarnDeg);
    num(L"RollDangerDeg", s.rollDangerDeg);
    num(L"PitchDangerDeg", s.pitchDangerDeg);
    num(L"WarmUpMinutes", s.warmUpMinutes);
    num(L"OverheatMinutes", s.overheatMinutes);
    num(L"CoolDownMinutes", s.coolDownMinutes);
    num(L"AmbientC", s.ambientC);
    whole(L"AmbientByMap", s.ambientByMap ? 1 : 0);
    num(L"AirFullKmh", s.airFullKmh);
    num(L"OilWarmUpMinutes", s.oilWarmUpMinutes);
    num(L"OilWarnC", s.oilWarnC);
    whole(L"Damage", s.damage ? 1 : 0);
    num(L"HotDamagePercentPerMinute", s.hotDamage);
    num(L"HotDamageFrom", s.hotDamageFrom);
    num(L"ColdDamagePercentPerMinute", s.coldDamage);
    num(L"StallDamagePercent", s.stallDamage);
    whole(L"ClutchBurn", s.clutchBurn ? 1 : 0);
    num(L"ClutchHeatPercent", s.clutchHeat);
    num(L"SpringScale", s.springScale);
    num(L"DampingScale", s.dampingScale);
    num(L"FrameFlex", s.frameFlex);
    num(L"TwistRange", s.twistRange);
    num(L"ExtraTravel", s.extraTravel);
    num(L"RideFront", s.rideFront);
    num(L"RideRear", s.rideRear);
    num(L"SwayFront", s.swayFront);
    num(L"SwayRear", s.swayRear);
    whole(L"SuspDisplay", s.suspDisplay ? 1 : 0);
    whole(L"AirSuspension", s.airSuspension ? 1 : 0);
    whole(L"AirHorizon", s.airHorizon ? 1 : 0);
    whole(L"DamperCurve", s.damperCurve);
    num(L"LowCompression", s.lowCompression);
    num(L"HighCompression", s.highCompression);
    num(L"LowRebound", s.lowRebound);
    num(L"HighRebound", s.highRebound);
    num(L"DamperKnee", s.damperKnee);
    whole(L"BumpStops", s.bumpStops ? 1 : 0);
    num(L"BumpZone", s.bumpZone);
    num(L"BumpForce", s.bumpForce);
    num(L"BumpDamping", s.bumpDamping);
    whole(L"SpringCurve", s.springCurve);
    num(L"SpringRise", s.springRise);
    num(L"SpringCross", s.springCross);
    whole(L"DampingZones", s.dampingZones ? 1 : 0);
    num(L"ZoneSoft", s.zoneSoft);
    num(L"ZoneFirm", s.zoneFirm);
    num(L"ZoneFrom", s.zoneFrom);
    num(L"ZoneTo", s.zoneTo);
    whole(L"ToggleKey", s.toggleKey);
    WritePrivateProfileStringW(kIniSection, L"PadToggle", Wide(s.padToggle).c_str(), ini.c_str());
    whole(L"SoundVolume", s.soundVolume);
    whole(L"SoundsOnly", s.soundsOnly ? 1 : 0);
    whole(L"DieselFlutter", s.dieselFlutter ? 1 : 0);
    whole(L"Demo", s.demo ? 1 : 0);
    whole(L"Probe", s.probe);
    whole(L"KeepLogs", s.keepLogs);
}

// ---- the outside temperature by map: the sections [Map:<name>] of Gauges.ini ----
// A region can have an outside temperature of its own (AmbientC in a section named after it, [Map:Michigan]), and a
// map one apart from its region's ([Map:Black River]); a map of a mod goes by its id ([Map:level_my_map]). They are
// in force while a truck drives there; elsewhere AmbientC of [Gauges] is. The names are the game's own (maps.h). The
// mod writes the regions' sections with a first guess into an ini that has no map section; one taken out by hand
// stays out.

static std::wstring MapSection(const char *name) { return L"Map:" + Wide(name); }

// The outside temperature of one section, held to its range. False when the ini has no such section with AmbientC.
static bool MapSectionAmbient(const char *name, float &out)
{
    if (!name[0]) return false;
    const std::wstring ini = IniPath(), section = MapSection(name);
    wchar_t buf[8] = {};
    if (!GetPrivateProfileStringW(section.c_str(), L"AmbientC", L"", buf, 8, ini.c_str())) return false;
    out = SettingsRange(IniFloatIn(section.c_str(), L"AmbientC", kTempOutside, ini), kAmbientRange[0], kAmbientRange[1]);
    return true;
}

// A section's air over the game's day: the day's warmest and the night's coldest, DayC and NightC where it has both,
// else AmbientC as the mean with kDaySwing between them.
struct MapAir
{
    float mean, day, night;
    bool dayNight; // DayC and NightC are the section's own
};

static bool MapSectionAir(const char *name, MapAir &out)
{
    if (!MapSectionAmbient(name, out.mean))
    {
        // a section with DayC and NightC needs no AmbientC
        const std::wstring ini = IniPath(), section = MapSection(name);
        wchar_t buf[8] = {};
        if (!name[0] || !GetPrivateProfileStringW(section.c_str(), L"DayC", L"", buf, 8, ini.c_str())) return false;
        out.mean = kTempOutside;
    }
    const std::wstring ini = IniPath(), section = MapSection(name);
    wchar_t day[16] = {}, night[16] = {};
    GetPrivateProfileStringW(section.c_str(), L"DayC", L"", day, 16, ini.c_str());
    GetPrivateProfileStringW(section.c_str(), L"NightC", L"", night, 16, ini.c_str());
    out.dayNight = day[0] && night[0];
    if (out.dayNight)
    {
        out.day = SettingsRange(IniFloatIn(section.c_str(), L"DayC", out.mean, ini), kAmbientRange[0], kAmbientRange[1]);
        out.night = SettingsRange(IniFloatIn(section.c_str(), L"NightC", out.mean, ini), kAmbientRange[0], kAmbientRange[1]);
        if (out.night > out.day)
        {
            const float t = out.day;
            out.day = out.night;
            out.night = t;
        }
        out.mean = (out.day + out.night) * 0.5f;
    }
    else
    {
        out.day = out.mean + kDaySwing * 0.5f;
        out.night = out.mean - kDaySwing * 0.5f;
    }
    return true;
}

// The air of the map with the game's id level: its own section, else its region's, else one named after the id.
// from: the section's name. False when none of them is in the ini.
static bool MapAmbient(const char *level, MapAir &out, char *from, size_t size)
{
    const char *name = MapNameOf(level);
    const MapRegion *region = MapRegionOf(level);
    const char *tries[3] = { name ? name : "", region ? region->name : "", level };
    for (const char *t : tries)
        if (MapSectionAir(t, out))
        {
            snprintf(from, size, "%s", t);
            return true;
        }
    from[0] = 0;
    return false;
}

// The section's air moved by delta C: DayC and NightC where it has them, else AmbientC.
static void MapAirShift(const char *name, const MapAir &air, float delta)
{
    const std::wstring ini = IniPath(), section = MapSection(name);
    if (air.dayNight)
    {
        IniPutFloat(section.c_str(), L"DayC", SettingsRange(air.day + delta, kAmbientRange[0], kAmbientRange[1]), ini);
        IniPutFloat(section.c_str(), L"NightC", SettingsRange(air.night + delta, kAmbientRange[0], kAmbientRange[1]), ini);
    }
    else IniPutFloat(section.c_str(), L"AmbientC", SettingsRange(air.mean + delta, kAmbientRange[0], kAmbientRange[1]), ini);
}

// ---- the game's clock (clock_scan.h): where it is, kept under [Clock] for the exe it was found in ----
static bool ClockLoad(DWORD stamp, ClockPath &p)
{
    const std::wstring ini = IniPath();
    wchar_t exe[16] = {}, place[64] = {};
    GetPrivateProfileStringW(L"Clock", L"Exe", L"", exe, 16, ini.c_str());
    GetPrivateProfileStringW(L"Clock", L"Place", L"", place, 64, ini.c_str());
    if (wcstoul(exe, nullptr, 16) != stamp || !place[0]) return false;
    char text[64];
    size_t n = 0;
    for (const wchar_t *c = place; *c && n + 1 < sizeof text; c++) text[n++] = *c < 128 ? (char)*c : '?';
    text[n] = 0;
    return ClockPathParse(text, p);
}

static void ClockSave(DWORD stamp, const ClockPath &p)
{
    const std::wstring ini = IniPath();
    wchar_t exe[16];
    swprintf_s(exe, L"%08lx", stamp);
    char text[64];
    ClockPathText(p, text, sizeof text);
    WritePrivateProfileStringW(L"Clock", L"Exe", exe, ini.c_str());
    WritePrivateProfileStringW(L"Clock", L"Place", Wide(text).c_str(), ini.c_str());
}

static void ClockForget()
{
    const std::wstring ini = IniPath();
    WritePrivateProfileStringW(L"Clock", nullptr, nullptr, ini.c_str());
}

// The exe's image size in memory, which the clock's search keeps its pointers out of.
static uint64_t ImageSize()
{
    const uint8_t *image = (const uint8_t *)g_base;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(image + ((const IMAGE_DOS_HEADER *)image)->e_lfanew);
    return nt->OptionalHeader.SizeOfImage;
}

// The names of the ini's sections.
static std::vector<std::wstring> IniSections(const std::wstring &ini)
{
    static wchar_t names[32768];
    std::vector<std::wstring> out;
    const DWORD n = GetPrivateProfileSectionNamesW(names, 32768, ini.c_str());
    for (const wchar_t *p = names; p < names + n && *p; p += wcslen(p) + 1) out.push_back(p);
    return out;
}

static bool IniHas(const std::vector<std::wstring> &sections, const std::wstring &name)
{
    for (const auto &s : sections)
        if (!_wcsicmp(s.c_str(), name.c_str())) return true;
    return false;
}

// Writes the regions' sections with their first temperatures when the ini has no map section at all. An ini from
// 0.11.0 to 0.11.2 has the placeholder names of then: the five that are not the game's names are renamed with the
// temperatures they hold, and the four regions they lacked are added. A region the game added after an ini got its
// sections (Switzerland, Season 19, in 0.16.0) is offered to it once: [Gauges] MapRegionsOffered lists the ones
// offered, so a section taken out by hand stays out.
static void MapDefaultsWrite()
{
    static const struct { const char *was, *now; } renamed[] = {
        { "kola", "Kola Peninsula" }, { "glades", "Belozersk Glades" }, { "british_columbia", "British Columbia" },
        { "north_carolina", "North Carolina" }, { "almaty", "Almaty Region" } };
    static const char *added[] = { "Quebec", "Washington", "Chiapas", "Zurdania" };
    const std::wstring ini = IniPath();
    const std::vector<std::wstring> sections = IniSections(ini);
    bool any = false, placeholders = false;
    for (const auto &name : sections)
        if (!_wcsnicmp(name.c_str(), L"Map:", 4)) any = true;
    for (const auto &r : renamed)
        if (IniHas(sections, MapSection(r.was))) placeholders = true;
    static const char *later[] = { "Switzerland" };
    std::wstring offered;
    {
        wchar_t b[512] = {};
        GetPrivateProfileStringW(L"Gauges", L"MapRegionsOffered", L"", b, 512, ini.c_str());
        offered = b;
    }
    bool more = false;
    for (const char *name : later)
    {
        const std::wstring w = Wide(name);
        if ((L"," + offered + L",").find(L"," + w + L",") != std::wstring::npos) continue;
        if (any)
            for (const auto &r : kMapRegions)
                if (!strcmp(r.name, name) && !IniHas(sections, MapSection(name))) IniPutFloat(MapSection(name).c_str(), L"AmbientC", r.ambientC, ini);
        offered += (offered.empty() ? L"" : L",") + w;
        more = true;
    }
    if (more) WritePrivateProfileStringW(L"Gauges", L"MapRegionsOffered", offered.c_str(), ini.c_str());
    if (!any)
    {
        for (const auto &r : kMapRegions) IniPutFloat(MapSection(r.name).c_str(), L"AmbientC", r.ambientC, ini);
        return;
    }
    if (!placeholders) return;
    for (const auto &r : renamed)
    {
        const std::wstring was = MapSection(r.was), now = MapSection(r.now);
        if (!IniHas(sections, was)) continue;
        wchar_t value[32] = {};
        GetPrivateProfileStringW(was.c_str(), L"AmbientC", L"", value, 32, ini.c_str());
        if (!IniHas(sections, now) && value[0]) WritePrivateProfileStringW(now.c_str(), L"AmbientC", value, ini.c_str());
        WritePrivateProfileStringW(was.c_str(), nullptr, nullptr, ini.c_str());
    }
    for (const char *name : added)
        for (const auto &r : kMapRegions)
            if (!strcmp(r.name, name) && !IniHas(sections, MapSection(name))) IniPutFloat(MapSection(name).c_str(), L"AmbientC", r.ambientC, ini);
}

// The ini into s (defaults for missing keys, every number held to its range); a missing ini is written with the
// defaults and how to use them.
static void ReadIni(GaugeSettings &s)
{
    SettingsDefaults(s);
    const std::wstring ini = IniPath();
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        FILE *f = nullptr;
        if (_wfopen_s(&f, ini.c_str(), L"w, ccs=UTF-16LE") == 0 && f)
        {
            fwprintf(f, L"; Gauges: the gauge cluster of Next Gen Gameplay for the truck being driven, drawn by the mod itself.\n"
                        L"; The Gameplay tab of the NEXT GEN menu (F2 opens it, or the key in NextGen.ini) edits everything here in the game.\n"
                        L"; Tab: the tab it opens on, the one open last (display, engine, gearbox, power, suspension, controls, sounds, modwork).\n"
                        L"; Layout: 0 = a strip along the bottom centre, 1 = a block in the bottom right corner, 2 = Spotter dials, 3 = Spotter digital (left of Spotter's gear panel).\n"
                        L"; SwapDials=1 puts the speedometer where the tachometer is and the tachometer where the speedometer is (not in Spotter digital).\n"
                        L"; Style (the strip and the block): 0 = solid dial faces, 1 = tinted see-through faces (FaceOpacity 0 to 0.85 sets how dark),\n"
                        L"; 2 = open: no faces, only the scales, numbers and needles.\n"
                        L"; OffsetX and OffsetY move the cluster from its place, in pixels of a 1080p screen (right and down).\n"
                        L"; Scale is its size (1 = as designed; it grows with the screen's height). Opacity 0.1 to 1.\n"
                        L"; Mph=1: speed in mph and range in miles. Psi=1: boost in psi. Fahrenheit=1: temperature in F.\n"
                        L"; ShowTemp, ShowBoost, ShowAir and ShowLamps switch the small dials and the lamps on (1) or off (0).\n"
                        L"; ShowClock=1 draws a clock dial with the game's time of day, the outside temperature in its face.\n"
                        L"; ShowFuelUse=1 draws the fuel used a minute, after the range under the fuel dial (litres a minute, gallons an hour with Mph).\n"
                        L"; OilGauge shows the oil temperature: 1 = a second needle (amber) in the temperature dial, 2 = a dial of its\n"
                        L"; own left of the temperature dial, 0 = not at all. The oil lamp is in the lamps either way.\n"
                        L"; TachClass sets the scales: 0 = truck (idle 650 rpm, red zone from 2200 of 3000, speedometer to 100 km/h),\n"
                        L"; 1 = scout (800, 5500 of 7000, 120 km/h), 2 = heavy (600, 1900 of 2500, 80 km/h).\n"
                        L"; IdleRpm, RedlineRpm, MaxRpm and SpeedMax (km/h) replace the class's numbers; 0 = the class's own.\n"
                        L"; The boost dial shows a turbo that blows by the fuel the engine burns and how fast it turns.\n"
                        L"; BoostMax is the boost in bar it reaches with the pedal floored at high engine speed (0.3 to 3);\n"
                        L"; 0 = the class's own: trucks 1.8 bar, scouts 1, heavy trucks 2.\n"
                        L"; BoostProfile is how the turbo gets there: 0 = the class's own, 1 = small and quick (full sooner, half the lag),\n"
                        L"; 2 = big and slow (later, more lag), 3 = two stages (full sooner with 1.3 times the boost).\n"
                        L"; A kind of truck can have these seven of its own. They stand in a file of its own in the folder Gauges\\trucks,\n"
                        L"; such as Gauges\\trucks\\chevrolet_ck1500.ini, which the mod writes when one of the seven is changed on the tab\n"
                        L"; while such a truck is driven; a file can be passed on and dropped in. The tab and Gauges.log name the truck.\n"
                        L"; A truck without a file takes the seven from [Gauges], and so does a truck's file for each of them it leaves out.\n"
                        L"; The tilt lamp is green while the truck stands level. It turns amber when the truck leans to a side by\n"
                        L"; RollWarnDeg degrees and red from RollDangerDeg, or when it points up or down a slope by PitchDangerDeg.\n"
                        L"; The coolant temperature is worked out from the fuel the engine burns (the pedal and the engine speed) and\n"
                        L"; how fast the truck drives.\n"
                        L"; WarmUpMinutes is the time a cold engine takes from 15 C to 85 C when it is driven as usual, OverheatMinutes\n"
                        L"; the time from 90 C into the red zone at 118 C floored at the red zone with the wheels spinning at a\n"
                        L"; standstill, CoolDownMinutes the time a stopped engine takes from 90 C down to 40 C. AmbientC is the outside\n"
                        L"; temperature in degrees C: a truck that has stood for long is that cold. The cold lamp is lit below 60 C.\n"
                        L"; A region can have an outside temperature of its own: AmbientC in a section named after it, such as\n"
                        L"; [Map:Michigan], in force while a truck drives there. A map can have one apart from its region's, in a\n"
                        L"; section named after the map, such as [Map:Black River]; a map of a mod goes by its id, such as\n"
                        L"; [Map:level_my_map]. The names are the game's own, and Gauges.log names the map. The mod learns the map\n"
                        L"; from the game's save, as the map loaded last. With AmbientByMap=1, the default, the map's section or its\n"
                        L"; region's is in force where the map is known; elsewhere, and everywhere with AmbientByMap=0, AmbientC is.\n"
                        L"; A section's air follows the game's day: DayC the warmest in the afternoon, NightC the coldest before\n"
                        L"; sunrise, such as DayC=3 and NightC=-12; a section with only AmbientC swings 10 C around it. The mod finds\n"
                        L"; the game's clock while you drive with Probe=1 and keeps its place under [Clock]; until then the mean holds.\n"
                        L"; AirFullKmh is the speed from which the radiator gets all the air that driving gives it (10 to 80 km/h).\n"
                        L"; Lower keeps a slow, hard pulling engine cooler.\n"
                        L"; The oil follows the coolant slowly and runs a little hotter under load. OilWarmUpMinutes is the time the oil\n"
                        L"; takes from 15 C to 85 C when the engine is driven as usual, a minute after the coolant at the least.\n"
                        L"; From OilWarnC (100 to 150 C) the oil lamp blinks and a warning sounds every few seconds.\n"
                        L"; Damage=1: the temperature costs the engine. An engine in the red zone takes the game's own engine damage:\n"
                        L"; HotDamagePercentPerMinute percent of what it can take a minute at 125 C, less below that and more above.\n"
                        L"; HotDamageFrom: the coolant temperature in C where the hot damage starts, 100 to 124 (118, the red zone's start); the rate above is reached at 125.\n"
                        L"; A cold engine (below 60 C) worked hard, burning more than half the fuel it burns floored at the red zone,\n"
                        L"; takes ColdDamagePercentPerMinute percent a minute floored at the red zone at 0 C, and less the warmer it is\n"
                        L"; and the lighter the work. A garage repairs this like any engine damage.\n"
                        L"; Damage=0: the temperature is only shown.\n"
                        L"; ColdDamagePercentPerMinute=0 switches the cold part off. Damage has not been tried in co-op: leave it at 0 there.\n"
                        L"; StallDamagePercent: what a shift without the clutch costs the gearbox, percent of what it takes, 0 to 25 (5), with Damage=1.\n"
                        L"; ClutchBurn=1: a clutch slipping under load heats up and costs the gearbox, a pedal's or the one that works by itself, with Damage=1.\n"
                        L"; ClutchHeatPercent: how fast it heats up, percent of the model's own pace, 10 to 300 (60); lower gives more time to react.\n"
                        L"; SpringScale, DampingScale (a test): the suspension's spring and damping as a factor of the truck's own, 0.25 to 4 (1 = the truck's own;\n"
                        L"; the game holds each at 1). ExtraTravel: cm added past the file's compression end, 0 to 30 (0 = the file's; past it a wheel may go into the body).\n"
                        L"; FrameFlex: the frame's twist spring as a factor of the truck's own, 0.1 to 7 (its damping by the square root); TwistRange: how far the frame\n"
                        L"; twists, 0.25 to 8 of the truck's own limits, at most 22 degrees (175 on an articulated truck's roll). 1 = the truck's own. Only a truck whose frame twists has them.\n"
                        L"; RideFront, RideRear: each axle's ride height in cm, -10 to 10; SwayFront, SwayRear: a sway bar, 0 to 0.3 of the axle's spring rate (BindSwayDisconnect\n"
                        L"; toggles them). Both move the attach point and stop on a truck whose game moves it. SuspDisplay=1 draws each wheel's travel. All tests, off by default.\n"
                        L"; AirSuspension=1 holds each wheel at its rest place of the first standstill, cargo or not (3 s to settle, under 10 km/h); AirHorizon=1 levels the body\n"
                        L"; to the horizon instead (about 7 degrees at most). BindRideUp, BindRideDown move it 2 cm under 20 km/h.\n"
                        L"; Only the axles with air follow them: by the mod's table, or AirAxles=all, rear (every axle but the front one) or none in [Truck] of the truck's file.\n"
                        L"; DamperCurve 0 linear, 1 digressive, 2 progressive; LowCompression, HighCompression, LowRebound, HighRebound: the damping\n"
                        L"; under and over DamperKnee (m/s, 0.02 to 0.5), rising and falling, 0.25 to 4 (a test; 1 and linear = the truck's own).\n"
                        L"; BumpStops=1: force (BumpForce, 1 to 6, the spring's at the end times it) and damping (BumpDamping, 1 to 4) over the last BumpZone m (0.03 to 0.2) of the\n"
                        L"; travel. SpringCurve 0 linear, 1 progressive, 2 dual rate (SpringRise 1 to 3, SpringCross 0.2 to 0.8). DampingZones=1: ZoneSoft (0.5 to 1)\n"
                        L"; from ZoneFrom to ZoneTo (shares of the travel), ZoneFirm (1 to 3) toward the ends. All tests, off by default.\n"
                        L"; SoundVolume (0 to 100) is how loud the turbo is heard when the pedal comes up quickly under boost: it flutters,\n"
                        L"; longer and louder the more boost there was, until the pedal goes down again. The oil warning goes by it too.\n"
                        L"; 0 = no sound. SoundsOnly=1 plays the sounds and draws no cluster.\n"
                        L"; A diesel has no blow-off valve: its lift is quiet, or plays sounds\\turbo_whistle_1.wav to _4.wav. DieselFlutter=1: it flutters.\n"
                        L"; WAV files sounds\\turbo_high_1.wav to _4.wav next to the game's exe (and turbo_mid_, turbo_low_ for less boost)\n"
                        L"; are played in place of the mod's own recordings of that strength, and sounds\\oil_beep_1.wav in place of the\n"
                        L"; oil warning.\n"
                        L"; ToggleKey is the virtual key code of a key that hides and shows the cluster (0 = none; 120 = F9).\n"
                        L"; PadToggle does the same with pad buttons (XInput): DPadUp, DPadDown, DPadLeft, DPadRight, A, B, X, Y,\n"
                        L"; LB, RB, LS, RS, Start, Back, joined with + (LB+DPadUp), or None. The game sees these buttons too.\n"
                        L"; The cluster is on screen while a truck is driven in the world: not on loading screens, in menus, the garage or the map.\n"
                        L"; Gearbox: 0 the game's own. 1 the mod's automatic in A (a torque converter, shifts up later the further the pedal\n"
                        L"; is down, a kickdown floored), the game's shifter yours, only its reverse on the brake refused. A position picked\n"
                        L"; on the shifter (L+ among them) hands the truck back until A is picked, so the automatic never shifts out of it. 2 the mod's\n"
                        L"; own gearbox takes the shifter: D, the numbered gears, N, R, H, L-, L, L+ on the bindings below (as 1 until one reaches R).\n"
                        L"; D is the mod's automatic, drawn so on the cluster; the game's shifter and this ini call it A (GearA, BackToA).\n"
                        L"; [Gearbox] Bind<Action> and Bind<Action>2: two bindings each for Gear1 to Gear16, GearH, GearL-, GearL, GearL+, GearN,\n"
                        L"; GearR, GearA, GearUp, GearDown, Clutch, RangeHigh, RangeLow, and at every Gearbox level DiffLock and Awd (the\n"
                        L"; differential lock and all-wheel drive flipped, on every truck, the upgrade fitted or not; none by default).\n"
                        L"; A binding is Key <name or code>, Pad <buttons with +, a stick's direction LSUp to RSRight among them>,\n"
                        L"; Wheel <n> Button <n>, Wheel <n> POV [<n>] Up/Right/Down/Left, Wheel <n> Axis <n> [inverted] (the clutch), or None.\n"
                        L"; The Controls tab binds them by a press. Without a key in the ini the defaults below hold:\n"
                        L";   BindGearUp=Key Num 8\n"
                        L";   BindGearDown=Key Num 2\n"
                        L";   BindGearN=Key Num 5\n"
                        L";   BindGearR=Key Num 0\n"
                        L";   BindGearA=Key Num 7\n"
                        L"; RequireClutch=1 stalls the engine on a shift into a gear without the clutch, RequireGearHeld=1 goes to N while no\n"
                        L"; gear key is held (an H shifter with every gear bound), SkipNeutral=1 steps from L- to reverse and back past N.\n"
                        L"; GearUp and GearDown walk R, N, L-, L, L+, 1, 2 and on to the top gear, on every truck. Up from L+ goes to 1\n"
                        L"; a second after an up press got there. A 1st from the low range stays held until the next shift (never to D).\n"
                        L"; DownStaysForward=1: GearDown stops at L-, N and R only on their own keys (wins over SkipNeutral).\n"
                        L"; At Gearbox=2 with Input 1 or 2 the game's own shifter picks a position too; with Input 0 its shifts stay refused.\n"
                        L"; IgnoreShifter=1: at Gearbox=2 a position picked on the game's shifter does nothing, with any Input.\n"
                        L"; BackToA: at Gearbox=2, seconds after a shift by hand into a numbered gear until it goes back to D by itself\n"
                        L"; (0 never, else 3 to 30); never with RequireGearHeld, or for a gear picked on the game's shifter.\n"
                        L"; Input is what you drive with: 0 a wheel's pedals, 1 a pad's trigger, 2 the keyboard. The mod's automatic kicks\n"
                        L"; down by it: a trigger has to come from under three quarters, and keys kick nothing down and shift as floored.\n"
                        L"; Powertrain=1: the truck pulls with the mod's engine: its torque curve against the engine speed the needle\n"
                        L"; shows, less with cold oil and without boost, and doubled by the converter at its stall (Gearbox=1), in place\n"
                        L"; of the game's own, which falls away toward each gear's top. The mod writes the game's torque factor for it.\n"
                        L"; EngineShare: how much of the mod's engine the game gets, 0 to 100 percent (50: half way between the two).\n"
                        L"; RoadLoad=1 (with Powertrain, on by default): the air's drag taken off, once each gear's pull is learned floored on flat ground with the wheels' drag left out.\n"
                        L";   RoadCdA: the drag's area in m2, 0 by the truck's mass (1.1 under 4 t, 3.5 under 10 t, else 6); a truck's file may set it.\n"
                        L";   Base1.<engine>, Base2.<engine> ... in a truck's file: each gear's acceleration at the game's own pull, floored, learned.\n"
                        L"; Creep=1: an idling truck on an automatic moves to walking pace; the brake holds it. For now only with an automatic, not in a gear picked by hand.\n"
                        L"; CreepManual=1: the creep in a gear picked by hand too (the gear's idle speed; stuck at Gearbox=2 it stalls; R and L on the game's shifter through the converter).\n"
                        L"; Gauges\\gearboxes\\<name>.ini GearHold: seconds a gearbox holds each gear going up with Powertrain=1, 1 to 3 (Upshift delay).\n"
                        L"; TopShare<n>.<gearbox> in a truck's file: each gear's top as a share of 2a + 5, learned where the game's pull reaches nothing floored.\n"
                        L"; Gauges\\engines\\<name>.ini: PeakTorqueRpm, Aspiration (0 turbo, 1 naturally aspirated), Fuel (diesel, petrol), TorqueCurve (the Engine tab).\n"
                        L";   Torque: how much of its torque the truck gets, 10 to 100 percent (100 when not set). Lower it for an RLM-like engine with several times a stock engine's torque.\n"
                        L"; A truck's own Torque.<engine> and GearHold.<gearbox>: [Engine] of its file in Gauges\\trucks (every truck of the kind) or [Engine:<id>] (one truck), with Own; before the two files above.\n"
                        L"; Mod work: Demo=1 draws demo values whenever no truck is driven, Probe=1 logs the values read from the game.\n"
                        L"; KeepLogs: how many of the last starts' logs Gauges\\logs keeps (20; at most 500 MB of them; 0 keeps all).\n");
            fclose(f);
            WriteIni(s);
            MapDefaultsWrite();
        }
        return;
    }
    MapDefaultsWrite();
    int moved[kSetupKinds];
    SetupsMove(moved);
    if (moved[kSetupTruck] || moved[kSetupGearbox])
        Log(L"settings: %d trucks' scales and %d gearboxes' holds moved out of Gauges.ini, each into a file of its own in Gauges\\trucks and "
            L"Gauges\\gearboxes", moved[kSetupTruck], moved[kSetupGearbox]);
    s.layout = IniInt(L"Layout", s.layout, ini);
    s.swapDials = IniBool(L"SwapDials", s.swapDials, ini);
    s.style = IniInt(L"Style", s.style, ini);
    s.faceOpacity = IniFloat(L"FaceOpacity", s.faceOpacity, ini);
    s.offsetX = IniFloat(L"OffsetX", s.offsetX, ini);
    s.offsetY = IniFloat(L"OffsetY", s.offsetY, ini);
    s.scale = IniFloat(L"Scale", s.scale, ini);
    s.opacity = IniFloat(L"Opacity", s.opacity, ini);
    s.mph = IniBool(L"Mph", s.mph, ini);
    s.psi = IniBool(L"Psi", s.psi, ini);
    s.fahrenheit = IniBool(L"Fahrenheit", s.fahrenheit, ini);
    s.showTemp = IniBool(L"ShowTemp", s.showTemp, ini);
    s.showBoost = IniBool(L"ShowBoost", s.showBoost, ini);
    s.showAir = IniBool(L"ShowAir", s.showAir, ini);
    s.showClock = IniBool(L"ShowClock", s.showClock, ini);
    s.showFuelUse = IniBool(L"ShowFuelUse", s.showFuelUse, ini);
    s.showLamps = IniBool(L"ShowLamps", s.showLamps, ini);
    s.oilGauge = IniInt(L"OilGauge", s.oilGauge, ini);
    s.tachClass = IniInt(L"TachClass", s.tachClass, ini);
    s.idleRpm = IniFloat(L"IdleRpm", s.idleRpm, ini);
    s.redlineRpm = IniFloat(L"RedlineRpm", s.redlineRpm, ini);
    s.maxRpm = IniFloat(L"MaxRpm", s.maxRpm, ini);
    s.speedMax = IniFloat(L"SpeedMax", s.speedMax, ini);
    s.boostMax = IniFloat(L"BoostMax", s.boostMax, ini);
    s.boostProfile = IniInt(L"BoostProfile", s.boostProfile, ini);
    s.gearbox = IniInt(L"Gearbox", s.gearbox, ini);
    s.requireClutch = IniBool(L"RequireClutch", s.requireClutch, ini);
    s.requireGearHeld = IniBool(L"RequireGearHeld", s.requireGearHeld, ini);
    s.skipNeutral = IniBool(L"SkipNeutral", s.skipNeutral, ini);
    s.downStaysForward = IniBool(L"DownStaysForward", s.downStaysForward, ini);
    s.ignoreShifter = IniBool(L"IgnoreShifter", s.ignoreShifter, ini);
    s.backToA = IniInt(L"BackToA", s.backToA, ini);
    s.input = IniInt(L"Input", s.input, ini);
    s.powertrain = IniBool(L"Powertrain", s.powertrain, ini);
    s.roadLoad = IniBool(L"RoadLoad", s.roadLoad, ini);
    s.roadCdA = IniFloat(L"RoadCdA", s.roadCdA, ini);
    s.engineShare = IniInt(L"EngineShare", s.engineShare, ini);
    s.creep = IniBool(L"Creep", s.creep, ini);
    s.creepManual = IniBool(L"CreepManual", s.creepManual, ini);
    s.rollWarnDeg = IniFloat(L"RollWarnDeg", s.rollWarnDeg, ini);
    s.rollDangerDeg = IniFloat(L"RollDangerDeg", s.rollDangerDeg, ini);
    s.pitchDangerDeg = IniFloat(L"PitchDangerDeg", s.pitchDangerDeg, ini);
    s.warmUpMinutes = IniFloat(L"WarmUpMinutes", s.warmUpMinutes, ini);
    s.overheatMinutes = IniFloat(L"OverheatMinutes", s.overheatMinutes, ini);
    s.coolDownMinutes = IniFloat(L"CoolDownMinutes", s.coolDownMinutes, ini);
    s.ambientC = IniFloat(L"AmbientC", s.ambientC, ini);
    s.ambientByMap = IniBool(L"AmbientByMap", s.ambientByMap, ini);
    s.airFullKmh = IniFloat(L"AirFullKmh", s.airFullKmh, ini);
    s.oilWarmUpMinutes = IniFloat(L"OilWarmUpMinutes", s.oilWarmUpMinutes, ini);
    s.oilWarnC = IniFloat(L"OilWarnC", s.oilWarnC, ini);
    s.damage = IniBool(L"Damage", s.damage, ini);
    s.hotDamage = IniFloat(L"HotDamagePercentPerMinute", s.hotDamage, ini);
    s.hotDamageFrom = IniFloat(L"HotDamageFrom", s.hotDamageFrom, ini);
    s.coldDamage = IniFloat(L"ColdDamagePercentPerMinute", s.coldDamage, ini);
    s.stallDamage = IniFloat(L"StallDamagePercent", s.stallDamage, ini);
    s.clutchBurn = IniBool(L"ClutchBurn", s.clutchBurn, ini);
    s.clutchHeat = IniFloat(L"ClutchHeatPercent", s.clutchHeat, ini);
    s.springScale = IniFloat(L"SpringScale", s.springScale, ini);
    s.dampingScale = IniFloat(L"DampingScale", s.dampingScale, ini);
    s.frameFlex = IniFloat(L"FrameFlex", s.frameFlex, ini);
    s.twistRange = IniFloat(L"TwistRange", s.twistRange, ini);
    s.extraTravel = IniFloat(L"ExtraTravel", s.extraTravel, ini); // TravelMinScale and TravelMaxScale are no longer read
    s.rideFront = IniFloat(L"RideFront", s.rideFront, ini);
    s.rideRear = IniFloat(L"RideRear", s.rideRear, ini);
    s.swayFront = IniFloat(L"SwayFront", s.swayFront, ini);
    s.swayRear = IniFloat(L"SwayRear", s.swayRear, ini);
    {
        // an ini from build B may hold up to 1.5; above 0.3 the bars swing, so it is held there (said once a start)
        static bool said = false;
        if (!said && (s.swayFront > kSwayRange[1] || s.swayRear > kSwayRange[1]))
        {
            GaugeLog(L"settings: SwayFront %.2f, SwayRear %.2f held to %.1f (above it the bars swing)", s.swayFront, s.swayRear, kSwayRange[1]);
            said = true;
        }
    }
    s.suspDisplay = IniInt(L"SuspDisplay", s.suspDisplay ? 1 : 0, ini) != 0;
    s.airSuspension = IniInt(L"AirSuspension", s.airSuspension ? 1 : 0, ini) != 0;
    s.airHorizon = IniInt(L"AirHorizon", s.airHorizon ? 1 : 0, ini) != 0;
    s.damperCurve = IniInt(L"DamperCurve", s.damperCurve, ini);
    s.lowCompression = IniFloat(L"LowCompression", s.lowCompression, ini);
    s.highCompression = IniFloat(L"HighCompression", s.highCompression, ini);
    s.lowRebound = IniFloat(L"LowRebound", s.lowRebound, ini);
    s.highRebound = IniFloat(L"HighRebound", s.highRebound, ini);
    s.damperKnee = IniFloat(L"DamperKnee", s.damperKnee, ini);
    s.bumpStops = IniInt(L"BumpStops", s.bumpStops ? 1 : 0, ini) != 0;
    s.bumpZone = IniFloat(L"BumpZone", s.bumpZone, ini);
    s.bumpForce = IniFloat(L"BumpForce", s.bumpForce, ini);
    s.bumpDamping = IniFloat(L"BumpDamping", s.bumpDamping, ini);
    s.springCurve = IniInt(L"SpringCurve", s.springCurve, ini);
    s.springRise = IniFloat(L"SpringRise", s.springRise, ini);
    s.springCross = IniFloat(L"SpringCross", s.springCross, ini);
    s.dampingZones = IniInt(L"DampingZones", s.dampingZones ? 1 : 0, ini) != 0;
    s.zoneSoft = IniFloat(L"ZoneSoft", s.zoneSoft, ini);
    s.zoneFirm = IniFloat(L"ZoneFirm", s.zoneFirm, ini);
    s.zoneFrom = IniFloat(L"ZoneFrom", s.zoneFrom, ini);
    s.zoneTo = IniFloat(L"ZoneTo", s.zoneTo, ini);
    s.suspStepTest = IniInt(L"SuspStepTest", s.suspStepTest, ini); // a test: read, never written
    s.shiftOnGameThread = IniInt(L"ShiftOnGameThread", s.shiftOnGameThread, ini); // review 17, step 1: read, never written
    s.toggleKey = IniInt(L"ToggleKey", s.toggleKey, ini);
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(kIniSection, L"PadToggle", Wide(s.padToggle).c_str(), buf, 64, ini.c_str());
    size_t n = 0;
    for (const wchar_t *p = buf; *p && n + 1 < sizeof s.padToggle; p++) s.padToggle[n++] = *p < 128 ? (char)*p : '?';
    s.padToggle[n] = 0;
    s.soundVolume = IniInt(L"SoundVolume", s.soundVolume, ini);
    s.soundsOnly = IniBool(L"SoundsOnly", s.soundsOnly, ini);
    s.dieselFlutter = IniBool(L"DieselFlutter", s.dieselFlutter, ini);
    s.demo = IniBool(L"Demo", s.demo, ini);
    s.probe = IniInt(L"Probe", s.probe, ini);
    s.keepLogs = IniInt(L"KeepLogs", s.keepLogs, ini);
    SettingsClamp(s);
}

LONG SettingsGet(GaugeSettings &out)
{
    AcquireSRWLockShared(&g_setLock);
    out = g_set;
    const LONG turn = g_setTurn;
    ReleaseSRWLockShared(&g_setLock);
    return turn;
}

// The tab made in from the settings of a turn. When the mod's thread has put others in force since (another truck
// is driven, the ini was read again), in is not an edit of those and is dropped.
void SettingsPut(const GaugeSettings &in, LONG turn)
{
    AcquireSRWLockExclusive(&g_setLock);
    if (turn == g_setTurn)
    {
        g_set = in;
        InterlockedExchange(&g_setDirty, 1);
    }
    ReleaseSRWLockExclusive(&g_setLock);
}

// The settings in force, for the tab to show. What the tab handed back and the mod's thread has not taken yet is
// dropped, as is what it hands back from here on for the settings before these: neither is an edit of these.
static void SettingsPublish(const GaugeSettings &s)
{
    AcquireSRWLockExclusive(&g_setLock);
    g_set = s;
    g_setTurn++;
    InterlockedExchange(&g_setDirty, 0);
    ReleaseSRWLockExclusive(&g_setLock);
}

void SettingsReload() { InterlockedExchange(&g_reloadAsked, 1); }
void SettingsTruckDrop() { InterlockedExchange(&g_truckDropAsked, 1); }
void SettingsSuspOwn(bool on) { InterlockedExchange(&g_suspOwnAsked, on ? 1 : 2); }
void SettingsSuspToDefault() { InterlockedExchange(&g_suspDefaultAsked, 1); }
void SettingsSuspLevel(bool truckOnly) { InterlockedExchange(&g_suspLevelAsked, truckOnly ? 1 : 2); }
void EngineOwnAsk(bool on) { InterlockedExchange(&g_engOwnAsked, on ? 1 : 2); }
void EngineToDefaultAsk() { InterlockedExchange(&g_engDefaultAsked, 1); }
void EngineLevelAsk(bool truckOnly) { InterlockedExchange(&g_engLevelAsked, truckOnly ? 1 : 2); }
void SettingsReset() { InterlockedExchange(&g_resetAsked, 1); }
void PullTestAsk(bool run) { InterlockedExchange(&g_pullTestAsked, run ? 1 : 2); }
void MapAmbientSet(float c)
{
    InterlockedExchange(&g_mapAmbientTenths, (LONG)lroundf(c * 10.0f));
    InterlockedExchange(&g_mapAmbientAsked, 1);
}

void EngineProfileSet(float peakRpm, int aspiration, int fuel)
{
    InterlockedExchange(&g_enginePeak, (LONG)lroundf(peakRpm));
    InterlockedExchange(&g_engineAspiration, aspiration);
    InterlockedExchange(&g_engineFuel, fuel);
    InterlockedExchange(&g_engineAsked, 1);
}

void EngineProfileDrop() { InterlockedExchange(&g_engineAsked, 2); }


void EngineTorqueSet(int percent)
{
    InterlockedExchange(&g_engineTorquePercent, percent);
    InterlockedExchange(&g_engineTorqueAsked, 1);
}

void GearHoldSet(float seconds)
{
    InterlockedExchange(&g_gearHoldTenths, (LONG)lroundf(seconds * 10.0f));
    InterlockedExchange(&g_gearHoldAsked, 1);
}

// ---- a gearbox's own hold: its file Gauges\gearboxes\<name>.ini ----
// GearHold: the seconds each gear of that gearbox is held going up with the mod's engine (kGearHoldRange); without
// it the truck's class has it (kEngineGearHold, kEngineGearHoldScout). The key is GearboxKey's: the gearbox's name in
// the game's files, or its gears' speeds where the name is not read.

// The gearbox's own hold, into hold; false where its file has none.
static bool GearHoldRead(const char *key, float &hold)
{
    wchar_t buf[16] = {};
    if (!key[0] || GetPrivateProfileStringW(kSetupSections[kSetupGearbox], L"GearHold", L"", buf, 16, SetupFile(kSetupGearbox, key).c_str()) == 0) return false;
    const float v = (float)_wtof(buf);
    if (!isfinite(v) || v <= 0.0f) return false;
    hold = SettingsRange(v, kGearHoldRange[0], kGearHoldRange[1]);
    return true;
}

static void GearHoldWrite(const char *key, float hold)
{
    wchar_t buf[16];
    swprintf_s(buf, L"%.1f", hold);
    SetupBegin(kSetupGearbox, Wide(key));
    WritePrivateProfileStringW(kSetupSections[kSetupGearbox], L"GearHold", buf, SetupFile(kSetupGearbox, key).c_str());
}

// A gearbox known by its gears whose file stands under another key of the same gears within kGearboxDrift (one
// gearbox under four keys in the logs of October and November, each a new file, the hold back at the class's: the
// author's "gearbox gear hold time resets now and then"). Into out the file's key, the one written first where more
// match; false where none does.
static bool GearboxFileMatch(const char *key, char *out, size_t size)
{
    WIN32_FIND_DATAW f;
    const HANDLE h = FindFirstFileW((SetupFolder(kSetupGearbox) + L"\\gears *.ini").c_str(), &f);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    FILETIME first = {};
    do
    {
        char name[MAX_PATH];
        const int n = WideCharToMultiByte(CP_UTF8, 0, f.cFileName, -1, name, sizeof name, nullptr, nullptr);
        if (n <= 5) continue;
        name[n - 5] = 0; // ".ini" off
        if (!GearboxKeysMatch(key, name)) continue;
        if (!found || CompareFileTime(&f.ftCreationTime, &first) < 0)
        {
            first = f.ftCreationTime;
            strncpy_s(out, size, name, _TRUNCATE);
            found = true;
        }
    } while (FindNextFileW(h, &f));
    FindClose(h);
    return found;
}

// Top1 .. Top<n> of a gearbox's file: each numbered gear's top in rad/s, as builds before 0.18.4 learned it (review 11).
// Not used for a truck any more (TopShare below): only counted, for the log's note and for finding a gearbox's file by
// its gears (GearboxFileMatch). Into tops, 0 for a gear without one; the number read.
static int GearTopRead(const char *key, float tops[kMaxGears])
{
    int n = 0;
    for (int g = 0; g < kMaxGears; g++) tops[g] = 0.0f;
    if (!key[0]) return 0;
    for (int g = 1; g < kMaxGears; g++)
    {
        wchar_t name[16], buf[24] = {};
        swprintf_s(name, L"Top%d", g);
        if (GetPrivateProfileStringW(kSetupSections[kSetupGearbox], name, L"", buf, 24, SetupFile(kSetupGearbox, key).c_str()) == 0) continue;
        const float v = (float)_wtof(buf);
        if (isfinite(v) && v > 0.0f && v < 1000.0f) tops[g] = v, n++;
    }
    return n;
}

// Base<n>.<engine>: each numbered gear's acceleration at the game's own pull, floored, at its pull's peak, in m/s²
// (BaseLearnStep), for the road load, in the truck's file by the engine fitted, as it is the truck's (engine, mass,
// wheels, gearbox) and an engine upgrade starts over (review F2); 0 for a gear without one. The number read.
// BaseDamp.<engine>=1 beside them marks figures learned with the wheels' drag added back (BRIEF_air_drag_wheel_damping.md).
// Without the mark the engine's Base<n> are the old ones, learned with the drag in them: the read deletes them all, then
// writes the mark, in that one step (the Reviewer's point 5), so no old figure ever sits under the mark. A crash between
// the two leaves no mark, and the next read deletes again. dropped: the old figures deleted.
static bool GearBaseMarked(const char *truck, const char *engine)
{
    wchar_t name[kMaxText + 16], buf[8] = {};
    swprintf_s(name, L"BaseDamp.%S", engine);
    return GetPrivateProfileStringW(kSetupSections[kSetupTruck], name, L"", buf, 8, SetupFile(kSetupTruck, truck).c_str()) > 0 && _wtoi(buf) == 1;
}
static void GearBaseMark(const char *truck, const char *engine)
{
    wchar_t name[kMaxText + 16];
    swprintf_s(name, L"BaseDamp.%S", engine);
    SetupBegin(kSetupTruck, Wide(truck));
    WritePrivateProfileStringW(kSetupSections[kSetupTruck], name, L"1", SetupFile(kSetupTruck, truck).c_str());
}
static int GearBaseRead(const char *truck, const char *engine, float bases[kMaxGears], int *dropped = nullptr)
{
    int n = 0;
    if (dropped) *dropped = 0;
    for (int g = 0; g < kMaxGears; g++) bases[g] = 0.0f;
    if (!truck[0] || !engine[0]) return 0;
    const std::wstring file = SetupFile(kSetupTruck, truck);
    if (!GearBaseMarked(truck, engine))
    {
        int old = 0;
        for (int g = 1; g < kMaxGears; g++)
        {
            wchar_t name[kMaxText + 16], buf[24] = {};
            swprintf_s(name, L"Base%d.%S", g, engine);
            if (GetPrivateProfileStringW(kSetupSections[kSetupTruck], name, L"", buf, 24, file.c_str()) == 0) continue;
            WritePrivateProfileStringW(kSetupSections[kSetupTruck], name, nullptr, file.c_str());
            old++;
        }
        // no file is made for a truck with nothing to drop: GearBaseWrite marks the first figure's file
        if (old) GearBaseMark(truck, engine);
        if (dropped) *dropped = old;
        return 0;
    }
    for (int g = 1; g < kMaxGears; g++)
    {
        wchar_t name[kMaxText + 16], buf[24] = {};
        swprintf_s(name, L"Base%d.%S", g, engine);
        if (GetPrivateProfileStringW(kSetupSections[kSetupTruck], name, L"", buf, 24, file.c_str()) == 0) continue;
        const float v = (float)_wtof(buf);
        if (isfinite(v) && v > 0.0f && v < 100.0f) bases[g] = v, n++;
    }
    return n;
}

// A figure, and the mark beside it: the read before it found the mark or deleted every old figure, so all the
// engine's figures in the file are new ones.
static void GearBaseWrite(const char *truck, const char *engine, int gear, float base)
{
    if (!truck[0] || !engine[0] || gear < 1 || gear >= kMaxGears) return;
    wchar_t name[kMaxText + 16], buf[24];
    swprintf_s(name, L"Base%d.%S", gear, engine);
    swprintf_s(buf, L"%.3f", base);
    SetupBegin(kSetupTruck, Wide(truck));
    WritePrivateProfileStringW(kSetupSections[kSetupTruck], name, buf, SetupFile(kSetupTruck, truck).c_str());
    GearBaseMark(truck, engine);
}

// TopShare<n>.<gearbox> in [Truck] of the truck kind's file (BRIEF_gear_tops_per_truck.md, the Reviewer's point 1):
// each numbered gear's top as its share of 2 a + 5, learned (GearTopStep), per kind and gearbox. A share, as one
// gearbox's name comes with other gear speeds on another truck, and the name new, so an old absolute Top<n> is never
// read as one. The gearbox's key goes in as SetupFileName cleans a name, with = ; [ ] as _.
static std::wstring GearTopShareName(int gear, const char *gearbox)
{
    std::wstring part = SetupFileName(Wide(gearbox));
    for (wchar_t &c : part)
        if (c == L'=' || c == L';' || c == L'[' || c == L']') c = L'_';
    wchar_t head[24];
    swprintf_s(head, L"TopShare%d.", gear);
    return head + part;
}
// Into shares, 0 for a gear without a usable one (GearTopShareUsable); the number read. outside: shares there but out
// of the plateau's window, not used.
static int GearTopShareRead(const char *truck, const char *gearbox, float shares[kMaxGears], int *outside = nullptr)
{
    int n = 0;
    if (outside) *outside = 0;
    for (int g = 0; g < kMaxGears; g++) shares[g] = 0.0f;
    if (!truck[0] || !gearbox[0]) return 0;
    const std::wstring file = SetupFile(kSetupTruck, truck);
    for (int g = 1; g < kMaxGears; g++)
    {
        wchar_t buf[24] = {};
        if (GetPrivateProfileStringW(kSetupSections[kSetupTruck], GearTopShareName(g, gearbox).c_str(), L"", buf, 24, file.c_str()) == 0) continue;
        const float v = (float)_wtof(buf);
        if (GearTopShareUsable(v)) shares[g] = v, n++;
        else if (outside) ++*outside;
    }
    return n;
}
static void GearTopShareWrite(const char *truck, const char *gearbox, int gear, float share)
{
    if (!truck[0] || !gearbox[0] || gear < 1 || gear >= kMaxGears) return;
    wchar_t buf[24];
    swprintf_s(buf, L"%.4f", share);
    SetupBegin(kSetupTruck, Wide(truck));
    WritePrivateProfileStringW(kSetupSections[kSetupTruck], GearTopShareName(gear, gearbox).c_str(), buf, SetupFile(kSetupTruck, truck).c_str());
}

// A failed drag read's reason for the log (WheelDampWhy): the figure and its value, the wheel counted from 1.
static std::wstring DampWhyText(const WheelDampWhy &y, int wheels)
{
    wchar_t t[160];
    const int n = y.wheel + 1;
    switch (y.what)
    {
    case kDampNoList: swprintf_s(t, L"the wheel list short: %d wheels read of %.0f models", wheels, y.value); break;
    case kDampNotRead: swprintf_s(t, L"wheel %d of %d: its body not read (or not a cylinder)", n, wheels); break;
    case kDampNoBodies: swprintf_s(t, L"the truck's bodies' mass %.0f kg", y.value); break;
    case kDampMass: swprintf_s(t, L"wheel %d of %d: inverse mass %.6f", n, wheels, y.value); break;
    case kDampDamping: swprintf_s(t, L"wheel %d of %d: damping %.1f", n, wheels, y.value); break;
    case kDampAxle: swprintf_s(t, L"wheel %d of %d: no axle (inertia %.1f, %.1f, %.1f)", n, wheels, y.inertia[0], y.inertia[1], y.inertia[2]); break;
    case kDampRadius: swprintf_s(t, L"wheel %d of %d: radius %.3f", n, wheels, y.value); break;
    case kDampSpin: swprintf_s(t, L"wheel %d of %d: spin %.3f", n, wheels, y.value); break;
    default: swprintf_s(t, L"read"); break;
    }
    return t;
}

// ---- an engine's own profile: its file Gauges\engines\<name>.ini ----
// PeakTorqueRpm: the engine speed of its peak torque (100 to 20000 rpm), the curve built around it (EngineCurveFromPeak);
// Aspiration: 0 turbo, 1 naturally aspirated (no boost, no turbo sounds, no boost dial); Fuel: diesel or petrol, in
// place of the class's (trucks and heavy trucks diesel, scouts petrol; review 12); TorqueCurve: a whole curve of
// its own, "x:y,x:y,..." with x the engine speed over the red zone's and y the torque over its most, in place of the
// one from the peak. Torque: how much of its torque
// the truck gets, in percent (10 to 100, kTorqueShareRange), the whole push the hand gives scaled by it (TorqueHeld).
// The key is the engine's name in the game's files (EngineRecord::name).
static const float kPeakRpmRange[2] = { 100.0f, 20000.0f };

// The engine's profile, into p; false where its file holds none of them.
static bool EngineProfileRead(const char *name, EngineProfile &p)
{
    p = EngineProfile{};
    if (!name[0]) return false;
    const std::wstring file = SetupFile(kSetupEngine, name);
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    const wchar_t *section = kSetupSections[kSetupEngine];
    wchar_t buf[256] = {};
    if (GetPrivateProfileStringW(section, L"PeakTorqueRpm", L"", buf, 256, file.c_str()) > 0)
    {
        const float v = (float)_wtof(buf);
        p.peakRpm = isfinite(v) && v > 0.0f ? SettingsRange(v, kPeakRpmRange[0], kPeakRpmRange[1]) : 0.0f;
        p.own = true;
    }
    if (GetPrivateProfileStringW(section, L"Aspiration", L"", buf, 256, file.c_str()) > 0)
    {
        const int a = _wtoi(buf);
        p.aspiration = a >= 0 && a < kAspCount ? a : kAspTurbo;
        p.own = true;
    }
    if (GetPrivateProfileStringW(section, L"Fuel", L"", buf, 256, file.c_str()) > 0)
    {
        p.fuel = !_wcsicmp(buf, L"diesel") ? kFuelDiesel : !_wcsicmp(buf, L"petrol") ? kFuelPetrol : kFuelClass;
        p.own = true;
    }
    if (GetPrivateProfileStringW(section, L"TorqueCurve", L"", buf, 256, file.c_str()) > 0)
    {
        char text[256];
        size_t n = 0;
        for (; buf[n] && n + 1 < sizeof text; n++) text[n] = buf[n] < 128 ? (char)buf[n] : '?';
        text[n] = 0;
        p.custom = TorqueCurveParse(text, p.curve);
        p.own = true;
    }
    if (GetPrivateProfileStringW(section, L"Torque", L"", buf, 256, file.c_str()) > 0)
    {
        const float v = (float)_wtof(buf);
        p.torque = isfinite(v) && v > 0.0f ? SettingsRange(v / 100.0f, kTorqueShareRange[0], kTorqueShareRange[1]) : 0.0f;
        p.own = true;
    }
    return p.own;
}

// The tab's Engine section into the engine's file: the peak torque's speed, the aspiration and the fuel (none
// written for the class's).
static void EngineProfileWrite(const char *name, float peakRpm, int aspiration, int fuel)
{
    const std::wstring file = SetupFile(kSetupEngine, name);
    SetupBegin(kSetupEngine, Wide(name));
    IniPutFloat(kSetupSections[kSetupEngine], L"PeakTorqueRpm", roundf(SettingsRange(peakRpm, kPeakRpmRange[0], kPeakRpmRange[1])), file);
    IniPutInt(kSetupSections[kSetupEngine], L"Aspiration", aspiration >= 0 && aspiration < kAspCount ? aspiration : kAspTurbo, file);
    WritePrivateProfileStringW(kSetupSections[kSetupEngine], L"Fuel", fuel == kFuelDiesel ? L"diesel" : fuel == kFuelPetrol ? L"petrol" : nullptr, file.c_str());
}

// The tab's slider into the engine's file: how much of its torque the truck gets, in percent.
static void EngineTorqueWrite(const char *name, int percent)
{
    const std::wstring file = SetupFile(kSetupEngine, name);
    SetupBegin(kSetupEngine, Wide(name));
    IniPutInt(kSetupSections[kSetupEngine], L"Torque", (int)lroundf(SettingsRange((float)percent, kTorqueShareRange[0] * 100.0f, kTorqueShareRange[1] * 100.0f)), file);
}

// ---- a truck's own scales: its file Gauges\trucks\<kind>.ini ----
// A truck can have seven settings of its own: TachClass, IdleRpm, RedlineRpm, MaxRpm, SpeedMax, BoostMax and BoostProfile. They are written
// when one of them is changed on the tab while that truck is driven, and take the place of the plain ones of
// [Gauges] whenever that truck is driven again. The key is the truck's as truck.h makes it (TruckKey).

// Lays the truck's own scales over s: each of the seven its file holds. One the file does not hold stays as it is in
// s. False when the file holds none of them, or is not there.
static bool ReadTruckIni(const char *key, GaugeSettings &s)
{
    const std::wstring ini = SetupFile(kSetupTruck, key), section = kSetupSections[kSetupTruck];
    bool any = false;
    auto has = [&](const wchar_t *name) {
        wchar_t buf[8] = {};
        const bool there = GetPrivateProfileStringW(section.c_str(), name, L"", buf, 8, ini.c_str()) > 0;
        any = any || there;
        return there;
    };
    if (has(L"TachClass")) s.tachClass = (int)GetPrivateProfileIntW(section.c_str(), L"TachClass", s.tachClass, ini.c_str());
    if (has(L"IdleRpm")) s.idleRpm = IniFloatIn(section.c_str(), L"IdleRpm", s.idleRpm, ini);
    if (has(L"RedlineRpm")) s.redlineRpm = IniFloatIn(section.c_str(), L"RedlineRpm", s.redlineRpm, ini);
    if (has(L"MaxRpm")) s.maxRpm = IniFloatIn(section.c_str(), L"MaxRpm", s.maxRpm, ini);
    if (has(L"SpeedMax")) s.speedMax = IniFloatIn(section.c_str(), L"SpeedMax", s.speedMax, ini);
    if (has(L"BoostMax")) s.boostMax = IniFloatIn(section.c_str(), L"BoostMax", s.boostMax, ini);
    if (has(L"BoostProfile")) s.boostProfile = (int)GetPrivateProfileIntW(section.c_str(), L"BoostProfile", s.boostProfile, ini.c_str());
    // the truck's own drag area, by hand in its file: not one of the scales, so it does not make them the truck's own
    {
        wchar_t buf[16] = {};
        if (GetPrivateProfileStringW(section.c_str(), L"RoadCdA", L"", buf, 16, ini.c_str()) > 0)
        {
            const float v = (float)_wtof(buf);
            if (isfinite(v) && v > 0.0f) s.roadCdA = v < 0.3f ? 0.3f : v > 20.0f ? 20.0f : v;
        }
    }
    if (any) SettingsClamp(s);
    return any;
}

// Writes the truck's file: the seven scales of s.
static void WriteTruckIni(const char *key, const GaugeSettings &s)
{
    const std::wstring ini = SetupFile(kSetupTruck, key), section = kSetupSections[kSetupTruck];
    SetupBegin(kSetupTruck, Wide(key));
    IniPutInt(section.c_str(), L"TachClass", s.tachClass, ini);
    IniPutFloat(section.c_str(), L"IdleRpm", s.idleRpm, ini);
    IniPutFloat(section.c_str(), L"RedlineRpm", s.redlineRpm, ini);
    IniPutFloat(section.c_str(), L"MaxRpm", s.maxRpm, ini);
    IniPutFloat(section.c_str(), L"SpeedMax", s.speedMax, ini);
    IniPutFloat(section.c_str(), L"BoostMax", s.boostMax, ini);
    IniPutInt(section.c_str(), L"BoostProfile", s.boostProfile, ini);
}

// Takes the truck's seven scales out of its file, and the file with them when nothing else is in it.
static void DropTruckIni(const char *key)
{
    const std::wstring ini = SetupFile(kSetupTruck, key), section = kSetupSections[kSetupTruck];
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    for (const wchar_t *name : { L"TachClass", L"IdleRpm", L"RedlineRpm", L"MaxRpm", L"SpeedMax", L"BoostMax", L"BoostProfile", L"Kind" })
        WritePrivateProfileStringW(section.c_str(), name, nullptr, ini.c_str());
    wchar_t rest[8] = {}, names[64] = {};
    // the file goes only when [Truck] is empty and no other section is in it: a kind's own suspension or engine, and an
    // owned truck's ([Suspension:<id>], [Engine:<id>]), stay
    const DWORD named = GetPrivateProfileSectionNamesW(names, 64, ini.c_str());
    const bool truckOnly = !named || (!_wcsicmp(names, section.c_str()) && !names[wcslen(names) + 1]);
    if (!GetPrivateProfileSectionW(section.c_str(), rest, 8, ini.c_str()) && truckOnly)
        DeleteFileW(ini.c_str());
    else SetupBegin(kSetupTruck, Wide(key)); // something else stays: the kind with it
}

// The seven scales a truck can have of its own: copied from one set of settings into another, and compared.
static void ScalesCopy(GaugeSettings &to, const GaugeSettings &from)
{
    to.tachClass = from.tachClass;
    to.idleRpm = from.idleRpm;
    to.redlineRpm = from.redlineRpm;
    to.maxRpm = from.maxRpm;
    to.speedMax = from.speedMax;
    to.boostMax = from.boostMax;
    to.boostProfile = from.boostProfile;
}

static bool ScalesSame(const GaugeSettings &a, const GaugeSettings &b)
{
    return a.tachClass == b.tachClass && a.idleRpm == b.idleRpm && a.redlineRpm == b.redlineRpm && a.maxRpm == b.maxRpm && a.speedMax == b.speedMax &&
           a.boostMax == b.boostMax && a.boostProfile == b.boostProfile;
}

// ---- a truck's own suspension: [Suspension] in the same file (BRIEF_suspension_per_truck.md step 1) ----
// Every setting of the Suspension tab but SuspDisplay (a display choice) and SuspStepTest (a test): the springs, the
// damping, the travel, the ride, the bars, the air suspension, the damper's curve, the bump stops, the spring's curve
// and the damping zones. Own=1 puts them in force for that kind of truck; Own=0 keeps them, not in use, so ticking the
// box again brings them back. A section without Own is taken as in force (written by hand). The names are the ini's.
static const wchar_t *const kSuspSection = L"Suspension";
static const struct { const wchar_t *name; float GaugeSettings::*v; } kSuspSetFloats[] = {
    { L"SpringScale", &GaugeSettings::springScale },     { L"DampingScale", &GaugeSettings::dampingScale },
    { L"ExtraTravel", &GaugeSettings::extraTravel },     { L"RideFront", &GaugeSettings::rideFront },
    { L"RideRear", &GaugeSettings::rideRear },           { L"SwayFront", &GaugeSettings::swayFront },
    { L"SwayRear", &GaugeSettings::swayRear },           { L"LowCompression", &GaugeSettings::lowCompression },
    { L"HighCompression", &GaugeSettings::highCompression }, { L"LowRebound", &GaugeSettings::lowRebound },
    { L"HighRebound", &GaugeSettings::highRebound },     { L"DamperKnee", &GaugeSettings::damperKnee },
    { L"BumpZone", &GaugeSettings::bumpZone },           { L"BumpForce", &GaugeSettings::bumpForce },
    { L"BumpDamping", &GaugeSettings::bumpDamping },     { L"SpringRise", &GaugeSettings::springRise },
    { L"SpringCross", &GaugeSettings::springCross },     { L"ZoneSoft", &GaugeSettings::zoneSoft },
    { L"ZoneFirm", &GaugeSettings::zoneFirm },           { L"ZoneFrom", &GaugeSettings::zoneFrom },
    { L"ZoneTo", &GaugeSettings::zoneTo },             { L"FrameFlex", &GaugeSettings::frameFlex },
    { L"TwistRange", &GaugeSettings::twistRange },
};
static const struct { const wchar_t *name; int GaugeSettings::*v; } kSuspSetInts[] = {
    { L"DamperCurve", &GaugeSettings::damperCurve }, { L"SpringCurve", &GaugeSettings::springCurve },
};
static const struct { const wchar_t *name; bool GaugeSettings::*v; } kSuspSetBools[] = {
    { L"AirSuspension", &GaugeSettings::airSuspension }, { L"AirHorizon", &GaugeSettings::airHorizon },
    { L"BumpStops", &GaugeSettings::bumpStops },         { L"DampingZones", &GaugeSettings::dampingZones },
};

static void SuspCopy(GaugeSettings &to, const GaugeSettings &from)
{
    for (const auto &k : kSuspSetFloats) to.*k.v = from.*k.v;
    for (const auto &k : kSuspSetInts) to.*k.v = from.*k.v;
    for (const auto &k : kSuspSetBools) to.*k.v = from.*k.v;
}

static bool SuspSame(const GaugeSettings &a, const GaugeSettings &b)
{
    for (const auto &k : kSuspSetFloats)
        if (a.*k.v != b.*k.v) return false;
    for (const auto &k : kSuspSetInts)
        if (a.*k.v != b.*k.v) return false;
    for (const auto &k : kSuspSetBools)
        if (a.*k.v != b.*k.v) return false;
    return true;
}

// Step 2 (per owned truck): the same keys in [Suspension:<id>] of the kind's file, the id the game's own of the truck
// ([vehicle + 0x48], BRIEF_truck_id.md, the lead's rule of 01:16). Its section's name; id "" is the kind's.
static std::wstring SectionOf(const wchar_t *base, const char *id) { return id && id[0] ? std::wstring(base) + L":" + Wide(id) : std::wstring(base); }
static std::wstring SuspSectionOf(const char *id) { return SectionOf(kSuspSection, id); }
// The engine's settings per truck (BRIEF_engine_per_truck.md): [Engine] of the kind's file and [Engine:<id>] of an owned
// truck, beside the suspension's, each with its own Own and its own take-over (the Reviewer, 09:58).
static const wchar_t *const kEngineSection = L"Engine";

// A truck's suspension section (the kind's, or with an id the owned truck's): none (no value there), kept (Own=0) or
// in force. Its values are laid over s where the section holds them, whichever it is; one it does not hold stays.
enum { kSuspNone, kSuspKept, kSuspInForce };
static int ReadTruckSusp(const char *key, GaugeSettings &s, const char *id = "")
{
    const std::wstring ini = SetupFile(kSetupTruck, key), sec = SuspSectionOf(id);
    const wchar_t *section = sec.c_str();
    bool any = false;
    auto has = [&](const wchar_t *name) {
        wchar_t buf[8] = {};
        const bool there = GetPrivateProfileStringW(section, name, L"", buf, 8, ini.c_str()) > 0;
        any = any || there;
        return there;
    };
    for (const auto &k : kSuspSetFloats)
        if (has(k.name)) s.*k.v = IniFloatIn(section, k.name, s.*k.v, ini);
    for (const auto &k : kSuspSetInts)
        if (has(k.name)) s.*k.v = (int)GetPrivateProfileIntW(section, k.name, s.*k.v, ini.c_str());
    for (const auto &k : kSuspSetBools)
        if (has(k.name)) s.*k.v = GetPrivateProfileIntW(section, k.name, s.*k.v ? 1 : 0, ini.c_str()) != 0;
    if (!any) return kSuspNone;
    SettingsClamp(s);
    return GetPrivateProfileIntW(section, L"Own", 1, ini.c_str()) ? kSuspInForce : kSuspKept;
}

// A truck's suspension section written: Own and every value of s. Key by key through the profile API, never the file
// whole, as the learned pulls go into the same file from the mod's thread (the addendum's point 5).
static void WriteTruckSusp(const char *key, const GaugeSettings &s, bool own, const char *id = "")
{
    const std::wstring ini = SetupFile(kSetupTruck, key), sec = SuspSectionOf(id);
    const wchar_t *section = sec.c_str();
    SetupBegin(kSetupTruck, Wide(key));
    IniPutInt(section, L"Own", own ? 1 : 0, ini);
    for (const auto &k : kSuspSetFloats) IniPutFloat(section, k.name, s.*k.v, ini);
    for (const auto &k : kSuspSetInts) IniPutInt(section, k.name, s.*k.v, ini);
    for (const auto &k : kSuspSetBools) IniPutInt(section, k.name, s.*k.v ? 1 : 0, ini);
}

// An owned truck's section out of the kind's file (its set went to the kind's level).
static void DropTruckSusp(const char *key, const char *id)
{
    if (!id[0]) return;
    WritePrivateProfileStringW(SuspSectionOf(id).c_str(), nullptr, nullptr, SetupFile(kSetupTruck, key).c_str());
}

// The take-over (BRIEF_suspension_per_truck.md step 2): the game made the driven truck anew under another id. The old
// id's section is copied to the new id when the new one has none of its own (one it has means another truck of the
// kind, which keeps its set). The old section stays: were it in fact another truck of the kind, it keeps its set.
// True when a set was taken over.
// The old section is then marked TakenBy=<new id> and TakenAt=<when, in 100 ns since 1601> (review N1): the mark goes
// when the old id is driven again (TruckSuspSeen), and only the newest kSuspTakenKept marked sections of a file are kept
// (TruckSuspPrune, after each take-over and at a save, review F3); an unmarked section is never dropped.
static const int kSuspTakenKept = 10;
// base: the sections' name before the colon, kSuspSection or kEngineSection; each runs on its own, with its own marks
// and its own newest kSuspTakenKept (the Reviewer, 09:58: a truck with only an engine set keeps it at a rebuild).
static int TruckSuspPrune(const char *key, const wchar_t *base = kSuspSection);
static bool TruckSuspTakeOver(const char *key, const char *oldId, const char *newId, unsigned long long at = 0, const wchar_t *base = kSuspSection)
{
    if (!key[0] || !oldId[0] || !newId[0] || !strcmp(oldId, newId)) return false;
    const std::wstring ini = SetupFile(kSetupTruck, key), from = SectionOf(base, oldId), to = SectionOf(base, newId);
    std::vector<wchar_t> keys(32768, L'\0'), mine(64, L'\0');
    if (!GetPrivateProfileSectionW(from.c_str(), keys.data(), (DWORD)keys.size(), ini.c_str())) return false;
    if (GetPrivateProfileSectionW(to.c_str(), mine.data(), (DWORD)mine.size(), ini.c_str())) return false;
    if (!WritePrivateProfileSectionW(to.c_str(), keys.data(), ini.c_str())) return false;
    // the new section is the truck's own now, unmarked; the old one is marked as taken
    WritePrivateProfileStringW(to.c_str(), L"TakenBy", nullptr, ini.c_str());
    WritePrivateProfileStringW(to.c_str(), L"TakenAt", nullptr, ini.c_str());
    if (!at)
    {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        at = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    }
    wchar_t when[32];
    swprintf_s(when, L"%llu", at);
    WritePrivateProfileStringW(from.c_str(), L"TakenBy", Wide(newId).c_str(), ini.c_str());
    WritePrivateProfileStringW(from.c_str(), L"TakenAt", when, ini.c_str());
    TruckSuspPrune(key, base); // a truck only driven, never edited, still keeps no more than the newest marked (review F3)
    return true;
}

// An id driven again: its section's take-over mark goes (it is a truck in use, not a left-over).
static void TruckSuspSeen(const char *key, const char *id, const wchar_t *base = kSuspSection)
{
    if (!key[0] || !id[0]) return;
    const std::wstring ini = SetupFile(kSetupTruck, key), sec = SectionOf(base, id);
    wchar_t by[48] = {};
    if (!GetPrivateProfileStringW(sec.c_str(), L"TakenBy", L"", by, 48, ini.c_str())) return;
    WritePrivateProfileStringW(sec.c_str(), L"TakenBy", nullptr, ini.c_str());
    WritePrivateProfileStringW(sec.c_str(), L"TakenAt", nullptr, ini.c_str());
}

// The marked sections beyond the newest kSuspTakenKept go out of the kind's file. The count dropped.
static int TruckSuspPrune(const char *key, const wchar_t *base)
{
    if (!key[0]) return 0;
    const std::wstring ini = SetupFile(kSetupTruck, key), prefix = std::wstring(base) + L":";
    std::vector<wchar_t> names(65536, L'\0');
    if (!GetPrivateProfileSectionNamesW(names.data(), (DWORD)names.size(), ini.c_str())) return 0;
    std::vector<std::pair<unsigned long long, std::wstring>> marked;
    for (const wchar_t *n = names.data(); *n; n += wcslen(n) + 1)
    {
        if (_wcsnicmp(n, prefix.c_str(), prefix.size())) continue;
        wchar_t at[32] = {};
        if (!GetPrivateProfileStringW(n, L"TakenAt", L"", at, 32, ini.c_str())) continue;
        marked.push_back({ _wcstoui64(at, nullptr, 10), n });
    }
    if ((int)marked.size() <= kSuspTakenKept) return 0;
    std::sort(marked.begin(), marked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    int dropped = 0;
    for (size_t i = kSuspTakenKept; i < marked.size(); i++, dropped++) WritePrivateProfileStringW(marked[i].second.c_str(), nullptr, nullptr, ini.c_str());
    return dropped;
}

// F1 of step 2's review: the same kind under a new id while driven (no "none" between) is a take-over only on the
// vehicle the old id was taken with. A new id on another vehicle is another truck of the kind.
static bool SuspAgainTakeOver(const char *oldId, uint64_t oldVehicle, const char *newId, uint64_t newVehicle)
{
    return oldId[0] && newId[0] && strcmp(oldId, newId) && oldVehicle && oldVehicle == newVehicle;
}

static const ULONGLONG kSuspTakeOverMs = 20000; // the take-over's window: the same kind back under a new id within 20 s

// The take-over's watch (step 2): the truck that went last with none driven since, and when. At each change of the
// driven truck's kind (or to none), SuspTrack says whether the truck now driven takes over the one that went: the same
// kind under another id within 20 s, no other truck driven between. oldId gets the id that went.
struct SuspTracker { TruckIdentity gone; ULONGLONG goneAt; };
static bool SuspTrack(SuspTracker &t, const TruckIdentity &was, const TruckIdentity &now, ULONGLONG at, char (&oldId)[40])
{
    oldId[0] = 0;
    if (was.key[0] && !now.key[0])
    {
        t.gone = was;
        t.goneAt = at;
        return false;
    }
    if (!now.key[0]) return false;
    const bool due = t.gone.key[0] && !strcmp(t.gone.key, now.key) && t.gone.id[0] && now.id[0] && strcmp(t.gone.id, now.id) && at - t.goneAt <= kSuspTakeOverMs;
    if (due) strcpy_s(oldId, t.gone.id);
    t.gone = TruckIdentity{};
    return due;
}

// ---- the engine's settings per truck (BRIEF_engine_per_truck.md, the user's word of 09:57: as the suspension's) ----
// Two settings move: the Engine torque slider (Torque, percent) and the Upshift delay (GearHold, seconds). Each is kept
// by the engine or gearbox fitted, Torque.<engine>= and GearHold.<gearbox>=, in a level's section:
// - this owned truck: [Engine:<id>] of the kind's file, with Own;
// - every truck of the kind: [Engine] of the kind's file, with Own;
// - the engine's default: Torque= in Gauges\engines\<engine>.ini and GearHold= in Gauges\gearboxes\<gearbox>.ini, as
//   before (an old file's figure is that default, so nothing is lost);
// - else 100 % and the truck class's hold.
// The most specific section in force that holds a key for the engine (or gearbox) fitted gives that figure; one in
// force without it falls through to the next level for that engine, and the tab names the level the figure came from.
// The box, the For choice, Own=0's "kept, not in use" and the take-over are the suspension's.
enum { kEngLevelNone, kEngLevelDefault, kEngLevelKind, kEngLevelTruck };
// An engine's or a gearbox's name as a key's part: as SetupFileName cleans a file name, and what would break an ini's
// key (=, ;, [ and ]) too.
static std::wstring EngineKeyOf(const wchar_t *what, const char *name)
{
    std::wstring part = SetupFileName(Wide(name));
    for (wchar_t &c : part)
        if (c == L'=' || c == L';' || c == L'[' || c == L']') c = L'_';
    return std::wstring(what) + L"." + part;
}

// One level's section: none (no key and no Own in it), kept (Own=0) or in force; and its figures for the engine and the
// gearbox fitted, 0 where it holds none.
static int ReadTruckEngine(const char *key, const char *id, const char *engine, const char *gearbox, int &torque, float &hold)
{
    torque = 0, hold = 0.0f;
    if (!key[0]) return kSuspNone;
    const std::wstring ini = SetupFile(kSetupTruck, key), sec = SectionOf(kEngineSection, id);
    wchar_t probe[8] = {};
    if (!GetPrivateProfileSectionW(sec.c_str(), probe, 8, ini.c_str())) return kSuspNone;
    wchar_t buf[24] = {};
    if (engine[0] && GetPrivateProfileStringW(sec.c_str(), EngineKeyOf(L"Torque", engine).c_str(), L"", buf, 24, ini.c_str()) > 0)
    {
        const float v = (float)_wtof(buf);
        if (isfinite(v) && v > 0.0f) torque = (int)lroundf(SettingsRange(v, kTorqueShareRange[0] * 100.0f, kTorqueShareRange[1] * 100.0f));
    }
    if (gearbox[0] && GetPrivateProfileStringW(sec.c_str(), EngineKeyOf(L"GearHold", gearbox).c_str(), L"", buf, 24, ini.c_str()) > 0)
    {
        const float v = (float)_wtof(buf);
        if (isfinite(v) && v > 0.0f) hold = SettingsRange(v, kGearHoldRange[0], kGearHoldRange[1]);
    }
    return GetPrivateProfileIntW(sec.c_str(), L"Own", 1, ini.c_str()) ? kSuspInForce : kSuspKept;
}

// A level's section written: Own, and the figures given (a 0 leaves its key as it is). Key by key, as the suspension's.
static void WriteTruckEngine(const char *key, const char *id, bool own, const char *engine, int torque, const char *gearbox, float hold)
{
    if (!key[0]) return;
    const std::wstring ini = SetupFile(kSetupTruck, key), sec = SectionOf(kEngineSection, id);
    SetupBegin(kSetupTruck, Wide(key));
    IniPutInt(sec.c_str(), L"Own", own ? 1 : 0, ini);
    if (engine[0] && torque > 0) IniPutInt(sec.c_str(), EngineKeyOf(L"Torque", engine).c_str(), torque, ini);
    if (gearbox[0] && hold > 0.0f)
    {
        wchar_t buf[16];
        swprintf_s(buf, L"%.1f", hold);
        WritePrivateProfileStringW(sec.c_str(), EngineKeyOf(L"GearHold", gearbox).c_str(), buf, ini.c_str());
    }
}

static void EngineSectionOwn(const char *key, const char *id, bool own) { IniPutInt(SectionOf(kEngineSection, id).c_str(), L"Own", own ? 1 : 0, SetupFile(kSetupTruck, key)); }

// The engine's default: the engine file's Torque (percent, 0 none) and the gearbox file's GearHold (0 none).
static int EngineTorqueRead(const char *engine)
{
    if (!engine[0]) return 0;
    wchar_t buf[24] = {};
    if (GetPrivateProfileStringW(kSetupSections[kSetupEngine], L"Torque", L"", buf, 24, SetupFile(kSetupEngine, engine).c_str()) == 0) return 0;
    const float v = (float)_wtof(buf);
    return isfinite(v) && v > 0.0f ? (int)lroundf(SettingsRange(v, kTorqueShareRange[0] * 100.0f, kTorqueShareRange[1] * 100.0f)) : 0;
}

// The driven truck's engine settings as the mod's thread keeps them: its kind (key), its game id, the engine and the
// gearbox fitted; each level's section state; and the figures in force with the level each came from.
struct EngineState
{
    char key[kMaxText], id[40], engine[kMaxText], gearbox[kMaxText];
    bool kindOwn, kindKept, truckOwn, truckKept;
    int torque, torqueFrom;     // percent in force (100 where no level has one), and its level (kEngLevel..., None: 100)
    float hold; int holdFrom;   // seconds in force (0 where no level has one: the class's), and its level
};

// The level whose section is in force: this truck's, the kind's, or none (the default).
static int EngineLevel(const EngineState &e) { return e.truckOwn ? kEngLevelTruck : e.kindOwn ? kEngLevelKind : kEngLevelDefault; }

// The figures in force from the files: this truck's section where in force and it holds the key, else the kind's, else
// the engine's default, else 100 and the class's.
static void EngineFromFiles(EngineState &e)
{
    int kt = 0, tt = 0;
    float kh = 0.0f, th = 0.0f;
    const int kind = ReadTruckEngine(e.key, "", e.engine, e.gearbox, kt, kh);
    const int truck = e.id[0] ? ReadTruckEngine(e.key, e.id, e.engine, e.gearbox, tt, th) : kSuspNone;
    e.kindOwn = kind == kSuspInForce, e.kindKept = kind == kSuspKept;
    e.truckOwn = truck == kSuspInForce, e.truckKept = truck == kSuspKept;
    const int dt = EngineTorqueRead(e.engine);
    float dh = 0.0f;
    const bool dhOwn = e.gearbox[0] && GearHoldRead(e.gearbox, dh);
    if (e.truckOwn && tt) e.torque = tt, e.torqueFrom = kEngLevelTruck;
    else if (e.kindOwn && kt) e.torque = kt, e.torqueFrom = kEngLevelKind;
    else if (dt) e.torque = dt, e.torqueFrom = kEngLevelDefault;
    else e.torque = 100, e.torqueFrom = kEngLevelNone;
    if (e.truckOwn && th > 0.0f) e.hold = th, e.holdFrom = kEngLevelTruck;
    else if (e.kindOwn && kh > 0.0f) e.hold = kh, e.holdFrom = kEngLevelKind;
    else if (dhOwn) e.hold = dh, e.holdFrom = kEngLevelDefault;
    else e.hold = 0.0f, e.holdFrom = kEngLevelNone;
}

// The state for a truck, its engine and its gearbox (any "" while not known).
static void EngineFor(EngineState &e, const char *key, const char *id, const char *engine, const char *gearbox)
{
    strcpy_s(e.key, key);
    strcpy_s(e.id, key[0] ? id : "");
    strcpy_s(e.engine, key[0] ? engine : "");
    strcpy_s(e.gearbox, key[0] ? gearbox : "");
    EngineFromFiles(e);
}

// An edit from the tab, by the suspension's rules (the addendum's point 4): into the level in force, this truck's
// before the kind's; with neither it makes the kind's own (the box ticks itself), unless a set is kept with Own=0 at
// either level: then it edits the engine's default and the kept set stays as it is (F1's rule). torque 0 or hold 0:
// that one is not edited. True where the kind's set was made by it. An edit whose engine (torque) or gearbox (hold) is
// not known yet writes nothing, so it never ticks an empty set (review N1 of engine per truck).
static bool EngineTake(EngineState &e, int torque, float hold)
{
    if (!e.key[0] || (torque > 0 && !e.engine[0]) || (hold > 0.0f && !e.gearbox[0])) return false;
    bool made = false;
    const char *engine = torque > 0 ? e.engine : "", *gearbox = hold > 0.0f ? e.gearbox : "";
    if (e.truckOwn) WriteTruckEngine(e.key, e.id, true, engine, torque, gearbox, hold);
    else if (e.kindOwn) WriteTruckEngine(e.key, "", true, engine, torque, gearbox, hold);
    else if (!e.kindKept && !e.truckKept) WriteTruckEngine(e.key, "", true, engine, torque, gearbox, hold), made = true;
    else
    {
        if (torque > 0 && e.engine[0]) EngineTorqueWrite(e.engine, torque);
        if (hold > 0.0f && e.gearbox[0]) GearHoldWrite(e.gearbox, hold);
    }
    EngineFromFiles(e);
    return made;
}

// The Engine tab's box "Own settings": as SettingsSuspOwn. Off: the level in force keeps its set with Own=0. On: a kept
// set is in force again, this truck's before the kind's; with none kept, the figures shown become the kind's own.
static void EngineOwnSet(EngineState &e, bool on)
{
    if (!e.key[0]) return;
    if (!on)
    {
        if (e.truckOwn) EngineSectionOwn(e.key, e.id, false);
        else if (e.kindOwn) EngineSectionOwn(e.key, "", false);
        EngineFromFiles(e);
        return;
    }
    if (e.truckOwn || e.kindOwn) return;
    if (e.truckKept) EngineSectionOwn(e.key, e.id, true);
    else if (e.kindKept) EngineSectionOwn(e.key, "", true);
    else WriteTruckEngine(e.key, "", true, e.engine, e.torque, e.gearbox, e.hold);
    EngineFromFiles(e);
}

// The tab's choice under the box, as SettingsSuspLevel: This truck only puts the figures in force into this truck's
// section in force (or brings back the one it kept); Every <kind> with no set for the kind moves this truck's section up
// to the kind's whole; with a set for the kind it puts the kind's in force and keeps this truck's with Own=0 (F2).
static void EngineLevelSet(EngineState &e, bool truckOnly)
{
    if (!e.key[0] || (truckOnly ? EngineLevel(e) == kEngLevelTruck : EngineLevel(e) != kEngLevelTruck)) return;
    const std::wstring ini = SetupFile(kSetupTruck, e.key);
    if (truckOnly)
    {
        if (!e.id[0]) return;
        if (e.truckKept) EngineSectionOwn(e.key, e.id, true);
        else WriteTruckEngine(e.key, e.id, true, e.engine, e.torque, e.gearbox, e.hold);
    }
    else if (!e.kindOwn && !e.kindKept)
    {
        // no set for the kind yet: this truck's goes up to it, every engine's and gearbox's key with it
        std::vector<wchar_t> keys(32768, L'\0');
        const std::wstring mine = SectionOf(kEngineSection, e.id);
        if (GetPrivateProfileSectionW(mine.c_str(), keys.data(), (DWORD)keys.size(), ini.c_str())) WritePrivateProfileSectionW(kEngineSection, keys.data(), ini.c_str());
        for (const wchar_t *mark : { L"TakenBy", L"TakenAt" }) WritePrivateProfileStringW(kEngineSection, mark, nullptr, ini.c_str());
        EngineSectionOwn(e.key, "", true);
        WritePrivateProfileStringW(mine.c_str(), nullptr, nullptr, ini.c_str());
    }
    else
    {
        EngineSectionOwn(e.key, e.id, false);
        EngineSectionOwn(e.key, "", true);
    }
    EngineFromFiles(e);
}

// The tab's button "Make this the engine's default": the figures in force into the engine's and the gearbox's own
// files, for every truck without a set of its own. The truck's set stays as it is.
static void EngineToDefault(EngineState &e)
{
    if (e.engine[0] && e.torque > 0) EngineTorqueWrite(e.engine, e.torque);
    if (e.gearbox[0] && e.hold > 0.0f) GearHoldWrite(e.gearbox, e.hold);
    EngineFromFiles(e);
}

// The tab's Defaults: 100 % and the class's hold. The sections in force are kept with Own=0 (not thrown away), and the
// engine's and the gearbox's own figures go out of their files.
static void EngineDefaults(EngineState &e)
{
    if (!e.key[0]) return;
    if (e.truckOwn) EngineSectionOwn(e.key, e.id, false);
    if (e.kindOwn) EngineSectionOwn(e.key, "", false);
    if (e.engine[0]) WritePrivateProfileStringW(kSetupSections[kSetupEngine], L"Torque", nullptr, SetupFile(kSetupEngine, e.engine).c_str());
    if (e.gearbox[0]) WritePrivateProfileStringW(kSetupSections[kSetupGearbox], L"GearHold", nullptr, SetupFile(kSetupGearbox, e.gearbox).c_str());
    EngineFromFiles(e);
}

static const wchar_t *EngineLevelWords(int level)
{
    return level == kEngLevelTruck ? L"this engine in this truck" : level == kEngLevelKind ? L"this engine in every " : level == kEngLevelDefault ? L"this engine's default" : L"none set";
}

// The settings as the mod's thread keeps them. plain is what [Gauges] holds. s is what is in force: plain, with the
// driven truck's own scales in the place of the plain ones where it has some (own), and the most specific suspension
// in force in the place of the plain one: the owned truck's [Suspension:<id>] (truckOwn), else the kind's [Suspension]
// (suspOwn). suspKept and truckKept: that level's section is there with Own=0, not in use. key is the driven truck's
// kind and id its game id ("" while none is driven or the id is not read). changed, suspChanged and truckChanged: the
// scales, the kind's or the truck's suspension were changed on the tab and are not in the ini yet. The functions below
// are all that changes a state once the ini is read into plain.
struct SettingsState { GaugeSettings plain, s; char key[kMaxText], id[40]; bool own, changed, suspOwn, suspKept, suspChanged, truckOwn, truckKept, truckChanged; };

// The suspension level in force: the default, the kind's, this owned truck's.
enum { kSuspLevelDefault, kSuspLevelKind, kSuspLevelTruck };
static int SuspLevel(const SettingsState &st) { return st.truckOwn ? kSuspLevelTruck : st.suspOwn ? kSuspLevelKind : kSuspLevelDefault; }

// The suspension in force from the files: the plain one, then the kind's where in force, then the truck's.
static void SuspFromFiles(SettingsState &st)
{
    SuspCopy(st.s, st.plain);
    GaugeSettings susp = st.s;
    const int kind = st.key[0] ? ReadTruckSusp(st.key, susp) : kSuspNone;
    st.suspOwn = kind == kSuspInForce;
    st.suspKept = kind == kSuspKept;
    if (st.suspOwn) SuspCopy(st.s, susp);
    GaugeSettings mine = st.s;
    const int truck = st.key[0] && st.id[0] ? ReadTruckSusp(st.key, mine, st.id) : kSuspNone;
    st.truckOwn = truck == kSuspInForce;
    st.truckKept = truck == kSuspKept;
    if (st.truckOwn) SuspCopy(st.s, mine);
    st.suspChanged = st.truckChanged = false;
}

// The settings in force for a truck (key "" for none, id "" while not read), from the plain ones and the ini. Each
// truck's set goes in over the plain one, never over the last truck's (the game's values are the stock times the set,
// the addendum's point 4).
static void SettingsFor(SettingsState &st, const char *key, const char *id = "")
{
    strcpy_s(st.key, key);
    strcpy_s(st.id, key[0] ? id : "");
    st.s = st.plain;
    st.own = key[0] && ReadTruckIni(key, st.s);
    st.changed = false;
    TruckSuspSeen(st.key, st.id);
    TruckSuspSeen(st.key, st.id, kEngineSection);
    SuspFromFiles(st);
}

// An edit from the tab, in. A scale changed while a truck is driven makes the seven scales that truck's own, and
// they stay out of the plain ones from then on. A suspension setting goes to the level in force: this truck's, else
// the kind's. With neither in force it makes the set the kind's own, the box ticking itself, unless a set is kept with
// Own=0 at either level: then the change edits the default and the kept set stays as it is (review F1, the lead's
// call). Everything else on the tab is every truck's.
static void SettingsTake(SettingsState &st, const GaugeSettings &in)
{
    if (st.key[0] && !ScalesSame(in, st.s)) st.own = st.changed = true;
    if (st.key[0] && !SuspSame(in, st.s))
    {
        if (st.truckOwn) st.truckChanged = true;
        else if (st.suspOwn) st.suspChanged = true;
        else if (!st.suspKept && !st.truckKept) st.suspOwn = st.suspChanged = true;
    }
    const GaugeSettings before = st.plain;
    st.plain = in;
    if (st.own) ScalesCopy(st.plain, before);
    if (st.suspOwn || st.truckOwn) SuspCopy(st.plain, before);
    st.s = in;
}

// The driven truck gives up its own scales: they go out of the ini, and the plain ones are in force for it again.
static void SettingsDrop(SettingsState &st)
{
    DropTruckIni(st.key);
    ScalesCopy(st.s, st.plain);
    st.own = st.changed = false;
}

// The tab's box "Own settings". Off: the level in force keeps its set with Own=0 (a change not yet written goes in
// first, so nothing is lost), and the level under it is in force. On: a kept set is in force again, this truck's
// before the kind's; with none kept, the set shown becomes the kind's own.
static void SettingsSuspOwn(SettingsState &st, bool on)
{
    if (!st.key[0]) return;
    if (!on)
    {
        if (st.truckOwn) WriteTruckSusp(st.key, st.s, false, st.id);
        else if (st.suspOwn) WriteTruckSusp(st.key, st.s, false);
        else return;
        SuspFromFiles(st);
        return;
    }
    if (st.truckOwn || st.suspOwn) return;
    GaugeSettings kept = st.s;
    if (st.truckKept && ReadTruckSusp(st.key, kept, st.id) != kSuspNone) WriteTruckSusp(st.key, kept, true, st.id);
    else if (st.suspKept && ReadTruckSusp(st.key, kept) != kSuspNone) WriteTruckSusp(st.key, kept, true);
    else WriteTruckSusp(st.key, st.s, true);
    SuspFromFiles(st);
}

// The tab's choice under the box: "This truck only" puts the set in force into this truck's section, in force there
// (the kind's level stays as it is for the other trucks of the kind), or brings back the set it kept with Own=0. "Every <kind>" with no set for the kind puts this
// truck's into the kind's section and takes this truck's out; with a set for the kind it puts the kind's in force and keeps
// this truck's with Own=0 (review F2). Nothing for a truck whose id is not read.
static void SettingsSuspLevel(SettingsState &st, bool truckOnly)
{
    // only a move between the levels: "Every <kind>" from this truck's, "This truck only" from the kind's or the default
    if (!st.key[0] || (truckOnly ? SuspLevel(st) == kSuspLevelTruck : SuspLevel(st) != kSuspLevelTruck)) return;
    if (truckOnly)
    {
        if (!st.id[0]) return;
        // a set this truck kept with Own=0 comes back; else the set shown becomes its own
        GaugeSettings kept = st.s;
        if (st.truckKept && ReadTruckSusp(st.key, kept, st.id) != kSuspNone) WriteTruckSusp(st.key, kept, true, st.id);
        else WriteTruckSusp(st.key, st.s, true, st.id);
    }
    else if (!st.suspOwn && !st.suspKept)
    {
        // no set for the kind yet: this truck's goes up to it (review F2)
        WriteTruckSusp(st.key, st.s, true);
        DropTruckSusp(st.key, st.id);
    }
    else
    {
        // the kind has a set: it is in force for this truck, and this truck's is kept with Own=0, so This truck only
        // brings it back (review F2: nothing is lost)
        WriteTruckSusp(st.key, st.s, false, st.id);
        IniPutInt(kSuspSection, L"Own", 1, SetupFile(kSetupTruck, st.key));
    }
    SuspFromFiles(st);
}

// The driven truck is the same kind under a new id (the game made it anew): its sections follow it, the suspension's
// and the engine's each on its own. Which were taken: kTakenSusp and kTakenEngine.
enum { kTakenSusp = 1, kTakenEngine = 2 };
static int TakeOverBoth(const char *key, const char *oldId, const char *newId)
{
    return (TruckSuspTakeOver(key, oldId, newId) ? kTakenSusp : 0) | (TruckSuspTakeOver(key, oldId, newId, 0, kEngineSection) ? kTakenEngine : 0);
}
static const wchar_t *TakenWords(int taken)
{
    return taken == (kTakenSusp | kTakenEngine) ? L"the suspension's and the engine's settings" : taken == kTakenEngine ? L"the engine's settings" : L"the suspension's settings";
}
static int SettingsTakeOver(SettingsState &st, const char *newId)
{
    const int taken = TakeOverBoth(st.key, st.id, newId);
    strcpy_s(st.id, newId);
    const GaugeSettings scales = st.s;
    SuspFromFiles(st);
    ScalesCopy(st.s, scales);
    return taken;
}

// The tab's button "Make these the default": the driven truck's suspension into the plain one, which [Gauges] then
// holds. The truck's own set stays as it is.
static void SettingsSuspToDefault(SettingsState &st)
{
    SuspCopy(st.plain, st.s);
}

// Every setting back to its default. The driven truck gives up its own scales, and its own suspension, at either
// level, is kept with Own=0 (not thrown away); other trucks keep theirs.
static void SettingsToDefaults(SettingsState &st)
{
    if (st.own) DropTruckIni(st.key);
    if (st.truckOwn) WriteTruckSusp(st.key, st.s, false, st.id);
    if (st.suspOwn)
    {
        GaugeSettings kind = st.s;
        if (st.truckOwn) ReadTruckSusp(st.key, kind); // the kind's own values, not this truck's
        WriteTruckSusp(st.key, kind, false);
    }
    SettingsDefaults(st.plain);
    st.s = st.plain;
    st.own = st.changed = false;
    SuspFromFiles(st);
}

// Into the ini: the plain settings, and the driven truck's own scales and suspension into their sections when they
// were changed. A truck's section that the tab did not change stays as it is, with what it leaves to [Gauges].
static void SettingsWrite(SettingsState &st)
{
    WriteIni(st.plain);
    if (st.own && st.changed) WriteTruckIni(st.key, st.s);
    if (st.truckOwn && st.truckChanged) WriteTruckSusp(st.key, st.s, true, st.id);
    else if (st.suspOwn && st.suspChanged) WriteTruckSusp(st.key, st.s, true);
    if (st.key[0]) TruckSuspPrune(st.key), TruckSuspPrune(st.key, kEngineSection);
    st.changed = st.suspChanged = st.truckChanged = false;
}

// ---- the gearbox's bindings: the [Gearbox] section of Gauges.ini ----
// One key per action and slot, Bind<Action> and Bind<Action>2, in the reader's text forms (input_bindings.h:
// "Key Num 8", "Pad LB+DPadDown", "Wheel 1 Button 5", "Wheel 1 POV Up", "Wheel 1 Axis 3 inverted", "None"). A key
// not there keeps the default: up, down, N, R and A on the number pad, which the game, the shipped ReShade.ini (Home
// alone) and the usual mods leave alone.
static_assert(kActCount <= kViewActions, "the view holds the bindings of every action");
static_assert(kActDiffLock == kViewShifts && kActAwd == kViewShifts + 1, "the tab's rows of the locks follow the shifter's");
static const wchar_t *const kBindSection = L"Gearbox";

static void BindingsDefault(GearBindings &b)
{
    b = GearBindings{};
    static const struct { int action, vk; } keys[] = { { kActUp, VK_NUMPAD8 }, { kActDown, VK_NUMPAD2 }, { kActGearN, VK_NUMPAD5 }, { kActGearR, VK_NUMPAD0 },
                                                       { kActGearA, VK_NUMPAD7 } };
    for (const auto &k : keys) b.slot[k.action][0] = Binding{ kBindKey, 0, (uint32_t)k.vk, 0, false };
}

static void BindingKey(int action, int slot, wchar_t *out, size_t size) { swprintf_s(out, size, L"Bind%S%s", kActionNames[action], slot ? L"2" : L""); }

// Whether the ini holds a binding key yet.
static bool BindingsInIni(const std::wstring &ini)
{
    static wchar_t keys[16384];
    if (!GetPrivateProfileSectionW(kBindSection, keys, 16384, ini.c_str())) return false;
    for (const wchar_t *p = keys; *p; p += wcslen(p) + 1)
        if (!_wcsnicmp(p, L"Bind", 4)) return true;
    return false;
}

// The bindings the ini holds over b; a text that is no binding is said with why (say) and leaves the slot as it was.
static void BindingsRead(const std::wstring &ini, GearBindings &b, bool say)
{
    for (int a = 0; a < kActCount; a++)
        for (int slot = 0; slot < kBindingSlots; slot++)
        {
            wchar_t key[48], buf[128];
            BindingKey(a, slot, key, 48);
            GetPrivateProfileStringW(kBindSection, key, L"\x01", buf, 128, ini.c_str());
            if (buf[0] == 1) continue; // not there
            char text[128];
            size_t n = 0;
            for (; buf[n] && n + 1 < sizeof text; n++) text[n] = buf[n] < 128 ? (char)buf[n] : '?';
            text[n] = 0;
            Binding bind = {};
            char why[256] = "";
            if (!text[0] || BindingParse(text, bind, why, sizeof why)) b.slot[a][slot] = bind;
            else if (say) Log(L"gearbox: %s=%S is no binding (%S), so it is left as it was", key, text, why);
        }
}

static void BindingsWrite(const std::wstring &ini, const GearBindings &b)
{
    for (int a = 0; a < kActCount; a++)
        for (int slot = 0; slot < kBindingSlots; slot++)
        {
            wchar_t key[48];
            char text[96];
            BindingKey(a, slot, key, 48);
            BindingText(b.slot[a][slot], text, sizeof text);
            WritePrivateProfileStringW(kBindSection, key, Wide(text).c_str(), ini.c_str());
        }
}

// Whether a binding that can be pressed now reaches reverse (Gear down, Reverse, or Neutral on the way): the mod's own
// gearbox takes the shifter only then, so a truck can always be backed up. A key counts always, a wheel's binding only
// while that wheel is there (s, read whether or not the game is in front), a pad's only while a pad answers (review 8,
// W5).
static bool BindingsCanReverse(const GearBindings &b, const InputSnapshot &s, bool padThere)
{
    auto reaches = [&](const Binding &x) {
        switch (x.kind)
        {
        case kBindKey: return true;
        case kBindPad: return padThere;
        case kBindButton:
        case kBindPov:
        case kBindAxis: return x.device >= 1 && x.device <= kInputDevicesMost && s.device[x.device - 1].present;
        default: return false;
        }
    };
    for (int slot = 0; slot < kBindingSlots; slot++)
        if (reaches(b.slot[kActDown][slot]) || reaches(b.slot[kActGearR][slot]) || reaches(b.slot[kActGearN][slot])) return true;
    return false;
}

// Why a capture may not bind what it caught: the NEXT GEN menu's key, the cluster's toggle key, or pad buttons that
// hold the PadToggle chord or are held in it, as both would then act at once (review 8, W1 and W9). nullptr: it may.
static const char *BindingClash(const Binding &b, int menuKey, int toggleKey, uint32_t padChord)
{
    if (b.kind == kBindKey && menuKey && (int)b.code == menuKey) return "the NEXT GEN menu's key";
    if (b.kind == kBindKey && toggleKey && (int)b.code == toggleKey) return "the cluster's toggle key (ToggleKey)";
    if (b.kind == kBindPad && padChord && ((b.code & padChord) == b.code || (b.code & padChord) == padChord)) return "the cluster's pad toggle (PadToggle)";
    return nullptr;
}

// The bound actions in the log, one line each.
static void BindingsLog(const GearBindings &b)
{
    for (int a = 0; a < kActCount; a++)
    {
        char first[96], second[96];
        BindingText(b.slot[a][0], first, sizeof first);
        BindingText(b.slot[a][1], second, sizeof second);
        if (b.slot[a][0].kind && b.slot[a][1].kind) Log(L"gearbox: %S = %S, %S", kActionNames[a], first, second);
        else if (b.slot[a][0].kind || b.slot[a][1].kind) Log(L"gearbox: %S = %S", kActionNames[a], b.slot[a][0].kind ? first : second);
    }
}

// The manual transmission mod's SMT.ini, taken over once: its [KEYBOARD] bindings (Kb.<n>, a DirectInput scan code,
// turned into a Windows key; a combination of keys or another code is left) into the first slot of each matching
// action, and its [OPTIONS] REQUIRE CLUTCH, REQUIRE GEAR HELD, SKIP NEUTRAL and IMMERSIVE MODE (its gear down kept in
// the forward gears, the mod's DownStaysForward). Its [CONTROLLER] section is left (its
// codes are OIS's own, not Windows'), and so are SHOW MENU and HANDBRAKE. Nothing in SMT.ini is
// written. The number of bindings taken; taken and left name them for the log.
static int SmtIniImport(const std::wstring &path, GearBindings &b, GaugeSettings &s, std::wstring &taken, std::wstring &left)
{
    struct Pair { wchar_t smt[16]; int action; };
    Pair pairs[kActCount] = {};
    int count = 0;
    for (int g = 0; g < kGearKeys; g++)
    {
        swprintf_s(pairs[count].smt, L"GEAR %d", g + 1);
        pairs[count++].action = kActGear1 + g;
    }
    static const struct { const wchar_t *smt; int action; } named[] = { { L"GEAR H", kActGearH }, { L"GEAR L-", kActGearLMinus }, { L"GEAR L", kActGearL },
                                                                         { L"GEAR L+", kActGearLPlus }, { L"GEAR N", kActGearN }, { L"GEAR R", kActGearR },
                                                                         { L"GEAR UP", kActUp }, { L"GEAR DOWN", kActDown }, { L"CLUTCH", kActClutch },
                                                                         { L"RANGE HIGH", kActRangeHigh }, { L"RANGE LOW", kActRangeLow },
                                                                         { L"DIFF LOCK", kActDiffLock }, { L"AWD", kActAwd } };
    for (const auto &n : named)
    {
        wcscpy_s(pairs[count].smt, n.smt);
        pairs[count++].action = n.action;
    }
    // the numpad's codes 0x47 to 0x53: Windows maps them to the arrows, Home, End and the rest of that block, which
    // NumLock on makes VK_NUMPADn again, so they are named here (review 8, W6)
    static const UINT numpad[] = { VK_NUMPAD7, VK_NUMPAD8, VK_NUMPAD9, VK_SUBTRACT, VK_NUMPAD4, VK_NUMPAD5, VK_NUMPAD6,
                                   VK_ADD,     VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3,  VK_NUMPAD0, VK_DECIMAL };
    bool took[kActCount] = {};
    int bound = 0;
    for (int i = 0; i < count; i++)
    {
        wchar_t value[64];
        GetPrivateProfileStringW(L"KEYBOARD", pairs[i].smt, L"", value, 64, path.c_str());
        if (!value[0] || !_wcsicmp(value, L"NONE")) continue;
        const wchar_t *digits = !_wcsnicmp(value, L"Kb.", 3) ? value + 3 : nullptr;
        wchar_t *end = nullptr;
        const long code = digits ? wcstol(digits, &end, 10) : -1;
        // an extended key's code has its high bit set: Windows reads it as 0xE0 and the low seven bits
        const UINT scan = code >= 0x80 ? (0xE000u | (UINT)(code & 0x7F)) : (UINT)code;
        const bool valid = code > 0 && code < 0x100 && end && !*end;
        const UINT vk = !valid ? 0 : code >= 0x47 && code <= 0x53 ? numpad[code - 0x47] : MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX);
        wchar_t line[96];
        if (vk)
        {
            b.slot[pairs[i].action][0] = Binding{ kBindKey, 0, vk, 0, false };
            took[pairs[i].action] = true;
            bound++;
            swprintf_s(line, L"%s%s", taken.empty() ? L"" : L", ", pairs[i].smt);
            taken += line;
        }
        else
        {
            swprintf_s(line, L"%s%s (%s)", left.empty() ? L"" : L", ", pairs[i].smt, value);
            left += line;
        }
    }
    // a default whose key the import gave to another action goes, so one key does not shift two ways
    for (int a = 0; a < kActCount; a++)
        for (int slot = 0; slot < kBindingSlots && !took[a]; slot++)
        {
            const Binding &d = b.slot[a][slot];
            for (int t = 0; t < kActCount && d.kind == kBindKey; t++)
                if (took[t] && b.slot[t][0].code == d.code)
                {
                    char key[32];
                    KeyText((int)d.code, key, sizeof key);
                    wchar_t line[96];
                    swprintf_s(line, L"%sthe default of %S (%S, now %S's)", left.empty() ? L"" : L", ", kActionNames[a], key, kActionNames[t]);
                    left += line;
                    b.slot[a][slot] = Binding{};
                    break;
                }
        }
    auto option = [&](const wchar_t *key, bool &out) {
        wchar_t value[16];
        GetPrivateProfileStringW(L"OPTIONS", key, L"", value, 16, path.c_str());
        if (value[0]) out = !_wcsicmp(value, L"true") || !_wcsicmp(value, L"1") || !_wcsicmp(value, L"yes");
    };
    option(L"REQUIRE CLUTCH", s.requireClutch);
    option(L"REQUIRE GEAR HELD", s.requireGearHeld);
    option(L"SKIP NEUTRAL", s.skipNeutral);
    option(L"IMMERSIVE MODE", s.downStaysForward);
    return bound;
}

// The gearbox's actions from the reader's states: the edges for up, down, the positions and the gears, the gear key
// held (RequireGearHeld), the clutch held, and its travel where it is bound to an axis (-1 without one).
static GearboxActions ActionsOf(const ActionState st[kActCount], const GearBindings &b)
{
    GearboxActions a = {};
    a.up = st[kActUp].pressed;
    a.down = st[kActDown].pressed;
    for (int g = 0; g < kGearKeys; g++)
    {
        if (st[kActGear1 + g].pressed && !a.gearPressed) a.gearPressed = g + 1;
        if (st[kActGear1 + g].held && !a.gearHeld) a.gearHeld = g + 1;
    }
    a.a = st[kActGearA].pressed;
    a.n = st[kActGearN].pressed;
    a.r = st[kActGearR].pressed;
    a.h = st[kActGearH].pressed;
    a.lowMinus = st[kActGearLMinus].pressed;
    a.low = st[kActGearL].pressed;
    a.lowPlus = st[kActGearLPlus].pressed;
    a.rangeHigh = st[kActRangeHigh].pressed;
    a.rangeLow = st[kActRangeLow].pressed;
    a.clutch = st[kActClutch].held;
    const bool axis = b.slot[kActClutch][0].kind == kBindAxis || b.slot[kActClutch][1].kind == kBindAxis;
    a.clutchTravel = axis ? st[kActClutch].travel : -1.0f;
    return a;
}

// The actions while the NEXT GEN menu is open or a capture runs: none goes down, and what was held (the gear key under
// RequireGearHeld, the clutch and its travel) stands as it was, so the position stays where it is (review 8, W2).
static GearboxActions ActionsResting(const GearboxActions &before)
{
    GearboxActions a = {};
    a.gearHeld = before.gearHeld;
    a.clutch = before.clutch;
    a.clutchTravel = before.clutchTravel;
    return a;
}

// The snapshot as the gearbox and a capture read it: the pad and the wheels only while the game is in front, as the
// keys (review 8, W7).
static InputSnapshot InputFrontOnly(const InputSnapshot &s)
{
    InputSnapshot out = s;
    if (!s.front)
    {
        memset(out.key, 0, sizeof out.key);
        out.pad = 0;
        for (InputDevice &d : out.device) d = InputDevice{};
    }
    return out;
}

// The tab's asks of the bindings (render thread), done by the mod's thread on its next pass.
static volatile LONG g_bindAsk = 0;
void BindingAsk(int what, int action, int slot) { InterlockedExchange(&g_bindAsk, (LONG)((what << 16) | ((action & 0xFF) << 8) | (slot & 0xFF))); }

// The tab's test buttons (render thread): the points wait here until the mod's thread writes them on its next pass.
static volatile LONG g_damageTest = 0;
void DamageTestAsk(int points) { InterlockedExchangeAdd(&g_damageTest, points); }

void ViewGet(GaugeView &out)
{
    AcquireSRWLockShared(&g_viewLock);
    out = g_view;
    ReleaseSRWLockShared(&g_viewLock);
}

// ---- the pad (read only: the game keeps every button) ----

typedef DWORD(WINAPI *XInputGetStateFn)(DWORD, XINPUT_STATE *);

// XInputGetState of the XInput DLL the game uses, once the game has loaded it; XInput 1.4 until then.
static XInputGetStateFn PadFunction()
{
    static XInputGetStateFn game = nullptr, own = nullptr;
    if (game) return game;
    const HMODULE x = GetModuleHandleW(L"xinput9_1_0.dll");
    if (x) game = (XInputGetStateFn)GetProcAddress(x, "XInputGetState");
    if (game) return game;
    if (!own)
    {
        const HMODULE m = LoadLibraryW(L"xinput1_4.dll");
        own = m ? (XInputGetStateFn)GetProcAddress(m, "XInputGetState") : nullptr;
    }
    return own;
}

// The buttons of every connected pad together, the sticks' directions among them (PadStickButtons). A slot with no pad
// is slow to ask, so the empty ones are only asked again every two seconds.
static volatile LONG g_padsConnected = 0xF; // the slots that answered when all were last asked

uint32_t PadButtons()
{
    static volatile LONG askedAt = 0;
    volatile LONG &connected = g_padsConnected;
    const XInputGetStateFn get = PadFunction();
    if (!get)
    {
        InterlockedExchange(&connected, 0);
        return 0;
    }
    const DWORD now = GetTickCount();
    const bool all = now - (DWORD)askedAt > 2000;
    if (all) InterlockedExchange(&askedAt, (LONG)now);
    const LONG known = connected;
    LONG answered = 0;
    uint32_t buttons = 0;
    for (DWORD i = 0; i < XUSER_MAX_COUNT; i++)
    {
        if (!all && !(known & (1L << i))) continue;
        XINPUT_STATE st = {};
        if (get(i, &st) != ERROR_SUCCESS) continue;
        answered |= 1L << i;
        buttons |= st.Gamepad.wButtons | PadStickButtons(st.Gamepad.sThumbLX, st.Gamepad.sThumbLY, st.Gamepad.sThumbRX, st.Gamepad.sThumbRY);
    }
    if (all) InterlockedExchange(&connected, answered);
    return buttons;
}

// Whether a pad answered when PadButtons last asked every slot.
static bool PadPresent() { return g_padsConnected != 0; }

// A path for the log with the Windows profile folder written %USERPROFILE%: the log is passed on, the account's name stays out.
static std::wstring PathForLog(const std::wstring &path)
{
    wchar_t profile[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (n > 0 && n < MAX_PATH && path.size() >= n && !_wcsnicmp(path.c_str(), profile, n)) return L"%USERPROFILE%" + path.substr(n);
    return path;
}

// The saves' names, by the store: Steam's and the Microsoft Store's .cfg, Epic's .dat.
static const wchar_t *const kSavePatterns[2] = { L"CompleteSave*.cfg", L"CompleteSave*.dat" };

// A save and when it was written.
typedef std::pair<std::wstring, FILETIME> SaveFile;

// Saves newest first; of two written at once, the one found first.
static void SavesNewestFirst(std::vector<SaveFile> &saves)
{
    std::stable_sort(saves.begin(), saves.end(), [](const SaveFile &a, const SaveFile &b) { return CompareFileTime(&a.second, &b.second) > 0; });
}

// The saves in the folders, newest first.
static void SavesIn(const std::vector<std::wstring> &folders, std::vector<SaveFile> &out)
{
    for (const auto &dir : folders)
        for (const wchar_t *pattern : kSavePatterns)
        {
            WIN32_FIND_DATAW f;
            const HANDLE h = FindFirstFileW((dir + L"\\" + pattern).c_str(), &f);
            if (h == INVALID_HANDLE_VALUE) continue;
            do
                if (!(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back({ dir + L"\\" + f.cFileName, f.ftLastWriteTime });
            while (FindNextFileW(h, &f));
            FindClose(h);
        }
    SavesNewestFirst(out);
}

// What a save says of the map: the one loaded last, an empty slot (SaveSlotEmpty), or nothing yet (being written, or
// not to be read).
enum SaveSays { kSaveUnread, kSaveEmpty, kSaveMap };

static SaveSays SaveReadMap(const std::wstring &path, char *level, size_t size)
{
    const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return kSaveUnread;
    LARGE_INTEGER length = {};
    std::string text;
    bool ok = GetFileSizeEx(h, &length) && length.QuadPart > 0 && length.QuadPart < (64LL << 20);
    if (ok)
    {
        text.resize((size_t)length.QuadPart);
        DWORD got = 0;
        ok = ReadFile(h, &text[0], (DWORD)text.size(), &got, nullptr) && got == text.size();
    }
    CloseHandle(h);
    if (!ok) return kSaveUnread;
    if (MapFromSave(text.data(), text.size(), level, size)) return kSaveMap;
    return SaveSlotEmpty(text.data(), text.size()) ? kSaveEmpty : kSaveUnread;
}

// The map of the slot being played, from the saves (newest first): the newest save that names one. Empty slots are
// passed over, as the game writes one a few seconds after the slot played; taken as the newest, it hid the map that
// changed. The look ends with nothing new at a save no newer than the one read last (written at lastWritten), and at
// a save that cannot be read yet (being written, unread names it), so an older slot does not take its place.
static bool SavePick(const std::vector<SaveFile> &saves, const FILETIME &lastWritten, SaveFile &picked, char *level, size_t size, std::wstring &unread)
{
    unread.clear();
    for (const auto &save : saves)
    {
        if (CompareFileTime(&save.second, &lastWritten) <= 0) return false;
        const SaveSays says = SaveReadMap(save.first, level, size);
        if (says == kSaveEmpty) continue;
        if (says == kSaveUnread)
        {
            unread = save.first;
            return false;
        }
        picked = save;
        return true;
    }
    return false;
}


// The mod's own gearbox holds a position of the player's own (not A): driving, the truck taken.
static bool GearboxHeldBy(bool driving, const GearboxPosModel &pos) { return driving && pos.running && pos.pos != kPosA; }

// The automatic's model, one pass. It steps in a position of the player's own too: it decides nothing and writes
// nothing there, but keeps the wheels' grip learned for the lift and Back to A, and the gear A would ask when it
// comes back by Back to A or the A key (the held gear grip brief). There the grip rises only under kHeldGripPedal.
static void GearboxModelPass(GearboxModel &m, bool driving, const GearboxPosModel &pos, const TruckRaw &raw, const GaugeScale &k, float foot, float power,
                             float dt, const EngineHand &hand)
{
    GearboxStep(m, raw, k, foot, power, dt, hand, GearboxHeldBy(driving, pos));
}

#ifndef GAUGES_SETTINGS_TEST
// The game's folder in the log at the start, and the store its path suggests: where its saves are follows from it.
static void LogGameFolder()
{
    std::wstring lower = g_dir;
    for (auto &c : lower) c = (wchar_t)towlower(c);
    const wchar_t *store = lower.find(L"\\steamapps\\") != std::wstring::npos ? L"a Steam library"
                           : lower.find(L"epic games") != std::wstring::npos ? L"the Epic store's folder"
                           : lower.find(L"windowsapps") != std::wstring::npos || lower.find(L"xboxgames") != std::wstring::npos ? L"the Microsoft Store's folder"
                           : L"a folder that does not tell the store";
    Log(L"game: its folder %s, by its path %s", PathForLog(g_dir).c_str(), store);
}

// ---- the map a truck is on: the game's save says which map it loaded last (maps.h) ----
// Steam keeps the saves in userdata\<account>\1465360\remote of its own folder as CompleteSave*.cfg, the Epic store in
// Documents\My Games\SnowRunner\base\storage\<account> as CompleteSave*.dat (the same save under another name, as
// the community's guides have it). The newest that names a map is the slot being played (SavePick). Documents is the shell's
// (which OneDrive may have moved) and the profile's own, for a game that writes to the latter. The
// Microsoft Store's build keeps them in the app's containers, %LOCALAPPDATA%\Packages\<the game's package>\SystemAppData\wgs,
// as files without names of their own: one whose first bytes name a CompleteSave is taken as such a file. It is
// looked for every kSaveLookMs and read when it is another file or was written since.
static const ULONGLONG kSaveLookMs = 5000;

// The folders that may hold the saves.
static std::vector<std::wstring> SaveFolders()
{
    std::vector<std::wstring> roots, out;
    auto subfolders = [](const std::wstring &dir, const wchar_t *tail, std::vector<std::wstring> &into) {
        WIN32_FIND_DATAW f;
        const HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &f);
        if (h == INVALID_HANDLE_VALUE) return;
        do
            if ((f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wcscmp(f.cFileName, L".") && wcscmp(f.cFileName, L".."))
                into.push_back(dir + L"\\" + f.cFileName + tail);
        while (FindNextFileW(h, &f));
        FindClose(h);
    };
    // Steam's own folder from the registry, and five folders up from the game's (steamapps\common\Snowrunner\Sources\Bin)
    // for a game in Steam's own library
    wchar_t steam[MAX_PATH] = {};
    DWORD size = sizeof steam;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, steam, &size) == ERROR_SUCCESS && steam[0])
    {
        for (wchar_t *p = steam; *p; p++)
            if (*p == L'/') *p = L'\\';
        roots.push_back(steam);
    }
    std::wstring up = g_dir;
    for (int i = 0; i < 5 && !up.empty(); i++)
    {
        while (!up.empty() && up.back() == L'\\') up.pop_back();
        const size_t cut = up.find_last_of(L'\\');
        up = cut == std::wstring::npos ? L"" : up.substr(0, cut);
    }
    if (!up.empty()) roots.push_back(up);
    for (const auto &root : roots) subfolders(root + L"\\userdata", L"\\1465360\\remote", out);
    std::vector<std::wstring> documents;
    wchar_t docs[MAX_PATH] = {};
    if (SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docs) == S_OK && docs[0]) documents.push_back(docs);
    wchar_t profile[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) > 0)
    {
        const std::wstring own = std::wstring(profile) + L"\\Documents";
        if (documents.empty() || _wcsicmp(documents[0].c_str(), own.c_str())) documents.push_back(own);
    }
    for (const auto &d : documents)
    {
        const std::wstring storage = d + L"\\My Games\\SnowRunner\\base\\storage";
        out.push_back(storage);
        subfolders(storage, L"", out);
    }
    return out;
}


// The files of the Microsoft Store build's save containers: wgs\<account>\<container>\<file> in each package of the game.
static void SaveContainerFiles(std::vector<SaveFile> &out)
{
    wchar_t local[MAX_PATH] = {};
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local) != S_OK) return;
    auto each = [](const std::wstring &dir, bool folders, const auto &take) {
        WIN32_FIND_DATAW f;
        const HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &f);
        if (h == INVALID_HANDLE_VALUE) return;
        do
            if (wcscmp(f.cFileName, L".") && wcscmp(f.cFileName, L"..") && !(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == !folders)
                take(dir + L"\\" + f.cFileName, f);
        while (FindNextFileW(h, &f));
        FindClose(h);
    };
    WIN32_FIND_DATAW p;
    const std::wstring packages = std::wstring(local) + L"\\Packages";
    const HANDLE h = FindFirstFileW((packages + L"\\*SnowRunner*").c_str(), &p);
    if (h == INVALID_HANDLE_VALUE) return;
    do
        if (p.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            each(packages + L"\\" + p.cFileName + L"\\SystemAppData\\wgs", true, [&](const std::wstring &account, const WIN32_FIND_DATAW &) {
                each(account, true, [&](const std::wstring &container, const WIN32_FIND_DATAW &) {
                    each(container, false, [&](const std::wstring &file, const WIN32_FIND_DATAW &f) { out.push_back({ file, f.ftLastWriteTime }); });
                });
            });
    while (FindNextFileW(h, &p));
    FindClose(h);
}

// Whether the file's first bytes name a CompleteSave (a save of the game's progress, as the Microsoft Store build's
// containers hold it).
static bool SaveHeadIsComplete(const std::wstring &path)
{
    const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char head[96] = {};
    DWORD got = 0;
    const bool read = ReadFile(h, head, sizeof head - 1, &got, nullptr) != 0;
    CloseHandle(h);
    return read && got > 0 && strstr(head, "CompleteSave") != nullptr;
}

// The saves in those folders and the CompleteSaves in the Microsoft Store build's containers, newest first.
static void Saves(std::vector<SaveFile> &saves)
{
    SavesIn(SaveFolders(), saves);
    std::vector<SaveFile> contained;
    SaveContainerFiles(contained);
    for (const auto &c : contained)
        if (SaveHeadIsComplete(c.first)) saves.push_back(c);
    SavesNewestFirst(saves);
}

// The game's shift function (ShiftGear: a vehicle and a gear, true when the game took the gear), once the code look
// found it: where its bytes are, once only, or behind another mod's hook, which leads on to the game's own. The mod's
// automatic sets its gears through it (Gearbox).
typedef bool (*GameShiftFn)(void *vehicle, int32_t gear);
static GameShiftFn g_shiftGear = nullptr;

// Sets a gear through the game's shift function. A fault in it is caught where the compiler can (MSVC's structured
// exceptions), and the function is not called again in this game.
static bool GameShiftCall(uint64_t vehicle, int gear)
{
#ifdef _MSC_VER
    __try
    {
        return g_shiftGear((void *)vehicle, gear);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_shiftGear = nullptr;
        return false;
    }
#else
    return g_shiftGear((void *)vehicle, gear);
#endif
}

static bool GameShift(uint64_t vehicle, int gear)
{
    if (!g_shiftGear || vehicle < 0x10000) return false;
    const bool took = GameShiftCall(vehicle, gear);
    if (!g_shiftGear) Log(L"gearbox: the game's shift function failed, so the mod's automatic does not set gears again in this game");
    return took;
}

// The exe's code (CodeRangeOf), from its base. False where it has none.
static bool CodeRange(size_t &from, size_t &to) { return CodeRangeOf((const uint8_t *)g_base, 0x1000, from, to); }

// The exe's code copied out through ReadProcessMemory, at its own offsets in copy (the rest zeros): the searches read
// the copy, so a page of the exe that cannot be read is no fault on the mod's thread (it reads as zeros). whole: the
// exe's whole image, its headers and data too (a class's type information lies in its data).
static bool CodeCopy(std::vector<uint8_t> &copy, size_t &from, size_t &to, bool whole = false)
{
    if (!CodeRange(from, to)) return false;
    const size_t first = whole ? 0 : from, last = whole && ImageSize() > to ? (size_t)ImageSize() : to;
    copy.assign(last, 0);
    const size_t kChunk = 1 << 20;
    for (size_t at = first; at < last; at += kChunk)
    {
        const size_t n = last - at < kChunk ? last - at : kChunk;
        if (ReadProcessMemory(GetCurrentProcess(), (const void *)(g_base + at), copy.data() + at, n, nullptr)) continue;
        for (size_t p = at; p < at + n; p += 0x1000) // page by page where one of them cannot be read
        {
            const size_t c = at + n - p < 0x1000 ? at + n - p : 0x1000;
            if (!ReadProcessMemory(GetCurrentProcess(), (const void *)(g_base + p), copy.data() + p, c, nullptr)) memset(copy.data() + p, 0, c);
        }
    }
    return true;
}

// The game's statics in the build that runs (truck.h's g_places, game_globals.h): on the Steam build of 22 July 2026
// its known places, on any other found by the code that uses them. clock: the clock's static, 0 where it is not
// known. False where one the mod cannot do without is not found: it stands down then. quiet: no line for each place (a
// look made again, kPlacesRetryMs after the last).
static const DWORD kPlacesRetryMs = 2000;
static const int kPlacesLooks = 31;
static bool PlacesFind(DWORD stamp, uint64_t &clock, bool quiet = false)
{
    clock = 0;
    if (stamp == kGaugesExeStamp)
    {
        clock = kGameGlobals[kGlobClock].steam;
        return true;
    }
    size_t from = 0, to = 0;
    std::vector<uint8_t> image;
    if (!CodeCopy(image, from, to, true))
    {
        Log(L"places: the exe has no code section to look in");
        return false;
    }
    const ULONGLONG started = GetTickCount64();
    uint64_t found[kGlobCount] = {};
    bool missing = false;
    // each place by its ways in turn (game_globals.h): its signatures, then for the ones the mod cannot do without the
    // ways Next Gen Tire Inflation finds them by
    static const wchar_t *const kWayWords[kWaysMost + 1] = { L"", L"", L"the truck update's three calls", L"the truck update's three calls, the frame left open" };
    for (int g = 0; g < kGlobCount; g++)
    {
        int signatures = 0, way = 0, firstSignatures = 0;
        bool conflict = false, firstConflict = false;
        for (int w = 1; w <= (kGameGlobals[g].needed ? kWaysMost : 1) && !found[g]; w++)
        {
            found[g] = GlobalByWay(image.data(), image.size(), g_base, from, to, g, w, found[kGlobControl], signatures, conflict);
            if (w == 1)
            {
                firstSignatures = signatures;
                firstConflict = conflict;
            }
            if (found[g]) way = w;
        }
        if (quiet) {}
        else if (way == 1) Log(L"places: %S at +%llx, by %d of its signatures", kGameGlobals[g].name, found[g], firstSignatures);
        else if (found[g])
            Log(L"places: %S at +%llx, by way %d (%s)%S", kGameGlobals[g].name, found[g], way,
                g == kGlobGame ? L"beside control" : g == kGlobWheelClass ? L"its type information" : kWayWords[way],
                firstConflict ? "; its signatures point at different places" : firstSignatures ? "; its signatures point outside the exe's data" : "; its signatures stand nowhere once");
        else if (kGameGlobals[g].needed) Log(L"places: %S not found by any of its ways%S", kGameGlobals[g].name, firstConflict ? " (its signatures point at different places)" : "");
        else Log(L"places: %S not found%S, so %S", kGameGlobals[g].name, firstConflict ? " (its signatures point at different places)" : "", kGlobalWithout[g]);
        missing = missing || (!found[g] && kGameGlobals[g].needed);
    }
    if (!quiet) Log(L"places: looked for in %.1f s", (GetTickCount64() - started) / 1000.0);
    g_places = GamePlaces{ found[kGlobControl], found[kGlobWheelClass], found[kGlobGame], found[kGlobMap], found[kGlobViewer], found[kGlobGarage], found[kGlobInput],
                           found[kGlobEngineSets] };
    clock = found[kGlobClock];
    return !missing;
}

// On the Steam build of 22 July 2026 with Probe=1: the signatures of the game's statics (game_globals.h), for the
// mod to find them in other builds, into Gauges.signatures.txt next to the log, as lines for kGlobalSigs, and the
// ones it has checked against the known places. Once for each version of the harvest: the file's first line names
// it, and the harvest runs again where the file is gone or names another (a log of its own, as Gauges.log starts
// anew at each start of the game).
static void PlacesHarvest(DWORD stamp)
{
    if (stamp != kGaugesExeStamp) return;
    const std::wstring path = g_dir + L"Gauges.signatures.txt";
    char head[64], first[64] = "";
    sprintf_s(head, "// harvested from exe %08lx, harvest %d", stamp, kHarvestVersion);
    FILE *f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r") == 0 && f)
    {
        if (!fgets(first, sizeof first, f)) first[0] = 0;
        fclose(f);
        first[strcspn(first, "\r\n")] = 0;
    }
    WritePrivateProfileStringW(L"Signatures", nullptr, nullptr, IniPath().c_str()); // where 0.12.11 to 0.13.0 kept it
    size_t from = 0, to = 0;
    if (!strcmp(first, head) || !CodeRange(from, to) || _wfopen_s(&f, path.c_str(), L"w") != 0 || !f) return;
    const uint8_t *code = (const uint8_t *)g_base;
    const ULONGLONG started = GetTickCount64();
    Log(L"signatures: harvesting the game's statics from this build, for the other builds (a few seconds, once)");
    fprintf(f, "%s\n", head);
    int total = 0;
    for (int g = 0; g < kGlobCount; g++)
    {
        GlobalHarvested sigs[4];
        GlobalHarvestNote note;
        const int n = GlobalHarvest(code, from, to, ImageSize(), kGameGlobals[g].steam, kGameGlobals[g].reach, sigs, 4, &note);
        if (!n) fprintf(f, "// %s: %d references to it, none that stands once in the code, nor one to an object that starts up to 0x%x before it\n", kGameGlobals[g].name, note.direct, kGlobalBaseSpan);
        else if (note.base)
            fprintf(f, "// %s: %d references to it, none that stands once; by the object that starts 0x%x before it, which %d reach\n", kGameGlobals[g].name, note.direct, -note.base, note.baseRefs);
        for (int i = 0; i < n; i++) fprintf(f, "{ %s, \"%s\", %d, %d, %d },\n", kGameGlobals[g].id, sigs[i].pattern, sigs[i].at, sigs[i].after, sigs[i].into);
        total += n;
    }
    // the signatures the mod has, checked against the known places
    for (int g = 0; g < kGlobCount && kGlobalSigs[0].global >= 0; g++)
    {
        int signatures = 0;
        bool conflict = false;
        const uint64_t place = GlobalResolve(code, from, to, kGlobalSigs, g, signatures, conflict);
        fprintf(f, "// %s %s (%d of them stand once)\n", kGameGlobals[g].name, place == kGameGlobals[g].steam ? "found where it is" : place ? "found at the wrong place" : "not found",
                signatures);
    }
    fclose(f);
    Log(L"signatures: %d written to Gauges.signatures.txt in %.1f s", total, (GetTickCount64() - started) / 1000.0);
}

// Another gearbox in the game: the manual transmission mod (SMT) by its file or its published state, else another mod
// that holds one of the game's gearbox functions (review 6, G5: that one is not named SMT). The mod's gearbox stands
// down then, and the tab says why (GaugeView::smtInstalled, ::gearboxHeld).
enum { kGearboxFree, kGearboxSmt, kGearboxHeld };
static volatile LONG g_gearboxBlocked = kGearboxFree;
static int GearboxBlocked(const FunctionFind finds[kFnCount])
{
    if (GetModuleHandleW(L"SMT.addon64") || GetModuleHandleW(L"SMT.asi")) return kGearboxSmt;
    wchar_t name[64];
    swprintf_s(name, L"Local\\SnowRunnerMT.State.%lu", GetCurrentProcessId());
    const HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (map)
    {
        CloseHandle(map);
        return kGearboxSmt;
    }
    for (int f = 0; f < kFnCount; f++)
        if (finds[f].state == kFnHeld) return kGearboxHeld;
    return kGearboxFree;
}

// A stall's gearbox damage, dealt on the game's thread (truck.h, StallJob): the job, the game's damage update (0 until
// found) and the callback the truck update's hook runs every frame (below PeekGame).
static StallJob g_stallJob = {};
static void (*g_damageUpdate)(uint64_t) = nullptr;
static void OnTruckFrame(void *first);

// The places of the hooks, from the finder's look, for the gate (GearboxHooksStep): the gearbox functions, and the
// game's truck update and damage update. g_hooksReady: looked, and no other mod holds the gearbox functions.
static FunctionFind g_finds[kFnCount] = {};
static DamagePath g_path = {};
static bool g_pathFound = false, g_hooksReady = false;
static DrivePlace g_drive = {}; // the game's wheels' drive, for the creep (g_drive.drive 0: not found)

// The game's gearbox functions in its code (game_code.h), looked for once a truck is driven, so a manual transmission
// mod has hooked its own by then: where each one is, or that another mod holds it. Without SMT in the game, the places
// are kept for the gate, which hooks only what the level needs (GearboxHooksStep): the eight gearbox functions,
// calling through, and the game's truck update, for the gearbox's damage on the game's thread.
static void GameCodeLook()
{
    size_t from = 0, to = 0;
    std::vector<uint8_t> code;
    if (!CodeCopy(code, from, to))
    {
        Log(L"gearbox code: the exe has no code section to look in");
        return;
    }
    const ULONGLONG started = GetTickCount64();
    FunctionFind finds[kFnCount];
    GameFunctionsFind(code.data(), from, to, finds);
    static char line[2048];
    GameFunctionsText(finds, line, sizeof line);
    int held = 0;
    for (const auto &f : finds) held += f.state == kFnHeld;
    Log(L"gearbox code (%.1f s, nothing written yet): %S", (GetTickCount64() - started) / 1000.0, line);
    if (held) Log(L"gearbox code: another mod holds %d of the game's gearbox functions; its hooks pass the game's shifts on", held);
    // the wheels' drive, for the creep: hooked by the creep's step (CreepHookStep) at any level of Gearbox
    if (DrivePlaceFind(code.data(), from, to, g_drive))
        Log(L"creep: the game's wheels' drive at +%zx, called at +%zx; hooked once Creep or Probe is on with a truck driven", g_drive.drive, g_drive.call);
    else Log(L"creep: %S, so an idling truck stands as in the game", g_drive.why);
    const FunctionFind &shift = finds[kFnShiftGear];
    // one place only, also behind another mod's jump
    if ((shift.state == kFnFound || shift.state == kFnHeld) && shift.count == 1) g_shiftGear = (GameShiftFn)(g_base + shift.at);
    Log(g_shiftGear ? L"gearbox code: the mod's automatic can set gears through the game's shift function (Gearbox=1)"
                    : L"gearbox code: the game's shift function is not known for sure, so the mod's automatic cannot set gears (Gearbox stays the game's)");
    const int blocked = GearboxBlocked(finds);
    InterlockedExchange(&g_gearboxBlocked, blocked);
    if (blocked == kGearboxSmt)
    {
        Log(L"gearbox: the manual transmission mod (SMT) is installed. It is not compatible with this version and no longer needed: remove SMT.addon64 "
            L"(or SMT.asi) and SMT.ini from Sources\\Bin and start the game again. The mod's gearbox stands down until then");
        return;
    }
    if (blocked == kGearboxHeld)
    {
        Log(L"gearbox: another mod holds the game's gearbox functions, so the mod's gearbox stands down");
        return;
    }
    // the places are kept for the gate (GearboxHooksStep), which attaches the hooks only when a level needs them
    memcpy(g_finds, finds, sizeof finds);
    if (DamagePathFind(code.data(), from, to, g_path)) g_pathFound = true;
    else Log(L"gearbox damage: %S, so a stall costs the gearbox nothing", g_path.why);
    g_hooksReady = true;
}

// The gate (gearbox_hooks.h, GearboxGateStep), every pass once the finder has looked: the eight shift hooks attached
// the first time Gearbox is 1 or 2, the truck update's the first time Gearbox is 2 with Damage on, each through the same
// transaction code with the same suspension of the other threads; at Gearbox=0 nothing is hooked, and a hook a level no
// longer needs stays in place until the game is started again (a hook is never taken out while the game runs). The
// policy that refuses the game's calls is set only while the mod has the truck (TakeoverStep), which needs the hooks:
// without them nothing is ever refused.
static void GearboxHooksStep(const GaugeSettings &s)
{
    if (!g_hooksReady) return;
    static HooksGate gate = {};
    static bool saidNone = false, saidNoTruck = false;
    const HooksWanted want = GearboxHooksWanted(s.gearbox, s.damage, s.shiftOnGameThread != 0);
    const int act = GearboxGateStep(gate, want);
    if (!want.shifts && !gate.shifts && !saidNone)
    {
        saidNone = true;
        Log(L"gearbox: Gearbox=0, the game's gearbox functions are left alone");
    }
    if (want.shifts && !want.truckUpdate && !gate.truckUpdate && !saidNoTruck)
    {
        saidNoTruck = true;
        Log(L"gearbox damage: the game's truck update is left alone (it is hooked at Gearbox=2 with Damage on, or with ShiftOnGameThread=1); the mod's shifts go from its own thread");
    }
    if (act & kGateAttachShifts)
    {
        static char why[1024];
        const int hooked = GearboxHook((uintptr_t)g_base, g_finds, why, sizeof why);
        if (hooked == 8) Log(L"gearbox: eight of the game's gearbox functions hooked at Gearbox=%d, calling through", s.gearbox);
        else if (hooked) Log(L"gearbox: %d of the game's eight gearbox functions hooked at Gearbox=%d, calling through", hooked, s.gearbox);
        for (char *one = why; *one;)
        {
            char *end = strchr(one, '\n');
            if (end) *end = 0;
            Log(L"gearbox: not hooked: %S", one);
            if (!end) break;
            one = end + 1;
        }
        // the mod's own shifts go through to the game's function behind the hook, so the probe counts only the game's.
        // With fewer than eight hooked the mod runs with those it has; each one missing is in the log above
        if (void *own = GearboxOriginal(kFnShiftGear)) g_shiftGear = (GameShiftFn)own;
    }
    if (act & kGateAttachTruck)
    {
        // the damage path: the game's truck update hooked so the damage is dealt on its thread, its damage update after
        char hookWhy[160] = "";
        if (!g_pathFound)
            Log(L"gearbox damage: the game's damage path was not found, so a stall or the clutch's burn costs the gearbox nothing, and the mod's shifts go from its own thread");
        else if (!TruckUpdateHook((uintptr_t)(g_base + g_path.truckUpdate), &OnTruckFrame, hookWhy, sizeof hookWhy))
            Log(L"gearbox damage: %S, so a stall or the clutch's burn costs the gearbox nothing, and the mod's shifts go from its own thread", hookWhy);
        else
        {
            g_damageUpdate = (void (*)(uint64_t))(g_base + g_path.damageUpdate);
            Log(L"gearbox damage: dealt on the game's own thread, from its truck update at +%zx (hooked at Gearbox=2 with Damage on, or with ShiftOnGameThread=1), with its "
                L"damage update at +%zx after; the mod's shifts are dealt there too (ShiftOnGameThread=%d)",
                g_path.truckUpdate, g_path.damageUpdate, s.shiftOnGameThread);
        }
    }
    if (act & kGateShiftsLeft) Log(L"gearbox: Gearbox=0 needs no hooks; the gearbox hooks are left in place until the game is started again");
    if (act & kGateTruckLeft) Log(L"gearbox damage: the truck update's hook is not needed now; left in place until the game is started again");
}

// The hook on the game's wheels' drive (gearbox_hooks.cpp, CreepHook): attached the first time Creep, Probe or the
// damper is on with a truck driven, at any level of Gearbox, and left in place until the game is started again. With
// Creep off it gives no share (TruckPass hands it none); without Probe=1 or the damper the step probe has no block and
// reads nothing. With all three off it is never attached, so the game's drive runs as it is.
static void SuspStepHook(uint64_t action, float dt);
static bool SuspFeaturesOn(const GaugeSettings &s);
static void CreepHookStep(const GaugeSettings &s, bool driving)
{
    static bool tried = false;
    const bool damper = SuspFeaturesOn(s);
    if (tried || (!s.creep && !s.probe && !damper) || !driving || !g_drive.drive) return;
    tried = true;
    DriveStepSet(SuspStepHook);
    char why[160] = "";
    if (CreepHook((uintptr_t)(g_base + g_drive.drive), why, sizeof why))
        Log(L"creep: the game's wheels' drive hooked (%s): %s", s.creep ? L"Creep=1" : s.probe ? L"Probe=1" : L"the damper on",
            s.creep ? L"an idling truck in gear moves, and each wheel's travel is read every physics step for the probe and the damper" : L"each wheel's travel is read every physics step");
    else Log(L"creep: %S, so an idling truck stands as in the game and the travel is not read every step", why);
}

// A file's time as the clock on the wall shows it, hh:mm:ss.
static void ClockText(const FILETIME &t, char *out, size_t size)
{
    FILETIME local;
    SYSTEMTIME st = {};
    FileTimeToLocalFileTime(&t, &local);
    FileTimeToSystemTime(&local, &st);
    snprintf(out, size, "%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
}

static void LogSettings(const GaugeSettings &s)
{
    static const wchar_t *layouts[kLayoutCount] = { L"strip", L"block", L"Spotter dials", L"Spotter digital" }, *styles[kFaceCount] = { L"solid", L"tinted", L"open" },
                         *classes[kClassCount] = { L"truck", L"scout", L"heavy" };
    const GaugeScale k = GaugeScaleOf(s);
    char key[32];
    KeyName(s.toggleKey, key, sizeof key);
    static const wchar_t *oils[kOilGaugeCount] = { L"", L", the oil as a second needle in the temperature dial", L", the oil in a dial of its own" },
                         *profiles[kSpoolCount] = { L"standard", L"small and quick", L"big and slow", L"two stages" };
    Log(L"settings: %s layout%s, %s style, size x%g, opacity %g, offset %g / %g, dials:%s%s%s%s%s%s%s", layouts[s.layout],
        s.swapDials ? L" with the speedometer and the tachometer swapped" : L"", styles[s.style], s.scale, s.opacity, s.offsetX,
        s.offsetY, s.showTemp ? L" temperature" : L"", s.showBoost ? L" boost" : L"", s.showAir ? L" air" : L"", s.showClock ? L" clock" : L"",
        s.showFuelUse ? L" fuel use" : L"", s.showLamps ? L" lamps" : L"", oils[s.oilGauge]);
    Log(L"settings: %s scales: idle %.0f rpm, red zone from %.0f of %.0f, speedometer to %.0f km/h, boost to %g bar, a %s turbo; units %s, %s, %s", classes[s.tachClass], k.idle,
        k.redline, k.max, k.speedMax, k.boost, profiles[s.boostProfile], s.mph ? L"mph" : L"km/h", s.psi ? L"psi" : L"bar", s.fahrenheit ? L"F" : L"C");
    Log(L"settings: tilt lamp amber from %g degrees to a side, red from %g, red from %g up or down a slope", s.rollWarnDeg, s.rollDangerDeg, s.pitchDangerDeg);
    Log(L"settings: coolant warm after %g minutes of driving, in the red zone after %g minutes floored with the wheels spinning at a standstill, cool %g minutes after the engine stops, %g C outside%s",
        s.warmUpMinutes, s.overheatMinutes, s.coolDownMinutes, s.ambientC, s.ambientByMap ? L" where the map's temperature is not known" : L" on every map");
    Log(L"settings: the oil warm after %g minutes of driving, too hot from %g C", s.oilWarmUpMinutes, s.oilWarnC);
    Log(L"settings: the radiator has all its air from %g km/h; engine damage %s: from %g C, %g percent a minute at 125 C, %g percent a minute for a cold engine floored at the red zone at 0 C; %g percent of the gearbox for a shift without the clutch; clutch burn %s, heating at %g percent of the model's pace",
        s.airFullKmh, s.damage ? L"on" : L"off", s.hotDamageFrom, s.hotDamage, s.coldDamage, s.stallDamage, s.clutchBurn ? L"on" : L"off", s.clutchHeat);
    Log(L"settings: suspension spring x%g, damping x%g of the truck's own, %g cm of travel past its compression end", s.springScale, s.dampingScale, s.extraTravel);
    Log(L"settings: sound volume %d of 100%s%s", s.soundVolume, s.soundsOnly ? L", sounds only: the cluster is not drawn" : L"",
        s.dieselFlutter ? L", a diesel's lift flutters"
                        : SoundWhistles() > 0 ? L", a diesel's lift plays the sounds\\turbo_whistle_ files" : L", a diesel's lift is quiet");
    Log(L"settings: driven with %s", s.input == kInputKeys ? L"the keyboard" : s.input == kInputPad ? L"a pad" : L"a wheel's pedals");
    Log(L"settings: gearbox %s; RequireClutch %d, RequireGearHeld %d, SkipNeutral %d, DownStaysForward %d, IgnoreShifter %d, BackToA %d",
        s.gearbox == kGearboxOwn && s.ignoreShifter ? L"the mod's own (Gearbox=2): it takes the shifter, its positions on the bindings (IgnoreShifter: the game's shifter does nothing)"
        : s.gearbox == kGearboxOwn && s.input != kInputWheel ? L"the mod's own (Gearbox=2): it takes the shifter, its positions on the bindings and on the game's shifter"
        : s.gearbox == kGearboxOwn ? L"the mod's own (Gearbox=2): it takes the shifter, its positions on the bindings (with a wheel's pedals the game's shifter stays refused)"
        : s.gearbox == kGearboxModel ? L"the mod's automatic in A (Gearbox=1), the game's shifter yours, its reverse on the brake refused"
                                     : L"the game's own (Gearbox=1 or 2 on the Gearbox tab hand it to the mod)",
        s.requireClutch, s.requireGearHeld, s.skipNeutral, s.downStaysForward, s.ignoreShifter, s.backToA);
    if (s.powertrain) Log(L"settings: powertrain on: the truck pulls with %d percent of the mod's engine and converter, through the game's torque factor; road load %s",
                          s.engineShare, !s.roadLoad ? L"off" : s.powertrain ? L"on (the air's drag, once each gear's pull is learned)" : L"on, acts with Powertrain on");
    else Log(L"settings: powertrain off: the truck pulls with the game's own engine (Powertrain=1 hands it the mod's)");
    Log(s.creep && s.creepManual ? L"settings: creep on, in a gear picked by hand too (CreepManual): an automatic through the converter to walking pace, a gear picked by hand at its idle speed"
        : s.creep ? L"settings: creep on: an idling truck on an automatic moves, through the converter to walking pace; not in a gear picked by hand (CreepManual=1 adds it)"
                : L"settings: creep off: an idling truck in gear stands, as in the game");
    Log(L"settings: toggle key %S, pad %S, demo values %s, probe %d", key, s.padToggle, s.demo ? L"on" : L"off", s.probe);
}

// The suspension level in force for the driven truck, in the log.
static void SuspLevelLog(const SettingsState &st, bool newToMod)
{
    if (!st.key[0]) return;
    const int level = SuspLevel(st);
    if (level == kSuspLevelTruck) Log(L"suspension: this truck's own settings (%S, [Suspension:%S])", st.key, st.id);
    else
        Log(L"suspension: %s%s%s", level == kSuspLevelKind ? L"every " : L"the default settings",
            level == kSuspLevelKind ? Wide(st.key).c_str() : L"",
            !st.id[0] ? L" (the truck's id not read, so no level of its own)"
            : newToMod ? L" (its id is new to the mod: a truck out of the warehouse or a new one; tick This truck only for its own)"
            : st.truckKept ? L" (this truck's own are kept, not in use)" : st.suspKept && level == kSuspLevelDefault ? L" (the kind's own are kept, not in use)" : L"");
}

// Saves the ini and says so in the log.
static void SettingsSave(SettingsState &st)
{
    if (st.own && st.changed) Log(L"settings: saved, the truck's scales in Gauges\\trucks\\%s.ini", SetupFileName(Wide(st.key)).c_str());
    if (st.truckOwn && st.truckChanged) Log(L"settings: saved, this truck's suspension in [Suspension:%S] of Gauges\\trucks\\%s.ini", st.id, SetupFileName(Wide(st.key)).c_str());
    else if (st.suspOwn && st.suspChanged) Log(L"settings: saved, the truck's suspension in Gauges\\trucks\\%s.ini", SetupFileName(Wide(st.key)).c_str());
    if (!(st.own && st.changed) && !(st.suspOwn && st.suspChanged) && !(st.truckOwn && st.truckChanged)) Log(L"settings: saved to Gauges.ini");
    SettingsWrite(st);
    LogSettings(st.s);
}

static bool GameInFront()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// Game memory is read through this, so an object the game freed under the mod is a failed read, not a fault.
static bool PeekGame(uint64_t addr, void *out, size_t size) { return ReadProcessMemory(GetCurrentProcess(), (const void *)addr, out, size, nullptr) != 0; }

// And written through this, for the same reason: the engine's damage (TruckDamageAdd), the gearbox's on a stall (on
// the game's thread, StallJobDeal), with Gearbox=1 the game's
// automatic flag, and with Powertrain=1 or the pull test the game's torque factor.
static bool PokeGame(uint64_t addr, const void *in, size_t size) { return WriteProcessMemory(GetCurrentProcess(), (void *)addr, in, size, nullptr) != 0; }

// The mod's shifts on the game's thread (review 17, step 1; truck.h, ShiftJob): the job, and + 0x70 watched after a shift.
static ShiftJob g_shiftJob = {};
static EngageWatch g_engage = {};
// The game's ShiftGear behind the hook, for the job (GameShiftCall: a fault turns it off for the game).
static bool ShiftOnGame(uint64_t vehicle, int gear) { return g_shiftGear && GameShiftCall(vehicle, gear); }

// On the game's thread, every frame of its truck update: a shift's job and a stall's dealt for the truck the update
// is for, and + 0x70 watched after a shift.
static void OnTruckFrame(void *first)
{
    const TruckMemory m = { PeekGame, g_base, PokeGame };
    const long frame = TruckUpdateFrames();
    if (g_shiftJob.state == kShiftPosted) ShiftJobFrame(g_shiftJob, g_engage, m, (uint64_t)first, ShiftOnGame, frame, GetCurrentThreadId());
    EngageFrame(g_engage, m, frame);
    if (g_stallJob.state != kStallJobPosted) return;
    StallJobFrame(g_stallJob, m, (uint64_t)first, g_damageUpdate);
}

// Havok's wheel constraint's vtable in the game (suspension_scan.h), by its RTTI name, looked for once: 0 where it is
// not found.
static uint64_t SuspVtable()
{
    static bool looked = false;
    static uint64_t vtable = 0;
    if (looked) return vtable;
    looked = true;
    size_t from = 0, to = 0;
    std::vector<uint8_t> image;
    const uint64_t rva = CodeCopy(image, from, to, true) ? VtableByName(image.data(), image.size(), g_base, kSuspConstraintName) : 0;
    vtable = rva ? g_base + rva : 0;
    if (rva) Log(L"suspension: hkpWheelConstraintData's vtable at +%llx", rva);
    else Log(L"suspension: hkpWheelConstraintData's vtable not found: no probe scan and no tune");
    return vtable;
}

// The wheel's collision shape's vtable (hkpCylinderShape), by its RTTI name as the tire mod finds it, looked for once:
// 0 where it is not found, and then the wheels' drag is not read and Air drag learns nothing.
static const char *const kWheelCylinderName = ".?AVhkpCylinderShape@@";
static uint64_t WheelCylinderVtable()
{
    static bool looked = false;
    static uint64_t vtable = 0;
    if (looked) return vtable;
    looked = true;
    size_t from = 0, to = 0;
    std::vector<uint8_t> image;
    const uint64_t rva = CodeCopy(image, from, to, true) ? VtableByName(image.data(), image.size(), g_base, kWheelCylinderName) : 0;
    vtable = rva ? g_base + rva : 0;
    if (rva) Log(L"road load: hkpCylinderShape's vtable at +%llx (the wheels' drag is read by it)", rva);
    else Log(L"road load: hkpCylinderShape's vtable not found: the wheels' drag is not read, and the gears' pull is not learned");
    return vtable;
}

// The probe's scan for each wheel's suspension constraint (suspension_scan.h), read only.
struct SuspProbe
{
    uint64_t vtable;            // the constraint's vtable in the game, 0 = not known
    uint64_t key;               // the vehicle and wheel objects the last scan was of, summed
    SuspHit hits[kSuspHitsMost];
    int count;
    ULONGLONG at;               // when the first two are logged next
};

// The driven truck as the mod's thread follows it from pass to pass.
// What a generation's windows are logged against: each wheel's attach point and axis along up, the limits as written
// when it began, and whether its body was not found (measured against the chassis, its speeds not to be trusted).
struct SuspStepFrame
{
    uint32_t generation;
    float attachUp[kMaxWheels], axisUp[kMaxWheels], low[kMaxWheels], high[kMaxWheels];
    bool other[kMaxWheels];
    bool damping;               // the damper ran on this generation
    int preset;                 // its preset (kDamperPresets), -1 custom
};

// The travel read every physics step (suspension_step.h), on the mod's side: the block published for the physics
// thread, what goes into its generation, and SuspStepTest's course.
struct SuspStepWatch
{
    SuspStepBlock block;        // as published last (serial 0: none)
    uint32_t generation, serial;
    int next;                   // the slot written next
    uint64_t vehicle, constraint[kMaxWheels];
    int wheels;
    float scale[kSuspValues], position[3];
    SuspStepFrame frame, last;  // this generation's and the one before (a window a new generation cut is logged by its own)
    ULONGLONG bodiesAt;         // when the bodies not found are looked for again
    LONG windows, tail, faults, refused; // the windows, the ring's entries, the faults and the refused writes taken so far
    uint64_t testVehicle;       // the truck SuspStepTest runs on: another one waits kStepTestEvery before its first write
    bool damping;               // whether the physics thread wrote on the generation, and its settings
    DamperSet damper;
    SpringSet spring;
    // the attach point's features (build B): each wheel's axle as read at the generation's start, the attach point's
    // stock per constraint (kept across generations, so the mod's own shift is never taken for it), the truck the game
    // was seen moving it on (the features stop there), the guard's count seen, and the display's newest figures
    int partner[kMaxWheels];
    bool isLeft[kMaxWheels], front[kMaxWheels], rearmost[kMaxWheels];
    float forward[kMaxWheels], left[kMaxWheels]; // each wheel's place in the chassis's frame, for the air suspension's plane
    uint64_t attachStockC[kMaxWheels];
    float attachStock[kMaxWheels];
    uint64_t attachOffVehicle;
    LONG attachMoved, displays;
    LONG levelTaken;            // the air suspension's rest places taken, as seen; its next log, and what the tab shows
    ULONGLONG levelLogAt;
    bool airTaken;
    float airLevel[2], airSide;
    SuspDisplaySlot shown;
    float windowRest[kMaxWheels];
    bool windowRested[kMaxWheels];
    uint64_t limitVehicle;      // the truck the clamp's note was told for, and the presets it was told for (bit preset + 1)
    unsigned limitTold;
    int test;                   // SuspStepTest as it runs
    bool on;                    // its write holds
    ULONGLONG due, until, report;
    uint32_t mark;              // the step the trace stood at when the write was made
    float was, value;           // the field before the write and while it held
};

struct TruckWatch
{
    TruckRefs refs;
    bool found;                 // refs are a truck's
    ULONGLONG findAt;           // when to look the objects up again
    ULONGLONG drivingSince;     // since when a truck is driven in the world without a break, 0 = it is not
    TruckRaw probed;            // what the probe wrote last
    World probedWorld;
    ULONGLONG probeAt;          // when the probe writes next
    ULONGLONG gearboxProbeAt;   // and its line of the game's gearbox functions (gearbox_hooks.h)
    bool probedAny;
    float way[3];               // where the chassis was half a second ago, for the speed by the way gone
    ULONGLONG wayAt;
    float byWay;
    TachModel tach;             // the engine speed of the truck at modelVehicle
    BoostModel boost;           // and its boost
    PowertrainModel power;      // and what its engine delivers, handed to the game with Powertrain=1
    GearboxModel gearbox;       // and the gear an automatic with a torque converter would be in, the same way
    ClutchModel clutch;         // and, in a position of the mod's gearbox other than A, the engine behind its clutch
    TakeoverModel take;         // the mod's gearbox has the truck (Gearbox)
    GearboxPosModel pos;        // the gearbox's position, clutch, stalls and shifts (gearbox_model.h)
    GearboxActions actions;     // the player's actions on this pass (from the bindings, at Gearbox=2)
    int stallsOwed;             // stalls whose gearbox damage waits to be posted to the game's thread (StallJob)
    ClutchHeat clutchHeat;      // each kind of truck's clutch heat, and what its burn owes the gearbox (ClutchBurn)
    int burnPoints;             // the gearbox's points posted for the clutch's burn since it began
    int gearboxDamageSeen;      // the gearbox's damage as read on the last pass, -1 not yet (a fall is a repair)
    StallWrite stallWrite;      // what the mod writes into the game's stall seconds (StallStep)
    bool lugSaid;               // the lugging was told in the log
    bool clutchStall;           // the clutch model stalled the engine on this pass, the clutch on a pedal's axis: handed to the game's stall seconds
    bool floorPan;              // the engine box's line after its film title's damage landed (EngineWarn)
    bool lockPress[kLocks];     // DiffLock and Awd pressed on this pass (at every level of Gearbox)
    LocksWatch locks;           // what the mod's keys wrote to the locks (LocksStep)
    bool canReverse;            // a binding reaches reverse (GearDown, GearR or GearN): Gearbox=2 with a wheel's pedals may take the shifter
    int gearboxLevel;           // the Gearbox level in force on the last pass (level 2 runs as 1 without a reverse), for state.h
    bool lowPlusSaid;           // the log said once that the game's shifter's L+ reads as the first gear
    GearboxDoors doors;         // GearboxDoorsNow on the last pass
    PullTest pullTest;          // the pull test (the tab's button)
    uint64_t pullVehicle, pullAction; // the truck whose torque factor a running pull test changed
    TorqueHand hand;            // the engine's torque handed to the game (Powertrain)
    float liftX, liftRoof;      // the lift's x and the roof there on the last pass, for the probe (review 11)
    bool creeping;              // the creep's share was above 0 on the last pass (models.h, CreepShare)
    float creepFor;             // seconds until its next line in the log
    int creepLines;             // its lines since it began
    long creepHits, creepCalls; // the hook's counts at its last line
    CreepModel creep;           // the creep's paced share and its stall (CreepStep)
    ShiftStage stage;           // the state of the mod's last shift (review 17, step 2)
    bool stallWaits;            // a shift without the clutch held back while the last job was out: its stall goes with the next one
    BackModel back;             // BackToA: the timer and the conditions of the return to A
    bool backHand;              // the last pass shifted by hand into a numbered gear
    int backSaid;               // the reason last logged
    SeqModel seq;               // Sequential's own shifts at the red zone and before a stall
    TurboChar turbo;            // the turbo's character in force on this pass (TurboOf)
    bool creepStall;            // the creep stalled a stuck truck on the last pass: handed to the game's stall seconds on this one
    AirModel air;               // and the air in its brakes' tanks
    TiresReader tiresReader;    // the tire inflation mod's published state (tires.h)
    TiresView tires;            // as read on this pass
    bool tiresSeen;
    int probedLifts;            // the lifts of the pedal the boost had told when the probe wrote last
    uint32_t chance;            // what the pitch of the next lift's sound is drawn from
    bool pedalDown;             // the pedal was more than half down on the last step
    uint64_t modelVehicle;
    RangeModel range;           // the range of the truck with the tank it holds
    float burn;                 // the game's fuel burn in litres a minute, smoothed over about a second (the cluster's readout)
    bool burnKnown;             // burn holds a truck's
    TiltModel tilt;             // how the truck stands, and the tilt lamp
    int probedLamp;             // the tilt lamp the probe wrote last
    TempModel temp;             // the coolant and oil temperatures of every truck driven since the last load
    bool loadingWas;            // the game showed its loading screen on the last pass
    char lastId[40];            // the game's id of the last truck taken, kept while none is driven
    char loadId[40];            // lastId as the last loading screen came up
    bool loadPending;           // a load ended and whether the same truck drives on is not decided yet (LoadTruckAfter)
    uint64_t loadEndedAt;       // when it ended
    OilWarning oil;             // the oil warning: on while the driven truck's oil is too hot
    int probedZone;             // where the coolant stood when the probe wrote last (TempZone)
    DamageModel damage;         // what the temperature costs the engine of the truck at modelVehicle
    EngineRecord engine;        // the record of the engine of the truck at engineVehicle
    float mass;                 // that truck's mass in kg by its files (TruckMass), 0 where not read
    bool ownScales;             // the driven truck has scales of its own (Gauges\trucks): their class is the automatic's, else its weight's
    uint64_t engineVehicle;
    EngineProfile profile;      // the engine's own profile (Gauges\engines), for the engine named profileFor
    char profileFor[kMaxText];
    char gearboxKey[96];        // the gearbox of the truck at gearboxVehicle: its key (GearboxKey), "" before one is known
    bool gearHoldOwn;           // [Gearbox:<key>] holds GearHold, which is gearHold; else the truck's class has it
    float gearHold;
    float gearTop[kMaxGears];   // each numbered gear's top as a share of 2a + 5, learned where the game's pull reaches nothing (GearTopStep), 0 not yet;
                                // kept in the truck kind's file by the gearbox (TopShare<n>.<gearbox>)
    char topTruck[kMaxText], topBox[96]; // the kind and gearbox gearTop is for
    uint32_t topUpSaid;         // the gears the stuck rule's shift up was logged from, for this kind and gearbox
    float gradeSinLast;         // the path's grade of the last pass (GradeStep), for the plateau; NAN unknown
    float gearBase[kMaxGears];  // each numbered gear's acceleration at the game's own pull, floored (BaseLearnStep), 0 not yet; kept in the truck's file by engine
    char baseTruck[kMaxText], baseEngine[kMaxText]; // the truck and engine gearBase is for
    BaseLearn baseLearn;
    float baseUse[kMaxGears];   // the a_base each gear goes by: its own, or the truck's figure over its top (BaseFold)
    GradeTrack grade;           // the path's rise over its way in the last second, for the learning's grade
    float baseSettled;          // seconds since the last shift's end (the stage idle, the engaged gear unchanged)
    int baseGearWas;
    ULONGLONG baseSaidAt[kMaxGears]; // when "road load: gear g learning" was last said (probe only, every 10 s at most)
    float accel, speedWas;     // the truck's acceleration over the ground, smoothed over kAccelSmooth, and the speed it goes from
    bool accelSeen;
    float damp;                // the wheels' drag as the truck's acceleration (TruckWheelDamp), smoothed as accel is
    bool dampSeen;             // damp holds a reading: false after a pass it could not be read, so it starts afresh
    DampSay dampSay;           // when the drag read goes into the log (DampSayStep)
    float roadShare, roadLoadA; // the road load taken off on the last pass, as a share of the pull and as m/s², for the probe
    PullBins pull;              // part 1's probe: the game's own pull by x in the gear being measured
    float pullPeak[kMaxGears];  // the most acceleration measured in each gear this start
    GearTopLearn topLearn;      // the plateau being watched
    uint64_t gearboxVehicle;
    uint64_t gearboxWhyVehicle; // the truck the failed name read was told for (once a truck)
    int gearSpeeds;             // that gearbox's speeds as last read: another set means another gearbox (the garage)
    float gearSpeed[kMaxGears];
    ULONGLONG engineAt;         // when a record that was not found is looked for again
    int owed, asked;            // whole points of engine damage that wait to be written: the model's, and the tab's test buttons'
    int cause;                  // what the model's points came from: 1 the heat, 2 a cold engine worked hard
    ULONGLONG writeAt;          // when the next write may go out
    ULONGLONG addedAt;          // when damage was last written, 0 = never: the damage lamp blinks for two seconds after
    const char *refused;        // why the last write did not go out, a string literal; a reason is logged once
    bool probedAdding;          // whether the model was adding damage when the probe wrote last
    ClockWatch clock;           // the game's own count of the seconds it has simulated the truck for, and when it last moved
    ULONGLONG modelAt;          // when the models last ran
    TruckWho who;               // which truck refs are, as the passes read it; who.taken's key is "" while none is read
    char idFor[40];             // the id idVehicle was recorded with, and the vehicle the pass that read that id had (step 2's F1)
    uint64_t idVehicle;
    SuspProbe susp;             // the probe's scan for each wheel's suspension constraint (Probe=1)
    SuspTune tune;              // what SpringScale and DampingScale wrote into each wheel's constraint
    FrameTune frame;            // the frame's joint and what FrameFlex and TwistRange wrote into it (frame_flex.h)
    SuspTravel travel;          // the travel probe: each wheel's height in the chassis's frame (Probe=1)
    ULONGLONG travelAt;         // when it logs next
    SuspStepWatch step;         // the travel read every physics step, and SuspStepTest (Probe=1)
    bool swayOff;               // the sway bars disconnected (BindSwayDisconnect), for this game; they start connected
    uint64_t restVehicle;       // the truck the last rest places were read on, kept while the reads stop (the ride height at 0)
    float rest[2];
    int airHeight;              // the air suspension's height set by BindRideUp and BindRideDown (cm), for this game; it starts at 0
    int airAxles, airFrom;      // the driven kind's air axles (air_axles.h: kAirAll, kAirRear, kAirNone) and where that came from
    char airFor[kMaxText];      // the kind they were read for ("" none yet)
    uint64_t airSaidFor;        // the vehicle the air axles' and the swing axles' lines were logged for
    ULONGLONG airPressedAt;     // the last press, for the overlay's height box
    uint64_t airVehicle;        // the truck the height was set on
    float kmh;                  // the driven truck's speed on the last pass, 0 none
};

// The mod's gearbox has the truck in a position of the player's own (not A): the engine runs behind its clutch.
static bool GearboxManual(const TruckWatch &w) { return GearboxHeldBy(w.take.driving, w.pos); }

// The mod's gearbox lets the truck go: the game's own shifting passes again.
static void GearboxLetGo(TruckWatch &w)
{
    GearboxPolicy(kPolicyPass, nullptr, false);
    w.pos = GearboxPosModel{};
}

// The calls that passed the doors a position picked on the game's shifter comes through, so far: one more on a pass at
// level 1 is the player's hand on the shifter (GearboxHandsBack).
static GearboxDoors GearboxDoorsNow()
{
    GearboxCounts c;
    GearboxCountsGet(c);
    return GearboxDoorsOf(c);
}

// What the game asked for, by the hook that refused it, for the log.
static const wchar_t *GearboxAskName(int fn)
{
    switch (fn)
    {
    case kFnShiftGear: return L"a gear";
    case kFnShiftToAuto: return L"its automatic";
    case kFnShiftToHigh: return L"the high gear";
    case kFnShiftToReverse: return L"reverse";
    case kFnShiftToNeutral: return L"neutral";
    case kFnAutoOffAndShift: return L"a gear with its automatic off";
    case kFnSetPower: return L"a power factor";
    default: return L"something";
    }
}

// The seconds each gear is held going up with the mod's engine: the gearbox's own, else its class's.
static float GearHoldOf(const TruckWatch &w, int tachClass)
{
    return w.gearHoldOwn ? w.gearHold : tachClass == kClassScout ? kEngineGearHoldScout : kEngineGearHold;
}

// The engine's settings per truck (EngineState) in force on the watch: the torque share for the engine fitted and the
// hold for the gearbox fitted, by the most specific level that has them. A level's figure for another engine or gearbox
// (the state not caught up yet) is not put in.
static void EngineApply(const EngineState &e, TruckWatch &w)
{
    if (e.key[0] && e.engine[0] && !strcmp(e.engine, w.profileFor)) w.profile.torque = e.torqueFrom != kEngLevelNone ? e.torque / 100.0f : 0.0f;
    if (e.key[0] && e.gearbox[0] && !strcmp(e.gearbox, w.gearboxKey))
    {
        w.gearHoldOwn = e.hold > 0.0f;
        if (w.gearHoldOwn) w.gearHold = e.hold;
    }
}

// The log, once per truck and engine and at each change (the addendum's point 6): the level each figure comes from.
static void EngineLevelLog(const EngineState &e, const wchar_t *why)
{
    if (!e.key[0]) return;
    if (e.engine[0])
        Log(L"engine torque: %s%S, %d %% for %S (%s)", EngineLevelWords(e.torqueFrom), e.torqueFrom == kEngLevelKind ? e.key : "", e.torque, e.engine,
            e.torqueFrom == kEngLevelNone ? L"none set, so 100 %" : why);
    if (e.gearbox[0])
    {
        if (e.holdFrom == kEngLevelNone) Log(L"gearbox hold: none set for %S, the truck class's (%s)", e.gearbox, why);
        else
            Log(L"gearbox hold: %s%S, %.1f s for %S (%s)", e.holdFrom == kEngLevelTruck ? L"this gearbox in this truck" : e.holdFrom == kEngLevelKind ? L"this gearbox in every " : L"this gearbox's default",
                e.holdFrom == kEngLevelKind ? e.key : "", e.hold, e.gearbox, why);
    }
}

static const wchar_t *const kClassWords[kClassCount] = { L"truck", L"scout", L"heavy" };

// The class the automatic goes by (each gear's hold, and the engine's curve where it has no profile of its own): the
// class of the truck's own scales, and without them the class of its weight (ClassByMass), the plain scales' where
// the weight is not read. tachClass: the class of the scales in force.
static int AutoClass(const TruckWatch &w, int tachClass)
{
    return w.ownScales ? tachClass : ClassByMass(w.mass, tachClass);
}

// Finds the driven truck's gearbox (its name, or its gears where the name is not read) and its own hold, and says so
// in the log when it is another than before. A gearbox keeps its first key: the same gears within kGearboxDrift keep
// the key in force (a name too, where one read of the name fails on the same truck), and a new key of gears takes the
// file of an earlier key of the same gears.
static void GearboxLook(TruckWatch &w, const TruckMemory &m, const TruckRaw &raw, int tachClass)
{
    char name[kMaxText], stock[kMaxText];
    uint32_t at = 0;
    const bool named = TruckGearboxName(m, w.refs.vehicle, name, at, stock);
    char key[96];
    GearboxKey(raw, named ? name : "", key, sizeof key);
    if (!named)
    {
        if (w.gearboxWhyVehicle != w.refs.vehicle)
        {
            // why, once a truck (the Arocs shows both named and unnamed in the logs)
            char why[512];
            TruckGearboxWhy(m, w.refs.vehicle, why, sizeof why);
            Log(L"gearbox: no name read: %S", why);
            w.gearboxWhyVehicle = w.refs.vehicle;
        }
        char held[96] = "";
        GearboxKey(raw, "", held, sizeof held);
        char before[96] = "";
        if (w.gearboxVehicle == w.refs.vehicle && w.gearboxKey[0])
        {
            // the key in force: its gears as they were read when it was taken
            TruckRaw then = raw;
            then.gearSpeeds = w.gearSpeeds;
            memcpy(then.gearSpeed, w.gearSpeed, sizeof then.gearSpeed);
            GearboxKey(then, "", before, sizeof before);
        }
        char file[96];
        float hold = 0.0f, tops[kMaxGears];
        if (before[0] && GearboxKeysMatch(held, before)) strcpy_s(key, w.gearboxKey);
        else if (!GearHoldRead(key, hold) && GearTopRead(key, tops) == 0 && GearboxFileMatch(key, file, sizeof file)) strcpy_s(key, file);
    }
    w.gearboxVehicle = w.refs.vehicle;
    w.gearSpeeds = raw.gearSpeeds;
    memcpy(w.gearSpeed, raw.gearSpeed, sizeof w.gearSpeed);
    if (!strcmp(key, w.gearboxKey)) return;
    strcpy_s(w.gearboxKey, key);
    w.gearHoldOwn = GearHoldRead(key, w.gearHold);
    float oldTops[kMaxGears];
    if (const int old = GearTopRead(key, oldTops))
        Log(L"gearbox: its file in Gauges\\gearboxes has %d gears' tops of an older build (Top<n>, rad/s): not used, each truck kind learns its own as a share of the game's gear speed", old);
    wchar_t whose[96];
    if (w.gearHoldOwn) wcscpy_s(whose, L"its own (its file in Gauges\\gearboxes)");
    else swprintf_s(whose, L"the %s class's%s", kClassWords[tachClass < 0 || tachClass >= kClassCount ? kClassTruck : tachClass],
                    w.ownScales || !(w.mass > 0.0f) ? L"" : L", the class of the truck's weight");
    // the gearbox fitted, and the truck's default where it is another (an upgrade fitted)
    char other[kMaxText + 40] = "";
    if (stock[0] && strcmp(stock, name)) sprintf_s(other, " (the truck's default is %s)", stock);
    if (named) Log(L"gearbox: %S fitted%S; each gear held going up %.1f s, %s", key, other, GearHoldOf(w, tachClass), whose);
    else Log(L"gearbox: its name not read, so it goes by its gears (%S); each gear held going up %.1f s, %s", key, GearHoldOf(w, tachClass), whose);
}

// Looks up the record of the driven truck's engine, and says so in the log when it is another one than before. With
// the probe on, the record's first 0x100 bytes and the floats of a mass's size in the vehicle and its model go into
// the log as well, to find the engine's Torque and the truck's mass.
static void EngineLook(TruckWatch &w, const TruckMemory &m, ULONGLONG now, bool probe)
{
    EngineRecord e;
    TruckEngineRecord(m, w.refs.vehicle, e);
    const float mass = TruckMass(m, w.refs.model);
    if (w.refs.vehicle != w.engineVehicle || mass != w.mass)
    {
        if (mass > 0.0f)
            Log(L"truck: it weighs %.0f kg by its files (without addons, cargo or a trailer): a %s's weight, the class the automatic goes by where the truck "
                L"has no scales of its own",
                mass, kClassWords[ClassByMass(mass, kClassTruck)]);
        else Log(L"truck: its mass by its files was not read (%.0f)", mass);
    }
    w.mass = mass;
    if (w.refs.vehicle != w.engineVehicle || e.found != w.engine.found || strcmp(e.name, w.engine.name) || e.capacity != w.engine.capacity ||
        e.threshold != w.engine.threshold || e.torqueMin != w.engine.torqueMin || e.torqueMax != w.engine.torqueMax || e.torque != w.engine.torque)
    {
        if (e.found)
            Log(L"engine: %S takes %d damage; past %.2f of it the torque falls, from x%.2f to x%.2f | its file's figures: torque %.0f, responsiveness %.2f, "
                L"MaxDeltaAngVel %.3f, fuel consumption %.2f (0: not read)",
                e.name, e.capacity, e.threshold, e.torqueMin, e.torqueMax, e.torque, e.responsiveness, e.deltaAngVel, e.consumption);
        else Log(L"engine: no record of \"%S\" was found; its damage is written without the effect, which the game sets at its next damage event", e.name);
        if (probe && e.found)
        {
            char words[1024];
            for (int row = 0; row < 4; row++)
            {
                TruckWordsText(m, e.at + 0x40 * (uint64_t)row, 16, words, sizeof words);
                Log(L"probe: engine record + 0x%02x: %S", 0x40 * row, words);
            }
        }
        if (probe && w.refs.vehicle != w.engineVehicle)
        {
            char masses[2048];
            const int inVehicle = TruckFloatsText(m, w.refs.vehicle, 0x1000, 300.0f, 100000.0f, 60, masses, sizeof masses);
            Log(L"probe: floats of a mass's size in the vehicle (%d): %S", inVehicle, masses);
            const int inModel = TruckFloatsText(m, w.refs.model, 0x1000, 300.0f, 100000.0f, 60, masses, sizeof masses);
            Log(L"probe: floats of a mass's size in its model (%d): %S", inModel, masses);
        }
    }
    w.engine = e;
    w.engineVehicle = w.refs.vehicle;
    // the engine's own profile, for each engine as it is first known
    if (e.name[0] && strcmp(e.name, w.profileFor))
    {
        strcpy_s(w.profileFor, e.name);
        if (EngineProfileRead(e.name, w.profile))
            Log(L"engine: %S has a profile of its own (Gauges\\engines\\%s.ini): %s%s%s", e.name, SetupFileName(Wide(e.name)).c_str(),
                w.profile.custom ? L"a torque curve of its own" : w.profile.peakRpm > 0.0f ? L"its torque curve from its peak torque's speed" : L"the class's torque curve",
                w.profile.aspiration == kAspNatural ? L", naturally aspirated" : L", turbocharged",
                w.profile.fuel == kFuelDiesel ? L", a diesel" : w.profile.fuel == kFuelPetrol ? L", a petrol engine" : L"");
        else Log(L"engine: %S has no profile of its own: the class's torque curve, turbocharged", e.name);
        if (w.profile.peakRpm > 0.0f) Log(L"engine: its peak torque at %.0f rpm", w.profile.peakRpm);
        if (w.profile.torque > 0.0f) Log(L"engine: the truck gets %.0f percent of its torque (Torque)", w.profile.torque * 100.0f);
    }
    w.engineAt = now + 5000;
}

// One pass over the truck: finds its objects twice a second and when a read fails, reads its values and what the
// game is showing, runs the models, writes the engine's damage into the game when there is some to write, and
// writes the probe log. front: the game's window is in front. True when a truck is driven in the world and its
// values hold up: raw, world and the models in w are then what to draw from.
// A write of the torque factor (TorqueHandStep): at the driven truck's action, or, the game's own share given back to a
// truck no longer driven, only where its vehicle still holds that action.
static void TorqueGive(const TruckMemory &m, const TruckRefs &refs, bool read, const TorqueOrders &o)
{
    if (!o.write || !o.action) return;
    uint64_t action = 0;
    if ((!read || o.action != refs.action) && !(m.get(o.vehicle + 0x68, action) && action == o.action)) return;
    m.put(o.action + 0xCC, o.factor);
}

// A running pull test ends: the torque factor it changed goes back to its truck, where that truck's vehicle still holds
// the drive train it changed (a switch of trucks or a truck lost mid-test left it changed before).
static void PullGiveBack(const TruckMemory &m, TruckWatch &w, const wchar_t *why)
{
    if (w.pullTest.state != kPullRunning) return;
    uint64_t action = 0;
    const bool back = w.pullAction && m.get(w.pullVehicle + 0x68, action) && action == w.pullAction && m.put(w.pullAction + 0xCC, w.pullTest.factor);
    Log(L"pull test: stopped, %s; the truck's torque factor %s", why, back ? L"given back" : L"not given back (its drive train is gone)");
    w.pullTest = PullTest{};
}

// With Probe=1: the driven truck's suspension constraints looked for once for each truck and again when its wheels
// are rebuilt, each logged with its path and floats, then the first two every kSuspEvery ms (suspension_scan.h).
// Reads only. Also the fitted suspension's name (vehicle + 0xd30), to match the floats against the truck's file.
static void SuspProbeStep(SuspProbe &p, const TruckMemory &m, const TruckRefs &r, ULONGLONG now)
{
    p.vtable = SuspVtable();
    if (!p.vtable) return;
    uint64_t key = r.vehicle;
    for (int i = 0; i < r.wheels; i++) key += r.wheel[i] * (uint64_t)(i + 3);
    char text[1400];
    if (key != p.key)
    {
        // each wheel object's model: the entry of the vehicle's list of wheel models that holds it
        uint64_t models[kMaxWheels] = {}, begin = 0, end = 0;
        if (m.get(r.vehicle + 0x200, begin) && m.get(r.vehicle + 0x208, end) && end > begin && end - begin <= kMaxWheels * 8)
            for (uint64_t at = begin; at < end; at += 8)
            {
                uint64_t model = 0, wheel = 0;
                if (!m.get(at, model) || !m.get(model + 0x2C8, wheel)) continue;
                for (int i = 0; i < r.wheels; i++)
                    if (r.wheel[i] == wheel) models[i] = model;
            }
        p.key = key;
        p.count = SuspScan(m, ImageSize(), r.wheel, models, r.wheels, p.vtable, p.hits, kSuspHitsMost);
        char name[kMaxText];
        size_t length = 0;
        Log(L"probe: suspension: %d constraints behind %d wheels, the suspension fitted \"%S\"", p.count, r.wheels,
            TruckNameText(m, r.vehicle + 0xd30, name, length) ? name : "?");
        for (int i = 0; i < p.count; i++)
        {
            char path[96];
            SuspHitWords(p.hits[i], path, sizeof path);
            if (SuspFloatsText(m, p.hits[i].at, p.vtable, text, sizeof text)) Log(L"probe: suspension: %S %p: %S", path, (void *)p.hits[i].at, text);
        }
        p.at = now + kSuspEvery;
    }
    if (now < p.at) return;
    p.at = now + kSuspEvery;
    for (int i = 0; i < p.count && i < 2; i++)
        if (SuspFloatsText(m, p.hits[i].at, p.vtable, text, sizeof text)) Log(L"probe: suspension %d: %S", i, text);
        else p.key = 0; // gone: the next pass scans again
}

// The vtables frame_flex.h checks the joint by, by their RTTI names, looked for once: 0 where one is not found (then
// the frame's joint is not looked for, and nothing is written).
static const FrameVtables &FrameFlexVtables()
{
    static bool looked = false;
    static FrameVtables vt = {};
    if (looked) return vt;
    looked = true;
    size_t from = 0, to = 0;
    std::vector<uint8_t> image;
    if (CodeCopy(image, from, to, true))
    {
        const uint64_t hinge = VtableByName(image.data(), image.size(), g_base, kFrameHingeName), motor = VtableByName(image.data(), image.size(), g_base, kFrameMotorName);
        vt.hinge = hinge ? g_base + hinge : 0;
        vt.motor = motor ? g_base + motor : 0;
    }
    vt.wheelData = SuspVtable();
    if (vt.hinge && vt.motor && vt.wheelData) Log(L"frame flex: the hinge's vtable at +%llx, its motor's at +%llx", vt.hinge - g_base, vt.motor - g_base);
    else Log(L"frame flex: a vtable was not found (hinge %llx, motor %llx, wheel %llx): the frame's joint is not looked for", vt.hinge, vt.motor, vt.wheelData);
    return vt;
}

static float FrameDeg(float rad) { return rad * 180.0f / 3.14159265f; }

// FrameFlex and TwistRange (BRIEF_frame_flex.md part 2, frame_flex.h): the frame's twist spring and limits as the stock
// times the sliders. The joint is looked for on each new truck and after each rebuild, and logged with the way to it
// whether the sliders are at 1 or not (so a drive at 1 is the probe of the chain, with nothing written). At 1 and 1
// nothing is written; the stock goes back where the mod wrote.
static void FrameFlexPass(FrameTune &t, const TruckMemory &m, const TruckRefs &r, const GaugeSettings &s)
{
    const FrameVtables &vt = FrameFlexVtables();
    const bool lookedBefore = t.looked && t.vehicle == r.vehicle;
    const uint64_t keyBefore = t.key;
    int restored = 0;
    FrameTuneStep(t, m, r, vt, s.frameFlex, s.twistRange, &restored);
    if (restored) Log(L"frame flex: the truck driven before: %d values put back to its stock", restored);
    const FrameJoint &j = t.joint;
    if (t.looked && (!lookedBefore || keyBefore != t.key))
    {
        if (t.found)
            Log(L"frame flex: the joint found: wheel %d's body hangs on %p, its hinge %p (data %p, motor %p) to %p, axis %.2f %.2f %.2f%s; %d of %d wheels' constraints found, %d spring hinges refused; %d of %d master entries hold their instance's bodies",
                j.wheel, (void *)j.front, (void *)j.instance, (void *)j.data, (void *)j.motor, (void *)j.other, j.axis[0], j.axis[1], j.axis[2], j.articulated ? L" (an articulated truck's roll)" : L"", j.hung, r.wheels, j.refused,
                j.masterMatch, j.masterSeen);
        else
            Log(L"frame flex: none on this truck, the frame is rigid (%d of %d wheels' constraints found, %d hang on the chassis, %d spring hinges refused; %d of %d master entries hold their instance's bodies)",
                j.hung, r.wheels, j.frontOnChassis, j.refused, j.masterMatch, j.masterSeen);
    }
    if (t.fresh)
        Log(L"frame flex: file %.0f, damping %.0f, twist %.1f to %.1f degrees; now x%.2f, twist x%.2f", t.stock[kFrameSpring], t.stock[kFrameDamping], FrameDeg(t.stock[kFrameMin]),
            FrameDeg(t.stock[kFrameMax]), s.frameFlex, s.twistRange);
    if (t.wroteNow)
        Log(L"frame flex: written x%.2f, twist x%.2f: read back %.0f, damping %.0f, twist %.1f to %.1f degrees", s.frameFlex, s.twistRange, t.back[kFrameSpring], t.back[kFrameDamping],
            FrameDeg(t.back[kFrameMin]), FrameDeg(t.back[kFrameMax]));
}

// SpringScale, DampingScale and ExtraTravel (a test, BRIEF_suspension_simple.md and the addendum of 2 November, 13:18):
// each wheel's spring and damping as the stock times the slider, its compression end that many cm past the file's
// (suspension_scan.h's SuspTuneStep). The stock is read on every truck, for the tab's figures, and nothing is written
// at the defaults. The stock and
// the first writes of each wheel are logged with what was read back right after; writes while a slider moves are not
// counted, and each wheel's values are logged once when the sliders stand still again. A wheel written kSuspWritesBack
// times with the sliders still is one the game keeps putting back, said once.
static const int kSuspWritesTold = 2, kSuspWritesBack = 20;
static void SuspTunePass(SuspTune &t, const TruckMemory &m, const TruckRefs &r, const GaugeSettings &s, const SuspTuneTest *test, const bool *damperOwns,
                         const bool *springOwns)
{
    const float scale[kSuspValues] = { s.springScale, s.dampingScale, 1.0f, 1.0f };
    int restored = 0;
    SuspTuneStep(t, m, r, SuspVtable(), scale, &restored, test, damperOwns, springOwns, s.extraTravel / 100.0f);
    if (restored) Log(L"suspension: the truck driven before: %d values put back to its stock", restored);
    for (int i = 0; i < r.wheels && i < kMaxWheels; i++)
    {
        SuspWheel &w = t.wheel[i];
        if (w.fresh)
            Log(L"suspension: wheel %d's stock spring %g, damping %g, travel %g to %g (its constraint %p)", i, w.stock[kSuspStrength], w.stock[kSuspDamping],
                w.stock[kSuspMin], w.stock[kSuspMax], (void *)w.constraint);
        if (w.settle && !t.moved)
        {
            float v[kSuspValues] = {};
            for (int k = 0; k < kSuspValues; k++) m.get(w.constraint + kSuspValueAt[k], v[k]);
            Log(L"suspension: wheel %d at spring x%g, damping x%g, %g cm of extra travel: read back spring %g, damping %g, travel %g to %g", i, scale[0], scale[1],
                s.extraTravel, v[kSuspStrength], v[kSuspDamping], v[kSuspMin], v[kSuspMax]);
            w.settle = false;
        }
        if (!w.wroteNow || t.moved) continue;
        if (w.writes <= kSuspWritesTold)
            Log(L"suspension: wheel %d written (spring x%g, damping x%g of the stock, %g cm of extra travel): read back spring %g, damping %g, travel %g to %g", i,
                scale[0], scale[1], s.extraTravel, w.back[kSuspStrength], w.back[kSuspDamping], w.back[kSuspMin], w.back[kSuspMax]);
        else if (w.writes == kSuspWritesBack) Log(L"suspension: wheel %d written %d times with the sliders still: the game puts its values back", i, w.writes);
    }
}

// With Probe=1: the travel probe (suspension_scan.h). Once a truck, each wheel's centre in the chassis's frame (along
// forward, up, left) beside its constraint's attach point (+ 0xA0) and axis (+ 0x70), to match the two frames; then
// every kSuspEvery ms each wheel's height along up, its range in the window and its fastest speed (rising positive).
// A new generation of the step probe's (SuspStepPass) starts a new window.
static void SuspTravelProbe(SuspTravel &p, ULONGLONG &at, const TruckMemory &m, const TruckRefs &r, const TruckRaw &t, ULONGLONG now, uint32_t generation)
{
    const bool fresh = p.vehicle != r.vehicle;
    if (!SuspTravelStep(p, t, r.vehicle)) return;
    if (!fresh && p.generation != generation)
    {
        // a slider move, a new constraint or a recovery: a new window, so none mixes two of them (review 14)
        SuspTravelWindow(p, t.wheels);
        at = now + kSuspEvery;
    }
    p.generation = generation;
    if (fresh)
    {
        for (int i = 0; i < t.wheels && i < kMaxWheels; i++)
        {
            float local[3], pivot[3] = {}, axis[3] = {};
            uint64_t c = 0;
            SuspLocal(t, i, local);
            const bool known = m.get(r.wheel[i] + kSuspConstraintAt, c) && m.get(c + 0xA0, pivot) && m.get(c + 0x70, axis);
            Log(L"probe: travel: wheel %d at %.4f, %.4f, %.4f in the chassis's frame; its constraint's attach point %.4f, %.4f, %.4f, axis %.3f, %.3f, %.3f%s", i, local[0],
                local[1], local[2], pivot[0], pivot[1], pivot[2], axis[0], axis[1], axis[2], known ? L"" : L" (not read)");
        }
        at = now + kSuspEvery;
        return;
    }
    if (now < at) return;
    at = now + kSuspEvery;
    wchar_t line[1600];
    int used = 0;
    for (int i = 0; i < t.wheels && i < kMaxWheels && used < 1500; i++)
        used += swprintf_s(line + used, 1600 - used, L"%s%d: %.4f (%.4f to %.4f) %+.3f m/s", i ? L" | " : L"", i, p.last[i], p.least[i], p.most[i], p.fastest[i]);
    Log(L"probe: travel: %s", line);
    SuspTravelWindow(p, t.wheels);
}

// ---- the travel read every physics step (suspension_step.h; BRIEF_suspension_step_probe.md, review 14's phase 1) ----
// With Probe=1 the mod's thread publishes the driven truck's block for the hook on the wheels' drive, which reads each
// wheel's height every step on the physics thread; each window it fills (every kStepWindow s, and at each new
// generation) is logged here. The ring of wheel 0's steps, kept while SuspStepTest is on, is drained into a history the
// test's report reads.
static SuspStepBlock g_stepSlots[2];
static SuspStepBlock *volatile g_stepBlock = nullptr;
static SuspStepState g_step;
static const int kStepHistory = 4096;
static SuspStepTraceEntry g_stepHistory[kStepHistory];
static int g_stepHistoryCount = 0;
static const ULONGLONG kStepTestEvery = 20000;
static const wchar_t *const kDamperCurveNames[kDamperCurves] = { L"linear", L"digressive", L"progressive" };
static const wchar_t *const kSpringCurveNames[kSpringCurves] = { L"linear springs", L"progressive springs", L"dual rate springs" };
// A generation's damper in words: "Trail", "custom" or "off".
static const char *DamperName(bool damping, int preset) { return !damping ? "the damper off" : preset >= 0 ? kDamperPresets[preset].name : "custom"; }
static const float kStepTestFactor[4] = { 1.0f, 0.5f, 0.1f, 1.0f };
static const ULONGLONG kStepTestHold[4] = { 0, 1000, 500, 1000 };
static const float kStepTestShift = 0.02f; // test 3: m toward the droop
static const float kStepTestBefore = 0.2f, kStepTestAfter = 0.5f, kStepTestGap = 0.002f;

static void SuspStepHook(uint64_t action, float dt) { SuspStepLook(g_step, g_stepBlock, action, dt); }

static float SuspSize(const float v[3]) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// The chassis's frame as the game holds it, from what TruckRead read: forward, up, left and position, four floats each.
static void SuspChassisFrame(const TruckRaw &t, float frame[16])
{
    memset(frame, 0, 16 * sizeof(float));
    memcpy(frame, t.forward, 12);
    memcpy(frame + 4, t.up, 12);
    memcpy(frame + 8, t.left, 12);
    memcpy(frame + 12, t.position, 12);
}

// Each wheel's body for its height: the chassis where its constraint's attach point fits the wheel in the chassis's
// frame, else a body of the chassis's class behind the wheel object (two levels deep, as the probe found the
// constraints) whose frame fits it, else the chassis with the wheel's speeds marked not to be trusted. told: the bodies
// behind each wheel are logged, with their place in the chassis's frame, to find the wheel's own body too.
static void SuspStepBodies(SuspStepWatch &p, const TruckMemory &m, const TruckRefs &r, const TruckRaw &t, const uint64_t *constraint, uint64_t *body, bool told,
                           bool quiet)
{
    float chassis[16];
    SuspChassisFrame(t, chassis);
    uint64_t vtable = 0;
    m.get(r.chassis, vtable);
    for (int i = 0; i < r.wheels && i < kMaxWheels; i++)
    {
        float attach[3] = {}, axis[3] = {};
        const bool known = constraint[i] && m.get(constraint[i] + 0xA0, attach) && m.get(constraint[i] + 0x70, axis);
        p.frame.attachUp[i] = attach[1];
        p.frame.axisUp[i] = known ? axis[1] : 0.0f;
        body[i] = r.chassis;
        p.frame.other[i] = false;
        const bool fits = !known || SuspBodyFits(chassis, attach, t.wheelAt[i]);
        if (fits && !told) continue;
        SuspHit hits[16];
        const int n = vtable ? SuspScanRoot(m, ImageSize(), r.wheel[i], i, false, vtable, hits, 0, 16) : 0;
        uint64_t found = 0;
        for (int k = 0; k < n; k++)
        {
            float frame[16], local[3];
            if (!m.get(hits[k].at + 0x170, frame)) continue;
            const bool fit = known && hits[k].at != r.chassis && SuspBodyFits(frame, attach, t.wheelAt[i]);
            if (fit && !found && !fits) found = hits[k].at;
            if (!told) continue;
            const float d[3] = { frame[12] - t.position[0], frame[13] - t.position[1], frame[14] - t.position[2] };
            const float *axes[3] = { t.forward, t.up, t.left };
            for (int a = 0; a < 3; a++) local[a] = d[0] * axes[a][0] + d[1] * axes[a][1] + d[2] * axes[a][2];
            const float off[3] = { frame[12] - t.wheelAt[i][0], frame[13] - t.wheelAt[i][1], frame[14] - t.wheelAt[i][2] };
            char path[96];
            SuspHitWords(hits[k], path, sizeof path);
            Log(L"probe: step: %S a body %p at %.3f, %.3f, %.3f in the chassis's frame, %.3f m from the wheel's centre%s", path, (void *)hits[k].at, local[0], local[1],
                local[2], SuspSize(off), hits[k].at == r.chassis ? L" (the chassis)" : fit ? L" (its constraint's attach point fits it)" : L"");
        }
        if (fits) continue;
        if (found) body[i] = found;
        p.frame.other[i] = !found;
        if (!quiet || found)
            Log(L"probe: step: wheel %d's constraint is on another body (its attach point %.3f, %.3f, %.3f does not fit the chassis): %s", i, attach[0], attach[1],
                attach[2], found ? L"measured against the body it fits" : L"measured against the chassis, its speeds not to be trusted, and the damper leaves it at the truck's own");
    }
}

// The ring's new entries into the history, the oldest dropped where it is full.
static void SuspStepHistoryAdd(SuspStepWatch &p)
{
    static SuspStepTraceEntry fresh[kStepRing];
    const int n = SuspStepDrain(g_step, p.tail, fresh, kStepRing);
    if (n <= 0) return;
    const int keep = g_stepHistoryCount + n > kStepHistory ? kStepHistory - n : g_stepHistoryCount;
    if (keep < g_stepHistoryCount) memmove(g_stepHistory, g_stepHistory + (g_stepHistoryCount - keep), keep * sizeof *g_stepHistory);
    memcpy(g_stepHistory + keep, fresh, n * sizeof *fresh);
    g_stepHistoryCount = keep + n;
}

// SuspStepTest's course, before the tune's pass: the write to hand the tune (its wheel 0's value at the test's factor
// while it holds, at 1 between), or nullptr with the test off.
static const SuspTuneTest *SuspStepTestPass(SuspStepWatch &p, const TruckMemory &m, const TruckRefs &r, const TruckRaw &t, const GaugeSettings &s, ULONGLONG now,
                                            SuspTuneTest &out)
{
    const int mode = s.probe ? s.suspStepTest : 0;
    if (mode != p.test)
    {
        p.test = mode;
        p.on = false;
        p.report = 0;
        p.due = now + kStepTestEvery;
        if (mode == 1) Log(L"probe: step test 1: every 20 s wheel 0's spring at half for 1 s, with the truck standing on its handbrake");
        if (mode == 2) Log(L"probe: step test 2: every 20 s wheel 0's damping at a tenth for 0.5 s, with the truck moving over 2 m/s");
        if (mode == 3) Log(L"probe: step test 3: every 20 s wheel 0's attach point 2 cm toward the droop for 1 s, with the truck standing on its handbrake");
    }
    if (!mode) return nullptr;
    if (r.vehicle != p.testVehicle)
    {
        // another truck: its first write waits, so the tune has written its sliders before the test reads its value
        p.testVehicle = r.vehicle;
        p.on = false;
        p.report = 0;
        p.due = now + kStepTestEvery;
    }
    const int k = mode == 1 ? kSuspStrength : kSuspDamping;
    uint64_t c = 0;
    if (r.wheels > 0) m.get(r.wheel[0] + kSuspConstraintAt, c);
    if (p.on && now >= p.until)
    {
        p.on = false;
        p.report = now + (ULONGLONG)(kStepTestAfter * 1000.0f) + 200;
    }
    else if (!p.on && !p.report && now >= p.due)
    {
        const float speed = SuspSize(t.velocity);
        const bool ready = c && (mode == 2 ? speed > 2.0f : speed < 0.2f && t.handbrake) && (mode != 3 || r.vehicle != p.attachOffVehicle);
        // test 3: the attach point's field (along up) before, and as the physics thread will write it (stock + the shift
        // along the axis, the ride height's included)
        const float ride = (p.front[0] ? s.rideFront : s.rideRear) / 100.0f;
        if (ready && mode == 3 && p.attachStockC[0] == c && p.frame.axisUp[0] != 0.0f)
        {
            p.was = p.attachStock[0] + SuspShiftHold(ride) * p.frame.axisUp[0];
            p.value = p.attachStock[0] + SuspShiftHold(ride + kStepTestShift) * p.frame.axisUp[0];
        }
        if (ready && (mode == 3 ? p.attachStockC[0] == c : m.get(c + kSuspValueAt[k], p.was)))
        {
            p.on = true;
            p.until = now + kStepTestHold[mode];
            const LONG head = g_step.ringHead;
            p.mark = head > 0 ? g_step.ring[(head - 1) % kStepRing].step : 0;
            if (mode != 3) p.value = -1.0f;
        }
        else p.due = now + 1000;
    }
    if (mode == 3) return nullptr; // the physics thread makes test 3's write, not the tune
    out = SuspTuneTest{ 0, k, p.on ? kStepTestFactor[mode] : 1.0f };
    return &out;
}

// The steps from the 0.2 s before at to the 0.5 s after it, ten a line: the step, its seconds, the field, the height
// in mm and the speed in m/s.
static void SuspStepTraceLog(const wchar_t *what, int at)
{
    const SuspStepTraceEntry *e = g_stepHistory;
    const int n = g_stepHistoryCount;
    int to = at;
    for (double after = 0.0; to + 1 < n && after < kStepTestAfter; to++) after += e[to + 1].dt;
    char line[1200];
    int used = 0;
    for (int i = SuspStepBack(e, at, kStepTestBefore), on = 0; i <= to; i++)
    {
        used += _snprintf_s(line + used, sizeof line - used, _TRUNCATE, "%s%u %.4f %g %.1f %+.3f", on ? " | " : "", e[i].step, e[i].dt, e[i].field, e[i].h * 1000.0f, e[i].v);
        if (++on == 10 || i == to)
        {
            Log(L"probe: step trace (%s): %S", what, line);
            used = on = 0;
        }
    }
}

// SuspStepTest's report, 0.5 s after its write ended: where the steps first read the new value, and how the wheel
// answered (test 1: the steps until its height leaves its trend by 2 mm, there and back; test 2: the speed's swing
// against the 0.5 s before).
static void SuspStepTestReport(const SuspStepWatch &p)
{
    const SuspStepTraceEntry *e = g_stepHistory;
    const int n = g_stepHistoryCount;
    int from = 0;
    while (from < n && e[from].step <= p.mark) from++;
    if (p.test == 3)
    {
        // the attach point (BRIEF_suspension_extras.md, test 3): read on which step, whether it held, how far the corner
        // rose (the wheel's height against the body falls as the point moves toward the droop) and after how many steps
        auto first = [&](int at, float value) {
            for (int i = at < 0 ? 0 : at; i < n; i++)
                if (fabsf(e[i].field - value) < 1e-4f) return i;
            return -1;
        };
        const int write = first(from, p.value), back = write >= 0 ? first(write, p.was) : -1;
        if (write < 0)
        {
            Log(L"probe: step test 3: wheel 0's attach point %g to %g was not seen in the steps' trace (%d steps after the write)", p.was, p.value, n - from);
            return;
        }
        int lost = -1;
        const int end = back >= 0 ? back : n;
        for (int i = write; i < end && lost < 0; i++)
            if (fabsf(e[i].field - p.value) >= 1e-4f) lost = i;
        auto mean = [&](int a, int b) {
            double sum = 0.0;
            int k = 0;
            for (int i = a < 0 ? 0 : a; i < b && i < n; i++) sum += e[i].h, k++;
            return k ? (float)(sum / k) : 0.0f;
        };
        const float before = mean(write - 12, write), during = mean(end - 12, end);
        const int leave = SuspStepLeave(e, n, write, kStepTestBefore, kStepTestGap);
        Log(L"probe: step test 3: wheel 0's attach point %g to %g read from step %u, %u steps after the write; %s; the wheel's height against the body %+.1f mm "
            L"(the corner %s), leaving its trend by 2 mm %d steps after the write; back to %g read from step %d",
            p.was, p.value, e[write].step, e[write].step - p.mark, lost < 0 ? L"it held while written" : L"the game wrote another value over it", (during - before) * 1000.0f,
            during < before ? L"rose" : L"did not rise", leave, p.was, back >= 0 ? (int)e[back].step : -1);
        SuspStepTraceLog(L"test 3, the write", write);
        if (back >= 0) SuspStepTraceLog(L"test 3, back", back);
        return;
    }
    const int write = p.value >= 0.0f ? SuspStepFirst(e, n, from, p.value) : -1;
    const wchar_t *field = p.test == 1 ? L"spring" : L"damping";
    if (write < 0)
    {
        Log(L"probe: step test %d: wheel 0's %s %g to %g was not seen in the steps' trace (%d steps after the write)", p.test, field, p.was, p.value, n - from);
        return;
    }
    const uint32_t late = e[write].step - p.mark;
    if (p.test == 1)
    {
        const int back = SuspStepFirst(e, n, write, p.was);
        const int down = SuspStepLeave(e, n, write, kStepTestBefore, kStepTestGap), up = back >= 0 ? SuspStepLeave(e, n, back, kStepTestBefore, kStepTestGap) : -1;
        Log(L"probe: step test 1: wheel 0's spring %g to %g read from step %u, %u steps after the write; the height left its trend by 2 mm %d steps later; back to %g "
            L"read from step %d, the height leaving its trend %d steps later (-1: not seen)",
            p.was, p.value, e[write].step, late, down, p.was, back >= 0 ? (int)e[back].step : -1, up);
        SuspStepTraceLog(L"test 1, the write", write);
        if (back >= 0) SuspStepTraceLog(L"test 1, back", back);
        return;
    }
    float rmsAfter, mostAfter, rmsBefore, mostBefore;
    int stepsAfter, stepsBefore;
    SuspStepSwing(e, n, write, kStepTestAfter, rmsAfter, mostAfter, stepsAfter);
    SuspStepSwing(e, n, SuspStepBack(e, write, kStepTestAfter), kStepTestAfter, rmsBefore, mostBefore, stepsBefore);
    Log(L"probe: step test 2: wheel 0's damping %g to %g read from step %u, %u steps after the write; the speed's swing over the 0.5 s after %.3f m/s rms, %.3f most "
        L"(%d steps), over the 0.5 s before %.3f rms, %.3f most (%d steps)",
        p.was, p.value, e[write].step, late, rmsAfter, mostAfter, stepsAfter, rmsBefore, mostBefore, stepsBefore);
    SuspStepTraceLog(L"test 2, the write", write);
}

// A window's lines: per wheel the steps a second, the repeats, the speeds' histogram, the range against the limits as
// written, then the bounce in mm.
static void SuspStepWindowLog(SuspStepWatch &p, const SuspStepWin &w)
{
    static char bounce[kStepBounceMost * 8 + 16];
    for (int i = 0; i < w.wheels && i < kMaxWheels; i++)
    {
        char bins[2][192];
        for (int side = 0; side < 2; side++)
        {
            int used = 0;
            for (int k = 0; k < kStepBins; k++)
                used += _snprintf_s(bins[side] + used, sizeof bins[side] - used, _TRUNCATE, k < kStepBins - 1 ? "%s<%g:%d" : "%s>%g:%d", k ? " " : "",
                                    k < kStepBins - 1 ? kStepBinTop[k] : kStepBinTop[kStepBins - 2], w.bins[i][side][k]);
        }
        // the frame of the window's own generation: a window a truck switch cut is logged by the truck it was of
        const SuspStepFrame *f = w.generation == p.frame.generation ? &p.frame : w.generation == p.last.generation ? &p.last : nullptr;
        float least = 0.0f, most = 0.0f;
        const float low = f ? f->low[i] : 0.0f, high = f ? f->high[i] : 0.0f;
        const bool along = f && f->axisUp[i] != 0.0f;
        if (along)
        {
            const float a = (w.least[i] - f->attachUp[i]) / f->axisUp[i], b = (w.most[i] - f->attachUp[i]) / f->axisUp[i];
            least = fminf(a, b), most = fmaxf(a, b);
        }
        char damped[320] = "";
        int put = 0;
        if (w.rested[i] && i < kMaxWheels) p.windowRest[i] = w.rest[i], p.windowRested[i] = true;
        if (w.written[i])
            put += _snprintf_s(damped + put, sizeof damped - put, _TRUNCATE, "; the damper wrote %d steps, %.4f on the mean, the clamp holding %.1f %%", w.written[i],
                               w.writtenSum[i] / w.written[i], 100.0f * w.clamped[i] / w.written[i]);
        if (w.springWritten[i] && put >= 0)
            put += _snprintf_s(damped + put, sizeof damped - put, _TRUNCATE, "; the spring wrote %d steps, %.4f on the mean", w.springWritten[i], w.springSum[i] / w.springWritten[i]);
        if (put >= 0)
            put += _snprintf_s(damped + put, sizeof damped - put, _TRUNCATE, "; %d steps in the bump zone, %d at the compression end", w.inZone[i], w.pastEnd[i]);
        if (w.rested[i] && put >= 0) _snprintf_s(damped + put, sizeof damped - put, _TRUNCATE, "; at rest at %.3f along the axis", w.rest[i]);
        Log(L"probe: step: wheel %d, %.1f s of generation %u (%S): %.1f steps a second, dt %.4f to %.4f s; %d of %d steps over 1 m/s repeat the height (%.1f %%); "
            L"compression %S; rebound %S (m/s: steps); height %.4f to %.4f, along its axis %.3f to %.3f against the limits %g to %g%S%s",
            i, w.seconds, w.generation, f ? DamperName(f->damping, f->preset) : "?", w.seconds > 0.0f ? w.steps / w.seconds : 0.0f, w.dtLeast, w.dtMost, w.repeats[i], w.moving[i],
            w.moving[i] ? 100.0f * w.repeats[i] / w.moving[i] : 0.0f, bins[0], bins[1], w.least[i], w.most[i], least, most, low, high, damped,
            !f ? L"; its generation's frame is gone" : f->other[i] ? L"; on another body: not to be trusted" : along ? L"" : L"; its axis not read");
        if (f && f->damping && w.written[i] && w.clamped[i] * 20 > w.written[i])
        {
            // the game's limit: once a truck and preset (the addendum of 01:18, item 3)
            if (p.limitVehicle != p.vehicle) p.limitVehicle = p.vehicle, p.limitTold = 0;
            const unsigned bit = 1u << (f->preset + 1);
            if (!(p.limitTold & bit))
            {
                p.limitTold |= bit;
                Log(L"damper: wheel %d held at the game's limit in %.0f %% of a window's steps (%S on this truck): its damping cannot pass 1, so a factor above 1 "
                    L"over the truck's own cannot act in full", i, 100.0f * w.clamped[i] / w.written[i], DamperName(true, f->preset));
            }
        }
        if (w.bounceSteps[i] <= 0) continue;
        int used = 0;
        for (int k = 0; k < w.bounceSteps[i] && used + 10 < (int)sizeof bounce; k++)
            used += _snprintf_s(bounce + used, sizeof bounce - used, _TRUNCATE, k ? " %.0f" : "%.0f", w.bounce[i][k] * 1000.0f);
        float ratio = 0.0f;
        char decay[64] = "; fewer than two swings";
        if (SuspBounceRatio(w.bounce[i], w.bounceSteps[i], ratio)) _snprintf_s(decay, sizeof decay, _TRUNCATE, "; the second swing %.2f of the first", ratio);
        Log(L"probe: step: wheel %d's bounce from %.2f s into the window, %d steps%S, its height in mm: %S", i, w.bounceAt[i], w.bounceSteps[i], decay, bounce);
    }
}

// The damper's settings as the physics thread takes them.
static DamperSet DamperFrom(const GaugeSettings &s)
{
    return DamperSet{ s.damperCurve, { s.lowCompression, s.lowRebound }, { s.highCompression, s.highRebound }, s.damperKnee };
}

// The spring's features and the damping by position (BRIEF_suspension_extras.md, build A), the same way.
// Its padding zeroed, as the block it goes into is compared whole.
static SpringSet SpringFrom(const GaugeSettings &s)
{
    SpringSet k;
    memset(&k, 0, sizeof k);
    k.curve = s.springCurve, k.rise = s.springRise, k.cross = s.springCross;
    k.bump = s.bumpStops, k.zone = s.bumpZone, k.bumpForce = s.bumpForce, k.bumpDamping = s.bumpDamping;
    k.zones = s.dampingZones, k.soft = s.zoneSoft, k.firm = s.zoneFirm, k.from = s.zoneFrom, k.to = s.zoneTo;
    return k;
}

// The attach point's features in force: a ride height, a sway bar, test 3 (build B), the air suspension (build C).
static bool AttachFeaturesOn(const GaugeSettings &s)
{
    return s.rideFront != 0.0f || s.rideRear != 0.0f || s.swayFront > 0.0f || s.swayRear > 0.0f || (s.probe && s.suspStepTest == 3) || s.airSuspension;
}

// The air suspension turned off and still fading out on the physics thread (review of build C, R2): the block keeps
// shifting until it is done, so the stock never comes back in one step.
static bool AirFading(const SuspStepWatch &p, const TruckRefs &r)
{
    return p.block.shifting && (p.block.air || (g_step.level.action && g_step.level.action == r.action));
}

// Anything the physics thread does each step: the damper off its defaults, a spring feature, the zones, the attach
// point's features, the display.
static bool SuspFeaturesOn(const GaugeSettings &s)
{
    const SpringSet k = SpringFrom(s);
    return DamperOn(DamperFrom(s)) || SpringOn(k) || ZonesOn(k) || AttachFeaturesOn(s) || s.suspDisplay;
}

// The damper let go, in the order the brief sets (its part 2, the addendum's item 5): the block dropped, the physics
// thread's drive called twice more (any truck; 100 ms at most, as a paused game steps nothing), then each wheel's
// damping and strength as the tune gives them written back once. The tune's pass then sees its own values again, and
// on a truck switch puts the stock back after it (review 15). why: for the log.
static void SuspDamperRelease(SuspStepWatch &p, const TruckMemory &m, const wchar_t *why)
{
    if (!p.block.serial || (!p.block.damping && !p.block.springing && !p.block.shifting)) return;
    const SuspStepBlock was = p.block;
    InterlockedExchangePointer((void *volatile *)&g_stepBlock, nullptr);
    p.block = SuspStepBlock{};
    long hits = 0, before = 0, calls = 0;
    CreepCounts(hits, before);
    for (int i = 0; i < 100; i++)
    {
        CreepCounts(hits, calls);
        if (calls - before >= 2) break;
        Sleep(1);
    }
    const int back = SuspDamperLetGo(was, m);
    Log(L"damper: let go (%s): %d wheels' damping and spring back to the truck's own times DampingScale and SpringScale", why, back);
}

// Before the tune's pass: the physics thread let go where it should stop (every feature back at its default, no truck,
// another truck), and the fields it owns now on each wheel, the damping (owns) and the strength (springs), which the
// tune leaves alone.
static void SuspDamperBefore(SuspStepWatch &p, const TruckMemory &m, const TruckRefs &r, const GaugeSettings &s, bool found, bool *owns, bool *springs)
{
    if (p.block.damping || p.block.springing || p.block.shifting)
    {
        if (!found) SuspDamperRelease(p, m, L"no truck driven");
        else if (r.vehicle != p.vehicle) SuspDamperRelease(p, m, L"another truck driven");
        else if (!SuspFeaturesOn(s) && !AirFading(p, r)) SuspDamperRelease(p, m, L"its settings back at the truck's own");
    }
    for (int i = 0; i < kMaxWheels; i++)
    {
        const bool named = i < r.wheels && p.block.constraint[i] != 0 && p.block.wheel[i] == r.wheel[i];
        owns[i] = named && p.block.damping;
        springs[i] = named && p.block.springing && p.block.baseK[i] > 0.0f;
    }
}

// The block for the driven truck (with Probe=1 or the damper on, and a truck), its generation, the windows logged and
// the test's trace kept. Returns the generation, which the 20 Hz travel probe goes by too.
// A kind's air axles (air_axles.h): AirAxles in [Truck] of its file wins, then the mod's table, then all. from: where.
static int AirAxlesRead(const char *key, int &from)
{
    char own[16] = {};
    if (key[0])
    {
        wchar_t buf[16] = {};
        if (GetPrivateProfileStringW(kSetupSections[kSetupTruck], L"AirAxles", L"", buf, 16, SetupFile(kSetupTruck, key).c_str()) > 0)
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, own, sizeof own, nullptr, nullptr);
    }
    return AirAxlesOf(key, own, from);
}

static const wchar_t *AirFromText(int from)
{
    return from == kAirFromKey ? L"the truck's AirAxles" : from == kAirFromTable ? L"the mod's table" : L"not in the mod's table; AirAxles=rear or none in its file in Gauges\\trucks changes it";
}

// Once per truck: which axles have air and from where (the Reviewer's note 8).
static void AirAxlesLog(int kind, int from, const SuspStepWatch &p, const TruckRefs &r)
{
    int fronts = 0;
    for (int i = 0; i < p.wheels && i < kMaxWheels; i++) fronts += p.front[i] ? 1 : 0;
    if (kind == kAirRear)
        Log(L"suspension: air on every axle but the front one (rear), leaf at the front (%d of %d wheels; %s)", fronts, r.wheels, AirFromText(from));
    else if (kind == kAirNone)
        Log(L"suspension: no air suspension on this truck, leaf on every axle (%s): AirSuspension, AirHorizon and the air's height keys do nothing on it", AirFromText(from));
    else Log(L"suspension: air on every axle (all; %s)", AirFromText(from));
}

// Once per truck: the swing axles' mark (101, tonight's part: read and said, nothing changed by it).
static void SwingLog(const TruckMemory &m, const TruckRefs &r)
{
    int swing = 0;
    float most = 0.0f;
    for (int i = 0; i < r.wheels && i < kMaxWheels; i++)
    {
        float multiplier = 0.0f, physics = 0.0f;
        if (!m.get(r.wheel[i] + kWheelCamberMultiplier, multiplier) || !m.get(r.wheel[i] + kWheelCamberPhysics, physics) || !SwingMark(multiplier, physics)) continue;
        swing++;
        most = fmaxf(most, multiplier);
    }
    if (swing) Log(L"suspension: swing axles, the camber follows the travel x%g (the look only; %d of %d wheels)", most, swing, r.wheels);
}

static uint32_t SuspStepPass(SuspStepWatch &p, const TruckMemory &m, const TruckRefs &r, const TruckRaw &t, const GaugeSettings &s, const SuspTune &tune, bool found,
                             ULONGLONG now, bool swayOff, int airHeight, const TiltModel &tilt, int airAxles)
{
    const DamperSet damper = DamperFrom(s);
    const SpringSet spring = SpringFrom(s);
    const bool damping = (DamperOn(damper) || ZonesOn(spring)) && SuspVtable(), springing = SpringOn(spring) && SuspVtable();
    const bool shifting = (AttachFeaturesOn(s) || (r.vehicle == p.vehicle && AirFading(p, r))) && r.vehicle != p.attachOffVehicle && SuspVtable();
    if ((!s.probe && !damping && !springing && !shifting && !s.suspDisplay) || !found)
    {
        SuspDamperRelease(p, m, found ? L"the damper off" : L"no truck driven");
        if (p.block.serial)
        {
            InterlockedExchangePointer((void *volatile *)&g_stepBlock, nullptr);
            p.block = SuspStepBlock{};
        }
        return p.generation;
    }
    const float scale[kSuspValues] = { s.springScale, s.dampingScale, s.extraTravel, 1.0f }; // a change of any is a new generation
    uint64_t constraint[kMaxWheels] = {};
    for (int i = 0; i < r.wheels && i < kMaxWheels; i++) m.get(r.wheel[i] + kSuspConstraintAt, constraint[i]);
    const float jump[3] = { t.position[0] - p.position[0], t.position[1] - p.position[1], t.position[2] - p.position[2] };
    bool missing = false;
    for (int i = 0; i < r.wheels && i < kMaxWheels; i++) missing |= p.frame.other[i];
    const bool running = damping || springing;
    const bool damperChanged = running != p.damping || (running && (memcmp(&damper, &p.damper, sizeof damper) != 0 || !SpringSame(spring, p.spring)));
    const wchar_t *why = !p.block.serial ? L"a start"
                         : r.vehicle != p.vehicle || r.wheels != p.wheels  ? L"a truck switch"
                         : memcmp(scale, p.scale, sizeof scale)         ? L"a slider move"
                         : memcmp(constraint, p.constraint, sizeof constraint) ? L"a constraint at a new address"
                         : SuspSize(jump) > 3.0f                         ? L"a recovery"
                         : damperChanged                                 ? L"a damper setting change"
                         : missing && now >= p.bodiesAt                  ? L"a look for the bodies not found"
                                                                         : nullptr;
    memcpy(p.position, t.position, sizeof p.position);
    SuspStepBlock b = p.block;
    if (why)
    {
        const bool told = r.vehicle != p.vehicle || memcmp(constraint, p.constraint, sizeof constraint) != 0;
        const bool retry = !wcscmp(why, L"a look for the bodies not found");
        uint64_t body[kMaxWheels] = {};
        bool other[kMaxWheels];
        memcpy(other, p.frame.other, sizeof other);
        SuspStepFrame before = p.frame;
        SuspStepBodies(p, m, r, t, constraint, body, told, retry);
        p.bodiesAt = now + 2000;
        bool regained = false;
        for (int i = 0; i < r.wheels && i < kMaxWheels; i++) regained |= other[i] && !p.frame.other[i];
        if (retry && !regained)
        {
            // nothing new: the block and its generation stay
            p.frame = before;
            why = nullptr;
        }
        else
        {
            p.last = before;
            p.generation++;
            p.vehicle = r.vehicle;
            p.wheels = r.wheels;
            memcpy(p.scale, scale, sizeof p.scale);
            memcpy(p.constraint, constraint, sizeof p.constraint);
            b = SuspStepBlock{};
            memcpy(b.body, body, sizeof b.body);
            // the axles, by each wheel's place in the chassis's frame
            float forward[kMaxWheels] = {}, left[kMaxWheels] = {};
            const int n = r.wheels < kMaxWheels ? r.wheels : kMaxWheels;
            for (int i = 0; i < n; i++)
            {
                float local[3];
                SuspLocal(t, i, local);
                forward[i] = local[0], left[i] = local[2];
            }
            SuspPairs(n, forward, left, p.partner, p.isLeft, p.front, p.rearmost);
            memcpy(p.forward, forward, sizeof p.forward);
            memcpy(p.left, left, sizeof p.left);
            // the limits as written now, for this generation's windows (the tune has written its pass by now)
            for (int i = 0; i < r.wheels && i < kMaxWheels; i++)
            {
                p.frame.low[i] = p.frame.high[i] = 0.0f;
                m.get(constraint[i] + kSuspValueAt[kSuspMin], p.frame.low[i]);
                m.get(constraint[i] + kSuspValueAt[kSuspMax], p.frame.high[i]);
            }
            p.frame.generation = p.generation;
            p.frame.damping = running;
            p.frame.preset = running ? DamperPresetOf(s) : -1;
            if (damperChanged || !wcscmp(why, L"a start"))
            {
                if (running)
                {
                    Log(L"damper: %S (%s, compression x%g and x%g, rebound x%g and x%g, the knee %g m/s) for the wheels whose body is known", DamperName(true, p.frame.preset),
                        kDamperCurveNames[damper.curve], damper.low[0], damper.high[0], damper.low[1], damper.high[1], damper.knee);
                    // one line for the spring's features
                    char line[320];
                    int used = _snprintf_s(line, sizeof line, _TRUNCATE, "%S", kSpringCurveNames[spring.curve]);
                    if (spring.curve == 1 && used > 0) used += _snprintf_s(line + used, sizeof line - used, _TRUNCATE, " (x%g at the end)", spring.rise);
                    if (spring.curve == 2 && used > 0)
                        used += _snprintf_s(line + used, sizeof line - used, _TRUNCATE, " (x%g from the crossover at %g of the travel)", spring.rise, spring.cross);
                    if (spring.bump && used > 0)
                        used += _snprintf_s(line + used, sizeof line - used, _TRUNCATE, "; bump stops over the last %g m (a third of the travel at most), the force x%g and the damping x%g at the end", spring.zone, spring.bumpForce,
                                            spring.bumpDamping);
                    if (spring.zones && used > 0)
                        _snprintf_s(line + used, sizeof line - used, _TRUNCATE, "; damping zones x%g from %g to %g of the travel, x%g toward the ends", spring.soft, spring.from,
                                    spring.to, spring.firm);
                    Log(L"spring: %S", line);
                }
                else if (p.damping) Log(L"damper: off");
            }
            p.damping = running;
            p.damper = damper;
            p.spring = spring;
            Log(L"probe: step: generation %u (%s), %d wheels, %S", p.generation, why, r.wheels, DamperName(running, p.frame.preset));
        }
    }
    b.generation = p.generation;
    b.action = r.action;
    b.chassis = r.chassis;
    b.wheels = r.wheels < kMaxWheels ? r.wheels : kMaxWheels;
    memcpy(b.wheel, r.wheel, sizeof b.wheel);
    b.traced = s.probe && p.test && constraint[0] ? constraint[0] + (p.test == 3 ? 0xA4 : kSuspValueAt[p.test == 1 ? kSuspStrength : kSuspDamping]) : 0;
    // the attach point's stock, read where the constraint is new (never one the mod has written), kept for it after
    for (int i = 0; i < b.wheels; i++)
        if (constraint[i] && constraint[i] != p.attachStockC[i] && m.get(constraint[i] + 0xA4, p.attachStock[i])) p.attachStockC[i] = constraint[i];
    // the damper: each wheel whose body is known, at its damping as the tune gives it (stock times DampingScale)
    b.damping = damping;
    b.damper = damping ? damper : DamperSet{};
    b.springing = springing;
    b.spring = spring;
    b.vtable = running || shifting ? SuspVtable() : 0;
    b.shifting = shifting;
    b.testShift = shifting && p.test == 3 && p.on ? kStepTestShift : 0.0f;
    b.display = s.suspDisplay;
    // only the axles with air follow the levelling, AirHorizon and the keys' height (BRIEF_air_axles.md part 2): none
    // has no air suspension at all, rear keeps the front axle on the game's own
    b.air = shifting && s.airSuspension && airAxles != kAirNone;
    b.airKeys = b.air ? airHeight / 100.0f : 0.0f;
    bool airWheel[kMaxWheels] = {};
    AirAxleWheels(b.wheels, p.front, airAxles, airWheel);
    for (int i = 0; i < kMaxWheels; i++) b.leafAxle[i] = i < b.wheels && !airWheel[i];
    b.horizon = b.air && s.airHorizon;
    b.tiltForward = tilt.running ? tilt.forward : NAN, b.tiltLeft = tilt.running ? tilt.left : NAN;
    for (int i = 0; i < kMaxWheels; i++)
    {
        b.constraint[i] = 0;
        b.base[i] = b.baseK[i] = 0.0f;
        // the place along the axis, for the figures and the features: this generation's frame
        const bool placed = i < b.wheels && !p.frame.other[i] && p.frame.axisUp[i] != 0.0f;
        b.attachUp[i] = placed ? p.frame.attachUp[i] : 0.0f;
        b.axisUp[i] = placed ? p.frame.axisUp[i] : 0.0f;
        b.travel[i] = placed && p.frame.low[i] < 0.0f ? -p.frame.low[i] : 0.0f;
        b.attachAt[i] = placed && constraint[i] ? constraint[i] + 0xA4 : 0; // the attach point along up, read each step (the addendum of 11:39)
        b.travelMax[i] = placed ? p.frame.high[i] : 0.0f;
        const SuspWheel &w = tune.wheel[i];
        // the attach point's features: a wheel whose body is known and whose stock was read
        b.attachC[i] = shifting && placed && constraint[i] && p.attachStockC[i] == constraint[i] ? constraint[i] : 0;
        b.attachStock[i] = p.attachStock[i];
        b.ride[i] = (p.front[i] ? s.rideFront : s.rideRear) / 100.0f;
        b.partner[i] = i < b.wheels ? p.partner[i] : -1;
        b.isLeft[i] = p.isLeft[i];
        b.along[i] = p.forward[i], b.across[i] = p.left[i];
        b.sway[i] = swayOff ? 0.0f : p.front[i] ? s.swayFront : s.swayRear;
        b.springK[i] = w.constraint == constraint[i] && SuspValueSane(kSuspStrength, w.stock[kSuspStrength]) ? fminf(1.0f, w.stock[kSuspStrength] * s.springScale) : 0.0f;
        if (!running || i >= b.wheels || p.frame.other[i] || !constraint[i] || w.constraint != constraint[i]) continue;
        const bool damped = damping && SuspValueSane(kSuspDamping, w.stock[kSuspDamping]);
        const bool sprung = springing && SuspValueSane(kSuspStrength, w.stock[kSuspStrength]) && b.travel[i] > 0.0f;
        if (!damped && !sprung) continue;
        b.constraint[i] = constraint[i];
        if (damped) b.base[i] = fminf(1.0f, w.stock[kSuspDamping] * s.dampingScale);
        if (sprung) b.baseK[i] = fminf(1.0f, w.stock[kSuspStrength] * s.springScale);
    }
    // a wheel the physics thread stops owning gets its values back before the new block runs without it
    bool dropped = false;
    for (int i = 0; i < kMaxWheels; i++)
        dropped |= (p.block.constraint[i] && (p.block.constraint[i] != b.constraint[i] || (p.block.base[i] > 0.0f && !(b.base[i] > 0.0f)) ||
                                              (p.block.baseK[i] > 0.0f && !(b.baseK[i] > 0.0f)))) ||
                   (p.block.shifting && p.block.attachC[i] && p.block.attachC[i] != b.attachC[i]);
    if (dropped) SuspDamperRelease(p, m, L"a wheel left to the tune");
    SuspStepBlock was = p.block;
    b.serial = b.seal = was.serial = was.seal = 0;
    if (!p.block.serial || memcmp(&b, &was, sizeof b))
    {
        SuspStepPublish(g_stepSlots, p.next, &g_stepBlock, b, p.serial);
        p.block = b;
        p.block.serial = p.block.seal = p.serial;
    }
    static SuspStepWin win;
    if (SuspStepWindowTake(g_step, p.windows, win) && s.probe) SuspStepWindowLog(p, win);
    // the guard (build B): the game moved an attach point the mod wrote, so the features stop on this truck and the stock
    // goes back, said once
    if (g_step.attachMoved != p.attachMoved)
    {
        p.attachMoved = g_step.attachMoved;
        if (p.block.shifting)
        {
            p.attachOffVehicle = p.vehicle;
            Log(L"suspension: the game moves this truck's attach point (its own ride height control, likely): the ride height, the sway bars, the air "
                L"suspension and test 3 stop on it, and its stock is put back");
            SuspDamperRelease(p, m, L"the game moves the attach point");
        }
    }
    // the air suspension (build C): its rest places said once taken, and with Probe=1 its levelling every 10 s
    // (each wheel levels on its own: the side figure is the left wheels' mean less the right's)
    float level[2] = {}, ref[2] = {}, count[2] = {}, side[2] = {}, sides[2] = {};
    for (int i = 0; p.block.air && g_step.level.taken && i < p.block.wheels; i++)
    {
        if (!p.block.attachC[i]) continue;
        const int axle = p.front[i] ? 0 : 1, at = p.isLeft[i] ? 0 : 1;
        level[axle] += g_step.level.level[i], ref[axle] += g_step.level.ref[i], count[axle] += 1.0f;
        side[at] += g_step.level.level[i], sides[at] += 1.0f;
    }
    for (int a = 0; a < 2; a++)
        if (count[a] > 0.0f) level[a] /= count[a], ref[a] /= count[a];
    const float leftOver = sides[0] > 0.0f && sides[1] > 0.0f ? side[0] / sides[0] - side[1] / sides[1] : 0.0f;
    if (g_step.levelTaken != p.levelTaken)
    {
        p.levelTaken = g_step.levelTaken;
        if (p.block.air)
            Log(L"air suspension: each wheel's rest place taken at a standstill, front %.3f m and rear %.3f m from its stock attach point on the mean (the ride height's shift "
                L"left out); held from now on, side to side too, the ride height %+d cm on top",
                ref[0], ref[1], airHeight);
    }
    if (s.probe && p.block.air && g_step.level.taken && now >= p.levelLogAt)
    {
        p.levelLogAt = now + 10000;
        Log(L"air suspension: levelling front %+.3f m, rear %+.3f m, left over right %+.3f m, the keys' height %+d cm", level[0], level[1], leftOver, airHeight);
    }
    p.airTaken = p.block.air && g_step.level.taken;
    p.airLevel[0] = level[0] * 100.0f, p.airLevel[1] = level[1] * 100.0f, p.airSide = leftOver * 100.0f;
    // the display's newest figures
    const LONG shown = g_step.displays;
    if (b.display && shown != p.displays)
    {
        _ReadBarrier();
        memcpy(&p.shown, &g_step.display[(shown - 1) & 1], sizeof p.shown);
        p.displays = shown;
    }
    if (!b.display) p.shown.wheels = 0;
    if (g_step.faults != p.faults || g_step.refused != p.refused)
    {
        p.faults = g_step.faults;
        p.refused = g_step.refused;
        Log(L"probe: step: the physics thread dropped its block (%ld faults, %ld writes refused as a wheel's constraint changed, so far) until the next one",
            (long)p.faults, (long)p.refused);
    }
    if (b.traced) SuspStepHistoryAdd(p);
    if (p.report && now >= p.report)
    {
        SuspStepTestReport(p);
        p.report = 0;
        p.due = now + kStepTestEvery;
    }
    return p.generation;
}

static bool TruckPass(TruckWatch &w, const TruckMemory &m, const GaugeSettings &s, bool front, TruckRaw &raw, World &world)
{
    const int probe = s.probe;
    const ULONGLONG now = GetTickCount64();
    // the tire inflation mod, when it publishes its state: the cluster's tire lamp goes by it
    w.tires = TiresRead(w.tiresReader, now);
    if (w.tires.present != w.tiresSeen)
        Log(w.tires.present ? L"tire inflation mod: it publishes its state; the cluster shows the tires' pressure mode in a lamp"
                            : L"tire inflation mod: its state is no longer published; the tire lamp is gone");
    w.tiresSeen = w.tires.present;
    // the game's own shifts the hooks refused while the mod's gearbox has the truck: each kind said once a truck, so the
    // drive that first sees a truck in A reverse on the brake names the call that carries it
    const unsigned fresh = GearboxRefusedNew();
    if (fresh & kRefusedBrakeReverse)
    {
        const int door = GearboxBrakeDoor();
        Log(L"gearbox: the game's reverse on the brake refused: the game's automatic was up for a moment (the shift modifier or A picked again); through %S",
            door >= 0 && door < kFnCount ? kGameFunctions[door].name : "?");
    }
    for (unsigned fn = 0; fresh && fn < kFnCount; fn++)
        if (fresh & (1u << fn)) Log(L"gearbox: the game asked for %s, refused: the mod's gearbox has the truck", GearboxAskName((int)fn));
    // the manual shift rebuild's probe (review 17, step 0), with Probe=1: the thread of each of the game's gearbox calls
    // since the last pass, refused ones too, against the truck update's, the drive's and the mod's; and the action's
    // fields around a call that passed through, each word that changed
    GearboxFieldProbe(probe != 0);
    if (probe && GearboxHooked())
    {
        static long last[kFnCount] = {}, lastRefused[kFnCount] = {}, looked[kFnCount] = {};
        GearboxCounts c;
        GearboxCountsGet(c);
        DWORD thread[kFnCount], truckThread = 0, driveThread = 0;
        GearboxThreadsGet(thread, truckThread, driveThread);
        const long count[kFnCount] = { c.shift, c.autoGear, c.high, c.reverse, c.neutral, 0, c.autoOff, c.power, c.vehicle };
        char calls[512] = "";
        size_t n = 0;
        for (int f = 0; f < kFnCount; f++)
        {
            if (count[f] == last[f]) continue;
            const int wrote = snprintf(calls + n, sizeof calls - n, "%s%s x%ld (%ld refused) on thread %lu", n ? ", " : "", kGameFunctions[f].name, count[f] - last[f],
                                       c.refused[f] - lastRefused[f], (unsigned long)thread[f]);
            if (wrote > 0 && (size_t)wrote < sizeof calls - n) n += (size_t)wrote;
            last[f] = count[f];
            lastRefused[f] = c.refused[f];
        }
        if (n)
            Log(L"probe: the game's gearbox calls: %S; the truck update's thread %lu, the drive's %lu, the mod's %lu", calls, (unsigned long)truckThread,
                (unsigned long)driveThread, GetCurrentThreadId());
        static const int lookFns[4] = { kFnShiftGear, kFnAutoOffAndShift, kFnSetPower, kFnShiftToAuto };
        for (const int f : lookFns)
        {
            static GearboxFieldLook look;
            if (GearboxFieldLookGet(f, look) <= looked[f]) continue;
            looked[f] = look.seq;
            char words[600] = "";
            size_t k = 0;
            for (int at = 0; at + 4 <= kFieldLookBytes; at += 4)
            {
                uint32_t a, b;
                memcpy(&a, look.before + at, 4);
                memcpy(&b, look.after + at, 4);
                if (a == b) continue;
                const int wrote = snprintf(words + k, sizeof words - k, " +%02X %08X->%08X", 0x38 + at, a, b);
                if (wrote > 0 && (size_t)wrote < sizeof words - k) k += (size_t)wrote;
            }
            Log(L"probe: the game's %S passed through (gear %d, factor %.2f): fields changed:%S", kGameFunctions[f].name, look.gear, look.factor, k ? words : " none");
        }
    }
    const bool was = w.found;
    bool read = w.found && TruckRead(m, w.refs, raw);
    if (!read && (w.found || now >= w.findAt))
    {
        // lost, or time to look again
        w.found = TruckFind(m, w.refs);
        w.findAt = now + 500;
        read = w.found && TruckRead(m, w.refs, raw);
    }
    else if (now >= w.findAt)
    {
        // the wheels and the gearbox can be rebuilt under a truck that stays: the objects are looked up anew. When the
        // game drives another truck by now, the values read above are the old truck's: this pass has no truck, and the
        // next one finds the new truck.
        TruckRefs again;
        w.findAt = now + 500;
        if (TruckFind(m, again))
        {
            if (again.vehicle != w.refs.vehicle)
            {
                read = false;
                w.findAt = 0;
            }
            w.refs = again;
        }
    }
    if (!read) w.found = false;
    if (w.found != was)
    {
        if (w.found) CrashLineSet();
        char lines[2][160];
        const int count = TruckFoundLines(w.found, raw, lines);
        for (int i = 0; i < count; i++) Log(L"%S", lines[i]);
    }
    if (probe && w.found) SuspProbeStep(w.susp, m, w.refs, now);
    bool damperOwns[kMaxWheels], springOwns[kMaxWheels];
    SuspDamperBefore(w.step, m, w.refs, s, w.found, damperOwns, springOwns);
    SuspTuneTest stepTestNow;
    const SuspTuneTest *stepTest = w.found ? SuspStepTestPass(w.step, m, w.refs, raw, s, now, stepTestNow) : nullptr;
    if (w.found) SuspTunePass(w.tune, m, w.refs, s, stepTest, damperOwns, springOwns);
    if (w.found) FrameFlexPass(w.frame, m, w.refs, s);
    if (w.found && w.step.on && w.step.value < 0.0f && w.refs.wheels > 0)
    {
        // the test's value as written, which the steps' trace looks for
        uint64_t c = 0;
        float v = -1.0f;
        if (m.get(w.refs.wheel[0] + kSuspConstraintAt, c) && m.get(c + kSuspValueAt[stepTest->value], v)) w.step.value = v;
    }
    // the kind's air axles: its file's AirAxles, else the mod's table, else all; read again when the kind changes
    if (strcmp(w.airFor, w.who.taken.key))
    {
        strcpy_s(w.airFor, w.who.taken.key);
        w.airAxles = AirAxlesRead(w.airFor, w.airFrom);
        w.airSaidFor = 0;
    }
    if (w.found && w.refs.vehicle && w.airFor[0] && w.airSaidFor != w.refs.vehicle && w.step.wheels > 0 && w.step.vehicle == w.refs.vehicle)
    {
        w.airSaidFor = w.refs.vehicle;
        AirAxlesLog(w.airAxles, w.airFrom, w.step, w.refs);
        SwingLog(m, w.refs);
    }
    const uint32_t generation = SuspStepPass(w.step, m, w.refs, raw, s, w.tune, w.found, now, w.swayOff, w.airHeight, w.tilt, w.airAxles);
    if (probe && w.found) SuspTravelProbe(w.travel, w.travelAt, m, w.refs, raw, now, generation);
    WorldRead(m, w.found ? w.refs.control : 0, world);
    // a load ended (the game's loading screen gone): the truck that drives on under the game's id it had before the
    // screen keeps its heat, so a travel between regions, a border crossing and a reload are no cold start (a tester's
    // UAZ arrived cold from each travel and took cold-engine damage at once, 2026-10-26; the author's answer: keep
    // it). A game start, the garage, a recovery (a new id) and a change of truck drop the coolant and oil the model
    // remembers for each kind of truck, so every truck starts as cold as the air outside. The id is taken as the screen
    // comes up, as the game can make a truck during it; the decision waits for the first truck read after it, a minute
    // at most (LoadTruckAfter). The game's save names a new map only minutes after a border crossing (found in the
    // game, 2026-10-25), so it is no guide.
    const bool loading = world.known && world.loading;
    if (!w.loadingWas && loading)
    {
        memcpy(w.loadId, w.lastId, sizeof w.loadId);
        w.loadPending = false;
    }
    if (w.loadingWas && !loading)
    {
        w.loadPending = true;
        w.loadEndedAt = now;
    }
    w.loadingWas = loading;
    if (w.loadPending && !read && LoadTruckAfter(w.loadId, nullptr, now - w.loadEndedAt) == kLoadNone)
    {
        w.loadPending = false;
        w.temp = TempModel{};
        w.oil = OilWarning{};
        Log(L"map: a load ended and no truck was driven for a minute, so every truck's engine is as cold as the air outside again (the coolant and the oil)");
    }
    const bool driving = read && TruckSane(raw) && WorldDriving(world);
    w.kmh = driving ? fabsf(raw.speed) * 3.6f : 0.0f;
    // the air suspension's height is the truck's: another truck taken starts at 0, as its rest places do (review of build C, R3)
    if (driving && w.refs.vehicle && w.refs.vehicle != w.airVehicle)
    {
        if (w.airVehicle && w.airHeight) Log(L"air suspension: another truck taken, so the height set (%+d cm) is back at 0", w.airHeight);
        w.airHeight = 0;
        w.airPressedAt = 0;
        w.airVehicle = w.refs.vehicle;
    }
    if (!driving) w.drivingSince = 0;
    else if (!w.drivingSince) w.drivingSince = now;
    // the game's own clock for the truck: while it stands, the game is not simulating and the models wait
    const bool held = read && ClockStands(w.clock, raw, now);
    // what the tab's test buttons asked for goes to a truck that is being driven, further down
    const int asked = (int)InterlockedExchange(&g_damageTest, 0);
    if (asked > 0 && (!driving || held)) Log(L"engine damage: the test's %d points were not written, as no truck is being driven", asked);
    if (!read)
    {
        // the truck is gone: what waited to be written goes with it, and the next truck is looked at anew, also when
        // the game puts it where the old one was
        w.wayAt = 0;
        w.probedAny = false;
        w.owed = w.asked = 0;
        w.engineVehicle = 0;
        w.clock = ClockWatch{};
        w.who = TruckWho{};
        w.burnKnown = false;
        if (w.take.driving)
        {
            Log(L"gearbox: the mod's gearbox lets the truck go: it is not read any more");
            w.take.driving = false;
        }
        GearboxLetGo(w);
        PullGiveBack(m, w, L"the truck is not read any more");
        // the engine's torque the mod handed to the game: the game's own share back, where the truck still is
        TorqueGive(m, w.refs, false, TorqueHandStep(w.hand, false, 0, 0, 0.0f, 1.0f));
        CreepGive(0, 0.0f);
        w.creeping = false;
        w.creep = CreepModel{};
        w.creepStall = false;
        return false;
    }
    // which truck this is (TruckWhoStep). While another truck than the one taken is read, the models wait: on the
    // pass that takes it the settings in force are still the last truck's, and the mod's thread changes them right
    // after. The models that go with one truck then start over, also when the game put it where the last one was.
    TruckIdentity id;
    bool taking = false;
    TruckKey(m, w.refs, raw, id);
    const bool same = TruckWhoStep(w.who, id, taking);
    if (taking) w.modelVehicle = 0;
    if (strcmp(w.who.taken.id, w.idFor) || taking)
    {
        memcpy(w.idFor, w.who.taken.id, sizeof w.idFor);
        w.idVehicle = w.refs.vehicle;
    }
    if (w.loadPending)
    {
        const int after = LoadTruckAfter(w.loadId, id.id, now - w.loadEndedAt);
        if (after == kLoadSame) Log(L"map: a load ended and the same truck drives on (%S), so its engine keeps its heat", id.id);
        else if (after != kLoadWait)
        {
            w.temp = TempModel{};
            w.oil = OilWarning{};
            Log(after == kLoadOther ? L"map: a load ended and another truck is driven, so every truck's engine is as cold as the air outside again (the coolant and the oil)"
                                    : L"map: a load ended and the driven truck's id was not read for a minute, so every truck's engine is as cold as the air outside again (the coolant and the oil)");
        }
        if (after != kLoadWait) w.loadPending = false;
    }
    if (w.who.taken.id[0]) memcpy(w.lastId, w.who.taken.id, sizeof w.lastId);
    if (asked > 0 && driving && !held && !same) Log(L"engine damage: the test's %d points were not written, as the truck was only just taken", asked);
    // the speed by the way the chassis went in the last half second, for the probe to hold the speed against
    if (!w.wayAt || now - w.wayAt >= 500)
    {
        if (w.wayAt)
        {
            const float dx = raw.position[0] - w.way[0], dy = raw.position[1] - w.way[1], dz = raw.position[2] - w.way[2];
            w.byWay = sqrtf(dx * dx + dy * dy + dz * dz) / ((now - w.wayAt) / 1000.0f);
        }
        memcpy(w.way, raw.position, sizeof w.way);
        w.wayAt = now;
    }
    // the models run while the truck is driven: a game held by a menu stands still with its values as they were. They
    // stay with a truck through the menus, the map and the garage. On another truck the engine speed starts over; the
    // range starts over when the truck is of another kind (RangeStep), so a truck put back into the world keeps its
    // litres a kilometre. The tilt starts over on another truck and after a second without a step, as the truck may
    // stand elsewhere by then. The coolant temperature stays with each kind of truck (TempStep). Engine damage that
    // waits to be written belongs to the truck it came from and goes with it. While the game's clock for the truck
    // stands (held), nothing is stepped: the game is not simulating, whatever its window shows.
    const GaugeScale scale = GaugeScaleOf(s);
    const uint64_t kind = TruckKeyNumber(w.who.taken.key);
    // out of the world (the garage, the map, the viewer, a menu): the game's own calls pass, so a gearbox fitted in the
    // garage takes its gear; the gearbox keeps the truck and reads its position again on the way back (review 7, R6)
    if (w.take.driving && !(driving && !held && same) && GearboxHoldingNow()) GearboxPolicy(kPolicyPass, nullptr, false);
    // and no creep there: the hook lets every call pass until the truck is driven in the world again
    if (!(driving && !held && same))
    {
        CreepGive(0, 0.0f);
        w.creeping = false;
        w.creep = CreepModel{};
        w.creepStall = false;
    }
    if (driving && !held && same)
    {
        if (w.refs.vehicle != w.modelVehicle)
        {
            w.tach = TachModel{};
            w.boost = BoostModel{};
            w.power = PowertrainModel{};
            w.gearbox = GearboxModel{};
            w.clutch = ClutchModel{};
            if (w.take.driving) Log(L"gearbox: the mod's gearbox lets the last truck go: another truck is driven");
            w.take = TakeoverModel{};
            GearboxLetGo(w);
            PullGiveBack(m, w, L"another truck is driven");
            w.air = AirModel{};
            w.tilt = TiltModel{};
            w.owed = w.asked = 0;
            w.stallsOwed = 0;
            w.gearboxDamageSeen = -1;
            w.floorPan = false;
            w.modelVehicle = w.refs.vehicle;
            w.modelAt = 0;
        }
        if (!w.modelAt || now - w.modelAt >= 1000) w.tilt = TiltModel{};
        const float dt = w.modelAt && now - w.modelAt < 1000 ? SettingsRange((now - w.modelAt) / 1000.0f, 0.001f, 0.2f) : 0.05f;
        // each gear's top as learned for this gearbox (TruckGearTop goes by it), and a floored plateau watched for: the
        // game's pull at nothing with the wheels steady is where it governs the gear (review 11)
        // per truck kind and gearbox (BRIEF_gear_tops_per_truck.md), as shares of 2a + 5: read again when either changes
        const char *topTruck = w.who.taken.key, *topBox = w.gearboxVehicle == w.refs.vehicle ? w.gearboxKey : "";
        if (strcmp(topTruck, w.topTruck) || strcmp(topBox, w.topBox))
        {
            strcpy_s(w.topTruck, topTruck);
            strcpy_s(w.topBox, topBox);
            w.topLearn = GearTopLearn{};
            w.topUpSaid = 0;
            int outside = 0;
            const int tops = GearTopShareRead(topTruck, topBox, w.gearTop, &outside);
            if (tops) Log(L"gearbox: %d of %S's gears' tops with %S learned before (its file in Gauges\\trucks): the limiter and the floored points go by them", tops, topTruck, topBox);
            if (outside) Log(L"gearbox: %d of %S's kept tops with %S lie outside 0.85 to 1.15 of the game's gear speed: not used, 2a + 5 until learned again", outside, topTruck, topBox);
        }
        const bool sameBox = topBox[0] && raw.gearSpeeds == w.gearSpeeds && !memcmp(raw.gearSpeed, w.gearSpeed, sizeof w.gearSpeed);
        if (sameBox) memcpy(raw.gearTop, w.gearTop, sizeof raw.gearTop);
        int plateauGear = 0;
        float plateauTop = 0.0f;
        if (sameBox && topTruck[0] && GearTopStep(w.topLearn, raw, dt, w.gradeSinLast, plateauGear, plateauTop))
        {
            const float formula = TruckGearFormulaTop(raw, plateauGear), was = w.gearTop[plateauGear], share = formula > 0.0f ? plateauTop / formula : 0.0f;
            const bool farOff = GearTopShareUsable(was) && GearTopFar(was, share);
            w.gearTop[plateauGear] = GearTopMerge(was, share);
            raw.gearTop[plateauGear] = w.gearTop[plateauGear];
            GearTopShareWrite(topTruck, topBox, plateauGear, w.gearTop[plateauGear]);
            Log(L"gearbox: a floored plateau in gear %d, the game's pull %.3f, the grade %.1f%%: the wheels at %.2f rad/s, 2a + 5 gives %.2f (x%.3f); the gear's top x%.3f%s, kept in %S's file",
                plateauGear, raw.actionB4, w.gradeSinLast * 100.0f, plateauTop, formula, share, w.gearTop[plateauGear],
                farOff ? L" (the one kept was more than 15 % off: replaced)" : GearTopShareUsable(was) ? L" (the mean with the one kept)" : L" (learned)", topTruck);
        }
        // the needle: while the mod's gearbox is in A, its converter's engine speed (of the last pass); in a position of
        // its own, the clutch model's, so the needle and the torque curve the game gets go by the one engine the clutch
        // sets (review 11: they went by the wheels' share and a pedal lift that worked with the clutch pressed too)
        if (w.take.driving && !GearboxManual(w)) TachFollow(w.tach, raw, scale, w.gearbox.rpm, dt);
        else if (w.take.driving && w.clutch.running) TachFollow(w.tach, raw, scale, w.clutch.rpm, dt);
        else TachStep(w.tach, raw, scale, dt);
        // the engine's torque curve: its own profile's (Gauges\\engines), else the class's; a naturally aspirated one
        // has no boost, no turbo sounds and no boost dial
        const int autoClass = AutoClass(w, s.tachClass);
        const TorqueCurve curve = EngineCurveOf(w.profile, kClassCurves[autoClass], scale.redline);
        const bool natural = w.profile.aspiration == kAspNatural;
        // the boost (review 12's model: the class's turbo under BoostProfile and BoostMax, the engine's fuel), before the
        // powertrain, so the limit it hands TorqueLift and ClutchStep is this pass's; and a lift of the pedal under it,
        // which is heard (sound.h). Nothing pulls against a free engine: neutral, or the clutch pressed. With the probe
        // on it is logged
        const float tachTop = scale.redline > 0.0f ? scale.max / scale.redline : 1.0f;
        w.turbo = TurboOf(autoClass, s.boostProfile, scale.boost, w.profile.fuel);
        const bool engineFree = raw.gearEngaged == 0 || (GearboxManual(w) && w.clutch.running && w.clutch.grip < 0.5f);
        if (natural) w.boost = BoostModel{};
        const int lift = natural ? kLiftNone
                                 : BoostStep(w.boost, raw, w.turbo, scale.redline > 0.0f ? w.tach.rpm / scale.redline : 0.0f, curve, tachTop,
                                             w.power.running ? w.power.cold : 1.0f, engineFree, dt);
        // a diesel has no blow-off valve: its lift plays the player's own turbo whistle files, and is quiet without them
        // (the one made by code went after the author's drive of 8ED18842), unless DieselFlutter has it flutter
        // (review 12, finding 6)
        const bool whistle = !w.turbo.petrol && !s.dieselFlutter;
        if (lift != kLiftNone && probe)
        {
            if (whistle && (SoundWhistles() <= 0 || s.soundVolume <= 0))
                Log(L"boost: a diesel's lift, quiet, at %.2f bar of the turbo's %.2f", w.boost.lastBar, w.turbo.most);
            else
                Log(L"boost: a %s at %.2f bar of the turbo's %.2f, %.2f of its full sound", whistle ? L"diesel's whistle" : lift == kLiftBlowOff ? L"blow-off" : L"flutter",
                    w.boost.lastBar, w.turbo.most, w.boost.lastVolume);
        }
        LiftSound sound;
        if (LiftSoundFor(w.turbo.petrol, s.dieselFlutter, lift, w.boost.lastVolume, w.boost.lastRotor, s.soundVolume, LiftChance(w.chance), sound))
            SoundPlay(sound.tier, sound.gain, sound.pitch); // the whistle's tier holds no clip without own files: nothing plays
        // the pedal going down again ends a flutter that still rings
        const bool pedalDown = TruckPedal(raw) > 0.5f;
        if (pedalDown && !w.pedalDown) SoundQuiet();
        w.pedalDown = pedalDown;
        RangeStep(w.range, kind, raw, dt);
        // the burn for the readout: the game's rule, nothing with the engine off, smoothed so it does not flicker with the pedal
        // (the rule's numbers not read: not known)
        const float burnNow = TruckFuelRate(raw);
        if (!(burnNow >= 0.0f)) w.burnKnown = false;
        else
        {
            if (!w.burnKnown || !isfinite(w.burn)) w.burn = burnNow;
            else w.burn += (1.0f - expf(-dt / 1.0f)) * (burnNow - w.burn);
            w.burnKnown = isfinite(w.burn);
        }
        // the air dryer's purge: a hiss each time the governor stops the compressor, a little under the oil warning
        if (AirStep(w.air, raw, TachSpeed(w.tach, scale), dt) && TruckEngineOn(raw)) SoundPlay(kSoundPurge, 0.8f * OilBeepGain(s.soundVolume), 1.0f);
        TiltStep(w.tilt, raw, s, dt);
        TempStep(w.temp, kind, raw, TachSpeed(w.tach, scale), s, dt);
        // the oil warning: heard every few seconds while the oil is too hot, and said in the log when it comes and goes
        const bool oilWas = w.oil.on;
        if (OilWarnStep(w.oil, w.temp, raw, s, dt)) SoundPlay(kSoundOil, OilBeepGain(s.soundVolume), 1.0f);
        // the powertrain: what the engine delivers, logged, and handed to the game with Powertrain=1 (below)
        PowertrainStep(w.power, raw, curve, TachSpeed(w.tach, scale), tachTop, w.temp.oil, natural ? 1.0f : w.boost.limit, dt);
        // the automatic: the gear it would be in, the same way, by the pedal as held. With the probe on each of its
        // shifts is logged beside the game's gear while the game's automatic drives a numbered gear
        const int boxGear = w.gearbox.gear;
        const float foot = TruckEngineOn(raw) ? TruckPedal(raw) : 0.0f;
        // the shift points a truck automatic's schedule while the mod's engine is handed to the game, else by the
        // game's own pull; an engine held back (Torque) sets off as a weaker one
        const float launch = w.engine.torque > 0.0f && w.mass > 0.0f && w.engineVehicle == w.refs.vehicle
                                 ? w.engine.torque / w.mass * (w.profile.torque > 0.0f ? w.profile.torque : 1.0f) : 0.0f;
        const EngineHand hand{ w.hand.on, &curve, scale.redline > 0.0f ? scale.max / scale.redline : 1.0f, s.engineShare / 100.0f, launch,
                               GearHoldOf(w, autoClass), EngineLightFloor(curve, scale.redline), s.input, TorqueRoof(autoClass) };
        const int huntsWere = w.gearbox.hunts, topUpsWere = w.gearbox.topUps;
        GearboxModelPass(w.gearbox, w.take.driving, w.pos, raw, scale, foot, w.power.power, dt, hand);
        if (w.gearbox.topUps != topUpsWere && w.take.driving && !GearboxManual(w))
        {
            // the stuck rule shifted up: once per gear for this kind and gearbox (BRIEF_gear_tops_per_truck.md point 3)
            const int from = w.gearbox.topUpFrom;
            if (from >= 1 && from < 32 && !(w.topUpSaid & (1u << from)))
            {
                w.topUpSaid |= 1u << from;
                Log(L"gearbox: floored in gear %d with the game's pull at nothing and the wheels not gaining for %.1f s: the game holds it at its top (%.2f rad/s, the mod's top %.2f), so up to %d",
                    from, kTopStuckFor, TruckDrivenSpin(raw), TruckGearTop(raw, from), from + 1);
            }
        }
        if (GearboxManual(w))
        {
            // a position of the player's own: the automatic's model only reads along, and the engine runs behind the
            // gearbox's clutch; a clutch on an axis is the driver's, without one (a button, or none) it works by itself
            // a stall with the clutch on a pedal's axis is the game's: handed to its stall seconds below, as the gearbox's
            // own; a clutch on a button works by itself and never stalls the engine
            const bool onAxis = w.actions.clutchTravel >= 0.0f;
            // one stall a lug: none of the clutch model's once the creep has stalled the engine (and the creep has no grip
            // to stall by once the clutch model's engine is dead)
            if (ClutchStep(w.clutch, raw, scale, curve, w.pos.travel, w.power.pedal, w.power.cold * w.power.boost, !onAxis, dt) && !w.creep.stalled)
            {
                w.clutchStall = onAxis;
                Log(L"clutch: the engine stalled, the clutch let up at %.0f rpm on the gearbox's input in gear %d; the game's engine stalls with it", w.clutch.input,
                    raw.gearEngaged);
            }
        }
        else
        {
            // told only while the model drives the game's gears, never in a position of the player's own, where it only
            // reads along; with the player in a position of the game's own (L+, H, L, R at Gearbox=1) the model's hunts
            // are its own following, not the truck's (review 11)
            if (w.gearbox.hunts != huntsWere && w.take.driving)
                Log(L"gearbox: a shift up taken back within %.0f s with the foot on (the grade or the ground), the %d%s time; no shift up past %d for %.0f s, "
                    L"the limiter's neither, then it tries again",
                    kHuntWindow, w.gearbox.hunts, w.gearbox.hunts == 1 ? L"st" : w.gearbox.hunts == 2 ? L"nd" : w.gearbox.hunts == 3 ? L"rd" : L"th",
                    w.gearbox.keep, w.gearbox.keepFor);
            w.clutch = ClutchModel{};
        }
        // the clutch's heat from the clutch as stepped, by the engine's most torque and its profile's share, for every
        // clutch, a pedal on an axis or the one that works by itself (a button, or none; the user, 26 October: "it should
        // do it if trying to use a too high gear even when using the sequential mode"). The automatic clutch's own take-up
        // in first stays cold (0.17 of the burn point floored, models test); what burns is the long slip of a start in a
        // gear the engine cannot pull, whoever drives. Outside a position of the player's the clutch is empty and only cools
        {
            const float most = w.engine.torque > 0.0f && w.engineVehicle == w.refs.vehicle ? w.engine.torque * (w.profile.torque > 0.0f ? w.profile.torque : 1.0f) : 0.0f;
            // a repair (the gearbox's damage as read fell: the game's repair or a repair point): the clutch cold again
            // and a burn's points not dealt yet dropped, so nothing of it lands on the repaired gearbox
            if (raw.gearboxKnown)
            {
                if (w.gearboxDamageSeen >= 0 && raw.gearboxDamage < w.gearboxDamageSeen)
                {
                    const bool dropped = StallJobDropBurn(g_stallJob);
                    if (w.clutchHeat.q > 0.0f || dropped) Log(L"clutch: repaired, the heat is gone%s", dropped ? L" and the burn's points not dealt yet dropped" : L"");
                    ClutchHeatRepair(w.clutchHeat);
                }
                w.gearboxDamageSeen = raw.gearboxDamage;
            }
            const bool burnWas = w.clutchHeat.burning;
            ClutchHeatStep(w.clutchHeat, kind, w.clutch, most, s.damage && s.clutchBurn, dt, isfinite(raw.speed) ? fabsf(raw.speed) * 3.6f : 0.0f,
                           s.clutchHeat / 100.0f);
            if (w.clutchHeat.burning && !burnWas)
            {
                w.burnPoints = 0;
                Log(s.damage && s.clutchBurn && g_damageUpdate ? L"clutch: burning, the gearbox pays"
                    : s.damage && s.clutchBurn ? L"clutch: burning, but the game's damage path was not found, so the gearbox pays nothing"
                                               : L"clutch: burning, but Damage or ClutchBurn is off, so the gearbox pays nothing");
            }
            else if (!w.clutchHeat.burning && burnWas) Log(L"clutch: cooled under its burn point; the burn cost the gearbox %d points", w.burnPoints);
        }
        if (probe && boxGear >= 1 && w.gearbox.gear >= 1 && w.gearbox.gear != boxGear && raw.automatic && raw.gearEngaged >= 1 && raw.gearEngaged <= TruckTopGear(raw))
            Log(L"automatic: the model would shift %d to %d at %.0f rpm on the gearbox's input, the pedal at %.2f (the game's gear %d)", boxGear, w.gearbox.gear,
                w.gearbox.input, foot, raw.gearEngaged);
        // the pull test (the tab's button): armed until the truck is floored in the fourth gear or higher, then seven
        // seconds of the torque factor changed and the pull measured, the gear held and the mod's automatic left out
        const LONG pullAsk = InterlockedExchange(&g_pullTestAsked, 0);
        if (pullAsk == 1 && w.pullTest.state != kPullRunning)
        {
            w.pullTest = PullTest{ kPullArmed };
            Log(L"pull test: armed; it runs once the truck is floored in the fourth gear or higher, the wheels under %.0f %% of the gear's top, the game's "
                L"automatic flag down", kPullTopMost * 100.0f);
        }
        else if (pullAsk == 2 && (w.pullTest.state == kPullArmed || w.pullTest.state == kPullRunning))
        {
            if (w.pullTest.state == kPullRunning) m.put(w.refs.action + 0xCC, w.pullTest.factor);
            w.pullTest.state = kPullOff;
            Log(L"pull test: cancelled from the tab");
        }
        const int pullWas = w.pullTest.state;
        const PullOrders pull = PullTestStep(w.pullTest, raw, foot, dt);
        if (pullWas == kPullArmed && w.pullTest.state == kPullRunning)
        {
            w.pullVehicle = w.refs.vehicle;
            w.pullAction = w.refs.action;
            Log(L"pull test: running in gear %d at %.1f m/s, the torque factor %.3f; keep it floored", w.pullTest.gear, raw.speed, w.pullTest.factor);
        }
        if (pull.write) m.put(w.refs.action + 0xCC, pull.factor);
        if (pull.hold || pull.ended)
        {
            // the gear held is the game's: the automatic's model goes on from it, not from a gear it picked meanwhile
            w.gearbox.gear = raw.gear;
            w.gearbox.pause = fmaxf(w.gearbox.pause, kShiftPause);
        }
        if (pull.ended)
        {
            char first[1400], second[320];
            PullTestText(w.pullTest, first, sizeof first, second, sizeof second);
            Log(L"%S", first);
            Log(L"%S", second);
        }
        const bool testing = w.pullTest.state == kPullRunning || pull.ended;
        // the levels of Gearbox (with the hooks in place): 1 the mod's automatic in A, the game's shifter the player's and
        // only the game's reverse on the brake refused; 2 the mod's own gearbox, which takes the truck the moment it is
        // driven, in whatever position, and refuses every shift of the game's. Driven with a pad or the keyboard the game's
        // shifter picks a position too (its refused calls read as requests), so reverse is always reachable; with a wheel's
        // pedals the game's shifts stay refused and level 2 runs as 1 while no binding reaches reverse (w.canReverse), so a
        // truck can always be backed up (the user's rule, 2026-10-27). No gear is set while the pull test holds one
        const bool shifterAsks = s.input != kInputWheel && !s.ignoreShifter; // IgnoreShifter: the refused calls are not asks either
        const int level = s.gearbox == kGearboxOwn && !shifterAsks && !w.canReverse ? kGearboxModel : s.gearbox;
        w.gearboxLevel = level;
        const bool own = level == kGearboxOwn;
        GearboxShifterAsks(own && shifterAsks);
        // the game's shifter's request since the last pass, taken every pass so none waits out a pause
        GearboxAsk ask = {};
        const bool shifterAsked = GearboxAskTake(ask) && own && shifterAsks;
        const TakeoverOrders orders = TakeoverStep(w.take, raw, level >= kGearboxModel && g_shiftGear != nullptr && GearboxHooked(), !own);
        const GearboxTruck boxTruck{ raw.gear, TruckTopGear(raw), isfinite(raw.speed) ? raw.speed : 0.0f, TruckEngineOn(raw), raw.stall,
                                     raw.powerFactor, raw.automatic != 0, foot };
        if (orders.took)
        {
            GearboxPosTake(w.pos, boxTruck);
            w.gearbox.gear = raw.gear >= 1 && raw.gear <= TruckTopGear(raw) ? raw.gear : 1;
            w.gearbox.pause = kShiftPause;
            GearboxHuntClear(w.gearbox);
            w.doors = GearboxDoorsNow();
            if (own && shifterAsks)
                Log(L"gearbox: the mod's gearbox takes the truck in %S (the game's gear %d); the game's own shifting is refused from now on, its shifter "
                    L"picks the mod's position (driven with %s)", GearboxPosName(w.pos.pos), raw.gear, s.input == kInputKeys ? L"the keyboard" : L"a pad");
            else if (own) Log(L"gearbox: the mod's gearbox takes the truck in %S (the game's gear %d); the game's own shifting is refused from now on, "
                              L"the bindings pick the position (driven with a wheel's pedals)", GearboxPosName(w.pos.pos), raw.gear);
            else Log(L"gearbox: the mod's automatic takes the truck in A (the game's gear %d); the game's reverse on the brake is refused", raw.gear);
        }
        if (orders.gaveBack)
        {
            Log(L"gearbox: the mod's gearbox lets the truck go: %S, after %d shifts and %d stalls", TakeoverWhy(w.take.lastHandBack), w.take.shifts, w.pos.stalls);
            // given back from A: the game's automatic back on, so the truck is not left in a plain gear (review 7, R10)
            if (w.pos.running && w.pos.pos == kPosA) m.put(w.refs.action + 0x3C, (uint8_t)1);
            GearboxLetGo(w);
        }
        if (w.take.driving)
        {
            // back in the world after the garage, the map or the viewer (the policy passed meanwhile): the position read
            // again from the game (review 7, R6), unless the game still has the gear and the power the mod set: then the
            // position stays, so L+ does not come back as a 1st nor A as a held gear (GearboxPosKeeps)
            if (!GearboxHoldingNow())
            {
                const bool keeps = GearboxPosKeeps(w.pos, boxTruck);
                if (!keeps) GearboxPosTake(w.pos, boxTruck);
                w.doors = GearboxDoorsNow();
                if (keeps) Log(L"gearbox: back in the world, the mod's gearbox keeps the truck in %S", GearboxPosName(w.pos.pos));
                else Log(L"gearbox: back in the world, the mod's gearbox reads the truck in %S again", GearboxPosName(w.pos.pos));
            }
            // level 1: the game's shifter is the player's. A position picked on it (H, N, R, the low range or a numbered
            // gear) hands the truck back by the door it came through, whatever gear it names, so L+ is told from the
            // automatic's own first gear (a tester's report, 28 October). The mod then sets no gear and no flag until A
            // is picked, when it takes the truck again
            const GearboxDoors doorsNow = GearboxDoorsNow();
            if (!own && (GearboxHandsBack(w.doors, doorsNow, w.pos.pos == kPosA, raw.automatic != 0) || w.pos.pos != kPosA))
            {
                const char *picked = GearboxDoorPos(w.doors, doorsNow, raw.gear, raw.powerFactor);
                if (picked) Log(L"gearbox: the game's shifter picked %S: the mod's automatic hands the truck back, and takes it again when A is picked", picked);
                else Log(L"gearbox: the game's shifter picked gear %d: the mod's automatic hands the truck back, and takes it again when A is picked", raw.gear);
                w.take.driving = false;
                w.take.handBacks++;
                GearboxLetGo(w);
            }
            w.doors = doorsNow;
        }
        if (w.take.driving)
        {
            // the policy for the held truck, each pass: everything refused at level 2; at level 1 only a reverse while
            // the truck stands in A with the brake down and the game's automatic flag up, read by the hook at the call
            // (the flag comes up for a moment after the shift modifier is tapped or A picked again, before this pass
            // puts it down; the first drives, 2026-10-26)
            const bool brakeStand = w.pos.pos == kPosA && fabsf(boxTruck.speed) < kStandStill && TruckBrake(raw) > 0.05f;
            GearboxPolicy(own ? kPolicyAll : kPolicyBrakeReverse, (void *)w.refs.vehicle, brakeStand, (const volatile uint8_t *)(w.refs.action + 0x3C));
        }
        // the mod's shifts on the game's thread (review 17, step 1): the last job's result taken back, one that no update
        // took dropped, and + 0x70's following said; each shift has one line, always written, so the exit hook's line
        // lands right after the last one
        const bool viaJob = s.shiftOnGameThread && TruckUpdateHooked() && g_shiftGear;
        if (g_shiftJob.state == kShiftDealt)
        {
            ShiftJob &j = g_shiftJob;
            if (j.shift)
            {
                Log(L"shift %u: %d to %d %S; dealt at truck update %ld on thread %lu (the truck update's), %llu ms after posting: %s, +74 %d, +70 %d, "
                    L"power %.2f at the shift%S", j.number, j.from, j.gear, j.context, j.frame, j.thread, (unsigned long long)(now - j.postedAt),
                    j.took ? L"took" : L"refused", j.gear74, j.gear70, j.factorAtShift, j.power ? " (set after)" : "");
                ShiftStageResult(w.stage, j.took);
                if (j.took) w.take.shifts++;
                else if (GearboxPosRefused(w.pos, j.gear))
                    Log(L"gearbox: the game did not take gear %d from the mod's gearbox, in gear %d; asked again when the position changes", j.gear, j.from);
            }
            InterlockedExchange(&j.state, kShiftIdle);
        }
        else if (ShiftJobDrop(g_shiftJob, w.refs.vehicle, now, TruckUpdateFrames()))
        {
            if (g_shiftJob.shift)
            {
                Log(L"shift %u: %d to %d dropped: %s", g_shiftJob.number, g_shiftJob.from, g_shiftJob.gear,
                    g_shiftJob.vehicle != w.refs.vehicle ? L"another truck is driven" : L"no truck update took it within a second and 30 updates");
                w.pos.lastSet = INT_MIN; // asked again on the next pass
                ShiftStageResult(w.stage, false);
            }
        }
        // the shift's state (review 17, step 2): + 0x70 as the game's thread saw it follow, else as this pass read it; a
        // stall owed by a shift without the clutch is ordered once the gear is set
        const int engagedNow = g_engage.on && g_engage.at && g_engage.gear == w.stage.gear ? g_engage.gear : raw.gearEngaged;
        const int stageWas = w.stage.stage;
        if (!w.take.driving) w.stage = ShiftStage{}, w.stallWaits = false;
        const bool stallOnSet = ShiftStageStep(w.stage, engagedNow, dt);
        if (stallOnSet) Log(L"gearbox: the shift into gear %d without the clutch is set: the engine stalls now, on the new gear", w.stage.gear);
        if (stageWas != kStageIdle && w.stage.stage == kStageIdle && w.take.driving) Log(L"shift: idle again");
        GearboxTruck boxNow = boxTruck;
        if (w.stage.stage == kStageAsked) boxNow.gear = w.stage.gear; // the gearbox decides on the gear asked, not the read before it
        if (g_engage.on && g_engage.at)
        {
            Log(L"shift: +70 showed gear %d at truck update %ld, %ld after the shift", g_engage.gear, (long)g_engage.at, (long)g_engage.at - g_engage.from);
            InterlockedExchange(&g_engage.on, 0);
        }
        else if (g_engage.on && TruckUpdateFrames() - g_engage.from > 600)
        {
            Log(L"shift: +70 did not show gear %d in 600 truck updates", g_engage.gear);
            InterlockedExchange(&g_engage.on, 0);
        }
        bool jobFlag = false, jobPower = false;
        float jobFactor = 0.0f;
        bool shiftAsk = false, stallOrdered = false;
        int shiftTo = 0;
        if (w.take.driving && !testing)
        {
            const GearboxOptions options{ s.requireClutch, s.requireGearHeld, s.skipNeutral, s.downStaysForward };
            const int autoGear = TakeoverAutoGear(raw, w.gearbox.gear, GearboxLowestFit(w.gearbox, raw, scale, w.hand.on));
            if (!own || w.pos.pos == kPosA) GearboxFollowFloor(w.gearbox, autoGear); // in A the model's gear goes where the floor lifts the game's
            const int posWas = w.pos.pos;
            GearboxActions acts = w.actions;
            // BackToA (review 17, Sequential): at Gearbox=2, back to A once the timer has run out and the conditions
            // have held a second; one log line for each change of what holds it
            if (own)
            {
                BackInputs bi = {};
                bi.pos = w.pos.pos;
                bi.heldGear = w.pos.gear;
                bi.autoGear = autoGear;
                bi.byShifter = GearboxPosHeld(w.pos); // the shifter's gear, or a 1st from the low range until the next shift
                bi.gearHeldRule = s.requireGearHeld;
                bi.pedal = foot;
                bi.grip = w.gearbox.grip;
                bi.spin = w.gearbox.spin;
                bi.speed = boxTruck.speed;
                bi.pitchKnown = w.tilt.running;
                bi.pitchDeg = w.tilt.pitchDeg;
                bi.climbHold = w.gearbox.climb > 0.0f;
                bi.awd = raw.awd != 0;
                bi.diffLock = raw.diffLock != 0;
                bi.handbrake = raw.handbrake != 0;
                bi.engineOn = TruckEngineOn(raw);
                bi.dying = raw.stall > 0.0f;
                bi.shiftBusy = g_shiftJob.state != kShiftIdle || w.stage.stage == kStageAsked || w.stage.stage == kStageSet;
                const float heldFor = w.back.since;
                if (BackToAStep(w.back, s.backToA, w.backHand, bi, dt))
                {
                    acts.a = true;
                    Log(L"sequential: back to A after %.1f s in gear %d, A's gear %d, %.0f rpm", heldFor + dt, w.pos.gear, autoGear, w.tach.rpm);
                }
                else if (w.back.why != w.backSaid && w.back.why > kBackTimer && w.back.why != kBackSteadying)
                {
                    // one line for each new reason: backSaid keeps the last one told, so a condition flickering at its
                    // edge (the pedal at 0.7) is told once (review N1)
                    Log(L"sequential: back to A held by %S (the pedal %.2f, the wheels %.1f of the ground's %.1f rad/s, the nose %.1f deg, A's gear %d in gear %d)",
                        kBackWhyNames[w.back.why], foot, bi.spin, bi.grip * fabsf(bi.speed), bi.pitchDeg, autoGear, w.pos.gear);
                    w.backSaid = w.back.why;
                }
                if (w.back.why <= kBackTimer || w.back.why == kBackGo) w.backSaid = w.back.why;
                // Sequential's own shifts: back to D, which picks the gear (the sequential to D brief)
                SeqInputs si = {};
                si.backToA = s.backToA;
                si.requireClutch = s.requireClutch;
                si.requireGearHeld = s.requireGearHeld;
                si.byShifter = GearboxPosHeld(w.pos);
                si.pos = w.pos.pos;
                si.gear = w.pos.gear;
                si.top = boxTruck.top;
                si.input = w.clutch.input;
                si.redline = scale.redline;
                si.idle = scale.idle;
                si.pedal = foot;
                si.braking = TruckBrake(raw) > 0.0f || raw.handbrake;
                si.gripKnown = bi.grip > 0.0f;
                si.spinning = bi.grip > 0.0f && bi.spin > bi.grip * fabsf(bi.speed) * kBackSlip + kBackSlipAdd;
                si.busy = g_shiftJob.state != kShiftIdle || w.stage.stage != kStageIdle;
                si.engineOn = TruckEngineOn(raw) && raw.stall <= 0.0f;
                const int seq = acts.a ? 0 : SequentialStep(w.seq, si, dt);
                if (seq)
                {
                    acts.a = true;
                    Log(seq > 0 ? L"sequential: up at the red zone, gear %d, back to D (D's gear %d), %.0f rpm on the gearbox's input"
                                : L"sequential: down before a stall, gear %d, back to D (D's gear %d), %.0f rpm on the gearbox's input",
                        w.pos.gear, autoGear, w.clutch.input);
                }
            }
            else w.back = BackModel{}, w.backSaid = kBackOff, w.seq = SeqModel{};
            const int gearWas = w.pos.gear;
            if (shifterAsked)
            {
                GearboxActionsAsk(acts, ask, boxTruck.top);
                static const char *const askName[] = { "", "A", "N", "R", "H", "a gear", "L-", "L" };
                if (ask.kind == kAskGear) Log(L"gearbox: the game's shifter asked for gear %d", ask.gear);
                else Log(L"gearbox: the game's shifter asked for %S", askName[ask.kind]);
                if (ask.kind == kAskGear && ask.gear == 1 && !w.lowPlusSaid)
                {
                    w.lowPlusSaid = true;
                    Log(L"gearbox: the game's L+ cannot be told from its first gear (the same gear, the plain power), so the shifter's L+ reads as the first gear");
                }
            }
            GearboxPosOrders box = GearboxPosStep(w.pos, options, acts, boxNow, autoGear, dt);
            // at Gearbox 0 and 1 the game's shifter is the player's: the mod's automatic writes and shifts only within
            // A, never into N or R and never out of a position of the game's (0.18.2's hotfix, the reviewer's item 4)
            if (!own && !GearboxOwnOrders(w.pos.pos, box.gear, box.setGear))
            {
                box.flagDown = box.setPower = box.setGear = false;
            }
            // with the shifts on the game's thread the flag and the power go in the same job, around the shift
            if (viaJob) jobFlag = box.flagDown, jobPower = box.setPower, jobFactor = box.power;
            else
            {
                if (box.flagDown) m.put(w.refs.action + 0x3C, (uint8_t)0);
                if (box.setPower) m.put(w.refs.action + 0x38, box.power);
            }
            // ramped into the game's stall seconds below (StallStep), so its notice shows; a shift's stall waits for the
            // gear to be set (the shift's state), so it never lands on the old one
            stallOrdered = box.stall && !box.setGear;
            if (box.stall && box.setGear) w.stallWaits = true;
            // the stall the model ordered costs the gearbox (StallDamagePercent, with Damage), dealt on the game's thread;
            // a stall of the game's own does not
            const wchar_t *stallCost = L"";
            if (box.stall && StallDamageShare(s) > 0.0f)
            {
                if (!g_damageUpdate) stallCost = L"; it costs the gearbox nothing, as the game's damage path was not found";
                else stallCost = box.setGear ? L"; its gearbox damage goes to the game's thread once the gear is set (not if the game refuses the shift)"
                                             : L"; its gearbox damage goes to the game's thread";
            }
            if (box.moved && w.pos.pos != posWas) Log(L"gearbox: %S to %S", GearboxPosName(posWas), GearboxPosName(w.pos.pos));
            // up from L+ refused within the hold after the walk: once a visit to L+, so a run of presses shows its refusal
            if (box.lowUpSay)
                Log(L"gearbox: up from L+ refused %.1f s after the walk got there (press again to go to 1st)", box.lowUpAfter);
            if (box.lowToFirst && w.pos.heldFromLow)
                Log(L"gearbox: %S to 1st by hand: held, no back to D until the next shift", GearboxPosName(posWas));
            // a shift by hand into a numbered gear starts BackToA's timer; one on the game's shifter does not
            w.backHand = w.pos.pos == kPosGear && !w.pos.byShifter && (posWas != kPosGear || gearWas != w.pos.gear);
            if (box.why[0] && box.stall) Log(L"gearbox: %S (%d since it took the truck)%s", box.why, w.pos.stalls, stallCost);
            else if (box.why[0]) Log(L"gearbox: %S", box.why);
            shiftAsk = box.setGear;
            shiftTo = box.gear;
        }
        // the game's stall seconds: a stall the gearbox ordered, the clutch model's on a pedal's axis (a dropped clutch,
        // review 11) or the creep's with the truck stuck in a gear picked by hand (CreepStep, at every level of Gearbox),
        // ramped in over kStallRamp, and while the automatic clutch lugs the engine at speed in too high a gear, raised by
        // dt each pass and cleared when it recovers, so the game shows its own "engine stalling" notice and kills the
        // engine by its rule (the user's hill test, 27 October)
        {
            // the gearbox's own stall costs the gearbox once it is ordered (StallDamagePercent, with Damage)
            const bool gearboxStall = stallOrdered || stallOnSet;
            if (gearboxStall && StallDamageShare(s) > 0.0f && g_damageUpdate) w.stallsOwed++;
            stallOrdered = gearboxStall || w.clutchStall || w.creepStall;
            w.clutchStall = w.creepStall = false;
            const float stallTo = StallStep(w.stallWrite, stallOrdered, w.clutch.lugging, TruckEngineOn(raw), dt);
            if (stallTo >= 0.0f) m.put(w.refs.vehicle + 0x11C, stallTo);
            if (w.clutch.lugging != w.lugSaid)
            {
                w.lugSaid = w.clutch.lugging;
                Log(w.lugSaid ? L"clutch: the engine lugs in gear %d at %.0f km/h, the game's stall seconds rise" : L"clutch: the engine is back above idle, the stall seconds cleared",
                    raw.gearEngaged, fabsf(raw.speed) * 3.6f);
            }
        }
        // a stall's gearbox damage: posted to the game's thread one stall at a time, and what came of it told; the
        // stalls owed go when the share has gone to 0 meanwhile (review 10, D5)
        if (StallDamageShare(s) <= 0.0f) w.stallsOwed = 0;
        if (w.stallsOwed && g_damageUpdate && StallJobPost(g_stallJob, w.refs, StallDamageShare(s), now)) w.stallsOwed--;
        // the clutch's burn, the same way once whole points are owed
        if (!w.stallsOwed && raw.gearboxKnown && g_damageUpdate) w.burnPoints += ClutchBurnPost(w.clutchHeat, g_stallJob, w.refs, raw.gearboxCapacity, now);
        {
            GearboxDamageWrite gw;
            bool updated = false, burn = false;
            int sum = 0, sumCapacity = 0;
            if (StallJobResult(g_stallJob, gw, updated, sum, sumCapacity, &burn))
            {
                const wchar_t *what = burn ? L"the clutch's burn" : L"the stall's damage";
                if (gw.written)
                    Log(L"gearbox damage: the game's thread dealt %s on its next frame: the gearbox took %d of %d (%d now)%s; the game's sum of "
                        L"the parts' damage %d of %d",
                        what, gw.points, gw.capacity, gw.wrote, updated ? L", the game's damage update run after it" : L"", sum, sumCapacity);
                else if (gw.capacity > 0 && gw.before >= gw.capacity) Log(L"gearbox damage: %s not dealt: the gearbox at its worst already", what);
                else Log(L"gearbox damage: %s not dealt: %S", what, gw.why);
            }
            if (StallJobDrop(g_stallJob, w.refs.vehicle))
                Log(L"gearbox damage: the last truck's gearbox damage not dealt: another truck is driven before the game's truck update took it");
        }
        // the differential lock and all-wheel drive on the mod's keys, at every level of Gearbox: not shifts, and the
        // game's own keys still work on a truck with the upgrade
        if (w.refs.action && !(w.refs.action >= m.base && w.refs.action < m.base + g_exeSize))
        {
            int lockSaid[kLocks];
            LocksStep(w.locks, m, w.refs.action, w.lockPress, lockSaid);
            static const wchar_t *const lockName[kLocks] = { L"the differential lock", L"all-wheel drive" };
            for (int i = 0; i < kLocks; i++)
                if (lockSaid[i] == kLockOn || lockSaid[i] == kLockOff) Log(L"locks: %s %s by the mod's key", lockName[i], lockSaid[i] == kLockOn ? L"on" : L"off");
                else if (lockSaid[i] == kLockCleared) Log(L"locks: the game cleared %s on this truck; it may not have the upgrade", lockName[i]);
        }
        // the road load's ground (the game pull and road load brief): the truck's acceleration over the ground, each
        // gear's pull at the game's own torque learned floored on flat ground, and with the probe the game's pull by x
        {
            const float v = isfinite(raw.speed) ? raw.speed : 0.0f;
            if (w.accelSeen && dt > 0.0f) w.accel += ((v - w.speedWas) / dt - w.accel) * (1.0f - expf(-dt / kAccelSmooth));
            else w.accel = 0.0f;
            w.speedWas = v;
            w.accelSeen = true;
            // the wheels' drag, added back to what the gears' pull learns (BRIEF_air_drag_wheel_damping.md), through the
            // same 0.2 s filter as the acceleration so both describe one moment (the Reviewer's point 4); NAN unread
            int dampGrounded = 0;
            float dampMass = 0.0f;
            WheelDampWhy dampWhy = {};
            const float dampFiles = TruckMass(m, w.refs.model);
            const float dampRaw = TruckWheelDamp(m, w.refs, WheelCylinderVtable(), dampFiles, &dampGrounded, &dampMass, &dampWhy);
            if (!isfinite(dampRaw) && dampWhy.what == kDampOk) dampWhy.what = kDampSpin; // a NAN with no reason given: the sum's
            if (!isfinite(dampRaw)) w.dampSeen = false;
            else if (w.dampSeen && dt > 0.0f) w.damp += (dampRaw - w.damp) * (1.0f - expf(-dt / kAccelSmooth));
            else w.damp = dampRaw, w.dampSeen = true;
            const float damp = w.dampSeen ? w.damp : NAN;
            // read every pass; logged as DampSayStep says (the Reviewer's point 5): the first good read of a truck, a
            // failure and each change of its reason, the first good read after one with the seconds it took, 10 s apart at least
            const float sayNow = (float)(GetTickCount64() % 100000000ULL) / 1000.0f, failedFor = sayNow - w.dampSay.failSince;
            const int say = DampSayStep(w.dampSay, w.refs.vehicle, isfinite(dampRaw) ? WheelDampWhy{ kDampOk, -1, 0.0f, {} } : dampWhy, sayNow);
            if (say == kDampSayGood || say == kDampSayBack)
                Log(L"road load: the wheels' drag read from %d wheels (%d on the ground), the mass taken %.0f kg (the truck's bodies %.0f, the wheels' %.0f): %.3f m/s2 now%s",
                    w.refs.wheels, dampGrounded, dampMass, dampFiles, dampMass - dampFiles, dampRaw,
                    say == kDampSayBack ? (L", read again after " + std::to_wstring((int)lroundf(failedFor)) + L" s not read").c_str() : L"");
            else if (say == kDampSayFail)
                Log(L"road load: the wheels' drag not read (%d of %d wheel models found, the shape's vtable %s; %s): the gears' pull is not learned until they are read",
                    w.refs.wheels, w.refs.models, WheelCylinderVtable() ? L"found" : L"not found", DampWhyText(dampWhy, w.refs.wheels).c_str());
            const int g = raw.gearEngaged;
            const float top = g >= 1 ? TruckGearTop(raw, g) : 0.0f, groundX = top > 0.0f && w.gearbox.grip > 0.0f ? w.gearbox.grip * fmaxf(v, 0.0f) / top : 0.0f;
            const float pedalNow = TruckPedal(raw);
            const bool gripping = w.gearbox.grip > 0.0f && TruckDrivenSpin(raw) <= w.gearbox.grip * fmaxf(v, 0.0f) * kSlipShare + kSlipSpare;
            // the grade by the truck's path, not the chassis's pitch, which squats under power (the Reviewer's point 3)
            const float gradeSin = GradeStep(w.grade, raw.position, (float)(GetTickCount64() % 100000000ULL) / 1000.0f);
            w.gradeSinLast = gradeSin; // for the next pass's plateau (BRIEF_gear_tops_per_truck.md, the Reviewer's point 3)
            // the seconds since the last shift's end: the mod's stage idle again (at Gearbox=2 the clutch slips through the
            // stages) and the engaged gear unchanged (the game's own shifts)
            if (g != w.baseGearWas || w.stage.stage != kStageIdle) w.baseSettled = 0.0f;
            else w.baseSettled += dt;
            w.baseGearWas = g;
            const bool clean = g >= 1 && g < kMaxGears && TruckEngineOn(raw) && gripping && TruckBrake(raw) <= 0.0f && !raw.handbrake && v > 1.0f && !testing;
            // each gear's pull is the truck's, by its engine: read again when either changes, learned only with both known;
            // the sums of every gear start over then
            const char *baseTruck = w.who.taken.key, *baseEngine = w.engine.found && w.engineVehicle == w.refs.vehicle ? w.engine.name : "";
            bool fold = false;
            if (strcmp(baseTruck, w.baseTruck) || strcmp(baseEngine, w.baseEngine))
            {
                strcpy_s(w.baseTruck, baseTruck);
                strcpy_s(w.baseEngine, baseEngine);
                w.baseLearn = BaseLearn{};
                memset(w.baseSaidAt, 0, sizeof w.baseSaidAt);
                int dropped = 0;
                const int bases = GearBaseRead(baseTruck, baseEngine, w.gearBase, &dropped);
                if (bases) Log(L"road load: %d of this truck's gears' pull with %S learned before (its file in Gauges\\trucks)", bases, baseEngine);
                if (dropped)
                    Log(L"road load: %d of this truck's gears' pull with %S were learned with the wheels' drag in them: dropped, to be learned again (BaseDamp.%S=1 now marks the new ones)",
                        dropped, baseEngine, baseEngine);
                fold = true;
            }
            float learned = 0.0f, added = 0.0f, dampMean = 0.0f;
            const int done = baseTruck[0] && baseEngine[0]
                                 ? BaseLearnStep(w.baseLearn, g, clean && BaseLearnPlainPower(g, raw.powerFactor), w.accel, damp, raw.torqueFactor, pedalNow, groundX, gradeSin,
                                                 w.baseSettled, dt, learned, &added, &dampMean)
                                 : 0;
            if (probe && added > 0.0f && !done && g >= 1 && g < kMaxGears && GetTickCount64() - w.baseSaidAt[g] >= 10000)
            {
                w.baseSaidAt[g] = GetTickCount64();
                Log(L"road load: gear %d learning, %.1f of %.1f s gathered", g, w.baseLearn.time[g], kBaseLearnFor);
            }
            if (done)
            {
                const float was = w.gearBase[done];
                w.gearBase[done] = was > 0.0f ? was + (learned - was) * 0.3f : learned;
                const bool moved = !(was > 0.0f) || fabsf(w.gearBase[done] - was) > 0.05f * was;
                if (moved) GearBaseWrite(baseTruck, baseEngine, done, w.gearBase[done]);
                // every learned gear (the Reviewer's point 6), so a drive shows the wheels' drag beside each figure
                Log(L"road load: gear %d's pull at the game's own torque with %S, floored: learned %.3f m/s2 with the wheels' drag %.3f m/s2 added back, kept %.3f (from %.3f%s)",
                    done, baseEngine, learned, dampMean, w.gearBase[done], was, moved ? L"; the truck's file in Gauges\\trucks" : L"; within 5 %, not written");
                fold = true;
            }
            // one figure per truck: a gear with none learned goes by the learned gears' a_base × top over its own top
            if (fold)
            {
                // H too, the gear over the top numbered one, where the game gives it a top: on a highway the drag is there (review N1)
                const int gears = TruckTopGear(raw) + 1 < kMaxGears && TruckGearTop(raw, TruckTopGear(raw) + 1) > 0.0f ? TruckTopGear(raw) + 1 : TruckTopGear(raw);
                float tops[kMaxGears] = {};
                for (int i = 1; i <= gears && i < kMaxGears; i++) tops[i] = TruckGearTop(raw, i);
                float k = 0.0f, spread = 0.0f;
                bool folded = false;
                const int learnedGears = BaseFold(w.gearBase, tops, gears, w.baseUse, k, spread, folded);
                if (learnedGears >= 2)
                {
                    char row[400] = "";
                    for (int i = 1; i <= gears && i < kMaxGears; i++)
                        if (w.gearBase[i] > 0.0f && tops[i] > 0.0f) snprintf(row + strlen(row), sizeof row - strlen(row), " %d: %.2f,", i, w.gearBase[i] * tops[i]);
                    Log(L"road load: the learned gears' pull x top:%S spread %.0f %%: %s", row, spread * 100.0f,
                        folded ? L"the gears not learned go by their mean over their own top" : L"over 15 %, so each gear goes by its own only");
                }
            }
            // part 1's probe: the game's own pull (no hand) floored in a numbered gear; one line once the gear has gone
            // past kPullDoneAt, or when it changes with enough bins filled
            if (probe)
            {
                // the nose within kPullPitch, as before: the probe's shape by x takes no grade out
                const bool measuring = clean && w.tilt.running && fabsf(w.tilt.pitchDeg) <= kPullPitch && !w.hand.on && pedalNow >= kBaseLearnPedal;
                if (g != w.pull.gear || (measuring && groundX >= kPullDoneAt))
                {
                    float most = 0.0f, mostX = 0.0f, fall = 0.0f;
                    const int bins = PullBinsFit(w.pull, most, mostX, fall);
                    if (bins >= 8 && w.pull.gear >= 1 && w.pull.gear < kMaxGears)
                    {
                        w.pullPeak[w.pull.gear] = fmaxf(w.pullPeak[w.pull.gear], most);
                        char row[400] = "", peaks[400] = "";
                        for (int i = 0; i < kPullBins; i++)
                            if (w.pull.time[i] > 0.0f)
                                snprintf(row + strlen(row), sizeof row - strlen(row), " %.2f %.2f,", kPullBinFrom + i * kPullBinWidth, w.pull.sum[i] / w.pull.time[i]);
                        for (int i = 1; i < kMaxGears; i++)
                            if (w.pullPeak[i] > 0.0f) snprintf(peaks + strlen(peaks), sizeof peaks - strlen(peaks), " %d: %.2f", i, w.pullPeak[i]);
                        Log(L"pull: gear %d, MaxDeltaAngVel %.3f, acceleration by x:%S (the most %.2f m/s2 at %.2f); the fall past it fitted %.2f (kGamePullFall %.1f); "
                            L"the peaks so far by gear:%S", w.pull.gear, w.engine.deltaAngVel, row, most, mostX, fall, kGamePullFall, peaks);
                    }
                    w.pull = PullBins{};
                    w.pull.gear = g;
                }
                if (measuring) PullBinsAdd(w.pull, groundX, w.accel, dt);
            }
        }
        // the engine's torque handed to the game (Powertrain): the torque factor for the engine's torque at the needle's
        // speed and the converter's ratio over the game's curve at the wheels' share of the gear's top, while the engine
        // runs and no pull test is armed or running; the game's own share given back once it stops
        {
            const bool pullBusy = w.pullTest.state == kPullArmed || w.pullTest.state == kPullRunning || pull.ended;
            const bool want = s.powertrain && TruckEngineOn(raw) && !pullBusy;
            // the wheels' share of the gear's top by the truck's speed over the ground, once the wheels' grip is known
            // (LiftShare: before the grip is known, no more than kLiftBlind); in a gear held by hand with the clutch fully
            // in, the engine locked to the wheels, by the wheels' own speed, the factor held to kSpinLiftMost (SpinShare)
            const float ground = isfinite(raw.speed) && raw.speed > 0.0f ? raw.speed : 0.0f;
            const bool locked = GearboxManual(w) && w.clutch.connected && !w.clutch.dead && w.clutch.grip >= kGripUp && w.pos.travel <= 0.0f;
            const float gearTop = TruckGearTop(raw, raw.gearEngaged);
            const float x = locked ? SpinShare(TruckDrivenSpin(raw), gearTop) : LiftShare(TruckDrivenSpin(raw), ground, w.gearbox.grip, gearTop);
            w.liftX = x;
            w.liftRoof = locked ? SpinRoof(TorqueRoof(autoClass), x) : LiftRoof(TorqueRoof(autoClass), x);
            const float ratio = w.take.driving && !GearboxManual(w) ? w.gearbox.ratio : 1.0f;
            // the road load (RoadLoad): the air's drag over this gear's learned pull at this pedal, taken off before the
            // fall's make-up; nothing standing, in reverse, with the foot off, or before the gear's pull is learned
            const int roadGear = raw.gearEngaged;
            const float roadPedal = TruckPedal(raw);
            w.roadLoadA = s.roadLoad && w.mass > 0.0f ? RoadLoadAccel(RoadCdA(w.mass, s.roadCdA), ground, w.mass) : 0.0f;
            w.roadShare = s.roadLoad && roadGear >= 1 && roadGear < kMaxGears && ground >= kStandStill
                              ? RoadLoadShare(w.roadLoadA, roadPedal, w.baseUse[roadGear]) : 0.0f;
            if (!want || !(w.roadShare > 0.0f)) w.roadShare = w.roadLoadA = 0.0f; // none taken off
            const bool handWas = w.hand.on;
            const float gameWas = w.hand.game;
            // held back to the engine's Torque share where one is set; the lift's roof is the truck's class's, fading over
            // the gear's last three tenths (TorqueRoof, LiftRoof); a rise paced by the pass, a new gear (the game's
            // engaged gear) at once
            TorqueGive(m, w.refs, true, TorqueHandStep(w.hand, want, w.refs.action, w.refs.vehicle, raw.torqueFactor,
                                                 TorqueHeld(TorqueBlend(fminf(TorqueLift(w.power.curve, w.power.cold * w.power.boost, TruckPedal(raw), ratio, x,
                                                                                         TorqueRoof(autoClass), w.roadShare),
                                                                              locked ? kSpinLiftMost : kTorqueFactorMost),
                                                                        s.engineShare / 100.0f),
                                                            w.profile.torque),
                                                 dt, raw.gearEngaged));
            if (w.hand.on != handWas)
                Log(w.hand.on ? L"powertrain: the engine's torque handed to the game, the game's own torque factor %.3f" : L"powertrain: the game's own torque factor %.3f given back",
                    w.hand.on ? w.hand.game : gameWas);
        }
        // the creep (review 11's addendum): an idling truck in gear moves, its drive throttle raised for this truck alone
        // by the hook on the game's wheels' drive. Through the converter while the mod's automatic drives in A or the
        // game's automatic is up; the idle governor in a position of the mod's own gearbox, by its clutch's grip, and in
        // a manual gear of the game's, where the mod has no clutch; stuck there it stalls the engine, handed to the stall
        // seconds on the next pass. Nothing while the pull test runs. Its first line in the log a quarter second after it
        // begins and one as it ends; with the probe on, one each quarter second
        {
            // R creeps through the converter at every level, the mod's own gearbox's R too (the author, 30 October: "R creep
            // is supposed to be torque converter. 0.18.0 worked, 18.1 didn't"): there is no governor, holding part, stall
            // or clutch for the creep in R
            const bool manual = own && GearboxManual(w) && raw.gearEngaged >= 0;
            // R and the low range picked on the game's shifter (Gearbox 0 and 1) drive through the converter, as a real
            // automatic's do (the author, 29 October: "i assumed it was handled through converter already? do it"), its
            // share calmed there as the governor's (CreepShare's calm). The game's L+ is its first gear at full power and
            // cannot be told from it, so it keeps the governor
            const bool shifterLow = !manual && (raw.gearEngaged < 0 || (raw.gearEngaged == 1 && raw.powerFactor < kLowUnder));
            const bool converter = !manual && (w.take.driving || raw.automatic != 0 || shifterLow);
            const float grip = manual ? (w.clutch.dead ? 0.0f : w.clutch.grip) : 1.0f;
            CreepCase creepCase = {};
            bool stalls = false;
            // only where an automatic picks the gear, for now: the governor's path, the holding part and the stall stay in
            // place, unreached (the author, 1 November: "manual should have no converter. but thats for another time")
            // with CreepManual a gear picked by hand creeps too (CreepGoes), as before 53f526d
            const bool autoPicks = CreepGoes(s.creepManual, w.take.driving, w.pos.running, w.pos.pos, raw.automatic);
            const bool on = s.creep && CreepHooked() && !testing && autoPicks;
            if (!on) w.creep = CreepModel{};
            // the holding part's most: the engine's full fuel at idle, a throttle of 1 with the engine handed to the game
            // (the factor makes it the curve's torque there), else the torque at idle of the curve it pulls by (review 13)
            const float holdMost = w.hand.on ? 1.0f : TorqueAt(curve, scale.redline > 0.0f ? scale.idle / scale.redline : 0.0f, tachTop);
            // the stalls only as a manual gearbox's: the mod's own gearbox in a position picked by hand (the author's drive
            // of BF57872B); on the automatic and the game's shifter the governor pushes and the engine idles on
            const bool mayStall = s.gearbox == kGearboxOwn && manual && autoPicks; // a hand gear creeps only with CreepManual
            // a pass that shifts gives nothing and leaves the model as it was: the gear it reads is the one before the
            // shift, and the next pass sees the change and keeps kCreepQuiet (round 3 of the crash hotfix)
            // and nothing from a shift asked until kCreepQuiet after it engaged (the shift's state): the model keeps the
            // gear and the range of these passes, so it does not start a second quiet once the shift is idle
            const bool shiftQuiet = shiftAsk || w.stage.stage != kStageIdle;
            if (on && w.stage.stage != kStageIdle)
            {
                w.creep.share = w.creep.hold = w.creep.given = 0.0f;
                w.creep.armed = false;
                w.creep.seen = w.creep.shifted = true;
                w.creep.gearSeen = raw.gearEngaged;
                w.creep.powerSeen = raw.powerFactor;
                w.creep.sinceShift = kCreepQuiet;
            }
            const float share = on && !shiftQuiet ? CreepStep(w.creep, raw, scale, converter, grip, dt, &creepCase, stalls, holdMost, mayStall,
                                                 shifterLow && !w.take.driving && raw.automatic == 0) : 0.0f;
            CreepGive(share > 0.0f ? w.refs.action : 0, share);
            if (stalls)
            {
                w.creepStall = true;
                Log(w.creep.stallArmed ? L"creep: the engine stalled, lugged under idle in gear %d (the wheels %.2f of the idle speed's %.2f rad/s, the game's pull %.3f)"
                                       : L"creep: the engine stalled, stuck in gear %d (the wheels %.2f of the idle speed's %.2f rad/s, the game's pull %.3f)",
                    raw.gearEngaged, TruckDrivenSpin(raw), creepCase.idleSpin, raw.actionB4);
            }
            const bool creeping = share > 0.0f;
            long hits = 0, calls = 0;
            CreepCounts(hits, calls);
            if (creeping && !w.creeping)
            {
                w.creepFor = 0.25f;
                w.creepLines = 0;
                w.creepHits = hits;
                w.creepCalls = calls;
            }
            w.creepFor -= dt;
            const bool line = creeping ? w.creepFor <= 0.0f && (probe || w.creepLines == 0) : w.creeping;
            if (line)
            {
                char text[512];
                CreepProbeText(raw, share, creepCase, hits - w.creepHits, calls - w.creepCalls, text, sizeof text, &w.creep);
                Log(L"%S%s", text, creeping ? L"" : L" (it ends)");
                w.creepFor = 0.25f;
                w.creepLines++;
                w.creepHits = hits;
                w.creepCalls = calls;
            }
            w.creeping = creeping;
        }
        if (shiftAsk)
        {
            // nothing of the creep's through the shift: its slot cleared first, and CreepStep gives nothing for
            // kCreepQuiet once the game's gear moves (0.18.2's hotfix)
            CreepGive(0, 0.0f);
            const float creepWas = w.creep.share;
            w.creep.share = w.creep.hold = w.creep.given = 0.0f;
            w.creep.armed = false;
            static unsigned shiftNumber = 0;
            char context[192];
            snprintf(context, sizeof context, "by %s (%s, clutch %s, %.1f m/s, creep %.2f, stall %.2f)", w.pos.byShifter ? "the game's shifter" : "the bindings",
                     GearboxPosName(w.pos.pos), w.pos.clutch ? "down" : "up", boxTruck.speed, creepWas, raw.stall);
            if (viaJob)
            {
                // posted for the truck update; one still out holds this pass's shift back, and the next pass asks again
                if (g_shiftJob.state == kShiftIdle)
                {
                    g_shiftJob.number = ++shiftNumber;
                    strcpy_s(g_shiftJob.context, context);
                    ShiftJobPost(g_shiftJob, w.refs.vehicle, w.refs.action, true, raw.gear, shiftTo, jobFlag, jobPower, jobFactor, TruckUpdateFrames(), now);
                    ShiftStageAsk(w.stage, shiftTo, w.stallWaits);
                    w.stallWaits = false;
                }
                else w.pos.lastSet = INT_MIN;
                jobFlag = jobPower = false;
            }
            else if (GameShift(w.refs.vehicle, shiftTo))
            {
                ShiftStageAsk(w.stage, shiftTo, w.stallWaits);
                ShiftStageResult(w.stage, true);
                w.stallWaits = false;
                int g74 = INT_MIN, g70 = INT_MIN;
                PeekGame(w.refs.action + 0x74, &g74, 4);
                PeekGame(w.refs.action + 0x70, &g70, 4);
                Log(L"shift %u: %d to %d %S; on the mod's thread %lu (ShiftOnGameThread=0 or no truck update hook): took, +74 %d, +70 %d", ++shiftNumber, raw.gear, shiftTo,
                    context, GetCurrentThreadId(), g74, g70);
                w.take.shifts++;
                if (probe && shiftTo >= 1 && raw.gear >= 1)
                {
                    // where the wheels are against each gear's top, by the ground, and what the game gives there; in a position
                    // of the gearbox's own the automatic's model stands down, so the wheels and the clutch model's engine
                    // give the figures (review 11: they were zeros)
                    const bool ownPos = GearboxManual(w);
                    const float by = ownPos ? TruckDrivenSpin(raw) : w.gearbox.by, shiftInput = ownPos ? w.clutch.input : w.gearbox.input,
                                shiftRpm = ownPos ? w.clutch.rpm : w.gearbox.rpm;
                    const float from = GearboxInput(raw, scale, raw.gear, by) / scale.redline, into = GearboxInput(raw, scale, shiftTo, by) / scale.redline;
                    Log(L"gearbox: %d to %d at %.0f rpm on the gearbox's input, the engine at %.0f rpm, the pedal at %.2f; the wheels at %.2f of the gear's top "
                        L"(the game's pull %.2f) and %.2f of the new one's (%.2f)",
                        raw.gear, shiftTo, shiftInput, shiftRpm, foot, from, GamePull(from), into, GamePull(into));
                }
            }
            else
            {
                Log(L"shift %u: %d to %d %S; on the mod's thread %lu: refused", ++shiftNumber, raw.gear, shiftTo, context, GetCurrentThreadId());
                w.stallWaits = false;
                if (GearboxPosRefused(w.pos, shiftTo))
                    Log(L"gearbox: the game did not take gear %d from the mod's gearbox, in gear %d; asked again when the position changes", shiftTo, raw.gear);
            }
        }
        // the flag or the power on their own (L to L+, the flag up for a moment): a job without a shift; one still out
        // leaves them to the next pass, which reads the fields again
        if (viaJob && (jobFlag || jobPower))
            ShiftJobPost(g_shiftJob, w.refs.vehicle, w.refs.action, false, raw.gear, raw.gear, jobFlag, jobPower, jobFactor, TruckUpdateFrames(), now);
        if (w.oil.on != oilWas)
            Log(w.oil.on ? L"oil: %.1f C, too hot from %g C: the lamp blinks and the warning sounds" : L"oil: %.1f C, the warning is over", w.temp.oil, s.oilWarnC);
        // the engine's record, for each truck as it is first driven; one that was not found is looked for again every
        // five seconds
        if (w.refs.vehicle != w.engineVehicle || (!w.engine.found && now >= w.engineAt)) EngineLook(w, m, now, s.probe != 0);
        // the gearbox: for each truck as it is first driven, and again when its gears change (another fitted in the garage)
        if (w.refs.vehicle != w.gearboxVehicle || raw.gearSpeeds != w.gearSpeeds || memcmp(raw.gearSpeed, w.gearSpeed, sizeof w.gearSpeed))
            GearboxLook(w, m, raw, AutoClass(w, s.tachClass));
        // what the temperature costs the engine: the whole points wait in owed until they are written. Nothing comes
        // together while the game's window is not in front, as the game may stand still then with its last values.
        const int points = DamageStep(w.damage, w.refs.vehicle, raw, w.temp, TachSpeed(w.tach, scale), s, front ? dt : 0.0f);
        if (points > 0)
        {
            w.owed = w.owed + points < 1000000 ? w.owed + points : 1000000; // more than any engine takes
            w.cause = w.damage.hot > 0.0f ? 1 : 2;
        }
        if (!s.damage) w.owed = 0;
        if (asked > 0) w.asked = w.asked + asked < 1000000 ? w.asked + asked : 1000000;
        // the write into the game: only once the truck has been driven for half a second without a break, with the
        // game in front and none of its menus up (and the two menu flags read, so that a 0 in them means that), and at
        // most once a second. The engine's record is looked up again for each write: an engine can change under a
        // truck that stays.
        if (w.owed + w.asked > 0 && front && world.menusKnown && world.menu == 0 && !GameMenuUp(world) && now - w.drivingSince >= 500 &&
            now >= w.writeAt)
        {
            DamageWrite d;
            const bool test = w.asked > 0;
            EngineLook(w, m, now, false);
            if (TruckDamageAdd(m, w.refs, w.owed + w.asked, w.engine, d))
            {
                char cause[128], effect[256], after[64] = ""; // the effect and the torque factor are the game's: room for numbers of any size
                if (test) strcpy_s(cause, "the tab's test");
                else
                    sprintf_s(cause, w.cause == 2 ? "a cold engine worked hard at %.1f C (%.1f percent a minute)" : "the heat at %.1f C (%.1f percent a minute)", w.temp.temp,
                              w.damage.hot + w.damage.cold);
                if (d.effectAfter != d.effectBefore || d.torqueAfter != d.torqueBefore)
                    sprintf_s(effect, "effect %.3f to %.3f, torque x%.3f to x%.3f", d.effectBefore, d.effectAfter, d.torqueBefore, d.torqueAfter);
                else sprintf_s(effect, "effect %.3f, torque x%.3f", d.effectAfter, d.torqueAfter);
                if (d.after != d.wrote) sprintf_s(after, "; the game held %d right after", d.after);
                Log(L"engine damage: %d to %d of %d, from %S; %S%S", d.before, d.wrote, d.capacity, cause, effect, after);
                w.addedAt = now;
                w.refused = "";
            }
            else if (test || !w.refused || strcmp(w.refused, d.why))
            {
                Log(L"engine damage: %d of %d, not written: %S", d.before, d.capacity, d.why);
                w.refused = d.why;
            }
            w.owed = w.asked = 0;
            w.writeAt = now + 1000;
        }
        w.modelAt = now;
    }
    if (probe && (!w.probedAny || now >= w.probeAt || TruckProbeEvent(raw, world, w.probed, w.probedWorld) || w.tilt.lamp != w.probedLamp ||
                  TempZone(w.temp) != w.probedZone || DamageAdding(w.damage, s) != w.probedAdding ||
                  w.boost.flutters + w.boost.blowOffs != w.probedLifts))
    {
        char first[640], second[1024], third[512], fourth[256], fifth[640], sixth[1024], seventh[400], eighth[900], ninth[1024], tenth[400], eleventh[320], twelfth[256];
        // while the mod's gearbox has the truck the letter is the model's, as the cluster's (the flag is down, so the
        // game's own rule would read the first gear's factor as L+)
        TruckProbeText(raw, world, w.byWay, first, sizeof first, second, sizeof second, w.take.driving && w.pos.running ? GearboxPosLetter(w.pos.pos) : nullptr);
        ModelProbeText(raw, w.tach, w.range, scale, third, sizeof third);
        Log(L"probe: %S", first);
        Log(L"probe: %S", second);
        TiltProbeText(raw, w.tilt, fourth, sizeof fourth);
        TempProbeText(raw, w.temp, s, fifth, sizeof fifth);
        // the record is the last driven truck's: a truck that is read and not driven yet has none
        DamageProbeText(raw, w.engineVehicle == w.refs.vehicle ? w.engine : EngineRecord{}, w.damage, s, sixth, sizeof sixth);
        Log(L"probe: %S", third);
        Log(L"probe: %S", fourth);
        Log(L"probe: %S", fifth);
        BoostProbeText(raw, w.boost, w.turbo, seventh, sizeof seventh);
        Log(L"probe: %S", sixth);
        Log(L"probe: %S", seventh);
        PowertrainProbeText(raw, w.power, w.tach.rpm, w.hand.on, w.hand.written, w.hand.game, eighth, sizeof eighth, w.hand.on ? w.liftX : -1.0f, w.liftRoof,
                            w.hand.heldBack);
        if (s.roadLoad)
            snprintf(eighth + strlen(eighth), sizeof eighth - strlen(eighth), " | road load %.2f of the pull (%.3f m/s2, CdA %.1f, gear %d's pull %.3f)", w.roadShare,
                      w.roadLoadA, RoadCdA(w.mass, s.roadCdA), raw.gearEngaged, raw.gearEngaged >= 1 && raw.gearEngaged < kMaxGears ? w.gearBase[raw.gearEngaged] : 0.0f);
        Log(L"probe: %S", eighth);
        const bool manual = GearboxManual(w);
        if (manual)
        {
            // the model as it reads along in the player's position: its grip and the gear A would ask
            char along[sizeof ninth];
            GearboxProbeText(raw, w.gearbox, false, along, sizeof along);
            snprintf(ninth, sizeof ninth, "in a position of the player's own, the model reads along: %s", along);
        }
        else GearboxProbeText(raw, w.gearbox, w.take.driving, ninth, sizeof ninth);
        Log(L"probe: %S", ninth);
        if (w.take.driving)
        {
            GearboxPosProbeText(w.pos, GearboxOptions{ s.requireClutch, s.requireGearHeld, s.skipNeutral, s.downStaysForward }, raw.gear, tenth, sizeof tenth);
            Log(L"probe: %S", tenth);
        }
        if (manual)
        {
            ClutchProbeText(w.clutch, w.pos.travel, eleventh, sizeof eleventh);
            char heat[96];
            ClutchHeatProbeText(w.clutchHeat, heat, sizeof heat);
            Log(L"probe: %S%S", eleventh, heat);
        }
        AirProbeText(raw, w.air, twelfth, sizeof twelfth);
        Log(L"probe: %S", twelfth);
        w.probedLifts = w.boost.flutters + w.boost.blowOffs;
        w.probedAdding = DamageAdding(w.damage, s);
        w.probedLamp = w.tilt.lamp;
        w.probedZone = TempZone(w.temp);
        w.probed = raw;
        w.probedWorld = world;
        w.probedAny = true;
        w.probeAt = now + 2000;
    }
    // the game's gearbox functions as the hooks count them, every second while a truck is driven (phase 1: which of
    // them the game calls when, as when a truck in A reverses on the brake). The counts nest: reverse and neutral go
    // through auto off, and auto off through ShiftGear, so one reverse counts in all three (review 6, G2)
    if (probe && driving && GearboxHooked() && now >= w.gearboxProbeAt)
    {
        GearboxCounts c;
        GearboxCountsGet(c);
        Log(L"probe: gearbox code: ShiftGear %ld (last gear %d) | auto %ld | high %ld | reverse %ld | neutral %ld | auto off %ld (last %d) | power %ld (last %.3f) | "
            L"vehicle %ld (%s) | counts nest: reverse and neutral go through auto off, auto off through ShiftGear | refused%s: ShiftGear %ld, auto %ld, "
            L"high %ld, reverse %ld, neutral %ld, auto off %ld, power %ld",
            c.shift, c.shiftGear, c.autoGear, c.high, c.reverse, c.neutral, c.autoOff, c.autoOffGear, c.power, c.factor, c.vehicle, c.vehicleSet ? L"set" : L"cleared",
            GearboxHoldingNow() ? L" (the mod's gearbox has the truck)" : L"", c.refused[kFnShiftGear], c.refused[kFnShiftToAuto], c.refused[kFnShiftToHigh],
            c.refused[kFnShiftToReverse], c.refused[kFnShiftToNeutral], c.refused[kFnAutoOffAndShift], c.refused[kFnSetPower]);
        w.gearboxProbeAt = now + 1000;
    }
    // on screen once the truck has been driven for half a second: a state that only flickers by does not show it
    return driving && now - w.drivingSince >= 500;
}

// The tab's buttons: the turbo's sound of one strength, as loud as the settings in force have it at the boost that
// strength stands for, and the oil warning.
void SoundHear(int tier)
{
    GaugeSettings s;
    SettingsGet(s);
    if (tier == kSoundOil)
    {
        SoundPlay(kSoundOil, OilBeepGain(s.soundVolume), 1.0f);
        return;
    }
    static const float loud[kTurboTiers] = { 1.0f, 0.55f, 0.25f };
    LiftSound sound;
    if (tier >= 0 && tier < kTurboTiers && LiftSoundOf(kLiftBlowOff, loud[tier], s.soundVolume, 0.0f, sound)) SoundPlay(tier, sound.gain, sound.pitch);
}

// ---- the mod's thread ----

// The exe's link time stamp, which tells one build of the game from another.
static DWORD ExeStamp()
{
    const BYTE *image = (const BYTE *)g_base;
    return *(const DWORD *)(image + *(const DWORD *)(image + 0x3C) + 8);
}

static DWORD WINAPI Run(void *)
{
    if (g_second)
    {
        // a second file of this mod: DllMain found the first one's mutex
        wchar_t file[MAX_PATH] = {};
        GetModuleFileNameW(g_self, file, MAX_PATH);
        Sleep(1000); // after the first copy's first line, which starts the log anew
        g_logStarted = true;
        Log(L"this mod is in the folder twice: only one copy runs, and this one (%s) stands down. Keep one file of it, Gauges.asi.", file);
        return 0;
    }
    const DWORD stamp = ExeStamp();
    Log(L"Next Gen Gameplay " GAUGES_VERSION L" (Gauges): exe stamp %08lx, loaded %s", stamp,
        g_addon ? L"by ReShade as Gauges.addon64, the name from before 0.16 (the mod needs no ReShade now: keep it as Gauges.asi)" : L"by an ASI loader");
    LogKeptSay();
    LogGameFolder();
    SettingsDefaults(g_set); // what the tab shows until the ini is read
    g_overlayOk = OverlayInit(g_self);
    g_exeSize = ImageSize(); // where the exe ends, which the engine damage write keeps out of
    // the game's statics: known on the build the mod was made on, found by the code that uses them on any other
    uint64_t clockPlace = 0;
    if (stamp != kGaugesExeStamp) Log(L"places: another build of the game than the one the mod was made on (exe stamp %08lx): looking for what it reads", kGaugesExeStamp);
    // a look that finds them not, as when the mod is loaded before the game has its code ready (another mod's early
    // DirectX probe loads ReShade and this add-on with it: the Spotter minimap mod, 2026-10-21, where one look found
    // nothing and the next start without it found all), is made again every kPlacesRetryMs, kPlacesLooks in all
    bool placesOk = PlacesFind(stamp, clockPlace);
    for (int look = 2; !placesOk && look <= kPlacesLooks; look++)
    {
        Sleep(kPlacesRetryMs);
        if (PlacesFind(stamp, clockPlace, true))
        {
            Log(L"places: all found on look %d, %.0f s after the first (the game's code was not ready yet):", look, (look - 1) * kPlacesRetryMs / 1000.0);
            placesOk = PlacesFind(stamp, clockPlace);
        }
    }
    if (!placesOk)
    {
        Log(L"places: the mod cannot find the driven truck in this build of the game, %d looks over %.0f s: standing down, nothing in the game is changed and "
            L"no cluster is drawn (the Gameplay tab says the mod is not running). "
            L"Gauges.log above says what is missing", kPlacesLooks, (kPlacesLooks - 1) * kPlacesRetryMs / 1000.0);
        InterlockedExchange(&g_modOff, 1);
        return 0;
    }
    // the settings: what [Gauges] holds, and with it what is in force (s), which has the driven truck's own scales
    // in the place of the plain ones where the truck has some. truck is the truck they are for.
    SettingsState set = {};
    EngineState eng = {}; // the engine's settings per truck (BRIEF_engine_per_truck.md)
    ReadIni(set.plain);
    SettingsFor(set, "");
    const GaugeSettings &s = set.s;
    SettingsPublish(s);
    // the gearbox's bindings: the defaults with the ini's over them; the first time, without one in the ini yet, the
    // manual transmission mod's SMT.ini taken over, and the result written so it happens once
    GearBindings binds;
    BindingsDefault(binds);
    {
        const std::wstring ini = IniPath(), smtIni = g_dir + L"SMT.ini";
        if (!BindingsInIni(ini) && GetFileAttributesW(smtIni.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            std::wstring taken, left;
            const int bound = SmtIniImport(smtIni, binds, set.plain, taken, left);
            set.s.requireClutch = set.plain.requireClutch;
            set.s.requireGearHeld = set.plain.requireGearHeld;
            set.s.skipNeutral = set.plain.skipNeutral;
            set.s.downStaysForward = set.plain.downStaysForward;
            BindingsWrite(ini, binds);
            SettingsWrite(set);
            SettingsPublish(s);
            Log(L"gearbox: SMT.ini taken over once: %d keyboard bindings (%s), its options RequireClutch %d, RequireGearHeld %d, SkipNeutral %d, DownStaysForward (its immersive mode) %d; left: "
                L"its pad and wheel section (other codes than Windows'), its menu and handbrake keys%s%s",
                bound, taken.empty() ? L"none" : taken.c_str(), s.requireClutch, s.requireGearHeld, s.skipNeutral, s.downStaysForward, left.empty() ? L"" : L", ", left.c_str());
        }
        else BindingsRead(ini, binds, true);
    }
    LogSettings(s);
    // the kept start logs capped by KeepLogs and kLogsMostBytes, once a start
    {
        const LogsCapResult r = LogsCap(g_dir + L"Gauges\\logs\\", s.keepLogs, kLogsMostBytes);
        if (r.removed) Log(L"log: %d older start logs removed from Gauges\\logs, %d kept (KeepLogs %d, at most 500 MB)", r.removed, r.kept, s.keepLogs);
        if (r.failed) Log(L"log: %d older start logs in Gauges\\logs could not be removed and are left as they are", r.failed);
    }
    BindingsLog(binds);
    InputDevicesStart([](const wchar_t *line) { Log(L"%s", line); }, PadButtons);
    if (s.probe) PlacesHarvest(stamp);
    Log(g_overlayOk ? L"overlay: the cluster and the Gameplay tab of the NEXT GEN menu are drawn by the mod's own overlay"
                    : L"overlay: the overlay did not start (the lines above say why), so nothing is drawn");
    SoundStart(g_dir); // the sounds are read or made now; the audio device is only taken while one plays
    // the block other mods read the mod's state from (state.h), written at the end of every pass
    StateWriter state = {};
    {
        wchar_t name[64];
        StateName(name, 64);
        if (StateOpen(state)) Log(L"state published for other mods in %s", name);
        else Log(L"state: Windows gave no shared memory for %s (error %lu), so nothing is published for other mods", name, GetLastError());
    }
    bool visible = true, keyDown = false;
    uint32_t padMask = PadParse(s.padToggle), padPrev = 0;
    ULONGLONG saveAt = 0;
    TruckIdentity truck = {};
    TruckWatch watch = {};
    InputReader bindReader = {};   // the bindings' states from pass to pass
    InputCapture bindCapture = {}; // a capture the tab began
    GameMenuGate gameMenu = {};    // the game's function menu, from the last pass's World: the bindings wait in it
    const TruckMemory memory = { PeekGame, g_base, PokeGame };
    // the map the truck drives on, the game's id for it from its save, and the outside temperature of its section,
    // its region's or its own, where the ini has one (mapOwn, from the section mapFrom). Without a save map stays ""
    // and AmbientC of [Gauges] is in force everywhere.
    char map[64] = "", mapRead[64] = "", mapFrom[64] = "";
    MapAir mapAir = {};
    // the game's clock: where it is (clockOwn), from Gauges.ini or found by the search (Probe=1), and the hour read on
    // this pass, -1 while it is not known
    ClockPath clockPath = {};
    bool clockOwn = ClockLoad(stamp, clockPath), clockSaid = false;
    const bool clockFromIni = clockOwn;
    if (!clockOwn && clockPlace)
    {
        // the place found in the running game on 2026-10-10: [exe + 0x2a8eb48] + 0x150 in that build, the hour
        clockPath = ClockPath{ (uint32_t)clockPlace, 1, { 0x150, 0 }, false, 1.0f };
        clockOwn = true;
    }
    ClockScan clockScan = {};
    float hour = -1.0f;
    int hourSaid = -1, clockFails = 0;
    bool wasDriving = false; // a truck was driven in the world on the last pass, where the clock must be readable
    if (clockOwn)
    {
        char words[96];
        ClockPathWords(clockPath, words, sizeof words);
        Log(clockFromIni ? L"clock: the game's time of day is read at %S, as found before (Gauges.ini, [Clock])"
                         : L"clock: the game's time of day is read at %S, the place known for this version of the game", words);
    }
    bool mapOwn = false, mapAsk = true, saveSeen = true;
    ULONGLONG mapAmbientLogAt = 0; // when to log the temperature the tab set, once the slider has rested
    ULONGLONG gearHoldLogAt = 0;   // and the gearbox's hold
    ULONGLONG engineLogAt = 0;     // and the engine's profile
    ULONGLONG torqueLogAt = 0;     // and the engine's torque share
    std::wstring saveUnread; // a save found that named no map (said once)
    FILETIME saveWritten = {}; // when the save read last was written
    ULONGLONG saveLookAt = 0;
    bool codeLooked = false; // the gearbox functions were looked for, once a truck was found
    SuspTracker suspWatch = {}; // the truck that went last with none driven since, and when (the take-over)
    uint64_t truckVehicle = 0;   // the vehicle the driven truck's id was taken with (review F1 of step 2)
    for (;;)
    {
        Sleep(50);
        LogTick(); // probe lines followed by silence go out within half a second
        // the settings tab: edits count on this pass and go into the ini once they have rested a second; "read the
        // ini again" takes the file as it is
        bool edited = false;
        const int keyBefore = s.toggleKey;
        if (InterlockedExchange(&g_reloadAsked, 0))
        {
            ReadIni(set.plain);
            SettingsFor(set, truck.key, truck.id);
            SettingsPublish(s);
            // the driven engine's profile and gearbox's hold read again too, as a file edited by hand may have changed them
            if (watch.profileFor[0]) EngineProfileRead(watch.profileFor, watch.profile);
            if (watch.gearboxKey[0]) watch.gearHoldOwn = GearHoldRead(watch.gearboxKey, watch.gearHold);
            EngineFromFiles(eng);
            EngineApply(eng, watch);
            BindingsDefault(binds);
            BindingsRead(IniPath(), binds, true);
            Log(L"settings: Gauges.ini read again, and the driven truck's engine and gearbox files");
            LogSettings(s);
            BindingsLog(binds);
            saveAt = 0;
            edited = true;
        }
        if (InterlockedExchange(&g_setDirty, 0))
        {
            const bool ownBefore = set.own, suspBefore = set.suspOwn;
            GaugeSettings in;
            SettingsGet(in);
            SettingsTake(set, in);
            if (set.own && !ownBefore) Log(L"truck: %S has scales of its own from now on, kept in Gauges\\trucks\\%s.ini", set.key, SetupFileName(Wide(set.key)).c_str());
            if (set.suspOwn && !suspBefore) Log(L"suspension: every %S has its own settings from now on, kept in Gauges\\trucks\\%s.ini", set.key, SetupFileName(Wide(set.key)).c_str());
            saveAt = GetTickCount64() + 1000;
            edited = true;
        }
        if (InterlockedExchange(&g_truckDropAsked, 0) && set.own)
        {
            // the tab's button
            SettingsDrop(set);
            SettingsPublish(s);
            Log(L"truck: %S gave up its own scales and takes the plain ones", set.key);
            LogSettings(s);
            edited = true;
        }
        if (const LONG ask = InterlockedExchange(&g_suspOwnAsked, 0))
        {
            // the Suspension tab's box: off keeps the truck's set with Own=0, on brings it back (or makes the shown one its own)
            if (set.key[0])
            {
                SettingsSuspOwn(set, ask == 1);
                SettingsPublish(s);
                Log(ask == 1 ? L"suspension: the box ticked, own settings in force (Gauges\\trucks\\%s.ini)"
                             : L"suspension: the box unticked, that level's settings kept, not in use (Own=0 in Gauges\\trucks\\%s.ini)",
                    SetupFileName(Wide(set.key)).c_str());
                SuspLevelLog(set, false);
                edited = true;
            }
        }
        if (const LONG ask = InterlockedExchange(&g_suspLevelAsked, 0))
        {
            // the Suspension tab's choice under the box: this truck only, or every truck of the kind
            if (set.key[0])
            {
                SettingsSuspLevel(set, ask == 1);
                SettingsPublish(s);
                SuspLevelLog(set, false);
                edited = true;
            }
        }
        if (InterlockedExchange(&g_suspDefaultAsked, 0) && set.key[0] && set.suspOwn)
        {
            // the Suspension tab's button: this truck's set becomes the default, its own stays as it is
            SettingsSuspToDefault(set);
            Log(L"suspension: %S's settings made the default (Gauges.ini)", set.key);
            saveAt = GetTickCount64() + 1000;
            edited = true;
        }
        if (const LONG ask = InterlockedExchange(&g_engOwnAsked, 0))
        {
            // the Engine tab's box, as the Suspension tab's
            if (eng.key[0])
            {
                EngineOwnSet(eng, ask == 1);
                EngineApply(eng, watch);
                Log(ask == 1 ? L"engine: the box ticked, own engine settings in force (Gauges\\trucks\\%s.ini)"
                             : L"engine: the box unticked, that level's engine settings kept, not in use (Own=0 in Gauges\\trucks\\%s.ini)",
                    SetupFileName(Wide(eng.key)).c_str());
                EngineLevelLog(eng, L"the box");
                edited = true;
            }
        }
        if (const LONG ask = InterlockedExchange(&g_engLevelAsked, 0))
        {
            // the Engine tab's choice under the box: this truck only, or every truck of the kind
            if (eng.key[0])
            {
                EngineLevelSet(eng, ask == 1);
                EngineApply(eng, watch);
                EngineLevelLog(eng, ask == 1 ? L"This truck only" : L"Every truck of its kind");
                edited = true;
            }
        }
        if (InterlockedExchange(&g_engDefaultAsked, 0) && eng.key[0] && EngineLevel(eng) != kEngLevelDefault)
        {
            // the Engine tab's button: the figures in force become the engine's and the gearbox's default
            EngineToDefault(eng);
            EngineApply(eng, watch);
            Log(L"engine: %S's torque %d %% made %S's default (Gauges\\engines)%s", eng.key, eng.torque, eng.engine[0] ? eng.engine : "no engine",
                eng.gearbox[0] && eng.hold > 0.0f ? L", and its hold the gearbox's (Gauges\\gearboxes)" : L"");
            edited = true;
        }
        if (InterlockedExchange(&g_resetAsked, 0))
        {
            // the tab's defaults button
            EngineDefaults(eng);
            EngineApply(eng, watch);
            SettingsToDefaults(set);
            SettingsPublish(s);
            Log(L"settings: back to the defaults");
            saveAt = GetTickCount64() + 1000;
            edited = true;
        }
        if (InterlockedExchange(&g_tabPutAsked, 0))
        {
            char id[32];
            strcpy_s(id, g_tabPut);
            IniTabPut(id, IniPath());
        }
        // a key just bound on the tab is still down: it toggles from its next press on
        if (s.toggleKey != keyBefore) keyDown = true;
        if (saveAt && GetTickCount64() >= saveAt)
        {
            SettingsSave(set);
            saveAt = 0;
        }
        if (edited) padMask = PadParse(s.padToggle);
        // the toggle: the key, or the pad buttons all down that were not all down on the last pass; only while the
        // game's window is in front, and the pad not while the tab waits for buttons to bind
        const bool front = GameInFront();
        // the bindings and the toggle stand aside while the NEXT GEN menu has the keys and the pad, and while the game's
        // function menu is up and a moment after (GameMenuGate)
        const bool menuGate = NgMenuOpen() || GameMenuGateHolds(gameMenu, GetTickCount64());
        // the key's own state is kept through a menu, so a key held as it closes is no press
        const bool keyNow = front && s.toggleKey && (GetAsyncKeyState(s.toggleKey) & 0x8000);
        bool toggle = keyNow && !keyDown && !menuGate;
        keyDown = keyNow;
        const LONG capture = g_pageCapturing;
        const bool capturing = capture && GetTickCount() - (DWORD)capture < 500;
        const uint32_t padHeld = padMask && front && !capturing ? PadButtons() : 0;
        // not while a menu has the pad. What is held then still counts as held on the last pass, so a toggle held as
        // the menu opens or closes is no press when it closes
        const uint32_t pad = menuGate ? 0 : padHeld;
        if (padMask && (pad & padMask) == padMask && (padPrev & padMask) != padMask) toggle = true;
        padPrev = padHeld;
        if (toggle)
        {
            visible = !visible;
            Log(L"cluster %s", visible ? L"shown" : L"hidden");
        }
        // the gearbox's bindings: one snapshot of the keys, the pad and the wheels and shifters a pass, read only while
        // the game is in front. The tab's asks (a capture into a slot, a slot cleared) are done here with it, and a
        // capture ends when the NEXT GEN menu closes; the actions go to the gearbox only at Gearbox=2 with a binding
        // that reaches reverse, the locks' at every level, and none goes down while the menu is open or a capture runs
        {
            static InputSnapshot snap, seen;
            InputSnapshotRead(snap, front); // what was down since the last pass, from the poll thread (a quick tap is not lost)
            seen = InputFrontOnly(snap);
            const ULONGLONG bindNow = GetTickCount64();
            bool bindsChanged = false;
            const LONG ask = InterlockedExchange(&g_bindAsk, 0);
            const int askWhat = ask >> 16, askAction = (ask >> 8) & 0xFF, askSlot = ask & 0xFF;
            if (ask && askAction < kActCount && askSlot < kBindingSlots)
            {
                if (askWhat == kBindAskCapture) BindingCaptureBegin(bindCapture, askAction, askSlot, bindNow);
                else if (askWhat == kBindAskCancel) bindCapture.active = false;
                else if (askWhat == kBindAskClear)
                {
                    binds.slot[askAction][askSlot] = Binding{};
                    bindsChanged = true;
                }
            }
            if (bindCapture.active && !NgMenuOpen())
            {
                bindCapture.active = false; // the menu closed under it: the key that closed it, or the next one driven with, is no binding
                Log(L"gearbox: the capture for %S ended as the NEXT GEN menu closed; nothing bound", kActionNames[bindCapture.action]);
            }
            if (bindCapture.active)
            {
                Binding caught = {};
                const int step = BindingCaptureStep(bindCapture, seen, bindNow, caught);
                // once a capture, what its start found held (a controller reporting a hat or a switch as pressed, two
                // players' reports): left alone, and said so the next report shows it
                if (bindCapture.started && !bindCapture.told)
                {
                    bindCapture.told = true;
                    char held[512];
                    if (BindingCaptureHeld(bindCapture, seen, held, sizeof held))
                        Log(L"gearbox: the capture for %S: held at the start and left alone until let go: %S", kActionNames[bindCapture.action], held);
                }
                const char *clash = step == kCaptureBound ? BindingClash(caught, NgMenuKeyFromIni(), s.toggleKey, padMask) : nullptr;
                if (clash)
                {
                    char text[96];
                    BindingText(caught, text, sizeof text);
                    Log(L"gearbox: %S not bound to %S: it is %S", text, kActionNames[bindCapture.action], clash);
                }
                else if (step == kCaptureBound)
                {
                    binds.slot[bindCapture.action][bindCapture.slot] = caught;
                    // held as it is bound: a press only once it was let go
                    bindReader.latched[bindCapture.action][bindCapture.slot] = true;
                    bindsChanged = true;
                }
                if (step != kCaptureWaiting) bindCapture.active = false;
            }
            if (bindsChanged)
            {
                BindingsWrite(IniPath(), binds);
                Log(L"gearbox: the bindings set from the tab, written to Gauges.ini:");
                BindingsLog(binds);
            }
            // the reader steps on what is really held every pass, so its latches follow the hardware through the menu
            // (review 8, W2); reverse is reached by what is there, in front or not
            static ActionState states[kActCount];
            InputStep(bindReader, binds, seen, states);
            watch.canReverse = BindingsCanReverse(binds, snap, PadPresent());
            if (s.gearbox != kGearboxOwn || (s.input == kInputWheel && !watch.canReverse)) watch.actions = GearboxActions{};
            else watch.actions = menuGate || bindCapture.active ? ActionsResting(watch.actions) : ActionsOf(states, binds);
            // the locks at every level: a press while no truck is driven is gone, not kept for later
            const bool locksHeard = !menuGate && !bindCapture.active;
            watch.lockPress[kLockDiff] = locksHeard && states[kActDiffLock].pressed;
            watch.lockPress[kLockAwd] = locksHeard && states[kActAwd].pressed;
            if (locksHeard && states[kActSway].pressed)
            {
                watch.swayOff = !watch.swayOff;
                Log(L"suspension: the sway bars %s (BindSwayDisconnect)", watch.swayOff ? L"disconnected" : L"connected");
            }
            // the air suspension's height (build C): with it on and a truck under 20 km/h; a press at the end stays there
            const int dir = locksHeard ? (states[kActRideUp].pressed ? 1 : 0) - (states[kActRideDown].pressed ? 1 : 0) : 0;
            if (dir && watch.found)
            {
                const int was = watch.airHeight;
                // a truck with no air axle: the keys do nothing on it (the Reviewer's note 5)
                const bool airOn = s.airSuspension && watch.airAxles != kAirNone;
                watch.airHeight = AirHeightStep(was, dir, watch.kmh, airOn);
                if (airOn && watch.kmh < kRideKeyKmh) watch.airPressedAt = GetTickCount64();
                if (watch.airHeight != was) Log(L"air suspension: the height set to %+d cm (BindRide%s)", watch.airHeight, dir > 0 ? L"Up" : L"Down");
            }
        }
        // the map: the game's save, looked for every kSaveLookMs and read when it is another or was written since
        const ULONGLONG lookNow = GetTickCount64();
        if (lookNow >= saveLookAt)
        {
            saveLookAt = lookNow + kSaveLookMs;
            std::vector<SaveFile> saves;
            Saves(saves);
            const bool found = !saves.empty();
            if (found != saveSeen)
            {
                if (!found)
                {
                    // where it looked: the folders that are there, and how many more are not
                    std::wstring there;
                    int missing = 0;
                    for (const auto &dir : SaveFolders())
                    {
                        if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) { missing++; continue; }
                        if (there.size() < 1500) there += (there.empty() ? L"" : L"; ") + PathForLog(dir);
                    }
                    Log(L"map: no save of the game was found, so the map is not known and AmbientC of [Gauges] is in force (looked for CompleteSave*.cfg "
                        L"and .dat in: %s; %d more folders not there; and the Microsoft Store's containers)", there.empty() ? L"no folder that is there" : there.c_str(),
                        missing);
                }
                saveSeen = found;
            }
            char level[64];
            SaveFile picked;
            std::wstring unread;
            if (SavePick(saves, saveWritten, picked, level, sizeof level, unread))
            {
                saveWritten = picked.second;
                saveUnread.clear();
                if (strcmp(level, map))
                {
                    char at[16], title[96];
                    ClockText(picked.second, at, sizeof at);
                    MapTitle(level, title, sizeof title);
                    Log(L"map: %s (%S), the map loaded last as the game's save of %S says (%s)", Wide(title).c_str(), level, at, PathForLog(picked.first).c_str());
                    strcpy_s(map, level);
                }
            }
            else if (!unread.empty() && unread != saveUnread)
            {
                // said once for each save: one the game is writing reads again on the next look
                saveUnread = unread;
                Log(L"map: the save %s names no map the mod can read (yet: it is read again when it is written)", PathForLog(unread).c_str());
            }
        }
        // the outside temperature in force: the map's or its region's (AmbientByMap), looked up again when the map
        // changes or the ini was read or saved. The tab's slider sets the section in force
        // the hour of the game's day, where the clock is known; a clock that cannot be read for long (a game update
        // that kept the exe's stamp, a wrong find) is forgotten
        if (clockOwn)
        {
            if (ClockRead(memory, clockPath, hour)) clockFails = 0;
            else
            {
                hour = -1.0f;
                if (wasDriving && ++clockFails == 6000) // five minutes of passes with a truck driven
                {
                    Log(L"clock: the game's time of day could not be read for five minutes, so its place is forgotten and looked for again");
                    ClockForget();
                    clockOwn = false;
                }
            }
        }
        // the map's temperature now: between its night and its day by the hour, or the day's mean without a clock
        const float mapNow = hour >= 0.0f ? DayNightC(mapAir.day, mapAir.night, hour) : mapAir.mean;
        if (InterlockedExchange(&g_mapAmbientAsked, 0) && mapOwn && mapFrom[0])
        {
            // the slider moves the section's air so the temperature now is what it shows
            const float c = SettingsRange(g_mapAmbientTenths / 10.0f, kAmbientRange[0], kAmbientRange[1]);
            if (fabsf(c - mapNow) >= 0.05f)
            {
                MapAirShift(mapFrom, mapAir, c - mapNow);
                MapSectionAir(mapFrom, mapAir);
                mapAmbientLogAt = GetTickCount64() + 1000;
            }
        }
        if (InterlockedExchange(&g_gearHoldAsked, 0) && watch.gearboxKey[0])
        {
            const float hold = SettingsRange(g_gearHoldTenths / 10.0f, kGearHoldRange[0], kGearHoldRange[1]);
            if (!watch.gearHoldOwn || fabsf(hold - watch.gearHold) >= 0.05f)
            {
                // to the level in force (BRIEF_engine_per_truck.md, the suspension's rules)
                if (EngineTake(eng, 0, hold)) Log(L"engine: every %S has its own engine settings from now on, kept in Gauges\\trucks\\%s.ini", eng.key, SetupFileName(Wide(eng.key)).c_str());
                EngineApply(eng, watch);
                gearHoldLogAt = GetTickCount64() + 1000;
            }
        }
        // the engine's profile from the tab: into its file, in force at once; or given up, the file gone
        const LONG engineAsk = InterlockedExchange(&g_engineAsked, 0);
        if (engineAsk && watch.profileFor[0])
        {
            if (engineAsk == 1)
            {
                EngineProfileWrite(watch.profileFor, (float)g_enginePeak, (int)g_engineAspiration, (int)g_engineFuel);
                engineLogAt = GetTickCount64() + 1000;
            }
            else
            {
                DeleteFileW(SetupFile(kSetupEngine, watch.profileFor).c_str());
                Log(L"engine: %S gave its profile up: the class's torque curve, turbocharged", watch.profileFor);
            }
            EngineProfileRead(watch.profileFor, watch.profile);
        }
        // the engine's torque share from the tab's slider: into its file, in force at once
        if (InterlockedExchange(&g_engineTorqueAsked, 0) && watch.profileFor[0])
        {
            const int percent = (int)lroundf(SettingsRange((float)g_engineTorquePercent, kTorqueShareRange[0] * 100.0f, kTorqueShareRange[1] * 100.0f));
            if (EngineTake(eng, percent, 0.0f)) Log(L"engine: every %S has its own engine settings from now on, kept in Gauges\\trucks\\%s.ini", eng.key, SetupFileName(Wide(eng.key)).c_str());
            EngineApply(eng, watch);
            torqueLogAt = GetTickCount64() + 1000;
        }
        if (torqueLogAt && GetTickCount64() >= torqueLogAt)
        {
            EngineLevelLog(eng, L"set from the tab");
            torqueLogAt = 0;
        }
        if (engineLogAt && GetTickCount64() >= engineLogAt)
        {
            Log(L"engine: %S set from the tab: its peak torque at %.0f rpm, %s, %s (Gauges\\engines\\%s.ini)", watch.profileFor, watch.profile.peakRpm,
                watch.profile.aspiration == kAspNatural ? L"naturally aspirated" : L"turbocharged",
                watch.profile.fuel == kFuelDiesel ? L"a diesel" : watch.profile.fuel == kFuelPetrol ? L"a petrol engine" : L"the class's fuel", SetupFileName(Wide(watch.profileFor)).c_str());
            engineLogAt = 0;
        }
        if (gearHoldLogAt && GetTickCount64() >= gearHoldLogAt)
        {
            EngineLevelLog(eng, L"set from the tab");
            gearHoldLogAt = 0;
        }
        if (mapAmbientLogAt && GetTickCount64() >= mapAmbientLogAt)
        {
            if (mapAir.dayNight) Log(L"map: [Map:%S] set from the tab to %g C by day and %g C by night", mapFrom, mapAir.day, mapAir.night);
            else Log(L"map: [Map:%S] set from the tab to %g C outside on the day's mean", mapFrom, mapAir.mean);
            mapAmbientLogAt = 0;
        }
        if (strcmp(mapRead, map) || edited) mapAsk = true;
        if (mapAsk)
        {
            const bool before = mapOwn;
            mapOwn = map[0] && MapAmbient(map, mapAir, mapFrom, sizeof mapFrom);
            if (strcmp(mapRead, map) || mapOwn != before)
            {
                char title[96];
                MapTitle(map, title, sizeof title);
                if (mapOwn && s.ambientByMap)
                    Log(L"map: %s, %g C outside by day and %g C by night%s ([Map:%s] in Gauges.ini)", Wide(title).c_str(), mapAir.day, mapAir.night,
                        mapAir.dayNight ? L"" : L", around the day's mean", Wide(mapFrom).c_str());
                else if (mapOwn) Log(L"map: %s, %g C outside (AmbientC: AmbientByMap=0 leaves [Map:%s] out)", Wide(title).c_str(), s.ambientC, Wide(mapFrom).c_str());
                else if (map[0]) Log(L"map: %s has no section in Gauges.ini, nor has its region, so it is %g C outside (AmbientC)", Wide(title).c_str(), s.ambientC);
            }
            strcpy_s(mapRead, map);
            mapAsk = false;
        }
        GaugeSettings inForce = s;
        if (mapOwn && s.ambientByMap) inForce.ambientC = hour >= 0.0f ? DayNightC(mapAir.day, mapAir.night, hour) : mapAir.mean;
        if (s.probe && hour >= 0.0f && (int)hour != hourSaid)
        {
            hourSaid = (int)hour;
            Log(L"clock: %02d:%02d of the game's day, %.1f C outside", (int)hour, (int)((hour - (int)hour) * 60.0f), inForce.ambientC);
        }
        // what the cluster shows: the truck being driven; with the ini's Demo, demo values while none is
        TruckRaw raw;
        World world;
        watch.ownScales = set.own;
        // the engine's settings for the truck, engine and gearbox fitted: read again when any of them changes (an
        // engine changed in the garage goes by the next level until an edit writes its own key), in force each pass
        // (the engine's and the gearbox's names only once they are this vehicle's, not the truck's driven before)
        const char *engNow = truck.key[0] && watch.engine.found && watch.engineVehicle == watch.refs.vehicle ? watch.profileFor : "",
                   *boxNow = truck.key[0] && watch.gearboxVehicle == watch.refs.vehicle ? watch.gearboxKey : "";
        if (strcmp(eng.key, truck.key) || strcmp(eng.id, truck.key[0] ? truck.id : "") || strcmp(eng.engine, engNow) || strcmp(eng.gearbox, boxNow))
        {
            EngineFor(eng, truck.key, truck.id, engNow, boxNow);
            EngineLevelLog(eng, L"the truck, engine or gearbox taken");
        }
        EngineApply(eng, watch);
        const bool driving = TruckPass(watch, memory, inForce, front, raw, world);
        wasDriving = driving;
        if (GameMenuGateSeen(gameMenu, GameMenuUp(world), GetTickCount64())) Log(L"input: the game's function menu is up, the bindings wait");
        if (watch.found && !codeLooked)
        {
            codeLooked = true;
            GameCodeLook();
        }
        // the hooks a level needs, attached the first time it needs them (also when the tab turns them on mid-game)
        GearboxHooksStep(s);
        CreepHookStep(s, driving);
        // the search for the game's clock (Probe=1, while its place is not known): begun once a truck is driven
        if (!clockOwn && s.probe && driving && raw.clockKnown)
        {
            if (clockScan.state == kClockIdle)
            {
                const ULONGLONG began = GetTickCount64();
                ClockScanBegin(clockScan, memory, ImageSize());
                Log(L"clock: looking for the game's time of day: %zu objects (%u KB), %zu places with a value from 0 to 24 (%llu ms). Drive on; "
                    L"skip the night on the map (or wait in a garage) once and it is found",
                    clockScan.objects.size(), clockScan.bytes / 1024, clockScan.slots.size(), GetTickCount64() - began);
            }
            const ClockNews news = ClockScanStep(clockScan, memory, watch.refs.vehicle, raw.clock, driving);
            if (news.steadyNow || news.jumpedNow)
                for (ClockSlot &slot : clockScan.slots)
                    if (slot.steady >= kClockSteady && (!slot.told || (slot.jumped && news.jumpedNow)))
                    {
                        char words[96];
                        const ClockPath p = ClockSlotPath(clockScan, slot, slot.most > 1.05 ? 1.0f : 24.0f);
                        ClockPathWords(p, words, sizeof words);
                        Log(L"clock: %S %s at %.3f, a day of %.0f minutes by its pace", words, slot.jumped ? L"jumped and kept its pace" : L"rises steadily",
                            slot.last, ClockDayMinutes(slot));
                        slot.told = true;
                    }
            if (news.looked && !news.found && !news.failed && clockScan.looks % 6 == 0)
                Log(L"clock: %d places still rise at a day's pace", news.left);
            if (news.found)
            {
                clockPath = clockScan.found;
                clockOwn = true;
                ClockSave(stamp, clockPath);
                char words[96];
                ClockPathWords(clockPath, words, sizeof words);
                Log(L"clock: the game's time of day is at %S (kept in Gauges.ini under [Clock]); the outside temperature follows it from now on", words);
                clockScan = ClockScan{};
            }
            else if (news.failed && !clockSaid)
            {
                Log(L"clock: no place rose at a day's pace; the time of day stays unknown until the game is started again");
                clockSaid = true;
            }
        }
        const TruckIdentity &now = watch.who.taken;
        if (strcmp(now.key, truck.key))
        {
            // another truck, or none: what was changed for the last one is saved first. Then the plain scales are in
            // force, with the new truck's own in their place where it has some.
            if (saveAt) SettingsSave(set);
            saveAt = 0;
            // the take-over (step 2): the driven truck went, and the same kind is back under another id within 20 s with
            // no other truck driven between: the game made it anew (a recovery, a garage), and the new id takes the old
            // one's suspension over. A truck driven between, or a longer gap, is none.
            char oldId[40];
            const ULONGLONG goneAt = suspWatch.goneAt;
            const bool takeOver = SuspTrack(suspWatch, truck, now, GetTickCount64(), oldId);
            truck = now;
            truckVehicle = watch.idVehicle;
            int taken = 0;
            if (takeOver)
            {
                strcpy_s(set.key, truck.key);
                taken = TakeOverBoth(truck.key, oldId, truck.id);
            }
            SettingsFor(set, truck.key, truck.id);
            SettingsPublish(s);
            const GaugeScale k = GaugeScaleOf(s);
            watch.ownScales = set.own;
            const int autoClass = AutoClass(watch, s.tachClass);
            if (truck.key[0])
                Log(L"truck: this is %S%s, the game's id for it %S (vehicle %p); %s: idle %.0f rpm, red zone from %.0f of %.0f, speedometer to %.0f km/h; the automatic "
                    L"goes by the %s class%s; the engine's lift at most %g (the class's roof, fading over each gear's last fifth)",
                    truck.key, truck.named ? L"" : L" (the name of its kind was not read, so it goes by its build)", truck.id[0] ? truck.id : "not read",
                    (void *)truckVehicle, set.own ? L"it has scales of its own" : L"it has no scales of its own and takes the plain ones", k.idle, k.redline, k.max, k.speedMax,
                    kClassWords[autoClass], set.own ? L", its scales'" : watch.mass > 0.0f ? L", its weight's" : L", the plain scales' (its weight not read)",
                    TorqueRoof(autoClass));
            if (taken)
                Log(L"suspension: the take-over: %S under its new id %S (vehicle %p) takes over %s of %S (back %.0f s after it went)", truck.key, truck.id,
                    (void *)truckVehicle, TakenWords(taken), oldId, (GetTickCount64() - goneAt) / 1000.0);
            if (truck.key[0]) SuspLevelLog(set, truck.id[0] && !set.truckOwn && !set.truckKept && !taken && !takeOver);
        }
        else if (strcmp(now.id, truck.id))
        {
            // the same kind under another id while driven: the game made the truck anew, or this is another truck of the
            // kind. A first read of the id is no take-over; else the new id takes the old one's suspension over when it
            // has none of its own (TruckSuspTakeOver)
            if (saveAt) SettingsSave(set);
            saveAt = 0;
            char oldId[40];
            strcpy_s(oldId, truck.id);
            const uint64_t oldVehicle = truckVehicle;
            truck = now;
            truckVehicle = watch.idVehicle; // recorded on the pass the new id was read with
            Log(L"truck: %S again, the game's id for it now %S (vehicle %p, the old id's %p)", truck.key, truck.id, (void *)truckVehicle, (void *)oldVehicle);
            // a take-over only on the vehicle the old id was taken with (review F1); another vehicle is another truck
            const bool sameTruck = SuspAgainTakeOver(oldId, oldVehicle, truck.id, truckVehicle);
            const int taken = sameTruck ? SettingsTakeOver(set, truck.id) : 0;
            if (!taken) SettingsFor(set, truck.key, truck.id);
            SettingsPublish(s);
            if (taken)
                Log(L"suspension: the take-over: %S under its new id %S (vehicle %p) takes over %s of %S (while driven, the same vehicle)", truck.key, truck.id,
                    (void *)truckVehicle, TakenWords(taken), oldId);
            SuspLevelLog(set, !set.truckOwn && !set.truckKept && !taken && truck.id[0]);
        }
        GaugeView v = {};
        strcpy_s(v.truck, truck.key);
        v.truckOwn = set.own;
        v.suspOwn = set.suspOwn || set.truckOwn;
        v.suspKept = set.suspKept || set.truckKept;
        v.suspLevel = SuspLevel(set);
        v.suspTruckKept = set.truckKept;
        v.suspKindSet = set.suspOwn || set.suspKept;
        v.suspIdKnown = set.id[0] != 0;
        if (map[0]) MapTitle(map, v.map, sizeof v.map);
        else v.map[0] = 0;
        strcpy_s(v.mapFrom, mapFrom);
        v.mapOwn = mapOwn && s.ambientByMap;
        v.ambientC = inForce.ambientC;
        v.ambientDay = mapAir.day;
        v.ambientNight = mapAir.night;
        v.hour = hour;
        v.smtInstalled = g_gearboxBlocked == kGearboxSmt;
        v.gearboxHeld = g_gearboxBlocked == kGearboxHeld;
        for (int a = 0; a < kViewActions; a++)
            for (int slot = 0; slot < 2; slot++)
                if (a < kActCount) BindingText(binds.slot[a][slot], v.bindText[a][slot], sizeof v.bindText[a][slot]);
                else v.bindText[a][slot][0] = 0;
        v.captureAction = bindCapture.active ? bindCapture.action : -1;
        v.captureSlot = bindCapture.active ? bindCapture.slot : -1;
        v.canReverse = watch.canReverse;
        v.controllers = InputDevicesFound();
        v.gearStalls = watch.take.driving ? watch.pos.stalls : 0;
        v.gearShifts = watch.take.driving ? watch.take.shifts : 0;
        strcpy_s(v.gearbox, driving ? watch.gearboxKey : "");
        // the engine and its curve in force, for the tab's Engine section and its plot
        if (driving && watch.profileFor[0])
        {
            const GaugeScale es = GaugeScaleOf(inForce);
            const TorqueCurve ec = EngineCurveOf(watch.profile, kClassCurves[AutoClass(watch, inForce.tachClass)], es.redline);
            strncpy_s(v.engine, watch.profileFor, _TRUNCATE);
            v.engineOwn = watch.profile.own;
            v.engineCustom = watch.profile.custom;
            v.engineAspiration = watch.profile.aspiration;
            v.engineFuel = watch.profile.fuel;
            v.engineTorque = watch.profile.torque > 0.0f ? watch.profile.torque : 1.0f;
            v.engineFigure = watch.engine.torque;
            v.enginePeakRpm = watch.profile.peakRpm > 0.0f && !watch.profile.custom ? watch.profile.peakRpm : CurvePeak(ec) * es.redline;
            v.engineIdle = es.idle;
            v.engineRedline = es.redline;
            const float top = es.redline > 0.0f ? es.max / es.redline : 1.0f, from = es.redline > 0.0f ? es.idle / es.redline : 0.0f;
            for (int i = 0; i < kEnginePlot; i++) v.engineCurve[i] = TorqueAt(ec, from + (top - from) * i / (kEnginePlot - 1), top);
        }
        v.gearHold = GearHoldOf(watch, AutoClass(watch, inForce.tachClass));
        v.gearHoldOwn = watch.gearHoldOwn;
        // the engine's settings per truck, for the Engine tab's box (as the Suspension tab's)
        v.engSetOwn = eng.kindOwn || eng.truckOwn;
        v.engSetKept = eng.kindKept || eng.truckKept;
        v.engSetTruckKept = eng.truckKept;
        v.engSetKindSet = eng.kindOwn || eng.kindKept;
        v.engSetLevel = eng.key[0] ? EngineLevel(eng) : kEngLevelNone;
        v.engTorqueFrom = eng.torqueFrom, v.engHoldFrom = eng.holdFrom;
        // the suspension's figures as the driven truck's file has them, front (wheel 0) and rear (the last wheel)
        // the display's figures, the bars' lamp and each axle's rest place (build B)
        {
            const SuspDisplaySlot &d = watch.step.shown;
            SuspDisplayView &sv = v.susp;
            sv = SuspDisplayView{};
            if (driving && inForce.suspDisplay && d.wheels > 0)
            {
                sv.wheels = d.wheels < kViewWheels ? d.wheels : kViewWheels;
                int axle[kMaxWheels];
                for (int i = 0; i < sv.wheels; i++) axle[i] = -1;
                for (int i = 0; i < sv.wheels; i++)
                {
                    if (axle[i] >= 0) continue;
                    axle[i] = sv.axles;
                    const int j = watch.step.partner[i];
                    if (j > i && j < sv.wheels) axle[j] = sv.axles;
                    sv.axles++;
                }
                for (int i = 0; i < sv.wheels; i++)
                {
                    sv.x[i] = d.x[i], sv.travel[i] = d.travel[i], sv.travelMax[i] = d.travelMax[i], sv.rest[i] = d.rest[i];
                    sv.hit[i] = d.hit[i], sv.hanging[i] = d.hanging[i], sv.placed[i] = d.placed[i], sv.isLeft[i] = watch.step.isLeft[i], sv.axle[i] = axle[i];
                }
                sv.stops = inForce.bumpStops;
                sv.zone = inForce.bumpZone;
                // the articulation: the front pair's difference of compression less the rearmost pair's, in cm
                float fr = 0.0f, rr = 0.0f;
                bool fk = false, rk = false;
                for (int i = 0; i < sv.wheels; i++)
                {
                    const int j = watch.step.partner[i];
                    if (j < 0 || j >= sv.wheels || !watch.step.isLeft[i] || !d.placed[i] || !d.placed[j]) continue;
                    const float diff = -d.x[i] + d.x[j];
                    if (watch.step.front[i] && !fk) fr = diff, fk = true;
                    if (watch.step.rearmost[i] && !watch.step.front[i] && !rk) rr = diff, rk = true;
                }
                sv.articulationKnown = fk && rk;
                sv.articulation = (fr - rr) * 100.0f;
            }
            v.swayOff = driving && watch.swayOff && (inForce.swayFront > 0.0f || inForce.swayRear > 0.0f);
            // the air suspension: the tab's figures, and the overlay's height box for 3 s after a press
            v.air = driving && inForce.airSuspension && watch.step.block.air;
            v.airTaken = v.air && watch.step.airTaken;
            v.airHeight = watch.airHeight;
            v.airKnown = driving && watch.airFor[0];
            v.airAxles = watch.airAxles, v.airFrom = watch.airFrom;
            v.airLevel[0] = watch.step.airLevel[0], v.airLevel[1] = watch.step.airLevel[1], v.airSide = watch.step.airSide;
            v.airShown = driving && inForce.airSuspension && watch.airPressedAt && GetTickCount64() - watch.airPressedAt < 3000;
            // each axle's rest place from the probe's windows or the display (cm along the axis, compression negative)
            v.rideRestKnown = false;
            float sum[2] = {}, count[2] = {};
            for (int i = 0; driving && i < watch.refs.wheels && i < kMaxWheels; i++)
            {
                const bool known = d.wheels > 0 && i < d.wheels && d.placed[i] ? true : watch.step.windowRested[i];
                if (!known) continue;
                const float rest = d.wheels > 0 && i < d.wheels && d.placed[i] ? d.rest[i] : watch.step.windowRest[i];
                const int axle = watch.step.front[i] ? 0 : 1;
                sum[axle] += rest * 100.0f, count[axle] += 1.0f;
            }
            if (count[0] > 0.0f && count[1] > 0.0f)
            {
                watch.rest[0] = sum[0] / count[0], watch.rest[1] = sum[1] / count[1];
                watch.restVehicle = watch.refs.vehicle;
            }
            // the last figures stay while the same truck is driven, so the line does not go when the ride height comes to 0
            if (driving && watch.restVehicle && watch.restVehicle == watch.refs.vehicle)
            {
                v.rideRest[0] = watch.rest[0], v.rideRest[1] = watch.rest[1];
                v.rideRestKnown = true;
            }
        }
        v.frameKnown = v.frameRigid = false;
        if (driving && watch.frame.vehicle == watch.refs.vehicle && watch.frame.looked)
        {
            v.frameKnown = watch.frame.found && watch.frame.motor;
            v.frameRigid = !watch.frame.found;
            v.frameFile[0] = watch.frame.stock[kFrameSpring], v.frameFile[1] = watch.frame.stock[kFrameDamping];
            v.frameFile[2] = FrameDeg(watch.frame.stock[kFrameMin]), v.frameFile[3] = FrameDeg(watch.frame.stock[kFrameMax]);
            v.frameMost = FrameDeg(FrameLimitMost(watch.frame.joint));
        }
        v.suspKnown = false;
        if (driving && watch.tune.vehicle == watch.refs.vehicle && watch.refs.wheels > 0)
        {
            const SuspWheel &front = watch.tune.wheel[0], &rear = watch.tune.wheel[(watch.refs.wheels < kMaxWheels ? watch.refs.wheels : kMaxWheels) - 1];
            v.suspKnown = front.constraint && rear.constraint;
            const int field[3] = { kSuspStrength, kSuspDamping, kSuspMin };
            for (int k = 0; k < 3; k++) v.suspFile[k][0] = front.stock[field[k]], v.suspFile[k][1] = rear.stock[field[k]];
        }
        v.autoClass = AutoClass(watch, inForce.tachClass);
        v.autoByWeight = driving && !watch.ownScales && watch.mass > 0.0f;
        v.massKg = driving ? watch.mass : 0.0f;
        v.pullTest = watch.pullTest.state;
        v.pullPhase = watch.pullTest.phase;
        v.pullWhy = watch.pullTest.why;
        v.demo = !driving && s.demo;
        v.hidden = !visible ? kHiddenByToggle : s.soundsOnly ? kHiddenSoundsOnly : driving || s.demo ? kShown : kHiddenNoTruck;
        v.show = v.hidden == kShown;
        uint32_t lampsLit = 0, lampsBlink = 0; // the lamps' steady states for state.h, set while a truck is driven
        if (driving)
        {
            TruckToState(raw, world, inForce, v.st);
            v.st.rpm = watch.tach.rpm;
            if (watch.take.driving && watch.pos.running)
            {
                // the game's flag is down, so its own letter would be a number: the gearbox's position gives it
                v.st.gearMode = GearboxPosLetter(watch.pos.pos);
                v.st.lowGear = watch.pos.pos >= kPosLowMinus && watch.pos.pos <= kPosLowPlus;
            }
            v.st.boostBar = watch.boost.bar;
            v.st.boostVacuum = watch.turbo.petrol;
            v.st.boostTop = watch.turbo.most;
            v.st.natural = watch.profile.aspiration == kAspNatural;
            v.st.airBar = watch.air.bar;
            v.st.rangeKm = watch.range.rangeKm;
            v.st.burnPerMin = watch.burnKnown ? watch.burn : -1.0f;
            v.st.tilt = watch.tilt.running ? watch.tilt.lamp : -1;
            v.st.rollDeg = watch.tilt.rollDeg;
            v.st.tempC = watch.temp.temp;
            v.st.oilC = watch.temp.oil;
            v.st.cold = TempCold(watch.temp);
            // the damage lamp blinks while the model adds damage, and for two seconds after a write; the oil lamp
            // while the oil is too hot
            const ULONGLONG ms = GetTickCount64();
            v.st.oilHot = OilLamp(watch.oil, ms);
            TiresLamp(watch.tires, watch.refs.vehicle, ms, v.st.tireLamp, v.st.tireLit, v.st.tireWarn);
            const bool damageAdding = DamageAdding(watch.damage, s) || (watch.addedAt && ms - watch.addedAt < 2000);
            // for other mods (state.h) the blinking lamps go as lit and blinking, so a reader blinks them at its own rate
            // and never samples the cluster's phase
            const bool tireBlinks = v.st.tireLamp && !v.st.tireWarn && (watch.tires.changing || watch.tires.held);
            if (v.st.damage || damageAdding) lampsLit |= SRG_LAMP_DAMAGE;
            if (damageAdding) lampsBlink |= SRG_LAMP_DAMAGE;
            if (watch.oil.on) lampsLit |= SRG_LAMP_OIL, lampsBlink |= SRG_LAMP_OIL;
            if (v.st.tireLamp && (v.st.tireWarn || tireBlinks || watch.tires.mode != 2)) lampsLit |= SRG_LAMP_TIRE;
            if (tireBlinks) lampsBlink |= SRG_LAMP_TIRE;
            if (v.st.tireWarn) lampsLit |= SRG_LAMP_TIRE_WARN;
            v.st.damage = DamageLamp(v.st.damage, damageAdding, ms);
            // the warning boxes, below the tire mod's: the clutch's with ClutchBurn and Damage, the engine's with Damage
            // (red while the heat's damage is dealt; a point dealt is a new write of engine damage)
            {
                static ULONGLONG addedWas = 0;
                const bool dealt = watch.addedAt != addedWas;
                addedWas = watch.addedAt;
                const bool full = BoostFull(watch.boost.bar, watch.boost.most, TruckPedal(raw));
                v.warn[kWarnClutch] = ClutchWarn(watch.clutchHeat, s.damage && s.clutchBurn);
                v.warn[kWarnEngine] = EngineWarn(watch.temp.temp, s.damage ? watch.damage.hot : 0.0f, s.damage, full, dealt, watch.floorPan);
            }
            v.engineDamage = raw.damage;
            v.engineCapacity = raw.damageKnown ? raw.damageCapacity : 0;
        }
        else if (v.demo) GaugeDemo(v.st, s);
        // the clock: the game's hour and the outside temperature in force (the demo keeps its own)
        if (!v.demo)
        {
            v.st.hour = hour;
            v.st.outsideC = inForce.ambientC;
        }
        AcquireSRWLockExclusive(&g_viewLock);
        g_view = v;
        ReleaseSRWLockExclusive(&g_viewLock);
        // and for other mods: the outside temperature in force always, the truck's values while one is driven
        StateValues published = {};
        published.ambientC = inForce.ambientC;
        if (mapOwn && s.ambientByMap) published.flags |= kStateByMap;
        if (driving)
        {
            published.vehicle = watch.refs.vehicle;
            published.coolantC = watch.temp.temp;
            published.rpm = watch.tach.rpm;
            published.boostBar = watch.boost.bar;
            published.flags |= kStateVehicle | (TruckEngineOn(raw) ? kStateEngineOn : 0);
            // version 2: what the cluster shows (v.st, this pass), the speed signed and the lamps steady
            const GaugeState &st = v.st;
            published.speedKmh = isfinite(raw.speed) ? raw.speed * 3.6f : 0.0f;
            published.speedMax = st.speedMax;
            published.rpmRedline = st.rpmRedline;
            published.rpmMax = st.rpmMax;
            published.gear = st.gear;
            published.gearMode = StateGearMode(st.gearMode);
            const int top = TruckTopGear(raw);
            published.top = (uint8_t)(top < 0 ? 0 : top > 255 ? 255 : top);
            published.gearboxLevel = (uint8_t)watch.gearboxLevel;
            published.tireLamp = (uint8_t)st.tireLamp;
            published.oilC = st.oilC;
            published.oilWarnC = st.oilWarnC;
            published.airBar = st.airBar;
            published.boostTop = st.boostTop;
            published.fuelFrac = st.fuelFrac;
            published.fuelL = raw.fuel;
            published.tankL = raw.tank;
            published.burnPerMin = st.burnPerMin;
            published.rangeKm = st.rangeKm;
            published.lamps = lampsLit | (st.handbrake ? SRG_LAMP_HANDBRAKE : 0) | (st.diffLock ? SRG_LAMP_DIFF_LOCK : 0) | (st.awd ? SRG_LAMP_AWD : 0) |
                              (st.lowGear ? SRG_LAMP_LOW_GEAR : 0) | (st.headlights ? SRG_LAMP_LIGHTS : 0) | (st.cold ? SRG_LAMP_COLD : 0) |
                              ((uint32_t)(st.tilt < 0 ? 3 : st.tilt > 2 ? 2 : st.tilt) << SRG_LAMP_TILT_SHIFT);
            published.blinks = lampsBlink;
            published.options = (st.mph ? SRG_OPT_MPH : 0) | (st.psi ? SRG_OPT_PSI : 0) | (st.fahrenheit ? SRG_OPT_FAHRENHEIT : 0) | (v.show ? SRG_OPT_CLUSTER : 0) |
                                (st.boostVacuum ? SRG_OPT_VACUUM : 0) | (st.natural ? SRG_OPT_NO_BOOST : 0);
        }
        StateWrite(state, published, GetTickCount64());
    }
}

// Loaded by an ASI loader (Gauges.asi), or by ReShade under the name from before 0.16 (Gauges.addon64): the mod's thread
// starts the overlay either way. The mod pins itself first and runs for the whole game: ReShade unloads an add-on that
// does not register with it, and the gearbox hooks must outlive any unload.
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_DETACH) LogFlushTry(); // the ExitProcess hook's flush is the one that counts
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t *name = wcsrchr(path, L'\\');
    wchar_t own[MAX_PATH] = {};
    GetModuleFileNameW(inst, own, MAX_PATH);
    const wchar_t *dot = wcsrchr(own, L'.');
    const bool addon = dot && !_wcsnicmp(dot, L".addon", 6);
    if (!name || _wcsicmp(name + 1, L"SnowRunner.exe")) return addon ? FALSE : TRUE; // loaded into another program: do nothing
    g_dir.assign(path, name + 1 - path);
    g_base = (uint64_t)GetModuleHandleW(nullptr);
    g_self = inst;
    g_addon = addon;
    // One copy of the mod per game: a second file of it (Gauges.asi beside Gauges.addon64, or a copy left under
    // another name) would draw a second cluster and write the same ini. The mutex is the process's.
    wchar_t once[64];
    swprintf_s(once, L"Local\\SnowRunnerGauges-%lu", GetCurrentProcessId());
    const HANDLE mutex = CreateMutexW(nullptr, FALSE, once);
    g_second = mutex && GetLastError() == ERROR_ALREADY_EXISTS;
    // pinned in every case: the gearbox hooks live as long as the game, so no unload path exists (review 6, G3)
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&DllMain, &pinned);
    const HANDLE t = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    return TRUE;
}
#else
// The offline test of the settings: build.bat makes out\test\settings_test.exe from this file with
// GAUGES_SETTINGS_TEST. It works on a Gauges.ini next to itself: a missing ini comes out as the defaults with the help
// text, every key reads back the same (every setting is given a value that is not its default, so a key that is never
// written shows), the help text and keys the mod does not know survive a write, values that are no numbers give the
// defaults, values out of range are held to it, the scales fit each other, a truck's own scales come and go with its
// section, and the maps' sections are written once and read. With an ini as its argument, a copy of that file must
// read back unchanged after a write.
bool OverlayInit(HMODULE) { return false; }
volatile LONG g_pageCapturing = 0, g_modOff = 0;

static bool SameSettings(const GaugeSettings &a, const GaugeSettings &b)
{
    return a.layout == b.layout && a.swapDials == b.swapDials && a.style == b.style && a.faceOpacity == b.faceOpacity && a.offsetX == b.offsetX && a.offsetY == b.offsetY &&
           a.scale == b.scale && a.opacity == b.opacity && a.mph == b.mph && a.psi == b.psi && a.fahrenheit == b.fahrenheit && a.showTemp == b.showTemp &&
           a.showBoost == b.showBoost && a.showAir == b.showAir && a.showClock == b.showClock && a.showFuelUse == b.showFuelUse && a.showLamps == b.showLamps && a.tachClass == b.tachClass && a.idleRpm == b.idleRpm &&
           a.redlineRpm == b.redlineRpm && a.maxRpm == b.maxRpm && a.speedMax == b.speedMax && a.boostMax == b.boostMax && a.toggleKey == b.toggleKey &&
           a.rollWarnDeg == b.rollWarnDeg && a.rollDangerDeg == b.rollDangerDeg && a.pitchDangerDeg == b.pitchDangerDeg &&
           a.warmUpMinutes == b.warmUpMinutes && a.overheatMinutes == b.overheatMinutes && a.coolDownMinutes == b.coolDownMinutes && a.ambientC == b.ambientC &&
           a.ambientByMap == b.ambientByMap && a.airFullKmh == b.airFullKmh && a.damage == b.damage && a.hotDamage == b.hotDamage && a.hotDamageFrom == b.hotDamageFrom && a.coldDamage == b.coldDamage && a.stallDamage == b.stallDamage && a.clutchBurn == b.clutchBurn && a.clutchHeat == b.clutchHeat &&
           a.springScale == b.springScale && a.dampingScale == b.dampingScale && a.extraTravel == b.extraTravel && a.frameFlex == b.frameFlex && a.twistRange == b.twistRange &&
           !strcmp(a.padToggle, b.padToggle) && a.demo == b.demo && a.probe == b.probe && a.keepLogs == b.keepLogs && a.soundVolume == b.soundVolume && a.soundsOnly == b.soundsOnly && a.dieselFlutter == b.dieselFlutter &&
           a.oilGauge == b.oilGauge && a.boostProfile == b.boostProfile && a.oilWarmUpMinutes == b.oilWarmUpMinutes && a.oilWarnC == b.oilWarnC && a.gearbox == b.gearbox && a.requireClutch == b.requireClutch && a.requireGearHeld == b.requireGearHeld && a.skipNeutral == b.skipNeutral && a.downStaysForward == b.downStaysForward && a.ignoreShifter == b.ignoreShifter && a.backToA == b.backToA && a.input == b.input && a.powertrain == b.powertrain && a.roadLoad == b.roadLoad && a.roadCdA == b.roadCdA && a.engineShare == b.engineShare && a.creep == b.creep && a.creepManual == b.creepManual;
}

static int g_failed = 0;
static void Check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failed++;
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    g_dir.assign(path, wcsrchr(path, L'\\') + 1 - path);
    const std::wstring ini = IniPath();
    DeleteFileW(ini.c_str());
    const bool gone = GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES;
    GaugeSettings d, a, b, c;
    SettingsDefaults(d);
    ReadIni(a);
    const bool written = GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES;
    ReadIni(b);
    Check(gone && written && SameSettings(a, d) && SameSettings(b, d), "ini: a missing ini is written with the defaults and reads back as them");
    Check(d.layout == kLayoutStrip && !d.swapDials && d.style == kFaceOpen && !d.showAir && d.showClock && d.showFuelUse && !d.demo && d.probe == 0 && d.keepLogs == 20 && !d.mph && !d.psi && !d.fahrenheit && d.toggleKey == 0 &&
          !strcmp(d.padToggle, "None") && d.rollWarnDeg == 15.0f && d.rollDangerDeg == 25.0f && d.pitchDangerDeg == 35.0f,
          "defaults: strip, open style, metric, the air dial off, no toggle, demo off, probe off, the tilt lamp at 15, 25 and 35 degrees");
    Check(d.warmUpMinutes == 3.0f && d.overheatMinutes == 1.5f && d.coolDownMinutes == 15.0f && d.ambientC == 15.0f && d.ambientByMap,
          "defaults: the coolant warm after 3 minutes, in the red zone after 1.5, cool after 15, with 15 C outside");
    Check(d.airFullKmh == 30.0f && d.damage && d.hotDamage == 10.0f && d.hotDamageFrom == kTempRed && d.coldDamage == 5.0f && d.stallDamage == 5.0f && d.clutchBurn && d.clutchHeat == 60.0f &&
              d.springScale == 1.0f && d.dampingScale == 1.0f && d.extraTravel == 0.0f && d.frameFlex == 1.0f && d.twistRange == 1.0f,
          "defaults: the radiator's full airflow from 30 km/h, engine damage on from the red zone, 10 percent a minute for the heat and 5 for the cold");
    Check(d.oilGauge == kOilNeedle && d.oilWarmUpMinutes == 5.0f && d.oilWarnC == 120.0f && d.boostProfile == kSpoolStandard && d.gearbox == kGearboxGame && !d.requireClutch && !d.requireGearHeld && !d.skipNeutral && !d.downStaysForward && !d.ignoreShifter && d.backToA == 0 && d.input == kInputWheel && !d.powertrain && d.roadLoad && d.roadCdA == 0.0f && d.engineShare == 50 && d.creep && !d.creepManual && !d.dieselFlutter,
          "defaults: the oil as a second needle, warm after 5 minutes, too hot from 120 C; the standard turbo, a diesel's lift without the flutter; the creep on");

    // Air drag on by default (BRIEF_defaults_on.md): a missing key gives 1, an explicit 0 stays 0, the defaults give 1
    {
        GaugeSettings r;
        SettingsDefaults(r);
        WriteIni(r);
        WritePrivateProfileStringW(kIniSection, L"RoadLoad", nullptr, ini.c_str());
        GaugeSettings missing;
        ReadIni(missing);
        WritePrivateProfileStringW(kIniSection, L"RoadLoad", L"0", ini.c_str());
        GaugeSettings zero;
        ReadIni(zero);
        GaugeSettings back;
        SettingsDefaults(back);
        Check(missing.roadLoad && !zero.roadLoad && back.roadLoad, "defaults: Air drag on with the key missing, an explicit RoadLoad=0 stays off, and the defaults give it on");
        WriteIni(back); // the ini as the tests below find it, with its help text
    }
    // keys the mod does not know, in its own section and in another one
    WritePrivateProfileStringW(kIniSection, L"FromALaterVersion", L"7", ini.c_str());
    WritePrivateProfileStringW(L"Other", L"Kept", L"yes", ini.c_str());
    c = d;
    c.layout = kLayoutBlock;
    c.swapDials = true;
    c.style = kFaceTinted;
    c.faceOpacity = 0.6f;
    c.offsetX = -212.5f;
    c.offsetY = 37.25f;
    c.scale = 1.35f;
    c.opacity = 0.8f;
    c.mph = c.psi = c.fahrenheit = true;
    c.showTemp = c.showBoost = c.showLamps = false;
    c.showAir = true;
    c.showClock = false;
    c.showFuelUse = false;
    c.tachClass = kClassHeavy;
    c.idleRpm = 700.0f;
    c.redlineRpm = 2100.0f;
    c.maxRpm = 3500.0f;
    c.speedMax = 90.0f;
    c.boostMax = 1.25f;
    c.rollWarnDeg = 12.5f;
    c.rollDangerDeg = 22.0f;
    c.pitchDangerDeg = 30.0f;
    c.warmUpMinutes = 4.5f;
    c.overheatMinutes = 2.3f;
    c.coolDownMinutes = 20.0f;
    c.ambientC = -12.5f;
    c.ambientByMap = false;
    c.airFullKmh = 45.5f;
    c.damage = false;
    c.hotDamage = 22.5f;
    c.hotDamageFrom = 110.5f;
    c.coldDamage = 7.5f;
    c.stallDamage = 12.0f;
    c.clutchBurn = false;
    c.clutchHeat = 150.0f;
    c.springScale = 1.5f;
    c.dampingScale = 0.75f;
    c.extraTravel = 12.0f;
    c.frameFlex = 2.5f;
    c.twistRange = 0.5f;
    c.toggleKey = VK_F9;
    strcpy_s(c.padToggle, "RB+DPadUp");
    c.demo = true;
    c.probe = 1;
    c.keepLogs = 5;
    c.soundVolume = 80;
    c.soundsOnly = true;
    c.dieselFlutter = true;
    c.oilGauge = kOilDial;
    c.boostProfile = kSpoolTwoStage;
    c.gearbox = kGearboxModel;
    c.requireClutch = c.requireGearHeld = c.skipNeutral = c.downStaysForward = c.ignoreShifter = true;
    c.backToA = 12;
    c.input = kInputKeys;
    c.powertrain = true;
    c.roadLoad = false;
    c.roadCdA = 2.5f;
    c.engineShare = 80;
    c.creep = false;
    c.creepManual = true;
    c.oilWarmUpMinutes = 12.5f;
    c.oilWarnC = 132.0f;
    const bool allChanged = c.layout != d.layout && c.swapDials != d.swapDials && c.style != d.style && c.faceOpacity != d.faceOpacity && c.offsetX != d.offsetX && c.offsetY != d.offsetY &&
                            c.scale != d.scale && c.opacity != d.opacity && c.mph != d.mph && c.psi != d.psi && c.fahrenheit != d.fahrenheit &&
                            c.showTemp != d.showTemp && c.showBoost != d.showBoost && c.showAir != d.showAir && c.showClock != d.showClock && c.showFuelUse != d.showFuelUse && c.showLamps != d.showLamps &&
                            c.tachClass != d.tachClass && c.idleRpm != d.idleRpm && c.redlineRpm != d.redlineRpm && c.maxRpm != d.maxRpm &&
                            c.speedMax != d.speedMax && c.boostMax != d.boostMax && c.rollWarnDeg != d.rollWarnDeg && c.rollDangerDeg != d.rollDangerDeg &&
                            c.pitchDangerDeg != d.pitchDangerDeg && c.warmUpMinutes != d.warmUpMinutes && c.overheatMinutes != d.overheatMinutes &&
                            c.coolDownMinutes != d.coolDownMinutes && c.ambientC != d.ambientC && c.ambientByMap != d.ambientByMap && c.airFullKmh != d.airFullKmh && c.damage != d.damage &&
                            c.hotDamage != d.hotDamage && c.hotDamageFrom != d.hotDamageFrom && c.coldDamage != d.coldDamage && c.stallDamage != d.stallDamage && c.clutchBurn != d.clutchBurn && c.clutchHeat != d.clutchHeat && c.springScale != d.springScale && c.dampingScale != d.dampingScale && c.extraTravel != d.extraTravel && c.frameFlex != d.frameFlex && c.twistRange != d.twistRange && c.toggleKey != d.toggleKey && strcmp(c.padToggle, d.padToggle) &&
                            c.demo != d.demo && c.probe != d.probe && c.keepLogs != d.keepLogs && c.soundVolume != d.soundVolume && c.soundsOnly != d.soundsOnly && c.dieselFlutter != d.dieselFlutter &&
                            c.oilGauge != d.oilGauge && c.boostProfile != d.boostProfile && c.oilWarmUpMinutes != d.oilWarmUpMinutes && c.oilWarnC != d.oilWarnC &&
                            c.gearbox != d.gearbox && c.requireClutch != d.requireClutch && c.requireGearHeld != d.requireGearHeld && c.skipNeutral != d.skipNeutral && c.downStaysForward != d.downStaysForward && c.ignoreShifter != d.ignoreShifter && c.backToA != d.backToA && c.input != d.input && c.powertrain != d.powertrain && c.roadLoad != d.roadLoad && c.roadCdA != d.roadCdA && c.engineShare != d.engineShare && c.creep != d.creep && c.creepManual != d.creepManual;
    WriteIni(c);
    ReadIni(b);
    Check(allChanged, "ini: the test changes every setting from its default");
    Check(SameSettings(b, c), "ini: every key written reads back the same");
    wchar_t later[16] = {}, other[16] = {}, first[80] = {};
    GetPrivateProfileStringW(kIniSection, L"FromALaterVersion", L"", later, 16, ini.c_str());
    GetPrivateProfileStringW(L"Other", L"Kept", L"", other, 16, ini.c_str());
    FILE *f = nullptr;
    int comments = 0;
    if (_wfopen_s(&f, ini.c_str(), L"r, ccs=UTF-16LE") == 0 && f)
    {
        wchar_t line[400];
        for (int i = 0; fgetws(line, 400, f); i++)
        {
            if (i == 0) wcsncpy_s(first, line, _TRUNCATE);
            if (line[0] == L';') comments++;
        }
        fclose(f);
    }
    Check(!wcscmp(later, L"7") && !wcscmp(other, L"yes"), "ini: keys the mod does not know survive a write");
    Check(!wcsncmp(first, L"; Gauges:", 9) && comments == 128, "ini: the help text survives a write (128 comment lines)");
    // the Gameplay tab's open tab: read and written on its own, kept through a write of the settings and of the defaults
    {
        char none[32] = "x", tab[32] = {}, kept[32] = {};
        PageTabRead(none, sizeof none);
        IniTabPut("power", ini);
        PageTabRead(tab, sizeof tab);
        WriteIni(c);
        WriteIni(d);
        PageTabRead(kept, sizeof kept);
        Check(!none[0] && !strcmp(tab, "power") && !strcmp(kept, "power"), "ini: Tab reads \"\" when not set, then the id written; a write of the settings or the defaults keeps it");
    }
    // the HUD layouts (BRIEF_cluster_hud_layouts.md): 2 and 3 load and save, 4 is held to the strip, and Style and
    // FaceOpacity survive a trip through Spotter dials
    {
        GaugeSettings h = c, r;
        bool both = true;
        for (int layout = kLayoutHudDials; layout <= kLayoutHudDigital; layout++)
        {
            h.layout = layout;
            WriteIni(h);
            ReadIni(r);
            both &= r.layout == layout && r.style == c.style && r.faceOpacity == c.faceOpacity;
        }
        h.layout = kLayoutBlock;
        WriteIni(h);
        ReadIni(r);
        const bool back = r.layout == kLayoutBlock && r.style == kFaceTinted && r.faceOpacity == 0.6f;
        WritePrivateProfileStringW(kIniSection, L"Layout", L"4", ini.c_str());
        ReadIni(r);
        Check(both && back && r.layout == kLayoutStrip, "ini: Layout 2 and 3 load and save with Style and FaceOpacity kept for a way back; 4 is held to the strip");
        WriteIni(c);
    }

    // what is no number gives the default, what is out of range is held to it
    WritePrivateProfileStringW(kIniSection, L"Scale", L"nan", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"Opacity", L"inf", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"OffsetX", L"abc", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"FaceOpacity", L"5", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"Layout", L"9", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"Style", L"-3", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"TachClass", L"12", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"IdleRpm", L"50", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"MaxRpm", L"-200", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"ToggleKey", L"4000", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"RollWarnDeg", L"40", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"RollDangerDeg", L"10", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"PitchDangerDeg", L"400", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"WarmUpMinutes", L"0", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"OverheatMinutes", L"1000", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"CoolDownMinutes", L"-5", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"AmbientC", L"nan", ini.c_str());
    ReadIni(b);
    Check(b.scale == d.scale && b.opacity == d.opacity, "ini: \"nan\" and \"inf\" give the defaults");
    Check(b.offsetX == 0.0f && b.faceOpacity == kFaceOpacityMax && b.layout == kLayoutStrip && b.style == kFaceOpen && b.tachClass == kClassTruck &&
          b.idleRpm == kIdleRange[0] && b.maxRpm == 0.0f && b.toggleKey == 0, "ini: values out of range are held to it");
    Check(b.rollWarnDeg == 40.0f && b.rollDangerDeg == 40.0f && b.pitchDangerDeg == kTiltRange[1], "ini: the tilt lamp's red never comes before its amber, and no angle passes 89 degrees");
    Check(b.warmUpMinutes == kWarmUpRange[0] && b.overheatMinutes == kOverheatRange[1] && b.coolDownMinutes == kCoolDownRange[0] && b.ambientC == d.ambientC,
          "ini: the coolant's three times are held to their ranges, and an outside temperature that is no number gives the default");
    WritePrivateProfileStringW(kIniSection, L"AmbientC", L"-90", ini.c_str());
    ReadIni(b);
    const float coldest = b.ambientC;
    WritePrivateProfileStringW(kIniSection, L"AmbientC", L"70", ini.c_str());
    ReadIni(b);
    Check(coldest == kAmbientRange[0] && b.ambientC == kAmbientRange[1], "ini: the outside temperature stays between 40 below and 50 C");
    WritePrivateProfileStringW(kIniSection, L"AirFullKmh", L"5", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"HotDamagePercentPerMinute", L"500", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"ColdDamagePercentPerMinute", L"-3", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"Damage", L"0", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"HotDamageFrom", L"90", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"StallDamagePercent", L"40", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"ClutchHeatPercent", L"5", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"SpringScale", L"9", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"FrameFlex", L"20", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"TwistRange", L"0.01", ini.c_str());
    ReadIni(b);
    const bool held = b.airFullKmh == kAirFullRange[0] && b.hotDamage == kDamageRange[1] && b.coldDamage == 0.0f && !b.damage && b.hotDamageFrom == kHotDamageFromRange[0] &&
                      b.stallDamage == kStallDamageRange[1] && b.clutchHeat == kClutchHeatRange[0] && b.springScale == kSuspScaleRange[1] && b.frameFlex == kFrameFlexRange[1] &&
                      b.twistRange == kTwistRange[0];
    WritePrivateProfileStringW(kIniSection, L"SpringScale", L"1", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"FrameFlex", L"1", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"TwistRange", L"1", ini.c_str());
    // an old ini's TravelMinScale and TravelMaxScale are no longer read: ExtraTravel stays as it was, and a write drops neither
    {
        WritePrivateProfileStringW(kIniSection, L"ExtraTravel", nullptr, ini.c_str()); // the round trip above left one
        WritePrivateProfileStringW(kIniSection, L"TravelMinScale", L"0.6", ini.c_str());
        WritePrivateProfileStringW(kIniSection, L"TravelMaxScale", L"2", ini.c_str());
        GaugeSettings old;
        SettingsDefaults(old);
        ReadIni(old);
        const bool unread = old.extraTravel == 0.0f;
        WritePrivateProfileStringW(kIniSection, L"ExtraTravel", L"45", ini.c_str());
        ReadIni(old);
        const bool capped = old.extraTravel == kExtraTravelRange[1];
        WritePrivateProfileStringW(kIniSection, L"ExtraTravel", nullptr, ini.c_str());
        WritePrivateProfileStringW(kIniSection, L"TravelMinScale", nullptr, ini.c_str());
        WritePrivateProfileStringW(kIniSection, L"TravelMaxScale", nullptr, ini.c_str());
        Check(unread && capped, "ini: an old TravelMinScale and TravelMaxScale are no longer read; ExtraTravel is held to 0 to 30 cm");
    }
    // the sway bars are held to 0.3 (more swings) and on 0.01; the ride height on whole cm, near 0 exactly 0 and "0 cm"
    {
        WritePrivateProfileStringW(kIniSection, L"SwayFront", L"1.5", ini.c_str());
        WritePrivateProfileStringW(kIniSection, L"SwayRear", L"0.123", ini.c_str());
        GaugeSettings sw;
        SettingsDefaults(sw);
        ReadIni(sw);
        const bool swayHeld = sw.swayFront == kSwayRange[1] && fabsf(sw.swayRear - 0.12f) < 1e-6f;
        WritePrivateProfileStringW(kIniSection, L"SwayFront", nullptr, ini.c_str());
        WritePrivateProfileStringW(kIniSection, L"SwayRear", nullptr, ini.c_str());
        bool zero = true;
        for (float v : { -0.0001f, 0.0f, 0.0001f, 0.5f * kRideStep, -0.5f * kRideStep })
        {
            const float r = SettingsStep(v, kRideStep);
            char text[16];
            snprintf(text, sizeof text, RideFormat(r), r);
            zero &= r == 0.0f && !signbit(r) && !signbit(r / 100.0f) && strcmp(text, "0 cm") == 0;
        }
        char one[16];
        const float up = SettingsStep(0.51f, kRideStep), down = SettingsStep(-2.6f, kRideStep);
        snprintf(one, sizeof one, RideFormat(up), up);
        Check(swayHeld && zero && up == 1.0f && down == -3.0f && strcmp(one, "+1 cm") == 0,
              "settings: SwayFront 1.5 is held to 0.3 and 0.123 goes to 0.12; a ride height within half a step of 0 (-0.0001, 0, 0.0001, "
              "+/-0.5) is exactly 0, shown \"0 cm\" and written as 0; 0.51 is +1 cm and -2.6 is -3");
    }
    WritePrivateProfileStringW(kIniSection, L"StallDamagePercent", L"5", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"ClutchHeatPercent", L"60", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"HotDamageFrom", L"130", ini.c_str());
    ReadIni(b);
    const bool fromHigh = b.hotDamageFrom == kHotDamageFromRange[1];
    WritePrivateProfileStringW(kIniSection, L"HotDamageFrom", L"118", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"AirFullKmh", L"200", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"HotDamagePercentPerMinute", L"nan", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"Damage", L"7", ini.c_str());
    ReadIni(b);
    Check(held && fromHigh && b.airFullKmh == kAirFullRange[1] && b.hotDamage == d.hotDamage && b.damage,
          "ini: the radiator's speed stays between 10 and 80 km/h, the damage rates between 0 and 100, where the hot damage starts between 100 and 124 C, "
          "and a rate that is no number gives the default");
    WritePrivateProfileStringW(kIniSection, L"OilWarmUpMinutes", L"0", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"OilWarnC", L"400", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"OilGauge", L"5", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"BoostProfile", L"-1", ini.c_str());
    WritePrivateProfileStringW(kIniSection, L"Gearbox", L"7", ini.c_str());
    ReadIni(b);
    Check(b.oilWarmUpMinutes == kOilWarmUpRange[0] && b.oilWarnC == kOilWarnRange[1] && b.oilGauge == kOilNeedle && b.boostProfile == kSpoolStandard &&
              b.gearbox == kGearboxGame,
          "ini: the oil's warm-up and warning temperature are held to their ranges, and an oil gauge, a turbo or a gearbox the mod does not have gives the default");

    // the scales in force
    GaugeScale k = GaugeScaleOf(d);
    Check(k.idle == 650.0f && k.redline == 2200.0f && k.max == 3000.0f && k.speedMax == 100.0f, "scales: the truck class by default");
    a = d;
    a.tachClass = kClassScout;
    k = GaugeScaleOf(a);
    Check(k.idle == 800.0f && k.redline == 5500.0f && k.max == 7000.0f && k.speedMax == 120.0f, "scales: the scout class");
    {
        // a class change: a number at the old class's own follows the new class, one set by hand stays, the class's stays the class's
        GaugeSettings sc = d;
        sc.tachClass = kClassTruck;
        sc.idleRpm = 650.0f;   // the truck class's own, written as a number
        sc.redlineRpm = 2600.0f; // set by hand
        sc.maxRpm = 0.0f;      // the class's
        sc.speedMax = 90.0f;   // set by hand
        sc.boostMax = 1.8f;    // the truck class's own
        sc.tachClass = kClassScout;
        ScalesClassChange(sc, kClassTruck);
        const GaugeScale ks = GaugeScaleOf(sc);
        Check(sc.idleRpm == 0.0f && sc.redlineRpm == 2600.0f && sc.maxRpm == 0.0f && sc.speedMax == 90.0f && sc.boostMax == 0.0f && ks.idle == 800.0f &&
                  ks.max == 7000.0f && ks.boost == 1.0f,
              "scales: on a class change the numbers set by hand stay, the ones at the old class's own follow the new class");
    }
    a.tachClass = kClassHeavy;
    a.maxRpm = 2750.0f;
    a.redlineRpm = 4000.0f;
    a.idleRpm = 2900.0f;
    a.speedMax = 65.0f;
    k = GaugeScaleOf(a);
    Check(k.max == 3000.0f && k.redline == 3000.0f && k.idle == 2400.0f && k.speedMax == 65.0f,
          "scales: own numbers replace the class's, the top rounds up to a whole step, the red zone and idle fit under it");
    GaugeState st;
    a = d;
    a.mph = true;
    GaugeDemo(st, a);
    Check(st.engineOn && st.rpm > 650.0f && st.rpm < 2200.0f && st.rpmMax == 3000.0f && st.speedMax == 100.0f && st.mph && !st.psi && st.gearMode &&
          !strcmp(st.gearMode, "A"), "demo: a cruising truck on the scales and units of the settings");

    // a truck's own scales, in its file in Gauges\trucks: one state, as the mod's thread keeps it
    {
        auto truckFile = [](const char *key) { return SetupFile(kSetupTruck, key); };
        auto truckPut = [&](const char *key, const wchar_t *name, const wchar_t *value) {
            SetupFolderMake(kSetupTruck);
            WritePrivateProfileStringW(L"Truck", name, value, truckFile(key).c_str());
        };
        for (const char *key : { "someone_elses", "mod_suv/1000001", "chevrolet_ck1500", "half", "wild" }) DeleteFileW(truckFile(key).c_str());
        DeleteFileW(ini.c_str());
        SettingsState state = {};
        ReadIni(state.plain);
        state.plain.tachClass = kClassScout;
        state.plain.redlineRpm = 5000.0f;
        WriteIni(state.plain);
        // a truck this test never drives, and a truck's section with something in it that the mod does not know
        truckPut("someone_elses", L"RedlineRpm", L"1800");
        truckPut("mod_suv/1000001", L"FromALaterVersion", L"7");
        SettingsFor(state, "");
        const bool none = !state.own && SameSettings(state.s, state.plain);
        GaugeSettings in = state.s;
        in.maxRpm = 6000.0f;
        SettingsTake(state, in);
        const bool plainEdit = !state.own && !state.changed && state.plain.maxRpm == 6000.0f;
        SettingsFor(state, "mod_suv/1000001");
        Check(none && plainEdit && !state.own && SameSettings(state.s, state.plain), "trucks: with no truck driven the tab sets the plain scales, and a truck without scales of its own shows them");
        in = state.s;
        in.opacity = 0.5f;
        SettingsTake(state, in);
        Check(!state.own && state.plain.opacity == 0.5f && SameSettings(state.s, state.plain), "trucks: a setting that is no scale is every truck's, whichever truck is driven");
        in = state.s;
        in.redlineRpm = 4500.0f;
        SettingsTake(state, in);
        Check(state.own && state.changed && state.s.redlineRpm == 4500.0f && state.s.tachClass == kClassScout && state.plain.redlineRpm == 5000.0f,
              "trucks: a scale changed while a truck is driven makes the scales that truck's own and leaves the plain ones as they were");
        SettingsWrite(state);
        // a second truck, set up another way; then a setting that is no scale changed while it is driven
        SettingsFor(state, "chevrolet_ck1500");
        const bool ckPlain = !state.own && state.s.redlineRpm == 5000.0f && state.s.maxRpm == 6000.0f;
        in = state.s;
        in.tachClass = kClassHeavy;
        in.speedMax = 70.0f;
        in.boostMax = 1.45f;
        in.boostProfile = kSpoolLaggy;
        SettingsTake(state, in);
        in = state.s;
        in.scale = 1.5f;
        SettingsTake(state, in);
        SettingsWrite(state);
        Check(ckPlain && state.own && state.plain.scale == 1.5f && state.plain.tachClass == kClassScout && state.plain.speedMax == 0.0f && state.plain.boostMax == 0.0f &&
              state.plain.boostProfile == kSpoolStandard && state.plain.redlineRpm == 5000.0f,
              "trucks: a second truck starts from the plain scales, and what is changed beside its scales still goes to every truck");
        // the game is started again: [Gauges] holds the plain settings, each truck its own scales
        SettingsState next = {};
        ReadIni(next.plain);
        SettingsFor(next, "mod_suv/1000001");
        const bool moddedBack = next.own && next.s.redlineRpm == 4500.0f && next.s.tachClass == kClassScout && next.s.maxRpm == 6000.0f && next.s.speedMax == 0.0f && next.s.boostMax == 0.0f && next.s.scale == 1.5f;
        SettingsFor(next, "chevrolet_ck1500");
        Check(SameSettings(next.plain, state.plain) && moddedBack && next.own && next.s.tachClass == kClassHeavy && next.s.speedMax == 70.0f && next.s.boostMax == 1.45f &&
              GaugeScaleOf(next.s).boost == 1.45f && next.s.boostProfile == kSpoolLaggy && next.s.redlineRpm == 5000.0f,
              "trucks: after a new start [Gauges] reads back as the plain settings and each truck has its own scales again");
        wchar_t kept[16] = {}, later2[16] = {};
        GetPrivateProfileStringW(L"Truck", L"RedlineRpm", L"", kept, 16, truckFile("someone_elses").c_str());
        GetPrivateProfileStringW(L"Truck", L"FromALaterVersion", L"", later2, 16, truckFile("mod_suv/1000001").c_str());
        Check(!wcscmp(kept, L"1800") && !wcscmp(later2, L"7"), "trucks: the file of a truck that was not driven, and a key the mod does not know in a truck's file, survive the writes");
        // a section that holds one of the seven takes the others from [Gauges]; what is out of range is held to it
        truckPut("half", L"MaxRpm", L"4000");
        truckPut("wild", L"RedlineRpm", L"50");
        truckPut("wild", L"TachClass", L"9");
        truckPut("wild", L"IdleRpm", L"nan");
        truckPut("wild", L"BoostProfile", L"9");
        SettingsFor(next, "wild");
        const bool wild = next.own && next.s.redlineRpm == kRedlineRange[0] && next.s.tachClass == kClassTruck && next.s.idleRpm == next.plain.idleRpm &&
                          next.s.boostProfile == kSpoolStandard;
        SettingsFor(next, "half");
        Check(wild && next.own && !next.changed && next.s.maxRpm == 4000.0f && next.s.redlineRpm == 5000.0f && next.s.tachClass == kClassScout,
              "trucks: a file takes from [Gauges] what it leaves out, and its values are held to their ranges as those of [Gauges] are");
        // a save after a change of something else leaves such a section as it is; after a change of a scale it has all seven
        wchar_t halfRed[16] = {};
        in = next.s;
        in.opacity = 0.75f;
        SettingsTake(next, in);
        SettingsWrite(next);
        GetPrivateProfileStringW(L"Truck", L"RedlineRpm", L"", halfRed, 16, truckFile("half").c_str());
        const bool leftAlone = next.own && !halfRed[0];
        in = next.s;
        in.idleRpm = 900.0f;
        SettingsTake(next, in);
        SettingsWrite(next);
        GetPrivateProfileStringW(L"Truck", L"RedlineRpm", L"", halfRed, 16, truckFile("half").c_str());
        wchar_t halfTurbo[16] = {};
        GetPrivateProfileStringW(L"Truck", L"BoostProfile", L"", halfTurbo, 16, truckFile("half").c_str());
        Check(leftAlone && !wcscmp(halfRed, L"5000") && !wcscmp(halfTurbo, L"0") && !next.changed,
              "trucks: a save leaves a truck's file as it is until one of its scales is changed on the tab, and then writes all seven");
        // the truck gives its scales up: they go out of the ini, what else its section holds stays, and a section left empty goes
        SettingsFor(next, "mod_suv/1000001");
        SettingsDrop(next);
        const bool dropped = !next.own && SameSettings(next.s, next.plain);
        SettingsFor(next, "mod_suv/1000001");
        GetPrivateProfileStringW(L"Truck", L"FromALaterVersion", L"", later2, 16, truckFile("mod_suv/1000001").c_str());
        const bool moddedPlain = !next.own && SameSettings(next.s, next.plain) && !wcscmp(later2, L"7");
        SettingsFor(next, "chevrolet_ck1500");
        SettingsDrop(next);
        wchar_t rest[64] = {};
        const DWORD left = GetPrivateProfileSectionW(L"Truck", rest, 64, truckFile("chevrolet_ck1500").c_str());
        const bool ckSection = GetFileAttributesW(truckFile("chevrolet_ck1500").c_str()) != INVALID_FILE_ATTRIBUTES;
        Check(dropped && moddedPlain && !next.own && !left && !ckSection,
              "trucks: a truck that gives up its scales takes the plain ones, its file keeps what else it holds, and an empty file goes");
        // the defaults: the plain settings and the driven truck's own scales; another truck keeps its own
        SettingsFor(next, "half");
        const bool halfOwn = next.own;
        SettingsToDefaults(next);
        SettingsWrite(next);
        GaugeSettings defaults, fromIni;
        SettingsDefaults(defaults);
        ReadIni(fromIni);
        wchar_t halfMax[16] = {};
        GetPrivateProfileStringW(L"Truck", L"MaxRpm", L"", halfMax, 16, truckFile("half").c_str());
        GetPrivateProfileStringW(L"Truck", L"RedlineRpm", L"", kept, 16, truckFile("someone_elses").c_str());
        Check(halfOwn && !next.own && SameSettings(next.s, defaults) && SameSettings(fromIni, defaults) && !halfMax[0] && !wcscmp(kept, L"1800"),
              "trucks: the defaults take the plain settings and the driven truck's own scales, and leave the other trucks' files as they were");
        // a truck kind's own suspension (BRIEF_suspension_per_truck.md step 1): [Suspension] beside [Truck] in its file
        {
            for (const char *key : { "susp_hx", "susp_ww", "susp_old" }) DeleteFileW(truckFile(key).c_str());
            DeleteFileW(ini.c_str());
            SettingsState su = {};
            ReadIni(su.plain);
            su.plain.springScale = 1.25f;
            WriteIni(su.plain);
            // a kind with no section uses the default; with no truck driven a change is the default's
            SettingsFor(su, "susp_hx");
            const bool hxPlain = !su.suspOwn && !su.suspKept && su.s.springScale == 1.25f;
            SettingsFor(su, "");
            GaugeSettings ed = su.s;
            ed.dampingScale = 1.5f;
            SettingsTake(su, ed);
            const bool noTruckDefault = !su.suspOwn && su.plain.dampingScale == 1.5f;
            // a change while a kind is driven makes the whole set its own, the bump keys with it; the default stays
            SettingsFor(su, "susp_hx");
            ed = su.s;
            ed.springScale = 2.0f;
            ed.bumpStops = true;
            ed.bumpZone = 0.12f;
            ed.bumpForce = 3.0f;
            ed.bumpDamping = 2.5f;
            SettingsTake(su, ed);
            SettingsWrite(su);
            wchar_t own[8] = {}, zone[16] = {};
            GetPrivateProfileStringW(L"Suspension", L"Own", L"", own, 8, truckFile("susp_hx").c_str());
            GetPrivateProfileStringW(L"Suspension", L"BumpZone", L"", zone, 16, truckFile("susp_hx").c_str());
            GaugeSettings fromIni2;
            ReadIni(fromIni2);
            Check(hxPlain && noTruckDefault && su.suspOwn && !wcscmp(own, L"1") && !wcscmp(zone, L"0.12") && su.plain.springScale == 1.25f &&
                      fromIni2.springScale == 1.25f && fromIni2.dampingScale == 1.5f && !fromIni2.bumpStops,
                  "suspension: a kind with no section uses the default, a change while it is driven makes the whole set its own with the bump keys, and the default stays");
            // two kinds in turn each get their own; A, then B, then A again gives A's set over the default, no climb
            SettingsFor(su, "susp_ww");
            ed = su.s;
            const bool wwStartsPlain = !su.suspOwn && ed.springScale == 1.25f && !ed.bumpStops;
            ed.springScale = 0.75f;
            SettingsTake(su, ed);
            SettingsWrite(su);
            SettingsFor(su, "susp_hx");
            const GaugeSettings hxFirst = su.s;
            SettingsFor(su, "susp_ww");
            const bool wwOwn = su.suspOwn && su.s.springScale == 0.75f && !su.s.bumpStops;
            SettingsFor(su, "susp_hx");
            Check(wwStartsPlain && wwOwn && su.suspOwn && su.s.springScale == 2.0f && su.s.bumpForce == 3.0f && SuspSame(su.s, hxFirst) &&
                      su.s.dampingScale == 1.5f,
                  "suspension: two kinds in turn each get their own set, and A to B to A gives A's set as it was, on the default, not on B's");
            // the box off keeps the values with Own=0 and the default is in force; on again brings them back
            SettingsSuspOwn(su, false);
            GetPrivateProfileStringW(L"Suspension", L"Own", L"", own, 8, truckFile("susp_hx").c_str());
            const bool offDefault = !su.suspOwn && su.suspKept && su.s.springScale == 1.25f && !su.s.bumpStops && !wcscmp(own, L"0");
            SettingsFor(su, "susp_hx");
            const bool offAfterReload = !su.suspOwn && su.suspKept && su.s.springScale == 1.25f;
            SettingsSuspOwn(su, true);
            const bool back = su.suspOwn && !su.suspKept && su.s.springScale == 2.0f && su.s.bumpZone == 0.12f;
            SettingsFor(su, "susp_hx");
            Check(offDefault && offAfterReload && back && su.suspOwn && su.s.springScale == 2.0f,
                  "suspension: the box off keeps the set with Own=0 and the default in force, on again brings the set back");
            // a change while the set is kept edits the default: the file's set and Own=0 stay, [Gauges] takes it (review F1)
            SettingsSuspOwn(su, false);
            ed = su.s;
            ed.springScale = 0.5f;
            SettingsTake(su, ed);
            SettingsWrite(su);
            wchar_t keptSpring[16] = {};
            GetPrivateProfileStringW(L"Suspension", L"Own", L"", own, 8, truckFile("susp_hx").c_str());
            GetPrivateProfileStringW(L"Suspension", L"SpringScale", L"", keptSpring, 16, truckFile("susp_hx").c_str());
            GaugeSettings fromIniKept;
            ReadIni(fromIniKept);
            const bool keptStays = !su.suspOwn && su.suspKept && !wcscmp(own, L"0") && !wcscmp(keptSpring, L"2") && fromIniKept.springScale == 0.5f && su.plain.springScale == 0.5f;
            SettingsSuspOwn(su, true);
            Check(keptStays && su.suspOwn && su.s.springScale == 2.0f && su.plain.springScale == 0.5f,
                  "suspension: a change while the set is kept edits the default, the box stays off, and the file's set and Own=0 stay as they were");
            ed = su.plain;
            ed.springScale = 1.25f;
            su.plain = ed;
            WriteIni(su.plain);
            SettingsFor(su, "susp_hx");
            // the box on with nothing kept makes the default shown the kind's own
            DeleteFileW(truckFile("susp_old").c_str());
            SettingsFor(su, "susp_old");
            SettingsSuspOwn(su, true);
            GetPrivateProfileStringW(L"Suspension", L"Own", L"", own, 8, truckFile("susp_old").c_str());
            const bool onFresh = su.suspOwn && su.s.springScale == 1.25f && !wcscmp(own, L"1");
            DeleteFileW(truckFile("susp_old").c_str());
            // "Make these the default": Gauges.ini takes the truck's set, the truck keeps its own
            SettingsFor(su, "susp_hx");
            SettingsSuspToDefault(su);
            SettingsWrite(su);
            GaugeSettings fromIni3;
            ReadIni(fromIni3);
            SettingsFor(su, "susp_hx");
            GetPrivateProfileStringW(L"Suspension", L"Own", L"", own, 8, truckFile("susp_hx").c_str());
            Check(onFresh && fromIni3.springScale == 2.0f && fromIni3.bumpStops && fromIni3.bumpZone == 0.12f && su.suspOwn && su.s.springScale == 2.0f && !wcscmp(own, L"1"),
                  "suspension: the box on with nothing kept takes the default shown, and Make these the default writes Gauges.ini and leaves the truck's own");
            // giving up the scales leaves the suspension and its file; the defaults button keeps the set with Own=0
            ed = su.s;
            ed.redlineRpm = 3900.0f;
            SettingsTake(su, ed);
            SettingsWrite(su);
            SettingsDrop(su);
            const bool fileStays = GetFileAttributesW(truckFile("susp_hx").c_str()) != INVALID_FILE_ATTRIBUTES;
            SettingsFor(su, "susp_hx");
            const bool suspStays = fileStays && !su.own && su.suspOwn && su.s.springScale == 2.0f;
            SettingsToDefaults(su);
            SettingsWrite(su);
            SettingsFor(su, "susp_hx");
            GaugeSettings defaults2;
            SettingsDefaults(defaults2);
            Check(suspStays && !su.suspOwn && su.suspKept && SuspSame(su.s, defaults2),
                  "suspension: the truck giving up its scales keeps its suspension and its file, and the defaults keep the set with Own=0");
            // a section written by hand without Own is in force; a value out of range is held as [Gauges]' are
            WritePrivateProfileStringW(L"Suspension", L"SpringScale", L"9", truckFile("susp_old").c_str());
            SettingsFor(su, "susp_old");
            Check(su.suspOwn && su.s.springScale == kSuspScaleRange[1], "suspension: a section without Own is in force, its values held to their ranges");
            DeleteFileW(truckFile("susp_old").c_str());
            // a pull learned on the mod's thread while the tab writes the suspension into the same file: neither is lost
            struct Both { GaugeSettings s; } both = { su.s };
            both.s.springScale = 1.75f;
            HANDLE writers[2];
            writers[0] = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                for (int k = 0; k < 200; k++) GearBaseWrite("susp_ww", "e_test", 1 + k % 8, 1.0f + k * 0.01f);
                return 0;
            }, nullptr, 0, nullptr);
            writers[1] = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
                for (int k = 0; k < 200; k++) WriteTruckSusp("susp_ww", ((Both *)p)->s, true);
                return 0;
            }, &both, 0, nullptr);
            WaitForMultipleObjects(2, writers, TRUE, INFINITE);
            CloseHandle(writers[0]);
            CloseHandle(writers[1]);
            float bases[kMaxGears] = {};
            const int learned = GearBaseRead("susp_ww", "e_test", bases);
            SettingsFor(su, "susp_ww");
            Check(learned == 8 && fabsf(bases[8] - 2.99f) < 0.001f && su.suspOwn && su.s.springScale == 1.75f,
                  "suspension: the learned pulls and the tab's suspension written into one file at once both stay");
            // the wheels' drag mark (the Reviewer's point 5): old figures for gears 2 and 3 without BaseDamp, the read drops
            // both and marks; gear 2 learned again; read again: gear 2 the new one, gear 3 is 0. A truck with no figures
            // gets no file from the read.
            {
                const std::wstring oldFile = truckFile("base_old");
                DeleteFileW(oldFile.c_str());
                SetupBegin(kSetupTruck, L"base_old");
                WritePrivateProfileStringW(kSetupSections[kSetupTruck], L"Base2.e_old", L"1.500", oldFile.c_str());
                WritePrivateProfileStringW(kSetupSections[kSetupTruck], L"Base3.e_old", L"1.700", oldFile.c_str());
                WritePrivateProfileStringW(kSetupSections[kSetupTruck], L"Base3.e_other", L"1.900", oldFile.c_str());
                float b1[kMaxGears] = {}, b2[kMaxGears] = {};
                int dropped1 = -1, dropped2 = -1;
                const int first = GearBaseRead("base_old", "e_old", b1, &dropped1);
                wchar_t left[24] = {};
                const bool gone = GetPrivateProfileStringW(kSetupSections[kSetupTruck], L"Base2.e_old", L"", left, 24, oldFile.c_str()) == 0 &&
                                  GetPrivateProfileStringW(kSetupSections[kSetupTruck], L"Base3.e_old", L"", left, 24, oldFile.c_str()) == 0 &&
                                  GearBaseMarked("base_old", "e_old") && !GearBaseMarked("base_old", "e_other") &&
                                  GetPrivateProfileStringW(kSetupSections[kSetupTruck], L"Base3.e_other", L"", left, 24, oldFile.c_str()) > 0 && !wcscmp(left, L"1.900");
                GearBaseWrite("base_old", "e_old", 2, 1.8f);
                const int second = GearBaseRead("base_old", "e_old", b2, &dropped2);
                float b3[kMaxGears] = {};
                int dropped3 = -1;
                const std::wstring noneFile = truckFile("base_none");
                DeleteFileW(noneFile.c_str());
                const int none = GearBaseRead("base_none", "e_old", b3, &dropped3);
                const bool noFile = GetFileAttributesW(noneFile.c_str()) == INVALID_FILE_ATTRIBUTES;
                Check(first == 0 && dropped1 == 2 && b1[2] == 0.0f && b1[3] == 0.0f && gone && second == 1 && dropped2 == 0 && fabsf(b2[2] - 1.8f) < 0.001f && b2[3] == 0.0f &&
                          none == 0 && dropped3 == 0 && noFile,
                      "road load: figures without BaseDamp are dropped and the mark written at the read; one learned again is read, the one not learned again is 0; another engine's keys stay");
                DeleteFileW(oldFile.c_str());
            }
            // an old Gauges.ini: no truck file has [Suspension], so every truck has the values of [Gauges] as before
            for (const char *key : { "susp_hx", "susp_ww" }) DeleteFileW(truckFile(key).c_str());
            SettingsFor(su, "susp_hx");
            Check(!su.suspOwn && !su.suspKept && SameSettings(su.s, su.plain), "suspension: with no [Suspension] in a truck's file the truck has every value of [Gauges]");
            DeleteFileW(ini.c_str());
            // step 2: the owned truck's level, [Suspension:<id>] in the kind's file, by the game's id of the truck (made up ids)
            {
                const char *kind = "susp_own", *id1 = "{11111111-1111-1111-1111-111111111111}", *id2 = "{22222222-2222-2222-2222-222222222222}",
                           *id3 = "{33333333-3333-3333-3333-333333333333}", *other = "susp_other", *idB = "{BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB}";
                DeleteFileW(truckFile(kind).c_str());
                ReadIni(su.plain);
                // the kind's own set, then this truck only: the truck's level in force, the kind's stays for the others
                SettingsFor(su, kind, id1);
                ed = su.s;
                ed.springScale = 1.5f;
                SettingsTake(su, ed);
                SettingsWrite(su);
                const bool kindFirst = SuspLevel(su) == kSuspLevelKind && su.suspOwn && !su.truckOwn;
                SettingsSuspLevel(su, true);
                ed = su.s;
                ed.springScale = 2.5f;
                ed.bumpStops = true;
                SettingsTake(su, ed);
                SettingsWrite(su);
                SettingsFor(su, kind, id1);
                const bool truckLevel = SuspLevel(su) == kSuspLevelTruck && su.s.springScale == 2.5f && su.s.bumpStops;
                SettingsFor(su, kind, id3);
                const bool othersKind = SuspLevel(su) == kSuspLevelKind && su.s.springScale == 1.5f && !su.s.bumpStops;
                Check(kindFirst && truckLevel && othersKind,
                      "suspension: This truck only puts the set at the truck's level by its id, in force for it, and the kind's stays for another truck of the kind");
                // a new id takes the kind's level (a truck out of the warehouse), and A to B to A gives A's set from the default, no climb
                SettingsFor(su, kind, id1);
                const GaugeSettings aFirst = su.s;
                SettingsFor(su, other, idB);
                const bool bPlain = SuspLevel(su) == kSuspLevelDefault && su.s.springScale == su.plain.springScale;
                SettingsFor(su, kind, id1);
                Check(bPlain && SuspSame(su.s, aFirst) && su.s.springScale == 2.5f,
                      "suspension: A to B to A gives the truck's own set as it was, on the default, not on B's");
                // the take-over: gone, back as the same kind under a new id within 20 s with none between
                SuspTracker t = {};
                TruckIdentity a1 = {}, a2 = {}, none = {}, b1 = {}, a3 = {};
                strcpy_s(a1.key, kind); strcpy_s(a1.id, id1);
                strcpy_s(a2.key, kind); strcpy_s(a2.id, id2);
                strcpy_s(a3.key, kind); strcpy_s(a3.id, id3);
                strcpy_s(b1.key, other); strcpy_s(b1.id, idB);
                char gone[40];
                const bool away = !SuspTrack(t, a1, none, 1000, gone);
                const bool due = SuspTrack(t, none, a2, 6000, gone) && !strcmp(gone, id1);
                const bool copied = TruckSuspTakeOver(kind, gone, id2);
                SettingsFor(su, kind, id2);
                const bool takenOver = SuspLevel(su) == kSuspLevelTruck && su.s.springScale == 2.5f && su.s.bumpStops;
                SettingsFor(su, kind, id1);
                const bool oldKept = SuspLevel(su) == kSuspLevelTruck && su.s.springScale == 2.5f;
                Check(away && due && copied && takenOver && oldKept,
                      "suspension: the take-over: the same kind back under a new id 5 s after it went takes the set over, and the old id keeps its own");
                // a switch to another truck and back is none, nor a gap over 20 s, nor the same id back
                SuspTracker u = {};
                SuspTrack(u, a1, none, 0, gone);
                SuspTrack(u, none, b1, 2000, gone);
                SuspTrack(u, b1, none, 3000, gone);
                const bool viaB = !SuspTrack(u, none, a2, 4000, gone);
                SuspTracker w = {};
                SuspTrack(w, a1, none, 0, gone);
                const bool late = !SuspTrack(w, none, a2, 25000, gone);
                SuspTracker x = {};
                SuspTrack(x, a1, none, 0, gone);
                const bool same = !SuspTrack(x, none, a1, 3000, gone);
                SuspTracker y = {};
                const bool direct = !SuspTrack(y, a1, b1, 0, gone) && !SuspTrack(y, b1, a2, 1000, gone);
                // a new id that has a set of its own is another truck of the kind: no take-over writes over it
                SettingsFor(su, kind, id3);
                SettingsSuspLevel(su, true);
                ed = su.s;
                ed.springScale = 0.75f;
                SettingsTake(su, ed);
                SettingsWrite(su);
                const bool notOver = !TruckSuspTakeOver(kind, id1, id3);
                SettingsFor(su, kind, id3);
                Check(viaB && late && same && direct && notOver && su.s.springScale == 0.75f,
                      "suspension: no take-over after a switch to another truck and back, a gap over 20 s, the same id back, or onto an id with a set of its own");
                // while driven, a new id (the game made the truck anew under the driver) takes the set over
                SettingsFor(su, kind, id1);
                const char *id4 = "{44444444-4444-4444-4444-444444444444}";
                const bool whileDriven = SettingsTakeOver(su, id4) && !strcmp(su.id, id4) && SuspLevel(su) == kSuspLevelTruck && su.s.springScale == 2.5f;
                // Own=0 at the truck's level keeps its set there, and the kind's is in force for it; ticked again it is back
                SettingsSuspOwn(su, false);
                wchar_t own[8] = {};
                GetPrivateProfileStringW(SuspSectionOf(id4).c_str(), L"Own", L"", own, 8, truckFile(kind).c_str());
                const bool keptTruck = !su.truckOwn && su.truckKept && SuspLevel(su) == kSuspLevelKind && su.s.springScale == 1.5f && !wcscmp(own, L"0");
                SettingsFor(su, kind, id4);
                const bool keptRead = su.truckKept && SuspLevel(su) == kSuspLevelKind;
                SettingsSuspLevel(su, true);
                Check(whileDriven && keptTruck && keptRead && SuspLevel(su) == kSuspLevelTruck && su.s.springScale == 2.5f,
                      "suspension: a new id while driven takes the set over; Own=0 at the truck's level keeps it, the kind's in force, and This truck only brings it back");
                // Every <kind> with a set for the kind (review F2): the kind's in force, the truck's kept with Own=0, This truck only brings it back
                SettingsSuspLevel(su, false);
                wchar_t keptOwn[8] = {};
                GetPrivateProfileStringW(SuspSectionOf(id4).c_str(), L"Own", L"", keptOwn, 8, truckFile(kind).c_str());
                const bool kindInForce = SuspLevel(su) == kSuspLevelKind && su.s.springScale == 1.5f && su.truckKept && !wcscmp(keptOwn, L"0");
                SettingsSuspLevel(su, true);
                const bool backAgain = SuspLevel(su) == kSuspLevelTruck && su.s.springScale == 2.5f;
                SettingsFor(su, kind, id3);
                const bool id3Own = SuspLevel(su) == kSuspLevelTruck; // id3 keeps its own
                Check(kindInForce && backAgain && id3Own,
                      "suspension: Every <kind> with a set for the kind puts the kind's in force and keeps the truck's with Own=0; This truck only brings it back");
                // Every <kind> with no set for the kind: the truck's set goes up to it and its own section goes
                {
                    const std::wstring kf = truckFile(kind);
                    WritePrivateProfileStringW(kSuspSection, nullptr, nullptr, kf.c_str());
                    SettingsFor(su, kind, id4);
                    const bool noKind = SuspLevel(su) == kSuspLevelTruck && !su.suspOwn && !su.suspKept;
                    SettingsSuspLevel(su, false);
                    wchar_t mine[16] = {};
                    GetPrivateProfileSectionW(SuspSectionOf(id4).c_str(), mine, 16, kf.c_str());
                    SettingsFor(su, kind, "{55555555-5555-5555-5555-555555555555}");
                    Check(noKind && !mine[0] && SuspLevel(su) == kSuspLevelKind && su.s.springScale == 2.5f,
                          "suspension: Every <kind> with no set for the kind puts the truck's set at the kind's level and takes the truck's own section out");
                }
                // with the id not read the truck has the kind's level only
                SettingsFor(su, kind, "");
                Check(SuspLevel(su) == kSuspLevelKind && !su.truckOwn && !su.truckKept, "suspension: a truck whose id is not read has the kind's level only");
                // FrameFlex and TwistRange go with the suspension set at both levels (BRIEF_frame_flex.md part 2); a section without them reads 1
                {
                    GaugeSettings flex = su.plain, back = su.plain;
                    flex.frameFlex = 0.5f, flex.twistRange = 2.0f;
                    WriteTruckSusp(kind, flex, true);
                    flex.frameFlex = 3.0f, flex.twistRange = 0.5f;
                    WriteTruckSusp(kind, flex, true, id1);
                    GaugeSettings atKind = su.plain, atTruck = su.plain;
                    ReadTruckSusp(kind, atKind);
                    ReadTruckSusp(kind, atTruck, id1);
                    wchar_t keyText[16] = {};
                    GetPrivateProfileStringW(SuspSectionOf(id1).c_str(), L"TwistRange", L"", keyText, 16, truckFile(kind).c_str());
                    WritePrivateProfileStringW(kSuspSection, L"FrameFlex", nullptr, truckFile(kind).c_str());
                    WritePrivateProfileStringW(kSuspSection, L"TwistRange", nullptr, truckFile(kind).c_str());
                    back.frameFlex = back.twistRange = 1.0f;
                    ReadTruckSusp(kind, back);
                    Check(atKind.frameFlex == 0.5f && atKind.twistRange == 2.0f && atTruck.frameFlex == 3.0f && atTruck.twistRange == 0.5f && !wcscmp(keyText, L"0.5") &&
                              back.frameFlex == 1.0f && back.twistRange == 1.0f,
                          "suspension: FrameFlex and TwistRange are kept at the kind's level and the truck's, and a section without them reads 1");
                }
                // the take-over while driven (review F1): only on the vehicle the old id was taken with
                Check(SuspAgainTakeOver(id1, 0x1000, id2, 0x1000) && !SuspAgainTakeOver(id1, 0x1000, id2, 0x2000) && !SuspAgainTakeOver(id1, 0, id2, 0) &&
                          !SuspAgainTakeOver(id1, 0x1000, id1, 0x1000) && !SuspAgainTakeOver("", 0x1000, id2, 0x1000),
                      "suspension: a new id while driven is a take-over on the same vehicle only; a new id on a new vehicle is another truck");
                // the take-over mark (review N1): written on the old section, cleared when the old id is driven again,
                // the marked sections beyond the newest 10 dropped at a save, an unmarked one kept
                {
                    const std::wstring kf = truckFile(kind);
                    DeleteFileW(kf.c_str());
                    SettingsFor(su, kind, id1);
                    SettingsSuspLevel(su, true);
                    SettingsWrite(su);
                    const bool took = TruckSuspTakeOver(kind, id1, id2, 100);
                    wchar_t by[48] = {}, byNew[48] = {};
                    GetPrivateProfileStringW(SuspSectionOf(id1).c_str(), L"TakenBy", L"", by, 48, kf.c_str());
                    GetPrivateProfileStringW(SuspSectionOf(id2).c_str(), L"TakenBy", L"", byNew, 48, kf.c_str());
                    const bool marked = took && Wide(id2) == by && !byNew[0];
                    SettingsFor(su, kind, id1);
                    wchar_t after[48] = {};
                    GetPrivateProfileStringW(SuspSectionOf(id1).c_str(), L"TakenBy", L"", after, 48, kf.c_str());
                    const bool cleared = !after[0] && SuspLevel(su) == kSuspLevelTruck;
                    // eleven marked sections, oldest first, and id1 unmarked
                    auto has = [&](const char *id) { wchar_t b[16] = {}; return GetPrivateProfileSectionW(SuspSectionOf(id).c_str(), b, 16, kf.c_str()) != 0; };
                    bool tenKept = false;
                    char ids[11][40];
                    for (int i = 0; i < 11; i++)
                    {
                        sprintf_s(ids[i], "{A%07d-0000-0000-0000-000000000000}", i);
                        WritePrivateProfileStringW(SuspSectionOf(ids[i]).c_str(), L"Own", L"1", kf.c_str());
                        TruckSuspTakeOver(kind, ids[i], "{FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF}", 1000 + i);
                        WritePrivateProfileStringW(SuspSectionOf("{FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF}").c_str(), nullptr, nullptr, kf.c_str());
                        if (i == 9) tenKept = has(ids[0]);
                    }
                    const bool noSave = !has(ids[0]) && has(ids[1]); // the 11th take-over dropped the oldest with no save (review F3)
                    SettingsFor(su, kind, id1);
                    su.truckChanged = true;
                    SettingsWrite(su);
                    bool newest = true;
                    for (int i = 1; i < 11; i++) newest = newest && has(ids[i]);
                    Check(marked && cleared && tenKept && noSave && !has(ids[0]) && newest && has(id1) && has(id2),
                          "suspension: a take-over marks the old section, the old id driven again clears it, ten marked stay, the 11th take-over drops the oldest with no save, a save keeps the newest 10 and the unmarked");
                }
                DeleteFileW(truckFile(kind).c_str());
            }
            DeleteFileW(ini.c_str());
        }
        // the engine's settings per truck (BRIEF_engine_per_truck.md): the levels, the box, the For choice, the
        // button, Defaults and the take-over, on made up kinds, engines and ids
        {
            const char *kind = "eng_kind", *kind2 = "eng_kind2", *kind3 = "eng_kind3", *kind4 = "eng_kind4", *kind5 = "eng_kind5";
            const char *e1 = "e_eng_test.v8", *e2 = "e_eng test two", *g1 = "g_eng_box";
            const char *id1 = "{E1111111-1111-1111-1111-111111111111}", *id2 = "{E2222222-2222-2222-2222-222222222222}",
                       *id3 = "{E3333333-3333-3333-3333-333333333333}";
            char tens[10][24];
            for (int i = 0; i < 10; i++) sprintf_s(tens[i], "e_ten_%d", i);
            auto clean = [&]() {
                for (const char *k : { kind, kind2, kind3, kind4, kind5 }) DeleteFileW(truckFile(k).c_str());
                for (const char *n : { e1, e2 }) DeleteFileW(SetupFile(kSetupEngine, n).c_str());
                for (const char *n : tens) DeleteFileW(SetupFile(kSetupEngine, n).c_str());
                DeleteFileW(SetupFile(kSetupGearbox, g1).c_str());
            };
            clean();
            EngineState e = {}, o = {};
            // nothing set: 100 % and the class's hold; an old file's Torque= and GearHold= are the defaults
            EngineFor(e, kind, id1, e1, g1);
            const bool none = e.torque == 100 && e.torqueFrom == kEngLevelNone && e.hold == 0.0f && e.holdFrom == kEngLevelNone && !e.kindOwn && !e.truckOwn &&
                              EngineLevel(e) == kEngLevelDefault;
            EngineTorqueWrite(e1, 80);
            GearHoldWrite(g1, 2.0f);
            EngineFor(e, kind, id1, e1, g1);
            const bool old = e.torque == 80 && e.torqueFrom == kEngLevelDefault && fabsf(e.hold - 2.0f) < 0.001f && e.holdFrom == kEngLevelDefault;
            Check(none && old, "engine: nothing set gives 100 % and the class's hold, an old Torque= and GearHold= read as the engine's and the gearbox's default");
            // the first edit makes the kind's own (the box ticks itself); another kind with the same engine keeps the default
            const bool made = EngineTake(e, 60, 0.0f);
            const bool kindLevel = e.kindOwn && e.torque == 60 && e.torqueFrom == kEngLevelKind && e.holdFrom == kEngLevelDefault && EngineTorqueRead(e1) == 80;
            EngineFor(o, kind2, id3, e1, g1);
            const bool otherDefault = o.torque == 80 && o.torqueFrom == kEngLevelDefault && !o.kindOwn;
            Check(made && kindLevel && otherDefault,
                  "engine: two kinds share one engine: the first edit makes one kind's own, the engine's default stays for the other kind");
            // an engine changed with the box ticked falls through to the next level for that engine
            EngineFor(e, kind, id1, e2, g1);
            const bool through = e.kindOwn && e.torque == 100 && e.torqueFrom == kEngLevelNone && e.holdFrom == kEngLevelDefault;
            EngineTake(e, 70, 0.0f);
            const bool spaced = e.torque == 70 && e.torqueFrom == kEngLevelKind;
            EngineFor(e, kind, id1, e1, g1);
            const bool firstKept = e.torque == 60 && e.torqueFrom == kEngLevelKind;
            Check(through && spaced && firstKept && EngineKeyOf(L"Torque", "a=b;c[d]") == L"Torque.a_b_c_d_" && EngineKeyOf(L"Torque", e2) == L"Torque.e_eng test two",
                  "engine: another engine in a kind with a set falls through to its own default, an engine's name with a space or a dot is a key, = ; [ ] go to _");
            // ten engines keep ten values in one kind's file
            bool ten = true;
            for (int i = 0; i < 10; i++) EngineFor(o, kind2, "", tens[i], ""), EngineTake(o, 50 + i, 0.0f);
            for (int i = 0; i < 10; i++) EngineFor(o, kind2, "", tens[i], ""), ten = ten && o.torque == 50 + i && o.torqueFrom == kEngLevelKind;
            Check(ten, "engine: ten engines keep ten values in one kind's file");
            // This truck only: the truck's level by its id; another truck of the kind keeps the kind's; A to B to A
            EngineFor(e, kind, id1, e1, g1);
            EngineLevelSet(e, true);
            EngineTake(e, 40, 1.5f);
            const bool truckLevel = EngineLevel(e) == kEngLevelTruck && e.torque == 40 && e.torqueFrom == kEngLevelTruck && fabsf(e.hold - 1.5f) < 0.001f &&
                                    e.holdFrom == kEngLevelTruck;
            EngineFor(o, kind, id3, e1, g1);
            const bool othersKind = o.torque == 60 && o.torqueFrom == kEngLevelKind && fabsf(o.hold - 2.0f) < 0.001f && o.holdFrom == kEngLevelDefault;
            EngineFor(o, kind2, id2, e1, g1);
            EngineFor(e, kind, id1, e1, g1);
            const bool aba = o.torque == 80 && e.torque == 40 && e.torqueFrom == kEngLevelTruck;
            Check(truckLevel && othersKind && aba,
                  "engine: This truck only puts both figures at the truck's level, another truck of the kind keeps the kind's, A to B to A gives A's as it was");
            // Every kind with a set for the kind: the kind's in force, the truck's kept (F2); This truck only brings it back
            EngineLevelSet(e, false);
            const bool f2 = EngineLevel(e) == kEngLevelKind && e.truckKept && !e.truckOwn && e.torque == 60;
            EngineLevelSet(e, true);
            const bool back = EngineLevel(e) == kEngLevelTruck && e.torque == 40;
            // Every kind with no set for the kind: the truck's whole section goes up to it
            EngineFor(o, kind3, id2, e1, g1);
            EngineLevelSet(o, true);
            EngineTake(o, 30, 0.0f);
            EngineLevelSet(o, false);
            wchar_t probe[8] = {};
            const bool up = EngineLevel(o) == kEngLevelKind && !o.truckKept && o.torque == 30 && fabsf(o.hold - 2.0f) < 0.001f &&
                            !GetPrivateProfileSectionW(SectionOf(kEngineSection, id2).c_str(), probe, 8, truckFile(kind3).c_str());
            Check(f2 && back && up,
                  "engine: Every kind with the kind's set keeps the truck's with Own=0 and This truck only brings it back, with none it moves the truck's up whole");
            // the box: off keeps the set with Own=0, an edit then goes to the default (F1), on brings the set back
            EngineOwnSet(o, false);
            const bool off = o.kindKept && !o.kindOwn && o.torque == 80 && o.torqueFrom == kEngLevelDefault;
            EngineTake(o, 90, 0.0f);
            const bool toDefault = EngineTorqueRead(e1) == 90 && o.kindKept && GetPrivateProfileIntW(kEngineSection, L"Torque.e_eng_test.v8", 0, truckFile(kind3).c_str()) == 30;
            EngineOwnSet(o, true);
            const bool on = o.kindOwn && o.torque == 30;
            Check(off && toDefault && on, "engine: the box off keeps the set with Own=0 and an edit goes to the engine's default, the box on brings the set back");
            // the button writes both defaults; the truck keeps its own
            EngineFor(e, kind, id1, e1, g1);
            EngineToDefault(e);
            float dh = 0.0f;
            const bool button = EngineTorqueRead(e1) == 40 && GearHoldRead(g1, dh) && fabsf(dh - 1.5f) < 0.001f && EngineLevel(e) == kEngLevelTruck;
            Check(button, "engine: Make this the engine's default writes the torque to the engine's file and the hold to the gearbox's");
            // Defaults: 100 % and the class's hold, the sets kept with Own=0
            EngineDefaults(e);
            const bool defaults = e.torque == 100 && e.torqueFrom == kEngLevelNone && e.hold == 0.0f && e.holdFrom == kEngLevelNone && e.truckKept && e.kindKept;
            Check(defaults, "engine: Defaults gives 100 % and the class's hold and keeps the sets with Own=0");
            // the take-over at a rebuild with no suspension set, and its own prune of the marked sections
            {
                const std::wstring kf = truckFile(kind4);
                EngineFor(e, kind4, id1, e1, g1);
                EngineLevelSet(e, true);
                EngineTake(e, 45, 0.0f);
                const int taken = TakeOverBoth(kind4, id1, id2);
                EngineFor(e, kind4, id2, e1, g1);
                wchar_t by[48] = {};
                GetPrivateProfileStringW(SectionOf(kEngineSection, id1).c_str(), L"TakenBy", L"", by, 48, kf.c_str());
                const bool tookEngine = taken == kTakenEngine && e.truckOwn && e.torque == 45 && Wide(id2) == by && !wcscmp(TakenWords(taken), L"the engine's settings");
                // the old id driven again clears its engine mark
                TruckSuspSeen(kind4, id1, kEngineSection);
                wchar_t after[48] = {};
                GetPrivateProfileStringW(SectionOf(kEngineSection, id1).c_str(), L"TakenBy", L"", after, 48, kf.c_str());
                const bool cleared = !after[0];
                // a marked suspension section, older than all, stays through the engine's prune
                WritePrivateProfileStringW(SuspSectionOf(id3).c_str(), L"TakenAt", L"1", kf.c_str());
                auto has = [&](const std::wstring &sec) { wchar_t b[16] = {}; return GetPrivateProfileSectionW(sec.c_str(), b, 16, kf.c_str()) != 0; };
                char ids[11][40];
                for (int i = 0; i < 11; i++)
                {
                    sprintf_s(ids[i], "{B%07d-0000-0000-0000-000000000000}", i);
                    WritePrivateProfileStringW(SectionOf(kEngineSection, ids[i]).c_str(), L"Own", L"1", kf.c_str());
                    TruckSuspTakeOver(kind4, ids[i], "{FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF}", 1000 + i, kEngineSection);
                    WritePrivateProfileStringW(SectionOf(kEngineSection, "{FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF}").c_str(), nullptr, nullptr, kf.c_str());
                }
                const bool pruned = !has(SectionOf(kEngineSection, ids[0])) && has(SectionOf(kEngineSection, ids[1])) && has(SuspSectionOf(id3));
                Check(tookEngine && cleared && pruned,
                      "engine: a rebuild takes over an engine set with no suspension set, the old id driven again clears its mark, its own prune drops the 11th marked engine section and leaves the suspension's");
            }
            // an edit with the engine's or the gearbox's name not known writes nothing (review N1)
            {
                EngineFor(o, kind5, id1, "", g1);
                const bool noEngine = !EngineTake(o, 55, 0.0f) && !o.kindOwn;
                EngineFor(o, kind5, id1, e1, "");
                const bool noBox = !EngineTake(o, 0, 1.5f) && !o.kindOwn;
                Check(noEngine && noBox && GetFileAttributesW(truckFile(kind5).c_str()) == INVALID_FILE_ATTRIBUTES,
                      "engine: an edit with the engine's or the gearbox's name not known yet writes nothing and ticks no set");
            }
            // DropTruckIni keeps the file while [Engine] is in it
            {
                const std::wstring kf = truckFile(kind5);
                SetupBegin(kSetupTruck, Wide(kind5));
                WritePrivateProfileStringW(kSetupSections[kSetupTruck], L"TachClass", L"1", kf.c_str());
                WritePrivateProfileStringW(kEngineSection, L"Own", L"1", kf.c_str());
                DropTruckIni(kind5);
                const bool kept = GetFileAttributesW(kf.c_str()) != INVALID_FILE_ATTRIBUTES;
                WritePrivateProfileStringW(kEngineSection, nullptr, nullptr, kf.c_str());
                DropTruckIni(kind5);
                const bool gone = GetFileAttributesW(kf.c_str()) == INVALID_FILE_ATTRIBUTES;
                Check(kept && gone, "engine: a truck's file with [Engine] in it stays when its scales go, with only [Truck] it goes");
            }
            clean();
        }
            // the files' names: a modded truck's stroke as "@", what no file's name may hold as "_"
        Check(SetupFileName(L"mod_suv/1000001") == L"mod_suv@1000001" && SetupFileName(L"a:b*c?") == L"a_b_c_" &&
                  SetupFileName(L"gears 1.5 4 high 2") == L"gears 1.5 4 high 2" && truckFile("mod_suv/1000001").find(L"Gauges\\trucks\\mod_suv@1000001.ini") != std::wstring::npos,
              "setups: a file's name from the truck's kind or the gearbox's name");
        // the learned tops per truck kind and gearbox (BRIEF_gear_tops_per_truck.md): TopShare<n>.<gearbox> in the kind's
        // file, two kinds with one gearbox's name each their own, an old Top4 in the gearbox's file not read as one, a
        // share outside 0.85 to 1.15 counted and not used, a gearbox's name with = ; [ ] as _
        {
            const char *k1 = "top_kind_a", *k2 = "top_kind_b", *box = "g_top_test", *odd = "g=top;[x]";
            for (const char *k : { k1, k2 }) DeleteFileW(truckFile(k).c_str());
            DeleteFileW(SetupFile(kSetupGearbox, box).c_str());
            SetupBegin(kSetupGearbox, Wide(box));
            WritePrivateProfileStringW(kSetupSections[kSetupGearbox], L"Top4", L"21.10", SetupFile(kSetupGearbox, box).c_str());
            float s1[kMaxGears], s2[kMaxGears], old[kMaxGears];
            const bool oldOnly = GearTopShareRead(k1, box, s1) == 0 && GearTopRead(box, old) == 1 && s1[4] == 0.0f;
            GearTopShareWrite(k1, box, 4, 1.005f);
            GearTopShareWrite(k2, box, 4, 0.92f);
            GearTopShareWrite(k2, odd, 3, 0.95f);
            const bool each = GearTopShareRead(k1, box, s1) == 1 && fabsf(s1[4] - 1.005f) < 0.0001f && GearTopShareRead(k2, box, s2) == 1 && fabsf(s2[4] - 0.92f) < 0.0001f;
            float s3[kMaxGears];
            const bool cleaned = GearTopShareRead(k2, odd, s3) == 1 && fabsf(s3[3] - 0.95f) < 0.0001f && GearTopShareName(3, odd) == L"TopShare3.g_top__x_";
            WritePrivateProfileStringW(kSetupSections[kSetupTruck], L"TopShare5.g_top_test", L"1.99", truckFile(k1).c_str());
            int outside = 0;
            const bool wild = GearTopShareRead(k1, box, s1, &outside) == 1 && outside == 1 && s1[5] == 0.0f;
            Check(oldOnly && each && cleaned && wild,
                  "gear tops: kept per truck kind and gearbox as TopShare<n>.<gearbox>, two kinds of one gearbox each their own; an old Top4=21.10 of the gearbox's file not read; "
                  "a share outside 0.85 to 1.15 counted and not used; = ; [ ] in a gearbox's name as _");
            for (const char *k : { k1, k2 }) DeleteFileW(truckFile(k).c_str());
            DeleteFileW(SetupFile(kSetupGearbox, box).c_str());
        }
        // a gearbox's hold into its file and back, held to its range
        DeleteFileW(SetupFile(kSetupGearbox, "g_test_box").c_str());
        float holdBack = 0.0f, holdWild = 0.0f;
        const bool noHold = !GearHoldRead("g_test_box", holdBack);
        GearHoldWrite("g_test_box", 1.7f);
        const bool holdRead = GearHoldRead("g_test_box", holdBack);
        WritePrivateProfileStringW(L"Gearbox", L"GearHold", L"9", SetupFile(kSetupGearbox, "g_test_box").c_str());
        GearHoldRead("g_test_box", holdWild);
        wchar_t boxName[32] = {};
        GetPrivateProfileStringW(L"Gearbox", L"Name", L"", boxName, 32, SetupFile(kSetupGearbox, "g_test_box").c_str());
        Check(noHold && holdRead && fabsf(holdBack - 1.7f) < 0.001f && holdWild == kGearHoldRange[1] && !wcscmp(boxName, L"g_test_box"),
              "setups: a gearbox's hold in its own file with its name, read back, held to 1 to 3 seconds");
        // a gearbox known by its gears finds the file of an earlier key of the same gears (BRIEF_manual_creep_and_hold.md):
        // the four keys of one gearbox in the logs find the first's file; a gear more or one off by 5 % finds none
        {
            const char *keys[4] = { "gears 3.03638 6.05623 8.06946 14.1091 20.1488 high 10.0794", "gears 3.0269 6.04157 8.05135 14.0807 20.11 high 10.0587",
                                    "gears 3.03574 6.05523 8.06823 14.1072 20.1462 high 10.078", "gears 3.03703 6.05722 8.07069 14.1111 20.1515 high 10.0808" };
            for (const char *k : keys) DeleteFileW(SetupFile(kSetupGearbox, k).c_str());
            GearHoldWrite(keys[0], 2.4f);
            char found[96] = "";
            bool all = true;
            for (const char *k : keys) all &= GearboxFileMatch(k, found, sizeof found) && !strcmp(found, keys[0]);
            const bool more = !GearboxFileMatch("gears 3.03638 6.05623 8.06946 14.1091 20.1488 25.0 high 10.0794", found, sizeof found);
            const bool off = !GearboxFileMatch("gears 3.18820 6.05623 8.06946 14.1091 20.1488 high 10.0794", found, sizeof found);
            float hold = 0.0f;
            const bool own = GearboxFileMatch(keys[2], found, sizeof found) && GearHoldRead(found, hold) && fabsf(hold - 2.4f) < 0.001f;
            DeleteFileW(SetupFile(kSetupGearbox, keys[0]).c_str());
            Check(all && more && off && own, "setups: a gearbox known by its gears finds its file under an earlier key of the same gears within 2 % (one gearbox's four keys "
                                             "of the logs), and keeps its hold; a gear more, or one off by 5 %, finds none");
        }
        // an engine's profile: its peak torque's speed and aspiration into its file and back; a curve of its own taken
        // in place of the one from the peak; what is out of range held to it; none without a file
        const char *engineName = "test_engine_2";
        DeleteFileW(SetupFile(kSetupEngine, engineName).c_str());
        EngineProfile profile;
        const bool noProfile = !EngineProfileRead(engineName, profile) && !profile.own;
        EngineProfileWrite(engineName, 1950.0f, kAspNatural, kFuelPetrol);
        const bool profileBack = EngineProfileRead(engineName, profile) && profile.own && profile.peakRpm == 1950.0f && profile.aspiration == kAspNatural && !profile.custom &&
                                 profile.fuel == kFuelPetrol;
        EngineProfileWrite(engineName, 1950.0f, kAspNatural, kFuelClass);
        EngineProfile classFuel;
        const bool fuelGone = EngineProfileRead(engineName, classFuel) && classFuel.fuel == kFuelClass;
        WritePrivateProfileStringW(L"Engine", L"Fuel", L"Diesel", SetupFile(kSetupEngine, engineName).c_str());
        const bool fuelCase = EngineProfileRead(engineName, classFuel) && classFuel.fuel == kFuelDiesel;
        WritePrivateProfileStringW(L"Engine", L"Fuel", nullptr, SetupFile(kSetupEngine, engineName).c_str());
        const TorqueCurve fromPeak = EngineCurveOf(profile, kClassCurves[kClassScout], 4850.0f);
        WritePrivateProfileStringW(L"Engine", L"TorqueCurve", L"0.3:0.7,0.5:1,1:0.75", SetupFile(kSetupEngine, engineName).c_str());
        WritePrivateProfileStringW(L"Engine", L"Aspiration", L"7", SetupFile(kSetupEngine, engineName).c_str());
        EngineProfileRead(engineName, profile);
        const TorqueCurve own = EngineCurveOf(profile, kClassCurves[kClassScout], 4850.0f);
        WritePrivateProfileStringW(L"Engine", L"PeakTorqueRpm", L"50000", SetupFile(kSetupEngine, engineName).c_str());
        EngineProfile wildEngine;
        EngineProfileRead(engineName, wildEngine);
        Check(noProfile && profileBack && fabsf(CurvePeak(fromPeak) - 1950.0f / 4850.0f) < 0.001f && profile.custom && own.n == 3 && own.x[1] == 0.5f &&
                  profile.aspiration == kAspTurbo && wildEngine.peakRpm == kPeakRpmRange[1] && fuelGone && fuelCase,
              "setups: an engine's profile in its own file, read back, its fuel too (none written for the class's); its curve from its peak torque's speed, "
              "or its own written one; out of range held");
        // its torque share: into its file and back, held to 10 to 100 percent; none set, all of it
        const bool torqueNone = wildEngine.torque == 0.0f && TorqueHeld(3.0f, wildEngine.torque) == 3.0f;
        EngineTorqueWrite(engineName, 25);
        EngineProfile heldEngine;
        EngineProfileRead(engineName, heldEngine);
        const bool torqueBack = fabsf(heldEngine.torque - 0.25f) < 0.001f;
        WritePrivateProfileStringW(L"Engine", L"Torque", L"5", SetupFile(kSetupEngine, engineName).c_str());
        EngineProfileRead(engineName, heldEngine);
        const bool torqueLow = heldEngine.torque == kTorqueShareRange[0];
        WritePrivateProfileStringW(L"Engine", L"Torque", L"150", SetupFile(kSetupEngine, engineName).c_str());
        EngineProfileRead(engineName, heldEngine);
        Check(torqueNone && torqueBack && torqueLow && heldEngine.torque == kTorqueShareRange[1],
              "setups: an engine's torque share in its file, read back and held to 10 to 100 percent; none set leaves the push as it is");
        DeleteFileW(SetupFile(kSetupEngine, engineName).c_str());
        // the sections of 0.13.9 and before moved out of Gauges.ini into the files, a key the file holds already kept
        DeleteFileW(truckFile("mover/123").c_str());
        DeleteFileW(SetupFile(kSetupGearbox, "g_moved").c_str());
        truckPut("mover/123", L"TachClass", L"1");
        WritePrivateProfileStringW(L"Truck:mover/123", L"TachClass", L"2", ini.c_str());
        WritePrivateProfileStringW(L"Truck:mover/123", L"RedlineRpm", L"3000", ini.c_str());
        WritePrivateProfileStringW(L"Gearbox:g_moved", L"GearHold", L"1.2", ini.c_str());
        GaugeSettings afterMove;
        ReadIni(afterMove);
        wchar_t movedClass[16] = {}, movedRed[16] = {}, movedKind[32] = {}, iniNames[2048] = {};
        GetPrivateProfileStringW(L"Truck", L"TachClass", L"", movedClass, 16, truckFile("mover/123").c_str());
        GetPrivateProfileStringW(L"Truck", L"RedlineRpm", L"", movedRed, 16, truckFile("mover/123").c_str());
        GetPrivateProfileStringW(L"Truck", L"Kind", L"", movedKind, 32, truckFile("mover/123").c_str());
        float movedHold = 0.0f;
        const bool holdMoved = GearHoldRead("g_moved", movedHold) && fabsf(movedHold - 1.2f) < 0.001f;
        GetPrivateProfileSectionNamesW(iniNames, 2048, ini.c_str());
        bool leftInIni = false;
        for (const wchar_t *n = iniNames; *n; n += wcslen(n) + 1) leftInIni = leftInIni || !_wcsnicmp(n, L"Truck:", 6) || !_wcsnicmp(n, L"Gearbox:", 8);
        Check(!wcscmp(movedClass, L"1") && !wcscmp(movedRed, L"3000") && !wcscmp(movedKind, L"mover/123") && holdMoved && !leftInIni,
              "setups: the trucks' and gearboxes' sections of Gauges.ini moved into their own files, what a file held already kept, none left behind");
    }

    // the saves: the newest of Steam's CompleteSave*.cfg and Epic's CompleteSave*.dat in the folders looked in; a path
    // in the log with the profile's folder written %USERPROFILE%
    {
        const std::wstring root = g_dir + L"savetest", steamDir = root + L"\\steam", epicDir = root + L"\\epic";
        CreateDirectoryW(root.c_str(), nullptr);
        CreateDirectoryW(steamDir.c_str(), nullptr);
        CreateDirectoryW(epicDir.c_str(), nullptr);
        auto saveAt = [](const std::wstring &file, int minutesAgo, const char *text = "{}") {
            const HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            DWORD put = 0;
            WriteFile(h, text, (DWORD)strlen(text), &put, nullptr);
            FILETIME now;
            GetSystemTimeAsFileTime(&now);
            ULARGE_INTEGER t;
            t.LowPart = now.dwLowDateTime;
            t.HighPart = now.dwHighDateTime;
            t.QuadPart -= (ULONGLONG)minutesAgo * 60ULL * 10000000ULL;
            FILETIME then = { t.LowPart, t.HighPart };
            SetFileTime(h, nullptr, nullptr, &then);
            CloseHandle(h);
        };
        saveAt(steamDir + L"\\CompleteSave.cfg", 30);
        saveAt(epicDir + L"\\CompleteSave1.dat", 5);
        saveAt(epicDir + L"\\CompleteSaveBackup.txt", 1); // no save
        auto newestIn = [](const std::vector<std::wstring> &folders) {
            std::vector<SaveFile> found;
            SavesIn(folders, found);
            return found.empty() ? std::wstring() : found[0].first;
        };
        const bool epicNewest = newestIn({ steamDir, epicDir }) == epicDir + L"\\CompleteSave1.dat";
        const bool steamOnly = newestIn({ steamDir }) == steamDir + L"\\CompleteSave.cfg";
        const bool none = newestIn({ root }).empty();
        wchar_t profile[MAX_PATH] = {};
        GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
        const bool masked = !profile[0] || PathForLog(std::wstring(profile) + L"\\Documents\\x") == L"%USERPROFILE%\\Documents\\x";
        Check(epicNewest && steamOnly && none && masked && PathForLog(L"D:\\Games\\x") == L"D:\\Games\\x",
              "saves: the newest of Steam's .cfg and Epic's .dat is found, nothing else is taken for one; the profile's folder stays out of the log");
        // the slot being played: the newest save that names a map, past an empty slot the game wrote after it (as
        // on 25 October); nothing new while that save is the same; a save being written ends the look, and an older
        // slot does not take its place
        const std::wstring played = steamDir + L"\\CompleteSave.cfg", emptySlot = steamDir + L"\\CompleteSave1.cfg", older = steamDir + L"\\CompleteSave2.cfg";
        saveAt(older, 30, "{\"CompleteSave2\":{\"lastLoadedLevel\":\"level_us_01_01\"}}");
        saveAt(played, 3, "{\"CompleteSave\":{\"lastLoadedLevel\":\"level_ru_17_02\"}}");
        saveAt(emptySlot, 2, "{\"CompleteSave1\":{\"lastLoadedLevel\":\"\",\"discoveredObjects\":[]}}");
        std::vector<SaveFile> saves;
        SavesIn({ steamDir }, saves);
        SaveFile picked;
        char level[64] = "";
        std::wstring unread;
        const FILETIME never = {};
        const bool pastEmpty = SavePick(saves, never, picked, level, sizeof level, unread) && picked.first == played && !strcmp(level, "level_ru_17_02") && unread.empty();
        const FILETIME pickedAt = picked.second;
        SaveFile again;
        const bool nothingNew = !SavePick(saves, pickedAt, again, level, sizeof level, unread) && unread.empty();
        saveAt(played, 1, "{\"CompleteSave\":{\"lastLoadedLevel\":\"level_ru_"); // being written
        saves.clear();
        SavesIn({ steamDir }, saves);
        const bool waits = !SavePick(saves, never, again, level, sizeof level, unread) && unread == played;
        Check(pastEmpty && nothingNew && waits,
              "saves: the map from the newest save that names one, past an empty slot written after it; none from a save being written, nor from an older slot");
        for (const wchar_t *f : { L"\\steam\\CompleteSave.cfg", L"\\steam\\CompleteSave1.cfg", L"\\steam\\CompleteSave2.cfg", L"\\epic\\CompleteSave1.dat",
                                  L"\\epic\\CompleteSaveBackup.txt" })
            DeleteFileW((root + f).c_str());
        RemoveDirectoryW(steamDir.c_str());
        RemoveDirectoryW(epicDir.c_str());
        RemoveDirectoryW(root.c_str());
    }

    // the kept start logs capped: of 25 fitting names and two other files, the 20 newest stay, the 5 oldest go and the
    // two others are left; 10 of 60 bytes under a cap of 480 keep the newest 8; KeepLogs 0 removes nothing; a name that
    // only looks close is never one of the mod's
    {
        const std::wstring root = g_dir + L"logcaptest\\";
        CreateDirectoryW(root.c_str(), nullptr);
        auto make = [&](const std::wstring &name, int minute, size_t bytes) {
            const std::wstring path = root + name;
            FILE *f = nullptr;
            if (_wfopen_s(&f, path.c_str(), L"wb") == 0 && f)
            {
                for (size_t i = 0; i < bytes; i++) fputc('x', f);
                fclose(f);
            }
            SYSTEMTIME st = { 2026, 10, 0, 12, 10, (WORD)minute, 0, 0 };
            FILETIME ft;
            SystemTimeToFileTime(&st, &ft);
            HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE)
            {
                SetFileTime(h, nullptr, nullptr, &ft);
                CloseHandle(h);
            }
        };
        auto name = [](int minute, int copy) {
            wchar_t n[64];
            if (copy) swprintf_s(n, L"Gauges 2026-10-12 10-%02d-00 (%d).log", minute, copy);
            else swprintf_s(n, L"Gauges 2026-10-12 10-%02d-00.log", minute);
            return std::wstring(n);
        };
        auto there = [&](const std::wstring &n) { return GetFileAttributesW((root + n).c_str()) != INVALID_FILE_ATTRIBUTES; };
        for (int i = 0; i < 25; i++) make(name(i, i == 24 ? 2 : 0), i, 10);
        make(L"Gauges backup.log", 0, 10);
        make(L"notes.txt", 0, 10);
        const LogsCapResult byCount = LogsCap(root, 20, kLogsMostBytes);
        bool newest = true, oldest = true;
        for (int i = 5; i < 25; i++) newest = newest && there(name(i, i == 24 ? 2 : 0));
        for (int i = 0; i < 5; i++) oldest = oldest && !there(name(i, 0));
        const bool others = there(L"Gauges backup.log") && there(L"notes.txt");
        const bool count = byCount.removed == 5 && byCount.kept == 20 && byCount.failed == 0 && newest && oldest && others;
        for (int i = 0; i < 25; i++) DeleteFileW((root + name(i, i == 24 ? 2 : 0)).c_str());
        for (int i = 0; i < 10; i++) make(name(30 + i, 0), 30 + i, 60);
        const LogsCapResult bySize = LogsCap(root, 20, 480);
        const bool size = bySize.kept == 8 && bySize.removed == 2 && there(name(39, 0)) && there(name(32, 0)) && !there(name(31, 0)) && !there(name(30, 0));
        const LogsCapResult none = LogsCap(root, 0, 1);
        const bool all = none.removed == 0 && there(name(32, 0)) && there(L"Gauges backup.log");
        const bool names = LogKeptName(L"Gauges 2026-10-12 10-00-00.log") && LogKeptName(L"Gauges 2026-10-12 10-00-00 (12).log") &&
                           !LogKeptName(L"Gauges backup.log") && !LogKeptName(L"Gauges 2026-10-12 10-00-00.log.bak") && !LogKeptName(L"Gauges 2026-10-12 10-00-00 ().log") &&
                           !LogKeptName(L"Gauges.log") && !LogKeptName(L"gauges 2026-10-12 10-00-00.log");
        Check(count && size && all && names, "log: the kept start logs capped, the newest KeepLogs (20) and at most 500 MB (480 bytes here: 8 of 60), the others left; "
                                             "KeepLogs 0 removes nothing; only names the mod gives are ever removed");
        for (int i = 0; i < 10; i++) DeleteFileW((root + name(30 + i, 0)).c_str());
        DeleteFileW((root + L"Gauges backup.log").c_str());
        DeleteFileW((root + L"notes.txt").c_str());
        RemoveDirectoryW(root.c_str());
    }

    // the last start's log kept in Gauges\logs, named by the day and time it began: the same day, a drive past
    // midnight, another log of the same start's time; an empty log left where it is
    {
        const std::wstring root = g_dir + L"logtest\\", kept = root + L"Gauges\\logs\\", log = root + L"Gauges.log";
        CreateDirectoryW(root.c_str(), nullptr);
        auto logAt = [&](const char *first, WORD day, WORD hour, WORD minute) {
            FILE *f = nullptr;
            if (_wfopen_s(&f, log.c_str(), L"wb") == 0 && f)
            {
                fputs("\xEF\xBB\xBF", f);
                fputs(first, f);
                fputs(" Gauges " GAUGES_VERSION "\n", f);
                fclose(f);
            }
            SYSTEMTIME st = { 2026, 10, 0, day, hour, minute, 0, 0 };
            FILETIME lt, ut;
            SystemTimeToFileTime(&st, &lt);
            LocalFileTimeToFileTime(&lt, &ut);
            HANDLE file = CreateFileW(log.c_str(), FILE_WRITE_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (file != INVALID_HANDLE_VALUE)
            {
                SetFileTime(file, nullptr, nullptr, &ut);
                CloseHandle(file);
            }
            return LogKeepLast(root);
        };
        const std::wstring sameDay = logAt("21:29:11", 12, 23, 10), pastMidnight = logAt("23:50:00", 13, 0, 20), again = logAt("21:29:11", 12, 22, 0);
        const bool moved = GetFileAttributesW((kept + sameDay).c_str()) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW(log.c_str()) == INVALID_FILE_ATTRIBUTES;
        FILE *f = nullptr;
        if (_wfopen_s(&f, log.c_str(), L"wb") == 0 && f) fclose(f);
        const bool emptyStays = LogKeepLast(root).empty() && GetFileAttributesW(log.c_str()) != INVALID_FILE_ATTRIBUTES;
        Check(sameDay == L"Gauges 2026-10-12 21-29-11.log" && pastMidnight == L"Gauges 2026-10-12 23-50-00.log" && again == L"Gauges 2026-10-12 21-29-11 (2).log" &&
                  moved && emptyStays,
              "log: the last start's log is kept in Gauges\\logs by the day and time it began, a drive past midnight by the day before, a second of the "
              "same time beside the first; an empty one stays");
        for (const std::wstring &name : { sameDay, pastMidnight, again }) DeleteFileW((kept + name).c_str());
        DeleteFileW(log.c_str());
        RemoveDirectoryW(kept.c_str());
        RemoveDirectoryW((root + L"Gauges").c_str());
        RemoveDirectoryW(root.c_str());
    }

    // the log kept open (BRIEF_log_kept_open.md, with the Reviewer's addendum): the first line truncates; 10 000 probe
    // lines and an ordinary one are all on disk in order once it returns; a probe line alone is out within 0.6 s by the
    // loop's tick; a crash line through its own handle lands in its place after the probe lines, and still lands with
    // the lock held; a failed write falls back to a line at a time. The time of 10 000 lines, kept open and as before
    {
        const std::wstring path = g_dir + L"Gauges.log";
        auto file = [&]() {
            std::string text;
            const HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (f == INVALID_HANDLE_VALUE) return text;
            static char chunk[65536];
            DWORD got = 0;
            while (ReadFile(f, chunk, sizeof chunk, &got, nullptr) && got) text.append(chunk, got);
            CloseHandle(f);
            return text;
        };
        // a new start: the log closed and its first line not in yet
        AcquireSRWLockExclusive(&g_logLock);
        LogFlushHeld();
        if (g_logFile != INVALID_HANDLE_VALUE) CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
        g_logStarted = g_logOneByOne = false;
        ReleaseSRWLockExclusive(&g_logLock);
        Log(L"log test: the first line");
        const std::string first = file();
        const bool fresh = !first.compare(0, 3, "\xEF\xBB\xBF") && first.size() >= 26 && first.find("log test: the first line\r\n") == first.size() - 26 &&
                           first.find('\n') == first.size() - 1 && g_logFile != INVALID_HANDLE_VALUE;
        const ULONGLONG at = GetTickCount64();
        LARGE_INTEGER freq, t0, t1, t2;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < 10000; i++) Log(L"probe: log test line %d", i);
        Log(L"log test: after the probe lines");
        QueryPerformanceCounter(&t1);
        const std::string all = file();
        bool inOrder = true;
        size_t from = 0;
        for (int i = 0; i < 10000 && inOrder; i++)
        {
            char want[48];
            snprintf(want, sizeof want, "probe: log test line %d\r\n", i);
            const size_t found = all.find(want, from);
            inOrder = found != std::string::npos;
            from = found + strlen(want);
        }
        inOrder = inOrder && all.find("log test: after the probe lines\r\n", from) != std::string::npos;
        (void)at;
        // 1 000 of the lines as before, an open, a write and a close for each (10 000 took the test nearly a minute here)
        const std::wstring old = g_dir + L"logtimetest.log";
        for (int i = 0; i < 1000; i++)
        {
            FILE *f = nullptr;
            if (_wfopen_s(&f, old.c_str(), i ? L"a, ccs=UTF-8" : L"w, ccs=UTF-8") == 0 && f)
            {
                fwprintf(f, L"00:00:00 probe: log test line %d\n", i);
                fclose(f);
            }
        }
        QueryPerformanceCounter(&t2);
        DeleteFileW(old.c_str());
        const double keptMs = 1000.0 * (t1.QuadPart - t0.QuadPart) / freq.QuadPart, beforeMs = 1000.0 * (t2.QuadPart - t1.QuadPart) / freq.QuadPart;
        printf("     log: 10 000 lines in %.0f ms kept open; 1 000 in %.0f ms opened and closed for each (%.2f ms a line)\n", keptMs, beforeMs, beforeMs / 1000.0);
        // a probe line alone, then silence: not out at once, out within 0.6 s by the tick
        Log(L"log test: flushed");
        Log(L"probe: log test alone");
        const bool notYet = file().find("probe: log test alone") == std::string::npos;
        const ULONGLONG alone = GetTickCount64();
        bool out = false;
        while (!out && GetTickCount64() - alone < 600)
        {
            Sleep(50);
            LogTick();
            out = file().find("probe: log test alone\r\n") != std::string::npos;
        }
        const bool soon = notYet && out && GetTickCount64() - alone <= 600;
        // a crash line in its place: after the probe lines in the buffer, before the next line
        wcsncpy_s(g_crashLog, path.c_str(), _TRUNCATE);
        Log(L"probe: log test before the crash");
        CrashLineFlushed("crash: log test line");
        Log(L"log test: after the crash");
        std::string text = file();
        const size_t before = text.find("probe: log test before the crash\r\n"), crash = text.find("crash: log test line\r\n"), after = text.find("log test: after the crash\r\n");
        const bool inPlace = before != std::string::npos && crash != std::string::npos && after != std::string::npos && before < crash && crash < after;
        // with the lock held (a fault inside LogV) the crash line lands anyway, and the probe line after it
        Log(L"probe: log test held");
        AcquireSRWLockExclusive(&g_logLock);
        CrashLineFlushed("crash: log test line held");
        ReleaseSRWLockExclusive(&g_logLock);
        text = file();
        const bool held = text.find("crash: log test line held\r\n") != std::string::npos && text.find("probe: log test held") == std::string::npos;
        Log(L"log test: after held");
        text = file();
        const size_t heldProbe = text.find("probe: log test held\r\n");
        const bool heldThen = heldProbe != std::string::npos && heldProbe > text.find("crash: log test line held\r\n");
        // a write that fails (a handle that cannot write): closed, the line tried the old way, and each line after it so
        AcquireSRWLockExclusive(&g_logLock);
        const HANDLE good = g_logFile;
        g_logFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        ReleaseSRWLockExclusive(&g_logLock);
        Log(L"log test: a failed write");
        const bool fellBack = g_logOneByOne && g_logFile == INVALID_HANDLE_VALUE;
        Log(L"log test: one by one");
        text = file();
        const bool oneByOne = fellBack && text.find("log test: a failed write\r\n") != std::string::npos && text.find("log test: one by one\r\n") != std::string::npos;
        // started with no handle, as the second copy is (its Run sets g_logStarted before its one line; review F1): the
        // line lands, a line at a time
        AcquireSRWLockExclusive(&g_logLock);
        g_logFile = INVALID_HANDLE_VALUE;
        g_logOneByOne = false;
        ReleaseSRWLockExclusive(&g_logLock);
        Log(L"log test: no handle, as the second copy");
        Log(L"probe: log test no handle");
        text = file();
        const bool noHandle = g_logOneByOne && text.find("log test: no handle, as the second copy\r\n") != std::string::npos &&
                              text.find("probe: log test no handle\r\n") != std::string::npos;
        AcquireSRWLockExclusive(&g_logLock);
        g_logFile = good;
        g_logOneByOne = false;
        ReleaseSRWLockExclusive(&g_logLock);
        // a line over the 2048 characters of the stack's room, and one over the 64 KB buffer, whole (review N2)
        std::wstring longText(3000, L'x'), hugeText(70000, L'y');
        longText += L" end of the long line";
        hugeText += L" end of the huge line";
        Log(L"log test long: %s", longText.c_str());
        Log(L"probe: log test huge %s", hugeText.c_str());
        Log(L"log test: after the long lines");
        text = file();
        const std::string longWant = "log test long: " + std::string(3000, 'x') + " end of the long line\r\n",
                          hugeWant = "probe: log test huge " + std::string(70000, 'y') + " end of the huge line\r\n";
        const size_t longAt = text.find(longWant), hugeAt = text.find(hugeWant), afterLong = text.find("log test: after the long lines\r\n");
        const bool longLines = longAt != std::string::npos && hugeAt != std::string::npos && afterLong != std::string::npos && longAt < hugeAt && hugeAt < afterLong;
        Check(fresh && inOrder && soon && inPlace && held && heldThen && oneByOne && noHandle && longLines,
              "log: kept open, the first line truncates; 10 000 probe lines and an ordinary one on disk in order once it returns; a probe line alone out within "
              "0.6 s by the tick; a crash line in its place after the probe lines, and with the lock held before them; a failed write, or no handle (the "
              "second copy), goes a line at a time; lines over 2048 characters and over 64 KB whole and in order");
        if (!(fresh && inOrder && soon && inPlace && held && heldThen && oneByOne && noHandle && longLines))
            printf("     log parts: fresh %d, in order %d, soon %d, in place %d, held %d then %d, one by one %d, no handle %d, long lines %d\n", fresh, inOrder, soon, inPlace,
                   held, heldThen, oneByOne, noHandle, longLines);
    }

    // the maps' sections: the regions' written into an ini that has none; a map takes its own section, else its
    // region's, else one named after its id, held to the range; one taken out by hand stays out; the placeholder
    // names of 0.11.0 to 0.11.2 are renamed with their temperatures, once
    {
        DeleteFileW(ini.c_str());
        GaugeSettings fresh;
        ReadIni(fresh);
        char section[64];
        float deg = 0.0f;
        const bool regionsWritten = MapSectionAmbient("Michigan", deg) && deg == 12.0f && MapSectionAmbient("Zurdania", deg) && deg == 22.0f &&
                                    MapSectionAmbient("kola peninsula", deg) && deg == -10.0f;
        MapAir air = {};
        const bool byRegion = MapAmbient("level_us_01_04_new", air, section, sizeof section) && air.mean == 12.0f && !strcmp(section, "Michigan") &&
                              !air.dayNight && air.day == 12.0f + kDaySwing * 0.5f && air.night == 12.0f - kDaySwing * 0.5f;
        const bool unknown = !MapAmbient("level_nowhere", air, section, sizeof section) && !section[0] && !MapSectionAmbient("", deg);
        WritePrivateProfileStringW(L"Map:Black River", L"AmbientC", L"-3", ini.c_str());
        WritePrivateProfileStringW(L"Map:level_my_map", L"AmbientC", L"99", ini.c_str());
        WritePrivateProfileStringW(L"Map:Alaska", nullptr, nullptr, ini.c_str());
        ReadIni(fresh);
        const bool byMap = MapAmbient("level_us_01_01", air, section, sizeof section) && air.mean == -3.0f && !strcmp(section, "Black River");
        const bool sameRegion = MapAmbient("level_us_01_02", air, section, sizeof section) && air.mean == 12.0f;
        const bool byId = MapAmbient("level_my_map", air, section, sizeof section) && air.mean == kAmbientRange[1] && !strcmp(section, "level_my_map");
        const bool takenOut = !MapAmbient("level_us_02_01", air, section, sizeof section);
        const bool swissFresh = MapAmbient("level_us_19_02", air, section, sizeof section) && air.mean == -10.0f && !strcmp(section, "Switzerland");
        WritePrivateProfileStringW(L"Map:Switzerland", nullptr, nullptr, ini.c_str());
        WritePrivateProfileStringW(L"Gauges", L"MapRegionsOffered", nullptr, ini.c_str());
        ReadIni(fresh);
        const bool swissOffered = MapAmbient("level_us_19_01", air, section, sizeof section) && air.mean == -10.0f;
        WritePrivateProfileStringW(L"Map:Switzerland", nullptr, nullptr, ini.c_str());
        ReadIni(fresh);
        const bool swissStaysOut = !MapAmbient("level_us_19_03", air, section, sizeof section);
        char swissTitle[96];
        MapTitle("level_us_19_01", swissTitle, sizeof swissTitle);
        const bool swissName = Wide(swissTitle) == L"Gr\u00fcntal Hollow, Switzerland";
        Check(swissFresh && swissOffered && swissStaysOut && swissName,
              "maps: Switzerland (Season 19) is in a new ini, is offered once to an ini that has the other regions, stays out once taken out, "
              "and its map's name reads as UTF-8");
        Check(regionsWritten && byRegion && unknown && byMap && sameRegion && byId && takenOut,
              "maps: an ini without map sections gets the regions', a map takes its own section, else its region's, else its id's, held to the range, "
              "and a section taken out or changed by hand stays so");
        // a section's day and night: DayC and NightC as given (the two the right way round), the slider's move
        // shifting both, and a section with only them and no AmbientC
        WritePrivateProfileStringW(L"Map:Scandinavia", L"DayC", L"-12", ini.c_str());
        WritePrivateProfileStringW(L"Map:Scandinavia", L"NightC", L"3", ini.c_str());
        const bool dayNight = MapSectionAir("Scandinavia", air) && air.dayNight && air.day == 3.0f && air.night == -12.0f && air.mean == -4.5f;
        MapAirShift("Scandinavia", air, 2.0f);
        const bool shifted = MapSectionAir("Scandinavia", air) && air.day == 5.0f && air.night == -10.0f;
        WritePrivateProfileStringW(L"Map:Taymyr", L"AmbientC", nullptr, ini.c_str());
        WritePrivateProfileStringW(L"Map:Taymyr", L"DayC", L"14", ini.c_str());
        WritePrivateProfileStringW(L"Map:Taymyr", L"NightC", L"6", ini.c_str());
        const bool noMean = MapSectionAir("Taymyr", air) && air.dayNight && air.day == 14.0f && air.night == 6.0f && air.mean == 10.0f;
        MapSectionAir("Michigan", air);
        MapAirShift("Michigan", air, -4.0f);
        const bool meanShifted = MapSectionAir("Michigan", air) && air.mean == 8.0f && !air.dayNight;
        Check(dayNight && shifted && noMean && meanShifted, "maps: a section's day and night, shifted together by the slider; a section of only the mean moves its mean");
        // the curve over the game's day: the coldest at 5, the warmest at 15, half way at 10 and at 22, no step at midnight
        const bool curve = DayNightC(3.0f, -12.0f, kColdestHour) == -12.0f && fabsf(DayNightC(3.0f, -12.0f, kWarmestHour) - 3.0f) < 0.001f &&
                           fabsf(DayNightC(3.0f, -12.0f, 10.0f) + 4.5f) < 0.001f && fabsf(DayNightC(3.0f, -12.0f, 22.0f) + 4.5f) < 0.001f &&
                           fabsf(DayNightC(3.0f, -12.0f, 23.999f) - DayNightC(3.0f, -12.0f, 0.0f)) < 0.01f && DayNightC(3.0f, -12.0f, 2.0f) < DayNightC(3.0f, -12.0f, 0.0f);
        Check(curve, "maps: the air over the game's day, coldest before sunrise and warmest in the afternoon, without a step at midnight");
        DeleteFileW(ini.c_str());
        WritePrivateProfileStringW(kIniSection, L"Layout", L"1", ini.c_str());
        ReadIni(fresh);
        Check(fresh.layout == kLayoutBlock && MapSectionAmbient("Michigan", deg) && deg == 12.0f, "maps: an ini from an earlier version gets the map sections and keeps its settings");
        DeleteFileW(ini.c_str());
        WritePrivateProfileStringW(kIniSection, L"Layout", L"1", ini.c_str());
        WritePrivateProfileStringW(L"Map:michigan", L"AmbientC", L"7", ini.c_str());
        WritePrivateProfileStringW(L"Map:kola", L"AmbientC", L"-30", ini.c_str());
        WritePrivateProfileStringW(L"Map:british_columbia", L"AmbientC", L"4", ini.c_str());
        ReadIni(fresh);
        wchar_t kolaLeft[8] = {};
        GetPrivateProfileStringW(L"Map:kola", L"AmbientC", L"", kolaLeft, 8, ini.c_str());
        const bool renamedOk = MapSectionAmbient("Kola Peninsula", deg) && deg == -30.0f && MapSectionAmbient("British Columbia", deg) && deg == 4.0f && !kolaLeft[0];
        MapAir kept = {};
        const bool heldTemp = MapAmbient("level_us_01_01", kept, section, sizeof section) && kept.mean == 7.0f && !MapSectionAmbient("Taymyr", deg);
        const bool addedOk = MapSectionAmbient("Zurdania", deg) && deg == 22.0f && MapSectionAmbient("Chiapas", deg) && deg == 26.0f;
        ReadIni(fresh);
        const bool once = MapSectionAmbient("Kola Peninsula", deg) && deg == -30.0f && !MapSectionAmbient("Taymyr", deg) && fresh.layout == kLayoutBlock;
        Check(renamedOk && heldTemp && addedOk && once,
              "maps: the placeholder names of 0.11.0 are renamed to the game's with their temperatures, the regions they lacked are added, and nothing else");
    }

    // the tab's edits and the turn they were made from
    {
        GaugeSettings inForce, replaced, got;
        SettingsDefaults(inForce);
        replaced = inForce;
        replaced.opacity = 0.3f;
        SettingsPublish(inForce);
        const LONG turn = SettingsGet(got);
        GaugeSettings edit = got;
        edit.opacity = 0.6f;
        SettingsPut(edit, turn);
        const bool flagged = InterlockedExchange(&g_setDirty, 0) == 1;
        SettingsGet(got);
        const bool taken = flagged && got.opacity == 0.6f;
        // the mod puts other settings in force: an edit made from the ones before is no edit of these
        SettingsPublish(replaced);
        SettingsPut(edit, turn);
        const LONG turnNow = SettingsGet(got);
        Check(taken && turnNow != turn && got.opacity == 0.3f && !g_setDirty, "tab: an edit made from the settings in force is taken, one made from settings the mod has replaced since is dropped");
    }

    // pad button names
    char name[32];
    PadName(PadParse("dpadup + rb"), name, sizeof name);
    Check(!strcmp(name, "RB+DPadUp") && PadParse("None") == 0 && PadParse("LB+Nothing") == 0, "pad: button names read and written, unknown names give none");
    KeyName(VK_F9, name, sizeof name);
    Check(!strcmp(name, "F9"), "key: F9 by name");
    KeyName(0, name, sizeof name);
    Check(!strcmp(name, "None"), "key: none by name");

    if (argc > 1)
    {
        if (!CopyFileW(argv[1], ini.c_str(), FALSE))
        {
            printf("FAIL ini: %ls could not be copied (error %lu)\n", argv[1], GetLastError());
            return 1;
        }
        ReadIni(a);
        WriteIni(a);
        ReadIni(b);
        Check(SameSettings(a, b), "ini: a copy of the given ini reads back the same after a write");
    }

    // the gearbox's bindings: the defaults without a key, a key written and read back, None clears, a text that is no
    // binding leaves the slot; Gearbox=2 takes the shifter only with a binding that reaches reverse
    {
        const std::wstring bindIni = g_dir + L"Gauges.binds_test.ini";
        DeleteFileW(bindIni.c_str());
        GearBindings b;
        BindingsDefault(b);
        const bool none = !BindingsInIni(bindIni);
        BindingsRead(bindIni, b, false);
        const bool defaults = b.slot[kActUp][0].kind == kBindKey && b.slot[kActUp][0].code == VK_NUMPAD8 && b.slot[kActDown][0].code == VK_NUMPAD2 &&
                              b.slot[kActGearN][0].code == VK_NUMPAD5 && b.slot[kActGearA][0].code == VK_NUMPAD7 && b.slot[kActGearR][0].code == VK_NUMPAD0 &&
                              !b.slot[kActClutch][0].kind && BindingsCanReverse(b, InputSnapshot{}, false);
        // the number pad's operators by name, and back from it
        char plus[16], dot[16];
        KeyName(VK_ADD, plus, sizeof plus);
        KeyName(VK_DECIMAL, dot, sizeof dot);
        Binding numPlus = {};
        char numWhy[64];
        const bool named = !strcmp(plus, "Num +") && !strcmp(dot, "Num .") && BindingParse("Key Num +", numPlus, numWhy, sizeof numWhy) &&
                           numPlus.kind == kBindKey && numPlus.code == VK_ADD && KeyFromText("Num /") == VK_DIVIDE;
        b.slot[kActClutch][1] = Binding{ kBindAxis, 1, 3, 0, true };
        b.slot[kActGear3][0] = Binding{ kBindButton, 2, 5, 0, false };
        b.slot[kActUp][0] = Binding{};
        BindingsWrite(bindIni, b);
        GearBindings back;
        BindingsDefault(back);
        BindingsRead(bindIni, back, false);
        const bool round = BindingsInIni(bindIni) && back.slot[kActClutch][1].kind == kBindAxis && back.slot[kActClutch][1].device == 1 &&
                           back.slot[kActClutch][1].code == 3 && back.slot[kActClutch][1].inverted && back.slot[kActGear3][0].kind == kBindButton &&
                           back.slot[kActGear3][0].code == 5 && !back.slot[kActUp][0].kind;
        WritePrivateProfileStringW(kBindSection, L"BindGearH", L"Wheel nine", bindIni.c_str());
        back.slot[kActGearH][0] = Binding{ kBindKey, 0, 'H', 0, false };
        BindingsRead(bindIni, back, false);
        const bool kept = back.slot[kActGearH][0].kind == kBindKey && back.slot[kActGearH][0].code == 'H';
        GearBindings bare = {};
        const bool fallback = !BindingsCanReverse(bare, InputSnapshot{}, true);
        bare.slot[kActGearR][1] = Binding{ kBindKey, 0, 'R', 0, false };
        DeleteFileW(bindIni.c_str());
        Check(none && defaults && named && round && kept && fallback && BindingsCanReverse(bare, InputSnapshot{}, false),
              "gearbox: bindings default to Num 8, Num 2, Num 5, Num 0 and Num 7, the number pad's operators by name, round trip through the ini, None "
              "clears, no binding leaves a slot; the shifter is taken only with one that reaches reverse");
    }
    // review 8: reverse reached only by what can be pressed now (W5), what a capture may not bind (W1, W9), the actions
    // from the reader's states, the actions resting while the menu is open (W2), the snapshot out of front (W7)
    {
        GearBindings b = {};
        b.slot[kActGearR][0] = Binding{ kBindButton, 2, 5, 0, false };
        b.slot[kActDown][1] = Binding{ kBindPad, 0, XINPUT_GAMEPAD_DPAD_DOWN, 0, false };
        InputSnapshot there = {};
        there.device[1].present = true;
        const bool wheel = !BindingsCanReverse(b, InputSnapshot{}, false) && BindingsCanReverse(b, there, false);
        b.slot[kActGearR][0] = Binding{};
        const bool pad = !BindingsCanReverse(b, there, false) && BindingsCanReverse(b, InputSnapshot{}, true);
        Check(wheel && pad, "gearbox: reverse counts a wheel's binding only while that wheel is there, a pad's only while a pad answers");

        const uint32_t chord = XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_DPAD_UP;
        const Binding f2 = { kBindKey, 0, VK_F2, 0, false }, f8 = { kBindKey, 0, VK_F8, 0, false }, num8 = { kBindKey, 0, VK_NUMPAD8, 0, false };
        const Binding rb = { kBindPad, 0, XINPUT_GAMEPAD_RIGHT_SHOULDER, 0, false }, rbUpA = { kBindPad, 0, chord | XINPUT_GAMEPAD_A, 0, false };
        const Binding padA = { kBindPad, 0, XINPUT_GAMEPAD_A, 0, false };
        Check(BindingClash(f2, VK_F2, VK_F8, chord) && BindingClash(f8, VK_F2, VK_F8, chord) && BindingClash(rb, VK_F2, VK_F8, chord) &&
                  BindingClash(rbUpA, VK_F2, VK_F8, chord) && !BindingClash(num8, VK_F2, VK_F8, chord) && !BindingClash(padA, VK_F2, VK_F8, chord) &&
                  !BindingClash(f8, VK_F2, 0, 0),
              "gearbox: a capture never binds the menu's key, the toggle key or pad buttons in or holding the PadToggle chord");

        ActionState rs[kActCount] = {};
        rs[kActUp].pressed = rs[kActUp].held = true;
        rs[kActGear3].held = true;
        rs[kActGear5].pressed = rs[kActGear5].held = true;
        rs[kActGearR].pressed = true;
        rs[kActClutch].held = true;
        rs[kActClutch].travel = 0.7f;
        GearBindings keys = {};
        keys.slot[kActClutch][0] = Binding{ kBindKey, 0, VK_SPACE, 0, false };
        const GearboxActions withKey = ActionsOf(rs, keys);
        GearBindings axis = {};
        axis.slot[kActClutch][1] = Binding{ kBindAxis, 1, 3, 0, false };
        const GearboxActions x = ActionsOf(rs, axis);
        Check(withKey.up && !withKey.down && withKey.gearPressed == 5 && withKey.gearHeld == 3 && withKey.r && !withKey.n && withKey.clutch && withKey.clutchTravel < 0.0f && x.clutchTravel == 0.7f,
              "gearbox: the actions from the reader's states: the edges, the gear held, the clutch's travel only from an axis");
        ActionState high[kActCount] = {};
        high[kActGear16].pressed = high[kActGear16].held = true;
        const GearboxActions sixteen = ActionsOf(high, keys);
        Check(sixteen.gearPressed == 16 && sixteen.gearHeld == 16 && !strcmp(kActionNames[kActGear16], "Gear16") && !strcmp(kActionNames[kActGearH], "GearH") &&
                  !strcmp(kActionNames[kActRideDown], "RideDown"),
              "gearbox: Gear16 is the 16th gear key, and the names stay with their actions after it");
        // the wiring (the held gear grip brief): the model the readers use, stepped through the pass's own code for 10 s
        // in a held 4th with the wheels gripping, has the grip learned and asks a gear next to the held one when A comes
        // back by Back to A or the A key, not the 1st of a model reset each pass
        {
            GearboxModel model = {};
            GearboxPosModel pos = {};
            pos.running = true;
            pos.pos = kPosGear;
            pos.gear = 4;
            TruckRaw t = {};
            t.flags = 3;
            t.powerFactor = 1.0f;
            t.gearSpeeds = 6;
            static const float speeds[6] = { 1.5f, 3.0f, 6.0f, 12.0f, 16.0f, 8.0f };
            memcpy(t.gearSpeed, speeds, sizeof speeds);
            t.wheels = 4;
            t.drive[0] = t.drive[1] = t.drive[2] = t.drive[3] = 1;
            t.gear = t.gearEngaged = 4;
            t.throttle = -0.3f;
            const float spin = TruckGearTop(t, 4) * 0.8f;
            for (int i = 0; i < 4; i++) t.spin[i] = spin;
            t.speed = spin * 0.45f;
            const GaugeScale &k = kClassScales[kClassTruck];
            for (int i = 0; i < 200; i++) GearboxModelPass(model, true, pos, t, k, 0.3f, 0.3f, 0.05f, EngineHand{});
            const int aGear = TakeoverAutoGear(t, model.gear, GearboxLowestFit(model, t, k));
            char line[200];
            snprintf(line, sizeof line, "gearbox: the pass's model in a held 4th learns the grip (%.2f) and A's gear is next to it (%d), not a reset model's 1st",
                     model.grip, aGear);
            Check(GearboxHeldBy(true, pos) && model.grip > 0.0f && aGear >= 3 && aGear <= 5, line);
        }

        const GearboxActions r = ActionsResting(x);
        Check(!r.up && !r.r && !r.gearPressed && r.gearHeld == 3 && r.clutch && r.clutchTravel == 0.7f,
              "gearbox: while the menu is open or a capture runs, nothing goes down and what was held stands");

        InputSnapshot back = {};
        back.front = false;
        back.pad = XINPUT_GAMEPAD_A;
        back.device[0].present = true;
        back.device[0].button[0] = 1;
        const InputSnapshot behind = InputFrontOnly(back);
        back.front = true;
        const InputSnapshot kept = InputFrontOnly(back);
        Check(!behind.pad && !behind.device[0].present && kept.pad == XINPUT_GAMEPAD_A && kept.device[0].button[0],
              "gearbox: the pad and the wheels are read as nothing while the game is not in front");
    }
    // the SMT.ini import: a Kb. scan code turned into its key, a combination and an unknown code left, the options taken
    {
        const std::wstring smtIni = g_dir + L"SMT.test.ini";
        DeleteFileW(smtIni.c_str());
        WritePrivateProfileStringW(L"KEYBOARD", L"GEAR UP", L"Kb.18", smtIni.c_str());        // E
        WritePrivateProfileStringW(L"KEYBOARD", L"GEAR R", L"Kb.207", smtIni.c_str());        // End, an extended key
        WritePrivateProfileStringW(L"KEYBOARD", L"GEAR 1", L"Kb.42+Kb.2", smtIni.c_str());    // a combination: left
        WritePrivateProfileStringW(L"KEYBOARD", L"GEAR 3", L"Kb.80", smtIni.c_str());         // Num 2, the default of GearDown, which goes
        WritePrivateProfileStringW(L"KEYBOARD", L"GEAR 4", L"Kb.83", smtIni.c_str());         // Num .
        WritePrivateProfileStringW(L"KEYBOARD", L"SHOW MENU", L"Kb.211", smtIni.c_str());     // not an action of the mod's
        WritePrivateProfileStringW(L"KEYBOARD", L"DIFF LOCK", L"Kb.38", smtIni.c_str());      // L
        WritePrivateProfileStringW(L"KEYBOARD", L"AWD", L"Kb.30", smtIni.c_str());            // A
        WritePrivateProfileStringW(L"CONTROLLER", L"GEAR DOWN", L"Joy.0.Btn.5", smtIni.c_str());
        WritePrivateProfileStringW(L"OPTIONS", L"REQUIRE CLUTCH", L"true", smtIni.c_str());
        WritePrivateProfileStringW(L"OPTIONS", L"SKIP NEUTRAL", L"1", smtIni.c_str());
        WritePrivateProfileStringW(L"OPTIONS", L"IMMERSIVE MODE", L"true", smtIni.c_str());
        GearBindings b;
        BindingsDefault(b);
        GaugeSettings o;
        SettingsDefaults(o);
        std::wstring taken, left;
        const int bound = SmtIniImport(smtIni, b, o, taken, left);
        DeleteFileW(smtIni.c_str());
        Check(bound == 6 && b.slot[kActUp][0].code == 'E' && b.slot[kActGearR][0].code == VK_END && b.slot[kActGear3][0].code == VK_NUMPAD2 &&
                  b.slot[kActGear4][0].code == VK_DECIMAL && !b.slot[kActDown][0].kind && b.slot[kActGearN][0].code == VK_NUMPAD5 &&
                  b.slot[kActDiffLock][0].code == 'L' && b.slot[kActAwd][0].code == 'A' && wcsstr(taken.c_str(), L"DIFF LOCK") &&
                  !b.slot[kActGear1][0].kind && o.requireClutch && o.skipNeutral && o.downStaysForward && !o.requireGearHeld && wcsstr(left.c_str(), L"GEAR 1") &&
                  wcsstr(left.c_str(), L"the default of GearDown") && wcsstr(taken.c_str(), L"GEAR UP"),
              "gearbox: SMT.ini's keyboard bindings taken over by their scan codes (an extended key and the number pad too), a default on a key "
              "it took dropped, a combination left, its controller section and menu key left, its diff lock and AWD keys taken, its options taken");
    }
    printf(g_failed ? "SETTINGS TEST FAILED (%d)\n" : "SETTINGS TEST OK\n", g_failed);
    return g_failed ? 1 : 0;
}
#endif
