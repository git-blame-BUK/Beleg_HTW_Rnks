#ifndef PACKET_H
#define PACKET_H

#define MAX_LINE_LEN 1024
#define MAX_PACKETS 10

typedef struct {
    int sequence_number;
    int is_end;
    char data[MAX_LINE_LEN];
    int timestamp;
} Packet;

#endif // PACKET_H
