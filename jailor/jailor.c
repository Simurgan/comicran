#define _GNU_SOURCE
#include <fcntl.h>
#include <libconfig.h>
#include <net/if.h>
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

// Function to configure networking inside the child namespace
void setup_network_in_child() {
    int ret;

    // Bring up the veth1 interface and set IP address
    ret = system("ip addr add 192.168.1.2/24 dev veth1");
    check_error(ret, "ip addr add in child");
    ret = system("ip link set veth1 up");
    check_error(ret, "ip link set veth1 up in child");
    ret = system("ip route add default via 192.168.1.1");
    check_error(ret, "ip route add in child");

    printf("Network in child namespace set up: veth1 with IP 192.168.1.2\n");
}

// Function that runs in the child process
int child_func(void *arg) {
    SandboxContext *ctx = (SandboxContext *)arg;

    // Chroot to the sandbox directory
    check_error(chroot(ctx->root_dir), "chroot");
    check_error(chdir("/"), "chdir");

    connect_symlinks(ctx);
    setup_network_in_child();

    // Execute bash inside the sandbox
    char *const bash_args[] = {"/bin/bash", NULL};
    check_error(execv("/bin/bash", bash_args), "execv /bin/bash");

    return 0;
}

// Function to set up veth pair on the host and move one end to child namespace
void setup_network_in_host(pid_t child_pid) {
    int ret;

    // Create a veth pair: veth0 on the host, veth1 to be moved to the child
    ret = system("ip link add veth0 type veth peer name veth1");
    check_error(ret, "ip link add veth pair");

    // Move veth1 to the child's network namespace
    char cmd[255];
    snprintf(cmd, sizeof(cmd), "ip link set veth1 netns %d", child_pid);
    ret = system(cmd);
    check_error(ret, "ip link set veth1 to child");

    // Set IP address and bring up veth0 on the host
    ret = system("ip addr add 192.168.1.1/24 dev veth0");
    check_error(ret, "ip addr add on host");
    ret = system("ip link set veth0 up");
    check_error(ret, "ip link set veth0 up");

    printf("Network in host namespace set up: veth0 with IP 192.168.1.1\n");
}

// Function to clean up network interfaces
void cleanup_network() {
    system("ip link delete veth0");
    printf("Network cleaned up: veth0 and veth1 deleted\n");
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

    // Allocate stack for child process
    char *stack = malloc(STACK_SIZE);
    check_error(stack == NULL ? -1 : 0, "malloc stack");

    // Unshare namespaces and create child process
    int child_pid = clone(child_func, stack + STACK_SIZE,
                          CLONE_NEWNS | CLONE_NEWCGROUP | CLONE_NEWPID | CLONE_NEWIPC |
                          CLONE_NEWNET | CLONE_NEWUTS | SIGCHLD,
                          &ctx);
    check_error(child_pid, "clone");

    // Set up the network on the host for the child process
    setup_network_in_host(child_pid);

    // Wait for the child process to finish
    check_error(waitpid(child_pid, NULL, 0), "waitpid");

    // Cleanup network interfaces
    cleanup_network();

    // Free resources
    free(stack);
    config_destroy(&(ctx.cfg));

    return 0;
}
