#include "link_layer.h"

#include "../include/utils.h"

#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define _POSIX_SOURCE 1     // POSIX compliant source

LinkLayer layer;            // Link layer connection parameters
int fd;                     // File descriptor for serial port
struct termios oldtio;      // Old Terminal I/O structure
struct termios newtio;      // New Terminal I/O structure

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int initiateCommunicationTransmiter() {
    int alarmCount = 0;

    // Will try to send SET nRetransmissions times
    while (alarmCount < layer.nRetransmissions) {
        // Send SET
        int bytes = sendSupervisionFrame(fd, A_T, C_SET);
        if (bytes == -1) {
            printf("ERROR - Not possible to send UA\n");
            return -1;
        }

        // Receive UA
        unsigned char frame[5];
        if (readFrame(fd, layer.timeout, frame) != -1) {
            // Verify BCC1
            if (frame[3] == BCC1(A_R, C_UA)) {
                return 0;
            }
        }

        alarmCount++;
    }
    printf("ERROR - Time Out\n");

    return -1;
}

int initiateCommunicationReciver() {
    // Receive SET
    unsigned char frame[5];
    if (readFrame(fd, 0, frame) == -1) {
        printf("ERROR - Not received SET\n");
        return -1;
    }
    // Verify BCC1
    if (frame[3] != BCC1(A_T, C_SET)) {
        return -1;
    }

    // Send UA
    if (sendSupervisionFrame(fd, A_R, C_UA) == -1) {
        printf("ERROR - Not possible to send UA\n");
        return -1;
    }

    return 0;
}

