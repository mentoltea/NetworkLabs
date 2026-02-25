#include "common.h"

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
    return 0;
}

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror(strerror(errno));
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = PORT;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sockfd, (const struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror(strerror(errno));
        return 1;
    }

    Message messageFrom, messageTo;
    bool connected = true;

    messageTo.length = sizeof(messageTo.type);
    messageTo.type = MSG_HELLO;

    if (send_message(sockfd, &messageTo) == -1) {
        goto EXIT;
    }
    if (recv_message(sockfd, &messageFrom) == -1) {
        goto EXIT;
    }

    if (messageFrom.type != MSG_WELCOME) {
        printf( "[!] Incorrect protocol: expected MSG_WELCOME, got %d\n", messageFrom.type); 
        goto EXIT;
    }

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &server_addr.sin_addr, ip, sizeof(ip));
    int port = ntohs(server_addr.sin_port);
    
    printf("Welcome %s:%d\n", ip, port);

    char buffer[MAX_PAYLOAD];
    while (connected) {
        fputs("> ", stdout);
        fgets(buffer, sizeof(buffer), stdin);
        size_t buffer_length = strlen(buffer);
        if (buffer[buffer_length-1] == '\n') {
            buffer[buffer_length-1] = '\0';
            buffer_length--;
        }

        if (strcmp(buffer, "/ping") == 0) {
            // ping
            messageTo.length = sizeof(messageTo.type);
            messageTo.type = MSG_PING;
            
            if (send_message(sockfd, &messageTo) == -1) goto EXIT;
            if (recv_message(sockfd, &messageFrom) == -1) goto EXIT;
            if (messageFrom.type != MSG_PONG) {
                printf( "[!] Incorrect protocol: expected MSG_PONG, got %d\n", messageFrom.type); 
                goto EXIT;
            }
            printf("< PONG\n");
        }
        else if (strcmp(buffer, "/quit") == 0) {
            // quit
            messageTo.length = sizeof(messageTo.type);
            messageTo.type = MSG_BYE;
            if (send_message(sockfd, &messageTo) == -1) goto EXIT;
            break;
        }
        else {
            // text
            messageTo.length = sizeof(messageTo.type) + buffer_length;
            messageTo.type = MSG_TEXT;
            memcpy(&messageTo.payload, buffer, buffer_length);
            if (send_message(sockfd, &messageTo) == -1) goto EXIT;
        }
    }


EXIT:
    close(sockfd);
    printf("[!] Connection closed\n");

    return 0;
}