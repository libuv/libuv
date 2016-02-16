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

#include <limits.h> /* PATH_MAX ��������֧���ȫ·���ĳ���*/
#include <stdio.h> /* FILE */


/* һ�� test / benchmark����ӵ�е���������(main + helpers)*/
#define MAX_PROCESSES 8


/* ��������tests�Ľṹ */
typedef struct {
  char *task_name;
  char *process_name;
  int (*main)(void);
  int is_helper;
  int show_output;
  int timeout;
} task_entry_t, bench_entry_t;


/* test-list.h | benchmark-list.h.
* ������������Ͽɳ�ʼ��task�б�
* ���һ��task��ȫΪ0��task
*/

#define TASK_LIST_START                             \
  task_entry_t TASKS[] = {

#define TASK_LIST_END                               \
    { 0, 0, 0, 0, 0, 0 }                               \
  };

//sugar���������Ľṹ�幹��
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

//ִ��·��
#ifdef PATH_MAX
extern char executable_path[PATH_MAX];
#else
extern char executable_path[4096];
#endif

/* ƽ̨�ض����ļ� */
#ifdef _WIN32
# include "runner-win.h"
#else
# include "runner-unix.h"
#endif


/* ��test-list.h | benchmark-list.h �б����*/
extern task_entry_t TASKS[];

/* �������в��� */
int run_tests(int benchmark_output);

/* ����һ�� */
int run_test(const char* test,
             int benchmark_output,
             int test_count);

/* ����һ�����ԵĲ��� */
int run_test_part(const char* test, const char* part);


/* ��ӡ�����б� */
void print_tests(FILE* stream);


/* �����Ķ�������runner win��ʵ�ֵ� */

/* ƽ̨�ض���ʼ�� */
int platform_init(int argc, char** argv);

/* ����"argv[0] test-name [test-part]" */
int process_start(char *name, char* part, process_info_t *p, int is_helper);

/* �ȴ�vec�е�n�������ս� */
/* Time out ��Ϊ-1һֱ�ȴ� */
/* ȫ���ս᷵��0�� -1���� -2 ��ʱ */
int process_wait(process_info_t *vec, int n, int timeout);

/* Ϊ�ý��̻�����ֽ��� */
long int process_output_size(process_info_t *p);

/* ����������浽�ļ�`fd`. */
int process_copy_output(process_info_t *p, int fd);

/* ���������������һ�е�`buffer` */
int process_read_last_line(process_info_t *p,
                           char * buffer,
                           size_t buffer_len);

/* ������ */
char* process_get_name(process_info_t *p);

/* �ս���� */
int process_terminate(process_info_t *p);

/* �����˳��� */
int process_reap(process_info_t *p);

/* �ս���̺������ */
void process_cleanup(process_info_t *p);

/* ����ƶ�����һ�п�ͷ */
void rewind_cursor(void);

/* ��ʶ����� */
extern int tap_output;

#endif /* RUNNER_H_ */
