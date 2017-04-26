
.. _migration_100_200:

libuv v1 -> v2 migration guide
===================================

Some APIs changed quite a bit throughout the v2 development process. Here
is a migration guide for the most significant changes that happened after v1
was released.


Windows HANDLEs are used instead of MSVCRT file descriptors
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``uv_file`` typedef was deleted and replaced by ``uv_os_fd_t`` uniformly across all APIs.
Constants ``UV_STDIN_FD``, ``UV_STDOUT_FD``, and ``UV_STDERR_FD`` provide cross-platform
references to the standard constants for stdio access, and can be used in place of any ``uv_os_fd_t``.
Existing clients can transition to the new API by using the conversion function ``uv_convert_fd_to_handle``
defined in the libuv header file.

For example, previously a library might pass ``0`` or ``STDIN_FILENO`` to ``uv_guess_handle`` to determine its type
before calling the appropriate uv_open function.
Now those uses should be handled by passing the constant ``UV_STDIN_FD`` instead.

If instead a library had an open file descriptor inherited from some other library,
instead of using the ``int fd`` directly, the value should first be converted to a native handle
by either calling ``uv_convert_fd_to_handle`` or ``uv_get_osfhandle``, depending on the required lifetime.
For example, a client might need to call::

    int fd;
    uv_os_fd_t newfd;
    DuplicateHandle(GetCurrentProcess(), uv_get_osfhandle(fd),
                    GetCurrentProcess(), &newfd,
                    0, FALSE, DUPLICATE_SAME_ACCESS);

to get a new handle to the file where either handle could be closed without affecting the other,
and the closing of both would be required to release the underlying file resource.
Or if it is a ``SOCKET``, the user may need to call ``WSADuplicateSocket``.
