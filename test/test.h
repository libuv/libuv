#ifndef TEST_H_
#define TEST_H_

#include <stdio.h>
#include <stdlib.h>

#define TEST_PORT 8123
#define TEST_PORT_2 8124


/* Log to stderr. */
#define LOG(...)    fprintf(stderr, "%s", __VA_ARGS__)
#define LOGF(...)   fprintf(stderr, __VA_ARGS__)

/* Die with fatal error. */
#define FATAL(msg)                                        \
  do {                                                    \
    fprintf(stderr,                                       \
            "Fatal error in %s on line %d: %s\n",         \
            __FILE__,                                     \
            __LINE__,                                     \
            msg);                                         \
    abort();                                              \
  } while (0)


/*
 * Have our own assert, so we are sure it does not get optimized away in
 * a release build.
 */
#define ASSERT(expr)                                      \
 do {                                                     \
  if (!(expr)) {                                          \
    fprintf(stderr,                                       \
            "Assertion failed in %s on line %d: %s\n",    \
            __FILE__,                                     \
            __LINE__,                                     \
            #expr);                                       \
    abort();                                              \
  }                                                       \
 } while (0)


/* Just sugar for wrapping the main() for a test. */
#define TEST_IMPL(name)   \
  int run_##name()

#endif /* TEST_H_ */
