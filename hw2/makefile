# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g

# Source files and target
SRC = main.c
TARGET = test_deamon

# Default target
all: $(TARGET) 



# Build target
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)
	rm log.txt
	

run: 
	./$(TARGET) 1 2	
# Clean up build files
clean:
	rm -f $(TARGET)

# Phony targets
.PHONY: all clean
