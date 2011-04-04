
#include "test-runner.h"

#include <assert.h>
#include <io.h>
#include <stdio.h>
#include <string.h>

/* Actual tests and helpers are defined in test-list.h */
#include "test-list.h"

/* The maximum number of processes (main + helpers) that a test can have. */
#define TEST_MAX_PROCESSES 8

/* The time in milliseconds after which a single test times out, */
#define TEST_TIMEOUT 20000

/* Die with fatal error. */
#define FATAL(msg)  assert(msg && 0);

/* Log to stderr. */
#define LOG(...)    fprintf(stderr, "%s", __VA_ARGS__)
#define LOGF(...)   fprintf(stderr, __VA_ARGS__)


/*
 * Runs an individual test; returns 1 if the test succeeded, 0 if it failed.
 * If the test fails it prints diagnostic information.
 */
int run_test(test_entry_t *test) {
  int i, result, success;
  char errmsg[256];
  test_entry_t *helper;
  int process_count;
  process_info_t processes[TEST_MAX_PROCESSES];
  process_info_t *main_process;

  success = 0;

  process_count = 0;

  /* Start all helpers for this test first */
  for (helper = (test_entry_t*)&TESTS; helper->main; helper++) {
    if (helper->is_helper &&
        strcmp(test->test_name, helper->test_name) == 0) {
      if (process_start(helper->process_name, &processes[process_count]) == -1) {
        sprintf_s((char*)&errmsg, sizeof(errmsg), "process `%s` failed to start.", helper->process_name);
        goto finalize;
      }
      process_count++;
    }
  }

  /* Start the main test process. */
  if (process_start(test->process_name, &processes[process_count]) == -1) {
    sprintf_s((char*)&errmsg, sizeof(errmsg), "process `%s` failed to start.", test->process_name);
    goto finalize;
  }
  main_process = &processes[process_count];
  process_count++;

  /* Wait for the main process to terminate. */
  result = process_wait(main_process, 1, TEST_TIMEOUT);
  if (result == -1) {
    FATAL("process_wait failed\n");
  } else if (result == -2) {
    sprintf_s((char*)&errmsg, sizeof(errmsg), "timeout.");
    goto finalize;
  }

  /* Reap main process */
  result = process_reap(main_process);
  if (result != 0) {
    sprintf_s((char*)&errmsg, sizeof(errmsg), "exit code %d.", result);
    goto finalize;
  }

  /* Yes! did it. */
  success = 1;

finalize:
  /* Kill all (helper) processes that are still running. */
  for (i = 0; i < process_count; i++)
    process_terminate(&processes[i]);

  /* Wait until all processes have really terminated. */
  if (process_wait((process_info_t*)&processes, process_count, -1) < 0)
    FATAL("process_wait failed\n");

  /* Show error and output from processes if the test failed. */
  if (!success) {
    LOG("===============================================================================\n");
    LOGF("Test `%s` failed: %s\n", test->test_name, errmsg);
    for (i = 0; i < process_count; i++) {
      switch (process_output_size(&processes[i])) {
        case -1:
          LOGF("Output from process `%s`: << unavailable >>\n", process_get_name(&processes[i]));
          break;

        case 0:
          LOGF("Output from process `%s`: << no output >>\n", process_get_name(&processes[i]));
          break;

        default:
          LOGF("Output from process `%s`:\n", process_get_name(&processes[i]));
          process_copy_output(&processes[i], fileno(stderr));
          break;
      }
    }
    LOG("\n");
  }

  /* Clean up all process handles. */
  for (i = 0; i < process_count; i++)
    process_cleanup(&processes[i]);

  return success;
}


void log_progress(int total, int passed, int failed, char *name) {
  LOGF("[%% %3d|+ %3d|- %3d]: %-50s\n", (passed + failed) / total * 100, passed, failed, name);
}


int main(int argc, char **argv) {
  int total, passed, failed;
  test_entry_t *test;

#ifdef _WIN32
  /* On windows disable the "application crashed" popup */
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif

  /* Disable output buffering */
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  if (argc > 1) {
    /* A specific test process is being started. */
    for (test = (test_entry_t*)&TESTS; test->main; test++) {
      if (strcmp(argv[1], test->process_name) == 0)
        return test->main();
    }
    LOGF("Test process %s not found!\n", argv[1]);
    return 255;

  } else {
    /* Count the number of tests */
    total = 0;
    test = (test_entry_t*)&TESTS;
    for (test = (test_entry_t*)&TESTS; test->main; test++) {
      if (!test->is_helper)
        total++;
    }

    /* Run all tests */
    passed = 0;
    failed = 0;
    test = (test_entry_t*)&TESTS;
    for (test = (test_entry_t*)&TESTS; test->main; test++) {
      if (test->is_helper)
        continue;

      log_progress(total, passed, failed, test->test_name);
      rewind_cursor();

      if (run_test(test)) {
        passed++;
      } else {
        failed++;
      }
    }
    log_progress(total, passed, failed, "Done.");

    return 0;
  }
}