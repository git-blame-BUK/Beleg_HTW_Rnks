#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/time.h>
//TODO NACK as package
#include "packet.h"

struct NACK{
    int Seqnr;
    int sender_Adresse;
    int Timestamp;
};

int NACK_Receiver_FKT(int sock, struct sockaddr_in *serveraddr, struct NACK *nack) {
    char buffer[1024] = {0};
    socklen_t addr_len = sizeof(*serveraddr);

    // NACK empfangen
    int len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)serveraddr, &addr_len);
    if (len == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            printf("Timeout: Kein NACK empfangen\n");
        } else {
            perror("Fehler beim Empfang");
        }
        return -1;
    }

    // Parse die empfangenen Daten in die NACK-Struktur
    if (len >= sizeof(struct NACK)) {
        memcpy(nack, buffer, sizeof(struct NACK));
        return 0; // Erfolgreich
    } 
    else {
        fprintf(stderr, "Fehler: Ungültige NACK-Daten empfangen\n");
        return -1;
    }
}

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

    //timout setzen
    


    while (1){
        int a = 10; //send schleife TODO a = laene Paket array
        int b = 0;
        for (b=0; b < a; b++) {
            send_packets(sock, packetlist, len, serveraddr);
        }
        struct timeval tv = {2, 0}; //reset für sock nach 2 sekunden falls kein NACK)
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            perror("Fehler beim Setzen des Timeouts");
            close(sock);
            exit(EXIT_FAILURE);
        }
            //TODO: END FRAME
        
            //NACK receiver
            
        struct NACK nack;
        for (int i = 0; i < 5; i++) { // Maximal 10 Sekunden
            if (NACK_Receiver_FKT(sock, &serveraddr, &nack) == 0) {
                printf("NACK empfangen:\nSEQ: %d, Sender: %d, Timestamp: %d\n",
                nack.Seqnr, nack.sender_Adresse, nack.Timestamp);
                break; // Erfolgreich, Schleife beenden
            }


        }   

        

    
    }
    close(sock);
}

