#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // for close()
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <errno.h>
#include <pthread.h>
#include <ctype.h>

#define PORT 5001  // Port number to listen on
#define BUFFER_SIZE 1024
#define MAX_RULES 100

struct rule {
    int destination_number;
    char ip[INET_ADDRSTRLEN];
    int port;
};

struct rule rules[MAX_RULES];
int rule_count = 0;
pthread_mutex_t rules_mutex = PTHREAD_MUTEX_INITIALIZER;

struct thread_args {
    int sockfd;
    struct sockaddr_in client_addr;
    socklen_t addr_len;
    char buffer[BUFFER_SIZE];
};

void *handle_request(void *args);
void handle_set_command(int sockfd, char *buffer, struct sockaddr_in *client_addr, socklen_t addr_len);
void handle_unset_command(int sockfd, char *buffer, struct sockaddr_in *client_addr, socklen_t addr_len);
void handle_normal_message(int sockfd, char *buffer, struct sockaddr_in *client_addr, socklen_t addr_len);

int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE];
    socklen_t addr_len;
    int n;

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Fill server information
    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));

    server_addr.sin_family = AF_INET;  // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind the socket with the server address
    if (bind(sockfd, (const struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("SDN server is listening on port %d\n", PORT);

    // Initialize lookup table
    memset(rules, 0, sizeof(rules));

    // Enter loop to receive messages
    while (1) {
        addr_len = sizeof(client_addr);
        n = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0,
                     (struct sockaddr *)&client_addr, &addr_len);
        if (n < 0) {
            perror("recvfrom error");
            continue;
        }
        buffer[n] = '\0';  // Null-terminate the buffer

        printf("Received message: %s\n", buffer);

        // Create a new thread to handle the request
        pthread_t thread_id;
        struct thread_args *args = malloc(sizeof(struct thread_args));
        if (args == NULL) {
            perror("malloc failed");
            continue;
        }
        args->sockfd = sockfd;
        args->client_addr = client_addr;
        args->addr_len = addr_len;
        strncpy(args->buffer, buffer, BUFFER_SIZE);

        if (pthread_create(&thread_id, NULL, handle_request, (void *)args) != 0) {
            perror("pthread_create failed");
            free(args);
            continue;
        }

        // Detach the thread so that resources are freed when it finishes
        pthread_detach(thread_id);
    }

    close(sockfd);
    pthread_mutex_destroy(&rules_mutex);
    return 0;
}

void *handle_request(void *args) {
    struct thread_args *t_args = (struct thread_args *)args;
    int sockfd = t_args->sockfd;
    struct sockaddr_in client_addr = t_args->client_addr;
    socklen_t addr_len = t_args->addr_len;
    char *buffer = t_args->buffer;

    // Handle the message
    if (strncmp(buffer, "set ", 4) == 0) {
        // Handle set command
        handle_set_command(sockfd, buffer, &client_addr, addr_len);
    } else if (strncmp(buffer, "unset ", 6) == 0) {
        // Handle unset command
        handle_unset_command(sockfd, buffer, &client_addr, addr_len);
    } else {
        // Handle normal message
        handle_normal_message(sockfd, buffer, &client_addr, addr_len);
    }

    free(args);
    return NULL;
}

void handle_set_command(int sockfd, char *buffer, struct sockaddr_in *client_addr, socklen_t addr_len) {
    char *token;
    int destination_number;
    char ip_port[BUFFER_SIZE];

    // Tokenize the buffer
    token = strtok(buffer, " ");  // "set"
    token = strtok(NULL, " ");    // destination_number
    if (token == NULL) {
        // Invalid format
        sendto(sockfd, "Invalid set command format", strlen("Invalid set command format"), 0,
               (struct sockaddr *)client_addr, addr_len);
        return;
    }
    destination_number = atoi(token);

    token = strtok(NULL, " ");    // ip:port
    if (token == NULL) {
        // Invalid format
        sendto(sockfd, "Invalid set command format", strlen("Invalid set command format"), 0,
               (struct sockaddr *)client_addr, addr_len);
        return;
    }
    strcpy(ip_port, token);

    // Parse ip and port
    char *colon = strchr(ip_port, ':');
    if (colon == NULL) {
        // Invalid format
        sendto(sockfd, "Invalid IP:port format", strlen("Invalid IP:port format"), 0,
               (struct sockaddr *)client_addr, addr_len);
        return;
    }
    *colon = '\0';  // Split ip_port into ip and port
    char *ip = ip_port;
    char *port_str = colon + 1;
    int port = atoi(port_str);

    // Lock the rules mutex before modifying the lookup table
    pthread_mutex_lock(&rules_mutex);

    // Check if the destination_number already exists
    int i;
    for (i = 0; i < rule_count; i++) {
        if (rules[i].destination_number == destination_number) {
            // Update existing rule
            strcpy(rules[i].ip, ip);
            rules[i].port = port;
            pthread_mutex_unlock(&rules_mutex);
            sendto(sockfd, "done", strlen("done"), 0,
                   (struct sockaddr *)client_addr, addr_len);
            return;
        }
    }
    // Add new rule
    if (rule_count >= MAX_RULES) {
        pthread_mutex_unlock(&rules_mutex);
        sendto(sockfd, "Rule limit reached", strlen("Rule limit reached"), 0,
               (struct sockaddr *)client_addr, addr_len);
        return;
    }
    rules[rule_count].destination_number = destination_number;
    strcpy(rules[rule_count].ip, ip);
    rules[rule_count].port = port;
    rule_count++;

    pthread_mutex_unlock(&rules_mutex);

    sendto(sockfd, "done", strlen("done"), 0,
           (struct sockaddr *)client_addr, addr_len);
}

void handle_unset_command(int sockfd, char *buffer, struct sockaddr_in *client_addr, socklen_t addr_len) {
    char *token;
    int destination_number;

    // Tokenize the buffer
    token = strtok(buffer, " ");  // "unset"
    token = strtok(NULL, " ");    // destination_number
    if (token == NULL) {
        // Invalid format
        sendto(sockfd, "Invalid unset command format", strlen("Invalid unset command format"), 0,
               (struct sockaddr *)client_addr, addr_len);
        return;
    }
    destination_number = atoi(token);

    // Lock the rules mutex before modifying the lookup table
    pthread_mutex_lock(&rules_mutex);

    // Find and remove the rule
    int i;
    for (i = 0; i < rule_count; i++) {
        if (rules[i].destination_number == destination_number) {
            // Remove the rule by shifting the rest
            int j;
            for (j = i; j < rule_count - 1; j++) {
                rules[j] = rules[j + 1];
            }
            rule_count--;
            pthread_mutex_unlock(&rules_mutex);
            sendto(sockfd, "done", strlen("done"), 0,
                   (struct sockaddr *)client_addr, addr_len);
            return;
        }
    }

    pthread_mutex_unlock(&rules_mutex);

    // Rule not found
    sendto(sockfd, "done", strlen("done"), 0,
           (struct sockaddr *)client_addr, addr_len);
}

void handle_normal_message(int sockfd, char *buffer, struct sockaddr_in *client_addr, socklen_t addr_len) {
    char *token;
    int destination_number;
    char number_str[BUFFER_SIZE];

    // Tokenize the buffer
    token = strtok(buffer, " ");  // destination_number
    if (token == NULL) {
        // Invalid format
        sendto(sockfd, "Invalid message format", strlen("Invalid message format"), 0,
               (struct sockaddr *)client_addr, addr_len);
        return;
    }
    destination_number = atoi(token);

    token = strtok(NULL, " ");    // number
    if (token == NULL) {
        // Invalid format
        sendto(sockfd, "Invalid message format", strlen("Invalid message format"), 0,
               (struct sockaddr *)client_addr, addr_len);
        return;
    }
    strcpy(number_str, token);

    // Lock the rules mutex before accessing the lookup table
    pthread_mutex_lock(&rules_mutex);

    // Look up the destination_number
    int i;
    struct rule found_rule;
    int rule_found = 0;
    for (i = 0; i < rule_count; i++) {
        if (rules[i].destination_number == destination_number) {
            // Found the rule
            found_rule = rules[i];
            rule_found = 1;
            break;
        }
    }

    pthread_mutex_unlock(&rules_mutex);

    if (!rule_found) {
        // Rule not found
        char reply[BUFFER_SIZE];
        snprintf(reply, BUFFER_SIZE, "no rule found for %d", destination_number);
        sendto(sockfd, reply, strlen(reply), 0,
               (struct sockaddr *)client_addr, addr_len);
        return;
    }

    // Send the number to the destination
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(found_rule.port);
    if (inet_pton(AF_INET, found_rule.ip, &dest_addr.sin_addr) <= 0) {
        perror("Invalid destination IP address");
        sendto(sockfd, "Invalid destination IP address", strlen("Invalid destination IP address"), 0,
               (struct sockaddr *)client_addr, addr_len);
        return;
    }
    // Send the number to the destination
    int n = sendto(sockfd, number_str, strlen(number_str), 0,
                   (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (n < 0) {
        perror("sendto to destination failed");
        sendto(sockfd, "Failed to send to destination", strlen("Failed to send to destination"), 0,
               (struct sockaddr *)client_addr, addr_len);
        return;
    }
    // Wait for a reply from the destination
    // Set a timeout for the socket
    struct timeval tv;
    tv.tv_sec = 5;  // 5 seconds timeout
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    char dest_buffer[BUFFER_SIZE];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    n = recvfrom(sockfd, dest_buffer, BUFFER_SIZE - 1, 0,
                 (struct sockaddr *)&from_addr, &from_len);
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            // Timeout
            sendto(sockfd, "No response from destination", strlen("No response from destination"), 0,
                   (struct sockaddr *)client_addr, addr_len);
        } else {
            perror("recvfrom from destination failed");
            sendto(sockfd, "Failed to receive from destination", strlen("Failed to receive from destination"), 0,
                   (struct sockaddr *)client_addr, addr_len);
        }
        // Reset socket timeout
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
        return;
    }
    dest_buffer[n] = '\0';

    // Verify that the reply is from the intended destination
    if (from_addr.sin_addr.s_addr != dest_addr.sin_addr.s_addr ||
        from_addr.sin_port != dest_addr.sin_port) {
        // Not from the intended destination
        printf("Received reply from unexpected source\n");
        sendto(sockfd, "Received reply from unexpected source", strlen("Received reply from unexpected source"), 0,
               (struct sockaddr *)client_addr, addr_len);
        // Reset socket timeout
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
        return;
    }

    // Send the reply back to the client
    sendto(sockfd, dest_buffer, strlen(dest_buffer), 0,
           (struct sockaddr *)client_addr, addr_len);

    // Reset socket timeout
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
}
