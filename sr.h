#ifndef SR_H
#define SR_H

/* Constants for Selective Repeat */
#define RTT  16.0
#define WINDOWSIZE 6
#define SEQSPACE 12   /* Must be at least 2 * WINDOWSIZE */
#define NOTINUSE (-1)

#define A 0
#define B 1

/* Function declarations (defined in sr.c) */
void A_init(void);
void A_output(struct msg);
void A_input(struct pkt);
void A_timerinterrupt(void);
void B_init(void);
void B_input(struct pkt);
void B_output(struct msg);     /* Optional */
void B_timerinterrupt(void);   /* Optional */

#endif /* SR_H */
