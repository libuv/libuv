#include "../ol.h"
#include "echo.h"

int main(int argc, char** argv) {
  ol_init();

  int r = echo_start(8000);
  if (r) {
    return r;
  }

  ol_run();

  return 0;
}
