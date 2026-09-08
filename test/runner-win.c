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

#include <fcntl.h>
#include <io.h>
#include <malloc.h>
#include <stdio.h>
#include <process.h>
#if !defined(__MINGW32__)
# include <crtdbg.h>
#endif


#include "task.h"
#include "runner.h"


/*
 * Define the stuff that MinGW doesn't have
 */
#ifndef GetFileSizeEx
  WINBASEAPI BOOL WINAPI GetFileSizeEx(HANDLE hFile,
                                       PLARGE_INTEGER lpFileSize);
#endif


/* Bits of the native API needed to enumerate the handles of this process.
 * Declared here with private names rather than pulling in <winternl.h>, which
 * disagrees with itself between MinGW and the Windows SDK.
 */
#define UV__STATUS_INFO_LENGTH_MISMATCH ((LONG) 0xC0000004L)
#define UV__PROCESS_HANDLE_INFORMATION 51 /* ProcessHandleInformation */
#define UV__OBJECT_TYPE_INFORMATION 2     /* ObjectTypeInformation */

typedef struct {
  HANDLE HandleValue;
  ULONG_PTR HandleCount;
  ULONG_PTR PointerCount;
  ACCESS_MASK GrantedAccess;
  ULONG ObjectTypeIndex;
  ULONG HandleAttributes;
  ULONG Reserved;
} uv__process_handle_entry_t;

typedef struct {
  ULONG_PTR NumberOfHandles;
  ULONG_PTR Reserved;
  uv__process_handle_entry_t Handles[1];
} uv__process_handle_snapshot_t;

typedef struct {
  USHORT Length;
  USHORT MaximumLength;
  WCHAR* Buffer;
} uv__unicode_string_t;

typedef struct {
  uv__unicode_string_t TypeName;
  ULONG Reserved[22];
} uv__object_type_information_t;

typedef LONG (WINAPI* uv__nt_query_information_process_t)(HANDLE process,
                                                          ULONG info_class,
                                                          void* info,
                                                          ULONG len,
                                                          ULONG* result_len);

typedef LONG (WINAPI* uv__nt_query_object_t)(HANDLE object,
                                             ULONG info_class,
                                             void* info,
                                             ULONG len,
                                             ULONG* result_len);

static uv__nt_query_information_process_t pNtQueryInformationProcess;
static uv__nt_query_object_t pNtQueryObject;


/* One entry per handle open in this process. The type index is carried along so
 * that a report can name what leaked, and so that a handle value that was
 * closed and reused for a different kind of object is not mistaken for the
 * original.
 */
typedef struct {
  HANDLE value;
  ULONG type_index;
} handle_entry_t;

/* Handles open when the test process started, sorted by handle value. Handles
 * are not small dense integers like descriptors are, so instead of indexing an
 * array by handle value the way runner-unix.c does, the snapshot is a sorted
 * vector that check_open_fds() diffs with a linear merge.
 */
static handle_entry_t* handle_open_at_startup;
static size_t nhandle_open_at_startup;
static HANDLE handle_not_tracked = INVALID_HANDLE_VALUE;
static int handle_check_disabled;


void disable_open_fds_check(void) {
  handle_check_disabled = 1;
}


static int cmp_handle_entry(const void* a, const void* b) {
  uintptr_t x = (uintptr_t) ((const handle_entry_t*) a)->value;
  uintptr_t y = (uintptr_t) ((const handle_entry_t*) b)->value;

  return (x > y) - (x < y);
}


/* Snapshot the handle table of this process into a vector sorted by handle
 * value. Returns NULL if the query is unavailable, in which case the caller
 * gives up on checking; there is no second way to ask.
 */
