/*
 * TU: simulates a "telephone unit", which interfaces a client with the PBX.
 */
#include <stdlib.h>
#include <semaphore.h>

#include "pbx.h"
#include "debug.h"
#include "csapp.h"

int report_current_state(TU *tu);

/* The actual structure definitions.*/
typedef struct tu{
    int refcnt;
    int extno;
    int tufd;
    TU_STATE state;
    TU *peer;
    sem_t mutex;
}TU;

/* Response the current stare of tu to client. */
int report_current_state(TU *tu){

    if(fcntl(tu->tufd, F_GETFD) == -1){
        return -1;
    }

    switch(tu->state)
    {
        case TU_ON_HOOK:
            dprintf(tu->tufd, "%s %d%s", tu_state_names[TU_ON_HOOK], tu->extno, EOL);
            break;

        case TU_RINGING:
            dprintf(tu->tufd, "%s%s", tu_state_names[TU_RINGING], EOL);
            break;

        case TU_DIAL_TONE:
            dprintf(tu->tufd, "%s%s", tu_state_names[TU_DIAL_TONE], EOL);
            break;

        case TU_RING_BACK:
            dprintf(tu->tufd, "%s%s", tu_state_names[TU_RING_BACK], EOL);
            break;

        case TU_BUSY_SIGNAL:
            dprintf(tu->tufd, "%s%s", tu_state_names[TU_BUSY_SIGNAL], EOL);
            break;

        case TU_CONNECTED:
            dprintf(tu->tufd, "%s %d%s", tu_state_names[TU_CONNECTED], tu_extension(tu->peer), EOL);
            break;

        case TU_ERROR:
            dprintf(tu->tufd, "%s%s", tu_state_names[TU_ERROR], EOL);
            break;

        default:
            break;

    }

    return 0;
}

/*
 * Initialize a TU
 *
 * @param fd  The file descriptor of the underlying network connection.
 * @return  The TU, newly initialized and in the TU_ON_HOOK state, if initialization
 * was successful, otherwise NULL.
 */
TU *tu_init(int fd) {
    TU *telunit;
    if( (telunit = (TU *)malloc(sizeof(TU))) == NULL ){
        return NULL;
    }
    telunit->refcnt=0;
    telunit->extno=-1;
    telunit->tufd=fd;
    telunit->state=TU_ON_HOOK;
    telunit->peer=NULL;
    sem_init(&(telunit->mutex), 0, 1);
    return telunit;
}

/*
 * Increment the reference count on a TU.
 *
 * @param tu  The TU whose reference count is to be incremented
 * @param reason  A string describing the reason why the count is being incremented
 * (for debugging purposes).
 */
void tu_ref(TU *tu, char *reason) {
    if(tu==NULL)
        return;

    tu->refcnt = tu->refcnt + 1;
    return;
}

/*
 * Decrement the reference count on a TU, freeing it if the count becomes 0.
 *
 * @param tu  The TU whose reference count is to be decremented
 * @param reason  A string describing the reason why the count is being decremented
 * (for debugging purposes).
 */
void tu_unref(TU *tu, char *reason) {
    if(tu==NULL)
        return;

    tu->refcnt = tu->refcnt - 1;
    if(tu->refcnt <= 0)
        free(tu);
    return;
}

/*
 * Get the file descriptor for the network connection underlying a TU.
 * This file descriptor should only be used by a server to read input from
 * the connection.  Output to the connection must only be performed within
 * the PBX functions.
 *
 * @param tu
 * @return the underlying file descriptor, if any, otherwise -1.
 */
int tu_fileno(TU *tu) {
    if(tu==NULL)
        return -1;
    return tu->tufd;
}

/*
 * Get the extension number for a TU.
 * This extension number is assigned by the PBX when a TU is registered
 * and it is used to identify a particular TU in calls to tu_dial().
 * The value returned might be the same as the value returned by tu_fileno(),
 * but is not necessarily so.
 *
 * @param tu
 * @return the extension number, if any, otherwise -1.
 */
int tu_extension(TU *tu) {
    if(tu==NULL)
        return -1;
    return tu->extno;
}

