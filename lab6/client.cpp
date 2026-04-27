#include "common.hpp"
#include <pthread.h>
#include <string>
#include <cstring>
#include <vector>
#include <map>
#include <fstream>
#include <cmath>
#include <unistd.h>

pthread_mutex_t focus_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t pending_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ping_mutex = PTHREAD_MUTEX_INITIALIZER;

char my_nickname[MAX_NAME];
std::vector<PendingMsg> pending_queue;

struct PingResult {
    uint32_t msg_id;
    double send_time_ms;
    double rtt_ms;
    bool received;
};

std::vector<PingResult> ping_results;
bool ping_active = false;

void add_pending(const MessageEx& msg, time_t original_ts) {
    pthread_mutex_lock(&pending_mutex);
    PendingMsg pm;
    pm.msg = msg;
    pm.send_time = time(nullptr);
    pm.retries = 0;
    pm.original_timestamp = original_ts;
    pending_queue.push_back(pm);
    pthread_mutex_unlock(&pending_mutex);
}

void remove_pending(uint32_t msg_id) {
    pthread_mutex_lock(&pending_mutex);
    for (auto it = pending_queue.begin(); it != pending_queue.end(); ++it) {
        if (it->msg.msg_id == msg_id) {
            pending_queue.erase(it);
            break;
        }
    }
    pthread_mutex_unlock(&pending_mutex);
}

PendingMsg* find_pending(uint32_t msg_id) {
    for (auto& pm : pending_queue) {
        if (pm.msg.msg_id == msg_id) {
            return &pm;
        }
    }
    return nullptr;
}

void* retry_thread(void* arg) {
    int sockfd = *(int*)arg;
    
    while (1) {
        sleep(1);
        
        pthread_mutex_lock(&pending_mutex);
        std::vector<PendingMsg> to_retry;
        time_t now = time(nullptr);
        
        for (auto& pm : pending_queue) {
            if (difftime(now, pm.send_time) >= ACK_TIMEOUT) {
                to_retry.push_back(pm);
            }
        }
        pthread_mutex_unlock(&pending_mutex);
        
        for (auto& pm : to_retry) {
            pthread_mutex_lock(&pending_mutex);
            PendingMsg* current = find_pending(pm.msg.msg_id);
            if (!current) {
                pthread_mutex_unlock(&pending_mutex);
                continue;
            }
            
            if (current->retries >= MAX_RETRIES) {
                char buf[128];
                snprintf(buf, sizeof(buf), "max retries reached (id=%u)", current->msg.msg_id);
                log_transport("RETRY", buf);
                remove_pending(current->msg.msg_id);
                
                if (current->msg.type == MSG_AUTH) {
                    printf("Authentication failed after %d retries\n", MAX_RETRIES);
                }
                
                pthread_mutex_unlock(&pending_mutex);
                continue;
            }
            
            current->retries++;
            current->send_time = time(nullptr);
            
            char buf[128];
            snprintf(buf, sizeof(buf), "resend %d/%d (id=%u)", current->retries, MAX_RETRIES, current->msg.msg_id);
            log_transport("RETRY", buf);
            
            MessageEx retry_msg = current->msg;
            retry_msg.timestamp = current->original_timestamp;
            pthread_mutex_unlock(&pending_mutex);
            
            send_message(sockfd, &retry_msg);
        }
    }
    
    return nullptr;
}

double get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

