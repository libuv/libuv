
.. _pipe:

:c:type:`uv_pipe_t` --- Pipe handle
===================================

Pipe handles provide an abstraction over streaming files on Unix (including
local domain sockets, pipes, and FIFOs) and named pipes or Unix domain sockets
on Windows.

:c:type:`uv_pipe_t` is a 'subclass' of :c:type:`uv_stream_t`.


Data types
----------

.. c:type:: uv_pipe_t

    Pipe handle type.

.. c:enum:: uv_pipe_init_flags

    Flags for :c:func:`uv_pipe_init_ex`.

    .. c:enumerator:: UV_PIPE_INIT_IPC

        Enable handle passing between processes. This has the same meaning as
        a nonzero ``ipc`` argument to :c:func:`uv_pipe_init`.

    .. c:enumerator:: UV_PIPE_INIT_UNIX_SOCKET

        Select a Unix domain socket. On Unix, this flag leaves the usual Unix
        domain socket behavior unchanged. On Windows, it selects a native
        AF_UNIX stream socket.


Public members
^^^^^^^^^^^^^^

.. c:member:: int uv_pipe_t.ipc

    Whether this pipe is suitable for handle passing between processes.
    Only a connected pipe that will be passing the handles should have this flag
    set, not the listening pipe that uv_accept is called on.

.. seealso:: The :c:type:`uv_stream_t` members also apply.


API
---

.. c:function:: int uv_pipe_init(uv_loop_t* loop, uv_pipe_t* handle, int ipc)

    Initialize a pipe handle. The `ipc` argument is a boolean to indicate if
    this pipe will be used for handle passing between processes (which may
    change the bytes on the wire). Only a connected pipe that will be
    passing the handles should have this flag set, not the listening pipe
    that uv_accept is called on.

    On Windows, this function initializes a named pipe. Use
    :c:func:`uv_pipe_init_ex` with ``UV_PIPE_INIT_UNIX_SOCKET`` to initialize a
    Unix domain socket.

.. c:function:: int uv_pipe_init_ex(uv_loop_t* loop, uv_pipe_t* handle, unsigned int flags)

    Initialize a pipe handle with a combination of :c:enum:`uv_pipe_init_flags`.
    A zero ``flags`` value is equivalent to ``uv_pipe_init(loop, handle, 0)``.
    Unknown flags return ``UV_EINVAL`` without initializing the handle.

    On Unix, ``UV_PIPE_INIT_UNIX_SOCKET`` can be combined with
    ``UV_PIPE_INIT_IPC``. On Windows, that combination returns ``UV_ENOTSUP``.

    Windows Unix domain sockets require native AF_UNIX support, introduced in
    Windows 10 version 1803. Initialization checks availability at runtime and
    returns ``UV_EAFNOSUPPORT`` if the provider is unavailable. A supported
    Windows version alone does not guarantee that the provider is available.

    .. versionadded:: unreleased

    .. note::
        Windows Unix domain sockets support filesystem paths only. Their names
        must contain 1 to 107 bytes of UTF-8, excluding the terminating nul byte.
        Empty names, embedded nul bytes, abstract socket addresses, Windows
        device pipe names, and longer paths return ``UV_EINVAL``. Paths are never
        truncated, regardless of ``UV_PIPE_NO_TRUNCATE``.

        A successful bind creates a socket file. The parent directory must be
        writable, and clients need write access to the socket file. Closing the
        bound handle removes its socket file, including when the working
        directory has changed since binding. An existing file is not removed
        when bind fails.

        Before accepting a Windows Unix domain socket, initialize the client
        handle with ``UV_PIPE_INIT_UNIX_SOCKET`` too. Both handles passed to
        :c:func:`uv_accept` must use the same pipe mode.

        Windows Unix domain sockets cannot pass handles, import a ``uv_file``
        through :c:func:`uv_pipe_open`, or serve as process stdio in
        :c:func:`uv_spawn`. Enabling blocking mode with
        :c:func:`uv_stream_set_blocking` returns ``UV_ENOTSUP``.

.. c:function:: int uv_pipe_open(uv_pipe_t* handle, uv_file file)

    Open an existing file descriptor or HANDLE as a pipe.

    Returns ``UV_ENOTSUP`` for a handle initialized as a Windows Unix domain
    socket. A Windows socket cannot be imported through the ``uv_file`` type.

    .. versionchanged:: 1.2.1 the file descriptor is set to non-blocking mode.

    .. note::
        The passed file descriptor or HANDLE is not checked for its type, but
        it's required that it represents a valid pipe.

.. c:function:: int uv_pipe_bind(uv_pipe_t* handle, const char* name)

    Bind the pipe to a Unix domain socket path or a Windows named pipe name.

    Does not support Linux abstract namespace sockets,
    unlike :c:func:`uv_pipe_bind2`.

    Alias for ``uv_pipe_bind2(handle, name, strlen(name), 0)``.

    .. note::
        Paths on Unix get truncated to ``sizeof(sockaddr_un.sun_path)`` bytes,
        typically between 92 and 108 bytes.

