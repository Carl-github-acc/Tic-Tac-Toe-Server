#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <string.h>
#include <semaphore.h>

// A struct containing information on a client
typedef struct {
    char *name;
    char *curString;
    FILE *fileIn;
    FILE *fileOut;
    int inPlay;
    int answer;
    int fd;
    int correct;
    int qNum;
    pthread_t tid;
    sem_t canRead;
} ClientInfo;

// A struct containing information oin a question
typedef struct {
    char *qStr;
    int answer;
} QuestionInfo;

// Struct with information on scores
typedef struct {
    char *name;
    int played;
    int wins;
    int disconnects;
    int score;
} ScoresInfo;

// Struct with information on the server on the specified port
typedef struct {
    int fdServer;
    int sNum;
    char *fileName;
} SendInfo;

// Information needed to listen to clients
typedef struct {
    int fd;
    int servNum;
} ListenInfo;

// Semaphore for locking modification of current number of players
sem_t *canAdd;

// User information
ClientInfo **usersAll;

// Scores of everyone that has entered a game so far
ScoresInfo *scores;

// Semaphore for allowing only one thread to write to scores;
sem_t scoresLock;

// Pids of all the accepting threads
pthread_t *lPid;

// Size of the scores
int scoresSize = 0;

// Value indicating if the server should keep going
int serverAlive = 1;

// The current question each game is on
int *qNum;

// Current number of players on the servers
int *curPlayNumAll;

// Maximum number of players for the server
int maxPlay = 0;

// Minimum number of players for the server
int minPlay = 0;

// Round time of the server
int roundTime = 0;

// Check if arguments given are valid
void check_args(int argc, char *argv[]);

// End the server on SIG_HUP
void end_server(int s);

// End the current game
void game_over(int s);

// Set up signal handling
void handle_signals(void);

// Start the server on the given port
void *start_server(void *sendArg);

// Check if the given string contains only intergers
int check_int(char *str);

// Restart the game
void restart_game(int answer, int sNum, int *curPlayNum, 
        ClientInfo *users, int fdServer);

// Exit on bad file if necessary
void bad_file(int *qNum, char* tempStr, char* sentStr);

// Open a port to listen on
int open_listen(int port);

// Send the user their status and the question
void send_status_question(int sNum, int *curPlayNum, ClientInfo *users, 
        char *qStr);

// Store all required user information
void setup_client(int *curPlayNum, int fd, ClientInfo *user, int sNum, 
        char *name, FILE *fileIn, FILE *fileOut);

// Some processing required before creating the user
void preuser_process(FILE *fileIn, char *name, FILE *fileOut, 
        int *curPlayNum);

// Listen to a connection from a client
void *listen_client(void *lInfo);

// Returns number of digits in an int
int num_digits(int dig);

// Send the questions to the users
void *send_info(void *users);

// Send out winners of the game
void end_game(int answer, int sNum);

// Send to the user which player's were correct in the last question
void check_correct(int answer, int servNum);

// Set up signal handling
void handle_signals(void);

// Disconnect a user
void disconnect_user(int userIndex, int servNum);

// Get the list of scores
void get_scores(FILE *fp);

// Send the current status of the game
void send_status(int servNum);

// Get the questions from the file
QuestionInfo get_questions(FILE *inputFile, int *qNum);


// Append end of game scores
void write_scores(int sNum, int type, int *users, int uSize);

// Get the ansdwers from the question
QuestionInfo get_answers(QuestionInfo qInfo, char *sentStr, char *tempStr, 
        int curSize, int sentSize, size_t len, FILE *inputFile, int *qNum);

QuestionInfo get_answers(QuestionInfo qInfo, char *sentStr, char *tempStr, 
        int curSize, int sentSize, size_t len, FILE *inputFile, int *qNum) {
    int ansInt = -1;
    sscanf(tempStr, "%d %d", &ansInt, &qInfo.answer);
    char tempCInt[num_digits(ansInt) + 2];
    sprintf(tempCInt, "%d\n", ansInt);
    sentStr = (char*)realloc(sentStr, sentSize + strlen(tempCInt));
    sentSize += strlen(tempCInt);
    strcat(sentStr, tempCInt);
    for(int i = 0; i < ansInt; i++) {
        if (getline(&tempStr, &len, inputFile) == -1) {
            bad_file(qNum, tempStr, sentStr);
            return qInfo;
        }
        strcat(sentStr, tempStr);
    }
    if (getline(&tempStr, &len, inputFile) == -1) {
        bad_file(qNum, tempStr, sentStr);
        return qInfo;
    }
    // Check if there are move options given than specified
    while (strcmp(tempStr, "\n")) {
        if (getline(&tempStr, &len, inputFile) == -1) {
            bad_file(qNum, tempStr, sentStr);
            return qInfo;
        }
    }
    qInfo.qStr = sentStr;
    return qInfo;
}

