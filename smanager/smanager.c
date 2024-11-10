#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

#define MAX_VMS 100
#define BUFFER_SIZE 1024

typedef struct {
    int vm_number;
    char ip_address[INET_ADDRSTRLEN];
    int port;
} VMConfig;

VMConfig vm_list[MAX_VMS];
int vm_count = 0;

void trim_newline(char *str);
int find_vm(int vm_number);
void add_or_update_vm(int vm_number, const char *ip_address, int port);
void remove_vm(int vm_number);
void send_command(int vm_number, const char *command, const char *server_number);

int main() {
    char input[BUFFER_SIZE];

    while (1) {
        printf("smanager> ");
        if (fgets(input, BUFFER_SIZE, stdin) == NULL) {
            break;
        }
        trim_newline(input);

        if (strncmp(input, "set vm", 6) == 0) {
            int vm_number, port;
            char ip_address[INET_ADDRSTRLEN];
            if (sscanf(input, "set vm %d %15s %d", &vm_number, ip_address, &port) == 3) {
                add_or_update_vm(vm_number, ip_address, port);
            } else {
                printf("Invalid command format.\n");
            }
        } else if (strncmp(input, "unset", 5) == 0) {
            int vm_number;
            if (sscanf(input, "unset %d", &vm_number) == 1) {
                remove_vm(vm_number);
            } else {
                printf("Invalid command format.\n");
            }
        } else if (strncmp(input, "start vm", 8) == 0) {
            int vm_number;
            char server_number[BUFFER_SIZE];
            if (sscanf(input, "start vm %d server %s", &vm_number, server_number) == 2) {
                send_command(vm_number, "start", server_number);
            } else {
                printf("Invalid command format.\n");
            }
        } else if (strncmp(input, "stop vm", 7) == 0) {
            int vm_number;
            char server_number[BUFFER_SIZE];
            if (sscanf(input, "stop vm %d server %s", &vm_number, server_number) == 2) {
                send_command(vm_number, "stop", server_number);
            } else {
                printf("Invalid command format.\n");
            }
        } else {
            printf("Unknown command.\n");
        }
    }
    return 0;
}

void trim_newline(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
}

int find_vm(int vm_number) {
    for (int i = 0; i < vm_count; i++) {
        if (vm_list[i].vm_number == vm_number) {
            return i;
        }
    }
    return -1;
}

void add_or_update_vm(int vm_number, const char *ip_address, int port) {
    int index = find_vm(vm_number);
    if (index >= 0) {
        // Update existing VM
        strcpy(vm_list[index].ip_address, ip_address);
        vm_list[index].port = port;
        printf("vm successfully updated\n");
    } else {
        // Add new VM
        if (vm_count < MAX_VMS) {
            vm_list[vm_count].vm_number = vm_number;
            strcpy(vm_list[vm_count].ip_address, ip_address);
            vm_list[vm_count].port = port;
            vm_count++;
            printf("vm successfully registered\n");
        } else {
            printf("VM list is full.\n");
        }
    }
}

void remove_vm(int vm_number) {
    int index = find_vm(vm_number);
    if (index >= 0) {
        // Remove VM by shifting the array
        for (int i = index; i < vm_count - 1; i++) {
            vm_list[i] = vm_list[i + 1];
        }
        vm_count--;
        printf("vm successfully removed\n");
    } else {
        printf("there is no such a vm\n");
    }
}

void send_command(int vm_number, const char *command, const char *server_number) {
    int index = find_vm(vm_number);
    if (index < 0) {
        printf("VM not found.\n");
        return;
    }

    VMConfig *vm = &vm_list[index];
    int sockfd;
    struct sockaddr_in server_addr;
    char send_buffer[BUFFER_SIZE];
    char recv_buffer[BUFFER_SIZE];

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        return;
    }

    // Setup server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(vm->port);
    if (inet_pton(AF_INET, vm->ip_address, &server_addr.sin_addr) <= 0) {
        perror("Invalid IP address");
        close(sockfd);
        return;
    }

    // Connect to VM
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sockfd);
        return;
    }

    // Prepare and send command
    snprintf(send_buffer, BUFFER_SIZE, "%s %s", command, server_number);
    if (send(sockfd, send_buffer, strlen(send_buffer), 0) < 0) {
        perror("Send failed");
        close(sockfd);
        return;
    }

    // Receive response
    int bytes_received = recv(sockfd, recv_buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received < 0) {
        perror("Receive failed");
    } else {
        recv_buffer[bytes_received] = '\0';
        printf("%s\n", recv_buffer);
    }

    // Close socket
    close(sockfd);
}
