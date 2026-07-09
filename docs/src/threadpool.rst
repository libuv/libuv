
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

.. versionchanged:: 1.50.0 threads now have a default name of libuv-worker.

The threadpool is global and shared across all event loops. When a particular
function makes use of the threadpool (e.g. when using :c:func:`uv_queue_work`)
libuv preallocates and initializes the maximum number of threads allowed by
``UV_THREADPOOL_SIZE``. More threads usually means more throughput but a higher
memory footprint. Thread stacks grow lazily on most platforms though.

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

    Loop that started this request and where completion will be reported.
    Readonly.

.. seealso:: The :c:type:`uv_req_t` members also apply.


API
---

.. c:function:: int uv_queue_work(uv_loop_t* loop, uv_work_t* req, uv_work_cb work_cb, uv_after_work_cb after_work_cb)

    Initializes a work request which will run the given `work_cb` in a thread
    from the threadpool. Once `work_cb` is completed, `after_work_cb` will be
    called on the loop thread.

    This request can be cancelled with :c:func:`uv_cancel`.

.. c:function:: int uv_threadpool_set_cancel_signal(int signum)

    Set the signal used to interrupt in-progress work on the thread pool, or
    pass zero or a negative value to disable interruption.  When configured,
    pending writes to blocking streams can be cancelled by libuv when the stream
    is closed by sending the worker thread this signal.  If this isn't
    configured, thread pool workers can be blocked indefinitely while waiting
    for a write to finish.  See the note on :c:enum:`uv_stdio_flags`.

    The signal must have a handler installed (it is only important that a
    handler exist, so the write can be interrupted and return ``EINTR``) and
    ``SA_RESTART`` must not be set.

    The setting is global because the thread pool is shared by all loops in the
    process: the most recently configured value applies to every loop.  Worker
    threads unblock the signal when they start, in case it is masked in the
    thread that starts the pool.  Call this function before the thread pool
    starts.

    Returns ``UV_ENOSYS`` on Windows.

    .. versionadded:: 1.52.0

.. seealso:: The :c:type:`uv_req_t` API functions also apply.
