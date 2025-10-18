#ifdef _WIN32

#include "uv.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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


static int path_exists(const char* path) {
  WCHAR wpath[32768];
  DWORD attrs;
  int len;
  
  len = MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 32768);
  if (len == 0)
    return 0;
    
  attrs = GetFileAttributesW(wpath);
  
  return attrs != INVALID_FILE_ATTRIBUTES && 
         (attrs & FILE_ATTRIBUTE_DIRECTORY);
}


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

  r = uv_cwd(original_cwd, &original_cwd_size);
  ASSERT_EQ(r, 0);

  drive = get_current_drive();
  ASSERT_NE(drive, 0);

  snprintf(test_path, sizeof(test_path), "\\\\?\\%c:\\Windows", drive);
  if (path_exists(test_path)) {
    r = uv_chdir(test_path);
    ASSERT_EQ(r, 0);

    ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
    
    snprintf(expected_path, sizeof(expected_path), "\\\\?\\%c:\\Windows", drive);
    ASSERT(strstr(env_value, "Windows") != NULL);
  }

  snprintf(test_path, sizeof(test_path), "\\\\?\\%c:\\Windows", drive + 32);
  if (path_exists(test_path)) {
    r = uv_chdir(test_path);
    ASSERT_EQ(r, 0);

    ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
  }

  snprintf(test_path, sizeof(test_path), "\\\\.\\%c:", drive);
  r = uv_chdir(test_path);
  if (r == 0) {
    ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
  }

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

  r = uv_cwd(original_cwd, &original_cwd_size);
  ASSERT_EQ(r, 0);

  drive = get_current_drive();
  ASSERT_NE(drive, 0);

  char old_env[1024];
  int had_env = get_drive_env(drive, old_env, sizeof(old_env));

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
    
    ASSERT(strncmp(cwd, "\\\\", 2) == 0 || strncmp(cwd, "//", 2) == 0);
  } else {
    ASSERT(r == UV_EACCES || r == UV_ENOENT || r == UV_EINVAL);
  }

  snprintf(test_path, sizeof(test_path), "\\\\localhost\\%c$", drive);
  r = uv_chdir(test_path);

  r = uv_chdir(original_cwd);
  ASSERT_EQ(r, 0);

  return 0;
}


TEST_IMPL(chdir_volume_guid_path) {
  char drive;
  char old_env[1024];
  int had_env;

  drive = get_current_drive();
  ASSERT_NE(drive, 0);

  had_env = get_drive_env(drive, old_env, sizeof(old_env));

  uv_chdir("\\\\?\\Volume{12345678-1234-1234-1234-123456789012}\\");

  char new_env[1024];
  int has_env = get_drive_env(drive, new_env, sizeof(new_env));

  if (had_env && has_env) {
    ASSERT_STR_EQ(old_env, new_env);
  } else if (had_env && !has_env) {
    ASSERT(0 && "Drive env variable was deleted");
  } else if (!had_env && has_env) {
    ASSERT(0 && "Drive env variable was created");
  }

  return 0;
}


TEST_IMPL(chdir_globalroot_path) {
  char drive;
  char old_env[1024];
  int had_env;

  drive = get_current_drive();
  ASSERT_NE(drive, 0);

  had_env = get_drive_env(drive, old_env, sizeof(old_env));

  uv_chdir("\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1");

  char new_env[1024];
  int has_env = get_drive_env(drive, new_env, sizeof(new_env));

  if (had_env && has_env) {
    ASSERT_STR_EQ(old_env, new_env);
  } else if (had_env && !has_env) {
    ASSERT(0 && "Drive env variable was deleted");
  } else if (!had_env && has_env) {
    ASSERT(0 && "Drive env variable was created");
  }

  return 0;
}


TEST_IMPL(chdir_nt_namespace) {
  char original_cwd[1024];
  size_t original_cwd_size = sizeof(original_cwd);
  char test_path[256];
  char env_value[1024];
  char drive;
  int r;

  r = uv_cwd(original_cwd, &original_cwd_size);
  ASSERT_EQ(r, 0);

  drive = get_current_drive();
  ASSERT_NE(drive, 0);

  snprintf(test_path, sizeof(test_path), "\\??\\%c:\\Windows", drive);
  r = uv_chdir(test_path);
  
  if (r == 0) {
    ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
    ASSERT(strstr(env_value, "Windows") != NULL);
  } else {
    ASSERT(r == UV_ENOENT || r == UV_EINVAL || r == UV_EACCES);
  }

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

  r = uv_cwd(original_cwd, &original_cwd_size);
  ASSERT_EQ(r, 0);

  drive = get_current_drive();
  ASSERT_NE(drive, 0);

  snprintf(test_path_upper, sizeof(test_path_upper), "%c:\\Windows", drive);
  if (path_exists(test_path_upper)) {
    r = uv_chdir(test_path_upper);
    ASSERT_EQ(r, 0);

    ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
  }

  snprintf(test_path_lower, sizeof(test_path_lower), "%c:\\Windows", drive + 32);
  if (path_exists(test_path_lower)) {
    r = uv_chdir(test_path_lower);
    ASSERT_EQ(r, 0);

    ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
  }

  snprintf(test_path_mixed, sizeof(test_path_mixed), "\\\\?\\%c:\\Windows", 
           drive + 32);
  if (path_exists(test_path_mixed)) {
    r = uv_chdir(test_path_mixed);
    ASSERT_EQ(r, 0);

    ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
  }

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

  r = uv_cwd(original_cwd, &original_cwd_size);
  ASSERT_EQ(r, 0);

  drive = get_current_drive();
  ASSERT_NE(drive, 0);

  snprintf(test_path_1, sizeof(test_path_1), "%c:\\Windows", drive);
  if (path_exists(test_path_1)) {
    r = uv_chdir(test_path_1);
    ASSERT_EQ(r, 0);

    ASSERT_EQ(get_drive_env(drive, env_value_1, sizeof(env_value_1)), 1);
    ASSERT(strstr(env_value_1, "Windows") != NULL);

    snprintf(test_path_2, sizeof(test_path_2), "%c:\\Windows\\System32", drive);
    if (path_exists(test_path_2)) {
      r = uv_chdir(test_path_2);
      ASSERT_EQ(r, 0);

      ASSERT_EQ(get_drive_env(drive, env_value_2, sizeof(env_value_2)), 1);
      ASSERT(strstr(env_value_2, "System32") != NULL);
      
      ASSERT_STR_NE(env_value_1, env_value_2);
    }
  }

  r = uv_chdir(original_cwd);
  ASSERT_EQ(r, 0);

  return 0;
}


TEST_IMPL(chdir_device_paths) {
  char drive = get_current_drive();
  char old_env[1024];
  int had_env = get_drive_env(drive, old_env, sizeof(old_env));
  
  uv_chdir("\\\\.\\COM1");
  
  char new_env[1024];
  int has_env = get_drive_env(drive, new_env, sizeof(new_env));
  
  if (had_env && has_env) {
    ASSERT_STR_EQ(old_env, new_env);
  }
  return 0;
}

#endif