void disconnect_user(int userIndex, int servNum) {
    write_scores(servNum, 0, &userIndex, 1);
    if (usersAll[servNum][userIndex].curString != NULL) {
        free(usersAll[servNum][userIndex].curString);
    }
    if (usersAll[servNum][userIndex].fileIn != NULL) {
        fclose(usersAll[servNum][userIndex].fileIn);
    }
    if (usersAll[servNum][userIndex].fileOut != NULL) {
        fclose(usersAll[servNum][userIndex].fileOut);
    }
    close(usersAll[servNum][userIndex].fd);
    if (usersAll[servNum][userIndex].name != NULL) {
        free(usersAll[servNum][userIndex].name);
    }
    for (int i = userIndex; i < maxPlay - 1; i++) {
        usersAll[servNum][i] = usersAll[servNum][i+1];
    }
    usersAll[servNum][maxPlay - 1].name = NULL;
    usersAll[servNum][maxPlay - 1].inPlay = 0;
    usersAll[servNum][maxPlay - 1].fd = -1;
    usersAll[servNum][maxPlay - 1].qNum = 0;
    usersAll[servNum][maxPlay - 1].curString = NULL;
    usersAll[servNum][maxPlay - 1].fileOut = NULL;
    usersAll[servNum][maxPlay - 1].fileIn = NULL;
    usersAll[servNum][maxPlay - 1].answer = -1;
    usersAll[servNum][maxPlay - 1].correct = 0;
    sem_init(&usersAll[servNum][maxPlay - 1].canRead, 0, 0);
    curPlayNumAll[servNum]--;
}

void end_game(int answer, int sNum) {
    int winners[curPlayNumAll[sNum]];
    int losers[curPlayNumAll[sNum]];
    int winSize = 0, loseSize = 0;
    int mostWin = usersAll[sNum][0].correct;
    // Find the highest number of points
    for (int i = 0; i < curPlayNumAll[sNum]; i++) {
        if (usersAll[sNum][i].qNum != qNum[sNum]) {
            if (usersAll[sNum][i].correct > mostWin) {
                mostWin = usersAll[sNum][i].correct;
            }
        }
    }
    // Find users who have the highest number of points
    for (int i = 0; i < curPlayNumAll[sNum]; i++) {
        if (usersAll[sNum][i].qNum != qNum[sNum]) {
            if (usersAll[sNum][i].correct == mostWin) {
                winners[winSize] = i;
                winSize++;
            } else {
                losers[loseSize] = i;
                loseSize++;
            }
        }
    }
    // Print out who won to everyone that's currently connected
    for (int i = 0; i < curPlayNumAll[sNum]; i++) {
        if (usersAll[sNum][i].qNum != qNum[sNum] && 
                usersAll[sNum][i].inPlay) {
            FILE *fp = usersAll[sNum][i].fileOut;
            fprintf(fp, "W");
            for (int j = 0; j < winSize; j++) {
                char *name = usersAll[sNum][winners[j]].name;
                fprintf(fp, "%s", name);
                if (i != winSize - 1) {
                    fprintf(fp, " ");
                }
            }
            fprintf(fp, "\n");
            fflush(fp);
        }
    }
    //write_scores(sNum, 4, losers, loseSize); 
    write_scores(sNum, 2, winners, winSize); 
    check_correct(answer, sNum);
    send_status(sNum);
}

void get_scores(FILE *fp) {
    for (int i = 0; i < scoresSize; i++) {
        fprintf(fp, "%s played:%d won:%d disc:%d score:%d\n", 
                scores[i].name, scores[i].played, scores[i].wins, 
                scores[i].disconnects, scores[i].score);
    }
    fflush(fp);
    fclose(fp);
}

