#include <stdio.h>
#include <direct.h>
int main(int argc, char **argv) { char cwd[512]; _getcwd(cwd, sizeof cwd); printf("[git in %s]", cwd); for (int i = 1; i < argc; i++) printf(" %s", argv[i]); printf("\n"); return 0; }
