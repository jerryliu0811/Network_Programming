#include <iostream>
#include <string>
#include <map>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

#define QLEN 5
#define BUFSIZE 4096
#define MAXLINE 10000

int serversock;
int clientID_pool_arr[30] = {0}; // value 1 means this idx+1 has been taken, otherwise this idx+1 is free to use
char line_cpy[MAXLINE];

struct client{
	int id;
	int fd;
	char *name;
	char *ip;
	int port;
	char *pathname;
	char *pathvalue;
	char *PATHvalue;
	int *pipenum_arr;
	int *pipe_open_flag_arr;
	int *pipefd[50000];
	int pipeClifd[30][2]; //for recv other client's pipe
	int pipeCli_open_flag_arr[30];
	int cmd_counter;
};

map<int, int> idTable; //id (1~30), clientsockfd
map<int, client> clientTable;
map<int, int>::iterator id_iter;
map<int, client>::iterator iter;

char welcome[] = "\
****************************************\n\
** Welcome to the information server. **\n\
****************************************\n";

int passiveTCP(int port_num, int qlen){
	int s = socket(AF_INET, SOCK_STREAM, 0);
	if(s < 0){
		printf("can't create socket\n");
	}
	printf("[Server] create socket success\n");

	struct sockaddr_in server_addr;
	bzero((char *)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port_num);
	
	if(bind(s, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
		printf("can't bind to port: %d\n", port_num);
	}
	printf("[Server] bind address success\n");

	if(listen(s, qlen) < 0){
		printf("can't listen on port: %d\n", port_num);
	}
	printf("[Server] wait for connection\n");

	return s;
}

int readline(int fd, char *ptr, int maxlen){
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

int parse_cmd(int cli_socketfd, char *str, char ***token){
	//remove '\n' in str
	if(strlen(str) > 1 && str[strlen(str)-1] == '\n') str[strlen(str)-1] = '\0';
	
	//according recv input, cutting them by space and '|' then save to token array
	//also set cmd's pipe destination index
	int i = 0, j = 0;
	token[i][j] = strtok(str," ");
	while(token[i][j] != NULL){
		//check tell
		if(strcmp(token[i][j], "tell") == 0){
			token[i][++j] = strtok(NULL, " ");
			token[i][++j] = strtok(NULL, "\0");
			break;
		}
		//check yell
		else if(strcmp(token[i][j], "yell") == 0){
			token[i][++j] = strtok(NULL, "\0");
			break;
		}
		//check name
		else if(strcmp(token[i][j], "name") == 0){
			token[i][++j] = strtok(NULL, "\0");
			break;
		}
		//check pipe
		else if(token[i][j][0] == '|'){
			//shift this input token's number to the global index
			int idx = i + clientTable[cli_socketfd].cmd_counter;
			
			//check if pipe number exists
			if(strlen(token[i][j]) > 1){ //condition like "|4"
				clientTable[cli_socketfd].pipenum_arr[idx] = atoi(token[i][j]+1) + idx;
			}
			else{ //condition like "|"
				clientTable[cli_socketfd].pipenum_arr[idx] = 1 + idx;
			}

			//set this pipe token to NULL for execvp used
			token[i][j] = NULL;

			//move index i to next row
			i++;

			//move index j to the head
			j = 0;
		}
		else{
			//move index j to next col
			j++;
		}

		//keep cutting the input string
		token[i][j] = strtok(NULL, " ");
	}

	//minus cmd_num if last token just pipe number
	if(j== 0) i--; 
	
	// set the last token pipe to -1 for execute cmd checking
	int last_idx = i + clientTable[cli_socketfd].cmd_counter;
	if(clientTable[cli_socketfd].pipenum_arr[last_idx] == 0){
		clientTable[cli_socketfd].pipenum_arr[last_idx] = -1;
	}
	
	//make i + 1 to cmd_num
	i += 1;

	//print token
	/*int k,l;
	for(k = 0; k < i; k++){
		for(l = 0; token[k][l]; l++){
			printf("token[%d][%d]:[%s]\n", k, l, token[k][l]);
		}
		printf("cmd[%d] -> [%d]\n", k+clientTable[cli_socketfd].cmd_counter, clientTable[cli_socketfd].pipenum_arr[k+clientTable[cli_socketfd].cmd_counter]);
	}
	printf("\n------parse success------\n");*/
	
	return i;
}

int check_cmd_exist(int cli_socketfd, char *cmd){
	char *path = getenv("PATH");
	char *tmppath;

	//go through all reachable directory to check
	tmppath = strtok(path,":");
	while(tmppath != NULL){
		char filepath[500];
		strcpy(filepath,tmppath);
		strcat(filepath,"/");
		strcat(filepath,cmd);

		//check cmd's execute file exists or not
		if(access(filepath, F_OK) != -1) {
			return 1;
		}

		tmppath = strtok(NULL, ":");
	}

	return 0;
}

int execute_cmd(int cli_socketfd, char ***token, int cmd_num){
	pid_t pid;
	int i, status; //create pipe array

	for(i = 0; i < cmd_num; i++){

		printf("Processing cli[%d] cmd:[%s]\n", cli_socketfd, token[i][0]);

		//top priority for special cmds checking like: exit, setenv, printenv
		if(strcmp(token[i][0],"exit") == 0){
			return -1;
		}
		else if(strcmp(token[i][0],"setenv") == 0){
			clientTable[cli_socketfd].pathname = strdup(token[i][1]);
			clientTable[cli_socketfd].pathvalue = strdup(token[i][2]);
			if(strcmp(token[i][1],"PATH") == 0){
				clientTable[cli_socketfd].PATHvalue = strdup(token[i][2]);
			}
			setenv(token[i][1], token[i][2], 1);
			break;
		}
		else if(strcmp(token[i][0],"printenv") == 0){
			char *path = clientTable[cli_socketfd].pathvalue;
			char buffer[500];
			sprintf(buffer, "%s=%s\n", token[i][1], path);
			write(cli_socketfd, buffer, strlen(buffer));
			break;
		}
		else if(strcmp(token[i][0],"who") == 0){
			int j;
			char buffer[5000] = {0};
			char row[100] = {0};
			strcat(buffer, "<ID>\t<nickname>\t<IP/port>\t<indicate me>\n");
			for(j = 0; j < 30; j++){
				if(clientID_pool_arr[j] == 1){
					if((j+1) == clientTable[cli_socketfd].id){
						if(strcmp(clientTable[cli_socketfd].name, "no name") == 0){
							sprintf(row, "%d\t(%s)\tCGILAB/511\t<-me\n", clientTable[cli_socketfd].id, clientTable[cli_socketfd].name);
							//sprintf(row, "%d\t(%s)\t%s/%d\t<-me\n", clientTable[cli_socketfd].id, clientTable[cli_socketfd].name, clientTable[cli_socketfd].ip, clientTable[cli_socketfd].port);
							strcat(buffer, row);
						}
						else{
							sprintf(row, "%d\t%s\tCGILAB/511\t<-me\n", clientTable[cli_socketfd].id, clientTable[cli_socketfd].name);
							//sprintf(row, "%d\t%s\t%s/%d\t<-me\n", clientTable[cli_socketfd].id, clientTable[cli_socketfd].name, clientTable[cli_socketfd].ip, clientTable[cli_socketfd].port);
							strcat(buffer, row);
						}
					}
					else{
						id_iter = idTable.find(j+1);
						int clientsockfd = id_iter->second;
						iter = clientTable.find(clientsockfd);
						if(strcmp(iter->second.name, "no name") == 0){
							sprintf(row, "%d\t(%s)\tCGILAB/511\n", iter->second.id, iter->second.name);
							//sprintf(row, "%d\t(%s)\t%s/%d\n", iter->second.id, iter->second.name, iter->second.ip, iter->second.port);
							strcat(buffer, row);
						}
						else{
							sprintf(row, "%d\t%s\tCGILAB/511\n", iter->second.id, iter->second.name);
							//sprintf(row, "%d\t%s\t%s/%d\n", iter->second.id, iter->second.name, iter->second.ip, iter->second.port);
							strcat(buffer, row);
						}
					}
				}
			}
			write(cli_socketfd, buffer, strlen(buffer));
			break;
		}
		else if(strcmp(token[i][0],"tell") == 0){
			char buffer[500];
			int targetCliId = atoi(token[i][1]);
			id_iter = idTable.find(targetCliId);
			if(id_iter != idTable.end()){
				sprintf(buffer, "*** %s told you ***: %s\n", clientTable[cli_socketfd].name, token[i][2]);
				int targetClifd = id_iter->second;
				write(targetClifd, buffer, strlen(buffer));
			}
			else{
				sprintf(buffer, "*** Error: user #%d does not exist yet. ***\n", targetCliId);
				write(cli_socketfd, buffer, strlen(buffer));
			}
			break;
		}
		else if(strcmp(token[i][0],"yell") == 0){
			char buffer[500];
			sprintf(buffer, "*** %s yelled ***: %s\n", clientTable[cli_socketfd].name, token[i][1]);
			for(iter = clientTable.begin(); iter != clientTable.end(); iter++){
				if(iter->second.fd != serversock){
					write(iter->second.fd, buffer, strlen(buffer));
				}
			}
			break;
		}
		else if(strcmp(token[i][0],"name") == 0){
			int name_existed_flag = 0;
			for(iter = clientTable.begin(); iter != clientTable.end(); iter++){
				if(iter->second.fd != serversock && iter->second.fd != cli_socketfd){
					if(strcmp(iter->second.name, token[i][1]) == 0){
						name_existed_flag = 1;
						break;
					}
				}
			}

			char buffer[500];
			if(name_existed_flag == 0){
				clientTable[cli_socketfd].name = strdup(token[i][1]);
				sprintf(buffer, "*** User from CGILAB/511 is named '%s'. ***\n", clientTable[cli_socketfd].name);
				//sprintf(buffer, "*** User from %s/%d is named '%s'. ***\n", clientTable[cli_socketfd].ip, clientTable[cli_socketfd].port, clientTable[cli_socketfd].name);
				for(iter = clientTable.begin(); iter != clientTable.end(); iter++){
					if(iter->second.fd != serversock){
						write(iter->second.fd, buffer, strlen(buffer));
					}
				}
			}
			else{
				sprintf(buffer, "*** User '%s' already exists. ***\n", token[i][1]);
				write(cli_socketfd, buffer, strlen(buffer));
			}
			break;
		}

		//set index to global counter for this cmd
		int idx = i + clientTable[cli_socketfd].cmd_counter;
		int pipe_out_idx = clientTable[cli_socketfd].pipenum_arr[idx];
		//printf("global index is [%d]\n", idx);

		//check normal cmds exist or not in now env.
		setenv("PATH", clientTable[cli_socketfd].pathvalue, 1);

		if(check_cmd_exist(cli_socketfd, token[i][0]) == 0){
			char GGbuffer[500];
			int s;
			//set all cmds after this unknown command that their pipe_out_idx to 0
			for(s = i; s < cmd_num; s++)
				clientTable[cli_socketfd].pipenum_arr[s+clientTable[cli_socketfd].cmd_counter] = 0;
			sprintf(GGbuffer, "Unknown command: [%s].\n", token[i][0]);
			write(cli_socketfd, GGbuffer, strlen(GGbuffer));
			printf("pipe out to [%d]\n", clientTable[cli_socketfd].pipenum_arr[idx]);
			printf("unknown command\n");
			printf("------execute cmd success------\n");
			if(cmd_num == 1) return 1; //special case for unknown cmd show alone
			return i;
		}

		//check cmd '>' for write file request
		//check cmd '>num' for pipe request
		//check cmd '<num' for recv pipe request
		int writefile_idx, writefile_flag = 0;
		int dest_clientId, pipeclient_flag = 0;
		int src_clientId, read_pipeclient_flag = 0;
		int j;
		for(j = 0; token[i][j]; j++){
			if(strcmp(token[i][j], ">") == 0){ //check '>'
				if(token[i][j+1]) writefile_flag = 1;
				token[i][j] = NULL;
				writefile_idx = j + 1;
				//printf("find WRITE FILE request to [%d]\n", writefile_idx);
				//break;
			}
			else if(token[i][j][0] == '>'){ // check '>num'
				pipeclient_flag = 1;
				//token[i][j] += 1;
				dest_clientId = atoi(token[i][j]+1);
				token[i][j] = NULL;
				//printf("find PIPE TO CLIENT request to client[%d]\n", dest_clientId);
				//break;
			}
			else if(token[i][j][0] == '<'){ // check '<num'
				read_pipeclient_flag = 1;
				//token[i][j] += 1;
				src_clientId = atoi(token[i][j]+1);
				token[i][j] = NULL;
				//printf("find READ FROM CLIENT request from client[%d]\n", src_clientId);
				//break;
			}
		}

		int myid;
		int dest_clientfd;
		int src_clientfd;
		char *myname;
		char *dest_clientname;
		char *src_clientname;

		//check pipe client request
		if(pipeclient_flag){
			//check dest. client exist or not
			id_iter = idTable.find(dest_clientId);
			if(id_iter == idTable.end()){ // dest. client not find
				char buffer[500];
				sprintf(buffer, "*** Error: user #%d does not exist yet. ***\n", dest_clientId);
				write(cli_socketfd, buffer, strlen(buffer));
				break;
			}

			myid = clientTable[cli_socketfd].id;
			myname = strdup(clientTable[cli_socketfd].name);
			dest_clientfd = id_iter->second;
			dest_clientname = strdup(clientTable[dest_clientfd].name);
			
			//check dest. client pipe opened or not
			if(clientTable[dest_clientfd].pipeCli_open_flag_arr[myid-1] == 1){ //dest. client pipe has already open
				char buffer[500];
				sprintf(buffer, "*** Error: the pipe #%d->#%d already exists. ***\n", myid, dest_clientId);
				write(cli_socketfd, buffer, strlen(buffer));
				break;
			}
			else{ //dest. client pipe not open
				//create pipe
				if(pipe(clientTable[dest_clientfd].pipeClifd[myid-1]) < 0) perror("ERROR:create dest. pipe error\n");
				clientTable[dest_clientfd].pipeCli_open_flag_arr[myid-1] = 1;
				printf("client out pipe create: %d %d on cliID[%d]\n", clientTable[dest_clientfd].pipeClifd[myid-1][0], clientTable[dest_clientfd].pipeClifd[myid-1][1], dest_clientId);
			}
		}
		
		//check read client pipe request
		if(read_pipeclient_flag){
			//check src. client exist or not
			myid = clientTable[cli_socketfd].id;
			myname = strdup(clientTable[cli_socketfd].name);
			
			id_iter = idTable.find(src_clientId);
			if(id_iter == idTable.end()){ // src. client not find
				char buffer[500];
				sprintf(buffer, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", src_clientId, myid);
				write(cli_socketfd, buffer, strlen(buffer));
				break;
			}
			
			src_clientfd = id_iter->second;
			src_clientname = strdup(clientTable[src_clientfd].name);

			//check my pipe[src_client_id] opened or not
			if(clientTable[cli_socketfd].pipeCli_open_flag_arr[src_clientId-1] == 0){ //src. client pipe not open
				char buffer[500];
				sprintf(buffer, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", src_clientId, myid);
				write(cli_socketfd, buffer, strlen(buffer));
				break;
			}

			printf("read client pipe [%d][%d]\n", clientTable[cli_socketfd].pipeClifd[src_clientId-1][0], clientTable[cli_socketfd].pipeClifd[src_clientId-1][1]);
		}

		//printf("pipe out to [%d]\n", pipe_out_idx);
		//check dest. pipe opened or not
		if(pipe_out_idx == -1){
			//no need to create dest. pipe, just do nothing
		}
		else if(clientTable[cli_socketfd].pipe_open_flag_arr[pipe_out_idx] == 0){
			//create pipe
			clientTable[cli_socketfd].pipefd[pipe_out_idx] = (int*)malloc(sizeof(int)*2);
			if(pipe(clientTable[cli_socketfd].pipefd[pipe_out_idx]) < 0) perror("ERROR:create dest. pipe error\n");
			clientTable[cli_socketfd].pipe_open_flag_arr[pipe_out_idx] = 1;
			printf("out pipe create : %d %d\n", clientTable[cli_socketfd].pipefd[pipe_out_idx][0], clientTable[cli_socketfd].pipefd[pipe_out_idx][1]);
		}

		//fork for exec() 
		if((pid = fork()) < 0) {printf("fork error\n");exit(1);}
		else if(pid == 0){ // for the cmd child process: 
			//check client input pipe
			if(read_pipeclient_flag){
				printf("cmdChild : set client input pipe [%d][%d]\n", clientTable[cli_socketfd].pipeClifd[src_clientId-1][0], clientTable[cli_socketfd].pipeClifd[src_clientId-1][1]);
				close(clientTable[cli_socketfd].pipeClifd[src_clientId-1][1]); //close this pipe write_end
				dup2(clientTable[cli_socketfd].pipeClifd[src_clientId-1][0], STDIN_FILENO); // read data from this pipe read_end
			}
			//check input pipe has opended
			else if(clientTable[cli_socketfd].pipe_open_flag_arr[idx] == 1){
				printf("cmdChild : set input pipe [%d][%d]\n", clientTable[cli_socketfd].pipefd[idx][0], clientTable[cli_socketfd].pipefd[idx][1]);
				close(clientTable[cli_socketfd].pipefd[idx][1]); //close this pipe write_end
				dup2(clientTable[cli_socketfd].pipefd[idx][0], STDIN_FILENO); // read data from this pipe read_end
			}

			//set output
			if(writefile_flag){ //write output to specified file
				printf("cmdChild : set output file\n");
				FILE *fp = fopen(token[i][writefile_idx],"w");
				if(fp == NULL) printf("cmdChild : open file error\n");
				else{
					int fp_num = fileno(fp);
					dup2(cli_socketfd, STDERR_FILENO); // send stderr to clisocketfd
					dup2(fp_num, STDOUT_FILENO); // write stdout to file
				}
			}
			else if(pipeclient_flag){ //write output to dest. client pipe
				printf("cmdChild : set output client pipe [%d][%d]\n", clientTable[dest_clientfd].pipeClifd[myid-1][0], clientTable[dest_clientfd].pipeClifd[myid-1][1]);
				close(clientTable[dest_clientfd].pipeClifd[myid-1][0]); //close dest. pipe read_end
				dup2(clientTable[dest_clientfd].pipeClifd[myid-1][1], STDERR_FILENO); // send stderr to the dest. pipe write_end
				dup2(clientTable[dest_clientfd].pipeClifd[myid-1][1], STDOUT_FILENO); // send stdout to the dest. pipe write_end
			}
			else if(pipe_out_idx == -1){ //write output to client
				printf("cmdChild : set output socket\n");
				dup2(cli_socketfd, STDERR_FILENO); // send stderr to clisocketfd
				dup2(cli_socketfd, STDOUT_FILENO); // send stdout to clisocketfd
			}
			else{ //write output to dest. pipe
				printf("cmdChild : set output pipe [%d][%d]\n", clientTable[cli_socketfd].pipefd[pipe_out_idx][0], clientTable[cli_socketfd].pipefd[pipe_out_idx][1]);
				close(clientTable[cli_socketfd].pipefd[pipe_out_idx][0]); //close dest. pipe read_end
				dup2(cli_socketfd, STDERR_FILENO); // send stderr to clisocketfd
				dup2(clientTable[cli_socketfd].pipefd[pipe_out_idx][1], STDOUT_FILENO); // send stdout to the dest. pipe write_end
			}

			// exec this cmd
			if(execvp(token[i][0], token[i]) < 0) puts("cmdChild : exec failed");
			exit(1);
		}
		else{ //for the cmd parent process:
			//check input pipe has opended
			if(clientTable[cli_socketfd].pipe_open_flag_arr[idx] == 1){
				//no longer needs this pipe, close it
				close(clientTable[cli_socketfd].pipefd[idx][1]);
				close(clientTable[cli_socketfd].pipefd[idx][0]);
			}

			//check read client pipe request, if so, clear pipe
			if(read_pipeclient_flag == 1){
				close(clientTable[cli_socketfd].pipeClifd[src_clientId-1][0]);
				close(clientTable[cli_socketfd].pipeClifd[src_clientId-1][1]);
				clientTable[cli_socketfd].pipeCli_open_flag_arr[src_clientId-1] = 0;

				char buffer1[500];
				char buffer2[500];
				char buffer[1000];

				for(iter = clientTable.begin(); iter != clientTable.end(); iter++){
					if(iter->second.fd != serversock){
						if(strcmp(myname, "no name") == 0){
							sprintf(buffer1, "*** (%s) ", myname);
						}
						else{
							sprintf(buffer1, "*** %s ", myname);
						}

						if(strcmp(src_clientname, "no name") == 0){
							sprintf(buffer2, "(#%d) just received from (%s) (#%d) by '%s' ***\n", myid, src_clientname, src_clientId, line_cpy);
						}
						else{
							sprintf(buffer2, "(#%d) just received from %s (#%d) by '%s' ***\n", myid, src_clientname, src_clientId, line_cpy);
						}

						sprintf(buffer, "%s%s", buffer1, buffer2);
						write(iter->second.fd, buffer, strlen(buffer));
					}
				}
				//free(myname);
				free(src_clientname);
			}

			//check pipe client request, if so , do broadcast
			if(pipeclient_flag == 1){
				char buffer[1000];
				char buffer1[500];
				char buffer2[500];
				for(iter = clientTable.begin(); iter != clientTable.end(); iter++){
					if(iter->second.fd != serversock){
						if(strcmp(myname, "no name") == 0){
							sprintf(buffer1, "*** (%s) ", myname);
						}
						else{
							sprintf(buffer1, "*** %s ", myname);
						}

						if(strcmp(dest_clientname, "no name") == 0){
							sprintf(buffer2, "(#%d) just piped '%s' to (%s) (#%d) ***\n", myid, line_cpy, clientTable[dest_clientfd].name, dest_clientId);
						}
						else{
							sprintf(buffer2, "(#%d) just piped '%s' to %s (#%d) ***\n", myid, line_cpy, clientTable[dest_clientfd].name, dest_clientId);
						}

						sprintf(buffer, "%s%s", buffer1, buffer2);
						write(iter->second.fd, buffer, strlen(buffer));
					}
				}
				//free(myname);
				free(dest_clientname);
			}

			

			//printf("cmdParent : wait\n");
			waitpid(pid, &status, WUNTRACED);
			printf("cmdParent : Child ended\n\n");
		}
	}

	printf("------execute cmd success------\n\n\n");
	return cmd_num;
}

int find_smallest_clientID(){
	int i, idx;
	for(i = 0; i < 30; i++){
		if(clientID_pool_arr[i] == 0){
			idx = i;
			clientID_pool_arr[i] = 1;
			break;
		}
	}
	return idx + 1;
}

int initClient(int clientsock, char *clientIP, int clientPORT){
	char initName[] = "no name";
	char initPathName[] = "PATH";
	char initPathValue[] = "bin:.";
	char initPATHValue[] = "bin:.";
	int id = find_smallest_clientID();

	idTable[id] = clientsock;
	clientTable[clientsock].id = id;
	clientTable[clientsock].fd = clientsock;
	clientTable[clientsock].name = strdup(initName);
	clientTable[clientsock].ip = strdup(clientIP);
	clientTable[clientsock].port = clientPORT;
	clientTable[clientsock].pathname = strdup(initPathName);
	clientTable[clientsock].pathvalue = strdup(initPathValue);
	clientTable[clientsock].PATHvalue = strdup(initPATHValue);
	clientTable[clientsock].pipenum_arr = (int*)malloc(sizeof(int)*50000);
	clientTable[clientsock].pipe_open_flag_arr = (int*)malloc(sizeof(int)*50000);
	clientTable[clientsock].cmd_counter = 0;
	//clientTable[clientsock].pipeCli_open_flag_arr = {0};

	memset(clientTable[clientsock].pipenum_arr, 0, sizeof(int)*50000);
	memset(clientTable[clientsock].pipe_open_flag_arr, 0, sizeof(int)*50000);
	memset(clientTable[clientsock].pipeCli_open_flag_arr, 0, sizeof(int)*30);

	return id;
}

void sendWelcomeMsg(int serversock, int clientsock){
	char Buffer[1000];
	char buffer[500];

	char tmp[] = "***";
	sprintf(buffer, "%s User '(no name)' entered from CGILAB/511. ***\n", tmp);
	//sprintf(buffer, "*** User '(no name)' entered from %s/%d. ***\n", clientTable[clientsock].ip, clientTable[clientsock].port);
	for(iter = clientTable.begin(); iter != clientTable.end(); iter++){
		if(iter->second.fd != serversock && iter->second.fd != clientsock){
			write(iter->second.fd, buffer, strlen(buffer));
		}
	}

	sprintf(Buffer, "%s%s", welcome, buffer);
	write(clientsock, Buffer, strlen(Buffer));
	write(clientsock, "% ", 2);
}

void deleteClient(int serversock, int fd){
	for(int i = 0; i < 30; i++){
		if(clientTable[fd].pipeCli_open_flag_arr[i] == 1){
			close(clientTable[fd].pipeClifd[i][0]);
			close(clientTable[fd].pipeClifd[i][1]);
		}
	}
	int idx = clientTable[fd].id - 1;
	clientID_pool_arr[idx] = 0;
	free(clientTable[fd].name);
	free(clientTable[fd].ip);
	free(clientTable[fd].pathname);
	free(clientTable[fd].pathvalue);
	free(clientTable[fd].PATHvalue);
	free(clientTable[fd].pipenum_arr);
	free(clientTable[fd].pipe_open_flag_arr);
	id_iter = idTable.find(clientTable[fd].id);
	idTable.erase(id_iter);
	iter = clientTable.find(fd);
	clientTable.erase(iter);
}

void sendLeavingMsg(int serversock, int clientsock){
	char buffer[500];
	for(iter = clientTable.begin(); iter != clientTable.end(); iter++){
		if(iter->second.fd != serversock){
			if(strcmp(clientTable[clientsock].name, "no name") == 0){
				sprintf(buffer, "*** User '(%s)' left. ***\n", clientTable[clientsock].name);
			}
			else{
				sprintf(buffer, "*** User '%s' left. ***\n", clientTable[clientsock].name);
			}
			
			write(iter->second.fd, buffer, strlen(buffer));
		}
	}
}

int main(int argc, char *argv[]){
	int port_num = atoi(argv[1]);
	int clilen, fd, n, i;
	char line[MAXLINE];
	struct sockaddr_in client_addr;
	fd_set rfds; /* read file descriptor set */
	fd_set afds; /* active file descriptor set */

	serversock = passiveTCP(port_num, QLEN);
	int nfds = getdtablesize();
	FD_ZERO(&afds);
	FD_SET(serversock, &afds);
	
	setenv("PATH", "bin:.", 1);
	chdir("ras"); //make sure server.o is outside the directory ras

	char ***token;
	token = (char***)malloc(sizeof(char**)*30000);
	for(i = 0; i < 30000; i++){
		token[i] = (char**)malloc(sizeof(char*)*1000);
	}

	while(1){
		memcpy(&rfds, &afds, sizeof(rfds));
		if(select(nfds, &rfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0) < 0){
			printf("select error\n");
		}

		if(FD_ISSET(serversock, &rfds)){
			clilen = sizeof(client_addr);
			int clientsock = accept(serversock, (struct sockaddr *)&client_addr, (socklen_t*)&clilen);
			if (clientsock < 0){
				printf("accept error\n");
			}

			char *clientIP = inet_ntoa(client_addr.sin_addr);
			int clientPORT = (int)ntohs(client_addr.sin_port);
			int myid = initClient(clientsock, clientIP, clientPORT);
			FD_SET(clientsock, &afds);
			sendWelcomeMsg(serversock, clientsock);

			printf("[Server] accept [%d]'s connection from %s/%d\n", myid, clientIP, clientPORT);
		}

		for(fd = 0; fd < nfds; ++fd){
			if (fd != serversock && FD_ISSET(fd, &rfds)){
				n = readline(fd, line, MAXLINE);
				//if(n == 0) return 0;
				if(n < 0) puts("readline error");
				
				strcpy(line_cpy,line);
				int cmd_num = parse_cmd(fd, line, token);
				int exec_cmd_num = execute_cmd(fd, token, cmd_num);

				if(exec_cmd_num < 0){
					//globalcmd_counter = 0; //reset globalcmd_counter to 0

					sendLeavingMsg(serversock, fd);
					deleteClient(serversock, fd);

					(void) close(fd);
					FD_CLR(fd, &afds);
					printf("------client exit success------\n");
					continue;
				}

				//add legal cmd_num to globalcmd_counter
				clientTable[fd].cmd_counter += exec_cmd_num;

				write(fd, "% ", 2);
			}
		}
	}

	/*for(i = 0; i < 30000; i++){
		free(token[i]);
	}
	free(token);*/
	//return 0;
}