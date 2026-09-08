#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define ISCOMPRESSED(b) ((b >> 6) == 0x03)

#define ISALPHANUMERIC(b) (b >= 40 && b <= 122)

#define HEADER_SIZE 12

struct domain_response
{
    char *name;
    uint8_t addr;
};

void print_byte_binary(unsigned char byte)
{
    for (int i = 7; i >= 0; i--)
    {
        printf("%d", (byte >> i) & 1);
    }
}

void str_to_qname_value(char *str, char *buf)
{
    unsigned char *label_len = (unsigned char *)buf++;
    unsigned char len = 0;

    while (*str != '\0')
    {
        if (*str == '.')
        {
            *label_len = len;
            label_len = (unsigned char *)buf++;
            len = 0;
        }
        else
        {
            *buf++ = *str;
            len++;
        }

        str++;
    }

    *label_len = len;
    *buf = 0;
}

int dns_response_to_user_friendly(uint8_t *dns_response, size_t resp_length)
{
    int i;
    uint16_t ancount = dns_response[7];
    uint8_t *dns_response_p = dns_response;

    char name[32] = {0};
    char *name_p = name;
    size_t handled = HEADER_SIZE;

    dns_response_p += HEADER_SIZE;

    while (*dns_response_p != 0)
    {
        if (ISALPHANUMERIC(*dns_response_p))
        {
            *name_p++ = *dns_response_p++;
        }
        else
        {
            *name_p++ = '.';
            dns_response_p++;
        }

        handled++;
    }

    *name_p++ = '\0';

    dns_response_p += 5;
    handled += 5;

    int ip_octets_to_handle = 0;

    printf("dns response byte: %d\n", *dns_response_p);
    printf("is compressed: %d\n", ISCOMPRESSED(*dns_response_p));

    while (handled < resp_length)
    {
        if (ISCOMPRESSED(*dns_response_p))
        {
            printf("%s: ", name);
            dns_response_p += 12;
            ip_octets_to_handle = *(dns_response_p - 1);
            handled += 12;
        }

        if (ip_octets_to_handle != 0)
        {
            while (ip_octets_to_handle-- > 0)
            {
                printf(ip_octets_to_handle == 0 ? "%d\n" : "%d.", *(unsigned char *)(dns_response_p++));
                handled++;
            }
        }
    }
}

int main(int argc, char **argv)
{
    int fd;

    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        perror("socket failed");
        return 1;
    }

    struct in_addr in_addr;
    if (!inet_pton(AF_INET, "1.1.1.1", &in_addr) == -1)
    {
        perror("inet_pton failed");
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr = in_addr;

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        perror("connect failed");
    }

    char buf[4096] = {0};
    char *host = "d3js.org";
    size_t question_len = strlen(host) + 2;

    buf[0] = 0x1f;
    buf[1] = 0x2f;
    buf[2] = 0;
    buf[3] = 0;
    buf[4] = 0;
    buf[5] = 1;
    buf[6] = 0;
    buf[7] = 0;
    buf[8] = 0;
    buf[9] = 0;
    buf[10] = 0;
    buf[11] = 0;

    str_to_qname_value(host, buf + 12);

    buf[12 + question_len] = 0x0;
    buf[13 + question_len] = 0x01;
    buf[14 + question_len] = 0x0;
    buf[15 + question_len] = 0x01;

    if (send(fd, buf, 16 + question_len, 0) == -1)
    {
        perror("send failed");
        return 1;
    }

    uint8_t dns_response[512];
    ssize_t query_length;
    size_t used = 0;
    while (1)
    {

        query_length = recv(fd, dns_response + used, 512 - used, 0);

        if (query_length == -1)
        {
            perror("recv failed");
            return 1;
        }

        if (query_length == 0)
        {
            break;
        }

        used += query_length;
        break;
    }

    int i;
    printf("The received dns result is: ");
    for (i = 0; i < query_length; i++)
    {
        printf("\\%02x", dns_response[i]);
        // print_byte_binary(query[i]);
    }
    printf("\n");

    dns_response_to_user_friendly(dns_response, query_length);

    return 0;
}
