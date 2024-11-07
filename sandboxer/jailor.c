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
#include <unistd.h>
#include <string.h>
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

// Utility function for error checking
void check_error(int ret, const char *msg) {
    if (ret == -1) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

// Function to create a directory and its parent directories if they do not exist
void create_directory(const char *path) {
    char temp[256];
    char *p = NULL;
    size_t len;

    // Copy path to a temporary variable
    snprintf(temp, sizeof(temp), "%s/", path);
    len = strlen(temp);

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
    if (stat(dest, &buffer_stat) == 0) {
        printf("File '%s' already exists. Skipping copy.\n", dest);
        return; // Exit the function if the file exists
    }

    // Create the directory for the destination if it doesn't exist
    char dest_dir[256];
    strncpy(dest_dir, dest, sizeof(dest_dir));
    char *last_slash = strrchr(dest_dir, '/');
    if (last_slash != NULL) {
        *last_slash = '\0';  // Null-terminate to isolate the directory path
        create_directory(dest_dir);
        *last_slash = '/';  // Restore the original path
    }

    int source_fd = open(src, O_RDONLY);
    if (source_fd < 0) {
        perror("open source file");
        exit(EXIT_FAILURE);
    }

    int dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (dest_fd < 0) {
        fprintf(stderr, "Error opening destination file '%s': %s\n", dest, strerror(errno));
        close(source_fd);
        exit(EXIT_FAILURE);
    }

    char buffer[4096];
    ssize_t bytes_read, bytes_written;
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

// Function that runs in the child process
int child_func(void *arg) {
    SandboxContext *ctx = (SandboxContext *)arg;

    // Chroot to the sandbox directory
    check_error(chroot(ctx->root_dir), "chroot");
    check_error(chdir("/"), "chdir");

    connect_symlinks(ctx);

    // Prepare arguments for execv
    int argc = ctx->root_process_argc + 2; // +1 for the process name, +1 for NULL terminator
    char **args = malloc(sizeof(char*) * argc);
    if (!args) {
        perror("malloc args");
        exit(EXIT_FAILURE);
    }
    args[0] = ctx->root_process;
    for (int i = 0; i < ctx->root_process_argc; ++i) {
        args[i + 1] = ctx->root_process_args[i];
    }
    args[argc - 1] = NULL;

    // Execute root_process inside the sandbox
    check_error(execv(ctx->root_process, args), "execv root_process");

    return 0;
}

// Function to initialize configuration
void init_config(const char *config_file_path, SandboxContext *ctx) {
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
    if (!config_lookup_string(&(ctx->cfg), "root_process", &(ctx->root_process))) {
        fprintf(stderr, "No 'root_process' setting in configuration file.\n");
        config_destroy(&(ctx->cfg));
        exit(EXIT_FAILURE);
    }

    // Read root_process_args (optional)
    config_setting_t *args_setting = config_lookup(&(ctx->cfg), "root_process_args");
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
    config_setting_t *veth_setting = config_lookup(&(ctx->cfg), "veth_ip_pair");
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
    config_setting_t *setting = config_lookup(&(ctx->cfg), "directories");
    if (setting != NULL) {
        int count = config_setting_length(setting);

        for (int i = 0; i < count; ++i) {
            const char *dir = config_setting_get_string_elem(setting, i);
            char full_dir[255];
            snprintf(full_dir, sizeof(full_dir), "%s%s", ctx->root_dir, dir);

            struct stat st;
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
    config_setting_t *setting = config_lookup(&(ctx->cfg), "file_copies");
    if (setting != NULL) {
        int count = config_setting_length(setting);

        for (int i = 0; i < count; ++i) {
            config_setting_t *f_c = config_setting_get_elem(setting, i);
            const char *src, *dst;

            if (!(config_setting_lookup_string(f_c, "src", &src) &&
                  config_setting_lookup_string(f_c, "dst", &dst)))
                continue;

            char full_dest[255];
            snprintf(full_dest, sizeof(full_dest), "%s%s", ctx->root_dir, dst);
            copy_file(src, full_dest);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    SandboxContext ctx;

    // Initialize configuration
    init_config(argv[1], &ctx);

    // Check if the sandbox directory exists, if not create it
    struct stat st;
    if (stat(ctx.root_dir, &st) == -1) {
        check_error(mkdir(ctx.root_dir, 0755), "mkdir sandbox");
    }

    // Create directories as per configuration
    create_directories(&ctx);

    // Copy files as per configuration
    copy_files(&ctx);

    // Network configuration if veth_ip_pair is defined
    const int host_veth = 100 + ctx.sandbox_id;
    const int sandbox_veth = 200 + ctx.sandbox_id;
    char cmd[255];
    if (ctx.veth_ip_pair_defined) {
        // Enable IP forwarding
        check_error(system("echo 1 > /proc/sys/net/ipv4/ip_forward"), "echo 1 > /proc/sys/net/ipv4/ip_forward");

        // Create veth pair
        snprintf(cmd, sizeof(cmd), "ip link add veth%d type veth peer name veth%d", host_veth, sandbox_veth);
        check_error(system(cmd), cmd);

        snprintf(cmd, sizeof(cmd), "ip addr add %s/24 dev veth%d", ctx.veth_ip_pair.host, host_veth);
        check_error(system(cmd), cmd);

        snprintf(cmd, sizeof(cmd), "ip link set veth%d up", host_veth);
        check_error(system(cmd), cmd);

    }

    // Allocate stack for child process
    char *stack = malloc(STACK_SIZE);
    check_error(stack == NULL ? -1 : 0, "malloc stack");

    // Unshare namespaces and create child process
    int child_pid = clone(child_func, stack + STACK_SIZE,
                          CLONE_NEWNS | CLONE_NEWCGROUP | CLONE_NEWPID | CLONE_NEWIPC |
                          CLONE_NEWNET | CLONE_NEWUTS | SIGCHLD,
                          &ctx);
    check_error(child_pid, "clone");

    // Network configuration if veth_ip_pair is defined
    if (ctx.veth_ip_pair_defined) {
        snprintf(cmd, sizeof(cmd), "ip link set veth%d netns %d", sandbox_veth, child_pid);
        check_error(system(cmd), cmd);

        snprintf(cmd, sizeof(cmd), "nsenter --net=/proc/%d/ns/net ip addr add %s/24 dev veth%d", child_pid, ctx.veth_ip_pair.sandbox, sandbox_veth);
        check_error(system(cmd), cmd);

        snprintf(cmd, sizeof(cmd), "nsenter --net=/proc/%d/ns/net ip link set veth%d up", child_pid, sandbox_veth);
        check_error(system(cmd), cmd);

        snprintf(cmd, sizeof(cmd), "nsenter --net=/proc/%d/ns/net ip route add default via %s", child_pid, ctx.veth_ip_pair.host);
        check_error(system(cmd), cmd);
    }

    // Wait for the child process to finish
    check_error(waitpid(child_pid, NULL, 0), "waitpid");

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
