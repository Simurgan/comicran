#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#define MAX_DEPENDENCIES 128
#define MAX_STRING_LENGTH 512
#define MAX_FILE_COPIES 128
#define MAX_SYMLINKS 128

// Structure to hold file copy information
typedef struct {
    char src[PATH_MAX];
    char dst[PATH_MAX];
} FileCopy;

// Structure to hold symlink information
typedef struct {
    char sym[PATH_MAX];
    char dst[PATH_MAX];
} Symlink;

// Function to resolve symlinks to their final path
char *resolve_symlink(const char *path, char *resolved_path) {
    if (realpath(path, resolved_path) != NULL) {
        return resolved_path;
    }
    return NULL;
}

// Function to add unique FileCopies to the list
int add_unique_file_copy(FileCopy file_copies[MAX_FILE_COPIES], int *count, const char *new_file_copy_src, const char *new_file_copy_dst) {
    for (int i = 0; i < *count; i++) {
        if (strcmp(file_copies[i].dst, new_file_copy_dst) == 0) {
            return 0; // Path already exists
        }
    }
    if (*count < MAX_DEPENDENCIES) {
        strncpy(file_copies[*count].src, new_file_copy_src, PATH_MAX - 1);
        strncpy(file_copies[*count].dst, new_file_copy_dst, PATH_MAX - 1);
        (*count)++;
        return 1; // Path added successfully
    }
    return 0; // No space to add
}

