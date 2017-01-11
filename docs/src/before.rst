
.. _before:

:c:type:`uv_before_t` --- Before handle
=========================================

Before handles will run the given callback once per loop iteration, right after
updating the loop clock, before invoking any other callbacks.


Data types
----------

.. c:type:: uv_before_t

    Before handle type.

.. c:type:: void (*uv_before_cb)(uv_before_t* handle)

    Type definition for callback passed to :c:func:`uv_before_start`.


Public members
^^^^^^^^^^^^^^

N/A

.. seealso:: The :c:type:`uv_handle_t` members also apply.


API
---

.. c:function:: int uv_before_init(uv_loop_t* loop, uv_before_t* before)

    Initialize the handle.

.. c:function:: int uv_before_start(uv_before_t* before, uv_before_cb cb)

    Start the handle with the given callback.

.. c:function:: int uv_before_stop(uv_before_t* before)

    Stop the handle, the callback will no longer be called.

.. seealso:: The :c:type:`uv_handle_t` API functions also apply.
