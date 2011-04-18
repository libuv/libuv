
/* Don't complain about _snprintf being unsecure. */
#define _CRT_SECURE_NO_WARNINGS

/* Dont complain about write(), fileno() etc. being deprecated. */
#pragma warning(disable : 4996)


#include <windows.h>
#include <stdio.h>


/* Windows has no snprintf, only _snprintf. */
#define snprintf _snprintf


typedef struct {
  HANDLE process;
  HANDLE stdio_in;
  HANDLE stdio_out;
  char *name;
} process_info_t;