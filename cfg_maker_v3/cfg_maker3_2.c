#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <libconfig.h>
#include <sys/stat.h>

#define MAX_DEPENDENCIES 128
#define MAX_STRING_LENGTH 512
#define MAX_FILE_COPIES 256 // Increased to accommodate executables
#define MAX_SYMLINKS 256    // Increased to accommodate executables
#define MAX_DIRECTORIES 512 // Increased to accommodate directories from file copies

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
    if (*count < MAX_FILE_COPIES) {
        strncpy(file_copies[*count].src, new_file_copy_src, PATH_MAX - 1);
        file_copies[*count].src[PATH_MAX - 1] = '\0'; // Ensure null termination
        strncpy(file_copies[*count].dst, new_file_copy_dst, PATH_MAX - 1);
        file_copies[*count].dst[PATH_MAX - 1] = '\0'; // Ensure null termination
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
    if (*count < MAX_SYMLINKS) {
        strncpy(symlinks[*count].sym, new_symlink_sym, PATH_MAX - 1);
        symlinks[*count].sym[PATH_MAX - 1] = '\0'; // Ensure null termination
        strncpy(symlinks[*count].dst, new_symlink_dst, PATH_MAX - 1);
        symlinks[*count].dst[PATH_MAX - 1] = '\0'; // Ensure null termination
        (*count)++;
        return 1; // Path added successfully
    }
    return 0; // No space to add
}

// Function to add unique directories to the list
int add_unique_directory(char directories[MAX_DIRECTORIES][PATH_MAX], int *count, const char *new_directory) {
    for (int i = 0; i < *count; i++) {
        if (strcmp(directories[i], new_directory) == 0) {
            return 0; // Directory already exists
        }
    }
    if (*count < MAX_DIRECTORIES) {
        strncpy(directories[*count], new_directory, PATH_MAX - 1);
        directories[*count][PATH_MAX - 1] = '\0'; // Ensure null termination
        (*count)++;
        return 1; // Directory added successfully
    }
    return 0; // No space to add
}

