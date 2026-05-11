
.. _udp:

:c:type:`uv_udp_t` --- UDP handle
=================================

UDP handles encapsulate UDP communication for both clients and servers.


Data types
----------

.. c:type:: uv_udp_t

    UDP handle type.

.. c:type:: uv_udp_send_t

    UDP send request type.

.. c:enum:: uv_udp_flags

    Flags used in :c:func:`uv_udp_bind` and :c:type:`uv_udp_recv_cb`.

    ::

        enum uv_udp_flags {
            /* Disables dual stack mode. */
            UV_UDP_IPV6ONLY = 1,
            /*
             * Indicates message was truncated because read buffer was too small. The
             * remainder was discarded by the OS. Used in uv_udp_recv_cb.
             */
            UV_UDP_PARTIAL = 2,
            /*
             * Indicates if SO_REUSEADDR will be set when binding the handle.
             * This sets the SO_REUSEPORT socket flag on the BSDs (except for
             * DragonFlyBSD), OS X, and other platforms where SO_REUSEPORTs don't
             * have the capability of load balancing, as the opposite of what
             * UV_UDP_REUSEPORT would do. On other Unix platforms, it sets the
             * SO_REUSEADDR flag. What that means is that multiple threads or
             * processes can bind to the same address without error (provided
             * they all set the flag) but only the last one to bind will receive
             * any traffic, in effect "stealing" the port from the previous listener.
             */
            UV_UDP_REUSEADDR = 4,
            /*
             * Indicates that the message was received by recvmmsg, so the buffer provided
             * must not be freed by the recv_cb callback.
             */
            UV_UDP_MMSG_CHUNK = 8,
            /*
             * Indicates that the buffer provided has been fully utilized by recvmmsg and
             * that it should now be freed by the recv_cb callback. When this flag is set
             * in uv_udp_recv_cb, nread will always be 0 and addr will always be NULL.
             */
            UV_UDP_MMSG_FREE = 16,
            /*
             * Indicates if IP_RECVERR/IPV6_RECVERR will be set when binding the handle.
             * This sets IP_RECVERR for IPv4 and IPV6_RECVERR for IPv6 UDP sockets on
             * Linux. This stops the Linux kernel from suppressing some ICMP error messages
             * and enables full ICMP error reporting for faster failover.
             * This flag is no-op on platforms other than Linux.
             */
            UV_UDP_LINUX_RECVERR = 32,
            /*
             * Indicates if SO_REUSEPORT will be set when binding the handle.
             * This sets the SO_REUSEPORT socket option on supported platforms.
             * Unlike UV_UDP_REUSEADDR, this flag will make multiple threads or
             * processes that are binding to the same address and port "share"
             * the port, which means incoming datagrams are distributed across
             * the receiving sockets among threads or processes.
             *
             * This flag is available only on Linux 3.9+, DragonFlyBSD 3.6+,
             * FreeBSD 12.0+, Solaris 11.4, and AIX 7.2.5+ for now.
             */
            UV_UDP_REUSEPORT = 64,
            /*
             * Indicates that recvmmsg should be used, if available.
             */
            UV_UDP_RECVMMSG = 256,
            /* Enable ECN codepoint reporting on received datagrams. */
            UV_UDP_RECVECN = 512,
            /* Enable Path MTU Discovery (set the Don't Fragment bit). */
            UV_UDP_PMTUD = 1024,
            /* Enable local destination address and interface index reporting. */
            UV_UDP_RECVPKTINFO = 2048,
            /* Enable UDP GRO; libuv splits coalesced packets. */
            UV_UDP_GRO = 4096,
            /* Enable UDP GRO; application splits coalesced packets. */
            UV_UDP_GRO_RAW = 8192,
            /* Enable kernel packet pacing via SO_TXTIME. */
            UV_UDP_TXTIME = 16384,
            /* Indicates data from the Linux MSG_ERRQUEUE error queue. */
            UV_UDP_RECV_LINUX_RECVERR = 32768
        };

.. c:type:: void (*uv_udp_send_cb)(uv_udp_send_t* req, int status)

    Type definition for callback passed to :c:func:`uv_udp_send`, which is
    called after the data was sent.

