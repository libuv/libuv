Networking
==========

Networking in libuv is not much different from directly using the BSD socket
interface, some things are easier, all are non-blocking, but the concepts stay
the same. In addition libuv offers utility functions to abstract the annoying,
repetitive and low-level tasks like setting up sockets using the BSD socket
structures, DNS lookup, and tweaking various socket parameters.

The ``uv_tcp_t`` and ``uv_udp_t`` structures are used for network I/O.

.. NOTE::

  The code samples in this chapter exist to show certain libuv APIs. They are
  not examples of good quality code. They leak memory and don't always close
  connections properly.

TCP
---

TCP is a connection oriented, stream protocol and is therefore based on the
libuv streams infrastructure.

Server
++++++

Server sockets proceed by:

1. ``uv_tcp_init`` the TCP handle.
2. ``uv_tcp_bind`` it.
3. Call ``uv_listen`` on the handle to have a callback invoked whenever a new
   connection is established by a client.
4. Use ``uv_accept`` to accept the connection.
5. Use :ref:`stream operations <buffers-and-streams>` to communicate with the
   client.

Here is a simple echo server

.. rubric:: tcp-echo-server/main.c - The listen socket
.. literalinclude:: ../../code/tcp-echo-server/main.c
    :language: c
    :linenos:
    :lines: 68-
    :emphasize-lines: 4-5,7-10

You can see the utility function ``uv_ip4_addr`` being used to convert from
a human readable IP address, port pair to the sockaddr_in structure required by
the BSD socket APIs. The reverse can be obtained using ``uv_ip4_name``.

.. NOTE::

    There are ``uv_ip6_*`` analogues for the ip4 functions.

Most of the setup functions are synchronous since they are CPU-bound.
``uv_listen`` is where we return to libuv's callback style. The second
arguments is the backlog queue -- the maximum length of queued connections.

When a connection is initiated by clients, the callback is required to set up
a handle for the client socket and associate the handle using ``uv_accept``.
In this case we also establish interest in reading from this stream.

.. rubric:: tcp-echo-server/main.c - Accepting the client
.. literalinclude:: ../../code/tcp-echo-server/main.c
    :language: c
    :linenos:
    :lines: 51-66
    :emphasize-lines: 9-10

The remaining set of functions is very similar to the streams example and can
be found in the code. Just remember to call ``uv_close`` when the socket isn't
required. This can be done even in the ``uv_listen`` callback if you are not
interested in accepting the connection.

Client
++++++

Where you do bind/listen/accept on the server, on the client side it's simply
a matter of calling ``uv_tcp_connect``. The same ``uv_connect_cb`` style
callback of ``uv_listen`` is used by ``uv_tcp_connect``. Try::

    uv_tcp_t* socket = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
    uv_tcp_init(loop, socket);

    uv_connect_t* connect = (uv_connect_t*)malloc(sizeof(uv_connect_t));

    struct sockaddr_in dest;
    uv_ip4_addr("127.0.0.1", 80, &dest);

    uv_tcp_connect(connect, socket, (const struct sockaddr*)&dest, on_connect);

where ``on_connect`` will be called after the connection is established. The
callback receives the ``uv_connect_t`` struct, which has a member ``.handle``
pointing to the socket.

UDP
---

The `User Datagram Protocol`_ offers connectionless, unreliable network
communication. Hence libuv doesn't offer a stream. Instead libuv provides
non-blocking UDP support via the `uv_udp_t` handle (for receiving) and
`uv_udp_send_t` request (for sending) and related functions. That said, the
actual API for reading/writing is very similar to normal stream reads. To look
at how UDP can be used, the example shows the first stage of obtaining an IP
address from a `DHCP`_ server -- DHCP Discover.

.. note::

    You will have to run `udp-dhcp` as **root** since it uses well known port
    numbers below 1024.

.. rubric:: udp-dhcp/main.c - Setup and send UDP packets
.. literalinclude:: ../../code/udp-dhcp/main.c
    :language: c
    :linenos:
    :lines: 7-11,104-
    :emphasize-lines: 8,10-11,17-18,21

.. note::

    The IP address ``0.0.0.0`` is used to bind to all interfaces. The IP
    address ``255.255.255.255`` is a broadcast address meaning that packets
    will be sent to all interfaces on the subnet.  port ``0`` means that the OS
    randomly assigns a port.

First we setup the receiving socket to bind on all interfaces on port 68 (DHCP
client) and start a read on it. This will read back responses from any DHCP
server that replies. We use the UV_UDP_REUSEADDR flag to play nice with any
other system DHCP clients that are running on this computer on the same port.
Then we setup a similar send socket and use ``uv_udp_send`` to send
a *broadcast message* on port 67 (DHCP server).

