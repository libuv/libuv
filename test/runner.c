/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>

#include "runner.h"
#include "task.h"
#include "uv.h"

char executable_path[sizeof(executable_path)];

int tap_output = 0;

//进程日志输出
static void log_progress(int total,
                         int passed,
                         int failed,
                         int todos,
                         int skipped,
                         const char* name) {
  int progress;

  if (total == 0)
    total = 1;

  progress = 100 * (passed + failed + skipped + todos) / total;
  fprintf(stderr, "[%% %3d|+ %3d|- %3d|T %3d|S %3d]: %s",
          progress,
          passed,
          failed,
          todos,
          skipped,
          name);
  fflush(stderr);
}


const char* fmt(double d) {
  static char buf[1024];
  static char* p;
  uint64_t v;

  if (p == NULL)
    p = buf;

  p += 31;

  if (p >= buf + sizeof(buf))
    return "<buffer too small>";

  v = (uint64_t) d;

#if 0 /* works but we don't care about fractional precision */
  if (d - v >= 0.01) {
    *--p = '0' + (uint64_t) (d * 100) % 10;
    *--p = '0' + (uint64_t) (d * 10) % 10;
    *--p = '.';
  }
#endif

  if (v == 0)
    *--p = '0';

  while (v) {
    if (v) *--p = '0' + (v % 10), v /= 10;
    if (v) *--p = '0' + (v % 10), v /= 10;
    if (v) *--p = '0' + (v % 10), v /= 10;
    if (v) *--p = ',';
  }

  return p;
}


int run_tests(int benchmark_output) {
	//一般是runtests（0）
  int total;
  int passed;
  int failed;
  int todos;
  int skipped;
  int current;
  int test_result;
  task_entry_t* task;

  /* 遍历task 计数 这个是在testlist里填充的
  typedef struct {
  char *task_name; //任务名称
  char *process_name; //进程名称
  int (*main)(void); //主函数
  int is_helper; //是否为helper
  int show_output; 
  int timeout;
  } task_entry_t, bench_entry_t;
  */

  total = 0;//总数
  for (task = TASKS; task->main; task++) {
    if (!task->is_helper) {
      total++;
    }
  }


  if (tap_output) {
    fprintf(stderr, "1..%d\n", total);
    fflush(stderr);
  }

  /* 运行所有测试 */
  passed = 0;
  failed = 0;
  todos = 0;
  skipped = 0;
  current = 1;
  for (task = TASKS; task->main; task++) {
    if (task->is_helper) {
      continue;
    }

    if (!tap_output)
      rewind_cursor();

    if (!benchmark_output && !tap_output) {
      log_progress(total, passed, failed, todos, skipped, task->task_name);
    }

    test_result = run_test(task->task_name, benchmark_output, current);// 运行
    switch (test_result) {
    case TEST_OK: passed++; break;
    case TEST_TODO: todos++; break;
    case TEST_SKIP: skipped++; break;
    default: failed++;
    }
    current++;
  }

  if (!tap_output)
    rewind_cursor();

  if (!benchmark_output && !tap_output) {
    log_progress(total, passed, failed, todos, skipped, "Done.\n");
  }

  return failed;
}

//tap 日志输出
void log_tap_result(int test_count,
                    const char* test,
                    int status,
                    process_info_t* process) {
  const char* result;
  const char* directive;
  char reason[1024];

  //根据状态设置相应的日志
  switch (status) {
  case TEST_OK:
    result = "ok";
    directive = "";
    break;
  case TEST_TODO:
    result = "not ok";
    directive = " # TODO ";
    break;
  case TEST_SKIP:
    result = "ok";
    directive = " # SKIP ";
    break;
  default:
    result = "not ok";
    directive = "";
  }

  if ((status == TEST_SKIP || status == TEST_TODO) &&
      process_output_size(process) > 0) {
    process_read_last_line(process, reason, sizeof reason);
  } else {
    reason[0] = '\0';
  }

  fprintf(stderr, "%s %d - %s%s%s\n", result, test_count, test, directive, reason);
  fflush(stderr);
}


