#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>

#define MAXLINE 10000
int globalcmd_counter = 0;
char welcome[] = "\
****************************************\n\
** Welcome to the information server. **\n\
****************************************\n";

int check_cmd_exist(char *cmd);
int readline(int fd, char *ptr, int maxlen);
int parse_cmd(char *str, char ***token, int *pipenum_arr);
int execute_cmd(int cli_socketfd, char ***token, int *pipenum_arr, int cmd_num);

int main(int argc ,char *argv[]){
	int socketfd, cli_socketfd;
	int clilen;
	int childpid;
	struct sockaddr_in serv_addr, cli_addr;
	
	//Create TCP socket
	socketfd = socket(AF_INET, SOCK_STREAM, 0);
	if(socketfd == -1) puts("Server : Could not create Internet stream socket");
	printf("Server: create socket\n");
	
	//Prepare the sockaddr_in structure
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(atoi(argv[1]));
	
	//Bind
	if(bind(socketfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
		puts("Server : Could not bind local address");
		return 1;
	}
	printf("Server: bind address\n");

	//Listen
	listen(socketfd, 5);
	printf("Server: listen\n");

	for(;;){
		//Accept connection from an incoming client
		clilen = sizeof(cli_addr);
		cli_socketfd = accept(socketfd, (struct sockaddr *)&cli_addr, (socklen_t*)&clilen);
		if (cli_socketfd < 0){
			puts("Server : Accept failed");
			return 1;
		}
		printf("Server: accept connection\n");
		
		//for every client fork one proc
		int status;
		if((childpid = fork()) < 0) puts("Server: Fork error");
		else if(childpid == 0){ // child process
			// close original socket
			close(socketfd);

			/* process the client request */
			
			// do basic settings
			setenv("PATH", "bin:.", 1);
			chdir("ras"); //make sure server.o is outside the directory ras

			char line[MAXLINE];
			write(cli_socketfd, welcome, strlen(welcome)); //show welcome msg
			
			// malloc token array for reading client's input
			char ***token;
			int i;
			token = malloc(sizeof(char**)*30000);
			for(i = 0; i < 30000; i++){
				token[i] = malloc(sizeof(char*)*1000);
			}

			// malloc pipenum_arr for recoding cmd's pipe destination 
			int *pipenum_arr = malloc(sizeof(int)*50000);
			memset(pipenum_arr, 0, sizeof(int)*50000); //initialize to 0

			for(;;){
				//show prompt
				write(cli_socketfd, "% ", 2);
				
				//read client's input 
				int n = readline(cli_socketfd, line, MAXLINE);
				if(n == 0) return 0; /* connection terminated */
				else if(n < 0) puts("readline error");

				//parse recv line to 2-D array
				int cmd_num = parse_cmd(line, token, pipenum_arr);

				//execute cmd
				int exec_cmd_num = execute_cmd(cli_socketfd, token, pipenum_arr, cmd_num);

				//check 'exit'
				if(exec_cmd_num < 0){
					globalcmd_counter = 0; //reset globalcmd_counter to 0
					printf("------client exit success------\n");
					break;
				}

				//add legal cmd_num to globalcmd_counter
				globalcmd_counter += exec_cmd_num;
			}

			//free token & pipenum array
			for(i = 0; i < 30000; i++){
				free(token[i]);
			}
			free(token);
			free(pipenum_arr);

			//close client proccess
			exit(0);
		}
		else{ // parent process
			//close client socket, finish this connection
			close(cli_socketfd); 
			waitpid(childpid, &status, WUNTRACED);
		}
	}
	return 0;
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

int parse_cmd(char *str, char ***token, int *pipenum_arr){
	//remove '\n' in str
	if(strlen(str) > 1 && str[strlen(str)-1] == '\n') str[strlen(str)-1] = '\0';
	
	//according recv input, cutting them by space and '|' then save to token array
	//also set cmd's pipe destination index
	int i = 0, j = 0;
	token[i][j] = strtok(str," ");
	while(token[i][j] != NULL){
		//check pipe
		if(token[i][j][0] == '|'){
			//shift this input token's number to the global index
			int idx = i + globalcmd_counter;
			
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
	int last_idx = i + globalcmd_counter;
	if(pipenum_arr[last_idx] == 0) pipenum_arr[last_idx] = -1;
	
	//make i + 1 to cmd_num
	i += 1;

	//print token
	int k,l;
	for(k = 0; k < i; k++){
		for(l = 0; token[k][l]; l++){
			printf("token[%d][%d]:%s\n", k, l, token[k][l]);
		}
		printf("cmd[%d] -> [%d]\n", k+globalcmd_counter, pipenum_arr[k+globalcmd_counter]);
	}
	printf("\n------parse success------\n");
	
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
		if(access(filepath, F_OK) != -1) return 1;

		tmppath = strtok(NULL, ":");
	}

	return 0;
}

int execute_cmd(int cli_socketfd, char ***token, int *pipenum_arr, int cmd_num){
	pid_t pid;
	int i, status, *pipefd[50000]; //create pipe array
	static int pipe_open_flag_arr[50000] = {0}; //this array records that pipe is open or not

	for(i = 0; i < cmd_num; i++){

		printf("Processing cmd:[%s]\n", token[i][0]);

		//top priority for special cmds checking like: exit, setenv, printenv
		if(strcmp(token[i][0],"exit") == 0){
			return -1;
		}
		else if(strcmp(token[i][0],"setenv") == 0){
			setenv(token[i][1], token[i][2], 1);
			continue;
		}
		else if(strcmp(token[i][0],"printenv") == 0){
			char *path = getenv(token[i][1]);
			char buffer[500];
			sprintf(buffer, "%s=%s\n", token[i][1], path);
			write(cli_socketfd, buffer, strlen(buffer));
			continue;
		}

		//set index to global counter for this cmd
		int idx = i + globalcmd_counter;
		int pipe_out_idx = pipenum_arr[idx];
		printf("global index is [%d]\n", idx);

		//check normal cmds exist or not in now env.
		if(check_cmd_exist(token[i][0]) == 0){
			char GGbuffer[500];
			int s;
			//set all cmds after this unknown command that their pipe_out_idx to 0
			for(s = i; s < cmd_num; s++)
				pipenum_arr[s+globalcmd_counter] = 0;
			sprintf(GGbuffer, "Unknown command: [%s].\n", token[i][0]);
			write(cli_socketfd, GGbuffer, strlen(GGbuffer));
			printf("pipe out to [%d]\n", pipenum_arr[idx]);
			printf("unknown command\n");
			printf("------execute cmd success------\n");
			if(cmd_num == 1) return 1; //special case for unknown cmd show alone
			return i;
		}

		//check cmd '>' for write file request
		int writefile_idx, writefile_flag = 0;
		for(writefile_idx = 0; token[i][writefile_idx]; writefile_idx++){
			if(strcmp(token[i][writefile_idx], ">") == 0){
				if(token[i][writefile_idx+1]) writefile_flag = 1;
				token[i][writefile_idx] = NULL;
				writefile_idx += 1;
				break;
			}
		}

		printf("pipe out to [%d]\n", pipe_out_idx);
		//check dest. pipe opened or not
		if(pipe_out_idx == -1){
			//no need to create dest. pipe, just do nothing
		}
		else if(pipe_open_flag_arr[pipe_out_idx] == 0){
			//create pipe
			pipefd[pipe_out_idx] = malloc(sizeof(int)*2);
			if(pipe(pipefd[pipe_out_idx]) < 0) perror("ERROR:create dest. pipe error\n");
			pipe_open_flag_arr[pipe_out_idx] = 1;
			printf("out pipe create : %d %d\n", pipefd[pipe_out_idx][0], pipefd[pipe_out_idx][1]);
		}

		//fork for exec() 
		if((pid = fork()) < 0) {printf("fork error\n");exit(1);}
		else if(pid == 0){ // for the cmd child process: 
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
			}
			
			//printf("cmdParent : wait\n");
			waitpid(pid, &status, WUNTRACED);
			printf("cmdParent : Child ended\n\n");
		}
	}

	printf("------execute cmd success------\n\n\n");
	return cmd_num;
}