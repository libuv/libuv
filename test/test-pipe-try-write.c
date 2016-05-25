#include "uv.h"
#include "task.h"

static void (*spam)(uv_pipe_t* handle);
static uv_pipe_t client_handle;
static uv_pipe_t peer_handle;
static uv_pipe_t server_handle;
static uv_write_t write_req;


static void write_cb(uv_write_t* req, int status) {
  ASSERT(0 == status);
}


static void spam_0(uv_pipe_t* handle) {
  uv_buf_t buf;

  buf = uv_buf_init("", 0);
  ASSERT(0 == uv_try_write((uv_stream_t*) handle, &buf, 1));

  /* Non-empty write to start the event loop moving. */
  buf = uv_buf_init("hello, world", sizeof("hello, world") - 1);
  ASSERT(0 == uv_write(&write_req, (uv_stream_t*) handle, &buf, 1, write_cb));
}


static void spam_1(uv_pipe_t* handle) {
  uv_buf_t buf;
  int rc;

  buf = uv_buf_init("hello, world", sizeof("hello, world") - 1);
  do
    rc = uv_try_write((uv_stream_t*) handle, &buf, 1);
  while (rc > 0);

  ASSERT(rc == UV_EAGAIN);
}


static void spam_2(uv_pipe_t* handle) {
  uv_buf_t bufs[2];
  int rc;

  bufs[0] = uv_buf_init("hello,", sizeof("hello,") - 1);
  bufs[1] = uv_buf_init(" world", sizeof(" world") - 1);

  do
    rc = uv_try_write((uv_stream_t*) handle, bufs, ARRAY_SIZE(bufs));
  while (rc > 0);

  ASSERT(rc == UV_EAGAIN);
}


static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  static char base[256];

  buf->base = base;
  buf->len = sizeof(base);
}


static void read_cb(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
  if (spam == spam_0) {
    ASSERT(nread > 0);  /* Expect some bytes. */
  } else {
    ASSERT(nread == (ssize_t) buf->len);  /* Expect saturation. */
  }

  if (handle == (uv_stream_t*) &peer_handle) {
    spam(&client_handle);
  } else {
    uv_close((uv_handle_t*) &peer_handle, NULL);
    uv_close((uv_handle_t*) &client_handle, NULL);
    uv_close((uv_handle_t*) &server_handle, NULL);
  }
}


static void connection_cb(uv_stream_t* handle, int status) {
  ASSERT(0 == status);
  ASSERT(0 == uv_pipe_init(uv_default_loop(), &peer_handle, 0));
  ASSERT(0 == uv_accept((uv_stream_t*) &server_handle,
                        (uv_stream_t*) &peer_handle));
  ASSERT(0 == uv_read_start((uv_stream_t*) &peer_handle, alloc_cb, read_cb));
  spam(&peer_handle);
}


static void connect_cb(uv_connect_t* req, int status) {
  ASSERT(0 == status);
  ASSERT(0 == uv_read_start((uv_stream_t*) &client_handle, alloc_cb, read_cb));
}


static int pipe_try_write(void (*spammer)(uv_pipe_t*)) {
  uv_connect_t connect_req;

  spam = spammer;
  ASSERT(0 == uv_pipe_init(uv_default_loop(), &client_handle, 0));
  ASSERT(0 == uv_pipe_init(uv_default_loop(), &server_handle, 0));
  ASSERT(0 == uv_pipe_bind(&server_handle, TEST_PIPENAME));
  ASSERT(0 == uv_listen((uv_stream_t*) &server_handle, 1, connection_cb));
  uv_pipe_connect(&connect_req, &client_handle, TEST_PIPENAME, connect_cb);
  ASSERT(0 == uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY();
  return 0;
}


TEST_IMPL(pipe_try_write_0) {
  return pipe_try_write(spam_0);
}


TEST_IMPL(pipe_try_write_1) {
  return pipe_try_write(spam_1);
}


TEST_IMPL(pipe_try_write_2) {
  return pipe_try_write(spam_2);
}
