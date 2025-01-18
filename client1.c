// Sender-Programm
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// benötigt inet_pton, inet_ntop, htons, ntohs
#include <arpa/inet.h>

// Benötigt für IPV6_JOIN_GROUP
#include <net/if.h>
// Benötigt für Timeouts
#include <time.h>

// Benötigt für Fehlerbehandlung
#include <errno.h>

#include "packet.h"
#include "nack.h"

int create_socket(const char* multicast_address, int port, const char* interface_name, struct sockaddr_in6* multicast_addr) {
    // Socketinstanz erstellen
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0){
        printf("Socket konnte nicht erstellt werden\n");
        exit(EXIT_FAILURE);
    }
    // Socket reuse option setzen
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        printf("setsockopt(SO_REUSEADDR) fehlgeschlagen\n");
        exit(EXIT_FAILURE);
    }
    
    // Loopback aktivieren -> Pakete werden auch an den Sender gesendet (Weil wir lokal arbeiten)
    unsigned int loop = 1;
    setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop));
    
    // ipv6_mreq Struktur erstellen
    struct ipv6_mreq group;
    memset(&group, 0, sizeof(group));

    // Multi-Cast-Adresse in die Struktur schreiben
    if (inet_pton(AF_INET6, multicast_address, &group.ipv6mr_multiaddr) != 1) {
        printf("Fehlerhafte Multi-Cast-Adresse\n");
        exit(EXIT_FAILURE);
    }

    // Interface-Index in die Struktur schreiben
    group.ipv6mr_interface = if_nametoindex(interface_name);
    if (group.ipv6mr_interface == 0) {
        printf("Fehlerhafter Interface-Name\n");
        exit(EXIT_FAILURE);
    }

    // Beitreten zur Multi-Cast-Gruppe
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &group, sizeof(group)) < 0) {
        printf("Fehler beim Beitreten zur Multi-Cast-Gruppe\n");
        exit(EXIT_FAILURE);
    }

    // Multicast-Adresse in die Struktur schreiben
    memset(multicast_addr, 0, sizeof(*multicast_addr));
    multicast_addr->sin6_family = AF_INET6;
    multicast_addr->sin6_port = htons(port);
    multicast_addr->sin6_addr = group.ipv6mr_multiaddr;
    multicast_addr->sin6_scope_id = group.ipv6mr_interface;

    return sock;
}

NACK receive_nack(int sock, struct sockaddr_in6* sender_addr, socklen_t* sender_len) {
    NACK nack = {-1, -1};
    int len = recvfrom(sock, &nack, sizeof(nack), 0, (struct sockaddr*)sender_addr, sender_len);
    if (len < 0) {
        printf("Paket konnte nicht empfangen werden\n");
        return nack;
    }

    printf("NACK empfangen: seq=%d, Timestamp=%d\n", nack.sequence_number, nack.Timestamp);
    return nack;
}

void send_one_packet(int sock, const struct sockaddr_in6* multicast_addr, Packet packet) {
    packet.timestamp = time(NULL);
    // Paket senden
    if (sendto(sock, &packet, sizeof(packet), 0, (struct sockaddr*)multicast_addr, sizeof(*multicast_addr)) < 0) {
        printf("Paket konnte nicht gesendet werden\n");
    }

    printf("Paket mit Sequenznummer %d und Daten: %s gesendet. Timestamp: %d\n", packet.sequence_number, packet.data, packet.timestamp);
}

void send_packet_list(int sock, const struct sockaddr_in6* multicast_addr, struct sockaddr_in6* sender_addr, socklen_t* sender_len, Packet* packets, int packet_count, int error) {
    NACK nack;

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 300000; // 300 ms

    // Dateidescritor für select
    fd_set read_fds;

    for (int i = 0; i < packet_count; i++) {
        if (i == packet_count - 1) {
            packets[i].is_end = 1;
        }
        if (error == 1){
            // Fehler simulieren
            if (i == 2) {
                continue;
            }
            if (i == 3) {
                continue;
            }
        }    
        send_one_packet(sock, multicast_addr, packets[i]);

        // Alle Dateidiscriptoren entfernen
        FD_ZERO(&read_fds);
        // Socket zu fd_set hinzufügen
        FD_SET(sock, &read_fds);

        int activity;
        // While Schleife für NACK-Handling
        while (1) {
            // Socket überwachen, nur lesen
            activity = select(sock + 1, &read_fds, NULL, NULL, &timeout);
            if (activity < 0) {
                perror("select Fehler");
                exit(EXIT_FAILURE);
            }
            if (activity == 0) {
                printf("Timeout\n");
                break;
            }
            // Wenn activity > 0 dann gibt es Daten zum Lesen
            if (activity > 0) {
                // Überprüft ob read_fds gesetzt ist
                if (FD_ISSET(sock, &read_fds)) {
                    nack = receive_nack(sock, sender_addr, sender_len);
                    if (nack.sequence_number != -1) {
                        printf("NACK empfangen: seq=%d, Timestamp=%d\n", nack.sequence_number, nack.Timestamp);
                        send_one_packet(sock, multicast_addr, packets[nack.sequence_number]);
                    }
                }
            }
            // Timeout reseten falls NACK empfangen wurde für weitere NACKs
            timeout.tv_sec = 0;
            timeout.tv_usec = 300000; // 300 ms
        }
    }
}

