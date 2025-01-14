#ifndef NACK_H
#define NACK_H

#define MAX_LINE_LEN 1024

typedef struct {
    int Seqnr;
    int sender_Adresse;
    int Timestamp;
} NACK;

#endif