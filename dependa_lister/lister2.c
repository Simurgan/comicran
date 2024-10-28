#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define MAX_DEPENDENCIES 128
#define MAX_STRING_LENGTH 512

// Function to resolve the real path of a file, following symlinks
char* resolve_symlink(const char* path, char* resolved_path, size_t size) {
    // Use realpath to resolve symlinks
    if (realpath(path, resolved_path) != NULL) {
        return resolved_path;
    } else {
        // If realpath fails, return the original path
        return (char*)path;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <path_to_executable>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Open a pipe to run the command and capture the output
    char command[256];
    snprintf(command, sizeof(command), "LD_TRACE_LOADED_OBJECTS=1 %s", argv[1]);

    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        perror("Error opening pipe");
        return EXIT_FAILURE;
    }

    // Array to store dependency paths
    char dependencies[MAX_DEPENDENCIES][MAX_STRING_LENGTH];
    int count = 0;

    // Read output line by line and extract paths
    char buffer[MAX_STRING_LENGTH];
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        // Trim leading and trailing whitespace
        char *start = buffer;
        while (isspace((unsigned char)*start)) start++; // Trim leading whitespace

        char *end = buffer + strlen(buffer) - 1;
        while (end > start && isspace((unsigned char)*end)) end--; // Trim trailing whitespace

        *(end + 1) = '\0'; // Null-terminate the trimmed string

        char *path = NULL;

        // Check if the line contains "=>"
        char *arrow_pos = strstr(start, "=>");
        if (arrow_pos != NULL) {
            // Extract the path after the "=>"
            path = arrow_pos + 3; // Skip "=> "
            // Strip the address in parentheses, if present
            char *paren_pos = strstr(path, " (");
            if (paren_pos != NULL) {
                *paren_pos = '\0'; // Terminate the string before the address
            }
        } else if (start[0] == '/') {
            // Handle lines without "=>", which are typically direct paths (like ld-linux)
            path = start;

            // Strip the address in parentheses, if present
            char *paren_pos = strstr(path, " (");
            if (paren_pos != NULL) {
                *paren_pos = '\0'; // Terminate the string before the address
            }
        }

        if (path != NULL) {
            // Trim any trailing newline
            path[strcspn(path, "\n")] = '\0';

            // Store the path in the array if there's space
            if (count < MAX_DEPENDENCIES) {
                strncpy(dependencies[count], path, MAX_STRING_LENGTH - 1);
                dependencies[count][MAX_STRING_LENGTH - 1] = '\0'; // Ensure null termination
                count++;
            }
        }
    }

    // Close the pipe
    pclose(fp);

    // Resolve and print the real paths of the dependencies
    char resolved_path[PATH_MAX];
    printf("Collected Dependency Paths (resolved):\n");
    for (int i = 0; i < count; i++) {
        // Resolve symlinks
        if (resolve_symlink(dependencies[i], resolved_path, sizeof(resolved_path))) {
            printf("%s -> %s\n", dependencies[i], resolved_path);
        } else {
            printf("%s\n", dependencies[i]); // In case of failure
        }
    }

    return 0;
}