Threads
=======

Wait a minute? Why are we on threads? Aren't event loops supposed to be **the
way** to do *web-scale programming*? Well... no. Threads are still the medium in
which processors do their jobs. Threads are therefore mighty useful sometimes, even
though you might have to wade through various synchronization primitives.

Threads are used internally to fake the asynchronous nature of all of the system
calls. libuv also uses threads to allow you, the application, to perform a task
asynchronously that is actually blocking, by spawning a thread and collecting
the result when it is done.

Today there are two predominant thread libraries: the Windows threads
implementation and POSIX's :man:`pthreads(7)`. libuv's thread API is analogous to
the pthreads API and often has similar semantics.

A notable aspect of libuv's thread facilities is that it is a self contained
section within libuv. Whereas other features intimately depend on the event
loop and callback principles, threads are complete agnostic, they block as
required, signal errors directly via return values, and, as shown in the
:ref:`first example <thread-create-example>`, don't even require a running
event loop.

libuv's thread API is also very limited since the semantics and syntax of
threads are different on all platforms, with different levels of completeness.

This chapter makes the following assumption: **There is only one event loop,
running in one thread (the main thread)**. No other thread interacts
with the event loop (except using ``uv_async_send``).

Core thread operations
----------------------

There isn't much here, you just start a thread using ``uv_thread_create()`` and
wait for it to close using ``uv_thread_join()``.

.. _thread-create-example:

.. rubric:: thread-create/main.c
.. literalinclude:: ../../code/thread-create/main.c
    :language: c
    :linenos:
    :lines: 26-36
    :emphasize-lines: 3-7

.. tip::

    ``uv_thread_t`` is just an alias for ``pthread_t`` on Unix, but this is an
    implementation detail, avoid depending on it to always be true.

The second parameter is the function which will serve as the entry point for
the thread, the last parameter is a ``void *`` argument which can be used to pass
custom parameters to the thread. The function ``hare`` will now run in a separate
thread, scheduled pre-emptively by the operating system:

.. rubric:: thread-create/main.c
.. literalinclude:: ../../code/thread-create/main.c
    :language: c
    :linenos:
    :lines: 6-14
    :emphasize-lines: 2

Unlike ``pthread_join()`` which allows the target thread to pass back a value to
the calling thread using a second parameter, ``uv_thread_join()`` does not. To
send values use :ref:`inter-thread-communication`.

Synchronization Primitives
--------------------------

This section is purposely spartan. This book is not about threads, so I only
catalogue any surprises in the libuv APIs here. For the rest you can look at
the :man:`pthreads(7)` man pages.

Mutexes
~~~~~~~

The mutex functions are a **direct** map to the pthread equivalents.

.. rubric:: libuv mutex functions
.. code-block:: c

    int uv_mutex_init(uv_mutex_t* handle);
    int uv_mutex_init_recursive(uv_mutex_t* handle);
    void uv_mutex_destroy(uv_mutex_t* handle);
    void uv_mutex_lock(uv_mutex_t* handle);
    int uv_mutex_trylock(uv_mutex_t* handle);
    void uv_mutex_unlock(uv_mutex_t* handle);

The ``uv_mutex_init()``, ``uv_mutex_init_recursive()`` and ``uv_mutex_trylock()``
functions will return 0 on success, and an error code otherwise.

If `libuv` has been compiled with debugging enabled, ``uv_mutex_destroy()``,
``uv_mutex_lock()`` and ``uv_mutex_unlock()`` will ``abort()`` on error.
Similarly ``uv_mutex_trylock()`` will abort if the error is anything *other
than* ``EAGAIN`` or ``EBUSY``.

Recursive mutexes are supported, but you should not rely on them. Also, they
should not be used with ``uv_cond_t`` variables.

The default BSD mutex implementation will raise an error if a thread which has
locked a mutex attempts to lock it again. For example, a construct like::

    uv_mutex_init(a_mutex);
    uv_mutex_lock(a_mutex);
    uv_thread_create(thread_id, entry, (void *)a_mutex);
    uv_mutex_lock(a_mutex);
    // more things here

can be used to wait until another thread initializes some stuff and then
unlocks ``a_mutex`` but will lead to your program crashing if in debug mode, or
return an error in the second call to ``uv_mutex_lock()``.

.. note::

    Mutexes on Windows are always recursive.

Locks
~~~~~

Read-write locks are a more granular access mechanism. Two readers can access
shared memory at the same time. A writer may not acquire the lock when it is
held by a reader. A reader or writer may not acquire a lock when a writer is
holding it. Read-write locks are frequently used in databases. Here is a toy
example.

