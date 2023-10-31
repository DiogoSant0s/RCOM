// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>

//Packets
#define MIDDLE_PACKET 1
#define STARTING_PACKET 2
#define ENDING_PACKET 3

#define FILE_SIZE 0
#define FILE_NAME 1

#define MAX_BUFFER_SIZE (MAX_PACKET_SIZE * 2 + 7)



int TransmitterApp(const char *filename) {
    
    struct stat file_stat;
    clock_t start_t = clock(); // Start time

    if (stat(filename, &file_stat) < 0) {
        perror("Error getting file information.");
        return -1;
    }

    // Open file
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Error opening file.");
        return -1;
    }

    // Starting packet (file size and name)
    unsigned int fileSize = sizeof(file_stat.st_size);       // Size of file
    unsigned int filenameSize = strlen(filename);            // Size of filename
    unsigned int packet_size = 5 + fileSize + filenameSize;  // Size of packet

    unsigned char packet[packet_size];
    packet[0] = STARTING_PACKET;
    packet[1] = FILE_SIZE;
    packet[2] = fileSize;
    memcpy(&packet[3], &file_stat.st_size, fileSize);
    packet[3 + fileSize] = FILE_NAME;
    packet[4 + fileSize] = filenameSize;
    memcpy(&packet[5 + fileSize], filename, filenameSize);

    // Print Frame
    // for (int i = 0; i < packet_size; i++) {
    //     printf("Packet Byte %d: 0x%02X\n", i, packet[i]);              // Remove
    // }

    if (llwrite(packet, packet_size) == -1) {
        printf("Error sending starting packet.\n");
        return -1;
    }
    printf("Starting packet sent\n");
    
    // Middle packets
    unsigned sequenceNumber = 0;
    unsigned packetsSent = 1;
    unsigned char buf[MAX_PAYLOAD_SIZE];

    while (TRUE) {
        // Read data from the file
        unsigned bytes_to_send = fread(buf, sizeof(unsigned char), MAX_PAYLOAD_SIZE - 4, file);

        // Create the data packet
        unsigned int dataPacketSize = 4 + bytes_to_send;  // Data packet size
        unsigned char dataPacket[dataPacketSize];         // Data packet
        dataPacket[0] = MIDDLE_PACKET;                  // Control field for data
        dataPacket[1] = sequenceNumber;                 // Sequence number (0 or 1)
        dataPacket[2] = (bytes_to_send >> 8) & 0xFF;    // High byte of size
        dataPacket[3] = bytes_to_send & 0xFF;           // Low byte of size
        memcpy(&dataPacket[4], buf, bytes_to_send);     // Copy data into the packet

        // Send the data packet
        if (llwrite(dataPacket, dataPacketSize) == -1) {
            printf("Error sending data packet.\n");
            return -1;
        }

        printf("%dº data packet sent\n", packetsSent++);
        // Print packet
        for (int i = 0; i < dataPacketSize; i++) {
            printf("Send Packet Byte %d: 0x%02X\n", i, dataPacket[i]);              // Remove
        }
        printf("\n");

        sequenceNumber = 1 - sequenceNumber;  // Toggle sequence number (0 or 1)

        // If no more data to read, break from the loop
        if (bytes_to_send < (MAX_PAYLOAD_SIZE - 4)) {
            break;
        }
    }
    printf("Middle packets sent\n");

    // Ending packet
    packet[0] = ENDING_PACKET;

    if (llwrite(packet, packet_size) == -1) {
        printf("Error sending ending packet.\n");
        return -1;
    }
    printf("Ending packet sent\n");

    // Close file
    fclose(file);

    // Calculate and print transfer information
    clock_t end_t = clock();
    double total_t = (double)(end_t - start_t) / CLOCKS_PER_SEC;

    printf("\nTotal time taken: %f seconds\n", total_t);
    printf("Size transfered: %d bytes\n", (int) file_stat.st_size);
    printf("Transfer Speed: %f B/s\n\n", file_stat.st_size/total_t);

    return 0;
}

int ReceiverApp(const char *filename) {

    FILE *file;
    unsigned int totalBytesReceived = 0;
    unsigned int packetsReceived = 0;
    unsigned char dataPacket[MAX_PAYLOAD_SIZE + 4];

    while (TRUE) {
        ssize_t bytesRead = llread(dataPacket);
        if (bytesRead == -1) {
            printf("Error reading data packet.\n");
            fclose(file);
            return -1;
        }
        if (bytesRead == 0) {
            continue;
        }
        else {
            // Print packet
            for (int i = 0; i < bytesRead; i++) {
                printf("Received Byte %d: 0x%02X\n", i, dataPacket[i]);              // Remove
            }
            printf("\n");

            if (dataPacket[0] == STARTING_PACKET) {
                file = fopen(filename, "wb");
                if (file == NULL) {
                    printf("Error opening file.\n");
                    return -1;
                }
                printf("Received starting packet\n");
            }
            else if (dataPacket[0] == MIDDLE_PACKET) {
                // Write the data to the file
                totalBytesReceived += bytesRead - 4;
                if (fwrite(&dataPacket[4], 1, bytesRead - 4, file) != bytesRead - 4) {
                    printf("Error writing data to file.\n");
                    fclose(file);
                    return -1;
                }
                fflush(file); // Remove
                printf("Received %dº data packet\n", packetsReceived++);
            }
            else if (dataPacket[0] == ENDING_PACKET) {
                fclose(file);
                printf("Received ending packet\n");
                break;
            }
            else {
                printf("Error: Invalid control field.\n");
                fclose(file);
                return -1;
            }
        }
    }

    // Print transfer information
    printf("Received %u bytes of data\n", totalBytesReceived);
    return 0;
}


void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{

    // Create link layer
    LinkLayer layer;

    layer.baudRate = baudRate;
    layer.nRetransmissions = nTries;

    if (strcmp(role, "tx") == 0) {
        layer.role = LlTx;
    }
    if (strcmp(role, "rx") == 0) {
        layer.role = LlRx;
    }

    sprintf(layer.serialPort, "%s", serialPort);
    layer.timeout = timeout;

    // Open link layer
    if (llopen(layer) == -1) {
        return ;
    }
    
    // Run application layer
    if (layer.role == LlTx) {
        TransmitterApp(filename);
    }
    if (layer.role == LlRx) {
        ReceiverApp(filename);
    }

    // Close link layer
    llclose(FALSE);
}
