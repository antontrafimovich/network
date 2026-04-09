#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

void to_upper_case(char *str) {
    int i;
    size_t len = strlen(str);

    for (i = 0; i < len; i++) {
        str[i] = toupper(str[i]);
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

    addr.s_addr = INADDR_ANY;

    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(8080);
    sockaddr.sin_addr = addr;

    if (-1 == bind(fd, (const struct sockaddr *) &sockaddr, sizeof(struct sockaddr_in))) {
        perror("bind failed");
        close(fd);
        return -1;
    }

    char buf[1024];

    memset(buf, 0, 1024);

    int recv_result;
    struct sockaddr_in src_addr;
    socklen_t addrlen = sizeof(struct sockaddr_in);

    struct msghdr header = {0};
    struct iovec iovec = {0};

    while((recv_result = recvfrom(fd, buf, 1024, 0, (struct sockaddr *) &src_addr, &addrlen)) != -1) {
        if (recv_result == 0) {
            printf("Transmission is finished\n");
            close(fd);
            return 0;
        }

        printf("source address is: %d\n", ntohl(src_addr.sin_addr.s_addr));
        printf("source port is: %d\n", ntohs(src_addr.sin_port));

        printf("Recieved message: %s", buf);

        header.msg_name = &src_addr;
        header.msg_namelen = addrlen;

        to_upper_case(buf);

        iovec.iov_base = buf;
        iovec.iov_len = recv_result;

        header.msg_iov = &iovec;
        header.msg_iovlen = 1;

        if (-1 == sendmsg(fd, &header, 0)) {
            printf("errno is %d\n", errno);
            perror("sendmsg failed");
        }

        memset(buf, 0, 1024);
    }

    if (-1 == recv_result) {
        perror("recvfrom failed");
        close(fd);
        return -1;
    }

    close(fd);

    return 0;
}