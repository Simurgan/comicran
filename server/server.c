#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char const *argv[])
{
    const int PORT = atoi(argv[1]);
    const int BUFFER_SIZE = 1024;
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE];
    socklen_t client_len = sizeof(client_addr);

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set up server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind the socket to the server address
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("UDP Server is up and listening on port %d...\n", PORT);

    while (1)
    {
        // Receive data from client
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &client_len);
        buffer[n] = '\0';
        printf("Received number: %s\n", buffer);

        // Convert received data to integer and compute square
        int number = atoi(buffer);
        int result = number * number;

        // Prepare the result to send back
        snprintf(buffer, sizeof(buffer), "%d", result);

        // Send the square of the number back to the client
        sendto(sockfd, buffer, strlen(buffer), 0, (const struct sockaddr *)&client_addr, client_len);
        printf("Sent result: %s\n", buffer);
    }

    close(sockfd);
    return 0;
}
