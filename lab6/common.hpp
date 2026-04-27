#ifndef COMMON_HPP
#define COMMON_HPP

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#define MAX_NAME       32
#define MAX_PAYLOAD    256
#define MAX_TIME_STR   32
#define PORT           4445
#define MAX_CLIENTS    16
#define MAX_RETRIES    5
#define ACK_TIMEOUT    20

typedef struct {
    uint32_t length;
    uint8_t  type;
    uint32_t msg_id;       
    char     sender[MAX_NAME];
    char     receiver[MAX_NAME];
    time_t   timestamp;
    char     payload[MAX_PAYLOAD];
} MessageEx;

enum {
    MSG_HELLO        = 1,
    MSG_WELCOME      = 2,
    MSG_TEXT         = 3,
    MSG_PING         = 4,
    MSG_PONG         = 5,
    MSG_BYE          = 6,

    MSG_AUTH         = 7,
    MSG_PRIVATE      = 8,
    MSG_ERROR        = 9,
    MSG_SERVER_INFO  = 10,
	
	MSG_LIST         = 11,
    MSG_HISTORY      = 12,
    MSG_HISTORY_DATA = 13,
    MSG_HELP         = 14,
    
    MSG_ACK          = 15
};

struct OfflineMsg {
    uint32_t msg_id;
    char sender[MAX_NAME];
    char receiver[MAX_NAME];
    char text[MAX_PAYLOAD];
    time_t timestamp;
};

struct ConnectionInfo {
    int fd;
    struct sockaddr_in addr;
    bool active;
    bool authorized;
    char nickname[MAX_NAME];
    pthread_mutex_t personal_mutex;
    char ip[INET_ADDRSTRLEN];
    uint32_t last_ids[32];
    int last_ids_count;
    uint32_t last_client_msg_id;
};

typedef struct {
    MessageEx msg;
    time_t send_time;
    int retries;
    time_t original_timestamp;
} PendingMsg;

void log_recv(const char* src_ip, uint16_t src_port, const char* dst_ip, uint16_t dst_port, const char* proto, size_t bytes);
void log_send(const char* dst_ip, uint16_t dst_port, const char* proto, size_t bytes);
void log_application(const char* action);
void log_transport(const char* subsystem, const char* action);

int send_message(int sockfd, MessageEx* msg, const char* dst_ip = nullptr, uint16_t dst_port = 0);
int recv_message(int sockfd, MessageEx* msg, char* src_ip = nullptr, char* serv_ip = nullptr, uint16_t src_port = 0);

void init_message(MessageEx* msg, uint8_t type, const char* sender, const char* receiver,
                  const char* payload, uint32_t msg_id, time_t ts);
void set_payload(MessageEx* msg, const char* fmt, ...);

MessageEx* message_from_fmt(MessageEx* msg, uint8_t type, const char* format, ...);
MessageEx* message_from_data(MessageEx* msg, uint8_t type, const char* data, uint32_t data_length);

std::string time_to_str(time_t t);

#endif