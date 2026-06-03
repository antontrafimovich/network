#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define CHUNKED_RESPONSE 1;
#define CONTENT_LENGTH_RESPONSE 2;

enum RESPONSE_TYPE
{
    UNKNOWN = -1,
    CHUNKED = 1,
    CONTENT_LENGTH = 2
};

struct response_info
{
    enum RESPONSE_TYPE response_type;
    size_t response_size;
    size_t header_size;
};

int request_is_complete(char *buf)
{
    return strstr(buf, "\r\n\r\n") != NULL;
}

int chunked_response_is_complete(char *response, size_t response_size)
{
    return response[response_size - 1] == '\n' && response[response_size - 2] == '\r' && response[response_size - 3] == '\n' && response[response_size - 4] == '\r' && response[response_size - 5] == '0';
}

int content_length_response_is_complete(char *response, size_t response_size)
{
    return CHUNKED;
}

int process_response(char *response_with_headers, size_t response_size, struct response_info *response_info)
{
    int i;
    char header_name[128], header_value[512];
    size_t header_name_i = 0;
    size_t header_value_i = 0;
    unsigned char header_name_is_done = 0;
    response_info->response_type = UNKNOWN;

    for (i = 0; i < response_size; i++)
    {
        if (response_with_headers[i] == ':')
        {
            header_name[header_name_i] = '\0';
            header_name_is_done = 1;
            if (strncmp("Content-Length", header_name, header_name_i - 1) == 0)
            {
                response_info->response_type = CONTENT_LENGTH;
                continue;
            }

            if (strncmp("transfer-encoding", header_name, header_name_i - 1) == 0)
            {
                response_info->response_type = CHUNKED;
                response_info->response_size = 0;
                continue;
            }
        }

        if (response_with_headers[i] == '\r' && response_with_headers[++i] == '\n')
        {
            // end of header section
            if (header_name_i == 0 && header_value_i == 0)
            {
                response_info->header_size = i + 1;
                return 0;
            }

            if (response_info->response_type == CONTENT_LENGTH && response_info->response_size == 0)
            {
                header_value[header_value_i] = '\0';
                response_info->response_size = atoi(header_value);
            }

            header_value_i = 0;
            header_name_i = 0;
            header_name_is_done = 0;

            continue;
        }

        if (header_name_is_done)
        {
            if (response_info->response_type == UNKNOWN)
            {
                continue;
            }
            header_value[header_value_i++] = response_with_headers[i];
        }
        else
        {
            header_name[header_name_i++] = response_with_headers[i];
        }
    }
}

int response_is_complete(char *response, size_t response_size)
{
    unsigned char is_chunked;

    // logic for is chunked response analyze
    int i;
    char header_name[128], header_value[512];
    size_t header_name_i = 0;
    size_t header_value_i = 0;
    unsigned char header_name_is_done = 0;

    for (i = 0; i < response_size; i++)
    {
        if (response[i] == ':')
        {
            header_name[header_name_i] = '\0';
            continue;
        }

        header_name[header_name_i++] = response[i];
    }

    if (is_chunked)
    {
        return chunked_response_is_complete(response, response_size);
    }

    return content_length_response_is_complete(response, response_size);
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
        printf("waiting for connection \n");
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
        perror("connect to the upstream server failed\n");
        return;
    }

    uint8_t buf[8192];

    while (1)
    {
        ssize_t client_recv_result = recv(client_socket, buf, sizeof(buf), 0);

        if (client_recv_result == -1)
        {
            perror("recv failed");
            printf("Error: %s\n", strerror(errno));
            continue;
        }

        printf("->   *      %ld B\n", client_recv_result);

        if (client_recv_result == 0)
        {
            close(client_socket);
            close(upstream_socket);
            printf("connection is closed\n");
            return;
        }

        if (client_recv_result > 0)
        {
            ssize_t upstream_send_result = send(upstream_socket, buf, client_recv_result, 0);
            if (upstream_send_result == -1)
            {
                perror("send to upstream failed, continueing\n");
                continue;
            }
            printf("     * ->   %ld B\n", upstream_send_result);
        }

        uint8_t upstream_buf[4096];
        ssize_t initial_upstream_recv_result = recv(upstream_socket, upstream_buf, sizeof(upstream_buf), 0);

        struct response_info response_info = {0};
        process_response(upstream_buf, initial_upstream_recv_result, &response_info);
        ssize_t used = 0;
        ssize_t upstream_recv_result;
        while (1)
        {
            if (initial_upstream_recv_result)
            {
                upstream_recv_result = initial_upstream_recv_result;
            }
            else
            {
                upstream_recv_result = recv(upstream_socket, upstream_buf, sizeof(upstream_buf), 0);
            }

            initial_upstream_recv_result = 0;
            printf("     * <-   %ld B\n", upstream_recv_result);

            used += upstream_recv_result;

            if (upstream_recv_result > 0)
            {
                for (size_t i = 0; i < upstream_recv_result; i++)
                {
                    printf("/%02x", upstream_buf[i]);
                }
                printf("\n");
            }

            ssize_t client_send_result = send(client_socket, upstream_buf, upstream_recv_result, 0);
            printf("<-   *      %ld B\n", client_send_result);

            if (response_info.response_type == CHUNKED && chunked_response_is_complete(upstream_buf, upstream_recv_result))
            {
                printf("chunked response is complete\n");
                break;
            }

            if (response_info.response_type == CONTENT_LENGTH && used >= response_info.header_size + response_info.response_size)
            {
                printf("content length response is complete\n");
                break;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    int sfd = tcp_listen("0.0.0.0", 8082);

    tcp_on_connect(sfd, &on_connect);
}
