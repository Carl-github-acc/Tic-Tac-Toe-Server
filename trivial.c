#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>

// Inputstring from the user
char *inputStr = NULL;

// The file descriptor of the socket
int fd = -1;

// Value indicating if the game is over
int gameOver = 0;

// The number of answers in the current question
int qNum = -1;

// The output file stream
FILE *fileOut = NULL;

// Convert hostname to byte address
struct in_addr* name_to_IP_addr(char* hostname);

// Connect to the given address and port
void connect_to(struct in_addr* ipAddress, int port);

// Print information from server
void print_to_user(int *qNum, int *qCount, FILE *netFileIn, size_t *len, 
        char *str, int fd, pthread_t pid);

// Print out the current status of the game
int print_status(FILE *netFileIn, char *str, int begin, int finish, 
        int *canWrite, int correct);

// Listen to the server and write what was sent to stdout
void listen_server(pthread_t pid);

// Exit on protocol error
void protocol_exit(char *str);

// Send data to the server
void *send_server(void* args);

// Handling server disconnects
void server_disconnected(int fd, pthread_t pid);

struct in_addr* name_to_IP_addr(char* hostname)
{
    int error;
    struct addrinfo* addressInfo;
    // Convert the given hostname to its ip address
    error = getaddrinfo(hostname, NULL, NULL, &addressInfo);
    if (error) {
        fprintf(stderr, "Bad Server\n");
        exit(5);
    }
    return &(((struct sockaddr_in*)(addressInfo->ai_addr))->sin_addr);
}

int main(int argc, char *argv[]) {
    struct in_addr* ipAddress;
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: trivial name port [host]\n");
        exit(1);
    }
    char *name = argv[1];
    int port = atoi(argv[2]);
    if (!(port > 1 && port <= 65535)) {
        fprintf(stderr, "Invalid Port\n");
        exit(4);
    }
    if (argc == 3) {
        ipAddress = name_to_IP_addr("localhost");
    } else {
        ipAddress = name_to_IP_addr(argv[3]);
    }
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    // Handle SIGPIPE
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGPIPE, &sa, NULL);
    connect_to(ipAddress, port);
    fileOut = fdopen(fd, "w");
    fprintf(fileOut, "%s\n", name);
    fflush(fileOut);
    pthread_t tid;
    if (pthread_create(&tid, NULL, send_server, NULL) != 0) {
        fprintf(stderr, "System Error\n");
        exit(8);
    }
    listen_server(tid);
    pthread_join(tid, 0);
    close(fd);
    return 0;
}

void server_disconnected(int fd, pthread_t pid) {
    if (inputStr != NULL) {
        free(inputStr);
    }
    if (fileOut != NULL) { 
        fclose(fileOut);
    }
    close(fd);
    if (gameOver == 1) {
        exit(0);
    }
    fprintf(stderr, "Server Disconnected\n");
    exit(10);
}

void print_to_user(int *qNum, int *qCount, FILE *netFileIn, size_t *len, 
        char *str, int fd, pthread_t pid) {
    if (*qNum != -1) {
        (*qCount)++;
    }
    if (!strcmp(str, ".\n") && *qNum == -1) {
        printf("=====\n");
        if (getline(&str, len, netFileIn) == -1) {
            server_disconnected(fd, pid);
        };
        *qNum = atoi(str);
        *qCount = 0;
    } else if (*qNum != -1) {
        printf("%d: %s", *qCount, str);
    } else if (gameOver != 1) {
        printf("%s", str);
    }
}

void protocol_exit(char *str) {
    fprintf(stderr, "Protocol Error\n");
    if (str != NULL) {
        free(str);
    }
    exit(12);
}

void listen_server(pthread_t pid) {
    FILE *netFileIn = fdopen(fd, "r");
    if (netFileIn == NULL) {
        fprintf(stderr, "System Error\n");
        exit(8);
    }
    char *str = NULL;
    size_t len = 0;
    int qCount = 0, begin = 0, canWrite = 1, correct = 1;
    // Print out information from server to standard out
    while (gameOver == 0 && getline(&str, &len, netFileIn) > 0) {
        if (!strncmp(str, "$", 1) && begin == 0) {
            fprintf(stderr, "Server Full\n");
            exit(11);
        }
        if (canWrite == 1) {
            qNum = -1;
            if (print_status(netFileIn, str, begin, 0, &canWrite, 
                    correct) == -1) {
                protocol_exit(str);
            }
            correct = 0;
            continue;
        }
        correct = 1;
        begin = 1;
        print_to_user(&qNum, &qCount, netFileIn, &len, str, fd, pid);
        if (qCount == qNum) {
            printf ("++++\n");
            canWrite = 1;
        }
        fflush(stdout);
    }
    free(str);
    fclose(netFileIn);
    server_disconnected(fd, pid);
}

int print_status(FILE *netFileIn, char *str, int begin, int finish, 
        int *canWrite, int correct) {
    size_t len = 0;
    if (!strncmp(str, "S", 1)) {
        if (correct == 1) {
            return -1;
        }
        str++;
        printf("Scores: %s", str);
        if (finish == 0) {
            printf("\n");
        }
        *canWrite = 0;
        return 0;
    } else if (!strncmp(str, "W", 1)) {
        str++;
        char *tempstr = NULL;
        int correct = 1;
        for (int i = 0; i < 2; i++) {
            if (getline(&tempstr, &len, netFileIn) < 0) {
                fprintf(stderr, "Server Disconnected\n");
                exit(10);
            }
            print_status(netFileIn, tempstr, begin, 1, canWrite, correct);
            correct = 0;
        }
        printf("Winner(s): %s", str);
        gameOver = 1;
        free(tempstr);
        return 0;
    } else if (!strncmp(str, "C", 1)) {
        str++;
        printf("Results: %s", str);
        if (begin == 0) {
            *canWrite = 0;
        }
        return 1;
    } else if (!strncmp(str, "Hello Player", 12)) {
        printf("%s", str);
        return 1;
    }
    return -1;
} 

void *send_server(void *args) {
    int inputNum = -1;
    size_t inputlen = 0;
    while (gameOver != 1) {
        while (inputNum == -1 || inputNum < 1 || inputNum > qNum) {
            if (getline(&inputStr, &inputlen, stdin) < 0) {
                fprintf(stderr, "Client EOF\n");
                exit(9);
            }
            inputNum = atoi(inputStr);
            if (inputNum == -1 || inputNum < 1 || inputNum > qNum) {
                printf("Invalid guess\n");
            }
        }
        fprintf(fileOut, "%s", inputStr);
        fflush(fileOut);
        inputNum = -1;
    }
    return NULL;
}

void connect_to(struct in_addr* ipAddress, int port) {
    struct sockaddr_in socketAddr;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    socketAddr.sin_family = AF_INET;
    socketAddr.sin_port = htons(port);
    socketAddr.sin_addr.s_addr = ipAddress->s_addr;
    // Connect to the given ip address and port
    if (connect(fd, (struct sockaddr*)&socketAddr, 
            sizeof(socketAddr)) == -1) {
        fprintf(stderr, "Bad Server\n");
        exit(5);
    }
}