void* listen_incoming(void* _data) {
    int* sockfd_ptr = (int*)_data;
    int sockfd = *sockfd_ptr;
    MessageEx msg;
    bool should_exit = false;
    
    while (!should_exit) {
        pthread_mutex_unlock(&focus_mutex);
        int bytes = recv_message(sockfd, &msg, nullptr, 0);
        if (bytes == -1) break;
        pthread_mutex_lock(&focus_mutex);

        switch (msg.type) {
            case MSG_TEXT:
                printf("[%s][id=%u][%s]: %s\n", time_to_str(msg.timestamp).c_str(), msg.msg_id, msg.sender, msg.payload);
                break;
            case MSG_PRIVATE:
                printf("[%s][id=%u][PRIVATE][%s -> %s]: %s\n", time_to_str(msg.timestamp).c_str(), msg.msg_id, msg.sender, msg.receiver, msg.payload);
                break;
            case MSG_PONG: {
                char buf[64];
                snprintf(buf, sizeof(buf), "recv MSG_PONG (id=%u)", msg.msg_id);
                log_transport("PING", buf);
                
                pthread_mutex_lock(&ping_mutex);
                if (ping_active) {
                    for (auto& pr : ping_results) {
                        if (pr.msg_id == msg.msg_id && !pr.received) {
                            pr.rtt_ms = get_time_ms() - pr.send_time_ms;
                            pr.received = true;
                            break;
                        }
                    }
                }
                pthread_mutex_unlock(&ping_mutex);
                break;
            }
            case MSG_ACK: {
                char buf[64];
                snprintf(buf, sizeof(buf), "ACK received (id=%u)", msg.msg_id);
                log_transport("RETRY", buf);
                remove_pending(msg.msg_id);
                break;
            }
            case MSG_WELCOME:
                printf("Connected to server\n");
                remove_pending(msg.msg_id);
                break;
            case MSG_ERROR:
                printf("[ERROR] %s\n", msg.payload);
                remove_pending(msg.msg_id);
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

void run_ping(int sockfd, int count) {
    pthread_mutex_lock(&ping_mutex);
    ping_active = true;
    ping_results.clear();
    pthread_mutex_unlock(&ping_mutex);
    
    double prev_rtt = -1;
    
    for (int i = 0; i < count; ++i) {
        PingResult pr;
        pr.msg_id = rand() % 1000000;
        pr.received = false;
        pr.rtt_ms = 0;
        pr.send_time_ms = get_time_ms();
        
        MessageEx ping_msg;
        time_t now = time(nullptr);
        init_message(&ping_msg, MSG_PING, my_nickname, "", "", pr.msg_id, now);
        
        pthread_mutex_lock(&ping_mutex);
        ping_results.push_back(pr);
        pthread_mutex_unlock(&ping_mutex);
        
        char buf[64];
        snprintf(buf, sizeof(buf), "send MSG_PING (id=%u)", pr.msg_id);
        log_transport("PING", buf);
        
        pthread_mutex_unlock(&focus_mutex);
        send_message(sockfd, &ping_msg);
        
        // Ждём 1 секунду, не блокируя мьютексы
        for (int w = 0; w < 10; ++w) {
            usleep(100000); // 100ms
        }
        pthread_mutex_lock(&focus_mutex);
        
        pthread_mutex_lock(&ping_mutex);
        bool received = ping_results.back().received;
        double rtt = ping_results.back().rtt_ms;
        pthread_mutex_unlock(&ping_mutex);
        
        if (received) {
            printf("PING %d → RTT=%.1fms", i+1, rtt);
            if (prev_rtt >= 0) {
                double jitter = fabs(rtt - prev_rtt);
                printf(" | Jitter=%.1fms", jitter);
            }
            printf("\n");
            prev_rtt = rtt;
        } else {
            printf("PING %d → timeout\n", i+1);
        }
    }
    
    pthread_mutex_lock(&ping_mutex);
    ping_active = false;
    pthread_mutex_unlock(&ping_mutex);
}

void show_netdiag() {
    pthread_mutex_lock(&ping_mutex);
    
    double total_rtt = 0;
    int received = 0;
    double total_jitter = 0;
    int jitter_count = 0;
    
    for (size_t i = 0; i < ping_results.size(); ++i) {
        if (ping_results[i].received) {
            total_rtt += ping_results[i].rtt_ms;
            received++;
            
            if (i > 0 && ping_results[i-1].received) {
                double jitter = fabs(ping_results[i].rtt_ms - ping_results[i-1].rtt_ms);
                total_jitter += jitter;
                jitter_count++;
            }
        }
    }
    
    int total = ping_results.size();
    int lost = total - received;
    double loss_pct = total > 0 ? (double)lost / total * 100.0 : 0;
    double avg_rtt = received > 0 ? total_rtt / received : 0;
    double avg_jitter = jitter_count > 0 ? total_jitter / jitter_count : 0;
    
    printf("\nRTT avg : %.1f ms\n", avg_rtt);
    printf("Jitter  : %.1f ms\n", avg_jitter);
    printf("Loss    : %.1f%%\n", loss_pct);
    
    char filename[128];
    snprintf(filename, sizeof(filename), "net_diag_%s.json", my_nickname);
    std::ofstream file(filename);
    file << "{\n";
    file << "  \"nickname\": \"" << my_nickname << "\",\n";
    file << "  \"rtt_avg_ms\": " << avg_rtt << ",\n";
    file << "  \"jitter_ms\": " << avg_jitter << ",\n";
    file << "  \"loss_percent\": " << loss_pct << ",\n";
    file << "  \"total_sent\": " << total << ",\n";
    file << "  \"total_received\": " << received << "\n";
    file << "}\n";
    file.close();
    
    printf("Diagnostics saved to %s\n", filename);
    
    pthread_mutex_unlock(&ping_mutex);
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

    srand(time(nullptr));

    pthread_t tid, retry_tid;
    pthread_create(&tid, nullptr, listen_incoming, &sockfd);
    pthread_detach(tid);
    pthread_create(&retry_tid, nullptr, retry_thread, &sockfd);
    pthread_detach(retry_tid);

    uint32_t auth_msg_id = 1;
    MessageEx msg;
    time_t auth_ts = time(nullptr);
    init_message(&msg, MSG_AUTH, my_nickname, "", my_nickname, auth_msg_id, auth_ts);
    
    char buf[64];
    snprintf(buf, sizeof(buf), "send MSG_AUTH (id=%u)", auth_msg_id);
    log_transport("RETRY", buf);
    add_pending(msg, auth_ts);
    
    if (send_message(sockfd, &msg) == -1) {
        close(sockfd);
        return 1;
    }

    char buffer[MAX_PAYLOAD];
    uint32_t client_msg_id = 2;
    
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
            run_ping(sockfd, 10);
        } else if (strncmp(buffer, "/ping ", 6) == 0) {
            int n = atoi(buffer + 6);
            if (n > 0) {
                run_ping(sockfd, n);
            }
        } else if (strcmp(buffer, "/netdiag") == 0) {
            show_netdiag();
        } else if (strcmp(buffer, "/list") == 0) {
            uint32_t mid = client_msg_id++;
            time_t ts = time(nullptr);
            init_message(&msg, MSG_LIST, my_nickname, "", "", mid, ts);
            char logbuf[64];
            snprintf(logbuf, sizeof(logbuf), "send MSG_LIST (id=%u)", mid);
            log_transport("RETRY", logbuf);
            add_pending(msg, ts);
            send_message(sockfd, &msg);
        } else if (strncmp(buffer, "/history", 8) == 0) {
            uint32_t mid = client_msg_id++;
            time_t ts = time(nullptr);
            int n = 0;
            if (sscanf(buffer, "/history %d", &n) == 1 && n > 0) {
                char num[16];
                snprintf(num, sizeof(num), "%d", n);
                init_message(&msg, MSG_HISTORY, my_nickname, "", num, mid, ts);
            } else {
                init_message(&msg, MSG_HISTORY, my_nickname, "", "", mid, ts);
            }
            char logbuf[64];
            snprintf(logbuf, sizeof(logbuf), "send MSG_HISTORY (id=%u)", mid);
            log_transport("RETRY", logbuf);
            add_pending(msg, ts);
            send_message(sockfd, &msg);
        } else if (strncmp(buffer, "/w ", 3) == 0) {
            uint32_t mid = client_msg_id++;
            time_t ts = time(nullptr);
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
                init_message(&msg, MSG_PRIVATE, my_nickname, target, full, mid, ts);
                char logbuf[64];
                snprintf(logbuf, sizeof(logbuf), "send MSG_PRIVATE (id=%u)", mid);
                log_transport("RETRY", logbuf);
                add_pending(msg, ts);
                send_message(sockfd, &msg);
            } else {
                printf("Usage: /w <nick> <message>\n");
            }
        } else if (strcmp(buffer, "/help") == 0) {
            printf("Available commands:\n"
                   "/help\n/list\n/history\n/history N\n/quit\n/w <nick> <message>\n"
                   "/ping\n/ping N\n/netdiag\n");
        } else if (strlen(buffer) > 0) {
            uint32_t mid = client_msg_id++;
            time_t ts = time(nullptr);
            init_message(&msg, MSG_TEXT, my_nickname, "", buffer, mid, ts);
            char logbuf[64];
            snprintf(logbuf, sizeof(logbuf), "send MSG_TEXT (id=%u)", mid);
            log_transport("RETRY", logbuf);
            add_pending(msg, ts);
            send_message(sockfd, &msg);
        }
    }

    close(sockfd);
    return 0;
}