#ifndef LOGIN_H
#define LOGIN_H

#include <stdbool.h>

#define MAX_USERNAME_LENGTH 15
#define MAX_PASSWORD_LENGTH 12


char* find_username(int index);
/* These use the socket descriptor
 * to send prompts and receive responses from the client.*/
int client_loginmenu(int client_fd);
void client_createmenu(int client_fd);
int client_Homemenu(int client_fd);

void client_send(int client_fd, const char *msg);

// Initialize the user database.
void init_db();

// Account management functions.
bool create_account(const char* username, const char* password);
bool del_account(const char* username);

// Sends a welcome banner
void client_printer(int client_fd);

#endif
