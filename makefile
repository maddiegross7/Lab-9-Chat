# Compiler
CC = gcc

# Include path for sockettome.h and dllist.h
INCLUDES = -I/home/mrjantz/cs360/include

# Library paths
LIBDIR = /home/mrjantz/cs360/lib

# Compiler flags
CFLAGS = -Wall -Wextra -g -fsanitize=address $(INCLUDES)

# Linker flags
LDFLAGS = -fsanitize=address

# Libraries to link: libfdr and sockettome.o
LIBS = $(LIBDIR)/libfdr.a /home/mrjantz/cs360/objs/sockettome.o -lpthread

# Target executable
TARGET = chat_server

# Default rule
all: $(TARGET)

# Rule to build chat_server
$(TARGET): chat_server.c
	$(CC) $(CFLAGS) -o $(TARGET) chat_server.c $(LIBS) $(LDFLAGS)

# Clean rule
clean:
	rm -f $(TARGET)
