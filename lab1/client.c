#include "common.h"

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        fprintf(stderr, "%s\n", strerror(errno));
        return 1;
    }

    struct sockaddr_in server_addr;
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = PORT;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    char buffer[1024];
    char message[1024];

    while (1) {
        printf(" > ");
        fgets(message, sizeof(message), stdin);
        size_t message_length = strlen(message);
        if (message[message_length-1] == '\n') {
            message[message_length-1] = '\0';
            message_length--;
        }

        sendto(sockfd, message, message_length, 0, (const struct sockaddr*)&server_addr, sizeof(server_addr));
        socklen_t addrlen = sizeof(server_addr);
        int n = recvfrom(sockfd, buffer, sizeof(buffer)-1, 0, (struct sockaddr*)&server_addr, &addrlen);
        buffer[n] = '\0';

        printf(" < %s\n", buffer);
    }

    close(sockfd);
    return 0;
}