.. c:type:: void (*uv_udp_recv_cb)(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags)

    Type definition for callback passed to :c:func:`uv_udp_recv_start`, which
    is called when the endpoint receives data.

    * `handle`: UDP handle
    * `nread`:  Number of bytes that have been received.
      0 if there is no more data to read. Note that 0 may also mean that an
      empty datagram was received (in this case `addr` is not NULL). < 0 if
      a transmission error was detected; if using :man:`recvmmsg(2)` no more
      chunks will be received and the buffer can be freed safely.
    * `buf`: :c:type:`uv_buf_t` with the received data.
    * `addr`: ``struct sockaddr*`` containing the address of the sender.
      Can be NULL. Valid for the duration of the callback only.
    * `flags`: One or more or'ed UV_UDP_* constants.

    The callee is responsible for freeing the buffer, libuv does not reuse it.
    The buffer may be a null buffer (where `buf->base` == NULL and `buf->len` == 0)
    on error.

    When using :man:`recvmmsg(2)`, chunks will have the `UV_UDP_MMSG_CHUNK` flag set,
    those must not be freed. If no errors occur, there will be a final callback with
    `nread` set to 0, `addr` set to NULL and the buffer pointing at the initially
    allocated data with the `UV_UDP_MMSG_CHUNK` flag cleared and the `UV_UDP_MMSG_FREE`
    flag set. If a UDP socket error occurs, `nread` will be < 0. In either scenario,
    the callee can now safely free the provided buffer.

    .. versionchanged:: 1.40.0 added the `UV_UDP_MMSG_FREE` flag.

    .. note::
        The receive callback will be called with `nread` == 0 and `addr` == NULL when there is
        nothing to read, and with `nread` == 0 and `addr` != NULL when an empty UDP packet is
        received.

.. c:enum:: uv_membership

    Membership type for a multicast address.

    ::

        typedef enum {
            UV_LEAVE_GROUP = 0,
            UV_JOIN_GROUP
        } uv_membership;


Public members
^^^^^^^^^^^^^^

.. c:member:: size_t uv_udp_t.send_queue_size

    Number of bytes queued for sending. This field strictly shows how much
    information is currently queued.

.. c:member:: size_t uv_udp_t.send_queue_count

    Number of send requests currently in the queue awaiting to be processed.

.. c:member:: uv_udp_t* uv_udp_send_t.handle

    UDP handle where this send request is taking place.

.. seealso:: The :c:type:`uv_handle_t` members also apply.


API
---

.. c:function:: int uv_udp_init(uv_loop_t* loop, uv_udp_t* handle)

    Initialize a new UDP handle. The actual socket is created lazily.
    Returns 0 on success.

.. c:function:: int uv_udp_init_ex(uv_loop_t* loop, uv_udp_t* handle, unsigned int flags)

    Initialize the handle with the specified flags. The lower 8 bits of the `flags`
    parameter are used as the socket domain. A socket will be created for the given domain.
    If the specified domain is ``AF_UNSPEC`` no socket is created, just like :c:func:`uv_udp_init`.

    The remaining bits can be used to set one of these flags:

    * `UV_UDP_RECVMMSG`: if set, and the platform supports it, :man:`recvmmsg(2)` will
      be used.

    .. versionadded:: 1.7.0
    .. versionchanged:: 1.37.0 added the `UV_UDP_RECVMMSG` flag.

.. c:function:: int uv_udp_open(uv_udp_t* handle, uv_os_sock_t sock)

    Opens an existing file descriptor or Windows SOCKET as a UDP handle.

    Unix only:
    The only requirement of the `sock` argument is that it follows the datagram
    contract (works in unconnected mode, supports sendmsg()/recvmsg(), etc).
    In other words, other datagram-type sockets like raw sockets or netlink
    sockets can also be passed to this function.

    .. versionchanged:: 1.2.1 the file descriptor is set to non-blocking mode.

    .. note::
        The passed file descriptor or SOCKET is not checked for its type, but
        it's required that it represents a valid datagram socket.

