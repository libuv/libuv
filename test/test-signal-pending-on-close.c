/* Copyright libuv project contributors. All rights reserved.
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
#ifndef _WIN32

#include "uv.h"
#include "task.h"

#include <signal.h>
#include <string.h>
#include <unistd.h>

static uv_loop_t loop;
static uv_signal_t signal_hdl;
static uv_pipe_t pipe_hdl;
static uv_write_t write_req;
static char* buf;
static int close_cb_called;


static void stop_loop_cb(uv_signal_t* signal, int signum) {
  ASSERT(signum == SIGPIPE);
  uv_stop(signal->loop);
}

static void signal_cb(uv_signal_t* signal, int signum) {
  ASSERT(0);
}

static void close_cb(uv_handle_t *handle) {
  close_cb_called++;
}


static void write_cb(uv_write_t* req, int status) {
  ASSERT_NOT_NULL(req);
  ASSERT(status == UV_EPIPE);
  free(buf);
  uv_close((uv_handle_t *) &pipe_hdl, close_cb);
  uv_close((uv_handle_t *) &signal_hdl, close_cb);
}


TEST_IMPL(signal_pending_on_close) {
  int pipefds[2];
  uv_buf_t buffer;
  int r;

  ASSERT(0 == uv_loop_init(&loop));

  ASSERT(0 == uv_signal_init(&loop, &signal_hdl));

  ASSERT(0 == uv_signal_start(&signal_hdl, signal_cb, SIGPIPE));

  ASSERT(0 == pipe(pipefds));

  ASSERT(0 == uv_pipe_init(&loop, &pipe_hdl, 0));

  ASSERT(0 == uv_pipe_open(&pipe_hdl, pipefds[1]));

  /* Write data large enough so it needs loop iteration */
  buf = malloc(1<<24);
  ASSERT_NOT_NULL(buf);
  memset(buf, '.', 1<<24);
  buffer = uv_buf_init(buf, 1<<24);

  r = uv_write(&write_req, (uv_stream_t *) &pipe_hdl, &buffer, 1, write_cb);
  ASSERT(0 == r);

  /* cause a SIGPIPE on write in next iteration */
  close(pipefds[0]);

  ASSERT(0 == uv_run(&loop, UV_RUN_DEFAULT));

  ASSERT(0 == uv_loop_close(&loop));

  ASSERT(2 == close_cb_called);

  MAKE_VALGRIND_HAPPY();
  return 0;
}

static uv_sem_t wait_for_main_thread;
static uv_sem_t wait_for_worker_thread;
static unsigned main_thread_signal_count;

static int signal_handler_is_default(int signum) {
  struct sigaction oact;
  ASSERT(0 == sigaction(signum, NULL, &oact));
  return oact.sa_handler == SIG_DFL;
}

static void couting_singnal_handler(uv_signal_t* signal, int signum) {
  ASSERT(signal == &signal_hdl);
  ASSERT(signum == SIGUSR1);
  main_thread_signal_count++;
}

/* Check that we don't end up triggering UAF invocations of signal handlers. */
struct context_data {
  unsigned num_calls;
};
void worker_signal_handler(uv_signal_t* handle, int signum) {
  struct context_data* context = uv_handle_get_data((uv_handle_t*)handle);
  ASSERT_EQ(signum, handle->signum);
  ASSERT_EQ(1, handle->caught_signals);
  ASSERT_EQ(0, handle->dispatched_signals);
  ASSERT_EQ(0, context->num_calls); /* Should only be called once */
  context->num_calls++;
  uv_sem_post(&wait_for_worker_thread); /* Worker inside signal handler. */
  uv_sem_wait(&wait_for_main_thread); /* Wait for main to raise() again. */
  ASSERT_EQ(2, handle->caught_signals); /* Should have caught it again */
  uv_close((uv_handle_t*)handle, close_cb);
  /* after this signal handler caught_signals==1,dispatched_signals==2, which
   * can cause the loop to end up in an infinite looping state */
  free(context);
  /* Should still have a libuv handler */
  ASSERT(!signal_handler_is_default(SIGUSR1));
}

