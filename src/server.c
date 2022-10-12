/*
 * "PBX" server module.
 * Manages interaction with a client telephone unit (TU).
 */
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "pbx.h"
#include "server.h"
#include "csapp.h"

/*
 * Thread function for the thread that handles interaction with a client TU.
 * This is called after a network connection has been made via the main server
 * thread and a new thread has been created to handle the connection.
 */
void *pbx_client_service(void *arg) {

    // Get the file descriptor and free the arg.
    int client_fd = *((int *)arg);
    Pthread_detach(pthread_self());
    free(arg);

    // Create a tu with the fd.
    TU *new_tu;
    if((new_tu = tu_init(client_fd)) == NULL){
        fprintf(stderr, "Failed to initialize tu.\n");
        return NULL;
    }

    // Register tu to pbx.
    if(pbx_register(pbx, new_tu, client_fd) == -1){
        fprintf(stderr, "Failed to register tu.\n");
        return NULL;
    }

    // Read input from client_fd.
    FILE *stream;
    char *buf, c;
    size_t len;

    char *msg;

    int target_ext;
    char *endp;

    char *client_input;

    stream = open_memstream(&buf, &len);

    while(read(client_fd, &c, 1 ) > 0)
    {
        fprintf(stream, "%c", c);
        if(c == '\n'){
            fflush(stream);
            if( (client_input = (char *) malloc((len+1)*sizeof(char))) == NULL){
                return NULL;
            }
            memcpy(client_input, buf, len-strlen(EOL));
            *(client_input+len-strlen(EOL)) = 0;
            fclose(stream);
            free(buf);

            // parse client input.
            if( (strcmp(client_input, tu_command_names[TU_PICKUP_CMD]) == 0)){
                if(tu_pickup(new_tu) < 0)
                    ;
            }
            else if(strcmp(client_input, tu_command_names[TU_HANGUP_CMD]) == 0){
                if(tu_hangup(new_tu) < 0)
                    ;
            }
            else if(strncmp(client_input, tu_command_names[TU_DIAL_CMD], strlen(tu_command_names[TU_DIAL_CMD])) == 0){
                if((*(client_input+strlen(tu_command_names[TU_DIAL_CMD])) == ' ') && *(client_input+strlen(tu_command_names[TU_DIAL_CMD])+1) != 0)
                {
                    target_ext = (int)strtol(client_input+strlen(tu_command_names[TU_DIAL_CMD])+1, &endp, 10);
                    //if(*endp == 0){
                    if(pbx_dial(pbx, new_tu, target_ext) < 0)
                        ;
                    //}
                }
            }
            else if(strncmp(client_input, tu_command_names[TU_CHAT_CMD], strlen(tu_command_names[TU_CHAT_CMD])) == 0){
                for(msg=client_input+strlen(tu_command_names[TU_CHAT_CMD]); *msg==' '; msg++)
                    ;
                if(tu_chat(new_tu, msg) < 0)
                    ;
            }

            free(client_input);
            // reopen memstream
            stream = open_memstream(&buf, &len);
        }

    }
    fclose(stream);
    free(buf);

    close(client_fd);

    pbx_unregister(pbx, new_tu);

    return NULL;


    // TO BE IMPLEMENTED
    //abort();
}
