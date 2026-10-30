
.. _freertos:

FreeRTOS notes
==============

FreeRTOS is closer to an operating system toolkit than an operating system
proper, that's why the FreeRTOS port provides only functionality that FreeRTOS
itself provides out of the box, which isn't much.

All the libuv API functions are still there but they return ``UV_ENOTSUP``
when something is unsupported.

Things that work:

- :c:type:`uv_async_t`
- :c:type:`uv_check_t`
- :c:type:`uv_cond_t`
- :c:type:`uv_handle_t`
- :c:type:`uv_idle_t`
- :c:type:`uv_timer_t`

- :c:type:`uv_barrier_t`
- :c:type:`uv_key_t` - needs ``configNUM_THREAD_LOCAL_STORAGE_POINTERS > 0`` and the scheduler running
- :c:type:`uv_mutex_t`
- :c:type:`uv_once_t`
- :c:type:`uv_prepare_t`
- :c:type:`uv_rwlock_t`
- :c:type:`uv_sem_t`
- :c:type:`uv_thread_t`

- :c:func:`uv_replace_allocator` - and you should use it because the default
  allocator probably won't work.

Things that don't work (this list is not exhaustive):

- :c:type:`uv_cpu_info_t` - no standardized API
- :c:type:`uv_fs_t` - no file system
- :c:type:`uv_getaddrinfo_t` - no network stack
- :c:type:`uv_pipe_t` - no pipes
- :c:type:`uv_process_t` - no processes
- :c:type:`uv_random_t` - no random pool
- :c:type:`uv_signal_t` - no signals
- :c:type:`uv_tcp_t` - no network stack
- :c:type:`uv_udp_t` - no network stack
- :c:type:`uv_work_t` - no thread pool (probably makes no sense on a MCU anyway)

Libuv functions are not ISR-safe, not even :c:func:`uv_async_send`.

YMMV on 8-bit or 16-bit systems.

Libuv tries its level best to run correctly on systems with unusual tick rates,
like 333 Hz.

Libuv uses index ``configTASK_NOTIFICATION_ARRAY_ENTRIES - 1`` (usually 0) for
bookkeeping. Build with ``-DUV_FREERTOS_NOTIFY_INDEX=<n>`` to use another index.

Threads created with :c:func:`uv_thread_create` and :c:func:`uv_thread_create_ex`:

- Inherit the priority of their parent thread; if the scheduler hasn't
  been started yet, priority ``tskIDLE_PRIORITY + 1`` is used instead.

- Get a 128 KiB stack by default; see the ``UV_THREAD_HAS_STACK_SIZE`` option
  of :c:type:`uv_thread_options_t`.

- Should *not* call ``vTaskDelete(NULL)``. See :c:func:`uv_thread_join` and
  :c:func:`uv_thread_detach`.

Build libuv with ``-DUV_FREERTOS_TLS_BASE=<n>`` and ``-DUV_FREERTOS_TLS_SIZE=<n>``
to limit the range of thread-local keys. The default range is
``0..configNUM_THREAD_LOCAL_STORAGE_POINTERS-1`` but that leaves no space for
other parts of your program that depend on thread-local storage.

Libuv requires the following FreeRTOS build options and errors if they are
not set:

- ``INCLUDE_uxTaskPriorityGet``
- ``INCLUDE_vTaskDelay``
- ``INCLUDE_vTaskDelete``
- ``INCLUDE_vTaskSuspend``
- ``INCLUDE_xTaskGetSchedulerState``
- ``configUSE_COUNTING_SEMAPHORES``
- ``configUSE_MUTEXES``
- ``configUSE_TASK_NOTIFICATIONS``

Supported but not required:

- ``configNUM_THREAD_LOCAL_STORAGE_POINTERS`` - makes :c:type:`uv_key_t` work
- ``configUSE_RECURSIVE_MUTEXES`` - makes :c:func:`uv_mutex_init_recursive` work

In your ``CMakeLists.txt``, include FreeRTOS before libuv. Libuv's build scripts
will then auto-detect FreeRTOS and do the right thing. If that's not an option,
set ``LIBUV_BUILD_FOR_FREERTOS=ON`` explicitly. Building with autotools is not
supported.
