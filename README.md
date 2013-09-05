# libuv

libuv is a platform layer for [node.js][]. Its purpose is to abstract IOCP
on Windows and epoll/kqueue/event ports/etc. on Unix systems. We intend to
eventually contain all platform differences in this library.

## Features

 * Non-blocking TCP sockets

 * Non-blocking named pipes

 * UDP

 * Timers

 * Child process spawning

 * Asynchronous DNS via `uv_getaddrinfo`.

 * Asynchronous file system APIs `uv_fs_*`

 * High resolution time `uv_hrtime`

 * Current executable path look up `uv_exepath`

 * Thread pool scheduling `uv_queue_work`

 * ANSI escape code controlled TTY `uv_tty_t`

 * File system events using inotify, kqueue, event ports,
   FSEvents and `ReadDirectoryChangesW`

 * IPC and socket sharing between processes `uv_write2`

## Community

 * [Mailing list](http://groups.google.com/group/libuv)

## Documentation

 * [include/uv.h](https://github.com/joyent/libuv/blob/master/include/uv.h)
   &mdash; API documentation in the form of detailed header comments.
 * [An Introduction to libuv](http://nikhilm.github.com/uvbook/) &mdash; An
   overview of libuv with tutorials.
 * [LXJS 2012 talk](http://www.youtube.com/watch?v=nGn60vDSxQ4) - High-level
   introductory talk about libuv.
 * [Tests and benchmarks](https://github.com/joyent/libuv/tree/master/test) -
   API specification and usage examples.

## Build Instructions

For GCC there are two methods building: via autotools or via [GYP][].
GYP is a meta-build system which can generate MSVS, Makefile, and XCode
backends. It is best used for integration into other projects.

To build with autotools:

    $ sh autogen.sh
    $ ./configure
    $ make
    $ make check
    $ make install

### Windows

First, Python 2.6 or 2.7 must be installed as it is required by [GYP][].

Also, the directory for the preferred Python executable must be specified
by the `PYTHON` or `Path` environment variables.

To build with Visual Studio, launch a git shell (e.g. Cmd or PowerShell)
and run vcbuild.bat which will checkout the GYP code into build/gyp and
generate uv.sln as well as related project files.

To have GYP generate build script for another system, checkout GYP into the
project tree manually:

    $ mkdir -p build
    $ git clone https://git.chromium.org/external/gyp.git build/gyp

### Unix

Run:

    $ ./gyp_uv -f make
    $ make -C out

### OS X

Run:

    $ ./gyp_uv -f xcode
    $ xcodebuild -project uv.xcodeproj -configuration Release -target All

### Android

Run:

    $ source ./android-configure NDK_PATH gyp
    $ make -C out

Note for UNIX users: compile your project with `-D_LARGEFILE_SOURCE` and
`-D_FILE_OFFSET_BITS=64`. GYP builds take care of that automatically.

## Supported Platforms

Microsoft Windows operating systems since Windows XP SP2. It can be built
with either Visual Studio or MinGW. Consider using
[Visual Studio Express 2010][] or later if you do not have a full Visual
Studio license.

Linux using the GCC toolchain.

OS X using the GCC or XCode toolchain.

Solaris 121 and later using GCC toolchain.

[node.js]: http://nodejs.org/
[GYP]: http://code.google.com/p/gyp/
[Visual Studio Express 2010]: http://www.microsoft.com/visualstudio/eng/products/visual-studio-2010-express
