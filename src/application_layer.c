#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

// Packets
#define MIDDLE_PACKET 1
#define STARTING_PACKET 2
#define ENDING_PACKET 3

#define FILE_SIZE 0
#define FILE_NAME 1

int TransmitterApp(const char *filename) {
    // Get file information
    struct stat file_stat;
    if (stat(filename, &file_stat) < 0) {
        perror("Error getting file information.");
        return -1;
    }

    // Open file
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        printf("Error - Not possible to open file\n");
        return -1;
    }

    // Construct Starting packet (file size and name)
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

    // Send the Starting packet
    if (llwrite(packet, packet_size) == -1) {
        printf("Error - Not possible to send starting packet\n");
        return -1;
    }
    
    // Send Middle packets
    unsigned sequenceNumber = 0;
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
            printf("Error - Not possible to send data packet\n");
            return -1;
        }

        sequenceNumber = 1 - sequenceNumber;  // Toggle sequence number (0 or 1)

        // If no more data to read, break from the loop
        if (bytes_to_send < (MAX_PAYLOAD_SIZE - 4)) {
            break;
        }
    }

    // Ending packet
    packet[0] = ENDING_PACKET;

    // Send the Ending packet
    if (llwrite(packet, packet_size) == -1) {
        printf("Error - Not possible to send ending packet\n");
        return -1;
    }

    // Close file
    fclose(file);
    return 0;
}

int ReceiverApp(const char *filename) {
    FILE *file;
    unsigned char dataPacket[MAX_PAYLOAD_SIZE + 4];

    // Receive Packets
    while (TRUE) {
        size_t bytesRead = llread(dataPacket);
        if (bytesRead == -1) {
            printf("Error - Not possible to read data packet.\n");
            fclose(file);
            return -1;
        }
        if (bytesRead == 0) {
            continue;
        }
        else {
            if (dataPacket[0] == STARTING_PACKET) {
                file = fopen(filename, "wb");
                if (file == NULL) {
                    printf("Error - Not possible to open file\n");
                    return -1;
                }
            }
            else if (dataPacket[0] == MIDDLE_PACKET) {
                // Write the data to the file
                if (fwrite(&dataPacket[4], 1, bytesRead - 4, file) != bytesRead - 4) {
                    printf("Error - Not possible to write data to file.\n");
                    fclose(file);
                    return -1;
                }
            }
            else if (dataPacket[0] == ENDING_PACKET) {
                fclose(file);
                break;
            }
            else {
                printf("Error - Invalid packet.\n");
                fclose(file);
                return -1;
            }
        }
    }

    return 0;
}

void applicationLayer(const char *serialPort, const char *role, int baudRate, int nTries, int timeout, const char *filename) {
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
    clock_t start_t_open, end_t_open; // Time variables
    start_t_open = clock(); // Start time
    if (llopen(layer) == -1) {
        perror("Error - Not possible to open link layer.");
    }
    end_t_open = clock();   // End time
    printf("\nConnection established ✓\n");
    
    // Run application layer
    clock_t start_t, end_t; // Time variables
    if (layer.role == LlTx) {
        start_t = clock(); // Start time

        TransmitterApp(filename);  // Main App

        end_t = clock();   // End time

        printf("All data Sent ✓\n");
    }
    if (layer.role == LlRx) {
        start_t = clock(); // Start time

        ReceiverApp(filename);     // Main App

        end_t = clock();   // End time

        printf("All data Received ✓\n");
    }

    // Close link layer
    clock_t start_t_close, end_t_close; // Time variables
    start_t_close = clock(); // Start time
    llclose(FALSE);
    end_t_close = clock();   // End time
    printf("Connection Closed ✓\n");

    // Print statistics
    struct stat file_stat;
    if (stat(filename, &file_stat) < 0) {
        perror("Error - Not possible to get file info (Stats will not be printed).");
    }
    else {
        printf("\nStatistics:\n");
        printf("  -Total time elapsed: %f seconds\n", (double)(end_t_close - start_t_open) / CLOCKS_PER_SEC);
        printf("  -Time elapsed (llopen): %f seconds\n", (double)(end_t_open - start_t_open) / CLOCKS_PER_SEC);
        printf("  -Time elapsed transfering data: %f seconds\n", (double)(end_t - start_t) / CLOCKS_PER_SEC);
        printf("  -Time elapsed (llclose): %f seconds\n", (double)(end_t_close - start_t_close) / CLOCKS_PER_SEC);
        printf("  -Size transfered: %ld bytes\n", file_stat.st_size);
        printf("  -Transfer rate: %f bytes/second\n", (double)file_stat.st_size / ((double)(end_t - start_t) / CLOCKS_PER_SEC));
    }

}
