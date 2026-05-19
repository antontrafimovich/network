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
        int cfd;
        if ((cfd = accept(sfd, (struct sockaddr *)&addrc, &addrclen)) == -1)
        {
            continue;
        }

        f(cfd);
        close(cfd);
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

void on_connect(int cfd)
{
    int destfd = tcp_connect("127.0.0.1", 8081);

    uint8_t buf[1024] = {0};
    size_t used = 0;
    while (1)
    {
        ssize_t recvr = recv(cfd, buf, sizeof(buf) - used, 0);

        if (recvr == -1)
        {
            perror("recv failed");
            printf("Error: %s\n", strerror(errno));
            continue;
        }

        if (recvr > 0)
        {
            send(destfd, buf + used, recvr, 0);
        }

        if (recvr == 0 || request_is_complete(buf))
        {
            uint8_t proxy_buf[4096] = {0};
            size_t proxy_used = 0;

            while (1)
            {
                int proxy_recv_result = recv(destfd, proxy_buf + proxy_used, sizeof(proxy_buf) - proxy_used, 0);
                if (proxy_recv_result == 0)
                {
                    break;
                }

                ssize_t sendr = send(cfd, proxy_buf + proxy_used, proxy_recv_result, 0);
                proxy_used += proxy_recv_result;
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
