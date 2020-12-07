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
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#if defined(__APPLE__) && !TARGET_OS_IPHONE
# include <spawn.h>
# include <paths.h>
# include <sys/kauth.h>
# include <sys/types.h>
# include <sys/sysctl.h>
# include <dlfcn.h>
# include <crt_externs.h>
# define environ (*_NSGetEnviron())

/* macOS 10.14 back does not define this constant */
# ifndef POSIX_SPAWN_SETSID
#  define POSIX_SPAWN_SETSID 1024
# endif

#else
extern char **environ;
#endif

#if defined(__linux__) || defined(__GLIBC__)
# include <grp.h>
#endif

static void uv__chld(uv_signal_t* handle, int signum) {
  uv_process_t* process;
  uv_loop_t* loop;
  int exit_status;
  int term_signal;
  int status;
  pid_t pid;
  QUEUE pending;
  QUEUE* q;
  QUEUE* h;

  assert(signum == SIGCHLD);

  QUEUE_INIT(&pending);
  loop = handle->loop;

  h = &loop->process_handles;
  q = QUEUE_HEAD(h);
  while (q != h) {
    process = QUEUE_DATA(q, uv_process_t, queue);
    q = QUEUE_NEXT(q);

    do
      pid = waitpid(process->pid, &status, WNOHANG);
    while (pid == -1 && errno == EINTR);

    if (pid == 0)
      continue;

    if (pid == -1) {
      if (errno != ECHILD)
        abort();
      continue;
    }

    process->status = status;
    QUEUE_REMOVE(&process->queue);
    QUEUE_INSERT_TAIL(&pending, &process->queue);
  }

  h = &pending;
  q = QUEUE_HEAD(h);
  while (q != h) {
    process = QUEUE_DATA(q, uv_process_t, queue);
    q = QUEUE_NEXT(q);

    QUEUE_REMOVE(&process->queue);
    QUEUE_INIT(&process->queue);
    uv__handle_stop(process);

    if (process->exit_cb == NULL)
      continue;

    exit_status = 0;
    if (WIFEXITED(process->status))
      exit_status = WEXITSTATUS(process->status);

    term_signal = 0;
    if (WIFSIGNALED(process->status))
      term_signal = WTERMSIG(process->status);

    process->exit_cb(process, exit_status, term_signal);
  }
  assert(QUEUE_EMPTY(&pending));
}

/*
 * Used for initializing stdio streams like options.stdin_stream. Returns
 * zero on success. See also the cleanup section in uv_spawn().
 */
static int uv__process_init_stdio(uv_stdio_container_t* container, int fds[2]) {
  int mask;
  int fd;

  mask = UV_IGNORE | UV_CREATE_PIPE | UV_INHERIT_FD | UV_INHERIT_STREAM;

  switch (container->flags & mask) {
  case UV_IGNORE:
    return 0;

  case UV_CREATE_PIPE:
    assert(container->data.stream != NULL);
    if (container->data.stream->type != UV_NAMED_PIPE)
      return UV_EINVAL;
    else
      return uv_socketpair(SOCK_STREAM, 0, fds, 0, 0);

  case UV_INHERIT_FD:
  case UV_INHERIT_STREAM:
    if (container->flags & UV_INHERIT_FD)
      fd = container->data.fd;
    else
      fd = uv__stream_fd(container->data.stream);

    if (fd == -1)
      return UV_EINVAL;

    fds[1] = fd;
    return 0;

  default:
    assert(0 && "Unexpected flags");
    return UV_EINVAL;
  }
}


static int uv__process_open_stream(uv_stdio_container_t* container,
                                   int pipefds[2]) {
  int flags;
  int err;

  if (!(container->flags & UV_CREATE_PIPE) || pipefds[0] < 0)
    return 0;

  err = uv__close(pipefds[1]);
  if (err != 0)
    abort();

  pipefds[1] = -1;
  uv__nonblock(pipefds[0], 1);

  flags = 0;
  if (container->flags & UV_WRITABLE_PIPE)
    flags |= UV_HANDLE_READABLE;
  if (container->flags & UV_READABLE_PIPE)
    flags |= UV_HANDLE_WRITABLE;

  return uv__stream_open(container->data.stream, pipefds[0], flags);
}


static void uv__process_close_stream(uv_stdio_container_t* container) {
  if (!(container->flags & UV_CREATE_PIPE)) return;
  uv__stream_close(container->data.stream);
}


