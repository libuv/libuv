
.. _udp2:

:c:type:`uv_udp2_t` --- UDP handle with ECN, PMTUD, and pktinfo
================================================================

``uv_udp2_t`` is a UDP handle type designed for protocols that require
per-packet metadata. It provides native support for:

* **ECN (Explicit Congestion Notification)** --- read ECN codepoints from
  received packets and mark outgoing packets.
* **Path MTU Discovery (PMTUD)** --- send datagrams with the Don't Fragment bit
  set so oversized sends fail with ``UV_EMSGSIZE``.
* **Packet destination address (pktinfo)** --- know which local address and
  interface received each incoming packet.

Unlike :c:type:`uv_udp_t`, which is unchanged for ABI stability, ``uv_udp2_t``
delivers per-packet metadata through a structured :c:type:`uv_udp2_recv_t`
passed to the receive callback. Features are enabled at bind time via
``UV_UDP2_*`` flags.


Data types
----------

.. c:type:: uv_udp2_t

    UDP2 handle type.

.. c:type:: uv_udp2_send_t

    UDP2 send request type.

.. c:type:: uv_udp2_recv_t

    Per-packet receive metadata. Stack-allocated by the library and passed to
    :c:type:`uv_udp2_recv_cb` by ``const`` pointer. Valid only for the duration
    of the callback invocation.

    ::

        struct uv_udp2_recv_s {
            ssize_t nread;                    /* >0: bytes, 0: EAGAIN, <0: error */
            const uv_buf_t* buf;              /* data buffer */
            const struct sockaddr* addr;      /* source (sender) address */
            struct sockaddr_storage local;    /* destination (local) address */
            unsigned int ifindex;             /* receiving interface index */
            int ecn;                          /* ECN codepoint 0-3 */
            unsigned int flags;               /* UV_UDP2_PARTIAL, etc. */
            unsigned int segment_size;        /* GRO segment stride; 0 if n/a */
        };

    .. c:member:: ssize_t uv_udp2_recv_t.nread

        Number of bytes received. Positive on success, 0 when there is nothing
        to read (EAGAIN), negative on error (a ``UV_E*`` code).

    .. c:member:: const uv_buf_t* uv_udp2_recv_t.buf

        Buffer containing the received data. The callee is responsible for
        freeing this buffer unless ``UV_UDP2_MMSG_CHUNK`` is set in ``flags``.

    .. c:member:: const struct sockaddr* uv_udp2_recv_t.addr

        Source (sender) address. ``NULL`` when ``nread <= 0``.

    .. c:member:: struct sockaddr_storage uv_udp2_recv_t.local

        Destination (local) address of the received packet. Populated when
        ``UV_UDP2_RECVPKTINFO`` was set at bind time. The ``ss_family`` field
        is ``AF_INET`` or ``AF_INET6``; zero if pktinfo is unavailable on the
        current platform.

    .. c:member:: unsigned int uv_udp2_recv_t.ifindex

        Index of the network interface that received the packet. Populated when
        ``UV_UDP2_RECVPKTINFO`` was set. Zero if unavailable.

    .. c:member:: int uv_udp2_recv_t.ecn

        ECN codepoint from the received packet's IP header. Populated when
        ``UV_UDP2_RECVECN`` was set at bind time:

        * ``0`` --- Not ECN-Capable Transport (Not-ECT), or ECN unavailable.
        * ``1`` --- ECN Capable Transport, codepoint ECT(1).
        * ``2`` --- ECN Capable Transport, codepoint ECT(0).
        * ``3`` --- Congestion Experienced (CE).

    .. c:member:: unsigned int uv_udp2_recv_t.flags

        Bitwise OR of receive flags: ``UV_UDP2_PARTIAL``,
        ``UV_UDP2_MMSG_CHUNK``, ``UV_UDP2_MMSG_FREE``,
        ``UV_UDP2_RECV_LINUX_RECVERR``.

    .. c:member:: unsigned int uv_udp2_recv_t.segment_size

        GRO segment stride in bytes. When ``UV_UDP2_GRO`` or
        ``UV_UDP2_GRO_RAW`` is active and the kernel delivered a coalesced
        super-packet, this is the per-segment size. Zero for normal
        (non-GRO) packets.

        With ``UV_UDP2_GRO``, libuv splits the super-packet and calls
        ``recv_cb`` once per segment; ``segment_size`` is informational.

        With ``UV_UDP2_GRO_RAW``, ``buf`` contains the entire super-packet
        and the application splits it:
        ``for offset in [0, nread, segment_size)``.

