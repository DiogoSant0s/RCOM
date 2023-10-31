#include "../include/utils.h"

#include "../include/macros.h"

#include "unistd.h"
#include "stdio.h"

unsigned char BCC2(const unsigned char *buffer, int length) {
    unsigned char bcc = 0x00;

    for (int i = 0; i < length; i++) {
        bcc ^= buffer[i];
    }

    return bcc;
}

int sendSupervisionFrame(int fd, unsigned char a, unsigned char c) {
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