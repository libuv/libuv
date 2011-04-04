
#include <assert.h>
#include <io.h>
#include <stdio.h>
#include <windows.h>

#include "test-runner.h"


int process_start(char *name, process_info_t *p) {
  HANDLE file = INVALID_HANDLE_VALUE;
  HANDLE nul = INVALID_HANDLE_VALUE;
  WCHAR path[MAX_PATH], filename[MAX_PATH];
  WCHAR image[MAX_PATH + 1];
  WCHAR args[MAX_PATH * 2];
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  DWORD result;

  if (GetTempPathW(sizeof(path), (WCHAR*)&path) == 0)
    goto error;
  if (GetTempFileNameW((WCHAR*)&path, L"ol_", 0, (WCHAR*)&filename) == 0)
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

  result = GetModuleFileName(NULL, (WCHAR*)&image, sizeof(image));
  if (result == 0 || result == sizeof(image))
    goto error;

  if (_snwprintf_s((wchar_t*)&args,
                   sizeof(args) / sizeof(wchar_t),
                   _TRUNCATE,
                   L"\"%s\" %S meh",
                   image,
                   name) < 0)
    goto error;

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
  p->name = name;

  return 0;


error:
  if (file != INVALID_HANDLE_VALUE)
    CloseHandle(file);
  if (nul != INVALID_HANDLE_VALUE)
    CloseHandle(file);

  return -1;
}


/* Timeout is is msecs. Set timeout < 0 to never time out. */
/* Returns 0 when all processes are terminated, -1 on timeout. */
int process_wait(process_info_t *vec, int n, int timeout) {
  int i;
  HANDLE handles[MAXIMUM_WAIT_OBJECTS];
  DWORD timeout_api, result;

  /* If there's nothing to wait for, return immedately. */
  if (n == 0)
    return 0;

  assert(n <= MAXIMUM_WAIT_OBJECTS);

  for (i = 0; i < n; i++)
    handles[i] = vec[i].process;

  if (timeout >= 0) {
    timeout_api = (DWORD)timeout;
  } else {
    timeout_api = INFINITE;
  }

  result = WaitForMultipleObjects(n, handles, TRUE, timeout_api);

  if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + n) {
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


int process_copy_output(process_info_t *p, int fd) {
  /* Any errors in this function are ignored */
  DWORD read;
  char buf[1024];

  if (SetFilePointer(p->stdio_out, 0, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
    return -1;

  while (ReadFile(p->stdio_out, (void*)&buf, sizeof(buf), &read, NULL) &&
         read > 0)
    write(fd, buf, read);

  if (GetLastError() != ERROR_HANDLE_EOF)
    return -1;

  return 0;
}


char* process_get_name(process_info_t *p) {
  return p->name;
}


int process_terminate(process_info_t *p) {
  /* If it fails the process is probably already closed. */
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
  CloseHandle(p->stdio_out);
}


int rewind_cursor() {
  HANDLE handle;
  CONSOLE_SCREEN_BUFFER_INFO info;
  COORD coord;

  handle = (HANDLE)_get_osfhandle(fileno(stdout));
  if (handle == INVALID_HANDLE_VALUE)
    return -1;

  if (!GetConsoleScreenBufferInfo(handle, &info))
    return -1;

  coord = info.dwCursorPosition;
  if (coord.Y <= 0)
    return -1;

  coord.Y--;
  coord.X = 0;

  if (!SetConsoleCursorPosition(handle, coord))
    return -1;

  return 0;
}