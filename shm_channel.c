
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <getopt.h>
#include <fcntl.h>

#include "shm_channel.h"

//By Jianming Sang

int create_server_socket()
{
  int portno = 8880;

  int server_sock = socket(AF_INET, SOCK_STREAM, 0);  
  int optval = 1;
  int opt = setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
  struct sockaddr_in servAddr;
  servAddr.sin_family = AF_INET;
  servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servAddr.sin_port = htons(portno);
  
  if(bind(server_sock, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0)
  {
    fprintf(stderr, "error in bind\n");
    exit(1);
  }
  //fprintf(stderr, "bind done.\n");
  return server_sock;
}

int* linked_socket(int server_sock)
{
  while(1)
  {
    if(listen(server_sock, maxnpending) < 0)
      continue;

   // fprintf(stderr, "listen done.\n");

    struct sockaddr_storage sockTmp;
    socklen_t sockLen = sizeof(sockTmp);
    int *sock =(int*)malloc(sizeof(int));
    if ((*sock = accept(server_sock, &sockTmp, &sockLen)) > 0)
    {
      //printf("linked socked done\n");
      return sock;
    }
  }
}

int create_client_socket(void)
{
    char *hostname = "localhost";
    char port[5] = "8880";
    
    struct addrinfo addrHints;
    memset(&addrHints, 0, sizeof(addrHints));
    addrHints.ai_family = AF_UNSPEC;
    addrHints.ai_socktype = SOCK_STREAM;
    addrHints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *addrList;
    //using getaddrifo to get the information for constructing socket
    int r = getaddrinfo(hostname, port, &addrHints, &addrList);
    if(r != 0)
    {
        fprintf(stderr, "getaddrinfo errors");
        exit(1);
    }
    
    int client_sock = -1;
    struct addrinfo * addr;
    //loop until the other side socket is ready
    for (addr = addrList; ; addr = addr->ai_next)
    {
        if (addr == NULL)
            addr = addrList;
        client_sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if(client_sock < 0)
            continue;
        if(connect(client_sock, addr->ai_addr, addr->ai_addrlen) < 0)
        {
            close(client_sock);
            continue;
        }
        break;
    }
    freeaddrinfo(addrList);
    if(client_sock == -1)
    {
        fprintf(stderr, "create client socket failed");
        exit(1);
    }

    return client_sock;
}

char* server_sock_recv(int sock)
{
    char buffer[BUFSIZE];
    ssize_t byte_total = 0;
    ssize_t byte_received = 1;

    byte_received = recv(sock, buffer, BUFSIZE-1, 0);
    if(byte_received <= 0)
    {
      fprintf(stderr, "error in receiving message.\n");
      exit(1);
    }
    buffer[byte_received] = '\0';
    //fprintf(stderr, "recv done.\n");
    //fprintf(stderr, "%s\n", buffer);
    char* msg = malloc(strlen(buffer) + 1);
    strcpy(msg, buffer);
    return msg;
}

void server_sock_send(int sock, char *message)
{
    send(sock, message, strlen(message), 0);
}

char* client_sock_recv(int sock)
{
    char buffer[BUFSIZE];
    ssize_t byte_received_total = recv(sock, buffer, BUFSIZE, 0);
    buffer[byte_received_total] = '\0';
    char* msg = malloc(strlen(buffer) + 1);
    strcpy(msg, buffer);
    return msg;
}

void client_sock_send(int sock, char *message)
{
    ssize_t byte_sent;
    ssize_t byte_sent_total = 0;
    while(byte_sent_total < strlen(message))
    {
        byte_sent = send(sock, message + byte_sent_total, strlen(message), 0);
        if(byte_sent <= 0)
        {
            fprintf(stderr, "error in sending message to server\n");
            exit(1);
        }
        byte_sent_total += byte_sent;
    }
}