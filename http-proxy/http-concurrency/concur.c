#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

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

struct upstream_connection
{
    char *host;
    in_port_t port;
    int _fd;
    int8_t _response;
};

int upstream_connection_create(struct upstream_connection *uc)
{
    int scon = socket(AF_INET, SOCK_STREAM, 0);

    struct in_addr in_addr;

    if (inet_pton(AF_INET, uc->host, &in_addr) < 1)
    {
        perror("inet_pton failed");
        printf("Error: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_in addr =
        {
            AF_INET,
            htons(uc->port),
            in_addr};

    if (connect(scon, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        perror("connect failed");
        return -1;
    };

    uc->_fd = scon;

    return 0;
}

int upstream_connection_destroy(struct upstream_connection *uc)
{
    close(uc->_fd);
    return 0;
}

int upstream_connection_send(struct upstream_connection *uc, uint8_t *request_buffer, ssize_t request_buffer_size)
{
    while (1)
    {
        printf("Sending request to upstream socket=%d\n", uc->_fd);

        ssize_t upstream_send_result = send(uc->_fd, request_buffer, request_buffer_size, 0);

        if (upstream_send_result == -1)
        {
            perror("Send to upstream failed");
            upstream_connection_destroy(uc);
            upstream_connection_create(uc);
            printf("Connection was recreated, new fd=%d\n", uc->_fd);
            continue;
        }

        printf("     * ->   %ld B\n", upstream_send_result);
        return upstream_send_result;
    }
}

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

            if (strncmp("Transfer-Encoding", header_name, header_name_i - 1) == 0)
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
        return -1;
    }

    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct in_addr in_addr;

    if (inet_pton(AF_INET, host, &in_addr) < 1)
    {
        perror("inet_pton failed");
        return -1;
    }

    struct sockaddr_in addr =
        {
            AF_INET,
            htons(port),
            in_addr};

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind failed");
        return -1;
    }

    if (listen(sfd, 1024) != 0)
    {
        perror("listen failed");
        return -1;
    }

    printf("Server is listening on port %d\n", port);

    return sfd;
}

int tcp_on_connect(int sfd, void (*f)(int, struct upstream_connection *), struct upstream_connection *uc)
{
    struct sockaddr_in client_addrs[10] = {0};
    size_t len = 0;

    struct pollfd pfds[10] = {0};

    pfds[0].fd = sfd;
    pfds[0].events = POLLIN;

    size_t npfds = 1;

    while (1)
    {
        int ready;

        ready = poll(pfds, npfds, -1);

        printf("Ready status is %d\n", ready);

        if (ready == -1)
        {
            perror("poll failed");
            exit(EXIT_FAILURE);
        }

        int i;
        for (i = 0; i < npfds; i++)
        {
            if (pfds[i].revents == 0)
            {
                continue;
            }

            printf("fd=%d; events value: %d, events: %s%s%s%s\n", pfds[i].fd, pfds[i].revents, (pfds[i].revents & POLLIN) ? "POLLIN " : "", (pfds[i].revents & POLLHUP) ? "POLLHUP " : "", (pfds[i].revents & POLLERR) ? "POLLERR" : "", (pfds[i].revents & POLLNVAL) ? "POLLNVAL" : "");

            if (i == 0 && pfds[i].revents & POLLIN)
            {
                int client_socket;
                socklen_t client_addrlen = sizeof(client_addrs[0]);
                if ((client_socket = accept(sfd, (struct sockaddr *)&client_addrs[len], &client_addrlen)) == -1)
                {
                    perror("accept failed \n");
                    continue;
                }

                printf("New connection from ('%s', '%d')\n", inet_ntoa(client_addrs[len].sin_addr), ntohs(client_addrs[len].sin_port));
                pfds[npfds].fd = client_socket;
                pfds[npfds].events = POLLIN;
                npfds++;
                len++;
                continue;
            }

            if (pfds[i].revents & POLLIN)
            {
                f(pfds[i].fd, uc);
            }

            if (pfds[i].revents & POLLNVAL)
            {
                printf("closing fd=%d\n", pfds[i].fd);
                close(pfds[i].fd);
                pfds[i].fd = -1;
                npfds--;
            }
        }
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

int on_request(int socket, void (*f)(int socket, uint8_t *buf, ssize_t recv_result, struct upstream_connection *), struct upstream_connection *uc)
{
    uint8_t recv_buffer[4096];

    // while (1)
    // {
    ssize_t recv_result = recv(socket, recv_buffer, sizeof(recv_buffer), 0);

    if (recv_result == -1)
    {
        perror("recv failed");
        return -1;
    }

    if (recv_result == 0)
    {
        fprintf(stderr, "connection has been closed\n");
        return 0;
    }

    f(socket, recv_buffer, recv_result, uc);
    // }
}

int send_request(struct upstream_connection *uc, uint8_t *request_buffer, ssize_t request_buffer_size, void (*f)(uint8_t *response_buffer, ssize_t response_size, int client_socket), int client_socket)
{
    ssize_t upstream_send_result = upstream_connection_send(uc, request_buffer, request_buffer_size);

    if (upstream_send_result == -1)
    {
        perror("send to upstream failed");
        return -1;
    }

    printf("     * ->   %ld B\n", upstream_send_result);

    uint8_t upstream_buf[4096];
    ssize_t initial_upstream_recv_result = recv(uc->_fd, upstream_buf, sizeof(upstream_buf), 0);

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
            upstream_recv_result = recv(uc->_fd, upstream_buf, sizeof(upstream_buf), 0);
        }

        initial_upstream_recv_result = 0;
        used += upstream_recv_result;

        f(upstream_buf, upstream_recv_result, client_socket);

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

void upstream_response_handler(uint8_t *upstream_response_buffer, ssize_t upstream_response_size, int client_socket)
{
    printf("     * <-   %ld B\n", upstream_response_size);

    ssize_t client_send_result = send(client_socket, upstream_response_buffer, upstream_response_size, 0);
    printf("<-   *      %ld B\n", client_send_result);
}

void client_request_handler(int client_socket, uint8_t *client_buffer, ssize_t size, struct upstream_connection *uc)
{
    printf("->   *      %ld B\n", size);

    send_request(uc, client_buffer, size, &upstream_response_handler, client_socket);
}

void on_connect(int client_socket, struct upstream_connection *uc)
{
    if (on_request(client_socket, &client_request_handler, uc) == 0)
    {
        close(client_socket);
        upstream_connection_destroy(uc);
    };
}

int main(int argc, char *argv[])
{
    int sfd;
    if ((sfd = tcp_listen("0.0.0.0", 8082)) == -1)
    {
        fprintf(stderr, "failed to listen on 0.0.0.0:8082\n");
        return 1;
    }

    struct upstream_connection u_connection = {0};

    u_connection.host = "127.0.0.1";
    u_connection.port = 8081;

    upstream_connection_create(&u_connection);

    if (upstream_connection_create(&u_connection) < 0)
    {
        perror("Failed to create upstream server connection");
        return 1;
    }

    tcp_on_connect(sfd, &on_connect, &u_connection);
}
