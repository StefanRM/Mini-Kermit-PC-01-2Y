#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "lib.h"

#define HOST "127.0.0.1"
#define PORT 10000
#define SIZEOFPROMPT 50

// parameters:
// -> t - message to be sent;
// -> pkt - packet to be updated;
// -> data_size - length of data field for obtaining check field position inside the message's payload;
// -> seq - sequence number;
// -> timeout - time to wait;
// -> argv0, screen_prompt - for displaying purposes.
//
// returns: received message or NULL, in case of timeout or lose.
msg *senderMessageTransmission(msg * t, packet * pkt, unsigned char data_size,
			       int *seq, int timeout, char *argv0,
			       char *screen_prompt)
{
	msg *y;			// received message
	int count = 3;		// limit of resending
	unsigned short crc;	// crc to be added in case of NAK

	while (1) {
		y = receive_message_timeout(timeout);

		if (y != NULL)	// a message was received
		{
			if (y->payload[3] == ACK)	// message succesfully received
			{
				printf("[%s] ACK\n", argv0);
				// update sequence number
				*seq = ((unsigned char)y->payload[2] + 1) % 64;
				break;
			} else {
				printf("[%s] NAK\n", argv0);
				// update the new message to be sent (updates: sequence number and check)
				(*pkt).seq = *seq;
				memcpy(&((*t).payload[2]), &((*pkt).seq), 1);
				crc = crc16_ccitt((*t).payload, (*t).len - 3);
				(*pkt).check = crc;
				memcpy(&((*t).payload[4 + data_size]),
				       &((*pkt).check), 2);
				printf("[%s] %s %d\n", argv0, screen_prompt,
				       (unsigned char)(*t).payload[2]);
				// send the updated message
				send_message(t);
				*seq = (*seq + 2) % 64;	// update sequence number
				count = 3;
			}
		} else {
			count--;
			if (count == 0) {
				break;
			}

			printf("[%s] %s %d\n", argv0, screen_prompt,
			       (unsigned char)(*t).payload[2]);
			// resend last message
			send_message(t);
		}
	}

	return y;
}

int main(int argc, char **argv)
{
	msg t;
	msg *y;
	int seq = 0;
	unsigned char maxl;
	int timeout;
	char *screen_prompt = (char *)malloc(SIZEOFPROMPT * sizeof(char));

	init(HOST, PORT);

	// sendinit
	packet pkt;

	// sendinit package data
	s_packet s_pkt;
	initializeSPacket(&s_pkt);
	// end of sendinit package data

	prepareMessage(&t, &pkt, SENDINITSIZE, seq, SENDINIT, 0, &s_pkt);

	printf("[%s] Sending Send-Init; seqNo = %d\n", argv[0],
	       (unsigned char)t.payload[2]);
	send_message(&t);
	seq = (seq + 2) % 64;	// update sequence number

	strcpy(screen_prompt, "Sending Send-Init; seqNo =\0");
	y = senderMessageTransmission(&t, &pkt, SENDINITSIZE, &seq, TIME * 1000,
				      argv[0], screen_prompt);

	if (y == NULL)		// no message received after resending 3 times
	{
		printf("[%s] TIMEOUT Send-Init\n", argv[0]);
		return 0;
	} else {
		// keep the characteristics of receiver
		maxl = (unsigned char)y->payload[4];
		timeout = ((unsigned char)y->payload[5]) * 1000;
	}
	//end of sendinit

	//file transmitting
	int i;
	for (i = 1; i < argc; ++i) {
		// file header
		int length_of_filename = strlen(argv[i]);
		prepareMessage(&t, &pkt, length_of_filename, seq, FHEADER, 0,
			       argv[i]);

		printf("[%s] Sending File-Header; seqNo = %d\n", argv[0],
		       (unsigned char)t.payload[2]);
		send_message(&t);
		seq = (seq + 2) % 64;	// update sequence number

		strcpy(screen_prompt, "Sending File-Header; seqNo =\0");
		y = senderMessageTransmission(&t, &pkt, length_of_filename,
					      &seq, timeout, argv[0],
					      screen_prompt);

		if (y == NULL)	// no message received after resending 3 times
		{
			printf("[%s] TIMEOUT File-Header\n", argv[0]);
			return 0;
		}
		//end file header

		// file data
		int file_send = open(argv[i], O_RDONLY);	// file to be sent
		int content_read;	// number of read bytes from file_send
		char *buffer = (char *)malloc(maxl * (sizeof(char)));	// store read bytes

		while ((content_read = read(file_send, buffer, maxl)) > 0) {
			prepareMessage(&t, &pkt, content_read, seq, FDATA, 0,
				       buffer);

			printf("[%s] Sending File-Data; seqNo = %d\n", argv[0],
			       (unsigned char)t.payload[2]);
			send_message(&t);
			seq = (seq + 2) % 64;	// update sequence number

			strcpy(screen_prompt, "Sending File-Data; seqNo =\0");
			y = senderMessageTransmission(&t, &pkt, content_read,
						      &seq, timeout, argv[0],
						      screen_prompt);

			if (y == NULL)	// no message received after resending 3 times
			{
				printf("[%s] TIMEOUT File-Data\n", argv[0]);
				return 0;
			}
			// end of reading file
		}

		close(file_send);
		// end file data

		// end of file
		prepareMessage(&t, &pkt, 1, seq, FEOF, 1, NULL);

		printf("[%s] Sending File-End; seqNo = %d\n", argv[0],
		       (unsigned char)t.payload[2]);
		send_message(&t);
		seq = (seq + 2) % 64;	// update sequence number

		strcpy(screen_prompt, "Sending File-End; seqNo =\0");
		y = senderMessageTransmission(&t, &pkt, 1, &seq, timeout,
					      argv[0], screen_prompt);

		if (y == NULL)	// no message received after resending 3 times
		{
			printf("[%s] TIMEOUT File-End\n", argv[0]);
			return 0;
		}
		// end of end of file
	}

	// end of transmission
	prepareMessage(&t, &pkt, 1, seq, FEOT, 1, NULL);

	printf("[%s] Sending Transmission-End; seqNo = %d\n", argv[0],
	       (unsigned char)t.payload[2]);
	send_message(&t);
	seq = (seq + 2) % 64;	// update sequence number

	strcpy(screen_prompt, "Sending Transmission-End; seqNo =\0");
	y = senderMessageTransmission(&t, &pkt, 1, &seq, timeout, argv[0],
				      screen_prompt);

	if (y == NULL)		// no message received after resending 3 times
	{
		printf("[%s] TIMEOUT Transmission-End\n", argv[0]);
		return 0;
	}
	// end of end of transmission

	printf("[%s] End of Transmission\n", argv[0]);

	return 0;
}
