#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libconfig.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>

#define MAX_DIRECTORIES 512
#define MAX_EXECUTABLES 256
#define MAX_FILE_COPIES 512
#define MAX_SYMLINKS 512

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

/**
 * Function prototypes
 */
int add_unique_string(char array[][PATH_MAX], int *count, const char *new_string, int max_count);
void add_directory_with_parents(char directories[][PATH_MAX], int *dir_count, const char *path);
int add_unique_file_copy(FileCopy file_copies[], int *count, const char *src, const char *dst, int max_count);
int add_unique_symlink(Symlink symlinks[], int *count, const char *sym, const char *dst, int max_count);
void resolve_paths_in_array(char array[][PATH_MAX], int count, Symlink symlinks[], int *symlink_count, int max_symlinks);
void resolve_paths_in_file_copies(FileCopy file_copies[], int file_copy_count, Symlink symlinks[], int *symlink_count, int max_symlinks);
void update_paths_with_resolved(char paths[][PATH_MAX], int count, const char *old_path, const char *new_path);
void update_file_copies_with_resolved(FileCopy file_copies[], int count, const char *old_path, const char *new_path);
void get_executable_dependencies(const char *executable, FileCopy file_copies[], int *file_copy_count, int max_file_copies, char directories[][PATH_MAX], int *dir_count, int max_directories);
void write_output_config(const char *output_file, const char *root_dir, const char *root_process, char directories[][PATH_MAX], int dir_count, FileCopy file_copies[], int file_copy_count, Symlink symlinks[], int symlink_count);

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
        return EXIT_FAILURE;
    }

    // [1] Read root_dir from input config file
    const char *root_dir;
    if (!config_lookup_string(&cfg, "root_dir", &root_dir)) {
        fprintf(stderr, "Error: 'root_dir' must be defined in the configuration file.\n");
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }

    // [2] Read root_process from input config file
    const char *root_process;
    if (!config_lookup_string(&cfg, "root_process", &root_process)) {
        fprintf(stderr, "Error: 'root_process' must be defined in the configuration file.\n");
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }

    // Arrays to store directories, executables, file copies, and symlinks
    char directories[MAX_DIRECTORIES][PATH_MAX];
    int dir_count = 0;

    char executables[MAX_EXECUTABLES][PATH_MAX];
    int exec_count = 0;

    FileCopy file_copies[MAX_FILE_COPIES];
    int file_copy_count = 0;

    Symlink symlinks[MAX_SYMLINKS];
    int symlink_count = 0;

    // [3] Read the directories list from the input config file
    config_setting_t *directories_setting = config_lookup(&cfg, "directories");
    if (directories_setting != NULL) {
        int count = config_setting_length(directories_setting);
        for (int i = 0; i < count; i++) {
            const char *dir = config_setting_get_string_elem(directories_setting, i);
            if (dir != NULL) {
                // [3.I] Add parent directories considering hierarchy
                add_directory_with_parents(directories, &dir_count, dir);
            }
        }
    }

    // [4] Read the executables list from the input config file
    config_setting_t *executables_setting = config_lookup(&cfg, "executables");
    if (executables_setting != NULL) {
        int count = config_setting_length(executables_setting);
        for (int i = 0; i < count; i++) {
            const char *exe = config_setting_get_string_elem(executables_setting, i);
            if (exe != NULL) {
                // [4.I] Add unique executables to the array
                add_unique_string(executables, &exec_count, exe, MAX_EXECUTABLES);
            }
        }
    }

    // [5] Add root_process to executables array if not already present
    add_unique_string(executables, &exec_count, root_process, MAX_EXECUTABLES);

    // [6] Read the file_copies list from the input config file
    config_setting_t *file_copies_setting = config_lookup(&cfg, "file_copies");
    if (file_copies_setting != NULL) {
        int count = config_setting_length(file_copies_setting);
        for (int i = 0; i < count; i++) {
            config_setting_t *file_copy_setting = config_setting_get_elem(file_copies_setting, i);
            const char *src, *dst;
            if (config_setting_lookup_string(file_copy_setting, "src", &src)
                && config_setting_lookup_string(file_copy_setting, "dst", &dst)) {
                // [6.I] Add necessary parent directories to directories array
                add_directory_with_parents(directories, &dir_count, dst);
                // [6.II] Add unique file copies to the array
                add_unique_file_copy(file_copies, &file_copy_count, src, dst, MAX_FILE_COPIES);
            }
        }
    }

    // [7] Add all dependencies of each executable to file_copies array
    for (int i = 0; i < exec_count; i++) {
        // Add necessary parent directories for the executable itself
        add_directory_with_parents(directories, &dir_count, executables[i]);

        // Add the executable to file_copies array
        add_unique_file_copy(file_copies, &file_copy_count, executables[i], executables[i], MAX_FILE_COPIES);

        // Get dependencies using ldd and add them to file_copies array
        get_executable_dependencies(executables[i], file_copies, &file_copy_count, MAX_FILE_COPIES, directories, &dir_count, MAX_DIRECTORIES);
    }

    // [8] Resolve real paths in directories array and update symlinks array
    resolve_paths_in_array(directories, dir_count, symlinks, &symlink_count, MAX_SYMLINKS);

    // [9] Resolve real paths in file_copies array and update symlinks array
    resolve_paths_in_file_copies(file_copies, file_copy_count, symlinks, &symlink_count, MAX_SYMLINKS);

    // [10] Read symlinks list from input config file and add to symlinks array
    config_setting_t *symlinks_setting = config_lookup(&cfg, "symlinks");
    if (symlinks_setting != NULL) {
        int count = config_setting_length(symlinks_setting);
        for (int i = 0; i < count; i++) {
            config_setting_t *symlink_setting = config_setting_get_elem(symlinks_setting, i);
            const char *sym, *dst;
            if (config_setting_lookup_string(symlink_setting, "sym", &sym)
                && config_setting_lookup_string(symlink_setting, "dst", &dst)) {
                add_unique_symlink(symlinks, &symlink_count, sym, dst, MAX_SYMLINKS);
            }
        }
    }

    // [11-15] Write the output configuration file
    write_output_config(output_config_file, root_dir, root_process, directories, dir_count, file_copies, file_copy_count, symlinks, symlink_count);

    // Cleanup
    config_destroy(&cfg);
    printf("Configuration written to %s\n", output_config_file);

    return 0;
}

