
.. _threadpool:

Thread pool work scheduling
===========================

libuv provides a threadpool which can be used to run user code and get notified
in the loop thread. This thread pool is internally used to run all file system
operations, as well as getaddrinfo and getnameinfo requests.

Its default size is 4, but it can be changed at startup time by setting the
``UV_THREADPOOL_SIZE`` environment variable to any value (the absolute maximum
is 1024).

.. versionchanged:: 1.30.0 the maximum UV_THREADPOOL_SIZE allowed was increased from 128 to 1024.

.. versionchanged:: 1.45.0 threads now have an 8 MB stack instead of the
   (sometimes too low) platform default.

.. versionchanged:: 1.46.0 :c:func:`uv_queue_work` is now thread-safe and can
   be used to queue work on the threadpool or event loop thread individually.

The threadpool is global and shared across all event loops. When a particular
function makes use of the threadpool (i.e. when using :c:func:`uv_queue_work`)
libuv preallocates and initializes the maximum number of threads allowed by
``UV_THREADPOOL_SIZE``. This causes a relatively minor memory overhead
(~1MB for 128 threads) but increases the performance of threading at runtime.

.. note::
    Note that even though a global thread pool which is shared across all events
    loops is used, the functions are not thread safe.


Data types
----------

.. c:type:: uv_work_t

    Work request type.

.. c:type:: void (*uv_work_cb)(uv_work_t* req)

    Callback passed to :c:func:`uv_queue_work` which will be run on the thread
    pool.

.. c:type:: void (*uv_after_work_cb)(uv_work_t* req, int status)

    Callback passed to :c:func:`uv_queue_work` which will be called on the loop
    thread after the work on the threadpool has been completed. If the work
    was cancelled using :c:func:`uv_cancel` `status` will be ``UV_ECANCELED``.


Public members
^^^^^^^^^^^^^^

.. c:member:: uv_loop_t* uv_work_t.loop

    Loop that started this request and where completion will be reported. If
    :c:func:`uv_queue_work` is called without the `after_work_cb` then the value
    is ``NULL``.
    Readonly.

.. seealso:: The :c:type:`uv_req_t` members also apply.


API
---

.. c:function:: int uv_queue_work(uv_loop_t* loop, uv_work_t* req, uv_work_cb work_cb, uv_after_work_cb after_work_cb)

    Initializes a work request which will run the given `work_cb` in a thread
    from the threadpool. Once `work_cb` is completed, `after_work_cb` will be
    called on the loop thread.

    This request can be cancelled with :c:func:`uv_cancel` which will cancel
    the execution of `work_cb` and call the `after_work_cb` with a `status` of
    ``UV_ECANCELED``.

    As of v1.46.0 :c:func:`uv_queue_work` is now thread-safe and can be called
    from any thread to queue work to be done on the threadpool. Then the thread
    corresponding to `loop` will call the `after_work_cb`. In addition, the
    user can pass in only the `work_cb` or `after_work_cb` to either have the
    callback only run on the threadpool or event loop thread.

    If only `work_cb` is specified then `loop` must be ``NULL``. The `work_cb`
    will then be executed from the threadpool.  Never call :c:func:`uv_cancel`
    on this `req` as it is not thread-safe to do so. Attempting to cancel the
    `req` will return ``UV_EINVAL``, but could result in attempted invalid
    memory access if the `req` is freed during the call.

    Before `work_cb` is called it will be removed from the internal queue and
    the user can then either free or reuse the `req` however they want. In the
    following example we fire-and-forget a request that doesn't need to report
    back to the event loop thread whether it succeeded:

.. code-block:: c

    void work_cb(uv_work_t* req) {
      /* Do the work. */
      if (is_work_complete)
        free(req);
      else
        /* Still more to do, so call into the threadpool directly once again by
         * reusing the same req. */
        uv_queue_work(NULL, req, work_cb, NULL);
    }

    void call_work() {
      uv_work_t* req = malloc(sizeof(*req));
      uv_queue_work(NULL, req, work_cb, NULL);
    }

..

    If only the `after_work_cb` is supplied then the callback is placed on the
    internal queue of the provided `loop` to run on the corresponding thread
    during event loop execution. Calling :c:func:`uv_cancel` on this `req` will
    result in the `after_work_cb` not being called. The call to
    :c:func:`uv_cancel` must be done from the same thread as the provided
    `loop`.

    In the following example we spawn a new thread then place a callback to be
    run by the default event loop from that thread:

.. code-block:: c

    void after_work_cb(uv_work_t* req, int status) {
      /* This is called from the event loop thread. */
      free(req);
    }

    void spawned_cb(void* arg) {
      uv_work_t* req = malloc(sizeof(*req));
      /* Have the specified event loop thread run a callback. */
      uv_queue_work((uv_loop_t*) arg, req, NULL, after_work_cb);
    }

    void run() {
      uv_thread_t thread;

      uv_thread_create(&thread, spawned_cb, uv_default_loop());
      /* Join the thread first to make sure req has been added. */
      uv_thread_join(&thread);
      uv_run(uv_default_loop(), UV_RUN_DEFAULT);
    }

..

    In either case it's important that the user be aware of the lifetime of all
    resources being used so that a `loop` or `req` is not accessed that has
    already been freed.

.. note:: Calling :c:func:`uv_cancel` on a `req` that doesn't have an
   `after_work_cb` will return ``UV_EINVAL``. :c:func:`uv_cancel` is not
   thread-safe and thus it is impossible to cancel a `req` that is not
   associated with a `loop`.

.. seealso:: The :c:type:`uv_req_t` API functions also apply.
