all: server client

server: server.c common.h
	gcc -Wall -Wextra server.c -o server

client: client.c common.h
	gcc -Wall -Wextra client.c -o client

clean:
	rm server client