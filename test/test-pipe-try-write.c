#include "uv.h"
#include "task.h"

typedef struct pipe_ctx_s {
  uv_pipe_t handle;
  uv_write_t write_req;
  ssize_t read;
  ssize_t written;
} pipe_ctx_t;

static void (*spam)(uv_pipe_t* handle);
static pipe_ctx_t client;
static pipe_ctx_t peer;
static uv_pipe_t server_handle;

static void write_cb(uv_write_t* req, int status) {
  ASSERT_EQ(0, status);
}


static int do_try_write(uv_pipe_t* handle, uv_buf_t* buf, size_t size) {
  int rc;
  pipe_ctx_t* pc;
  pc = container_of(handle, struct pipe_ctx_s, handle);
  rc = 0;
  do {
    pc->written += rc;
    rc = uv_try_write((uv_stream_t*)handle, buf, size);
  } while (rc > 0);

  return rc;
};


static void handle_read(uv_stream_t* handle,
                        ssize_t nread,
                        const uv_buf_t* buf) {
  pipe_ctx_t* send_ctx;
  pipe_ctx_t* recv_ctx;
  recv_ctx = container_of(handle, struct pipe_ctx_s, handle);
  send_ctx = recv_ctx == &client ? &peer : &client;
  ASSERT_UINT64_GT(nread, 0);
  if (send_ctx->written >= recv_ctx->read + (ssize_t)buf->len) {
    ASSERT_UINT64_EQ(nread, (ssize_t)buf->len);  /* Expect saturation. */
  }
  recv_ctx->read += nread;
}


static void spam_0(uv_pipe_t* handle) {
  uv_buf_t buf;
  pipe_ctx_t* pc;

  buf = uv_buf_init("", 0);
  ASSERT_EQ(0, uv_try_write((uv_stream_t*) handle, &buf, 1));

  /* Non-empty write to start the event loop moving. */
  pc = container_of(handle, struct pipe_ctx_s, handle);
  buf = uv_buf_init("hello, world", sizeof("hello, world") - 1);
  ASSERT_EQ(0,
            uv_write(&pc->write_req,(uv_stream_t*) handle, &buf, 1, write_cb));
}


static void spam_1(uv_pipe_t* handle) {
  uv_buf_t buf;
  int rc;

  buf = uv_buf_init("hello, world", sizeof("hello, world") - 1);
  rc = do_try_write(handle, &buf, 1);

  ASSERT_EQ(rc, UV_EAGAIN);
}


static void spam_2(uv_pipe_t* handle) {
  uv_buf_t bufs[2];
  int rc;

  bufs[0] = uv_buf_init("hello,", sizeof("hello,") - 1);
  bufs[1] = uv_buf_init(" world", sizeof(" world") - 1);
  rc = do_try_write(handle, bufs, ARRAY_SIZE(bufs));

  ASSERT_EQ(rc, UV_EAGAIN);
}


static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  static char base[256];

  buf->base = base;
  buf->len = sizeof(base);
}


static void read_cb(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
  ASSERT_UINT64_GT(nread, 0);  /* Expect some bytes. */
  if (spam != spam_0) {
    handle_read(handle, nread, buf);
  }

  if (handle == (uv_stream_t*) &client.handle) {
    spam(&client.handle);
  } else {
    uv_close((uv_handle_t*) &peer.handle, NULL);
    uv_close((uv_handle_t*) &client.handle, NULL);
    uv_close((uv_handle_t*) &server_handle, NULL);
  }
}


static void connection_cb(uv_stream_t* handle, int status) {
  ASSERT_EQ(0, status);
  ASSERT_EQ(0, uv_pipe_init(uv_default_loop(), &peer.handle, 0));
  ASSERT_EQ(0, uv_accept((uv_stream_t*) &server_handle,
                         (uv_stream_t*) &peer.handle));
  ASSERT_EQ(0, uv_read_start((uv_stream_t*) &peer.handle, alloc_cb, read_cb));
  spam(&peer.handle);
}


static void connect_cb(uv_connect_t* req, int status) {
  ASSERT_EQ(0, status);
  ASSERT_EQ(0, uv_read_start((uv_stream_t*) &client.handle, alloc_cb, read_cb));
}


static int pipe_try_write(void (*spammer)(uv_pipe_t*)) {
  uv_connect_t connect_req;

  spam = spammer;
  ASSERT_EQ(0, uv_pipe_init(uv_default_loop(), &client.handle, 0));
  ASSERT_EQ(0, uv_pipe_init(uv_default_loop(), &server_handle, 0));
  ASSERT_EQ(0, uv_pipe_bind(&server_handle, TEST_PIPENAME));
  ASSERT_EQ(0, uv_listen((uv_stream_t*) &server_handle, 1, connection_cb));
  uv_pipe_connect(&connect_req, &client.handle, TEST_PIPENAME, connect_cb);
  ASSERT_EQ(0, uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
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
