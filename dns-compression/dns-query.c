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

#define COMPRESSEDTOOFFSET(b, c) (((b & 0x3f) << 8) | c)

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

struct domain_message_answer
{
    char name[256];
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t rdlength;
    uint8_t *rdata;
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
    struct domain_message_answer *answers;
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

int dns_name_to_string(uint8_t *dns_section_record_start, char *name, uint8_t *dns_response_start, size_t *dns_name_length)
{
    uint8_t *dns_section_record_start_p = dns_section_record_start;

    while (*dns_section_record_start_p != 0)
    {
        if (ISALPHANUMERIC(*dns_section_record_start_p))
        {
            *name++ = *dns_section_record_start_p++;
        }
        else if (ISCOMPRESSED(*dns_section_record_start_p))
        {
            size_t decompressed_name_length = 0;
            dns_name_to_string(dns_response_start + COMPRESSEDTOOFFSET(dns_section_record_start_p[0], dns_section_record_start_p[1]), name, dns_response_start, &decompressed_name_length);
            name += decompressed_name_length;
            dns_section_record_start_p += 1;
            break;
        }
        else
        {
            *name++ = '.';
            dns_section_record_start_p++;
        }
    }

    if (dns_name_length != NULL)
    {
        *dns_name_length = dns_section_record_start_p - dns_section_record_start + 1;
    }

    *name = '\0';

    return 0;
}

int parse_dns_question_entry(struct domain_message_question *question, uint8_t *dns_response, size_t question_entry_offset, size_t *question_length)
{
    uint8_t *dns_question_section_p = dns_response + question_entry_offset;

    size_t dns_name_length = 0;
    dns_name_to_string(dns_question_section_p, question->name, dns_response, &dns_name_length);
    dns_question_section_p += dns_name_length;

    question->type = (uint16_t)((dns_question_section_p[0] << 8) | dns_question_section_p[1]);
    dns_question_section_p += 2;

    question->class = (uint16_t)((dns_question_section_p[0] << 8) | dns_question_section_p[1]);
    dns_question_section_p += 2;

    *question_length = dns_question_section_p - dns_response - question_entry_offset;

    return 0;
}

int parse_dns_answer_entry(struct domain_message_answer *answer, uint8_t *dns_response, size_t answer_entry_offset, size_t *answer_length)
{
    uint8_t *dns_answer_section_p = dns_response + answer_entry_offset;

    size_t dns_name_length = 0;
    dns_name_to_string(dns_answer_section_p, answer->name, dns_response, &dns_name_length);
    dns_answer_section_p += dns_name_length;

    answer->type = (uint16_t)((dns_answer_section_p[0] << 8) | dns_answer_section_p[1]);
    dns_answer_section_p += 2;

    answer->class = (uint16_t)((dns_answer_section_p[0] << 8) | dns_answer_section_p[1]);
    dns_answer_section_p += 2;

    memcpy(&answer->ttl, dns_answer_section_p, 4);
    answer->ttl = ntohl(answer->ttl);
    dns_answer_section_p += 4;

    answer->rdlength = (uint16_t)((dns_answer_section_p[0] << 8) | dns_answer_section_p[1]);
    dns_answer_section_p += 2;

    answer->rdata = malloc(sizeof(uint8_t) * answer->rdlength);
    memcpy(answer->rdata, dns_answer_section_p, answer->rdlength);
    dns_answer_section_p += answer->rdlength;

    *answer_length = dns_answer_section_p - dns_response - answer_entry_offset;

    return 0;
}

int dns_response_to_user_friendly(uint8_t *dns_response)
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
    size_t question_length = 0;
    size_t question_entry_offset = dns_response_p - dns_response;
    while (i < domain_message_answer.qdcount_question_section_entries_number)
    {
        parse_dns_question_entry(&domain_message_answer.questions[i], dns_response, question_entry_offset, &question_length);
        question_entry_offset += question_length;
        dns_response_p += question_length;
        i++;
    }

