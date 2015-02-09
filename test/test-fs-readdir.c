/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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

#include "uv.h"
#include "task.h"

/* FIXME we shouldn't need to branch in this file */
#if defined(__unix__) || defined(__POSIX__) || \
	defined(__APPLE__) || defined(_AIX)
#include <unistd.h> /* unlink, rmdir, memset */
#else
# include <direct.h>
# include <io.h>
# define unlink _unlink
# define rmdir _rmdir
#endif

#if defined(__sun) || defined(__linux__)
#include <string.h> /* memset */
#endif

#include <fcntl.h>
#include <sys/stat.h>

uv_loop_t* loop;

uv_fs_t opendir_req;
uv_fs_t readdir_req;
uv_fs_t closedir_req;

uv_dirent_t dirents[1];

static int empty_opendir_cb_count;
static int empty_readdir_cb_count;
static int empty_closedir_cb_count;

static void empty_closedir_cb(uv_fs_t* req) {
  ASSERT(req == &closedir_req);
  ++empty_closedir_cb_count;
}

static void empty_readdir_cb(uv_fs_t* req) {
  ASSERT(req == &readdir_req);
  ASSERT(req->fs_type == UV_FS_READDIR);

  // TODO jgilli: by default, uv_fs_readdir doesn't return 0 when reading
  // an emptry dir. Instead, it returns "." and ".." entries in sequence.
  // Should this be changed to mimic uv_fs_scandir's behavior?
  if (req->result != 0) {
    ASSERT(req->result == 1);
    ASSERT(req->ptr == &dirents);

#ifdef HAVE_DIRENT_TYPES
    // In an empty directory, all entries are directories ("." and "..")
    ASSERT(dirents[0].type == UV_DIRENT_DIR);
#else
    ASSERT(dirents[0].type == UV_DIRENT_UNKNOWN);
#endif /* HAVE_DIRENT_TYPES */

    ++empty_readdir_cb_count;

    uv_fs_readdir(uv_default_loop(),
                  req,
                  req->dir,
                  dirents,
                  sizeof(dirents) / sizeof(dirents[0]),
                  empty_readdir_cb);
  } else {
    ASSERT(empty_readdir_cb_count == 2);
    uv_fs_closedir(loop, &closedir_req, req->dir, empty_closedir_cb);

    uv_fs_req_cleanup(req);
  }
}

static void empty_opendir_cb(uv_fs_t* req) {
  ASSERT(req == &opendir_req);
  ASSERT(req->fs_type == UV_FS_OPENDIR);
  ASSERT(req->result == 0);
  ASSERT(req->dir != NULL);
  ASSERT(0 == uv_fs_readdir(uv_default_loop(),
                            &readdir_req,
                            req->dir,
                            dirents,
                            sizeof(dirents) / sizeof(dirents[0]),
                            empty_readdir_cb));
  uv_fs_req_cleanup(req);
  ++empty_opendir_cb_count;
}

/*
 * This test makes sure that both synchronous and asynchronous flavors
 * of the uv_fs_opendir -> uv_fs_readdir -> uv_fs_closedir sequence work
 * as expected when processing an empty directory.
 */
