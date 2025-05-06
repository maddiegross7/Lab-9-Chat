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
} Player;

typedef struct server {
    JRB rooms;
} Server;

//I dont think this is really necessary but it works with it so I guess it is staying
typedef struct mainThread {
    int fd;
    Server *server;
} MainThread;

//This is the function to add a new player to the server
void *addPlayer(void *arg) {
    MainThread *thread = (MainThread *)arg;

    //open the file descriptors for reading and writing
    FILE *reader = fdopen(thread->fd, "r");
    if(reader == NULL){
        perror("fdopen failed");
        close(thread->fd);
        free(thread);
        return NULL;
    }
    FILE *writer = fdopen(thread->fd, "w");
    if(writer == NULL){
        perror("fdopen failed");
        fclose(reader);
        close(thread->fd);
        free(thread);
        return NULL;
    }

    fputs("Chat Rooms:\n\n", writer);

    //print out all of the rooms on the server
    JRB roomPtr;
    jrb_traverse(roomPtr, thread->server->rooms) {
        Room *room = (Room *)jval_v(roomPtr->val);
        fputs(room->name, writer);
        fputs(":", writer);
        Dllist playerPtr;
        int i = 0;
        fflush(writer);
        if(!dll_empty(room->players)){
            fputs(" ", writer);
        }
        dll_traverse(playerPtr, room->players) {
            Player *player = (Player *)jval_v(playerPtr->val);
            if (i > 0) fputs(" ", writer);
            fputs(player->name, writer);
            i++;
        }
        fputs("\n", writer);
    }
    fputs("\n", writer);
    fflush(writer);
    //ask user for name
    fputs("Enter your chat name (no spaces):\n", writer);
    fflush(writer);

    //fgetting the name of the user
    char name[4096];
    if (fgets(name, sizeof(name), reader) == NULL) {
        fprintf(stderr, "Client disconnected or name read error\n");
        fclose(reader);
        fclose(writer);
        close(thread->fd);
        free(thread);
        return NULL;
    }
    name[strcspn(name, "\n")] = '\0';//gets rid of the newline character
    //ask for which room thay would like to join
    fputs("Enter chat room:\n", writer);
    fflush(writer);

    //fget the room name and do error checking 
    char roomName[1024];
    if (fgets(roomName, sizeof(roomName), reader) == NULL) {
        fprintf(stderr, "Client disconnected or room read error\n");
        fclose(reader);
        fclose(writer);
        close(thread->fd);
        free(thread);
        return NULL;
    }
    roomName[strcspn(roomName, "\n")] = '\0';
    if (roomName[0] == '\0') {
        fputs("Room name cannot be empty!\n", writer);
        fflush(writer);
        fclose(reader);
        fclose(writer);
        close(thread->fd);
        free(thread);
        return NULL;
    }

    //find it in the jrb tree
    JRB thisRoom = jrb_find_str(thread->server->rooms, roomName);
    if (thisRoom == NULL) {
        fputs("Room ", writer);
        fputs(roomName, writer);
        fputs(" not found\n", writer);
        fflush(writer);
        fclose(reader);
        fclose(writer);
        close(thread->fd);
        free(thread);
        return NULL;
    }

    //create the player struct and assign its values and stuff
    Player *player = malloc(sizeof(Player));
    if (!player) {
        perror("malloc failed for player");
        fclose(reader);
        fclose(writer);
        close(thread->fd);
        free(thread);
        return NULL;
    }

    player->fd = thread->fd;
    player->name = strdup(name);
    if (!player->name) {//doing a bunch of error checking because what is wrong
        perror("strdup failed for name");
        free(player);
        fclose(reader);
        fclose(writer);
        close(thread->fd);
        free(thread);
        return NULL;
    }
    player->room = (Room *)jval_v(thisRoom->val);
    player->reader = reader;
    player->writer = writer;

    //locking the threads so that we can add the player
    pthread_mutex_lock(&player->room->lock);
    dll_append(player->room->players, new_jval_v(player));
    char *welcomeMessage = malloc(strlen(player->name) + 20);
    snprintf(welcomeMessage, strlen(player->name) + 20, "%s has joined\n", player->name);
    dll_append(player->room->messages, new_jval_s(welcomeMessage));
    pthread_cond_signal(&player->room->condition);
    pthread_mutex_unlock(&player->room->lock);

    //fgetting the messages that are sent by the user
    char buffer[2048];
    while (fgets(buffer, sizeof(buffer), reader) != NULL) {
        buffer[strcspn(buffer, "\n")] = '\0';

        size_t total_len = strlen(player->name) + 2 + strlen(buffer) + 2;
        char *message = malloc(total_len);
        if (!message) {
            perror("malloc failed for message");
            continue;
        }

        snprintf(message, total_len, "%s: %s\n", player->name, buffer);

        pthread_mutex_lock(&player->room->lock);
        dll_append(player->room->messages, new_jval_s(message));
        pthread_cond_signal(&player->room->condition);
        pthread_mutex_unlock(&player->room->lock);
    }

    //if the user leaves you need to handle accordingly
    pthread_mutex_lock(&player->room->lock);
    Dllist ptr;
    dll_traverse(ptr, player->room->players) {
        if (jval_v(ptr->val) == player) {
            dll_delete_node(ptr);
            break;
        }
    }
    //and then you need this stupid ass exist message because shit is stupid and dumb and awful
    char *leaveMessage = malloc(strlen(player->name) + 20);
    snprintf(leaveMessage, strlen(player->name) + 20, "%s has left\n", player->name);
    dll_append(player->room->messages, new_jval_s(leaveMessage));
    pthread_cond_signal(&player->room->condition);
    pthread_mutex_unlock(&player->room->lock);

    //close all the stuff and free and shit
    fclose(reader);
    fclose(writer);
    close(thread->fd);
    free(player->name);
    free(player);
    free(thread);

    return NULL;
}

void *initializeRoomThread(void *arg) {
    Room *room = (Room *)arg;
    while (1) {
        //wait for message
        pthread_mutex_lock(&room->lock);
        while (dll_empty(room->messages)) {
            pthread_cond_wait(&room->condition, &room->lock);
        }
        pthread_mutex_unlock(&room->lock);
        //send the messages
        while (!dll_empty(room->messages)) {
            pthread_mutex_lock(&room->lock);
            Dllist messageNode = dll_first(room->messages);
            char *message = (char *)jval_v(messageNode->val);
            
            Dllist player;
            dll_traverse(player, room->players) {
                Player *thisPlayer = (Player *)jval_v(player->val);
                if (thisPlayer->writer != NULL) {
                    if (fputs(message, thisPlayer->writer) == EOF ||
                        fflush(thisPlayer->writer) == EOF) {
                        perror("fputs or fflush failed");
                    }
                }
            }
            dll_delete_node(messageNode);
            free(message);
            pthread_mutex_unlock(&room->lock);
        }
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

    //making all of the rooms
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

    //accept connections and create player threads
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
