Processes
=========

libuv offers considerable child process management, abstracting the platform
differences and allowing communication with the child process using streams or
named pipes.

A common idiom in Unix is for every process to do one thing and do it well. In
such a case, a process often uses multiple child processes to achieve tasks
(similar to using pipes in shells). A multi-process model with messages
may also be easier to reason about compared to one with threads and shared
memory.

A common refrain against event-based programs is that they cannot take
advantage of multiple cores in modern computers. In a multi-threaded program
the kernel can perform scheduling and assign different threads to different
cores, improving performance. But an event loop has only one thread.  The
workaround can be to launch multiple processes instead, with each process
running an event loop, and each process getting assigned to a separate CPU
core.

Spawning child processes
------------------------

The simplest case is when you simply want to launch a process and know when it
exits. This is achieved using ``uv_spawn``.

.. rubric:: spawn/main.c
.. literalinclude:: ../../code/spawn/main.c
    :linenos:
    :lines: 6-8,15-
    :emphasize-lines: 11,13-17

.. NOTE::

    ``options`` is implicitly initialized with zeros since it is a global
    variable.  If you change ``options`` to a local variable, remember to
    initialize it to null out all unused fields::

        uv_process_options_t options = {0};

The ``uv_process_t`` struct only acts as the handle, all options are set via
``uv_process_options_t``. To simply launch a process, you need to set only the
``file`` and ``args`` fields. ``file`` is the program to execute. Since
``uv_spawn`` uses :man:`execvp(3)` internally, there is no need to supply the full
path. Finally as per underlying conventions, **the arguments array has to be
one larger than the number of arguments, with the last element being NULL**.

After the call to ``uv_spawn``, ``uv_process_t.pid`` will contain the process
ID of the child process.

The exit callback will be invoked with the *exit status* and the type of *signal*
which caused the exit.

.. rubric:: spawn/main.c
.. literalinclude:: ../../code/spawn/main.c
    :linenos:
    :lines: 9-12
    :emphasize-lines: 3

It is **required** to close the process watcher after the process exits.

Changing process parameters
---------------------------

Before the child process is launched you can control the execution environment
using fields in ``uv_process_options_t``.

Change execution directory
++++++++++++++++++++++++++

Set ``uv_process_options_t.cwd`` to the corresponding directory.

Set environment variables
+++++++++++++++++++++++++

``uv_process_options_t.env`` is a null-terminated array of strings, each of the
form ``VAR=VALUE`` used to set up the environment variables for the process. Set
this to ``NULL`` to inherit the environment from the parent (this) process.

Option flags
++++++++++++

Setting ``uv_process_options_t.flags`` to a bitwise OR of the following flags,
modifies the child process behaviour:

* ``UV_PROCESS_SETUID`` - sets the child's execution user ID to ``uv_process_options_t.uid``.
* ``UV_PROCESS_SETGID`` - sets the child's execution group ID to ``uv_process_options_t.gid``.

Changing the UID/GID is only supported on Unix, ``uv_spawn`` will fail on
Windows with ``UV_ENOTSUP``.

* ``UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS`` - No quoting or escaping of
  ``uv_process_options_t.args`` is done on Windows. Ignored on Unix.
* ``UV_PROCESS_DETACHED`` - Starts the child process in a new session, which
  will keep running after the parent process exits. See example below.

Detaching processes
-------------------

Passing the flag ``UV_PROCESS_DETACHED`` can be used to launch daemons, or
child processes which are independent of the parent so that the parent exiting
does not affect it.

.. rubric:: detach/main.c
.. literalinclude:: ../../code/detach/main.c
    :linenos:
    :lines: 9-30
    :emphasize-lines: 12,19

Just remember that the handle is still monitoring the child, so your program
won't exit. Use ``uv_unref()`` if you want to be more *fire-and-forget*.

Sending signals to processes
----------------------------

