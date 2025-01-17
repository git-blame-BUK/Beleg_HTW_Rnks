

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


#define MAX_PACKET_SIZE 1024
#define TIMEOUT 300 // Timeout in Millisekunden
#define EOT_SEQ_NUM UINT32_MAX
#define MAX_RECEIVERS 100

// Strukturdefinitionen
typedef struct {
    uint32_t seq_num;
    int is_eod;
    char data[MAX_PACKET_SIZE];
} Packet;

// Hilfsfunktionen
void die(const char* message) {
    perror(message);
    exit(EXIT_FAILURE);
}

void wait_ms(int milliseconds) {
    usleep(milliseconds * 1000);
}

void set_nonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) die("F_GETFL failed");
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) die("F_SETFL failed");
}

int create_socket(const char* multicast_address, int port, const char* interface_name) {
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) die("Socket creation failed");

    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        die("Failed to set SO_REUSEADDR");
    }
    unsigned int loop = 1;
    setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop));


    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    addr.sin6_addr = in6addr_any;

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        die("Bind failed");
    }

    struct ipv6_mreq group;
    memset(&group, 0, sizeof(group));

    if (inet_pton(AF_INET6, multicast_address, &group.ipv6mr_multiaddr) != 1) {
        die("Invalid multicast address");
    }

    group.ipv6mr_interface = if_nametoindex(interface_name);
    if (group.ipv6mr_interface == 0) {
        die("Invalid interface name");
    }

    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &group, sizeof(group)) < 0) {
        die("Failed to join multicast group");
    }

    set_nonblocking(sock);
    return sock;
}

void receiver_main(int sock, const char* output_file) {
    FILE* file = fopen(output_file, "w");
    if (!file) die("Failed to open output file");

    Packet packet;
    while (1) {
        struct sockaddr_in6 sender_addr;
        socklen_t sender_len = sizeof(sender_addr);

        int len = recvfrom(sock, &packet, sizeof(packet), 0, (struct sockaddr*)&sender_addr, &sender_len);
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                wait_ms(100);
                continue;
            }
            perror("recvfrom failed");
            break;
        }

        uint32_t seq_num = ntohl(packet.seq_num);
        if (seq_num == EOT_SEQ_NUM) {
            printf("End of transmission received.\n");
            break;
        }

        fprintf(file, "%s", packet.data);
        printf("Received packet %u\n", seq_num);
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