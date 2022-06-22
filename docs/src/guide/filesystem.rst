Filesystem
==========

Simple filesystem read/write is achieved using the ``uv_fs_*`` functions and the
``uv_fs_t`` struct.

.. note::

    The libuv filesystem operations are different from :doc:`socket operations
    <networking>`. Socket operations use the non-blocking operations provided
    by the operating system. Filesystem operations use blocking functions
    internally, but invoke these functions in a `thread pool`_ and notify
    watchers registered with the event loop when application interaction is
    required.

.. _thread pool: https://docs.libuv.org/en/v1.x/threadpool.html#thread-pool-work-scheduling

All filesystem functions have two forms - *synchronous* and *asynchronous*.

The *synchronous* forms automatically get called (and **block**) if the
callback is null. The return value of functions is a :ref:`libuv error code
<libuv-error-handling>`. This is usually only useful for synchronous calls.
The *asynchronous* form is called when a callback is passed and the return
value is 0.

Reading/Writing files
---------------------

A file descriptor is obtained using

.. code-block:: c

    int uv_fs_open(uv_loop_t* loop, uv_fs_t* req, const char* path, int flags, int mode, uv_fs_cb cb)

``flags`` and ``mode`` are standard
`Unix flags <https://man7.org/linux/man-pages/man2/open.2.html>`_.
libuv takes care of converting to the appropriate Windows flags.

File descriptors are closed using

.. code-block:: c

    int uv_fs_close(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb)


Filesystem operation callbacks have the signature:

.. code-block:: c

    void callback(uv_fs_t* req);

Let's see a simple implementation of ``cat``. We start with registering
a callback for when the file is opened:

.. rubric:: uvcat/main.c - opening a file
.. literalinclude:: ../../code/uvcat/main.c
    :language: c
    :linenos:
    :lines: 41-53
    :emphasize-lines: 4, 6-7

The ``result`` field of a ``uv_fs_t`` is the file descriptor in case of the
``uv_fs_open`` callback. If the file is successfully opened, we start reading it.

.. rubric:: uvcat/main.c - read callback
.. literalinclude:: ../../code/uvcat/main.c
    :language: c
    :linenos:
    :lines: 26-39
    :emphasize-lines: 2,8,12

In the case of a read call, you should pass an *initialized* buffer which will
be filled with data before the read callback is triggered. The ``uv_fs_*``
operations map almost directly to certain POSIX functions, so EOF is indicated
in this case by ``result`` being 0. In the case of streams or pipes, the
``UV_EOF`` constant would have been passed as a status instead.

Here you see a common pattern when writing asynchronous programs. The
``uv_fs_close()`` call is performed synchronously. *Usually tasks which are
one-off, or are done as part of the startup or shutdown stage are performed
synchronously, since we are interested in fast I/O when the program is going
about its primary task and dealing with multiple I/O sources*. For solo tasks
the performance difference usually is negligible and may lead to simpler code.

Filesystem writing is similarly simple using ``uv_fs_write()``.  *Your callback
will be triggered after the write is complete*.  In our case the callback
simply drives the next read. Thus read and write proceed in lockstep via
callbacks.

.. rubric:: uvcat/main.c - write callback
.. literalinclude:: ../../code/uvcat/main.c
    :language: c
    :linenos:
    :lines: 17-24
    :emphasize-lines: 6

.. warning::

    Due to the way filesystems and disk drives are configured for performance,
    a write that 'succeeds' may not be committed to disk yet.

We set the dominos rolling in ``main()``:

.. rubric:: uvcat/main.c
.. literalinclude:: ../../code/uvcat/main.c
    :language: c
    :linenos:
    :lines: 55-
    :emphasize-lines: 2

.. warning::

    The ``uv_fs_req_cleanup()`` function must always be called on filesystem
    requests to free internal memory allocations in libuv.

Filesystem operations
---------------------

All the standard filesystem operations like ``unlink``, ``rmdir``, ``stat`` are
supported asynchronously and have intuitive argument order. They follow the
same patterns as the read/write/open calls, returning the result in the
``uv_fs_t.result`` field. The full list:

