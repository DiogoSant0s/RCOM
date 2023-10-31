#ifndef MACROS_H
#define MACROS_H

// MISC
#define FALSE 0
#define TRUE 1

// Supervision Buffer's
#define FLAG    0x7E

#define A_R     0x01        // Sent by Receiver
#define A_T     0x03        // Sent by Transmitter

#define C_INF0  0x00        // Information Frame 0
#define C_INF1  0x40        // Information Frame 1

#define C_SET   0x03        // Set Up
#define C_UA    0x07        // Unnumbered Acknowledgement
#define C_RR0   0x05        // Receiver Ready 0
#define C_RR1   0x85        // Receiver Ready 1
#define C_REJ0  0x01        // Reject 0
#define C_REJ1  0x81        // Reject 1
#define C_DISC  0x0B        // Disconnect

#define BCC1(a, c) (a^c)    // BCC1

#define ESCAPE 0x7D         // Escape character

#endif // MACROS_H