static void uv__write_int(int fd, int val) {
  ssize_t n;

  do
    n = write(fd, &val, sizeof(val));
  while (n == -1 && errno == EINTR);

  if (n == -1 && errno == EPIPE)
    return; /* parent process has quit */

  assert(n == sizeof(val));
}


#if !(defined(__APPLE__) && (TARGET_OS_TV || TARGET_OS_WATCH))
/* execvp is marked __WATCHOS_PROHIBITED __TVOS_PROHIBITED, so must be
 * avoided. Since this isn't called on those targets, the function
 * doesn't even need to be defined for them.
 */
static void uv__process_child_init(const uv_process_options_t* options,
                                   int stdio_count,
                                   int (*pipes)[2],
                                   int error_fd) {
  sigset_t set;
  int close_fd;
  int use_fd;
  int err;
  int fd;
  int n;

  if (options->flags & UV_PROCESS_DETACHED)
    setsid();

  /* First duplicate low numbered fds, since it's not safe to duplicate them,
   * they could get replaced. Example: swapping stdout and stderr; without
   * this fd 2 (stderr) would be duplicated into fd 1, thus making both
   * stdout and stderr go to the same fd, which was not the intention. */
  for (fd = 0; fd < stdio_count; fd++) {
    use_fd = pipes[fd][1];
    if (use_fd < 0 || use_fd >= fd)
      continue;
    pipes[fd][1] = fcntl(use_fd, F_DUPFD, stdio_count);
    if (pipes[fd][1] == -1) {
      uv__write_int(error_fd, UV__ERR(errno));
      _exit(127);
    }
  }

  for (fd = 0; fd < stdio_count; fd++) {
    close_fd = pipes[fd][0];
    use_fd = pipes[fd][1];

    if (use_fd < 0) {
      if (fd >= 3)
        continue;
      else {
        /* redirect stdin, stdout and stderr to /dev/null even if UV_IGNORE is
         * set
         */
        use_fd = open("/dev/null", fd == 0 ? O_RDONLY : O_RDWR);
        close_fd = use_fd;

        if (use_fd < 0) {
          uv__write_int(error_fd, UV__ERR(errno));
          _exit(127);
        }
      }
    }

    if (fd == use_fd)
      uv__cloexec_fcntl(use_fd, 0);
    else
      fd = dup2(use_fd, fd);

    if (fd == -1) {
      uv__write_int(error_fd, UV__ERR(errno));
      _exit(127);
    }

    if (fd <= 2)
      uv__nonblock_fcntl(fd, 0);

    if (close_fd >= stdio_count)
      uv__close(close_fd);
  }

  for (fd = 0; fd < stdio_count; fd++) {
    use_fd = pipes[fd][1];

    if (use_fd >= stdio_count)
      uv__close(use_fd);
  }

  if (options->cwd != NULL && chdir(options->cwd)) {
    uv__write_int(error_fd, UV__ERR(errno));
    _exit(127);
  }

  if (options->flags & (UV_PROCESS_SETUID | UV_PROCESS_SETGID)) {
    /* When dropping privileges from root, the `setgroups` call will
     * remove any extraneous groups. If we don't call this, then
     * even though our uid has dropped, we may still have groups
     * that enable us to do super-user things. This will fail if we
     * aren't root, so don't bother checking the return value, this
     * is just done as an optimistic privilege dropping function.
     */
    SAVE_ERRNO(setgroups(0, NULL));
  }

  if ((options->flags & UV_PROCESS_SETGID) && setgid(options->gid)) {
    uv__write_int(error_fd, UV__ERR(errno));
    _exit(127);
  }

  if ((options->flags & UV_PROCESS_SETUID) && setuid(options->uid)) {
    uv__write_int(error_fd, UV__ERR(errno));
    _exit(127);
  }

  if (options->env != NULL)
    environ = options->env;

  /* Reset signal disposition.  Use a hard-coded limit because NSIG
   * is not fixed on Linux: it's either 32, 34 or 64, depending on
   * whether RT signals are enabled.  We are not allowed to touch
   * RT signal handlers, glibc uses them internally.
   */
  for (n = 1; n < 32; n += 1) {
    if (n == SIGKILL || n == SIGSTOP)
      continue;  /* Can't be changed. */

#if defined(__HAIKU__)
    if (n == SIGKILLTHR)
      continue;  /* Can't be changed. */
#endif

    if (SIG_ERR != signal(n, SIG_DFL))
      continue;

    uv__write_int(error_fd, UV__ERR(errno));
    _exit(127);
  }

  /* Reset signal mask. */
  sigemptyset(&set);
  err = pthread_sigmask(SIG_SETMASK, &set, NULL);

  if (err != 0) {
    uv__write_int(error_fd, UV__ERR(err));
    _exit(127);
  }

  execvp(options->file, options->args);
  uv__write_int(error_fd, UV__ERR(errno));
  _exit(127);
}
#endif


