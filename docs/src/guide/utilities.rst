Utilities
=========

This chapter catalogues tools and techniques which are useful for common tasks.
The `libev man page`_ already covers some patterns which can be adopted to
libuv through simple API changes. It also covers parts of the libuv API that
don't require entire chapters dedicated to them.

Timers
------

Timers invoke the callback after a certain time has elapsed since the timer was
started. libuv timers can also be set to invoke at regular intervals instead of
just once.

Simple use is to init a watcher and start it with a ``timeout``, and optional ``repeat``.
Timers can be stopped at any time.

.. code-block:: c

    uv_timer_t timer_req;

    uv_timer_init(loop, &timer_req);
    uv_timer_start(&timer_req, callback, 5000, 2000);

will start a repeating timer, which first starts 5 seconds (the ``timeout``) after the execution
of ``uv_timer_start``, then repeats every 2 seconds (the ``repeat``). Use:

.. code-block:: c

    uv_timer_stop(&timer_req);

to stop the timer. This can be used safely from within the callback as well.

The repeat interval can be modified at any time with::

    uv_timer_set_repeat(uv_timer_t *timer, int64_t repeat);

which will take effect **when possible**. If this function is called from
a timer callback, it means:

* If the timer was non-repeating, the timer has already been stopped. Use
  ``uv_timer_start`` again.
* If the timer is repeating, the next timeout has already been scheduled, so
  the old repeat interval will be used once more before the timer switches to
  the new interval.

The utility function::

    int uv_timer_again(uv_timer_t *)

applies **only to repeating timers** and is equivalent to stopping the timer
and then starting it with both initial ``timeout`` and ``repeat`` set to the
old ``repeat`` value. If the timer hasn't been started it fails (error code
``UV_EINVAL``) and returns -1.

An actual timer example is in the :ref:`reference count section
<reference-count>`.

.. _reference-count:

Event loop reference count
--------------------------

The event loop only runs as long as there are active handles. This system
works by having every handle increase the reference count of the event loop
when it is started and decreasing the reference count when stopped. It is also
possible to manually change the reference count of handles using::

    void uv_ref(uv_handle_t*);
    void uv_unref(uv_handle_t*);

These functions can be used to allow a loop to exit even when a watcher is
active or to use custom objects to keep the loop alive.

The latter can be used with interval timers. You might have a garbage collector
which runs every X seconds, or your network service might send a heartbeat to
others periodically, but you don't want to have to stop them along all clean
exit paths or error scenarios. Or you want the program to exit when all your
other watchers are done. In that case just unref the timer immediately after
creation so that if it is the only watcher running then ``uv_run`` will still
exit.

This is also used in node.js where some libuv methods are being bubbled up to
the JS API. A ``uv_handle_t`` (the superclass of all watchers) is created per
JS object and can be ref/unrefed.

.. rubric:: ref-timer/main.c
.. literalinclude:: ../../code/ref-timer/main.c
    :language: c
    :linenos:
    :lines: 5-8, 17-
    :emphasize-lines: 9

We initialize the garbage collector timer, then immediately ``unref`` it.
Observe how after 9 seconds, when the fake job is done, the program
automatically exits, even though the garbage collector is still running.

Idler pattern
-------------

The callbacks of idle handles are invoked once per event loop. The idle
callback can be used to perform some very low priority activity. For example,
you could dispatch a summary of the daily application performance to the
developers for analysis during periods of idleness, or use the application's
CPU time to perform SETI calculations :) An idle watcher is also useful in
a GUI application. Say you are using an event loop for a file download. If the
TCP socket is still being established and no other events are present your
event loop will pause (**block**), which means your progress bar will freeze
and the user will face an unresponsive application. In such a case queue up and
idle watcher to keep the UI operational.

.. rubric:: idle-compute/main.c
.. literalinclude:: ../../code/idle-compute/main.c
    :language: c
    :linenos:
    :lines: 5-9, 34-
    :emphasize-lines: 13

