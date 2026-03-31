CFLAGS = -Wall -Wextra

all: server client

server: server.c common.h
	gcc $(CFLAGS) server.c -o server -lpthread

client: client.c common.h
	gcc $(CFLAGS) client.c -o client -lpthread