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
#include "internal.h"
#include <stdio.h>
#include <Rpc.h>

#define MAX_LONG_PATH 32768
#define MAX_LINK_FILENAME_RETRIES 16

static int uv__dlerror(uv_lib_t* lib, int errorno);

int uv_dlopen(const char* filename, uv_lib_t* lib) {
  WCHAR filename_w[MAX_LONG_PATH];
  WCHAR temp_path[MAX_PATH];
  WCHAR symlink_name[MAX_PATH];
  WCHAR dll_filename_long[MAX_LONG_PATH] = L"\\\\?\\";
  WCHAR *filename_ptr = filename_w;
  DWORD load_library_last_error;
  UUID uuid;
  RPC_WSTR uuid_string;

  lib->handle = NULL;
  lib->errmsg = NULL;

  if (!MultiByteToWideChar(CP_UTF8,
                           0,
                           filename,
                           -1,
                           filename_w,
                           ARRAY_SIZE(filename_w))) {
    return uv__dlerror(lib, GetLastError());
  }

  lib->handle = LoadLibraryExW(filename_w, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
  load_library_last_error = GetLastError();
  if (lib->handle == NULL
      && load_library_last_error == ERROR_INSUFFICIENT_BUFFER) {
    /* If filename is too long, LoadLibrary will fail on some versions of
       Windows. We will create a symbolic link in temporary folder to that
       DLL, and load it from there. */

    /* Prefix filename with \\?\ if not already present,
       otherwise it won't work */
    if (wcsncmp(filename_w, dll_filename_long, 4) == 0) {
      wcsncat(dll_filename_long + 4, filename_w + 4, MAX_LONG_PATH - 4);
    } else {
      wcsncat(dll_filename_long, filename_w, MAX_LONG_PATH - 4);
      /* Adding \\?\ disables support for forward slashes. Substitute
         them with back slashes. */
      while (*filename_ptr != 0) {
        if (*filename_ptr == '/') {
          *filename_ptr = '\\';
        }
        ++filename_ptr;
      }
    }

    /* Create temporary file name */
    if (!GetTempPathW(MAX_PATH, temp_path)) {
      return uv__dlerror(lib, GetLastError());
    }
    if (UuidCreateSequential(&uuid) == RPC_S_UUID_NO_ADDRESS) {
      return uv__dlerror(lib, GetLastError());
    }
    if (UuidToStringW(&uuid, &uuid_string) != RPC_S_OK) {
      return uv__dlerror(lib, GetLastError());
    }
    swprintf_s(symlink_name, MAX_PATH, L"%s%s.tmp", temp_path, uuid_string);
    RpcStringFreeW(&uuid_string);

    /* Create symbolic link and try to load it */
    if (!CreateSymbolicLinkW(symlink_name, dll_filename_long, 0)) {
      return uv__dlerror(lib, GetLastError());
    }
    lib->handle = LoadLibraryExW(symlink_name, NULL,
                                 LOAD_WITH_ALTERED_SEARCH_PATH);
    load_library_last_error = GetLastError();
    DeleteFileW(symlink_name);
  }

  if (lib->handle == NULL) {
    return uv__dlerror(lib, load_library_last_error);
  }

  return 0;
}


void uv_dlclose(uv_lib_t* lib) {
  if (lib->errmsg) {
    LocalFree((void*)lib->errmsg);
    lib->errmsg = NULL;
  }

  if (lib->handle) {
    /* Ignore errors. No good way to signal them without leaking memory. */
    FreeLibrary(lib->handle);
    lib->handle = NULL;
  }
}


int uv_dlsym(uv_lib_t* lib, const char* name, void** ptr) {
  *ptr = (void*) GetProcAddress(lib->handle, name);
  return uv__dlerror(lib, *ptr ? 0 : GetLastError());
}


const char* uv_dlerror(const uv_lib_t* lib) {
  return lib->errmsg ? lib->errmsg : "no error";
}


static void uv__format_fallback_error(uv_lib_t* lib, int errorno){
  DWORD_PTR args[1] = { (DWORD_PTR) errorno };
  LPSTR fallback_error = "error: %1!d!";

  FormatMessageA(FORMAT_MESSAGE_FROM_STRING |
                 FORMAT_MESSAGE_ARGUMENT_ARRAY |
                 FORMAT_MESSAGE_ALLOCATE_BUFFER,
                 fallback_error, 0, 0,
                 (LPSTR) &lib->errmsg,
                 0, (va_list*) args);
}



static int uv__dlerror(uv_lib_t* lib, int errorno) {
  DWORD res;

  if (lib->errmsg) {
    LocalFree((void*)lib->errmsg);
    lib->errmsg = NULL;
  }

  if (errorno) {
    res = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                         FORMAT_MESSAGE_FROM_SYSTEM |
                         FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorno,
                         MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                         (LPSTR) &lib->errmsg, 0, NULL);
    if (!res && GetLastError() == ERROR_MUI_FILE_NOT_FOUND) {
      res = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                           FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorno,
                           0, (LPSTR) &lib->errmsg, 0, NULL);
    }

    if (!res) {
      uv__format_fallback_error(lib, errorno);
    }
  }

  return errorno ? -1 : 0;
}