    domain_message_answer.answers = malloc(sizeof(struct domain_message_answer) * domain_message_answer.arcount_additinal_section_entries_number);

    size_t j = 0;
    size_t answer_length = 0;
    size_t answer_entry_offset = dns_response_p - dns_response;

    while (j < domain_message_answer.ancount_answer_section_entries_number)
    {
        parse_dns_answer_entry(&domain_message_answer.answers[j], dns_response, answer_entry_offset, &answer_length);
        answer_entry_offset += answer_length;
        dns_response_p += answer_length;
        j++;
    }

    size_t k;
    for (k = 0; k < domain_message_answer.ancount_answer_section_entries_number; k++)
    {
        struct domain_message_answer aw = domain_message_answer.answers[k];
        printf("%s: ", aw.name);

        if (aw.type == 1)
        {
            size_t l;
            for (l = 0; l < aw.rdlength; l++)
            {
                printf(l == aw.rdlength - 1 ? "%d\n" : "%d.", aw.rdata[l]);
            }
        }

        if (aw.type == 2)
        {
            char authoritatve_name_server[64];
            dns_name_to_string(aw.rdata, authoritatve_name_server, dns_response, NULL);

            printf("%s\n", authoritatve_name_server);
        }
    }

    printf("end\n");
}

void build_dns_query(char *domain, uint8_t *buf, size_t *dns_query_size)
{
    uint16_t id = 0x1f2f;
    uint8_t qr_query_or_response = 0;
    uint8_t opcode_operation_code = 0;
    uint8_t aa_authoritative_answer = 0;
    uint8_t tc_truncation = 0;
    uint8_t rd_recursion_desired = 1;
    uint8_t ra_recursion_available = 0;
    uint8_t z_future_use = 0;
    uint8_t rcode_response_code = 0;
    uint16_t qdcount_question_entries_count = 1;
    uint16_t ancount_answer_entries_count = 0;
    uint16_t nscount_name_server_resource_records_count = 0;
    uint16_t arcount_additional_records_count = 0;

    buf[0] = (uint8_t)(id >> 8);
    buf[1] = (uint8_t)(id & 0xff);
    buf[2] = (qr_query_or_response << 7) | (opcode_operation_code << 3) | (aa_authoritative_answer << 2) | (tc_truncation << 1) | rd_recursion_desired;
    buf[3] = (ra_recursion_available << 7) | (z_future_use << 4) | (rcode_response_code);
    buf[4] = (uint8_t)(qdcount_question_entries_count >> 8);
    buf[5] = (uint8_t)(qdcount_question_entries_count & 0xff);
    buf[6] = (uint8_t)(ancount_answer_entries_count >> 8);
    buf[7] = (uint8_t)(ancount_answer_entries_count & 0xff);
    buf[8] = (uint8_t)(nscount_name_server_resource_records_count >> 8);
    buf[9] = (uint8_t)(nscount_name_server_resource_records_count & 0xff);
    buf[10] = (uint8_t)(arcount_additional_records_count >> 8);
    buf[11] = (uint8_t)(arcount_additional_records_count & 0xff);

    str_to_qname_value(domain, buf + 12);
    size_t question_len = strlen(domain) + 2;

    uint16_t qtype_question_type = 2;
    uint16_t qclass_question_class = 1;

    buf[12 + question_len] = (uint8_t)(qtype_question_type >> 8);
    buf[13 + question_len] = (uint8_t)(qtype_question_type & 0xff);
    buf[14 + question_len] = (uint8_t)(qclass_question_class >> 8);
    buf[15 + question_len] = (uint8_t)(qclass_question_class & 0xff);

    *dns_query_size = 16 + question_len;
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

    uint8_t buf[4096] = {0};
    char *host = "d3js.org";

    size_t dns_query_size = 0;
    build_dns_query(host, buf, &dns_query_size);

    if (send(fd, buf, dns_query_size, 0) == -1)
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

    dns_response_to_user_friendly(dns_response);

    return 0;
}