/*
 * Set the extension number for a TU.
 * A notification is set to the client of the TU.
 * This function should be called at most once one any particular TU.
 *
 * @param tu  The TU whose extension is being set.
 */
int tu_set_extension(TU *tu, int ext) {
    if(tu==NULL)
        return -1;

    P(&(tu->mutex));
    tu->extno=ext;
    report_current_state(tu);
    V(&(tu->mutex));

    return 0;
}

/*
 * Initiate a call from a specified originating TU to a specified target TU.
 *   If the originating TU is not in the TU_DIAL_TONE state, then there is no effect.
 *   If the target TU is the same as the originating TU, then the TU transitions
 *     to the TU_BUSY_SIGNAL state.
 *   If the target TU already has a peer, or the target TU is not in the TU_ON_HOOK
 *     state, then the originating TU transitions to the TU_BUSY_SIGNAL state.
 *   Otherwise, the originating TU and the target TU are recorded as peers of each other
 *     (this causes the reference count of each of them to be incremented),
 *     the target TU transitions to the TU_RINGING state, and the originating TU
 *     transitions to the TU_RING_BACK state.
 *
 * In all cases, a notification of the resulting state of the originating TU is sent to
 * to the associated network client.  If the target TU has changed state, then its client
 * is also notified of its new state.
 *
 * If the caller of this function was unable to determine a target TU to be called,
 * it will pass NULL as the target TU.  In this case, the originating TU will transition
 * to the TU_ERROR state if it was in the TU_DIAL_TONE state, and there will be no
 * effect otherwise.  This situation is handled here, rather than in the caller,
 * because here we have knowledge of the current TU state and we do not want to introduce
 * the possibility of transitions to a TU_ERROR state from arbitrary other states,
 * especially in states where there could be a peer TU that would have to be dealt with.
 *
 * @param tu  The originating TU.
 * @param target  The target TU, or NULL if the caller of this function was unable to
 * identify a TU to be dialed.
 * @return 0 if successful, -1 if any error occurs that results in the originating
 * TU transitioning to the TU_ERROR state. 
 */
int tu_dial(TU *tu, TU *target) {
    if(tu == NULL)
        return -1;

    /* P(mutex) */
    if( (target == NULL) || (tu == target) ){
        P(&(tu->mutex));
    }
    else if(tu < target){
        P(&(tu->mutex));
        P(&(target->mutex));
    }
    else{
        P(&(target->mutex));
        P(&(tu->mutex));
    }
    /* P(mutex) */

    if(tu->state == TU_DIAL_TONE){

        if(target == NULL){
            tu->state = TU_ERROR;
            report_current_state(tu);
            V(&(tu->mutex));
            return -1;
        }
        else if(tu == target){
            tu->state = TU_BUSY_SIGNAL;
            report_current_state(tu);
            V(&(tu->mutex));
            return 0;
        }
        else if((target->peer != NULL) || (target->state != TU_ON_HOOK) ){
            tu->state = TU_BUSY_SIGNAL;
            report_current_state(tu);
            /* V(mutex) */
            if(tu < target){
                V(&(tu->mutex));
                V(&(target->mutex));
            }
            else{
                V(&(target->mutex));
                V(&(tu->mutex));
            }
            /* V(mutex) */
            return 0;
        }
        else{
            tu->peer = target;
            target->peer = tu;
            tu_ref(tu, "Dial.\n");
            tu_ref(target, "Dial.\n");
            tu->state = TU_RING_BACK;
            report_current_state(tu);
            target->state = TU_RINGING;
            report_current_state(target);
            /* V(mutex) */
            if(tu < target){
                V(&(tu->mutex));
                V(&(target->mutex));
            }
            else{
                V(&(target->mutex));
                V(&(tu->mutex));
            }
            /* V(mutex) */
            return 0;
        }
    }
    else{
        report_current_state(tu);

        /* V(mutex) */
        if( (target == NULL) || (tu == target) ){
            V(&(tu->mutex));
        }
        else if(tu < target){
            V(&(tu->mutex));
            V(&(target->mutex));
        }
        else{
            V(&(target->mutex));
            V(&(tu->mutex));
        }
        /* V(mutex) */

        return 0;
    }
}

