#include <stdlib.h>
#include "event_loop.h"

int event_loop_init(struct event_loop *el)
{
    return 0;
}

int event_loop_destroy(struct event_loop *el)
{
    int i;

    for (i = 0; i < el->_npfds; i++)
    {
        close(el->_pfds[i].fd);
    }

    return 0;
}

int event_loop_add(struct event_loop *el, int fd, short int events, struct event_loop_action *action)
{
    el->_pfds[el->_npfds].fd = fd;
    el->_pfds[el->_npfds].events = events;

    el->_actions[el->_npfds] = action;

    el->_npfds++;

    return 0;
}

int event_loop_start(struct event_loop *el)
{
    while (1)
    {
        int ready;

        ready = poll(el->_pfds, el->_npfds, -1);

        printf("%d events happened in poll\n", ready);

        if (ready == -1)
        {
            perror("poll failed");
            exit(EXIT_FAILURE);
        }

        int i;
        for (i = 0; i < el->_npfds; i++)
        {
            if (ready == 0)
            {
                break;
            }

            if (el->_pfds[i].revents == 0)
            {
                continue;
            }

            printf("fd=%d (%s);\t"
                   "events value: %d,\t"
                   "events: %s%s%s%s\n",
                   el->_pfds[i].fd,
                   i == 0 ? "event on connections listening socket" : "event on connection data socket",
                   el->_pfds[i].revents,
                   (el->_pfds[i].revents & POLLIN) ? "POLLIN " : "",
                   (el->_pfds[i].revents & POLLHUP) ? "POLLHUP " : "",
                   (el->_pfds[i].revents & POLLERR) ? "POLLERR" : "",
                   (el->_pfds[i].revents & POLLNVAL) ? "POLLNVAL" : "");

            if (el->_pfds[i].revents & el->_pfds[i].events)
            {
                el->_actions[i]->action(el->_pfds[i].fd, el->_actions[i]->payload);
            }

            if (el->_pfds[i].revents & POLLNVAL)
            {
                printf("closing fd=%d\n", el->_pfds[i].fd);
                close(el->_pfds[i].fd);
                el->_pfds[i].fd = -1;
                // npfds--;
            }

            ready--;
        }
    }
}