#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>

#include "packet.h"


static char* encode_packet(Packet *packet) {
    static char buffer[MAX_LINE_LEN + 32]; // Static buffer
    snprintf(buffer, sizeof(buffer), "%d|%s", packet->sequence_number, packet->data);
    return buffer;
}

int main(void) {
    //test msg
    Packet packet;
    packet.sequence_number = 1;
    strcpy(packet.data, "hallo hier ist der client");
    char * hello = encode_packet(&packet);
   
    struct sockaddr_in serveraddr = {0};
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    
    //Socket error meldung
    if (sock == -1){ 
        perror("Fehler beim erstellen des Sockets");
        exit(EXIT_FAILURE);
    }

    serveraddr.sin_family = AF_INET;  //IPV4
    serveraddr.sin_port = htons(40400);
    serveraddr.sin_addr.s_addr = INADDR_ANY;

    //senden
    int len = sendto(sock, (const char *)hello, strlen(hello),0, (const struct sockaddr *)&serveraddr, sizeof(serveraddr));
    if(len == -1){
        perror("Fehler beim senden");
        
    }
    close(sock);
}