#if defined(__APPLE__)
typedef struct uv__posix_spawn_fncs_tag {
  struct {
    int (*set_uid_np)(const posix_spawnattr_t *, uid_t);
    int (*set_gid_np)(const posix_spawnattr_t *, gid_t);
    int (*set_groups_np)(const posix_spawnattr_t*, int, gid_t*, uid_t);
  } spawnattr;

  struct {
    int (*addchdir_np)(const posix_spawn_file_actions_t *, const char *);
  } file_actions;
} uv__posix_spawn_fncs_t;


static uv_once_t posix_spawn_init_once = UV_ONCE_INIT;
static uv__posix_spawn_fncs_t posix_spawn_fncs;
static int posix_spawn_can_use_setsid;


void uv__spawn_init_posix_spawn_fncs(void) {
  /* Try to locate all non-portable functions at runtime */
  posix_spawn_fncs.spawnattr.set_uid_np = 
    dlsym(RTLD_DEFAULT, "posix_spawnattr_set_uid_np");
  posix_spawn_fncs.spawnattr.set_gid_np = 
    dlsym(RTLD_DEFAULT, "posix_spawnattr_set_gid_np");
  posix_spawn_fncs.spawnattr.set_groups_np = 
    dlsym(RTLD_DEFAULT, "posix_spawnattr_set_groups_np");
  posix_spawn_fncs.file_actions.addchdir_np = 
    dlsym(RTLD_DEFAULT, "posix_spawn_file_actions_addchdir_np");
}


void uv__spawn_init_can_use_setsid(void) {
  static const int MACOS_CATALINA_VERSION_MAJOR = 19;
  char version_str[256];
  char* version_major_str;
  size_t version_str_size = 256;
  int r;
  int version_major;

  /* By default, assume failure */
  posix_spawn_can_use_setsid = 0;

  /* Get a version string */
  r = sysctlbyname("kern.osrelease", version_str, &version_str_size, NULL, 0);
  if (r != 0)
    return;

  /* Try to get the major version number. If not found
   * fall back to the fork/exec flow */
  version_major_str = strtok(version_str, ".");
  if (version_major_str == NULL)
    return;

  /* Parse the version major as a number. If it is greater than
   * the major version for macOS Catalina (aka macOS 10.15), then
   * the POSIX_SPAWN_SETSID flag is available */
  version_major = atoi(version_major_str);
  if (version_major >= MACOS_CATALINA_VERSION_MAJOR)
    posix_spawn_can_use_setsid = 1;
}


void uv__spawn_init_posix_spawn(void) {
  /* Init handles to all potentially non-defined functions */
  uv__spawn_init_posix_spawn_fncs();

  /* Init feature detection for POSIX_SPAWN_SETSID flag */
  uv__spawn_init_can_use_setsid();
}