void worker_threadfunc(void* user_arg) {
  uv_loop_t worker_loop;
  uv_signal_t worker_sig;
  (void)user_arg;

  /* Create a signal handler with heap metadata to check we don't trigger UAF. */
  struct context_data* context = malloc(sizeof(struct context_data));
  ASSERT_NOT_NULL(context);
  context->num_calls = 0;
  uv_handle_set_data((uv_handle_t*)&worker_sig, context);
  ASSERT_EQ(0, uv_loop_init(&worker_loop));
  ASSERT_EQ(0, uv_signal_init(&worker_loop, &worker_sig));
  ASSERT(signal_handler_is_default(SIGUSR1));
  ASSERT_EQ(0, uv_signal_start(&worker_sig, worker_signal_handler, SIGUSR1));
  ASSERT(!signal_handler_is_default(SIGUSR1));

  uv_sem_post(&wait_for_worker_thread); /* Thread initialization done. */
  uv_sem_wait(&wait_for_main_thread); /* Wait for raise(SIGUSR). */
  ASSERT_EQ(0, close_cb_called);
#ifdef SIGNAL_HANDLER_BUG_HAS_BEEN_FIXED
  ASSERT_EQ(0, uv_run(&worker_loop, UV_RUN_ONCE));
#else
  ASSERT_EQ(1, uv_run(&worker_loop, UV_RUN_ONCE)); /* FIXME: this is wrong */
#endif
  uv_sem_post(&wait_for_worker_thread); /* Completed uv_run(). */

  ASSERT(!uv_is_active((uv_handle_t*)&worker_sig));
#ifdef SIGNAL_HANDLER_BUG_HAS_BEEN_FIXED
  ASSERT_EQ(1, close_cb_called);
  ASSERT_EQ(0, uv_loop_alive(&worker_loop));
  ASSERT_EQ(2, worker_sig.caught_signals);
  ASSERT_EQ(1, worker_sig.dispatched_signals);
#else
  /* FIXME: we have call uv_run once more to ensure close_cb was called. */
  ASSERT_EQ(0, close_cb_called);
  /* Check that we have fewer dispatched signals than caught signals. */
  ASSERT_EQ(2, worker_sig.caught_signals);
  ASSERT_EQ(1, worker_sig.dispatched_signals);
  ASSERT_EQ(0, uv_run(&worker_loop, UV_RUN_ONCE));
  ASSERT_EQ(2, worker_sig.caught_signals);
  ASSERT_EQ(2, worker_sig.dispatched_signals); /* FIXME: this is wrong */
#endif
  ASSERT_EQ(1, close_cb_called);
  ASSERT_EQ(0, uv_loop_alive(&worker_loop));
  ASSERT_EQ(0, uv_loop_close(&worker_loop));
}

TEST_IMPL(signal_delivered_in_handler) {
  uv_thread_t worker;

  ASSERT_EQ(0, close_cb_called);
  ASSERT_EQ(0, main_thread_signal_count);
  ASSERT(signal_handler_is_default(SIGUSR1));
  /* Set up semaphores for synchronization (to produce the race condition). */
  ASSERT_EQ(0, uv_sem_init(&wait_for_main_thread, 0));
  ASSERT_EQ(0, uv_sem_init(&wait_for_worker_thread, 0));
  ASSERT_EQ(0, uv_thread_create(&worker, worker_threadfunc, 0));

  uv_sem_wait(&wait_for_worker_thread); /* Wait for thread init */
  /* We also register a signal handler in the main thread to ensure that
   * we still have a signal handler registered with the kernel rather than
   * just deregistering it on the last uv_signal_stop().
   */
  ASSERT_EQ(0, uv_loop_init(&loop));
  ASSERT_EQ(0, uv_signal_init(&loop, &signal_hdl));
  ASSERT_EQ(0, uv_signal_start(&signal_hdl, couting_singnal_handler, SIGUSR1));
  ASSERT(!signal_handler_is_default(SIGUSR1));
  /* Both signal handlers have been registered, we can raise() now */
  raise(SIGUSR1);
  ASSERT_EQ(0, main_thread_signal_count); /* Main thread should not see it yet */
  uv_sem_post(&wait_for_main_thread); /* Worker can call uv_run() now. */
  uv_sem_wait(&wait_for_worker_thread); /* Wait until worker is in handler. */
  /* Worker is handling the signal, raise() again before it finishes. */
  raise(SIGUSR1);
  ASSERT_EQ(0, main_thread_signal_count); /* Main thread should not see it yet */
  uv_sem_post(&wait_for_main_thread); /* Tell worker we raise()'d again. */

  /* Wait for worker to finish. */
  uv_sem_wait(&wait_for_worker_thread); /* Worker called uv_run(). */

  uv_thread_join(&worker);

  /* clean up main thread. */
  ASSERT(!signal_handler_is_default(SIGUSR1));
  ASSERT_EQ(0, main_thread_signal_count); /* Main thread should not see it yet */
  ASSERT_EQ(1, uv_run(&loop, UV_RUN_ONCE));
  ASSERT_EQ(2, main_thread_signal_count); /* Signals should be processed now */
  ASSERT_EQ(1, uv_loop_alive(&loop));
  uv_close((uv_handle_t*)&signal_hdl, close_cb);
  ASSERT(signal_handler_is_default(SIGUSR1)); /* last handler deregistered. */
  ASSERT_EQ(0, uv_run(&loop, UV_RUN_ONCE)); /* Process close callback. */
  ASSERT_EQ(0, uv_loop_alive(&loop));
  ASSERT_EQ(0, uv_loop_close(&loop));
  ASSERT_EQ(2, close_cb_called);

  MAKE_VALGRIND_HAPPY();
  return 0;
}


TEST_IMPL(signal_close_loop_alive) {
  ASSERT(0 == uv_loop_init(&loop));
  ASSERT(0 == uv_signal_init(&loop, &signal_hdl));
  ASSERT(0 == uv_signal_start(&signal_hdl, stop_loop_cb, SIGPIPE));
  uv_unref((uv_handle_t*) &signal_hdl);

  ASSERT(0 == uv_kill(uv_os_getpid(), SIGPIPE));
  ASSERT(0 == uv_run(&loop, UV_RUN_DEFAULT));
  uv_close((uv_handle_t*) &signal_hdl, close_cb);
  ASSERT(1 == uv_loop_alive(&loop));

  ASSERT(0 == uv_run(&loop, UV_RUN_DEFAULT));
  ASSERT(0 == uv_loop_close(&loop));
  ASSERT(1 == close_cb_called);

  MAKE_VALGRIND_HAPPY();
  return 0;
}

#endif
