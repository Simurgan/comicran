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

#define STACK_SIZE (1024 * 1024) // Stack size for the child process

// Utility function for error checking
void check_error(int ret, const char *msg) {
  if (ret == -1) {
    perror(msg);
    exit(EXIT_FAILURE);
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

  int source_fd = open(src, O_RDONLY);
  check_error(source_fd, "open source file");

  int dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  check_error(dest_fd, "open dest file");

  char buffer[4096];
  ssize_t bytes_read, bytes_written;
  while ((bytes_read = read(source_fd, buffer, sizeof(buffer))) > 0) {
    bytes_written = write(dest_fd, buffer, bytes_read);
    if (bytes_written != bytes_read) {
      perror("write");
      exit(EXIT_FAILURE);
    }
  }

  check_error(close(source_fd), "close source file");
  check_error(close(dest_fd), "close dest file");
}

const char *root_dir;
config_setting_t *symlink_setting;

void connect_symlinks() {
  if (symlink_setting != NULL) {
    int count = config_setting_length(symlink_setting);
    int i;

    for (i = 0; i < count; ++i) {
      config_setting_t *s_l = config_setting_get_elem(symlink_setting, i);
      const char *sym, *dst;

      if (!(config_setting_lookup_string(s_l, "sym", &sym) &&
            config_setting_lookup_string(s_l, "dst", &dst)))
        continue;

      printf("%s --> %s\n", sym, dst);
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
  // Chroot to the sandbox directory
  check_error(chroot(root_dir), "chroot");
  check_error(chdir("/"), "chdir");

  connect_symlinks();
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
  sprintf(cmd, "ip link set veth1 netns %d", child_pid);
  ret = system(cmd);
  check_error(ret, "ip link set veth1 to child");

  // Set IP address and bring up veth0 on the host
  ret = system("ip addr add 192.168.1.1/24 dev veth0");
  check_error(ret, "ip addr add on host");
  ret = system("ip link set veth0 up");
  check_error(ret, "ip link set veth0 up");

  printf("Network in host namespace set up: veth0 with IP 192.168.1.1\n");
}

void cleanup_network() {
  system("ip link delete veth0");
  printf("Network cleaned up: veth0 and veth1 deleted\n");
}

int main(int argc, char *argv[]) {
  config_t cfg;
  config_init(&cfg);

  if (!config_read_file(&cfg, argv[1])) {
    fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
            config_error_line(&cfg), config_error_text(&cfg));
    config_destroy(&cfg);
    return (EXIT_FAILURE);
  }

  if (!config_lookup_string(&cfg, "root_dir", &root_dir))
    fprintf(stderr, "No 'root_dir' setting in configuration file.\n");

  struct stat st;
  // Check if the sandbox directory exists
  if (stat(root_dir, &st) == -1) {
    // If it doesn't exist, create it
    check_error(mkdir(root_dir, 0755), "mkdir sandbox");
  }

  // Create directories
  config_setting_t *setting;
  setting = config_lookup(&cfg, "directories");
  if (setting != NULL) {
    int count = config_setting_length(setting);
    int i;

    for (i = 0; i < count; ++i) {
      const char *dir = config_setting_get_string_elem(setting, i);
      char full_dir[255];
      sprintf(full_dir, "%s%s", root_dir, dir);

      // Check if the sandbox directory exists
      if (stat(full_dir, &st) == -1) {
        // If it doesn't exist, create it
        check_error(mkdir(full_dir, 0755), "mkdir full_dir");
      }
    }
  }

  // Copy files
  setting = config_lookup(&cfg, "file_copies");
  if (setting != NULL) {
    int count = config_setting_length(setting);
    int i;

    for (i = 0; i < count; ++i) {
      config_setting_t *f_c = config_setting_get_elem(setting, i);
      const char *src, *dst;

      if (!(config_setting_lookup_string(f_c, "src", &src) &&
            config_setting_lookup_string(f_c, "dst", &dst)))
        continue;

      char full_dir[255];
      sprintf(full_dir, "%s%s", root_dir, dst);
      copy_file(src, full_dir);
    }
  }

  // Get symlink configs
  symlink_setting = config_lookup(&cfg, "symlinks");

  char *stack = malloc(STACK_SIZE);
  check_error(stack == NULL ? -1 : 0, "malloc stack");

  // Unshare namespaces and create child process
  int child_pid =
      clone(child_func, stack + STACK_SIZE,
            CLONE_NEWNS | CLONE_NEWCGROUP | CLONE_NEWPID | CLONE_NEWIPC |
                CLONE_NEWNET | CLONE_NEWUTS | SIGCHLD,
            NULL);
  check_error(child_pid, "clone");

  // Set up the network on the host for the child process
  setup_network_in_host(child_pid);

  // Wait for the child process to finish
  check_error(waitpid(child_pid, NULL, 0), "waitpid");

  // Cleanup network interfaces
  cleanup_network();

  free(stack);
  return 0;
}
