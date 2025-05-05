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

  checksum = packet.seqnum + packet.acknum;
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


/********* Sender (A) variables and functions ************/
static struct pkt buffer[SEQSPACE]; /* create buffer for all potential packets that may occur in the sender's window*/
static bool acked[SEQSPACE]; /*track the status of each packet */
static int A_left = 0; /*the left most or the base or the window*/
static int A_nextseqnum = 0; /*next sequence number to use*/ 

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;
  int window_unACKed = (A_nextseqnum - A_left + SEQSPACE) % SEQSPACE; /*the no of unACKed packets in the sender's window*/

  /* if not blocked waiting on ACK */
  if (window_unACKed< WINDOWSIZE) { /*Check whether the window is full*/
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for ( i=0; i<20 ; i++ )
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    /*store new packets in sender's buffer at its seqnum --> allows SR if errors occur*/
    buffer[A_nextseqnum] = sendpkt;
    acked[A_nextseqnum] = false; /* marked as unACKed --> used for tracking*/

    /*Transmit to B*/
    tolayer3(A, sendpkt);

    /*Start a timer, if this packet is still unACKed --> retransmitted*/
    starttimer(A, RTT);
    
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", A_nextseqnum);
    
    /*Move to the next packet, +1 sequence number*/
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE; /*wrapping back to 0*/
  }
  /* if blocked,  window is full */
  else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
      window_full++;
  }
}


/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{

  /* if received ACK is not corrupted */
  if (!IsCorrupted(packet)) {
    int acknumber = packet.acknum;
    int ack_index = (acknumber - A_left + SEQSPACE) % SEQSPACE;

    /*Check whether the ACK is new in the sender's window*/
    if(!acked[acknumber] && (ack_index < WINDOWSIZE)){
        acked[acknumber] = true;
        new_ACKs++;
        stoptimer(A);
    }
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n", acknumber);


	    /* slide window by the number of packets ACKed */
        while (acked[A_left]){
            acked[A_left] = false;
            A_left = (A_left + 1) % SEQSPACE;
        }

	    /* start timer again if there are still more unacked packets in window */
        if (A_left != A_nextseqnum)
            starttimer(A,RTT);
    }else{
        if (TRACE > 0){
            printf ("----A: corrupted ACK is received, do nothing!\n");
        }
    }
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
    int i;
    if (TRACE > 0)
        printf("----A: time out,resend packets!\n");
    for (i = 0; i < WINDOWSIZE; i++){
        int sequence = (A_left + i) % SEQSPACE; /*calculate the sequnec number of the i-th packet. Wrapped by % SEQSPACE*/
        if (!acked[sequence]){ /*If not yet acked*/
            tolayer3(A, buffer[sequence]); /*Resend packet*/
            packets_resent++;
            if (TRACE > 0)
                printf("---A: resending packet %d\n");
        }
    }
    starttimer(A,RTT);
}


/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
    int i; 
    /*Initialise each ACK to false*/
    for (i = 0; i < SEQSPACE; i++){
        acked[i] = false;
    }
    A_left = 0; /*Start at the base of the sender window*/
    A_nextseqnum = 0;
}



/********* Receiver (B)  variables and procedures ************/
static int B_base=0; /*base*/
static struct pkt B_buffer[SEQSPACE]; /*store received packets*/
static bool received[SEQSPACE]; /*flag to track which seq has been recieved*/

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt sendpkt;

  /* if not corrupted and received packet is in order */
  if (!IsCorrupted(packet)){
    int sequence = packet.seqnum; /*declare varibale to manage and call easier*/
    /*if not processed and the seqnum stays within the window*/
    if (!received[sequence] && ((sequence - B_base + SEQSPACE)%SEQSPACE <WINDOWSIZE)){
       /*Save and marked as received*/
        B_buffer[sequence] = packet; 
        received[sequence] = true;
        packets_received++;
        
        /*Once received send to layer 5*/
        while (received[B_base]){
            tolayer5(B, B_buffer[B_base].payload);
            received[B_base]=false;
            B_base = (B_base + 1) % SEQSPACE; /*move to the next seq num*/
        }
    }
    /*ACK packet*/
    sendpkt.seqnum = 0;
    sendpkt.acknum = packet.seqnum;
    tolayer3(B, sendpkt);
    }
}
/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
    for (int i = 0; i < SEQSPACE; i++){
        received[i] = false;
    B_base = 0;
    }
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}
