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
char *argvs, *file;

int create_http_serversock(int port){
	int serversock;
	struct sockaddr_in server_addr;

	serversock = socket(AF_INET, SOCK_STREAM, 0);
	if(serversock < 0){
		printf("caanot create server socket\n");
	}
	printf("create server socket\n");

	bzero((char *)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port);

	if(bind(serversock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
		printf("can't bind to port: %d\n", port);
	}
	printf("bind to port: %d\n", port);

	if(listen(serversock, 5) < 0){
		printf("can't listen on port: %d\n", port);
	}

	printf("server wait for connection\n");
	return serversock;
}

void sig_handler(int signo){
	int status;
	//for waiting child process
	if(signo == SIGCHLD){
		waitpid(0, &status, WNOHANG);
	}
}

int read_http_requrst(int clientsock, char *buffer){
	int n, i;

	n = read(clientsock, buffer, BUFFERSIZE);
	if(n == 0 || n < 0){
		return -1;
	}

	if(n > 0 && n < BUFFERSIZE)
		buffer[n] = 0;
	else
		buffer[0] = 0;

	for(i = 0; i < n; i++){
		if(buffer[i] == '\n' || buffer[i] == '\r'){
			buffer[i] = 0;
		}
	}

	return n;
}

int parse_http_request(char *buffer){
	//GET /form_get.htm?h1=GG&p1=ININ&f1=der&h2=&p2=&f2= HTTP/1.1
	//GET /form_get.htm HTTP/1.1
	char *tmp, *tmp2;
	int i = 0;
	
	tmp = strtok(buffer ," ");
	while(tmp != NULL){
		if(i == 1) break;
		i++;
		tmp = strtok(NULL ," ");
	}

	i = 0;
	tmp2 = strtok(tmp ,"?");
	while(tmp2 != NULL){
		if(i == 0) file = strdup(++tmp2);
		if(i == 1) argvs = strdup(tmp2);
		i++;
		tmp2 = strtok(NULL ,"?");
	}

	if((i-1) == 0){
		printf("%s\n", file);
		return 0;
	}
	else{
		printf("%s\n%s\n", file, argvs);
		return 1;
	}

	return 0;
}

void set_env(){
	setenv("CONTENT_LENGTH", "", 1);
	setenv("REQUEST_METHOD", "", 1);
	setenv("SCRIPT_NAME", "", 1);
	setenv("REMOTE_HOST", "", 1);
	setenv("REMOTE_ADDR", "", 1);
	setenv("AUTH_TYPE", "", 1);
	setenv("REMOTE_USER", "", 1);
	setenv("REMOTE_IDENT", "", 1);
	setenv("QUERY_STRING", "", 1);
}

void client_request_handler(int clientsock){
	int len, i, argv_flag, file_fd;
	char buffer[BUFFERSIZE+1];
	FILE *fp;

	len = read_http_requrst(clientsock, buffer);
	if(len < 0) printf("read error\n");

	//set env
	set_env();

	argv_flag = parse_http_request(buffer);
	if(argv_flag){ //request contain file with argvs
		//need to set query_string env
		setenv("QUERY_STRING", argvs, 1);
	}

	//check form_get2.htm
	if(strcmp(file, "form_get2.htm") == 0){
		file_fd = open(file, O_RDONLY);
		if(file_fd < 0){
			printf("%s doesn't exist\n", file);
			return;
		}
		printf("%s open\n", file);

		//send 200 response
		write(clientsock, "HTTP/1.1 200 OK\r\n", 17);
		//printf("HTTP/1.1 200 OK\r\n");
		
		strcpy(buffer, "Content-Type: text/html\n\n");
		write(clientsock, buffer, strlen(buffer));
		//printf("%s\n", buffer);
		
		while((len = read(file_fd, buffer, BUFFERSIZE)) > 0){
			write(clientsock, buffer, len);
			//printf("%s\n", buffer);
		}
		printf("print form_get2 done\n");
	}
	else{ //exec *.cgi
		if(strcmp(file, "favicon.ico") == 0) return;

		int pid, status;
		printf("executing %s\n", file);
		pid = fork();
		if(pid < 0){
			printf("fork error\n");
			return;
		}
		else if(pid == 0){
			char *arggv[] = {NULL};
			char path[100], fullpathfile[200];
			
			if(getcwd(path, sizeof(path)) == NULL){
				printf("get path error\n");
				return;
			}
			sprintf(fullpathfile, "%s/%s", path, file);
			printf("%s\n", fullpathfile);

			write(clientsock, "HTTP/1.1 200 OK\r\n", 17);
			strcpy(buffer, "Content-Type: text/html\n\n");
			write(clientsock, buffer, strlen(buffer));

			dup2(clientsock, STDOUT_FILENO);
			if(execvp(fullpathfile, arggv) < 0){
				//printf("child : exec failed\n");
				return;
			}

			exit(1);
		}
		else{
			waitpid(pid, &status, WUNTRACED);
			printf("%s exec success\n", file);
			//printf("in parent\n");
		}
	}
	return;
}

int main(int argc, char *argv[], char *envp[]){
	int clilen, clientsock, clientpid;
	struct sockaddr_in client_addr;

	int serversock = create_http_serversock(atoi(argv[1]));

	//wait for child process for preventing zombie process
	signal(SIGCHLD, sig_handler);

	while(1){
		clilen = sizeof(client_addr);
		clientsock = accept(serversock, (struct sockaddr *)&client_addr, (socklen_t*)&clilen);
		if (clientsock < 0){
			printf("accept fail\n");
		}

		char *client_ip = inet_ntoa(client_addr.sin_addr);
		int client_port = (int)ntohs(client_addr.sin_port);
		printf("accept [%d]'s connection from %s/%d\n", clientsock, client_ip, client_port);

		int status;
		clientpid = fork();
		if(clientpid < 0) {
			printf("fork ERROR\n");
		}
		else if(clientpid == 0){ //clild proc
			//close(serversock);
			client_request_handler(clientsock);
			exit(0);
		}
		else{ //parent proc
			close(clientsock);
			waitpid(-1, NULL, WNOHANG);
		}
	}

	return 0;
}