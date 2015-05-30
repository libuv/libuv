
.. _device:

:c:type:`uv_device_t` --- Device handle
=======================================

Device handles are used to read and write on char/block device.

:c:type:`uv_device_t` is a 'subclass' of :c:type:`uv_stream_t`.


Data types
----------

.. c:type:: uv_device_t

    Device handle type.

.. c:type:: uv_ioargs_t

    Device abstract arg struct, it is related with OS.

    ::

        /* on windows */
        typedef struct uv_ioargs_s {
          void* input;
          uint32_t input_len;
          void* output;
          uint32_t output_len;
        } uv_ioargs_t; 

        /* on ?nux */
        typedef struct uv_ioargs_s {
          void* arg;
        } uv_ioargs_t;

    On windows, `uv_ioargs_t` contains paramaters pass to `DeviceIoControl` 
    function, please see: https://msdn.microsoft.com/en-us/library/windows/desktop/aa363216(v=vs.85).aspx

    On linux `uv_ioargs_t` conatins arg (an untyped pointer to memory) pass to 
    `ioctl` at 3rd paramater,  which decided by request type.


Public members
^^^^^^^^^^^^^^

N/A

.. seealso:: The :c:type:`uv_stream_t` members also apply.


API
---

.. c:function::  int uv_device_init(uv_loop_t* handle, uv_device_t* device, const char* path, int flags);

    Initialize a new device with the given path. `path` must point a dev path, 
    for example ``/dev/device_name`` on linux, bsd or osx, and 
    ``\.\Global\DeviceName`` or ``\\.\DeviceName`` on windows. `flags` must be
    O_RDONLY, O_WRONLY or O_RDWR, and other values to open device on linux, 
    like O_NOCTTY when opening a serial device.

.. c:function::  int uv_device_open(uv_loop_t* handle, uv_device_t* device, uv_os_fd_t fd);

    Initialize a new device with the given file descriptor. `fd` must be a dev
    file descriptor. We assume that passed `fd` are readable and writable, if
    it's not, the read or write operations will fail.

.. c:function:: int uv_device_ioctl(uv_device_t* device, unsigned long cmd, uv_ioargs_t* args)

    Set or get device paramater, just wrap for DeviceIOControl or ioctl, `cmd` 
    is a device-dependent request code, request in or out data has encoded in 
    `args`. Basicly return code >=0 for success, but device io control heavely
    depend on lower drivers, so you must know what are you doing.

.. seealso:: The :c:type:`uv_stream_t` API functions also apply.

