#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>	//inet_addr
#include <unistd.h>

#define MAXLINE 512

char *WELLCOME_MSG = "\
**************************************************************\n\
** Welcome to the information server, myserver.nctu.edu.tw. **\n\
**************************************************************\n\
** You are in the directory, /home/0656508/ras/.\n\
** This directory will be under \"/\", in this system.  \n\
** This directory includes the following executable programs. \n\
** \n\
**	bin/ \n\
**	test.html	(test file)\n\
**\n\
** The directory bin/ includes: \n\
**	cat\n\
**	ls\n\
**	removetag		(Remove HTML tags.)\n\
**	removetag0		(Remove HTML tags with error message.)\n\
**	number  		(Add a number in each line.)\n\
**\n\
** In addition, the following two commands are supported by ras. \n\
**	setenv	\n\
**	printenv	\n\
** \n";

void str_cli(FILE *fp, int sockfd);
int readline(int fd, char *ptr, int maxlen);

int main(int argc ,char *argv[]){
	int socketfd;
	struct sockaddr_in serv_addr;

	//Create TCP socket
	socketfd = socket(AF_INET ,SOCK_STREAM ,0);
	if(socketfd == -1){
		puts("Client : Could not create socket");
	}
	puts("Client : Socket created");
	
	//Prepare the sockaddr_in structure
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	serv_addr.sin_port = htons(atoi(argv[1]));
	
	//Connect to remote server
	if(connect(socketfd ,(struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
		puts("Client : Connect failed");
		return 1;
	}
	puts("Client : Connected");
	//puts(WELLCOME_MSG);
	
	str_cli(stdin, socketfd); /* do it all */

	close(socketfd);
	return 0;
}

void str_cli(FILE *fp, int socketfd){
	int n;
	char sendline[MAXLINE], recvline[MAXLINE + 1];
	printf("%% ");
	while(fgets(sendline, MAXLINE, fp) != NULL){
		//Send some data
		if(send(socketfd, sendline, strlen(sendline), 0) < 0){
			puts("str_cli : Send failed");
		}
		
		//Receive a reply from the server
		/*if(recv(socketfd, recvline, MAXLINE, 0) < 0){
			puts("str_cli : Recv failed");
			break;
		}*/

		// Now read a line from the socket and write it to our standard output.
		n = readline(socketfd, recvline, MAXLINE);
		if(n < 0){
			puts("str_cli: readline error");
		}
		printf("%d\n", n);
		recvline[n] = 0; /* null terminate */
		printf("Client : recv > %s",recvline);
		//fputs(recvline, stdout);
		printf("%% ");
	}
}

int readline(int fd, char *ptr, int maxlen){
	int n, rc;
	char c;
	for(n = 1; n < maxlen; n++){
		if((rc = read(fd, &c, 1)) == 1){
			*ptr++ = c;
			if(c == '\n') break;
		}
		else if(rc == 0){
			if(n == 1) return 0; /* EOF, no data read */
			else break; /* EOF, some data was read */
		}
		else
			return -1; /* error */
	}
	*ptr = 0;
	printf("func-%d\n", n);
	return n;
}