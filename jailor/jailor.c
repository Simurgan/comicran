/* jailor.c */
#define _GNU_SOURCE

#include <fcntl.h>
#include <libconfig.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/mount.h>
#include <unistd.h>
#include <string.h>
#include <linux/limits.h>
#include <errno.h>

#define STACK_SIZE (1024 * 1024) // Stack size for the child process

// Struct to hold the sandbox context
typedef struct {
    const char *root_dir;
    config_t cfg;
    config_setting_t *symlink_setting;
    int sandbox_id;
    char *root_process;
    char **root_process_args;
    int root_process_argc;
    struct {
        const char *host;
        const char *sandbox;
    } veth_ip_pair;
    int veth_ip_pair_defined;
} SandboxContext;

void check_error(int ret, const char *msg);
void terminate_child(int signo);
void create_directory(const char *path);
void copy_file(const char *src, const char *dest);
void connect_symlinks(SandboxContext *ctx);
int child_func(void *arg);
void init_config(const char *config_file_path, SandboxContext *ctx);
void create_directories(SandboxContext *ctx);
void copy_files(SandboxContext *ctx);

// Utility function for error checking
void check_error(int ret, const char *msg) {
    if (ret == -1) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

pid_t child_pid;

// Handle SIGUSR1
void terminate_child(int signo)
{
    (void)signo;
    if(child_pid > 0) {
        kill(child_pid, SIGTERM);
    }
}

// Function to create a directory and its parent directories if they do not exist
void create_directory(const char *path) {
    char temp[256];
    char *p = NULL;

    // Copy path to a temporary variable
    snprintf(temp, sizeof(temp), "%s/", path);

    // Create each directory in the path
    for (p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';  // Temporarily null-terminate
            if (mkdir(temp, 0755) == -1 && errno != EEXIST) {
                perror("Error creating directory");
                exit(EXIT_FAILURE);
            }
            *p = '/';  // Restore the null-terminator
        }
    }
}

// Function to copy a file into the sandbox
void copy_file(const char *src, const char *dest) {
    // Check if the destination file already exists
    struct stat buffer_stat;
    char buffer[4096];
    int source_fd;
    int dest_fd;
    ssize_t bytes_read, bytes_written;
    char *last_slash;

    // Create the directory for the destination if it doesn't exist
    char dest_dir[256];

    if (stat(dest, &buffer_stat) == 0) {
        printf("File '%s' already exists. Skipping copy.\n", dest);
        return; // Exit the function if the file exists
    }

    strncpy(dest_dir, dest, sizeof(dest_dir));
    last_slash = strrchr(dest_dir, '/');
    if (last_slash != NULL) {
        *last_slash = '\0';  // Null-terminate to isolate the directory path
        create_directory(dest_dir);
        *last_slash = '/';  // Restore the original path
    }

    source_fd = open(src, O_RDONLY);
    if (source_fd < 0) {
        perror("open source file");
        exit(EXIT_FAILURE);
    }

    dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (dest_fd < 0) {
        fprintf(stderr, "Error opening destination file '%s': %s\n", dest, strerror(errno));
        close(source_fd);
        exit(EXIT_FAILURE);
    }

    while ((bytes_read = read(source_fd, buffer, sizeof(buffer))) > 0) {
        bytes_written = write(dest_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            perror("write");
            close(source_fd);
            close(dest_fd);
            exit(EXIT_FAILURE);
        }
    }

    // Check for read errors
    if (bytes_read < 0) {
        perror("read");
    }

    // Close file descriptors
    close(source_fd);
    close(dest_fd);
}

// Function to connect symlinks
void connect_symlinks(SandboxContext *ctx) {
    config_setting_t *symlink_setting = ctx->symlink_setting;

    if (symlink_setting != NULL) {
        int count = config_setting_length(symlink_setting);

        for (int i = 0; i < count; ++i) {
            config_setting_t *s_l = config_setting_get_elem(symlink_setting, i);
            const char *sym, *dst;

            if (!(config_setting_lookup_string(s_l, "sym", &sym) &&
                  config_setting_lookup_string(s_l, "dst", &dst)))
                continue;

            check_error(symlink(dst, sym), "symlink");
        }
    }
}