.. c:enum:: uv_udp2_flags

    Flags used in :c:func:`uv_udp2_bind`.

    ::

        enum uv_udp2_flags {
            UV_UDP2_IPV6ONLY          = 1,     /* Disable dual-stack mode. */
            UV_UDP2_REUSEADDR         = 2,     /* SO_REUSEADDR. */
            UV_UDP2_REUSEPORT         = 4,     /* SO_REUSEPORT load balancing. */
            UV_UDP2_LINUX_RECVERR     = 8,     /* Linux: IP_RECVERR error queue. */
            UV_UDP2_RECVMMSG          = 16,    /* Batch receive (recvmmsg). */
            UV_UDP2_RECVECN           = 32,    /* ECN codepoint reporting. */
            UV_UDP2_PMTUD             = 64,    /* Path MTU Discovery (set DF). */
            UV_UDP2_RECVPKTINFO       = 128,   /* Local address reporting. */
            UV_UDP2_PARTIAL           = 256,   /* Message truncated. */
            UV_UDP2_MMSG_CHUNK        = 512,   /* recvmmsg chunk; don't free. */
            UV_UDP2_MMSG_FREE         = 1024,  /* recvmmsg done; free buffer. */
            UV_UDP2_RECV_LINUX_RECVERR = 2048, /* MSG_ERRQUEUE delivery. */
            UV_UDP2_GRO               = 4096,  /* GRO; libuv splits. */
            UV_UDP2_GRO_RAW           = 8192,  /* GRO; app splits. */
            UV_UDP2_TXTIME            = 16384  /* SO_TXTIME; per-packet TX time. */
        };

.. c:enum:: uv_udp2_pmtud

    PMTUD modes for :c:func:`uv_udp2_set_pmtud`.

    ::

        enum uv_udp2_pmtud {
            UV_UDP2_PMTUD_OFF   = 0,  /* Disable PMTUD. */
            UV_UDP2_PMTUD_DO    = 1,  /* Set DF; enforce kernel PMTU cache. */
            UV_UDP2_PMTUD_PROBE = 2   /* Set DF; don't enforce cache. */
        };

.. c:type:: void (*uv_udp2_alloc_cb)(uv_udp2_t* handle, size_t suggested_size, uv_buf_t* buf)

    Allocation callback for :c:func:`uv_udp2_recv_start`.

.. c:type:: void (*uv_udp2_recv_cb)(uv_udp2_t* handle, const uv_udp2_recv_t* recv)

    Receive callback for :c:func:`uv_udp2_recv_start`. Called when data
    arrives or an error occurs.

    The ``recv`` pointer is valid only for the duration of the callback. The
    callee is responsible for freeing ``recv->buf->base`` unless
    ``UV_UDP2_MMSG_CHUNK`` is set in ``recv->flags``.

.. c:type:: void (*uv_udp2_send_cb)(uv_udp2_send_t* req, int status)

    Send completion callback for :c:func:`uv_udp2_send`.


Public members
^^^^^^^^^^^^^^

.. c:member:: size_t uv_udp2_t.send_queue_size

    Number of bytes queued for sending.

.. c:member:: size_t uv_udp2_t.send_queue_count

    Number of send requests currently in the queue.

.. c:member:: uv_udp2_t* uv_udp2_send_t.handle

    UDP2 handle where this send request is taking place.

.. seealso:: The :c:type:`uv_handle_t` members also apply.


API
---

Lifecycle
^^^^^^^^^

.. c:function:: int uv_udp2_init(uv_loop_t* loop, uv_udp2_t* handle)

    Initialize a new UDP2 handle. The actual socket is created lazily.
    Returns 0 on success.

.. c:function:: int uv_udp2_init_ex(uv_loop_t* loop, uv_udp2_t* handle, unsigned int flags)

    Initialize with the specified flags. The lower 8 bits of ``flags`` are used
    as the socket domain (``AF_INET``, ``AF_INET6``, or ``AF_UNSPEC``).

