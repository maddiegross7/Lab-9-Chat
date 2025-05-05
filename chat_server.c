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
}Room;

typedef struct player{
    int fd;
    char *name;
    FILE *stream;
    Room *room;
}Player;

typedef struct server{
    JRB rooms;
}Server;

typedef struct mainThread{
    int fd;
    Server *server;
}MainThread;

void *addPlayer(void *arg){
    MainThread *thread = (MainThread *)arg;

    FILE *reader = fdopen(thread->fd, "r");
    FILE *writer = fdopen(dup(thread->fd), "w");

    fprintf(writer, "Chat Rooms:\n\n\n");

    JRB roomPtr;
    jrb_traverse(roomPtr, thread->server->rooms){
        Room *room = (Room *)jval_v(roomPtr->val);
        fprintf(writer, "%s:", room->name);
        Dllist playerPtr;
        int i = 0;
        dll_traverse(playerPtr, room->players) {
            Player *player = (Player *)jval_v(playerPtr->val);
            if(i > 0) fprintf(writer, ", ");
            fprintf(writer, "%s", player->name);
            i++;
        }
        fprintf(writer, "\n\n");
    }

    fprintf(writer, "Enter your chat name (no spaces):\n");
    if (fflush(writer) == EOF) {
        perror("fflush failed");
    }

    char *name = malloc(256);
    if (fgets(name, sizeof(name), reader) == NULL) {
        if (feof(reader)) {
            fprintf(stderr, "Connection closed by client\n");
        } else {
            perror("fgets failed");
        }
    }
    name[strcspn(name, "\n")] = 0;
    
    fprintf(writer, "Enter chat room:\n");
    if (fflush(writer) == EOF) {
        perror("fflush failed");
    }

    char *roomName = malloc(512);
    if (fgets(roomName, sizeof(roomName), reader) == NULL) {
        if (feof(reader)) {
            fprintf(stderr, "Connection closed by client\n");
        } else {
            perror("fgets failed");
        }
    }
    roomName[strcspn(roomName, "\n")] = 0;

    JRB thisRoom = jrb_find_str(roomName, thread->server->rooms);
    if(thisRoom == NULL){
        fprintf(writer, "Room %s not found\n", roomName);
        fflush(writer);
        free(name);
        free(roomName);
        fclose(reader);
        fclose(writer);
        close(thread->fd);
        free(arg);
        return NULL;
    }

    Player *player = malloc(sizeof(Player));
    player->fd = thread->fd;
    player->name = strdup(name);
    player->room = (Room *) jval_v(thisRoom->val);
    
    dll_append(player->room->players, new_jval_v(player));

    fflush(writer);
    free(arg);
}

int main(int argc, char const *argv[])
{
    if(argc < 3){
        fprintf(stderr, "Usage: %s <port> <chat rooms> ...\n", argv[0]);
        exit(1);
    }
    int port = atoi(argv[1]);

    if(port < 8000){
        fprintf(stderr, "Port number must be greater than 8000\n");
        exit(1);
    }

    Server *server = malloc(sizeof(Server));
    server->rooms = make_jrb();

    for(int i = 2; i < argc; i++){
        Room *room = malloc(sizeof(Room));
        room->name = strdup(argv[i]);
        room->players = new_dllist();
        jrb_insert_str(server->rooms, room->name, new_jval_v(room));
    }

    int sock = serve_socket(port);
    if (sock < 0) {
        perror("serve_socket failed");
        exit(1);
    }

    while(1){
        int fd = accept_connection(sock);
        if (fd < 0) {
            perror("accept_connection failed");
            exit(1);
        }
    
        int *fdptr = malloc(sizeof(int));
        *fdptr = fd;

        MainThread *thread = malloc(sizeof(MainThread));
        thread->fd = fd;
        thread->server = server;

        pthread_t tid;
        pthread_create(&tid, NULL, addPlayer, thread);
        pthread_detach(tid);
    }

    
}
