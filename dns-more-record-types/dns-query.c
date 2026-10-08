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

struct dns_client_args
{
    bool reverse;
    uint8_t qtype;
    char value[256];
};

enum resolver_input_type
{
    ipv4 = 0,
    domain = 1
};

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
    struct domain_message_answer *authorities;
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
    size_t len = 0;

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

void dns_class_to_string(uint16_t class, char *class_str)
{
    switch (class)
    {
    case 1:
        strncpy(class_str, "IN_INTERNET", strlen("IN_INTERNET") + 1);
        break;
    case 2:
        strncpy(class_str, "CS_CSNET_OBSOLETE", strlen("CS_CSNET_OBSOLETE") + 1);
        break;
    case 3:
        strncpy(class_str, "CH_CHAOS", strlen("CH_CHAOS") + 1);
        break;
    case 4:
        strncpy(class_str, "IN_HESIOD", strlen("IN_HESIOD") + 1);
        break;

    default:
        strncpy(class_str, "IN_INTERNET", strlen("IN_INTERNET") + 1);
        break;
    }
}

void dns_type_to_string(uint16_t type, char *type_str)
{
    switch (type)
    {
    case 1:
        strncpy(type_str, "A_IPV4", strlen("A_IPV4") + 1);
        break;
    case 2:
        strncpy(type_str, "NS_AUTHORITATIVE_NAME_SERVER", strlen("NS_AUTHORITATIVE_NAME_SERVER") + 1);
        break;
    case 3:
        strncpy(type_str, "MD_MAIL_DESTINATION_OBSOLETE", strlen("MD_MAIL_DESTINATION_OBSOLETE") + 1);
        break;
    case 5:
        strncpy(type_str, "CNAME_CANONICAL_NAME", strlen("CNAME_CANONICAL_NAME") + 1);
        break;
    case 6:
        strncpy(type_str, "SOA_START_OF_AUTHORITY", strlen("SOA_START_OF_AUTHORITY") + 1);
        break;
    case 11:
        strncpy(type_str, "WSK_WELL_KNOWN_SERVICE_DESCRIPTION", strlen("WSK_WELL_KNOWN_SERVICE_DESCRIPTION") + 1);
        break;
    case 12:
        strncpy(type_str, "PTR_DOMAIN_NAME_POINTER", strlen("PTR_DOMAIN_NAME_POINTER") + 1);
        break;
    case 13:
        strncpy(type_str, "HINFO_HOST_INFORMATION", strlen("HINFO_HOST_INFORMATION") + 1);
        break;
    case 14:
        strncpy(type_str, "MINFO_MAILBOX_OR_MAIL_LIST_INFORMATION", strlen("MINFO_MAILBOX_OR_MAIL_LIST_INFORMATION") + 1);
        break;
    case 15:
        strncpy(type_str, "MX_MAIL_EXCHANGE", strlen("MX_MAIL_EXCHANGE") + 1);
        break;
    case 16:
        strncpy(type_str, "TXT_TEXT_STRINGs", strlen("TXT_TEXT_STRINGs") + 1);
        break;
    case 28:
        strncpy(type_str, "AAAA_IPV6", strlen("AAAA_IPV6") + 1);
        break;

    default:
        strncpy(type_str, "A_IPV4", strlen("A_IPV4") + 1);
        break;
    }
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

void dns_name_to_ipv6_string(uint8_t *dns_section_record_start, char *name, uint8_t *dns_response_start, size_t *dns_name_length)
{
    size_t length = 16;
    char tmp[5];

    size_t i;
    for (i = 0; i < length; i += 2)
    {
        int result = snprintf(name, 5, "%x", (uint16_t)(dns_section_record_start[i] << 8) | (uint16_t)(dns_section_record_start[i + 1]));
        name += result;

        if (i + 2 < length)
        {
            *name++ = ':';
        }
    }

    *name = 0;
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

void dns_answer_to_soa_string(char *response_str, uint8_t *rdata_section_start, size_t rdata_section_size, uint8_t *dns_response)
{
    size_t i = 0;

    char mname_authoritative_name_server_for_zone[64];
    char rname_mailbox_responsible_for_zone[128];

    uint32_t serial_version_number_of_zone;
    uint32_t refresh_time_interval_before_refresh;
    uint32_t retry_time_interval_elapse_before_retry_on_fail;
    uint32_t expire_upper_time_limit_before_zone_no_authoritative;
    uint32_t minimum_ttl_field_exported_with_any_rr;

    size_t mname_length;
    dns_name_to_string(rdata_section_start, mname_authoritative_name_server_for_zone, dns_response, &mname_length);
    rdata_section_start += mname_length;

    size_t rname_length;
    dns_name_to_string(rdata_section_start, rname_mailbox_responsible_for_zone, dns_response, &rname_length);
    rdata_section_start += rname_length;

    serial_version_number_of_zone = (uint32_t)(rdata_section_start[0] << 24) | (uint32_t)(rdata_section_start[1] << 16) | (uint32_t)(rdata_section_start[2] << 8) | (uint32_t)rdata_section_start[3];
    rdata_section_start += 4;

    refresh_time_interval_before_refresh = (uint32_t)(rdata_section_start[0] << 24) | (uint32_t)(rdata_section_start[1] << 16) | (uint32_t)(rdata_section_start[2] << 8) | (uint32_t)rdata_section_start[3];
    rdata_section_start += 4;

    retry_time_interval_elapse_before_retry_on_fail = (uint32_t)(rdata_section_start[0] << 24) | (uint32_t)(rdata_section_start[1] << 16) | (uint32_t)(rdata_section_start[2] << 8) | (uint32_t)rdata_section_start[3];
    rdata_section_start += 4;

    expire_upper_time_limit_before_zone_no_authoritative = (uint32_t)(rdata_section_start[0] << 24) | (uint32_t)(rdata_section_start[1] << 16) | (uint32_t)(rdata_section_start[2] << 8) | (uint32_t)rdata_section_start[3];
    rdata_section_start += 4;

    minimum_ttl_field_exported_with_any_rr = (uint32_t)(rdata_section_start[0] << 24) | (uint32_t)(rdata_section_start[1] << 16) | (uint32_t)(rdata_section_start[2] << 8) | (uint32_t)rdata_section_start[3];
    rdata_section_start += 4;

    snprintf(response_str, 512, "%s  %s  %d  %d  %d  %d %d", mname_authoritative_name_server_for_zone, rname_mailbox_responsible_for_zone, serial_version_number_of_zone, refresh_time_interval_before_refresh, retry_time_interval_elapse_before_retry_on_fail, expire_upper_time_limit_before_zone_no_authoritative, minimum_ttl_field_exported_with_any_rr);
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

    domain_message_answer.qdcount_question_section_entries_number = ((uint16_t)dns_response_p[0] << 8) | dns_response_p[1];
    dns_response_p += 2;

    domain_message_answer.ancount_answer_section_entries_number = ((uint16_t)dns_response_p[0] << 8) | dns_response_p[1];
    dns_response_p += 2;

    domain_message_answer.nscount_authority_section_entries_number = ((uint16_t)dns_response_p[0] << 8) | dns_response_p[1];
    dns_response_p += 2;

    domain_message_answer.arcount_additinal_section_entries_number = ((uint16_t)dns_response_p[0] << 8) | dns_response_p[1];
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

    domain_message_answer.answers = malloc(sizeof(struct domain_message_answer) * domain_message_answer.ancount_answer_section_entries_number);

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

    domain_message_answer.authorities = malloc(sizeof(struct domain_message_answer) * domain_message_answer.nscount_authority_section_entries_number);

    size_t m = 0;
    size_t authority_length = 0;
    size_t authority_entry_offset = dns_response_p - dns_response;

    while (m < domain_message_answer.nscount_authority_section_entries_number)
    {
        parse_dns_answer_entry(&domain_message_answer.authorities[m], dns_response, authority_entry_offset, &authority_length);
        authority_entry_offset += authority_length;
        dns_response_p += authority_length;
        m++;
    }

    printf("ANSWER SECTION:\n\n");
    size_t k;
    for (k = 0; k < domain_message_answer.ancount_answer_section_entries_number; k++)
    {
        struct domain_message_answer aw = domain_message_answer.answers[k];

        char dns_type_str[128];
        dns_type_to_string(aw.type, dns_type_str);

        char dns_class_str[128];
        dns_class_to_string(aw.class, dns_class_str);

        printf("%s:    %s    %s    ", aw.name, dns_type_str, dns_class_str);

        if (aw.type == 1)
        {
            size_t l;
            for (l = 0; l < aw.rdlength; l++)
            {
                printf(l == aw.rdlength - 1 ? "%d\n" : "%d.", aw.rdata[l]);
            }
        }
        else if (aw.type == 28)
        {
            char dns_answer_ipv6_value[64];
            dns_name_to_ipv6_string(aw.rdata, dns_answer_ipv6_value, dns_response, NULL);

            printf("%s\n", dns_answer_ipv6_value);
        }
        else if (aw.type == 6)
        {
            char dns_answer_soa_value[512];
            dns_answer_to_soa_string(dns_answer_soa_value, aw.rdata, aw.rdlength, dns_response);

            printf("%s\n", dns_answer_soa_value);
        }
        else
        {
            char dns_answer_text_value[256];
            dns_name_to_string(aw.rdata, dns_answer_text_value, dns_response, NULL);

            printf("%s\n", dns_answer_text_value);
        }
    }

    printf("AUTHORITY SECTION:\n\n");
    size_t l;
    for (l = 0; l < domain_message_answer.nscount_authority_section_entries_number; l++)
    {
        struct domain_message_answer au = domain_message_answer.authorities[l];

        char dns_type_str[128];
        dns_type_to_string(au.type, dns_type_str);

        char dns_class_str[128];
        dns_class_to_string(au.class, dns_class_str);

        printf("%s:    %s    %s    ", au.name, dns_type_str, dns_class_str);

        if (au.type == 1)
        {
            size_t l;
            for (l = 0; l < au.rdlength; l++)
            {
                printf(l == au.rdlength - 1 ? "%d\n" : "%d.", au.rdata[l]);
            }
        }
        else if (au.type == 28)
        {
            char dns_answer_ipv6_value[64];
            dns_name_to_ipv6_string(au.rdata, dns_answer_ipv6_value, dns_response, NULL);

            printf("%s\n", dns_answer_ipv6_value);
        }
        else if (au.type == 6)
        {
            char dns_answer_soa_value[512];
            dns_answer_to_soa_string(dns_answer_soa_value, au.rdata, au.rdlength, dns_response);

            printf("%s\n", dns_answer_soa_value);
        }
        else
        {
            char dns_answer_text_value[256];
            dns_name_to_string(au.rdata, dns_answer_text_value, dns_response, NULL);

            printf("%s\n", dns_answer_text_value);
        }
    }

    printf("end\n");
}

void build_dns_inverse_query(char *domain, uint8_t *buf, size_t *dns_query_size)
{
    const uint16_t QTYPE_PTR = 12;

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

    const char *inverse_domain_suffix = ".in-addr.arpa";
    char *inverse_domain = malloc(strlen(domain) + strlen(inverse_domain_suffix) + 1);

    char *tmp_domain = domain;
    size_t label_len = 0;
    size_t inverse_domain_offset = 0;

    while (*tmp_domain != 0)
    {
        if (*tmp_domain == '.')
        {
            inverse_domain_offset += label_len;
            strncpy(inverse_domain + strlen(domain) - inverse_domain_offset, (tmp_domain - label_len), label_len);
            *(inverse_domain + strlen(domain) - inverse_domain_offset - 1) = '.';
            inverse_domain_offset += 1;
            label_len = 0;
        }
        else
        {
            label_len++;
        }

        tmp_domain++;
    }

    if (label_len != 0)
    {
        inverse_domain_offset += label_len;
        strncpy(inverse_domain + strlen(domain) - inverse_domain_offset, tmp_domain - label_len, label_len);
    }

    strncpy(inverse_domain + strlen(domain), inverse_domain_suffix, strlen(inverse_domain_suffix));
    *(inverse_domain + strlen(domain) + strlen(inverse_domain_suffix)) = '\0';

    str_to_qname_value(inverse_domain, buf + 12);
    size_t question_len = strlen(inverse_domain) + 2;

    free(inverse_domain);

    uint16_t qtype_question_type = QTYPE_PTR;
    uint16_t qclass_question_class = 1;

    buf[12 + question_len] = (uint8_t)(qtype_question_type >> 8);
    buf[13 + question_len] = (uint8_t)(qtype_question_type & 0xff);
    buf[14 + question_len] = (uint8_t)(qclass_question_class >> 8);
    buf[15 + question_len] = (uint8_t)(qclass_question_class & 0xff);

    *dns_query_size = 16 + question_len;
}

void build_dns_query(char *domain, uint8_t qtype, uint8_t *buf, size_t *dns_query_size)
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

    uint16_t qtype_question_type = (uint16_t)qtype;
    uint16_t qclass_question_class = 1;

    buf[12 + question_len] = (uint8_t)(qtype_question_type >> 8);
    buf[13 + question_len] = (uint8_t)(qtype_question_type & 0xff);
    buf[14 + question_len] = (uint8_t)(qclass_question_class >> 8);
    buf[15 + question_len] = (uint8_t)(qclass_question_class & 0xff);

    *dns_query_size = 16 + question_len;
}

int parse_args(int argc, char **argv, struct dns_client_args *args)
{

    if (strncmp(argv[1], "-x", 2) == 0)
    {
        args->reverse = 1;
        strncpy(args->value, argv[2], strlen(argv[2]));
        args->qtype = 12;

        return 0;
    }
    else
    {
        args->reverse = 0;
        strncpy(args->value, argv[1], strlen(argv[1]));
    }

    const size_t arg_qtype_length = strlen(argv[2]);

    if (strncmp(argv[2], "TXT", arg_qtype_length) == 0)
    {

        args->qtype = 16;
    }
    else if (strncmp(argv[2], "AAAA", arg_qtype_length) == 0)
    {
        args->qtype = 28;
    }
    else if (strncmp(argv[2], "NS", arg_qtype_length) == 0)
    {
        args->qtype = 2;
    }
    else if (strncmp(argv[2], "PTR", arg_qtype_length) == 0)
    {
        args->qtype = 12;
    }
    else if (strncmp(argv[2], "SOA", arg_qtype_length) == 0)
    {
        args->qtype = 6;
    }
    else
    {
        args->qtype = 1;
    }

    return 0;
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
    size_t dns_query_size = 0;
    struct dns_client_args args = {0};
    parse_args(argc, argv, &args);

    if (args.reverse)
    {
        build_dns_inverse_query(args.value, buf, &dns_query_size);
    }
    else
    {
        build_dns_query(args.value, args.qtype, buf, &dns_query_size);
    }

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
