#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include "macros.h"

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

extern State currentState;

int stateMachine(unsigned char receivedByte);

#endif // STATE_MACHINE_H