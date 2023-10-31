#include "../include/utils.h"

#include "../include/macros.h"
#include "../include/state_machine.h"
#include "../include/alarm.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

State currentState = START;
int alarmEnabled = TRUE;

unsigned char BCC2(const unsigned char *buffer, int length) {
    unsigned char bcc = 0x00;

    for (int i = 0; i < length; i++) {
        bcc ^= buffer[i];
    }

    return bcc;
}

int sendSupervisionFrame(int fd, unsigned char a, unsigned char c) {
    // Create Frame
    unsigned char frame[5];
    frame[0] = FLAG;
    frame[1] = a;
    frame[2] = c;
    frame[3] = a ^ c;
    frame[4] = FLAG;

    // Send Frame
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

int readFrame(int fd, unsigned int timeout, unsigned char* data) {
    // Ser alarm Handler
    (void)signal(SIGALRM, alarmHandler);

    // Turn on Alarm
    if (timeout != 0) {
        alarmEnabled = TRUE;
        alarm(timeout);
    }

    currentState = START;
    int dataIndex = 0;

    // Read Frame while alarm is enabled or Infinite Loop, until STOP state
    while (alarmEnabled) {
        // Read Byte
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
            data[dataIndex++] = receivedByte; // Save Byte
            stateMachine(receivedByte);       // Update State Machine

            if (currentState == STOP) {
                return dataIndex;
            }
        }
    }

    return -1;
}

int stuffData(const unsigned char* data, int dataSize, unsigned char* stuffedData) {
    int stuffedSize = 0;

    // Stuff Data
    for (int i = 0; i < dataSize; i++) {
        // If data contains FLAG or ESCAPE, stuff it
        if (data[i] == FLAG || data[i] == ESCAPE) {
            stuffedData[stuffedSize++] = ESCAPE;          // Add ESCAPE
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
        // If data contains ESCAPE, destuff it
        if (stuffedData[i] == ESCAPE) {
            destuffedData[destuffedDataSize++] = stuffedData[++i] ^ 0x20;  // Toggle the 5th bit of the next byte
        } 
        else {
            destuffedData[destuffedDataSize++] = stuffedData[i];
        }
    }

    return destuffedDataSize;
}