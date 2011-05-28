Warning: this is not actual API but desired API.

# `uv_handle_t`

This is the abstract base class of all types of handles. All handles have in
common:

* When handles are initialized, the reference count to the event loop is
  increased by one.

* The user owns the `uv_handle_t` memory and is in charge of freeing it.

* In order to free resources associated with a handle, one must `uv_close()`
  and wait for the `uv_close_cb` callback. After the close callback has been
  made, the user is allowed to the `uv_handle_t` object.

* The `uv_close_cb` is always made directly off the event loop. That is, it
  is not called from `uv_close()`.



# `uv_tcp_server_t`

A TCP server class that is a subclass of `uv_handle_t`. This can be bound to
an address and begin accepting new TCP sockets.

## `int uv_bind4(uv_tcp_server_t* tcp_server, struct sockaddr_in* address);`
## `int uv_bind6(uv_tcp_server_t* tcp_server, struct sockaddr_in6* address);`

Binds the TCP server to an address. The `address` can be created with
`uv_ip4_addr()`. Call this before `uv_listen()`

Returns zero on success, -1 on failure. Errors in order of least-seriousness:

* `UV_EADDRINUSE` There is already another socket bound to the specified
  address.

* `UV_EADDRNOTAVAIL` The `address` parameter is an IP address that is not 

* `UV_EINVAL` The server is already bound to an address.

* `UV_EFAULT` Memory of `address` parameter is unintelligible.


# `uv_stream_t`

An abstract subclass of `uv_handle_t`. Streams represent something that
reads and/or writes data. Streams can be half or full-duplex. TCP sockets
are streams, files are streams with offsets.

## `int uv_read_start(uv_stream_t* stream, uv_alloc_cb alloc_cb, uv_read_cb read_cb);`

Starts the stream reading continuously. The `alloc_cb` is used to allow the
user to implement various means of supplying the stream with buffers to
fill. The `read_cb` returns buffers to the user filled with data.

Sometimes the buffers returned to the user do not contain data. This does
not indicate EOF as in other systems. EOF is made via the `uv_eof_cb` which
can be set like this

    uv_set_eof_cb(stream, eof_cb);


## `int uv_read_stop(uv_stream_t* stream);`


# `uv_tcp_t`

The TCP handle class abstracts one endpoint of a duplex TCP stream or a
listening socket. `uv_tcp_t` is a subclass of `uv_stream_t`.

TCP stream's fundamental operations are reading and writing data.
Additionally duplex TCP streams can be half-closed. 


# `uv_req_t`

Abstract class represents an asynchronous request. This is a subclass of `uv_handle_t`.


# `uv_connect_req_t`

Subclass of `uv_req_t`. Represents a request for a TCP connection. Operates
on `uv_tcp_t` handles. Like other types of requests the `close_cb` indicates
completion of the request.

## `int uv_connect_req_init(uv_connect_req_t* req, uv_tcp_t* socket, struct sockaddr* addr, uv_close_cb close_cb, void* data);`

Initializes the connection request. Returning 0 indicates success, -1 if
there was an error. The following values can be retrieved from
`uv_last_error` in the case of an error:

* ???
