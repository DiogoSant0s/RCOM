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
#define A_T     0x01
#define A_R     0x03
#define C_SET   0x03
#define C_UA    0x07
#define C_RR0   0x05
#define C_RR1   0x85
#define C_REJ0  0x01
#define C_REJ1  0x81
#define C_DISC  0x0B
#define BCC(a, c) (a^c) 

// Information Buffer's
#define C_INF0  0x00
#define C_INF1  0x40

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
    BCC_OK,
    FLAG_OK,

    // Receiver
    A_RCV,
    C_RCV,

    // Transmitter
    A_TX,
    C_TX,
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

    size_t bytes = write(fd, frame, sizeof(frame));

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
        size_t bytesRead = read(fd, &receivedByte, 1);

        if (bytesRead == -1) {
            perror("Error reading from serial port");
            return -1;
        }
        else if (bytesRead == 0) {
            continue;
        }

        if (stateMachine(receivedByte) == -1) {
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
        size_t bytesRead = read(fd, &receivedByte, 1);

        if (bytesRead == -1) {
            perror("Error reading from serial port");
            return -1;
        }
        else if (bytesRead == 0) {
            continue;
        }

        if (stateMachine(receivedByte) == -1) {
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

int stateMachine(unsigned char receivedByte) {

    switch (currentState) {
        case START:
            if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            break;

        // SET State Machine
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

        // SET State Machine
        case A_RCV:
            if (receivedByte == C_SET) {
                currentState = C_RCV;
            }
            else if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            else {
                currentState = START;
            }
            break;

        case C_RCV:
            if (receivedByte == BCC(A_T, C_SET)) {
                currentState = BCC_OK;
            }
            else if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            else {
                currentState = START;
            }
            break;

        // UA State Machine
        case A_TX:
            if (receivedByte == C_UA) {
                currentState = C_TX;
            }
            else if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            else {
                currentState = START;
            }
            break;
        
        case C_TX:
            if (receivedByte == BCC(A_R, C_UA)) {
                currentState = BCC_OK;
            }
            else if (receivedByte == FLAG) {
                currentState = FLAG_OK;
            }
            else {
                currentState = START;
            }
            break;

        case BCC_OK:
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
        exit(-1);
    }

    printf("Connection established\n");
    return 0;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {
    // TODO

    return 0;
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