/**
 * Adds a unique string to an array of strings if not already present.
 */
int add_unique_string(char array[][PATH_MAX], int *count, const char *new_string, int max_count) {
    for (int i = 0; i < *count; i++) {
        if (strcmp(array[i], new_string) == 0) {
            return 0; // Already exists
        }
    }
    if (*count < max_count) {
        strncpy(array[*count], new_string, PATH_MAX - 1);
        array[*count][PATH_MAX - 1] = '\0';
        (*count)++;
        return 1; // Added successfully
    }
    fprintf(stderr, "Error: Array is full. Cannot add %s\n", new_string);
    return -1; // Array full
}

/**
 * Adds a directory and all its parent directories to the directories array.
 */
void add_directory_with_parents(char directories[][PATH_MAX], int *dir_count, const char *path) {
    char temp_path[PATH_MAX];
    strncpy(temp_path, path, PATH_MAX - 1);
    temp_path[PATH_MAX - 1] = '\0';

    // Remove trailing slashes
    while (strlen(temp_path) > 1 && temp_path[strlen(temp_path) - 1] == '/') {
        temp_path[strlen(temp_path) - 1] = '\0';
    }

    // Build parent directories
    char *last_slash = strrchr(temp_path, '/');
    while (last_slash != NULL && last_slash != temp_path) {
        *last_slash = '\0';
        add_unique_string(directories, dir_count, temp_path, MAX_DIRECTORIES);
        last_slash = strrchr(temp_path, '/');
    }

    // Add root directory if not already added
    if (strlen(temp_path) > 0) {
        add_unique_string(directories, dir_count, "/", MAX_DIRECTORIES);
    }
}

/**
 * Adds a unique FileCopy to the array if not already present.
 */
