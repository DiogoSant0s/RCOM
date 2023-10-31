#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include "macros.h"

// State Machine States
typedef enum {
    START,
    STOP,
    FLAG_OK,

    // Adress
    A_TX,
    A_RCV,

    // Receving Data
    RECEIVE,

} State;

extern State currentState; // Current state of the state machine

// Changes the currentState of the state machine according to the received byte.
// Returns 0 if everything went well.
int stateMachine(unsigned char receivedByte);

#endif // STATE_MACHINE_H