void restart_game(int answer, int sNum, int *curPlayNum, 
        ClientInfo *users, int fdServer) {
    end_game(answer, sNum);
    for (int i = 0; i < *curPlayNum; i++) {
        users[i].inPlay = 0;
        int semnum = 0;
        sem_getvalue(&users[i].canRead, &semnum);
        if (semnum < 1) {
            sem_post(&users[i].canRead);
        }
        pthread_kill(users[i].tid, SIGTERM);
        if (users[i].fileIn != NULL) {
            fclose(users[i].fileIn);
        }
        if (users[i].fileOut != NULL) {
            fclose(users[i].fileOut);
        }
        close(users[i].fd);
        users[i].fd = -1;
        if (users[i].name != NULL) {
            free(users[i].name);
            users[i].name = NULL;
        }
        if (users[i].curString != NULL) {
            free(users[i].curString);
            users[i].curString = NULL;
        }
    }
    *curPlayNum = 0;
    if (serverAlive == 0) {
        pthread_kill(lPid[sNum], SIGTERM);
        close(fdServer);
        pthread_exit(NULL);
    }
    sem_post(&canAdd[sNum]);
}

void send_status_question(int sNum, int *curPlayNum, ClientInfo *users, 
        char *qStr) {
    int semnum = 0;
    sem_wait(&canAdd[sNum]);
    qNum[sNum]++;
    send_status(sNum);
    for (int i = 0; i < *curPlayNum; i++) {
        sem_getvalue(&users[i].canRead, &semnum);
        if (semnum < 1) {
            sem_post(&users[i].canRead);
        }
        fprintf(users[i].fileOut, "%s", qStr);
        fflush(users[i].fileOut);
    }
    sem_post(&canAdd[sNum]);
    if (qStr != NULL) {
        free(qStr);
    }
}

void *send_info(void *sendInfo) {
    SendInfo *sInfo = (SendInfo*)sendInfo;
    int answer = -1;
    ClientInfo *users = usersAll[sInfo->sNum];
    int *curPlayNum = &curPlayNumAll[sInfo->sNum];
    FILE *readFile = fopen(sInfo->fileName, "r");
    QuestionInfo qInfo;
    qInfo = get_questions(readFile, &qNum[sInfo->sNum]);
    while(1) {
        while(*curPlayNum < minPlay) {
        }
        send_status_question(sInfo->sNum, curPlayNum, users, qInfo.qStr);
        answer = qInfo.answer;
        sleep(roundTime);
        qInfo = get_questions(readFile, &qNum[sInfo->sNum]);
        sem_wait(&canAdd[sInfo->sNum]);
        if (qInfo.qStr == NULL || qInfo.answer == -1) {
            rewind(readFile);
            restart_game(answer, sInfo->sNum, curPlayNum, users, 
                    sInfo->fdServer);
            qInfo = get_questions(readFile, &qNum[sInfo->sNum]);
            qNum[sInfo->sNum] = 0;
            continue;
        }
        check_correct(answer, sInfo->sNum);
        for (int i = 0; i < *curPlayNum; i++) {
            if (!users[i].inPlay) {
                sem_post(&users[i].canRead);
                disconnect_user(i, sInfo->sNum);
            }
        }
        for (int i = 0; i < *curPlayNum; i++) {
            users[i].qNum = qNum[sInfo->sNum];
        }
        sem_post(&canAdd[sInfo->sNum]);
    }
}

void write_scores(int sNum, int type, int *users, int uSize) {
    sem_wait(&scoresLock);
    for (int i = 0; i < uSize; i++) {
        for (int j = 0; j <= scoresSize; j++) {
            // IF user does not exist add it
            if (j == scoresSize) {
                scores = realloc(scores, 
                        (scoresSize + 1)*sizeof(ScoresInfo));
                int strSize = (strlen(usersAll[sNum][users[i]].name) * 
                        sizeof(char) + 2);
                scores[j].name = malloc(strSize);
                strcpy(scores[j].name, usersAll[sNum][users[i]].name);
                scores[j].played = 0;
                scores[j].disconnects = 0;
                scores[j].wins = 0;
                scores[j].score = 0;
                if (type == 0) {
                    scores[j].disconnects++;
                } else if (type == 1) {
                    scores[j].score++;
                } else if (type == 2) {
                    scores[j].wins++;
                } else if (type == 3) {
                    scores[j].played++;
                }
                scoresSize++;
                break;
            }
            // IF user does exist modify their scores
            if (!strcmp(usersAll[sNum][users[i]].name, scores[j].name)) {
                if (type == 0) {
                    scores[j].disconnects++;
                } else if (type == 1) {
                    scores[j].score++;
                } else if (type == 2) {
                    scores[j].wins++;
                } else if (type == 3) {
                    scores[j].played++;
                }
                break;
            }
        }
    }
    sem_post(&scoresLock);
}

