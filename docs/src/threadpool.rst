
.. _threadpool:

Thread pool work scheduling
===========================

libuv provides a threadpool which can be used to run user code and get notified
in the loop thread. This thread pool is internally used to run all filesystem
operations, as well as getaddrinfo and getnameinfo requests.

Its default size is 4, but it can be changed at startup time by setting the
``UV_THREADPOOL_SIZE`` environment variable to any value (the absolute maximum
is 128).

The threadpool is global and shared across all event loops.


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

.. seealso:: The :c:type:`uv_req_t` API functions also apply.
