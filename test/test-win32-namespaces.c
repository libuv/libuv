#ifdef _WIN32

#include "uv.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper to get current drive letter */
static char get_current_drive(void) {
  char cwd[MAX_PATH];
  size_t cwd_size = sizeof(cwd);
  if (uv_cwd(cwd, &cwd_size) != 0)
    return 0;
  if (cwd[0] >= 'A' && cwd[0] <= 'Z')
    return cwd[0];
  if (cwd[0] >= 'a' && cwd[0] <= 'z')
    return cwd[0] - 'a' + 'A';
  return 0;
}


/* Helper to check if a path exists and is accessible */
static int path_exists(const char* path) {
  WCHAR wpath[32768];  /* Max path length for Win32 namespace */
  DWORD attrs;
  int len;
  
  /* Convert UTF-8 to UTF-16 using Windows API */
  len = MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 32768);
  if (len == 0)
    return 0;
    
  attrs = GetFileAttributesW(wpath);
  
  return attrs != INVALID_FILE_ATTRIBUTES && 
         (attrs & FILE_ATTRIBUTE_DIRECTORY);
}


/* Helper to get environment variable value (drive-local path) */
static int get_drive_env(char drive, char* buf, size_t buflen) {
  char env_name[4];
  env_name[0] = '=';
  env_name[1] = drive;
  env_name[2] = ':';
  env_name[3] = '\0';
  
  DWORD result = GetEnvironmentVariableA(env_name, buf, (DWORD)buflen);
  return result > 0 && result < buflen;
}


TEST_IMPL(chdir_win32_namespace) {
  char original_cwd[1024];
  size_t original_cwd_size = sizeof(original_cwd);
  char test_path[1024];
  char expected_path[1024];
  char env_value[1024];
  char drive;
  int r;

  /* Save original working directory */
  r = uv_cwd(original_cwd, &original_cwd_size);
  ASSERT_EQ(r, 0);

  drive = get_current_drive();
  ASSERT_NE(drive, 0);

  /* Test 1: Win32 namespace path \\?\C:\Windows */
  snprintf(test_path, sizeof(test_path), "\\\\?\\%c:\\Windows", drive);
  if (path_exists(test_path)) {
    r = uv_chdir(test_path);
    ASSERT_EQ(r, 0);

    /* Verify the drive-local environment variable is set */
    ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
    
    /* The env value should contain the current path (check substring) */
    snprintf(expected_path, sizeof(expected_path), "\\\\?\\%c:\\Windows", drive);
    ASSERT(strstr(env_value, "Windows") != NULL);
  }

  /* Test 2: Win32 namespace with lowercase drive letter */
  snprintf(test_path, sizeof(test_path), "\\\\?\\%c:\\Windows", drive + 32);
  if (path_exists(test_path)) {
    r = uv_chdir(test_path);
    ASSERT_EQ(r, 0);

    /* Environment variable should use uppercase drive letter */
    ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
  }

  /* Test 3: Device namespace \\.\C: (if accessible) */
  snprintf(test_path, sizeof(test_path), "\\\\.\\%c:", drive);
  r = uv_chdir(test_path);
  /* May fail with access denied, which is OK */
  if (r == 0) {
    ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
  }

  /* Restore original directory */
  r = uv_chdir(original_cwd);
  ASSERT_EQ(r, 0);

  return 0;
}


TEST_IMPL(chdir_unc_paths) {
  char original_cwd[1024];
  size_t original_cwd_size = sizeof(original_cwd);
  char test_path[1024];
  char drive;
  int r;

  /* Save original working directory */
  r = uv_cwd(original_cwd, &original_cwd_size);
  ASSERT_EQ(r, 0);

  drive = get_current_drive();
  ASSERT_NE(drive, 0);

  char old_env[1024];
  int had_env = get_drive_env(drive, old_env, sizeof(old_env));

  /* Test 1: UNC path with Win32 namespace \\?\UNC\localhost\C$ */
  snprintf(test_path, sizeof(test_path), "\\\\?\\UNC\\localhost\\%c$", drive);
  r = uv_chdir(test_path);
  
  if (r == 0) {
    char new_env[1024];
    int has_env = get_drive_env(drive, new_env, sizeof(new_env));

    if (had_env && has_env) {
      ASSERT_STR_EQ(old_env, new_env);
    }

    char cwd[1024];
    size_t cwd_size = sizeof(cwd);
    r = uv_cwd(cwd, &cwd_size);
    ASSERT_EQ(r, 0);
    
    /* Verify we're actually in a UNC path */
    ASSERT(strncmp(cwd, "\\\\", 2) == 0 || strncmp(cwd, "//", 2) == 0);
  } else {
    /* Access denied or path not found is acceptable for UNC paths */
    ASSERT(r == UV_EACCES || r == UV_ENOENT || r == UV_EINVAL);
  }

  /* Test 2: Regular UNC path \\localhost\C$ */
  snprintf(test_path, sizeof(test_path), "\\\\localhost\\%c$", drive);
  r = uv_chdir(test_path);
  /* May fail, which is acceptable */

  /* Restore original directory */
  r = uv_chdir(original_cwd);
  ASSERT_EQ(r, 0);

  return 0;
}


