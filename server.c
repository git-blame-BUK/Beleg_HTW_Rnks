#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>

#include "packet.h"


char* encode_packet(Packet *packet) {
    char* buffer = (char*)malloc(MAX_LINE_LEN + 32); // Allocate memory for each packet
    if (buffer == NULL) {
        perror("Memory allocation failed");
        return NULL;
    }
    snprintf(buffer, MAX_LINE_LEN + 32, "%d|%s", packet->sequence_number, packet->data);
    return buffer;
}

char** create_encoded_array(Packet* packetlist, int len) {
    char** encoded_array = (char**)malloc(len * sizeof(char*));
    if (encoded_array == NULL) {
        perror("Memory allocation failed");
        return NULL;
    }

    for (int i = 0; i < len; i++) {
        encoded_array[i] = encode_packet(&packetlist[i]);
        if (encoded_array[i] == NULL) {
            perror("Encoding failed");
            // Free previously allocated memory
            for (int j = 0; j < i; j++) {
                free(encoded_array[j]);
            }
            free(encoded_array);
            return NULL;
        }
    }

    return encoded_array;
}

// Usage example
void send_packets(int sock, Packet* packetlist, int len, struct sockaddr_in serveraddr) {
    char** encoded_array = create_encoded_array(packetlist, len);
    if (encoded_array == NULL) {
        return;
    }

    for (int i = 0; i < len; i++) {
        int sent_len = sendto(sock, (const char *)encoded_array[i], strlen(encoded_array[i]), 0, (const struct sockaddr *)&serveraddr, sizeof(serveraddr));
        if (sent_len == -1) {
            perror("Fehler beim senden");
        }
        free(encoded_array[i]);
    }

    free(encoded_array);
}

int main(void) {
    // Packetlist
    Packet packetlist[1];
    int len = sizeof(packetlist) / sizeof(packetlist[0]);
    //test msg
    Packet packet_for_list;
    packet_for_list.sequence_number = 1;
    strcpy(packet_for_list.data, "hallo hier ist der client");
    packetlist[0] = packet_for_list;
   
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

    send_packets(sock, packetlist, len, serveraddr);

    close(sock);
}





