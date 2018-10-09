#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#define CCOUNT 64*1024
#define BUFFERSIZE 20000

#define F_CONNECTING 0
#define F_READING 1
#define F_WRITING 2
#define F_DONE 3
#define F_REQEST 4
#define F_REPLY 5

unsigned char buffer[50];
int proxy_check[6] = {0};

int parse_query_string(char *query, char **ip, char **port, char **file, char **proxyip, char ** proxyport){
	//h1=GG&p1=ININ&f1=der&sh1&sp1
	//h1=GG1&p1=GG2&f1=GG3&sh1=GG4&sp1=GG5&h2=&p2=&f2=&sh2=&sp2=&h3=&p3=&f3=&sh3=&sp3=&h4=&p4=&f4=&sh4=&sp4=&h5=&p5=&f5=&sh5=&sp5=
	char *str;
	int clientID = 1, i = 1, client_count = 0;
	str = strtok(query, "&");
	while(str != NULL){
		if(i > 5){
			if(strcmp(ip[clientID],"")!=0 && strcmp(port[clientID], "")!=0 && strcmp(file[clientID], "")!=0){
				client_count = client_count + 1;
			}
			i = 1;
			clientID = clientID + 1;
		}
		switch(i){
			case 1:
				ip[clientID] = str+3;
				break;
			case 2:
				port[clientID] = str+3;
				break;
			case 3:
				file[clientID] = str+3;
				break;
			case 4:
				proxyip[clientID] = str+4;
				break;
			case 5:
				proxyport[clientID] = str+4;
				break;
		}
		i = i + 1;
		str = strtok(NULL, "&");
	}

	if(strcmp(ip[clientID],"")!=0 && strcmp(port[clientID], "")!=0 && strcmp(file[clientID], "")!=0 && strcmp(proxyip[clientID], "")!=0 && strcmp(proxyport[clientID], "")!=0){
		client_count = client_count + 1;
	}

	/*for(i = 1; i <= 5; i++){
		printf("<br>client[%d]<br>\n", i);
		printf("-%s-<br>\n", ip[i]);
		printf("-%s-<br>\n", port[i]);
		printf("-%s-<br>\n", file[i]);
		printf("-%s-<br>\n", proxyip[i]);
		printf("-%s-<br>\n", proxyport[i]);
	}
	printf("-%d-<br>\n", client_count);*/

	return client_count;
}

int SOCKorTCPconnect(int id, char *ip, char *port, char *proxyip, char *proxyport){
	struct sockaddr_in client_sin;
	struct hostent *he;
	int server_port, client_fd, one = 1;

	//check if proxy server needed
	if(strcmp(proxyip,"")!=0 && strcmp(proxyport,"")!=0){
		//do proxy connect
		if((he=gethostbyname(proxyip)) == NULL){
			printf("gethostbyname error<br>");
			return -1;
		}

		client_fd = socket(AF_INET, SOCK_STREAM, 0);
		server_port = (u_short)atoi(proxyport);

		bzero(&client_sin, sizeof(client_sin));
		client_sin.sin_family = AF_INET;
		client_sin.sin_addr = *((struct in_addr *)he->h_addr); 
		client_sin.sin_port = htons(server_port);

		//set socket non-blocking
		int flags = fcntl(client_fd, F_GETFL, 0);
		fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

		if(connect(client_fd, (struct sockaddr *)&client_sin, sizeof(client_sin)) < 0){
			if(errno != EINPROGRESS){
				printf("client connect error<br>");
				return -1;
			}
		}
		proxy_check[id] = 1;
		return client_fd;
	}
	else{
		//no proxy server, normal connect
		if((he=gethostbyname(ip)) == NULL){
			printf("gethostbyname error<br>");
			return -1;
		}

		client_fd = socket(AF_INET, SOCK_STREAM, 0);
		server_port = (u_short)atoi(port);

		bzero(&client_sin, sizeof(client_sin));
		client_sin.sin_family = AF_INET;
		client_sin.sin_addr = *((struct in_addr *)he->h_addr); 
		client_sin.sin_port = htons(server_port);

		//set socket non-blocking
		int flags = fcntl(client_fd, F_GETFL, 0);
		fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

		if(connect(client_fd, (struct sockaddr *)&client_sin, sizeof(client_sin)) < 0){
			if(errno != EINPROGRESS){
				printf("client connect error<br>");
				return -1;
			}
		}
		proxy_check[id] = 0;
		return client_fd;
	}
	return -1;
}

