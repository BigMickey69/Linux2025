/* chat server using select(2) with client–side UI for login and menus */

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

#include "login.h"

int client_colors[FD_SETSIZE] = {0};
int fd_to_index[FD_SETSIZE] = {0};
// Stack to keep track online fd-s
int online_fd[FD_SETSIZE] = {0};
int user_count = 0;


int main(int argc, char **argv)
{
    init_db();

    if (argc < 2) {
        printf("usage: %s <port>\n", argv[0]);
        exit(1);
    }
    int port = atoi(argv[1]);
    if (port <= 0) {
        printf("'%s' not a valid port number\n", argv[1]);
        exit(1);
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }
    int onoff = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &onoff, sizeof(onoff)) < 0) {
        perror("setsockopt");
        exit(1);
    }
    struct sockaddr_in sin = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = {.s_addr = htonl(INADDR_ANY)},
    };
    if (bind(server_fd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
        perror("bind");
        exit(1);
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(1);
    }
    printf("listening on port %d\n", port);

    int conns[FD_SETSIZE];
    memset(conns, 0, sizeof(conns));
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(server_fd, &rfds);
    int max_fd = server_fd + 1;

    while (select(max_fd, &rfds, NULL, NULL, NULL) >= 0) {
        if (FD_ISSET(server_fd, &rfds)) {
            struct sockaddr_in client_addr;
            socklen_t addrlen = sizeof(client_addr);
            int new_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
            if (new_fd < 0) {
                perror("accept");
            } else {
                printf("[%d] connect from %s:%d\n", new_fd,
                    inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
                
                // Welcome message
                client_printer(new_fd);
                // Show home menu
                int user_index = client_Homemenu(new_fd);
                if (user_index == -1) {
                    close(new_fd);
                    continue;
                }
                fd_to_index[new_fd] = user_index;
                // Addd to online_fd, marking user as online
                online_fd[user_count++] = new_fd;
                printf("[Logged in] fd: %d, index: %d\n", new_fd, user_index);

                int onoff = 1;
                if (ioctl(new_fd, FIONBIO, &onoff) < 0) {
                    printf("fcntl(%d): %s\n", new_fd, strerror(errno));
                    close(new_fd);
                    continue;
                }
                conns[new_fd] = 1;
                // [CHANGE]: Color is now determined by the user’s account index.
                client_colors[new_fd] = 30 + (user_index % 7);
            }
        }
        

        for (int fd = 0; fd < FD_SETSIZE; fd++) {
            if (!conns[fd])
                continue;

            if (FD_ISSET(fd, &rfds)) {
                printf("[%d] activity\n", fd);

                char buf[1024];
                int nread = read(fd, buf, sizeof(buf));
                if (nread < 0) {
                    fprintf(stderr, "read(%d): %s\n", fd, strerror(errno));
                    close(fd);
                    conns[fd] = 0;
                }
                
                else if (nread > 0) {
                    // Making sure that there IS a '\0'
                    buf[nread-1] = '\0';


                    // Check if it's a command
                    if(buf[0] == '/'){
                        char msg[1024];
                        if(strcmp(buf, "/online") == 0){
                            sprintf(msg, "Current online users (%d user(s)): \n", user_count);
                            client_send(fd, msg);
                            for(int i=0; i<user_count; i++) {
                                sprintf(msg, "- %s \n", find_username(fd_to_index[online_fd[i]]));
                                client_send(fd, msg);
                            }
                        }else if(strcmp(buf, "/hello") == 0){
                            client_send(fd, "Why hello!\n");
                        }
                        else{
                            client_send(fd, "Unknown command!\n");
                        }
                        // Since it's a command, we don't send anything to other users
                        continue;
                    }
                    
                    char colored_msg[1024 + 20]; // extra space for escape characters
                    int colored_len = snprintf(colored_msg, sizeof(colored_msg),
                        "\033[47m\033[%dm%s\033[0m\n", client_colors[fd], buf);

                    printf("[%s]: %s\n", find_username(fd_to_index[fd]), colored_msg);


                    for (int dest_fd = 0; dest_fd < FD_SETSIZE; dest_fd++) {
                        if (conns[dest_fd] && dest_fd != fd) {
                            if (write(dest_fd, colored_msg, colored_len) < 0) {
                                fprintf(stderr, "write(%d): %s\n", dest_fd, strerror(errno));
                                close(dest_fd);
                                conns[dest_fd] = 0;
                            }
                        }
                    }
                } else {
                    printf("[%d] closed\n", fd);
                    close(fd);
                    conns[fd] = 0;
                }
            }
        }
        
        // Rebuild the descriptor set for select().
        FD_ZERO(&rfds);
        FD_SET(server_fd, &rfds);
        max_fd = server_fd + 1;
        for (int fd = 0; fd < FD_SETSIZE; fd++) {
            if (conns[fd]) {
                FD_SET(fd, &rfds);
                if (fd + 1 > max_fd)
                    max_fd = fd + 1;
            }
        }
    }
    
    exit(1);
}
