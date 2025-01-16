#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h> 
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include "packet.h"
#include "nack.h"

#define MULTICAST_ADDR "ff02::1" // Multicast-Adresse
#define MULTICAST_PORT 40400
#define MAX_PACKETS 10
#define TIMEOUT_SEC 10

void send_hello_message(int sock, struct sockaddr_in6 *serveraddr) {
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
Packet* receive_and_decode_packets(int sock, struct sockaddr_in6* serveraddr, int* out_count) {
    Packet* packets = (Packet*)malloc(sizeof(Packet) * MAX_PACKETS);
    if (!packets) {
        perror("malloc fehlgeschlagen");
        *out_count = 0;
        return NULL;
    }
    memset(packets, 0, sizeof(Packet) * MAX_PACKETS);

    int count = 0;
    while (count < MAX_PACKETS) {
        char buffer[MAX_LINE_LEN + 32] = {0};
        socklen_t server_len = sizeof(*serveraddr);

        // Debugging: Warten auf Paket
        printf("Warte auf Multicast-Pakete auf Schnittstelle ID: %u, Port: %d...\n",
               serveraddr->sin6_scope_id, ntohs(serveraddr->sin6_port));

        int n = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                         (struct sockaddr *)serveraddr, &server_len);

        // Debugging: Ergebnis von recvfrom
        if (n > 0) {
            buffer[n] = '\0';
            printf("Empfangenes Paket (Länge %d): %s\n", n, buffer);
        } else if (n == 0) {
            printf("Leeres Paket empfangen.\n");
            break;
        } else {
            perror("recvfrom fehlgeschlagen");
            continue;
        }

        // Dekodierung und Speicherung des Pakets
        Packet received_packet = decode_packet(buffer);
        printf("Dekodiertes Paket: Sequenznummer = %d, Daten = %s\n",
               received_packet.sequence_number, received_packet.data);

        packets[count] = received_packet;
        count++;
    }

    *out_count = count;
    return packets;
}


//Missing Packet check mit rückgabewert missing count

int check_missing_packets(Packet *packets, int received_count, int expected_count, int sock, struct sockaddr_in6 *serveraddr) {
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
void receive_missing_packets(int sock, struct sockaddr_in6* serveraddr, Packet* packets, int* packet_count, int max_packets) {
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


int main() {
    struct sockaddr_in6 serveraddr;
    struct ipv6_mreq group;
    int sock;
    int packet_count = 0;
    Packet* packet_list = NULL;

    // Socket erstellen
    sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock == -1) {
        perror("Fehler beim Erstellen des Sockets");
        exit(EXIT_FAILURE);
    }
    printf("Socket erfolgreich erstellt.\n");

    // Multicast-Adresse setzen
    if (inet_pton(AF_INET6, MULTICAST_ADDR, &group.ipv6mr_multiaddr) != 1) {
        perror("Ungültige Multicast-Adresse");
        close(sock);
        exit(EXIT_FAILURE);
    }
    printf("Multicast-Adresse gesetzt: %s\n", MULTICAST_ADDR);

    // Interface-ID für lo0 setzen (oder ein anderes Interface)
    group.ipv6mr_interface = if_nametoindex("lo0");
    if (group.ipv6mr_interface == 0) {
        perror("Fehler beim Ermitteln der Interface-ID");
        close(sock);
        exit(EXIT_FAILURE);
    }
    printf("Interface-ID gesetzt: %u\n", group.ipv6mr_interface);

    // Multicast-Gruppe beitreten
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &group, sizeof(group)) < 0) {
        perror("Fehler beim Beitreten zur Multicast-Gruppe");
        close(sock);
        exit(EXIT_FAILURE);
    }
    printf("Multicast-Gruppe %s auf Interface-ID %u erfolgreich abonniert.\n", MULTICAST_ADDR, group.ipv6mr_interface);


    // Scope-ID und andere Parameter setzen
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin6_family = AF_INET6;
    serveraddr.sin6_port = htons(MULTICAST_PORT);
    if (inet_pton(AF_INET6, MULTICAST_ADDR, &serveraddr.sin6_addr) != 1) {
        perror("Ungültige Multicast-Adresse für Ziel");
        close(sock);
        exit(EXIT_FAILURE);
    }
    serveraddr.sin6_scope_id = if_nametoindex("lo0");
    printf("Client Scope-ID: %u\n", if_nametoindex("lo0"));


    // Multicast-Loopback aktivieren
    unsigned int loop = 1;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop)) < 0) {
        perror("Fehler beim Aktivieren des Multicast-Loopback");
        close(sock);
        exit(EXIT_FAILURE);
    }
    printf("Multicast-Loopback aktiviert.\n");

    // Hop Limit setzen
    unsigned int hop_limit = 64;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hop_limit, sizeof(hop_limit)) < 0) {
        perror("Fehler beim Setzen des Hop Limits");
        close(sock);
        exit(EXIT_FAILURE);
    }
    printf("Hop Limit auf %u gesetzt.\n", hop_limit);

    // Timeout für Empfang setzen
    struct timeval tv = {TIMEOUT_SEC, 0};
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Fehler beim Setzen des Timeouts");
        close(sock);
        exit(EXIT_FAILURE);
    }
    printf("Timeout auf %d Sekunden gesetzt.\n", TIMEOUT_SEC);

    // Handshake mit dem Server
    printf("Sende Hallo-Nachricht an den Server...\n");
    send_hello_message(sock, &serveraddr);

    // Pakete empfangen und dekodieren
    printf("Empfange Pakete von der Multicast-Gruppe...\n");
    packet_list = receive_and_decode_packets(sock, &serveraddr, &packet_count);

    // Debug-Ausgabe der empfangenen Pakete
    printf("Empfangene Pakete: %d\n", packet_count);
    for (int i = 0; i < packet_count; i++) {
        printf("  Packet[%d]: seq = %d, data = %s\n",
               i, packet_list[i].sequence_number, packet_list[i].data);
    }

    // Fehlende Pakete prüfen
    int missing_count = check_missing_packets(packet_list, packet_count, MAX_PACKETS, sock, &serveraddr);
    if (missing_count > 0) {
        printf("Empfange fehlende Pakete...\n");
        receive_missing_packets(sock, &serveraddr, packet_list, &packet_count, MAX_PACKETS);
    } else {
        printf("Alle Pakete wurden empfangen.\n");
    }

    // Pakete sortieren und ausgeben
    sort_packets(packet_list, packet_count);
    printf("Vollständige Paketliste:\n");
    for (int i = 0; i < packet_count; i++) {
        printf("  Packet[%d]: seq = %d, data = %s\n",
               i, packet_list[i].sequence_number, packet_list[i].data);
    }

    // Ressourcen freigeben
    free(packet_list);
    close(sock);
    printf("Socket geschlossen, Programm beendet.\n");
    return 0;
}
