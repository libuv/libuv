
#include <windows.h>


typedef struct {
  HANDLE process;
  HANDLE stdio_in;
  HANDLE stdio_out;
  char *name;
} process_info_t;
