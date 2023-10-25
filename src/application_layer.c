// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdint.h>
#include <stdio.h>

#define CONTROL_DATA 0x01       // Control field for data packets.
#define CONTROL_START 0x02      // Control field for start control packets.
#define CONTROL_END 0x03        // Control field for end control packets.

/// @brief
/// @param data
/// @param length
/// @return Return "1" on success or "-1" on error.
int sendControlPacket(uint8_t controlField, const char *filename, int fileSize) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    buf[0] = controlField;
    buf[1] = strlen(filename) + 1;
    strcpy((char *) &buf[2], filename);
    buf[2 + strlen(filename)] = fileSize >> 24;
    buf[3 + strlen(filename)] = fileSize >> 16;
    buf[4 + strlen(filename)] = fileSize >> 8;
    buf[5 + strlen(filename)] = fileSize;

    if (llwrite(buf, fileSize) == -1) {
        perror("Failed to send control packet\n");
        return -1;
    }
    return 1;
}

/// @brief 
/// @param data 
/// @param length
/// @return Return "1" on success or "-1" on error.
int sendDataPacket(uint8_t *data, int length) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    buf[0] = CONTROL_DATA;
    buf[1] = length >> 8;
    buf[2] = length;
    memcpy(&buf[3], data, length);

    if (llwrite(buf, length) == -1) {
        perror("Failed to send data packet\n");
        return -1;
    }
    return 1;
}

/// @brief 
/// @param filename 
/// @return Return "1" on success or "-1" on error.
int sendFile(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Failed to open file.\n");
        return -1;
    }

    fileeek(file, 0, SEEK_END);
    int fileSize = ftell(file);
    fileeek(file, 0, SEEK_SET);

    if (sendControlPacket(CONTROL_START, filename, fileSize) == -1) {
        perror("Failed to send start control packet\n");
        return -1;
    }

    uint8_t buf[MAX_PAYLOAD_SIZE - 3];
    size_t bytes_read;
    while ((bytes_read = fread(buf, 1, MAX_PAYLOAD_SIZE - 3, file)) > 0) {
        if (sendDataPacket(buf, bytes_read) == -1) {
            perror("Failed to send data packet\n");
            return -1;
        }
    }

    if (sendControlPacket(CONTROL_END, filename, fileSize) == -1) {
        perror("Failed to send end control packet\n");
        return -1;
    }

    fclose(file);
    return 1;
}

/// @brief
/// @param controlField
/// @param buf
/// @param file_size
/// @param filename
/// @return Return "1" on success or "-1" on error.
int readControlPacket(uint8_t controlField, uint8_t *buf, size_t *file_size, char *filename) {
if (file_size == NULL) {
        perror("Invalid file size pointer\n");
        return -1;
    }

    int size;
    if ((size = llread(buf)) < 0) {
        perror("Failed to read control packet\n");
        return -1;
    }

    if (buf[0] != controlField) {
        perror("Invalid control packet\n");
        return -1;
    }

    uint8_t type;
    size_t length;
    size_t offset = 1;

    while (offset < size) {
        type = buf[offset++];
        if (type == 0X00) {
            length = buf[offset++];
            if (length != sizeof(size_t)) {
                perror("Invalid file size length\n");
                return -1;
            }
            memcpy(file_size, buf + offset, sizeof(size_t));
            offset += sizeof(size_t);
        } else {
            length = buf[offset++];
            if (length > MAX_PAYLOAD_SIZE - offset) {
                perror("Invalid file name length\n");
                return -1;
            }
            memcpy(filename, buf + offset, length);
            offset += length;
        }
    }

    return 1;
}

/// @brief
/// @param filename
/// @return Return "1" on success or "-1" on error.
int receiveFile(const char *filename) {
    uint8_t buf[MAX_PAYLOAD_SIZE];
    size_t file_size;

    char fileSize[255];

    if (readControlPacket(CONTROL_START, buf, &file_size, fileSize) == -1) {
        perror("Failed to read start control packet\n");
        return -1;
    }

    FILE* file;
    if ((file = fopen(filename, "wb")) == NULL) {
        perror("Error opening file\n");
        return -1;
    }

    int size;
    while ((size = llread(buf)) > 0) {
        if (buf[0] == CONTROL_END) {
            break;
        }

        if (buf[0] != CONTROL_DATA) {
            perror("Invalid data packet\n");
            return -1;
        }

        size_t length = buf[1] * 256 + buf[2];
        uint8_t* data = (uint8_t*)malloc(length);
        memcpy(data, buf + 3, size - 3);

        if (fwrite(data, sizeof(uint8_t), length, file) != length) {
            perror("Failed to write to file\n");
            return -1;
        }

        free(data);
    }

    fclose(file);
    return 0;
}

void applicationLayer(const char *serialPort, const char *role, int baudRate, int nTries, int timeout, const char *filename) {
    LinkLayer layer;

    layer.baudRate = baudRate;
    layer.nRetransmissions = nTries;

    if (strcmp(role, "tx") == 0) {
        layer.role = LlTx;
    } else if (strcmp(role, "rx") == 0) {
        layer.role = LlRx;
    } else {
        perror("LinkLayer Role not valid.\n");
    }

    strcpy(layer.serialPort, serialPort);
    layer.timeout = timeout;

    if (llopen(layer) != 1)
        perror("Failed to open connection.\n");

    if (layer.role == LlTx) {
        sendFile(filename);
    } else if (layer.role == LlRx) {
        receiveFile(filename);
    }

    if (llclose(FALSE) != 1) {
        perror("Failed to close connection.\n");
    }
}