TEST_IMPL(fs_readdir_empty_dir) {
  const char* path;
  uv_fs_t mkdir_req;
  uv_fs_t rmdir_req;
  int r;
  int nb_entries_read;
  int entry_idx;
  size_t entries_count;
  const uv_dir_t* dir;

  path = "./empty_dir/";
  loop = uv_default_loop();

  uv_fs_mkdir(loop, &mkdir_req, path, 0777, NULL);
  uv_fs_req_cleanup(&mkdir_req);

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&opendir_req, 0xdb, sizeof(opendir_req));

  /* Testing the synchronous flavor */
  r = uv_fs_opendir(loop,
                    &opendir_req,
                    path,
                    NULL);
  ASSERT(r == 0);
  ASSERT(opendir_req.fs_type == UV_FS_OPENDIR);
  ASSERT(opendir_req.result == 0);
  ASSERT(opendir_req.ptr != NULL);

  dir = opendir_req.ptr;
  uv_fs_req_cleanup(&opendir_req);

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&readdir_req, 0xdb, sizeof(readdir_req));
  entries_count = 0;
  nb_entries_read = uv_fs_readdir(loop,
                                  &readdir_req,
                                  dir,
                                  dirents,
                                  sizeof(dirents) / sizeof(dirents[0]),
                                  NULL);
  while (0 != nb_entries_read) {
    entry_idx = 0;
    while (entry_idx < nb_entries_read) {
#ifdef HAVE_DIRENT_TYPES
      // In an empty directory, all entries are directories ("." and "..")
      ASSERT(dirents[entry_idx].type == UV_DIRENT_DIR);
#else
      ASSERT(dirents[entry_idx].type == UV_DIRENT_UNKNOWN);
#endif /* HAVE_DIRENT_TYPES */
      ++entry_idx;
      ++entries_count;
    }

    nb_entries_read =  uv_fs_readdir(loop,
                                     &readdir_req,
                                     dir,
                                     dirents,
                                     sizeof(dirents) / sizeof(dirents[0]),
                                     NULL);
  }

  /*
   * TODO jgilli: by default, uv_fs_readdir doesn't return UV_EOF when reading
   * an emptry dir. Instead, it returns "." and ".." entries in sequence.
   * Should this be changed to mimic uv_fs_scandir's behavior?
   */
  ASSERT(entries_count == 2);

  uv_fs_req_cleanup(&readdir_req);

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&closedir_req, 0xdb, sizeof(closedir_req));
  uv_fs_closedir(loop, &closedir_req, dir, NULL);
  ASSERT(closedir_req.result == 0);

  /* Testing the asynchronous flavor */

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&opendir_req, 0xdb, sizeof(opendir_req));
  memset(&readdir_req, 0xdb, sizeof(readdir_req));
  memset(&closedir_req, 0xdb, sizeof(closedir_req));

  r = uv_fs_opendir(loop,
    &opendir_req,
    path,
    empty_opendir_cb);
  ASSERT(r == 0);

  ASSERT(empty_opendir_cb_count == 0);
  ASSERT(empty_closedir_cb_count == 0);

  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT(empty_opendir_cb_count == 1);
  ASSERT(empty_closedir_cb_count == 1);

  uv_fs_rmdir(loop, &rmdir_req, path, NULL);
  uv_fs_req_cleanup(&rmdir_req);

  MAKE_VALGRIND_HAPPY();
  return 0;
}

/*
 * This test makes sure that reading a non-existing directory with
 * uv_fs_{open,read}_dir returns proper error codes.
 */

static int non_existing_opendir_cb_count;

static void non_existing_opendir_cb(uv_fs_t* req) {
  ASSERT(req == &opendir_req);
  ASSERT(req->fs_type == UV_FS_OPENDIR);
  ASSERT(req->result == UV_ENOENT);
  ASSERT(req->ptr == NULL);

  uv_fs_req_cleanup(req);
  ++non_existing_opendir_cb_count;
}

TEST_IMPL(fs_readdir_non_existing_dir) {
  const char* path;
  int r;

  path = "./non-existing-dir/";
  loop = uv_default_loop();

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&opendir_req, 0xdb, sizeof(opendir_req));

  /* Testing the synchronous flavor */
  r = uv_fs_opendir(loop, &opendir_req, path, NULL);

  ASSERT(r == UV_ENOENT);
  ASSERT(opendir_req.fs_type == UV_FS_OPENDIR);
  ASSERT(opendir_req.result == UV_ENOENT);
  ASSERT(opendir_req.ptr == NULL);

  uv_fs_req_cleanup(&opendir_req);

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&opendir_req, 0xdb, sizeof(opendir_req));

  /* Testing the async flavor */
  r = uv_fs_opendir(loop, &opendir_req, path, non_existing_opendir_cb);
  ASSERT(r == 0);

  ASSERT(non_existing_opendir_cb_count == 0);

  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT(non_existing_opendir_cb_count == 1);

  uv_fs_req_cleanup(&opendir_req);

  MAKE_VALGRIND_HAPPY();
  return 0;
}

/*
 * This test makes sure that reading a file as a directory reports correct
 * error codes.
 */

static int file_opendir_cb_count;