int add_unique_file_copy(FileCopy file_copies[], int *count, const char *src, const char *dst, int max_count) {
    for (int i = 0; i < *count; i++) {
        if (strcmp(file_copies[i].src, src) == 0 && strcmp(file_copies[i].dst, dst) == 0) {
            return 0; // Already exists
        }
    }
    if (*count < max_count) {
        strncpy(file_copies[*count].src, src, PATH_MAX - 1);
        file_copies[*count].src[PATH_MAX - 1] = '\0';
        strncpy(file_copies[*count].dst, dst, PATH_MAX - 1);
        file_copies[*count].dst[PATH_MAX - 1] = '\0';
        (*count)++;
        return 1; // Added successfully
    }
    fprintf(stderr, "Error: FileCopy array is full. Cannot add %s -> %s\n", src, dst);
    return -1; // Array full
}

/**
 * Adds a unique Symlink to the array if not already present.
 */
int add_unique_symlink(Symlink symlinks[], int *count, const char *sym, const char *dst, int max_count) {
    for (int i = 0; i < *count; i++) {
        if (strcmp(symlinks[i].sym, sym) == 0 && strcmp(symlinks[i].dst, dst) == 0) {
            return 0; // Already exists
        }
    }
    if (*count < max_count) {
        strncpy(symlinks[*count].sym, sym, PATH_MAX - 1);
        symlinks[*count].sym[PATH_MAX - 1] = '\0';
        strncpy(symlinks[*count].dst, dst, PATH_MAX - 1);
        symlinks[*count].dst[PATH_MAX - 1] = '\0';
        (*count)++;
        return 1; // Added successfully
    }
    fprintf(stderr, "Error: Symlink array is full. Cannot add %s -> %s\n", sym, dst);
    return -1; // Array full
}

/**
 * Resolves real paths in the directories array and updates symlinks array.
 */
void resolve_paths_in_array(char array[][PATH_MAX], int count, Symlink symlinks[], int *symlink_count, int max_symlinks) {
    for (int i = 0; i < count; i++) {
        char resolved_path[PATH_MAX];
        if (realpath(array[i], resolved_path) != NULL) {
            if (strcmp(array[i], resolved_path) != 0) {
                // [8.I.a] Add to symlinks array
                add_unique_symlink(symlinks, symlink_count, array[i], resolved_path, max_symlinks);

                // [8.I.c] Replace the item in the array with resolved path
                strcpy(array[i], resolved_path);

                // [8.I.d] Update any other paths that start with the old path
                for (int j = i + 1; j < count; j++) {
                    if (strncmp(array[j], array[i], strlen(array[i])) == 0) {
                        char new_path[PATH_MAX];
                        snprintf(new_path, PATH_MAX, "%s%s", resolved_path, array[j] + strlen(array[i]));
                        strcpy(array[j], new_path);
                    }
                }
            }
        }
    }
}

/**
 * Resolves real paths in the file_copies array and updates symlinks array.
 */
void resolve_paths_in_file_copies(FileCopy file_copies[], int file_copy_count, Symlink symlinks[], int *symlink_count, int max_symlinks) {
    for (int i = 0; i < file_copy_count; i++) {
        char resolved_src[PATH_MAX];
        if (realpath(file_copies[i].src, resolved_src) != NULL) {
            if (strcmp(file_copies[i].src, resolved_src) != 0) {
                // [9.I.a] Add to symlinks array
                add_unique_symlink(symlinks, symlink_count, file_copies[i].src, resolved_src, max_symlinks);

                // [9.I.c] Replace the src in file_copies array
                strcpy(file_copies[i].src, resolved_src);

                // [9.I.d] Update any other src paths that start with the old src
                for (int j = i + 1; j < file_copy_count; j++) {
                    if (strncmp(file_copies[j].src, file_copies[i].src, strlen(file_copies[i].src)) == 0) {
                        char new_src[PATH_MAX];
                        snprintf(new_src, PATH_MAX, "%s%s", resolved_src, file_copies[j].src + strlen(file_copies[i].src));
                        strcpy(file_copies[j].src, new_src);
                    }
                }
            }
        }

        // Resolve dst paths similarly
        char resolved_dst[PATH_MAX];
        if (realpath(file_copies[i].dst, resolved_dst) != NULL) {
            if (strcmp(file_copies[i].dst, resolved_dst) != 0) {
                // [9.I.a] Add to symlinks array
                add_unique_symlink(symlinks, symlink_count, file_copies[i].dst, resolved_dst, max_symlinks);

                // [9.I.c] Replace the dst in file_copies array
                strcpy(file_copies[i].dst, resolved_dst);

                // [9.I.d] Update any other dst paths that start with the old dst
                for (int j = i + 1; j < file_copy_count; j++) {
                    if (strncmp(file_copies[j].dst, file_copies[i].dst, strlen(file_copies[i].dst)) == 0) {
                        char new_dst[PATH_MAX];
                        snprintf(new_dst, PATH_MAX, "%s%s", resolved_dst, file_copies[j].dst + strlen(file_copies[i].dst));
                        strcpy(file_copies[j].dst, new_dst);
                    }
                }
            }
        }
    }
}

