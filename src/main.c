#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "pbx.h"
#include "server.h"
#include "debug.h"
#include "csapp.h"

static volatile sig_atomic_t got_hup_signal = 0;

static void terminate(int status);

static void hup_handler(int sig){
    got_hup_signal = 1;
}

/*
 * "PBX" telephone exchange simulation.
 *
 * Usage: pbx <port>
 */
int main(int argc, char* argv[]){
    // Option processing should be performed here.
    // Option '-p <port>' is required in order to specify the port number
    // on which the server should listen.

    // Parse port number.
    if(argc > 3){
        fprintf(stderr, "usage: -p <port>%s", EOL);
        exit(EXIT_SUCCESS);
    }

    char *portno;
    int opt;
    while((opt = getopt(argc, argv, "-:p:")) != -1)
    {
        switch(opt)
        {
            case 'p':
                portno = optarg;
                break;
            default:
                fprintf(stderr, "usage: -p <port>%s", EOL);
                exit(EXIT_SUCCESS);
        }
    }

    // Perform required initialization of the PBX module.
    debug("Initializing PBX...");
    pbx = pbx_init();

    // TODO: Set up the server socket and enter a loop to accept connections
    // on this socket.  For each connection, a thread should be started to
    // run function pbx_client_service().  In addition, you should install
    // a SIGHUP handler, so that receipt of SIGHUP will perform a clean
    // shutdown of the server.

    // Install SIGHUP handler.
    struct sigaction action, old_action;
    action.sa_handler = hup_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGHUP, &action, &old_action);

    // Ignore SIGPIPE
    struct sigaction ignact;
    ignact.sa_handler = SIG_IGN;
    sigemptyset(&ignact.sa_mask);
    ignact.sa_flags = 0;
    sigaction(SIGPIPE, &ignact, NULL);

    int listenfd, *connfdp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    listenfd = Open_listenfd(portno);
    while(1){

        clientlen = sizeof(struct sockaddr_storage);
        connfdp = Malloc(sizeof(int));
        *connfdp = accept(listenfd, (SA *)&clientaddr, &clientlen);
        if(got_hup_signal){
            close(*connfdp);
            free(connfdp);
            break;
        }
        Pthread_create(&tid, NULL, pbx_client_service, connfdp);
    }
    close(listenfd);
    terminate(EXIT_SUCCESS);

    // fprintf(stderr, "You have to finish implementing main() "
	   //  "before the PBX server will function.\n");

    // terminate(EXIT_FAILURE);
}

/*
 * Function called to cleanly shut down the server.
 */
static void terminate(int status) {
    debug("Shutting down PBX...");
    pbx_shutdown(pbx);
    debug("PBX server terminating");
    pthread_exit(NULL);
}
