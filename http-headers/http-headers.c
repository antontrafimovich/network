#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

int split(char *str, char **result) {
    char *start = str;
    size_t i = 0;
    while(start && *start) {
        char *end = strchr(start, '\n');

        if (!end) {
            result[i] = start;
            break;
        }

        *end = '\0';
        result[i] = start;

        start = end + 1;
        i++;
    }

    return 0;
}

int main(int argc, char **argv)
{
    int tcp_socket;

    tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket == -1)
    {
        perror("socket failed");
        return 1;
    }

    struct in_addr s_peer_addr;
    struct sockaddr_in s_peer;

    s_peer_addr.s_addr = INADDR_ANY;

    s_peer.sin_family = AF_INET;
    s_peer.sin_port = htons(8081);
    s_peer.sin_addr = s_peer_addr;

    if (bind(tcp_socket, (struct sockaddr *)&s_peer, sizeof(s_peer)) == -1)
    {
        perror("bind failed");
        return 1;
    }

    if (listen(tcp_socket, 1000) != 0) {
        perror("listen failed");
        return 1;
    }

    printf("Socket is being listened\n");

    struct sockaddr_in peer = {0};
    socklen_t peer_len = sizeof(peer);
    int conn_fd;

    while(1) {
        if ((conn_fd = accept(tcp_socket, (struct sockaddr *)&peer, &peer_len)) == -1) {
            perror("accept failed");
            continue;
        }
        break;
    }

    printf("connection fd is %d\n", conn_fd);

    char buf[4096] = {0};

    while (1) {
        int recv_result = recv(conn_fd, buf, 4096, 0);

        if (recv_result == -1) {
            perror("recv failed");
            continue;
        }

        if (recv_result == 0) {
            printf("The result is %s\n", buf);
            return 0;
        }

        size_t headers_len = strlen(buf);
        char *result[16] = {0};

        split(buf, result);

        printf("The first header is: %s\n", result[1]);

        char sendbuf[4096] = "HTTP/1.1 200 OK\nContent-Length:17\nContent-Type: application/json\nCache-Control: no-store\n\n{\"name\": \"Anton\"}";

        if (send(conn_fd, sendbuf, 4096, 0) == -1) {
            perror("send failed");
            return -1;
        };


        printf("Intermediate result is %s\n", buf);
    }

}