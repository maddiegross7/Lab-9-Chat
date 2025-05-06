#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "sockettome.h"
#include "dllist.h"
#include "jval.h"
#include "jrb.h"

typedef struct room {
    char *name;
    Dllist players;
    Dllist messages;
    pthread_mutex_t lock;
    pthread_cond_t condition;
} Room;

typedef struct player {
    int fd;
    char *name;
    FILE *reader;
    FILE *writer;
    Room *room;
    pthread_mutex_t writers_block;
} Player;

typedef struct server {
    JRB rooms;
} Server;

typedef struct mainThread {
    int fd;
    Server *server;
} MainThread;

void *addPlayer(void *arg) {
    MainThread *thread = (MainThread *)arg;

    FILE *reader = fdopen(thread->fd, "r");
    FILE *writer = fdopen(dup(thread->fd), "w");

    fprintf(writer, "Chat Rooms:\n\n\n");

    JRB roomPtr;
    jrb_traverse(roomPtr, thread->server->rooms) {
        Room *room = (Room *)jval_v(roomPtr->val);
        fprintf(writer, "%s:", room->name);
        Dllist playerPtr;
        int i = 0;
        dll_traverse(playerPtr, room->players) {
            Player *player = (Player *)jval_v(playerPtr->val);
            if (i > 0) fprintf(writer, ", ");
            fprintf(writer, "%s", player->name);
            i++;
        }
        fprintf(writer, "\n\n");
    }

    fprintf(writer, "Enter your chat name (no spaces):\n");
    fflush(writer);

    char name[4096];
    if (fgets(name, sizeof(name), reader) == NULL) {
        if (feof(reader)) {
            fprintf(stderr, "Connection closed by client\n");
        } else {
            perror("fgets failed");
        }
        fclose(reader);
        fclose(writer);
        close(thread->fd);
        free(arg);
        return NULL;
    }
    name[strcspn(name, "\n")] = 0;

    fprintf(writer, "Enter chat room:\n");
    fflush(writer);

    char roomName[1024];
    if (fgets(roomName, sizeof(roomName), reader) == NULL) {
        if (feof(reader)) {
            fprintf(stderr, "Connection closed by client\n");
        } else {
            perror("fgets failed");
        }
        fclose(reader);
        fclose(writer);
        close(thread->fd);
        free(arg);
        return NULL;
    }
    roomName[strcspn(roomName, "\n")] = 0;

    if (roomName[0] == '\0') {
        fprintf(writer, "Room name cannot be empty!\n");
        fflush(writer);
        fclose(reader);
        fclose(writer);
        close(thread->fd);
        free(arg);
        return NULL;
    }

    JRB thisRoom = jrb_find_str(thread->server->rooms, roomName);
    if (thisRoom == NULL) {
        fprintf(writer, "Room %s not found\n", roomName);
        fflush(writer);
        fclose(reader);
        fclose(writer);
        close(thread->fd);
        free(arg);
        return NULL;
    }

    Player *player = malloc(sizeof(Player));
    if (!player) {
        perror("malloc failed for player");
        fclose(reader);
        fclose(writer);
        close(thread->fd);
        free(arg);
        return NULL;
    }

    player->fd = thread->fd;
    player->name = strdup(name);
    player->room = (Room *)jval_v(thisRoom->val);
    player->reader = reader;
    player->writer = writer;
    pthread_mutex_init(&player->writers_block, NULL);

    dll_append(player->room->players, new_jval_v(player));

    char *buffer = malloc(2048);
    if (!buffer) {
        perror("malloc failed for message buffer");
        free(player->name);
        free(player);
        fclose(reader);
        fclose(writer);
        close(thread->fd);
        free(arg);
        return NULL;
    }

    while (fgets(buffer, 2047, reader) != NULL) {
        buffer[strcspn(buffer, "\n")] = '\0';

        size_t name_len = strlen(player->name);
        size_t buffer_len = strlen(buffer);
        size_t total_len = name_len + 2 + buffer_len + 1;

        char *message = malloc(total_len);
        if (!message) {
            perror("malloc failed for message");
            continue;
        }

        snprintf(message, total_len, "%s: %s", player->name, buffer);

        pthread_mutex_lock(&player->room->lock);
        dll_append(player->room->messages, new_jval_s(message));
        pthread_cond_signal(&player->room->condition);
        pthread_mutex_unlock(&player->room->lock);
    }

    Dllist ptr;
    dll_traverse(ptr, player->room->players) {
        if (jval_v(ptr->val) == player) {
            dll_delete_node(ptr);
            break;
        }
    }

    fflush(writer);
    fflush(reader);
    fclose(reader);
    fclose(writer);
    close(thread->fd);
    free(player->name);
    free(player);
    free(buffer);
    pthread_mutex_destroy(&player->writers_block);
    free(arg);

    return NULL;
}

void *initializeRoomThread(void *arg) {
    Room *room = (Room *)arg;

    while (1) {
        pthread_mutex_lock(&room->lock);
        while (dll_empty(room->messages)) {
            pthread_cond_wait(&room->condition, &room->lock);
        }

        while (!dll_empty(room->messages)) {
            Dllist messageNode = dll_first(room->messages);
            char *message = (char *)jval_v(messageNode->val);
            dll_delete_node(messageNode);

            Dllist player;
            dll_traverse(player, room->players) {
                Player *thisPlayer = (Player *)jval_v(player->val);
                pthread_mutex_lock(&(thisPlayer->writers_block));
                if (thisPlayer->writer != NULL) {
                    if (fputs(message, thisPlayer->writer) == EOF ||
                        fflush(thisPlayer->writer) == EOF) {
                        perror("fputs or fflush failed");
                    }
                }
                pthread_mutex_unlock(&(thisPlayer->writers_block));
            }

            free(message);
        }

        pthread_mutex_unlock(&room->lock);
    }
    return NULL;
}

int main(int argc, char const *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <port> <chat rooms> ...\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    if (port < 8000) {
        fprintf(stderr, "Port number must be greater than 8000\n");
        exit(1);
    }

    Server *server = malloc(sizeof(Server));
    server->rooms = make_jrb();

    for (int i = 2; i < argc; i++) {
        Room *room = malloc(sizeof(Room));
        room->name = strdup(argv[i]);
        room->players = new_dllist();
        room->messages = new_dllist();
        pthread_mutex_init(&room->lock, NULL);
        pthread_cond_init(&room->condition, NULL);
        jrb_insert_str(server->rooms, room->name, new_jval_v(room));

        pthread_t tid;
        pthread_create(&tid, NULL, initializeRoomThread, room);
        pthread_detach(tid);
    }

    int sock = serve_socket(port);
    if (sock < 0) {
        perror("serve_socket failed");
        exit(1);
    }

    while (1) {
        int fd = accept_connection(sock);
        if (fd < 0) {
            perror("accept_connection failed");
            continue;
        }

        MainThread *thread = malloc(sizeof(MainThread));
        thread->fd = fd;
        thread->server = server;

        pthread_t tid;
        pthread_create(&tid, NULL, addPlayer, thread);
        pthread_detach(tid);
    }

    return 0;
}
