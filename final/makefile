CC = gcc
CFLAGS = -Wall -Wextra -pthread
CLIENT_SRC = client/chatclient.c
SERVER_SRC = server/chatserver.c
CLIENT_BIN = chatclient
SERVER_BIN = chatserver

.PHONY: all clean server client 

all: server client



server: 
	$(CC) $(CFLAGS) $(SERVER_SRC) -o $(SERVER_BIN)
	

client: 
	$(CC) $(CFLAGS) $(CLIENT_SRC) -o $(CLIENT_BIN)
	

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN) server.log
