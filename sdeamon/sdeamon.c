#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>           // for fork, exec
#include <sys/types.h>
#include <sys/socket.h>       // for sockets
#include <netinet/in.h>       // for sockaddr_in
#include <arpa/inet.h>        // for inet_ntoa
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <sys/wait.h>
#include <libconfig.h>
#include <net/if.h>
#include <sys/ioctl.h>

#define PORT 5005
#define BACKLOG 10  // Number of allowed pending connections
#define INTERFACE_NAME "ens33"

// Data structure to store program information
struct ProgramData {
    char program_id[256];
    char root_process_arg[256];  // Assuming single argument
    char veth_host_ip[64];
    char veth_sandbox_ip[64];
    pid_t child_pid;             // Child process ID
    struct ProgramData *next;
};

struct ProgramData *program_list = NULL;  // Linked list head

// Function prototypes
void add_program(struct ProgramData *program);
struct ProgramData *find_program(const char *program_id);
void remove_program(const char *program_id);
void sigchld_handler(int s);
int parse_config_file(const char *config_path, struct ProgramData *program);
void check_error(int ret, const char *msg);
char* get_ip_address(const char *interface);

int main() {
    int sockfd, new_fd;
    struct sockaddr_in my_addr, their_addr;
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char buf[1024];
    int numbytes;

    char *vm_ip = get_ip_address(INTERFACE_NAME);

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    // Reuse address
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    // Bind
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(PORT);
    my_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(my_addr.sin_zero), '\0', 8);

    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1) {
        perror("bind");
        exit(1);
    }

    // Listen
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    // Handle dead processes
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("sdeamon: waiting for connections on port %d...\n", PORT);

    // Main loop
    while (1) {
        sin_size = sizeof(struct sockaddr_in);
        if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size)) == -1) {
            perror("accept");
            continue;
        }
        printf("sdeamon: got connection from %s\n", inet_ntoa(their_addr.sin_addr));

        // Receive data
        memset(buf, 0, sizeof(buf));
        if ((numbytes = recv(new_fd, buf, sizeof(buf) - 1, 0)) == -1) {
            perror("recv");
            close(new_fd);
            continue;
        }
        buf[numbytes] = '\0';

        printf("Received: %s\n", buf);

        // Parse request
        char *command = strtok(buf, " \n");
        char *program_id = strtok(NULL, " \n");

        if (command == NULL || program_id == NULL) {
            char *response = "error: invalid request format\n";
            send(new_fd, response, strlen(response), 0);
            close(new_fd);
            continue;
        }

        if (strcmp(command, "start") == 0) {
            // Start command
            char config_path[512];
            snprintf(config_path, sizeof(config_path), "/home/simurgan/Workspace/comicran/config/base_config_%s.cfg", program_id);
            struct ProgramData *program = (struct ProgramData *)malloc(sizeof(struct ProgramData));
            memset(program, 0, sizeof(struct ProgramData));
            strcpy(program->program_id, program_id);

            if (parse_config_file(config_path, program) != 0) {
                char response[512];
                snprintf(response, sizeof(response), "error: no base_config_%s.cfg found\n", program_id);
                send(new_fd, response, strlen(response), 0);
                free(program);
                close(new_fd);
                continue;
            }

            // Fork and execute /configer/configer
            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                free(program);
                close(new_fd);
                continue;
            } else if (pid == 0) {
                // Child process
                char full_config_path[512];
                snprintf(full_config_path, sizeof(full_config_path), "/home/simurgan/Workspace/comicran/config/full_config_%s.cfg", program_id);
                char *args[] = {"/home/simurgan/Workspace/comicran/configer/configer", config_path, full_config_path, NULL};
                execv("/home/simurgan/Workspace/comicran/configer/configer", args);
                perror("execv");
                exit(1);
            } else {
                // Parent process
                int status;
                waitpid(pid, &status, 0);

                // Fork and execute /jailor/jailor
                pid_t pid2 = fork();
                if (pid2 < 0) {
                    perror("fork");
                    free(program);
                    close(new_fd);
                    continue;
                } else if (pid2 == 0) {
                    // Child process
                    char full_config_path[512];
                    snprintf(full_config_path, sizeof(full_config_path), "/home/simurgan/Workspace/comicran/config/full_config_%s.cfg", program_id);
                    char *args[] = {"/home/simurgan/Workspace/comicran/jailor/jailor", full_config_path, NULL};
                    execv("/home/simurgan/Workspace/comicran/jailor/jailor", args);
                    perror("execv");
                    exit(1);
                } else {
                    // Parent process
                    char cmd[1024];
                    snprintf(cmd, sizeof(cmd), "iptables -t nat -A PREROUTING -p udp -d %s --dport %s -j DNAT --to-destination %s:%s", vm_ip, program->root_process_arg, program->veth_sandbox_ip, program->root_process_arg);
                    check_error(system(cmd), cmd);

                    snprintf(cmd, sizeof(cmd), "iptables -A FORWARD -p udp -d %s --dport %s -j ACCEPT", program->veth_sandbox_ip, program->root_process_arg);
                    check_error(system(cmd), cmd);

                    program->child_pid = pid2;
                    add_program(program);

                    // Respond to request
                    char response[512];
                    snprintf(response, sizeof(response), "success: the program%s has run\n", program_id);
                    send(new_fd, response, strlen(response), 0);
                }
            }
        } else if (strcmp(command, "stop") == 0) {
            // Stop command
            struct ProgramData *program = find_program(program_id);
            if (program == NULL) {
                char response[512];
                snprintf(response, sizeof(response), "error: program%s not found\n", program_id);
                send(new_fd, response, strlen(response), 0);
                close(new_fd);
                continue;
            }

            kill(program->child_pid, SIGTERM);

            // Wait for the child process to finish
            waitpid(program->child_pid, NULL, 0);

            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "iptables -t nat -D PREROUTING -p udp -d %s --dport %s -j DNAT --to-destination %s:%s", vm_ip, program->root_process_arg, program->veth_sandbox_ip, program->root_process_arg);
            check_error(system(cmd), cmd);

            snprintf(cmd, sizeof(cmd), "iptables -D FORWARD -p udp -d %s --dport %s -j ACCEPT", program->veth_sandbox_ip, program->root_process_arg);
            check_error(system(cmd), cmd);

            // Respond to request
            char response[512];
            snprintf(response, sizeof(response), "success: program%s terminated\n", program_id);
            send(new_fd, response, strlen(response), 0);

            // Remove program from list
            remove_program(program_id);
        } else {
            char *response = "error: invalid command\n";
            send(new_fd, response, strlen(response), 0);
        }

        close(new_fd);
    }

    return 0;
}

