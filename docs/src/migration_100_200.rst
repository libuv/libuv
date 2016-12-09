
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
