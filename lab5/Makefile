CFLAGS = -Wall -Wextra

all: server client

server: server.cpp common.hpp common.cpp
	g++ $(CFLAGS) server.cpp common.cpp -o server -lpthread

client: client.cpp common.hpp common.cpp
	g++ $(CFLAGS) client.cpp common.cpp -o client -lpthread