/**
 * Retrieves the dependencies of an executable using ldd and adds them to file_copies array.
 */
void get_executable_dependencies(const char *executable, FileCopy file_copies[], int *file_copy_count, int max_file_copies, char directories[][PATH_MAX], int *dir_count, int max_directories) {
    char command[PATH_MAX + 10];
    snprintf(command, sizeof(command), "ldd %s", executable);

    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        perror("Error executing ldd");
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *start = line;
        while (isspace((unsigned char)*start)) start++;

        // Skip empty lines
        if (*start == '\0') continue;

        char *path = NULL;
        char *arrow = strstr(start, "=>");
        if (arrow != NULL) {
            // Extract path after "=>"
            path = arrow + 2;
            while (isspace((unsigned char)*path)) path++;

            // Remove any address in parentheses
            char *paren = strstr(path, " (");
            if (paren != NULL) {
                *paren = '\0';
            }
        } else if (start[0] == '/') {
            // Direct path without "=>"
            path = start;
            // Remove any address in parentheses
            char *paren = strstr(path, " (");
            if (paren != NULL) {
                *paren = '\0';
            }
        }

        if (path != NULL && path[0] == '/') {
            // Trim trailing whitespace
            size_t len = strlen(path);
            while (len > 0 && isspace((unsigned char)path[len - 1])) {
                path[--len] = '\0';
            }

            // [7.I] Add necessary parent directories
            add_directory_with_parents(directories, dir_count, path);

            // [7.II] Add unique dependencies to file_copies array
            add_unique_file_copy(file_copies, file_copy_count, path, path, max_file_copies);
        }
    }

    pclose(fp);
}

/**
 * Writes the output configuration file with the collected data.
 */
void write_output_config(const char *output_file, const char *root_dir, const char *root_process, char directories[][PATH_MAX], int dir_count, FileCopy file_copies[], int file_copy_count, Symlink symlinks[], int symlink_count) {
    FILE *fp = fopen(output_file, "w");
    if (fp == NULL) {
        perror("Error opening output config file");
        return;
    }

    // [11] Write root_dir to output config file
    fprintf(fp, "root_dir = \"%s\";\n", root_dir);

    // [12] Write root_process to output config file
    fprintf(fp, "root_process = \"%s\";\n", root_process);

    // [13] Write directories array to output config file
    fprintf(fp, "directories = (\n");
    for (int i = 0; i < dir_count; i++) {
        fprintf(fp, "    \"%s\"", directories[i]);
        if (i < dir_count - 1) {
            fprintf(fp, ",");
        }
        fprintf(fp, "\n");
    }
    fprintf(fp, ");\n");

    // [14] Write file_copies array to output config file
    fprintf(fp, "file_copies = (\n");
    for (int i = 0; i < file_copy_count; i++) {
        fprintf(fp, "    {\n        src = \"%s\";\n        dst = \"%s\"\n    }", file_copies[i].src, file_copies[i].dst);
        if (i < file_copy_count - 1) {
            fprintf(fp, ",");
        }
        fprintf(fp, "\n");
    }
    fprintf(fp, ");\n");

    // [15] Write symlinks array to output config file
    fprintf(fp, "symlinks = (\n");
    for (int i = 0; i < symlink_count; i++) {
        fprintf(fp, "    {\n        sym = \"%s\";\n        dst = \"%s\"\n    }", symlinks[i].sym, symlinks[i].dst);
        if (i < symlink_count - 1) {
            fprintf(fp, ",");
        }
        fprintf(fp, "\n");
    }
    fprintf(fp, ");\n");

    fclose(fp);
}
