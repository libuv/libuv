
.. _threadpool_stats:

Thread pool statistics
===========================

libuv provides a threadpool which can be used to run user code and get notified
in the loop thread. It is also used internally by libuv.  The threadpool is
global and shared across all event loops, see :c:ref:`threadpool` for more
information.

It is possible to request statistics on the threadpool activity to observe the
number of idle threads, and the number of queued work items waiting for an idle
thread to do the work. This maybe be used to detect performance problems, or to
tune the static threadpool size for a specific work load.


Data types
----------

.. c:type:: uv_queue_stats_t

.. c:type:: (*uv_queue_stats_cb)(int queued, int idle_threads, void* data)

    `queued` is the number of submitted items not yet started by a thread.
    `idle_threads` is the number of threads waiting for a item to start.
    `data` is the corresponding value from the uv_queue_stats_t structure.

    *Note*: No uv API functions may be called in these callbacks other than
    `uv_async_send()`.

Public members
^^^^^^^^^^^^^^

.. c:member:: uv_queue_stats_cb submit_cb

    Called once for every work item that is submitted.

.. c:member:: uv_queue_stats_cb start_cb

    Called once for every work item that is started.

.. c:member:: uv_queue_stats_cb done_cb

    Called once for every work item that is completed.


API
---

.. c:function:: int uv_stats_start(uv_queue_stats_t* stats)

    Start calling stats callbacks on threadpool activity.

.. c:function:: int uv_stats_start(uv_queue_stats_t* stats)

    Stop calling stats callbacks on threadpool activity.
