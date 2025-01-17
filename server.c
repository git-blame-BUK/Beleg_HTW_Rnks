

// Receiver-Programm
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <errno.h>
#include <time.h>
#include <net/if.h>

#include "packet.h"


void set_nonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1){
        printf("Setzen von Non-Blocking fehlgeschlagen\n");
        exit(EXIT_FAILURE);
    }
}

int create_socket(const char* multicast_address, int port, const char* interface_name) {
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0){
        printf("Socket konnte nicht erstellt werden\n");
        exit(EXIT_FAILURE);
    }
    {
        int reuse = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            perror("setsockopt(SO_REUSEADDR)");
        }
        #ifdef SO_REUSEPORT
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
            perror("setsockopt(SO_REUSEPORT)");
        }
        #endif
    }
    unsigned int loop = 1;
    setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop));


    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    addr.sin6_addr = in6addr_any;

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("Bind failed");
        exit(EXIT_FAILURE);
    }

    struct ipv6_mreq group;
    memset(&group, 0, sizeof(group));

    if (inet_pton(AF_INET6, multicast_address, &group.ipv6mr_multiaddr) != 1) {
        printf("Invalid multicast address");
        exit(EXIT_FAILURE);
    }

    group.ipv6mr_interface = if_nametoindex(interface_name);
    if (group.ipv6mr_interface == 0) {
        printf("Invalid interface name");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &group, sizeof(group)) < 0) {
        printf("Failed to join multicast group");
        exit(EXIT_FAILURE);
    }

    set_nonblocking(sock);
    return sock;
}

void receiver_main(int sock, const char* output_file) {
    FILE* file = fopen(output_file, "w");
    if (!file){
        printf("Datei konnte nicht geöffnet werden\n");
        exit(EXIT_FAILURE);
    }

    Packet packet;
    while (1) {
        struct sockaddr_in6 sender_addr;
        socklen_t sender_len = sizeof(sender_addr);

        int len = recvfrom(sock, &packet, sizeof(packet), 0, (struct sockaddr*)&sender_addr, &sender_len);
        if (len < 0) {
            printf("recvfrom failed");
            exit(EXIT_FAILURE);
        }

        int is_end = packet.is_end;
        int sequence_number = packet.sequence_number;
        char* data = packet.data;

        if (strcmp(data, "Hello") == 0) {
            printf("Hallo empfangen.\n");
            continue;
        }

        fprintf(file, "%s", packet.data);
        fprintf(file, "\n");
        
        printf("Received packet with sequence number %d and data: %s\n", sequence_number, data);

        if (is_end == 1) {
            printf("End of transmission received.\n");
            break;
        }
    }

    fclose(file);
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <multicast_address> <port> <interface> <output_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char* multicast_address = argv[1];
    int port = atoi(argv[2]);
    const char* interface_name = argv[3];
    const char* output_file = argv[4];

    int sock = create_socket(multicast_address, port, interface_name);
    receiver_main(sock, output_file);

    printf("Receiver completed successfully.\n");
    close(sock);
    return 0;
}