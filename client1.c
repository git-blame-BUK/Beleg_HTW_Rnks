// Sender-Programm
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <errno.h>
#include <time.h>

#include "packet.h"

int create_socket(const char* multicast_address, int port, const char* interface_name, struct sockaddr_in6* multicast_addr) {
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0){
        printf("Socket konnte nicht erstellt werden\n");
        exit(EXIT_FAILURE);
    }
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        printf("setsockopt(SO_REUSEADDR) fehlgeschlagen\n");
        exit(EXIT_FAILURE);
    }
    
    //loopback aktiv;
    unsigned int loop = 1;
    setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop));
    
    struct ipv6_mreq group;
    memset(&group, 0, sizeof(group));

    if (inet_pton(AF_INET6, multicast_address, &group.ipv6mr_multiaddr) != 1) {
        printf("Fehlerhafte Multi-Cast-Adresse\n");
        exit(EXIT_FAILURE);
    }

    group.ipv6mr_interface = if_nametoindex(interface_name);
    if (group.ipv6mr_interface == 0) {
        printf("Fehlerhafter Interface-Name\n");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &group, sizeof(group)) < 0) {
        printf("Fehler beim Beitreten zur Multi-Cast-Gruppe\n");
        exit(EXIT_FAILURE);
    }

    memset(multicast_addr, 0, sizeof(*multicast_addr));
    multicast_addr->sin6_family = AF_INET6;
    multicast_addr->sin6_port = htons(port);
    multicast_addr->sin6_addr = group.ipv6mr_multiaddr;
    multicast_addr->sin6_scope_id = group.ipv6mr_interface;

    return sock;
}

void send_one_packet(int sock, const struct sockaddr_in6* multicast_addr, Packet packet) {
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





int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Nutzung: %s <multicast_address> <port> <interface>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char* multicast_address = argv[1];
    int port = atoi(argv[2]);
    const char* interface_name = argv[3];

    struct sockaddr_in6 multicast_addr;
    int sock = create_socket(multicast_address, port, interface_name, &multicast_addr);

    Packet hello_packet = {0, 0, "Hello"};
    send_one_packet(sock, &multicast_addr, hello_packet);

    Packet *packetlist = NULL;
    int packet_count = read_file("pakete.txt", &packetlist);
    send_packet_list(sock, &multicast_addr, packetlist, packet_count);

    printf("Client beendet\n");
    close(sock);
    return 0;
}