// Function to add unique Symlinks to the list
int add_unique_symlink(Symlink symlinks[MAX_SYMLINKS], int *count, const char *new_symlink_sym, const char *new_symlink_dst) {
    for (int i = 0; i < *count; i++) {
        if (strcmp(symlinks[i].sym, new_symlink_sym) == 0) {
            return 0; // Path already exists
        }
    }
    if (*count < MAX_DEPENDENCIES) {
        strncpy(symlinks[*count].sym, new_symlink_sym, PATH_MAX - 1);
        strncpy(symlinks[*count].dst, new_symlink_dst, PATH_MAX - 1);
        (*count)++;

        return 1; // Path added successfully
    }

    return 0; // No space to add
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <config_file_name>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *config_file_name = argv[1];

    // Prompt user for configuration details
    char root_dir[PATH_MAX];
    char process[PATH_MAX];
    char directories[MAX_DEPENDENCIES][PATH_MAX];
    int dir_count = 0;

    printf("Enter root_dir: ");
    fgets(root_dir, sizeof(root_dir), stdin);
    root_dir[strcspn(root_dir, "\n")] = '\0'; // Remove newline

    printf("Enter process: ");
    fgets(process, sizeof(process), stdin);
    process[strcspn(process, "\n")] = '\0'; // Remove newline

    printf("Enter directories (comma-separated): ");
    char dir_input[MAX_STRING_LENGTH];
    fgets(dir_input, sizeof(dir_input), stdin);
    char *token = strtok(dir_input, ",");
    while (token != NULL && dir_count < MAX_DEPENDENCIES) {
        strncpy(directories[dir_count], token, PATH_MAX - 1);
        directories[dir_count][(int)strlen(token)] = '\0'; // Null-terminate
        if(directories[dir_count][(int)strlen(token) - 1] == '\n') {
            directories[dir_count][(int)strlen(token) - 1] = '\0'; // Null-terminate
        }
        dir_count++;
        token = strtok(NULL, ",");
    }

    // Get executable paths
    char executables[MAX_DEPENDENCIES][PATH_MAX];
    int exec_count = 0;
    printf("Enter paths of executables (comma-separated): ");
    char exec_input[MAX_STRING_LENGTH];
    fgets(exec_input, sizeof(exec_input), stdin);
    token = strtok(exec_input, ",");
    while (token != NULL && exec_count < MAX_DEPENDENCIES) {
        strncpy(executables[exec_count], token, PATH_MAX - 1);
        executables[exec_count][PATH_MAX - 1] = '\0'; // Null-terminate
        exec_count++;
        token = strtok(NULL, ",");
    }

    // Gather dependencies
    FileCopy file_copies[MAX_FILE_COPIES];
    int file_copy_count = 0;
    Symlink symlinks[MAX_SYMLINKS];
    int symlink_count = 0;

    int count = 0;
    for (int i = 0; i < exec_count; i++) {
        char command[256];
        snprintf(command, sizeof(command), "LD_TRACE_LOADED_OBJECTS=1 %s", executables[i]);

        FILE *fp = popen(command, "r");
        if (fp == NULL) {
            perror("Error opening pipe");
            return EXIT_FAILURE;
        }

        char buffer[MAX_STRING_LENGTH];
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            // Trim leading and trailing whitespace
            char *start = buffer;
            while (isspace((unsigned char)*start)) start++;

            char *end = buffer + strlen(buffer) - 1;
            while (end > start && isspace((unsigned char)*end)) end--;

            *(end + 1) = '\0'; // Null-terminate

            char *path = NULL;

            // Check if the line contains "=>"
            char *arrow_pos = strstr(start, "=>");
            if (arrow_pos != NULL) {
                path = arrow_pos + 3; // Skip "=> "
            } else if (start[0] == '/') {
                path = start;
            }

            if (path != NULL) {
                // Strip any address in parentheses
                char *paren_pos = strstr(path, " (");
                if (paren_pos != NULL) {
                    *paren_pos = '\0'; // Terminate
                }

                // Trim any trailing newline
                path[strcspn(path, "\n")] = '\0';

                // Resolve the path and add to the list if it's unique
                char resolved_path[PATH_MAX];
                if (resolve_symlink(path, resolved_path) != NULL) {
                    add_unique_file_copy(file_copies, &file_copy_count, resolved_path, resolved_path);
                    add_unique_symlink(symlinks, &symlink_count, path, resolved_path);
                }
            }
        }
        pclose(fp);
    }

    // Gather file copy information
    char add_file_copy;

    do {
        if (file_copy_count >= MAX_FILE_COPIES) break;

        printf("Would you like to add a file copy? (y/n): ");
        scanf(" %c", &add_file_copy);
        if (add_file_copy == 'y' || add_file_copy == 'Y') {
            printf("Enter source path: ");
            scanf("%s", file_copies[file_copy_count].src);
            printf("Enter destination path: ");
            scanf("%s", file_copies[file_copy_count].dst);
            file_copy_count++;
        }
    } while (add_file_copy == 'y' || add_file_copy == 'Y');

    // Gather symlink information
    char add_symlink;

    do {
        if (symlink_count >= MAX_SYMLINKS) break;

        printf("Would you like to add a symlink? (y/n): ");
        scanf(" %c", &add_symlink);
        if (add_symlink == 'y' || add_symlink == 'Y') {
            printf("Enter symlink path: ");
            scanf("%s", symlinks[symlink_count].sym);
            printf("Enter destination path: ");
            scanf("%s", symlinks[symlink_count].dst);
            symlink_count++;
        }
    } while (add_symlink == 'y' || add_symlink == 'Y');

    // Write to the configuration file
    FILE *config_file = fopen(config_file_name, "w");
    if (config_file == NULL) {
        perror("Error opening config file");
        return EXIT_FAILURE;
    }

    fprintf(config_file, "root_dir = \"%s\"\n", root_dir);
    fprintf(config_file, "process = \"%s\"\n", process);

    // Write directories
    fprintf(config_file, "\ndirectories = (");
    for (int i = 0; i < dir_count; i++) {
        fprintf(config_file, "\"%s\"", directories[i]);
        if (i < dir_count - 1) fprintf(config_file, ", ");
    }
    fprintf(config_file, ")\n");

    // Write file copies
    fprintf(config_file, "\nfile_copies = (\n");
    for (int i = 0; i < file_copy_count; i++) {
        fprintf(config_file, "\t{\n\t\tsrc = \"%s\",\n\t\tdst = \"%s\"\n\t},\n", file_copies[i].src, file_copies[i].dst);
    }
    fprintf(config_file, ")\n");

    // Write symlinks
    fprintf(config_file, "\nsymlinks = (\n");
    for (int i = 0; i < symlink_count; i++) {
        fprintf(config_file, "\t{\n\t\tsym = \"%s\",\n\t\tdst = \"%s\"\n\t},\n", symlinks[i].sym, symlinks[i].dst);
    }
    fprintf(config_file, ")\n");

    fclose(config_file);
    printf("Configuration written to %s\n", config_file_name);

    return 0;
}
