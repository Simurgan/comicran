# Makefile for jailor
# -------------------

# Compiler and flags
CC     = gcc
CFLAGS = -Wall -Werror -Wunused -Wextra -Wmaybe-uninitialized \
         -Wstrict-prototypes -Wmissing-prototypes \
         -Wdeclaration-after-statement -Wmissing-declarations \
         -Wmissing-format-attribute -Wnull-dereference -Wformat=2 \
         -Wshadow -Wsizeof-pointer-memaccess \
         -std=gnu99 -pthread -O0 -g -Wstack-protector \
         -fno-omit-frame-pointer -fwrapv -D_FORTIFY_SOURCE=2

LDLIBS = -lconfig

# Default target
all: jailor

# Build the final jailor executable
jailor: jailor.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Compile jailor.c to an object file
jailor.o: jailor.c
	$(CC) $(CFLAGS) -c jailor.c

# Clean up
clean:
	rm -f jailor.o jailor
