#ifndef SR_H
#define SR_H

#include <stdbool.h>

#define RTT  16.0
#define WINDOWSIZE 6
#define SEQSPACE 12   // must be at least 2 * WINDOWSIZE for Selective Repeat
#define NOTINUSE (-1)

#define A 0
#define B 1

// Message passed from layer 5 to layer 4
struct msg {
    char data[20];
};

// Packet passed between layer 4 and layer 3
struct pkt {
    int seqnum;
    int acknum;
    int checksum;
    char payload[20];
};

// Function declarations for sender and receiver logic
void A_init(void);
void B_init(void);
void A_output(struct msg message);
void A_input(struct pkt packet);
void A_timerinterrupt(void);
void B_input(struct pkt packet);
void B_output(struct msg message);
void B_timerinterrupt(void);

// Provided by emulator
extern void tolayer3(int AorB, struct pkt packet);
extern void tolayer5(int AorB, char data[20]);
extern void starttimer(int AorB, double increment);
extern void stoptimer(int AorB);

// Trace and statistics
extern int TRACE;
extern int total_ACKs_received;
extern int packets_resent;
extern int new_ACKs;
extern int packets_received;
extern int window_full;

#endif // SR_H
