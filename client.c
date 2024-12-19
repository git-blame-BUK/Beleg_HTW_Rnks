#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>

int main(void) {
    //test msg
    char * hello = "hallo hier ist der client";
   
    struct sockaddr_in serveraddr = {0};
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    
    //Socket error meldung
    if (sock == -1){ 
        perror("Fehler beim erstellen des Sockets");
        exit(EXIT_FAILURE);
    }

    serveraddr.sin_family = AF_INET;  //IPV4
    serveraddr.sin_port = htons(40400);
    serveraddr.sin_addr.s_addr = INADDR_ANY;

    //senden
    int len = sendto(sock, (const char *)hello, strlen(hello),0, (const struct sockaddr *)&serveraddr, sizeof(serveraddr));
    if(len == -1){
        perror("Fehler beim senden");
        
    }
    close(sock);
}