// New chatgpt suggested childfunc (failed) to use pivot_root instead of chroot
int child_func(void *arg) {
    char old_root_path[PATH_MAX];
    int argc;
    char **args;
    SandboxContext *ctx = (SandboxContext *)arg;

    check_error(mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL), "mount / NULL");

    // Make ctx->root_dir a mount point with a bind mount
    check_error(mount(ctx->root_dir, ctx->root_dir, NULL, MS_BIND, NULL),
            "bind-mount root_dir");

    // Ensure you're root and have capabilities to pivot_root.
    // Create the old_root directory inside ctx->root_dir before pivot_root:
    // Inside that mount, create "old_root"
    snprintf(old_root_path, sizeof(old_root_path), "%s/old_root", ctx->root_dir);
    check_error(mkdir(old_root_path, 0777), "mkdir old_root");

    // pivot_root(new_root, put_old)
    // new_root: The new root directory (".")
    // put_old: The directory under new_root where the old root will be placed ("old_root")
    check_error(syscall(SYS_pivot_root, ctx->root_dir, old_root_path), "pivot_root to root_dir");

    // Now new root is ctx->root_dir. Move into it:
    check_error(chdir("/"), "chdir to /");

    // Unmount old_root to remove reference to the old root:
    check_error(umount2("/old_root", MNT_DETACH), "umount old_root");
    check_error(rmdir("/old_root"), "rmdir old_root");

    // At this point, pivot_root has replaced chroot functionality.
    // No need to call chroot again.

    connect_symlinks(ctx);

    // Prepare arguments for execv as before
    argc = ctx->root_process_argc + 2;
    args = malloc(sizeof(char*) * argc);
    if (!args) {
        perror("malloc args");
        exit(EXIT_FAILURE);
    }
    args[0] = ctx->root_process;
    for (int i = 0; i < ctx->root_process_argc; ++i) {
        args[i + 1] = ctx->root_process_args[i];
    }
    args[argc - 1] = NULL;

    check_error(execv(ctx->root_process, args), "execv root_process");

    return 0;
}

// Function to initialize configuration
void init_config(const char *config_file_path, SandboxContext *ctx) {
    config_setting_t *args_setting;
    config_setting_t *veth_setting;
    config_init(&(ctx->cfg));

    if (!config_read_file(&(ctx->cfg), config_file_path)) {
        fprintf(stderr, "%s:%d - %s\n", config_error_file(&(ctx->cfg)),
                config_error_line(&(ctx->cfg)), config_error_text(&(ctx->cfg)));
        config_destroy(&(ctx->cfg));
        exit(EXIT_FAILURE);
    }

    if (!config_lookup_string(&(ctx->cfg), "root_dir", &(ctx->root_dir))) {
        fprintf(stderr, "No 'root_dir' setting in configuration file.\n");
        config_destroy(&(ctx->cfg));
        exit(EXIT_FAILURE);
    }

    ctx->symlink_setting = config_lookup(&(ctx->cfg), "symlinks");

    // Read sandbox_id (mandatory)
    if (!config_lookup_int(&(ctx->cfg), "sandbox_id", &(ctx->sandbox_id))) {
        fprintf(stderr, "No 'sandbox_id' setting in configuration file.\n");
        config_destroy(&(ctx->cfg));
        exit(EXIT_FAILURE);
    }

    // Read root_process (default to "/bin/bash" if not specified)
    if (!config_lookup_string(&(ctx->cfg), "root_process", (const char **)&(ctx->root_process))) {
        fprintf(stderr, "No 'root_process' setting in configuration file.\n");
        config_destroy(&(ctx->cfg));
        exit(EXIT_FAILURE);
    }

    // Read root_process_args (optional)
    args_setting = config_lookup(&(ctx->cfg), "root_process_args");
    if (args_setting != NULL) {
        ctx->root_process_argc = config_setting_length(args_setting);
        ctx->root_process_args = malloc(sizeof(char*) * ctx->root_process_argc);
        if (!ctx->root_process_args) {
            perror("malloc root_process_args");
            config_destroy(&(ctx->cfg));
            exit(EXIT_FAILURE);
        }
        for (int i = 0; i < ctx->root_process_argc; ++i) {
            const char *arg = config_setting_get_string_elem(args_setting, i);
            ctx->root_process_args[i] = strdup(arg);
            if (!ctx->root_process_args[i]) {
                perror("strdup root_process_arg");
                config_destroy(&(ctx->cfg));
                exit(EXIT_FAILURE);
            }
        }
    } else {
        ctx->root_process_argc = 0;
        ctx->root_process_args = NULL;
    }

    // Read veth_ip_pair (optional)
    veth_setting = config_lookup(&(ctx->cfg), "veth_ip_pair");
    if (veth_setting != NULL) {
        if (!(config_setting_lookup_string(veth_setting, "host", &(ctx->veth_ip_pair.host)) &&
              config_setting_lookup_string(veth_setting, "sandbox", &(ctx->veth_ip_pair.sandbox)))) {
            fprintf(stderr, "veth_ip_pair must contain 'host' and 'sandbox'.\n");
            config_destroy(&(ctx->cfg));
            exit(EXIT_FAILURE);
        }
        ctx->veth_ip_pair_defined = 1;
    } else {
        ctx->veth_ip_pair_defined = 0;
    }
}

