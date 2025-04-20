# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -pthread 

# Source files and target
SRC = main.c
TARGET = hw3

# Default target
all: $(TARGET)

run: $(TARGET)
	./$(TARGET)

# Build target
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) 

# Clean up build files
clean:
	rm -f $(TARGET)

# Phony targets
.PHONY: all clean