.. rubric:: Filesystem operations
.. code-block:: c

    int uv_fs_close(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb);
    int uv_fs_open(uv_loop_t* loop, uv_fs_t* req, const char* path, int flags, int mode, uv_fs_cb cb);
    int uv_fs_read(uv_loop_t* loop, uv_fs_t* req, uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset, uv_fs_cb cb);
    int uv_fs_unlink(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb);
    int uv_fs_write(uv_loop_t* loop, uv_fs_t* req, uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset, uv_fs_cb cb);
    int uv_fs_copyfile(uv_loop_t* loop, uv_fs_t* req, const char* path, const char* new_path, int flags, uv_fs_cb cb);
    int uv_fs_mkdir(uv_loop_t* loop, uv_fs_t* req, const char* path, int mode, uv_fs_cb cb);
    int uv_fs_mkdtemp(uv_loop_t* loop, uv_fs_t* req, const char* tpl, uv_fs_cb cb);
    int uv_fs_mkstemp(uv_loop_t* loop, uv_fs_t* req, const char* tpl, uv_fs_cb cb);
    int uv_fs_rmdir(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb);
    int uv_fs_scandir(uv_loop_t* loop, uv_fs_t* req, const char* path, int flags, uv_fs_cb cb);
    int uv_fs_scandir_next(uv_fs_t* req, uv_dirent_t* ent);
    int uv_fs_opendir(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb);
    int uv_fs_readdir(uv_loop_t* loop, uv_fs_t* req, uv_dir_t* dir, uv_fs_cb cb);
    int uv_fs_closedir(uv_loop_t* loop, uv_fs_t* req, uv_dir_t* dir, uv_fs_cb cb);
    int uv_fs_stat(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb);
    int uv_fs_fstat(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb);
    int uv_fs_rename(uv_loop_t* loop, uv_fs_t* req, const char* path, const char* new_path, uv_fs_cb cb);
    int uv_fs_fsync(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb);
    int uv_fs_fdatasync(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb);
    int uv_fs_ftruncate(uv_loop_t* loop, uv_fs_t* req, uv_file file, int64_t offset, uv_fs_cb cb);
    int uv_fs_sendfile(uv_loop_t* loop, uv_fs_t* req, uv_file out_fd, uv_file in_fd, int64_t in_offset, size_t length, uv_fs_cb cb);
    int uv_fs_access(uv_loop_t* loop, uv_fs_t* req, const char* path, int mode, uv_fs_cb cb);
    int uv_fs_chmod(uv_loop_t* loop, uv_fs_t* req, const char* path, int mode, uv_fs_cb cb);
    int uv_fs_utime(uv_loop_t* loop, uv_fs_t* req, const char* path, double atime, double mtime, uv_fs_cb cb);
    int uv_fs_futime(uv_loop_t* loop, uv_fs_t* req, uv_file file, double atime, double mtime, uv_fs_cb cb);
    int uv_fs_lutime(uv_loop_t* loop, uv_fs_t* req, const char* path, double atime, double mtime, uv_fs_cb cb);
    int uv_fs_lstat(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb);
    int uv_fs_link(uv_loop_t* loop, uv_fs_t* req, const char* path, const char* new_path, uv_fs_cb cb);
    int uv_fs_symlink(uv_loop_t* loop, uv_fs_t* req, const char* path, const char* new_path, int flags, uv_fs_cb cb);
    int uv_fs_readlink(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb);
    int uv_fs_realpath(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb);
    int uv_fs_fchmod(uv_loop_t* loop, uv_fs_t* req, uv_file file, int mode, uv_fs_cb cb);
    int uv_fs_chown(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_uid_t uid, uv_gid_t gid, uv_fs_cb cb);
    int uv_fs_fchown(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_uid_t uid, uv_gid_t gid, uv_fs_cb cb);
    int uv_fs_lchown(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_uid_t uid, uv_gid_t gid, uv_fs_cb cb);
    int uv_fs_statfs(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb);


.. _buffers-and-streams:

Buffers and Streams
-------------------

The basic I/O handle in libuv is the stream (``uv_stream_t``). TCP sockets, UDP
sockets, and pipes for file I/O and IPC are all treated as stream subclasses.

Streams are initialized using custom functions for each subclass, then operated
upon using

.. code-block:: c

    int uv_read_start(uv_stream_t*, uv_alloc_cb alloc_cb, uv_read_cb read_cb);
    int uv_read_stop(uv_stream_t*);
    int uv_write(uv_write_t* req, uv_stream_t* handle,
                 const uv_buf_t bufs[], unsigned int nbufs, uv_write_cb cb);

The stream based functions are simpler to use than the filesystem ones and
libuv will automatically keep reading from a stream when ``uv_read_start()`` is
called once, until ``uv_read_stop()`` is called.

The discrete unit of data is the buffer -- ``uv_buf_t``. This is simply
a collection of a pointer to bytes (``uv_buf_t.base``) and the length
(``uv_buf_t.len``). The ``uv_buf_t`` is lightweight and passed around by value.
What does require management is the actual bytes, which have to be allocated
and freed by the application.

.. ERROR::

    **THIS PROGRAM DOES NOT ALWAYS WORK, NEED SOMETHING BETTER**

