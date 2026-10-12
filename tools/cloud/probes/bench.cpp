#include <windows.h>
#include <vector>
#include "game_code.h"
int main()
{
    std::vector<uint8_t> code(100u << 20);
    uint32_t x = 12345;
    for (auto &b : code) { x = x * 1664525u + 1013904223u; b = (uint8_t)(x >> 24); }
    for (size_t i = 0; i < code.size(); i += 7) code[i] = 0x48; // the common prefix, often
    const ULONGLONG t = GetTickCount64();
    FunctionFind f[kFnCount];
    GameFunctionsFind(code.data(), 0, code.size(), f);
    printf("%.2f s\n", (GetTickCount64() - t) / 1000.0);
    return 0;
}