.. c:function:: int uv_udp_bind(uv_udp_t* handle, const struct sockaddr* addr, unsigned int flags)

    Bind the UDP handle to an IP address and port.

    :param handle: UDP handle. Should have been initialized with
        :c:func:`uv_udp_init`.

    :param addr: `struct sockaddr_in` or `struct sockaddr_in6`
        with the address and port to bind to.

    :param flags: Indicate how the socket will be bound. Supported flags:

        * ``UV_UDP_IPV6ONLY`` --- disable dual-stack mode.
        * ``UV_UDP_REUSEADDR`` --- set ``SO_REUSEADDR``.
        * ``UV_UDP_REUSEPORT`` --- set ``SO_REUSEPORT`` for load balancing.
        * ``UV_UDP_LINUX_RECVERR`` --- enable ``IP_RECVERR`` / ``IPV6_RECVERR``.
        * ``UV_UDP_RECVMMSG`` --- enable :man:`recvmmsg(2)` batch receive.
        * ``UV_UDP_RECVECN`` --- enable ECN codepoint reporting.
        * ``UV_UDP_PMTUD`` --- enable Path MTU Discovery (set DF bit).
        * ``UV_UDP_RECVPKTINFO`` --- enable local address and interface reporting.
        * ``UV_UDP_GRO`` --- enable GRO with automatic splitting by libuv.
        * ``UV_UDP_GRO_RAW`` --- enable GRO without splitting (app splits).
        * ``UV_UDP_TXTIME`` --- enable kernel packet pacing via ``SO_TXTIME``.

        Flags from ``UV_UDP_RECVECN`` onward are used with
        :c:func:`uv_udp_recv_start2`; see :ref:`udp-extensions` below.

    :returns: 0 on success, or an error code < 0 on failure.

    .. versionchanged:: 1.49.0 added the ``UV_UDP_REUSEPORT`` flag.

    .. note::
        ``UV_UDP_REUSEPORT`` flag is available only on Linux 3.9+, DragonFlyBSD 3.6+,
        FreeBSD 12.0+, Solaris 11.4, and AIX 7.2.5+ at the moment. On other platforms
        this function will return an UV_ENOTSUP error.
        For platforms where `SO_REUSEPORT`s have the capability of load balancing,
        specifying both ``UV_UDP_REUSEADDR`` and ``UV_UDP_REUSEPORT`` in flags is allowed
        and `SO_REUSEPORT` will always override the behavior of `SO_REUSEADDR`.
        For platforms where `SO_REUSEPORT`s don't have the capability of load balancing,
        specifying both ``UV_UDP_REUSEADDR`` and ``UV_UDP_REUSEPORT`` in flags will fail,
        returning an UV_ENOTSUP error.

.. c:function:: int uv_udp_connect(uv_udp_t* handle, const struct sockaddr* addr)

    Associate the UDP handle to a remote address and port, so every
    message sent by this handle is automatically sent to that destination.
    Calling this function with a `NULL` `addr` disconnects the handle.
    Trying to call `uv_udp_connect()` on an already connected handle will result
    in an `UV_EISCONN` error. Trying to disconnect a handle that is not
    connected will return an `UV_ENOTCONN` error.

    :param handle: UDP handle. Should have been initialized with
        :c:func:`uv_udp_init`.

    :param addr: `struct sockaddr_in` or `struct sockaddr_in6`
        with the address and port to associate to.

    :returns: 0 on success, or an error code < 0 on failure.

    .. versionadded:: 1.27.0

.. c:function:: int uv_udp_getpeername(const uv_udp_t* handle, struct sockaddr* name, int* namelen)

    Get the remote IP and port of the UDP handle on connected UDP handles.
    On unconnected handles, it returns `UV_ENOTCONN`.

    :param handle: UDP handle. Should have been initialized with
        :c:func:`uv_udp_init` and bound.

    :param name: Pointer to the structure to be filled with the address data.
        In order to support IPv4 and IPv6 `struct sockaddr_storage` should be
        used.

    :param namelen: On input it indicates the data of the `name` field. On
        output it indicates how much of it was filled.

    :returns: 0 on success, or an error code < 0 on failure

    .. versionadded:: 1.27.0

.. c:function:: int uv_udp_getsockname(const uv_udp_t* handle, struct sockaddr* name, int* namelen)

    Get the local IP and port of the UDP handle.

    :param handle: UDP handle. Should have been initialized with
        :c:func:`uv_udp_init` and bound.

    :param name: Pointer to the structure to be filled with the address data.
        In order to support IPv4 and IPv6 `struct sockaddr_storage` should be
        used.

    :param namelen: On input it indicates the data of the `name` field. On
        output it indicates how much of it was filled.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp_set_membership(uv_udp_t* handle, const char* multicast_addr, const char* interface_addr, uv_membership membership)

    Set membership for a multicast address

    :param handle: UDP handle. Should have been initialized with
        :c:func:`uv_udp_init`.

    :param multicast_addr: Multicast address to set membership for.

    :param interface_addr: Interface address.

    :param membership: Should be ``UV_JOIN_GROUP`` or ``UV_LEAVE_GROUP``.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp_set_source_membership(uv_udp_t* handle, const char* multicast_addr, const char* interface_addr, const char* source_addr, uv_membership membership)

    Set membership for a source-specific multicast group.

    :param handle: UDP handle. Should have been initialized with
        :c:func:`uv_udp_init`.

    :param multicast_addr: Multicast address to set membership for.

    :param interface_addr: Interface address.

    :param source_addr: Source address.

    :param membership: Should be ``UV_JOIN_GROUP`` or ``UV_LEAVE_GROUP``.

    :returns: 0 on success, or an error code < 0 on failure.

    .. versionadded:: 1.32.0

