#include <windows.h>
// what the Windows SDK's headers define and MinGW's leave out here: a name clash shows up as it would with MSVC
#ifndef small
#define small char
#endif
#ifndef hyper
#define hyper __int64
#endif
#ifndef near
#define near
#endif
#ifndef far
#define far
#endif
#ifndef interface
#define interface struct
#endif