.. c:function:: int uv_pipe_bind2(uv_pipe_t* handle, const char* name, size_t namelen, unsigned int flags)

    Bind the pipe to a Unix domain socket path or a Windows named pipe name.

    ``flags`` must be zero or ``UV_PIPE_NO_TRUNCATE``. Returns ``UV_EINVAL``
    for unsupported flags without performing the bind operation.

    Supports Linux abstract namespace sockets. ``namelen`` must include
    the leading nul byte but not the trailing nul byte.

    .. versionadded:: 1.46.0

    .. note::
        Paths on Unix get truncated to ``sizeof(sockaddr_un.sun_path)`` bytes,
        typically between 92 and 108 bytes, unless the ``UV_PIPE_NO_TRUNCATE``
        flag is specified, in which case an ``UV_EINVAL`` error is returned.

.. c:function:: void uv_pipe_connect(uv_connect_t* req, uv_pipe_t* handle, const char* name, uv_connect_cb cb)

    Connect to the Unix domain socket or the Windows named pipe selected at
    initialization.

    Does not support Linux abstract namespace sockets,
    unlike :c:func:`uv_pipe_connect2`.

    Alias for ``uv_pipe_connect2(req, handle, name, strlen(name), 0, cb)``.

    .. note::
        Paths on Unix get truncated to ``sizeof(sockaddr_un.sun_path)`` bytes,
        typically between 92 and 108 bytes.

.. c:function:: int uv_pipe_connect2(uv_connect_t* req, uv_pipe_t* handle, const char* name, size_t namelen, unsigned int flags, uv_connect_cb cb)

    Connect to the Unix domain socket or the Windows named pipe selected at
    initialization.

    ``flags`` must be zero or ``UV_PIPE_NO_TRUNCATE``. Returns ``UV_EINVAL``
    for unsupported flags without performing the connect operation.

    Supports Linux abstract namespace sockets. ``namelen`` must include
    the leading nul byte but not the trailing nul byte.

    .. versionadded:: 1.46.0

    .. note::
        Paths on Unix get truncated to ``sizeof(sockaddr_un.sun_path)`` bytes,
        typically between 92 and 108 bytes, unless the ``UV_PIPE_NO_TRUNCATE``
        flag is specified, in which case an ``UV_EINVAL`` error is returned.

.. c:function:: int uv_pipe_getsockname(const uv_pipe_t* handle, char* buffer, size_t* size)

    Get the name of the Unix domain socket or the named pipe.

    A preallocated buffer must be provided. The size parameter holds the length
    of the buffer and it's set to the number of bytes written to the buffer on
    output. If the buffer is not big enough ``UV_ENOBUFS`` will be returned and
    ``size`` will contain the required size.

    For Windows Unix domain sockets, the required size includes space for the
    terminating nul byte. On success, the buffer is nul-terminated and ``size``
    excludes the terminator. An unnamed socket returns an empty name.

    .. versionchanged:: 1.3.0 the returned length no longer includes the terminating null byte.

.. c:function:: int uv_pipe_getpeername(const uv_pipe_t* handle, char* buffer, size_t* size)

    Get the name of the Unix domain socket or the named pipe to which the handle
    is connected.

    A preallocated buffer must be provided. The size parameter holds the length
    of the buffer and it's set to the number of bytes written to the buffer on
    output. If the buffer is not big enough ``UV_ENOBUFS`` will be returned and
    ``size`` will contain the required size.

    For Windows Unix domain sockets, the required size includes space for the
    terminating nul byte. On success, the buffer is nul-terminated and ``size``
    excludes the terminator. An unnamed peer returns an empty name.

    .. versionadded:: 1.3.0

.. c:function:: void uv_pipe_pending_instances(uv_pipe_t* handle, int count)

    Set the number of pending pipe instance handles when the pipe server is
    waiting for connections.

    .. note::
        This setting applies to Windows named pipes only.

.. c:function:: int uv_pipe_pending_count(uv_pipe_t* handle)
.. c:function:: uv_handle_type uv_pipe_pending_type(uv_pipe_t* handle)

    Used to receive handles over IPC pipes.

    First - call :c:func:`uv_pipe_pending_count`, if it's > 0 then initialize
    a handle of the given `type`, returned by :c:func:`uv_pipe_pending_type`
    and call ``uv_accept(pipe, handle)``.

.. seealso:: The :c:type:`uv_stream_t` API functions also apply.

.. c:function:: int uv_pipe_chmod(uv_pipe_t* handle, int flags)

    Alters pipe permissions, allowing it to be accessed from processes run by
    different users. Makes the pipe writable or readable by all users. Mode can
    be ``UV_WRITABLE``, ``UV_READABLE`` or ``UV_WRITABLE | UV_READABLE``. This
    function is blocking.

    Returns ``UV_ENOTSUP`` for Windows Unix domain sockets. Use filesystem ACLs
    to control access to their socket files.

    .. versionadded:: 1.16.0

.. c:function:: int uv_pipe(uv_file fds[2], int read_flags, int write_flags)

    Create a pair of connected pipe handles.
    Data may be written to `fds[1]` and read from `fds[0]`.
    The resulting handles can be passed to `uv_pipe_open`, used with `uv_spawn`,
    or for any other purpose.

    On Windows, the pair uses named pipes.

    Valid values for `flags` are:

      - UV_NONBLOCK_PIPE: Opens the specified socket handle for `OVERLAPPED`
        or `FIONBIO`/`O_NONBLOCK` I/O usage.
        This is recommended for handles that will be used by libuv,
        and not usually recommended otherwise.

    Equivalent to :man:`pipe(2)` with the `O_CLOEXEC` flag set.

    .. versionadded:: 1.41.0
