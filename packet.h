#ifndef PACKET_H
#define PACKET_H

#define MAX_LINE_LEN 1024

typedef struct {
    int sequence_number;
    char data[MAX_LINE_LEN];
} Packet;

#endif // PACKET_H