void send_status(int servNum) {
    ClientInfo *users = usersAll[servNum];
    int *curPlayNum = &curPlayNumAll[servNum];
    for (int j = 0; j < *curPlayNum; j++) {
        if (users[j].qNum != qNum[servNum] && users[j].inPlay) {
            fprintf(users[j].fileOut, "S");
            FILE *fp = users[j].fileOut;
            for (int i = 0; i < *curPlayNum; i++) {
                if (users[i].qNum != qNum[servNum]) {
                    fprintf(fp, "%s:%d", users[i].name, users[i].correct);
                    if (i != *curPlayNum - 1) {
                        fprintf(fp, " ");
                    }
                }
            }
            fprintf(users[j].fileOut, "\n");
            fflush(users[j].fileOut);
        }
    }
}

void check_correct(int answer, int servNum) {
    ClientInfo *users = usersAll[servNum];
    int *curPlayNum = &curPlayNumAll[servNum];
    for (int j = 0; j < *curPlayNum; j++) {
        if (users[j].qNum != qNum[servNum] && users[j].inPlay) {
            fprintf(users[j].fileOut, "C");
            FILE *fp = users[j].fileOut;
            for (int i = 0; i < *curPlayNum; i++) {
                if (users[i].qNum != qNum[servNum]) {
                    if (users[i].answer == answer) {
                        fprintf(fp, "%s:Correct", users[i].name);
                        if (j == 0) {
                            users[i].correct++;
                            write_scores(servNum, 1, &i, 1);
                        }
                    } else if (users[i].answer == -1) {
                        fprintf(fp, "%s:TimedOut", users[i].name);
                    } else {
                        fprintf(fp, "%s:Incorrect", users[i].name);
                    }
                    if (i != *curPlayNum - 1) {
                        fprintf(fp, " ");
                    }
                }
            }
            fprintf(users[j].fileOut, "\n");
            fflush(users[j].fileOut);
        }
    }
}

int num_digits(int dig) {
    int numOfDigits = 1;
    while ((dig /= 10) != 0) {
        numOfDigits++;
    }
    return numOfDigits;
}

void bad_file(int *qNum, char *tempStr, char *sentStr) {
    if (tempStr != NULL) {
        free(tempStr);
    }
    if (sentStr != NULL) {
        free(sentStr);
    }
    // If it's the first question, exit with Bad File error
    if (*qNum == 0) {
        fprintf(stderr, "Bad File\n");
        exit(3);
    }
}

QuestionInfo get_questions(FILE *inputFile, int *qNum) {
    char *sentStr = malloc(1024 * sizeof(char));
    char *tempStr = NULL;
    size_t len = 0;
    QuestionInfo qInfo = {NULL, -1};
    int sentSize = 1024, curSize = 1;
    if (inputFile == NULL) {
        fprintf(stderr, "Bad File\n");
        exit(3);
    }
    // Clear the string buffer
    sentStr[0] = '\0';
    // Grab lines from the question file until ---- is encountered
    while(1) {
        if (getline(&tempStr, &len, inputFile) == -1) {
            bad_file(qNum, tempStr, sentStr);
            return qInfo;
        }
        while ((strlen(tempStr) + curSize) > sentSize) {
            sentStr = (char*)realloc(sentStr, sentSize + 1024);
            sentSize += 1024;
        }
        if (!(strcmp(tempStr, "----\n"))) {
            strcat(sentStr, ".\n");
            break;
        }
        strcat(sentStr, tempStr);
    }
    // Grab the line containing number of answers and the correct answer
    if (getline(&tempStr, &len, inputFile) == -1) {
        bad_file(qNum, tempStr, sentStr);
        return qInfo;
    }
    qInfo = get_answers(qInfo, sentStr, tempStr, curSize, sentSize, 
            len, inputFile, qNum);
    free(tempStr);
    return qInfo;
}

