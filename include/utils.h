#ifndef UTILS_H
#define UTILS_H

unsigned char BCC2(const unsigned char *buffer, int length);

int sendSupervisionFrame(int fd, unsigned char a, unsigned char c);

int readFrame(int fd, unsigned int timeout, unsigned char* data);

int stuffData(const unsigned char* data, int dataSize, unsigned char* stuffedData);

int destuffData(const unsigned char* stuffedData, int stuffedDataSize, unsigned char* destuffedData);


#endif // UTILS_H