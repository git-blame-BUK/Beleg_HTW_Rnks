// Receiver Programm
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// benötigt inet_pton, inet_ntop, htons, ntohs
#include <arpa/inet.h>

// Benötigt für IPV6_JOIN_GROUP
#include <net/if.h>

// Benötigt für Fehlerbehandlung
#include <errno.h>

#include <time.h>

#include "packet.h"
# include "nack.h"


int create_socket(const char* multicast_address, int port, const char* interface_name) {
    // Socketinstanz erstellen
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0){
        printf("Socket konnte nicht erstellt werden\n");
        exit(EXIT_FAILURE);
    }
    // Socket reuse option setzen
    {
        int reuse = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            printf("setsockopt(SO_REUSEADDR) fehlgeschlagen\n");
            exit(EXIT_FAILURE);
        }
        #ifdef SO_REUSEPORT
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
            printf("setsockopt(SO_REUSEPORT) fehlgeschlagen\n");
            exit(EXIT_FAILURE);
        }
        #endif
    }
    // Loopback aktivieren -> Pakete werden auch an den Sender gesendet (Weil wir lokal arbeiten)
    int loop = 1;
    setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop));

    // Initialisiere sockaddr_in6 Struct für die Serveradresse
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    addr.sin6_addr = in6addr_any;

    // Binden des Sockets an die Adresse
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("Bind fehgeschlagen\n");
        exit(EXIT_FAILURE);
    }

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

    return sock;
}

void send_nack(int sock, const struct sockaddr_in6* sender_addr, int sequence_number, int timestamp) {
    NACK nack = {sequence_number, timestamp};
    if (sendto(sock, &nack, sizeof(nack), 0, (struct sockaddr*)sender_addr, sizeof(*sender_addr)) < 0) {
     printf("Paket konnte nicht gesendet werden\n");
    }
    printf("Nack für Sequenznummer %d mit Timestamp: %d gesendet\n", nack.sequence_number, nack.Timestamp);
}

int receive_one_packet(int sock, Packet* packet, struct sockaddr_in6* sender_addr, socklen_t* sender_len) {
    int len = recvfrom(sock, packet, sizeof(Packet), 0, (struct sockaddr*)sender_addr, sender_len);
    if (len < 0) {
        printf("Paket konnte nicht empfangen werden\n");
        return -1;
    }

    // -> weil wir nur Pointer auf das Paket haben
    int sequence_number = packet->sequence_number;
    char* data = packet->data;
    int timestamp = packet->timestamp;

    printf("Paket empfangen: seq=%d, data=%s, Timestamp=%d\n", sequence_number, data, timestamp);
    return packet->is_end;
}

void get_missing_packets(Packet* packets, int sequence_number, int sock, struct sockaddr_in6 sender_addr, socklen_t sender_len) {
    int missing_packet;
    Packet packet;

    while (1) {
        missing_packet = -1;
        // Suche nach fehlenden Paketen über Sequenznummer
        for (int i = 0; i < sequence_number; i++) {
            // Suche nach leerem Feld in Array
            if (packets[i].sequence_number != i) {
                missing_packet = i;
                break;
            }
        }

        // Ende der Schleife, wenn kein fehlendes Paket mehr gefunden wurde
        if (missing_packet == -1) {
            break; 
        }

        send_nack(sock, &sender_addr, missing_packet, packets[missing_packet].timestamp);
        int is_end = receive_one_packet(sock, &packet, &sender_addr, &sender_len);
        packets[packet.sequence_number] = packet;

        if (is_end == 1) {
            break; // End of transmission
        }
    }
}

int get_packet_count(Packet* packets) {
    int packet_count = 0;
    // Zählen wie viele Pakete im Array liegen
    for (int i = 0; i < MAX_PACKETS; i++) {
        // Prüft dass Arrayfeld nicht leer ist
        if (packets[i].sequence_number == i) {
            packet_count++;
        }
    }
    return packet_count;
}

void send_one_packet(int sock, const struct sockaddr_in6* multicast_addr, Packet packet) {
    packet.timestamp = time(NULL);
    // Paket senden
    if (sendto(sock, &packet, sizeof(packet), 0, (struct sockaddr*)multicast_addr, sizeof(*multicast_addr)) < 0) {
        printf("Paket konnte nicht gesendet werden\n");
    }

    printf("Paket mit Sequenznummer %d und Daten: %s gesendet. Timestamp: %d\n", packet.sequence_number, packet.data, packet.timestamp);
}

