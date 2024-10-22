#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#define MAX_DEPENDENCIES 128
#define MAX_STRING_LENGTH 512

// Function to resolve symlinks to their final path
char *resolve_symlink(const char *path, char *resolved_path) {
    if (realpath(path, resolved_path) != NULL) {
        return resolved_path;
    }
    return NULL;
}

// Function to add unique resolved paths to the list
int add_unique_path(char resolved_paths[][PATH_MAX], int *count, const char *new_path) {
    // Check if the path already exists
    for (int i = 0; i < *count; i++) {
        if (strcmp(resolved_paths[i], new_path) == 0) {
            return 0; // Path already exists, don't add
        }
    }
    // Add the new path to the list
    if (*count < MAX_DEPENDENCIES) {
        strncpy(resolved_paths[*count], new_path, PATH_MAX - 1);
        resolved_paths[*count][PATH_MAX - 1] = '\0'; // Ensure null termination
        (*count)++;
        return 1; // Path added successfully
    }
    return 0; // No space to add
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path_to_executable1> <path_to_executable2> ...\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Array to store unique resolved paths
    char resolved_paths[MAX_DEPENDENCIES][PATH_MAX];
    int count = 0;

    // Process each executable provided as an argument
    for (int i = 1; i < argc; i++) {
        // Open a pipe to run the command and capture the output
        char command[256];
        snprintf(command, sizeof(command), "LD_TRACE_LOADED_OBJECTS=1 %s", argv[i]);

        FILE *fp = popen(command, "r");
        if (fp == NULL) {
            perror("Error opening pipe");
            return EXIT_FAILURE;
        }

        // Read output line by line and extract paths
        char buffer[MAX_STRING_LENGTH];
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            // Trim leading and trailing whitespace
            char *start = buffer;
            while (isspace((unsigned char)*start)) start++;

            char *end = buffer + strlen(buffer) - 1;
            while (end > start && isspace((unsigned char)*end)) end--;

            *(end + 1) = '\0'; // Null-terminate the trimmed string

            char *path = NULL;

            // Check if the line contains "=>"
            char *arrow_pos = strstr(start, "=>");
            if (arrow_pos != NULL) {
                // Extract the path after the "=>"
                path = arrow_pos + 3; // Skip "=> "
            } else if (start[0] == '/') {
                // Handle lines without "=>", which are typically direct paths (like ld-linux)
                path = start;
            }

            if (path != NULL) {
                // Strip any address in parentheses
                char *paren_pos = strstr(path, " (");
                if (paren_pos != NULL) {
                    *paren_pos = '\0'; // Terminate the string before the address
                }

                // Trim any trailing newline
                path[strcspn(path, "\n")] = '\0';

                // Resolve the path and add to the list if it's unique
                char resolved_path[PATH_MAX];
                if (resolve_symlink(path, resolved_path) != NULL) {
                    add_unique_path(resolved_paths, &count, resolved_path);
                }
            }
        }

        // Close the pipe
        pclose(fp);
    }

    // Print all unique resolved paths
    for (int i = 0; i < count; i++) {
        printf("%s\n", resolved_paths[i]);
    }

    return 0;
}
