#include "common.h"


int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        fprintf(stderr, "%s\n", strerror(errno));
        return 1;
    }
    
    struct sockaddr_in server_addr, client_addr;
    
    memset(&client_addr, 0, sizeof(client_addr));
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = PORT;
    
    if (bind(sockfd, (const struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        fprintf(stderr, "%s\n", strerror(errno));
        close(sockfd);
        return 1;
    }

    char buffer[1024];
    char ip[INET_ADDRSTRLEN];
    printf("Server is listening on port %d\n", PORT);
    while (1) {
        socklen_t addrlen = sizeof(client_addr);
        int n = recvfrom(sockfd, buffer, sizeof(buffer)-1, 0, (struct sockaddr*)&client_addr, &addrlen);
        buffer[n] = '\0';

        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));

        int port = ntohs(client_addr.sin_port);
        
        sendto(sockfd, buffer, n, 0, (const struct sockaddr*)&client_addr, sizeof(client_addr));

        printf("%s:%d : %s\n", ip, port, buffer);
    }

    close(sockfd);
    return 0;
}