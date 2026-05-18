#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <string.h>

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

    return sfd;
}

int main(int argc, char *argv[])
{
    int sfd = tcp_listen("0.0.0.0", 8082);

    while (1)
    {
        struct sockaddr_in addrc = {0};
        socklen_t addrclen = 0;
        int cfd;
        if ((cfd = accept(sfd, (struct sockaddr *)&addrc, &addrclen)) == -1)
        {
            continue;
        }

        int scon = socket(AF_INET, SOCK_STREAM, 0);

        struct in_addr in_addr;

        if (inet_pton(AF_INET, "127.0.0.1", &in_addr) < 1)
        {
            perror("inet_pton failed");
            return 1;
        }

        struct sockaddr_in addr =
            {
                AF_INET,
                htons(8081),
                in_addr};

        if (connect(scon, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            perror("connect failed");
            return 1;
        };

        uint8_t buf[1024] = {0};
        ssize_t used = 0;
        while (1)
        {
            ssize_t recvr = recv(cfd, buf + used, 1024 - used, 0);

            if (recvr == -1)
            {
                continue;
            }

            if (recvr == 0 || request_is_complete(buf))
            {
                uint8_t proxy_buf[4096] = {0};
                size_t proxy_used = 0;

                send(scon, buf + used, recvr, 0);

                while (1)
                {
                    int proxy_recv_result = recv(scon, proxy_buf + proxy_used, sizeof(proxy_buf) - proxy_used, 0);
                    if (proxy_recv_result == 0) {
                        break;
                    }
                    send(cfd, proxy_buf + proxy_used, proxy_recv_result, 0);
                    proxy_used += proxy_recv_result;
                }
                break;
            }

            printf("%s\n", (char *)buf);

            send(scon, buf + used, recvr, 0);

            used += recvr;

        }

        printf("Data has been received\n");
    }
}
