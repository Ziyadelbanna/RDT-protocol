
#include <iostream>
#include <stdio.h>
#include <stdlib.h>	/* needed for os x*/
#include <string.h>	/* for strlen */
#include <netdb.h>      /* for gethostbyname() */
#include <sys/socket.h>
#include <netinet/in.h>
#include <fstream>
#include <vector>
#define FILE_FIN 150



using namespace std;

int protocol; //= 2;//0 for stop and wait, 1 for selective repeat, 2 for GBN

string serverIP="";
int serverPort;
int clientPort;
int intialWindowSize;
ofstream  writeFile;
struct sockaddr_in clientAddr; /* our address */
struct sockaddr_in serverAddr; /* server address */
socklen_t addrlen;
ifstream infoFile;
ofstream fp;
char fileName[100];



/* Data-only packets */
struct packet {
	/* Header */
	uint16_t cksum; /* Optional bonus part */
	uint16_t len;
	uint32_t seqno;
	/* Data */
	char data[500]; /* Not always 500 bytes, can be less */
};

/* Ack-only packets are only 8 bytes */
struct ack_packet {
	uint16_t cksum; /* Optional bonus part */
	uint16_t len;
	uint32_t ackno;

};

vector<packet*> *buffer = new vector<packet*>;
int bufferCount = 0;
void receiveStopAndWait();
void receiveGBN();
void receiveSelectiveRepeat();
void connectToServer();
void disconnect(void);
int getPacketLength(char[]);
void extractPacket(packet*, char[], int);
void sendAck(packet*);
void makeAckChunk(char[], ack_packet*);
int addDataToFile(ofstream*, packet*);
void openFile(char*, ofstream*);

void readInputFile()
{
    string line;
    ifstream myfile ("client.in");
    if (myfile.is_open())
    {
        if( getline (myfile,line) )
        {
            serverIP=line;
        }
        if( getline (myfile,line) )
        {
            serverPort=atoi(line.c_str());
        }

        if( getline (myfile,line) )
        {
            clientPort=atoi(line.c_str());
        }
        if( getline (myfile,line) )
        {
            strcpy(fileName,line.c_str());
        }
        if( getline (myfile,line) )
        {
            intialWindowSize=atoi(line.c_str());
        }
				if( getline (myfile,line) )
        {
            protocol=atoi(line.c_str());
        }

        myfile.close();
    }


}
void parseAckPacket(char *buffer, int buffLength, ack_packet* packet) {
	//convert char to unsigned char
	unsigned char b0 = buffer[0];
	unsigned char b1 = buffer[1];
	unsigned char b2 = buffer[2];
	unsigned char b3 = buffer[3];
	unsigned char b4 = buffer[4];
	unsigned char b5 = buffer[5];
	unsigned char b6 = buffer[6];
	unsigned char b7 = buffer[7];
	//checksum combine first two bytes
	packet->cksum = (b0 << 8) | b1;
	//len combine second two bytes
	packet->len = (b2 << 8) | b3;
	//seq_no combine third four bytes
	packet->ackno = (b4 << 24) | (b5 << 16) | (b6 << 8) | (b7);
}

int main(int argc, char **argv) {

	readInputFile();
	connectToServer();
	return 0;
}

int clientSocket;

int checksum(char *a){

int sum=0;
int i=0;
    while(a[i]!='\0')
    {
     sum+=(int)a[i];
     i++;
    }
  sum = ~sum;
return sum;
}