void init_html(char **ip){
	printf("<html>\
				<head>\
					<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />\
					<title>Network Programming Homework 3</title>\
				</head>\
			<body bgcolor=#336699>\
			<font face=\"Courier New\" size=2 color=#FFFF99>\
				<table width=\"800\" border=\"1\">\
					<tr>\
						<td>%s</td>\
						<td>%s</td>\
						<td>%s</td>\
						<td>%s</td>\
						<td>%s</td>\
					</tr>\
					<tr>\
						<td valign=\"top\" id=\"m1\"></td>\
						<td valign=\"top\" id=\"m2\"></td>\
						<td valign=\"top\" id=\"m3\"></td>\
						<td valign=\"top\" id=\"m4\"></td>\
						<td valign=\"top\" id=\"m5\"></td>\
					</tr>\
				</table>\
			</font>\
			</body>\
			</html>", ip[1], ip[2], ip[3], ip[4], ip[5]);
	//fflush(stdout);
}

int readline(int fd, char *ptr, int maxlen){
	int n, rc;
	char c;
	for(n = 1; n < maxlen; n++){
		if((rc = read(fd, &c, 1)) == 1){
			if(c == '\n') {n--; break;}
			if(c == '\r') {n--; continue;}
			*ptr++ = c;
			if(c==' ' && *(ptr-2) == '%'){ break; }
		}
		else if(rc == 0){
			if(n == 1) return 0; /* EOF, no data read */
			else {n--; break;} /* EOF, some data was read */
			//else { printf("[%c] [%c] [%c] [%c] [%d]<br>", *(ptr-4), *(ptr-3), *(ptr-2), *(ptr-1), *(ptr));; break;} /* EOF, some data was read */
			//else { /* EOF, some data was read */
			//	if(*(ptr) == 0) printf("EOF<br>");
			//	break;
			//}
		}
		else
			return -1; /* error */
	}
	*ptr = 0;
	return n;
}

char* str_to_html_format(char *line){
	int i, j=0, len;
	char *newline = (char*)malloc(sizeof(char) * (BUFFERSIZE + 100));

	len = strlen(line);
	for(i = 0; i < len; i++){
		if(line[i] == '<'){
			newline[j++] = '&';
			newline[j++] = 'l';
			newline[j++] = 't';
		}
		else if(line[i] == '>'){
			newline[j++] = '&';
			newline[j++] = 'g';
			newline[j++] = 't';
		}
		else if(line[i] == '"'){
			newline[j++] = '\\';
			newline[j++] = '"';
		}
		else{
			newline[j++] = line[i];
		}
	}

	//if line is NOT '% ' needs to print newline
	if(strstr(line, "% ") == NULL){
		newline[j++] = '<';
		newline[j++] = 'b';
		newline[j++] = 'r';
		newline[j++] = '>';
	}

	newline[j] = 0;

	return newline;
}

int find_id(int fd, int id_clifd_arr[6]){
	int i = 0;
	for(i = 1;i <= 5; i++){
		if(id_clifd_arr[i] == fd){
			return i;
		}
	}
	return i;
}

