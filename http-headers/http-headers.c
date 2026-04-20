#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int split(char *str, char **result)
{
    char *start = str;
    size_t i = 0;
    while (start && *start)
    {
        char *end = strchr(start, '\n');

        if (!end)
        {
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

int request_is_complete(char *buf)
{
    return strstr(buf, "\r\n\r\n") != NULL;
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

    int opt = 1;
    setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(tcp_socket, (struct sockaddr *)&s_peer, sizeof(s_peer)) == -1)
    {
        perror("bind failed");
        return 1;
    }

    if (listen(tcp_socket, 1000) != 0)
    {
        perror("listen failed");
        return 1;
    }

    printf("Socket is being listened\n");

    struct sockaddr_in peer = {0};
    socklen_t peer_len = sizeof(peer);
    int conn_fd;

    while (1)
    {
        if ((conn_fd = accept(tcp_socket, (struct sockaddr *)&peer, &peer_len)) == -1)
        {
            perror("accept failed");
            continue;
        }

        printf("connection fd is %d\n", conn_fd);

        char buf[8192] = {0};
        size_t used = 0;

        printf("Sizeof buf is: %ld\n", sizeof(buf));

        while (1)
        {
            int recv_result = recv(conn_fd, buf + used, sizeof(buf) - used, 0);

            if (recv_result == -1)
            {
                perror("recv failed");
                continue;
            }

            if (request_is_complete(buf))
            {
                // run through all the headers in request up until \r\n\r\n sequence
                // split each header string into (key, value) tuple by :
                // add "key": "value" string to the common json
                // take the length of the final string
                // create initial headers response and attach json to it;

                char result[4096] = "{";
                size_t result_cursor = 1;

                char *start = buf;
                size_t i = 0;
                while (start && *start)
                {
                    char *end = strstr(start, "\r\n");

                    if (!end)
                    {
                        result[result_cursor] = '}';
                        break;
                    }

                    if (i == 0)
                    {
                        start = end + 2;
                        i++;
                        continue;
                    }
                    else
                    {
                        char *header_start = start;
                        char *header_end = end;
                        char *header_key_end = strchr(start, ':');

                        if (!header_key_end)
                        {
                            printf("For header %s there's no header key\n", start);
                            start = end + 2;
                            continue;
                        }

                        result[result_cursor++] = '\"';

                        int i;
                        for (i = 0; i < (header_key_end - header_start); i++)
                        {
                            result[result_cursor++] = header_start[i];
                        }

                        result[result_cursor++] = '\"';
                        result[result_cursor++] = ':';

                        result[result_cursor++] = '\"';

                        for (i = 0; i < (header_end - (header_key_end + 2)); i++)
                        {
                            result[result_cursor++] = header_key_end[i + 2];
                        }

                        result[result_cursor++] = '\"';
                        result[result_cursor++] = ',';
                    }

                    start = end + 2;
                    i++;
                }
                result[--result_cursor] = '}';

                char sendbuf[4096];
                snprintf(sendbuf, 106 + result_cursor + 1, "HTTP/1.1 200 OK\r\nContent-Length:%ld\r\nContent-Type: application/json\r\nCache-Control: no-store\r\n\r\n%s", result_cursor + 1, result);

                printf("Result is %s\n", sendbuf);

                if (send(conn_fd, sendbuf, 106 + result_cursor + 1, 0) == -1)
                {
                    perror("send failed");
                    close(conn_fd);
                    break;
                };
            }

            if (recv_result == 0)
            {
                if (close(conn_fd) == -1)
                {
                    perror("close failed");
                };
                break;
            }

            used += recv_result;
        }
    }
}