/*
 * Take a TU receiver off-hook (i.e. pick up the handset).
 *   If the TU is in neither the TU_ON_HOOK state nor the TU_RINGING state,
 *     then there is no effect.
 *   If the TU is in the TU_ON_HOOK state, it goes to the TU_DIAL_TONE state.
 *   If the TU was in the TU_RINGING state, it goes to the TU_CONNECTED state,
 *     reflecting an answered call.  In this case, the calling TU simultaneously
 *     also transitions to the TU_CONNECTED state.
 *
 * In all cases, a notification of the resulting state of the specified TU is sent to
 * to the associated network client.  If a peer TU has changed state, then its client
 * is also notified of its new state.
 *
 * @param tu  The TU that is to be picked up.
 * @return 0 if successful, -1 if any error occurs that results in the originating
 * TU transitioning to the TU_ERROR state. 
 */
int tu_pickup(TU *tu) {
    if(tu == NULL)
        return -1;

    /* P(mutex) */
    if( (tu->peer == NULL) || (tu == tu->peer) ){
        P(&(tu->mutex));
    }
    else if(tu < tu->peer){
        P(&(tu->mutex));
        P(&(tu->peer->mutex));
    }
    else{
        P(&(tu->peer->mutex));
        P(&(tu->mutex));
    }
    /* P(mutex) */

    TU* target = NULL;
    if(tu->state == TU_ON_HOOK){
        tu->state = TU_DIAL_TONE;
        report_current_state(tu);
        V(&(tu->mutex));
        return 0;
    }
    else if(tu->state == TU_RINGING){
        target = tu->peer;

        tu->state = TU_CONNECTED;
        report_current_state(tu);
        target->state = TU_CONNECTED;
        report_current_state(target);

        /* V(mutex) */
        if(tu < target){
            V(&(tu->mutex));
            V(&(target->mutex));
        }
        else{
            V(&(target->mutex));
            V(&(tu->mutex));
        }
        /* V(mutex) */
        return 0;
    }
    else{
        report_current_state(tu);
        /* V(mutex) */
        if( (tu->peer == NULL) || (tu == tu->peer) ){
            V(&(tu->mutex));
        }
        else if(tu < tu->peer){
            V(&(tu->mutex));
            V(&(tu->peer->mutex));
        }
        else{
            V(&(tu->peer->mutex));
            V(&(tu->mutex));
        }
        /* V(mutex) */
        return 0;
    }
}

/*
 * Hang up a TU (i.e. replace the handset on the switchhook).
 *
 *   If the TU is in the TU_CONNECTED or TU_RINGING state, then it goes to the
 *     TU_ON_HOOK state.  In addition, in this case the peer TU (the one to which
 *     the call is currently connected) simultaneously transitions to the TU_DIAL_TONE
 *     state.
 *   If the TU was in the TU_RING_BACK state, then it goes to the TU_ON_HOOK state.
 *     In addition, in this case the calling TU (which is in the TU_RINGING state)
 *     simultaneously transitions to the TU_ON_HOOK state.
 *   If the TU was in the TU_DIAL_TONE, TU_BUSY_SIGNAL, or TU_ERROR state,
 *     then it goes to the TU_ON_HOOK state.
 *
 * In all cases, a notification of the resulting state of the specified TU is sent to
 * to the associated network client.  If a peer TU has changed state, then its client
 * is also notified of its new state.
 *
 * @param tu  The tu that is to be hung up.
 * @return 0 if successful, -1 if any error occurs that results in the originating
 * TU transitioning to the TU_ERROR state. 
 */
