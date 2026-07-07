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
#include "queue.h"
#include "event_loop.h"

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
    int8_t _response[8192];
};

struct pending_request
{
    int is_request_finished;
    int is_response_finished;
    int8_t _request[4096];
    int8_t _response[8192];
};

struct event_loop event_loop = {0};

Queue pending_requests_queue = {0};

int chunked_response_is_complete(char *response, size_t response_size)
{
    return response[response_size - 1] == '\n' && response[response_size - 2] == '\r' && response[response_size - 3] == '\n' && response[response_size - 4] == '\r' && response[response_size - 5] == '0';
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

int on_upstream_data(int upstream_fd, void *payload)
{
}

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

    struct event_loop_action *el_action = calloc(1, sizeof(struct event_loop_action));
    el_action->action = &on_upstream_data;
    el_action->payload = NULL;

    event_loop_add(&event_loop, scon, POLLIN, el_action);

    return 0;
}

int upstream_connection_destroy(struct upstream_connection *uc)
{
    close(uc->_fd);
    return 0;
}

int upstream_connection_send(struct upstream_connection *uc, uint8_t *request_buffer, ssize_t request_buffer_size)
{
    ssize_t sent = 0;

    while (sent < request_buffer_size)
    {
        printf("Sending request to upstream socket=%d\n", uc->_fd);

        ssize_t upstream_send_result = send(uc->_fd, request_buffer, request_buffer_size, 0);

        if (upstream_send_result == -1)
        {
            perror("Send to upstream failed");
            upstream_connection_destroy(uc);
            upstream_connection_create(uc);
            printf("Connection was recreated, new fd=%d\n", uc->_fd);
            sent = 0;
            continue;
        }

        sent += upstream_send_result;

        printf("     * ->   %ld B\n", upstream_send_result);
        return upstream_send_result;
    }
}

struct upstream_connection_pipe_payload
{
    void (*cb)(uint8_t *response_buffer, ssize_t response_size, int client_socket);
    struct upstream_connection *uc;
    int client_socket;
};

int upstream_connection_pipe(int upstream_socket, void *payload)
{
    struct upstream_connection_pipe_payload *ucp_payload = payload;
    int client_socket = ucp_payload->client_socket;
    void (*cb)(uint8_t *response_buffer, ssize_t response_size, int client_socket) = ucp_payload->cb;
    struct upstream_connection *uc = ucp_payload->uc;

    struct response_info response_info = {0};
    ssize_t used = 0, headers_used = 0;

    while (1)
    {
        ssize_t upstream_recv_result = recv(uc->_fd, uc->_response + headers_used, sizeof(uc->_response) - headers_used, 0);

        if (response_info.response_type == UNKNOWN || !response_info.response_type)
        {
            process_response(uc->_response, upstream_recv_result, &response_info);
        }

        used += upstream_recv_result;

        cb(uc->_response + headers_used, upstream_recv_result, client_socket);

        if (response_info.response_type == UNKNOWN)
        {

            headers_used += upstream_recv_result;
        }
        else
        {
            headers_used = 0;
        }

        if (response_info.response_type == CHUNKED && chunked_response_is_complete(uc->_response, upstream_recv_result))
        {
            printf("chunked response is complete\n");
            return 0;
        }

        if (response_info.response_type == CONTENT_LENGTH && used >= response_info.header_size + response_info.response_size)
        {
            printf("content length response is complete\n");
            return 0;
        }
    }
}

int request_is_complete(char *buf)
{
    return strstr(buf, "\r\n\r\n") != NULL;
}

int content_length_response_is_complete(char *response, size_t response_size)
{
    return CHUNKED;
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

int send_request(struct upstream_connection *uc, uint8_t *request_buffer, ssize_t request_buffer_size, void (*f)(uint8_t *response_buffer, ssize_t response_size, int client_socket), int client_socket)
{
    ssize_t upstream_send_result = upstream_connection_send(uc, request_buffer, request_buffer_size);

    if (upstream_send_result == -1)
    {
        perror("send to upstream failed");
        return -1;
    }

    struct upstream_connection_pipe_payload *ucp_payload = calloc(1, sizeof(struct upstream_connection_pipe_payload));
    ucp_payload->cb = f;
    ucp_payload->client_socket = client_socket;
    ucp_payload->uc = uc;

    struct event_loop_action *el_action = calloc(1, sizeof(struct event_loop_action));
    el_action->action = &upstream_connection_pipe;
    el_action->payload = (void *)ucp_payload;

    event_loop_add(&event_loop, uc->_fd, POLLIN, el_action);
    return 1;
}

void upstream_response_handler(uint8_t *upstream_response_buffer, ssize_t upstream_response_size, int client_socket)
{
    printf("     * <-   %ld B\n", upstream_response_size);

    ssize_t client_send_result = send(client_socket, upstream_response_buffer, upstream_response_size, 0);
    printf("<-   *      %ld B\n", client_send_result);
}

int on_data(int client_socket, void *payload)
{
    struct upstream_connection *uc = payload;
    uint8_t recv_buffer[4096];

    ssize_t recv_result = recv(client_socket, recv_buffer, sizeof(recv_buffer), 0);

    if (recv_result == -1)
    {
        perror("recv failed");
        return -1;
    }

    if (recv_result == 0)
    {
        fprintf(stderr, "connection has been closed\n");
        close(client_socket);
        return 0;
    }

    printf("->   *      %ld B\n", recv_result);

    send_request(uc, recv_buffer, recv_result, &upstream_response_handler, client_socket);

    return 1;
}

int tcp_on_connect(int sfd, void *payload)
{
    struct upstream_connection *uc = payload;
    struct sockaddr_in client_addrs[10] = {0};
    size_t len = 0;

    int client_socket;
    socklen_t client_addrlen = sizeof(client_addrs[0]);
    if ((client_socket = accept(sfd, (struct sockaddr *)&client_addrs[len], &client_addrlen)) == -1)
    {
        perror("accept failed \n");
        return 1;
    }

    struct event_loop_action *el_action = calloc(1, sizeof(struct event_loop_action));
    el_action->action = &on_data;
    el_action->payload = (void *)uc;

    event_loop_add(&event_loop, client_socket, POLLIN, el_action);

    return 0;
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
    return 1;
}

void client_request_handler(int client_socket, uint8_t *client_buffer, ssize_t size, struct upstream_connection *uc)
{
    printf("->   *      %ld B\n", size);

    send_request(uc, client_buffer, size, &upstream_response_handler, client_socket);
}

int main(int argc, char *argv[])
{
    initializeQueue(&pending_requests_queue);

    int sfd;
    if ((sfd = tcp_listen("0.0.0.0", 8082)) == -1)
    {
        fprintf(stderr, "failed to listen on 0.0.0.0:8082\n");
        return 1;
    }

    struct upstream_connection u_connection = {0};

    u_connection.host = "127.0.0.1";
    u_connection.port = 8081;

    if (upstream_connection_create(&u_connection) < 0)
    {
        perror("Failed to create upstream server connection");
        return 1;
    }

    struct event_loop_action action;
    action.action = &tcp_on_connect;
    action.payload = (void *)&u_connection;

    event_loop_add(&event_loop, sfd, POLLIN, &action);
    event_loop_start(&event_loop);
}

// upstream server response is added to el

// client request is received
// some instance is added to queue
// upstream socket data notification
// take first item from queue
// process response from upstream socket
// if full response is served, remove item from queue