int uv__spawn_set_posix_spawn_attrs(posix_spawnattr_t* attrs,
                                    const uv__posix_spawn_fncs_t* posix_spawn_fncs,
                                    const uv_process_options_t* options) {
  int err;
  unsigned int flags;
  sigset_t signal_set;

  err = posix_spawnattr_init(attrs);
  if (err != 0) {
    /* If initialization fails, no need to de-init, just return */
    return err;
  }

  if (options->flags & UV_PROCESS_SETUID) {
    if (posix_spawn_fncs->spawnattr.set_uid_np == NULL) {
      err = ENOSYS;
      goto error;
    }

    err = posix_spawn_fncs->spawnattr.set_uid_np(attrs, options->uid);
    if (err != 0)
      goto error;
  }

  if (options->flags & UV_PROCESS_SETGID) {
    if (posix_spawn_fncs->spawnattr.set_gid_np == NULL) {
      err = ENOSYS;
      goto error;
    }

    err = posix_spawn_fncs->spawnattr.set_gid_np(attrs, options->gid);
    if (err != 0) 
      goto error;
  }

  if (options->flags & (UV_PROCESS_SETUID | UV_PROCESS_SETGID)) {
    /* Using ngroups = 0 implied the group_array is empty, and so 
     * its contents are never traversed. Still the 
     * posix_spawn_set_groups_np function seems to require that the 
     * group_array pointer be non-null */
    const int ngroups = 0;
    gid_t group_array = KAUTH_GID_NONE;

    if (posix_spawn_fncs->spawnattr.set_groups_np == NULL) {
      err = ENOSYS;
      goto error;
    }
    
    /* See the comment on the call to setgroups in uv__process_child_init above
     * for why this is not a fatal error */
    SAVE_ERRNO(posix_spawn_fncs->spawnattr.set_groups_np(
      attrs, 
      ngroups, 
      &group_array, 
      KAUTH_UID_NONE));
  }

  /* Set flags for spawn behavior 
    * 1) POSIX_SPAWN_CLOEXEC_DEFAULT: (Apple Extension) All descriptors in
    * the parent will be treated as if they had been created with O_CLOEXEC.
    * The only fds that will be passed on to the child are those manipulated
    * by the file actions
    * 2) POSIX_SPAWN_SETSIGDEF: Signals mentioned in spawn-sigdefault in
    * the spawn attributes will be reset to behave as their default
    * 3) POSIX_SPAWN_SETSIGMASK: Signal mask will be set to the value of
    * spawn-sigmask in attributes
    * 4) POSIX_SPAWN_SETSID: Make the process a new session leader if a
    * detached session was requested. */
  flags = POSIX_SPAWN_CLOEXEC_DEFAULT |
          POSIX_SPAWN_SETSIGDEF |
          POSIX_SPAWN_SETSIGMASK;
  if (options->flags & UV_PROCESS_DETACHED) {
    /* If running on a version of macOS where this flag is not supported,
     * revert back to the fork/exec flow. Otherwise posix_spawn will
     * silently ignore the flag. */
    if (!posix_spawn_can_use_setsid) {
      err = ENOSYS;
      goto error;
    }

    flags |= POSIX_SPAWN_SETSID;
  }
  err = posix_spawnattr_setflags(attrs, flags);
  if (err != 0)
    goto error;

  /*  Reset all signal the child to their default behavior */
  sigfillset(&signal_set);
  err = posix_spawnattr_setsigdefault(attrs, &signal_set);
  if (err != 0) 
    goto error;

  /*  Reset the signal mask for all signals */
  sigemptyset(&signal_set);
  err = posix_spawnattr_setsigmask(attrs, &signal_set);
  if (err != 0)
    goto error;

  return err;

error:
  (void) posix_spawnattr_destroy(attrs);
  return err;
}


int uv__spawn_set_posix_spawn_file_actions(posix_spawn_file_actions_t* actions,
                                           const uv__posix_spawn_fncs_t* posix_spawn_fncs,
                                           const uv_process_options_t* options,
                                           int stdio_count,
                                           int (*pipes)[2]) {
  int fd;
  int err;

  err = posix_spawn_file_actions_init(actions);
  if (err != 0) {
    /* If initialization fails, no need to de-init, just return */
    return err;
  }

  /* Set the current working directory if requested */
  if (options->cwd != NULL) {
    if (posix_spawn_fncs->file_actions.addchdir_np == NULL) {
      err = ENOSYS;
      goto error;
    }

    err = posix_spawn_fncs->file_actions.addchdir_np(actions, options->cwd);
    if (err != 0)
      goto error;
  }

  /* First, duplicate any required fd into orbit, out of the range of 
   * the descriptors that should be mapped in. */
  for (fd = 0; fd < stdio_count; fd++) {
    if (pipes[fd][1] < 0)
      continue;
    
    err = posix_spawn_file_actions_adddup2(
      actions, 
      pipes[fd][1], 
      stdio_count + fd);
    if (err != 0)
      goto error;
  }

  /*  Second, move the descriptors into their respective places */
  for (fd = 0; fd < stdio_count; fd++) {
    if (pipes[fd][1] < 0)
      continue;

    err = posix_spawn_file_actions_adddup2(actions, stdio_count + fd, fd);
    if (err != 0)
      goto error;
  }

  /*  Finally, close all the superfluous descriptors */
  for (fd = 0; fd < stdio_count; fd++) {
    if (pipes[fd][1] < 0)
      continue;
    
    err = posix_spawn_file_actions_addclose(actions, stdio_count + fd);
    if (err != 0)
      goto error;
  }

  /*  Finally process the standard streams as per documentation */
  for (fd = 0; fd < 3; fd++) {
    int oflags;
    const int mode = 0;

    oflags = fd == 0 ? O_RDONLY : O_RDWR;

    if (pipes[fd][1] != -1) {
      /* If not ignored, make sure the fd is marked as non-blocking */
      uv__nonblock_fcntl(pipes[fd][1], 0);
    } else {
      /* If ignored, redirect to (or from) /dev/null, */
      err = posix_spawn_file_actions_addopen(
        actions, 
        fd, 
        "/dev/null", 
        oflags, 
        mode);
      if (err != 0)
        goto error;
    }
  }  

  return 0;

error:
  (void) posix_spawn_file_actions_destroy(actions);
  return err;
}

