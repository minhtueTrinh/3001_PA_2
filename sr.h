#define BIDIRECTIONAL 0

/* Function declarations (defined in sr.c) */
void A_init(void);
void A_output(struct msg);
void A_input(struct pkt);
void A_timerinterrupt(void);
void B_init(void);
void B_input(struct pkt);
extern void B_output(struct msg);
extern void B_timerinterrupt(void);