int llopen(LinkLayer connectionParameters) {
    // Set connection parameters
    layer = connectionParameters;

    // Open serial port device for reading and writing and not as controlling tty
    fd = open(layer.serialPort, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror(layer.serialPort);
        exit(-1);
    }

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1) {
        printf("ERROR - Not possible to save current port settings\n");
        return -1;
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    // Set new port settings
    newtio.c_cflag = layer.baudRate | CS8 | CLOCAL | CREAD; // Set baudrate, 8 bits, no parity, 1 stop bit, ...
    newtio.c_iflag = IGNPAR;                                // Ignore bytes with parity errors
    newtio.c_oflag = 0;                                     // Raw output
    newtio.c_lflag = 0;                                     // Raw input
    newtio.c_cc[VTIME] = 0;                                 // Inter-character timer unused
    newtio.c_cc[VMIN] = 0;                                  // Blocking read until 0 chars received

    // TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1) {
        printf("ERROR - Not possible to set New port settings\n");
        return -1;
    }

    // Initialize Connection
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
        printf("ERROR - Invalid Role\n");
        return -1;
    }

    return 0;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {
    static int lastSequence = 1; // First should be 0 so last is "1"

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

    // Send frame
    int alarmCount = 0;

    // Will try to send frame nRetransmissions times
    while (alarmCount < layer.nRetransmissions) {
        // Send frame
        int bytes = write(fd, frame, frameSize);
        if (bytes == -1) {
            printf("ERROR - Not possible to write to Serial Port\n");
            return -1;
        }
        else if (bytes != frameSize) {
            printf("ERROR - Partial write to Serial Port\n");
            return -1;
        }

        // Receive RR
        unsigned char frame[5];
        if (readFrame(fd, layer.timeout, frame) != -1) {
            // Verify BCC1
            if (frame[3] == BCC1(A_R, frame[2])) {
                return frameSize;
            }
        }
        alarmCount++;
    }
    printf("ERROR - Time Out\n");

    return -1;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {

    unsigned char stuffedFrame[2 * (MAX_PAYLOAD_SIZE + 1) + 5]; // Allocate memory for stuffed frame
    unsigned int stuffedFrameSize = 0;

    // Read frame
    stuffedFrameSize = readFrame(fd, 0, stuffedFrame);
    if (stuffedFrameSize == -1) {
        printf("ERROR - Not possible to read Data Frame\n");
        return -1;
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

    static int lastReceivedSequence = -1;           // 0 or 1 
    int receivedSequence = (frame[2] & 0x40) >> 6;  // Gets 7th bit

    // Check duplicate
    if (receivedSequence == lastReceivedSequence) {
        printf("ERROR - Received duplicated frame\n");
        if (receivedSequence == 0) {
            // Send RR0
            if (sendSupervisionFrame(fd, A_R, C_RR0) == -1) {
                printf("ERROR - Not possible to send RR0\n");
                return -1;
            }   
        } 
        else if (receivedSequence == 1) {
            // Send RR1
            if (sendSupervisionFrame(fd, A_R, C_RR1) == -1) {
                printf("ERROR - Not possible to send RR1\n");
                return -1;
            }
        }
        return 0;
    }

    // Check BCC1
    if (frame[3] != BCC1(A_T, frame[2])) {
        printf("ERROR - BCC1 failed - (Received: 0x%x \t Expected: 0x%x)\n", frame[3], BCC2(A_T, frame[2]));
        if (frame[2] == C_INF0) {
            // Send REJ0
            if (sendSupervisionFrame(fd, A_T, C_REJ0) == -1) {
                printf("ERROR - Not possible to send REJ0\n");
                return -1;
            }
        }
        if (frame[2] == C_INF1) {
            // Send REJ1
            if (sendSupervisionFrame(fd, A_T, C_REJ1) == -1) {
                printf("ERROR - Not possible to send REJ1\n");
                return -1;
            }
        }
        return -1;
    }

    // Check BCC2
    if (frame[frameSize - 2] != BCC2(data, dataSize - 1)) {
        printf("ERROR - BCC2 failed - (Received: 0x%x \t Expected: 0x%x)\n", frame[frameSize - 2], BCC2(data, dataSize));
        if (frame[2] == C_INF0) {
            // Send REJ0
            if (sendSupervisionFrame(fd, A_T, C_REJ0) == -1) {
                printf("ERROR - Not possible to send REJ0\n");
                return -1;
            }
        }
        else if (frame[2] == C_INF1) {
            // Send REJ1
            if (sendSupervisionFrame(fd, A_T, C_REJ1) == -1) {
                printf("ERROR - Not possible to send REJ1\n");
                return -1;
            }
        }
        return -1;
    }

    // Send RR0 or RR1
    if (frame[2] == C_INF0) {
        if (sendSupervisionFrame(fd, A_R, C_RR0) == -1) {
            printf("ERROR - Not possible to send RR0\n");
            return -1;
        }
    }
    else if (frame[2] == C_INF1) {
        if (sendSupervisionFrame(fd, A_R, C_RR1) == -1) {
            printf("ERROR - Not possible to send RR1\n");
            return -1;
        }
    }

    lastReceivedSequence = receivedSequence; // Update last received sequence

    // Copy data payload
    memcpy(packet, data, dataSize - 1);

    // Return data payload size
    return dataSize - 1;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int terminateCommunicationTransmitter() {
    int alarmCount = 0;

    // Will try to send DISC nRetransmissions times
    while (alarmCount < layer.nRetransmissions) {
        // Send DISC
        if (sendSupervisionFrame(fd, A_T, C_DISC) == -1) {
            printf("ERROR - Not possible to send DISC\n");
            return -1;
        }

        // Receive DISC
        unsigned char frame[5];
        if (readFrame(fd, layer.timeout, frame) != -1) {
            // Verify BCC1
            if (frame[3] == BCC1(A_R, C_DISC)) {
                // Send UA
                if (sendSupervisionFrame(fd, A_T, C_UA) == -1) {
                    printf("ERROR - Not possible to send UA\n");
                    return -1;
                }
                return 0;
            }
        }
        alarmCount++;
    }
    printf("ERROR - Time Out\n");

    return -1;
}

int terminateCommunicationReceiver() {
    // Receive DISC
    unsigned char frame[5];
    if (readFrame(fd, 0, frame) == -1) {
        printf("ERROR - Not received DISC\n");
        return -1;
    }
    // Verify BCC1
    if (frame[3] != BCC1(A_T, C_DISC)) {
        return -1;
    }

    // Send DISC
    if (sendSupervisionFrame(fd, A_R, C_DISC) == -1) {
        return -1;
    }

    // Receive UA
    if (readFrame(fd, 0, frame) == -1) {
        printf("ERROR - Not received UA\n");
        return -1;
    }
    // Verify BCC1
    if (frame[3] != BCC1(A_T, C_UA)) {
        return -1;
    }

    return 0;
}

int llclose(int showStatistics) {
    // Transmitter
    if (layer.role == LlTx) {
        terminateCommunicationTransmitter();
    }
    // Receiver
    else if (layer.role == LlRx) {
        terminateCommunicationReceiver();
    }
    else {
        printf("ERROR - Invalide Role\n");
        return -1;
    }

    // Restore old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1) {
        printf("ERROR - Not possible to restore old port settings\n");
        return -1;
    }

    // Close serial port
    if (close(fd) == -1) {
        printf("ERROR - Not possible to close Serial Port\n");
        return -1;
    }

    return 0;
}