int main(int argc, char *argv[], char *envp[]){
	printf("Content-type: text/html\n\n");
	
	FILE *fp;
	char line[BUFFERSIZE];
	int client_fd, nfds, id, error, i, n, NeedWrite, fd;
	int id_clifd_arr[6], id_filefd_arr[6], id_status_arr[6];
	int exit_flag[6] = {0};
	int line_num[6] = {0};
	int line_flag[6] = {0};

	fd_set rfds; FD_ZERO(&rfds); /* readable file descriptors*/
	fd_set wfds; FD_ZERO(&wfds); /* writable file descriptors*/
	fd_set rs; FD_ZERO(&rs); /* active file descriptors*/
	fd_set ws; FD_ZERO(&ws); /* active file descriptors*/
	
	char **ip, **port, **file, **proxyip, **proxyport;
	ip = (char**)malloc(sizeof(char*)*6);
	port = (char**)malloc(sizeof(char*)*6);
	file = (char**)malloc(sizeof(char*)*6);
	proxyip = (char**)malloc(sizeof(char*)*6);
	proxyport = (char**)malloc(sizeof(char*)*6);
	char *query = getenv("QUERY_STRING");
	//char *query=malloc(sizeof(char)*1000);
	//strcpy(query, "h1=&p1=&f1=&h2=nplinux3.cs.nctu.edu.tw&p2=8998&f2=t1.txt&h3=&p3=&f3=&h4=&p4=&f4=&h5=&p5=&f5=");

	int client_num = parse_query_string(query, ip, port, file, proxyip, proxyport);

	init_html(ip);

	for(i = 1; i <= 5; i++){
		if(strcmp(ip[i],"")==0 || strcmp(port[i],"")==0 || strcmp(file[i],"")==0){
			id_clifd_arr[i] = 0;
			continue;
		}

		//get client_fd
		client_fd = SOCKorTCPconnect(i, ip[i], port[i], proxyip[i], proxyport[i]);
		if(client_fd < 0) continue;
		id_clifd_arr[i] = client_fd;
		
		//open test file
		fp = fopen(file[i], "r");
		if(fp == NULL){
			printf("Error : file doesn't exist<br>");
			fflush(stdout);
			exit(1);
		}
		id_filefd_arr[i] = fileno(fp);

		//set init state
		id_status_arr[i] = F_CONNECTING;
		
		//set client_fd in fds
		FD_SET(client_fd, &rs);
		FD_SET(client_fd, &ws);
	}

	nfds = FD_SETSIZE;
	rfds = rs;
	wfds = ws;

	while(client_num > 0){
		memcpy(&rfds, &rs, sizeof(rfds)); 
		memcpy(&wfds, &ws, sizeof(wfds));
		
		if(select(nfds, &rfds, &wfds, (fd_set*)0, (struct timeval*)0) < 0 ){
			printf("select error<br>");
			return -1;
		}
		
		for(id = 1; id <=5; id++){
			fd = id_clifd_arr[id];
			//filter illegal client_fd
			if(fd == 0) continue;

			int status = id_status_arr[id];
			//in connecting state
			if(status == F_CONNECTING && (FD_ISSET(fd, &rfds) || FD_ISSET(fd, &wfds))){
				if(getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&error, &n) < 0 || error != 0) {
					//non-blocking connect failed
					printf("non-blocking connect failed<br>");
					return -1;
				}
				if(proxy_check[id]){
					id_status_arr[id] = F_REQEST;
				}
				else{
					id_status_arr[id] = F_READING;
					FD_CLR(fd, &ws);
				}
			}
			else if(status == F_REQEST && (FD_ISSET(fd, &rfds) || FD_ISSET(fd, &wfds))){
				//send sock4 request
				struct sockaddr_in server_addr;
				struct hostent *he;
				int req_port = atoi(port[id]);
				if((he=gethostbyname(ip[id])) == NULL){
					printf("gethostbyname error<br>");
					return 0;
				}
				bzero((char *) &server_addr, sizeof(server_addr));
				bcopy((char *)he->h_addr, (char *) &server_addr.sin_addr.s_addr, he->h_length);
				uint32_t req_ip = server_addr.sin_addr.s_addr;

				unsigned char request[10];
				request[0] = 4;
				request[1] = 1;
				request[2] = (req_port / 256) & 0xff;
				request[3] = (req_port % 256) & 0xff;
				request[4] = (req_ip) & 0xff;
				request[5] = (req_ip >> 8) & 0xff;
				request[6] = (req_ip >> 16) & 0xff;
				request[7] = (req_ip >> 24) & 0xff;
				request[8] = 0;
				request[9] = 0;
				
				n = write(fd, request, 9);
				if(n == 9){
					id_status_arr[id] = F_REPLY;
				}
			}
			else if(status == F_REPLY && (FD_ISSET(fd, &rfds) || FD_ISSET(fd, &wfds))){
				//recv sock4 reply
				n = read(fd, buffer, 8);
				if(n != 8) continue;

				//check if proxy granted this connect request
				if(buffer[0] == 0 && buffer[1] == 90){
					printf("proxy accept [%d] connect!!<br>", id);
					id_status_arr[id] = F_READING;
					FD_CLR(fd, &ws);
				}
				else{
					printf("proxy deny [%d] connect QQ<br>\n", id);
					id_status_arr[id] = F_DONE;
					FD_CLR(fd, &ws);
					FD_CLR(fd, &rs);
				}
			}
			//in writing state
			else if(status == F_WRITING && FD_ISSET(fd, &wfds)){
				memset(line, 0, sizeof(line));
				NeedWrite = readline(id_filefd_arr[id], line, BUFFERSIZE);
				printf("rfile [%d],[%d],[%s]<br>", id, NeedWrite, line);
				fflush(stdout);

				//print cmd in file
				if(line_flag[id] == 0){ //fuck print
					char line_buf[20];
					line_num[id] += 1;
					sprintf(line_buf, "%d ", line_num[id]);
					char *newline = str_to_html_format(line);
					printf("<script>document.all[\'m%d\'].innerHTML += \"%s%s\";</script>", id, line_buf, newline);
					fflush(stdout);
				}
				else{ //not print
					char *newline = str_to_html_format(line);
					printf("<script>document.all[\'m%d\'].innerHTML += \"%s\";</script>", id, newline);
					fflush(stdout);
					line_flag[id] = 0;
				}

				//segmentate write msg
				int offset = 0, write_cnt;
				while(NeedWrite > 0){
					write_cnt = write(fd, line + offset, strlen(line + offset));
					//printf("w %d %d %s<br>", id, write_cnt, line + offset);
					offset += write_cnt;
					NeedWrite -= write_cnt;
				}
				write(fd, "\n", 1); //append newline in msg's end for server's readline

				//read file 'exit'
				if(strstr(line, "exit") != NULL){
					printf("FUCKING EXIT<br>");
					fflush(stdout);
					exit_flag[id] = 1;
				}

				//write error or write finished
				if(NeedWrite <= 0){
					FD_CLR(fd, &ws);
					id_status_arr[id] = F_READING;
					FD_SET(fd, &rs);
				}
			}
			//in reading state
			else if(status == F_READING && FD_ISSET(fd, &rfds)){
				memset(line, 0, sizeof(line));
				n = readline(fd, line, BUFFERSIZE);
				//printf("r [%d],[%d],[%s]<br>", id, n, line);

				fflush(stdout);
				if(n < 0){
					// read error
					//printf("r %d %d read error<br>", id, n);
					FD_CLR(fd, &rs);
					id_status_arr[id] = F_DONE;
					client_num--;
					close(fd);
					close(id_filefd_arr[id]);
					continue;
				}
				else if((n == 0 || n == 2) && exit_flag[id] == 1){
					//printf("r fuckexit %d<br>", exit_flag[id]);
					//read file 'exit' and finish client
					FD_CLR(fd, &rs);
					FD_CLR(fd, &ws);
					close(fd);
					close(id_filefd_arr[id]);
					id_status_arr[id] = F_DONE;
					client_num--;
					continue;
				}
				else {
					//print everything received to html
					char line_buf[20];
					line_num[id] += 1;
					sprintf(line_buf, "%d ", line_num[id]);
					char *newline = str_to_html_format(line);
					printf("<script>document.all[\'m%d\'].innerHTML += \"%s%s\";</script>", id, line_buf, newline);
					fflush(stdout);

					if(strstr(line, "% ") != NULL){
						line_flag[id] = 1;
						printf("r find %%<br>");
						fflush(stdout);
						//read "% " change to write_state
						FD_CLR(fd, &rs);
						id_status_arr[id] = F_WRITING;
						FD_SET(fd, &ws);
					}
				}
			}
		}
	}

	//free
	free(ip);
	free(port);
	free(file);
	free(proxyip);
	free(proxyport);

	return 0;
}
