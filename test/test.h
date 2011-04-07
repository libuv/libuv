#ifndef TEST_H_
#define TEST_H_

#include <assert.h>


#define TEST_IMPL(name)   \
  int run_##name()

#define TEST_PORT 8123
#define TEST_PORT_2 8124

#endif /* TEST_H_ */