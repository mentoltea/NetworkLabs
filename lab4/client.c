#include "common.h"

pthread_mutex_t focus_mutex = PTHREAD_MUTEX_INITIALIZER;

void* listen_incoming(void* _data) {
    struct ConnectionInfo * data = (struct ConnectionInfo *) _data;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &data->addr.sin_addr, ip, sizeof(ip));
    int port = ntohs(data->addr.sin_port);

    Message message;
    while (1) {
        pthread_mutex_unlock(&focus_mutex);

        int bytes;
        if ( (bytes = recv_message(data->fd, &message)) == -1) {
            goto EXIT;
        }
        int payload_length = bytes - sizeof(message.type);

        pthread_mutex_lock(&focus_mutex);
        
        switch (message.type) {
        case MSG_HELLO: {
        } break;
        
        case MSG_TEXT: 
        {
            printf("\n[PUBLIC] %.*s\n", payload_length, message.payload);
            continue;
        } break;  

        case MSG_PRIVATE:
        {
            printf("\n[PRIVATE] %.*s\n", payload_length, message.payload);
            continue;
        } break;  
        
        case MSG_PING: {
            printf("[%s:%d:!] PING\n", ip, port);
            continue;
        } break;  
        
        case MSG_PONG: {
            printf("[%s:%d:!] PONG\n", ip, port);
            continue;
        } break;  

        case MSG_BYE: {
            printf("[%s:%d:!] Gracefully disconnected\n", ip, port);
            goto EXIT;
        }break;

        case MSG_ERROR: {
            printf("\n[ERROR] %.*s\n", payload_length, message.payload);
            continue;
        } break;  
        
        case MSG_SERVER_INFO: {
            printf("\n[INFO] %.*s\n", payload_length, message.payload);
            continue;
        } break;  

        default: {
            printf( "[%s:%d:!] Unexpected message type: %d\n", ip, port, message.type); 
            goto EXIT;
        } break;
        }
    }

EXIT:
    pthread_mutex_unlock(&focus_mutex);
    exit(0);
}

int main() {
    char nickname[MAX_NICKNAME_LEN];
    printf("Enter nickname: ");
    fgets(nickname, MAX_NICKNAME_LEN, stdin);
    size_t nickname_length = strlen(nickname);
    if (nickname_length>0 && (nickname[nickname_length-1] == '\n' || nickname[nickname_length-1] == '\r')) {
        nickname[nickname_length-1] = '\0';
        nickname_length--;
    }

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

    message_from_fmt(&messageTo, MSG_AUTH, "%s", nickname);

    if (send_message(sockfd, &messageTo) == -1) {
        goto EXIT;
    }
    if (recv_message(sockfd, &messageFrom) == -1) {
        goto EXIT;
    }

    if (messageFrom.type == MSG_ERROR) {
        int payload_length = messageFrom.length - sizeof(messageFrom.type);
        printf("\n[ERROR] %.*s\n", payload_length, messageFrom.payload);
        goto EXIT;
    }
    if (messageFrom.type != MSG_WELCOME) {
        printf( "[!] Incorrect protocol: expected MSG_WELCOME, got %d\n", messageFrom.type); 
        goto EXIT;
    }

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &server_addr.sin_addr, ip, sizeof(ip));
    int port = ntohs(server_addr.sin_port);
    
    printf("Welcome to %s:%d\n", ip, port);

    pthread_t thread;

    struct ConnectionInfo conninfo = {
        .fd = sockfd,
        .addr = server_addr,
    };
    pthread_create(&thread, NULL, listen_incoming, &conninfo);
    pthread_detach(thread);

    char buffer[MAX_PAYLOAD];
    while (connected) {
        pthread_mutex_unlock(&focus_mutex);
        
        fgets(buffer, sizeof(buffer), stdin);
        size_t buffer_length = strlen(buffer);
        while (buffer[buffer_length-1] == '\n' || buffer[buffer_length-1] == '\r') {
            buffer[buffer_length-1] = '\0';
            buffer_length--;
        }

        pthread_mutex_lock(&focus_mutex);
        
        if (strcmp(buffer, "/ping") == 0) {
            // ping
            message_from_data(&messageTo, MSG_PING, NULL, 0);
            if (send_message(sockfd, &messageTo) == -1) goto EXIT;
        }
        else if (strcmp(buffer, "/quit") == 0) {
            // quit
            message_from_data(&messageTo, MSG_BYE, NULL, 0);
            if (send_message(sockfd, &messageTo) == -1) goto EXIT;
            break;
        }
        else if (
            buffer[0] == '/' && buffer[1] == 'w'
        ) {
            // quit
            char* nickname_start = strchr(buffer, ' ') + 1;
            if (nickname_start == NULL) continue;

            char* nickname_end = strchr(nickname_start, ' ');
            if (nickname_end == NULL) continue;

            int nickname_length = nickname_end - nickname_start;

            message_from_fmt(&messageTo, MSG_PRIVATE, "%.*s:%s", nickname_length, nickname_start, nickname_end+1);

            if (send_message(sockfd, &messageTo) == -1) goto EXIT;
        }
        else {
            // text
            message_from_data(&messageTo, MSG_TEXT, buffer, buffer_length);
            if (send_message(sockfd, &messageTo) == -1) goto EXIT;
        }

    }
    
    
EXIT:
    pthread_mutex_unlock(&focus_mutex);
    close(sockfd);
    printf("[!] Connection closed\n");

    return 0;
}