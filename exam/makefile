# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -pthread
# Source files and target
SERVER_SRC = bankServer.c
CLIENT_SRC = bankClient.c
CLIENT_TARGET = bankClient
SERVER_TARGET = bankServer
SERVER_ARG = AdaBank Test.fifo
CLIENT_ARG = Client01.file Test.fifo

all: $(CLIENT_TARGET) $(SERVER_TARGET)
# Default target
# Build target
$(CLIENT_TARGET): $(CLIENT_SRC)
	$(CC) $(CFLAGS) $(CLIENT_SRC) -o $(CLIENT_TARGET)
$(SERVER_TARGET): $(SERVER_SRC)
	$(CC) $(CFLAGS) $(SERVER_SRC) -o $(SERVER_TARGET)




run_server:
	./$(SERVER_TARGET) $(SERVER_ARG)
run_client:
	./$(CLIENT_TARGET) $(CLIENT_ARG)
run:
	./$(CLIENT_TARGET) $(CLIENT_ARG) & \
	./$(SERVER_TARGET) $(SERVER_ARG)

server:
	./$(SERVER_TARGET) $(RUN_ARG)

client:
	./$(CLIENT_TARGET) $(RUN_ARG)

# Clean up build files
clean:
	rm -f $(CLIENT_TARGET) $(SERVER_TARGET)

vd:
	valgrind --leak-check=full --track-origins=yes ./$(TARGET) $(RUN_ARG)

# Phony targets
.PHONY: all clean
