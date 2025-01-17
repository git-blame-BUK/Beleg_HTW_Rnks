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

#define MAX_PACKET_SIZE 1024
#define TIMEOUT 300 // Timeout in Millisekunden
#define HELLO_SEQ_NUM (UINT32_MAX - 1)
#define MAX_EOT_RETRIES 5
#define TIMER_MULTIPLIER 3

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
    //loopback aktiv;
    unsigned int loop = 1;
    setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop));


    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    addr.sin6_addr = in6addr_any;

    //if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    //    die("Bind failed");
    //}
    
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

void send_hello_message(int sock, const struct sockaddr_in6* multicast_addr) {
    Packet hello_packet = { .seq_num = htonl(HELLO_SEQ_NUM), .is_eod = 0 };

    if (sendto(sock, &hello_packet, sizeof(hello_packet), 0, (struct sockaddr*)multicast_addr, sizeof(*multicast_addr)) < 0) {
        die("Failed to send hello message");
    }

    printf("Hello message sent.\n");
}







int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <multicast_address> <port> <interface>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char* multicast_address = argv[1];
    int port = atoi(argv[2]);
    const char* interface_name = argv[3];

    int sock = create_socket(multicast_address, port, interface_name);

    struct sockaddr_in6 multicast_addr;
    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin6_family = AF_INET6;
    multicast_addr.sin6_port = htons(port);

    if (inet_pton(AF_INET6, multicast_address, &multicast_addr.sin6_addr) != 1) {
        die("Invalid multicast address");
    }
    multicast_addr.sin6_scope_id = if_nametoindex(interface_name);

    send_hello_message(sock, &multicast_addr);



    printf("Sender completed successfully.\n");
    close(sock);
    return 0;
}