// Function to process directory hierarchy, handle symlinks, and add directories
void process_directory_hierarchy(
    char directories[MAX_DIRECTORIES][PATH_MAX],
    int *dir_count,
    Symlink symlinks[MAX_SYMLINKS],
    int *symlink_count,
    const char *path) {

    char temp_path[PATH_MAX];
    char accumulated_path[PATH_MAX];

    // Get the directory part of the path
    strncpy(temp_path, path, PATH_MAX - 1);
    temp_path[PATH_MAX - 1] = '\0';

    // Remove trailing slashes
    while (strlen(temp_path) > 1 && temp_path[strlen(temp_path) - 1] == '/') {
        temp_path[strlen(temp_path) - 1] = '\0';
    }

    // Remove file name if path is not a directory
    struct stat st;
    if (lstat(temp_path, &st) == 0 && !S_ISDIR(st.st_mode)) {
        char *last_slash = strrchr(temp_path, '/');
        if (last_slash != NULL && last_slash != temp_path) {
            *last_slash = '\0';
        } else {
            strcpy(temp_path, "/");
        }
    }

    // Now temp_path is the directory path
    // Process each component
    accumulated_path[0] = '\0';

    char *token;
    char *rest = temp_path;

    if (temp_path[0] == '/') {
        strcpy(accumulated_path, "/");
        rest++; // Skip the leading '/'
    }

    token = strtok(rest, "/");
    while (token != NULL) {
        if (strlen(accumulated_path) > 1 && accumulated_path[strlen(accumulated_path) - 1] != '/')
            strcat(accumulated_path, "/");
        strcat(accumulated_path, token);

        // Check if the directory is already added
        add_unique_directory(directories, dir_count, accumulated_path);

        // Check if the directory is a symlink
        if (lstat(accumulated_path, &st) == 0 && S_ISLNK(st.st_mode)) {
            // It's a symlink
            char resolved_path[PATH_MAX];
            ssize_t len = readlink(accumulated_path, resolved_path, sizeof(resolved_path) - 1);
            if (len != -1) {
                resolved_path[len] = '\0';

                // Handle relative symlink targets
                char target_path[PATH_MAX];
                if (resolved_path[0] != '/') {
                    // Relative path
                    char dir_name[PATH_MAX];
                    strncpy(dir_name, accumulated_path, PATH_MAX - 1);
                    dir_name[PATH_MAX - 1] = '\0';
                    char *last_slash = strrchr(dir_name, '/');
                    if (last_slash != NULL) {
                        *last_slash = '\0';
                    } else {
                        strcpy(dir_name, "/");
                    }
                    snprintf(target_path, PATH_MAX, "%s/%s", dir_name, resolved_path);
                } else {
                    // Absolute path
                    strncpy(target_path, resolved_path, PATH_MAX - 1);
                    target_path[PATH_MAX - 1] = '\0';
                }

                // Resolve the target path to an absolute path
                char absolute_target[PATH_MAX];
                if (realpath(target_path, absolute_target) == NULL) {
                    strncpy(absolute_target, target_path, PATH_MAX - 1);
                    absolute_target[PATH_MAX - 1] = '\0';
                }

                // Add the symlink to the symlinks list
                add_unique_symlink(symlinks, symlink_count, accumulated_path, resolved_path);

                // Recursively process the target directory
                process_directory_hierarchy(directories, dir_count, symlinks, symlink_count, absolute_target);
            }
        }

        token = strtok(NULL, "/");
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input_config_file> <output_config_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *input_config_file = argv[1];
    const char *output_config_file = argv[2];

    // Initialize libconfig
    config_t cfg;
    config_init(&cfg);

    // Read the input configuration file
    if (!config_read_file(&cfg, input_config_file)) {
        fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
                config_error_line(&cfg), config_error_text(&cfg));
        config_destroy(&cfg);
        return (EXIT_FAILURE);
    }

    // Get root_dir and process
    const char *root_dir;
    const char *process;
    if (!config_lookup_string(&cfg, "root_dir", &root_dir)) {
        fprintf(stderr, "Error: 'root_dir' must be defined in the configuration file.\n");
        config_destroy(&cfg);
        return (EXIT_FAILURE);
    }
    if (!config_lookup_string(&cfg, "process", &process)) {
        fprintf(stderr, "Error: 'process' must be defined in the configuration file.\n");
        config_destroy(&cfg);
        return (EXIT_FAILURE);
    }

    // Initialize directories
    char directories[MAX_DIRECTORIES][PATH_MAX];
    int dir_count = 0;
    config_setting_t *directories_setting = config_lookup(&cfg, "directories");
    if (directories_setting != NULL) {
        int count = config_setting_length(directories_setting);
        for (int i = 0; i < count && dir_count < MAX_DIRECTORIES; i++) {
            const char *dir = config_setting_get_string_elem(directories_setting, i);
            if (dir != NULL) {
                strncpy(directories[dir_count], dir, PATH_MAX - 1);
                directories[dir_count][PATH_MAX - 1] = '\0'; // Null-terminate
                dir_count++;
            }
        }
    }

    // Initialize executables
    char executables[MAX_DEPENDENCIES][PATH_MAX];
    int exec_count = 0;
    config_setting_t *executables_setting = config_lookup(&cfg, "executables");
    if (executables_setting != NULL) {
        int count = config_setting_length(executables_setting);
        for (int i = 0; i < count && exec_count < MAX_DEPENDENCIES; i++) {
            const char *exe = config_setting_get_string_elem(executables_setting, i);
            if (exe != NULL) {
                strncpy(executables[exec_count], exe, PATH_MAX - 1);
                executables[exec_count][PATH_MAX - 1] = '\0'; // Null-terminate
                exec_count++;
            }
        }
    }

    // Add the process to the executables array
    if (exec_count < MAX_DEPENDENCIES) {
        strncpy(executables[exec_count], process, PATH_MAX - 1);
        executables[exec_count][PATH_MAX - 1] = '\0';
        exec_count++;
    } else {
        fprintf(stderr, "Error: Executables array is full. Cannot add process.\n");
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }

    // Gather dependencies
    FileCopy file_copies[MAX_FILE_COPIES];
    int file_copy_count = 0;
    Symlink symlinks[MAX_SYMLINKS];
    int symlink_count = 0;

    // Get file_copies from config
    config_setting_t *file_copies_setting = config_lookup(&cfg, "file_copies");
    if (file_copies_setting != NULL) {
        int count = config_setting_length(file_copies_setting);
        for (int i = 0; i < count && file_copy_count < MAX_FILE_COPIES; i++) {
            config_setting_t *file_copy_setting = config_setting_get_elem(file_copies_setting, i);
            const char *src, *dst;
            if (!(config_setting_lookup_string(file_copy_setting, "src", &src)
                  && config_setting_lookup_string(file_copy_setting, "dst", &dst))) {
                continue; // Skip if either src or dst is missing
            }
            // Add to file_copies array
            add_unique_file_copy(file_copies, &file_copy_count, src, dst);
        }
    }

    // Get symlinks from config
    config_setting_t *symlinks_setting = config_lookup(&cfg, "symlinks");
    if (symlinks_setting != NULL) {
        int count = config_setting_length(symlinks_setting);
        for (int i = 0; i < count && symlink_count < MAX_SYMLINKS; i++) {
            config_setting_t *symlink_setting = config_setting_get_elem(symlinks_setting, i);
            const char *sym, *dst;
            if (!(config_setting_lookup_string(symlink_setting, "sym", &sym)
                  && config_setting_lookup_string(symlink_setting, "dst", &dst))) {
                continue; // Skip if either sym or dst is missing
            }
            // Add to symlinks array
            add_unique_symlink(symlinks, &symlink_count, sym, dst);
        }
    }

    // Process executables and their dependencies
    for (int i = 0; i < exec_count; i++) {
        // Resolve the path of the executable
        char resolved_exe_path[PATH_MAX];
        if (resolve_symlink(executables[i], resolved_exe_path) != NULL) {
            // Add the executable to file_copies
            add_unique_file_copy(file_copies, &file_copy_count, resolved_exe_path, resolved_exe_path);
        } else {
            fprintf(stderr, "Failed to resolve path for executable: %s\n", executables[i]);
            continue;
        }

        // Add the executable symlink if it is a symlink
        char exe_link_target[PATH_MAX];
        ssize_t len = readlink(executables[i], exe_link_target, sizeof(exe_link_target) - 1);
        if (len != -1) {
            exe_link_target[len] = '\0';
            add_unique_symlink(symlinks, &symlink_count, executables[i], exe_link_target);
        }

        // Get dependencies using ldd
        char command[512];
        snprintf(command, sizeof(command), "ldd %s", executables[i]);

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
                path = arrow_pos + 2; // Skip "=>"
                while (isspace((unsigned char)*path)) path++;
            } else if (start[0] == '/') {
                path = start;
            }

            if (path != NULL && path[0] == '/') {
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

    // Extract directories from destination paths in file_copies
    for (int i = 0; i < file_copy_count; i++) {
        process_directory_hierarchy(directories, &dir_count, symlinks, &symlink_count, file_copies[i].dst);
    }

    // Extract directories from symlinks' sym paths
    for (int i = 0; i < symlink_count; i++) {
        process_directory_hierarchy(directories, &dir_count, symlinks, &symlink_count, symlinks[i].sym);
    }

    // Write to the output configuration file
    FILE *config_file = fopen(output_config_file, "w");
    if (config_file == NULL) {
        perror("Error opening output config file");
        return EXIT_FAILURE;
    }

    fprintf(config_file, "root_dir = \"%s\"\n", root_dir);
    fprintf(config_file, "process = \"%s\"\n", process);

    // Write directories
    fprintf(config_file, "\ndirectories = (\n");
    for (int i = 0; i < dir_count; i++) {
        fprintf(config_file, "\t\"%s\"", directories[i]);
        if (i < dir_count - 1) fprintf(config_file, ",");
        fprintf(config_file, "\n");
    }
    fprintf(config_file, ")\n");

    // Write file copies
    fprintf(config_file, "\nfile_copies = (\n");
    for (int i = 0; i < file_copy_count; i++) {
        fprintf(config_file, "\t{\n\t\tsrc = \"%s\",\n\t\tdst = \"%s\"\n\t}", file_copies[i].src, file_copies[i].dst);
        if (i < file_copy_count - 1) fprintf(config_file, ",");
        fprintf(config_file, "\n");
    }
    fprintf(config_file, ")\n");

    // Write symlinks
    fprintf(config_file, "\nsymlinks = (\n");
    for (int i = 0; i < symlink_count; i++) {
        fprintf(config_file, "\t{\n\t\tsym = \"%s\",\n\t\tdst = \"%s\"\n\t}", symlinks[i].sym, symlinks[i].dst);
        if (i < symlink_count - 1) fprintf(config_file, ",");
        fprintf(config_file, "\n");
    }
    fprintf(config_file, ")\n");

    fclose(config_file);
    printf("Configuration written to %s\n", output_config_file);

    config_destroy(&cfg);
    return 0;
}