.. rubric:: locks/main.c - simple rwlocks
.. literalinclude:: ../../code/locks/main.c
    :language: c
    :linenos:
    :emphasize-lines: 13,16,27,31,42,55

Run this and observe how the readers will sometimes overlap. In case of
multiple writers, schedulers will usually give them higher priority, so if you
add two writers, you'll see that both writers tend to finish first before the
readers get a chance again.

We also use barriers in the above example so that the main thread can wait for
all readers and writers to indicate they have ended.

Others
~~~~~~

libuv also supports semaphores_, `condition variables`_ and barriers_ with APIs
very similar to their pthread counterparts.

.. _semaphores: https://en.wikipedia.org/wiki/Semaphore_(programming)
.. _condition variables: https://en.wikipedia.org/wiki/Monitor_(synchronization)#Condition_variables_2
.. _barriers: https://en.wikipedia.org/wiki/Barrier_(computer_science)

In addition, libuv provides a convenience function ``uv_once()``. Multiple
threads can attempt to call ``uv_once()`` with a given guard and a function
pointer, **only the first one will win, the function will be called once and
only once**::

    /* Initialize guard */
    static uv_once_t once_only = UV_ONCE_INIT;

    int i = 0;

    void increment() {
        i++;
    }

    void thread1() {
        /* ... work */
        uv_once(once_only, increment);
    }

    void thread2() {
        /* ... work */
        uv_once(once_only, increment);
    }

    int main() {
        /* ... spawn threads */
    }

After all threads are done, ``i == 1``.

.. _libuv-work-queue:

libuv v0.11.11 onwards also added a ``uv_key_t`` struct and api_ for
thread-local storage.

.. _api: http://docs.libuv.org/en/v1.x/threading.html#thread-local-storage

libuv work queue
----------------

``uv_queue_work()`` is a convenience function that allows an application to run
a task in a separate thread, and have a callback that is triggered when the
task is done. A seemingly simple function, what makes ``uv_queue_work()``
tempting is that it allows potentially any third-party libraries to be used
with the event-loop paradigm. When you use event loops, it is *imperative to
make sure that no function which runs periodically in the loop thread blocks
when performing I/O or is a serious CPU hog*, because this means that the loop
slows down and events are not being handled at full capacity.

However, a lot of existing code out there features blocking functions (for example
a routine which performs I/O under the hood) to be used with threads if you
want responsiveness (the classic 'one thread per client' server model), and
getting them to play with an event loop library generally involves rolling your
own system of running the task in a separate thread.  libuv just provides
a convenient abstraction for this.

Here is a simple example inspired by `node.js is cancer`_. We are going to
calculate fibonacci numbers, sleeping a bit along the way, but run it in
a separate thread so that the blocking and CPU bound task does not prevent the
event loop from performing other activities.

.. rubric:: queue-work/main.c - lazy fibonacci
.. literalinclude:: ../../code/queue-work/main.c
    :language: c
    :linenos:
    :lines: 17-29

The actual task function is simple, nothing to show that it is going to be
run in a separate thread. The ``uv_work_t`` structure is the clue. You can pass
arbitrary data through it using the ``void* data`` field and use it to
communicate to and from the thread. But be sure you are using proper locks if
you are changing things while both threads may be running.

The trigger is ``uv_queue_work``:

.. rubric:: queue-work/main.c
.. literalinclude:: ../../code/queue-work/main.c
    :language: c
    :linenos:
    :lines: 31-44
    :emphasize-lines: 10

The thread function will be launched in a separate thread, passed the
``uv_work_t`` structure and once the function returns, the *after* function
will be called on the thread the event loop is running in. It will be passed
the same structure.

For writing wrappers to blocking libraries, a common :ref:`pattern <baton>`
is to use a baton to exchange data.

Since libuv version `0.9.4` an additional function, ``uv_cancel()``, is
available. This allows you to cancel tasks on the libuv work queue. Only tasks
that *are yet to be started* can be cancelled. If a task has *already started
executing, or it has finished executing*, ``uv_cancel()`` **will fail**.

``uv_cancel()`` is useful to cleanup pending tasks if the user requests
termination. For example, a music player may queue up multiple directories to
be scanned for audio files. If the user terminates the program, it should quit
quickly and not wait until all pending requests are run.

Let's modify the fibonacci example to demonstrate ``uv_cancel()``. We first set
up a signal handler for termination.

.. rubric:: queue-cancel/main.c
.. literalinclude:: ../../code/queue-cancel/main.c
    :language: c
    :linenos:
    :lines: 43-

When the user triggers the signal by pressing ``Ctrl+C`` we send
``uv_cancel()`` to all the workers. ``uv_cancel()`` will return ``0`` for those that are already executing or finished.