int tu_hangup(TU *tu) {
    if(tu == NULL)
        return -1;

    /* P(mutex) */
    if( (tu->peer == NULL) || (tu == tu->peer) ){
        P(&(tu->mutex));
    }
    else if(tu < tu->peer){
        P(&(tu->mutex));
        P(&(tu->peer->mutex));
    }
    else{
        P(&(tu->peer->mutex));
        P(&(tu->mutex));
    }
    /* P(mutex) */

    TU *target = NULL;
    if( (tu->state == TU_CONNECTED) || (tu->state == TU_RINGING) ){

        target = tu->peer;

        tu->state = TU_ON_HOOK;
        report_current_state(tu);
        target->state = TU_DIAL_TONE;
        report_current_state(target);

        tu->peer = NULL;
        target->peer = NULL;

        tu_unref(tu, "Hang Up.\n");
        tu_unref(target, "Hang Up.\n");

        /* V(mutex) */
        if(tu < target){
            V(&(tu->mutex));
            V(&(target->mutex));
        }
        else{
            V(&(target->mutex));
            V(&(tu->mutex));
        }
        /* V(mutex) */

        return 0;

    }
    else if( tu->state == TU_RING_BACK ){
        target = tu->peer;

        tu->state = TU_ON_HOOK;
        report_current_state(tu);
        target->state = TU_ON_HOOK;
        report_current_state(target);

        tu->peer = NULL;
        target->peer = NULL;

        tu_unref(tu, "Hang Up.\n");
        tu_unref(target, "Hang Up.\n");

        /* V(mutex) */
        if(tu < target){
            V(&(tu->mutex));
            V(&(target->mutex));
        }
        else{
            V(&(target->mutex));
            V(&(tu->mutex));
        }
        /* V(mutex) */

        return 0;
    }
    else if( (tu->state == TU_DIAL_TONE) || (tu->state == TU_BUSY_SIGNAL) || (tu->state == TU_ERROR) ){
        tu->state = TU_ON_HOOK;
        report_current_state(tu);
        V(&(tu->mutex));
        return 0;
    }
    else{
        report_current_state(tu);
        /* V(mutex) */
        if( (tu->peer == NULL) || (tu == tu->peer) ){
            V(&(tu->mutex));
        }
        else if(tu < tu->peer){
            V(&(tu->mutex));
            V(&(tu->peer->mutex));
        }
        else{
            V(&(tu->peer->mutex));
            V(&(tu->mutex));
        }
        /* V(mutex) */
        return 0;
    }
}

/*
 * "Chat" over a connection.
 *
 * If the state of the TU is not TU_CONNECTED, then nothing is sent and -1 is returned.
 * Otherwise, the specified message is sent via the network connection to the peer TU.
 * In all cases, the states of the TUs are left unchanged and a notification containing
 * the current state is sent to the TU sending the chat.
 *
 * @param tu  The tu sending the chat.
 * @param msg  The message to be sent.
 * @return 0  If the chat was successfully sent, -1 if there is no call in progress
 * or some other error occurs.
 */
int tu_chat(TU *tu, char *msg) {
    if(tu == NULL)
        return -1;

    /* P(mutex) */
    if( (tu->peer == NULL) || (tu == tu->peer) ){
        P(&(tu->mutex));
    }
    else if(tu < tu->peer){
        P(&(tu->mutex));
        P(&(tu->peer->mutex));
    }
    else{
        P(&(tu->peer->mutex));
        P(&(tu->mutex));
    }
    /* P(mutex) */

    if(tu->state != TU_CONNECTED){
        report_current_state(tu);
        /* V(mutex) */
        if( (tu->peer == NULL) || (tu == tu->peer) ){
            V(&(tu->mutex));
        }
        else if(tu < tu->peer){
            V(&(tu->mutex));
            V(&(tu->peer->mutex));
        }
        else{
            V(&(tu->peer->mutex));
            V(&(tu->mutex));
        }
        /* V(mutex) */
        return -1;
    }

    // CONNECTED STATE.
    report_current_state(tu);
    dprintf(tu->peer->tufd, "%s %s%s", "CHAT", msg, EOL);
    if(tu < tu->peer){
        V(&(tu->mutex));
        V(&(tu->peer->mutex));
    }
    else{
        V(&(tu->peer->mutex));
        V(&(tu->mutex));
    }
    return 0;
}
