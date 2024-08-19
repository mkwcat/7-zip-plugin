#pragma once

#include <cstdio>

#if 0

extern FILE* g_logF;

#  define PRINT(...)                                                           \
      do {                                                                     \
          g_logF = fopen("D:\\wii\\repo\\7-zip-mod\\mod_log.txt", "a");        \
          fprintf(g_logF, __VA_ARGS__);                                        \
          fclose(g_logF);                                                      \
      } while (false)

#else

#  define PRINT(...)

#endif