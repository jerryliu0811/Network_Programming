#include <iostream>
#include <string>
#include <map>

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

using namespace std;

#define CLIENTTABLE_SHMKEY ((key_t) 7671) /* base value for shmclientTable key */
#define IDTABLE_SHMKEY ((key_t) 7672) /* base value for shmidTable key */
#define MSGBUFFER_SHMKEY ((key_t) 7673) /* base value for msgbuffer key */
#define PERMS 0666
#define QLEN 5
#define BUFSIZE 4096
#define MAXLINE 10000

int serversock;
int clientsock;
char *msgbuffer;
int sig_myid;
int shmclientTableid;
int shmidTableid;
int msgBufferid;
char line_cpy[MAXLINE];

char welcome[] = "\
****************************************\n\
** Welcome to the information server. **\n\
****************************************\n";

struct client{
	int id;
	int fd;
	int pid;
	char name[50];
	char ip[50];
	int port;
	// 0:init, 2:create_request, 1:pipe_exist
	int fifopipe_openflag[31]; 
	//[n][0]:read_end, [n][1]:write_end
	int fifopipe_fd[31][2]; 
};

client *shmclientTable;

int passiveTCP(int port_num, int qlen){
	int s = socket(AF_INET, SOCK_STREAM, 0);
	if(s < 0){
		printf("[Server]: create socket ERROR\n");
	}
	printf("[Server]: create socket success\n");

	struct sockaddr_in server_addr;
	bzero((char *)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port_num);
	
	if(bind(s, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
		printf("[Server]: can't bind to port: %d\n", port_num);
	}
	printf("[Server]: bind address success\n");

	if(listen(s, qlen) < 0){
		printf("[Server]: can't listen on port: %d\n", port_num);
	}
	printf("[Server]: wait for connection\n");

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

int parse_cmd(int cli_socketfd, char *str, char ***token, int cmd_counter, int *pipenum_arr){
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
			int idx = i + cmd_counter;
			
			//check if pipe number exists
			if(strlen(token[i][j]) > 1){ //condition like "|4"
				pipenum_arr[idx] = atoi(token[i][j]+1) + idx;
			}
			else{ //condition like "|"
				pipenum_arr[idx] = 1 + idx;
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
	int last_idx = i + cmd_counter;
	if(pipenum_arr[last_idx] == 0){
		pipenum_arr[last_idx] = -1;
	}
	
	//make i + 1 to cmd_num
	i += 1;

	//print token
	/*int k,l;
	for(k = 0; k < i; k++){
		for(l = 0; token[k][l]; l++){
			printf("token[%d][%d]:[%s]\n", k, l, token[k][l]);
		}
		printf("cmd[%d] -> [%d]\n", k+cmd_counter, pipenum_arr[k+cmd_counter]);
	}
	printf("\n------parse success------\n");*/
	
	return i;
}

int check_cmd_exist(char *cmd){
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

int execute_cmd(int myid, int cli_socketfd, char ***token, int cmd_num, int *shmidTable, client* shmclientTable, int cmd_counter, int *pipenum_arr, int *pipe_open_flag_arr, int **pipefd){
	pid_t pid;
	int i, status; //create pipe array

	for(i = 0; i < cmd_num; i++){

		printf("Processing cli[%d] cmd:[%s]\n", shmclientTable[myid].pid, token[i][0]);

		//top priority for special cmds checking like: exit, setenv, printenv
		if(strcmp(token[i][0],"exit") == 0){
			return -1;
		}
		else if(strcmp(token[i][0],"setenv") == 0){
			setenv(token[i][1], token[i][2], 1);
			break;
		}
		else if(strcmp(token[i][0],"printenv") == 0){
			char *path = getenv(token[i][1]);
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
			for(j = 1; j < 31; j++){
				if(shmidTable[j] == 1){
					if(j == myid){
						if(strcmp(shmclientTable[j].name, "no name") == 0){
							sprintf(row, "%d\t(%s)\tCGILAB/511\t<-me\n", shmclientTable[j].id, shmclientTable[j].name);
							//sprintf(row, "%d\t(%s)\t%s/%d\t<-me\n", shmclientTable[j].id, shmclientTable[j].name, shmclientTable[j].ip, shmclientTable[j].port);
							strcat(buffer, row);
						}
						else{
							sprintf(row, "%d\t%s\tCGILAB/511\t<-me\n", shmclientTable[j].id, shmclientTable[j].name);
							//sprintf(row, "%d\t%s\t%s/%d\t<-me\n", shmclientTable[j].id, shmclientTable[j].name, shmclientTable[j].ip, shmclientTable[j].port);
							strcat(buffer, row);
						}
					}
					else{
						if(strcmp(shmclientTable[j].name, "no name") == 0){
							sprintf(row, "%d\t(%s)\tCGILAB/511\n", shmclientTable[j].id, shmclientTable[j].name);
							//sprintf(row, "%d\t(%s)\t%s/%d\n", shmclientTable[j].id, shmclientTable[j].name, shmclientTable[j].ip, shmclientTable[j].port);
							strcat(buffer, row);
						}
						else{
							sprintf(row, "%d\t%s\tCGILAB/511\n", shmclientTable[j].id, shmclientTable[j].name);
							//sprintf(row, "%d\t%s\t%s/%d\n", shmclientTable[j].id, shmclientTable[j].name, shmclientTable[j].ip, shmclientTable[j].port);
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
			if(shmidTable[targetCliId] == 1){
				sprintf(buffer, "*** %s told you ***: %s\n", shmclientTable[myid].name, token[i][2]);
				strcpy(msgbuffer, buffer);
				kill(shmclientTable[targetCliId].pid, SIGUSR1);
			}
			else{
				sprintf(buffer, "*** Error: user #%d does not exist yet. ***\n", targetCliId);
				write(cli_socketfd, buffer, strlen(buffer));
			}
			break;
		}
		else if(strcmp(token[i][0],"yell") == 0){
			int j;
			char buffer[500];
			sprintf(buffer, "*** %s yelled ***: %s\n", shmclientTable[myid].name, token[i][1]);
			strcpy(msgbuffer, buffer);
			for(j = 1; j < 31; j++){
				if(shmidTable[j] == 1){
					kill(shmclientTable[j].pid, SIGUSR1);
				}
			}
			break;
		}
		else if(strcmp(token[i][0],"name") == 0){
			int j, name_existed_flag = 0;
			for(j = 1; j < 31; j++){
				if(shmidTable[j] == 1){
					if(strcmp(shmclientTable[j].name, token[i][1]) == 0){
						name_existed_flag = 1;
						break;
					}
				}
			}

			char buffer[500];
			if(name_existed_flag == 0){
				strcpy(shmclientTable[myid].name, token[i][1]);
				sprintf(buffer, "*** User from CGILAB/511 is named '%s'. ***\n", shmclientTable[myid].name);
				//sprintf(buffer, "*** User from %s/%d is named '%s'. ***\n", shmclientTable[myid].ip, shmclientTable[myid].port, shmclientTable[myid].name);
				strcpy(msgbuffer, buffer);
				
				for(j = 1; j < 31; j++){
					if(shmidTable[j] == 1){
						kill(shmclientTable[j].pid, SIGUSR1);
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
		int idx = i + cmd_counter;
		int pipe_out_idx = pipenum_arr[idx];
		//printf("global index is [%d]\n", idx);

		//check normal cmds exist or not in now env.
		if(check_cmd_exist(token[i][0]) == 0){
			int s;
			char GGbuffer[500];
			//set all cmds after this unknown command that their pipe_out_idx to 0
			for(s = i; s < cmd_num; s++)
				pipenum_arr[s+cmd_counter] = 0;

			sprintf(GGbuffer, "Unknown command: [%s].\n", token[i][0]);
			write(cli_socketfd, GGbuffer, strlen(GGbuffer));

			printf("pipe out to [%d]\n", pipenum_arr[idx]);
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
				dest_clientId = atoi(token[i][j]+1);
				token[i][j] = NULL;
				//printf("find PIPE TO CLIENT request to client[%d]\n", dest_clientId);
				//break;
			}
			else if(token[i][j][0] == '<'){ // check '<num'
				read_pipeclient_flag = 1;
				src_clientId = atoi(token[i][j]+1);
				token[i][j] = NULL;
				//printf("find READ FROM CLIENT request from client[%d]\n", src_clientId);
				//break;
			}
		}

		char *myname;
		char *dest_clientname;
		char *src_clientname;
		char fifopipe_file[100];

		//check pipe client request
		if(pipeclient_flag){
			//check dest. client exist or not
			if(shmidTable[dest_clientId] == 0){ // dest. client not find
				char buffer[500];
				sprintf(buffer, "*** Error: user #%d does not exist yet. ***\n", dest_clientId);
				write(cli_socketfd, buffer, strlen(buffer));
				break;
			}

			myname = strdup(shmclientTable[myid].name);
			dest_clientname = strdup(shmclientTable[dest_clientId].name);
			
			//check dest. client pipe opened or not
			if(shmclientTable[dest_clientId].fifopipe_openflag[myid] == 1){ //dest. client pipe has already open
				char buffer[500];
				sprintf(buffer, "*** Error: the pipe #%d->#%d already exists. ***\n", myid, dest_clientId);
				write(cli_socketfd, buffer, strlen(buffer));
				break;
			}
			else{ //dest. client pipe not open
				//set array to create_request
				shmclientTable[dest_clientId].fifopipe_openflag[myid] = 2;
				sprintf(fifopipe_file, "../tmp/fifopipe%dto%d", myid, dest_clientId);
				//send create pipe signal
				kill(shmclientTable[dest_clientId].pid, SIGUSR2);
				printf("client out pipe create on cliID[%d]\n",dest_clientId);
			}
		}
		
		//check read client pipe request
		if(read_pipeclient_flag){
			//check src. client exist or not
			char buffer[500];
			myname = strdup(shmclientTable[myid].name);
			
			if(shmidTable[src_clientId] == 0){ // src. client not find
				sprintf(buffer, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", src_clientId, myid);
				write(cli_socketfd, buffer, strlen(buffer));
				break;
			}
			
			src_clientname = strdup(shmclientTable[src_clientId].name);

			//check my pipe[src_client_id] opened or not
			if(shmclientTable[myid].fifopipe_openflag[src_clientId] == 0){ //src. client pipe not open
				sprintf(buffer, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", src_clientId, myid);
				write(cli_socketfd, buffer, strlen(buffer));
				break;
			}

			//write receive msg
			if(strcmp(src_clientname, "no name") == 0){
				sprintf(buffer, "*** %s (#%d) just received from (%s) (#%d) by '%s' ***\n", myname, myid, src_clientname, src_clientId, line_cpy);
			}
			else{
				sprintf(buffer, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n", myname, myid, src_clientname, src_clientId, line_cpy);
			}
			write(cli_socketfd, buffer, strlen(buffer));
		}

		//printf("pipe out to [%d]\n", pipe_out_idx);
		//check dest. pipe opened or not
		if(pipe_out_idx == -1){
			//no need to create dest. pipe, just do nothing
		}
		else if(pipe_open_flag_arr[pipe_out_idx] == 0){
			//create pipe
			pipefd[pipe_out_idx] = (int*)malloc(sizeof(int)*2);
			if(pipe(pipefd[pipe_out_idx]) < 0) perror("ERROR:create dest. pipe error\n");
			pipe_open_flag_arr[pipe_out_idx] = 1;
			printf("out pipe create : %d %d\n", pipefd[pipe_out_idx][0], pipefd[pipe_out_idx][1]);
		}

		//fork for exec() 
		if((pid = fork()) < 0) {printf("fork error\n");exit(1);}
		else if(pid == 0){ // for the cmd child process: 
			//check client input pipe
			if(read_pipeclient_flag){
				int fifopipe_readend_fd = shmclientTable[myid].fifopipe_fd[src_clientId][0];
				printf("cmdChild : set client input pipe [%d]\n", fifopipe_readend_fd);
				dup2(fifopipe_readend_fd, STDIN_FILENO); // read data from this pipe read_end
			}
			//check input pipe has opended
			if(pipe_open_flag_arr[idx] == 1){
				printf("cmdChild : set input pipe [%d][%d]\n", pipefd[idx][0], pipefd[idx][1]);
				close(pipefd[idx][1]); //close this pipe write_end
				dup2(pipefd[idx][0], STDIN_FILENO); // read data from this pipe read_end
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
				int fifopipe_writeend_fd = open(fifopipe_file, O_WRONLY);
				shmclientTable[dest_clientId].fifopipe_fd[myid][1] = fifopipe_writeend_fd;
				printf("cmdChild : set output client pipe write end[%d]\n",fifopipe_writeend_fd);
				dup2(fifopipe_writeend_fd, STDERR_FILENO);
				dup2(fifopipe_writeend_fd, STDOUT_FILENO);
			}
			else if(pipe_out_idx == -1){ //write output to client
				printf("cmdChild : set output socket\n");
				dup2(cli_socketfd, STDERR_FILENO); // send stderr to clisocketfd
				dup2(cli_socketfd, STDOUT_FILENO); // send stdout to clisocketfd
			}
			else{ //write output to dest. pipe
				printf("cmdChild : set output pipe [%d][%d]\n", pipefd[pipe_out_idx][0], pipefd[pipe_out_idx][1]);
				close(pipefd[pipe_out_idx][0]); //close dest. pipe read_end
				dup2(cli_socketfd, STDERR_FILENO); // send stderr to clisocketfd
				dup2(pipefd[pipe_out_idx][1], STDOUT_FILENO); // send stdout to the dest. pipe write_end
			}

			// exec this cmd
			if(execvp(token[i][0], token[i]) < 0) puts("cmdChild : exec failed");
			exit(1);
		}
		else{ //for the cmd parent process:
			//check input pipe has opended
			if(pipe_open_flag_arr[idx] == 1){
				//no longer needs this pipe, close it
				close(pipefd[idx][1]);
				close(pipefd[idx][0]);
				free(pipefd[idx]);
			}

			//check read client pipe request, if so, clear pipe
			if(read_pipeclient_flag == 1){
				close(shmclientTable[myid].fifopipe_fd[src_clientId][0]);
				shmclientTable[myid].fifopipe_openflag[src_clientId] = 0;

				char fifopipe_file[100];
				sprintf(fifopipe_file, "../tmp/fifopipe%dto%d", src_clientId, myid);
				unlink(fifopipe_file);

				int j;
				char buffer[500];
				for(j = 1; j < 31; j++){
					if(shmidTable[j] == 1 && j != myid){
						if(strcmp(src_clientname, "no name") == 0){
							sprintf(buffer, "*** %s (#%d) just received from (%s) (#%d) by '%s' ***\n", myname, myid, src_clientname, src_clientId, line_cpy);
						}
						else{
							sprintf(buffer, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n", myname, myid, src_clientname, src_clientId, line_cpy);
						}
						strcpy(msgbuffer, buffer);
						kill(shmclientTable[j].pid, SIGUSR1);
					}
				}
				free(myname);
				free(src_clientname);
			}

			//check pipe client request, if so , do broadcast
			if(pipeclient_flag == 1){
				int j;
				char buffer[500];
				for(j = 1; j < 31; j++){
					if(shmidTable[j] == 1){
						if(strcmp(dest_clientname, "no name") == 0){
							sprintf(buffer, "*** %s (#%d) just piped '%s' to (%s) (#%d) ***\n", myname, myid, line_cpy, dest_clientname, dest_clientId);
						}
						else{
							sprintf(buffer, "*** %s (#%d) just piped '%s' to %s (#%d) ***\n", myname, myid, line_cpy, dest_clientname, dest_clientId);
						}
						strcpy(msgbuffer, buffer);
						kill(shmclientTable[j].pid, SIGUSR1);
					}
				}
				free(myname);
				free(dest_clientname);
				close(shmclientTable[dest_clientId].fifopipe_fd[myid][1]);
			}

			//printf("cmdParent : wait\n");
			waitpid(pid, &status, WUNTRACED);
			printf("cmdParent : Child ended\n\n");
		}
	}

	printf("------execute cmd success------\n\n\n");
	return cmd_num;
}

int* getshmidTableAddr(){
	//get shmidTable
	shmidTableid = shmget(IDTABLE_SHMKEY, sizeof(int)*31, PERMS);
	if(shmidTableid < 0){
		printf("[Child]: shmget shmidTableid ERROR\n");
	}

	//attach shmidTable address
	int *shmidTable = (int*)shmat(shmidTableid, (char *)0, 0);
	if(shmidTable == (int *)-1){
		printf("[Child]: at shmidTable ERROR\n");
	}

	return shmidTable;
}

int find_smallest_clientID(int *shmidTable){
	int i, idx;
	for(i = 1; i < 31; i++){
		if(shmidTable[i] == 0){
			idx = i;
			shmidTable[i] = 1;
			break;
		}
	}

	return idx;
}

void createidTble(){
	int i;

	//create shmidTable for clientid management
	shmidTableid = shmget(IDTABLE_SHMKEY, sizeof(int)*31, PERMS|IPC_CREAT);
	if(shmidTableid < 0){
		printf("[Server]: shmget shmidTableid ERROR\n");
	}

	//attach shmidTable address
	int *shmidTable = (int*)shmat(shmidTableid, (char *)0, 0);
	if(shmidTable == (int *)-1){
		printf("child:at shmidTable error\n");
	}
	
	//initial to 0 represent that all clientID unused
	for(i = 0; i < 31; i++){
		shmidTable[i] = 0;
	}
	
	//detach shmidTable address
	if(shmdt(shmidTable) != 0){
		printf("[Server]: dt shmidTableid ERROR\n");
	}
}

void initClient(client *shmclientTable, int id, int clientsock, char *clientIP, int clientPORT){
	int i;
	char initName[] = "no name";

	shmclientTable[id].id = id;
	shmclientTable[id].fd = clientsock;
	shmclientTable[id].pid = getpid();
	strcpy(shmclientTable[id].name, initName);
	strcpy(shmclientTable[id].ip, clientIP);
	shmclientTable[id].port = clientPORT;

	for(i = 1; i < 31; i++){
		shmclientTable[id].fifopipe_openflag[i] = 0;
	}
}

void sendWelcomeMsg(client *shmclientTable, int *shmidTable, int id, int clientsock){
	int i;
	char Buffer[1000];
	char buffer[500];

	char tmp[] = "***";
	sprintf(buffer, "%s User '(no name)' entered from CGILAB/511. ***\n", tmp);
	//sprintf(buffer, "*** User '(no name)' entered from %s/%d. ***\n", shmclientTable[id].ip, clientTable[id].port);
	
	strcpy(msgbuffer, buffer);
	for(i = 1; i < 31; i++){
		if(shmidTable[i] == 1 && i != id){
			kill(shmclientTable[i].pid, SIGUSR1);
		}
	}

	sprintf(Buffer, "%s%s", welcome, buffer);
	write(clientsock, Buffer, strlen(Buffer));
}

void sendLeavingMsg(int id, int *shmidTable, client *shmclientTable){
	char *name = shmclientTable[id].name;
	char buffer[500];
	int j;
	for(j = 1; j < 31; j++){
		if(shmidTable[j] == 1){
			if(strcmp(name, "no name") == 0){
				sprintf(buffer, "*** User '(%s)' left. ***\n", name);
			}
			else{
				sprintf(buffer, "*** User '%s' left. ***\n", name);
			}
			strcpy(msgbuffer, buffer);
			kill(shmclientTable[j].pid, SIGUSR1);
		}
	}
}

void deleteClient(int id, int *shmidTable){
	int i;
	char fifopipe_file[100];
	for(i = 1; i < 31; i++){
		if(shmclientTable[id].fifopipe_openflag[i] == 1){
			close(shmclientTable[id].fifopipe_fd[i][0]);
			sprintf(fifopipe_file, "../tmp/fifopipe%dto%d", i, id);
			unlink(fifopipe_file);
		}
	}

	shmidTable[id] = 0;
}

void sig_handler(int signo){
	pid_t pid;
	int status, i;

	//for server close, need to release shared memory
	if(signo == SIGINT){
		if(shmctl(shmclientTableid, IPC_RMID, NULL) == -1){
			printf("[Server]: shmctl shmclientTableid ERROR\n");
		}
		if(shmctl(shmidTableid, IPC_RMID, NULL) == -1){
			printf("[Server]: shmctl shmidTableid ERROR\n");
		}
		if(shmctl(msgBufferid, IPC_RMID, NULL) == -1){
			printf("[Server]: shmctl msgBufferid ERROR\n");
		}
		printf("[Server]: release all shared memory success\n");
		exit(0);
	}
	//for waiting child process
	else if(signo == SIGCHLD){
		waitpid(0,&status,WNOHANG);
	}
	//for broadcast or tell 
	else if(signo == SIGUSR1){ //read msgbuffer and show it on screen
		write(clientsock, msgbuffer, strlen(msgbuffer));
	}
	//for FIFO pipe
	else if(signo == SIGUSR2){ //create client FIFOpipe and open read end
		for(i = 1; i < 31; i++){
			if(shmclientTable[sig_myid].fifopipe_openflag[i] == 2){
				char fifopipe_file[100];
				sprintf(fifopipe_file, "../tmp/fifopipe%dto%d", i, sig_myid);
				if(mkfifo(fifopipe_file, PERMS) < 0){ //create client FIFOpipe
					printf("[Child]: mkfifo ERROR\n");
				}
				//open FIFOpipe read end for non blocking
				int fifopipe_readend_fd = open(fifopipe_file, O_RDONLY);
				shmclientTable[sig_myid].fifopipe_fd[i][0] = fifopipe_readend_fd;
				shmclientTable[sig_myid].fifopipe_openflag[i] = 1;
				break;
			}
		}
	}
}

int main(int argc, char *argv[]){
	int port_num = atoi(argv[1]);
	int clilen, n, i, childpid;
	char line[MAXLINE];
	struct sockaddr_in client_addr;

	serversock = passiveTCP(port_num, QLEN);

	setenv("PATH", "bin:.", 1);
	chdir("ras"); //make sure server.o is outside the directory ras

	char ***token;
	token = (char***)malloc(sizeof(char**)*30000);
	for(i = 0; i < 30000; i++){
		token[i] = (char**)malloc(sizeof(char*)*1000);
	}

	//create shmclientTable for client communication
	shmclientTableid = shmget(CLIENTTABLE_SHMKEY, sizeof(client)*31, PERMS|IPC_CREAT);
	if(shmclientTableid < 0){
		printf("[Server]: shmget shmclientTableid ERROR\n");
	}
	//create and init idTable for clientID management
	createidTble();
	//create msgBuffer for receiving other clients msg
	msgBufferid = shmget(MSGBUFFER_SHMKEY, sizeof(char)*2000, PERMS|IPC_CREAT);
	if(msgBufferid < 0){
		printf("[Server]: shmget msgBufferid ERROR\n");
	}

	//wait for child process for preventing zombie process
	(void) signal(SIGCHLD, sig_handler);

	//delete shared memory
	(void) signal(SIGINT, sig_handler);

	//create signal handler for other client's kill
	(void) signal(SIGUSR1, sig_handler);
	(void) signal(SIGUSR2, sig_handler);

	while(1){
		clilen = sizeof(client_addr);
		clientsock = accept(serversock, (struct sockaddr *)&client_addr, (socklen_t*)&clilen);
		if (clientsock < 0){
			printf("[Server]: accept ERROR\n");
		}
		char *clientIP = inet_ntoa(client_addr.sin_addr);
		int clientPORT = (int)ntohs(client_addr.sin_port);
		printf("[Server] accept [%d]'s connection from %s/%d\n", clientsock, clientIP, clientPORT);

		int status;
		childpid = fork();
		if(childpid < 0){
			printf("[Server]: fork ERROR\n");
		}
		else if(childpid == 0){ /* child */
			(void) close(serversock);
			/* do something here*/
			
			//attach shmclientTable address
			shmclientTable = (client*)shmat(shmclientTableid, (char *)0, 0);
			if(shmclientTable == (client *)-1){
				printf("[Child]: at shmclientTable error\n");
			}
			//attach shmidTable address
			int *shmidTable = getshmidTableAddr();
			//attach msgBuffer address
			msgbuffer = (char*)shmat(msgBufferid, (char *)0, 0);

			//init client in shmclientTable
			int id = find_smallest_clientID(shmidTable);
			sig_myid = id;
			initClient(shmclientTable, id, clientsock, clientIP, clientPORT);
			//broadcast WelcomeMsg
			sendWelcomeMsg(shmclientTable, shmidTable, id, clientsock);

			//create/malloc local pipe-related array and variables
			int cmd_counter = 0;
			int **pipefd = (int**)malloc(sizeof(int*)*50000);
			int *pipenum_arr = (int*)malloc(sizeof(int)*50000);
			int *pipe_open_flag_arr = (int*)malloc(sizeof(int)*50000);
			memset(pipenum_arr, 0, sizeof(int)*50000);
			memset(pipe_open_flag_arr, 0, sizeof(int)*50000);

			for(;;){
				//show prompt
				write(clientsock, "% ", 2);

				//read input
				n = readline(clientsock, line, MAXLINE);
				if(n == 0) return 0;
				else if(n < 0) puts("readline error");

				//parse input
				strcpy(line_cpy,line);
				int cmd_num = parse_cmd(clientsock, line, token, cmd_counter, pipenum_arr);
				
				//execute command
				int exec_cmd_num = execute_cmd(id, clientsock, token, cmd_num, shmidTable, shmclientTable, cmd_counter, pipenum_arr, pipe_open_flag_arr, pipefd);

				//client exit request
				if(exec_cmd_num < 0){
					//reset globalcmd_counter to 0
					cmd_counter = 0;
					//broadcast LeavingMsg
					sendLeavingMsg(id, shmidTable, shmclientTable);
					//reset client
					deleteClient(id, shmidTable);
					
					printf("------client exit success------\n");
					break;
				}

				//add legal cmd_num to globalcmd_counter
				cmd_counter += exec_cmd_num;
			}

			//detach shmidTable address
			if(shmdt(shmidTable) != 0){
				printf("[Child]: dt shmidTable ERROR\n");
			}
			//detach shmclientTable address
			if(shmdt(shmclientTable) != 0){
				printf("[Child]: dt shmclientTable ERROR\n");
			}
			//detach msgBuffer address
			if(shmdt(msgbuffer) != 0){
				printf("[Child]: dt msgbuffer ERROR\n");
			}

			//free token & pipenum array
			for(i = 0; i < 30000; i++){
				free(token[i]);
			}
			free(token);
			free(pipenum_arr);
			free(pipefd);

			//close client proccess
			exit(0);
		}
		else{ /* parent */
			(void) close(clientsock);
			//waitpid(childpid, &status, WNOHANG);
			waitpid(-1, NULL, WNOHANG);
		}
	}
	//return 0;
}