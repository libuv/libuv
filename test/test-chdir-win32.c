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

static int get_drive_env(char drive, char* buf, size_t buflen) {
  char env_name[4];
  env_name[0] = '=';
  env_name[1] = drive;
  env_name[2] = ':';
  env_name[3] = '\0';
  
  DWORD result = GetEnvironmentVariableA(env_name, buf, (DWORD)buflen);
  return result > 0 && result < buflen;
}

TEST_IMPL(chdir_sets_drive_env_for_normal_path) {
  char original_cwd[1024];
  size_t size = sizeof(original_cwd);
  char env_value[1024];
  char drive;
  int r;
  
  r = uv_cwd(original_cwd, &size);
  ASSERT_EQ(r, 0);
  
  drive = get_current_drive();
  if (drive == 0)
    return 0;
  
  r = uv_chdir(original_cwd);
  ASSERT_EQ(r, 0);
  
  ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
  ASSERT(strstr(env_value, original_cwd) != NULL);
  
  return 0;
}

TEST_IMPL(chdir_normalizes_drive_letter_case) {
  char original_cwd[1024];
  size_t size = sizeof(original_cwd);
  char lowercase_path[1024];
  char env_value[1024];
  char drive;
  int r;
  
  r = uv_cwd(original_cwd, &size);
  ASSERT_EQ(r, 0);
  
  drive = get_current_drive();
  if (drive == 0)
    return 0;
  
  strcpy(lowercase_path, original_cwd);
  if (lowercase_path[0] >= 'A' && lowercase_path[0] <= 'Z')
    lowercase_path[0] = lowercase_path[0] + 32;
  
  r = uv_chdir(lowercase_path);
  ASSERT_EQ(r, 0);
  
  ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
  
  return 0;
}

TEST_IMPL(chdir_updates_env_on_path_change) {
  char original_cwd[1024];
  size_t size = sizeof(original_cwd);
  char parent_dir[1024];
  char env_value_1[1024];
  char env_value_2[1024];
  char drive;
  char* last_slash;
  int r;
  
  r = uv_cwd(original_cwd, &size);
  ASSERT_EQ(r, 0);
  
  drive = get_current_drive();
  if (drive == 0) return 0;
  
  strcpy(parent_dir, original_cwd);
  last_slash = strrchr(parent_dir, '\\');
  if (last_slash == NULL || last_slash == parent_dir + 2)
    return 0;

  *last_slash = '\0';
  
  r = uv_chdir(parent_dir);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(get_drive_env(drive, env_value_1, sizeof(env_value_1)), 1);
  
  r = uv_chdir(original_cwd);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(get_drive_env(drive, env_value_2, sizeof(env_value_2)), 1);
  
  ASSERT_STR_NE(env_value_1, env_value_2);
  
  return 0;
}

TEST_IMPL(chdir_preserves_env_for_regular_unc) {
  char original_cwd[1024];
  size_t size = sizeof(original_cwd);
  char unc_path[256];
  char old_env[1024];
  char new_env[1024];
  char drive;
  int had_env;
  int has_env;
  int r;
  
  r = uv_cwd(original_cwd, &size);
  ASSERT_EQ(r, 0);
  
  drive = get_current_drive();
  if (drive == 0)
    return 0;
  
  had_env = get_drive_env(drive, old_env, sizeof(old_env));
  
  snprintf(unc_path, sizeof(unc_path), "\\\\localhost\\%c$", drive);
  r = uv_chdir(unc_path);
  
  if (r != 0)
    return 0;
  
  has_env = get_drive_env(drive, new_env, sizeof(new_env));
  
  if (had_env && has_env) {
    ASSERT_STR_EQ(old_env, new_env);
  } else if (had_env && !has_env) {
    ASSERT(0 && "Drive env should not be deleted");
  } else if (!had_env && has_env) {
    ASSERT(0 && "Drive env should not be created");
  }
  
  uv_chdir(original_cwd);
  return 0;
}

TEST_IMPL(chdir_preserves_env_for_volume_guid) {
  char original_cwd[1024];
  size_t size = sizeof(original_cwd);
  char old_env[1024];
  char new_env[1024];
  char drive;
  int had_env;
  int has_env;
  int r;
  
  r = uv_cwd(original_cwd, &size);
  ASSERT_EQ(r, 0);
  
  drive = get_current_drive();
  if (drive == 0)
    return 0;
  
  had_env = get_drive_env(drive, old_env, sizeof(old_env));
  
  r = uv_chdir("\\\\?\\Volume{12345678-1234-1234-1234-123456789012}\\");
  
  if (r != 0)
    return 0;
  
  has_env = get_drive_env(drive, new_env, sizeof(new_env));
  
  if (had_env && has_env) {
    ASSERT_STR_EQ(old_env, new_env);
  } else if (had_env && !has_env) {
    ASSERT(0 && "Drive env should not be deleted for volume GUID");
  } else if (!had_env && has_env) {
    ASSERT(0 && "Drive env should not be created for volume GUID");
  }
  
  uv_chdir(original_cwd);
  
  return 0;
}

