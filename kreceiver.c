#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "lib.h"

#define HOST "127.0.0.1"
#define PORT 10001

// parameters:
// -> y - message from which we take the two bytes of crc;
// -> offset - size of data (crc is next to data);
//
// returns: computed crc
unsigned short reconstruct_CRC(msg * y, unsigned char offset)
{
	unsigned char c2 = y->payload[4 + offset];
	unsigned char c1 = y->payload[5 + offset];
	unsigned short rec_crc = (c1 << 8) | c2;
	return rec_crc;
}

// parameters:
// -> r - message ACK to be sent;
// -> t - message NAK to be sent;
// -> pkt - packet to be updated;
// -> data_size - length of data field for obtaining check field position inside the message's payload;
// -> seq - sequence number;
// -> timeout - time to wait;
// -> argv0 - for displaying purposes;
// -> sendinit - flag in case of SENDINIT type message;
// -> end_of_loop - flag to be updated in case of FEOF or FEOT types;
// -> type - type to be verified;
//
// returns: received message or NULL, in case of timeout or lose.
msg *receiverMessageTransmission(msg * r, msg * t, packet * pkt, int *data_size,
				 unsigned char *seq, int timeout, char *argv0,
				 int sendinit, int *end_of_loop, char type)
{
	msg *y;			// received message
	unsigned short crc;	// crc computed in receiver
	unsigned short rec_crc;	// crc received from sender
	int count = 3;		// limit of waiting the same package
	int data_size2 = 1;	// ACK and NAK has an empty data field

	while (count > 0) {
		y = receive_message_timeout(timeout);

		if (y != NULL)	// a message was received
		{
			// compute crcs to check after
			crc = crc16_ccitt(y->payload, y->len - 3);
			*data_size = y->len - LENPKTNODATA;
			rec_crc = reconstruct_CRC(y, *data_size);

			if (sendinit == 1)	// in case of SENDINIT message
			{
				// SENDINIT message has SENDINITSIZE data size
				data_size2 = *data_size;
			}

			if (crc == rec_crc)	// if the message is not corrupted
			{
				// check if it is FEOF or FEOT type of message
				char type_of_packet = y->payload[3];
				if (data_size2 == 1 && type_of_packet == type) {
					*end_of_loop = 1;
				}
				// update sequence number
				*seq = ((unsigned char)y->payload[2] + 1) % 64;
				printf("[%s] CRC equal\n", argv0);
				// update the ACK message to be sent (updates: sequence number and check)
				(*pkt).seq = *seq;
				memcpy(&((*r).payload[2]), &((*pkt).seq), 1);
				crc = crc16_ccitt((*r).payload, (*r).len - 3);
				(*pkt).check = crc;
				memcpy(&((*r).payload[4 + data_size2]),
				       &((*pkt).check), 2);

				printf("[%s] Sending ACK; seqNo = %d\n", argv0,
				       (*r).payload[2]);
				send_message(r);
				*seq = (*seq + 2) % 64;	// update sequence number
				break;
			} else {
				printf("[%s] CRC not equal\n", argv0);
				// update the ACK message to be sent (updates: sequence number and check)
				(*pkt).seq = *seq;
				memcpy(&((*t).payload[2]), &((*pkt).seq), 1);
				crc = crc16_ccitt((*t).payload, (*t).len - 3);
				(*pkt).check = crc;
				memcpy(&((*t).payload[4 + 1]), &((*pkt).check),
				       2);

				printf("[%s] Sending NAK; seqNo = %d\n", argv0,
				       (*t).payload[2]);
				send_message(t);
				*seq = (*seq + 2) % 64;	// update sequence number
				count = 3;
			}
		} else {
			count--;
		}
	}

	return y;
}

int main(int argc, char **argv)
{
	msg r, t;
	unsigned char seq = 1;
	unsigned char maxl;
	int timeout;
	msg *y;

	init(HOST, PORT);

	// sendinit in ACK
	packet pkt;

	// sendinit package data
	s_packet s_pkt;
	initializeSPacket(&s_pkt);
	// end of sendinit package data

	// prepare r for transmission ACK
	prepareMessage(&r, &pkt, SENDINITSIZE, seq, ACK, 0, &s_pkt);
	// end preparing r

	// prepare t for transmission NAK
	prepareMessage(&t, &pkt, 1, seq, NAK, 1, NULL);
	// end preparing t

	int data_size;
	y = receiverMessageTransmission(&r, &t, &pkt, &data_size, &seq,
					(TIME * 1000), argv[0], 1, NULL,
					SENDINIT);

	if (y == NULL) {
		printf("[%s] TIMEOUT REC Send-Init\n", argv[0]);
		return 0;
	} else {
		maxl = (unsigned char)y->payload[4];
		timeout = ((unsigned char)y->payload[5]) * 1000;
	}
	// end of sendinit in ACK

	// transmission
	while (1) {
		int end_of_transmission = 0;

		// file header

		// prepare r for transmission ACK
		prepareMessage(&r, &pkt, 1, seq, ACK, 1, NULL);
		// end preparing r

		// prepare t for transmission NAK
		prepareMessage(&t, &pkt, 1, seq, NAK, 1, NULL);
		// end preparing t

		int length_of_filename = 0;
		y = receiverMessageTransmission(&r, &t, &pkt,
						&length_of_filename, &seq,
						timeout, argv[0], 0,
						&end_of_transmission, FEOT);

		if (y == NULL) {
			printf("[%s] TIMEOUT REC File-Header\n", argv[0]);
			return 0;
		}

		if (end_of_transmission == 1) {
			break;
		}

		char *filename =
		    (char *)malloc(length_of_filename * (sizeof(char)));
		strncpy(filename, (y->payload + 4), length_of_filename);
		char *new_filename =
		    (char *)malloc((length_of_filename + 5) * (sizeof(char)));

		strcpy(new_filename, "recv_");
		strcat(new_filename, filename);
		int file_recv =
		    open(new_filename, O_WRONLY | O_CREAT | O_TRUNC, 0777);
		//end file header

		// file data
		int end_of_file = 0;
		int length_of_filedata = 0;

		while (1) {
			y = receiverMessageTransmission(&r, &t, &pkt,
							&length_of_filedata,
							&seq, timeout, argv[0],
							0, &end_of_file, FEOF);

			if (y == NULL) {
				printf("[%s] TIMEOUT REC File-Data\n", argv[0]);
				return 0;
			}

			if (end_of_file == 1) {
				break;
			} else {
				char *buffer =
				    (char *)malloc(maxl * (sizeof(char)));
				memcpy(buffer, (y->payload + 4),
				       length_of_filedata);
				int written =
				    write(file_recv, buffer,
					  length_of_filedata);
				if (written < 0) {
					printf("Writing file error!\n");
				}
			}
		}

		close(file_recv);
		// end file data
	}
	// end of transmission

	printf("[%s] End of Transmission REC\n", argv[0]);

	return 0;
}
