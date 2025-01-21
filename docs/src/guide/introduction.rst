Introduction
============

This 'book' is a small set of tutorials about using libuv_ as
a high performance evented I/O library which offers the same API on Windows and Unix.

It is meant to cover the main areas of libuv, but is not a comprehensive
reference discussing every function and data structure. The `official libuv
documentation`_ may be consulted for full details.

.. _official libuv documentation: https://docs.libuv.org/en/v1.x/

This book is still a work in progress, so sections may be incomplete, but
I hope you will enjoy it as it grows.

Who this book is for
--------------------

If you are reading this book, you are either:

1) a systems programmer, creating low-level programs such as daemons or network
   services and clients. You have found that the event loop approach is well
   suited for your application and decided to use libuv.

2) a node.js module writer, who wants to wrap platform APIs
   written in C or C++ with a set of (a)synchronous APIs that are exposed to
   JavaScript. You will use libuv purely in the context of node.js. For
   this you will require some other resources as the book does not cover parts
   specific to v8/node.js.

This book assumes that you are comfortable with the C programming language.

Background
----------

The node.js_ project began in 2009 as a JavaScript environment decoupled
from the browser. Using Google's V8_ and Marc Lehmann's libev_, node.js
combined a model of I/O -- evented -- with a language that was well suited to
the style of programming; due to the way it had been shaped by browsers. As
node.js grew in popularity, it was important to make it work on Windows, but
libev ran only on Unix. The Windows equivalent of kernel event notification
mechanisms like kqueue or (e)poll is IOCP. libuv was an abstraction around libev
or IOCP depending on the platform, providing users an API based on libev.
In the node-v0.9.0 version of libuv `libev was removed`_.

Since then libuv has continued to mature and become a high quality standalone
library for system programming. Users outside of node.js include Mozilla's
Rust_ programming language, and a variety_ of language bindings.

This book and the code is based on libuv version `v1.42.0`_.

Code
----

All the example code and the source of the book is included as part of
the libuv_ project on GitHub.
Clone or Download libuv_, then build it::

    sh autogen.sh
    ./configure
    make

There is no need to ``make install``. To build the examples run ``make`` in the
``docs/code/`` directory.

.. _v1.42.0: https://github.com/libuv/libuv/releases/tag/v1.42.0
.. _V8: https://v8.dev
.. _libev: http://software.schmorp.de/pkg/libev.html
.. _libuv: https://github.com/libuv/libuv
.. _node.js: https://www.nodejs.org
.. _libev was removed: https://github.com/joyent/libuv/issues/485
.. _Rust: https://www.rust-lang.org
.. _variety: https://github.com/libuv/libuv/blob/v1.x/LINKS.md