.. c:function:: int uv_udp_set_multicast_loop(uv_udp_t* handle, int on)

    Set IP multicast loop flag. Makes multicast packets loop back to
    local sockets.

    :param handle: UDP handle. Should have been initialized with
        :c:func:`uv_udp_init_ex` as either ``AF_INET`` or ``AF_INET6``, or have
        been bound to an address explicitly with :c:func:`uv_udp_bind`, or
        implicitly with :c:func:`uv_udp_send()` or :c:func:`uv_udp_recv_start`.

    :param on: 1 for on, 0 for off.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp_set_multicast_ttl(uv_udp_t* handle, int ttl)

    Set the multicast ttl.

    :param handle: UDP handle. Should have been initialized with
        :c:func:`uv_udp_init_ex` as either ``AF_INET`` or ``AF_INET6``, or have
        been bound to an address explicitly with :c:func:`uv_udp_bind`, or
        implicitly with :c:func:`uv_udp_send()` or :c:func:`uv_udp_recv_start`.

    :param ttl: 1 through 255.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp_set_multicast_interface(uv_udp_t* handle, const char* interface_addr)

    Set the multicast interface to send or receive data on.

    :param handle: UDP handle. Should have been initialized with
        :c:func:`uv_udp_init_ex` as either ``AF_INET`` or ``AF_INET6``, or have
        been bound to an address explicitly with :c:func:`uv_udp_bind`, or
        implicitly with :c:func:`uv_udp_send()` or :c:func:`uv_udp_recv_start`.

    :param interface_addr: interface address.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp_set_broadcast(uv_udp_t* handle, int on)

    Set broadcast on or off.

    :param handle: UDP handle. Should have been initialized with
        :c:func:`uv_udp_init_ex` as either ``AF_INET`` or ``AF_INET6``, or have
        been bound to an address explicitly with :c:func:`uv_udp_bind`, or
        implicitly with :c:func:`uv_udp_send()` or :c:func:`uv_udp_recv_start`.

    :param on: 1 for on, 0 for off.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp_set_ttl(uv_udp_t* handle, int ttl)

    Set the time to live.

    :param handle: UDP handle. Should have been initialized with
        :c:func:`uv_udp_init_ex` as either ``AF_INET`` or ``AF_INET6``, or have
        been bound to an address explicitly with :c:func:`uv_udp_bind`, or
        implicitly with :c:func:`uv_udp_send()` or :c:func:`uv_udp_recv_start`.

    :param ttl: 1 through 255.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp_send(uv_udp_send_t* req, uv_udp_t* handle, const uv_buf_t bufs[], unsigned int nbufs, const struct sockaddr* addr, uv_udp_send_cb send_cb)

    Send data over the UDP socket. If the socket has not previously been bound
    with :c:func:`uv_udp_bind` it will be bound to 0.0.0.0
    (the "all interfaces" IPv4 address) and a random port number.

    On Windows if the `addr` is initialized to point to an unspecified address
    (``0.0.0.0`` or ``::``) it will be changed to point to ``localhost``.
    This is done to match the behavior of Linux systems.

    For connected UDP handles, `addr` must be set to `NULL`, otherwise it will
    return `UV_EISCONN` error.

    For connectionless UDP handles, `addr` cannot be `NULL`, otherwise it will
    return `UV_EDESTADDRREQ` error.

    :param req: UDP request handle. Need not be initialized.

    :param handle: UDP handle. Should have been initialized with
        :c:func:`uv_udp_init`.

    :param bufs: List of buffers to send.

    :param nbufs: Number of buffers in `bufs`.

    :param addr: `struct sockaddr_in` or `struct sockaddr_in6` with the
        address and port of the remote peer.

    :param send_cb: Callback to invoke when the data has been sent out.

    :returns: 0 on success, or an error code < 0 on failure.

    .. versionchanged:: 1.19.0 added ``0.0.0.0`` and ``::`` to ``localhost``
        mapping

    .. versionchanged:: 1.27.0 added support for connected sockets

