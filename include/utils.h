#ifndef UTILS_H
#define UTILS_H

// Calculates the XOR of a array of bytes with a given length
// Returns the result of the XOR
unsigned char BCC2(const unsigned char *buffer, int length);

// Write a Supervision Frame to the serial port with the given Adress and Control fields
// Returns 0 on success, -1 otherwise
int sendSupervisionFrame(int fd, unsigned char a, unsigned char c);

// Reads a frame from the serial port. If timeout is 0, it will wait forever for a frame.
// Returns the size of the frame read, -1 otherwise
int readFrame(int fd, unsigned int timeout, unsigned char* data);

// Stuffes the data with the byte stuffing technique. Only FLAG and ESCAPE equal bytes are stuffed.
// Returns the size of the stuffed data
int stuffData(const unsigned char* data, int dataSize, unsigned char* stuffedData);

// Destuffes the data with the byte stuffing technique. Only FLAG and ESCAPE bytes are destuffed.
// Returns the size of the destuffed data
int destuffData(const unsigned char* stuffedData, int stuffedDataSize, unsigned char* destuffedData);


#endif // UTILS_H