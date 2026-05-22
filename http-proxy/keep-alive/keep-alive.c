#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

int request_is_complete(char *buf)
{
    return strstr(buf, "\r\n\r\n") != NULL;
}

int response_is_complete(char *response, size_t response_size) {
    return response[response_size - 1] == '\n' && response[response_size - 2] == '\r' && response[response_size - 3] == '\n' && response[response_size - 4] == '\r' && response[response_size - 5] == '0';
}

int tcp_listen(const char *host, in_port_t port)
{
    int sfd;

    if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket failed");
        return 1;
    }

    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct in_addr in_addr;

    if (inet_pton(AF_INET, host, &in_addr) < 1)
    {
        perror("inet_pton failed");
        return 1;
    }

    struct sockaddr_in addr =
        {
            AF_INET,
            htons(port),
            in_addr};

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind failed");
        return 1;
    }

    if (listen(sfd, 1024) != 0)
    {
        perror("listen failed");
        return 1;
    }

    printf("Server is listening on port %d\n", port);

    return sfd;
}

int tcp_on_connect(int sfd, void (*f)(int))
{
    while (1)
    {
        struct sockaddr_in client_addr = {0};
        socklen_t client_addrlen = sizeof(client_addr);
        int client_socket;
        if ((client_socket = accept(sfd, (struct sockaddr *)&client_addr, &client_addrlen)) == -1)
        {
            continue;
        }

        printf("New connection from ('%s', '%d')\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        f(client_socket);
        // close(client_socket);
    }
}

int tcp_connect(const char *host, in_port_t port)
{
    int scon = socket(AF_INET, SOCK_STREAM, 0);

    struct in_addr in_addr;

    if (inet_pton(AF_INET, host, &in_addr) < 1)
    {
        perror("inet_pton failed");
        printf("Error: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_in addr =
        {
            AF_INET,
            htons(port),
            in_addr};

    if (connect(scon, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        perror("connect failed");
        return -1;
    };

    return scon;
}

void on_connect(int client_socket)
{
    int upstream_socket;
    if ((upstream_socket = tcp_connect("127.0.0.1", 8081)) < 0)
    {
        fprintf(stderr, "connect to the upstream server failed\n");
        return;
    }

    uint8_t buf[8192] = {0};
    size_t used = 0;
    while (1)
    {
        ssize_t recvr = recv(client_socket, buf + used, sizeof(buf) - used, 0);

        if (recvr == -1)
        {
            perror("recv failed");
            printf("Error: %s\n", strerror(errno));
            continue;
        }

        printf("->   *      %ld B\n", recvr);

        if (recvr > 0)
        {
            send(upstream_socket, buf + used, recvr, 0);
        }

        if (recvr == 0 || request_is_complete(buf))
        {
            uint8_t upstream_buf[4096] = {0};

            while (1)
            {
                ssize_t upstream_recv_result = recv(upstream_socket, upstream_buf, sizeof(upstream_buf), 0);
                printf("     * <-   %ld B\n", upstream_recv_result);

                for (size_t i = 0; i < upstream_recv_result; i++) {
                    printf("/%02x", upstream_buf[i]);
                }
                printf("\n");

                ssize_t sendr = send(client_socket, upstream_buf, upstream_recv_result, 0);
                printf("<-   *      %ld B\n", sendr);



                if (response_is_complete(upstream_buf, upstream_recv_result) || sendr == 0)
                {
                    printf("response is complete \n");
                    break;
                }

                // break;
            }

            used = 0;

            if (recvr == 0) {
                close(client_socket);
                return;
            }

            continue;
        }

        used += recvr;
    }
}

int main(int argc, char *argv[])
{
    int sfd = tcp_listen("0.0.0.0", 8082);

    tcp_on_connect(sfd, &on_connect);
}
