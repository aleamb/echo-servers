#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/select.h>


typedef enum  {
    BUFFER_TYPE_GENERAL = 0,
    BUFFER_TYPE_CHANNEL,
    BUFFER_TYPE_QUERY,
    BUFFER_TYPE_SERVER
} BUFFER_TYPE;

typedef struct {
    char name[64];
    int fd;
    BUFFER_TYPE buffer_type;
} BUFFER;

BUFFER* buffer_create(int fd, const char *name, BUFFER_TYPE type) {
    BUFFER* buffer = malloc(sizeof(BUFFER));
    buffer->buffer_type = type;
    buffer->fd = fd;
    strcpy(buffer->name, name);
    return buffer;
}

int buffer_prompt(BUFFER* buffer) {

    printf("%s>", buffer->name);

    switch (buffer->buffer_type) {
        case BUFFER_TYPE_GENERAL:
            break;
        case BUFFER_TYPE_CHANNEL:
            break;
        case BUFFER_TYPE_QUERY:
            break;
        case BUFFER_TYPE_SERVER:
            break;
    }
    fflush(stdout);
    return 0;
}

BUFFER* buffer_match_fd(int fd, BUFFER *buffers, int buffers_count) {
    for (int i = 0; i < buffers_count; i++) {
    }
    return NULL;
}

int buffer_process(BUFFER* buffer, const char* input) {
    switch (buffer->buffer_type) {
        case BUFFER_TYPE_GENERAL:
            if (input[0] != '/') {
                printf("\x1b[1;A");
                printf("No message buffer\n");
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
    int buffers_count = 0;
    fd_set fds;

    FD_ZERO(&fds);

    buffers = (BUFFER*)malloc(sizeof(BUFFER) * 10);

    active_buffer = buffer_create(STDIN_FILENO, "General", BUFFER_TYPE_GENERAL);
    FD_SET(STDIN_FILENO, &fds);
    memcpy(&buffers[buffers_count++], active_buffer, sizeof(BUFFER));

    while (!quit) {
        buffer_prompt(active_buffer);

        select(buffers_count, &fds, NULL, NULL, NULL);

        for (int n = 0; n < buffers_count; n++) {

            int fd = buffers[n].fd;

            if (FD_ISSET(fd, &fds)){
                
                char input[256];
                ssize_t bytes_read = read(fd, input, sizeof(input) - 1);
                
                if (bytes_read < 0) {
                    perror("read");
                    continue;
                } else if (bytes_read == 0) {
                    printf("EOF on fd %d\n", fd);
                    quit = 1;
                    break;
                }

                buffer_process(active_buffer, input);
            }
        }
       
    }



    return 0;
}