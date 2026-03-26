CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -D_GNU_SOURCE
INCLUDES = -Iinclude

BIN_DIR = .

SERVER_OBJS = src/rawtcp_server.o src/rawtcp.o
CLIENT_OBJS = src/tcp_client.o

all: rawtcp_server tcp_client

rawtcp_server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/$@ $(SERVER_OBJS)

tcp_client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/$@ $(CLIENT_OBJS)

src/rawtcp_server.o: src/rawtcp_server.c include/rawtcp.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

src/rawtcp.o: src/rawtcp.c include/rawtcp.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

src/tcp_client.o: src/tcp_client.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SERVER_OBJS) $(CLIENT_OBJS) rawtcp_server tcp_client

.PHONY: all clean
