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
#include <stdarg.h>

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

    MSG_AUTH = 7,        // аутентификация
    MSG_PRIVATE = 8,     // личное сообщение
    MSG_ERROR = 9,       // ошибка
    MSG_SERVER_INFO = 10 // системные сообщения
};

#define PORT 4445

#define MAX_NICKNAME_LEN 32
struct ConnectionInfo {
    int fd;
    struct sockaddr_in addr;

#ifdef SERVER_SPECIFIC 
    bool active;
    bool authorized;
    char nickname[MAX_NICKNAME_LEN];

    pthread_mutex_t personal_mutex;
#endif
};

int send_message(int sockfd, Message* msg) {    
    #ifdef SERVER_SPECIFIC 
    printf("[Layer 7] send_message\n");
    #endif

    #ifdef SERVER_SPECIFIC 
    printf("[Layer 4] send\n");
    #endif
    if (send(sockfd, msg, sizeof(msg->length) + msg->length, 0) == -1) {
        perror(strerror(errno));
        return -1;
    }

    
    return 0;
}

int recv_message(int sockfd, Message* msg) {
    #ifdef SERVER_SPECIFIC 
    printf("[Layer 7] recv_message\n");
    #endif

    #ifdef SERVER_SPECIFIC 
    printf("[Layer 4] recv\n");
    #endif
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

    if (msg->length > MAX_PAYLOAD) {
        printf("[!] Message payload length (%u) is bigger than MAX_PAYLOAD (%d)\n", msg->length, MAX_PAYLOAD); 
        return -1;
    }

    #ifdef SERVER_SPECIFIC 
    printf("[Layer 4] recv\n");
    #endif
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


Message* message_from_data(Message* msg, uint8_t type, const char* data, uint32_t data_length) {
    #ifdef SERVER_SPECIFIC 
    printf("[Layer 6] message_from_data\n");
    #endif
    msg->type = type;

    int bytes = data_length > MAX_PAYLOAD ? MAX_PAYLOAD : data_length;
    memcpy(msg->payload, data, bytes);

    msg->length = sizeof(msg->type) + bytes;

    return msg;
}

Message* message_from_fmt(Message* msg, uint8_t type, const char* format, ...) {
    #ifdef SERVER_SPECIFIC 
    printf("[Layer 6] message_from_fmt\n");
    #endif
    msg->type = type;
    
    va_list args;
    va_start(args, format);
    int bytes = vsnprintf(msg->payload, MAX_PAYLOAD, format, args);
    if (bytes > MAX_PAYLOAD) bytes = MAX_PAYLOAD;
    va_end(args);

    msg->length = sizeof(msg->type) + bytes;
    
    return msg;
}