TEST_IMPL(chdir_forward_slash_rejection) {
  char test_path[256];
  char drive;
  int r;

  drive = get_current_drive();
  ASSERT_NE(drive, 0);

  /* Test 1: Win32 namespace with forward slash should fail */
  snprintf(test_path, sizeof(test_path), "\\\\?\\%c:/Windows", drive);
  r = uv_chdir(test_path);
  ASSERT_EQ(r, UV_EINVAL);

  /* Test 2: Win32 namespace with mixed slashes should fail */
  snprintf(test_path, sizeof(test_path), "\\\\?\\%c:\\Windows/System32", drive);
  r = uv_chdir(test_path);
  ASSERT_EQ(r, UV_EINVAL);

  /* Test 3: Device namespace with forward slash should fail */
  snprintf(test_path, sizeof(test_path), "\\\\.\\%c:/", drive);
  r = uv_chdir(test_path);
  ASSERT_EQ(r, UV_EINVAL);

  /* Test 4: Regular path with forward slash should succeed (Windows normalizes) */
  snprintf(test_path, sizeof(test_path), "%c:/Windows", drive);
  r = uv_chdir(test_path);
  /* Should succeed if path exists, or fail with ENOENT, but NOT EINVAL */
  ASSERT_NE(r, UV_EINVAL);

  return 0;
}


TEST_IMPL(chdir_path_too_long) {
  char* long_path;
  int r;
  size_t i;
  char drive = get_current_drive();
  ASSERT_NE(drive, 0);

  /* Create a path longer than 32767 characters */
  long_path = malloc(35000);
  ASSERT(long_path != NULL);

  /* Build: \\?\C:\ + many repetitions of "LongDirectory\" */
  snprintf(long_path, 100, "\\\\?\\%c:\\", drive);
  size_t pos = strlen(long_path);

  for (i = 0; i < 2500; i++) {
    strcpy(long_path + pos, "LongDirectory\\");
    pos += 14;
    if (pos > 33000) break;
  }
  long_path[pos] = '\0';

  /* This should fail with UV_ENAMETOOLONG */
  r = uv_chdir(long_path);
  ASSERT_EQ(r, UV_ENAMETOOLONG);

  free(long_path);
  return 0;
}


TEST_IMPL(chdir_volume_guid_path) {
  /* Note: This test may not work on all systems as volume GUIDs are dynamic.
   * We're testing that the code handles them correctly without crashing. */
  char original_cwd[1024];
  size_t original_cwd_size = sizeof(original_cwd);
  int r;

  /* Save original working directory */
  r = uv_cwd(original_cwd, &original_cwd_size);
  ASSERT_EQ(r, 0);

  /* Try a Volume GUID path (will likely fail with ENOENT, which is fine) */
  r = uv_chdir("\\\\?\\Volume{12345678-1234-1234-1234-123456789012}\\");
  
  /* Should fail with ENOENT or EINVAL, but should NOT crash */
  ASSERT(r == UV_ENOENT || r == UV_EINVAL || r == UV_EACCES);

  /* Verify we're still in the original directory */
  char current_cwd[1024];
  size_t current_cwd_size = sizeof(current_cwd);
  r = uv_cwd(current_cwd, &current_cwd_size);
  ASSERT_EQ(r, 0);
  ASSERT_STR_EQ(current_cwd, original_cwd);

  return 0;
}


TEST_IMPL(chdir_globalroot_path) {
  char original_cwd[1024];
  size_t original_cwd_size = sizeof(original_cwd);
  int r;

  /* Save original working directory */
  r = uv_cwd(original_cwd, &original_cwd_size);
  ASSERT_EQ(r, 0);

  /* Try a GLOBALROOT path (will likely fail, which is fine) */
  r = uv_chdir("\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1");
  
  /* Should fail with ENOENT or EINVAL or EACCES, but should NOT crash */
  ASSERT(r == UV_ENOENT || r == UV_EINVAL || r == UV_EACCES);

  /* Verify we're still in the original directory */
  char current_cwd[1024];
  size_t current_cwd_size = sizeof(current_cwd);
  r = uv_cwd(current_cwd, &current_cwd_size);
  ASSERT_EQ(r, 0);
  ASSERT_STR_EQ(current_cwd, original_cwd);

  return 0;
}


