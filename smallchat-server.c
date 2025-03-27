/* smallchat.c -- Read clients input, send to all the other connected clients.
 *
 * Copyright (c) 2023, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the project name of nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/select.h>
#include <unistd.h>
#include <time.h>

#include "chatlib.h"

/* ============================ Data structures =================================
 * The minimal stuff we can afford to have. This example must be simple
 * even for people that don't know a lot of C.
 * =========================================================================== */

#define MAX_CLIENTS 1000 // This is actually the higher file descriptor.
#define SERVER_PORT 7711

/* This structure represents a connected client. There is very little
 * info about it: the socket descriptor and the nick name, if set, otherwise
 * the first byte of the nickname is set to 0 if not set.
 * The client can set its nickname with /nick <nickname> command. */
struct client {
    int fd;     // Client socket.
    char *nick; // Nickname of the client.
    int password_verified; // Whether client has verified password
    char *color; // ANSI color code for client messages
};

/* This global structure encapsulates the global state of the chat. */
struct chatState {
    int serversock;     // Listening server socket.
    int numclients;     // Number of connected clients right now.
    int maxclient;      // The greatest 'clients' slot populated.
    struct client *clients[MAX_CLIENTS]; // Clients are set in the corresponding
                                         // slot of their socket descriptor.
};

struct chatState *Chat; // Initialized at startup.

/* ====================== Small chat core implementation ========================
 * Here the idea is very simple: we accept new connections, read what clients
 * write us and fan-out (that is, send-to-all) the message to everybody
 * with the exception of the sender. And that is, of course, the most
 * simple chat system ever possible.
 * =========================================================================== */

/* Create a new client bound to 'fd'. This is called when a new client
 * connects. As a side effect updates the global Chat state. */
struct client *createClient(int fd) {
    char nick[32]; // Used to create an initial nick for the user.
    int nicklen = snprintf(nick,sizeof(nick),"user:%d",fd);
    struct client *c = chatMalloc(sizeof(*c));
    socketSetNonBlockNoDelay(fd); // Pretend this will not fail.
    c->fd = fd;
    c->nick = chatMalloc(nicklen+1);
    memcpy(c->nick,nick,nicklen);
    c->password_verified = 0; // Initially not verified
    c->color = chatMalloc(8); // Default color code length
    strcpy(c->color, "\033[0m"); // Default color (no color)
    assert(Chat->clients[c->fd] == NULL); // This should be available.
    Chat->clients[c->fd] = c;
    /* We need to update the max client set if needed. */
    if (c->fd > Chat->maxclient) Chat->maxclient = c->fd;
    Chat->numclients++;
    return c;
}

/* Free a client, associated resources, and unbind it from the global
 * state in Chat. */
void freeClient(struct client *c) {
    free(c->nick);
    free(c->color); // Free the color string
    close(c->fd);
    Chat->clients[c->fd] = NULL;
    Chat->numclients--;
    if (Chat->maxclient == c->fd) {
        /* Ooops, this was the max client set. Let's find what is
         * the new highest slot used. */
        int j;
        for (j = Chat->maxclient-1; j >= 0; j--) {
            if (Chat->clients[j] != NULL) {
                Chat->maxclient = j;
                break;
            }
        }
        if (j == -1) Chat->maxclient = -1; // We no longer have clients.
    }
    free(c);
}

/* Allocate and init the global stuff. */
void initChat(void) {
    Chat = chatMalloc(sizeof(*Chat));
    memset(Chat,0,sizeof(*Chat));
    /* No clients at startup, of course. */
    Chat->maxclient = -1;
    Chat->numclients = 0;

    /* Create our listening socket, bound to the given port. This
     * is where our clients will connect. */
    Chat->serversock = createTCPServer(SERVER_PORT);
    if (Chat->serversock == -1) {
        perror("Creating listening socket");
        exit(1);
    }
}

/* Send the specified string to all connected clients but the one
 * having as socket descriptor 'excluded'. If you want to send something
 * to every client just set excluded to an impossible socket: -1. */
