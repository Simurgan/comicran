#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>         // for close()
#include <arpa/inet.h>      // for sockaddr_in, inet_addr()
#include <netinet/in.h>     // for sockaddr_in
#include <sys/socket.h>     // for socket functions

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 5005
#define BUFFER_SIZE 1024

int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    char send_buf[BUFFER_SIZE];
    char recv_buf[BUFFER_SIZE];
    int bytes_sent, bytes_received;
    
    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }
    
    // Server address setup
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if(inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid server IP address");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Error connecting to server");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    printf("Connected to sdeamon server at %s:%d\n", SERVER_IP, SERVER_PORT);
    
    while (1) {
        // Prompt user for input
        printf("Enter command (e.g., start 1, stop 2, or 'quit' to exit): ");
        if (fgets(send_buf, sizeof(send_buf), stdin) == NULL) {
            perror("Error reading input");
            break;
        }
        
        // Remove trailing newline character
        size_t len = strlen(send_buf);
        if (len > 0 && send_buf[len - 1] == '\n') {
            send_buf[len - 1] = '\0';
        }
        
        // Check if user wants to quit
        if (strcmp(send_buf, "quit") == 0) {
            printf("Exiting client.\n");
            break;
        }
        
        // Send command to server
        bytes_sent = send(sockfd, send_buf, strlen(send_buf), 0);
        if (bytes_sent == -1) {
            perror("Error sending data to server");
            break;
        }
        
        // Receive response from server
        memset(recv_buf, 0, sizeof(recv_buf));
        bytes_received = recv(sockfd, recv_buf, sizeof(recv_buf) - 1, 0);
        if (bytes_received == -1) {
            perror("Error receiving data from server");
            break;
        } else if (bytes_received == 0) {
            printf("Server closed the connection.\n");
            break;
        }
        
        recv_buf[bytes_received] = '\0';  // Null-terminate the received data
        
        // Display server response
        printf("Server response: %s\n", recv_buf);
        
        // Close and reopen the socket for the next command
        close(sockfd);
        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            perror("Error creating socket");
            exit(EXIT_FAILURE);
        }
        if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
            perror("Error connecting to server");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }
    
    // Close socket
    close(sockfd);
    return 0;
}
