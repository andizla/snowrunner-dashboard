// MinGW build shim for the offline tests: the std headers first, then windows.h, then MSVC's min and max macros
#include <algorithm>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cwchar>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <windows.h>
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
