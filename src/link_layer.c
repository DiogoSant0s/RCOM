// Link layer protocol implementation

#include "link_layer.h"

#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

// Supervision Buffer's
#define FLAG    0x7E

#define A_R     0x01    // Sent by Receiver
#define A_T     0x03    // Sent by Transmitter

#define C_INF0  0x00    // Information Frame 0
#define C_INF1  0x40    // Information Frame 1

#define C_SET   0x03    // Set Up
#define C_UA    0x07    // Unnumbered Acknowledgement
#define C_RR0   0x05    // Receiver Ready 0
#define C_RR1   0x85    // Receiver Ready 1
#define C_REJ0  0x01    // Reject 0
#define C_REJ1  0x81    // Reject 1
#define C_DISC  0x0B    // Disconnect

#define BCC1(a, c) (a^c) 

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

    // Receving Data
    RECEIVE,

} State;

State currentState = START;

////////////////////////////////////////////////
// Auxiliary functions
////////////////////////////////////////////////
// Calculate BCC2
unsigned char BCC2(const unsigned char *buffer, int length) {
    unsigned char bcc = 0x00;

    // printf("Length: %d\n", length);
    // printf("Stating Byte: 0x%x\n", buffer[0]);                                    // Remove
    // printf("Ending Byte: 0x%x\n", buffer[length - 1]);

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

// State Machine
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
            int bytes = sendSupervisionFrame(A_T, C_SET);
            if (bytes == -1) {
                printf("Error sending SET\n");
                return -1;
            }
            
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
        else {
            stateMachine(receivedByte);

            if (currentState == STOP) {
                CYCLE_STOP = TRUE;
            }
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
        else {
            stateMachine(receivedByte);

            if (currentState == STOP) {
                CYCLE_STOP = TRUE;
            }
        }
    }

    if (sendSupervisionFrame(A_R, C_UA) == -1) {
        return -1;
    }

    return 0;
}

int stuffData(const unsigned char* data, int dataSize, unsigned char* stuffedData) {
    int stuffedSize = 0;

    // Stuff Data
    for (int i = 0; i < dataSize; i++) {
        if (data[i] == FLAG || data[i] == ESCAPE) {
            // If data contains FLAG or ESC, stuff it
            stuffedData[stuffedSize++] = ESCAPE;
            stuffedData[stuffedSize++] = data[i] ^ 0x20;  // Toggle the 5th bit
        } 
        else {
            stuffedData[stuffedSize++] = data[i];
        }
    }

    return stuffedSize;
}

