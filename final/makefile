CC = gcc
CFLAGS = -Wall -Wextra -pthread
CLIENT_SRC = chatclient.c
SERVER_SRC = chatserver.c
CLIENT_BIN = chatclient
SERVER_BIN = chatserver

.PHONY: all clean server client directories

all: directories server client

directories:
	mkdir -p server client

server: directories
	$(CC) $(CFLAGS) $(SERVER_SRC) -o $(SERVER_BIN)
	mv $(SERVER_BIN) server/

client: directories
	$(CC) $(CFLAGS) $(CLIENT_SRC) -o $(CLIENT_BIN)
	mv $(CLIENT_BIN) client/

clean:
	rm -f server/$(SERVER_BIN) client/$(CLIENT_BIN)