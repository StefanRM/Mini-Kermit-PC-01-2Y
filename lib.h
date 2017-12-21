#ifndef LIB
#define LIB

#define SENDINIT 'S'		// Send-Init type of message
#define FHEADER 'F'		// File-Header type of message
#define FDATA 'D'		// File-Data type of message
#define FEOF 'Z'		// File-End type of message
#define FEOT 'B'		// Transmission-End type of message
#define ACK 'Y'			// ACK type of message
#define NAK 'N'			// NAK type of message
#define ERROR 'E'		// Error type of message

#define LENPKTNODATA 7		// length of packet without data field
#define SENDINITSIZE 11		// send-init data field size
#define MAXL 250		// MAXL to be used before it is received from send-init
#define TIME 5			// timeout to be used before it is received from send-init

// message
typedef struct {
	int len;
	char payload[1400];
} msg;

// packet to be put in message
typedef struct {
	char soh;
	char len;
	char seq;
	char type;
	char *data;
	unsigned short check;
	char mark;
} packet;

// send-init data-filed structure
typedef struct {
	char maxl;
	char s_time;
	char npad;
	char padc;
	char eol;
	char qctl;
	char qbin;
	char chkt;
	char rept;
	char capa;
	char r;
} s_packet;

// initializing Send-Init data field to be put in packet
void initializeSPacket(s_packet * s_pkt)
{
	(*s_pkt).maxl = MAXL;
	(*s_pkt).s_time = TIME;
	(*s_pkt).npad = 0x00;
	(*s_pkt).padc = 0x00;
	(*s_pkt).eol = 0x0D;
	(*s_pkt).qctl = 0x00;
	(*s_pkt).qbin = 0x00;
	(*s_pkt).chkt = 0x00;
	(*s_pkt).rept = 0x00;
	(*s_pkt).capa = 0x00;
	(*s_pkt).r = 0x00;
}

void init(char *remote, int remote_port);
void set_local_port(int port);
void set_remote(char *ip, int port);
int send_message(const msg * m);
int recv_message(msg * r);
msg *receive_message_timeout(int timeout);	//timeout in milliseconds
unsigned short crc16_ccitt(const void *buf, int len);

// parameters:
// -> t - message to be prepared;
// -> pkt - packet to be updated and from which the bytes are copied;
// -> data_size - length of data field for obtaining check field position inside the message's payload;
// -> seq - sequence number;
// -> type - type of message;
// -> no_data - flag to indicate if the message does not have any data;
// -> data_to_copy - address where the data is stored before memcopied;
//
// returns: received message or NULL, in case of timeout or lose.
void prepareMessage(msg * t, packet * pkt, unsigned char data_size, int seq,
		    char type, int no_data, void *data_to_copy)
{
	(*pkt).soh = 0x01;
	(*pkt).len = LENPKTNODATA + data_size - 2;
	(*pkt).seq = seq;
	(*pkt).type = type;

	if (no_data == 0)	// the message has data
	{
		(*pkt).data = (char *)malloc(data_size * sizeof(char));
		memcpy((*pkt).data, data_to_copy, data_size);
	} else {
		(*pkt).data = NULL;
	}

	memset((*t).payload, 0, 1400);	// reset message
	(*t).len = ((unsigned char)(*pkt).len) + 2;

	// put information in the message
	memcpy(&((*t).payload[0]), &((*pkt).soh), 1);
	memcpy(&((*t).payload[1]), &((*pkt).len), 1);
	memcpy(&((*t).payload[2]), &((*pkt).seq), 1);
	memcpy(&((*t).payload[3]), &((*pkt).type), 1);

	if (no_data == 0)	// the message has data
	{
		memcpy(&((*t).payload[4]), (*pkt).data, data_size);
	}
	// compute crc
	unsigned short crc = crc16_ccitt((*t).payload, (*t).len - 3);
	(*pkt).check = crc;
	(*pkt).mark = 0x0D;

	// put crc and mark information in the message
	memcpy(&((*t).payload[4 + data_size]), &((*pkt).check), 2);
	memcpy(&((*t).payload[4 + data_size + 2]), &((*pkt).mark), 1);
}

#endif