// Function to create directories from configuration
void create_directories(SandboxContext *ctx) {
    struct stat st;
    config_setting_t *setting = config_lookup(&(ctx->cfg), "directories");
    if (setting != NULL) {
        int count = config_setting_length(setting);

        for (int i = 0; i < count; ++i) {
            const char *dir = config_setting_get_string_elem(setting, i);
            char full_dir[255];
            snprintf(full_dir, sizeof(full_dir), "%s%s", ctx->root_dir, dir);

            // Check if the directory exists
            if (stat(full_dir, &st) == -1) {
                // If it doesn't exist, create it
                check_error(mkdir(full_dir, 0755), "mkdir full_dir");
            }
        }
    }
}

// Function to copy files from configuration
void copy_files(SandboxContext *ctx) {
    char full_dest[255];
    config_setting_t *setting = config_lookup(&(ctx->cfg), "file_copies");
    if (setting != NULL) {
        int count = config_setting_length(setting);

        for (int i = 0; i < count; ++i) {
            config_setting_t *f_c = config_setting_get_elem(setting, i);
            const char *src, *dst;

            if (!(config_setting_lookup_string(f_c, "src", &src) &&
                  config_setting_lookup_string(f_c, "dst", &dst)))
                continue;

            snprintf(full_dest, sizeof(full_dest), "%s%s", ctx->root_dir, dst);
            copy_file(src, full_dest);
        }
    }
}

int main(int argc, char *argv[]) {
    SandboxContext ctx;
    // Allocate stack for child process
    char *stack = malloc(STACK_SIZE);
    int host_veth;
    int sandbox_veth;
    char cmd[255];
    struct stat st;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Initialize configuration
    init_config(argv[1], &ctx);

    // Check if the sandbox directory exists, if not create it
    if (stat(ctx.root_dir, &st) == -1) {
        check_error(mkdir(ctx.root_dir, 0755), "mkdir sandbox");
    }

    // Create directories as per configuration
    create_directories(&ctx);

    // Copy files as per configuration
    copy_files(&ctx);

    // Network configuration if veth_ip_pair is defined
    host_veth = 100 + ctx.sandbox_id;
    sandbox_veth = 200 + ctx.sandbox_id;
    if (ctx.veth_ip_pair_defined) {
        // Enable IP forwarding
        check_error(system("echo 1 > /proc/sys/net/ipv4/ip_forward"), "echo 1 > /proc/sys/net/ipv4/ip_forward");

        // Create veth pair
        snprintf(cmd, sizeof(cmd), "ip link add veth%d type veth peer name veth%d", host_veth, sandbox_veth);
        check_error(system(cmd), cmd);

        snprintf(cmd, sizeof(cmd), "ip addr add %s/28 dev veth%d", ctx.veth_ip_pair.host, host_veth);
        check_error(system(cmd), cmd);

        snprintf(cmd, sizeof(cmd), "ip link set veth%d up", host_veth);
        check_error(system(cmd), cmd);

    }

    check_error(stack == NULL ? -1 : 0, "malloc stack");

    // Unshare namespaces and create child process
    child_pid = clone(child_func, stack + STACK_SIZE,
                          CLONE_NEWNS | CLONE_NEWCGROUP | CLONE_NEWPID | CLONE_NEWIPC |
                          CLONE_NEWNET | CLONE_NEWUTS | SIGCHLD,
                          &ctx);
    check_error(child_pid, "clone");

    // Network configuration if veth_ip_pair is defined
    if (ctx.veth_ip_pair_defined) {
        snprintf(cmd, sizeof(cmd), "ip link set veth%d netns %d", sandbox_veth, child_pid);
        check_error(system(cmd), cmd);

        snprintf(cmd, sizeof(cmd), "nsenter --net=/proc/%d/ns/net ip addr add %s/28 dev veth%d", child_pid, ctx.veth_ip_pair.sandbox, sandbox_veth);
        check_error(system(cmd), cmd);

        snprintf(cmd, sizeof(cmd), "nsenter --net=/proc/%d/ns/net ip link set veth%d up", child_pid, sandbox_veth);
        check_error(system(cmd), cmd);

        snprintf(cmd, sizeof(cmd), "nsenter --net=/proc/%d/ns/net ip route add default via %s", child_pid, ctx.veth_ip_pair.host);
        check_error(system(cmd), cmd);
    }


    signal(SIGTERM, terminate_child);

    // Wait for the child process to finish
    check_error(waitpid(child_pid, NULL, 0), "waitpid");

    umount2(ctx.root_dir, MNT_DETACH);

    snprintf(cmd, sizeof(cmd), "rm -rf %s", ctx.root_dir);
    check_error(system(cmd), cmd);

    // Free resources
    free(stack);
    if (ctx.root_process_args) {
        for (int i = 0; i < ctx.root_process_argc; ++i) {
            free(ctx.root_process_args[i]);
        }
        free(ctx.root_process_args);
    }
    config_destroy(&(ctx.cfg));

    return 0;
}
