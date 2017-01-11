
.. _after:

:c:type:`uv_after_t` --- After handle
=========================================

After handles will run the given callback once per loop iteration, after all
other handles have been run.


Data types
----------

.. c:type:: uv_after_t

    After handle type.

.. c:type:: void (*uv_after_cb)(uv_after_t* handle)

    Type definition for callback passed to :c:func:`uv_after_start`.


Public members
^^^^^^^^^^^^^^

N/A

.. seealso:: The :c:type:`uv_handle_t` members also apply.


API
---

.. c:function:: int uv_after_init(uv_loop_t* loop, uv_after_t* after)

    Initialize the handle.

.. c:function:: int uv_after_start(uv_after_t* after, uv_after_cb cb)

    Start the handle with the given callback.

.. c:function:: int uv_after_stop(uv_after_t* after)

    Stop the handle, the callback will no longer be called.

.. seealso:: The :c:type:`uv_handle_t` API functions also apply.
