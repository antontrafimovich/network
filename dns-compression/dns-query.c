#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#define ISCOMPRESSED(b) ((b >> 6) == 0x03)

#define ISALPHANUMERIC(b) (b >= 40 && b <= 122)

#define HEADER_SIZE 12

struct domain_response
{
    char *name;
    uint8_t addr;
};

struct domain_message_question
{
    char name[256];
    uint16_t type;
    uint16_t class;
};

struct domain_message
{
    int16_t id;
    bool qr;
    uint8_t opcode;
    bool aa_authoritative_answer;
    bool tc_truncation;
    bool rd_recursion_desired;
    bool ra_recursion_available;
    int8_t z_for_future_use;
    uint8_t rcode_response_code;
    uint16_t qdcount_question_section_entries_number;
    uint16_t ancount_answer_section_entries_number;
    uint16_t nscount_authority_section_entries_number;
    uint16_t arcount_additinal_section_entries_number;
    struct domain_message_question *questions;
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

int dns_name_to_string(uint8_t *dns_section_record_start, char *name)
{
    uint8_t *dns_section_record_start_p = dns_section_record_start;

    while (*dns_section_record_start_p != 0)
    {
        if (ISALPHANUMERIC(*dns_section_record_start_p))
        {
            *name++ = *dns_section_record_start_p++;
        }
        else
        {
            *name++ = '.';
            dns_section_record_start_p++;
        }
    }

    *name = '\0';

    return 0;
}

int parse_dns_question_entry(struct domain_message_question question, uint8_t *dns_response)
{
    const size_t null_root_label_length = 1;
    uint8_t *dns_question_section_p = dns_response + 12;

    if (!ISCOMPRESSED(*dns_question_section_p))
    {
        dns_name_to_string(dns_question_section_p, question.name);
        dns_question_section_p += strlen(question.name) + null_root_label_length;
    }

    // while (*dns_response_p != 0)
    // {
    //     if (ISCOMPRESSED(*dns_response_p))
    //     {
    //     }
    //     else
    //     {
    //     }
    // }
}

int dns_response_to_user_friendly(uint8_t *dns_response, size_t resp_length)
{
    struct domain_message domain_message_answer = {0};

    uint8_t *dns_response_p = dns_response;

    // start parse query response header
    memcpy(&domain_message_answer.id, dns_response_p, 2);
    domain_message_answer.id = ntohs(domain_message_answer.id);

    dns_response_p += 2;

    uint16_t codes = 0;
    memcpy(&codes, dns_response_p, 2);
    codes = ntohs(codes);
    domain_message_answer.qr = (codes >> 15) & 1;
    domain_message_answer.opcode = (codes >> 11) & 0x0f;
    domain_message_answer.aa_authoritative_answer = (codes >> 10) & 1;
    domain_message_answer.tc_truncation = (codes >> 9) & 1;
    domain_message_answer.rd_recursion_desired = (codes >> 8) & 1;
    domain_message_answer.ra_recursion_available = (codes >> 7) & 1;
    domain_message_answer.z_for_future_use = (codes >> 4) & 7;
    domain_message_answer.rcode_response_code = codes & 0x0f;

    dns_response_p += 2;

    domain_message_answer.qdcount_question_section_entries_number = (uint16_t)(dns_response_p[0] | dns_response_p[1]);
    dns_response_p += 2;

    domain_message_answer.ancount_answer_section_entries_number = (uint16_t)(dns_response_p[0] | dns_response_p[1]);
    dns_response_p += 2;

    domain_message_answer.nscount_authority_section_entries_number = (uint16_t)(dns_response_p[0] | dns_response_p[1]);
    dns_response_p += 2;

    domain_message_answer.arcount_additinal_section_entries_number = (uint16_t)(dns_response_p[0] | dns_response_p[1]);
    dns_response_p += 2;
    // end parse query response header

    // parse question section entries
    domain_message_answer.questions = malloc(sizeof(struct domain_message_question) * domain_message_answer.qdcount_question_section_entries_number);

    size_t i = 0;
    while (i < domain_message_answer.qdcount_question_section_entries_number)
    {
        parse_dns_question_entry(domain_message_answer.questions[i], dns_response_p);

        i++;
    }

    // parse answer section entries
    // int i;
    // uint16_t ancount = dns_response[7];
    // uint8_t *dns_response_p = dns_response;

    // char name[32] = {0};
    // char *name_p = name;
    // size_t handled = HEADER_SIZE;

    // dns_response_p += HEADER_SIZE;

    // while (*dns_response_p != 0)
    // {
    //     if (ISALPHANUMERIC(*dns_response_p))
    //     {
    //         *name_p++ = *dns_response_p++;
    //     }
    //     else
    //     {
    //         *name_p++ = '.';
    //         dns_response_p++;
    //     }

    //     handled++;
    // }

    // *name_p++ = '\0';

    // dns_response_p += 5;
    // handled += 5;

    // int ip_octets_to_handle = 0;

    // while (handled < resp_length)
    // {
    //     if (ISCOMPRESSED(*dns_response_p))
    //     {
    //         printf("%s: ", name);
    //         dns_response_p += 12;
    //         ip_octets_to_handle = *(dns_response_p - 1);
    //         handled += 12;
    //     }

    //     if (ip_octets_to_handle != 0)
    //     {
    //         while (ip_octets_to_handle-- > 0)
    //         {
    //             printf(ip_octets_to_handle == 0 ? "%d\n" : "%d.", *(unsigned char *)(dns_response_p++));
    //             handled++;
    //         }
    //     }
    // }
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
    buf[2] = 0x01;
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
