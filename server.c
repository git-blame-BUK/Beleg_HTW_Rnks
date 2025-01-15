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
#include "packet.h"
#include "nack.h"

void wait_for_hello_message(int hello_sock, struct sockaddr_in *clientaddr) {
    socklen_t addr_len = sizeof(*clientaddr);
    char buffer[1024] = {0};

    printf("Warte auf Hallo-Nachricht vom Client...\n");

    while (1) {
        int len = recvfrom(hello_sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)clientaddr, &addr_len);
        if (len < 0) {
            perror("Fehler beim Empfang der Hallo-Nachricht");
            continue;
        }
        buffer[len] = '\0';

        if (strcmp(buffer, "Hallo") == 0) {
            printf("Hallo-Nachricht vom Client empfangen\n");

            // Antwort senden
            const char *reply = "Hallo zurück";
            if (sendto(hello_sock, reply, strlen(reply), 0, (struct sockaddr *)clientaddr, addr_len) < 0) {
                perror("Fehler beim Senden der Hallo-Antwort");
            } else {
                printf("Antwort gesendet: %s\n", reply);
            }
            break;
        }
    }
}


int NACK_Receiver_FKT(int sock, struct sockaddr_in *serveraddr, NACK *nack) {
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
    if (len >= sizeof(NACK)) {
        memcpy(nack, buffer, sizeof(NACK));
        return 0; // Erfolgreich
    } 
    else {
        fprintf(stderr, "Fehler: Ungültige NACK-Daten empfangen\n");
        return -1;
    }
}

int read_file(const char* filename, Packet** packets_out) {
    //open file
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        perror("Fehler beim Öffnen der Datei");
        exit(EXIT_FAILURE);
    }

    int counter = 0;
    int max_packet = 10;

    //make paketlist 
    Packet* packets = malloc(max_packet * sizeof(Packet));
    if (packets == NULL) {
        perror("Fehler beim Allokieren von Speicher für die Paketliste.");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    //reading each line of the file
    char line[MAX_LINE_LEN];
    while (fgets(line, MAX_LINE_LEN, file) != NULL) {
        char* newline_pos = strchr(line, '\n');
        if (newline_pos) {
            *newline_pos = '\0';
        }
        //maximum size reached
        if (counter == max_packet) {
            printf("Maximale Anzahl von Paketen erreicht.");
            break;
        }

        // Parse line in correct format: "<seqnr>|<data>")
        packets[counter].sequence_number = counter; // automatic sequenznumber
        strncpy(packets[counter].data, line, MAX_LINE_LEN - 1); 
        packets[counter].data[MAX_LINE_LEN - 1] = '\0'; 

        printf("Gelesene Zeile: seq=%d, data=%s\n", packets[counter].sequence_number, packets[counter].data);

        counter++;
    }
    fclose(file);

    *packets_out = packets;
    return counter;
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

void handle_nack(int sock, NACK *nack, Packet *packetlist, int packet_count, struct sockaddr_in *clientaddr) {
    int seqnr = nack->Seqnr;  // Fehlende Sequenznummer aus dem NACK
    if (seqnr < 0 || seqnr >= packet_count) {
        printf("Ungültige Sequenznummer im NACK: %d\n", seqnr);
        return;
    }
    printf("NACK empfangen für Paket %d\n", seqnr);

    for (int i = 0; i < packet_count; i++) {
        if (packetlist[i].sequence_number == seqnr) {
            // Paket erneut senden
            char *encoded_packet = encode_packet(&packetlist[i]);
            if (encoded_packet) {
                int sent_len = sendto(sock, encoded_packet, strlen(encoded_packet), 0,
                                      (struct sockaddr *)clientaddr, sizeof(*clientaddr));
                if (sent_len == -1) {
                    perror("Fehler beim erneuten Senden des Pakets");
                } else {
                    printf("Paket %d erneut gesendet\n", seqnr);
                }
                free(encoded_packet);
            }
            return;
        }
    }

    // Wenn Paket nicht gefunden wurde
    printf("Paket %d konnte nicht gefunden werden\n", seqnr);
}

int main(void) {
    struct sockaddr_in serveraddr = {0}, clientaddr = {0};
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        perror("Fehler beim Erstellen des Sockets");
        exit(EXIT_FAILURE);
    }

    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(40400);
    serveraddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1) {
        perror("Fehler beim Binden des Sockets");
        close(sock);
        exit(EXIT_FAILURE);
    }
    
    // Zieladresse (Client) setzen
    clientaddr.sin_family = AF_INET;
    clientaddr.sin_port = htons(40401); // Zielport
    clientaddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Ziel-IP (localhost)

    // Pakete definieren
    Packet* packetlist = NULL;
    int packet_count = read_file("pakete.txt", &packetlist);
    if (packet_count < 0) {
        close(sock);
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < packet_count; i++) {
        printf("  Packet[%d]: seq = %d, data = %s\n",
            i, packetlist[i].sequence_number, packetlist[i].data);
    }
   
   
    // Warten auf die initiale "Hallo"-Nachricht vom Client
    wait_for_hello_message(sock, &clientaddr);
   // for (int i = 0; i < packet_count; i++) {
   //     packetlist[i].sequence_number = i;
   //     snprintf(packetlist[i].data, MAX_LINE_LEN, "Daten für Paket %d", i);
   // }
    // Kurze Verzögerung vor dem Senden der Pakete
    usleep(100000); // 100 ms

    // Pakete senden
    send_packets(sock, packetlist, packet_count, clientaddr);

    // Timeout für NACK-Empfang setzen
    struct timeval tv = {10, 0};
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Fehler beim Setzen des Timeouts");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // NACK-Handling
    while (1) {
        NACK nack;
        int nack_result = NACK_Receiver_FKT(sock, &clientaddr, &nack);
        if (nack_result == 0) {
            handle_nack(sock, &nack, packetlist, packet_count, &clientaddr);
        } else if (nack_result == -1) {
            printf("Keine NACK-Nachricht empfangen (Timeout oder Fehler)\n");
            break;
        }
    }

    close(sock);
    return 0;
}
