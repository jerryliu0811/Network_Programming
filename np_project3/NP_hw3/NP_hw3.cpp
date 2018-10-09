#include <windows.h>
#include <list>
#include "resource.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
using namespace std;

#define SERVER_PORT 7788
#define WM_SOCKET_NOTIFY (WM_USER + 1)
#define WM_CGI_NOTIFY (WM_USER + 2)
#define F_CONNECTING 0
#define F_READING 1
#define F_WRITING 2
#define F_DONE 3
#define BUFFERSIZE 20001

BOOL CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
int EditPrintf(HWND, TCHAR *, ...);
//=================================================================
//	Global Variables
//=================================================================
list<SOCKET> Socks;
static HWND hwndEdit;
char *argvs, *files;
char buffer[BUFFERSIZE - 1], ip[6][50], port[6][50], file[6][50];
int id_clifd_arr[6], id_status_arr[6], browser_fd, client_num;
int exit_flag[6];
FILE *id_filefd_arr[6];

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	return DialogBox(hInstance, MAKEINTRESOURCE(ID_MAIN), NULL, MainDlgProc);
}

int parse_query_string(char *query, char ip[6][50], char port[6][50], char file[6][50]) {
	//h1=GG&p1=ININ&f1=der&h2=&p2=&f2=&h3=&p3=&f3=&h4=&p4=&f4=&h5=&p5=&f5=
	char *str;
	int clientID = 1, i = 1;

	int client_count = 0;
	str = strtok(query, "&");
	while (str != NULL) {
		//EditPrintf(hwndEdit, TEXT("[%s]\r\n"), str);
		if (i > 3) {
			if (strcmp(ip[clientID], "") != 0 && strcmp(port[clientID], "") != 0 && strcmp(file[clientID], "") != 0) {
				client_count = client_count + 1;
			}
			i = 1;
			clientID = clientID + 1;
		}
		switch (i) {
			case 1:
				strcpy(ip[clientID], str + 3);
				break;
			case 2:
				strcpy(port[clientID], str + 3);
				break;
			case 3:
				strcpy(file[clientID], str + 3);
				break;
		}
		i = i + 1;
		str = strtok(NULL, "&");
	}

	if (strcmp(ip[clientID], "") != 0 && strcmp(port[clientID], "") != 0 && strcmp(file[clientID], "") != 0) {
		client_count = client_count + 1;
	}

	/*for (i = 1; i <= 5; i++) {
		EditPrintf(hwndEdit, TEXT("client[%d]<br>\r\n"), i);
		EditPrintf(hwndEdit, TEXT("-%s-<br>\r\n"), ip[i]);
		EditPrintf(hwndEdit, TEXT("-%s-<br>\r\n"), port[i]);
		EditPrintf(hwndEdit, TEXT("-%s-<br>\r\n"), file[i]);
	}
	EditPrintf(hwndEdit, TEXT("client_num -%d-<br>\r\n"), client_count);*/

	return client_count;
}

int read_http_requrst(int clientsock, char *buffer) {
	int n, i;

	memset(buffer, 0, sizeof(buffer));
	n = recv(clientsock, buffer, BUFFERSIZE, 0);

	if (n < 0) {
		return n;
	}

	if (n > 0 && n < BUFFERSIZE)
		buffer[n] = 0;
	else
		buffer[0] = 0;

	for (i = 0; i < n; i++) {
		if (buffer[i] == '\n' || buffer[i] == '\r') {
			buffer[i] = 0;
		}
	}
	return n;
}

int parse_http_request(char *buffer) {
	//GET /form_get.htm?h1=GG&p1=ININ&f1=der&h2=&p2=&f2= HTTP/1.1
	//GET /form_get.htm HTTP/1.1
	char *tmp, *tmp2;
	int i = 0;

	tmp = strtok(buffer, " ");
	while (tmp != NULL) {
		if (i == 1) break;
		i++;
		tmp = strtok(NULL, " ");
	}

	i = 0;
	tmp2 = strtok(tmp, "?");
	while (tmp2 != NULL) {
		if (i == 0) files = strdup(++tmp2);
		if (i == 1) argvs = strdup(tmp2);
		i++;
		tmp2 = strtok(NULL, "?");
	}

	if ((i - 1) == 0) {
		if (strcmp(files, "form_get.htm") != 0)
			return 0;

		EditPrintf(hwndEdit, TEXT("suppose form_get: %s\r\n"), files);
		return 1;
	}
	else {
		if (strcmp(files, "hw3.cgi") != 0)
			return 0;
		EditPrintf(hwndEdit, TEXT("suppose hw3.cgi %s\r\n%s\r\n"), files, argvs);
		return 2;
	}

	return 0;
}