To demonstrate streams we will need to use ``uv_pipe_t``. This allows streaming
local files [#]_. Here is a simple tee utility using libuv.  Doing all operations
asynchronously shows the power of evented I/O. The two writes won't block each
other, but we have to be careful to copy over the buffer data to ensure we don't
free a buffer until it has been written.

The program is to be executed as::

    ./uvtee <output_file>

We start off opening pipes on the files we require. libuv pipes to a file are
opened as bidirectional by default.

.. rubric:: uvtee/main.c - read on pipes
.. literalinclude:: ../../code/uvtee/main.c
    :language: c
    :linenos:
    :lines: 62-80
    :emphasize-lines: 4,5,15

The third argument of ``uv_pipe_init()`` should be set to 1 for IPC using named
pipes. This is covered in :doc:`processes`. The ``uv_pipe_open()`` call
associates the pipe with the file descriptor, in this case ``0`` (standard
input).

We start monitoring ``stdin``. The ``alloc_buffer`` callback is invoked as new
buffers are required to hold incoming data. ``read_stdin`` will be called with
these buffers.

.. rubric:: uvtee/main.c - reading buffers
.. literalinclude:: ../../code/uvtee/main.c
    :language: c
    :linenos:
    :lines: 19-22,44-60

The standard ``malloc`` is sufficient here, but you can use any memory allocation
scheme. For example, node.js uses its own slab allocator which associates
buffers with V8 objects.

The read callback ``nread`` parameter is less than 0 on any error. This error
might be EOF, in which case we close all the streams, using the generic close
function ``uv_close()`` which deals with the handle based on its internal type.
Otherwise ``nread`` is a non-negative number and we can attempt to write that
many bytes to the output streams. Finally remember that buffer allocation and
deallocation is application responsibility, so we free the data.

The allocation callback may return a buffer with length zero if it fails to
allocate memory. In this case, the read callback is invoked with error
UV_ENOBUFS. libuv will continue to attempt to read the stream though, so you
must explicitly call ``uv_close()`` if you want to stop when allocation fails.

The read callback may be called with ``nread = 0``, indicating that at this
point there is nothing to be read. Most applications will just ignore this.

.. rubric:: uvtee/main.c - Write to pipe
.. literalinclude:: ../../code/uvtee/main.c
    :language: c
    :linenos:
    :lines: 9-13,23-42

``write_data()`` makes a copy of the buffer obtained from read. This buffer
does not get passed through to the write callback trigged on write completion. To
get around this we wrap a write request and a buffer in ``write_req_t`` and
unwrap it in the callbacks. We make a copy so we can free the two buffers from
the two calls to ``write_data`` independently of each other. While acceptable
for a demo program like this, you'll probably want smarter memory management,
like reference counted buffers or a pool of buffers in any major application.

.. WARNING::

    If your program is meant to be used with other programs it may knowingly or
    unknowingly be writing to a pipe. This makes it susceptible to `aborting on
    receiving a SIGPIPE`_. It is a good idea to insert::

        signal(SIGPIPE, SIG_IGN)

    in the initialization stages of your application.

.. _aborting on receiving a SIGPIPE: http://pod.tst.eu/http://cvs.schmorp.de/libev/ev.pod#The_special_problem_of_SIGPIPE

File change events
------------------

All modern operating systems provide APIs to put watches on individual files or
directories and be informed when the files are modified. libuv wraps common
file change notification libraries [#fsnotify]_. This is one of the more
inconsistent parts of libuv. File change notification systems are themselves
extremely varied across platforms so getting everything working everywhere is
difficult. To demonstrate, I'm going to build a simple utility which runs
a command whenever any of the watched files change::

    ./onchange <command> <file1> [file2] ...

.. note::

    Currently this example only works on OSX and Windows.
    Refer to the `notes of uv_fs_event_start`_ function.

.. _notes of uv_fs_event_start: https://docs.libuv.org/en/v1.x/fs_event.html#c.uv_fs_event_start

The file change notification is started using ``uv_fs_event_init()``:

.. rubric:: onchange/main.c - The setup
.. literalinclude:: ../../code/onchange/main.c
    :language: c
    :linenos:
    :lines: 26-
    :emphasize-lines: 15

The third argument is the actual file or directory to monitor. The last
argument, ``flags``, can be:

.. code-block:: c

    /*
     * Flags to be passed to uv_fs_event_start().
     */
    enum uv_fs_event_flags {
        UV_FS_EVENT_WATCH_ENTRY = 1,
        UV_FS_EVENT_STAT = 2,
        UV_FS_EVENT_RECURSIVE = 4
    };

``UV_FS_EVENT_WATCH_ENTRY`` and ``UV_FS_EVENT_STAT`` don't do anything (yet).
``UV_FS_EVENT_RECURSIVE`` will start watching subdirectories as well on
supported platforms.

The callback will receive the following arguments:

  #. ``uv_fs_event_t *handle`` - The handle. The ``path`` field of the handle
     is the file on which the watch was set.
  #. ``const char *filename`` - If a directory is being monitored, this is the
     file which was changed. Only non-``null`` on Linux and Windows. May be ``null``
     even on those platforms.
  #. ``int events`` - one of ``UV_RENAME`` or ``UV_CHANGE``, or a bitwise OR of
     both.
  #. ``int status`` - If ``status < 0``, there is an :ref:`libuv error<libuv-error-handling>`.

In our example we simply print the arguments and run the command using
``system()``.

.. rubric:: onchange/main.c - file change notification callback
.. literalinclude:: ../../code/onchange/main.c
    :language: c
    :linenos:
    :lines: 9-24

----

.. [#fsnotify] inotify on Linux, FSEvents on Darwin, kqueue on BSDs,
               ReadDirectoryChangesW on Windows, event ports on Solaris, unsupported on Cygwin
.. [#] see :ref:`pipes`
