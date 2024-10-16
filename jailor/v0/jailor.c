#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define SANDBOX_DIR "/tmp/sandbox2/"
#define BASH_PATH "/bin/bash"
#define STACK_SIZE (1024 * 1024) // Stack size for the child process

// Utility function for error checking
void check_error(int ret, const char *msg) {
  if (ret == -1) {
    perror(msg);
    exit(EXIT_FAILURE);
  }
}

// Function that runs in the child process
int child_func(void *arg) {
  // setup_sandbox();

  // Chroot to the sandbox directory
  check_error(chroot(SANDBOX_DIR), "chroot");
  check_error(chdir("/"), "chdir");

  // Execute bash inside the sandbox
  char *const bash_args[] = {"/hello", NULL};
  check_error(execv("/hello", bash_args), "execv /hello");

  return 0;
}

int main() {
  char *stack = malloc(STACK_SIZE);
  check_error(stack == NULL ? -1 : 0, "malloc stack");

  // Unshare namespaces and create child process
  int child_pid =
      clone(child_func, stack + STACK_SIZE,
            CLONE_NEWNS | CLONE_NEWCGROUP | CLONE_NEWPID | CLONE_NEWIPC |
                CLONE_NEWNET | CLONE_NEWUTS | SIGCHLD,
            NULL);
  check_error(child_pid, "clone");

  // Wait for the child process to finish
  check_error(waitpid(child_pid, NULL, 0), "waitpid");

  free(stack);
  return 0;
}
