// Link layer protocol implementation

#include "link_layer.h"

#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

// Supervision Buffer's
#define FLAG    0x7E

#define A_T     0x01    // Sent by Transmitter
#define A_R     0x03    // Sent by Receiver

#define C_INF0  0x00    // Information Frame 0
#define C_INF1  0x40    // Information Frame 1

#define C_SET   0x03    // Set Up
#define C_UA    0x07    // Unnumbered Acknowledgement
#define C_RR0   0x05    // Receiver Ready 0
#define C_RR1   0x85    // Receiver Ready 1
#define C_REJ0  0x01    // Reject 0
#define C_REJ1  0x81    // Reject 1

#define C_DISC  0x0B    // Disconnect

#define BCC(a, c) (a^c) 

#define ESCAPE 0x7D    // Escape character

// Information Buffer's

LinkLayer layer;
int fd;
struct termios oldtio;
struct termios newtio;

volatile int CYCLE_STOP = FALSE;

int alarmEnabled = FALSE;
int alarmCount = 0;

typedef enum {
    START,
    STOP,
    FLAG_OK,

    // Adress
    A_TX,
    A_RCV,

    // Connection
    CONN_RCV,
    CONN_TX,
    
    // Data Transmission
    DATA_RCV,
    DATA_RR_TX,
    DATA_REJ_TX,
    
    // Connection Termination
    CLOSE_RCV,
    CLOSE_TX,

    // Errors
    BCC1_OK,
    BCC2_OK,

} State;

State currentState = START;

////////////////////////////////////////////////
// Auxiliary functions
////////////////////////////////////////////////
// Calculate BCC2
unsigned char BCC2(unsigned char *buffer, int length) {
    unsigned char bcc = 0x00;

    for (int i = 0; i < length; i++) {
        bcc ^= buffer[i];
    }

    return bcc;
}

// Alarm function handler
void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;

    printf("Alarm #%d\n", alarmCount);
}

// Send supervision buffer
int sendSupervisionFrame(unsigned char a, unsigned char c) {
    unsigned char frame[5];

    frame[0] = FLAG;
    frame[1] = a;
    frame[2] = c;
    frame[3] = a ^ c;
    frame[4] = FLAG;

    ssize_t bytes = write(fd, frame, sizeof(frame));

    if (bytes == -1) {
        perror("Error writing to serial port");
        return -1;
    } 
    else if (bytes != sizeof(frame)) {
        perror("Partial write to serial port");
        return -1;
    }

    return 0;
}

// 
int initiateCommunicationTransmiter() {
    // Loop for input
    while (CYCLE_STOP == FALSE && alarmCount < layer.nRetransmissions) {
        if (alarmEnabled == FALSE) {
            // Send SET
            int bytes = sendSupervisionBuffer(A_T, C_SET);
            
            alarmEnabled = TRUE;
            alarm(layer.timeout);
        }

        // Wait for incoming data
        unsigned char receivedByte;
        ssize_t bytesRead = read(fd, &receivedByte, 1);

        if (bytesRead == -1) {
            perror("Error reading from serial port");
            return -1;
        }
        else if (bytesRead == 0) {
            continue;
        }

        if (stateMachine(receivedByte, NULL, 0) == -1) {
            return -1;
        }
        if (currentState == STOP) {
            CYCLE_STOP = TRUE;
        }
    }

    // Timeout
    if (alarmCount == layer.nRetransmissions) {
        printf("Time Out\n");
        return -1;
    }

    return 0;
}

int initiateCommunicationReciver() {
    // Wait for incoming data
    while (CYCLE_STOP == FALSE) {
        // Wait for incoming data
        unsigned char receivedByte;
        ssize_t bytesRead = read(fd, &receivedByte, 1);

        if (bytesRead == -1) {
            perror("Error reading from serial port");
            return -1;
        }
        else if (bytesRead == 0) {
            continue;
        }

        if (stateMachine(receivedByte, NULL, 0) == -1) {
            return -1;
        }
        if (currentState == STOP) {
            CYCLE_STOP = TRUE;
        }
    }

    if (sendSupervisionFrame(A_R, C_UA) == -1) {
        return -1;
    }

    return 0;
}

