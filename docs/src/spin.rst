
.. _spin:

:c:type:`uv_spin_t` --- Spin handle
===================================

Idle handles will run the given callback once per loop iteration, right
before the :c:type:`uv_prepare_t` handles.

.. note::
    The notable difference with prepare handles is that when there are active spin handles,
    the loop will perform a zero timeout poll instead of blocking for i/o.

    spin handles will get their callbacks called on every loop iteration.


Data types
----------

.. c:type:: uv_spin_t

    Spin handle type.

.. c:type:: void (*uv_spin_cb)(uv_spin_t* handle)

    Type definition for callback passed to :c:func:`uv_spin_start`.


Public members
^^^^^^^^^^^^^^

N/A

.. seealso:: The :c:type:`uv_handle_t` members also apply.


API
---

.. c:function:: int uv_spin_init(uv_loop_t* loop, uv_spin_t* spin)

    Initialize the handle.

.. c:function:: int uv_spin_start(uv_spin_t* spin, uv_spin_cb cb)

    Start the handle with the given callback.

.. c:function:: int uv_spin_stop(uv_spin_t* spin)

    Stop the handle, the callback will no longer be called.

.. seealso:: The :c:type:`uv_handle_t` API functions also apply.
