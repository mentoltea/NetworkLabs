#include <stdint.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <pthread.h>

#define MAX_PAYLOAD 1024  
  
typedef struct {  
    uint32_t length;  
    uint8_t type;
    char payload[MAX_PAYLOAD];
} Message;  

enum {  
    MSG_HELLO = 1,
    MSG_WELCOME= 2,  
    MSG_TEXT = 3,  
    MSG_PING = 4,  
    MSG_PONG = 5,  
    MSG_BYE = 6,
};

#define PORT 4445

#define MAX_NICKNAME_LEN 128
struct ConnectionInfo {
    int fd;
    struct sockaddr_in addr;
    bool active;
    bool greated;

    char nickname[MAX_NICKNAME_LEN];
};

int send_message(int sockfd, Message* msg) {
    if (send(sockfd, msg, sizeof(msg->length) + msg->length, 0) == -1) {
        perror(strerror(errno));
        return -1;
    }
    return 0;
}

int recv_message(int sockfd, Message* msg) {
    int bytes;
    bytes = recv(sockfd, &msg->length, sizeof(msg->length), 0);
    if (bytes == 0) {
        // close
        printf("[!] Disconnected\n");
        return -1;
    }
    if (bytes == -1) {
        // error
        perror(strerror(errno));
        return -1;
    }
    if (bytes != sizeof(msg->length)) {
        printf("[!] Incorrect header: expected size %ld, got %d\n", sizeof(msg->length), bytes); 
        return -1;
    }

    bytes = recv(sockfd, &msg->type, msg->length, 0);
    if (bytes == 0) {
        // close
        printf("[!] Disconnected\n");
        return -1;
    }
    if (bytes == -1) {
        // error
        perror(strerror(errno));
        return -1;
    }
    if ((size_t)bytes != msg->length) {
        printf( "[!] Incorrect payload: expected size %d, got %d\n", msg->length, bytes); 
        return -1;
    }
    return bytes;
}