.. c:function:: int uv_udp_try_send(uv_udp_t* handle, const uv_buf_t bufs[], unsigned int nbufs, const struct sockaddr* addr)

    Same as :c:func:`uv_udp_send`, but won't queue a send request if it can't
    be completed immediately.

    For connected UDP handles, `addr` must be set to `NULL`, otherwise it will
    return `UV_EISCONN` error.

    For connectionless UDP handles, `addr` cannot be `NULL`, otherwise it will
    return `UV_EDESTADDRREQ` error.

    :returns: >= 0: number of bytes sent (it matches the given buffer size).
        < 0: negative error code (``UV_EAGAIN`` is returned when the message
        can't be sent immediately).

    .. versionchanged:: 1.27.0 added support for connected sockets

.. c:function:: int uv_udp_try_send2(uv_udp_t* handle, unsigned int count, uv_buf_t* bufs[/*count*/], unsigned int nbufs[/*count*/], struct sockaddr* addrs[/*count*/], unsigned int flags)

    Like :c:func:`uv_udp_try_send`, but can send multiple datagrams.
    Lightweight abstraction around :man:`sendmmsg(2)`, with a :man:`sendmsg(2)`
    fallback loop for platforms that do not support the former. The handle must
    be fully initialized; call :c:func:`uv_udp_bind` first.

    :returns: >= 0: number of datagrams sent. Zero only if `count` was zero.
        < 0: negative error code. Only if sending the first datagram fails,
        otherwise returns a positive send count. ``UV_EAGAIN`` when datagrams
        cannot be sent right now; fall back to :c:func:`uv_udp_send`.

    .. versionadded:: 1.50.0

.. c:function:: int uv_udp_recv_start(uv_udp_t* handle, uv_alloc_cb alloc_cb, uv_udp_recv_cb recv_cb)

    Prepare for receiving data. If the socket has not previously been bound
    with :c:func:`uv_udp_bind` it is bound to 0.0.0.0 (the "all interfaces"
    IPv4 address) and a random port number.

    :param handle: UDP handle. Should have been initialized with
        :c:func:`uv_udp_init`.

    :param alloc_cb: Callback to invoke when temporary storage is needed.

    :param recv_cb: Callback to invoke with received data.

    :returns: 0 on success, or an error code < 0 on failure.

    .. note::
        When using :man:`recvmmsg(2)`, the number of messages received at a time is limited
        by the number of max size dgrams that will fit into the buffer allocated in `alloc_cb`, and
        `suggested_size` in `alloc_cb` for udp_recv is always set to the size of 1 max size dgram.

    .. versionchanged:: 1.35.0 added support for :man:`recvmmsg(2)` on supported platforms).
                        The use of this feature requires a buffer larger than
                        2 * 64KB to be passed to `alloc_cb`.
    .. versionchanged:: 1.37.0 :man:`recvmmsg(2)` support is no longer enabled implicitly,
                        it must be explicitly requested by passing the `UV_UDP_RECVMMSG` flag to
                        :c:func:`uv_udp_init_ex`.
    .. versionchanged:: 1.39.0 :c:func:`uv_udp_using_recvmmsg` can be used in `alloc_cb` to
                        determine if a buffer sized for use with :man:`recvmmsg(2)` should be
                        allocated for the current handle/platform.

.. c:function:: int uv_udp_using_recvmmsg(uv_udp_t* handle)

    Returns 1 if the UDP handle was created with the `UV_UDP_RECVMMSG` flag
    and the platform supports :man:`recvmmsg(2)`, 0 otherwise.

    .. versionadded:: 1.39.0

.. c:function:: int uv_udp_recv_stop(uv_udp_t* handle)

    Stop listening for incoming datagrams.

    :param handle: UDP handle. Should have been initialized with
        :c:func:`uv_udp_init`.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: size_t uv_udp_get_send_queue_size(const uv_udp_t* handle)

    Returns `handle->send_queue_size`.

    .. versionadded:: 1.19.0

.. c:function:: size_t uv_udp_get_send_queue_count(const uv_udp_t* handle)

    Returns `handle->send_queue_count`.

    .. versionadded:: 1.19.0

.. seealso:: The :c:type:`uv_handle_t` API functions also apply.


.. _udp-extensions:

ECN, PMTUD, pktinfo, and GSO/GRO extensions
--------------------------------------------

The following API extensions add per-packet metadata support to
:c:type:`uv_udp_t`. Features are enabled at bind time via ``UV_UDP_*`` flags
and metadata is delivered through :c:type:`uv_udp_recv_t`.

Data types
^^^^^^^^^^

