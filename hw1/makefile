# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g

# Source files and target
SRC = main.c
TARGET = fileManager

# Default target
all: $(TARGET)



# Build target
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

# Clean up build files
clean:
	rm -f $(TARGET)

# Phony targets
.PHONY: all clean