char* uv__spawn_find_path_in_env(char** env) {
  char** env_iterator;
  const char path_var[] = "PATH=";

  /* Look for an environment variable called PATH in the 
   * provided env array, and return its value if found */
  for (env_iterator = env; *env_iterator != NULL; env_iterator++) {
    if (strncmp(*env_iterator, path_var, sizeof(path_var) - 1) == 0) {
      /* Found "PATH=" at the beginning of the string */
      return *env_iterator + sizeof(path_var) - 1;
    }
  }

  return NULL;
}


int uv__spawn_resolve_and_spawn(const uv_process_options_t* options, 
                                posix_spawnattr_t* attrs,
                                posix_spawn_file_actions_t* actions,
                                pid_t* pid) {
  const char *p;
  const char *z;
  const char *path;
  size_t l;
  size_t k;
  int err;

  path = NULL;
  err = -1;

  /* Short circuit for erroneous case */
  if (options->file == NULL) 
    return ENOENT;

  /* The environment for the child process is that of the parent unless overriden 
   * by options->env */
  char** env = environ;
  if (options->env != NULL)
    env = options->env;

  /* If options->file contains a slash, posix_spawn/posix_spawnp behave
   * the same, and don't involve PATH resolution at all. Otherwise, if
   * options->file does not include a slash, but no custom environment is 
   * to be used, the environment used for path resolution as well for the 
   * child process is that of the parent process, so posix_spawnp is the
   * way to go. */
  if (strchr(options->file, '/') != NULL || options->env == NULL)
    return posix_spawnp(pid, options->file, actions, attrs, options->args, env);

  /* Look for the definition of PATH in the provided env */
  path = uv__spawn_find_path_in_env(options->env);

  /* The following resolution logic (execvpe emulation) is taken from 
   * https://github.com/JuliaLang/libuv/commit/9af3af617138d6a6de7d72819ed362996ff255d9
   * and adapted to work around our own situations */

  /* If no path was provided in options->env, use the default value 
   * to look for the executable */
  if (path == NULL)
    path = _PATH_DEFPATH;

  k = strnlen(options->file, NAME_MAX + 1);
  if (k > NAME_MAX)
    return ENAMETOOLONG;

  l = strnlen(path, PATH_MAX - 1) + 1;

  for (p = path;; p = z) {
    /* Compose the new process file from the entry in the PATH
     * environment variable and the actual file name */
    char b[PATH_MAX + NAME_MAX];
    z = strchr(p, ':');
    if (!z) 
      z = p + strlen(p);
    if ((size_t)(z - p) >= l) {
      if (!*z++) 
        break;
      
      continue;
    }
    memcpy(b, p, z - p);
    b[z - p] = '/';
    memcpy(b + (z - p) + (z > p), options->file, k + 1);

    /* Try to spawn the new process file. If it fails with ENOENT, the
     * new process file is not in this PATH entry, continue with the next
     * PATH entry. */
    err = posix_spawn(pid, b, actions, attrs, options->args, env);
    if (err != ENOENT) 
      return err;

    if (!*z++) 
      break;
  }

  return err;
}


