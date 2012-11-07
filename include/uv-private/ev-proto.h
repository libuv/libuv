/*
 * libev native API header
 *
 * Copyright (c) 2007,2008,2009,2010,2011 Marc Alexander Lehmann <libev@schmorp.de>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modifica-
 * tion, are permitted provided that the following conditions are met:
 *
 *   1.  Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *
 *   2.  Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MER-
 * CHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPE-
 * CIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTH-
 * ERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License ("GPL") version 2 or any later version,
 * in which case the provisions of the GPL are applicable instead of
 * the above. If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the BSD license, indicate your decision
 * by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL. If you do not delete the
 * provisions above, a recipient may use your version of this file under
 * either the BSD or the GPL.
 */

#ifndef EV_PROTO_H_
#define EV_PROTO_H_

#include "ev.h"

int ev_version_major (void);
int ev_version_minor (void);

unsigned int ev_supported_backends (void);
unsigned int ev_recommended_backends (void);
unsigned int ev_embeddable_backends (void);

ev_tstamp ev_time (void);
void ev_sleep (ev_tstamp delay); /* sleep for a while */

/* Sets the allocation function to use, works like realloc.
 * It is used to allocate and free memory.
 * If it returns zero when memory needs to be allocated, the library might abort
 * or take some potentially destructive action.
 * The default is your system realloc function.
 */
void ev_set_allocator (void *(*cb)(void *ptr, long size));

/* set the callback function to call on a
 * retryable syscall error
 * (such as failed select, poll, epoll_wait)
 */
void ev_set_syserr_cb (void (*cb)(const char *msg));

#if EV_MULTIPLICITY

/* the default loop is the only one that handles signals and child watchers */
/* you can call this as often as you like */
struct ev_loop *ev_default_loop (unsigned int flags EV_CPP (= 0));

EV_INLINE struct ev_loop *
EV_MAYBE_UNUSED ev_default_loop_uc_ (void)
{
  extern struct ev_loop *ev_default_loop_ptr;

  return ev_default_loop_ptr;
}

EV_INLINE int
EV_MAYBE_UNUSED ev_is_default_loop (EV_P)
{
  return EV_A == EV_DEFAULT_UC;
}

/* create and destroy alternative loops that don't handle signals */
struct ev_loop *ev_loop_new (unsigned int flags EV_CPP (= 0));

int ev_loop_refcount (EV_P);

ev_tstamp ev_now (EV_P); /* time w.r.t. timers and the eventloop, updated after each poll */

#else

int ev_default_loop (unsigned int flags EV_CPP (= 0)); /* returns true when successful */

EV_INLINE ev_tstamp
ev_now (void)
{
  extern ev_tstamp ev_rt_now;

  return ev_rt_now;
}

/* looks weird, but ev_is_default_loop (EV_A) still works if this exists */
EV_INLINE int
ev_is_default_loop (void)
{
  return 1;
}

#endif /* multiplicity */

/* destroy event loops, also works for the default loop */
void ev_loop_destroy (EV_P);

/* this needs to be called after fork, to duplicate the loop */
/* when you want to re-use it in the child */
/* you can call it in either the parent or the child */
/* you can actually call it at any time, anywhere :) */
void ev_loop_fork (EV_P);

unsigned int ev_backend (EV_P); /* backend in use by loop */

void ev_now_update (EV_P); /* update event loop time */

#if EV_WALK_ENABLE
/* walk (almost) all watchers in the loop of a given type, invoking the */
/* callback on every such watcher. The callback might stop the watcher, */
/* but do nothing else with the loop */
void ev_walk (EV_P_ int types, void (*cb)(EV_P_ int type, void *w));
#endif

void ev_run (EV_P_ int flags EV_CPP (= 0));
void ev_break (EV_P_ int how EV_CPP (= EVBREAK_ONE)); /* break out of the loop */

/*
 * ref/unref can be used to add or remove a refcount on the mainloop. every watcher
 * keeps one reference. if you have a long-running watcher you never unregister that
 * should not keep ev_loop from running, unref() after starting, and ref() before stopping.
 */
void ev_ref   (EV_P);
void ev_unref (EV_P);

/*
 * convenience function, wait for a single event, without registering an event watcher
 * if timeout is < 0, do wait indefinitely
 */
void ev_once (EV_P_ int fd, int events, ev_tstamp timeout, void (*cb)(int revents, void *arg), void *arg);

# if EV_FEATURE_API
unsigned int ev_iteration (EV_P); /* number of loop iterations */
unsigned int ev_depth     (EV_P); /* #ev_loop enters - #ev_loop leaves */
void         ev_verify    (EV_P); /* abort if loop data corrupted */

void ev_set_io_collect_interval (EV_P_ ev_tstamp interval); /* sleep at least this time, default 0 */
void ev_set_timeout_collect_interval (EV_P_ ev_tstamp interval); /* sleep at least this time, default 0 */

/* advanced stuff for threading etc. support, see docs */
void ev_set_userdata (EV_P_ void *data);
void *ev_userdata (EV_P);
void ev_set_invoke_pending_cb (EV_P_ void (*invoke_pending_cb)(EV_P));
void ev_set_loop_release_cb (EV_P_ void (*release)(EV_P), void (*acquire)(EV_P));