TEST_IMPL(chdir_preserves_env_for_globalroot) {
  char original_cwd[1024];
  size_t size = sizeof(original_cwd);
  char old_env[1024];
  char new_env[1024];
  char drive;
  int had_env;
  int has_env;
  int r;
  
  r = uv_cwd(original_cwd, &size);
  ASSERT_EQ(r, 0);
  
  drive = get_current_drive();
  if (drive == 0) return 0;
  
  had_env = get_drive_env(drive, old_env, sizeof(old_env));
  
  r = uv_chdir("\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1");
  
  if (r != 0)
    return 0;
  
  has_env = get_drive_env(drive, new_env, sizeof(new_env));
  
  if (had_env && has_env) {
    ASSERT_STR_EQ(old_env, new_env);
  } else if (had_env && !has_env) {
    ASSERT(0 && "Drive env should not be deleted for GLOBALROOT");
  } else if (!had_env && has_env) {
    ASSERT(0 && "Drive env should not be created for GLOBALROOT");
  }
  
  uv_chdir(original_cwd);
  
  return 0;
}

TEST_IMPL(chdir_invalid_input_returns_error) {
  int r;
  
  r = uv_chdir(NULL);
  ASSERT_EQ(r, UV_EINVAL);
  
  r = uv_chdir("");
  ASSERT_EQ(r, UV_EINVAL);
  
  return 0;
}

TEST_IMPL(chdir_nonexistent_path_returns_error) {
  char drive;
  char nonexistent[256];
  int r;
  
  drive = get_current_drive();
  if (drive == 0) return 0;
  
  snprintf(nonexistent, sizeof(nonexistent), "%c:\\NonExistentDirectory_xyz123456789", drive);
  
  r = uv_chdir(nonexistent);
  
  ASSERT(r != 0);
  ASSERT(r == UV_ENOENT || r == UV_EACCES);
  
  return 0;
}

TEST_IMPL(chdir_win32_namespace_updates_env) {
  char original_cwd[1024];
  size_t size = sizeof(original_cwd);
  char win32_path[2048];
  char env_value[1024];
  char drive;
  int r;
  
  r = uv_cwd(original_cwd, &size);
  ASSERT_EQ(r, 0);
  
  drive = get_current_drive();
  if (drive == 0) return 0;
  
  if (strlen(original_cwd) > sizeof(win32_path) - 5) {
    return 0;
  }
  
  snprintf(win32_path, sizeof(win32_path), "\\\\?\\%s", original_cwd);
  
  r = uv_chdir(win32_path);
  ASSERT_EQ(r, 0);
  
  ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
  
  return 0;
}

TEST_IMPL(chdir_device_namespace_with_drive) {
  char original_cwd[1024];
  size_t size = sizeof(original_cwd);
  char device_path[256];
  char old_env[1024];
  char drive;
  int r;
  
  r = uv_cwd(original_cwd, &size);
  ASSERT_EQ(r, 0);
  
  drive = get_current_drive();
  if (drive == 0) return 0;
  
  get_drive_env(drive, old_env, sizeof(old_env));
  
  snprintf(device_path, sizeof(device_path), "\\\\.\\%c:", drive);
  r = uv_chdir(device_path);
  
  if (r == 0) {
    char new_env[1024];
    ASSERT_EQ(get_drive_env(drive, new_env, sizeof(new_env)), 1);
  }
  
  uv_chdir(original_cwd);
  return 0;
}

TEST_IMPL(chdir_nt_namespace_with_drive) {
  char original_cwd[1024];
  size_t size = sizeof(original_cwd);
  char nt_path[1024];
  char env_value[1024];
  char drive;
  char* path_after_drive;
  int r;
  
  r = uv_cwd(original_cwd, &size);
  ASSERT_EQ(r, 0);
  
  drive = get_current_drive();
  if (drive == 0) return 0;
  
  path_after_drive = strchr(original_cwd + 2, '\\');
  if (path_after_drive == NULL) {
    return 0;
  }
  
  snprintf(nt_path, sizeof(nt_path), "\\??\\%c:%s", drive, path_after_drive);
  
  r = uv_chdir(nt_path);
  
  if (r == 0) {
    ASSERT_EQ(get_drive_env(drive, env_value, sizeof(env_value)), 1);
  } else {
    ASSERT(r == UV_ENOENT || r == UV_EINVAL || r == UV_EACCES);
  }
  
  uv_chdir(original_cwd);
  return 0;
}

#endif