int uv__spawn_and_init_child_posix_spawn(const uv_process_options_t* options,
                                         int stdio_count,
                                         int (*pipes)[2], 
                                         pid_t* pid,
                                         const uv__posix_spawn_fncs_t* posix_spawn_fncs) {
  int err;
  posix_spawnattr_t attrs;
  posix_spawn_file_actions_t actions;

  err = uv__spawn_set_posix_spawn_attrs(&attrs, posix_spawn_fncs, options);
  if (err != 0) 
    goto error;

  err = uv__spawn_set_posix_spawn_file_actions(
    &actions,
    posix_spawn_fncs,
    options,
    stdio_count,
    pipes);
  if (err != 0) {
    (void) posix_spawnattr_destroy(&attrs);
    goto error;
  }

  /* Try to spawn options->file resolving in the provided environment 
   * if any */
  err = uv__spawn_resolve_and_spawn(options, &attrs, &actions, pid);

  /* Destroy the actions/attributes */
  (void) posix_spawn_file_actions_destroy(&actions);
  (void) posix_spawnattr_destroy(&attrs);

error:
  /* In an error situation, the attributes and file actions are 
   * already destroyed, only the happy path requires cleanup */
  return UV__ERR(err);
}
#endif

int uv__spawn_and_init_child_fork(const uv_process_options_t* options,
                                  int stdio_count,
                                  int (*pipes)[2],
                                  int error_fd,
                                  pid_t* pid) {
  *pid = fork();

  if (*pid == -1) {
    /* Failed to fork */
    return UV__ERR(errno);
  }

  if (*pid == 0) {
    /* Fork succeeded, in the child process */
    uv__process_child_init(options, stdio_count, pipes, error_fd);
    abort();
  }

  /* Fork succeeded, in the parent process */
  return 0;
}

int uv__spawn_and_init_child(const uv_process_options_t* options,
                             int stdio_count,
                             int (*pipes)[2],
                             int error_fd,
                             pid_t* pid) {
  int err;

#if defined(__APPLE__) 
  uv_once(&posix_spawn_init_once, uv__spawn_init_posix_spawn);

  /* Special child process spawn case for macOS Big Sur (11.0) onwards 
   *
   * Big Sur introduced a significant performance degradation on a call to
   * fork/exec when the process has many pages mmaped in with MAP_JIT, like, say
   * a javascript interpreter. Electron-based applications, for example,
   * are impacted; though the magnitude of the impact depends on how much the
   * app relies on subprocesses. 
   * 
   * On macOS, though, posix_spawn is implemented in a way that does not 
   * exhibit the problem. This block implements the forking and preparation
   * logic with posix_spawn and its related primitives. It also takes advantage of
   * the macOS extension POSIX_SPAWN_CLOEXEC_DEFAULT that makes impossible to
   * leak descriptors to the child process. */
  err = uv__spawn_and_init_child_posix_spawn(options,
                                              stdio_count,
                                              pipes,
                                              pid,
                                              &posix_spawn_fncs);

  /* The posix_spawn flow will return UV_ENOSYS if any of the posix_spawn_x_np
   * non-standard functions is both _needed_ and _undefined_. In those cases, 
   * default back to the fork/execve strategy. For all other errors, just fail. */
  if (err != UV_ENOSYS)
    return err;

#endif  
  err = uv__spawn_and_init_child_fork(options, stdio_count, pipes, error_fd, pid);

  return err;
}