unsigned int ev_pending_count (EV_P); /* number of pending events, if any */
void ev_invoke_pending (EV_P); /* invoke all pending watchers */

/*
 * stop/start the timer handling.
 */
void ev_suspend (EV_P);
void ev_resume  (EV_P);
#endif /* EV_FEATURE_API */

/* stopping (enabling, adding) a watcher does nothing if it is already running */
/* stopping (disabling, deleting) a watcher does nothing unless its already running */

/* feeds an event into a watcher as if the event actually occured */
/* accepts any ev_watcher type */
void ev_feed_event     (EV_P_ void *w, int revents);
void ev_feed_fd_event  (EV_P_ int fd, int revents);
#if EV_SIGNAL_ENABLE
void ev_feed_signal    (int signum);
void ev_feed_signal_event (EV_P_ int signum);
#endif
void ev_invoke         (EV_P_ void *w, int revents);
int  ev_clear_pending  (EV_P_ void *w);

void ev_io_start       (EV_P_ ev_io *w);
void ev_io_stop        (EV_P_ ev_io *w);

void ev_timer_start    (EV_P_ ev_timer *w);
void ev_timer_stop     (EV_P_ ev_timer *w);
/* stops if active and no repeat, restarts if active and repeating, starts if inactive and repeating */
void ev_timer_again    (EV_P_ ev_timer *w);
/* return remaining time */
ev_tstamp ev_timer_remaining (EV_P_ ev_timer *w);

#if EV_PERIODIC_ENABLE
void ev_periodic_start (EV_P_ ev_periodic *w);
void ev_periodic_stop  (EV_P_ ev_periodic *w);
void ev_periodic_again (EV_P_ ev_periodic *w);
#endif

/* only supported in the default loop */
#if EV_SIGNAL_ENABLE
void ev_signal_start   (EV_P_ ev_signal *w);
void ev_signal_stop    (EV_P_ ev_signal *w);
#endif

/* only supported in the default loop */
# if EV_CHILD_ENABLE
void ev_child_start    (EV_P_ ev_child *w);
void ev_child_stop     (EV_P_ ev_child *w);
# endif

# if EV_STAT_ENABLE
void ev_stat_start     (EV_P_ ev_stat *w);
void ev_stat_stop      (EV_P_ ev_stat *w);
void ev_stat_stat      (EV_P_ ev_stat *w);
# endif

# if EV_IDLE_ENABLE
void ev_idle_start     (EV_P_ ev_idle *w);
void ev_idle_stop      (EV_P_ ev_idle *w);
# endif

#if EV_PREPARE_ENABLE
void ev_prepare_start  (EV_P_ ev_prepare *w);
void ev_prepare_stop   (EV_P_ ev_prepare *w);
#endif

#if EV_CHECK_ENABLE
void ev_check_start    (EV_P_ ev_check *w);
void ev_check_stop     (EV_P_ ev_check *w);
#endif

# if EV_FORK_ENABLE
void ev_fork_start     (EV_P_ ev_fork *w);
void ev_fork_stop      (EV_P_ ev_fork *w);
# endif

# if EV_CLEANUP_ENABLE
void ev_cleanup_start  (EV_P_ ev_cleanup *w);
void ev_cleanup_stop   (EV_P_ ev_cleanup *w);
# endif

# if EV_EMBED_ENABLE
/* only supported when loop to be embedded is in fact embeddable */
void ev_embed_start    (EV_P_ ev_embed *w);
void ev_embed_stop     (EV_P_ ev_embed *w);
void ev_embed_sweep    (EV_P_ ev_embed *w);
# endif

# if EV_ASYNC_ENABLE
void ev_async_start    (EV_P_ ev_async *w);
void ev_async_stop     (EV_P_ ev_async *w);
void ev_async_send     (EV_P_ ev_async *w);
# endif

#if EV_COMPAT3
  #define EVLOOP_NONBLOCK EVRUN_NOWAIT
  #define EVLOOP_ONESHOT  EVRUN_ONCE
  #define EVUNLOOP_CANCEL EVBREAK_CANCEL
  #define EVUNLOOP_ONE    EVBREAK_ONE
  #define EVUNLOOP_ALL    EVBREAK_ALL
    EV_INLINE void EV_MAYBE_UNUSED ev_loop   (EV_P_ int flags) { ev_run   (EV_A_ flags); }
    EV_INLINE void EV_MAYBE_UNUSED ev_unloop (EV_P_ int how  ) { ev_break (EV_A_ how  ); }
    EV_INLINE void EV_MAYBE_UNUSED ev_default_destroy (void) { ev_loop_destroy (EV_DEFAULT); }
    EV_INLINE void EV_MAYBE_UNUSED ev_default_fork    (void) { ev_loop_fork    (EV_DEFAULT); }
    #if EV_FEATURE_API
      EV_INLINE unsigned int EV_MAYBE_UNUSED ev_loop_count  (EV_P) { return ev_iteration  (EV_A); }
      EV_INLINE unsigned int EV_MAYBE_UNUSED ev_loop_depth  (EV_P) { return ev_depth      (EV_A); }
      EV_INLINE void         EV_MAYBE_UNUSED ev_loop_verify (EV_P) {        ev_verify     (EV_A); }
    #endif
#else
  typedef struct ev_loop ev_loop;
#endif

#endif /* EV_PROTO_H_ */