void connectToServer() {
	struct hostent *hp; /* server information */
	unsigned int alen; /* address length when we get the port number */
	addrlen = sizeof(serverAddr);

	if ((clientSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		perror("cannot create socket");
		exit(0);
	}

	memset((char *) &clientAddr, 0, sizeof(clientAddr));
	clientAddr.sin_family = AF_INET;
	clientAddr.sin_addr.s_addr = htonl(INADDR_ANY );
	clientAddr.sin_port = htons(clientPort);
	alen = sizeof(clientAddr);

	memset((char*) &serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(serverPort);
	hp = gethostbyname(serverIP.c_str());
	if (!hp) {
		cout<<"server not found"<<endl;
		exit(0);
	}

	memcpy((void *) &serverAddr.sin_addr, hp->h_addr_list[0], hp->h_length);
	char *dataSend;
	dataSend = fileName;
	int sendFlag = sendto(clientSocket, dataSend, strlen(dataSend), 0,
			(struct sockaddr *) &serverAddr, sizeof(serverAddr));

	openFile(fileName, &fp);
	if (protocol==1) {

		cout << "SELECTIVE REPEAT PROTOCOL" << endl;
		receiveSelectiveRepeat();
	} else if (protocol==0) {
		cout << "STOP AND WAIT PROTOCOL" << endl;
		receiveStopAndWait();
	} else if (protocol==2) {
        cout << "GBN PROTOCOL" << endl;
        receiveGBN();
	}
	fp.close();
	disconnect();
}
void convertAckPacketToByte(ack_packet *packet, char* buffer) {

	//chksum

	buffer[0] = packet->cksum >> 8;
	buffer[1] = (packet->cksum) & 255;

	//len field

	buffer[2] = packet->len >> 8;
	buffer[3] = (packet->len) & 255;

	//seqnumber

	buffer[4] = packet->ackno >> 24;
	buffer[5] = (packet->ackno >> 16) & 255;
	buffer[6] = (packet->ackno >> 8) & 255;
	buffer[7] = (packet->ackno) & 255;
}

void sendAck(int seqno) {
	char ackData[8];
	ack_packet Ack;
	Ack.ackno = seqno;
	Ack.cksum = 1;
	Ack.len = 8;
	convertAckPacketToByte(&Ack, ackData);
	//cout<<"Sending ack to server on new port : "<< serverAddr.sin_addr.s_addr<<endl;
	int sendFlag = sendto(clientSocket, ackData, 8, 0, (struct sockaddr *) &serverAddr,
			sizeof(serverAddr));
	if (sendFlag == -1)
		perror("receive New Port Server ACK Error");
}

uint16_t calculateChecksum(char const *buf) {
    int i = 0;

    uint32_t sum = 0;
    while (buf[i]!='\0') {
      sum += (((buf[i] << 8) & 0xFF00) | ((buf[i + 1]) & 0xFF));
      if ((sum & 0xFFFF0000) > 0) {
        sum = sum & 0xFFFF;
        sum += 1;
      }
      i += 2;
    }
    if (buf[i]!='\0') {
      sum += (buf[i] << 8 & 0xFF00);
      if ((sum & 0xFFFF0000) > 0) {
        sum = sum & 0xFFFF;
        sum += 1;
      }
    }
    sum = ~sum;
    sum = sum & 0xFFFF;
    return sum;

  }
void receiveStopAndWait() {
	int dublicateCount = 0;
	bool isfirstRecieve = true;
	char data[512];
	int recvLen = 1;
	int currentlyReceivedBytes = 0;
	int packetLength = 0;
	int packetCount = 0;
	while (1) {

		currentlyReceivedBytes = 0;
		isfirstRecieve = true;
		while (1) {
			recvLen = recvfrom(clientSocket, data, 512, 0, (struct sockaddr *) &serverAddr,
					&addrlen);
			if (isfirstRecieve) {
				packetLength = getPacketLength(data) + 8;
				isfirstRecieve = false;
			}
			if (packetLength == FILE_FIN + 8) {
				cout << "File received Successfully" << endl;
				break;
			}

			currentlyReceivedBytes += recvLen;
			if (currentlyReceivedBytes == packetLength || recvLen == 0) {
				break;
			}
		}

		packet recvdPacket;
        uint16_t prev =0;
		extractPacket(&recvdPacket, data, recvLen);
		string st = (string)recvdPacket.data;
        vector<char> bytes(st.begin(), st.end());
        bytes.push_back('\0');
        char *c = &bytes[0];
        uint16_t cksum = (uint16_t)calculateChecksum(c);

		//send acks here
		prev = recvdPacket.cksum;
		if (recvdPacket.seqno == packetCount) {
			sendAck(recvdPacket.seqno);
			addDataToFile(&fp, &recvdPacket);
			packetCount++;
			packetCount = packetCount % 2;
		} else {
			dublicateCount++;
			sendAck(recvdPacket.seqno);
		}

		if (packetLength == FILE_FIN + 8) {
			break;
		}


	}
}
void receiveSelectiveRepeat() {
	/* now loop, receiving data and printing what we received */
	bool isfirstRecieve = true;
	char data[512];
	int recvLen = 1;
	int currentlyReceivedBytes = 0;
	int packetLength = 0;
	int packetCount = 0;

	uint16_t prev =0;

	while (1) {
		currentlyReceivedBytes = 0;
		isfirstRecieve = true;

		while (1) {
			recvLen = recvfrom(clientSocket, data, 512, 0, (struct sockaddr *) &serverAddr,
					&addrlen);
			if (isfirstRecieve) {
				packetLength = getPacketLength(data) + 8;
				isfirstRecieve = false;
			}
			if (packetLength == FILE_FIN + 8) {
				break;
			}

			currentlyReceivedBytes += recvLen;
			if (currentlyReceivedBytes == packetLength || recvLen == 0) {
				break;
			}
		}

		packet *recvdPacket = new packet;
		extractPacket(recvdPacket, data, recvLen);
		string st(recvdPacket->data, strlen(recvdPacket->data));
        vector<char> bytes(st.begin(), st.end());
        bytes.push_back('\0');
        char *c = &bytes[0];
        int n = checksum(c)& 255;
        uint16_t cksum = (uint16_t)n;

		prev = recvdPacket->cksum;
		sendAck(recvdPacket->seqno);
		if (recvdPacket->seqno < packetCount) {
	} else if (recvdPacket->seqno > packetCount&&buffer->size()<=intialWindowSize) {
			buffer->push_back(recvdPacket);
			++bufferCount;
		} else {
			addDataToFile(&fp, recvdPacket);
			++packetCount;
			int tempBufferCount = bufferCount;

			for (int i = 0; i < tempBufferCount; ++i) {
				packet* currPacket = buffer->at(0);
				if (currPacket->seqno == packetCount) {
					addDataToFile(&fp, currPacket);
					++packetCount; // update expected seqno
					--bufferCount;
					buffer->erase(buffer->begin());
				} else {
					//gap detected
					break;
				}
			}
		}

		if (packetLength == FILE_FIN + 8) {
			break;
		}


	}
}

void receiveGBN() {
	bool isfirstRecieve = true;
	char data[512];
	int recvLen = 1;
	int currentlyReceivedBytes = 0;
	int packetLength = 0;
	int packetCount = 0;

	while (1) {
		currentlyReceivedBytes = 0;
		isfirstRecieve = true;
		while (1) {
			recvLen = recvfrom(clientSocket, data, 512, 0, (struct sockaddr *) &serverAddr,
					&addrlen);
			if (isfirstRecieve) {
				packetLength = getPacketLength(data) + 8;
				isfirstRecieve = false;
			}
			if (packetLength == FILE_FIN + 8) {
				break;
			}

			currentlyReceivedBytes += recvLen;
			if (currentlyReceivedBytes == packetLength || recvLen == 0) {
				break;
			}
		}
		packet *recvdPacket = new packet;

		extractPacket(recvdPacket, data, recvLen);
		string st(recvdPacket->data, strlen(recvdPacket->data));
    vector<char> bytes(st.begin(), st.end());
    bytes.push_back('\0');
    char *c = &bytes[0];
    uint16_t cksum = (uint16_t)checksum(c);

      //  if((cksum+11)!= recvdPacket->cksum||(cksum+9)!= recvdPacket->cksum)
    if (recvdPacket->seqno < packetCount) {
    	sendAck(recvdPacket->seqno);

		} else if (recvdPacket->seqno == packetCount) {
      sendAck(recvdPacket->seqno);
			addDataToFile(&fp, recvdPacket);
			++packetCount;
		}

		if (packetLength == FILE_FIN + 8) {
			break;
		}
		}
}

/* disconnect from the service */
void disconnect(void) {
	printf("disconn()\n");
	shutdown(clientSocket, 2);
}

int getPacketLength(char data[]) {

	unsigned char b2 = data[2];
	unsigned char b3 = data[3];
	int len = (b2 << 8) | b3;
	return len;

}
void extractPacket(packet* packet, char buffer[], int buffLength) {
	unsigned char b0 = buffer[0];
	unsigned char b1 = buffer[1];
	unsigned char b2 = buffer[2];
	unsigned char b3 = buffer[3];
	unsigned char b4 = buffer[4];
	unsigned char b5 = buffer[5];
	unsigned char b6 = buffer[6];
	unsigned char b7 = buffer[7];
	//checksum combine first two bytes
	packet->cksum = (b0 << 8) | b1;
	//len combine second two bytes
	packet->len = (b2 << 8) | b3;
	//seq_no combine third four bytes
	packet->seqno = (b4 << 24) | (b5 << 16) | (b6 << 8) | (b7);
	for (int i = 8; i < buffLength; ++i) {
		packet->data[i - 8] = buffer[i];
	}

}
void sendAck(packet* pkt) {

	ack_packet ackPacket;

	ackPacket.ackno = pkt->seqno;
	ackPacket.cksum = 1;
	ackPacket.len = 8;
	char data[8];
	int sendFlag = 1;
	while (1) {
		sendFlag = sendto(clientSocket, data, strlen(data), 0,
				(struct sockaddr *) &serverAddr, sizeof(serverAddr));
		if (sendFlag == 0)
			break;
		else if (sendFlag == -1)
			perror("sendto");
	}
}

void openFile(char *filename, ofstream *myfile) {
	myfile->open(filename);
}

int addDataToFile(ofstream *fp, packet* packet) {
	if (packet->len == FILE_FIN)
		return 3;
	if (fp == NULL) {
		return -1;
	}
	if (fp->is_open()) {
		for (int i = 0; i < packet->len; ++i) {
			*fp << packet->data[i];
		}
	} else {
		//file is not opened yet
		return -2;
	}
	//success
	return 1;
}
