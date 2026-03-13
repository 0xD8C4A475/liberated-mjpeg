/*
 * simd_detect.c - Runtime CPU feature detection using CPUID
 */

#include "simd_detect.h"

int cmj_simd_enabled = 1;
int cmj_fast_enabled = 1;

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>

int cmj_detect_cpu_features(void)
{
    int features = 0;
    int info[4];

    __cpuid(info, 0);
    int max_id = info[0];

    if (max_id >= 1) {
        __cpuid(info, 1);
        if (info[3] & (1 << 26)) features |= CMJ_CPU_SSE2;
        if (info[2] & (1 << 0))  features |= CMJ_CPU_SSE3;
        if (info[2] & (1 << 9))  features |= CMJ_CPU_SSSE3;
        if (info[2] & (1 << 19)) features |= CMJ_CPU_SSE41;
        if (info[2] & (1 << 28)) features |= CMJ_CPU_AVX;
    }

    if (max_id >= 7) {
        __cpuidex(info, 7, 0);
        if (info[1] & (1 << 5)) features |= CMJ_CPU_AVX2;
    }

    return features;
}

#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#include <cpuid.h>

int cmj_detect_cpu_features(void)
{
    int features = 0;
    unsigned int eax, ebx, ecx, edx;

    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        if (edx & (1 << 26)) features |= CMJ_CPU_SSE2;
        if (ecx & (1 << 0))  features |= CMJ_CPU_SSE3;
        if (ecx & (1 << 9))  features |= CMJ_CPU_SSSE3;
        if (ecx & (1 << 19)) features |= CMJ_CPU_SSE41;
        if (ecx & (1 << 28)) features |= CMJ_CPU_AVX;
    }

    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        if (ebx & (1 << 5)) features |= CMJ_CPU_AVX2;
    }

    return features;
}

#else
/* Non-x86 platform */
int cmj_detect_cpu_features(void)
{
    return 0;
}
#endif
