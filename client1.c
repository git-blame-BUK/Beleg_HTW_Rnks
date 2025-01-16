#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include "packet.h"
#include "nack.h"

#define MAX_PACKETS 10
#define MAX_LINE_LEN 1024

void send_hello_message(int sock, struct sockaddr_in *serveraddr) {
    const char *hello_msg = "Hallo";
    char buffer[1024] = {0};
    socklen_t addr_len = sizeof(*serveraddr);

    // "Hallo"-Nachricht senden
    if (sendto(sock, hello_msg, strlen(hello_msg), 0, (struct sockaddr *)serveraddr, addr_len) < 0) {
        perror("Fehler beim Senden der Hallo-Nachricht");
        exit(EXIT_FAILURE);
    }
    printf("Hallo-Nachricht gesendet\n");

    // Auf Antwort warten
    int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)serveraddr, &addr_len);
    if (len < 0) {
        perror("Fehler beim Empfang der Hallo-Antwort");
        exit(EXIT_FAILURE);
    }
    buffer[len] = '\0';
    printf("Antwort vom Server: %s\n", buffer);

    if (strcmp(buffer, "Hallo zurück") != 0) {
        fprintf(stderr, "Unerwartete Antwort vom Server: %s\n", buffer);
        exit(EXIT_FAILURE);
    }
}


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
        // Simuliere das Ignorieren eines bestimmten Pakets (z. B. Sequenznummer 5)
        Packet received_packet = decode_packet(buffer);
        if (received_packet.sequence_number == 5 || received_packet.sequence_number == 7) {
            printf("Paket %d wird ignoriert (Simulation eines Fehlers)\n", received_packet.sequence_number);
            continue; // Überspringe das Speichern dieses Pakets
        }
        packets[count] = received_packet;
        count++;
    }

    *out_count = count;
    return packets;
}

//Missing Packet check mit rückgabewert missing count

int check_missing_packets(Packet *packets, int received_count, int expected_count, int sock, struct sockaddr_in *serveraddr) {
    int missing_count = 0;

    for (int i = 0; i < expected_count; i++) {
        int found = 0;
        for (int j = 0; j < received_count; j++) {
            if (packets[j].sequence_number == i) {
                found = 1;
                break;
            }
        }
        if (!found) {
            missing_count++;
            printf("Fehlendes Paket: %d\n", i);

            // Sende NACK
            NACK nack = {0};
            nack.Seqnr = i;
            nack.sender_Adresse = 1234;  // Beispiel-Senderadresse
            nack.Timestamp = (int)time(NULL);

            sendto(sock, &nack, sizeof(NACK), 0, (struct sockaddr *)serveraddr, sizeof(*serveraddr));
            printf("NACK für Paket %d gesendet\n", i);
        }
    }

    return missing_count;
}


//add missing packets after NACK cycle
void add_packet_if_missing(Packet* packets, int* packet_count, int max_packets, Packet new_packet) {
    for (int i = 0; i < *packet_count; i++) {
        if (packets[i].sequence_number == new_packet.sequence_number) {
            // Paket existiert bereits
            return;
        }
    }

    // Neues Paket hinzufügen, wenn Platz vorhanden ist
    if (*packet_count < max_packets) {
        packets[*packet_count] = new_packet;
        (*packet_count)++;
    }
}

//missing Packet receiver after Nack cicle
void receive_missing_packets(int sock, struct sockaddr_in* serveraddr, Packet* packets, int* packet_count, int max_packets) {
    char buffer[MAX_LINE_LEN + 32] = {0};
    socklen_t server_len = sizeof(*serveraddr);

    while (1) {
        int n = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)serveraddr, &server_len);
        if (n < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                printf("Timeout: Keine weiteren Pakete empfangen\n");
                break;
            } else {
                perror("Fehler beim Empfangen der Pakete(NACK)");
                break;
            }
        }

        buffer[n] = '\0';
        Packet new_packet = decode_packet(buffer);
        printf("Empfangenes Paket: seq = %d, data = %s\n", new_packet.sequence_number, new_packet.data);

        // Paket hinzufügen, falls es fehlt
        add_packet_if_missing(packets, packet_count, max_packets, new_packet);
    }
}
// Bubble sort für erneut empfangene pakete
void sort_packets(Packet* packets, int packet_count) {
    for (int i = 0; i < packet_count - 1; i++) {
        for (int j = 0; j < packet_count - i - 1; j++) {
            if (packets[j].sequence_number > packets[j + 1].sequence_number) {
                // Pakete tauschen
                Packet temp = packets[j];
                packets[j] = packets[j + 1];
                packets[j + 1] = temp;
            }
        }
    }
}

void write_packets_into_file (const char *filename, Packet *packets, int packet_count)
{
    FILE *file = fopen(filename, "w");
    if (file == NULL){
        perror("Datei konnte nicht geoeffnet werden!");
        return ;
    }

    for (int i = 0 ; i < packet_count; i++){
        fprintf(file, "%s\n", packets[i].data);
    }

    fclose(file);
    printf("Paketinhalte wurden erfolgreich in Datei %s geschrieben.", filename);
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
    serveraddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Server-IP (localhost)

    
    //if (bind(sock, (const struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1) {
      //  perror("socket konnte nicht gebunden werden");
        //close(sock);
        //exit(EXIT_FAILURE);
    //}
    
    
    // Handshake mit dem Server
    send_hello_message(sock, &serveraddr);

    struct timeval tv = {5, 0}; // 5 Sekunden Timeout
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Fehler beim Setzen des Timeouts");
        close(sock);
        exit(EXIT_FAILURE);
    }


    int packet_count = 0;
    Packet* packet_list = receive_and_decode_packets(sock, &serveraddr, &packet_count);
    //status Ausgabe
    printf("Received %d packets:\n", packet_count);
    for (int i = 0; i < packet_count; i++) {
        printf("  Packet[%d]: seq = %d, data = %s\n",
               i, packet_list[i].sequence_number, packet_list[i].data);
    }
    int expected_count = MAX_PACKETS;
    //Fehlende Pakete finden Nack weiterleiten
    int missing_count = check_missing_packets(packet_list, packet_count, expected_count, sock, &serveraddr);

    //erneutes Empfangen Der Pakete mit timeout nach 5 sekunden (siehe)
    if (missing_count > 0) {
        printf("Empfange fehlende Pakete...\n");
        receive_missing_packets(sock, &serveraddr, packet_list, &packet_count, expected_count);
    } else {
        printf("Alle Pakete wurden bereits empfangen. Keine fehlenden Pakete.\n");
    }

    //finale Ausgabe
    sort_packets(packet_list, packet_count);
    printf("Vollständige Paketliste:\n");
    for (int i = 0; i < packet_count; i++) {
        printf("  Packet[%d]: seq = %d, data = %s\n",
               i, packet_list[i].sequence_number, packet_list[i].data);
    }

    // erhaltene Pakete zeilenweise in Datei schreiben
    const char* output_file = "output.txt";
    write_packets_into_file ("output.txt", packet_list, packet_count);
    
    free(packet_list);
    close(sock);
    return 0;
}