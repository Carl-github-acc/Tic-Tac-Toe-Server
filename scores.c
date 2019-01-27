#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>

// Connect to the given address and port
int connect_to(struct in_addr* ipAddress, int port);

// Send the string scores\n to the server
void send_string(FILE *outFile);

// Convert hostname to ip address
struct in_addr* name_to_IP_addr(char* hostname);

// Connect to the given ip address and port
int connect_to(struct in_addr* ipAddress, int port);

struct in_addr* name_to_IP_addr(char* hostname) {
    int error;
    struct addrinfo* addressInfo;
    // Grab the address of the client from its host name
    error = getaddrinfo(hostname, NULL, NULL, &addressInfo);
    if (error) {
        fprintf(stderr, "Bad Server\n");
        exit(5);
    }
    // Return the address of the client
    return &(((struct sockaddr_in*)(addressInfo->ai_addr))->sin_addr);
}

void send_string(FILE *outFile) {
    if (outFile == NULL) {
        fprintf(stderr, "System Error\n");
        exit(8);
    }
    if (fprintf(outFile, "scores\n") < 0) {
        fprintf(stderr, "Bad Server\n");
        exit(5);
    }
    fflush(outFile);
}

void get_string(int fd) {
    char buffer[1024];
    FILE *inFile = fdopen(fd, "r");
    if (inFile == NULL) {
        fprintf(stderr, "System Error\n");
        exit(8);
    }
    // Grab string from server and print it out to standard out
    while (fgets(buffer, 1023, inFile) != NULL) {
        printf("%s", buffer);
    }
    fclose(inFile);
}

int main(int argc, char *argv[]) {
    int fd;
    struct in_addr* ipAddress;
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "Usage: scores port [host]\n");
        exit(1);
    }
    int port = atoi(argv[1]);
    if (!(port >= 1 && port <= 65535)) {
        fprintf(stderr, "Invalid Port\n");
        exit(4);
    }
    if (argc == 2) {
        ipAddress = name_to_IP_addr("localhost");
    } else {
        ipAddress = name_to_IP_addr(argv[2]);
    }
    fd = connect_to(ipAddress, port);
    FILE *outFile = fdopen(fd, "w");
    send_string(outFile);
    get_string(fd);
    fclose(outFile);
    return 0;
}

int connect_to(struct in_addr* ipAddress, int port) {
    struct sockaddr_in socketAddr;
    int fd;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    // IPV4 family
    socketAddr.sin_family = AF_INET;
    socketAddr.sin_port = htons(port);
    socketAddr.sin_addr.s_addr = ipAddress->s_addr;
    // Connect to the ip address and port given
    if (connect(fd, (struct sockaddr*)&socketAddr, 
            sizeof(socketAddr)) == -1) {
        fprintf(stderr, "Bad Server\n");
        exit(5);
    }
    return fd;
}
