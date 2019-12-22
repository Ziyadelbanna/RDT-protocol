#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/errno.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/types.h>
#include <vector>
#include <pthread.h>
#include <sys/time.h>
#include <fstream>

typedef unsigned char uchar;
int TIMEOUT = 300000;
int TIMER_STOP = -100;
using namespace std;
//extern int errno;
ifstream infoFile;
ofstream logFile;

timeval start_time;
timeval end_time;

int time_count = 0;
int protocol;// = 2;//0 for stop and wait, 1 for selective repeat, 2 for GBN

struct ack_packet
{
    uint16_t cksum; /* Optional bonus part */
    uint16_t len;
    uint32_t ackno;
};
/* Data-only packets */
struct packet
{
    /* Header */
    uint16_t cksum; /* Optional bonus part */
    uint16_t len;
    uint32_t seqno;
    /* Data */
    char data[500]; /* Not always 500 bytes, can be less */
};
int server_port = 0;
int max_window_size;
int seed;
float p;
int cur_window_size=2;

void sig_func(int);
int get_file_size(char*);
void send_selective_repeat(char*);
void run(); /* main server function */
void convert_to_bytes(packet*, char*);
void parse_Ack_packet(char *, int, ack_packet*);
void* receive_Ack_selective_repeat(void *);
void* receive_Ack_stop_and_wait(void *);
void *receive_Ack_GBN(void *threadid);
void send_stop_and_wait(char*);
void send_GBN(char*);
void *timer_selective(void *time);
void *timer_GBN(void *time);
FILE* fp;
int ssth=50;

vector<char*> *packets = new vector<char*>;
vector<bool> *acked_packets = new vector<bool>;
vector<int> *times = new vector<int>;
vector<int> *lens = new vector<int>;

int base = 0;
int cur = 0;
int child_socket;




pthread_t rec_thread;
pthread_t stop_and_wait_timer_thread;
pthread_t selective_thread;

bool stop_and_wait_acked = false;
bool rec_thread_is_created = false;

pthread_mutex_t selective_mutex;

pthread_mutex_t stop_and_wait_mutex;

pthread_mutex_t Stop_and_wait_timer_mutex;

pthread_mutex_t SAW_rec_thread_creation_mutex;

pthread_mutex_t lock1;
pthread_mutex_t lock2;
void extract_packet(packet* packet, char buffer[], int buffer_length);

/* serve: set up the service */
struct sockaddr_in my_addr; /* address of this service */
struct sockaddr_in child_addr; /* address of this service */
struct sockaddr_in client_addr; /* client's address */
int svc; /* listening socket providing service */
int rqst; /* socket accepting the request */
int drop_count = 0;
int no_of_dropped_packets=0;

