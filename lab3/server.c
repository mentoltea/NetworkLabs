#include "common.h"

#include <pthread.h>

#define MAX_CLIENTS 16
struct ConnectionInfo clients[MAX_CLIENTS];
struct ConnectionInfo* pickup_client = NULL;
pthread_cond_t pickup_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t pickup_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t broadcast_mutex = PTHREAD_MUTEX_INITIALIZER;

void broadcast(
    int sender_fd,
    const char* sender_name,
    const char* string
) {
    pthread_mutex_lock(&broadcast_mutex);

    Message message;
    message.type = MSG_TEXT;
    snprintf(message.payload, MAX_PAYLOAD, "<%s> %s", sender_name, string);
    message.length = sizeof(message.type) + strlen(message.payload);
    

    for (int i=0; i<MAX_CLIENTS; i++) {
        if (
            sender_fd != clients[i].fd &&
            clients[i].active && 
            clients[i].greated
        ) {
            send_message(clients[i].fd, &message);
        }
    }

    pthread_mutex_unlock(&broadcast_mutex);
};

void *handle_connection(void* _data) {
    struct ConnectionInfo* data = _data;

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &data->addr.sin_addr, ip, sizeof(ip));
    int port = ntohs(data->addr.sin_port);

    printf("[%s:%d:!] New connection\n", ip, port);

    bool connected = true;
    
    Message messageFrom;
    
    Message messageTo;
    bool if_send_message;

    int bytes = 0;
    while (connected) {
        if_send_message = false;
        bytes = recv_message(data->fd, &messageFrom);
        if (bytes == -1) {
            goto EXIT;
        }

        int payload_length = bytes - sizeof(messageFrom.type);

        switch (messageFrom.type) {
        case MSG_HELLO: {
            messageTo.length = sizeof(messageTo.type);
            messageTo.type = MSG_WELCOME;
            
            if (payload_length > 0) {
                snprintf(data->nickname, MAX_NICKNAME_LEN, "%.*s", payload_length, messageFrom.payload);
            }

            if_send_message = true;
            data->greated = true;
        } break;
        case MSG_WELCOME: {
            printf( "[%s:%d:!] Incorrect protocol: Got MSG_WELCOME from client\n", ip, port); 
            connected = false;
            continue;
        } break;  
        case MSG_TEXT: {
            if (!data->greated) {
                printf( "[%s:%d:!] Incorrect protocol: Got MSG_TEXT before greating\n", ip, port); 
                connected = false;
                continue;
            }
            printf("[%s:%d] %.*s\n", ip, port, payload_length, messageFrom.payload);

            char string[MAX_PAYLOAD + 1];
            memcpy(string, messageFrom.payload, payload_length);
            string[payload_length] = '\0';

            if (data->nickname[0] != 0) {
                broadcast(data->fd, data->nickname, string);                
            } else {
                char temp[MAX_NICKNAME_LEN];
                snprintf(temp, MAX_NICKNAME_LEN, "%d", port);
                broadcast(data->fd, temp, string);
            }
        } break;  
        case MSG_PING: {
            if (!data->greated) {
                printf( "[%s:%d:!] Incorrect protocol: Got MSG_PING before greating\n", ip, port); 
                connected = false;
                continue;
            }
            messageTo.length = sizeof(messageTo.type);
            messageTo.type = MSG_PONG;
            if_send_message = true;
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

        if (if_send_message) {
            if (send_message(data->fd, &messageTo) == -1) {
                perror(strerror(errno));
                goto EXIT;
            }
        }
    }

EXIT:
    data->active = false;
    close(data->fd);
    printf("[%s:%d:!] Connection closed\n", ip, port);
    return NULL;
}

void *poll_thread(void* _data) {
    (void)(_data);

    while (1) {
        pthread_cond_wait(&pickup_cond, &pickup_mutex);
        pthread_mutex_unlock(&pickup_mutex);
        if (pickup_client) handle_connection( pickup_client );
    }
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

    if (listen(sockfd, MAX_CLIENTS) == -1) {
        perror(strerror(errno));
        return 1;
    }
    memset(clients, 0, sizeof(clients));

    for (int i=0; i<MAX_CLIENTS; i++) {
        pthread_t thread;
        pthread_create(&thread, NULL, poll_thread, NULL);
        pthread_detach(thread);
    }

    printf("Server is listening on port %d\n", server_addr.sin_port);
    
    while (1) {
        socklen_t addrlen = sizeof(client_addr);
        int connfd = accept(sockfd, (struct sockaddr*) &client_addr, &addrlen);
        if (connfd == -1) {
            perror(strerror(errno));
            continue;
        }

        struct ConnectionInfo* data = NULL;

        for (int i=0; i<MAX_CLIENTS; i++) {
            if (!clients[i].active) {
                data = &clients[i];
                break;
            }
        }
        
        if (!data) {
            close(connfd);
            printf("All threads are busy, client is ignored...\n");
            continue;
        }

        data->fd = connfd;
        data->addr = client_addr;
        data->active = true;
        data->greated = false;
        memset(data->nickname, 0, sizeof(data->nickname));

        pickup_client = data;
        pthread_cond_signal(&pickup_cond);
    }

    close(sockfd);
    return 0;
}