It is **necessary** to set the broadcast flag, otherwise you will get an
``EACCES`` error [#]_. The exact message being sent is not relevant to this
book and you can study the code if you are interested. As usual the read and
write callbacks will receive a status code of < 0 if something went wrong.

Since UDP sockets are not connected to a particular peer, the read callback
receives an extra parameter about the sender of the packet.

``nread`` may be zero if there is no more data to be read. If ``addr`` is NULL,
it indicates there is nothing to read (the callback shouldn't do anything), if
not NULL, it indicates that an empty datagram was received from the host at
``addr``. The ``flags`` parameter may be ``UV_UDP_PARTIAL`` if the buffer
provided by your allocator was not large enough to hold the data. *In this case
the OS will discard the data that could not fit* (That's UDP for you!).

.. rubric:: udp-dhcp/main.c - Reading packets
.. literalinclude:: ../../code/udp-dhcp/main.c
    :language: c
    :linenos:
    :lines: 17-40
    :emphasize-lines: 1,23

UDP Options
+++++++++++

Time-to-live
~~~~~~~~~~~~

The TTL of packets sent on the socket can be changed using ``uv_udp_set_ttl``.

IPv6 stack only
~~~~~~~~~~~~~~~

IPv6 sockets can be used for both IPv4 and IPv6 communication. If you want to
restrict the socket to IPv6 only, pass the ``UV_UDP_IPV6ONLY`` flag to
``uv_udp_bind`` [#]_.

Multicast
~~~~~~~~~

A socket can (un)subscribe to a multicast group using:

.. code::block:: c

    int uv_udp_set_membership(uv_udp_t* handle, const char* multicast_addr, const char* interface_addr, uv_membership membership);

where ``membership`` is ``UV_JOIN_GROUP`` or ``UV_LEAVE_GROUP``.

The concepts of multicasting are nicely explained in `this guide`_.

.. _this guide: https://www.tldp.org/HOWTO/Multicast-HOWTO-2.html

Local loopback of multicast packets is enabled by default [#]_, use
``uv_udp_set_multicast_loop`` to switch it off.

The packet time-to-live for multicast packets can be changed using
``uv_udp_set_multicast_ttl``.

Querying DNS
------------

libuv provides asynchronous DNS resolution. For this it provides its own
``getaddrinfo`` replacement [#]_. In the callback you can
perform normal socket operations on the retrieved addresses. Let's connect to
Libera.chat to see an example of DNS resolution.

.. rubric:: dns/main.c
.. literalinclude:: ../../code/dns/main.c
    :language: c
    :linenos:
    :lines: 61-
    :emphasize-lines: 12

If ``uv_getaddrinfo`` returns non-zero, something went wrong in the setup and
your callback won't be invoked at all. All arguments can be freed immediately
after ``uv_getaddrinfo`` returns. The `hostname`, `servname` and `hints`
structures are documented in `the getaddrinfo man page <getaddrinfo_>`_. The
callback can be ``NULL`` in which case the function will run synchronously.

In the resolver callback, you can pick any IP from the linked list of ``struct
addrinfo(s)``. This also demonstrates ``uv_tcp_connect``. It is necessary to
call ``uv_freeaddrinfo`` in the callback.

.. rubric:: dns/main.c
.. literalinclude:: ../../code/dns/main.c
    :language: c
    :linenos:
    :lines: 42-60
    :emphasize-lines: 8,16

libuv also provides the inverse `uv_getnameinfo`_.

.. _uv_getnameinfo: http://docs.libuv.org/en/v1.x/dns.html#c.uv_getnameinfo

Network interfaces
------------------

Information about the system's network interfaces can be obtained through libuv
using ``uv_interface_addresses``. This simple program just prints out all the
interface details so you get an idea of the fields that are available. This is
useful to allow your service to bind to IP addresses when it starts.

.. rubric:: interfaces/main.c
.. literalinclude:: ../../code/interfaces/main.c
    :language: c
    :linenos:
    :emphasize-lines: 9,17

``is_internal`` is true for loopback interfaces. Note that if a physical
interface has multiple IPv4/IPv6 addresses, the name will be reported multiple
times, with each address being reported once.

.. _c-ares: https://c-ares.haxx.se
.. _getaddrinfo: https://man7.org/linux/man-pages/man3/getaddrinfo.3.html

.. _User Datagram Protocol: https://en.wikipedia.org/wiki/User_Datagram_Protocol
.. _DHCP: https://tools.ietf.org/html/rfc2131

----

.. [#] https://beej.us/guide/bgnet/html/#broadcast-packetshello-world
.. [#] on Windows only supported on Windows Vista and later.
.. [#] https://www.tldp.org/HOWTO/Multicast-HOWTO-6.html#ss6.1
.. [#] libuv use the system ``getaddrinfo`` in the libuv threadpool. libuv
    v0.8.0 and earlier also included c-ares_ as an alternative, but this has been
    removed in v0.9.0.
