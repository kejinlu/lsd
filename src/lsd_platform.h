//
//  lsd_platform.h
//  libud
//
//  Created by kejinlu on 2026/04/15.
//

#ifndef LSD_PLATFORM_H
#define LSD_PLATFORM_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ---- Thread-local storage ----
#if defined(_MSC_VER)
    #define LSD_THREAD_LOCAL __declspec(thread)
#else
    #define LSD_THREAD_LOCAL __thread
#endif

// ---- 64-bit fseek/ftell ----
#if defined(_MSC_VER)
    #define lsd_fseek _fseeki64
    #define lsd_ftell _ftelli64
#elif defined(__APPLE__)
    // macOS: off_t is 64-bit by default; fseeko/ftello are available
    #define lsd_fseek fseeko
    #define lsd_ftell ftello
#else
    // Linux/POSIX
    #define lsd_fseek fseeko
    #define lsd_ftell ftello
#endif

// ---- strtok_r / strtok_s (MSVC) ----
#if defined(_MSC_VER)
    #define strtok_r strtok_s
#endif

// ---- strndup polyfill (MSVC) ----
#if defined(_MSC_VER) && !defined(strndup)
static inline char *lsd_strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *dup = (char *)malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len);
        dup[len] = '\0';
    }
    return dup;
}
#define strndup lsd_strndup
#endif

#endif /* LSD_PLATFORM_H */
