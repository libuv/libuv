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

#ifndef RUNNER_H_
#define RUNNER_H_

#include <limits.h> /* PATH_MAX 编译器所支持最长全路径的长度*/
#include <stdio.h> /* FILE */


/* 一个 test / benchmark可以拥有的最大进程数(main + helpers)*/
#define MAX_PROCESSES 8


/* 保存所有tests的结构 */
typedef struct {
  char *task_name;
  char *process_name;
  int (*main)(void);
  int is_helper;
  int show_output;
  int timeout;
} task_entry_t, bench_entry_t;


/* test-list.h | benchmark-list.h.
* 下面的两个宏结合可初始化task列表
* 最后一个task是全为0的task
*/

#define TASK_LIST_START                             \
  task_entry_t TASKS[] = {

#define TASK_LIST_END                               \
    { 0, 0, 0, 0, 0, 0 }                               \
  };

//sugar按照上述的结构体构建
#define TEST_DECLARE(name)                          \
  int run_test_##name(void);

#define TEST_ENTRY(name)                            \
    { #name, #name, &run_test_##name, 0, 0, 5000 },

#define TEST_ENTRY_CUSTOM(name, is_helper, show_output, timeout) \
    { #name, #name, &run_test_##name, is_helper, show_output, timeout },

#define BENCHMARK_DECLARE(name)                     \
  int run_benchmark_##name(void);

#define BENCHMARK_ENTRY(name)                       \
    { #name, #name, &run_benchmark_##name, 0, 0, 60000 },

#define HELPER_DECLARE(name)                        \
  int run_helper_##name(void);

#define HELPER_ENTRY(task_name, name)               \
    { #task_name, #name, &run_helper_##name, 1, 0, 0 },

#define TEST_HELPER       HELPER_ENTRY
#define BENCHMARK_HELPER  HELPER_ENTRY

//执行路径
#ifdef PATH_MAX
extern char executable_path[PATH_MAX];
#else
extern char executable_path[4096];
#endif

/* 平台特定的文件 */
#ifdef _WIN32
# include "runner-win.h"
#else
# include "runner-unix.h"
#endif


/* 在test-list.h | benchmark-list.h 中被填充*/
extern task_entry_t TASKS[];

/* 运行所有测试 */
int run_tests(int benchmark_output);

/* 运行一个 */
int run_test(const char* test,
             int benchmark_output,
             int test_count);

/* 运行一个测试的部分 */
int run_test_part(const char* test, const char* part);


/* 打印测试列表 */
void print_tests(FILE* stream);


/* 下述的东西是在runner win下实现的 */

/* 平台特定初始化 */
int platform_init(int argc, char** argv);

/* 调用"argv[0] test-name [test-part]" */
int process_start(char *name, char* part, process_info_t *p, int is_helper);

/* 等待vec中的n个进程终结 */
/* Time out 设为-1一直等待 */
/* 全部终结返回0， -1错误， -2 超时 */
int process_wait(process_info_t *vec, int n, int timeout);

/* 为该进程缓存的字节数 */
long int process_output_size(process_info_t *p);

/* 复制输出缓存到文件`fd`. */
int process_copy_output(process_info_t *p, int fd);

/* 复制输出缓存的最后一行到`buffer` */
int process_read_last_line(process_info_t *p,
                           char * buffer,
                           size_t buffer_len);

/* 进程名 */
char* process_get_name(process_info_t *p);

/* 终结进程 */
int process_terminate(process_info_t *p);

/* 返回退出码 */
int process_reap(process_info_t *p);

/* 终结进程后的清理 */
void process_cleanup(process_info_t *p);

/* 光标移动到上一行开头 */
void rewind_cursor(void);

/* 标识输出的 */
extern int tap_output;

#endif /* RUNNER_H_ */