void wait_for_hello_back(int sock, struct sockaddr_in6* sender_addr, socklen_t* sender_len, int receiver_count) {
    Packet packet;

    int hallo_count = 0;

    while (1) {
        int len = recvfrom(sock, &packet, sizeof(packet), 0, (struct sockaddr*)sender_addr, sender_len);
        if (len < 0) {
            printf("Paket konnte nicht empfangen werden\n");
            continue;
        }

        if (strcmp(packet.data, "Hello ack") == 0) {
            printf("Hallo ack empfangen\n");
            hallo_count++;
        }
        if (hallo_count == receiver_count) {
            break;
        }
    }
}

void wait_for_close(int sock, struct sockaddr_in6* sender_addr, socklen_t* sender_len, int receiver_count) {
    Packet packet;

    int close_count = 0;

    while (1) {
        int len = recvfrom(sock, &packet, sizeof(packet), 0, (struct sockaddr*)sender_addr, sender_len);
        if (len < 0) {
            printf("Paket konnte nicht empfangen werden\n");
            continue;
        }

        if (strcmp(packet.data, "Close") == 0) {
            printf("CLose empfangen\n");
            close_count++;
        }
        if (close_count == receiver_count) {
            break;
        }
    }
}

int read_file(const char* filename, Packet** packets_out) {
    // Datei öffnen
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        perror("Fehler beim Öffnen der Datei");
        exit(EXIT_FAILURE);
    }

    int counter = 0;

    // Paketliste erstellen 
    Packet* packets = malloc(MAX_PACKETS * sizeof(Packet));
    if (packets == NULL) {
        perror("Fehler beim Allokieren von Speicher für die Paketliste.");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    // Datei zeilenweise lesen bis Zeile leer ist
    char line[MAX_LINE_LEN];
    while (fgets(line, MAX_LINE_LEN, file) != NULL) {
        char* newline_pos = strchr(line, '\n');
        if (newline_pos) {
            *newline_pos = '\0';
        }
        // Maximale Anzahl von Paketen erreicht
        if (counter == MAX_PACKETS) {
            printf("Maximale Anzahl von Paketen erreicht.");
            break;
        }

        // Paketliste füllen
        packets[counter].sequence_number = counter;
        strncpy(packets[counter].data, line, MAX_LINE_LEN - 1); 
        packets[counter].data[MAX_LINE_LEN - 1] = '\0';
        packets[counter].is_end = 0;
        packets[counter].timestamp = -1; 

        printf("Gelesene Zeile: seq=%d, data=%s\n", packets[counter].sequence_number, packets[counter].data);

        counter++;
    }
    fclose(file);

    *packets_out = packets;
    return counter;
}


int main(int argc, char* argv[]) {
    if (argc < 8) {
        // Kommando für die Nutzung des Programms ohne simulierten Fehler
        // ./client1 ff02::1 12345 lo0 pakete.txt 2 0 3

        // Kommando für die Nutzung des Programms mit simulierten Fehler
        // ./client1 ff02::1 12345 lo0 pakete.txt 2 1 3

        fprintf(stderr, "Nutzung: %s <multicast_address> <port> <interface> <input_file> <Anzahl Receiver> <Fehlerfall simulieren> <Fenstergröße>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    printf("Fenstergröße erscheint zum nächsten release\n");

    // Parameter einlesen
    const char* multicast_address = argv[1];
    int port = atoi(argv[2]);
    const char* interface_name = argv[3];
    const char* input_file = argv[4];
    int receiver_count = atoi(argv[5]);
    int error = atoi(argv[6]);
    int window_size = atoi(argv[7]);

    // Socketadresse erstellen
    struct sockaddr_in6 multicast_addr;
    // Socket erstellen
    int sock = create_socket(multicast_address, port, interface_name, &multicast_addr);

    // Senderadresse
    struct sockaddr_in6 sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    // Hallo-Paket senden
    Packet hello_packet = {-1, 0, "Hello"};
    send_one_packet(sock, &multicast_addr, hello_packet);
    // Hallo acks empfangen
    wait_for_hello_back(sock, &sender_addr, &sender_len, receiver_count);

    // Paketliste aus Datei lesen
    Packet *packetlist = NULL;
    int packet_count = read_file(input_file, &packetlist);

    // Paketliste senden
    send_packet_list(sock, &multicast_addr, &sender_addr, &sender_len, packetlist, packet_count, error);

    // Warten auf Close
    wait_for_close(sock, &sender_addr, &sender_len, receiver_count);
    // Timeout für Close ack, damit Server sie empfangen können
    usleep(300 * 1000); // 300 ms
    // Close ack senden
    Packet end_packet = {-1, 1, "Close Ack"};
    send_one_packet(sock, &multicast_addr, end_packet);


    printf("Client beendet\n");
    close(sock);
    return 0;
}