static handle_entry_t* snapshot_handles(size_t* count) {
  uv__process_handle_snapshot_t* info;
  handle_entry_t* entries;
  ULONG len;
  ULONG needed;
  LONG status;
  size_t n;
  size_t i;

  if (pNtQueryInformationProcess == NULL)
    return NULL;

  len = 16384;
  for (;;) {
    info = malloc(len);
    ASSERT_NOT_NULL(info);
    needed = 0;
    status = pNtQueryInformationProcess(GetCurrentProcess(),
                                        UV__PROCESS_HANDLE_INFORMATION,
                                        info,
                                        len,
                                        &needed);
    if (status != UV__STATUS_INFO_LENGTH_MISMATCH)
      break;
    free(info);
    /* The table can grow between the two calls, so ask for slack. */
    len = (needed > len ? needed : len) + 16384;
  }

  if (status < 0) { /* !NT_SUCCESS(status) */
    fprintf(stderr,
            "NtQueryInformationProcess(ProcessHandleInformation): 0x%08lx\n",
            (unsigned long) status);
    fflush(stderr);
    free(info);
    return NULL;
  }

  n = info->NumberOfHandles;
  entries = malloc((n + 1) * sizeof(*entries));
  ASSERT_NOT_NULL(entries);

  for (i = 0; i < n; i++) {
    entries[i].value = info->Handles[i].HandleValue;
    entries[i].type_index = info->Handles[i].ObjectTypeIndex;
  }

  free(info);
  qsort(entries, n, sizeof(*entries), cmp_handle_entry);
  *count = n;

  return entries;
}


/* Name of the object type a handle refers to, e.g. "File" or "IoCompletion".
 * Falls back to the numeric type index. Querying the type is safe on any
 * handle, unlike querying the object *name*, which can block forever on a
 * synchronous file or pipe handle that has I/O pending.
 */
static void handle_type_name(const handle_entry_t* e,
                             char* buf,
                             size_t size) {
  union {
    uv__object_type_information_t info;
    char space[1024];
  } u;
  ULONG len;
  int n;

  if (pNtQueryObject != NULL) {
    memset(&u, 0, sizeof(u));
    if (pNtQueryObject(e->value,
                       UV__OBJECT_TYPE_INFORMATION,
                       &u,
                       sizeof(u),
                       &len) >= 0 &&
        u.info.TypeName.Buffer != NULL) {
      n = WideCharToMultiByte(CP_UTF8,
                              0,
                              u.info.TypeName.Buffer,
                              (int) (u.info.TypeName.Length / sizeof(WCHAR)),
                              buf,
                              (int) size - 1,
                              NULL,
                              NULL);
      if (n > 0) {
        buf[n] = '\0';
        return;
      }
    }
  }

  snprintf(buf, size, "type %lu", (unsigned long) e->type_index);
}


/* Best-effort description of what a handle refers to, to make the leak report
 * actionable. Not every object type can answer, hence "?".
 */
static void print_handle(const handle_entry_t* e) {
  WCHAR path[MAX_PATH];
  char type[128];
  char utf8[MAX_PATH * 3];
  DWORD n;

  handle_type_name(e, type, sizeof(type));

  if (0 == strcmp(type, "File") && GetFileType(e->value) == FILE_TYPE_DISK) {
    n = GetFinalPathNameByHandleW(e->value,
                                  path,
                                  (DWORD) ARRAY_SIZE(path),
                                  VOLUME_NAME_DOS);
    if (n > 0 && n < ARRAY_SIZE(path) &&
        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, (int) sizeof(utf8),
                            NULL, NULL) > 0) {
      fprintf(stderr, "handle %p (%s) -> %s\n", e->value, type, utf8);
      return;
    }
  }

  fprintf(stderr, "handle %p (%s) -> ?\n", e->value, type);
}