void init_html(char ip[6][50], int clientsocket) {
	char buf[5000];
	sprintf(buf, "Content-type: text/html\n\n\
			<html>\
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
	//EditPrintf(hwndEdit, TEXT("\r\n%s\r\n"), buf);
	send(clientsocket, buf, strlen(buf), 0);
}

int TCPconnect(char *ip, char *port, HWND hwnd) {
	struct sockaddr_in client_sin;
	struct hostent *he;
	int server_port, client_fd, iResult, err;
	u_long iMode = 1;

	if ((he = gethostbyname(ip)) == NULL) {
		EditPrintf(hwndEdit, TEXT("=== Error: gethostbyname error ===\r\n"));
		WSACleanup();
		return -1;
	}

	client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (client_fd == INVALID_SOCKET) {
		EditPrintf(hwndEdit, TEXT("=== Error: create socket error ===\r\n"));
		WSACleanup();
		return -1;
	}
	EditPrintf(hwndEdit, TEXT("=== create socket ===\r\n"));

	server_port = atoi(port);

	ZeroMemory(&client_sin, sizeof(client_sin));
	client_sin.sin_family = AF_INET;
	//client_sin.sin_addr = *((struct in_addr *)he->h_addr);
	memcpy(&client_sin.sin_addr, he->h_addr_list[0], he->h_length);
	client_sin.sin_port = htons(server_port);

	if (connect(client_fd, (struct sockaddr *)&client_sin, sizeof(client_sin)) == SOCKET_ERROR) {
		EditPrintf(hwndEdit, TEXT("=== Error: connect socket error ===\r\n"));
		WSACleanup();
		return -1;
	}
	EditPrintf(hwndEdit, TEXT("=== connect success ===\r\n"));

	err = WSAAsyncSelect(client_fd, hwnd, WM_CGI_NOTIFY, F_CONNECTING);
	if (err == SOCKET_ERROR) {
		EditPrintf(hwndEdit, TEXT("=== Error: select error ===\r\n"));
		closesocket(client_fd);
		WSACleanup();
		return -1;
	}
	EditPrintf(hwndEdit, TEXT("=== connect AsyncSelect socket success ===\r\n"));

	return client_fd;
}

int find_id(int fd, int id_clifd_arr[6]) {
	int i = 0;
	for (i = 1; i <= 5; i++) {
		if (id_clifd_arr[i] == fd) {
			return i;
		}
	}
	return i;
}

int write_html_handler(int clientsock, HWND hwnd) {
	FILE *fp;
	int file_num, err;

	file_num = parse_http_request(buffer);
	EditPrintf(hwndEdit, TEXT("file_num %d\r\n"), file_num);

	//check form_get.htm
	EditPrintf(hwndEdit, TEXT("%s\r\n"), files);
	if (file_num == 1) {
		fp = fopen("form_get.htm", "r");
		if (fp == NULL) {
			EditPrintf(hwndEdit, TEXT("form_get.htm doesn't exist\r\n"));
			return -1;
		}
		EditPrintf(hwndEdit, TEXT("form_get.htm open\r\n"));

		//send 200 response
		send(clientsock, "HTTP/1.1 200 OK\r\n", 17, 0);
		EditPrintf(hwndEdit, TEXT("HTTP/1.1 200 OK\r\n"));

		send(clientsock, "Content-type: text/html\r\n\r\n", 17, 0);
		//EditPrintf(hwndEdit, TEXT("Content-type: text/html\r\n\r\n"));

		while (fgets(buffer, BUFFERSIZE, fp) != NULL) {
			send(clientsock, buffer, strlen(buffer), 0);
			//EditPrintf(hwndEdit, TEXT("%s\r\n"), buffer);
		}
		EditPrintf(hwndEdit, TEXT("send html finish\r\n"));
	}
	else if (file_num == 2) { //exec *.cgi
		int i, client_fd;

		for (i = 0; i <= 6; i++) {
			exit_flag[i] = 0;
		}

		browser_fd = clientsock;

		EditPrintf(hwndEdit, TEXT("executing %s\r\n"), files);
		EditPrintf(hwndEdit, TEXT("argvs %s\r\n"), argvs);

		client_num = parse_query_string(argvs, ip, port, file);
		EditPrintf(hwndEdit, TEXT("-----------------parse success with %d clients----------------\r\n"), client_num);

		send(clientsock, "HTTP/1.1 200 OK\r\n", 17, 0);
		EditPrintf(hwndEdit, TEXT("send 200 OK\r\n"));

		init_html(ip, clientsock);
		EditPrintf(hwndEdit, TEXT("init success\r\n"));

		//create client socket
		for (i = 1; i <= 5; i++) {
			if (strcmp(ip[i], "") == 0 || strcmp(port[i], "") == 0 || strcmp(file[i], "") == 0) {
				id_clifd_arr[i] = 0;
				continue;
			}

			EditPrintf(hwndEdit, TEXT("conncting client%d\r\n"), i);

			client_fd = TCPconnect(ip[i], port[i], hwnd);
			if (client_fd < 0) continue;
			id_clifd_arr[i] = client_fd;

			fp = fopen(file[i], "r");
			if (fp == NULL) {
				EditPrintf(hwndEdit, TEXT("cannot open file: %s\r\n"), file[i]);
				return -1;
			}

			id_filefd_arr[i] = fp;
			id_status_arr[i] = F_READING;

			EditPrintf(hwndEdit, TEXT("%d connct success : %d\r\n"), i, client_fd);

			err = WSAAsyncSelect(client_fd, hwnd, WM_CGI_NOTIFY, F_READING);
			if (err == SOCKET_ERROR) {
				EditPrintf(hwndEdit, TEXT("=== Error: create client socket select error ===\r\n"));
				closesocket(client_fd);
				WSACleanup();
				return -1;
			}
			EditPrintf(hwndEdit, TEXT("create client socket select OK\r\n"));
		}

	}
	else {
		EditPrintf(hwndEdit, TEXT("WHAT IS THIS FUCKING FILE\r\n"));
	}
	return file_num;
}

char* str_to_html_format(int id, char *line) {
	int i, j = 0, len;
	char *newline = (char*)malloc(sizeof(char) * (BUFFERSIZE + 100));
	char *newline2 = (char*)malloc(sizeof(char) * (BUFFERSIZE + 200));

	len = strlen(line);
	for (i = 0; i < len; i++) {
		if (line[i] == '\r') {
			continue;
		}
		else if (line[i] == '<') {
			newline[j++] = '&';
			newline[j++] = 'l';
			newline[j++] = 't';
		}
		else if (line[i] == '>') {
			newline[j++] = '&';
			newline[j++] = 'g';
			newline[j++] = 't';
		}
		else if (line[i] == '"') {
			newline[j++] = '\\';
			newline[j++] = '"';
		}
		else if (line[i] == '\n') {
			newline[j++] = '<';
			newline[j++] = 'b';
			newline[j++] = 'r';
			newline[j++] = '>';
		}
		else {
			newline[j++] = line[i];
		}
	}

	//if line is NOT '% ' needs to print newline
	/*if (strstr(line, "% ") == NULL) {
		newline[j++] = '<';
		newline[j++] = 'b';
		newline[j++] = 'r';
		newline[j++] = '>';
	}*/

	newline[j] = 0;

	sprintf(newline2, "<script>document.all[\'m%d\'].innerHTML += \"%s\";</script>", id, newline);
	free(newline);

	return newline2;
}

BOOL CALLBACK MainDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam) {
	WSADATA wsaData;
	static SOCKET msock, ssock;
	static struct sockaddr_in sa;
	int err;

	switch (Message)
	{
	case WM_INITDIALOG:
		hwndEdit = GetDlgItem(hwnd, IDC_RESULT);
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_LISTEN:

			WSAStartup(MAKEWORD(2, 0), &wsaData);

			//create master socket
			msock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

			if (msock == INVALID_SOCKET) {
				EditPrintf(hwndEdit, TEXT("=== Error: create socket error ===\r\n"));
				WSACleanup();
				return TRUE;
			}

			err = WSAAsyncSelect(msock, hwnd, WM_SOCKET_NOTIFY, FD_ACCEPT);
			if (err == SOCKET_ERROR) {
				EditPrintf(hwndEdit, TEXT("=== Error: select error ===\r\n"));
				closesocket(msock);
				WSACleanup();
				return TRUE;
			}

			//fill the address info about server
			sa.sin_family = AF_INET;
			sa.sin_port = htons(SERVER_PORT);
			sa.sin_addr.s_addr = INADDR_ANY;

			//bind socket
			err = bind(msock, (LPSOCKADDR)&sa, sizeof(struct sockaddr));

			if (err == SOCKET_ERROR) {
				EditPrintf(hwndEdit, TEXT("=== Error: binding error ===\r\n"));
				WSACleanup();
				return FALSE;
			}

			err = listen(msock, 2);

			if (err == SOCKET_ERROR) {
				EditPrintf(hwndEdit, TEXT("=== Error: listen error ===\r\n"));
				WSACleanup();
				return FALSE;
			}
			else {
				EditPrintf(hwndEdit, TEXT("=== Server START ===\r\n"));
			}

			break;
		case ID_EXIT:
			EndDialog(hwnd, 0);
			break;
		};
		break;

	case WM_CLOSE:
		EndDialog(hwnd, 0);
		break;

	case WM_SOCKET_NOTIFY:
		switch (WSAGETSELECTEVENT(lParam))
		{
		case FD_ACCEPT:
			ssock = accept(msock, NULL, NULL);
			Socks.push_back(ssock);
			EditPrintf(hwndEdit, TEXT("=== Accept one new client(%d), List size:%d ===\r\n"), ssock, Socks.size());

			err = WSAAsyncSelect(ssock, hwnd, WM_SOCKET_NOTIFY, FD_READ);
			if (err == SOCKET_ERROR) {
				EditPrintf(hwndEdit, TEXT("=== Error: FD_ACCEPT select error ===\r\n"));
				closesocket(ssock);
				WSACleanup();
				return TRUE;
			}
			break;
		case FD_READ:
			//Write your code for read event here.
			EditPrintf(hwndEdit, TEXT("\r\n***FD_READ client(%d)***\r\n\r\n"), wParam);
			int read_len;
			read_len = read_http_requrst(wParam, buffer);
			EditPrintf(hwndEdit, TEXT("read_len %d\r\n"), read_len);
			if (read_len < 0) {
				EditPrintf(hwndEdit, TEXT("read error or not get form_get.htm/hw3.cgi \r\n"));
				//err = WSAAsyncSelect(ssock, hwnd, WM_SOCKET_NOTIFY, FD_CLOSE);
				return TRUE;
			}
			else {
				err = WSAAsyncSelect(wParam, hwnd, WM_SOCKET_NOTIFY, FD_WRITE);
				if (err == SOCKET_ERROR) {
					EditPrintf(hwndEdit, TEXT("=== Error: FD_READ select error ===\r\n"));
					closesocket(ssock);
					WSACleanup();
					return TRUE;
				}
			}

			break;
		case FD_WRITE:
			//Write your code for write event here
			int file_num;
			EditPrintf(hwndEdit, TEXT("\r\n***FD_WRITE***\r\n\r\n"));
			file_num = write_html_handler(wParam, hwnd);
			if (file_num <= 1) {
				EditPrintf(hwndEdit, TEXT("\r\nwrite %d over\r\n\r\n"), file_num);
				closesocket(wParam);
			}
			//TODO when all client exit closesocket or browser will keep loading...

			break;
		case FD_CLOSE:
			break;
		};
		break;

	case WM_CGI_NOTIFY:
		switch (WSAGETSELECTEVENT(lParam))
		{
		case F_CONNECTING:
			//GG
			break;
		case F_READING:
			//Read server msg
			EditPrintf(hwndEdit, TEXT("\r\n***F_READING***\r\n\r\n"));
			int id, status, n;

			id = find_id(wParam, id_clifd_arr);
			EditPrintf(hwndEdit, TEXT("client %d\r\n"), id);
			if (id <= 5 && id >= 1) {
				status = id_status_arr[id];
				if (status == F_READING) {
					EditPrintf(hwndEdit, TEXT("start reading\r\n"));
					memset(buffer, 0, sizeof(buffer));
					n = recv(wParam, buffer, BUFFERSIZE, 0);
					if (n < 0) {
						//read error
						EditPrintf(hwndEdit, TEXT("recv error\r\n"));
					}
					/*else if(n == 0 && exit_flag[id] == 1){
						//client disconnect
						EditPrintf(hwndEdit, TEXT("client %d leaving\r\n"), id);
						closesocket(wParam);
					}*/
					else {
						//print to html and change state to F_WRITING
						EditPrintf(hwndEdit, TEXT("recv:[%s]\r\n"), buffer);
						char *newline;
						newline = str_to_html_format(id, buffer);
						EditPrintf(hwndEdit, TEXT("proc:[%s]\r\n"), newline);
						send(browser_fd, newline, strlen(newline), 0);
						
						if (strstr(buffer, "% ") != NULL) {
							EditPrintf(hwndEdit, TEXT("change to F_WRITING\r\n"));
							id_status_arr[id] = F_WRITING;

							err = WSAAsyncSelect(wParam, hwnd, WM_CGI_NOTIFY, F_WRITING);
							if (err == SOCKET_ERROR) {
								EditPrintf(hwndEdit, TEXT("=== Error: create client to F_WRITING select error ===\r\n"));
								closesocket(wParam);
								WSACleanup();
								//return -1;
							}
						}
					}
					//client disconnect
					if (exit_flag[id] == 1) {
						EditPrintf(hwndEdit, TEXT("client %d leaving\r\n"), id);
						closesocket(wParam);
						client_num--;
					}
					//all client disconnect, close browser conn
					if (client_num == 0) {
						EditPrintf(hwndEdit, TEXT("all client has left, close browser conn\r\n"));
						closesocket(browser_fd);
						int i;
						for (i = 0; i <= 6; i++) {
							exit_flag[i] = 0;
						}
					}
				}
			}
			break;
		case F_WRITING:
			//Read file and send to server
			EditPrintf(hwndEdit, TEXT("\r\n***F_WRITING***\r\n\r\n"));
			int NeedSend, offset, send_cnt;

			id = find_id(wParam, id_clifd_arr);
			EditPrintf(hwndEdit, TEXT("client %d\r\n"), id);
			if (id <= 5 && id >= 1) {
				status = id_status_arr[id];
				if (status == F_WRITING) {
					EditPrintf(hwndEdit, TEXT("start read file\r\n"));
					memset(buffer, 0, sizeof(buffer));
					if (fgets(buffer, BUFFERSIZE, id_filefd_arr[id]) != NULL) {
						//add \n
						if (strcmp(buffer, "exit") == 0) {
							buffer[4] = '\n';
						}
						
						char *newline;
						newline = str_to_html_format(id, buffer);
						EditPrintf(hwndEdit, TEXT("%s\r\n"), newline);
						send(browser_fd, newline, strlen(newline), 0);

						NeedSend = strlen(buffer);
						offset = 0;
						while (NeedSend > 0) {
							send_cnt = send(wParam, buffer + offset, strlen(buffer + offset), 0);
							EditPrintf(hwndEdit, TEXT("sending cmd: %s, %d\r\n"), buffer + offset, NeedSend);
							offset += send_cnt;
							NeedSend -= send_cnt;
						}
						if (strstr(buffer, "exit") != NULL) {
							printf("FUCKING EXIT\r\n");
							exit_flag[id] = 1;
						}
						if (NeedSend <= 0) {
							id_status_arr[id] = F_READING;

							err = WSAAsyncSelect(wParam, hwnd, WM_CGI_NOTIFY, F_READING);
							if (err == SOCKET_ERROR) {
								EditPrintf(hwndEdit, TEXT("=== Error: create client to F_READING select error ===\r\n"));
								closesocket(wParam);
								WSACleanup();
								//return -1;
							}
						}
					}
				}
			}
			break;
		case F_DONE:
			//GG
			break;
		default:
			//GG
			EditPrintf(hwndEdit, TEXT("\r\n***WHAT THE FUCK***\r\n\r\n"));
			return FALSE;
		}

	default:
		return FALSE;

	};

	return TRUE;
}

int EditPrintf(HWND hwndEdit, TCHAR * szFormat, ...)
{
	TCHAR   szBuffer[10240];
	va_list pArgList;

	va_start(pArgList, szFormat);
	wvsprintf(szBuffer, szFormat, pArgList);
	va_end(pArgList);

	SendMessage(hwndEdit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
	SendMessage(hwndEdit, EM_REPLACESEL, FALSE, (LPARAM)szBuffer);
	SendMessage(hwndEdit, EM_SCROLLCARET, 0, 0);
	return SendMessage(hwndEdit, EM_GETLINECOUNT, 0, 0);
}