void *start_server(void *sendArg) {
    SendInfo *sInfo = (SendInfo*)sendArg;
    ClientInfo *users = usersAll[sInfo->sNum];
    int fd, fdServer = sInfo->fdServer;
    int *curPlayNum = &curPlayNumAll[sInfo->sNum];
    pthread_t qTid, tempTid;
    struct sockaddr_in fromAddr;
    socklen_t fromAddrSize;
    qNum[sInfo->sNum] = 0;
    // Initalize the values of clients
    for (int i = 0; i < maxPlay; i++) {
        users[i].fd = -1;
        users[i].curString = NULL;
        users[i].name = NULL;
        users[i].answer = -1;
        users[i].inPlay = 0;
        if (sem_init(&users[i].canRead, 0, 0)) {
            fprintf(stderr, "System Error\n");
            exit(8);
        }
    }
    pthread_create(&qTid, NULL, send_info, sInfo);
    while(1) {
        fromAddrSize = sizeof(struct sockaddr_in);
        // Wait until a client connects
        fd = accept(fdServer, (struct sockaddr*)&fromAddr, &fromAddrSize);
        if (fd < 0) {
            fprintf(stderr, "Bad Client\n");
            exit(7);
        }
        // Create struct needed to be used to communicate with the client
        ListenInfo *lInfo = malloc(sizeof(ListenInfo));
        lInfo->fd = fd;
        lInfo->servNum = sInfo->sNum;
        users[*curPlayNum].fd = fd;
        if (pthread_create(&tempTid, NULL, listen_client, 
                lInfo) != 0) {
            fprintf(stderr, "System Error\n");
            exit(8);
        }
    }
}

void setup_client(int *curPlayNum, int fd, ClientInfo *user, int sNum, 
        char *name, FILE *fileIn, FILE *fileOut) {
    name[strlen(name) - 1] = '\0';
    user->name = name;
    user->qNum = qNum[sNum];
    user->correct = 0;
    user->fd = fd;
    user->tid = pthread_self();
    user->curString = NULL;
    user->fileOut = fileOut;
    user->fileIn = fileIn;
    user->inPlay = 1;
    int playNum = *curPlayNum - 1;
    write_scores(sNum, 3, &playNum, 1); 
    user->answer = -1;
    sem_post(&canAdd[sNum]);
    sem_wait(&user->canRead);
}

void preuser_process(FILE *fileIn, char *name, FILE *fileOut, 
        int *curPlayNum) {
    if (!strcmp(name, "scores\n")) {
        get_scores(fileOut);
        fclose(fileIn);
        free(name);
        pthread_exit(NULL);
    }
    if (*curPlayNum + 1 > maxPlay) {
        fprintf(fileOut, "$\n");
        fflush(fileOut);
        fclose(fileIn);
        fclose(fileOut);
        free(name);
        pthread_exit(NULL);
    }
}

void *listen_client(void *lInfo) {
    ListenInfo *lInf = (ListenInfo*)lInfo;
    int sNum = lInf->servNum;
    int fd = lInf->fd, *curPlayNum = &curPlayNumAll[lInf->servNum];
    ClientInfo *users = usersAll[lInf->servNum];
    char *name = NULL;
    size_t nameLen = 0, len = 0;
    FILE *fileIn = fdopen(fd, "r");
    FILE *fileOut = fdopen(fd, "w");
    free(lInf);
    if (fileIn == NULL || fileOut == NULL) {
        fprintf(stderr, "System Error\n");
        exit(8);
    }
    if (getline(&name, &nameLen, fileIn) < 0) {
        if (name != NULL) {
            free(name);
        }
        pthread_exit(NULL);
    }
    preuser_process(fileIn, name, fileOut, curPlayNum);
    fprintf(fileOut, "Hello Player %d/%d.\n", *curPlayNum + 1, minPlay);
    fflush(fileOut);
    sem_wait(&canAdd[sNum]);
    (*curPlayNum)++;
    ClientInfo *user = &users[*curPlayNum - 1];
    setup_client(curPlayNum, fd, user, sNum, name, fileIn, fileOut);
    while (user->inPlay == 1 && getline(&user->curString, 
            &len, fileIn) >= 0) {
        // Check if the user's input is valid
        if (check_int(user->curString)) {
            break;
        }
        user->answer = atoi(user->curString);
        if (user->answer == 0 || user->answer > 999) {
            user->answer = 0;
        }
        user->answer = atoi(user->curString);
        sem_wait(&user->canRead);
        user->answer = -1;
    }
    user->inPlay = 0;
    return NULL;
}

