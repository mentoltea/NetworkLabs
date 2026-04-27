#include "common.hpp"
#include <cstdarg>
#include <ctime>
#include <string>

void log_recv(const char* src_ip, uint16_t src_port, const char* dst_ip, uint16_t dst_port, const char* proto, size_t bytes) {
    printf("[Network Access] frame arrived from network interface\n");
    printf("[Internet] simulated IP hdr: src=%s dst=%s proto=%s\n", src_ip, dst_ip, proto);
    printf("[Transport] simulated TCP hdr: src_port=%u dst_port=%u seq=0 ack=0\n", src_port, dst_port);
    printf("[Application] recv() %zu bytes via %s\n", bytes, proto);
}

void log_send(const char* dst_ip, uint16_t dst_port, const char* proto, size_t bytes) {
    (void)(dst_port);
    printf("[Application] prepare message\n");
    printf("[Transport] send() %zu bytes via %s\n", bytes, proto);
    printf("[Internet] destination ip = %s\n", dst_ip);
    printf("[Network Access] frame sent to network interface\n");
}

void log_application(const char* action) {
    printf("[Application] %s\n", action);
}

void log_transport(const char* subsystem, const char* action) {
    printf("[Transport][%s] %s\n", subsystem, action);
}

int send_message(int sockfd, MessageEx* msg, const char* dst_ip, uint16_t dst_port) {
    msg->length = sizeof(MessageEx);
    ssize_t sent = send(sockfd, msg, msg->length, 0);
    if (sent == -1) {
        perror("send");
        return -1;
    }
    if (dst_ip && dst_port)
        log_send(dst_ip, dst_port, "TCP", sent);
    return 0;
}

int recv_message(int sockfd, MessageEx* msg, char* serv_ip, char* src_ip, uint16_t src_port) {
    uint32_t len;
    ssize_t bytes = recv(sockfd, &len, sizeof(len), MSG_WAITALL);
    if (bytes <= 0) return -1;
    if (bytes != sizeof(len)) return -1;
    if (len > sizeof(MessageEx)) return -1;

    bytes = recv(sockfd, (char*)msg + sizeof(len), len - sizeof(len), MSG_WAITALL);
    if (bytes <= 0) return -1;
    msg->length = len;

    if (src_ip && src_port) {
        log_recv(src_ip, src_port, serv_ip, src_port, "TCP", bytes + sizeof(len));
    }
    return bytes + sizeof(len);
}

void init_message(MessageEx* msg, uint8_t type, const char* sender, const char* receiver,
                  const char* payload, uint32_t msg_id, time_t ts) {
    msg->type = type;
    msg->msg_id = msg_id;
    strncpy(msg->sender, sender, MAX_NAME-1);
    msg->sender[MAX_NAME-1] = '\0';
    strncpy(msg->receiver, receiver, MAX_NAME-1);
    msg->receiver[MAX_NAME-1] = '\0';
    msg->timestamp = ts;
    strncpy(msg->payload, payload, MAX_PAYLOAD-1);
    msg->payload[MAX_PAYLOAD-1] = '\0';
    msg->length = sizeof(MessageEx);
}

void set_payload(MessageEx* msg, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg->payload, MAX_PAYLOAD, fmt, args);
    va_end(args);
}

MessageEx* message_from_fmt(MessageEx* msg, uint8_t type, const char* format, ...) {
    msg->type = type;
    va_list args;
    va_start(args, format);
    vsnprintf(msg->payload, MAX_PAYLOAD, format, args);
    va_end(args);
    msg->payload[MAX_PAYLOAD-1] = '\0';
    msg->length = sizeof(MessageEx);
    strncpy(msg->sender, "Server", MAX_NAME-1);
    msg->sender[MAX_NAME-1] = '\0';
    msg->receiver[0] = '\0';
    msg->timestamp = time(nullptr);
    msg->msg_id = 0;
    return msg;
}

MessageEx* message_from_data(MessageEx* msg, uint8_t type, const char* data, uint32_t data_length) {
    msg->type = type;
    if (data && data_length > 0) {
        uint32_t copy_len = data_length > MAX_PAYLOAD-1 ? MAX_PAYLOAD-1 : data_length;
        memcpy(msg->payload, data, copy_len);
        msg->payload[copy_len] = '\0';
    } else {
        msg->payload[0] = '\0';
    }
    msg->length = sizeof(MessageEx);
    strncpy(msg->sender, "Server", MAX_NAME-1);
    msg->sender[MAX_NAME-1] = '\0';
    msg->receiver[0] = '\0';
    msg->timestamp = time(nullptr);
    msg->msg_id = 0;
    return msg;
}

std::string time_to_str(time_t t) {
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
    return std::string(buf);
}