.. c:function:: int uv_udp2_open(uv_udp2_t* handle, uv_os_sock_t sock)

    Open an existing file descriptor or Windows SOCKET as a UDP2 handle.

.. c:function:: int uv_udp2_bind(uv_udp2_t* handle, const struct sockaddr* addr, unsigned int flags)

    Bind the handle to a local address. ``flags`` is a bitwise OR of values
    from :c:enum:`uv_udp2_flags`. The flags ``UV_UDP2_RECVECN``,
    ``UV_UDP2_PMTUD``, and ``UV_UDP2_RECVPKTINFO`` configure the
    corresponding socket options before binding.

    Flags for features unsupported on the current platform are silently
    ignored.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp2_connect(uv_udp2_t* handle, const struct sockaddr* addr)

    Associate the handle with a remote address. Pass ``NULL`` to disconnect.

    :returns: 0 on success, or an error code < 0 on failure.


Configuration
^^^^^^^^^^^^^

.. c:function:: int uv_udp2_configure(uv_udp2_t* handle, unsigned int flags)

    Configure socket options on a handle that already has a socket (after
    :c:func:`uv_udp2_init_ex` with an AF hint, :c:func:`uv_udp2_open`, or
    :c:func:`uv_udp2_bind`). Accepts ``UV_UDP2_RECVECN``, ``UV_UDP2_PMTUD``,
    ``UV_UDP2_RECVPKTINFO``, and ``UV_UDP2_LINUX_RECVERR``.

    :returns: 0 on success, ``UV_EBADF`` if no socket exists.

.. c:function:: int uv_udp2_set_ecn(uv_udp2_t* handle, int ecn)

    Set the ECN codepoint for all outgoing datagrams from this socket.

    :param ecn: ``0`` = Not-ECT, ``1`` = ECT(1), ``2`` = ECT(0), ``3`` = CE.

    Uses ``IP_TOS`` (IPv4) / ``IPV6_TCLASS`` (IPv6). Must be called after
    bind or connect. On Windows, CE (3) is rejected by the network stack.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp2_set_pmtud(uv_udp2_t* handle, enum uv_udp2_pmtud mode)

    Set the PMTUD mode. The ``UV_UDP2_PMTUD`` bind flag is equivalent to
    calling this with ``UV_UDP2_PMTUD_PROBE``.

    On BSD and macOS, ``UV_UDP2_PMTUD_DO`` and ``UV_UDP2_PMTUD_PROBE`` are
    equivalent (the platform only supports a boolean DF bit via
    ``IP_DONTFRAG``).

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp2_getmtu(const uv_udp2_t* handle, size_t* mtu)

    Query the kernel's cached path MTU for this socket.

    Only meaningful on a connected socket with ``UV_UDP2_PMTUD`` enabled.
    Supported on Linux (``IP_MTU`` / ``IPV6_MTU``) and Windows. Returns
    ``UV_ENOTSUP`` on BSD and macOS.

    :param mtu: On success, receives the PMTU value in bytes.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp2_set_ttl(uv_udp2_t* handle, int ttl)

    Set the time to live (1 through 255).

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp2_set_broadcast(uv_udp2_t* handle, int on)

    Set broadcast on or off.

    :returns: 0 on success, or an error code < 0 on failure.


Address queries
^^^^^^^^^^^^^^^

.. c:function:: int uv_udp2_getsockname(const uv_udp2_t* handle, struct sockaddr* name, int* namelen)

    Get the local IP and port of the handle.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp2_getpeername(const uv_udp2_t* handle, struct sockaddr* name, int* namelen)

    Get the remote IP and port on connected handles.

    :returns: 0 on success, ``UV_ENOTCONN`` if not connected.


Sending
^^^^^^^

