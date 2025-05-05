#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
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

static struct pkt buffer[WINDOWSIZE]; /* create buffer for all potential packets that may occur in the sender's window*/
static bool acked[WINDOWSIZE]; /*track the status of each packet */
static int windowcount;
static int A_left = 0; /*the left most or the base or the window*/
static int A_nextseqnum = 0; /*next sequence number to use*/
static int timer_sequence;
static int timer[WINDOWSIZE];

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message) {
    int i;
    struct pkt sendpkt;
    /* if not blocked waiting on ACK */
    if (windowcount < WINDOWSIZE) {  /*Check whether the window is full*/
        if (TRACE > 1) printf("----A: New message arrives, send window is not full, send new message to layer3!\n");
        /*Create packet*/
        sendpkt.seqnum = A_nextseqnum;
        sendpkt.acknum = NOTINUSE;
        for (i = 0; i < 20; i++) sendpkt.payload[i] = message.data[i];
        sendpkt.checksum = ComputeChecksum(sendpkt);

        /*store new packets in sender's buffer at its seqnum --> allows SR if errors occur*/
        buffer[A_nextseqnum % WINDOWSIZE] = sendpkt; /*Wrapped by WINDOWSIZE*/
        acked[A_nextseqnum % WINDOWSIZE] = false; /* marked as unACKed --> used for tracking*/

        if (TRACE > 0);
            printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
        /*Transmit to B*/
        tolayer3(A, sendpkt);

        /*Start timer if first packet in the window*/
        if (windowcount == 1){
            starttimer(A, RTT);
            timer_sequence = sendpkt.seqnum;
        }
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
    int index;
    int sequence;
    int window_end = (A_left + WINDOWSIZE - 1) % SEQSPACE;
    bool IsInWindow;
    int acknum = packet.acknum;
    index = acknum % WINDOWSIZE;

    /* if received ACK is not corrupted */
    if (!IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
        total_ACKs_received++;
        /*Check whether the ACK is new in the sender's window*/

        if (TRACE > 0)
            printf("----A: ACK %d is not a duplicate\n", acknum);
        total_ACKs_received++;

        if (A_left <= window_end){
            IsInWindow = (acknum >= A_left && acknum <= window_end);
        } else{
            /*Wrap around SEQSPACE*/
            IsInWindow = (acknum >=  A_left || acknum <= window_end);
        }     

        if (!IsInWindow){
            if (TRACE > 0) printf("----A: ACK %d outside current window, do nothing!\n", acknum);
            return;
        }
            new_ACKs++;

        /* slide window by the number of packets ACKed */
        if (acked[index]){
            if (TRACE > 0) printf("----A: corrupted ACK is received, do nothing!\n");
            return;
        }
        if (TRACE > 0)
            printf("----A: duplicate ACK received, do nothing!\n");

        new_ACKs++;
        acked[index] = true;
        stoptimer(A);

        if (acknum == A_left){
            while (acked[A_left % WINDOWSIZE]){
                acked[A_left % WINDOWSIZE] = false;
                A_left = (A_left + 1) % SEQSPACE;
                windowcount--;
                if (windowcount == 0) break;
            }
        }
        if (windowcount > 0) {
            for (i = 0; i < WINDOWSIZE; i++){
                sequence = (A_left + i) % SEQSPACE;
                if (sequence == A_nextseqnum)
                    break;
                index = sequence % WINDOWSIZE;
                if (!acked[index]){
                    starttimer(A, RTT);
                    break;
                }
            }
        }
    }
}

/* called when A's timer goes off */
void A_timerinterrupt(void) {
    int i;
    int index;

    if (TRACE > 0)
        printf("----A: time out, resend packets!\n");
    if (windowcount > 0){
        for (i = 0; i < WINDOWSIZE; i++){
            index = (A_left + i) % SEQSPACE % WINDOWSIZE;
            if (!acked[index] && (A_left + 1) % SEQSPACE != A_nextseqnum){
                if (TRACE > 0) printf("----A: resending packet %d\n", buffer[index].seqnum);

            tolayer3(A, buffer[index]);
            packets_resent++;
            starttimer(A, RTT);
            break;
            }
        }
    }
}
/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void) {
    int i;
    A_left = 0;
    A_nextseqnum = 0; /*A starts with 0*/
    windowcount = 0;
    for (i = 0; i < WINDOWSIZE; i++){
        acked[i] = true;
        timer[i] = NOTINUSE;
    }
}

/********* Receiver (B) variables and procedures for Selective Repeat ************/

static int B_expectedseqnum;
static int B_nextseqnum;
static int B_base;
static struct pkt B_buffer[WINDOWSIZE];
static bool received[WINDOWSIZE];

void B_input(struct pkt packet) {
    struct pkt sendpkt;
    int i;
    int window_index;
    
    int B_sequence = packet.seqnum;
    /*Calculate the window position*/
    window_index = (B_sequence - B_base + SEQSPACE) % SEQSPACE;

    if (!IsCorrupted(packet)) {
        if (TRACE > 0) printf("----B: packet %d is correctly received, send ACK!\n",packet.seqnum);
    packets_received++; /*Increase  packet received*/
        
    if (window_index < WINDOWSIZE){
        if (!received[window_index]){
            B_buffer[window_index] = packet;
            received[window_index] = true;

            if (window_full == 0){
                while (received[0]){
                    tolayer5(B, B_buffer[0].payload);

                    /*Slide window and shift packet fwd*/
                    for (i = 0; i < WINDOWSIZE - 1; i++){
                        received[i] = received[i + 1];
                        B_buffer[i] = B_buffer[i+1];
                    }
                    /*Change the state of the last window*/
                    received[WINDOWSIZE - 1] = false;

                    /*Move the slide forward the seqspace*/
                    B_base = (B_base + 1) % SEQSPACE;
                    }
                }
            }
        }
    }
}

void B_init(void) {
    int i; 
    B_expectedseqnum = 0;
    B_nextseqnum = 1;
    B_base = 0;

    for (i = 0; i < WINDOWSIZE; i++){
        received[i] =false;
    }

}

void B_output(struct msg message) {}
void B_timerinterrupt(void) {}
