#define SERVER_SPECIFIC
#include "common.hpp"
#include <pthread.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>
#include <algorithm>
#include <iostream>
#include <cstring>

char server_ip[INET_ADDRSTRLEN] = "0.0.0.0";

ConnectionInfo clients[MAX_CLIENTS];
pthread_mutex_t broadcast_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t nickname_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t offline_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t history_mutex = PTHREAD_MUTEX_INITIALIZER;

uint32_t next_msg_id = 1;
pthread_mutex_t msg_id_mutex = PTHREAD_MUTEX_INITIALIZER;

std::vector<OfflineMsg> offline_queue;

ConnectionInfo* pickup_client = nullptr;
pthread_cond_t pickup_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t pickup_mutex = PTHREAD_MUTEX_INITIALIZER;

std::string history_filename = "history.json";

int sim_delay_ms = 0;
double sim_drop_rate = 0.0;
double sim_corrupt_rate = 0.0;

bool simulate_network(MessageEx* msg) {
    if (sim_delay_ms > 0) {
        usleep(sim_delay_ms * 1000);
        char buf[64];
        snprintf(buf, sizeof(buf), "DELAY applied: %d ms", sim_delay_ms);
        log_transport("SIM", buf);
    }
    
    if (sim_drop_rate > 0.0) {
        double r = (double)rand() / RAND_MAX;
        if (r < sim_drop_rate) {
            char buf[64];
            snprintf(buf, sizeof(buf), "DROP (id=%u, rate=%.1f)", msg->msg_id, sim_drop_rate);
            log_transport("SIM", buf);
            return false;
        }
    }
    
    if (sim_corrupt_rate > 0.0) {
        double r = (double)rand() / RAND_MAX;
        if (r < sim_corrupt_rate) {
            int pos = rand() % MAX_PAYLOAD;
            msg->payload[pos] = (char)(rand() % 256);
            char buf[64];
            snprintf(buf, sizeof(buf), "CORRUPT payload (id=%u)", msg->msg_id);
            log_transport("SIM", buf);
        }
    }
    
    return true;
}

bool is_duplicate(ConnectionInfo* client, uint32_t client_msg_id) {
    for (int i = 0; i < client->last_ids_count; ++i) {
        if (client->last_ids[i] == client_msg_id) {
            return true;
        }
    }
    return false;
}

void add_processed_id(ConnectionInfo* client, uint32_t client_msg_id) {
    if (client->last_ids_count < 32) {
        client->last_ids[client->last_ids_count++] = client_msg_id;
    } else {
        for (int i = 1; i < 32; ++i) {
            client->last_ids[i-1] = client->last_ids[i];
        }
        client->last_ids[31] = client_msg_id;
    }
}

