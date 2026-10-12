// loads each DLL named on the command line in turn, as a loader would, and says what came of it
#include <windows.h>
#include <stdio.h>
int wmain(int argc, wchar_t **argv)
{
    for (int i = 1; i < argc; i++)
    {
        HMODULE m = LoadLibraryW(argv[i]);
        printf("load %ls: %s (error %lu)\n", argv[i], m ? "loaded" : "refused", m ? 0 : GetLastError());
        Sleep(2500);
    }
    return 0;
}
