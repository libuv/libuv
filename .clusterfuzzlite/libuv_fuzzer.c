/* Copyright libuv contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
 
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/idna.c"
#include "uv.h"

static uv_loop_t* loop;

static void dummy_cb(uv_fs_t* req) { (void)req; }

void test_idna(const uint8_t* data, size_t size) {
  char* new_str = malloc(size + 1);
  if (new_str == NULL)
    return;

  memcpy(new_str, data, size);
  new_str[size] = '\0';

  char de[256];
  uv__idna_toascii(new_str, new_str + size, de, de + 256);

  uv_wtf8_length_as_utf16(new_str);

  free(new_str);
}

void test_file_ops_1(const uint8_t* data, size_t size) {
  char read_buf[256];
  uv_fs_t open_req1;
  uv_fs_t write_req;
  uv_buf_t iov;
  uv_fs_t close_req;
  uv_fs_t read_req;

  unlink("test_file");
  uv_fs_open(NULL, &open_req1, "test_file", UV_FS_O_WRONLY | UV_FS_O_CREAT,
                 S_IWUSR | S_IRUSR, NULL);
  uv_fs_req_cleanup(&open_req1);

  iov = uv_buf_init((char*)data, size);
  uv_fs_write(NULL, &write_req, open_req1.result, &iov, 1, -1, NULL);
  uv_fs_req_cleanup(&write_req);

  /* Close after writing */
  uv_fs_close(NULL, &close_req, open_req1.result, NULL);
  uv_fs_req_cleanup(&close_req);

  /* Open again to read */
  uv_fs_open(NULL, &open_req1, "test_file", UV_FS_O_WRONLY | UV_FS_O_CREAT,
                 S_IWUSR | S_IRUSR, NULL);
  uv_fs_req_cleanup(&open_req1);

  iov = uv_buf_init(read_buf, sizeof(read_buf));
  uv_fs_read(NULL, &read_req, open_req1.result, &iov, 1, -1, NULL);
  uv_fs_req_cleanup(&read_req);

  uv_fs_close(NULL, &close_req, open_req1.result, NULL);
  uv_fs_req_cleanup(&close_req);
  unlink("test_file");
}

void test_file_ops_2(const uint8_t* data, size_t size) {
  uv_buf_t iov;
  uv_fs_t open_req1;
  uv_fs_t write_req;
  uv_fs_t copy_req;
  uv_fs_t close_req;

  uv_fs_open(NULL, &open_req1, "test_file", UV_FS_O_WRONLY | UV_FS_O_CREAT,
                 S_IWUSR | S_IRUSR, NULL);
  uv_fs_req_cleanup(&open_req1);

  iov = uv_buf_init(data, size);
  uv_fs_write(NULL, &write_req, open_req1.result, &iov, 1, -1, NULL);
  uv_fs_req_cleanup(&write_req);

  uv_fs_copyfile(NULL, &copy_req, "test_file", "test_file2", 0, NULL);
  uv_fs_req_cleanup(&copy_req);

  uv_fs_close(NULL, &close_req, open_req1.result, NULL);
  uv_fs_req_cleanup(&close_req);

  uv_fs_req_cleanup(&copy_req);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size == 0)
    return 0;

  uint8_t decider = data[0] % 7;
  data++;
  size--;

  /* Allocate a null-terminated string that can be used in various fuzz
   * operations.
   */
  char* new_str = malloc(size + 1);
  if (new_str == NULL)
    return 0;

  memcpy(new_str, data, size);
  new_str[size] = '\0';

  /* Perform a single fuzz operation and use the fuzz data to decide
   * which it should be.
   */
  if (decider == 0) {
    uv_fs_t req;
    loop = uv_default_loop();
    uv_fs_realpath(loop, &req, new_str, dummy_cb);
    uv_run(loop, UV_RUN_DEFAULT);
    uv_fs_req_cleanup(&req);
  } else if (decider == 1) {
    struct sockaddr_in addr;
    uv_ip4_addr(new_str, 9123, &addr);
  } else if (decider == 2) {
    struct sockaddr_in6 addr;
    uv_ip6_addr(new_str, 9123, &addr);
  } else if (decider == 3) {
    test_file_ops_1(data, size);
  } else if (decider == 4) {
    test_file_ops_2(data, size);
  } else if (decider == 5) {
    test_idna(data, size);
  } else {
    uv_fs_t req;
    loop = uv_default_loop();
    uv_fs_open(NULL, &req, new_str, UV_FS_O_RDONLY, 0, NULL);
    uv_run(loop, UV_RUN_DEFAULT);
    uv_fs_req_cleanup(&req);
  }

  free(new_str);
  return 0;
}
