#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#define CCOUNT 64*1024
#define MAXLINE 20000

int parse_query_string(char *query, char **ip, char **port, char **file){
	//h1=GG&p1=ININ&f1=der&h2=&p2=&f2=&h3=&p3=&f3=&h4=&p4=&f4=&h5=&p5=&f5=
	char *str;
	int clientID = 1, i = 1, client_count = 0;
	str = strtok(query, "&");
	while(str != NULL){
		if(i > 3){
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
		}
		i = i + 1;
		str = strtok(NULL, "&");
	}

	if(strcmp(ip[clientID],"")!=0 && strcmp(port[clientID], "")!=0 && strcmp(file[clientID], "")!=0){
		client_count = client_count + 1;
	}

	for(int i = 1; i <= 5; i++){
		printf("<br>client[%d]<br>", i);
		printf("-%s-<br>", ip[i]);
		printf("-%s-<br>", port[i]);
		printf("-%s-<br>", file[i]);
	}
	printf("-%d-<br>", client_count);

	return client_count;
}

int TCPconnect(char *ip, char *port){
	struct sockaddr_in client_sin;
	struct hostent *he;
	int server_port, client_fd, one = 1;

	if((he=gethostbyname(ip)) == NULL){
		fprintf(stderr,"Usage : client <server ip> <port> <testfile>");
	}

	client_fd = socket(AF_INET, SOCK_STREAM, 0);
	server_port = (u_short)atoi(port);

	bzero(&client_sin, sizeof(client_sin));
	client_sin.sin_family = AF_INET;
	client_sin.sin_addr = *((struct in_addr *)he->h_addr); 
	client_sin.sin_port = htons(server_port);

	if(ioctl(client_fd, FIONBIO, (char *)&one)){
		printf("can't mark socket nonblocking");
	}

	if(connect(client_fd, (struct sockaddr *)&client_sin, sizeof(client_sin)) < 0){
		if(errno != EINPROGRESS){
			printf("client connect error<br>");
		}
	}
	
	sleep(1); //waiting for welcome messages

	return client_fd;
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
	fflush(stdout);
}

int readline(int fd, char *ptr, int maxlen){
	int n, rc;
	char c;
	for(n = 1; n < maxlen; n++){
		if((rc = read(fd, &c, 1)) == 1){
			if(c==' ' && *(ptr-1) == '%'){ return -87; }
			if(c == '\n') break;
			if(c == '\r') continue; //set for character "carriage return"
			*ptr++ = c;
		}
		else if(rc == 0){
			if(n == 1) return 0; /* EOF, no data read */
			else break; /* EOF, some data was read */
		}
		else
			return -1; /* error */
	}
	*ptr = 0;
	return n;
}

int readfile(int fd, char *ptr, int maxlen){
	int n, rc;
	char c;
	for(n = 1; n < maxlen; n++){
		if((rc = read(fd, &c, 1)) == 1){
			if(c == '\n') break;
			if(c == '\r') continue; //set for character "carriage return"
			*ptr++ = c;
		}
		else if(rc == 0){
			if(n == 1) return 0; /* EOF, no data read */
			else break; /* EOF, some data was read */
		}
		else
			return -1; /* error */
	}
	*ptr = 0;
	return n;
}

int find_id(int fd, int id_clifd_arr[5]){
	int i = 0;
	for(i = 1;i <= 5; i++){
		if(id_clifd_arr[i] == fd){
			return i;
		}
	}
	return i;
}

char* str_to_html_format(char *line){
	int i, j=0, len;
	char *newline = (char*)malloc(sizeof(char) * (MAXLINE + 100));

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
	newline[j] = 0;

	return newline;
}

