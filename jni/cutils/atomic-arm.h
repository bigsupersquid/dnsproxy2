/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_CUTILS_ATOMIC_ARM_H
#define ANDROID_CUTILS_ATOMIC_ARM_H

#include <stdint.h>

/* Some of the harware instructions used below are not available in Thumb-1
 * mode (they are if you build in ARM or Thumb-2 mode though). To solve this
 * problem, we're going to use the same technique than libatomics_ops,
 * which is to temporarily switch to ARM, do the operation, then switch
 * back to Thumb-1.
 *
 * This results in two 'bx' jumps, just like a normal function call, but
 * everything is kept inlined, avoids loading or computing the function's
 * address, and prevents a little I-cache trashing too.
 *
 * However, it is highly recommended to avoid compiling any C library source
 * file that use these functions in Thumb-1 mode.
 *
 * Define three helper macros to implement this:
 */
#if defined(__thumb__) && !defined(__thumb2__)
#  define  __ATOMIC_SWITCH_TO_ARM \
            "adr r3, 5f\n" \
            "bx  r3\n" \
            ".align\n" \
            ".arm\n" \
        "5:\n"
/* note: the leading \n below is intentional */
#  define __ATOMIC_SWITCH_TO_THUMB \
            "\n" \
            "adr r3, 6f+1\n" \
            "bx  r3\n" \
            ".thumb\n" \
        "6:\n"

#  define __ATOMIC_CLOBBERS   "r3",  /* list of clobbered registers */
#else
#  define  __ATOMIC_SWITCH_TO_ARM   /* nothing */
#  define  __ATOMIC_SWITCH_TO_THUMB /* nothing */
#  define  __ATOMIC_CLOBBERS        /* nothing */
#endif

#ifndef ANDROID_ATOMIC_INLINE
#define ANDROID_ATOMIC_INLINE inline __attribute__((always_inline))
#endif

extern ANDROID_ATOMIC_INLINE void android_compiler_barrier()
{
    __asm__ __volatile__ ("" : : : "memory");
}

extern ANDROID_ATOMIC_INLINE void android_memory_barrier()
{
#if defined(ANDROID_SMP) && ANDROID_SMP == 1
  __asm__ __volatile__ ( "dmb ish" : : : "memory" );
#elif (__ARM_ARCH__ >= 7)
  /* A simple compiler barrier. */
  __asm__ __volatile__ ( "" : : : "memory" );
#endif
}

extern ANDROID_ATOMIC_INLINE void android_memory_store_barrier()
{
#if ANDROID_SMP == 0
    android_compiler_barrier();
#else
    __asm__ __volatile__ ("dmb st" : : : "memory");
#endif
}

extern ANDROID_ATOMIC_INLINE
int32_t android_atomic_acquire_load(volatile const int32_t *ptr)
{
    int32_t value = *ptr;
    android_memory_barrier();
    return value;
}

extern ANDROID_ATOMIC_INLINE
int32_t android_atomic_release_load(volatile const int32_t *ptr)
{
    android_memory_barrier();
    return *ptr;
}

extern ANDROID_ATOMIC_INLINE
void android_atomic_acquire_store(int32_t value, volatile int32_t *ptr)
{
    *ptr = value;
    android_memory_barrier();
}

extern ANDROID_ATOMIC_INLINE
void android_atomic_release_store(int32_t value, volatile int32_t *ptr)
{
    android_memory_barrier();
    *ptr = value;
}

extern ANDROID_ATOMIC_INLINE
int android_atomic_cas(int32_t old_value, int32_t new_value,
                       volatile int32_t *ptr)
{
    int32_t prev, status;
    do {
        __asm__ __volatile__ (
            __ATOMIC_SWITCH_TO_ARM
            "ldrex %0, [%3]\n"
            "mov %1, #0\n"
            "teq %0, %4\n"
#ifdef __thumb2__
                              "it eq\n"
#endif
            "strexeq %1, %5, [%3]"
            __ATOMIC_SWITCH_TO_THUMB
            : "=&r" (prev), "=&r" (status), "+m"(*ptr)
            : "r" (ptr), "Ir" (old_value), "r" (new_value)
            : __ATOMIC_CLOBBERS "cc");
    } while (__builtin_expect(status != 0, 0));
    return prev != old_value;
}

extern ANDROID_ATOMIC_INLINE
int android_atomic_acquire_cas(int32_t old_value, int32_t new_value,
                               volatile int32_t *ptr)
{
    int status = android_atomic_cas(old_value, new_value, ptr);
    android_memory_barrier();
    return status;
}

extern ANDROID_ATOMIC_INLINE
int android_atomic_release_cas(int32_t old_value, int32_t new_value,
                               volatile int32_t *ptr)
{
    android_memory_barrier();
    return android_atomic_cas(old_value, new_value, ptr);
}

extern ANDROID_ATOMIC_INLINE
int32_t android_atomic_add(int32_t increment, volatile int32_t *ptr)
{
    int32_t prev, tmp, status;
    android_memory_barrier();
    do {
        __asm__ __volatile__ (
							  "ldrex %0, [%4]\n"
                              "add %1, %0, %5\n"
                              "strex %2, %1, [%4]"
                              : "=&r" (prev), "=&r" (tmp),
                                "=&r" (status), "+m" (*ptr)
                              : "r" (ptr), "Ir" (increment)
                              : "cc");
    } while (__builtin_expect(status != 0, 0));
    return prev;
}

extern ANDROID_ATOMIC_INLINE int32_t android_atomic_inc(volatile int32_t *addr)
{
    return android_atomic_add(1, addr);
}

extern ANDROID_ATOMIC_INLINE int32_t android_atomic_dec(volatile int32_t *addr)
{
    return android_atomic_add(-1, addr);
}

extern ANDROID_ATOMIC_INLINE
int32_t android_atomic_and(int32_t value, volatile int32_t *ptr)
{
    int32_t prev, tmp, status;
    android_memory_barrier();
    do {
        __asm__ __volatile__ ("ldrex %0, [%4]\n"
                              "and %1, %0, %5\n"
                              "strex %2, %1, [%4]"
                              : "=&r" (prev), "=&r" (tmp),
                                "=&r" (status), "+m" (*ptr)
                              : "r" (ptr), "Ir" (value)
                              : "cc");
    } while (__builtin_expect(status != 0, 0));
    return prev;
}

extern ANDROID_ATOMIC_INLINE
int32_t android_atomic_or(int32_t value, volatile int32_t *ptr)
{
    int32_t prev, tmp, status;
    android_memory_barrier();
    do {
        __asm__ __volatile__ ("ldrex %0, [%4]\n"
                              "orr %1, %0, %5\n"
                              "strex %2, %1, [%4]"
                              : "=&r" (prev), "=&r" (tmp),
                                "=&r" (status), "+m" (*ptr)
                              : "r" (ptr), "Ir" (value)
                              : "cc");
    } while (__builtin_expect(status != 0, 0));
    return prev;
}

#endif /* ANDROID_CUTILS_ATOMIC_ARM_H */
