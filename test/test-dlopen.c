#include "uv.h"
#include "task.h"
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define MAX_DIR_NAME_LENGTH 247
#define BUFFER_SIZE 1024

TEST_IMPL(dlopen) {
#ifdef _WIN32
  const char* system_dll = "\\ntdll.dll";
  char system_dll_filename[BUFFER_SIZE];
  char temporary_folder_short[BUFFER_SIZE];
  char temporary_folder[BUFFER_SIZE];
  unsigned temporary_folder_length;
  char temporary_filename[BUFFER_SIZE];
  unsigned temporary_filename_length;
  char temporary_filename_long[BUFFER_SIZE + 4] = "\\\\?\\";
  uv_lib_t lib;
  int open_result;

  /* Get system DLL full filename */
  if (!GetSystemDirectoryA(system_dll_filename, BUFFER_SIZE))
    abort();
  strncat(system_dll_filename, system_dll, 
          BUFFER_SIZE - strlen(system_dll_filename));

  /* Create a temporary folder just within MAX_PATH length limits*/
  if (!GetTempPathA(MAX_PATH, temporary_folder_short))
    abort();
  temporary_folder_length = GetLongPathNameA(temporary_folder_short,
                                             temporary_folder,
                                             BUFFER_SIZE);
  if (temporary_folder_length == 0)
    abort();
  while (temporary_folder_length < MAX_DIR_NAME_LENGTH) {
    temporary_folder[temporary_folder_length++] = '0';
  }
  temporary_folder[temporary_folder_length] = '\0';
  if (!CreateDirectoryA(temporary_folder, NULL) 
      && GetLastError() != ERROR_ALREADY_EXISTS) {
    abort();
  }

  /* Copy system DLL to new location, but lengthen the name so it is
     over MAX_PATH in length. Symlink won't work here. */
  strcpy(temporary_filename, temporary_folder);
  temporary_filename_length = temporary_folder_length;
  temporary_filename[temporary_filename_length++] = '\\';
  while (temporary_filename_length < MAX_PATH + 1) {
    temporary_filename[temporary_filename_length++] = '0';
  }
  temporary_filename[temporary_filename_length] = '\0';
  /* Add +1 to skip \ from system_dll beginning */
  strncat(temporary_filename, system_dll + 1,
          BUFFER_SIZE - temporary_filename_length);
  strncat(temporary_filename_long, temporary_filename, BUFFER_SIZE);
  DeleteFileA(temporary_filename_long);
  if (!CopyFileA(system_dll_filename, temporary_filename_long, TRUE))
    abort();

  /* Open DLL with very long filename */
  open_result = uv_dlopen(temporary_filename_long, &lib);
  if (open_result == 0)
    uv_dlclose(&lib);

  /* Clean up */
  DeleteFileA(temporary_filename_long);
  RemoveDirectoryA(temporary_folder);

  /* Test if we succeeded */
  ASSERT(open_result == 0);
  return 0;

#else
  RETURN_SKIP("Windows only test");
#endif	
}
