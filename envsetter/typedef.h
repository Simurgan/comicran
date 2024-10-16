typedef struct directory Directory ;

struct directory {
	char *name;
	Directory *subdirs;
};