.. rubric:: queue-cancel/main.c
.. literalinclude:: ../../code/queue-cancel/main.c
    :language: c
    :linenos:
    :lines: 33-41
    :emphasize-lines: 6

For tasks that do get cancelled successfully, the *after* function is called
with ``status`` set to ``UV_ECANCELED``.

.. rubric:: queue-cancel/main.c
.. literalinclude:: ../../code/queue-cancel/main.c
    :language: c
    :linenos:
    :lines: 28-31
    :emphasize-lines: 2

``uv_cancel()`` can also be used with ``uv_fs_t`` and ``uv_getaddrinfo_t``
requests. For the filesystem family of functions, ``uv_fs_t.errorno`` will be
set to ``UV_ECANCELED``.

.. TIP::

    A well designed program would have a way to terminate long running workers
    that have already started executing. Such a worker could periodically check
    for a variable that only the main process sets to signal termination.

.. _inter-thread-communication:

Inter-thread communication
--------------------------

Sometimes you want various threads to actually send each other messages *while*
they are running. For example you might be running some long duration task in
a separate thread (perhaps using ``uv_queue_work``) but want to notify progress
to the main thread. This is a simple example of having a download manager
informing the user of the status of running downloads.

.. rubric:: progress/main.c
.. literalinclude:: ../../code/progress/main.c
    :language: c
    :linenos:
    :lines: 7-8,35-
    :emphasize-lines: 2,11

The async thread communication works *on loops* so although any thread can be
the message sender, only threads with libuv loops can be receivers (or rather
the loop is the receiver). libuv will invoke the callback (``print_progress``)
with the async watcher whenever it receives a message.

.. warning::

    It is important to realize that since the message send is *async*, the callback
    may be invoked immediately after ``uv_async_send`` is called in another
    thread, or it may be invoked after some time. libuv may also combine
    multiple calls to ``uv_async_send`` and invoke your callback only once. The
    only guarantee that libuv makes is -- The callback function is called *at
    least once* after the call to ``uv_async_send``. If you have no pending
    calls to ``uv_async_send``, the callback won't be called. If you make two
    or more calls, and libuv hasn't had a chance to run the callback yet, it
    *may* invoke your callback *only once* for the multiple invocations of
    ``uv_async_send``. Your callback will never be called twice for just one
    event.

.. rubric:: progress/main.c
.. literalinclude:: ../../code/progress/main.c
    :language: c
    :linenos:
    :lines: 10-24
    :emphasize-lines: 7-8

In the download function, we modify the progress indicator and queue the message
for delivery with ``uv_async_send``. Remember: ``uv_async_send`` is also
non-blocking and will return immediately.

.. rubric:: progress/main.c
.. literalinclude:: ../../code/progress/main.c
    :language: c
    :linenos:
    :lines: 31-34

The callback is a standard libuv pattern, extracting the data from the watcher.

Finally it is important to remember to clean up the watcher.

.. rubric:: progress/main.c
.. literalinclude:: ../../code/progress/main.c
    :language: c
    :linenos:
    :lines: 26-29
    :emphasize-lines: 3

After this example, which showed the abuse of the ``data`` field, bnoordhuis_
pointed out that using the ``data`` field is not thread safe, and
``uv_async_send()`` is actually only meant to wake up the event loop. Use
a mutex or rwlock to ensure accesses are performed in the right order.

.. note::

    mutexes and rwlocks **DO NOT** work inside a signal handler, whereas
    ``uv_async_send`` does.

One use case where ``uv_async_send`` is required is when interoperating with
libraries that require thread affinity for their functionality. For example in
node.js, a v8 engine instance, contexts and its objects are bound to the thread
that the v8 instance was started in. Interacting with v8 data structures from
another thread can lead to undefined results. Now consider some node.js module
which binds a third party library. It may go something like this:

1. In node, the third party library is set up with a JavaScript callback to be
   invoked for more information::

    var lib = require('lib');
    lib.on_progress(function() {
        console.log("Progress");
    });

    lib.do();

    // do other stuff

2. ``lib.do`` is supposed to be non-blocking but the third party lib is
   blocking, so the binding uses ``uv_queue_work``.

3. The actual work being done in a separate thread wants to invoke the progress
   callback, but cannot directly call into v8 to interact with JavaScript. So
   it uses ``uv_async_send``.

4. The async callback, invoked in the main loop thread, which is the v8 thread,
   then interacts with v8 to invoke the JavaScript callback.

----

.. _node.js is cancer: http://widgetsandshit.com/teddziuba/2011/10/node-js-is-cancer.html
.. _bnoordhuis: https://github.com/bnoordhuis