void send_hello_ack(int sock, const struct sockaddr_in6* sender_addr) {
    Packet packet = {-1, 0, "Hello ack", time(NULL)};
    send_one_packet(sock, sender_addr, packet);
    printf("Hallo ack gesendet \n");
}

void wait_for_hello(int sock, struct sockaddr_in6* sender_addr, socklen_t* sender_len) {
    Packet packet;

    while (1) {
        int len = recvfrom(sock, &packet, sizeof(packet), 0, (struct sockaddr*)sender_addr, sender_len);
        if (len < 0) {
            printf("Paket konnte nicht empfangen werden\n");
            continue;
        }

        if (strcmp(packet.data, "Hello") == 0) {
            printf("Hallo empfangen.\n");
            break;
        }
    }
}

void send_close(int sock, const struct sockaddr_in6* sender_addr) {
    Packet packet = {-1, 1, "Close", time(NULL)};
    send_one_packet(sock, sender_addr, packet);
    printf("Close gesendet \n");
}

void wait_for_close(int sock, struct sockaddr_in6* sender_addr, socklen_t* sender_len) {
    Packet packet;

    while (1) {
        int len = recvfrom(sock, &packet, sizeof(packet), 0, (struct sockaddr*)sender_addr, sender_len);
        if (len < 0) {
            printf("Paket konnte nicht empfangen werden\n");
            continue;
        }

        if (strcmp(packet.data, "Close Ack") == 0) {
            printf("Close ack empfangen\n");
            break;
        }
    }
}

Packet* receiver_main(int sock) {
    int packet_count = 0;

    Packet* packets = (Packet*)malloc(MAX_PACKETS * sizeof(Packet));
    if (!packets) {
        printf("Speicher konnte nicht allokiert werden\n");
        exit(EXIT_FAILURE);
    }
    // Paket variable zur Prüfung der Sequenznummer
    Packet packet;

    // Senderadresse Struktur initialisieren
    struct sockaddr_in6 sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    // Hello Ack
    wait_for_hello(sock, &sender_addr, &sender_len);
    send_hello_ack(sock, &sender_addr);

    // While-Schleife für Paketempfang
    while (1) {
        // Paket empfangen, Senderadresse wird in sender_addr gespeichert
        int is_end = receive_one_packet(sock, &packet, &sender_addr, &sender_len);     
        if (strcmp(packet.data, "Hello") == 0) {
            printf("Hallo empfangen.\n");
            // Hallo ack senden
            send_hello_ack(sock, &sender_addr);
            continue;
        }
        // Paket wird immer an die richtige Stelle (Sequenznummer) in Array geschrieben -> Array ist immer nach Sequenznummer sortiert
        packets[packet.sequence_number] = packet;

        // Überprüfung auf Fehlende Pakete und NACK-Handling
        get_missing_packets(packets, packet.sequence_number, sock, sender_addr, sender_len); 

        // Paketcount setzen (paket_count++ zu kompliziert wegen NACKs und Paketen die doppelt verschickt werden)
        packet_count = get_packet_count(packets);

        // Letztes Paket hat immer is_end = 1
        if (is_end == 1) {
            printf("Ende der Übertragung erreicht.\n");
            break;
        }
        // Schleife verlassen falls die Maximale Anzahl an Paketen erhalten wurde / das Paket Array voll ist
        if (packet_count == MAX_PACKETS) {
            printf("Maximale Anzahl an Paketen erreicht\n");
            break;
        }
    }

    // Timeout damit Close Paket nicht in NACK Timeout vom Client gesendet wird
    usleep(500 * 1000); // 300 ms
    // Close Ack
    send_close(sock, &sender_addr);
    wait_for_close(sock, &sender_addr, &sender_len);

    return packets;
}

void write_packets_to_file(const char* output_file, Packet* packets) {
    FILE* file = fopen(output_file, "w");
    if (!file) {
        printf("Datei konnte nicht geöffnet werden\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < MAX_PACKETS; i++) {
        fprintf(file, "%s\n", packets[i].data);
        if (packets[i].is_end == 1) {
            break;
        }
    }

    fclose(file);
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        // Kommando Aufruf
        // ./server1 ff02::1 12345 lo0 output.txt
        fprintf(stderr, "Nutzung: %s <multicast_address> <port> <interface> <output_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char* multicast_address = argv[1];
    int port = atoi(argv[2]);
    const char* interface_name = argv[3];
    const char* output_file = argv[4];

    int sock = create_socket(multicast_address, port, interface_name);

    // Paket Programmlogik
    Packet* packets = receiver_main(sock);
    write_packets_to_file(output_file, packets);

    printf("Receiver beendet\n");
    close(sock);
    return 0;
}