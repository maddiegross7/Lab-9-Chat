#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sockettome.h"
#include "dllist.h"

typedef struct room {
    char *name;
    Dllist players;
}Room;

typedef struct server{
    Dllist rooms;
}Server;

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
    server->rooms = new_dllist();

    for(int i = 2; i < argc; i++){
        Room *room = malloc(sizeof(Room));
        room->name = strdup(argv[i]);
        room->players = new_dllist();
        dll_append(server->rooms, new_jval_v(room));
    }

    int sock = serve_socket(port);
    if (sock < 0) {
        perror("serve_socket failed");
        exit(1);
    }

    int fd = accept_connection(sock);
    if (fd < 0) {
        perror("accept_connection failed");
        exit(1);
    }

    char *un = getenv("USER");
    printf("Connection established. Sending 'Server: %s'\n", un);
    


    
    return 0;
}
