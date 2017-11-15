Advanced event loops
====================

libuv provides considerable user control over event loops, and you can achieve
interesting results by juggling multiple loops. You can also embed libuv's
event loop into another event loop based library -- imagine a Qt based UI, and
Qt's event loop driving a libuv backend which does intensive system level
tasks.

Stopping an event loop
~~~~~~~~~~~~~~~~~~~~~~

``uv_stop()`` can be used to stop an event loop. The earliest the loop will
stop running is *on the next iteration*, possibly later. This means that events
that are ready to be processed in this iteration of the loop will still be
processed, so ``uv_stop()`` can't be used as a kill switch. When ``uv_stop()``
is called, the loop **won't** block for i/o on this iteration. The semantics of
these things can be a bit difficult to understand, so let's look at
``uv_run()`` where all the control flow occurs.

.. rubric:: src/unix/core.c - uv_run
.. literalinclude:: ../../../src/unix/core.c
    :linenos:
    :lines: 304-324
    :emphasize-lines: 10,19,21

``stop_flag`` is set by ``uv_stop()``. Now all libuv callbacks are invoked
within the event loop, which is why invoking ``uv_stop()`` in them will still
lead to this iteration of the loop occurring. First libuv updates timers, then
runs pending timer, idle and prepare callbacks, and invokes any pending I/O
callbacks. If you were to call ``uv_stop()`` in any of them, ``stop_flag``
would be set. This causes ``uv_backend_timeout()`` to return ``0``, which is
why the loop does not block on I/O. If on the other hand, you called
``uv_stop()`` in one of the check handlers, I/O has already finished and is not
affected.

``uv_stop()`` is useful to shutdown a loop when a result has been computed or
there is an error, without having to ensure that all handlers are stopped one
by one.

Here is a simple example that stops the loop and demonstrates how the current
iteration of the loop still takes places.

.. rubric:: uvstop/main.c
.. literalinclude:: ../../code/uvstop/main.c
    :linenos:
    :emphasize-lines: 11

