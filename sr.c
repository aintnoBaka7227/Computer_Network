#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "gbn.h"

/* ******************************************************************
   Selective repeat protocol
**********************************************************************/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet */
#define SEQSPACE 12      /* the min sequence space for SR must be at least 2*windowsize*/
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


/********* Sender (A) variables and functions ************/

static struct pkt buffer[WINDOWSIZE];  /* array for storing packets waiting for ACK */
static int windowfirst, windowlast;    /* array indexes of the first/last packet awaiting ACK */
static int windowcount;                /* the number of packets currently awaiting an ACK */
static int A_nextseqnum;               /* the next sequence number to be used by the sender */

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;

  /* if not blocked waiting on ACK */
  if ( windowcount < WINDOWSIZE) {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for ( i=0; i<20 ; i++ ) 
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt); 

    /* put packet in window buffer */
    windowlast = (windowlast + 1) % WINDOWSIZE; 
    buffer[windowlast] = sendpkt;
    windowcount++;

    /* send out packet */
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3 (A, sendpkt);

    /* start timer if first packet in window */
    if (windowcount == 1)
      starttimer(A,RTT);

    /* get next sequence number, wrap back to 0 */
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;  
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

/*create ack[] to track the state of each individual packet (acked or not)*/ 
static bool acked[SEQSPACE];
void A_input(struct pkt packet)
{
  if(!IsCorrupted(packet)) {
    /*checking for new ACKs*/ 
    if (!acked[packet.acknum]) {
      acked[packet.acknum] = true;
      total_ACKs_received++;
      new_ACKs++;
      if (TRACE > 0)
        printf("----A: uncorrupted ACK %d is received\n",packet.acknum);
    }
    
    int seqfirst;
    seqfirst = buffer[windowfirst].seqnum;
    while (windowcount > 0 && acked[seqfirst]) {
      acked[seqfirst] = false;
      windowfirst = (windowfirst + 1) % WINDOWSIZE;
      windowcount--;
    }
    
    stoptimer(A);
    if (windowcount > 0) {
      starttimer(A, RTT);
    }
  }

}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
  if (TRACE > 0)
    printf("----A: time out,resend packets!\n");

  int count; 
  count = 0;
  int i;
  for(i=0; i<windowcount; i++) {

    if (!acked[buffer[(windowfirst+i) % WINDOWSIZE].seqnum]) {
        tolayer3(A, buffer[(windowfirst+i) % WINDOWSIZE]);
        count++;
        if (TRACE > 0)
          printf ("---A: resending packet %d\n", (buffer[(windowfirst+i) % WINDOWSIZE]).seqnum);
        /*
          if (count == 1) {
           starttimer(A, RTT);
          }
        */ 
    }

    if (windowcount > 0) {
      starttimer(A, RTT);
    }
  }
}       



/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
  /* initialise A's window, buffer and sequence number */
  A_nextseqnum = 0;  /* A starts with seq num 0, do not change this */
  windowfirst = 0;
  windowlast = -1;   /* windowlast is where the last packet sent is stored.  
		     new packets are placed in winlast + 1 
		     so initially this is set to -1
		   */
  windowcount = 0;

  /*initialise the acked[]*/ 
  int i;
  for (i=0; i<SEQSPACE; i++)
    acked[i] = false;
}



/********* Receiver (B)  variables and procedures ************/

static int expectedseqnum;
/*track received packet status*/ 
static bool received[SEQSPACE];
/*store received packet*/ 
static struct pkt recv_buffer[SEQSPACE];

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt ackpkt;
  /*Check if the packet is corrupted*/ 
  if (IsCorrupted(packet)) {
    if (TRACE > 0)
        printf("----B: Received corrupted packet, ignoring\n");
    return;
  }

  /*check if the packet is within the receiver's window*/
  int start = expectedseqnum; 
  int end = (expectedseqnum + WINDOWSIZE - 1) % SEQSPACE;
  bool in_window = (start <= end && packet.seqnum >= start && packet.seqnum <= end) || (start > end && (packet.seqnum >= start || packet.seqnum <= end)); 

  if (in_window) {
    ackpkt.acknum = packet.seqnum;
    ackpkt.seqnum = NOTINUSE;
    int i;
    for (i = 0; i < 20; i++) {
      ackpkt.payload[i] = '0';
    }
    ackpkt.checksum = ComputeChecksum(ackpkt); 
    tolayer3(B, ackpkt);

    if (TRACE > 0) {
      printf("----B: packet %d is correctly received, send ACK!\n",packet.seqnum);
    }
    packets_received++;

    if (packet.seqnum == expectedseqnum) {
      tolayer5(B, packet.payload);
      received[packet.seqnum] = true;

      expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
  
      while (received[expectedseqnum]) {
        tolayer5(B, recv_buffer[expectedseqnum].payload);
        received[expectedseqnum] = false;
        expectedseqnum = (expectedseqnum+1)%SEQSPACE;
      }
    }
    else {
      if (!received[packet.seqnum]) {
        recv_buffer[packet.seqnum] = packet; 
        received[packet.seqnum] = true;
      }
    }
  } 
  else {
    if (TRACE > 0) {
      printf("----B: Packet %d is outside the window, ignoring\n", packet.seqnum);
    }
  }
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  expectedseqnum = 0; 
  int i;
  for (i = 0; i < SEQSPACE; i++) {
    received[i] = false; 
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

