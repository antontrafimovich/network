#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

int request_is_complete(char *buf)
{
    return strstr(buf, "\r\n\r\n") != NULL;
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
        struct sockaddr_in addrc = {0};
        socklen_t addrclen = 0;
        int client_socket;
        if ((client_socket = accept(sfd, (struct sockaddr *)&addrc, &addrclen)) == -1)
        {
            continue;
        }

        f(client_socket);
        close(client_socket);
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
        return 1;
    }

    struct sockaddr_in addr =
        {
            AF_INET,
            htons(port),
            in_addr};

    if (connect(scon, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        perror("connect failed");
        return 1;
    };

    return scon;
}

void on_connect(int client_socket)
{
    int upstream_socket = tcp_connect("127.0.0.1", 8081);
    struct timespec u_recv_start, u_recv_end, dest_send_start, dest_send_end, dest_recv_start, dest_recv_end, u_send_start, u_send_end;

    uint8_t buf[1024] = {0};
    size_t used = 0;
    while (1)
    {
        clock_gettime(CLOCK_MONOTONIC, &u_recv_start);
        ssize_t recvr = recv(client_socket, buf, sizeof(buf) - used, 0);
        clock_gettime(CLOCK_MONOTONIC, &u_recv_end);

        double elapsed = (u_recv_end.tv_sec - u_recv_start.tv_sec) +
                         (u_recv_end.tv_nsec - u_recv_start.tv_nsec) / 1e9;
        printf("Recv from user in %.6f seconds\n", elapsed);

        if (recvr == -1)
        {
            perror("recv failed");
            printf("Error: %s\n", strerror(errno));
            continue;
        }

        if (recvr > 0)
        {
            clock_gettime(CLOCK_MONOTONIC, &dest_send_start);
            send(upstream_socket, buf + used, recvr, 0);
            clock_gettime(CLOCK_MONOTONIC, &dest_send_end);

            double elapsed = (dest_send_end.tv_sec - dest_send_start.tv_sec) +
                             (dest_send_end.tv_nsec - dest_send_start.tv_nsec) / 1e9;
            printf("Send to destination in %.6f seconds\n", elapsed);
        }

        if (recvr == 0 || request_is_complete(buf))
        {
            uint8_t proxy_buf[4096] = {0};
            size_t proxy_used = 0;

            while (1)
            {
                clock_gettime(CLOCK_MONOTONIC, &dest_recv_start);
                int proxy_recv_result = recv(upstream_socket, proxy_buf + proxy_used, sizeof(proxy_buf) - proxy_used, 0);
                clock_gettime(CLOCK_MONOTONIC, &dest_recv_end);

                printf("%s\n", proxy_buf);

                double dest_recv_elapsed = (dest_recv_end.tv_sec - dest_recv_start.tv_sec) +
                                           (dest_recv_end.tv_nsec - dest_recv_start.tv_nsec) / 1e9;
                printf("Recv from destination in %.6f seconds\n", dest_recv_elapsed);

                if (proxy_recv_result == 0)
                {
                    break;
                }

                clock_gettime(CLOCK_MONOTONIC, &u_send_start);
                ssize_t sendr = send(client_socket, proxy_buf + proxy_used, proxy_recv_result, 0);
                clock_gettime(CLOCK_MONOTONIC, &u_send_end);

                double elapsed = (u_send_end.tv_sec - u_send_start.tv_sec) +
                                 (u_send_end.tv_nsec - u_send_start.tv_nsec) / 1e9;
                printf("Recv from destination in %.6f seconds\n", elapsed);

                proxy_used += proxy_recv_result;
                break;
            }

            break;
        }

        used += recvr;
    }
}

int main(int argc, char *argv[])
{
    int sfd = tcp_listen("0.0.0.0", 8082);

    tcp_on_connect(sfd, &on_connect);
}
