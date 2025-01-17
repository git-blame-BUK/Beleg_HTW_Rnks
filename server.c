#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/time.h>
#include "packet.h"
#include "nack.h"

#define MULTICAST_ADDR "ff02::1" // Beispielhafte IPv6-Multicast-Adresse
#define MULTICAST_PORT 40400
#define MAX_PACKETS 10

void wait_for_hello_message(int sock, struct sockaddr_in6 *clientaddr) {
    socklen_t addr_len = sizeof(*clientaddr);
    char buffer[1024] = {0};

    printf("Warte auf Hallo-Nachricht vom Client...\n");

    while (1) {
        int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)clientaddr, &addr_len);
        if (len < 0) {
            perror("Fehler beim Empfang der Hallo-Nachricht");
            continue;
        }
        buffer[len] = '\0';

        if (strcmp(buffer, "Hallo") == 0) {
            printf("Hallo-Nachricht vom Client empfangen\n");

            const char *reply = "Hallo zurück";
            if (sendto(sock, reply, strlen(reply), 0, (struct sockaddr *)clientaddr, addr_len) < 0) {
                perror("Fehler beim Senden der Hallo-Antwort");
            } else {
                printf("Antwort gesendet: %s\n", reply);
            }
            break;
        }
    }
}



int NACK_Receiver_FKT(int sock, struct sockaddr_in6 *clientaddr, NACK *nack) {
    char buffer[1024] = {0};
    socklen_t addr_len = sizeof(*clientaddr);

    // NACK empfangen
    int len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)clientaddr, &addr_len);
    
    printf("Empfangene Daten (Länge: %d): %.*s\n", len, len, buffer);
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

void setup_multicast_group(int sock) {
    struct ipv6_mreq group;
    memset(&group, 0, sizeof(group));

    if (inet_pton(AF_INET6, MULTICAST_ADDR, &group.ipv6mr_multiaddr) != 1) {
        perror("Ungültige Multicast-Adresse");
        exit(EXIT_FAILURE);
    }

    group.ipv6mr_interface = if_nametoindex("lo0"); // Ersetzen Sie "lo0" durch das gewünschte Interface
    if (group.ipv6mr_interface == 0) {
        perror("Fehler beim Ermitteln der Interface-ID");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &group, sizeof(group)) < 0) {
        perror("Fehler beim Beitreten zur Multicast-Gruppe");
        exit(EXIT_FAILURE);
    }

    printf("Dem Multicast-Gruppe %s beigetreten\n", MULTICAST_ADDR);
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
void send_packets(int sock, Packet *packetlist, int len, struct sockaddr_in6 *multicastaddr) {
    char buffer[1024];

    for (int i = 0; i < len; i++) {
        snprintf(buffer, sizeof(buffer), "%d|%s", packetlist[i].sequence_number, packetlist[i].data);
        if (sendto(sock, buffer, strlen(buffer), 0, (struct sockaddr *)multicastaddr, sizeof(*multicastaddr)) < 0) {
            perror("Fehler beim Senden eines Pakets");
        } else {
            printf("Paket %d gesendet: %s\n", packetlist[i].sequence_number, packetlist[i].data);
        }
    }
}

void handle_nack(int sock, NACK *nack, Packet *packetlist, int packet_count, struct sockaddr_in6 *clientaddr) {
    int seqnr = nack->Seqnr;
    if (seqnr < 0 || seqnr >= packet_count) {
        printf("Ungültige Sequenznummer im NACK: %d\n", seqnr);
        return;
    }
    printf("NACK empfangen für Paket %d\n", seqnr);

    for (int i = 0; i < packet_count; i++) {
        if (packetlist[i].sequence_number == seqnr) {
            char *encoded_packet = encode_packet(&packetlist[i]);
            if (encoded_packet) {
                int sent_len = sendto(sock, encoded_packet, strlen(encoded_packet), 0, (struct sockaddr *)clientaddr, sizeof(*clientaddr));
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

    printf("Paket %d konnte nicht gefunden werden\n", seqnr);


    // Wenn Paket nicht gefunden wurde
    printf("Paket %d konnte nicht gefunden werden\n", seqnr);
}

int main(void) {
    struct sockaddr_in6 serveraddr = {0}, multicastaddr = {0};
    int sock;

    // Socket erstellen
    sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock == -1) {
        perror("Fehler beim Erstellen des Sockets");
        exit(EXIT_FAILURE);
    }
    printf("Server-Socket erfolgreich erstellt.\n");

    // Serveradresse konfigurieren
    serveraddr.sin6_family = AF_INET6;
    serveraddr.sin6_port = htons(MULTICAST_PORT);
    serveraddr.sin6_addr = in6addr_any;

    // Server an Adresse binden
    if (bind(sock, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1) {
        perror("Fehler beim Binden des Sockets");
        close(sock);
        exit(EXIT_FAILURE);
    }
    printf("Server erfolgreich an Port %d gebunden.\n", MULTICAST_PORT);

    // Multicast-Gruppe beitreten
    setup_multicast_group(sock);

    // Zieladresse (Multicast) konfigurieren
    multicastaddr.sin6_family = AF_INET6;
    multicastaddr.sin6_port = htons(MULTICAST_PORT);
    if (inet_pton(AF_INET6, MULTICAST_ADDR, &multicastaddr.sin6_addr) != 1) {
        perror("Ungültige Multicast-Adresse");
        close(sock);
        exit(EXIT_FAILURE);
    }
    multicastaddr.sin6_scope_id = if_nametoindex("lo0"); // Interface-ID für Loopback
    if (multicastaddr.sin6_scope_id == 0) {
        perror("Fehler beim Ermitteln der Interface-ID");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Multicast-Loopback deaktivieren
    unsigned int loop = 0;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop)) < 0) {
        perror("Fehler beim Deaktivieren des Multicast-Loopbacks");
    }

    // Pakete lesen
    Packet *packetlist = NULL;
    int packet_count = read_file("pakete.txt", &packetlist);
    if (packet_count < 0) {
        close(sock);
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < packet_count; i++) {
        printf("  Packet[%d]: seq = %d, data = %s\n",
               i, packetlist[i].sequence_number, packetlist[i].data);
    }

    usleep(1500000);

    // Warten auf Hallo-Nachricht vom Client
    wait_for_hello_message(sock, &multicastaddr);

    // Pakete senden
    printf("Sende Pakete an die Multicast-Gruppe...\n");
    send_packets(sock, packetlist, packet_count, &multicastaddr);

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
        int nack_result = NACK_Receiver_FKT(sock, &multicastaddr, &nack);
        if (nack_result == 0) {
            handle_nack(sock, &nack, packetlist, packet_count, &multicastaddr);
        } else if (nack_result == -1) {
            printf("Keine NACK-Nachricht empfangen (Timeout oder Fehler)\n");
            break;
        }
    }

    close(sock);
    free(packetlist);
    printf("Server beendet.\n");
    return 0;
}
