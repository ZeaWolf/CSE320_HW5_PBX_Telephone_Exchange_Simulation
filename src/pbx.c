/*
 * PBX: simulates a Private Branch Exchange.
 */
#include <stdlib.h>
#include <semaphore.h>

#include "pbx.h"
#include "debug.h"
#include "csapp.h"

/* The actual structure definitions.*/
// typedef struct pbx_node{
//     struct pbx_node *prev;
//     struct pbx_node *next;
//     int extno;
//     TU *tu_ptr;
// }PBX_NODE;

typedef struct pbx{
    TU *tu_storage[PBX_MAX_EXTENSIONS];
    sem_t mutex;
    sem_t shutdown_flag;
    int active_tu;
}PBX;

/*
 * Initialize a new PBX.
 *
 * @return the newly initialized PBX, or NULL if initialization fails.
 */
PBX *pbx_init() {

    /* Initialize the pbx. */
    PBX *pbx_storage;
    if( (pbx_storage = (PBX *)calloc(1, sizeof(PBX))) == NULL ){
        return NULL;
    }
    sem_init(&(pbx_storage->mutex), 0, 1);
    sem_init(&(pbx_storage->shutdown_flag), 0, 1);
    pbx_storage->active_tu = 0;
    return pbx_storage;
}

/*
 * Shut down a pbx, shutting down all network connections, waiting for all server
 * threads to terminate, and freeing all associated resources.
 * If there are any registered extensions, the associated network connections are
 * shut down, which will cause the server threads to terminate.
 * Once all the server threads have terminated, any remaining resources associated
 * with the PBX are freed.  The PBX object itself is freed, and should not be used again.
 *
 * @param pbx  The PBX to be shut down.
 */
void pbx_shutdown(PBX *pbx) {

    /* Loop through pbx. */
    int i, tufd;
    TU *current_tu;
    for(i=0; i<PBX_MAX_EXTENSIONS; i++){
        if((current_tu=pbx->tu_storage[i])!=NULL){
            tufd=tu_fileno(current_tu);
            shutdown(tufd, SHUT_RDWR);
        }
    }

    // wait for semaphore here, when count = 0, call post inside unregister
    // sem_wait
    P(&(pbx->shutdown_flag));
    free(pbx);
}

/*
 * Register a telephone unit with a PBX at a specified extension number.
 * This amounts to "plugging a telephone unit into the PBX".
 * The TU is initialized to the TU_ON_HOOK state.
 * The reference count of the TU is increased and the PBX retains this reference
 *for as long as the TU remains registered.
 * A notification of the assigned extension number is sent to the underlying network
 * client.
 *
 * @param pbx  The PBX registry.
 * @param tu  The TU to be registered.
 * @param ext  The extension number on which the TU is to be registered.
 * @return 0 if registration succeeds, otherwise -1.
 */
int pbx_register(PBX *pbx, TU *tu, int ext) {
    P(&(pbx->mutex));
    if(tu_set_extension(tu, ext) < 0){
        V(&(pbx->mutex));
        return -1;
    }
    /* Loop the array and store tu in an empty slot. */
    int i;
    for(i=0; i<PBX_MAX_EXTENSIONS; i++){
        if(pbx->tu_storage[i]==NULL){
            pbx->tu_storage[i]=tu;
            tu_ref(tu, "TU registered to pbx.");\
            // Write ON HOOK 4
            if(pbx->active_tu == 0){
                P(&(pbx->shutdown_flag));
            }
            (pbx->active_tu)++;
            V(&(pbx->mutex));
            return 0;
        }
    }
    V(&(pbx->mutex));
    return -1;

}

/*
 * Unregister a TU from a PBX.
 * This amounts to "unplugging a telephone unit from the PBX".
 * The TU is disassociated from its extension number.
 * Then a hangup operation is performed on the TU to cancel any
 * call that might be in progress.
 * Finally, the reference held by the PBX to the TU is released.
 *
 * @param pbx  The PBX.
 * @param tu  The TU to be unregistered.
 * @return 0 if unregistration succeeds, otherwise -1.
 */
//#if 0
int pbx_unregister(PBX *pbx, TU *tu) {
    P(&(pbx->mutex));
    /* Loop through pbx. */
    int i;
    for(i=0; i<PBX_MAX_EXTENSIONS; i++){
        if(pbx->tu_storage[i]==tu){
            tu_hangup(tu);
            pbx->tu_storage[i]=NULL;
            tu_unref(tu, "TU unregistered from pbx.");

            // if active tu == 0, post(semaphore)
            (pbx->active_tu)--;
            if(pbx->active_tu == 0){
                V(&(pbx->shutdown_flag));
            }

            V(&(pbx->mutex));
            return 0;
        }
    }
    V(&(pbx->mutex));
    return -1;
}


/*
 * Use the PBX to initiate a call from a specified TU to a specified extension.
 *
 * @param pbx  The PBX registry.
 * @param tu  The TU that is initiating the call.
 * @param ext  The extension number to be called.
 * @return 0 if dialing succeeds, otherwise -1.
 */
int pbx_dial(PBX *pbx, TU *tu, int ext) {
    P(&(pbx->mutex));
    TU *src=NULL, *dst=NULL;
    /* Loop through pbx. */
    int i;
    for(i=0; i<PBX_MAX_EXTENSIONS; i++){
        if(pbx->tu_storage[i]!=NULL){
            if(pbx->tu_storage[i]==tu)
                src = pbx->tu_storage[i];
            if(tu_extension(pbx->tu_storage[i])==ext)
                dst = pbx->tu_storage[i];
        }
    }

    if(src == NULL){
        V(&(pbx->mutex));
        return -1;
    }

    if(tu_dial(src, dst) < 0){
        V(&(pbx->mutex));
        return -1;
    }

    V(&(pbx->mutex));
    return 0;
}

