
#ifndef TEST_RUNNER_H_
#define TEST_RUNNER_H_


/*
 * Struct to store both tests and to define helper processes for tests.
 */
typedef struct {
  char *test_name;
  char *process_name;
  int (*main)();
  int is_helper;
} test_entry_t;


/*
 * Macros used by test-list.h
 */
#define TEST_DECLARE(name)                    \
  int run_##name();

#define TEST_LIST_START                       \
  test_entry_t TESTS[] = {

#define TEST_LIST_END                         \
    { 0, 0, 0, 0 }                            \
  };

#define TEST_ENTRY(name)                      \
    { #name, #name, &run_##name, 0 },

#define TEST_HELPER(name, proc)               \
    { #name, #proc, &run_##proc, 1 },


/*
 * Include platform-dependent definitions
 */
#ifdef _WIN32
# include "test-runner-win32.h"
#else
# include "test-runner-unix.h"
#endif


/*
 * Stuff that should be implemented by test-runner-<platform>.h
 * All functions return 0 on success, -1 on failure, unless specified
 * otherwise.
 */

/* Invoke "arv[0] test-name". Store process info in *p. */
/* Make sure that all stdio output of the processes is buffered up. */
int process_start(char *name, process_info_t *p);

/* Wait for all `n` processes in `vec` to terminate. */
/* Time out after `timeout` msec, or never if timeout == -1 */
/* Return 0 if all processes are terminated, -1 on error, -2 on timeout. */
int process_wait(process_info_t *vec, int n, int timeout);

/* Returns the number of bytes in the stdio output buffer for process `p`. */
long int process_output_size(process_info_t *p);

/* Copy the contents of the stdio output buffer to `fd`. */
int process_copy_output(process_info_t *p, int fd);

/* Return the name that was specified when `p` was started by process_start */
char* process_get_name(process_info_t *p);

/* Terminate process `p`. */
int process_terminate(process_info_t *p);

/* Return the exit code of process p. */
/* On error, return -1. */
int process_reap(process_info_t *p);

/* Clean up after terminating process `p` (e.g. free the output buffer etc.). */
void process_cleanup(process_info_t *p);

/* Move the console cursor one line up and back to the first column. */
int rewind_cursor();

#endif /* TEST_RUNNER_H_ */