static void file_opendir_cb(uv_fs_t* req) {
  ASSERT(req == &opendir_req);
  ASSERT(req->fs_type == UV_FS_OPENDIR);
  ASSERT(req->result == UV_ENOTDIR);
  ASSERT(req->ptr == NULL);

  uv_fs_req_cleanup(req);
  ++file_opendir_cb_count;
}

TEST_IMPL(fs_readdir_file) {
  const char* path;
  int r;

  path = "test/fixtures/empty_file";
  loop = uv_default_loop();

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&opendir_req, 0xdb, sizeof(opendir_req));

  /* Testing the synchronous flavor */
  r = uv_fs_opendir(loop, &opendir_req, path, NULL);

  ASSERT(r == UV_ENOTDIR);
  ASSERT(opendir_req.fs_type == UV_FS_OPENDIR);
  ASSERT(opendir_req.result == UV_ENOTDIR);
  ASSERT(opendir_req.ptr == NULL);

  uv_fs_req_cleanup(&opendir_req);

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&opendir_req, 0xdb, sizeof(opendir_req));

  /* Testing the async flavor */
  r = uv_fs_opendir(loop, &opendir_req, path, file_opendir_cb);
  ASSERT(r == 0);

  ASSERT(file_opendir_cb_count == 0);

  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT(file_opendir_cb_count == 1);

  uv_fs_req_cleanup(&opendir_req);

  MAKE_VALGRIND_HAPPY();
  return 0;
}

/*
 * This test makes sure that reading a non-empty directory with
 * uv_fs_{open,read}_dir returns proper directory entries, including the
 * correct entry types.
 */

static int non_empty_opendir_cb_count;
static int non_empty_readdir_cb_count;
static int non_empty_closedir_cb_count;

static void non_empty_closedir_cb(uv_fs_t* req) {
  ASSERT(req == &closedir_req);
  ASSERT(req->result == 0);

  ++non_empty_closedir_cb_count;
}

static void non_empty_readdir_cb(uv_fs_t* req) {
  ASSERT(req == &readdir_req);
  ASSERT(req->fs_type == UV_FS_READDIR);

  // TODO jgilli: by default, uv_fs_readdir doesn't return UV_EOF when reading
  // an emptry dir. Instead, it returns "." and ".." entries in sequence.
  // Should this be fixed to mimic uv_fs_scandir's behavior?
  if (req->result == 0) {
    ASSERT(non_empty_readdir_cb_count == 5);
    uv_fs_closedir(loop, &closedir_req, req->dir, non_empty_closedir_cb);
  } else {
    ASSERT(req->result == 1);
    ASSERT(req->ptr == dirents);

#ifdef HAVE_DIRENT_TYPES
    if (!strcmp(dirents[0].name, "test_subdir") ||
        !strcmp(dirents[0].name, ".") ||
        !strcmp(dirents[0].name, "..")) {
      ASSERT(dirents[0].type == UV_DIRENT_DIR);
    } else {
      ASSERT(dirents[0].type == UV_DIRENT_FILE);
    }
#else
    ASSERT(dirents[0].type == UV_DIRENT_UNKNOWN);
#endif /* HAVE_DIRENT_TYPES */

    ++non_empty_readdir_cb_count;

    uv_fs_readdir(uv_default_loop(),
                  &readdir_req,
                  req->dir,
                  dirents,
                  sizeof(dirents) / sizeof(dirents[0]),
                  non_empty_readdir_cb);
  }

  uv_fs_req_cleanup(req);
}

static void non_empty_opendir_cb(uv_fs_t* req) {
  ASSERT(req == &opendir_req);
  ASSERT(req->fs_type == UV_FS_OPENDIR);
  ASSERT(req->result == 0);
  ASSERT(req->ptr == req->dir);
  ASSERT(req->dir != NULL);
  ASSERT(0 == uv_fs_readdir(uv_default_loop(),
                            &readdir_req,
                            req->ptr,
                            dirents,
                            sizeof(dirents) / sizeof(dirents[0]),
                            non_empty_readdir_cb));
  uv_fs_req_cleanup(req);
  ++non_empty_opendir_cb_count;
}

