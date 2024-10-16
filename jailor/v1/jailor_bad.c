#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sched.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <libcgroup.h> // Include the libcgroup library

#define SANDBOX_DIR "./sandbox"
#define BASH_PATH "/bin/bash"
#define STACK_SIZE (1024 * 1024) // Stack size for the child process

// Utility function for error checking
void check_error(int ret, const char *msg) {
    if (ret == -1) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

// Function to copy the bash binary into the sandbox
void copy_bash(const char *src, const char *dest) {
    int source_fd = open(src, O_RDONLY);
    check_error(source_fd, "open source bash");

    int dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    check_error(dest_fd, "open dest bash");

    char buffer[4096];
    ssize_t bytes_read, bytes_written;
    while ((bytes_read = read(source_fd, buffer, sizeof(buffer))) > 0) {
        bytes_written = write(dest_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            perror("write");
            exit(EXIT_FAILURE);
        }
    }

    check_error(close(source_fd), "close source bash");
    check_error(close(dest_fd), "close dest bash");
}

// Function to setup sandbox and bind mount directories
void setup_sandbox() {
    // Create the necessary directories inside the sandbox
    check_error(mkdir(SANDBOX_DIR, 0755), "mkdir sandbox");
    check_error(mkdir(SANDBOX_DIR "/bin", 0755), "mkdir sandbox/bin");
    check_error(mkdir(SANDBOX_DIR "/usr", 0755), "mkdir sandbox/usr");
    check_error(mkdir(SANDBOX_DIR "/lib", 0755), "mkdir sandbox/lib");
    check_error(mkdir(SANDBOX_DIR "/lib32", 0755), "mkdir sandbox/lib32");
    check_error(mkdir(SANDBOX_DIR "/lib64", 0755), "mkdir sandbox/lib64");
    check_error(mkdir(SANDBOX_DIR "/sys", 0755), "mkdir sandbox/sys");         // Create ./sandbox/sys
    check_error(mkdir(SANDBOX_DIR "/sys/fs", 0755), "mkdir sandbox/sys/fs");   // Create ./sandbox/sys/fs
    check_error(mkdir(SANDBOX_DIR "/sys/fs/cgroup", 0755), "mkdir sandbox/sys/fs/cgroup"); // Create ./sandbox/sys/fs/cgroup

    // Copy /bin/bash to the sandbox
    copy_bash(BASH_PATH, SANDBOX_DIR "/bin/bash");

    // Bind mount /usr, /lib, /lib32, /lib64, and cgroup directories as read-only directly
    check_error(mount("/usr", SANDBOX_DIR "/usr", NULL, MS_BIND | MS_RDONLY, NULL), "mount /usr read-only");
    check_error(mount("/lib", SANDBOX_DIR "/lib", NULL, MS_BIND | MS_RDONLY, NULL), "mount /lib read-only");
    check_error(mount("/lib32", SANDBOX_DIR "/lib32", NULL, MS_BIND | MS_RDONLY, NULL), "mount /lib32 read-only");
    check_error(mount("/lib64", SANDBOX_DIR "/lib64", NULL, MS_BIND | MS_RDONLY, NULL), "mount /lib64 read-only");
    check_error(mount("/sys/fs/cgroup", SANDBOX_DIR "/sys/fs/cgroup", NULL, MS_BIND, NULL), "mount /sys/fs/cgroup");  // cgroup not read-only
}

// Function to configure cgroups using libcgroup
void configure_cgroups(int child_pid) {
    struct cgroup *cgroup;
    struct cgroup_controller *controller;

    // Initialize libcgroup
    check_error(cgroup_init(), "cgroup_init");

    // Create a new cgroup named "sandbox"
    cgroup = cgroup_new_cgroup("sandbox");
    if (cgroup == NULL) {
        fprintf(stderr, "Error creating cgroup\n");
        exit(EXIT_FAILURE);
    }

    // Add the memory controller
    controller = cgroup_add_controller(cgroup, "memory");
    if (controller == NULL) {
        fprintf(stderr, "Error adding memory controller\n");
        exit(EXIT_FAILURE);
    }

    // Set memory limit for the cgroup to 512MB
    check_error(cgroup_set_value_uint64(controller, "memory.limit_in_bytes", 512 * 1024 * 1024), "set memory limit");

    // Add the child process to the cgroup
    check_error(cgroup_attach_task_pid(controller, child_pid), "attach task to cgroup");

    // Commit the cgroup configuration to the kernel
    check_error(cgroup_create_cgroup(cgroup, 0), "create cgroup");
}

// Function that runs in the child process
int child_func(void *arg) {
    setup_sandbox();

    // Chroot to the sandbox
    check_error(chroot(SANDBOX_DIR), "chroot");
    check_error(chdir("/"), "chdir");

    // Execute bash inside the sandbox
    char *const bash_args[] = {"/bin/bash", NULL};
    check_error(execv("/bin/bash", bash_args), "execv /bin/bash");

    return 0;
}

int main() {
    char *stack = malloc(STACK_SIZE);
    check_error(stack == NULL ? -1 : 0, "malloc stack");

    // Unshare namespaces and create child process
    int child_pid = clone(child_func, stack + STACK_SIZE, CLONE_NEWNS | CLONE_NEWCGROUP | CLONE_NEWPID | CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWUTS | SIGCHLD, NULL);
    check_error(child_pid, "clone");

    // Configure cgroups for the child process
    configure_cgroups(child_pid);

    // Wait for the child process to finish
    check_error(waitpid(child_pid, NULL, 0), "waitpid");

    free(stack);
    return 0;
}