TEST_IMPL(chdir_nt_namespace) {
  char original_cwd[1024];
  size_t original_cwd_size = sizeof(original_cwd);
  char test_path[256];
  char env_value[1024];
  char drive;
  int r;

  /* Save original working directory */
  r = uv_cwd(original_cwd, &original_cwd_size);
  ASSERT_EQ(r, 0);

  drive = get_current_drive();
  ASSERT_NE(drive, 0);

  /* Test NT namespace path \??\C:\Windows */
  snprintf(test_path, sizeof(test_path), "\\??\\%c:\\Windows", drive);
  r = uv_chdir(test_path);
  
  if (r == 0) {
    /* Verify the drive-local environment variable is set */
    ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
    ASSERT(strstr(env_value, "Windows") != NULL);
  } else {
    /* May fail with access issues, which is acceptable */
    ASSERT(r == UV_ENOENT || r == UV_EINVAL || r == UV_EACCES);
  }

  /* Restore original directory */
  r = uv_chdir(original_cwd);
  ASSERT_EQ(r, 0);

  return 0;
}


TEST_IMPL(chdir_case_insensitive) {
  char original_cwd[1024];
  size_t original_cwd_size = sizeof(original_cwd);
  char test_path_upper[256];
  char test_path_lower[256];
  char test_path_mixed[256];
  char env_value[1024];
  char drive;
  int r;

  /* Save original working directory */
  r = uv_cwd(original_cwd, &original_cwd_size);
  ASSERT_EQ(r, 0);

  drive = get_current_drive();
  ASSERT_NE(drive, 0);

  /* Test uppercase drive */
  snprintf(test_path_upper, sizeof(test_path_upper), "%c:\\Windows", drive);
  if (path_exists(test_path_upper)) {
    r = uv_chdir(test_path_upper);
    ASSERT_EQ(r, 0);

    /* Environment variable should always use uppercase */
    ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
  }

  /* Test lowercase drive */
  snprintf(test_path_lower, sizeof(test_path_lower), "%c:\\Windows", drive + 32);
  if (path_exists(test_path_lower)) {
    r = uv_chdir(test_path_lower);
    ASSERT_EQ(r, 0);

    /* Environment variable should STILL use uppercase */
    ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
  }

  /* Test with Win32 namespace and lowercase */
  snprintf(test_path_mixed, sizeof(test_path_mixed), "\\\\?\\%c:\\Windows", 
           drive + 32);
  if (path_exists(test_path_mixed)) {
    r = uv_chdir(test_path_mixed);
    ASSERT_EQ(r, 0);

    /* Environment variable should use uppercase */
    ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
  }

  /* Restore original directory */
  r = uv_chdir(original_cwd);
  ASSERT_EQ(r, 0);

  return 0;
}


TEST_IMPL(chdir_drive_env_variable_update) {
  char original_cwd[1024];
  size_t original_cwd_size = sizeof(original_cwd);
  char test_path_1[256];
  char test_path_2[256];
  char env_value_1[1024];
  char env_value_2[1024];
  char drive;
  int r;

  /* Save original working directory */
  r = uv_cwd(original_cwd, &original_cwd_size);
  ASSERT_EQ(r, 0);

  drive = get_current_drive();
  ASSERT_NE(drive, 0);

  /* Change to Windows directory */
  snprintf(test_path_1, sizeof(test_path_1), "%c:\\Windows", drive);
  if (path_exists(test_path_1)) {
    r = uv_chdir(test_path_1);
    ASSERT_EQ(r, 0);

    /* Get environment variable value */
    ASSERT_EQ(get_drive_env(drive, env_value_1, sizeof(env_value_1)), 1);
    ASSERT(strstr(env_value_1, "Windows") != NULL);

    /* Change to System32 subdirectory */
    snprintf(test_path_2, sizeof(test_path_2), "%c:\\Windows\\System32", drive);
    if (path_exists(test_path_2)) {
      r = uv_chdir(test_path_2);
      ASSERT_EQ(r, 0);

      /* Environment variable should be updated */
      ASSERT_EQ(get_drive_env(drive, env_value_2, sizeof(env_value_2)), 1);
      ASSERT(strstr(env_value_2, "System32") != NULL);
      
      /* Values should be different */
      ASSERT_STR_NE(env_value_1, env_value_2);
    }
  }

  /* Restore original directory */
  r = uv_chdir(original_cwd);
  ASSERT_EQ(r, 0);

  return 0;
}

TEST_IMPL(chdir_device_paths) {
  /* These should fail gracefully, not crash */
  ASSERT_NE(uv_chdir("\\\\.\\COM1"), 0);
  ASSERT_NE(uv_chdir("\\\\.\\PhysicalDrive0"), 0);
  return 0;
}

#endif