int open_listen(int port) {
    int fd;
    int optVal = 1;
    struct sockaddr_in serverAddr;
    // Setup a TCP IPV4 socket
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "System Error\n");
        exit(8);
    }
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(int)) < 0) {
        fprintf(stderr, "System Error\n");
        exit(8);
    }
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    // Bind the socket to one of the local network interfaces
    if (bind(fd, (struct sockaddr*)&serverAddr, 
            sizeof(struct sockaddr_in)) < 0) {
        fprintf(stderr, "System Error\n");
        exit(8);
    }
    // Listen to the socket which accepts SOMAXCONN of connections
    if (listen(fd, SOMAXCONN) < 0) {
        fprintf(stderr, "Bad Listen\n");
        exit(6);
    }
    return fd;
}

void check_args(int argc, char *argv[]) {
    if (argc <= 5 || argc % 2 != 0) {
        fprintf(stderr, "Usage: serv round_time minplayers maxplayers "
                "port qfile [port qfile ...]\n");
        exit(1);
    }
    for (int i = 1; i < 4; i++) {
        if (strlen(argv[i]) < 1 || check_int(argv[i]) || 
                atoi(argv[i]) < 0 || atoi(argv[1]) < 1) {
            fprintf(stderr, "Bad Number\n");
            exit(2);
        }
    }
    roundTime = atoi(argv[1]);
    minPlay = atoi(argv[2]);
    maxPlay = atoi(argv[3]);
}

void end_server(int s) {
    serverAlive = 0;
}

void game_over(int s) {
    pthread_exit(NULL);
}

void handle_signals(void) {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGPIPE, &sa, NULL);
    sa.sa_handler = end_server;
    sigaction(SIGHUP, &sa, NULL);
    sa.sa_handler = game_over;
    sigaction(SIGTERM, &sa, NULL);
}

int main(int argc, char *argv[]) {
    check_args(argc, argv);
    handle_signals();
    int servNum = argc/2 - 2;
    int curPlayNumStore[servNum];
    // Set up buffers that are shared across threads
    sem_t canAddData[servNum];
    canAdd = canAddData;
    curPlayNumAll = curPlayNumStore;
    usersAll = malloc(sizeof(ClientInfo*)*servNum);
    for (int i = 0; i < servNum; i++) {
        sem_init(&canAdd[i], 0, 1); 
        usersAll[i] = malloc(sizeof(ClientInfo)*maxPlay);
        curPlayNumAll[i] = 0;
    }
    SendInfo sInfo[servNum];
    pthread_t tid[servNum];
    int qNumdata[servNum];
    qNum = qNumdata;
    lPid = tid;
    sem_init(&scoresLock, 0, 1);
    for (int i = 0; i < servNum; i++) {
        int portnum;
        if (strlen(argv[4]) < 0 || check_int(argv[4]) || atoi(argv[4]) < 1 || 
                atoi(argv[4]) > 65535) {
            fprintf(stderr, "Invalid port\n");
            exit(4);
        }
        portnum = atoi(argv[4 + 2*i]);
        sInfo[i].fileName = argv[5 + 2*i];
        sInfo[i].sNum = i;
        sInfo[i].fdServer = open_listen(portnum);
        pthread_create(&tid[i], NULL, start_server, &sInfo[i]);
    }
    for (int i = 0; i < servNum; i++) {
        pthread_join(tid[i], NULL);
        free(usersAll[i]);
    }
    free(usersAll);
    for (int i = 0; i < scoresSize; i++) {
        free(scores[i].name);
    }
    free(scores);
    return 0;
}

int check_int(char *str) {
    int i;
    /* Loop through each character to see if it's an integer */
    for (i = 0; i < strlen(str); i++) {
        /* If character is not an integer return 1 */
        if (!(str[i] >= '0' && str[i] <= '9')) {
            if (str[strlen(str) - 1] == '\n' && i == strlen(str) - 1) {
                return 0;
            }
            return 1;
        }
    }
    /* Return 0 indicating that all characters were integers */
    return 0;
}

