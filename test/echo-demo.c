#include "../ol.h"
#include "echo.h"

int main(int argc, char** argv) {
  int r;

  ol_init();

  r = echo_start(8000);
  if (r) {
    return r;
  }

  ol_run();

  return 0;
}
