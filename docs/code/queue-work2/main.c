#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <uv.h>

#define MAX_FIB 100000

/* loop needs to be assigned NULL before the threadpool is started. */
uv_loop_t* loop = NULL;
uv_mutex_t loop_lock;
uv_timer_t timer;
struct timespec random_time;
size_t fib_vals[2] = { 1, 0 };  /* [F-1, F-2] */

static void timer_complete(uv_timer_t* timer) {
    fprintf(stderr, "Timer reached, ending execution.\n");
    uv_close((uv_handle_t*) timer, NULL);
}

static void after_fib(uv_work_t* req, int status) {
    fprintf(stderr, "Fib calculation completed.\n");
    /* Fib calculation completed, so cancel the timer and allow uv_run() to
     * end naturally. */
    uv_close((uv_handle_t*) &timer, NULL);
    free(req);
}

static void fib_cb(uv_work_t* req) {
    size_t* fib_vals = req->data;
    size_t fib;

    /* Force a small amount of random delay for academic purposes. */
    nanosleep(&random_time, NULL);

    fib = fib_vals[0] + fib_vals[1];

    if (fib > MAX_FIB) {
        /* Gain lock to make sure it's safe to continue. */
        uv_mutex_lock(&loop_lock);
        /* Make sure loop is alive, otherwise no loop exists for us to queue a
         * callback on. */
        if (loop != NULL) {
            /* Reached the maximum size, so close everything out. */
            uv_queue_work(loop, req, NULL, after_fib);
            req = NULL;
        }
        uv_mutex_unlock(&loop_lock);
        free(req);
        return;
    }

    fib_vals[1] = fib_vals[0];
    fib_vals[0] = fib;

    /* Gain lock to make sure it's safe to continue. */
    uv_mutex_lock(&loop_lock);
    /* Make sure loop is alive, otherwise no reason to continue. This check
     * could technically be done with an atomic, but since all other usage
     * requires a lock it'll be used here too. */
    if (loop != NULL) {
        /* Queue up next work to process next step in calculation. */
        uv_queue_work(NULL, req, fib_cb, NULL);
        req = NULL;
    }
    uv_mutex_unlock(&loop_lock);
    free(req);
}

static void thread_cb(void* arg) {
    uv_loop_t main_loop;
    uv_work_t* req = malloc(sizeof(*req));

    /* Not technically necessary here, but better to be safe by future-proofing
     * any changes that may have the threadpool attempt accessing the loop
     * before it's ready. */
    uv_mutex_lock(&loop_lock);
    loop = &main_loop;
    uv_loop_init(loop);
    uv_mutex_unlock(&loop_lock);

    /* The timer serves two purposes. First is to keep the event loop alive.
     * Second is to terminate the process if the work takes too long. Making it
     * global since it's important for the lifetime of the process. */
    uv_timer_init(loop, &timer);
    uv_timer_start(&timer, timer_complete, 1000, 0);

    req->data = &fib_vals;

    /* Start doing work on only the threadpool. Since this is not related to
     * an individual event loop, it will not keep uv_run() open. */
    uv_queue_work(NULL, req, fib_cb, NULL);

    /* We want the spawning thread to be aware of the return value, so store it
     * on the pointer. */
    *(int*) arg = uv_run(loop, UV_RUN_DEFAULT);

    /* We are done using the loop, so close it an reassign the pointer. This
     * will let future threadpool callback calls know that there's nothing else
     * to be done. */
    uv_mutex_lock(&loop_lock);
    uv_loop_close(loop);
    loop = NULL;
    uv_mutex_unlock(&loop_lock);
}

int main() {
    uv_thread_t thread;
    uv_random_t rreq;
    size_t buf[1] = { 0 };
    int ret;

    /* Generate random time that will be used to sleep to simulate work done on
     * threadpool. Random value has been tested to give a fairly good
     * distribution of completions. */
    uv_random(NULL, &rreq, buf, sizeof(buf), 0, NULL);
    random_time.tv_nsec = (buf[0] % 80 + 20) * 1000000;
    random_time.tv_sec = 0;

    /* This will be used to control loop access from another thread, but init
     * it here since it will be used by the threadpool. Which will out live the
     * thread we spawn. */
    uv_mutex_init(&loop_lock);

    /* Academic exercise of running all the code from another thread. */
    uv_thread_create(&thread, thread_cb, &ret);
    uv_thread_join(&thread);

    /* Must run this first to join all threads from the threadpool. */
    uv_library_shutdown();
    /* Now the lock can be safely destroyed since we know it won't be used by
     * the threadpool. */
    uv_mutex_destroy(&loop_lock);

    /* Can't print until here otherwise there's still a chance that a worker
     * thread will write to fib_vals. */
    fprintf(stderr, "Max fibonacci value calculated was: %lu\n", fib_vals[0]);

    return ret;
}