// Add program to the linked list
void add_program(struct ProgramData *program) {
    program->next = program_list;
    program_list = program;
}

// Find program by ID
struct ProgramData *find_program(const char *program_id) {
    struct ProgramData *curr = program_list;
    while (curr != NULL) {
        if (strcmp(curr->program_id, program_id) == 0) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

// Remove program from the linked list
void remove_program(const char *program_id) {
    struct ProgramData **curr = &program_list;
    while (*curr != NULL) {
        if (strcmp((*curr)->program_id, program_id) == 0) {
            struct ProgramData *temp = *curr;
            *curr = (*curr)->next;
            free(temp);
            return;
        }
        curr = &((*curr)->next);
    }
}

// Signal handler to reap zombie processes
void sigchld_handler(int s) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

// Parse the configuration file
int parse_config_file(const char *config_path, struct ProgramData *program) {
    config_t cfg;
    config_setting_t *setting;
    const char *str;
    int result = 0;

    // Initialize the configuration
    config_init(&cfg);

    // Read the file
    if (!config_read_file(&cfg, config_path)) {
        fprintf(stderr, "Error reading config file %s:%d - %s\n",
                config_error_file(&cfg),
                config_error_line(&cfg),
                config_error_text(&cfg));
        config_destroy(&cfg);
        return -1;
    }

    // Extract root_process_args
    setting = config_lookup(&cfg, "root_process_args");
    if (setting != NULL) {
        const char *arg = config_setting_get_string_elem(setting, 0);
        if (arg != NULL) {
            strcpy(program->root_process_arg, arg);
        } else {
            fprintf(stderr, "Error: root_process_args[0] is NULL or not a string.\n");
            result = -1;
        }
    } else {
        fprintf(stderr, "No 'root_process_args' setting in configuration file.\n");
        result = -1;
    }

    // Extract veth_ip_pair.host
    if (config_lookup_string(&cfg, "veth_ip_pair.host", &str)) {
        strcpy(program->veth_host_ip, str);
    } else {
        fprintf(stderr, "No 'veth_ip_pair.host' setting in configuration file.\n");
        result = -1;
    }

    // Extract veth_ip_pair.sandbox
    if (config_lookup_string(&cfg, "veth_ip_pair.sandbox", &str)) {
        strcpy(program->veth_sandbox_ip, str);
    } else {
        fprintf(stderr, "No 'veth_ip_pair.sandbox' setting in configuration file.\n");
        result = -1;
    }

    // Clean up
    config_destroy(&cfg);
    return result;
}

// Utility function for error checking
void check_error(int ret, const char *msg) {
    if (ret == -1) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

char* get_ip_address(const char *interface) {
    int fd;
    struct ifreq ifr;
    char *ip_address = (char *)malloc(INET_ADDRSTRLEN);

    // Create a socket to perform the ioctl call
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        free(ip_address);
        return NULL;
    }

    // Set the interface name in the ifreq structure
    strncpy(ifr.ifr_name, interface, IFNAMSIZ-1);

    // Perform the ioctl call to get the IP address
    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        perror("ioctl");
        close(fd);
        free(ip_address);
        return NULL;
    }

    // Extract the IP address
    struct sockaddr_in *ip_addr = (struct sockaddr_in *)&ifr.ifr_addr;
    strncpy(ip_address, inet_ntoa(ip_addr->sin_addr), INET_ADDRSTRLEN);

    close(fd);
    return ip_address;
}
