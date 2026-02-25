#include "common.h"

#include <pthread.h>

struct ConnectionInfo {
    int conn_fd;
    struct sockaddr_in client_addr;
};

void *handle_connection(void* _data) {
    struct ConnectionInfo* data = _data;

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &data->client_addr.sin_addr, ip, sizeof(ip));
    int port = ntohs(data->client_addr.sin_port);

    printf("[%s:%d:!] New connection\n", ip, port);

    bool connected = true;
    
    Message messageFrom;
    
    Message messageTo;
    bool send_message;

    bool greated = false;

    int bytes = 0;
    while (connected) {
        send_message = false;
        bytes = recv(data->conn_fd, &messageFrom.length, sizeof(messageFrom.length), 0);
        if (bytes == 0) {
            // close
            printf("[%s:%d:!] Disconnected\n", ip, port);
            connected = false;
            continue;
        }
        if (bytes == -1) {
            // error
            perror(strerror(errno));
            connected = false;
            continue;
        }
        if (bytes != sizeof(messageFrom.length)) {
            printf( "[%s:%d!] Incorrect header: expected size %ld, got %d\n", ip, port, sizeof(messageFrom.length), bytes); 
            connected = false;
            continue;
        }

        if (messageFrom.length > MAX_PAYLOAD + sizeof(messageFrom.type)) {
            printf( "[%s:%d!] Message is too long (max %ld): %d\n", ip, port, MAX_PAYLOAD + sizeof(messageFrom.type), messageFrom.length); 
            connected = false;
            continue;
        }

        bytes = recv(data->conn_fd, &messageFrom.type, messageFrom.length, 0);
        if (bytes == 0) {
            // close
            printf("[%s:%d:!] Disconnected\n", ip, port);
            connected = false;
            continue;
        }
        if (bytes == -1) {
            // error
            perror(strerror(errno));
            connected = false;
            continue;
        }
        if ((size_t)bytes != messageFrom.length) {
            printf( "[%s:%d!] Incorrect payload: expected size %d, got %d\n", ip, port, messageFrom.length, bytes); 
            connected = false;
            continue;
        }

        int payload_length = bytes - sizeof(messageFrom.type);

        switch (messageFrom.type) {
        case MSG_HELLO: {
            messageTo.length = sizeof(messageTo.type);
            messageTo.type = MSG_WELCOME;
            send_message = true;
            greated = true;
        } break;
        case MSG_WELCOME: {
            printf( "[%s:%d:!] Incorrect protocol: Got MSG_WELCOME from client\n", ip, port); 
            connected = false;
            continue;
        } break;  
        case MSG_TEXT: {
            if (!greated) {
                printf( "[%s:%d:!] Incorrect protocol: Got MSG_TEXT before greating\n", ip, port); 
                connected = false;
                continue;
            }
            printf("[%s:%d] %.*s\n", ip, port, payload_length, messageFrom.payload);
        } break;  
        case MSG_PING: {
            if (!greated) {
                printf( "[%s:%d:!] Incorrect protocol: Got MSG_PING before greating\n", ip, port); 
                connected = false;
                continue;
            }
            messageTo.length = sizeof(messageTo.type);
            messageTo.type = MSG_PONG;
            send_message = true;
        } break;  
        case MSG_PONG: {
            printf( "[%s:%d:!] Incorrect protocol: Got MSG_PONG from client\n", ip, port); 
            connected = false;
            continue;
        } break;  
        case MSG_BYE: {
            printf("[%s:%d:!] Gracefully disconnected\n", ip, port);
            connected = false;
            continue;
        }break;  

        default: {
            printf( "[%s:%d:!] Unexpected message type: %d\n", ip, port, messageFrom.type); 
            connected = false;
            continue;
        } break;
        }

        if (send_message) {
            if (send(data->conn_fd, &messageTo, sizeof(messageTo.length) + messageTo.length, 0) == -1) {
                perror(strerror(errno));
                connected = false;
                continue;
            }
        }
    }

    close(data->conn_fd);
    printf("[%s:%d:!] Connection closed\n", ip, port);
    free(_data);
    return NULL;
}

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror(strerror(errno));
        return 1;
    }

    struct sockaddr_in server_addr, client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = PORT;
    

    if (bind(sockfd, (const struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror(strerror(errno));
        close(sockfd);
        return 1;
    }

#define MAX_CLIENTS 16
    if (listen(sockfd, MAX_CLIENTS) == -1) {
        perror(strerror(errno));
        return 1;
    }
    printf("Server is listening on port %d\n", server_addr.sin_port);
    
    while (1) {
        socklen_t addrlen = sizeof(client_addr);
        int connfd = accept(sockfd, (struct sockaddr*) &client_addr, &addrlen);
        if (connfd == -1) {
            perror(strerror(errno));
            continue;
        }

        struct ConnectionInfo* data = calloc(1, sizeof(struct ConnectionInfo));
        data->conn_fd = connfd;
        data->client_addr = client_addr;

        pthread_t thread;
        pthread_create(&thread, NULL, &handle_connection, data);
        pthread_detach(thread);
    }

    close(sockfd);
    return 0;
}
