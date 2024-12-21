#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include "packet.h"

Packet decode_packet(const char *encoded) {
    Packet packet;
    int sequence_number;
    char data[MAX_LINE_LEN];

    int parsed = sscanf(encoded, "%d|%1023[^\"]", &sequence_number, data);

    packet.sequence_number = sequence_number;
    strncpy(packet.data, data, MAX_LINE_LEN);
    packet.data[MAX_LINE_LEN - 1] = '\0'; // Ensure null termination

    return packet; // Success
}

int main(){
//msg buffer
char buffer[MAX_LINE_LEN + 32] = {0};
struct sockaddr_in serveraddr = {0};
// server socket
int sock = socket(AF_INET, SOCK_DGRAM, 0);
//error erstellung socket
if (sock == -1){
perror("failed to create socket");
exit(EXIT_FAILURE);
}

serveraddr.sin_family= AF_INET;
serveraddr.sin_port= htons(40400);
serveraddr.sin_addr.s_addr = INADDR_ANY;
//socket bind
int rc = bind(sock, (const struct sockaddr *)&serveraddr, sizeof(serveraddr
));
//socket error bindung
if(rc == -1){
perror("socket wurde nicht gebunden");
close(sock);
exit(EXIT_FAILURE);
}
socklen_t len = 0;

int n = recvfrom(sock, (char *)buffer, 50, MSG_WAITALL, 0, &len);
buffer[n] = '\n';
Packet packet = decode_packet(buffer);
printf("%d--|--%s",packet.sequence_number, packet.data);
close(sock);


}
