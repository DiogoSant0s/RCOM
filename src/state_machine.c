#include "../include/state_machine.h"

int stateMachine(unsigned char receivedByte) {

    switch (currentState) {
        case START:
            if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            break;
 
        case FLAG_OK:
            if (receivedByte == A_T) {
                currentState = A_RCV;
            }
            else if (receivedByte == A_R) {
                currentState = A_TX;
            }
            else if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            else {
                currentState = START;
            }
            break; 

        // Address
        case A_TX:
            if (receivedByte == C_UA) {
                currentState = RECEIVE;
            }
            else if (receivedByte == C_RR0 || receivedByte == C_RR1 ||
                     receivedByte == C_REJ0 || receivedByte == C_REJ1) {
                currentState = RECEIVE;
            }
            else if (receivedByte == C_DISC) {
                currentState = RECEIVE;
            }
            else if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            else {
                currentState = START;
            }
            break;

        case A_RCV:
            if (receivedByte == C_SET) {
                currentState = RECEIVE;
            }
            else if (receivedByte == C_UA) {
                currentState = RECEIVE;
            }
            else if (receivedByte == C_INF0 || receivedByte == C_INF1) {
                currentState = RECEIVE;
            }
            else if (receivedByte == C_DISC) {
                currentState = RECEIVE;
            }
            else if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            else {
                currentState = START;
            }
            break;

        // Receive Data
        case RECEIVE:
            if (receivedByte == FLAG) {
                currentState = STOP;
            }
            break;
            
        default:
            break;
    }
    
    return 0;
}