static void record_open_handles(void) {
  HMODULE ntdll;
  const char* arg;

  ntdll = GetModuleHandleA("ntdll.dll");
  ASSERT_NOT_NULL(ntdll);
  pNtQueryInformationProcess = (uv__nt_query_information_process_t)
      GetProcAddress(ntdll, "NtQueryInformationProcess");
  pNtQueryObject = (uv__nt_query_object_t)
      GetProcAddress(ntdll, "NtQueryObject");

  /* Helpers close this handle in notify_parent_process() to signal the runner
   * that they are ready, so it is expected to go away mid-test.
   */
  arg = getenv("UV_TEST_RUNNER_FD");
  if (arg != NULL)
    handle_not_tracked = (HANDLE) (uintptr_t) strtoull(arg, NULL, 10);

  handle_open_at_startup = snapshot_handles(&nhandle_open_at_startup);
  if (handle_open_at_startup == NULL) {
    fprintf(stderr, "unable to snapshot handles, skipping leak check\n");
    fflush(stderr);
    handle_check_disabled = 1;
  }
}


void check_open_fds(void) {
  const handle_entry_t* was;
  const handle_entry_t* is;
  handle_entry_t* now;
  size_t nnow;
  size_t i;
  size_t j;
  int leaked;
  int closed;

  if (handle_check_disabled)
    return;

  now = snapshot_handles(&nnow);
  if (now == NULL)
    return;

  leaked = 0;
  closed = 0;
  i = 0;
  j = 0;

  while (i < nhandle_open_at_startup || j < nnow) {
    was = i < nhandle_open_at_startup ? &handle_open_at_startup[i] : NULL;
    is = j < nnow ? &now[j] : NULL;

    if (was != NULL && is != NULL && was->value == is->value) {
      /* Same slot. A different object type means the handle that was open at
       * startup was closed and the slot handed out again. */
      if (was->type_index != is->type_index) {
        fprintf(stderr, "replaced ");
        print_handle(is);
        leaked++;
        closed++;
      }
      i++;
      j++;
    } else if (is == NULL ||
               (was != NULL && cmp_handle_entry(was, is) < 0)) {
      if (was->value != handle_not_tracked) {
        fprintf(stderr, "closed handle %p that was open at startup\n",
                was->value);
        closed++;
      }
      i++;
    } else {
      if (is->value != handle_not_tracked) {
        fprintf(stderr, "leaked ");
        print_handle(is);
        leaked++;
      }
      j++;
    }
  }

  free(now);
  fflush(stderr);
  ASSERT_OK(leaked);
  ASSERT_OK(closed);
}


/* Do platform-specific initialization. */
void platform_init(int argc, char **argv) {
  /* Disable the "application crashed" popup. */
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
      SEM_NOOPENFILEERRORBOX);
#if !defined(__MINGW32__)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
#endif

  _setmode(0, _O_BINARY);
  _setmode(1, _O_BINARY);
  _setmode(2, _O_BINARY);

#ifdef _MSC_VER
  _set_fmode(_O_BINARY);
#else
  _fmode = _O_BINARY;
#endif

  /* Disable stdio output buffering. */
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  strcpy(executable_path, argv[0]);
  record_open_handles();
}


void notify_parent_process(void) {
  HANDLE handle;
  char* arg;

  arg = getenv("UV_TEST_RUNNER_FD");
  if (arg == NULL)
    return;

  handle = (HANDLE)(uintptr_t)strtoull(arg, NULL, 10);
  SetEnvironmentVariableA("UV_TEST_RUNNER_FD", NULL);
  ASSERT_NE(CloseHandle(handle), 0);
}