int client_read_write_handler(fd_set *pafds, int nfds, int hcount, int id_clifd_arr[6], int id_filefd_arr[6]){
	char line[MAXLINE];
	int fd, i, n, id;
	
	int state_flag[6]; //1: for read, 2: for write
	for(i = 1 ; i <= 5; i++){
		state_flag[i] = 1;
	}

	fd_set rfds, rcfds; // read/write fd sets
	bcopy((char *)pafds, (char *)&rcfds, sizeof(rcfds));

	//int cnt = 500;

	while(hcount){
		//cnt--;
		//if(cnt < 0) break;

		bcopy((char *)&rcfds, (char *)&rfds, sizeof(rfds));

		if(select(nfds, &rfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0) < 0){
			printf("select failed<br>"); fflush(stdout);
		}
		
		for(fd=0; fd < nfds; ++fd){
			//get client id by fd
			id = find_id(fd, id_clifd_arr);

			//only check legal id
			if(id >=1 && id <=5){
				//receive msg from server
				if(FD_ISSET(fd, &rfds) && (state_flag[id] == 1)){
					//read server response
					memset(line, 0, strlen(line));
					n = readline(fd, line, MAXLINE);
					if(n == 0){
						//puts("Read: read nothing<br>"); fflush(stdout);
						shutdown(fd,2);
						close(fd);
						FD_CLR(fd, &rcfds);
						hcount--;
					}
					else if(n < 0){
						//check server response if write request
						if(n == -87){
							state_flag[id] = 2;
							printf("<script>document.all[\'m%d\'].innerHTML += \"%s\" ;</script>", id, "% ");
							fflush(stdout);
						}
						else{
							//puts("Read: readline error<br>"); fflush(stdout);
							shutdown(fd,2);
							close(fd);
							FD_CLR(fd, &rcfds);
							hcount--;
						}
					}
					else{
						char *newline = str_to_html_format(line);
						printf("<script>document.all[\'m%d\'].innerHTML += \"%s<br>\";</script>", id, newline);
						fflush(stdout);
						free(newline);
					}
				}
				//send msg to server
				if(state_flag[id] == 2){
					//read file as input
					memset(line, 0, strlen(line));
					n = readfile(id_filefd_arr[id], line, MAXLINE);
					if(n == 0){
						puts("Readfile: readfile nothing<br>"); fflush(stdout);
					}
					else if(n < 0) puts("Readfile error<br>");

					//send to server
					printf("<script>document.all[\'m%d\'].innerHTML += \"%s<br>\";</script>", id, line);
					fflush(stdout);
					strcat(line, "\n\0");
					if(write(fd, line, strlen(line)) <= 0){
						printf("Write error<br>"); fflush(stdout);
					}
					state_flag[id] = 1;
				}
			}
		}
	}
}

int main(int argc, char *argv[], char *envp[]){
	printf("Content-type: text/html\n\n");
	
	int id_clifd_arr[6] = {0};
	int client_fd, id_filefd_arr[6], maxfd = -1;
	FILE *fp;

	fd_set afds;
	FD_ZERO(&afds);

	char **ip, **port, **file;
	ip = (char**)malloc(sizeof(char*)*6);
	port = (char**)malloc(sizeof(char*)*6);
	file = (char**)malloc(sizeof(char*)*6);
	char *query = getenv("QUERY_STRING");
	//char *query=malloc(sizeof(char)*1000);
	//strcpy(query, "h1=nplinux3.cs.nctu.edu.tw&p1=8779&f1=t1.txt&h2=&p2=&f2=&h3=&p3=&f3=&h4=&p4=&f4=&h5=&p5=&f5=");

	int client_num = parse_query_string(query, ip, port, file);

	init_html(ip);

	for(int i = 1; i <= client_num; i++){
		client_fd = TCPconnect(ip[i], port[i]);
		id_clifd_arr[i] = client_fd;
		
		fp = fopen(file[i], "r");
		if(fp == NULL){
			printf("Error : file doesn't exist<br>");
			fflush(stdout);
			exit(1);
		}
		id_filefd_arr[i] = fileno(fp);

		if(client_fd > maxfd){
			maxfd = client_fd;
		}
		FD_SET(client_fd, &afds);
	}

	client_read_write_handler(&afds, maxfd+1, client_num, id_clifd_arr, id_filefd_arr);

	free(ip);
	free(port);
	free(file);
	return 0;
}