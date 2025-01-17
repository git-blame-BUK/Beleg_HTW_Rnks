// Sender-Programm
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//benötigt inet_pton, inet_ntop, htons, ntohs
#include <arpa/inet.h>

// Benötigt für IPV6_JOIN_GROUP
#include <net/if.h>

#include <errno.h>

#include "packet.h"

int create_socket(const char* multicast_address, int port, const char* interface_name, struct sockaddr_in6* multicast_addr) {
    //Socketinstanz erstellen
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0){
        printf("Socket konnte nicht erstellt werden\n");
        exit(EXIT_FAILURE);
    }
    //Socket reuse option setzen
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        printf("setsockopt(SO_REUSEADDR) fehlgeschlagen\n");
        exit(EXIT_FAILURE);
    }
    
    //Loopback aktivieren -> Pakete werden auch an den Sender gesendet
    unsigned int loop = 1;
    setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop));
    
    //ipv6_mreq Struktur erstellen
    struct ipv6_mreq group;
    memset(&group, 0, sizeof(group));

    //Multi-Cast-Adresse in die Struktur schreiben
    if (inet_pton(AF_INET6, multicast_address, &group.ipv6mr_multiaddr) != 1) {
        printf("Fehlerhafte Multi-Cast-Adresse\n");
        exit(EXIT_FAILURE);
    }

    //Interface-Index in die Struktur schreiben
    group.ipv6mr_interface = if_nametoindex(interface_name);
    if (group.ipv6mr_interface == 0) {
        printf("Fehlerhafter Interface-Name\n");
        exit(EXIT_FAILURE);
    }

    //Beitreten zur Multi-Cast-Gruppe
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &group, sizeof(group)) < 0) {
        printf("Fehler beim Beitreten zur Multi-Cast-Gruppe\n");
        exit(EXIT_FAILURE);
    }

    //Multicast-Adresse in die Struktur schreiben
    memset(multicast_addr, 0, sizeof(*multicast_addr));
    multicast_addr->sin6_family = AF_INET6;
    multicast_addr->sin6_port = htons(port);
    multicast_addr->sin6_addr = group.ipv6mr_multiaddr;
    multicast_addr->sin6_scope_id = group.ipv6mr_interface;

    return sock;
}

void send_one_packet(int sock, const struct sockaddr_in6* multicast_addr, Packet packet) {
    //Paket senden
    if (sendto(sock, &packet, sizeof(packet), 0, (struct sockaddr*)multicast_addr, sizeof(*multicast_addr)) < 0) {
        printf("Paket konnte nicht gesendet werden\n");
    }

    printf("Paket mit Sequenznummer %d und Daten: %s gesendet\n", packet.sequence_number, packet.data);
}

void send_packet_list(int sock, const struct sockaddr_in6* multicast_addr, Packet* packets, int packet_count) {
    for (int i = 0; i < packet_count; i++) {
        if (i == packet_count - 1) {
            packets[i].is_end = 1;
        }
        send_one_packet(sock, multicast_addr, packets[i]);
    }
}

int read_file(const char* filename, Packet** packets_out) {
    //Datei öffnen
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        perror("Fehler beim Öffnen der Datei");
        exit(EXIT_FAILURE);
    }

    int counter = 0;
    int max_packet = 10;

    //Paketliste erstellen 
    Packet* packets = malloc(max_packet * sizeof(Packet));
    if (packets == NULL) {
        perror("Fehler beim Allokieren von Speicher für die Paketliste.");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    //Datei zeilenweise lesen bis Zeile leer ist
    char line[MAX_LINE_LEN];
    while (fgets(line, MAX_LINE_LEN, file) != NULL) {
        char* newline_pos = strchr(line, '\n');
        if (newline_pos) {
            *newline_pos = '\0';
        }
        //Maximale Anzahl von Paketen erreicht
        if (counter == max_packet) {
            printf("Maximale Anzahl von Paketen erreicht.");
            break;
        }

        //Paketliste füllen
        packets[counter].sequence_number = counter; // automatic sequenznumber
        strncpy(packets[counter].data, line, MAX_LINE_LEN - 1); 
        packets[counter].data[MAX_LINE_LEN - 1] = '\0';
        packets[counter].is_end = 0; 

        printf("Gelesene Zeile: seq=%d, data=%s\n", packets[counter].sequence_number, packets[counter].data);

        counter++;
    }
    fclose(file);

    *packets_out = packets;
    return counter;
}





int main(int argc, char* argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Nutzung: %s <multicast_address> <port> <interface> <input_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //Parameter einlesen
    const char* multicast_address = argv[1];
    int port = atoi(argv[2]);
    const char* interface_name = argv[3];
    const char* input_file = argv[4];

    //Socketadresse erstellen
    struct sockaddr_in6 multicast_addr;
    //Socket erstellen
    int sock = create_socket(multicast_address, port, interface_name, &multicast_addr);

    //Hallo-Paket senden
    Packet hello_packet = {0, 0, "Hello"};
    send_one_packet(sock, &multicast_addr, hello_packet);

    //Paketliste aus Datei lesen
    Packet *packetlist = NULL;
    int packet_count = read_file(input_file, &packetlist);

    //Paketliste senden
    send_packet_list(sock, &multicast_addr, packetlist, packet_count);

    printf("Client beendet\n");
    close(sock);
    return 0;
}