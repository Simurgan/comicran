/*************************************************************
 * File: prime_udp_server.c
 * Compile with: gcc prime_udp_server.c -o prime_udp_server
 *************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 5005
#define BUFFER_SIZE 1024

int main() {
    // 1. Hardcode the first 100 prime numbers in an array.
    int primes[100] = {
        2, 3, 5, 7, 11, 13, 17, 19, 23, 29,
        31, 37, 41, 43, 47, 53, 59, 61, 67, 71,
        73, 79, 83, 89, 97, 101, 103, 107, 109, 113,
        127, 131, 137, 139, 149, 151, 157, 163, 167, 173,
        179, 181, 191, 193, 197, 199, 211, 223, 227, 229,
        233, 239, 241, 251, 257, 263, 269, 271, 277, 281,
        283, 293, 307, 311, 313, 317, 331, 337, 347, 349,
        353, 359, 367, 373, 379, 383, 389, 397, 401, 409,
        419, 421, 431, 433, 439, 443, 449, 457, 461, 463,
        467, 479, 487, 491, 499, 503, 509, 521, 523, 541
    };

    // 2. Create a UDP socket.
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket() failed");
        return EXIT_FAILURE;
    }

    // 3. Bind to port 5005 on any local address.
    struct sockaddr_in server_addr, client_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(PORT);

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind() failed");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("UDP server listening on port %d...\n", PORT);

    // 4. Main loop to receive, process, and respond.
    while (1) {
        char buffer[BUFFER_SIZE];
        socklen_t client_len = sizeof(client_addr);
        memset(&client_addr, 0, client_len);
        memset(buffer, 0, BUFFER_SIZE);

        // Receive data from client
        ssize_t received = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                                    (struct sockaddr*)&client_addr, &client_len);
        if (received < 0) {
            perror("recvfrom() failed");
            break;
        }

        // Parse incoming data
        //  - If not a valid integer, respond -2
        //  - If integer == -1, respond -1 and terminate
        //  - If out of valid range, respond -2
        //  - Otherwise, do the prime-index math and respond

        buffer[received] = '\0';  // Null-terminate for safety

        char *endptr;
        long incoming_number = strtol(buffer, &endptr, 10);

        // Check for parse errors (e.g., not an integer)
        if (*endptr != '\0' && *endptr != '\n') {
            // Invalid integer format
            snprintf(buffer, BUFFER_SIZE, "%d", -2);
        } else if (incoming_number == -1) {
            // Terminate
            snprintf(buffer, BUFFER_SIZE, "%d", -1);
            sendto(sockfd, buffer, strlen(buffer), 0,
                   (struct sockaddr*)&client_addr, client_len);
            printf("Received -1, shutting down server.\n");
            break;
        } else if (incoming_number < 0 || incoming_number >= 1000000) {
            // Out of range
            snprintf(buffer, BUFFER_SIZE, "%d", -2);
        } else {
            // Valid integer range => apply the transformations
            int incoming_index  = incoming_number / 10000;              
            int additional_val  = (incoming_number % 10000) / 100;      
            int returning_index = incoming_number % 100;               

            // Ensure indices are within array bounds [0..99]
            if (incoming_index  < 0 || incoming_index  > 99 ||
                returning_index < 0 || returning_index > 99) {
                snprintf(buffer, BUFFER_SIZE, "%d", -2);
            } else {
                // Update the prime at incoming_index
                primes[incoming_index] += additional_val;
                // Respond with the prime at returning_index
                snprintf(buffer, BUFFER_SIZE, "%d", primes[returning_index]);
            }
        }

        // Send response
        sendto(sockfd, buffer, strlen(buffer), 0,
               (struct sockaddr*)&client_addr, client_len);
    }

    // Clean up
    close(sockfd);
    return EXIT_SUCCESS;
}
