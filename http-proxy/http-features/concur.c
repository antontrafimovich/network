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
#include "zlib.h"

#define HASHTABLE_IMPLEMENTATION
#include "hashtable.h"

#define CHUNK 16384

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
    int client_socket;
    int is_request_finished;
    int8_t request[4096];
    ssize_t request_used;
    ssize_t request_sent;
    int8_t response_temp[4096];
    int8_t *response;
    ssize_t response_used;
    ssize_t response_sent;
    struct response_info response_info;
};

int on_upstream_data(int upstream_fd, void *payload);

struct event_loop event_loop = {0};

Queue pending_requests_queue = {0};

hashtable_t *client_socket_buffers = NULL;

char *gzip_response(char *response, struct response_info *response_info)
{
    size_t i = 0;

    const char *gzip_header = "Content-Encoding:deflate\r\n";

    char cl_header[26];

    int ret;
    z_stream strm;

    unsigned char *in = response + response_info->header_size;
    unsigned char *out = calloc(response_info->response_size, sizeof(unsigned char));

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    ret = deflateInit(&strm, 1);

    if (ret != Z_OK)
        return NULL;

    strm.avail_in = response_info->response_size;
    strm.next_in = in;
    strm.avail_out = response_info->response_size;
    strm.next_out = out;

    ret = deflate(&strm, Z_FINISH);

    int cl_header_content_size = snprintf(cl_header, 26, "Content-Length:%ld\r\n\r\n", strm.total_out);

    printf("The return size is %ld\n", strm.total_out);

    char *gzipped_response = calloc(response_info->header_size + cl_header_content_size + strlen(gzip_header) + strm.total_out, sizeof(char));

    char old_content_length[26];
    size_t old_content_length_size = snprintf(old_content_length, 26, "Content-Length: %ld\r\n", response_info->response_size);

    char *first_part = strstr(response, old_content_length);

    response_info->response_size = strm.total_out;

    char *p;

    p = strncpy(gzipped_response, response, first_part - response);
    p = strncpy(gzipped_response + (first_part - response), first_part + old_content_length_size, response_info->header_size - (first_part - response) - old_content_length_size - 2);
    p = strncpy(p + response_info->header_size - (first_part - response) - old_content_length_size - 2, gzip_header, strlen(gzip_header));
    p = strncpy(p + strlen(gzip_header), cl_header, cl_header_content_size);
    response_info->header_size = first_part - response + response_info->header_size - (first_part - response) - old_content_length_size - 2 + strlen(gzip_header) + cl_header_content_size;

    p = (char *)memcpy(p + cl_header_content_size, out, strm.total_out);

    return gzipped_response;
}

int alter_request(char *request, char *altered_request, size_t *size)
{
    size_t i = 0;

    const char *xtra_header = "X-Header-Key:anton\r\n\r\n";

    while (1)
    {
        if (request[i] == '\r' && request[i + 1] == '\n' && request[i + 2] == '\r' && request[i + 3] == '\n')
        {
            memcpy(altered_request, request, i + 2);
            break;
        }
        i++;
    }

    strncpy(altered_request + i + 2, xtra_header, strlen(xtra_header));

    *size = i + 2 + strlen(xtra_header);

    return 1;
}

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

