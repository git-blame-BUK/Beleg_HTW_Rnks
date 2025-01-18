#ifndef NACK_H
#define NACK_H

#define MAX_LINE_LEN 1024

typedef struct {
    int sequence_number;
    int Timestamp;
} NACK;

#endif