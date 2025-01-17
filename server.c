// Receiver Programm
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


int create_socket(const char* multicast_address, int port, const char* interface_name) {
    //Socketinstanz erstellen
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0){
        printf("Socket konnte nicht erstellt werden\n");
        exit(EXIT_FAILURE);
    }
    //Socket reuse option setzen
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
    //Loopback aktivieren -> Pakete werden auch an den Sender gesendet
    int loop = 1;
    setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop));

    //Initialisiere sockaddr_in6 Struct für die Serveradresse
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    addr.sin6_addr = in6addr_any;

    //Binden des Sockets an die Adresse
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("Bind fehgeschlagen\n");
        exit(EXIT_FAILURE);
    }

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

    return sock;
}

void receiver_main(int sock, const char* output_file) {
    //Datei erstellen
    FILE* file = fopen(output_file, "w");
    if (!file){
        printf("Datei konnte nicht geöffnet werden\n");
        exit(EXIT_FAILURE);
    }

    Packet packet;
    while (1) {
        //Senderadresse Stuktur initialisieren
        struct sockaddr_in6 sender_addr;
        socklen_t sender_len = sizeof(sender_addr);

        //Paket empfangen, Senderadresse wird in sender_addr gespeichert
        int len = recvfrom(sock, &packet, sizeof(packet), 0, (struct sockaddr*)&sender_addr, &sender_len);
        if (len < 0) {
            printf("Paket konnte nicht empfangen werden\n");
            exit(EXIT_FAILURE);
        }

        int is_end = packet.is_end;
        int sequence_number = packet.sequence_number;
        char* data = packet.data;

        if (strcmp(data, "Hello") == 0) {
            printf("Hallo empfangen.\n");
            continue;
        }

        //Paket in Datei schreiben
        fprintf(file, "%s", packet.data);
        fprintf(file, "\n");
        
        printf("Paket empfangen: seq=%d, data=%s\n", sequence_number, data);

        if (is_end == 1) {
            printf("Ende der Übertragung erreicht.\n");
            break;
        }
    }

    fclose(file);
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Nutzung: %s <multicast_address> <port> <interface> <output_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char* multicast_address = argv[1];
    int port = atoi(argv[2]);
    const char* interface_name = argv[3];
    const char* output_file = argv[4];

    int sock = create_socket(multicast_address, port, interface_name);
    receiver_main(sock, output_file);

    printf("Receiver beendet\n");
    close(sock);
    return 0;
}