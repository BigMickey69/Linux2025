/* chat server using select(2) */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

int client_colors[FD_SETSIZE] = {0};
int user_cnt = 0;





int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    if (port <= 0) {
        printf("'%s' not a valid port number\n", argv[1]);
        exit(1);
    }

    /* Create the server socket */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }

    /* Enable reuse of the listening address.
     * This option slightly reduces TCP's safety (due to several nuanced
     * issues), but it simplifies restarting the program without delay.
     */
    int onoff = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &onoff, sizeof(onoff)) <
        0) {
        perror("setsockopt");
        exit(1);
    }

    /* Configure the address structure for binding; this binds to all interfaces
     * on the specified port
     */
    struct sockaddr_in sin = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = {.s_addr = htonl(INADDR_ANY)},
    };

    /* Bind the server socket to the specified address */
    if (bind(server_fd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
        perror("bind");
        exit(1);
    }

    /* Mark the socket as passive, ready to accept incoming connections */
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(1);
    }

    printf("listening on port %d\n", port);

    /* Allocate an array to track active connections.
     * In a full-featured server, this would be a mapping from file descriptor
     * to connection object. Here, we only need to know whether a descriptor is
     * connected, so an integer (acting as a boolean) is sufficient: if
     * conns[fd] is true, then the descriptor fd is currently connected.
     */


    int conns[FD_SETSIZE];
    memset(&conns, 0, sizeof(conns));

    /* Create an fd_set to monitor descriptors for readability */
    fd_set rfds;
    FD_ZERO(&rfds);

    /* Add the server socket to the set. When it becomes "readable", it
     * indicates an incoming connection */
    FD_SET(server_fd, &rfds);

    /* Determine the highest-numbered descriptor in the set for select().
     * While FD_SETSIZE is small (typically 1024), we still track the maximum
     * descriptor.
     */
    int max_fd = server_fd + 1;

    /* Main I/O loop:
     * Call select() to monitor the set of descriptors. After select() returns,
     * only the descriptors with activity will remain set in rfds.
     */
    while (select(max_fd, &rfds, NULL, NULL, NULL) >= 0) {
        /* Check if the server socket has become readable, which indicates an
         * incoming connection */
        if (FD_ISSET(server_fd, &rfds)) {
            /* Prepare a structure to store the client's address */
            struct sockaddr_in sin;
            socklen_t sinlen = sizeof(sin);

            /* Accept the incoming connection */
            int new_fd = accept(server_fd, (struct sockaddr *) &sin, &sinlen);
            if (new_fd < 0) {
                perror("accept");
            } else {
                /* Log the new connection's details */
                printf("[%d] connect from %s:%d\n", new_fd,
                       inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));

                /* Set the new socket to non-blocking mode.
                 * This is necessary because a disconnected socket might still
                 * become readable, and a blocking read() would hang
                 * indefinitely. In non-blocking mode, read() will return 0
                 * immediately on a disconnected socket, allowing us to handle
                 * it properly.
                 */
                int onoff = 1;
                if (ioctl(new_fd, FIONBIO, &onoff) < 0) {
                    printf("fcntl(%d): %s\n", new_fd, strerror(errno));
                    close(new_fd);
                    continue;
                }

                /* Record the new connection.
                 * In a full-featured server, you might create a connection or
                 * user object, send a greeting, start authentication, etc.
                 */
                conns[new_fd] = 1;

                // assign a color to this user
                client_colors[new_fd] = 30 + user_cnt++ % 7;
                
            }
        }

        /* Iterate through all file descriptors to check for activity on each
         * connection */
        for (int fd = 0; fd < FD_SETSIZE; fd++) {
            /* skip if no connection */
            if (!conns[fd])
                continue;

            /* is their activity on their fd? */
            if (FD_ISSET(fd, &rfds)) {
                /* yes! */
                printf("[%d] activity\n", fd);

                /* create a buffer to read into */
                char buf[1024];
                int nread = read(fd, buf, sizeof(buf));

                /* see how much we read */
                if (nread < 0) {
                    /* less then zero is some error. disconnect them */
                    fprintf(stderr, "read(%d): %s\n", fd, strerror(errno));
                    close(fd);
                    conns[fd] = 0;
                }

                else if (nread > 0) {
                    /* we got some stuff from them! */
                    
                    buf[nread-1] = '\0';

                    char colored_msg[1024 + 20]; // extra space for escape characters
                    int colored_len = snprintf(colored_msg, sizeof(colored_msg), "\033[47m\033[%dm%.*s\033[0m\n", client_colors[fd], nread, buf);

                    
                    printf("[%d] read: %.*s\n", fd, colored_len, colored_msg);

                    /* loop over all our connections, and send stuff onto them!
                     */
                    for (int dest_fd = 0; dest_fd < FD_SETSIZE; dest_fd++) {
                        /* take active connections, but not ourselves */
                        if (conns[dest_fd] && dest_fd != fd) {
                            /* write to them */
                            if (write(dest_fd, colored_msg, colored_len) < 0) {
                                /* disconnect if it fails; they might have
                                 * legitimately gone away without telling us */
                                fprintf(stderr, "write(%d): %s\n", dest_fd,
                                        strerror(errno));
                                close(dest_fd);
                                conns[dest_fd] = 0;
                            }
                        }
                    }
                }

                /* zero byes read */
                else {
                    /* so they gracefully disconnected and we should forget them
                     */
                    printf("[%d] closed\n", fd);
                    close(fd);
                    conns[fd] = 0;
                }
            }
        }

        /* we've processed all activity, so now we need to set up the descriptor
         * set again (remember, select() removes descriptors that had no
         * activity) */
        FD_ZERO(&rfds);

        /* add the server */
        FD_SET(server_fd, &rfds);
        max_fd = server_fd + 1;

        /* and all the active connections */
        for (int fd = 0; fd < FD_SETSIZE; fd++) {
            if (conns[fd]) {
                FD_SET(fd, &rfds);
                max_fd = fd + 1;
            }
        }
    }

    exit(1);
}