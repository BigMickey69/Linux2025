#ifndef LOGIN_H
#define LOGIN_H

#include <stdbool.h>

#define MAX_USERNAME_LENGTH 15
#define MAX_PASSWORD_LENGTH 12

// Client–side login functions: these now use the socket descriptor
// to send prompts and receive responses from the client.
int client_loginmenu(int client_fd);        // [CHANGE]: now takes client_fd
void client_createmenu(int client_fd);        // [CHANGE]: now takes client_fd
int client_Homemenu(int client_fd);           // [CHANGE]: now takes client_fd

// Initialize the user database.
void init_db();

// Account management functions.
bool create_account(const char* username, const char* password);
bool del_account(const char* username);

// Sends a welcome banner to the client.
// [CHANGE]: This replaces the server–side printer() with one that sends to the client.
void client_printer(int client_fd);

#endif