libuv wraps the standard ``kill(2)`` system call on Unix and implements one
with similar semantics on Windows, with *one caveat*: all of ``SIGTERM``,
``SIGINT`` and ``SIGKILL``, lead to termination of the process. The signature
of ``uv_kill`` is::

    uv_err_t uv_kill(int pid, int signum);

For processes started using libuv, you may use ``uv_process_kill`` instead,
which accepts the ``uv_process_t`` watcher as the first argument, rather than
the pid. In this case, **remember to call** ``uv_close`` on the watcher.

Signals
-------

libuv provides wrappers around Unix signals with `some Windows support
<http://docs.libuv.org/en/v1.x/signal.html#signal>`_ as well.

Use ``uv_signal_init()`` to initialize
a handle and associate it with a loop. To listen for particular signals on
that handler, use ``uv_signal_start()`` with the handler function. Each handler
can only be associated with one signal number, with subsequent calls to
``uv_signal_start()`` overwriting earlier associations. Use ``uv_signal_stop()`` to
stop watching. Here is a small example demonstrating the various possibilities:

.. rubric:: signal/main.c
.. literalinclude:: ../../code/signal/main.c
    :linenos:
    :emphasize-lines: 17-18,27-28

.. NOTE::

    ``uv_run(loop, UV_RUN_NOWAIT)`` is similar to ``uv_run(loop, UV_RUN_ONCE)``
    in that it will process only one event. UV_RUN_ONCE blocks if there are no
    pending events, while UV_RUN_NOWAIT will return immediately. We use NOWAIT
    so that one of the loops isn't starved because the other one has no pending
    activity.

Send ``SIGUSR1`` to the process, and you'll find the handler being invoked
4 times, one for each ``uv_signal_t``. The handler just stops each handle,
so that the program exits. This sort of dispatch to all handlers is very
useful. A server using multiple event loops could ensure that all data was
safely saved before termination, simply by every loop adding a watcher for
``SIGINT``.

Child Process I/O
-----------------

A normal, newly spawned process has its own set of file descriptors, with 0,
1 and 2 being ``stdin``, ``stdout`` and ``stderr`` respectively. Sometimes you
may want to share file descriptors with the child. For example, perhaps your
applications launches a sub-command and you want any errors to go in the log
file, but ignore ``stdout``. For this you'd like to have ``stderr`` of the
child be the same as the stderr of the parent. In this case, libuv supports
*inheriting* file descriptors. In this sample, we invoke the test program,
which is:

.. rubric:: proc-streams/test.c
.. literalinclude:: ../../code/proc-streams/test.c

The actual program ``proc-streams`` runs this while sharing only ``stderr``.
The file descriptors of the child process are set using the ``stdio`` field in
``uv_process_options_t``. First set the ``stdio_count`` field to the number of
file descriptors being set. ``uv_process_options_t.stdio`` is an array of
``uv_stdio_container_t``, which is:

.. code-block:: c

    typedef struct uv_stdio_container_s {
        uv_stdio_flags flags;

        union {
            uv_stream_t* stream;
            int fd;
        } data;
    } uv_stdio_container_t;

where flags can have several values. Use ``UV_IGNORE`` if it isn't going to be
used. If the first three ``stdio`` fields are marked as ``UV_IGNORE`` they'll
redirect to ``/dev/null``.

Since we want to pass on an existing descriptor, we'll use ``UV_INHERIT_FD``.
Then we set the ``fd`` to ``stderr``.

.. rubric:: proc-streams/main.c
.. literalinclude:: ../../code/proc-streams/main.c
    :linenos:
    :lines: 15-17,27-
    :emphasize-lines: 6,10,11,12

If you run ``proc-stream`` you'll see that only the line "This is stderr" will
be displayed. Try marking ``stdout`` as being inherited and see the output.

It is dead simple to apply this redirection to streams.  By setting ``flags``
to ``UV_INHERIT_STREAM`` and setting ``data.stream`` to the stream in the
parent process, the child process can treat that stream as standard I/O. This
can be used to implement something like CGI_.

