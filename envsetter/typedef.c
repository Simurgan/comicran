#include "typedef.h"

Directory *createDirectory(const char *name, const char *path){
	Directory *dir = (Directory *)malloc(sizeof(Directory));
	dir->name = name;
	dir->path = path;
	return dir;
}
