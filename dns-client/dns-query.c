#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdint.h>

void print_byte_binary(unsigned char byte)
{
    for (int i = 7; i >= 0; i--)
    {
        printf("%d", (byte >> i) & 1);
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

    struct sockaddr_in addr = {0};
    struct in_addr in_addr = {htonl(0x0afffffe)};

    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr = in_addr;

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        perror("connect failed");
    }

    char buf[4096] = {0};
    buf[0] = 0x1f;
    buf[1] = 0x2f;
    buf[2] = 0;
    buf[3] = 0;
    buf[4] = 0;
    buf[5] = 0x01;
    buf[6] = 0;
    buf[7] = 0;
    buf[8] = 0;
    buf[9] = 0;
    buf[10] = 0;
    buf[11] = 0;
    buf[12] = 0x03;
    buf[13] = 'w';
    buf[14] = 'w';
    buf[15] = 'w';
    buf[16] = 0x07;
    buf[17] = 'e';
    buf[18] = 'x';
    buf[19] = 'a';
    buf[20] = 'm';
    buf[21] = 'p';
    buf[22] = 'l';
    buf[23] = 'e';
    buf[24] = 0x03;
    buf[25] = 'c';
    buf[26] = 'o';
    buf[27] = 'm';
    buf[28] = 0x0;
    buf[29] = 0x0;
    buf[30] = 'A';
    buf[31] = 'I';
    buf[32] = 'N';

    if (send(fd, buf, 33, 0) == -1)
    {
        perror("send failed");
        return 1;
    }

    uint8_t query[512];
    ssize_t query_length;
    size_t used = 0;
    while (1)
    {

        query_length = recv(fd, query + used, 512 - used, 0);

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
    for (i = 12; i < query_length; i++)
    {
        printf("\\");
        print_byte_binary(query[i]);
    }
    printf("\n");

    return 0;
}