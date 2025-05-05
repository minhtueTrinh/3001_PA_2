#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "gbn.h"
#include "sr.h"

/* ******************************************************************
Go Back N protocol.  Adapted from J.F.Kurose
ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2

Network properties:
- one way network delay averages five time units (longer if there
are other messages in the channel for GBN), but can be larger
- packets can be corrupted (either the header or the data portion)
or lost, according to user-defined probabilities
- packets will be delivered in the order in which they were sent
(although some can be lost).

Modifications:
- removed bidirectional GBN code and other code not used by prac.
- fixed C style to adhere to current programming style
- added GBN implementation
**********************************************************************/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet
                        MUST BE SET TO 6 when submitting assignment */
#define SEQSPACE 7      /* the min sequence space for GBN must be at least windowsize + 1 */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver
the simulator will overwrite part of your packet with 'z's.  It will not overwrite your
original checksum.  This procedure must generate a different checksum to the original if
the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for ( i=0; i<20; i++ )
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}
/********* Sender (A) variables and functions for Selective Repeat ************/

static struct pkt buffer[SEQSPACE];
static bool A_acked[SEQSPACE];
static int A_left = 0;
static int A_nextseqnum = 0;

void A_output(struct msg message) {
    int i;
    int window_usage = (A_nextseqnum - A_left + SEQSPACE) % SEQSPACE;
    if (window_usage < WINDOWSIZE) {
        struct pkt sendpkt;
        sendpkt.seqnum = A_nextseqnum;
        sendpkt.acknum = NOTINUSE;
        for (i = 0; i < 20; i++) sendpkt.payload[i] = message.data[i];
        sendpkt.checksum = ComputeChecksum(sendpkt);

        buffer[A_nextseqnum] = sendpkt;
        A_acked[A_nextseqnum] = false;

        if (TRACE > 1)
            printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");
        if (TRACE > 0);
            printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
        tolayer3(A, sendpkt);

        if (A_left == A_nextseqnum)
            starttimer(A, RTT);

        A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
    } else {
        if (TRACE > 0)
        printf("----A: New message arrives, send window is full\n");
        window_full++;
    }
}

void A_input(struct pkt packet) {
    if (!IsCorrupted(packet)) {
        if (TRACE > 0)
        printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
        total_ACKs_received++;

        int acknum = packet.acknum;
        if (!A_acked[acknum] && ((acknum - A_left + SEQSPACE) % SEQSPACE < WINDOWSIZE)) {
        A_acked[acknum] = true;
        new_ACKs++;

        if (TRACE > 0)
            printf("----A: ACK %d is not a duplicate\n", acknum);

        while (A_acked[A_left]) {
            A_acked[A_left] = false;
            A_left = (A_left + 1) % SEQSPACE;
        }

        stoptimer(A);
        if (A_left != A_nextseqnum)
            starttimer(A, RTT);
        } else {
        if (TRACE > 0)
            printf("----A: duplicate ACK received, do nothing!\n");
        }
    } else {
        if (TRACE > 0)
        printf("----A: corrupted ACK is received, do nothing!\n");
    }
}

void A_timerinterrupt(void) {
    int i;
    if (TRACE > 0)
        printf("----A: time out, resend packets!\n");

    for (i = 0; i < SEQSPACE; i++) {
        int seq = (A_left + i) % SEQSPACE;
        if (!A_acked[i] &&  ((i - A_left + SEQSPACE) % SEQSPACE < WINDOWSIZE)) {
            if (TRACE > 0){
                printf("---A: resending packet %d\n", seq);
            }
            tolayer3(A, buffer[seq]);
            packets_resent++;
            break;
        }
    }
    starttimer(A, RTT);
}

void A_init(void) {
    int i;
    for (i = 0; i < SEQSPACE; i++) A_acked[i] = false;
    A_left = 0;
    A_nextseqnum = 0;
}

/********* Receiver (B) variables and procedures for Selective Repeat ************/

static int B_base = 0;
static struct pkt B_buffer[SEQSPACE];
static bool received[SEQSPACE];

void B_input(struct pkt packet) {
struct pkt ackpkt;

if (!IsCorrupted(packet)) {
    int seq = packet.seqnum;

    if (!received[seq] && ((seq - B_base + SEQSPACE) % SEQSPACE < WINDOWSIZE)) {
    B_buffer[seq] = packet;
    received[seq] = true;
    packets_received++;

    while (received[B_base]) {
        tolayer5(B, B_buffer[B_base].payload);
        received[B_base] = false;
        B_base = (B_base + 1) % SEQSPACE;
    }
    }

    ackpkt.seqnum = 0;
    ackpkt.acknum = packet.seqnum;
    for (int i = 0; i < 20; i++) ackpkt.payload[i] = '0';
    ackpkt.checksum = ComputeChecksum(ackpkt);
    printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);
    printf("B_input: ACK %d sent\n", packet.seqnum);
    tolayer3(B, ackpkt);
}
}

void B_init(void) {
    for (int i = 0; i < SEQSPACE; i++) received[i] = false;
    B_base = 0;
}

void B_output(struct msg message) {}
void B_timerinterrupt(void) {}
