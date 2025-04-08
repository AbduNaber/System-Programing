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
	@if [ ! -f /tmp/log.txt ]; then rm -f /tmp/log.txt; fi
	

run: 
	./$(TARGET) 1 2	
# Clean up build files
clean:
	@if [ -f $(TARGET) ]; then rm -f $(TARGET); fi
	@if [ -p /tmp/fifo1 ]; then rm -f /tmp/fifo1; fi
	@if [ -p /tmp/fifo2 ]; then rm -f /tmp/fifo2; fi
	@if [ -p /tmp/daemon_fifo ]; then rm -f /tmp/daemon_fifo; fi
	@if [ -f /tmp/log.txt ]; then rm -f /tmp/log.txt; fi
# Phony targets
.PHONY: all clean
