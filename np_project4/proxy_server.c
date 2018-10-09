#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define CCOUNT 64*1024
#define BUFFERSIZE 20000

//[0]action [1]mode [2]ip [3]ip [4]ip [5]ip
char *config_table[50][6];
char *USER_ID, *client_ip;
char DST_ip[40], DST_ip1[5], DST_ip2[5], DST_ip3[5], DST_ip4[5], Command[20], Reply[20];
int serversock, server_port, client_port, check_flag;
unsigned char VN, CD, uchar1 = 1, uchar2 = 2, ucahr4 = 4, buffer[BUFFERSIZE+1];
unsigned int DST_IP, DST_PORT;
uint16_t DST_BIND_PORT;

int create_serversock(int port){
	struct sockaddr_in server_addr;

	serversock = socket(AF_INET, SOCK_STREAM, 0);
	if(serversock < 0){
		printf("caanot create server socket\n");
		return -1;
	}
	printf("create server socket\n");

	bzero((char *)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port);

	if(bind(serversock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
		printf("can't bind to port: %d\n", port);
		return -1;
	}
	printf("bind to port: %d\n", port);

	if(listen(serversock, 5) < 0){
		printf("can't listen on port: %d\n", port);
		return -1;
	}

	printf("server wait for connection\n\n");
	return serversock;
}

int create_bindmodesock(){
	struct sockaddr_in bindmode_addr;

	int bindmodesock = socket(AF_INET, SOCK_STREAM, 0);
	if(bindmodesock < 0){
		printf("caanot create bindmode socket\n");
		return -1;
	}
	printf("create bindmode socket\n");

	bzero((char *)&bindmode_addr, sizeof(bindmode_addr));
	bindmode_addr.sin_family = AF_INET;
	bindmode_addr.sin_addr.s_addr = INADDR_ANY;
	bindmode_addr.sin_port = htons(INADDR_ANY);

	if(bind(bindmodesock, (struct sockaddr *)&bindmode_addr, sizeof(bindmode_addr)) < 0){
		printf("can't bind any port\n");
		return -1;
	}
	printf("bind port success\n");

	if(listen(bindmodesock, 5) < 0){
		printf("can't listen on port\n");
		return -1;
	}

	printf("bindmode server wait for connection\n\n");
	return bindmodesock;
}

void sig_handler(int signo){
	int status;
	
	//for waiting child process
	if(signo == SIGCHLD){
		waitpid(0, &status, WNOHANG);
	}
}

int checkin_config_file(){
	char line[100];
	char *tmp;
	int cnt = 0, i = 0, len;

	FILE *config_fp= fopen("socks.conf","r");
	if(config_fp == NULL){
		printf("open config file error");
		return 0;
	}

	//read config file to table
	while(fgets(line, sizeof(line), config_fp) != NULL){
		//remove \r \n
		len = strlen(line);
		if(line[len-1] == '\r') line[len-1] = 0;
		if(line[len-1] == '\n') line[len-1] = 0;

		//retrieve mode and IP
		tmp = strtok(line ," .");
		while(tmp != NULL){
			//permit c 140.114.*.*
			switch(i%6){
				case 0:{ //action
					config_table[cnt][0] = strdup(tmp); break;
				}
				case 1:{ //mode
					config_table[cnt][1] = strdup(tmp); break;
				}
				case 2:{ //ip1
					config_table[cnt][2] = strdup(tmp); break;
				}
				case 3:{ //ip2
					config_table[cnt][3] = strdup(tmp); break;
				}
				case 4:{ //ip3
					config_table[cnt][4] = strdup(tmp); break;
				}
				case 5:{ //ip4
					config_table[cnt][5] = strdup(tmp);
					cnt++;
					break;
				}
			}
			tmp = strtok(NULL ," .");
			i++;
		}
	}
	fclose(config_fp);

	char mode[5];
	if(CD == uchar1) sprintf(mode, "%s", "c");
	else if(CD == uchar2) sprintf(mode, "%s", "b");

	//check dst_ip with config_ip
	int pass_flag = 0;
	for(i = 0; i < cnt; i++){
		//printf("[%d] %s %s %s %s %s %s\n",i , config_table[i][0], config_table[i][1], config_table[i][2], config_table[i][3], config_table[i][4], config_table[i][5]);
		if(strcmp(config_table[i][1], mode) == 0){
			//phase1
			if((strcmp(config_table[i][2], "*") != 0) && (strcmp(config_table[i][2], DST_ip1) != 0)){
				pass_flag = 0;
				continue;
			}
			//phase2
			if((strcmp(config_table[i][3], "*") != 0) && (strcmp(config_table[i][3], DST_ip2) != 0)){
				pass_flag = 0;
				continue;
			}
			//phase3
			if((strcmp(config_table[i][4], "*") != 0) && (strcmp(config_table[i][4], DST_ip3) != 0)){
				pass_flag = 0;
				continue;
			}
			//phase4
			if((strcmp(config_table[i][5], "*") != 0) && (strcmp(config_table[i][5], DST_ip4) != 0)){
				pass_flag = 0;
				continue;
			}
			//final
			pass_flag = 1;
			break;
		}
	}

	return pass_flag;
}

int read_sock4_req(int browsersock){
	int n = read(browsersock, buffer, BUFFERSIZE);
	if(n < 8){
		printf("read sock4 error\n");
		return 0;
	}

	VN = buffer[0];
	CD = buffer[1];
	DST_IP = buffer[4] << 24 | buffer[5] << 16 | buffer[6] << 8 | buffer[7];
	DST_PORT = buffer[2] << 8 | buffer[3];
	USER_ID = buffer + 8;

	//convert to ip
	sprintf(DST_ip, "%u.%u.%u.%u", ((DST_IP>>24)&0xff), ((DST_IP>>16)&0xff), ((DST_IP>>8)&0xff), (DST_IP&0xff));
	sprintf(DST_ip1, "%u", ((DST_IP>>24)&0xff));
	sprintf(DST_ip2, "%u", ((DST_IP>>16)&0xff));
	sprintf(DST_ip3, "%u", ((DST_IP>>8)&0xff));
	sprintf(DST_ip4, "%u", ((DST_IP)&0xff));

	/*printf("======\n");
	printf("VN: %u\n", VN);
	printf("CD: %u\n", CD);
	printf("DST_IP: %s\n", DST_ip);
	printf("DST_PORT: %u\n", DST_PORT);
	printf("USER_ID: %u\n", USER_ID);
	printf("======\n");*/
	if(CD == uchar1) return 1;
	else if(CD == uchar2) return 2;

	return 0;
}

void send_sock4_reply(int browsersock, int flag){
	unsigned char package[50];
	if(flag == 0){
		package[0] = 0;
		package[1] = 91;
		package[2] = 0;
		package[3] = 0;
		package[4] = 0;
		package[5] = 0;
		package[6] = 0;
		package[7] = 0;
	}
	else if(flag == 1){ // ip = ip in SOCKS4_REQUEST for connect mode
		package[0] = 0;
		//package[1] = 90;
		if(check_flag) package[1] = 90;
		else package[1] = 91;
		package[2] = DST_PORT / 256;
		package[3] = DST_PORT % 256;
		package[4] = DST_IP >> 24;
		package[5] = (DST_IP >> 16) & 0xFF;
		package[6] = (DST_IP >> 8) & 0xFF;
		package[7] = DST_IP & 0xFF;
	}
	else if(flag == 2){ // ip = 0 for bind mode
		package[0] = 0;
		//package[1] = 90;
		if(check_flag) package[1] = 90;
		else package[1] = 91;
		package[2] = (DST_BIND_PORT / 256) & 0xff;
		package[3] = (DST_BIND_PORT % 256) & 0xff;
		package[4] = 0;
		package[5] = 0;
		package[6] = 0;
		package[7] = 0;
	}
	write(browsersock, package, 8);
}

int check_sock4_req(){
	int i;
	check_flag = 0;

	if(VN != ucahr4) return 0;
	if(CD == uchar1){
		strcpy(Command, "CONNECT");
		check_flag = checkin_config_file();
		if(check_flag){
			strcpy(Reply, "Accept");
			return 1;
		}
		else{
			strcpy(Reply, "Reject");
			return 0;
		}
	}
	else if(CD == uchar2){
		strcpy(Command, "BIND");
		check_flag = checkin_config_file();
		if(check_flag){
			strcpy(Reply, "Accept");
			return 1;
		}
		else{
			strcpy(Reply, "Reject");
			return 0;
		}
	}
	return 0;
}

void print_server_msg(){
	printf("<S_IP>\t%s\n", client_ip);
	printf("<S_PORT>\t%d\n", client_port);
	printf("<D_IP>\t%s\n", DST_ip);
	printf("<D_PORT>\t%u\n", DST_PORT);
	printf("<Commnad>\t%s\n", Command);
	printf("<Reply>\t%s\n\n", Reply);
	//printf("<Content>\t\n\n");
}

int TCPconnect(){
	struct sockaddr_in client_sin;
	int client_fd;

	client_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(client_fd < 0){
		printf("create local client error\n");
		return -1;
	}

	bzero(&client_sin, sizeof(client_sin));
	client_sin.sin_family = AF_INET;
	client_sin.sin_addr.s_addr = inet_addr(DST_ip);
	client_sin.sin_port = htons(DST_PORT);

	if(connect(client_fd, (struct sockaddr *)&client_sin, sizeof(client_sin)) < 0){
		printf("connect error\n");
		return -1;
	}

	return client_fd;
}

void connect_mode(int browsersock){
	printf("---connect_mode start---\n");
	int nfds, dstclientsock, n;
	fd_set rfds, afds;
	FD_ZERO(&afds);

	//create local client's socket AND connect to browser client's host
	dstclientsock = TCPconnect();
	if(dstclientsock < 0) return;
	FD_SET(dstclientsock, &afds);
	FD_SET(browsersock, &afds);

	send_sock4_reply(browsersock, 1);
	print_server_msg();

	if(dstclientsock > browsersock) nfds = dstclientsock+1;
	else nfds = browsersock+1;
	
	//anything sended by browser just redirect to dst host
	//anything received from dst host just send back to browser
	while(1){
		n = 0;
		FD_ZERO(&rfds);
		memcpy(&rfds, &afds, sizeof(rfds));
		if(select(nfds, &rfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0) < 0){
			printf("select error\n");
			break;
		}

		if(FD_ISSET(dstclientsock, &rfds)){
			//recv msg from host
			n = read(dstclientsock, buffer, BUFFERSIZE-1);
			if(n <= 0) {
				printf("dstclientsock read error\n");
				break;
				//continue;
			}
			

			//printf("recv msg from dst_host: %s\n[%X,%X,%X,%X,%X...]\n", DST_ip, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);
			printf("<Content>\t%s\n\n", buffer);

			//write back to browser client
			n = write(browsersock, buffer, sizeof(unsigned char)*n);
			if(n <= 0){
				printf("dstclientsock write error\n");
				break;
			}
		}

		if(FD_ISSET(browsersock, &rfds)){
			//recv msg from browser client
			n = read(browsersock, buffer, BUFFERSIZE-1);
			if(n < 0) {
				printf("browsersock read error\n");
				break;
				//continue;
			}
			
			//printf("recv msg from host: %s\n[%X,%X,%X,%X,%X...]\n", client_ip, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);
			//printf("<Content>\t%s\n\n", buffer);

			//redirect to host
			n = write(dstclientsock, buffer, sizeof(unsigned char)*n);
			if(n <= 0){
				printf("browsersock write error\n");
				break;
			}
		}
	}

	close(dstclientsock);
	printf("---connect_mode finish---\n");
}

void bind_mode(int browsersock){
	printf("---bind_mode start---\n");
	struct sockaddr_in tmp_addr, ftp_addr;
	int bindmodesock, ftpsock, len, nfds, n;
	// 1. create bind mode server with any IP/port
	bindmodesock = create_bindmodesock();
	if(bindmodesock < 0) return;
	
	// 2. send SOCK4_REPLY with above port
	len = sizeof(tmp_addr);
	if(getsockname(bindmodesock, (struct sockaddr *)&tmp_addr, (socklen_t*)&len) < 0){
		printf("getsockname fail\n");
		return;
	}
	DST_BIND_PORT = ntohs(tmp_addr.sin_port);
	send_sock4_reply(browsersock, 2);

	// 3. accept FTP server socket
	len = sizeof(ftp_addr);
	ftpsock = accept(bindmodesock, (struct sockaddr *)&ftp_addr, (socklen_t*)&len);
	if (ftpsock < 0){
		printf("accept ftpsock fail\n");
		return;
	}
	printf("accept [%d]'s BIND MODE connection from %s/%d\n", getpid(), inet_ntoa(ftp_addr.sin_addr), (int)ntohs(ftp_addr.sin_port));
	send_sock4_reply(browsersock, 2);

	// 4. while loop to monitor sockets from browser/FTP server socket
	fd_set rfds, afds;
	FD_ZERO(&afds);
	FD_SET(ftpsock, &afds);
	FD_SET(browsersock, &afds);

	if(ftpsock > browsersock) nfds = ftpsock + 1;
	else nfds = browsersock + 1;

	while(1){
		n = 0;
		FD_ZERO(&rfds);
		memcpy(&rfds, &afds, sizeof(rfds));
		if(select(nfds, &rfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0) < 0){
			printf("select error\n");
			break;
		}

		if(FD_ISSET(ftpsock, &rfds)){
			//recv msg from ftpserver
			n = read(ftpsock, buffer, BUFFERSIZE-1);
			if(n <= 0){
				printf("ftpsock read error\n");
				//break;
			}

			printf("<Content>\tdata transfer~\n\n");
			//printf("<Content>\t%s\n\n", buffer);
			//write back to browser
			n = write(browsersock, buffer, sizeof(unsigned char)*n);
			if(n <= 0){
				printf("ftpsock write error\n");
				//break;
			}
		}

		if(FD_ISSET(browsersock, &rfds)){
			//recv msg from browser
			n = read(browsersock, buffer, BUFFERSIZE-1);
			if(n <= 0){
				printf("browser read error\n");
				//break;
			}

			printf("<Content>\tdata transfer~\n\n");
			//printf("<Content>\t%s\n\n", buffer);
			//write back to ftp server
			n = write(ftpsock, buffer, sizeof(unsigned char)*n);
			if(n <= 0){
				printf("browser write error\n");
				//break;
			}
		}
	}

	close(ftpsock);
	printf("---bind_mode finish---\n");
}

void client_request_handler(int browsersock){
	//read SOCK4 REQ
	int mode = read_sock4_req(browsersock);

	//do something in requested mode 
	if(mode == 0){ //check fail
		printf("in mode 0 eorror\n");
		send_sock4_reply(browsersock, 0);
	}
	else if(mode == 1){ // connect mode
		printf("in mode 1 connect\n");
		if(check_sock4_req()){
			printf("check pass\n");
			connect_mode(browsersock);
		}
		else{
			printf("check fail QQ\n");
			send_sock4_reply(browsersock, 1);
		}
	}
	else if(mode == 2){ //bind mode
		//Login at: http://np4at674.5gbfree.com:2082/
		//Username: np4at674
		//Password: !F1ASc2IAn%lOx
		printf("in mode 2 bind\n");
		if(check_sock4_req()){
			printf("check pass\n");
			bind_mode(browsersock);
		}
		else{
			printf("check fail QQ\n");
			send_sock4_reply(browsersock, 2);
		}
	}
}

int main(int argc, char *argv[], char *envp[]){
	int clilen, browsersock, clientpid;
	struct sockaddr_in client_addr;

	server_port = atoi(argv[1]);
	serversock = create_serversock(server_port);
	//create_serversock(server_port);
	if(serversock < 0) return 0;

	//wait for child process for preventing zombie process
	signal(SIGCHLD, sig_handler);

	while(1){
		clilen = sizeof(client_addr);
		browsersock = accept(serversock, (struct sockaddr *)&client_addr, (socklen_t*)&clilen);
		if (browsersock < 0){
			printf("accept fail\n");
		}

		client_ip = inet_ntoa(client_addr.sin_addr);
		client_port = (int)ntohs(client_addr.sin_port);

		int status;
		clientpid = fork();
		if(clientpid < 0) {
			printf("fork ERROR\n");
		}
		else if(clientpid == 0){ //clild proc
			//close(serversock);
			printf("accept [%d]'s connection from %s/%d\n", getpid(), client_ip, client_port);
			client_request_handler(browsersock);
			exit(0);
		}
		else{ //parent proc
			close(browsersock);
			waitpid(-1, NULL, WNOHANG);
		}
	}

	return 0;
}