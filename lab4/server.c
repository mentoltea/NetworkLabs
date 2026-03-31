#define SERVER_SPECIFIC
#include "common.h"

#include <pthread.h>

#define MAX_CLIENTS 16
struct ConnectionInfo clients[MAX_CLIENTS];
struct ConnectionInfo* pickup_client = NULL;
pthread_cond_t pickup_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t pickup_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t broadcast_mutex = PTHREAD_MUTEX_INITIALIZER;

void broadcast(
    Message *message,
    struct ConnectionInfo* ignore
) {
    #ifdef SERVER_SPECIFIC 
    printf("[Layer 7] broadcast\n");
    #endif
    pthread_mutex_lock(&broadcast_mutex);

    for (int i=0; i<MAX_CLIENTS; i++) {
        if (
            &clients[i] != ignore &&
            clients[i].active && 
            clients[i].authorized
        ) {
            pthread_mutex_lock(&clients[i].personal_mutex);
            send_message(clients[i].fd, message);
            pthread_mutex_unlock(&clients[i].personal_mutex);
        }
    }

    pthread_mutex_unlock(&broadcast_mutex);
};

pthread_mutex_t nickname_mutex = PTHREAD_MUTEX_INITIALIZER;
struct ConnectionInfo* find_user_by_nickname(const char* nickname, struct ConnectionInfo* ignore) {
    pthread_mutex_lock(&nickname_mutex);
    struct ConnectionInfo* found = NULL;
    for (int i=0; i<MAX_CLIENTS; i++) {
        if (
            &clients[i] != ignore &&
            clients[i].active && 
            clients[i].authorized
        ) {
            if (strcmp(nickname, clients[i].nickname) == 0) found = &clients[i];
        }
    }

    pthread_mutex_unlock(&nickname_mutex);
    return found;
}