.. c:function:: int uv_udp2_send(uv_udp2_send_t* req, uv_udp2_t* handle, const uv_buf_t bufs[], unsigned int nbufs, const struct sockaddr* addr, uv_udp2_send_cb send_cb)

    Send data over the UDP2 socket. If the socket has not previously been bound
    it will be bound to ``0.0.0.0`` and a random port.

    For connected handles, ``addr`` must be ``NULL``. For connectionless handles,
    ``addr`` must not be ``NULL``.

    When ``UV_UDP2_PMTUD`` is active and the datagram exceeds the path MTU, the
    callback will receive ``UV_EMSGSIZE``.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp2_try_send(uv_udp2_t* handle, const uv_buf_t bufs[], unsigned int nbufs, const struct sockaddr* addr)

    Same as :c:func:`uv_udp2_send`, but won't queue if it can't complete
    immediately.

    :returns: >= 0: bytes sent. < 0: error (``UV_EAGAIN`` if it would block).

.. c:function:: int uv_udp2_try_send2(uv_udp2_t* handle, unsigned int count, uv_buf_t* bufs[], unsigned int nbufs[], struct sockaddr* addrs[], unsigned int flags)

    Send multiple datagrams. Lightweight abstraction around
    :man:`sendmmsg(2)` / ``sendmsg_x``, with a :man:`sendmsg(2)` fallback.

    :returns: >= 0: number of datagrams sent. < 0: error on first datagram.


Receiving
^^^^^^^^^

.. c:function:: int uv_udp2_recv_start(uv_udp2_t* handle, uv_udp2_alloc_cb alloc_cb, uv_udp2_recv_cb recv_cb)

    Start receiving data. The ``recv_cb`` is called with a
    :c:type:`uv_udp2_recv_t` containing all per-packet metadata (ECN, local
    address, interface index) when the corresponding bind flags were set.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp2_recv_stop(uv_udp2_t* handle)

    Stop listening for incoming datagrams.

    :returns: 0 on success, or an error code < 0 on failure.

.. c:function:: int uv_udp2_using_recvmmsg(const uv_udp2_t* handle)

    Returns 1 if the handle was created with ``UV_UDP2_RECVMMSG`` and the
    platform supports it, 0 otherwise.


Queue queries
^^^^^^^^^^^^^

.. c:function:: size_t uv_udp2_get_send_queue_size(const uv_udp2_t* handle)

    Returns ``handle->send_queue_size``.

.. c:function:: size_t uv_udp2_get_send_queue_count(const uv_udp2_t* handle)

    Returns ``handle->send_queue_count``.

GSO / GRO
^^^^^^^^^

.. c:type:: uv_udp2_mmsg_t

    Describes a single message for :c:func:`uv_udp2_try_send_batch`.

    ::

        struct uv_udp2_mmsg_s {
            uv_buf_t* bufs;
            unsigned int nbufs;
            const struct sockaddr* addr;
            unsigned int gso_size;
            uint64_t txtime;
        };

    When ``gso_size`` is 0, the message is sent as a normal datagram.
    When ``gso_size`` is > 0, the kernel splits the concatenated buffer in
    ``bufs`` into segments of ``gso_size`` bytes (the last segment may be
    shorter). GSO requires Linux 4.18+; on other platforms the ``gso_size``
    field is ignored and each message must contain exactly one datagram.

    When ``txtime`` is nonzero and ``UV_UDP2_TXTIME`` was set at bind time,
    the Linux FQ qdisc holds the packet until the specified time (in
    nanoseconds since ``CLOCK_TAI``). Zero means send immediately. On
    non-Linux platforms ``txtime`` is ignored.

.. c:function:: int uv_udp2_try_send_batch(uv_udp2_t* handle, uv_udp2_mmsg_t* msgs, unsigned int count)

    Send a batch of datagrams with optional GSO (UDP Segmentation Offload).

    :param msgs: Array of messages to send.
    :param count: Number of messages in the array.
    :returns: >= 0: number of messages sent. < 0: error on first message.

.. c:function:: unsigned int uv_udp2_gso_max_segments(const uv_udp2_t* handle)

    Returns the maximum number of GSO segments per send, or 0 if GSO is
    unavailable on this platform or socket. Currently returns 64 on Linux
    and 0 elsewhere.

