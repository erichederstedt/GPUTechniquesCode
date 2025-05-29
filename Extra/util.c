#include "util.h"

// grabbed from https://gist.github.com/pmttavara/6f06fc5c7679c07375483b06bb77430c

// SPDX-FileCopyrightText: © 2022 Phillip Trudeau-Tavara <pmttavara@protonmail.com>
// SPDX-License-Identifier: 0BSD

// https://hero.handmade.network/forums/code-discussion/t/7485-queryperformancefrequency_returning_10mhz_bug/2
// https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/timers#partition-reference-tsc-mechanism

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define NOMINMAX
#include <Windows.h>
#include <intrin.h>

unsigned long long GetRdtscFreq()
{
    // Cache the answer so that multiple calls never take the slow path more than once
    static uint64_t tsc_freq = 0;
    if (tsc_freq) {
        return tsc_freq;
    }

    // Fast path: Load kernel-mapped memory page
    HMODULE ntdll = LoadLibraryA("ntdll.dll");
    if (ntdll) {

        int (*NtQuerySystemInformation)(int, void *, unsigned int, unsigned int *) =
            (int (*)(int, void *, unsigned int, unsigned int *))GetProcAddress(ntdll, "NtQuerySystemInformation");
        if (NtQuerySystemInformation) {

            volatile uint64_t *hypervisor_shared_page = NULL;
            unsigned int size = 0;

            // SystemHypervisorSharedPageInformation == 0xc5
            int result = (NtQuerySystemInformation)(0xc5, (void *)&hypervisor_shared_page, sizeof(hypervisor_shared_page), &size);

            // success
            if (size == sizeof(hypervisor_shared_page) && result >= 0) {
                // docs say ReferenceTime = ((VirtualTsc * TscScale) >> 64)
                //      set ReferenceTime = 10000000 = 1 second @ 10MHz, solve for VirtualTsc
                //       =>    VirtualTsc = 10000000 / (TscScale >> 64)
                tsc_freq = (10000000ull << 32) / (hypervisor_shared_page[1] >> 32);
                // If your build configuration supports 128 bit arithmetic, do this:
                // tsc_freq = ((unsigned __int128)10000000ull << (unsigned __int128)64ull) / hypervisor_shared_page[1];
            }
        }
        FreeLibrary(ntdll);
    }

    // Slow path
    if (!tsc_freq) {
        puts("Couldn't find ntdll.dll, using slow path for tsc freq gathering.");
        // Get time before sleep
        uint64_t qpc_begin = 0; QueryPerformanceCounter((LARGE_INTEGER *)&qpc_begin);
        uint64_t tsc_begin = __rdtsc();

        Sleep(2);

        // Get time after sleep
        uint64_t qpc_end = qpc_begin + 1; QueryPerformanceCounter((LARGE_INTEGER *)&qpc_end);
        uint64_t tsc_end = __rdtsc();

        // Do the math to extrapolate the RDTSC ticks elapsed in 1 second
        uint64_t qpc_freq = 0; QueryPerformanceFrequency((LARGE_INTEGER *)&qpc_freq);
        tsc_freq = (tsc_end - tsc_begin) * qpc_freq / (qpc_end - qpc_begin);
    }

    // Failure case
    if (!tsc_freq) {
        tsc_freq = 1000000000;
    }

    return tsc_freq;
}

void SetCpuAndThreadPriority()
{
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    SetThreadAffinityMask(GetCurrentThread(), 2);
}

void CreateConsole()
{
    AllocConsole();

    // Redirect STDOUT
    FILE* fp_out;
    freopen_s(&fp_out, "CONOUT$", "w", stdout);

    // Redirect STDERR
    FILE* fp_err;
    freopen_s(&fp_err, "CONOUT$", "w", stderr);

    // Redirect STDIN
    FILE* fp_in;
    freopen_s(&fp_in, "CONIN$", "r", stdin);
}

#include <string.h>
#define ASSET_PATH "..\\..\\..\\Assets\\"
char* get_asset_path(const char* path)
{
    size_t len = strlen(ASSET_PATH) + strlen(path);
    char* string = calloc(len+1, sizeof(char));
    strcat(string, ASSET_PATH);
    strcat(string, path);
    return string;
}