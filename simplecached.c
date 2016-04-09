#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "shm_channel.h"
#include "simplecache.h"
#include "steque.h"

#define MAX_CACHE_REQUEST_LEN 256

//By Jianming Sang

static void _sig_handler(int signo){
	if (signo == SIGINT || signo == SIGTERM){
		/* Unlink IPC mechanisms here*/
		exit(signo);
	}
}

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  simplecached [options]\n"                                                  \
"options:\n"                                                                  \
"  -t [thread_count]   Num worker threads (Default: 1, Range: 1-1000)\n"      \
"  -c [cachedir]       Path to static files (Default: ./)\n"                  \
"  -h                  Show this help message\n"                              

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
  {"nthreads",           required_argument,      NULL,           't'},
  {"cachedir",           required_argument,      NULL,           'c'},
  {"help",               no_argument,            NULL,           'h'},
  {NULL,                 0,                      NULL,             0}
};

void Usage() {
  fprintf(stdout, "%s", USAGE);
}

typedef struct blob_t
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
}blob_t;

steque_t* queue;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;
void* worker(void* arg);

int main(int argc, char **argv) 
{
	int nthreads = 5;
	int i;
	char *cachedir = "locals.txt";
	char option_char;

	while ((option_char = getopt_long(argc, argv, "t:c:h", gLongOptions, NULL)) != -1) 
	{
		switch (option_char) 
		{
			case 't': // thread-count
				nthreads = atoi(optarg);
				break;   
			case 'c': //cache directory
				cachedir = optarg;
				break;
			case 'h': // help
				Usage();
				exit(0);
				break;    
			default:
				Usage();
				exit(1);
		}
	}

	if (signal(SIGINT, _sig_handler) == SIG_ERR){
		fprintf(stderr,"Can't catch SIGINT...exiting.\n");
		exit(EXIT_FAILURE);
	}

	if (signal(SIGTERM, _sig_handler) == SIG_ERR){
		fprintf(stderr,"Can't catch SIGTERM...exiting.\n");
		exit(EXIT_FAILURE);
	}

	/* Initializing the cache */
	queue = (steque_t*)malloc(sizeof(*queue));
	steque_init(queue);
	simplecache_init(cachedir);

    pthread_t workers[nthreads];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	for(i = 0; i < nthreads; i++)
		pthread_create(&workers[i], &attr, worker, NULL);

	int serv_sock = create_server_socket();  //create boss server socket
	while(1)
	{
		int *sock = linked_socket(serv_sock); //boss server socket accept request and push the new socketed to the queue
		pthread_mutex_lock(&lock);
		steque_enqueue(queue, sock);
		pthread_mutex_unlock(&lock);
		pthread_cond_signal(&empty);
	}
	simplecache_destroy();
	exit(0);
}

void *worker(void* arg)
{
	while(1)
	{
		//worker thread handles the request start with poping one socket from the queue
		pthread_mutex_lock(&lock);
		while(queue->N == 0)
			pthread_cond_wait(&empty, &lock);
		int *sock = steque_pop(queue);
		pthread_mutex_unlock(&lock);

        char *cp, *size, *name, *key; //for parsing the header information from handle with cache
        size_t segment_size;

        //first message contains: "segment_size name filekey"
		cp = server_sock_recv(*sock);
		//printf("%s\n", cp);

		size = strtok(cp, " ");
		name = strtok(NULL, " ");
		key = strtok(NULL, " ");

		//printf("handle: %s\n", name);

		segment_size = atoi(size);
		
		int fildes = simplecache_get(key);
		
        char error[10] = "error";
		if(fildes == -1)  //requested file does not exist, send back "error" signal
		{
			server_sock_send(*sock, error);
			close(*sock);
			free(sock);
			free(cp);
			continue;
		}

		int shm_fd = shm_open(name, O_RDWR, 0666);
		void* ptr = mmap(NULL, sizeof(blob_t) + segment_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	    if (ptr == MAP_FAILED) 
	    {
	      printf("Map failed\n");
	      exit(1);
	    }
	    close(shm_fd);

	    blob_t* blob = (blob_t*) ptr;
	 
		size_t file_len, bytes_transferred;
		ssize_t read_len;
		char file_len_char[100];

		file_len = lseek(fildes, 0, SEEK_END);
		lseek(fildes, 0, SEEK_SET);
		sprintf(file_len_char, "%zu", file_len);
		server_sock_send(*sock, file_len_char); //request file exists, send back the file length
		
	    blob->file_len = file_len;
	    bytes_transferred = 0;

	   // printf("begin file transfer for %s\n", name);
	    //begin file transfer through shared memory untill all bytes have been sent
	    while(bytes_transferred < file_len)
	    {
	    	pthread_mutex_lock(&blob->sh_lock);
	    	while(blob->done_read == 0)  //waiting for read to be done
	    		pthread_cond_wait(&blob->sh_write_cv, &blob->sh_lock);
	    	read_len = read(fildes, blob->buffer, blob->size - 1);
	    	blob->read_len = read_len;
	        blob->done_write = 1; //writing done, good for reading
	        blob->done_read = 0;
	        pthread_mutex_unlock(&blob->sh_lock);
	        pthread_cond_signal(&blob->sh_read_cv);
	        bytes_transferred += read_len;
	    }
	   // printf("file transfer done for %s\n", name);
	    close(*sock);
		free(sock);
		free(cp);
	    munmap(ptr, sizeof(blob_t) + segment_size);
	}
}