void *handle_connection(void* _data) {
    struct ConnectionInfo* client = _data;

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client->addr.sin_addr, ip, sizeof(ip));
    int port = ntohs(client->addr.sin_port);

    printf("[%s:%d:!] New connection\n", ip, port);

    bool connected = true;
    
    Message messageFrom;
    Message messageTo;
    bool if_send_message;

    bool disconnected_gracefully = false;
    bool disconnected_protocol_violation = false;
    bool disconnected_unexpected = false;

    int bytes = 0;
    
    while (!client->authorized) {
        bytes = recv_message(client->fd, &messageFrom);
        if (bytes == -1) {
            goto EXIT;
        }
        int payload_length = bytes - sizeof(messageFrom.type);

        if (messageFrom.type == MSG_AUTH) {
            #ifdef SERVER_SPECIFIC 
            printf("[Layer 6] MSG_AUTH\n");
            #endif
            pthread_mutex_lock(&nickname_mutex);
            bytes = snprintf(client->nickname, MAX_NICKNAME_LEN, "%.*s", payload_length, messageFrom.payload);
            pthread_mutex_unlock(&nickname_mutex);

            if (bytes == 0) {
                send_message(
                    client->fd, 
                    message_from_fmt(&messageTo, MSG_ERROR, "Empty nickname is invalid")
                );
                // goto EXIT;
                #ifdef SERVER_SPECIFIC 
                printf("[Layer 5] authorization failed\n");
                #endif
            }

            if (find_user_by_nickname(client->nickname, client)) {
                send_message(
                    client->fd, 
                    message_from_fmt(&messageTo, MSG_ERROR, "Nickname is already taken")
                );
                // goto EXIT;
                #ifdef SERVER_SPECIFIC 
                printf("[Layer 5] authorization failed\n");
                #endif
            }
            
            // success
            #ifdef SERVER_SPECIFIC 
            printf("[Layer 5] authorization succeed\n");
            #endif
            broadcast(
                message_from_fmt(
                    &messageTo, MSG_SERVER_INFO, 
                    "User %s connected!", 
                    client->nickname
                ), 
                client
            );
            send_message(client->fd, message_from_data(&messageTo, MSG_WELCOME, NULL, 0));
            client->authorized = true;
        }
    }
    


    while (connected) {
        if_send_message = false;
        bytes = recv_message(client->fd, &messageFrom);
        if (bytes == -1) {
            disconnected_unexpected = true;
            goto EXIT;
        }
        int payload_length = bytes - sizeof(messageFrom.type);

        switch (messageFrom.type) {
        case MSG_HELLO: {
            // old protocol
        } break;
        case MSG_WELCOME: {
            // old protocol
        } break;  

        case MSG_TEXT: {
            #ifdef SERVER_SPECIFIC 
            printf("[Layer 6] MSG_TEXT\n");
            #endif

            printf("[%s:%d][TEXT] %.*s\n", ip, port, payload_length, messageFrom.payload);

            broadcast(
                message_from_fmt(
                    &messageTo, MSG_TEXT, 
                    "[%s] %.*s", 
                    client->nickname, 
                    payload_length,
                    messageFrom.payload
                ), 
                client
            );
        } break; 
        
        case MSG_PRIVATE:{
            #ifdef SERVER_SPECIFIC 
            printf("[Layer 6] MSG_PRIVATE\n");
            #endif

            printf("[%s:%d][PRIVATE] %.*s\n", ip, port, payload_length, messageFrom.payload);

            char temp[MAX_PAYLOAD + 1];
            snprintf(temp, MAX_PAYLOAD, "%.*s", messageFrom.length, messageFrom.payload);
            temp[MAX_PAYLOAD] = '\0';

            char* end = strchr(temp, ':');
            char target_nickname[MAX_NICKNAME_LEN];
            int nickname_len = end-temp;

            if (nickname_len >= MAX_NICKNAME_LEN) {
                message_from_fmt(&messageTo, MSG_ERROR, "Nickname length is too big");
                if_send_message = true;
                break;
            }
            snprintf(target_nickname, MAX_NICKNAME_LEN, "%.*s", nickname_len, temp);
            // printf("%s\n", target_nickname);
            
            struct ConnectionInfo* user = find_user_by_nickname(target_nickname, NULL);

            if (user == NULL) {
                message_from_fmt(&messageTo, MSG_ERROR, "No user with such nickname");
                if_send_message = true;
                break;
            }

            pthread_mutex_lock(&user->personal_mutex);
            send_message(
                user->fd,
                message_from_fmt(
                    &messageTo, MSG_PRIVATE,
                    "[%s] %s",
                    target_nickname, 
                    end + 1
                ) 
            );
            pthread_mutex_unlock(&user->personal_mutex);
        } break;

        case MSG_PING: {
            #ifdef SERVER_SPECIFIC 
            printf("[Layer 6] MSG_PING\n");
            #endif
            message_from_data(&messageTo, MSG_PONG, NULL, 0);
            if_send_message = true;
        } break;

        case MSG_BYE: {
            #ifdef SERVER_SPECIFIC 
            printf("[Layer 6] MSG_BYE\n");
            #endif
            printf("[%s:%d:!] Gracefully disconnected\n", ip, port);
            connected = false;
            disconnected_gracefully = true;
        }break;

        case MSG_AUTH: {
            #ifdef SERVER_SPECIFIC 
            printf("[Layer 6] MSG_AUTH\n");
            #endif
            message_from_fmt(&messageTo, MSG_ERROR, "Cannot authenticate twice in one session");
            if_send_message = true;
            connected = false;
            disconnected_protocol_violation = true;
        } break;

        case MSG_ERROR:{
            // ???
        } break;

        case MSG_SERVER_INFO:{
            // ??????
        } break;
        
        default: {
            #ifdef SERVER_SPECIFIC 
            printf("[Layer 6] Unknown message\n");
            #endif

            printf( "[%s:%d:!] Unexpected message type: %d\n", ip, port, messageFrom.type); 
            connected = false;
            disconnected_protocol_violation = true;
            continue;
        } break;
        }

        if (if_send_message) {
            pthread_mutex_lock(&client->personal_mutex);
            send_message(client->fd, &messageTo);
            pthread_mutex_unlock(&client->personal_mutex);
        }
    }
    
EXIT:
    if (client->authorized) {
        if (disconnected_gracefully) {
            broadcast(
                message_from_fmt(
                    &messageTo, MSG_SERVER_INFO, 
                    "User %s disconnected :(", 
                    client->nickname
                ), 
                client
            );
        }

        if (disconnected_protocol_violation) {
            broadcast(
                message_from_fmt(
                    &messageTo, MSG_SERVER_INFO, 
                    "User %s disconnected due to protocol violation >:(", 
                    client->nickname
                ), 
                client
            );
        }

        if (disconnected_unexpected) {
            broadcast(
                message_from_fmt(
                    &messageTo, MSG_SERVER_INFO, 
                    "User %s dropped the connection unexpectedly :O", 
                    client->nickname
                ), 
                client
            );
        }
    } 
    client->active = false;
    pthread_mutex_destroy(&client->personal_mutex);
    close(client->fd);
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

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
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
        data->authorized = false;
        
        pthread_mutex_init(&data->personal_mutex, NULL);

        memset(data->nickname, 0, sizeof(data->nickname));

        pickup_client = data;
        pthread_cond_signal(&pickup_cond);
    }

    close(sockfd);
    return 0;
}