.. c:type:: uv_udp_recv_t

    Per-packet receive metadata. Stack-allocated by the library and passed to
    :c:type:`uv_udp_recv2_cb` by ``const`` pointer. Valid only for the duration
    of the callback invocation.

    ::

        struct uv_udp_recv_s {
            ssize_t nread;                    /* >0: bytes, 0: EAGAIN, <0: error */
            const uv_buf_t* buf;              /* data buffer */
            const struct sockaddr* addr;      /* source (sender) address */
            struct sockaddr_storage local;    /* destination (local) address */
            unsigned int ifindex;             /* receiving interface index */
            int ecn;                          /* ECN codepoint 0-3 */
            unsigned int flags;               /* UV_UDP_PARTIAL, etc. */
            unsigned int segment_size;        /* GRO segment stride; 0 if n/a */
        };

    .. c:member:: ssize_t uv_udp_recv_t.nread

        Number of bytes received. Positive on success, 0 when there is nothing
        to read (EAGAIN), negative on error (a ``UV_E*`` code).

    .. c:member:: const uv_buf_t* uv_udp_recv_t.buf

        Buffer containing the received data. The callee is responsible for
        freeing this buffer unless ``UV_UDP_MMSG_CHUNK`` is set in ``flags``.

    .. c:member:: const struct sockaddr* uv_udp_recv_t.addr

        Source (sender) address. ``NULL`` when ``nread <= 0``.

    .. c:member:: struct sockaddr_storage uv_udp_recv_t.local

        Destination (local) address of the received packet. Populated when
        ``UV_UDP_RECVPKTINFO`` was set at bind time. The ``ss_family`` field
        is ``AF_INET`` or ``AF_INET6``; zero if pktinfo is unavailable on the
        current platform.

    .. c:member:: unsigned int uv_udp_recv_t.ifindex

        Index of the network interface that received the packet. Populated when
        ``UV_UDP_RECVPKTINFO`` was set. Zero if unavailable.

    .. c:member:: int uv_udp_recv_t.ecn

        ECN codepoint from the received packet's IP header. Populated when
        ``UV_UDP_RECVECN`` was set at bind time:

        * ``0`` --- Not ECN-Capable Transport (Not-ECT), or ECN unavailable.
        * ``1`` --- ECN Capable Transport, codepoint ECT(1).
        * ``2`` --- ECN Capable Transport, codepoint ECT(0).
        * ``3`` --- Congestion Experienced (CE).

    .. c:member:: unsigned int uv_udp_recv_t.flags

        Bitwise OR of receive flags: ``UV_UDP_PARTIAL``,
        ``UV_UDP_MMSG_CHUNK``, ``UV_UDP_MMSG_FREE``,
        ``UV_UDP_RECV_LINUX_RECVERR``.

        When using :man:`recvmmsg(2)` with :c:func:`uv_udp_recv_start2`,
        ``UV_UDP_MMSG_CHUNK`` and ``UV_UDP_MMSG_FREE`` behave the same as with
        :c:func:`uv_udp_recv_start`: do not free the buffer while
        ``UV_UDP_MMSG_CHUNK`` is set; free it when ``UV_UDP_MMSG_FREE`` is set.

    .. c:member:: unsigned int uv_udp_recv_t.segment_size

        GRO segment stride in bytes. When ``UV_UDP_GRO`` or
        ``UV_UDP_GRO_RAW`` is active and the kernel delivered a coalesced
        super-packet, this is the per-segment size. Zero for normal
        (non-GRO) packets.

        With ``UV_UDP_GRO``, libuv splits the super-packet and calls
        ``recv_cb`` once per segment; ``segment_size`` is informational.

        With ``UV_UDP_GRO_RAW``, ``buf`` contains the entire super-packet
        and the application splits it:
        ``for offset in [0, nread, segment_size)``.

.. c:enum:: uv_udp_pmtud

    PMTUD modes for :c:func:`uv_udp_set_pmtud`.

    ::

        enum uv_udp_pmtud {
            UV_UDP_PMTUD_OFF   = 0,  /* Disable PMTUD. */
            UV_UDP_PMTUD_DO    = 1,  /* Set DF; enforce kernel PMTU cache. */
            UV_UDP_PMTUD_PROBE = 2   /* Set DF; don't enforce cache. */
        };

.. c:type:: void (*uv_udp_recv2_cb)(uv_udp_t* handle, const uv_udp_recv_t* recv)

    Receive callback for :c:func:`uv_udp_recv_start2`. Called when data
    arrives or an error occurs.

    The ``recv`` pointer is valid only for the duration of the callback. The
    callee is responsible for freeing ``recv->buf->base`` unless
    ``UV_UDP_MMSG_CHUNK`` is set in ``recv->flags``.

.. c:type:: uv_udp_mmsg_t

    Describes a single message for :c:func:`uv_udp_try_send_batch`.

    ::

        struct uv_udp_mmsg_s {
            uv_buf_t* bufs;
            unsigned int nbufs;
            const struct sockaddr* addr;
            unsigned int gso_size;
            uint64_t txtime;
        };

    When ``gso_size`` is 0, the message is sent as a normal datagram.
    When ``gso_size`` > 0, the kernel splits the concatenated buffer in
    ``bufs`` into segments of ``gso_size`` bytes (the last segment may be
    shorter). GSO requires Linux 4.18+; on other platforms the buffer is
    sent as a single datagram regardless of ``gso_size``.

    For connected handles, ``addr`` should be ``NULL``. For unconnected
    handles, ``addr`` must point to the destination address.

    When ``txtime`` is nonzero and ``UV_UDP_TXTIME`` was set at bind time,
    the Linux FQ qdisc holds the packet until the specified time (in
    nanoseconds since ``CLOCK_TAI``). Zero means send immediately. On
    non-Linux platforms ``txtime`` is ignored.