Here we initialize the idle watcher and queue it up along with the actual
events we are interested in. ``crunch_away`` will now be called repeatedly
until the user types something and presses Return. Then it will be interrupted
for a brief amount as the loop deals with the input data, after which it will
keep calling the idle callback again.

.. rubric:: idle-compute/main.c
.. literalinclude:: ../../code/idle-compute/main.c
    :language: c
    :linenos:
    :lines: 10-19

.. _baton:

Passing data to worker thread
-----------------------------

When using ``uv_queue_work`` you'll usually need to pass complex data through
to the worker thread. The solution is to use a ``struct`` and set
``uv_work_t.data`` to point to it. A slight variation is to have the
``uv_work_t`` itself as the first member of this struct (called a baton [#]_).
This allows cleaning up the work request and all the data in one free call.

.. code-block:: c
    :linenos:
    :emphasize-lines: 2

    struct ftp_baton {
        uv_work_t req;
        char *host;
        int port;
        char *username;
        char *password;
    }

.. code-block:: c
    :linenos:
    :emphasize-lines: 2

    ftp_baton *baton = (ftp_baton*) malloc(sizeof(ftp_baton));
    baton->req.data = (void*) baton;
    baton->host = strdup("my.webhost.com");
    baton->port = 21;
    // ...

    uv_queue_work(loop, &baton->req, ftp_session, ftp_cleanup);

Here we create the baton and queue the task.

Now the task function can extract the data it needs:

.. code-block:: c
    :linenos:
    :emphasize-lines: 2, 12

    void ftp_session(uv_work_t *req) {
        ftp_baton *baton = (ftp_baton*) req->data;

        fprintf(stderr, "Connecting to %s\n", baton->host);
    }

    void ftp_cleanup(uv_work_t *req) {
        ftp_baton *baton = (ftp_baton*) req->data;

        free(baton->host);
        // ...
        free(baton);
    }

We then free the baton which also frees the watcher.

External I/O with polling
-------------------------

Usually third-party libraries will handle their own I/O, and keep track of
their sockets and other files internally. In this case it isn't possible to use
the standard stream I/O operations, but the library can still be integrated
into the libuv event loop. All that is required is that the library allow you
to access the underlying file descriptors and provide functions that process
tasks in small increments as decided by your application. Some libraries though
will not allow such access, providing only a standard blocking function which
will perform the entire I/O transaction and only then return. It is unwise to
use these in the event loop thread, use the :ref:`threadpool` instead. Of
course, this will also mean losing granular control on the library.

The ``uv_poll`` section of libuv simply watches file descriptors using the
operating system notification mechanism. In some sense, all the I/O operations
that libuv implements itself are also backed by ``uv_poll`` like code. Whenever
the OS notices a change of state in file descriptors being polled, libuv will
invoke the associated callback.

Here we will walk through a simple download manager that will use libcurl_ to
download files. Rather than give all control to libcurl, we'll instead be
using the libuv event loop, and use the non-blocking, async multi_ interface to
progress with the download whenever libuv notifies of I/O readiness.

.. _libcurl: https://curl.haxx.se/libcurl/
.. _multi: https://curl.haxx.se/libcurl/c/libcurl-multi.html

.. rubric:: uvwget/main.c - The setup
.. literalinclude:: ../../code/uvwget/main.c
    :language: c
    :linenos:
    :lines: 1-9,140-
    :emphasize-lines: 7,21,24-25

The way each library is integrated with libuv will vary. In the case of
libcurl, we can register two callbacks. The socket callback ``handle_socket``
is invoked whenever the state of a socket changes and we have to start polling
it. ``start_timeout`` is called by libcurl to notify us of the next timeout
interval, after which we should drive libcurl forward regardless of I/O status.
This is so that libcurl can handle errors or do whatever else is required to
get the download moving.

Our downloader is to be invoked as::

    $ ./uvwget [url1] [url2] ...

So we add each argument as an URL

.. rubric:: uvwget/main.c - Adding urls
.. literalinclude:: ../../code/uvwget/main.c
    :language: c
    :linenos:
    :lines: 39-56
    :emphasize-lines: 13-14

We let libcurl directly write the data to a file, but much more is possible if
you so desire.

``start_timeout`` will be called immediately the first time by libcurl, so
things are set in motion. This simply starts a libuv `timer <#timers>`_ which
drives ``curl_multi_socket_action`` with ``CURL_SOCKET_TIMEOUT`` whenever it
times out. ``curl_multi_socket_action`` is what drives libcurl, and what we
call whenever sockets change state. But before we go into that, we need to poll
on sockets whenever ``handle_socket`` is called.

.. rubric:: uvwget/main.c - Setting up polling
.. literalinclude:: ../../code/uvwget/main.c
    :language: c
    :linenos:
    :lines: 102-140
    :emphasize-lines: 9,11,15,21,24

We are interested in the socket fd ``s``, and the ``action``. For every socket
we create a ``uv_poll_t`` handle if it doesn't exist, and associate it with the
socket using ``curl_multi_assign``. This way ``socketp`` points to it whenever
the callback is invoked.

In the case that the download is done or fails, libcurl requests removal of the
poll. So we stop and free the poll handle.

Depending on what events libcurl wishes to watch for, we start polling with
``UV_READABLE`` or ``UV_WRITABLE``. Now libuv will invoke the poll callback
whenever the socket is ready for reading or writing. Calling ``uv_poll_start``
multiple times on the same handle is acceptable, it will just update the events
mask with the new value. ``curl_perform`` is the crux of this program.

.. rubric:: uvwget/main.c - Driving libcurl.
.. literalinclude:: ../../code/uvwget/main.c
    :language: c
    :linenos:
    :lines: 81-95
    :emphasize-lines: 2,6-7,12

The first thing we do is to stop the timer, since there has been some progress
in the interval. Then depending on what event triggered the callback, we set
the correct flags. Then we call ``curl_multi_socket_action`` with the socket
that progressed and the flags informing about what events happened. At this
point libcurl does all of its internal tasks in small increments, and will
attempt to return as fast as possible, which is exactly what an evented program
wants in its main thread. libcurl keeps queueing messages into its own queue
about transfer progress. In our case we are only interested in transfers that
are completed. So we extract these messages, and clean up handles whose
transfers are done.

.. rubric:: uvwget/main.c - Reading transfer status.
.. literalinclude:: ../../code/uvwget/main.c
    :language: c
    :linenos:
    :lines: 58-79
    :emphasize-lines: 6,9-10,13-14

Check & Prepare watchers
------------------------

TODO

Loading libraries
-----------------

libuv provides a cross platform API to dynamically load `shared libraries`_.
This can be used to implement your own plugin/extension/module system and is
used by node.js to implement ``require()`` support for bindings. The usage is
quite simple as long as your library exports the right symbols. Be careful with
sanity and security checks when loading third party code, otherwise your
program will behave unpredictably. This example implements a very simple
plugin system which does nothing except print the name of the plugin.

Let us first look at the interface provided to plugin authors.

.. rubric:: plugin/plugin.h
.. literalinclude:: ../../code/plugin/plugin.h
    :language: c
    :linenos:

You can similarly add more functions that plugin authors can use to do useful
things in your application [#]_. A sample plugin using this API is:

.. rubric:: plugin/hello.c
.. literalinclude:: ../../code/plugin/hello.c
    :language: c
    :linenos:

Our interface defines that all plugins should have an ``initialize`` function
which will be called by the application. This plugin is compiled as a shared
library and can be loaded by running our application::

    $ ./plugin libhello.dylib
    Loading libhello.dylib
    Registered plugin "Hello World!"

.. NOTE::

    The shared library filename will be different depending on platforms. On
    Linux it is ``libhello.so``.

This is done by using ``uv_dlopen`` to first load the shared library
``libhello.dylib``. Then we get access to the ``initialize`` function using
``uv_dlsym`` and invoke it.

.. rubric:: plugin/main.c
.. literalinclude:: ../../code/plugin/main.c
    :language: c
    :linenos:
    :lines: 7-
    :emphasize-lines: 15, 18, 24

``uv_dlopen`` expects a path to the shared library and sets the opaque
``uv_lib_t`` pointer. It returns 0 on success, -1 on error. Use ``uv_dlerror``
to get the error message.

``uv_dlsym`` stores a pointer to the symbol in the second argument in the third
argument. ``init_plugin_function`` is a function pointer to the sort of
function we are looking for in the application's plugins.

.. _shared libraries: https://en.wikipedia.org/wiki/Shared_library#Shared_libraries

TTY
---

Text terminals have supported basic formatting for a long time, with a `pretty
standardised`_ command set. This formatting is often used by programs to
improve the readability of terminal output. For example ``grep --colour``.
libuv provides the ``uv_tty_t`` abstraction (a stream) and related functions to
implement the ANSI escape codes across all platforms. By this I mean that libuv
converts ANSI codes to the Windows equivalent, and provides functions to get
terminal information.

.. _pretty standardised: https://en.wikipedia.org/wiki/ANSI_escape_sequences

The first thing to do is to initialize a ``uv_tty_t`` with the file descriptor
it reads/writes from. This is achieved with::

    int uv_tty_init(uv_loop_t*, uv_tty_t*, uv_file fd, int unused)

The ``unused`` parameter is now auto-detected and ignored. It previously needed
to be set to use ``uv_read_start()`` on the stream.

It is then best to use ``uv_tty_set_mode`` to set the mode to *normal*
which enables most TTY formatting, flow-control and other settings. Other_ modes
are also available.

.. _Other: http://docs.libuv.org/en/v1.x/tty.html#c.uv_tty_mode_t

Remember to call ``uv_tty_reset_mode`` when your program exits to restore the
state of the terminal. Just good manners. Another set of good manners is to be
aware of redirection. If the user redirects the output of your command to
a file, control sequences should not be written as they impede readability and
``grep``. To check if the file descriptor is indeed a TTY, call
``uv_guess_handle`` with the file descriptor and compare the return value with
``UV_TTY``.

Here is a simple example which prints white text on a red background:

.. rubric:: tty/main.c
.. literalinclude:: ../../code/tty/main.c
    :language: c
    :linenos:
    :emphasize-lines: 11-12,14,17,27

The final TTY helper is ``uv_tty_get_winsize()`` which is used to get the
width and height of the terminal and returns ``0`` on success. Here is a small
program which does some animation using the function and character position
escape codes.

.. rubric:: tty-gravity/main.c
.. literalinclude:: ../../code/tty-gravity/main.c
    :language: c
    :linenos:
    :emphasize-lines: 19,25,38

The escape codes are:

======  =======================
Code    Meaning
======  =======================
*2* J    Clear part of the screen, 2 is entire screen
H        Moves cursor to certain position, default top-left
*n* B    Moves cursor down by n lines
*n* C    Moves cursor right by n columns
m        Obeys string of display settings, in this case green background (40+2), white text (30+7)
======  =======================

As you can see this is very useful to produce nicely formatted output, or even
console based arcade games if that tickles your fancy. For fancier control you
can try `ncurses`_.

.. _ncurses: https://www.gnu.org/software/ncurses/ncurses.html

.. versionchanged:: 1.23.1: the `readable` parameter is now unused and ignored.
                    The appropriate value will now be auto-detected from the kernel.

----

.. [#] I was first introduced to the term baton in this context, in Konstantin
       Käfer's excellent slides on writing node.js bindings --
       https://kkaefer.com/node-cpp-modules/#baton
.. [#] mfp is My Fancy Plugin

.. _libev man page: http://pod.tst.eu/http://cvs.schmorp.de/libev/ev.pod#COMMON_OR_USEFUL_IDIOMS_OR_BOTH
