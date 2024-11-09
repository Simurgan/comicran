#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char const *argv[]) {
  const char *server_ip = "10.0.0.2"; // Server IP as argument
  const int PORT = 5006;
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
  inet_pton(AF_INET, server_ip, &server_addr.sin_addr); // Set server IP address

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
    buffer[n] = '\0';

    // Print the received result
    printf("Received square from server: %s\n", buffer);
  }

  close(sockfd); // Close the socket when done
  return 0;
}
