/*
 * Minimal host test helpers: CHECK macros and a summary.
 * Each test_*.c includes this once and ends main() with TEST_SUMMARY().
 */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include <string.h>

static int t_run;
static int t_fail;

#define CHECK(cond) do {                                                     \
        t_run++;                                                             \
        if (!(cond)) {                                                       \
            t_fail++;                                                        \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                    \
    } while (0)

#define CHECK_EQ_INT(a, b) do {                                              \
        long long va_ = (long long)(a), vb_ = (long long)(b);                \
        t_run++;                                                             \
        if (va_ != vb_) {                                                    \
            t_fail++;                                                        \
            printf("  FAIL %s:%d: %s == %lld, expected %lld\n",              \
                   __FILE__, __LINE__, #a, va_, vb_);                        \
        }                                                                    \
    } while (0)

#define CHECK_EQ_STR(a, b) do {                                              \
        const char *sa_ = (a), *sb_ = (b);                                   \
        t_run++;                                                             \
        if (strcmp(sa_, sb_) != 0) {                                         \
            t_fail++;                                                        \
            printf("  FAIL %s:%d: %s\n    got      \"%s\"\n    expected \"%s\"\n", \
                   __FILE__, __LINE__, #a, sa_, sb_);                        \
        }                                                                    \
    } while (0)

#define RUN(fn) do { printf("- %s\n", #fn); fn(); } while (0)

#define TEST_SUMMARY(name) do {                                              \
        printf("%s: %d checks, %d failed -> %s\n", (name), t_run, t_fail,    \
               t_fail ? "FAIL" : "PASS");                                    \
        return t_fail ? 1 : 0;                                               \
    } while (0)

#endif /* TEST_UTIL_H */
