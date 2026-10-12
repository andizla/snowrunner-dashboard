#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
int main() {
  const wchar_t *v[] = { L"nan", L"inf", L"abc" };
  for (auto s : v) { float f = (float)_wtof(s); printf("%ls -> %g finite %d\n", s, f, (int)isfinite(f)); }
  wchar_t path[MAX_PATH]; GetFullPathNameW(L"t.ini", MAX_PATH, path, nullptr);
  WritePrivateProfileStringW(L"G", L"Scale", L"nan", path);
  wchar_t buf[64] = {}; GetPrivateProfileStringW(L"G", L"Scale", L"", buf, 64, path);
  printf("read back '%ls'\n", buf);
}
