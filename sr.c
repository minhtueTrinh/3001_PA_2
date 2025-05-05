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

static struct pkt buffer[SEQSPACE]; /* create buffer for all potential packets that may occur in the sender's window*/
static bool A_acked[SEQSPACE]; /*track the status of each packet */
static int A_left = 0; /*the left most or the base or the window*/
static int A_nextseqnum = 0; /*next sequence number to use*/

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message) {
    int i;
    int window_usage = (A_nextseqnum - A_left + SEQSPACE) % SEQSPACE; /*the no of unACKed packets in the sender's window*/
      /* if not blocked waiting on ACK */
    if (window_usage < WINDOWSIZE) {  /*Check whether the window is full*/
        struct pkt sendpkt;
        /*Create packet*/
        sendpkt.seqnum = A_nextseqnum;
        sendpkt.acknum = NOTINUSE;
        for (i = 0; i < 20; i++) sendpkt.payload[i] = message.data[i];
        sendpkt.checksum = ComputeChecksum(sendpkt);

        /*store new packets in sender's buffer at its seqnum --> allows SR if errors occur*/
        buffer[A_nextseqnum] = sendpkt;
        A_acked[A_nextseqnum] = false; /* marked as unACKed --> used for tracking*/

        if (TRACE > 1)
            printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");
        if (TRACE > 0);
            printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
        /*Transmit to B*/
        tolayer3(A, sendpkt);
        /*Start a timer, if this packet is still unACKed --> retransmitted*/
        if (A_left == A_nextseqnum)
            starttimer(A, RTT);
        /*Move to the next packet, +1 sequence number*/
        A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE; /*Wrapping back to 0*/
    } else {
        if (TRACE > 0)
        printf("----A: New message arrives, send window is full\n");
        window_full++;
    }
}
/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet) {
    int i;
    /* if received ACK is not corrupted */
    if (!IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
        total_ACKs_received++;
        /*Check whether the ACK is new in the sender's window*/
        int acknum = packet.acknum;
        if (!A_acked[acknum] && ((acknum - A_left + SEQSPACE) % SEQSPACE < WINDOWSIZE)) {
        A_acked[acknum] = true;
        new_ACKs++;

        if (TRACE > 0)
            printf("----A: ACK %d is not a duplicate\n", acknum);

        /* slide window by the number of packets ACKed */
        while (A_acked[A_left] && A_left != A_nextseqnum){
            A_acked[A_left] = false;
            A_left = (A_left + 1) % SEQSPACE;
        }
        bool ACKedAll = true;
        for (i = 0; i < WINDOWSIZE; i++){
            int index = (A_left + i) % SEQSPACE;
            if (!A_acked[index] && index != A_nextseqnum){
                ACKedAll = false;
                break;
            }
        }
        if (ACKedAll) stoptimer(A);
        else starttimer(A,RTT);
        } else {
        if (TRACE > 0)
            printf("----A: duplicate ACK received, do nothing!\n");
        }
    } else {
        if (TRACE > 0)
        printf("----A: corrupted ACK is received, do nothing!\n");
    }
}

/* called when A's timer goes off */
void A_timerinterrupt(void) {
    int i;
    if (TRACE > 0)
        printf("----A: time out, resend packets!\n");

    for (i = 0; i < WINDOWSIZE; i++) {
        int sequence = (A_left + i) % SEQSPACE; /*calculate the sequnec number of the i-th packet. Wrapped by % SEQSPACE*/
        if (!A_acked[sequence] &&  ((i - A_left + SEQSPACE) % SEQSPACE < WINDOWSIZE)) { /*If not yet acked*/
            if (TRACE > 0){
                printf("---A: resending packet %d\n", sequence);
            }
            tolayer3(A, buffer[sequence]); /*Resend packet*/
            packets_resent++;
            break;
        }
    }
    starttimer(A, RTT);
}
/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void) {
    int i;
    /*Initialise each ACK to false*/
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
    int B_sequence = packet.seqnum;
    if (IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("----B: corrupted packet received, ignoring!\n");
        return;

        if (!received[B_sequence] && ((B_sequence- B_base + SEQSPACE) % SEQSPACE < WINDOWSIZE)) {
        B_buffer[B_sequence] = packet;
        received[B_sequence] = true;
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
        
        if (TRACE > 0)
            printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);
        tolayer3(B, ackpkt);
}
}

void B_init(void) {
    for (int i = 0; i < SEQSPACE; i++) received[i] = false;
    B_base = 0;
}

void B_output(struct msg message) {}
void B_timerinterrupt(void) {}