bool isdropped()
{
    float drop_count_float = drop_count;
    float drop = drop_count_float * p;

    if (drop >= 0.9)
    {
        drop_count = 0;
        no_of_dropped_packets++;
        return true;
    }
    else
    {
        return false;
    }

}
uint16_t calculate_checksum(char const *buf) {
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

void resend(int i)
{
    int send_flag = 0;

    send_flag = sendto(child_socket, packets->at(i), lens->at(i) + 8, 0,
                      (struct sockaddr *) &client_addr, sizeof(client_addr));


    packet p;
    extract_packet(&p, packets->at(i), lens->at(i));
}
void read_input_file()
{
    string line;
    ifstream myfile ("server.in");
    if (myfile.is_open())
    {

        if( getline (myfile,line) )
        {
            server_port=atoi(line.c_str());
        }

        if( getline (myfile,line) )
        {
            max_window_size=atoi(line.c_str());
        }
        if( getline (myfile,line) )
        {
            seed=atoi(line.c_str());
        }
        if( getline (myfile,line) )
        {
            p=atof(line.c_str());
        }
        if( getline (myfile,line) )
        {
            protocol=atoi(line.c_str());
        }
    }

    myfile.close();
}


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


int main(int argc, char** argv)
{
    logFile.open("log.txt");
    read_input_file();
    run();
    logFile.close();
    return 0;
}

void *receive_Ack_selective_repeat(void *threadid) {

	while (1) {

		socklen_t alen = sizeof(client_addr);
		int reclen;
		char Buffer[8];
		if (reclen = (recvfrom(child_socket, Buffer, 8, 0,
				(struct sockaddr *) &client_addr, &alen)) < 0)
			perror("recvfrom() failed");
		ack_packet packet;
		parse_Ack_packet(Buffer, 8, &packet);
		int frame_index = packet.ackno - base;
		pthread_mutex_lock(&selective_mutex);
		if (frame_index < 0 || acked_packets->size() == 0) {
			cout << "dublicate ack" << endl;

		} else if (acked_packets->size() != 0
				&& acked_packets->at(frame_index) == true) {
			     cout << "dublicate ack" << endl;
			} else {
			if (cur_window_size < max_window_size) {
				cur_window_size++;
				cout << "WINDOW SIZE FROM  " << cur_window_size - 1<< " TO " << cur_window_size << endl;
        logFile << cur_window_size << endl;
			}
			acked_packets->at(frame_index) = true;
			times->at(frame_index) = TIMER_STOP;
			if (acked_packets->at(0) == true) {
				for (int i = 0; i <= cur - base; ++i) {
					if (acked_packets->size() == 0)
						break;
					if (acked_packets->at(0) == true) {
						packets->erase(packets->begin());
						acked_packets->erase(acked_packets->begin());
						times->erase(times->begin());
						lens->erase(lens->begin());
						++base;
					} else {
						break;
					}
				}
			}
		}
		pthread_mutex_unlock(&selective_mutex);
	}
}


void *timer_selective(void *time) {

	while (true) {
		pthread_mutex_lock(&selective_mutex);
		for (int i = 0; i < times->size(); ++i) {
			times->at(i)--;
			if(times->at(i)==0) {
				times->at(i)= TIMEOUT;
				resend(i);
			}
		}

		pthread_mutex_unlock(&selective_mutex);
	}

}

void send_selective_repeat(char* fileName) {

	int size = get_file_size(fileName);
	packet curr_packet;
	cout << "File Size : " << size << endl;
	int packet_count = 0;

	for (int i = 0; i < size;) {

		drop_count++;
		drop_count = drop_count % 101;
		curr_packet.len = fread(curr_packet.data, 1, 500, fp);
		string st(curr_packet.data, strlen(curr_packet.data));
        vector<char> bytes(st.begin(), st.end());
        bytes.push_back('\0');
        char *c = &bytes[0];
        int n = checksum(c)& 255;
        curr_packet.cksum = (uint16_t)n;
		curr_packet.seqno = packet_count;
		i += curr_packet.len;
		char *buffer = new char[512];
		unsigned int send_flag = 1;
		convert_to_bytes(&curr_packet, buffer);
		while (1) {
			pthread_mutex_lock(&selective_mutex);

			if (cur - base < cur_window_size-1) {
				if (!isdropped()) {
					send_flag = sendto(child_socket, buffer, curr_packet.len + 8,
							0, (struct sockaddr *) &client_addr,
							sizeof(client_addr));
					if (send_flag < -1)
						break;
					else if (send_flag == -1)
						perror("sendto");
				} else {
					if (cur_window_size > 2) {
						cur_window_size = cur_window_size / 2;

					} else if (cur_window_size < 2) {
						cur_window_size = 2;

					}
					break;
				}
			} else {
				pthread_mutex_unlock(&selective_mutex);
			}
		}

		cur = curr_packet.seqno;
		packets->push_back(buffer);
		acked_packets->push_back(false);
		times->push_back(TIMEOUT);
		lens->push_back(curr_packet.len);
		pthread_mutex_unlock(&selective_mutex);
		packet_count++;

	}
	curr_packet.cksum = 1;
	curr_packet.len = 150;
	curr_packet.seqno = packet_count;

	gettimeofday(&end_time, NULL);
	long t2 = end_time.tv_sec;
	t2 = t2 - start_time.tv_sec;
	cout << "Time Taken is " << t2 << " secs" << endl;
	char *dataFinish = new char[8];
	int send_flag = 1;
	convert_to_bytes(&curr_packet, dataFinish);

	while (1) {
		if (cur - base < cur_window_size - 1) {
			send_flag = sendto(child_socket, dataFinish, sizeof(dataFinish), 0,
					(struct sockaddr *) &client_addr, sizeof(client_addr));
			break;
		}
	}
	pthread_mutex_lock(&selective_mutex);

	cur = curr_packet.seqno;
	packets->push_back(dataFinish);
	acked_packets->push_back(false);
	lens->push_back(curr_packet.len);
	times->push_back(TIMEOUT);
	pthread_mutex_lock(&selective_mutex);

	if (send_flag == -1)
		perror("sendto");
	cout << "******		Finish Window size =" << cur_window_size << endl;
}

void run()
{
    signal(SIGSEGV, sig_func);
    socklen_t alen;
    int sockoptval = 1;

    char hostname[128] = "localhost";
    if ((svc = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
        perror("cannot create socket");
        exit(1);
    }

    memset((char*) &my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(server_port);
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY );

    if (bind(svc, (struct sockaddr *) &my_addr, sizeof(my_addr)) < 0)
    {
        perror("bind failed");
        exit(1);
    }
    while(1)
    {

        alen = sizeof(client_addr);
        cout << "Server is now Ready for requests" << endl;
        char echo_buffer[100];
        if ((recvfrom(svc, echo_buffer, 100, 0, (struct sockaddr *) &client_addr,
                      &alen)) < 0)
            cout<<"receive requested file from client failed"<<endl;
        cout << "requested file  : " << echo_buffer << endl;
        int pid = fork();
        //child process will enter
        if (pid == 0)
        {
            pthread_t ack_thread;
            int rc;
            long t = 0;
            if ((child_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
            {
                cout<<"cannot create socket"<<endl;
                exit(1);
            }

            memset((char*) &child_addr, 0, sizeof(child_addr));
            child_addr.sin_family = AF_INET;
            child_addr.sin_addr.s_addr = htonl(INADDR_ANY );
            child_addr.sin_port = htons(server_port);

            if (protocol==1)
            {
                cout << "SELECTIVE REPEAT PROTOCOL" << endl;

				int receive_Ack_thread = pthread_create(&ack_thread, NULL, receive_Ack_selective_repeat, (void *) t);

				int timer_thread = pthread_create(&selective_thread, NULL,
						timer_selective, (void *) t);

				if (receive_Ack_thread || timer_thread) {
					cout<< "error in starting the ack thread and timer Selective"<< endl;
				} else {
					pthread_detach(ack_thread);
					gettimeofday(&start_time, NULL);
					long t1 = start_time.tv_sec;

					send_selective_repeat(echo_buffer);
					gettimeofday(&end_time, NULL);
					long t2 = end_time.tv_sec;
					t2 = t2 - t1;
					cout << "Time Taken is " << t2 << " secs" << endl;
					exit(0);
					shutdown(rqst, 2);
				}
            }
            else if(protocol==0)
            {
                cout << "STOP AND WAIT PROTOCOL" << endl;
                gettimeofday(&start_time, NULL);
                long t1 = start_time.tv_sec;
                send_stop_and_wait(echo_buffer);
                gettimeofday(&end_time, NULL);
                long t2 = end_time.tv_sec;
                t2 = t2 - t1;
                cout << "Time Taken is " << t2 << " secs" << endl;
                exit(0);

            }
            else if(protocol==2)
            {

                cout << "GO BACK N PROTOCOL" << endl;
                rc = pthread_create(&ack_thread, NULL, receive_Ack_GBN, (void *) t);
                int rc1 = pthread_create(&selective_thread, NULL,
                                         timer_GBN, (void *) t);

                if (rc || rc1)
                {
                    cout<< "error in starting the ack thread and timer Selective"<< endl;
                }
                else
                {
                    pthread_detach(ack_thread);
                    gettimeofday(&start_time, NULL);
                    long t1 = start_time.tv_sec;

                    send_GBN(echo_buffer);
                    gettimeofday(&end_time, NULL);
                    long t2 = end_time.tv_sec;
                    t2 = t2 - t1;
                    cout << "Time Taken is " << t2 << " secs" << endl;
                    exit(0);
                    shutdown(rqst, 2); /* 3,590,313 close the connection */
                }

            }
        close(child_socket);
        }
        else if (pid == -1)
        {
            cout << "error forking the client process" << endl;
        }
    }

close(svc);
}

int get_file_size(char* file_name)
{
    fp = fopen(file_name, "r");
    if (fp == NULL)
    {
        perror("Server- File NOT FOUND 404");
        return -1;
    }
    fseek(fp, 0L, SEEK_END);
    long size = ftell(fp);

    fseek(fp, 0L, SEEK_SET);

    return size;

}
int out_of_order = 0;

void extract_packet(packet* packet, char buffer[], int buffer_length)
{

    //convert char to unsigned char
    uchar b0 = buffer[0];
    uchar b1 = buffer[1];
    uchar b2 = buffer[2];
    uchar b3 = buffer[3];
    uchar b4 = buffer[4];
    uchar b5 = buffer[5];
    uchar b6 = buffer[6];
    uchar b7 = buffer[7];
    //checksum combine first two bytes
    packet->cksum = (b0 << 8) | b1;
    //len combine second two bytes
    packet->len = (b2 << 8) | b3;
    //seq_no combine third four bytes
    packet->seqno = (b4 << 24) | (b5 << 16) | (b6 << 8) | (b7);
    for (int i = 8; i < buffer_length; ++i)
    {
        packet->data[i - 8] = buffer[i];
    }

}

void resend_GBN()
{
    int send_flag = 0;
    for(int i=0;i<packets->size();i++){
    send_flag = sendto(child_socket, packets->at(i), lens->at(i) + 8, 0,
                      (struct sockaddr *) &client_addr, sizeof(client_addr));
    times->at(i)=TIMEOUT;
    packet p;
    extract_packet(&p, packets->at(i), lens->at(i));
    }

}

void *timer_GBN(void *time)
{

    while (true)
    {
        pthread_mutex_lock(&selective_mutex);
        for (int i = 0; i < times->size(); ++i)
        {
            times->at(i)--;
            if(times->at(i)==0)
            {
                times->at(i)= TIMEOUT;
                resend(i);
            }
        }

        pthread_mutex_unlock(&selective_mutex);
    }

}

void *receive_Ack_stop_and_wait(void *threadid)
{
    int packet_count = ((intptr_t) threadid);
    socklen_t alen = sizeof(client_addr); /* length of address */
    int reclen;
    char Buffer[8];
    bool wait_end = false;
    while (!wait_end)
    {
        stop_and_wait_acked = false;
        if (reclen = (recvfrom(child_socket, Buffer, 8, 0,
                               (struct sockaddr *) &client_addr, &alen)) < 0)
            perror("recvfrom() failed");

        pthread_mutex_lock(&Stop_and_wait_timer_mutex);
        ack_packet packet;
        parse_Ack_packet(Buffer, 8, &packet);
        uint16_t temp;
        if (packet_count == 0)
            temp = 0;
        else
            temp = 1;
        if (packet.ackno != temp)
        {
            out_of_order++;
            pthread_mutex_unlock(&Stop_and_wait_timer_mutex);
        }
        else
        {
            wait_end = true;
            stop_and_wait_acked = true;
        }
    }
    pthread_mutex_unlock(&Stop_and_wait_timer_mutex);
    pthread_mutex_unlock(&stop_and_wait_mutex);
    pthread_exit(NULL);
}
void *receive_Ack_GBN(void *threadid)
{

    while (1)
    {
        socklen_t alen = sizeof(client_addr); /* length of address */
        int reclen;
        char Buffer[8];
        if (reclen = (recvfrom(child_socket, Buffer, 8, 0,
                               (struct sockaddr *) &client_addr, &alen)) < 0)
            perror("recvfrom() failed");
        ack_packet packet;
        parse_Ack_packet(Buffer, 8, &packet);
        int frame_index = packet.ackno - base;
        pthread_mutex_lock(&selective_mutex);

        if (frame_index < 0 || acked_packets->size() == 0)
        {
            cout << "dublicate ack" << endl;
        }
        else if (acked_packets->size() != 0
                 && acked_packets->at(frame_index) == true)
        {
            cout << "dublicate ack" << endl;
        }
        else
        {
            if (cur_window_size*2 < ssth)
            {

                cur_window_size*=2;
                cout << "WIDOW SIZE FROM  " << cur_window_size/2<< " TO " << cur_window_size << endl;

            }else if(cur_window_size<max_window_size&&cur_window_size>=ssth){
                cur_window_size++;
                cout << "WINDOW SIZE FROM  " << (cur_window_size - 1)<< " TO " << cur_window_size << endl;
                logFile << cur_window_size << endl;
            }else if(cur_window_size*2>ssth&&cur_window_size<ssth){
                cur_window_size+= (ssth-cur_window_size);
                cout << "WINDOW SIZE FROM  " << (ssth-cur_window_size)<< " TO " << cur_window_size << endl;
                logFile << cur_window_size << endl;
            }
            acked_packets->at(frame_index) = true;
            //TODO stop timer
            times->at(frame_index) = TIMER_STOP;
            if (acked_packets->at(0) == true)
            {
                for (int i = 0; i <= cur - base; ++i)
                {
                    if (acked_packets->size() == 0)
                        break;
                    if (acked_packets->at(0) == true)
                    {
                        packets->erase(packets->begin());
                        acked_packets->erase(acked_packets->begin());
                        times->erase(times->begin());
                        lens->erase(lens->begin());
                        ++base;
                    }
                    else
                    {
                        break;
                    }
                }
            }
        }
        pthread_mutex_unlock(&selective_mutex);
    }
}

void send_GBN(char* file_name)
{

    int size = get_file_size(file_name);
    packet curr_packet;

    cout << "File Size : " << size << endl;
    int packet_count = 0;

    for (int i = 0; i < size;)
    {
        drop_count++;
        drop_count = drop_count % 101;

        curr_packet.len = fread(curr_packet.data, 1, 500, fp);
        string st(curr_packet.data, strlen(curr_packet.data));
        vector<char> bytes(st.begin(), st.end());
        bytes.push_back('\0');
        char *c = &bytes[0];
        curr_packet.cksum = (uint16_t)checksum(c);
        curr_packet.seqno = packet_count;
        i += curr_packet.len;
        char *buffer = new char[512];
        unsigned int send_flag = 1;
        convert_to_bytes(&curr_packet, buffer);

        while (1)
        {
            pthread_mutex_lock(&selective_mutex);
            if (cur - base < cur_window_size - 1)
            {
                if (!isdropped())
                {
                    send_flag = sendto(child_socket, buffer, curr_packet.len + 8,
                                      0, (struct sockaddr *) &client_addr,
                                      sizeof(client_addr));
                    if (send_flag < -1)
                        break;
                    else if (send_flag == -1)
                        perror("sendto");
                }
                else
                {
                    ssth= cur_window_size/2;
                    cur_window_size = 1;
                    break;
                }
            }
            else
            {
                pthread_mutex_unlock(&selective_mutex);
            }
        }

        cur = curr_packet.seqno;
        packets->push_back(buffer);
        acked_packets->push_back(false);
        times->push_back(TIMEOUT);
        lens->push_back(curr_packet.len);

        pthread_mutex_unlock(&selective_mutex);
        packet_count++;
    }
    curr_packet.cksum = 1;
    curr_packet.len = 150;
    curr_packet.seqno = packet_count;

    gettimeofday(&end_time, NULL);
    long t2 = end_time.tv_sec;
    t2 = t2 - start_time.tv_sec;
    cout << "Time Taken is " << t2 << " secs" << endl;
    char *dataFinish = new char[8];
    int sendFlag = 1;
    convert_to_bytes(&curr_packet, dataFinish);
    while (1)
    {
        if (cur - base < cur_window_size - 1)
        {
            sendFlag = sendto(child_socket, dataFinish, sizeof(dataFinish), 0,
                              (struct sockaddr *) &client_addr, sizeof(client_addr));
            break;
        }
    }
    pthread_mutex_lock(&selective_mutex);

    cur = curr_packet.seqno;
    packets->push_back(dataFinish);
    acked_packets->push_back(false);
    lens->push_back(curr_packet.len);
    times->push_back(TIMEOUT);
    pthread_mutex_lock(&selective_mutex);

    if (sendFlag == -1)
        perror("sendto");
}


void *stop_and_wait_timer(void *time)
{

    int sleeptime = 3 * 100000;
    int temp_time = sleeptime;
    while (true)
    {
        pthread_mutex_lock(&SAW_rec_thread_creation_mutex);
        pthread_mutex_lock(&Stop_and_wait_timer_mutex);
        sleeptime--;
        if (stop_and_wait_acked == true)
        {
            sleeptime = temp_time;
        }
        else if (sleeptime <= 0)
        {
            sleeptime = temp_time;
            time_count++;
            pthread_cancel(rec_thread);
            stop_and_wait_acked = false;
            pthread_mutex_unlock(&stop_and_wait_mutex);
        }
        pthread_mutex_unlock(&Stop_and_wait_timer_mutex);
        pthread_mutex_unlock(&SAW_rec_thread_creation_mutex);
    }

}
void send_stop_and_wait(char* fileName)
{
    bool rec_thread_created = false;
    bool timer_started = false;
    int size = get_file_size(fileName);
    packet curr_packet;

    cout << "File Size : " << size << endl;
    int packet_count = 0;

    int couny = 0;
    drop_count = 0;
    for (int i = 0; i < size;)
    {
        drop_count++;
        drop_count = drop_count % 101;
        curr_packet.len = fread(curr_packet.data, 1, 500, fp);
        string st = (string)curr_packet.data;
        vector<char> bytes(st.begin(), st.end());
        bytes.push_back('\0');
        char *c = &bytes[0];
        curr_packet.cksum = (uint16_t)calculate_checksum(c);
        curr_packet.seqno = packet_count;
        char buffer[512];
        i += curr_packet.len;
        unsigned int send_flag = 1;
        convert_to_bytes(&curr_packet, buffer);
        bool acked = false;
        stop_and_wait_acked = false;
        while (!acked)
        {
            if (!isdropped())
            {
                while (1)
                {
                    send_flag = sendto(child_socket, buffer, curr_packet.len + 8,
                                      0, (struct sockaddr *) &client_addr,
                                      sizeof(client_addr));
                    if (send_flag == 0)
                    {
                        break;
                    }
                    else if (send_flag < -1)
                        break;
                    else if (send_flag == -1)
                        cout<<"error sendto"<<endl;
                }
            }
            else
            {
                cout << "Package Dropped" << endl;
            }
            pthread_mutex_lock(&stop_and_wait_mutex);
            pthread_mutex_lock(&SAW_rec_thread_creation_mutex);
            if (rec_thread_created == true)
            {
                pthread_kill(rec_thread, 0);
                rec_thread_created = false;
            }
            while (1)
            {
                int rc = pthread_create(&rec_thread, NULL, receive_Ack_stop_and_wait,
                                        (void *) packet_count);
                rec_thread_created = true;
                pthread_detach(rec_thread);
                if (rc)
                {
                    cout << "error in starting the Stop and wait ack thread"
                         << endl;
                    sleep(1);
                }
                else
                {
                    break;
                }
            }
            pthread_mutex_unlock(&SAW_rec_thread_creation_mutex);
            if (!timer_started)
            {
                timer_started = true;
                int timeout = 3;
                while (1)
                {
                    int rc2 = pthread_create(&stop_and_wait_timer_thread, NULL,
                                             stop_and_wait_timer, (void *) timeout);
                    if (rc2)
                    {
                    }
                    else
                    {
                        pthread_detach(stop_and_wait_timer_thread);
                        break;
                    }
                }
            }

            pthread_mutex_lock(&stop_and_wait_mutex);
            pthread_mutex_lock(&Stop_and_wait_timer_mutex);
            if (stop_and_wait_acked == true)
            {
                stop_and_wait_acked = false;
                acked = true;
            }
            else
            {
                acked = false;
            }
            pthread_mutex_unlock(&stop_and_wait_mutex);
            pthread_mutex_unlock(&Stop_and_wait_timer_mutex);

        }

        packet_count++;
        packet_count = packet_count % 2;

    }

    curr_packet.cksum = 1;
    curr_packet.len = 150;
    curr_packet.seqno = packet_count;
    char data_finish[8];
    int send_flag = 1;
    convert_to_bytes(&curr_packet, data_finish);
    send_flag = sendto(child_socket, data_finish, sizeof(data_finish), 0,
                      (struct sockaddr *) &client_addr, sizeof(client_addr));

    if (send_flag == -1)
        perror("sendto");
    pthread_kill(stop_and_wait_timer_thread, 0);
    cout << "Out Of order Count = " << out_of_order << endl;
}

void convert_to_bytes(packet *packet, char* buffer)
{
    //chksum

    buffer[0] = packet->cksum >> 8;
    buffer[1] = (packet->cksum) & 255;

    //len field

    buffer[2] = packet->len >> 8;
    buffer[3] = (packet->len) & 255;

    //seqnumber

    buffer[4] = packet->seqno >> 24;
    buffer[5] = (packet->seqno >> 16) & 255;
    buffer[6] = (packet->seqno >> 8) & 255;
    buffer[7] = (packet->seqno) & 255;

    //data

    for (int i = 8; i < packet->len + 8; ++i)
    {
        buffer[i] = packet->data[i - 8];
    }

}
void parse_Ack_packet(char *buffer, int buff_length, ack_packet* packet)
{
    //convert char to unsigned char
    uchar b0 = buffer[0];
    uchar b1 = buffer[1];
    uchar b2 = buffer[2];
    uchar b3 = buffer[3];
    uchar b4 = buffer[4];
    uchar b5 = buffer[5];
    uchar b6 = buffer[6];
    uchar b7 = buffer[7];
    //checksum combine first two bytes
    packet->cksum = (b0 << 8) | b1;
    //len combine second two bytes
    packet->len = (b2 << 8) | b3;
    //seq_no combine third four bytes
    packet->ackno = (b4 << 24) | (b5 << 16) | (b6 << 8) | (b7);
}

void sig_func(int sig)
{
    cout << "receiving thread is dead" << endl;
    pthread_exit(NULL);
}
