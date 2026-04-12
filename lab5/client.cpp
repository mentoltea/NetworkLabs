#include "common.hpp"
#include <pthread.h>
#include <string>
#include <cstring>

pthread_mutex_t focus_mutex = PTHREAD_MUTEX_INITIALIZER;
char my_nickname[MAX_NAME];

void* listen_incoming(void* _data) {
    ConnectionInfo* conn = (ConnectionInfo*)_data;
    MessageEx msg;
    bool should_exit = false;
    
    while (!should_exit) {
        pthread_mutex_unlock(&focus_mutex);
        int bytes = recv_message(conn->fd, &msg, nullptr, 0);
        if (bytes == -1) break;
        pthread_mutex_lock(&focus_mutex);

        switch (msg.type) {
            case MSG_TEXT:
                printf("[%s][id=%u][%s]: %s\n", time_to_str(msg.timestamp).c_str(), msg.msg_id, msg.sender, msg.payload);
                break;
            case MSG_PRIVATE:
                printf("[%s][id=%u][PRIVATE][%s -> %s]: %s\n", time_to_str(msg.timestamp).c_str(), msg.msg_id, msg.sender, msg.receiver, msg.payload);
                break;
            case MSG_PONG:
                printf("[SERVER]: PONG\n");
                break;
            case MSG_ERROR:
                printf("[ERROR] %s\n", msg.payload);
                break;
            case MSG_SERVER_INFO:
                printf("[INFO]\n%s\n", msg.payload);
                break;
            case MSG_HISTORY_DATA:
                printf("=== History ===\n%s", msg.payload);
                break;
            case MSG_BYE:
                printf("Server closed connection\n");
                should_exit = true;
                break;
            default:
                printf("Unexpected message type %d\n", msg.type);
                should_exit = true;
                break;
        }
    }
    pthread_mutex_unlock(&focus_mutex);
    pthread_exit(nullptr);
}

int main() {
    printf("Enter nickname: ");
    fgets(my_nickname, MAX_NAME, stdin);
    size_t len = strlen(my_nickname);
    if (len && (my_nickname[len-1] == '\n')) my_nickname[len-1] = '\0';

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) { perror("socket"); return 1; }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sockfd, (const struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    // Аутентификация
    MessageEx msg;
    init_message(&msg, MSG_AUTH, my_nickname, "", my_nickname, 0, time(nullptr));
    if (send_message(sockfd, &msg) == -1) {
        close(sockfd);
        return 1;
    }
    if (recv_message(sockfd, &msg, nullptr, 0) == -1) {
        close(sockfd);
        return 1;
    }
    if (msg.type == MSG_ERROR) {
        printf("Auth error: %s\n", msg.payload);
        close(sockfd);
        return 1;
    }
    if (msg.type != MSG_WELCOME) {
        printf("Protocol error\n");
        close(sockfd);
        return 1;
    }
    printf("Connected to server\n");

    pthread_t tid;
    ConnectionInfo conn = { sockfd, server_addr, false, false, "" };
    pthread_create(&tid, nullptr, listen_incoming, &conn);
    pthread_detach(tid);

    char buffer[MAX_PAYLOAD];
    while (1) {
        pthread_mutex_unlock(&focus_mutex);
        fgets(buffer, MAX_PAYLOAD, stdin);
        size_t blen = strlen(buffer);
        if (blen && (buffer[blen-1] == '\n')) buffer[blen-1] = '\0';
        pthread_mutex_lock(&focus_mutex);

        if (strcmp(buffer, "/quit") == 0) {
            init_message(&msg, MSG_BYE, my_nickname, "", "", 0, time(nullptr));
            send_message(sockfd, &msg);
            break;
        } else if (strcmp(buffer, "/ping") == 0) {
            init_message(&msg, MSG_PING, my_nickname, "", "", 0, time(nullptr));
            send_message(sockfd, &msg);
        } else if (strcmp(buffer, "/list") == 0) {
            init_message(&msg, MSG_LIST, my_nickname, "", "", 0, time(nullptr));
            send_message(sockfd, &msg);
        } else if (strncmp(buffer, "/history", 8) == 0) {
            int n = 0;
            if (sscanf(buffer, "/history %d", &n) == 1 && n > 0) {
                char num[16];
                snprintf(num, sizeof(num), "%d", n);
                init_message(&msg, MSG_HISTORY, my_nickname, "", num, 0, time(nullptr));
            } else {
                init_message(&msg, MSG_HISTORY, my_nickname, "", "", 0, time(nullptr));
            }
            send_message(sockfd, &msg);
        } else if (strncmp(buffer, "/w ", 3) == 0) {
            char* space = strchr(buffer+3, ' ');
            if (space) {
                char target[MAX_NAME];
                int nick_len = space - (buffer+3);
                if (nick_len >= MAX_NAME) nick_len = MAX_NAME-1;
                strncpy(target, buffer+3, nick_len);
                target[nick_len] = '\0';
                const char* text = space+1;
                char full[MAX_PAYLOAD];
                snprintf(full, MAX_PAYLOAD, "%s:%s", target, text);
                init_message(&msg, MSG_PRIVATE, my_nickname, target, full, 0, time(nullptr));
                send_message(sockfd, &msg);
            } else {
                printf("Usage: /w <nick> <message>\n");
            }
        } else if (strcmp(buffer, "/help") == 0) {
            printf("Available commands:\n"
                   "/help\n/list\n/history\n/history N\n/quit\n/w <nick> <message>\n/ping\n");
        } else if (strlen(buffer) > 0) {
            init_message(&msg, MSG_TEXT, my_nickname, "", buffer, 0, time(nullptr));
            send_message(sockfd, &msg);
        }
    }

    close(sockfd);
    return 0;
}