int stateMachine(unsigned char receivedByte, unsigned char* data, int dataSize) {

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
        case A_RCV:
            if (receivedByte == C_INF0 || receivedByte == C_INF1) {
                currentState = DATA_RCV;
            }
            else if (receivedByte == C_SET) {
                currentState = CONN_RCV;
            }
            else if (receivedByte == C_DISC) {
                currentState = CLOSE_RCV;
            }
            else if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            else {
                currentState = START;
            }
            break;

        case A_TX:
            if (receivedByte == C_UA) {
                currentState = CONN_TX;
            }
            else if (receivedByte == C_RR0 || receivedByte == C_RR1) {
                currentState = DATA_RR_TX;
            }
            else if (receivedByte == C_REJ0 || receivedByte == C_REJ1) {
                currentState = DATA_REJ_TX;
            }
            else if (receivedByte == C_DISC) {
                currentState = CLOSE_TX;
            }
            else if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            else {
                currentState = START;
            }
            break;

        // Connection
        case CONN_RCV:
            if (receivedByte == BBC(A_T, C_SET)) {
                currentState = BCC1_OK;
            }
            else if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            else {
                currentState = START;

            }
            break;
        
        case CONN_TX:
            if (receivedByte == BCC(A_R, C_UA)) {
                currentState = BCC1_OK;
            }
            else if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            else { 
                currentState = START;
            }
            break;

        // Data Transmission
        case DATA_RCV:
            if (receivedByte == BCC2(data, dataSize) && data != NULL) {
                currentState = BCC2_OK;
            }
            else if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            else {
                currentState = START;
            }
            break;
        
        case DATA_RR_TX:
            if (receivedByte == BCC1(A_R, C_RR0) || receivedByte == BCC1(A_R, C_RR1)) {
                currentState = BCC1_OK;
            }
            else if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            else {
                currentState = START;
            }
            break;

        case DATA_REJ_TX:
            if (receivedByte == BCC1(A_R, C_REJ0) || receivedByte == BCC1(A_R, C_REJ1)) {
                currentState = BCC1_OK;
            }
            else if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            else {
                currentState = START;
            }
            break;

        // Connection Termination
        case CLOSE_RCV:
            if (receivedByte == BCC1(A_T, C_DISC)) {
                currentState = BCC1_OK;
            }
            else if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            else {
                currentState = START;
            }
            break;

        case CLOSE_TX:
            if (receivedByte == BCC1(A_R, C_DISC)) {
                currentState = BCC1_OK;
            }
            else if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            else {
                currentState = START;
            }
            break;
        
        // Errors
        case BCC1_OK:
        case BCC2_OK:
            if (receivedByte == FLAG) {
                currentState = STOP;
            }
            else {
                currentState = START;
            }
            break;
    
        default:
            break;
    }
    
    return 0;
}

void byteStuff(const unsigned char* data, int dataSize, unsigned char* stuffedData, int* stuffedSize) {
    int i, j;

    for (i = 0, j = 0; i < dataSize; i++) {
        if (data[i] == FLAG || data[i] == ESCAPE) {
            // If data contains FLAG or ESC, stuff it
            stuffedData[j++] = ESCAPE;
            stuffedData[j++] = data[i] ^ 0x20;  // Toggle the 5th bit
        } 
        else {
            stuffedData[j++] = data[i];
        }
    }

    *stuffedSize = j;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) {

    // Set alarm function handler
    (void)signal(SIGALRM, alarmHandler);

    layer = connectionParameters;

    fd = open(layer.serialPort, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror(layer.serialPort);
        exit(-1);
    }

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = layer.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 0;  // Blocking read until 0 chars received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    if (layer.role == LlTx) {
        if (initiateCommunicationTransmiter() == -1) {
            return -1;
        }
    }
    else if (layer.role == LlRx) {
        if (initiateCommunicationReciver() == -1) {
            return -1;
        }
    }
    else {
        printf("Invalid role\n");
        return -1;
    }

    printf("Connection established\n");
    return 0;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {

    unsigned char frame[bufSize + 6];

    // Frame
    frame[0] = FLAG;                            // Start Flag
    frame[1] = A_T;                             // Address
    frame[2] = C_INF0;                          // Control Field
    frame[3] = BCC(A_T, C_INF0);                // BCC calculation
    memcpy(frame + 4, buf, bufSize);            // Copy data payload
    frame[4 + bufSize] = BCC2(buf, bufSize);    // BCC2
    frame[5 + bufSize] = FLAG;                  // End Flag

    // Stuffing
    unsigned char stuffedFrame[2 * (bufSize + 6)];
    int stuffedFrameSize;
    byteStuff(frame, bufSize + 6, stuffedFrame, &stuffedFrameSize);

    // Send frame
    CYCLE_STOP = FALSE;
    alarmEnabled = FALSE;
    currentState = START;
    ssize_t bytesRead = 0;

    while (CYCLE_STOP == FALSE && alarmCount < layer.nRetransmissions) {
        if (alarmEnabled == FALSE) {
            // Send frame
            int bytes = write(fd, stuffedFrame, stuffedFrameSize);

            alarm(layer.timeout); // Set alarm to be triggered
            alarmEnabled = TRUE;
        }

        // Wait for incoming data
        unsigned char receivedByte;
        bytesRead = read(fd, &receivedByte, 1);

        if (bytesRead == -1) {
            perror("Error reading from serial port");
            return -1;
        }
        else if (bytesRead == 0) {
            continue;
        }

        // State Machine
        if (stateMachine(receivedByte, stuffedFrame, stuffedFrameSize) == -1) {
            return -1;
        }
        if (currentState == STOP) {
            CYCLE_STOP = TRUE;
        }
    }
    
    return bytesRead;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics) {
    // TODO

    return 1;
}