.. _CGI: https://en.wikipedia.org/wiki/Common_Gateway_Interface

A sample CGI script/executable is:

.. rubric:: cgi/tick.c
.. literalinclude:: ../../code/cgi/tick.c

The CGI server combines the concepts from this chapter and :doc:`networking` so
that every client is sent ten ticks after which that connection is closed.

.. rubric:: cgi/main.c
.. literalinclude:: ../../code/cgi/main.c
    :linenos:
    :lines: 49-63
    :emphasize-lines: 10

Here we simply accept the TCP connection and pass on the socket (*stream*) to
``invoke_cgi_script``.

.. rubric:: cgi/main.c
.. literalinclude:: ../../code/cgi/main.c
    :linenos:
    :lines: 16, 25-45
    :emphasize-lines: 8-9,18,20

The ``stdout`` of the CGI script is set to the socket so that whatever our tick
script prints, gets sent to the client. By using processes, we can offload the
read/write buffering to the operating system, so in terms of convenience this
is great. Just be warned that creating processes is a costly task.

.. _pipes:

Parent-child IPC
----------------

A parent and child can have one or two way communication over a pipe created by
settings ``uv_stdio_container_t.flags`` to a bit-wise combination of
``UV_CREATE_PIPE`` and ``UV_READABLE_PIPE`` or ``UV_WRITABLE_PIPE``. The
read/write flag is from the perspective of the child process.  In this case,
the ``uv_stream_t* stream`` field must be set to point to an initialized,
unopened ``uv_pipe_t`` instance.

New stdio Pipes
+++++++++++++++

The ``uv_pipe_t`` structure represents more than just `pipe(7)`_ (or ``|``),
but supports any streaming file-like objects. On Windows, the only object of
that description is the `Named Pipe`_.  On Unix, this could be any of `Unix
Domain Socket`_, or derived from `mkfifo(1)`_, or it could actually be a
`pipe(7)`_.  When ``uv_spawn`` initializes a ``uv_pipe_t`` due to the
`UV_CREATE_PIPE` flag, it opts for creating a `socketpair(2)`_.

This is intended for the purpose of allowing multiple libuv processes to
communicate with IPC. This is discussed below.

.. _pipe(7): http://man7.org/linux/man-pages/man7/pipe.7.html
.. _mkfifo(1): http://man7.org/linux/man-pages/man1/mkfifo.1.html
.. _socketpair(2): http://man7.org/linux/man-pages/man2/socketpair.2.html
.. _Unix Domain Socket: http://man7.org/linux/man-pages/man7/unix.7.html
.. _Named Pipe: https://docs.microsoft.com/en-us/windows/win32/ipc/named-pipes


Arbitrary process IPC
+++++++++++++++++++++

