#include <stdint.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#define MAX_PAYLOAD 1024  
  
typedef struct {  
    uint32_t length;  
    uint8_t type;
    char payload[MAX_PAYLOAD];
} Message;  

enum {  
    MSG_HELLO = 1,
    MSG_WELCOME= 2,  
    MSG_TEXT = 3,  
    MSG_PING = 4,  
    MSG_PONG = 5,  
    MSG_BYE = 6  
};

#define PORT 4445