/* test.h — tiny assertion helpers for the host unit test. */
#ifndef ST25R_TEST_H
#define ST25R_TEST_H

#include <stdio.h>
#include <stdlib.h>

static int g_test_failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            g_test_failures++;                                                                     \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(a, b)                                                                             \
    do {                                                                                           \
        long long va_ = (long long)(a), vb_ = (long long)(b);                                      \
        if (va_ != vb_) {                                                                          \
            fprintf(stderr, "%s:%d: CHECK_EQ failed: %s (%lld) != %s (%lld)\n", __FILE__,          \
                    __LINE__, #a, va_, #b, vb_);                                                   \
            g_test_failures++;                                                                     \
        }                                                                                          \
    } while (0)

#define TEST_MAIN_END()                                                                            \
    do {                                                                                           \
        if (g_test_failures) {                                                                     \
            fprintf(stderr, "%d failure(s)\n", g_test_failures);                                   \
            return 1;                                                                              \
        }                                                                                          \
        printf("ok\n");                                                                            \
        return 0;                                                                                  \
    } while (0)

#endif