uint32_t get_max_msg_id_from_history() {
    std::ifstream file(history_filename);
    if (!file.is_open()) {
        return 0;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    
    if (content.empty()) {
        return 0;
    }
    
    uint32_t max_id = 0;
    size_t pos = 0;
    
    while (true) {
        size_t obj_start = content.find('{', pos);
        if (obj_start == std::string::npos) break;
        
        size_t obj_end = content.find('}', obj_start);
        if (obj_end == std::string::npos) break;
        
        std::string obj = content.substr(obj_start, obj_end - obj_start + 1);
        pos = obj_end + 1;
        
        size_t key_pos = obj.find("\"msg_id\"");
        if (key_pos != std::string::npos) {
            size_t colon_pos = obj.find(':', key_pos);
            if (colon_pos != std::string::npos) {
                size_t num_start = colon_pos + 1;
                while (num_start < obj.length() && (obj[num_start] == ' ' || obj[num_start] == '\t')) {
                    num_start++;
                }
                try {
                    uint32_t id = static_cast<uint32_t>(std::stoul(obj.substr(num_start)));
                    if (id > max_id) {
                        max_id = id;
                    }
                } catch (...) {
                }
            }
        }
    }
    
    return max_id;
}

void add_to_history(const MessageEx* msg, bool delivered, bool is_offline) {
    pthread_mutex_lock(&history_mutex);
    
    std::string type_str;
    switch (msg->type) {
        case MSG_TEXT: type_str = "MSG_TEXT"; break;
        case MSG_PRIVATE: type_str = "MSG_PRIVATE"; break;
        default: type_str = "UNKNOWN";
    }
    
    char new_entry[1024];
    snprintf(new_entry, sizeof(new_entry),
        "{\"msg_id\":%u,\"timestamp\":%ld,\"sender\":\"%s\",\"receiver\":\"%s\","
        "\"type\":\"%s\",\"text\":\"%s\",\"delivered\":%s,\"is_offline\":%s}",
        msg->msg_id, (long)msg->timestamp, msg->sender, msg->receiver,
        type_str.c_str(), msg->payload,
        delivered ? "true" : "false", is_offline ? "true" : "false");
    
    std::ifstream infile(history_filename);
    std::string content((std::istreambuf_iterator<char>(infile)),
                         std::istreambuf_iterator<char>());
    infile.close();
    
    std::string new_content;
    if (content.empty() || content == "\n") {
        new_content = "[\n  " + std::string(new_entry) + "\n]";
    } else {
        size_t last_brace = content.rfind(']');
        if (last_brace != std::string::npos) {
            new_content = content.substr(0, last_brace);
            while (!new_content.empty() && (new_content.back() == '\n' || new_content.back() == ' '))
                new_content.pop_back();
            new_content += ",\n  " + std::string(new_entry) + "\n]";
        } else {
            new_content = "[\n  " + std::string(new_entry) + "\n]";
        }
    }
    
    std::ofstream outfile(history_filename);
    outfile << new_content;
    outfile.close();
    
    pthread_mutex_unlock(&history_mutex);
}

void update_delivered_flag(uint32_t msg_id) {
    pthread_mutex_lock(&history_mutex);
    
    std::ifstream infile(history_filename);
    std::string content((std::istreambuf_iterator<char>(infile)),
                         std::istreambuf_iterator<char>());
    infile.close();
    
    char search[64];
    snprintf(search, sizeof(search), "\"msg_id\":%u", msg_id);
    size_t pos = content.find(search);
    if (pos != std::string::npos) {
        size_t delivered_pos = content.find("\"delivered\":false", pos);
        if (delivered_pos != std::string::npos && delivered_pos < content.find('}', pos)) {
            content.replace(delivered_pos, 17, "\"delivered\":true");
            std::ofstream outfile(history_filename);
            outfile << content;
            outfile.close();
        }
    }
    
    pthread_mutex_unlock(&history_mutex);
}

void load_offline_messages() {
    pthread_mutex_lock(&offline_mutex);
    offline_queue.clear();
    
    std::ifstream file(history_filename);
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();
    
    if (content.empty()) {
        pthread_mutex_unlock(&offline_mutex);
        return;
    }
    
    size_t start = content.find('{');
    while (start != std::string::npos) {
        size_t end = content.find('}', start);
        if (end == std::string::npos) break;
        std::string obj = content.substr(start, end - start + 1);
        
        if (obj.find("\"delivered\":false") != std::string::npos &&
            obj.find("\"is_offline\":true") != std::string::npos) {
            OfflineMsg om;
            if (sscanf(obj.c_str(), "{\"msg_id\":%u", &om.msg_id) == 1) {
                const char* s = strstr(obj.c_str(), "\"sender\":\"");
                if (s) {
                    s += 10;
                    const char* e = strchr(s, '"');
                    if (e) {
                        int len = e - s;
                        if (len >= MAX_NAME) len = MAX_NAME-1;
                        strncpy(om.sender, s, len);
                        om.sender[len] = '\0';
                    }
                }
                s = strstr(obj.c_str(), "\"receiver\":\"");
                if (s) {
                    s += 11;
                    const char* e = strchr(s, '"');
                    if (e) {
                        int len = e - s;
                        if (len >= MAX_NAME) len = MAX_NAME-1;
                        strncpy(om.receiver, s, len);
                        om.receiver[len] = '\0';
                    }
                }
                s = strstr(obj.c_str(), "\"text\":\"");
                if (s) {
                    s += 8;
                    const char* e = strchr(s, '"');
                    if (e) {
                        int len = e - s;
                        if (len >= MAX_PAYLOAD) len = MAX_PAYLOAD-1;
                        strncpy(om.text, s, len);
                        om.text[len] = '\0';
                    }
                }
                long ts;
                if (sscanf(obj.c_str(), "{\"timestamp\":%ld", &ts) == 1)
                    om.timestamp = (time_t)ts;
                offline_queue.push_back(om);
            }
        }
        start = content.find('{', end + 1);
    }
    
    pthread_mutex_unlock(&offline_mutex);
}

std::string get_user_history(const std::string& nickname, int n) {
    std::ifstream file(history_filename);
    if (!file.is_open()) {
        return "No history yet.\n";
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    
    if (content.empty()) {
        return "No history yet.\n";
    }
    
    struct Message {
        uint32_t msg_id;
        long timestamp;
        std::string sender;
        std::string receiver;
        std::string type;
        std::string text;
        bool is_offline;
    };
    
    std::vector<Message> all_messages;
    
    size_t pos = 0;
    while (true) {
        size_t obj_start = content.find('{', pos);
        if (obj_start == std::string::npos) break;
        
        size_t obj_end = content.find('}', obj_start);
        if (obj_end == std::string::npos) break;
        
        std::string obj = content.substr(obj_start, obj_end - obj_start + 1);
        pos = obj_end + 1;
        
        Message msg = {};
        msg.is_offline = false;
        
        auto parse_num_field = [&](const std::string& field_name) -> long {
            size_t field_pos = obj.find("\"" + field_name + "\"");
            if (field_pos == std::string::npos) return 0;
            size_t colon_pos = obj.find(':', field_pos);
            if (colon_pos == std::string::npos) return 0;
            size_t num_start = colon_pos + 1;
            while (num_start < obj.length() && (obj[num_start] == ' ' || obj[num_start] == '\t'))
                num_start++;
            try {
                return std::stol(obj.substr(num_start));
            } catch (...) {
                return 0;
            }
        };
        
        auto parse_string_field = [&](const std::string& field_name) -> std::string {
            size_t field_pos = obj.find("\"" + field_name + "\"");
            if (field_pos == std::string::npos) return "";
            size_t colon_pos = obj.find(':', field_pos);
            if (colon_pos == std::string::npos) return "";
            size_t quote_start = obj.find('"', colon_pos + 1);
            if (quote_start == std::string::npos) return "";
            size_t quote_end = obj.find('"', quote_start + 1);
            if (quote_end == std::string::npos) return "";
            return obj.substr(quote_start + 1, quote_end - quote_start - 1);
        };
        
        msg.msg_id = parse_num_field("msg_id");
        msg.timestamp = parse_num_field("timestamp");
        msg.sender = parse_string_field("sender");
        msg.receiver = parse_string_field("receiver");
        msg.type = parse_string_field("type");
        msg.text = parse_string_field("text");
        
        size_t offline_pos = obj.find("\"is_offline\"");
        if (offline_pos != std::string::npos) {
            msg.is_offline = (obj.find("true", offline_pos) < obj.find("false", offline_pos));
        }
        
        // Включаем сообщения, где пользователь отправитель или получатель
        bool is_relevant = false;
        if (msg.type == "MSG_TEXT") {
            is_relevant = true;  // все публичные сообщения
        } else if (msg.type == "MSG_PRIVATE") {
            if (msg.sender == nickname || msg.receiver == nickname) {
                is_relevant = true;
            }
        }
        
        if (is_relevant) {
            all_messages.push_back(msg);
        }
    }
    
    if (all_messages.empty()) {
        return "No history yet.\n";
    }
    
    // Сортируем по timestamp и msg_id
    std::sort(all_messages.begin(), all_messages.end(), [](const Message& a, const Message& b) {
        if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
        return a.msg_id < b.msg_id;
    });
    
    size_t total = all_messages.size();
    size_t start_idx = (n > 0 && static_cast<size_t>(n) < total) ? total - n : 0;
    
    std::string result;
    
    for (size_t i = start_idx; i < total; ++i) {
        const Message& msg = all_messages[i];
        
        char time_buffer[64];
        time_t raw_time = msg.timestamp;
        struct tm* time_info = localtime(&raw_time);
        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", time_info);
        
        char line_buffer[1024];
        
        if (msg.type == "MSG_TEXT") {
            snprintf(line_buffer, sizeof(line_buffer),
                     "[%s][id=%u][%s]: %s\n",
                     time_buffer, msg.msg_id, msg.sender.c_str(), msg.text.c_str());
            result += line_buffer;
        }
        else if (msg.type == "MSG_PRIVATE") {
            const char* tag = msg.is_offline ? "OFFLINE" : "PRIVATE";
            snprintf(line_buffer, sizeof(line_buffer),
                     "[%s][id=%u][%s -> %s][%s]: %s\n",
                     time_buffer, msg.msg_id, msg.sender.c_str(), 
                     msg.receiver.c_str(), tag, msg.text.c_str());
            result += line_buffer;
        }
    }
    
    return result;
}

uint32_t generate_server_msg_id() {
    pthread_mutex_lock(&msg_id_mutex);
    uint32_t id = next_msg_id++;
    pthread_mutex_unlock(&msg_id_mutex);
    return id;
}

void broadcast(MessageEx* message, ConnectionInfo* ignore) {
    pthread_mutex_lock(&broadcast_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (&clients[i] != ignore && clients[i].active && clients[i].authorized) {
            pthread_mutex_lock(&clients[i].personal_mutex);
            send_message(clients[i].fd, message, clients[i].ip, PORT);
            pthread_mutex_unlock(&clients[i].personal_mutex);
        }
    }
    pthread_mutex_unlock(&broadcast_mutex);
}

ConnectionInfo* find_user_by_nickname(const char* nickname, ConnectionInfo* ignore) {
    pthread_mutex_lock(&nickname_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (&clients[i] != ignore && clients[i].active && clients[i].authorized &&
            strcmp(nickname, clients[i].nickname) == 0) {
            pthread_mutex_unlock(&nickname_mutex);
            return &clients[i];
        }
    }
    pthread_mutex_unlock(&nickname_mutex);
    return nullptr;
}

void deliver_offline_messages(ConnectionInfo* client) {
    pthread_mutex_lock(&offline_mutex);
    std::vector<OfflineMsg> to_deliver;
    for (auto it = offline_queue.begin(); it != offline_queue.end(); ) {
        if (strcmp(it->receiver, client->nickname) == 0) {
            to_deliver.push_back(*it);
            update_delivered_flag(it->msg_id);
            it = offline_queue.erase(it);
        } else {
            ++it;
        }
    }
    pthread_mutex_unlock(&offline_mutex);
    
    for (auto& om : to_deliver) {
        MessageEx msg;
        // Используем оригинальный timestamp из offline сообщения
        init_message(&msg, MSG_PRIVATE, om.sender, om.receiver, om.text, om.msg_id, om.timestamp);
        char modified[MAX_PAYLOAD];
        snprintf(modified, MAX_PAYLOAD, "[OFFLINE] %s", om.text);
        strncpy(msg.payload, modified, MAX_PAYLOAD-1);
        pthread_mutex_lock(&client->personal_mutex);
        send_message(client->fd, &msg, client->ip, PORT);
        pthread_mutex_unlock(&client->personal_mutex);
        log_application("delivered offline message");
    }
}

void send_ack(ConnectionInfo* client, uint32_t client_msg_id, const char* ip, int port) {
    MessageEx ack_msg;
    init_message(&ack_msg, MSG_ACK, "", "", "", client_msg_id, time(nullptr));
    char buf[64];
    snprintf(buf, sizeof(buf), "send MSG_ACK (id=%u)", client_msg_id);
    log_transport("ACK", buf);
    send_message(client->fd, &ack_msg, ip, port);
}

void* handle_connection(void* _data) {
    ConnectionInfo* client = (ConnectionInfo*)_data;
    inet_ntop(AF_INET, &client->addr.sin_addr, client->ip, sizeof(client->ip));
    char* ip = client->ip;
    int port = ntohs(client->addr.sin_port);
    
    log_application("new connection");
    MessageEx msg_from, msg_to;
    bool disconnected_gracefully = false, disconnected_protocol = false, disconnected_unexpected = false;
    
    while (!client->authorized) {
        int bytes = recv_message(client->fd, &msg_from, ip, server_ip, port);
        if (bytes == -1) { disconnected_unexpected = true; goto EXIT; }
        
        if (!simulate_network(&msg_from)) {
            continue;
        }
        
        if (msg_from.type == MSG_AUTH) {
            uint32_t client_msg_id = msg_from.msg_id;
            
            if (is_duplicate(client, client_msg_id)) {
                char buf[64];
                snprintf(buf, sizeof(buf), "duplicate ignored (id=%u)", client_msg_id);
                log_transport("DEDUP", buf);
                send_ack(client, client_msg_id, ip, port);
                continue;
            }
            add_processed_id(client, client_msg_id);
            
            log_application("MSG_AUTH received");
            strncpy(client->nickname, msg_from.payload, MAX_NAME-1);
            client->nickname[MAX_NAME-1] = '\0';
            
            if (strlen(client->nickname) == 0) {
                message_from_fmt(&msg_to, MSG_ERROR, "Empty nickname invalid");
                send_message(client->fd, &msg_to, ip, port);
                send_ack(client, client_msg_id, ip, port);
                continue;
            }
            if (find_user_by_nickname(client->nickname, client)) {
                message_from_fmt(&msg_to, MSG_ERROR, "Nickname already taken");
                send_message(client->fd, &msg_to, ip, port);
                send_ack(client, client_msg_id, ip, port);
                continue;
            }
            
            client->authorized = true;
            client->last_client_msg_id = client_msg_id;
            
            message_from_data(&msg_to, MSG_WELCOME, nullptr, 0);
            send_message(client->fd, &msg_to, ip, port);
            send_ack(client, client_msg_id, ip, port);
            
            broadcast(message_from_fmt(&msg_to, MSG_SERVER_INFO, "User %s connected!", client->nickname), client);
            deliver_offline_messages(client);
        } else {
            message_from_fmt(&msg_to, MSG_ERROR, "Authenticate first");
            send_message(client->fd, &msg_to, ip, port);
            disconnected_protocol = true;
            goto EXIT;
        }
    }
    
    while (1) {
        int bytes = recv_message(client->fd, &msg_from, ip, server_ip, port);
        if (bytes == -1) { disconnected_unexpected = true; break; }
        
        if (!simulate_network(&msg_from)) {
            continue;
        }
        
        uint32_t client_msg_id = msg_from.msg_id;
        
        if (is_duplicate(client, client_msg_id)) {
            char buf[64];
            snprintf(buf, sizeof(buf), "duplicate ignored (id=%u)", client_msg_id);
            log_transport("DEDUP", buf);
            send_ack(client, client_msg_id, ip, port);
            continue;
        }
        add_processed_id(client, client_msg_id);
        
        switch (msg_from.type) {
            case MSG_TEXT: {
                log_application("MSG_TEXT");
                
                char buf[64];
                snprintf(buf, sizeof(buf), "process MSG_TEXT (id=%u)", client_msg_id);
                log_transport("ACK", buf);
                
                uint32_t server_msg_id = generate_server_msg_id();
                char text_copy[MAX_PAYLOAD];
                strncpy(text_copy, msg_from.payload, MAX_PAYLOAD-1);
                
                MessageEx broadcast_msg;
                init_message(&broadcast_msg, MSG_TEXT, client->nickname, "",
                             text_copy, server_msg_id, msg_from.timestamp);
                add_to_history(&broadcast_msg, true, false);
                broadcast(&broadcast_msg, client);
                send_ack(client, client_msg_id, ip, port);
                break;
            }
            case MSG_PRIVATE: {
                log_application("MSG_PRIVATE");
                char target_nick[MAX_NAME];
                char msg_text[MAX_PAYLOAD];
                char* colon = strchr(msg_from.payload, ':');
                if (!colon) {
                    message_from_fmt(&msg_to, MSG_ERROR, "Invalid private message format");
                    send_message(client->fd, &msg_to, ip, port);
                    send_ack(client, client_msg_id, ip, port);
                    break;
                }
                int nick_len = colon - msg_from.payload;
                if (nick_len >= MAX_NAME) nick_len = MAX_NAME-1;
                strncpy(target_nick, msg_from.payload, nick_len);
                target_nick[nick_len] = '\0';
                strncpy(msg_text, colon+1, MAX_PAYLOAD-1);
                msg_text[MAX_PAYLOAD-1] = '\0';
                
                char buf[64];
                snprintf(buf, sizeof(buf), "process MSG_PRIVATE (id=%u)", client_msg_id);
                log_transport("ACK", buf);
                
                uint32_t server_msg_id = generate_server_msg_id();
                time_t original_ts = msg_from.timestamp;
                
                ConnectionInfo* receiver = find_user_by_nickname(target_nick, nullptr);
                if (receiver) {
                    MessageEx priv_msg;
                    init_message(&priv_msg, MSG_PRIVATE, client->nickname, target_nick,
                                 msg_text, server_msg_id, original_ts);
                    add_to_history(&priv_msg, true, false);
                    pthread_mutex_lock(&receiver->personal_mutex);
                    send_message(receiver->fd, &priv_msg, ip, port);
                    pthread_mutex_unlock(&receiver->personal_mutex);
                } else {
                    OfflineMsg om;
                    om.msg_id = server_msg_id;
                    strncpy(om.sender, client->nickname, MAX_NAME-1);
                    strncpy(om.receiver, target_nick, MAX_NAME-1);
                    strncpy(om.text, msg_text, MAX_PAYLOAD-1);
                    om.timestamp = original_ts;
                    pthread_mutex_lock(&offline_mutex);
                    offline_queue.push_back(om);
                    pthread_mutex_unlock(&offline_mutex);
                    MessageEx hist_msg;
                    init_message(&hist_msg, MSG_PRIVATE, client->nickname, target_nick,
                                 msg_text, server_msg_id, original_ts);
                    add_to_history(&hist_msg, false, true);
                    log_application("stored offline message");
                }
                
                send_ack(client, client_msg_id, ip, port);
                break;
            }
            case MSG_LIST: {
                log_application("MSG_LIST");
                std::string list;
                pthread_mutex_lock(&nickname_mutex);
                for (int i = 0; i < MAX_CLIENTS; ++i) {
                    if (clients[i].active && clients[i].authorized)
                        list += std::string(clients[i].nickname) + "\n";
                }
                pthread_mutex_unlock(&nickname_mutex);
                message_from_data(&msg_to, MSG_SERVER_INFO, list.c_str(), list.length());
                send_message(client->fd, &msg_to, ip, port);
                send_ack(client, client_msg_id, ip, port);
                break;
            }
            case MSG_HISTORY: {
                log_application("MSG_HISTORY");
                int n = 20;
                if (strlen(msg_from.payload) > 0) {
                    n = atoi(msg_from.payload);
                    if (n <= 0) n = 20;
                }
                std::string history = get_user_history(client->nickname, n);
                message_from_data(&msg_to, MSG_HISTORY_DATA, history.c_str(), history.length());
                send_message(client->fd, &msg_to, ip, port);
                send_ack(client, client_msg_id, ip, port);
                break;
            }
            case MSG_PING: {
                char buf[64];
                snprintf(buf, sizeof(buf), "recv MSG_PING (id=%u)", client_msg_id);
                log_transport("PING", buf);
                
                message_from_data(&msg_to, MSG_PONG, nullptr, 0);
                msg_to.msg_id = client_msg_id;
                char pong_buf[64];
                snprintf(pong_buf, sizeof(pong_buf), "send MSG_PONG (id=%u)", client_msg_id);
                log_transport("PING", pong_buf);
                send_message(client->fd, &msg_to, ip, port);
                break;
            }
            case MSG_BYE: {
                disconnected_gracefully = true;
                goto EXIT;
            }
            default:
                message_from_fmt(&msg_to, MSG_ERROR, "Unknown command");
                send_message(client->fd, &msg_to, ip, port);
                send_ack(client, client_msg_id, ip, port);
                break;
        }
    }
    
EXIT:
    if (client->authorized) {
        if (disconnected_gracefully) {
            broadcast(message_from_fmt(&msg_to, MSG_SERVER_INFO, "User %s disconnected", client->nickname), client);
        } else if (disconnected_protocol) {
            broadcast(message_from_fmt(&msg_to, MSG_SERVER_INFO, "User %s disconnected (protocol violation)", client->nickname), client);
        } else if (disconnected_unexpected) {
            broadcast(message_from_fmt(&msg_to, MSG_SERVER_INFO, "User %s dropped connection", client->nickname), client);
        }
    }
    client->active = false;
    pthread_mutex_destroy(&client->personal_mutex);
    close(client->fd);
    log_application("connection closed");
    return nullptr;
}

void* poll_thread(void* _data) {
    (void)_data;
    while (1) {
        pthread_mutex_lock(&pickup_mutex);
        pthread_cond_wait(&pickup_cond, &pickup_mutex);
        pthread_mutex_unlock(&pickup_mutex);
        if (pickup_client) {
            handle_connection(pickup_client);
            pickup_client = nullptr;
        }
    }
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--delay=", 8) == 0) {
            sim_delay_ms = atoi(argv[i] + 8);
        } else if (strncmp(argv[i], "--drop=", 7) == 0) {
            sim_drop_rate = atof(argv[i] + 7);
        } else if (strncmp(argv[i], "--corrupt=", 10) == 0) {
            sim_corrupt_rate = atof(argv[i] + 10);
        }
    }
    
    srand(time(nullptr));
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind"); close(sockfd); return 1;
    }
    if (listen(sockfd, MAX_CLIENTS) == -1) {
        perror("listen"); close(sockfd); return 1;
    }
    
    inet_ntop(AF_INET, &server_addr.sin_addr, server_ip, sizeof(server_ip));
    
    printf("Server IP: %s\n", server_ip);
    printf("Simulation: delay=%dms drop=%.1f corrupt=%.1f\n", sim_delay_ms, sim_drop_rate, sim_corrupt_rate);
    
    memset(clients, 0, sizeof(clients));
    
    uint32_t max_id = get_max_msg_id_from_history();
    if (max_id > 0) {
        next_msg_id = max_id + 1;
        printf("Loaded history: next message ID will be %u\n", next_msg_id);
    }
    
    load_offline_messages();
    
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        pthread_t tid;
        pthread_create(&tid, nullptr, poll_thread, nullptr);
        pthread_detach(tid);
    }
    
    printf("Server listening on port %d\n", PORT);
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int connfd = accept(sockfd, (struct sockaddr*)&client_addr, &addrlen);
        if (connfd == -1) { perror("accept"); continue; }
        
        ConnectionInfo* data = nullptr;
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (!clients[i].active) {
                data = &clients[i];
                break;
            }
        }
        if (!data) {
            close(connfd);
            printf("All threads busy, connection rejected\n");
            continue;
        }
        data->fd = connfd;
        data->addr = client_addr;
        data->active = true;
        data->authorized = false;
        pthread_mutex_init(&data->personal_mutex, nullptr);
        memset(data->nickname, 0, sizeof(data->nickname));
        memset(data->last_ids, 0, sizeof(data->last_ids));
        data->last_ids_count = 0;
        data->last_client_msg_id = 0;
        
        pickup_client = data;
        pthread_cond_signal(&pickup_cond);
    }
    close(sockfd);
    return 0;
}