#ifndef UTIL
#define UTIL
#include <intrin.h>

inline unsigned long long GetRdtsc()
{
    static int trash[4];
    __cpuid(trash, 0);
    return __rdtsc();
}
unsigned long long GetRdtscFreq();

void SetCpuAndThreadPriority();

void CreateConsole();

char* get_asset_path(const char* path);
#endif