int run_test(const char* test,
             int benchmark_output,
             int test_count) {
  char errmsg[1024] = "no error";
  process_info_t processes[1024];
  process_info_t *main_proc;
  task_entry_t* task;
  int process_count;
  int result;
  int status;
  int i;

  status = 255;
  main_proc = NULL;
  process_count = 0;

#ifndef _WIN32
  /* Clean up stale socket from previous run. */
  remove(TEST_PIPENAME);
  remove(TEST_PIPENAME_2);
  remove(TEST_PIPENAME_3);
#endif

  /* 如果用户调用的helper直接启动 */
  for (task = TASKS; task->main; task++) {
    if (task->is_helper && strcmp(test, task->process_name) == 0) {
      return task->main();
    }
  }

  /* 先启动helper */
  for (task = TASKS; task->main; task++) {
    if (strcmp(test, task->task_name) != 0) {
      continue;
    }

    /* 先跳过测试 */
    if (!task->is_helper) {
      continue;
    }
	//开进程
    if (process_start(task->task_name,
                      task->process_name,
                      &processes[process_count],
                      1 /* is_helper */) == -1) {
      snprintf(errmsg,
               sizeof errmsg,
               "Process `%s` failed to start.",
               task->process_name);
      goto out;
    }

    process_count++;
  }

  /* Give the helpers time to settle. Race-y, fix this. */
  uv_sleep(250);

  /* 现在启动测试 */
  for (task = TASKS; task->main; task++) {
    if (strcmp(test, task->task_name) != 0) {
      continue;
    }

    if (task->is_helper) {
      continue;
    }

    if (process_start(task->task_name,
                      task->process_name,
                      &processes[process_count],
                      0 /* !is_helper */) == -1) {
      snprintf(errmsg,
               sizeof errmsg,
               "Process `%s` failed to start.",
               task->process_name);
      goto out;
    }

    main_proc = &processes[process_count];
    process_count++;
    break;
  }

  if (main_proc == NULL) {
    snprintf(errmsg,
             sizeof errmsg,
             "No test with that name: %s",
             test);
    goto out;
  }

  result = process_wait(main_proc, 1, task->timeout);
  if (result == -1) {
    FATAL("process_wait failed");
  } else if (result == -2) {
    /* Don't have to clean up the process, process_wait() has killed it. */
    snprintf(errmsg,
             sizeof errmsg,
             "timeout");
    goto out;
  }

  status = process_reap(main_proc);
  if (status != TEST_OK) {
    snprintf(errmsg,
             sizeof errmsg,
             "exit code %d",
             status);
    goto out;
  }

  if (benchmark_output) {
    /* Give the helpers time to clean up their act. */
    uv_sleep(1000);
  }

out:
  /* Reap running processes except the main process, it's already dead. */
  for (i = 0; i < process_count - 1; i++) {
    process_terminate(&processes[i]);
  }

  if (process_count > 0 &&
      process_wait(processes, process_count - 1, -1) < 0) {
    FATAL("process_wait failed");
  }

  if (tap_output)
    log_tap_result(test_count, test, status, &processes[i]);

  /* Show error and output from processes if the test failed. */
  if (status != 0 || task->show_output) {
    if (tap_output) {
      fprintf(stderr, "#");
    } else if (status == TEST_TODO) {
      fprintf(stderr, "\n`%s` todo\n", test);
    } else if (status == TEST_SKIP) {
      fprintf(stderr, "\n`%s` skipped\n", test);
    } else if (status != 0) {
      fprintf(stderr, "\n`%s` failed: %s\n", test, errmsg);
    } else {
      fprintf(stderr, "\n");
    }
    fflush(stderr);

    for (i = 0; i < process_count; i++) {
      switch (process_output_size(&processes[i])) {
       case -1:
        fprintf(stderr, "Output from process `%s`: (unavailable)\n",
                process_get_name(&processes[i]));
        fflush(stderr);
        break;

       case 0:
        fprintf(stderr, "Output from process `%s`: (no output)\n",
                process_get_name(&processes[i]));
        fflush(stderr);
        break;

       default:
        fprintf(stderr, "Output from process `%s`:\n", process_get_name(&processes[i]));
        fflush(stderr);
        process_copy_output(&processes[i], fileno(stderr));
        break;
      }
    }

    if (!tap_output) {
      fprintf(stderr, "=============================================================\n");
    }

  /* In benchmark mode show concise output from the main process. */
  } else if (benchmark_output) {
    switch (process_output_size(main_proc)) {
     case -1:
      fprintf(stderr, "%s: (unavailable)\n", test);
      fflush(stderr);
      break;

     case 0:
      fprintf(stderr, "%s: (no output)\n", test);
      fflush(stderr);
      break;

     default:
      for (i = 0; i < process_count; i++) {
        process_copy_output(&processes[i], fileno(stderr));
      }
      break;
    }
  }

  /* 清理进程 handles. */
  for (i = 0; i < process_count; i++) {
    process_cleanup(&processes[i]);
  }

  return status;
}


/* 返回任务状态，255说明没有找到 */
int run_test_part(const char* test, const char* part) {
  task_entry_t* task;
  int r;

  for (task = TASKS; task->main; task++) {
    if (strcmp(test, task->task_name) == 0 &&
        strcmp(part, task->process_name) == 0) {
      r = task->main();
      return r;
    }
  }

  fprintf(stderr, "No test part with that name: %s:%s\n", test, part);
  fflush(stderr);
  return 255;
}

//比较两个task 即比较两个task的名称
static int compare_task(const void* va, const void* vb) {
  const task_entry_t* a = va;
  const task_entry_t* b = vb;
  return strcmp(a->task_name, b->task_name);
}

//找到对应 task 的 helper（可能有多个对应的helper）返回个数
static int find_helpers(const task_entry_t* task,
                        const task_entry_t** helpers) {
  const task_entry_t* helper;
  int n_helpers;

  for (n_helpers = 0, helper = TASKS; helper->main; helper++) {
    if (helper->is_helper && strcmp(helper->task_name, task->task_name) == 0) {
      *helpers++ = helper;
      n_helpers++;
    }
  }

  return n_helpers;
}

//打印test
void print_tests(FILE* stream) {
  const task_entry_t* helpers[1024];
  const task_entry_t* task;
  int n_helpers;
  int n_tasks;
  int i;

  //将task排序 计数 
  for (n_tasks = 0, task = TASKS; task->main; n_tasks++, task++);
  qsort(TASKS, n_tasks, sizeof(TASKS[0]), compare_task);

  //打印task helper（如果有的话）
  for (task = TASKS; task->main; task++) {
    if (task->is_helper) {
      continue;
    }

    n_helpers = find_helpers(task, helpers);
    if (n_helpers) {
      printf("%-25s (helpers:", task->task_name);
      for (i = 0; i < n_helpers; i++) {
        printf(" %s", helpers[i]->process_name);
      }
      printf(")\n");
    } else {
      printf("%s\n", task->task_name);
    }
  }
}