API
^^^

.. c:function:: int uv_udp_recv_start2(uv_udp_t* handle, uv_alloc_cb alloc_cb, uv_udp_recv2_cb recv_cb)

    Start receiving data with per-packet metadata. The ``recv_cb`` is called
    with a :c:type:`uv_udp_recv_t` containing all per-packet metadata (ECN,
    local address, interface index, GRO segment size) when the corresponding
    bind flags were set.

    Call :c:func:`uv_udp_recv_stop` before switching between
    :c:func:`uv_udp_recv_start` and :c:func:`uv_udp_recv_start2`.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp_configure(uv_udp_t* handle, unsigned int flags)

    Configure socket options on a handle that already has a socket (after
    :c:func:`uv_udp_init_ex` with an AF hint, :c:func:`uv_udp_open`, or
    :c:func:`uv_udp_bind`). This allows enabling features after the socket
    is created without re-binding.

    Accepted flags (Unix): ``UV_UDP_RECVECN``, ``UV_UDP_PMTUD``,
    ``UV_UDP_RECVPKTINFO``, ``UV_UDP_LINUX_RECVERR``, ``UV_UDP_GRO``,
    ``UV_UDP_GRO_RAW``, ``UV_UDP_TXTIME``.

    Accepted flags (Windows): ``UV_UDP_RECVECN``, ``UV_UDP_PMTUD``,
    ``UV_UDP_RECVPKTINFO``.

    :returns: 0 on success, ``UV_EBADF`` if no socket exists, ``UV_EINVAL``
        for unrecognized flags.

.. c:function:: int uv_udp_set_ecn(uv_udp_t* handle, int ecn)

    Set the ECN codepoint for all outgoing datagrams from this socket.

    :param ecn: ``0`` = Not-ECT, ``1`` = ECT(1), ``2`` = ECT(0), ``3`` = CE.

    Uses ``IP_TOS`` (IPv4) / ``IPV6_TCLASS`` (IPv6). Must be called after
    bind or connect. On Windows, CE (3) is rejected by the network stack.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp_set_pmtud(uv_udp_t* handle, enum uv_udp_pmtud mode)

    Set the PMTUD mode. The ``UV_UDP_PMTUD`` bind flag is equivalent to
    calling this with ``UV_UDP_PMTUD_PROBE``.

    On BSD and macOS, ``UV_UDP_PMTUD_DO`` and ``UV_UDP_PMTUD_PROBE`` are
    equivalent (the platform only supports a boolean DF bit via
    ``IP_DONTFRAG``).

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp_getmtu(const uv_udp_t* handle, size_t* mtu)

    Query the kernel's cached path MTU for this socket.

    Only meaningful on a connected socket with ``UV_UDP_PMTUD`` enabled.
    Supported on Linux (``IP_MTU`` / ``IPV6_MTU``) and Windows. Returns
    ``UV_ENOTSUP`` on BSD and macOS.

    :param mtu: On success, receives the PMTU value in bytes.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp_try_send_batch(uv_udp_t* handle, uv_udp_mmsg_t* msgs, unsigned int count)

    Send a batch of datagrams with optional GSO (UDP Segmentation Offload).

    :param msgs: Array of messages to send. For connected handles, set
        ``msgs[i].addr`` to ``NULL``.
    :param count: Number of messages in the array.
    :returns: >= 0: number of messages sent. < 0: error on first message.

.. c:function:: unsigned int uv_udp_gso_max_segments(const uv_udp_t* handle)

    Returns the maximum number of GSO segments per send, or 0 if GSO is
    unavailable on this platform or socket. Currently returns 64 on Linux
    and 0 elsewhere.

.. c:function:: int uv_udp_set_cpu_affinity(uv_udp_t* handle, int cpu)

    Set the CPU affinity for incoming packets on this socket. With
    ``UV_UDP_REUSEPORT``, this pins the socket to receive packets processed
    by the specified CPU, reducing cross-CPU cache bouncing in multi-threaded
    servers. Linux 3.9+ only.

    :param cpu: CPU index to pin to.
    :returns: 0 on success, ``UV_ENOTSUP`` on non-Linux platforms.


