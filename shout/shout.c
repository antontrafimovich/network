#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

void to_upper_case(char *str)
{
    size_t i;
    size_t len = strlen(str);

    for (i = 0; i < len; i++)
    {
        str[i] = toupper((unsigned char *)str[i]);
    }
}

int main(int argc, char **argv)
{
    int fd;

    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        perror("socket failed");
        return -1;
    }

    struct sockaddr_in sockaddr;
    struct in_addr addr;

    memset(&sockaddr, 0, sizeof(sockaddr));
    memset(&addr, 0, sizeof(addr));

    addr.s_addr = htonl(0x7f000001);

    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(8080);
    sockaddr.sin_addr = addr;

    if (-1 == bind(fd, (const struct sockaddr *)&sockaddr, sizeof(struct sockaddr_in)))
    {
        perror("bind failed");
        close(fd);
        return -1;
    }

    char buf[1024];

    memset(buf, 0, 1024);

    int recv_result;
    struct sockaddr_in src_addr;
    socklen_t addrlen;

    while (1)
    {
        addrlen = sizeof(src_addr);
        recv_result = recvfrom(fd, buf, 1024, 0, (struct sockaddr *)&src_addr, &addrlen);

        struct msghdr header = {0};
        struct iovec iovec[2];

        if (recv_result == -1)
        {
            perror("recvfrom failed");
            close(fd);
            return -1;
        }

        if (recv_result == 0)
        {
            printf("Transmission is finished\n");
            close(fd);
            return 0;
        }

        printf("source address is: %d\n", ntohl(src_addr.sin_addr.s_addr));
        printf("source port is: %d\n", ntohs(src_addr.sin_port));

        char *str = malloc(sizeof(char) * (recv_result + 1));
        strncpy(str, buf, recv_result);
        str[recv_result] = '\0';

        printf("Recieved message: %s", str);

        free(str);
        header.msg_name = &src_addr;
        header.msg_namelen = addrlen;

        to_upper_case(buf);

        iovec[0].iov_base = buf;
        iovec[0].iov_len = recv_result;
        iovec[1].iov_base = "anton\n\0";
        iovec[1].iov_len = 7;

        header.msg_iov = iovec;
        header.msg_iovlen = 2;

        if (-1 == sendmsg(fd, &header, 0))
        {
            printf("errno is %d\n", errno);
            perror("sendmsg failed");
        }

        memset(buf, 0, 1024);
    }

    close(fd);

    return 0;
}