int destuffData(const unsigned char* stuffedData, int stuffedDataSize, unsigned char* destuffedData) {
    int destuffedDataSize = 0;

    for (int i = 0; i < stuffedDataSize; i++) {
        if (stuffedData[i] == ESCAPE) {
            // If data contains ESC, destuff it
            destuffedData[destuffedDataSize++] = stuffedData[++i] ^ 0x20;  // Toggle the 5th bit
        } 
        else {
            destuffedData[destuffedDataSize++] = stuffedData[i];
        }
    }

    return destuffedDataSize;
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
    static int lastSequence = 1;

    // Stuff Data
    unsigned int stuffedDataSize = 2 * bufSize;
    unsigned char stuffedData[stuffedDataSize];
    stuffedDataSize = stuffData(buf, bufSize, stuffedData);

    // Construct Frame
    unsigned int frameSize = stuffedDataSize + 6;
    unsigned char frame[frameSize];

    frame[0] = FLAG;                                         // Start Flag
    frame[1] = A_T;                                          // Address

    if (lastSequence == 0) {                                 //
        lastSequence = 1;                                    //
        frame[2] = C_INF1;                                   //
    }                                                        // Control
    else if (lastSequence == 1) {                            //
        lastSequence = 0;                                    //
        frame[2] = C_INF0;                                   //
    }

    frame[3] = BCC1(A_T, frame[2]);                          // BCC1
    memcpy(frame + 4, stuffedData, stuffedDataSize);         // Copy stuffed data

    unsigned char bcc2 = BCC2(buf, bufSize);                 //
    if (bcc2 == FLAG) {                                      //
        frameSize++;                                         //
        frame[frameSize - 3] = ESCAPE;                       //
        frame[frameSize - 2] = FLAG ^ 0x20;                  //
    }                                                        // 
    else if (bcc2 == ESCAPE) {                               // BCC2
        frameSize++;                                         //
        frame[frameSize - 3] = ESCAPE;                       //
        frame[frameSize - 2] = ESCAPE ^ 0x20;                //
    }                                                        //
    else {                                                   //
        frame[frameSize - 2] = bcc2;                         //
    }                                                        //

    frame[frameSize - 1] = FLAG;                             // End Flag

    // Print frame
    // for (int i = 0; i < frameSize; i++) {
    //     printf("Frame Byte %d: 0x%x\n", i, frame[i]);                   // Remove
    // }
    // printf("\n");

    // Send frame
    CYCLE_STOP = FALSE;
    alarmEnabled = FALSE;
    currentState = START;
    ssize_t bytesRead = 0;

    while (CYCLE_STOP == FALSE && alarmCount < layer.nRetransmissions) {
        if (alarmEnabled == FALSE) {
            // Send frame
            int bytes = write(fd, frame, frameSize);
            
            if (bytes == -1) {
                perror("Error writing to serial port");
                return -1;
            }
            else if (bytes != frameSize) {
                perror("Partial write to serial port");
                return -1;
            }

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
        else {
            stateMachine(receivedByte);

            if (currentState == STOP) {
                CYCLE_STOP = TRUE;
            }
        }
    }

    if (alarmCount == layer.nRetransmissions) {
        printf("Time Out\n");
        return -1;
    }

    return frameSize;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {

    unsigned char stuffedFrame[2 * (MAX_PAYLOAD_SIZE + 1) + 5];
    unsigned int stuffedFrameSize = 0;

    CYCLE_STOP = FALSE;
    currentState = START;

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
        else {
            stuffedFrame[stuffedFrameSize] = receivedByte;
            stuffedFrameSize++;

            stateMachine(receivedByte);

            if (currentState == STOP) {
                CYCLE_STOP = TRUE;
            }
        }
    }

    // Unstuffing Data
    unsigned char data[stuffedFrameSize - 5]; 
    unsigned int dataSize = destuffData(stuffedFrame + 4, stuffedFrameSize - 5, data);

    // Construct Frame
    unsigned int frameSize = dataSize + 5;
    unsigned char frame[frameSize];

    frame[0] = stuffedFrame[0];                                // Start Flag
    frame[1] = stuffedFrame[1];                                // Address
    frame[2] = stuffedFrame[2];                                // Control
    frame[3] = stuffedFrame[3];                                // BCC1
    memcpy(frame + 4, data, dataSize);                         // Copy data + BCC2
    frame[frameSize - 1] = stuffedFrame[stuffedFrameSize - 1]; // End Flag

    static int lastReceivedSequence = -1;
    int receivedSequence = (frame[2] & 0x40) >> 6;

    // Check duplicate
    if (receivedSequence == lastReceivedSequence) {
        printf("Duplicate\n");
        if (receivedSequence == 0) {
            sendSupervisionFrame(A_R, C_RR0);
        } 
        else if (receivedSequence == 1) {
            sendSupervisionFrame(A_R, C_RR1);
        }
        return 0;
    }

    // Check BCC1
    if (frame[3] != BCC1(A_T, frame[2])) {
        printf("BCC1: 0x%x\tExpected: 0x%02x\n", frame[3], BCC1(A_T, frame[2]));                   // Remove
        // Send REJ
        if (frame[2] == C_INF0) {
            sendSupervisionFrame(A_T, C_REJ0);
        }
        if (frame[2] == C_INF1) {
            sendSupervisionFrame(A_T, C_REJ1);
        }
        return -1;
    }

    // Check BCC2
    if (frame[frameSize - 2] != BCC2(data, dataSize - 1)) {
        printf("BCC2: 0x%x\tExpected: 0x%02x\n", frame[frameSize - 2], BCC2(data, dataSize));          // Removwe
        // Send REJ
        if (frame[2] == C_INF0) {
            sendSupervisionFrame(A_T, C_REJ0);
        }
        else if (frame[2] == C_INF1) {
            sendSupervisionFrame(A_T, C_REJ1);
        }
        return -1;
    }

    // Send RR
    if (frame[2] == C_INF0) {
        sendSupervisionFrame(A_R, C_RR0);
    }
    else if (frame[2] == C_INF1) {
        sendSupervisionFrame(A_R, C_RR1);
    }

    lastReceivedSequence = receivedSequence;

    // Print Data
    for (int i = 0; i < dataSize - 1; i++) {
        printf("Data Byte %d: 0x%x\n", i, data[i]);              // Remove
    }
    printf("\n");

    // Copy data payload
    memcpy(packet, data, dataSize - 1);

    // Return data payload size
    return dataSize - 1;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics) {

    CYCLE_STOP = FALSE;
    alarmEnabled = FALSE;
    alarmCount = 0;

    currentState = START;

    // Transmitter
    if (layer.role == LlTx) {

        while (CYCLE_STOP == FALSE && alarmCount < layer.nRetransmissions) {
            if (alarmEnabled == FALSE) {
                // Send DISC
                ssize_t bytes = sendSupervisionFrame(A_T, C_DISC);
                if (bytes == -1) {
                    printf("Error sending DISC\n");
                    return -1;
                }

                alarm(layer.timeout);
                alarmEnabled = TRUE;
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
            else {
                stateMachine(receivedByte);

                if (currentState == STOP) {
                    CYCLE_STOP = TRUE;
                }

            }
        }
        
        if (alarmCount == layer.nRetransmissions) {
            printf("Time Out\n");
            return -1;
        }

        // Send UA
        if (sendSupervisionFrame(A_T, C_UA) == -1) {
            printf("Error sending UA\n");
            return -1;
        }
    }
    // Receiver
    else if (layer.role == LlRx) {
        // Receive DISC
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
            else {
                stateMachine(receivedByte);

                if (currentState == STOP) {
                    CYCLE_STOP = TRUE;
                }
            }
        }

        // Send DISC
        if (sendSupervisionFrame(A_R, C_DISC) == -1) {
            return -1;
        }

        // Receive UA
        CYCLE_STOP = FALSE;
        currentState = START;

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
            else {
                stateMachine(receivedByte);

                if (currentState == STOP) {
                    CYCLE_STOP = TRUE;
                }
            }
        }
    }
    else {
        printf("Invalid role\n");
        return -1;
    }

    // Restore old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("Old termios structure restored\n");

    // Close serial port
    if (close(fd) == -1) {
        perror("Error closing serial port");
        return -1;
    }

    printf("Serial port closed\n");

    return 0;
}
