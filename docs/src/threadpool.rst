
.. _threadpool:

Threadpool
===========================

libuv provides the notion of an executor for asynchronous work.
An executor runs work off of the loop thread and delivers a notification
to the event loop when the work is finished.

Users can submit work directly to the executor via
:c:func:`uv_executor_queue_work`.

libuv will also convert certain asynchronous requests into work for the
executor when appropriate OS-level facilities are unavailable.
This includes asynchronous file system operations and DNS requests
(getaddrinfo, getnameinfo).
All such internally-generated work is submitted to the public
executor API.

libuv offers a default executor called the threadpool.
This pool maintains a pool of worker threads that consume pending work in
a FIFO manner.
By default there are 4 threads in this pool.
The size can be controlled by setting the ``UV_THREADPOOL_SIZE`` environment
variable to the desired number of threads.

libuv also permits users to replace the default executor with their own
implementation via :c:func:`uv_executor_replace`.
Users may thus define a more sophisticated executor if desired,
e.g. handling I/O and CPU in different pools.

.. note::
    The default executor must be overridden prior to queuing any work on it.

The executor is global and shared across all event loops.

When a function makes use of the default executor (i.e. when using :c:func:`uv_queue_work`)
libuv preallocates and initializes the maximum number of threads allowed by
``UV_THREADPOOL_SIZE``. This causes a relatively minor memory overhead
(~1MB for 128 threads) but increases the performance of threading at runtime.

.. note::
    Even though a global thread pool which is shared across all events
    loops is used, the functions are not thread safe.


Data types
----------

.. c:type:: uv_work_t

    Work request type.

.. c:type:: void (*uv_work_cb)(uv_work_t* req)

    Callback passed to :c:func:`uv_queue_work` which will be run on
    the executor.

.. c:type:: void (*uv_after_work_cb)(uv_work_t* req, int status)

    Callback passed to :c:func:`uv_queue_work` which will be called on the loop
    thread after the work on the threadpool has been completed. If the work
    was cancelled using :c:func:`uv_cancel` `status` will be ``UV_ECANCELED``.

.. c:type:: uv_executor_t

    Executor type. Use when overriding the default threadpool.

.. c:type:: void (*uv_executor_submit_func)(uv_work_t* req, uv_work_options_t* opts)

    Instruct the executor to eventually handle this request off of the loop thread.

.. c:type:: int (*uv_executor_cancel_func)(uv_work_t* req)

    Instruct the executor to cancel this request.

.. seealso:: :c:func:`uv_cancel_t`.

.. c:type:: int (*uv_executor_replace)(uv_executor_t* executor)

    Replace the default libuv executor with this user-defined one.
    Must be called before any work is submitted to the default libuv executor.

.. c:type:: uv_work_options_t

    Options for guiding the executor in its policy decisions.

Public members
^^^^^^^^^^^^^^

.. c:member:: uv_loop_t* uv_work_t.loop

    Loop that started this request and where completion will be reported.
    Readonly.

.. seealso:: The :c:type:`uv_req_t` members also apply.

.. c:member:: uv_executor_submit_func uv_executor_t.submit

.. c:member:: uv_executor_cancel_func uv_executor_t.cancel

.. c:member:: uv_work_type uv_work_options_t.type

    Type of request. Readonly.

    ::

        typedef enum {
            UV_WORK_UNKNOWN = 0,
            UV_WORK_FS,
            UV_WORK_DNS,
            UV_WORK_USER_IO,
            UV_WORK_USER_CPU,
            UV_WORK_PRIVATE,
            UV_WORK_TYPE_MAX
        } uv_work_type;

.. c:member:: int uv_work_options_t.priority

    Suggested for use in user-defined executor.
    Has no effect on libuv's default executor.

.. c:member:: int uv_work_options_t.cancelable

    Boolean.
    If truth-y, it is safe to abort this work while it is being handled
    by a thread.
    In addition, work that has not yet been assigned to a thread can be
    cancelled.

.. c:member:: void * uv_work_options_t.data

    Space for user-defined arbitrary data. libuv does not use this field.


API
---

.. c:function:: int uv_queue_work(uv_loop_t* loop, uv_work_t* req, uv_work_cb work_cb, uv_after_work_cb after_work_cb)

    Calls :c:func:`uv_executor_queue_work` with NULL options.
    This API is deprecated.
    Use :c:func:`uv_executor_queue_work` instead.

.. c:function:: int uv_executor_queue_work(uv_loop_t* loop, uv_work_t* req, uv_work_options_t* opts, uv_work_cb work_cb, uv_after_work_cb after_work_cb)

    Submits a work request with options to the executor.
    The executor will run the given `work_cb` off of the loop thread.
    Once `work_cb` is completed, `after_work_cb` will be
    called on loop's loop thread.

    This request can be cancelled with :c:func:`uv_cancel`.

.. seealso:: The :c:type:`uv_req_t` API functions also apply.
