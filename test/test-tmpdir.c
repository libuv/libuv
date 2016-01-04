#include "uv.h"
#include "task.h"
#include <string.h>

#define PATHMAX 1024
#define SMALLPATH 1

TEST_IMPL(tmpdir) {
  char tmpdir[PATHMAX];
  size_t len;
  char last;
  int r;

  /* Test the normal case */
  len = sizeof tmpdir;
  tmpdir[0] = '\0';

  ASSERT(strlen(tmpdir) == 0);
  r = uv_os_tmpdir(tmpdir, &len);
  ASSERT(r == 0);
  ASSERT(strlen(tmpdir) == len);
  ASSERT(len > 0);
  ASSERT(tmpdir[len] == '\0');

  if (len > 1) {
    last = tmpdir[len - 1];
#ifdef _WIN32
    ASSERT(last != '\\');
#else
    ASSERT(last != '/');
#endif
  }

  /* Test the case where the buffer is too small */
  len = SMALLPATH;
  r = uv_os_tmpdir(tmpdir, &len);
  ASSERT(r == UV_ENOBUFS);
  ASSERT(len > SMALLPATH);

  /* Test invalid inputs */
  r = uv_os_tmpdir(NULL, &len);
  ASSERT(r == UV_EINVAL);
  r = uv_os_tmpdir(tmpdir, NULL);
  ASSERT(r == UV_EINVAL);
  len = 0;
  r = uv_os_tmpdir(tmpdir, &len);
  ASSERT(r == UV_EINVAL);

  return 0;
}
