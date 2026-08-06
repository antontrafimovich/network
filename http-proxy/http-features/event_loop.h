#include <poll.h>
#include <unistd.h>

struct event_loop_action
{
    int (*action)(int fd, void *payload);
    void *payload;
};

struct event_loop
{
    struct pollfd _pfds[10];
    size_t _npfds;
    struct event_loop_action *_actions[10];
};

int event_loop_init(struct event_loop *el);
int event_loop_destroy(struct event_loop *el);
int event_loop_add(struct event_loop *el, int fd, short int events, struct event_loop_action *action);
int event_loop_start(struct event_loop *el);