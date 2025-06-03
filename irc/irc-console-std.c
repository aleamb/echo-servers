#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include <unistd.h>

#ifdef EPOLL_H
#include <sys/epoll.h>
#endif


typedef enum  {
    BUFFER_TYPE_GENERAL = 0,
    BUFFER_TYPE_CHANNEL,
    BUFFER_TYPE_QUERY,
    BUFFER_TYPE_SERVER
} BUFFER_TYPE;

typedef struct {
    struct epoll_event ev;
    char name[64];
    BUFFER_TYPE buffer_type;
} BUFFER;

int buffer_create(int epollfd, int fd, BUFFER_TYPE type, BUFFER* buffer) {

    buffer->ev.events = EPOLLIN;
    buffer->ev.data.fd = fd;
    buffer->buffer_type = type;

    return epoll_ctl(epollfd, EPOLL_CTL_ADD, STDIN_FILENO, &buffer->ev);
}

int buffer_prompt(BUFFER* buffer) {
    switch (buffer->buffer_type) {
        case BUFFER_TYPE_GENERAL:
            printf("General>");
            break;
        case BUFFER_TYPE_CHANNEL:
            printf("%s> ", buffer->name);
            break;
        case BUFFER_TYPE_QUERY:
            printf("%s> ", buffer->name);
            break;
        case BUFFER_TYPE_SERVER:
            printf("%s> ", buffer->name);
            break;
    }
    fflush(stdout);
    return 0;
}

BUFFER* buffer_match_fd(int fd, BUFFER *buffers, int buffers_count) {
    for (int i = 0; i < buffers_count; i++) {
        if (buffers[i].ev.data.fd == fd) {
            return &buffers[i];
        }
    }
    return NULL;
}

int buffer_process(BUFFER* buffer, const char* input) {
    switch (buffer->buffer_type) {
        case BUFFER_TYPE_GENERAL:
            if (input[0] != '/') {
                putc('\r', stdout);
                //putc('\b', stdout);
                //fflush(stdout);
                //printf("No message buffer\n");
                fflush(stdout);
            } else {
                printf("Processing general command: %s\n", input);
            }
    }
    return 0;
}


int main() {

    int quit = 0;
    BUFFER buffer;
    BUFFER* active_buffer;
    BUFFER *buffers;
    int epollfd;
    int buffers_count = 0;

     epollfd= epoll_create1(0);
    
    if (epollfd == -1) {
        int err = errno;
        perror("epoll_create1");
        exit(err);
    }

    
    buffers = malloc(sizeof(BUFFER) * 10); // Allocate space for 10 buffers
    buffer_create(epollfd, STDIN_FILENO, BUFFER_TYPE_GENERAL, &buffers[0]);
    buffers_count++;

    while (!quit) {
        buffer_prompt(&buffer);

        struct epoll_event *events;
        events = malloc(sizeof(struct epoll_event) * buffers_count);

        int nfds = epoll_wait(epollfd, events, buffers_count, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(1);
        }
        for (int n = 0; n < nfds; ++n) {
            int fd = events[n].data.fd;
            active_buffer = buffer_match_fd(fd, buffers, buffers_count);
            if (active_buffer == NULL) {
                fprintf(stderr, "No buffer found for fd %d\n", fd);
                continue;
            }

            if (active_buffer->ev.events & EPOLLIN) {
                char input[256];
                ssize_t bytes_read = read(fd, input, sizeof(input) - 1);
                if (bytes_read < 0) {
                    perror("read");
                    continue;
                } else if (bytes_read == 0) {
                    // EOF, close the buffer
                    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
                    continue;
                }
                input[bytes_read] = '\0'; // Null-terminate the string
                buffer_process(active_buffer, input);
            }
        }
        free(events);
    }



    return 0;
}