#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "shm_channel.h"
#include "gfserver.h"
#include "steque.h"

//By Jianming Sang

//Replace with an implementation of handle_with_cache and any other
//functions you may need.

steque_t *qsegment_pointers;
steque_t *qnames;
size_t segment_size;

pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;  //mutex and cd for accessing steque of nameID and blob_t pointer
pthread_cond_t qempty = PTHREAD_COND_INITIALIZER;

typedef struct blob_t blob_t;

extern struct blob_t
{
  size_t size;
  int done_read;
  int done_write;
  size_t file_len;
  size_t read_len;
  pthread_mutex_t sh_lock;
  pthread_cond_t sh_read_cv;
  pthread_cond_t sh_write_cv;
  char buffer[];
};


//function to clear system after thread has finished its job
void recovery_shared_objects(char *name, blob_t *blob)
{
    pthread_mutex_lock(&qlock);
    steque_enqueue(qnames, name);    //return the nameID and blob_t pointer for reuse
    steque_enqueue(qsegment_pointers, blob);
    pthread_mutex_unlock(&qlock);
    pthread_cond_signal(&qempty);
}

ssize_t handle_with_cache(gfcontext_t *ctx, char *path, void* arg)
{
    ssize_t bytes_transferred;
    char send_msg[2048];
    char *recv_msg;
    char *name;
    sprintf(send_msg, "%zu ", segment_size);

    pthread_mutex_lock(&qlock);
    while(steque_isempty(qnames))
        pthread_cond_wait(&qempty, &qlock);
    name = steque_pop(qnames);
    blob_t *blob = steque_pop(qsegment_pointers);
    pthread_mutex_unlock(&qlock);

   // printf("handle: %s\n", name);

    //initially header information from webproxy to cache: segmentsize nameID path
    strcat(send_msg, name);
    strcat(send_msg, " ");
    strcat(send_msg, path);
    
    int sock = create_client_socket();
    client_sock_send(sock, send_msg);
    recv_msg = client_sock_recv(sock);  
  
    //webproxy will receive "error" (file not exist, stop) or file length (contiune to transfer)
    if(strcmp(recv_msg, "error") == 0)
    {
        recovery_shared_objects(name, blob);
        free(recv_msg);
        return gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
    }
        
    ssize_t file_len;
    file_len = atoi(recv_msg);
    gfs_sendheader(ctx, GF_OK, file_len);

    bytes_transferred = 0;

    //printf("begin recv for %s\n", name);
    while(bytes_transferred < blob->file_len) //keeping transferring until the transfered bytes = file length
    {
        pthread_mutex_lock(&blob->sh_lock);
        while(blob->done_write == 0)  //wait until writing finish
            pthread_cond_wait(&blob->sh_read_cv, &blob->sh_lock);       
        gfs_send(ctx, blob->buffer, blob->read_len);
        bytes_transferred += blob->read_len;
        blob->done_read = 1;  //reading finish, good for writing
        blob->done_write = 0; 
        pthread_mutex_unlock(&blob->sh_lock);
        pthread_cond_signal(&blob->sh_write_cv);
    }

   // printf("recv done for %s\n", name);

    recovery_shared_objects(name, blob);
    free(recv_msg);

	return bytes_transferred;
}


