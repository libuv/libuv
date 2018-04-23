
.. _tracing:

Trace Event Utilities
=====================

libuv provides a basic trace event mechanism that is configured using the
:c:func:`uv_loop_configure` API.

Data types
----------

.. c:type:: uv_loop_trace_t

    Configuration for event loop tracing.

.. c:type:: uv_threadpool_trace_t

    Configuration for threadloop tracing.

.. c:type:: uv_trace

    Enum identifying the trace event type.

    ::

        typedef enum {
          UV_TRACE_TICK,
          UV_TRACE_TIMERS,
          UV_TRACE_CHECK,
          UV_TRACE_IDLE,
          UV_TRACE_PREPARE,
          UV_TRACE_PENDING,
          UV_TRACE_POLL,
          UV_TRACE_THREADPOOL_SUBMIT,
          UV_TRACE_THREADPOOL_START,
          UV_TRACE_THREADPOOL_DONE
        } uv_trace;

.. c:type:: uv_trace_info_t

    Base trace event information type.

.. c:type:: uv_trace_tick_info_t

.. c:type:: uv_trace_timers_info_t

.. c:type:: uv_trace_check_info_t

.. c:type:: uv_trace_idle_info_t

.. c:type:: uv_trace_prepare_info_t

.. c:type:: uv_trace_pending_info_t

.. c:type:: uv_trace_poll_info_t

.. c:type:: uv_trace_threadpool_info_t

.. c:type:: void (*uv_trace_cb)(const uv_trace_info_t* info, void* data)

    Type definition for callback that receives trace event information.

Public members
^^^^^^^^^^^^^^

.. c:member:: void* uv_loop_trace_t.data

    Space for user-defined arbitrary data. libuv does not use and does not
    touch this field.

.. c:member:: uv_trace_cb uv_loop_trace_t.start_cb

    :c:type:`uv_trace_cb` callback that receives trace event span-start events.

.. c:member:: uv_trace_cb uv_loop_trace_t.end_cb

    :c:type:`uv_trace_cb` callback that receives trace event span-end events.

.. c:member:: void* uv_threadpool_trace_t.data

    Space for user-defined arbitrary data. libuv does not use and does not
    touch this field.

.. c:member:: uv_trace_cb uv_threadpool_trace_t.cb

    :c:type:`uv_trace_cb` callback that receives threadpool trace events.

.. c:member:: uv_trace uv_trace_info_t.type

    Identifies the :c:type:`uv_trace` type of this :c:type:`uv_trace_info_t`.

.. seealso:: For all `*_info_t` types, the :c:type:`uv_trace_info_t` members
   also apply.

.. c:member:: size_t uv_trace_timers_info_t.count

    The number of :c:type:`uv_timer_t` handles processed.

.. c:member:: size_t uv_trace_check_info_t.count

    The number of :c:type:`uv_check_t` handles processed.

.. c:member:: size_t uv_trace_idle_info_t.count

    The number of :c:type:`uv_idle_t` handles processed.

.. c:member:: size_t uv_trace_prepare_info_t.count

    The number of :c:type:`uv_prepare_t` handles processed.

.. c:member:: size_t uv_pending_info_t.count

    The number of pending callbacks processed.

.. c:member:: int uv_trace_poll_info_t.timeout

    The `timeout` selected for the current polling phase.

.. c:member:: unsigned uv_trace_threadpool_info_t.queued

    The number of queued tasks in the threadpool.

.. c:member:: unsigned uv_trace_threadpool_info_t.idle_threads

    The current number of idle threads in the threadpool.