TEST_IMPL(fs_readdir_non_empty_dir) {
  int r;
  size_t entries_count;

  uv_fs_t mkdir_req;
  uv_fs_t rmdir_req;
  uv_fs_t create_req;
  uv_fs_t close_req;

  const uv_dir_t* dir;

  loop = uv_default_loop();

  /* Setup */
  unlink("test_dir/file1");
  unlink("test_dir/file2");
  rmdir("test_dir/test_subdir");
  rmdir("test_dir");

  r = uv_fs_mkdir(loop, &mkdir_req, "test_dir", 0755, NULL);
  ASSERT(r == 0);

  /* Create 2 files synchronously. */
  r = uv_fs_open(loop, &create_req, "test_dir/file1", O_WRONLY | O_CREAT,
      S_IWUSR | S_IRUSR, NULL);
  ASSERT(r >= 0);
  uv_fs_req_cleanup(&create_req);
  r = uv_fs_close(loop, &close_req, create_req.result, NULL);
  ASSERT(r == 0);
  uv_fs_req_cleanup(&close_req);

  r = uv_fs_open(loop, &create_req, "test_dir/file2", O_WRONLY | O_CREAT,
      S_IWUSR | S_IRUSR, NULL);
  ASSERT(r >= 0);
  uv_fs_req_cleanup(&create_req);
  r = uv_fs_close(loop, &close_req, create_req.result, NULL);
  ASSERT(r == 0);
  uv_fs_req_cleanup(&close_req);

    r = uv_fs_mkdir(loop, &mkdir_req, "test_dir/test_subdir", 0755, NULL);
  ASSERT(r == 0);

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&opendir_req, 0xdb, sizeof(opendir_req));

  /* Testing the synchronous flavor */
  r = uv_fs_opendir(loop, &opendir_req, "test_dir", NULL);

  ASSERT(r == 0);
  ASSERT(opendir_req.fs_type == UV_FS_OPENDIR);
  ASSERT(opendir_req.result == 0);
  ASSERT(opendir_req.ptr != NULL);
  // TODO jgilli: by default, uv_fs_readdir doesn't return UV_EOF when reading
  // an emptry dir. Instead, it returns "." and ".." entries in sequence.
  // Should this be changed to mimic uv_fs_scandir's behavior?
  entries_count = 0;
  dir = opendir_req.ptr;
  while (uv_fs_readdir(loop,
                       &readdir_req,
                       dir,
                       dirents,
                       sizeof(dirents) / sizeof(dirents[0]),
                       NULL) != 0) {
#ifdef HAVE_DIRENT_TYPES
    if (!strcmp(dirents[0].name, "test_subdir") ||
        !strcmp(dirents[0].name, ".") ||
        !strcmp(dirents[0].name, "..")) {
      ASSERT(dirents[0].type == UV_DIRENT_DIR);
    } else {
      ASSERT(dirents[0].type == UV_DIRENT_FILE);
    }
#else
    ASSERT(dirents[0].type == UV_DIRENT_UNKNOWN);
#endif /* HAVE_DIRENT_TYPES */
    ++entries_count;
  }

  ASSERT(entries_count == 5);
  uv_fs_req_cleanup(&readdir_req);

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&closedir_req, 0xdb, sizeof(closedir_req));
  uv_fs_closedir(loop, &closedir_req, dir, NULL);
  ASSERT(closedir_req.result == 0);

  /* Testing the asynchronous flavor */

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&opendir_req, 0xdb, sizeof(opendir_req));

  r = uv_fs_opendir(loop, &opendir_req, "test_dir", non_empty_opendir_cb);
  ASSERT(r == 0);

  ASSERT(non_empty_opendir_cb_count == 0);
  ASSERT(non_empty_closedir_cb_count == 0);

  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT(non_empty_opendir_cb_count == 1);
  ASSERT(non_empty_closedir_cb_count == 1);

  uv_fs_rmdir(loop, &rmdir_req, "test_subdir", NULL);
  uv_fs_req_cleanup(&rmdir_req);

  /* Cleanup */
  unlink("test_dir/file1");
  unlink("test_dir/file2");
  rmdir("test_dir/test_subdir");
  rmdir("test_dir");

  MAKE_VALGRIND_HAPPY();
  return 0;
 }
