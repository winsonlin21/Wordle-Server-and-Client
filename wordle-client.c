#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define MAXLINE 64
#define PACKET_SIZE 8

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "USAGE: %s <server-ip> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *server_ip = argv[1];
    int port = atoi(argv[2]);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket() failed");
        return EXIT_FAILURE;
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &servaddr.sin_addr) <= 0) {
        perror("inet_pton() failed");
        close(sockfd);
        return EXIT_FAILURE;
    }

    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect() failed");
        close(sockfd);
        return EXIT_FAILURE;
    }

    char sendline[MAXLINE];
    printf("CLIENT: connecting to server...\n");

    int guess_count = 0;
    while (1) {
        printf("CLIENT: sending to server: ");
        if (!fgets(sendline, MAXLINE, stdin)) break;
        sendline[strcspn(sendline, "\r\n")] = '\0';

        if (strlen(sendline) == 0) continue;

        send(sockfd, sendline, strlen(sendline), 0);

        // Receive exactly 8 bytes
        char packet[PACKET_SIZE];
        ssize_t n = recv(sockfd, packet, PACKET_SIZE, MSG_WAITALL);
        if (n <= 0) {
            printf("CLIENT: server closed connection or error occurred.\n");
            break;
        }
        if (n != PACKET_SIZE) {
            printf("CLIENT: incomplete packet received.\n");
            break;
        }

        char valid = packet[0];
        short guesses_left;
        memcpy(&guesses_left, packet + 1, 2);
        guesses_left = ntohs(guesses_left);

        char result[6] = {0};
        memcpy(result, packet + 3, 5);
        result[5] = '\0';

        if (valid == 'N') {
            printf("CLIENT: invalid guess -- %d guesses remaining\n", guesses_left);
            continue;
        }

        guess_count++;
        printf("CLIENT: response: %s -- %d guesses remaining\n", result, guesses_left);

        // Check for win (all uppercase and length 5)
        int is_win = 1;
        for (int i = 0; i < 5; i++) {
            if (!(result[i] >= 'A' && result[i] <= 'Z')) {
                is_win = 0;
                break;
            }
        }
        if (is_win) {
            printf("CLIENT: you won!\n");
            break;
        }
        if (guesses_left == 0) {
            printf("CLIENT: you lost!\n");
            break;
        }
    }

    close(sockfd);
    printf("CLIENT: disconnecting...\n");
    return 0;
}