int downstream_data_ready(int client_socket, void *payload)
{
    struct pending_request *pr = payload;

    char *gzipped_response = gzip_response(pr->response, &pr->response_info);
    const size_t full_response_size = pr->response_info.response_size + pr->response_info.header_size;

    while (pr->response_sent < full_response_size)
    {

        ssize_t client_send_result = send(client_socket, gzipped_response + pr->response_sent, full_response_size - pr->response_sent, MSG_DONTWAIT);

        printf("  <-   *      %ld B\n", client_send_result);

        if (client_send_result == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            return 0;
        }

        if (client_send_result == -1)
        {
            perror("error during sending back to the client");
            return -1;
        }

        if (client_send_result == 0)
        {
            printf("client socket has been closed\n");
            return 0;
        }

        pr->response_sent += client_send_result;
    }

    pr->response_used = 0;
    pr->request_sent = 0;
    pr->response_sent = 0;
    free(pr->response);
    free(gzipped_response);
    pr->response = NULL;
    pr->response_info.response_size = 0;
    pr->response_info.header_size = 0;
    pr->response_info.response_type = -1;

    event_loop_remove(&event_loop, client_socket, POLLOUT, &downstream_data_ready);
    return 0;
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
    el_action->payload = uc;

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
    ssize_t upstream_send_result;

    while (1)
    {
        printf("Sending request to upstream socket=%d\n", uc->_fd);

        upstream_send_result = send(uc->_fd, request_buffer, request_buffer_size, MSG_DONTWAIT);

        if (upstream_send_result == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            return -1;
        }

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

int handle_queue(struct upstream_connection *uc)
{
    struct pending_request *first = peek(&pending_requests_queue);

    if (first == NULL)
    {
        return 1;
    }

    int8_t altered_request[4096];
    size_t altered_request_size;
    int8_t *request = first->request;
    size_t request_size = first->request_used;

    if (alter_request(first->request, altered_request, &altered_request_size) != 1)
    {
        perror("alter request failed");
    }

    request = altered_request;
    request_size = altered_request_size;

    while (first->request_sent < request_size)
    {
        ssize_t upstream_send_result = upstream_connection_send(uc, request + first->request_sent, request_size - first->request_sent);

        if (upstream_send_result == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            return -1;
        }

        first->request_sent += upstream_send_result;
    }

    first->is_request_finished = 0;
    first->request_used = 0;

    return 1;
}

int on_upstream_data(int upstream_fd, void *payload)
{
    struct pending_request *pr = (struct pending_request *)peek(&pending_requests_queue);
    struct upstream_connection *uc = payload;
    int8_t *response_storage;
    size_t response_storage_size;

    if (pr == NULL)
    {
        return 1;
    }

    while (1)
    {
        if (pr->response != NULL)
        {
            response_storage = pr->response;
            response_storage_size = sizeof(pr->response);
        }
        else
        {
            response_storage = pr->response_temp;
            response_storage_size = sizeof(pr->response_temp);
        }

        ssize_t upstream_recv_result = recv(upstream_fd, response_storage + pr->response_used, response_storage_size - pr->response_used, MSG_DONTWAIT);

        printf("     * <-   %ld B\n", upstream_recv_result);

        if (upstream_recv_result == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            break;
        }

        if (pr->response_info.response_type == UNKNOWN || !pr->response_info.response_type)
        {
            process_response(pr->response_temp, upstream_recv_result, &(pr->response_info));
        }

        pr->response_used += upstream_recv_result;

        if (pr->response_info.response_type != UNKNOWN && pr->response == NULL)
        {
            pr->response = malloc(pr->response_info.header_size + pr->response_info.response_size);
            memcpy(pr->response, pr->response_temp, pr->response_used);
        }

        if (pr->response_info.response_type == CHUNKED && chunked_response_is_complete(response_storage, pr->response_used))
        {
            struct event_loop_action *el_action = calloc(1, sizeof(struct event_loop_action));
            el_action->action = &downstream_data_ready;
            el_action->payload = pr;

            event_loop_add(&event_loop, pr->client_socket, POLLOUT, el_action);

            dequeue(&pending_requests_queue);
            handle_queue(uc);
            printf("chunked response is complete\n");
            return 0;
        }

        if (pr->response_info.response_type == CONTENT_LENGTH && pr->response_used >= pr->response_info.header_size + pr->response_info.response_size)
        {
            struct event_loop_action *el_action = calloc(1, sizeof(struct event_loop_action));
            el_action->action = &downstream_data_ready;
            el_action->payload = pr;

            event_loop_add(&event_loop, pr->client_socket, POLLOUT, el_action);

            dequeue(&pending_requests_queue);
            handle_queue(uc);
            printf("content length response is complete\n");
            return 0;
        }
    }
}

int request_is_complete(char *buf, ssize_t request_size)
{
    return buf[request_size - 1] == '\n' && buf[request_size - 2] == '\r' && buf[request_size - 3] == '\n' && buf[request_size - 4] == '\r';
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

int on_client_data(int client_socket, void *payload)
{

    hashtable_entry_t *data = hashtable_get(client_socket_buffers, &client_socket, sizeof(client_socket));

    struct pending_request *pr = (struct pending_request *)(data->val.data);

    struct upstream_connection *uc = payload;

    while (1)
    {
        if (pr->is_request_finished)
        {
            break;
        }

        ssize_t recv_result = recv(client_socket, pr->request + pr->request_used, sizeof(pr->request) - pr->request_used, MSG_DONTWAIT);

        if (recv_result == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            handle_queue(uc);
            break;
        }

        pr->request_used += recv_result;

        printf("->   *      %ld B\n", recv_result);

        if (request_is_complete(pr->request, pr->request_used))
        {
            pr->is_request_finished = 1;
            enqueue(&pending_requests_queue, pr);
            handle_queue(uc);
            break;
        }

        if (recv_result == 0)
        {
            fprintf(stderr, "connection has been closed\n");
            hashtable_remove(client_socket_buffers, &client_socket, sizeof(client_socket));
            close(client_socket);
            return 0;
        }
    }

    return 1;
}

int on_connect(int sfd, void *payload)
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

    struct pending_request *pr = calloc(1, sizeof(struct pending_request));
    pr->response = NULL;
    pr->client_socket = client_socket;

    hashtable_kv_t *key_value = calloc(2, sizeof(hashtable_kv_t));

    key_value[0].data = &client_socket;
    key_value[0].bytes = sizeof(client_socket);

    key_value[1].data = pr;
    key_value[1].bytes = sizeof(struct pending_request);

    hashtable_put(client_socket_buffers, key_value, key_value + 1);

    // enqueue(&pending_requests_queue, pr);

    struct event_loop_action *el_action = calloc(1, sizeof(struct event_loop_action));
    el_action->action = &on_client_data;
    el_action->payload = (void *)uc;

    event_loop_add(&event_loop, client_socket, POLLIN, el_action);

    return 0;
}

int main(int argc, char *argv[])
{
    initialize_queue(&pending_requests_queue);

    client_socket_buffers = hashtable_create(16);

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
    action.action = &on_connect;
    action.payload = (void *)&u_connection;

    event_loop_add(&event_loop, sfd, POLLIN, &action);
    event_loop_start(&event_loop);
}