int uv_spawn(uv_loop_t* loop,
             uv_process_t* process,
             const uv_process_options_t* options) {
#if defined(__APPLE__) && (TARGET_OS_TV || TARGET_OS_WATCH)
  /* fork is marked __WATCHOS_PROHIBITED __TVOS_PROHIBITED. */
  return UV_ENOSYS;
#else
  int signal_pipe[2] = { -1, -1 };
  int pipes_storage[8][2];
  int (*pipes)[2];
  int stdio_count;
  ssize_t r;
  pid_t pid;
  int err;
  int exec_errorno;
  int i;
  int status;

  assert(options->file != NULL);
  assert(!(options->flags & ~(UV_PROCESS_DETACHED |
                              UV_PROCESS_SETGID |
                              UV_PROCESS_SETUID |
                              UV_PROCESS_WINDOWS_HIDE |
                              UV_PROCESS_WINDOWS_HIDE_CONSOLE |
                              UV_PROCESS_WINDOWS_HIDE_GUI |
                              UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS)));

  uv__handle_init(loop, (uv_handle_t*)process, UV_PROCESS);
  QUEUE_INIT(&process->queue);

  stdio_count = options->stdio_count;
  if (stdio_count < 3)
    stdio_count = 3;

  err = UV_ENOMEM;
  pipes = pipes_storage;
  if (stdio_count > (int) ARRAY_SIZE(pipes_storage))
    pipes = uv__malloc(stdio_count * sizeof(*pipes));

  if (pipes == NULL)
    goto error;

  for (i = 0; i < stdio_count; i++) {
    pipes[i][0] = -1;
    pipes[i][1] = -1;
  }

  for (i = 0; i < options->stdio_count; i++) {
    err = uv__process_init_stdio(options->stdio + i, pipes[i]);
    if (err)
      goto error;
  }

  /* This pipe is used by the parent to wait until
   * the child has called `execve()`. We need this
   * to avoid the following race condition:
   *
   *    if ((pid = fork()) > 0) {
   *      kill(pid, SIGTERM);
   *    }
   *    else if (pid == 0) {
   *      execve("/bin/cat", argp, envp);
   *    }
   *
   * The parent sends a signal immediately after forking.
   * Since the child may not have called `execve()` yet,
   * there is no telling what process receives the signal,
   * our fork or /bin/cat.
   *
   * To avoid ambiguity, we create a pipe with both ends
   * marked close-on-exec. Then, after the call to `fork()`,
   * the parent polls the read end until it EOFs or errors with EPIPE.
   */
  err = uv__make_pipe(signal_pipe, 0);
  if (err)
    goto error;

  uv_signal_start(&loop->child_watcher, uv__chld, SIGCHLD);

  /* Acquire write lock to prevent opening new fds in worker threads */
  uv_rwlock_wrlock(&loop->cloexec_lock);

  /* Spawn the child */
  err = uv__spawn_and_init_child(options, stdio_count, pipes, signal_pipe[1], &pid);
  if (err != 0) {
    uv_rwlock_wrunlock(&loop->cloexec_lock);
    uv__close(signal_pipe[0]);
    uv__close(signal_pipe[1]);
    goto error;
  }

  /* Release lock in parent process */
  uv_rwlock_wrunlock(&loop->cloexec_lock);
  uv__close(signal_pipe[1]);

  process->status = 0;
  exec_errorno = 0;
  do
    r = read(signal_pipe[0], &exec_errorno, sizeof(exec_errorno));
  while (r == -1 && errno == EINTR);

  if (r == 0)
    ; /* okay, EOF */
  else if (r == sizeof(exec_errorno)) {
    do
      err = waitpid(pid, &status, 0); /* okay, read errorno */
    while (err == -1 && errno == EINTR);
    assert(err == pid);
  } else if (r == -1 && errno == EPIPE) {
    do
      err = waitpid(pid, &status, 0); /* okay, got EPIPE */
    while (err == -1 && errno == EINTR);
    assert(err == pid);
  } else
    abort();

  uv__close_nocheckstdio(signal_pipe[0]);

  for (i = 0; i < options->stdio_count; i++) {
    err = uv__process_open_stream(options->stdio + i, pipes[i]);
    if (err == 0)
      continue;

    while (i--)
      uv__process_close_stream(options->stdio + i);

    goto error;
  }

  /* Only activate this handle if exec() happened successfully */
  if (exec_errorno == 0) {
    QUEUE_INSERT_TAIL(&loop->process_handles, &process->queue);
    uv__handle_start(process);
  }

  process->pid = pid;
  process->exit_cb = options->exit_cb;

  if (pipes != pipes_storage)
    uv__free(pipes);

  return exec_errorno;

error:
  if (pipes != NULL) {
    for (i = 0; i < stdio_count; i++) {
      if (i < options->stdio_count)
        if (options->stdio[i].flags & (UV_INHERIT_FD | UV_INHERIT_STREAM))
          continue;
      if (pipes[i][0] != -1)
        uv__close_nocheckstdio(pipes[i][0]);
      if (pipes[i][1] != -1)
        uv__close_nocheckstdio(pipes[i][1]);
    }

    if (pipes != pipes_storage)
      uv__free(pipes);
  }

  return err;
#endif
}


int uv_process_kill(uv_process_t* process, int signum) {
  return uv_kill(process->pid, signum);
}


int uv_kill(int pid, int signum) {
  if (kill(pid, signum))
    return UV__ERR(errno);
  else
    return 0;
}


void uv__process_close(uv_process_t* handle) {
  QUEUE_REMOVE(&handle->queue);
  uv__handle_stop(handle);
  if (QUEUE_EMPTY(&handle->loop->process_handles))
    uv_signal_stop(&handle->loop->child_watcher);
}