Platform support
^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 30 10 10 10 10 10

   * - Feature
     - Linux
     - macOS
     - FreeBSD
     - OpenBSD
     - Windows
   * - ``UV_UDP_RECVECN`` (IPv4)
     - Yes
     - Yes
     - Yes
     - no-op
     - Yes
   * - ``UV_UDP_RECVECN`` (IPv6)
     - Yes
     - Yes
     - Yes
     - Yes
     - Yes
   * - ``UV_UDP_PMTUD`` (IPv4)
     - Yes
     - Yes
     - Yes
     - no-op
     - Yes
   * - ``UV_UDP_PMTUD`` (IPv6)
     - Yes
     - Yes
     - Yes
     - Yes
     - Yes
   * - ``UV_UDP_RECVPKTINFO`` (IPv4)
     - Yes
     - Yes
     - Yes
     - Yes
     - Yes
   * - ``UV_UDP_RECVPKTINFO`` (IPv6)
     - Yes
     - no-op
     - Yes
     - Yes
     - Yes
   * - ``uv_udp_set_ecn``
     - Yes
     - Yes
     - Yes
     - Yes
     - Yes
   * - ``uv_udp_getmtu``
     - Yes
     - ENOTSUP
     - ENOTSUP
     - ENOTSUP
     - Yes
   * - GSO (``uv_udp_try_send_batch``)
     - Yes (4.18+)
     - no-op
     - no-op
     - no-op
     - no-op
   * - GRO (``UV_UDP_GRO`` / ``UV_UDP_GRO_RAW``)
     - Yes (5.0+)
     - no-op
     - no-op
     - no-op
     - no-op
   * - SO_TXTIME (``UV_UDP_TXTIME``)
     - Yes (4.19+)
     - no-op
     - no-op
     - no-op
     - no-op
   * - SO_INCOMING_CPU (``uv_udp_set_cpu_affinity``)
     - Yes (3.9+)
     - ENOTSUP
     - ENOTSUP
     - ENOTSUP
     - ENOTSUP

*"no-op"* means the flag is silently ignored; the socket remains usable.

.. note::
   On **macOS**, received IPv4 ECN ancillary data uses ``cmsg_type == IP_RECVTOS``
   rather than ``IP_TOS`` as on Linux and FreeBSD. This is handled internally.

.. note::
   On **macOS**, ``IPV6_RECVPKTINFO`` is not available. IPv6 pktinfo
   (``UV_UDP_RECVPKTINFO``) is silently ignored; ``recv->local.ss_family``
   will be 0.

.. note::
   On **OpenBSD**, ``IP_RECVTOS`` is unavailable (no IPv4 ECN read) and
   ``IP_DONTFRAG`` is absent (no IPv4 PMTUD). IPv6 features work normally.
   The ``IP_RECVDSTADDR`` option is used instead of ``IP_PKTINFO`` for
   destination address reporting.

.. note::
   On **Linux**, combining ``UV_UDP_PMTUD`` with ``UV_UDP_LINUX_RECVERR``
   enables PMTU errors to be delivered via the error queue with the discovered
   MTU value in ``sock_extended_err.ee_info``.


Example
^^^^^^^

.. code-block:: c

    #include <uv.h>
    #include <stdio.h>
    #include <stdlib.h>

    static uv_udp_t sock;

    static void alloc_cb(uv_handle_t* handle, size_t suggested, uv_buf_t* buf) {
      buf->base = malloc(suggested);
      buf->len = suggested;
    }

    static void recv_cb(uv_udp_t* handle, const uv_udp_recv_t* recv) {
      if (recv->nread < 0) {
        fprintf(stderr, "recv error: %s\n", uv_strerror(recv->nread));
        free(recv->buf->base);
        return;
      }
      if (recv->nread == 0) {
        if (recv->buf->base)
          free(recv->buf->base);
        return;
      }

      printf("Received %zd bytes, ECN=%d\n", recv->nread, recv->ecn);

      if (recv->local.ss_family != 0) {
        printf("  interface index: %u\n", recv->ifindex);
      }

      free(recv->buf->base);
    }

    int main(void) {
      uv_loop_t* loop = uv_default_loop();
      struct sockaddr_in addr;

      uv_udp_init(loop, &sock);
      uv_ip4_addr("0.0.0.0", 4433, &addr);

      uv_udp_bind(&sock, (struct sockaddr*)&addr,
                    UV_UDP_RECVECN |
                    UV_UDP_PMTUD |
                    UV_UDP_RECVPKTINFO |
                    UV_UDP_LINUX_RECVERR);

      uv_udp_set_ecn(&sock, 2);  /* ECT(0) */

      uv_udp_recv_start2(&sock, alloc_cb, recv_cb);
      return uv_run(loop, UV_RUN_DEFAULT);
    }