int process_start(char* name, char* part, process_info_t* p, int is_helper) {
  HANDLE file = INVALID_HANDLE_VALUE;
  HANDLE nul = INVALID_HANDLE_VALUE;
  WCHAR path[MAX_PATH], filename[MAX_PATH];
  WCHAR image[MAX_PATH + 1];
  WCHAR args[MAX_PATH * 2];
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  DWORD result;
  HANDLE fds[2];
  char fdstr[32];

  fds[0] = fds[1] = INVALID_HANDLE_VALUE;

  if (is_helper) {
    /* Create a pipe so the helper can signal when it is ready. */
    if (!CreatePipe(&fds[0], &fds[1], NULL, 0))
      goto error;
    if (!SetHandleInformation(fds[0], HANDLE_FLAG_INHERIT, 0))
      goto error;
    if (!SetHandleInformation(fds[1], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
      goto error;
    snprintf(fdstr, sizeof(fdstr), "%" PRIuPTR, (uintptr_t) fds[1]);
    if (!SetEnvironmentVariableA("UV_TEST_RUNNER_FD", fdstr))
      goto error;
  }

  if (GetTempPathW(sizeof(path) / sizeof(WCHAR), (WCHAR*)&path) == 0)
    goto error;
  if (GetTempFileNameW((WCHAR*)&path, L"uv", 0, (WCHAR*)&filename) == 0)
    goto error;

  file = CreateFileW((WCHAR*)filename,
                     GENERIC_READ | GENERIC_WRITE,
                     0,
                     NULL,
                     CREATE_ALWAYS,
                     FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                     NULL);
  if (file == INVALID_HANDLE_VALUE)
    goto error;

  if (!SetHandleInformation(file, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
    goto error;

  nul = CreateFileA("nul",
                    GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL);
  if (nul == INVALID_HANDLE_VALUE)
    goto error;

  if (!SetHandleInformation(nul, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
    goto error;

  result = GetModuleFileNameW(NULL,
                              (WCHAR*) &image,
                              sizeof(image) / sizeof(WCHAR));
  if (result == 0 || result == sizeof(image))
    goto error;

  if (part) {
    if (_snwprintf((WCHAR*)args,
                   sizeof(args) / sizeof(WCHAR),
                   L"\"%s\" %S %S",
                   image,
                   name,
                   part) < 0) {
      goto error;
    }
  } else {
    if (_snwprintf((WCHAR*)args,
                   sizeof(args) / sizeof(WCHAR),
                   L"\"%s\" %S",
                   image,
                   name) < 0) {
      goto error;
    }
  }

  memset((void*)&si, 0, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = nul;
  si.hStdOutput = file;
  si.hStdError = file;

  if (!CreateProcessW(image, args, NULL, NULL, TRUE,
                      0, NULL, NULL, &si, &pi))
    goto error;

  CloseHandle(pi.hThread);

  SetHandleInformation(nul, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(file, HANDLE_FLAG_INHERIT, 0);

  p->stdio_in = nul;
  p->stdio_out = file;
  p->process = pi.hProcess;
  p->name = part;

  if (!is_helper)
    return 0;

  /* Close the write end in the parent and wait for the child to close its
   * copy, which signals that the helper has finished starting up. */
  ASSERT_NE(CloseHandle(fds[1]), 0);
  fds[1] = INVALID_HANDLE_VALUE;
  SetEnvironmentVariableA("UV_TEST_RUNNER_FD", NULL);

  {
    char buf[1];
    DWORD bytes;
    if (ReadFile(fds[0], buf, sizeof(buf), &bytes, NULL)) {
      if (bytes > 0) {
        fprintf(stderr, "EOF expected but got data.\n");
        CloseHandle(fds[0]);
        return -1;
      }
    } else if (GetLastError() != ERROR_BROKEN_PIPE) {
      fprintf(stderr, "ReadFile: %lu\n", GetLastError());
      CloseHandle(fds[0]);
      return -1;
    }
  }

  ASSERT_NE(CloseHandle(fds[0]), 0);
  return 0;

error:
  if (fds[0] != INVALID_HANDLE_VALUE)
    CloseHandle(fds[0]);
  if (fds[1] != INVALID_HANDLE_VALUE) {
    CloseHandle(fds[1]);
    SetEnvironmentVariableA("UV_TEST_RUNNER_FD", NULL);
  }
  if (file != INVALID_HANDLE_VALUE)
    CloseHandle(file);
  if (nul != INVALID_HANDLE_VALUE)
    CloseHandle(nul);

  return -1;
}


/* Timeout is in msecs. Set timeout < 0 to never time out. Returns 0 when all
 * processes are terminated, -2 on timeout. */
int process_wait(process_info_t *vec, int n, int timeout) {
  int i;
  HANDLE handles[MAXIMUM_WAIT_OBJECTS];
  DWORD timeout_api, result;

  /* If there's nothing to wait for, return immediately. */
  if (n == 0)
    return 0;

  ASSERT_LE(n, MAXIMUM_WAIT_OBJECTS);

  for (i = 0; i < n; i++)
    handles[i] = vec[i].process;

  if (timeout >= 0) {
    timeout_api = (DWORD)timeout;
  } else {
    timeout_api = INFINITE;
  }

  result = WaitForMultipleObjects(n, handles, TRUE, timeout_api);

  if (result < WAIT_OBJECT_0 + n) {
    /* All processes are terminated. */
    return 0;
  }
  if (result == WAIT_TIMEOUT) {
    return -2;
  }
  return -1;
}


long int process_output_size(process_info_t *p) {
  LARGE_INTEGER size;
  if (!GetFileSizeEx(p->stdio_out, &size))
    return -1;
  return (long int)size.QuadPart;
}


int process_copy_output(process_info_t* p, FILE* stream) {
  char buf[1024];
  int partial;
  int fd, r;

  fd = _open_osfhandle((intptr_t)p->stdio_out, _O_RDONLY | _O_TEXT);
  if (fd == -1)
    return -1;

  r = _lseek(fd, 0, SEEK_SET);
  if (r < 0)
    return -1;

  partial = 0;
  while ((r = _read(fd, buf, sizeof(buf))) != 0)
    partial = print_lines(buf, r, stream, partial);

  _close(fd);
  return 0;
}


int process_read_last_line(process_info_t *p,
                           char * buffer,
                           size_t buffer_len) {
  DWORD size;
  DWORD read;
  DWORD start;
  OVERLAPPED overlapped;

  ASSERT_GT(buffer_len, 0);

  size = GetFileSize(p->stdio_out, NULL);
  if (size == INVALID_FILE_SIZE)
    return -1;

  if (size == 0) {
    buffer[0] = '\0';
    return 1;
  }

  memset(&overlapped, 0, sizeof overlapped);
  if (size >= buffer_len)
    overlapped.Offset = size - buffer_len - 1;

  if (!ReadFile(p->stdio_out, buffer, buffer_len - 1, &read, &overlapped))
    return -1;

  start = read;
  while (start-- > 0) {
    if (buffer[start] == '\n' || buffer[start] == '\r')
      break;
  }

  if (start > 0)
    memmove(buffer, buffer + start, read - start);

  buffer[read - start] = '\0';

  return 0;
}


char* process_get_name(process_info_t *p) {
  return p->name;
}


int process_terminate(process_info_t *p) {
  if (!TerminateProcess(p->process, 1))
    return -1;
  return 0;
}


int process_reap(process_info_t *p) {
  DWORD exitCode;
  if (!GetExitCodeProcess(p->process, &exitCode))
    return -1;
  return (int)exitCode;
}


void process_cleanup(process_info_t *p) {
  CloseHandle(p->process);
  CloseHandle(p->stdio_in);
}


static int clear_line(void) {
  HANDLE handle;
  CONSOLE_SCREEN_BUFFER_INFO info;
  COORD coord;
  DWORD written;

  handle = (HANDLE)_get_osfhandle(_fileno(stderr));
  if (handle == INVALID_HANDLE_VALUE)
    return -1;

  if (!GetConsoleScreenBufferInfo(handle, &info))
    return -1;

  coord = info.dwCursorPosition;
  if (coord.Y <= 0)
    return -1;

  coord.X = 0;

  if (!SetConsoleCursorPosition(handle, coord))
    return -1;

  if (!FillConsoleOutputCharacterW(handle,
                                   0x20,
                                   info.dwSize.X,
                                   coord,
                                   &written)) {
    return -1;
  }

  return 0;
}


void rewind_cursor(void) {
  if (clear_line() == -1) {
    /* If clear_line fails (stdout is not a console), print a newline. */
    fprintf(stderr, "\n");
  }
}