Since domain sockets [#]_ can have a well known name and a location in the
file-system they can be used for IPC between unrelated processes. The D-BUS_
system used by open source desktop environments uses domain sockets for event
notification. Various applications can then react when a contact comes online
or new hardware is detected. The MySQL server also runs a domain socket on
which clients can interact with it.

.. _D-BUS: https://www.freedesktop.org/wiki/Software/dbus

When using domain sockets, a client-server pattern is usually followed with the
creator/owner of the socket acting as the server. After the initial setup,
messaging is no different from TCP, so we'll re-use the echo server example.

.. rubric:: pipe-echo-server/main.c
.. literalinclude:: ../../code/pipe-echo-server/main.c
    :linenos:
    :lines: 70-
    :emphasize-lines: 5,10,14

We name the socket ``echo.sock`` which means it will be created in the local
directory. This socket now behaves no different from TCP sockets as far as
the stream API is concerned. You can test this server using `socat`_::

    $ socat - /path/to/socket

A client which wants to connect to a domain socket will use::

    void uv_pipe_connect(uv_connect_t *req, uv_pipe_t *handle, const char *name, uv_connect_cb cb);

where ``name`` will be ``echo.sock`` or similar. On Unix systems, ``name`` must
point to a valid file (e.g. ``/tmp/echo.sock``). On Windows, ``name`` follows a
``\\?\pipe\echo.sock`` format.

.. _socat: http://www.dest-unreach.org/socat/

Sending file descriptors over pipes
+++++++++++++++++++++++++++++++++++

The cool thing about domain sockets is that file descriptors can be exchanged
between processes by sending them over a domain socket. This allows processes
to hand off their I/O to other processes. Applications include load-balancing
servers, worker processes and other ways to make optimum use of CPU. libuv only
supports sending **TCP sockets or other pipes** over pipes for now.

To demonstrate, we will look at a echo server implementation that hands of
clients to worker processes in a round-robin fashion. This program is a bit
involved, and while only snippets are included in the book, it is recommended
to read the full code to really understand it.

The worker process is quite simple, since the file-descriptor is handed over to
it by the master.

.. rubric:: multi-echo-server/worker.c
.. literalinclude:: ../../code/multi-echo-server/worker.c
    :linenos:
    :lines: 7-9,81-
    :emphasize-lines: 6-8

``queue`` is the pipe connected to the master process on the other end, along
which new file descriptors get sent. It is important to set the ``ipc``
argument of ``uv_pipe_init`` to 1 to indicate this pipe will be used for
inter-process communication! Since the master will write the file handle to the
standard input of the worker, we connect the pipe to ``stdin`` using
``uv_pipe_open``.

.. rubric:: multi-echo-server/worker.c
.. literalinclude:: ../../code/multi-echo-server/worker.c
    :linenos:
    :lines: 51-79
    :emphasize-lines: 10,15,20

First we call ``uv_pipe_pending_count()`` to ensure that a handle is available
to read out. If your program could deal with different types of handles,
``uv_pipe_pending_type()`` can be used to determine the type.
Although ``accept`` seems odd in this code, it actually makes sense. What
``accept`` traditionally does is get a file descriptor (the client) from
another file descriptor (The listening socket). Which is exactly what we do
here. Fetch the file descriptor (``client``) from ``queue``. From this point
the worker does standard echo server stuff.

Turning now to the master, let's take a look at how the workers are launched to
allow load balancing.

.. rubric:: multi-echo-server/main.c
.. literalinclude:: ../../code/multi-echo-server/main.c
    :linenos:
    :lines: 9-13

The ``child_worker`` structure wraps the process, and the pipe between the
master and the individual process.

.. rubric:: multi-echo-server/main.c
.. literalinclude:: ../../code/multi-echo-server/main.c
    :linenos:
    :lines: 51,61-95
    :emphasize-lines: 17,20-21

In setting up the workers, we use the nifty libuv function ``uv_cpu_info`` to
get the number of CPUs so we can launch an equal number of workers. Again it is
important to initialize the pipe acting as the IPC channel with the third
argument as 1. We then indicate that the child process' ``stdin`` is to be
a readable pipe (from the point of view of the child). Everything is
straightforward till here. The workers are launched and waiting for file
descriptors to be written to their standard input.

It is in ``on_new_connection`` (the TCP infrastructure is initialized in
``main()``), that we accept the client socket and pass it along to the next
worker in the round-robin.

.. rubric:: multi-echo-server/main.c
.. literalinclude:: ../../code/multi-echo-server/main.c
    :linenos:
    :lines: 31-49
    :emphasize-lines: 9,12-13

The ``uv_write2`` call handles all the abstraction and it is simply a matter of
passing in the handle (``client``) as the right argument. With this our
multi-process echo server is operational.

Thanks to Kyle for `pointing out`_ that ``uv_write2()`` requires a non-empty
buffer even when sending handles.

.. _pointing out: https://github.com/nikhilm/uvbook/issues/56

----

.. [#] In this section domain sockets stands in for named pipes on Windows as
    well.
