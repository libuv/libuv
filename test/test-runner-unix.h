#ifndef TEST_RUNNER_UNIX_H
#define TEST_RUNNER_UNIX_H

#include <sys/types.h>
#include <stdio.h> /* FILE */

typedef struct {
  FILE* stdout_file;
  pid_t pid;
  char* name;
  int status;
  int terminated;
} process_info_t;

#endif  /* TEST_RUNNER_UNIX_H */
