// fault_place.h: the words of a fault's place for the crash line (BRIEF_tester_1114.md, part 196, the Reviewer's
// addendum): what the memory at the faulting address is, and whether the fault came on another thread after the game
// called ExitProcess. Pure functions with no heap, so a handler may call them and a test can check them.
#pragma once
#include <windows.h>
#include <stdio.h>

// The memory at a fault's address as VirtualQuery gave it into out: its state, its type (image, mapped or private),
// its allocation base, and whether that region holds one of the mod's hook trampolines (ours). read false: the query
// gave nothing.
inline int FaultMemoryText(bool read, DWORD state, DWORD type, const void *base, bool ours, char *out, size_t size)
{
    if (!read) return _snprintf_s(out, size, _TRUNCATE, "memory: not read");
    const char *st = state == MEM_COMMIT ? "committed" : state == MEM_RESERVE ? "reserved" : state == MEM_FREE ? "free" : "of no known state";
    const char *ty = state == MEM_FREE ? "none" : type == MEM_IMAGE ? "image" : type == MEM_MAPPED ? "mapped" : type == MEM_PRIVATE ? "private" : "of no known type";
    return _snprintf_s(out, size, _TRUNCATE, "memory: %s, %s, allocation base %p, %s", st, ty, base,
                       ours ? "a region of the mod's hook trampolines" : "no region of the mod's hooks");
}

// A fault on a thread other than the one that called ExitProcess, after that call: the game is ending, so the line
// says so instead of "crash" (exitThread 0: no ExitProcess yet).
inline bool FaultAtQuit(DWORD exitThread, DWORD thread) { return exitThread != 0 && thread != exitThread; }
