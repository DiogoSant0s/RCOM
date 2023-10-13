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

struct termios oldtio;
struct termios newtio;

volatile int STOP = FALSE;

int alarmEnabled = FALSE;
int alarmCount = 0;

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
int sendSupervisionBuffer(unsigned char a, unsigned char c) {
    // Create string to send
    unsigned char buffer[5];

    buffer[0] = FLAG;
    buffer[1] = a;
    buffer[2] = c;
    buffer[3] = a ^ c;
    buffer[4] = FLAG;

    return write(fd, buffer, sizeof(buffer));
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) {

    // Set alarm function handler
    (void)signal(SIGALRM, alarmHandler);

    layer = connectionParameters;

    int fd = open(layer.serialPort, O_RDWR | O_NOCTTY);

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

        // Loop for input
        while (STOP == FALSE && alarmCount < layer.nRetransmissions) {
            if (alarmEnabled == FALSE) {
                // Send SET
                int bytes = sen;

                alarmEnabled = TRUE;
                alarm(layer.timeout);
            }

            }
        }
    }
    else if (layer.role == LlRx) {
    }
    else {
        printf("Invalid role\n");
        exit(-1);
    }

    return 1;
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
