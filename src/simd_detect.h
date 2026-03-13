/*
 * simd_detect.h - Runtime CPU feature detection
 */

#ifndef SIMD_DETECT_H
#define SIMD_DETECT_H

/* CPU feature flags */
#define CMJ_CPU_SSE2   (1 << 0)
#define CMJ_CPU_SSE3   (1 << 1)
#define CMJ_CPU_SSSE3  (1 << 2)
#define CMJ_CPU_SSE41  (1 << 3)
#define CMJ_CPU_AVX    (1 << 4)
#define CMJ_CPU_AVX2   (1 << 5)

/*
 * Detect CPU features at runtime.
 * Returns a bitmask of CMJ_CPU_* flags.
 */
int cmj_detect_cpu_features(void);

/* Global SIMD enable/disable flag */
extern int cmj_simd_enabled;

/* Global fast-algorithm enable/disable flag */
extern int cmj_fast_enabled;

#endif /* SIMD_DETECT_H */
