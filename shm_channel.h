 //In case you want to implement the shared memory IPC as a library...

#ifndef SHM_CHANNEL_H
#define SHM_CHANNEL_H


#define BUFSIZE 4096
#define maxnpending 10

//By Jianming Sang

//create boss socket for boss thread in cache
int create_server_socket(void);

//create socket for the thread in handle with cache
int create_client_socket(void);

//creat socket for worker thread in cache
int* linked_socket(int server_sock);

//worker socket recv msg in cache from handle with cache 
char* server_sock_recv(int sock);

//worker socket send msg in cache from handle with cache
void server_sock_send(int sock, char *message);

//socket recv msg in handle with cache from cache
char* client_sock_recv(int sock);

//socket send msg in handle with cache to cache
void client_sock_send(int sock, char *message);

#endif