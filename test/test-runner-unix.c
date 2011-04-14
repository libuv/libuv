#include "test-runner-unix.h"
#include "test-runner.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <string.h>

#define PATHMAX 1024
static char executable_path[PATHMAX] = { '\0' };


/* Linux-only */
static void get_executable_path() {
  if (!executable_path[0]) {
    readlink("/proc/self/exe", executable_path, PATHMAX - 1);
  }
}


/* Invoke "arv[0] test-name". Store process info in *p. */
/* Make sure that all stdio output of the processes is buffered up. */
int process_start(char* name, process_info_t* p) {
  FILE* stdout_file = tmpfile();
  if (!stdout_file) {
    perror("tmpfile");
    return -1;
  }

  p->terminated = 0;

  get_executable_path();

  pid_t pid = vfork();

  if (pid < 0) {
    perror("vfork");
    return -1;
  }

  if (pid == 0) {
    /* child */
    dup2(fileno(stdout_file), STDOUT_FILENO);
    dup2(fileno(stdout_file), STDERR_FILENO);

    char* args[3] = { executable_path, name, NULL };
    execvp(executable_path, args);
    perror("execvp()");
    _exit(127);
  }

  /* parent */
  p->pid = pid;
  p->name = strdup(name);
  p->stdout_file = stdout_file;

  return 0;
}


/* Wait for all `n` processes in `vec` to terminate. */
/* Time out after `timeout` msec, or never if timeout == -1 */
/* Return 0 if all processes are terminated, -1 on error, -2 on timeout. */
int process_wait(process_info_t* vec, int n, int timeout) {
  int i;
  process_info_t* p;
  for (i = 0; i < n; i++) {
    p = (process_info_t*)(vec + i * sizeof(process_info_t));
    if (p->terminated) continue;
    int status = 0;
    int r = waitpid(p->pid, &p->status, 0);
    if (r < 0) {
      return -1;
    }
    p->terminated = 1;
  }
  return 0;
}


/* Returns the number of bytes in the stdio output buffer for process `p`. */
long int process_output_size(process_info_t *p) {
  /* Size of the p->stdout_file */
  struct stat buf;

  int r = fstat(fileno(p->stdout_file), &buf);
  if (r < 0) {
    return -1;
  }

  return (long)buf.st_size;
}


/* Copy the contents of the stdio output buffer to `fd`. */
int process_copy_output(process_info_t *p, int fd) {
  int r = fseek(p->stdout_file, 0, SEEK_SET);
  if (r < 0) {
    perror("fseek");
    return -1;
  }

  size_t nread, nwritten;
  char buf[1024];

  while ((nread = read(fileno(p->stdout_file), buf, 1024)) > 0) {
    nwritten = write(fd, buf, nread);
    /* TODO: what if write doesn't write the whole buffer... */
    if (nwritten < 0) {
      perror("write");
      return -1;
    }
  }

  if (nread < 0) {
    perror("read");
    return -1;
  }

  return 0;
}


/* Return the name that was specified when `p` was started by process_start */
char* process_get_name(process_info_t *p) {
  return p->name;
}


/* Terminate process `p`. */
int process_terminate(process_info_t *p) {
  return kill(p->pid, SIGTERM);
}


/* Return the exit code of process p. */
/* On error, return -1. */
int process_reap(process_info_t *p) {
  return WEXITSTATUS(p->status);
}


/* Clean up after terminating process `p` (e.g. free the output buffer etc.). */
void process_cleanup(process_info_t *p) {
  fclose(p->stdout_file);
  free(p->name);
}


/* Move the console cursor one line up and back to the first column. */
int rewind_cursor() {
  printf("\033[1A\033[80D");
}