.. c:function:: int uv_udp2_set_cpu_affinity(uv_udp2_t* handle, int cpu)

    Set the CPU affinity for incoming packets on this socket. With
    ``UV_UDP2_REUSEPORT``, this pins the socket to receive packets processed
    by the specified CPU, reducing cross-CPU cache bouncing in multi-threaded
    servers. Linux 3.9+ only.

    :param cpu: CPU index to pin to.
    :returns: 0 on success, ``UV_ENOTSUP`` on non-Linux platforms.

.. seealso:: The :c:type:`uv_handle_t` API functions also apply.


Platform support
----------------

.. list-table::
   :header-rows: 1
   :widths: 30 10 10 10 10 10

   * - Feature
     - Linux
     - macOS
     - FreeBSD
     - OpenBSD
     - Windows
   * - ``UV_UDP2_RECVECN`` (IPv4)
     - Yes
     - Yes
     - Yes
     - no-op
     - Yes
   * - ``UV_UDP2_RECVECN`` (IPv6)
     - Yes
     - Yes
     - Yes
     - Yes
     - Yes
   * - ``UV_UDP2_PMTUD`` (IPv4)
     - Yes
     - Yes
     - Yes
     - no-op
     - Yes
   * - ``UV_UDP2_PMTUD`` (IPv6)
     - Yes
     - Yes
     - Yes
     - Yes
     - Yes
   * - ``UV_UDP2_RECVPKTINFO`` (IPv4)
     - Yes
     - Yes
     - Yes
     - Yes
     - Yes
   * - ``UV_UDP2_RECVPKTINFO`` (IPv6)
     - Yes
     - Yes
     - Yes
     - Yes
     - Yes
   * - ``uv_udp2_set_ecn``
     - Yes
     - Yes
     - Yes
     - Yes
     - Yes
   * - ``uv_udp2_getmtu``
     - Yes
     - ENOTSUP
     - ENOTSUP
     - ENOTSUP
     - Yes
   * - GSO (``uv_udp2_try_send_batch``)
     - Yes (4.18+)
     - no-op
     - no-op
     - no-op
     - no-op
   * - GRO (``UV_UDP2_GRO`` / ``UV_UDP2_GRO_RAW``)
     - Yes (5.0+)
     - no-op
     - no-op
     - no-op
     - no-op
   * - SO_TXTIME (``UV_UDP2_TXTIME``)
     - Yes (4.19+)
     - no-op
     - no-op
     - no-op
     - no-op
   * - SO_INCOMING_CPU (``uv_udp2_set_cpu_affinity``)
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
   On **OpenBSD**, ``IP_RECVTOS`` is unavailable (no IPv4 ECN read) and
   ``IP_DONTFRAG`` is absent (no IPv4 PMTUD). IPv6 features work normally.
   The ``IP_RECVDSTADDR`` option is used instead of ``IP_PKTINFO`` for
   destination address reporting.

.. note::
   On **Linux**, combining ``UV_UDP2_PMTUD`` with ``UV_UDP2_LINUX_RECVERR``
   enables PMTU errors to be delivered via the error queue with the discovered
   MTU value in ``sock_extended_err.ee_info``.


Example
-------

.. code-block:: c

    #include <uv.h>
    #include <stdio.h>
    #include <stdlib.h>

    static uv_udp2_t sock;

    static void alloc_cb(uv_udp2_t* handle, size_t suggested, uv_buf_t* buf) {
      buf->base = malloc(suggested);
      buf->len = suggested;
    }

    static void recv_cb(uv_udp2_t* handle, const uv_udp2_recv_t* recv) {
      if (recv->nread < 0) {
        fprintf(stderr, "recv error: %s\n", uv_strerror(recv->nread));
        free(recv->buf->base);
        return;
      }
      if (recv->nread == 0) {
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

      uv_udp2_init(loop, &sock);
      uv_ip4_addr("0.0.0.0", 4433, &addr);

      uv_udp2_bind(&sock, (struct sockaddr*)&addr,
                    UV_UDP2_RECVECN |
                    UV_UDP2_PMTUD |
                    UV_UDP2_RECVPKTINFO |
                    UV_UDP2_LINUX_RECVERR);

      uv_udp2_set_ecn(&sock, 2);  /* ECT(0) */

      uv_udp2_recv_start(&sock, alloc_cb, recv_cb);
      return uv_run(loop, UV_RUN_DEFAULT);
    }
