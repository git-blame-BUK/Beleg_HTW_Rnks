#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>

#include "packet.h"

#define MAX_PACKETS 1
#define MAX_LINE_LEN 1024

/**
 * Decodes a single, encoded packet string (e.g. "5|Hello").
 */
Packet decode_packet(const char *encoded) {
    Packet packet;
    memset(&packet, 0, sizeof(packet));

    int sequence_number;
    char data_buffer[MAX_LINE_LEN] = {0};

    int parsed = sscanf(encoded, "%d|%1023[^\n]", &sequence_number, data_buffer);
    if (parsed < 2) {
        packet.sequence_number = 0;
        strncpy(packet.data, "", MAX_LINE_LEN);
    } else {
        packet.sequence_number = sequence_number;
        strncpy(packet.data, data_buffer, MAX_LINE_LEN - 1);
        packet.data[MAX_LINE_LEN - 1] = '\0';
    }

    return packet;
}

/**
 * Continuously receive and decode packets from the given socket.
 * This example stops after receiving MAX_PACKETS or a recv error/zero-length message.
 *
 * @param sock      The socket file descriptor to receive from
 * @param serveraddr The server address structure
 * @param out_count Output pointer where the number of received packets is stored
 * @return Pointer to a heap-allocated array of Packet. Caller must free().
 */
Packet* receive_and_decode_packets(int sock, struct sockaddr_in* serveraddr, int* out_count) {
    Packet* packets = (Packet*)malloc(sizeof(Packet) * MAX_PACKETS);
    if (!packets) {
        perror("malloc failed");
        *out_count = 0;
        return NULL;
    }
    memset(packets, 0, sizeof(Packet) * MAX_PACKETS);

    int count = 0;
    while (count < MAX_PACKETS) {
        char buffer[MAX_LINE_LEN + 32] = {0};
        socklen_t server_len = sizeof(*serveraddr);

        int n = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                         (struct sockaddr *)serveraddr, &server_len);

        if (n < 0) {
            perror("recvfrom failed");
            break;
        }
        if (n == 0) {
            break;
        }

        buffer[n] = '\0';

        packets[count] = decode_packet(buffer);
        count++;
    }

    *out_count = count;
    return packets;
}

int main() {
    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        perror("failed to create socket");
        exit(EXIT_FAILURE);
    }

    serveraddr.sin_family      = AF_INET;
    serveraddr.sin_port        = htons(40400);
    serveraddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (const struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1) {
        perror("socket konnte nicht gebunden werden");
        close(sock);
        exit(EXIT_FAILURE);
    }

    /*
    Test Commit to show the difference between the original and the modified code
    */

    int packet_count = 0;
    Packet* packet_list = receive_and_decode_packets(sock, &serveraddr, &packet_count);

    printf("Received %d packets:\n", packet_count);
    for (int i = 0; i < packet_count; i++) {
        printf("  Packet[%d]: seq = %d, data = %s\n",
               i, packet_list[i].sequence_number, packet_list[i].data);
    }

    free(packet_list);
    close(sock);
    return 0;
}