void sendMsgToAllClientsBut(int excluded, char *s, size_t len) {
    struct client *sender = NULL;
    if (excluded >= 0 && excluded <= Chat->maxclient) {
        sender = Chat->clients[excluded];
    }
    for (int j = 0; j <= Chat->maxclient; j++) {
        if (Chat->clients[j] == NULL ||
            Chat->clients[j]->fd == excluded) continue;

        /* Important: we don't do ANY BUFFERING. We just use the kernel
         * socket buffers. If the content does not fit, we don't care.
         * This is needed in order to keep this program simple. */
        /* Add terminal notification sequences before the message */
        // write(Chat->clients[j]->fd,s,len);
        /* 发送终端控制序列来增强通知效果:
        * \a - 响铃
        * \033]0;...007 - 设置终端标题
        * \033[1m - 高亮显示
        * \033[0m - 重置所有属性 */
        char timebuf[32];
        time_t now = time(NULL);
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
        char notify_msg[256];
        snprintf(notify_msg, sizeof(notify_msg), "\a\033]0;New Message!\007\033[1mNew Message! [%s]\033[0m\n", timebuf);
        write(Chat->clients[j]->fd, notify_msg, strlen(notify_msg));
        
        /* Format message with sender's color and bold style */
        char formatted_msg[512];
        if (sender) {
            snprintf(formatted_msg, sizeof(formatted_msg), "%s\033[1m%s\033[0m\n", sender->color, s);
            write(Chat->clients[j]->fd, formatted_msg, strlen(formatted_msg));
        } else {
            /* If no sender (system message), use default formatting */
            write(Chat->clients[j]->fd, s, len);
        }
    }
}

/* The main() function implements the main chat logic:
 * 1. Accept new clients connections if any.
 * 2. Check if any client sent us some new message.
 * 3. Send the message to all the other clients. */
