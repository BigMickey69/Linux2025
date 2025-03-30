#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "login.h"

int size = 0;
int db_max = 10;

char** user_db;
char** pswd_db;


void flush_client(int client_fd){
    int bytes_available = 0;
    if (ioctl(client_fd, FIONREAD, &bytes_available) < 0)
        return;

    char dummy[128];
    while (bytes_available > 0) {
        int n = read(client_fd, dummy, sizeof(dummy));
        if (n <= 0)
            break;
        if (ioctl(client_fd, FIONREAD, &bytes_available) < 0)
            break;
    }
}


char* find_username(int index){
    return user_db[index];
}


void init_db(){
    user_db = (char**)malloc(sizeof(char*) * db_max);
    pswd_db = (char**)malloc(sizeof(char*) * db_max);
    for (int i = 0; i < db_max; i++){
        user_db[i] = (char*)malloc(MAX_USERNAME_LENGTH);
        pswd_db[i] = (char*)malloc(MAX_PASSWORD_LENGTH);
    }
}

void expand_db(){
    int new_size = db_max * 2;
    user_db = (char**)realloc(user_db, sizeof(char*) * new_size);
    pswd_db = (char**)realloc(pswd_db, sizeof(char*) * new_size);  // [CHANGE]: fixed reallocation for pswd_db
    for (int i = db_max; i < new_size; i++){
        user_db[i] = (char*)malloc(MAX_USERNAME_LENGTH);
        pswd_db[i] = (char*)malloc(MAX_PASSWORD_LENGTH);
    }
    db_max = new_size;
}

static bool check_password(int index, const char* password){
    return strcmp(pswd_db[index], password) == 0;
}

static int find_account(const char* username){
    for (int i = 0; i < size; i++){
        if (strcmp(user_db[i], username) == 0)
            return i;
    }
    return -1;
}

bool create_account(const char* username, const char* password){
    if (find_account(username) != -1)
        return false;  // account already exists
    if (size >= db_max)
        expand_db();
    strcpy(user_db[size], username);
    strcpy(pswd_db[size], password);
    size++;
    return true;
}

bool del_account(const char* username){
    int index = find_account(username);
    if (index == -1)
        return false;
    for (int i = index; i < size - 1; i++){
        strcpy(user_db[i], user_db[i + 1]);
        strcpy(pswd_db[i], pswd_db[i + 1]);
    }
    size--;
    return true;
}

// ===== Utility functions for client socket I/O =====

// Sends a message to the client over the socket.
void client_send(int client_fd, const char *msg) {
    write(client_fd, msg, strlen(msg));
}

// Reads a line from the client into buffer (up to maxlen chars).
// Returns number of bytes read or a negative value on error/disconnect.
static int client_readline(int client_fd, char *buffer, int maxlen) {
    int total = 0;
    while (total < maxlen - 1) {
        char c;
        int n = read(client_fd, &c, 1);
        if (n <= 0) {
            return n; // error or disconnect
        }
        if (c == '\n')
            break;
        buffer[total++] = c;
    }
    buffer[total] = '\0';
    return total;
}

// ===== Client–Side UI Functions =====

// Sends the welcome banner to the client.
void client_printer(int client_fd){    // [CHANGE]: Now sends to client.
    client_send(client_fd, "=============================================\n");
    client_send(client_fd, "         BASIC CHATROOM SERVER\n");
    client_send(client_fd, "=============================================\n");
}

// Prompts the client for username and password over the socket.
// Returns the user’s index on successful login or -1 on exit/disconnect.
int client_loginmenu(int client_fd){   
    char username[MAX_USERNAME_LENGTH];
    char password[MAX_PASSWORD_LENGTH];
    while (1) {
        client_send(client_fd, "Username (or 'exit' to cancel): ");
        if (client_readline(client_fd, username, sizeof(username)) <= 0)
            return -1;
        if (strcmp(username, "exit") == 0)
            return -1;
        int index = find_account(username);
        if (index == -1) {
            client_send(client_fd, "User doesn't exist! (type exit to cancel)\n");
            continue;
        }
        client_send(client_fd, "Password: ");
        if (client_readline(client_fd, password, sizeof(password)) <= 0)
            return -1;
        if (check_password(index, password)){
            client_send(client_fd, "Successfully logged in!\n\n");
            return index;
        } else
            client_send(client_fd, "Wrong password!\n");
    }
}

// Prompts the client to create an account.
void client_createmenu(int client_fd){  // [CHANGE]: Uses client_fd for I/O.
    char username[MAX_USERNAME_LENGTH];
    char password[MAX_PASSWORD_LENGTH];
    char confirm[3];  // for 'y' or 'n'
    while (1) {
        client_send(client_fd, "Enter your desired username (< 15 characters): ");
        if (client_readline(client_fd, username, sizeof(username)) <= 0)
            return;
        client_send(client_fd, "Is your desired name \"");
        client_send(client_fd, username);
        client_send(client_fd, "\"? (y/n): ");
        if (client_readline(client_fd, confirm, sizeof(confirm)) <= 0)
            return;
        if (confirm[0] == 'n' || confirm[0] == 'N')
            continue;
        flush_client(client_fd);
        client_send(client_fd, "Enter your password (< 12 characters): ");
        if (client_readline(client_fd, password, sizeof(password)) <= 0)
            return;
        if (create_account(username, password))
            client_send(client_fd, "Account created successfully!\n");
        else
            client_send(client_fd, "Account creation failed: Username already exists.\n");
        break;
    }
}

// Displays the home menu to the client. Offers options for login,
// account creation, or exit. Returns the account index if login succeeds,
// or -1 if the client chooses to exit.
int client_Homemenu(int client_fd){   // [CHANGE]: Uses client_fd for I/O.
    char choice[10];
    while (1) {
        client_send(client_fd, "=============================================\n");
        client_send(client_fd, "Welcome to BASIC CHATROOM, please enter:\n");
        client_send(client_fd, "    (1) to login\n");
        client_send(client_fd, "    (2) to create an account\n");
        client_send(client_fd, "    (3) to exit\n");
        client_send(client_fd, "Choice: ");
        if (client_readline(client_fd, choice, sizeof(choice)) <= 0)
            return -1;
        if (choice[0] == '1') {
            int index = client_loginmenu(client_fd);
            if (index != -1)
                return index;
        } else if (choice[0] == '2') {
            client_createmenu(client_fd);
        } else if (choice[0] == '3') {
            client_send(client_fd, "Goodbye!\n");
            return -1;
        } else {
            client_send(client_fd, "Invalid choice. Please try again.\n");
        }
    }
}
