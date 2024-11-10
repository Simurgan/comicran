#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char const *argv[]) {
    // Check if IP and port are provided
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <Server IP> <Port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1]; // Server IP as argument

    // Parse and validate port number
    char *endptr;
    long port_num = strtol(argv[2], &endptr, 10);
    if (*endptr != '\0' || port_num <= 0 || port_num > 65535) {
        fprintf(stderr, "Invalid port number: %s\n", argv[2]);
        exit(EXIT_FAILURE);
    }
    const int PORT = (int)port_num;

    const int BUFFER_SIZE = 1024;
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char number[BUFFER_SIZE];
    socklen_t addr_len;

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set up server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    // Convert and validate the server IP address
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address: %s\n", server_ip);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    while (1) {
        // Prompt the user for a number
        printf("Enter a number (or type 'exit' to quit): ");
        fgets(number, BUFFER_SIZE, stdin);
        number[strcspn(number, "\n")] = '\0'; // Remove trailing newline

        // Check if the user wants to exit
        if (strcmp(number, "exit") == 0) {
            printf("Exiting...\n");
            break;
        }

        // Send the number to the server
        sendto(sockfd, number, strlen(number), 0,
               (const struct sockaddr *)&server_addr, sizeof(server_addr));

        // Receive the result from the server
        addr_len = sizeof(server_addr);
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                         (struct sockaddr *)&server_addr, &addr_len);
        if (n < 0) {
            perror("Receiving data failed");
            break;
        }
        buffer[n] = '\0';

        // Print the received result
        printf("Received square from server: %s\n", buffer);
    }

    close(sockfd); // Close the socket when done
    return 0;
}