int main(void) {
    initChat();

    while(1) {
        fd_set readfds;
        struct timeval tv;
        int retval;

        FD_ZERO(&readfds);
        /* When we want to be notified by select() that there is
         * activity? If the listening socket has pending clients to accept
         * or if any other client wrote anything. */
        FD_SET(Chat->serversock, &readfds);

        for (int j = 0; j <= Chat->maxclient; j++) {
            if (Chat->clients[j]) FD_SET(j, &readfds);
        }

        /* Set a timeout for select(), see later why this may be useful
         * in the future (not now). */
        tv.tv_sec = 1; // 1 sec timeout
        tv.tv_usec = 0;

        /* Select wants as first argument the maximum file descriptor
         * in use plus one. It can be either one of our clients or the
         * server socket itself. */
        int maxfd = Chat->maxclient;
        if (maxfd < Chat->serversock) maxfd = Chat->serversock;
        retval = select(maxfd+1, &readfds, NULL, NULL, &tv);
        if (retval == -1) {
            perror("select() error");
            exit(1);
        } else if (retval) {

            /* If the listening socket is "readable", it actually means
             * there are new clients connections pending to accept. */
            if (FD_ISSET(Chat->serversock, &readfds)) {
                int fd = acceptClient(Chat->serversock);
                struct client *c = createClient(fd);
                /* Send a welcome message. */
                char *prompt = "Please enter password to join chat:\n";
                write(c->fd,prompt,strlen(prompt));
                printf("New client connection fd=%d, waiting for password\n", fd);
            }

            /* Here for each connected client, check if there are pending
             * data the client sent us. */
            char readbuf[256];
            for (int j = 0; j <= Chat->maxclient; j++) {
                if (Chat->clients[j] == NULL) continue;
                if (FD_ISSET(j, &readfds)) {
                    /* Here we just hope that there is a well formed
                     * message waiting for us. But it is entirely possible
                     * that we read just half a message. In a normal program
                     * that is not designed to be that simple, we should try
                     * to buffer reads until the end-of-the-line is reached. */
                    int nread = read(j,readbuf,sizeof(readbuf)-1);

                    if (nread <= 0) {
                        /* Error or short read means that the socket
                         * was closed. */
                        printf("Disconnected client fd=%d, nick=%s\n",
                            j, Chat->clients[j]->nick);
                        freeClient(Chat->clients[j]);
                    } else {
                        /* The client sent us a message. We need to
                         * relay this message to all the other clients
                         * in the chat. */
                        struct client *c = Chat->clients[j];
                        readbuf[nread] = 0;

                        /* Check if client needs password verification */
                        if (!c->password_verified) {
                            /* Remove trailing newlines */
                            char *p;
                            p = strchr(readbuf,'\r'); if (p) *p = 0;
                            p = strchr(readbuf,'\n'); if (p) *p = 0;

                            /* Verify password (using "password123" as example) */
                            if (strcmp(readbuf,"password123") == 0) {
                                c->password_verified = 1;
                                char *welcome = "Password correct! Welcome to Simple Chat!\n"
                                            "Available commands:\n"
                                            "  /nick <nickname> - Set your nickname\n"
                                            "  /color <color> - Set your message color (red, green, yellow, blue, magenta, cyan, white, reset)\n";
                                write(c->fd,welcome,strlen(welcome));
                                printf("Client fd=%d password verified\n", j);
                            } else {
                                char *error = "Wrong password! Connection closed.\n";
                                write(c->fd,error,strlen(error));
                                printf("Client fd=%d failed password verification\n", j);
                                freeClient(c);
                            }
                            continue;
                        }
                        /* If the user message starts with "/", we
                         * process it as a client command. So far
                         * only the /nick <newnick> command is implemented. */
                        if (readbuf[0] == '/') {
                            /* Remove any trailing newline. */
                            char *p;
                            p = strchr(readbuf,'\r'); if (p) *p = 0;
                            p = strchr(readbuf,'\n'); if (p) *p = 0;
                            /* Check for an argument of the command, after
                             * the space. */
                            char *arg = strchr(readbuf,' ');
                            if (arg) {
                                *arg = 0; /* Terminate command name. */
                                arg++; /* Argument is 1 byte after the space. */
                            }

                            if (!strcmp(readbuf,"/nick") && arg) {
                                free(c->nick);
                                int nicklen = strlen(arg);
                                c->nick = chatMalloc(nicklen+1);
                                memcpy(c->nick,arg,nicklen+1);
                            } else if (!strcmp(readbuf,"/color") && arg) {
                                /* Handle color command */
                                char *color_code = NULL;
                                
                                /* Map color names to ANSI color codes */
                                if (!strcmp(arg,"red")) {
                                    color_code = "\033[31m";
                                } else if (!strcmp(arg,"green")) {
                                    color_code = "\033[32m";
                                } else if (!strcmp(arg,"yellow")) {
                                    color_code = "\033[33m";
                                } else if (!strcmp(arg,"blue")) {
                                    color_code = "\033[34m";
                                } else if (!strcmp(arg,"magenta")) {
                                    color_code = "\033[35m";
                                } else if (!strcmp(arg,"cyan")) {
                                    color_code = "\033[36m";
                                } else if (!strcmp(arg,"white")) {
                                    color_code = "\033[37m";
                                } else if (!strcmp(arg,"reset") || !strcmp(arg,"default")) {
                                    color_code = "\033[0m";
                                } else {
                                    /* Invalid color */
                                    char *errmsg = "Invalid color. Available colors: red, green, yellow, blue, magenta, cyan, white, reset\n";
                                    write(c->fd,errmsg,strlen(errmsg));
                                    continue;
                                }
                                
                                /* Set the client's color */
                                free(c->color);
                                c->color = chatMalloc(strlen(color_code)+1);
                                strcpy(c->color, color_code);
                                
                                /* Confirm color change */
                                char confirm[128];
                                snprintf(confirm, sizeof(confirm), "Color set to %s. %sThis is how your messages will look.\033[0m\n", arg, color_code);
                                write(c->fd, confirm, strlen(confirm));
                            } else {
                                /* Unsupported command. Send an error. */
                                char *errmsg = "Unsupported command. Available commands: /nick <nickname>, /color <color>\n";
                                write(c->fd,errmsg,strlen(errmsg));
                            }
                        } else {
                            /* Create a message to send everybody (and show
                             * on the server console) in the form:
                             *   nick> some message. */
                            char msg[256];
                            int msglen = snprintf(msg, sizeof(msg),
                                "%s: %s", c->nick, readbuf);

                            /* snprintf() return value may be larger than
                             * sizeof(msg) in case there is no room for the
                             * whole output. */
                            if (msglen >= (int)sizeof(msg))
                                msglen = sizeof(msg)-1;
                            printf("%s",msg);

                            /* Send it to all the other clients. */
                            sendMsgToAllClientsBut(j,msg,msglen);
                        }
                    }
                }
            }
        } else {
            /* Timeout occurred. We don't do anything right now, but in
             * general this section can be used to wakeup periodically
             * even if there is no clients activity. */
        }
    }
    return 0;
}
