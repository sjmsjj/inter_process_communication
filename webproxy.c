#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

#include "gfserver.h"
  
//By Jianming Sang
                                                                \
#define USAGE                                                                 \
"usage:\n"                                                                    \
"  webproxy [options]\n"                                                     \
"options:\n"                                                                  \
"  -p [listen_port]    Listen port (Default: 8888)\n"                         \
"  -t [thread_count]   Num worker threads (Default: 1, Range: 1-1000)\n"      \
"  -s [server]         The server to connect to (Default: Udacity S3 instance)"\
"  -h                  Show this help message\n"                              \
"special options:\n"                                                          \
"  -d [drop_factor]    Drop connects if f*t pending requests (Default: 5).\n"


/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
  {"port",          required_argument,      NULL,           'p'},
  {"thread-count",  required_argument,      NULL,           't'},
  {"server",        required_argument,      NULL,           's'},         
  {"help",          no_argument,            NULL,           'h'},
  {NULL,            0,                      NULL,             0}
};

extern ssize_t handle_with_cache(gfcontext_t *ctx, char *path, void* arg);

static gfserver_t gfs;

static void _sig_handler(int signo){
  if (signo == SIGINT || signo == SIGTERM){
    gfserver_stop(&gfs);
    exit(signo);
  }
}

extern steque_t *qsegment_pointers;  //steque to store the pointers to the blob_t struct defined below
extern steque_t *qnames;     //steque to store the nameID used to create shared memory object
extern size_t segment_size;   

typedef struct blob_t blob_t;

struct blob_t
{
  size_t size;   //segment size
  int done_read;   //indicate webproxy whether or not finishes reading data from shared memory: 1: done, 0: still reading
  int done_write;  //indicate cache has whether or not finishes writing data to shared memory: 1: done, 0: still writing
  size_t file_len;
  size_t read_len;  
  pthread_mutex_t sh_lock;
  pthread_cond_t sh_read_cv;
  pthread_cond_t sh_write_cv;
  char buffer[];  //used to transfer data between webproxy and cache, size = segment_size
};

/* Main ========================================================= */
int main(int argc, char **argv) {
  int i, option_char = 0;
  unsigned short port = 8888;
  unsigned short nworkerthreads = 5;
  char *server = "s3.amazonaws.com/content.udacity-data.com";

  unsigned short nsegments = 5;
  segment_size = 2048;



  if (signal(SIGINT, _sig_handler) == SIG_ERR){
    fprintf(stderr,"Can't catch SIGINT...exiting.\n");
    exit(EXIT_FAILURE);
  }

  if (signal(SIGTERM, _sig_handler) == SIG_ERR){
    fprintf(stderr,"Can't catch SIGTERM...exiting.\n");
    exit(EXIT_FAILURE);
  }

  // Parse and set command line arguments
  while ((option_char = getopt_long(argc, argv, "n:z:p:t:s:h", gLongOptions, NULL)) != -1) {
    switch (option_char) {
      case 'n':
        nsegments = atoi(optarg);
        break;
      case 'z':
        segment_size = atoi(optarg);
        break;
      case 'p': // listen-port
        port = atoi(optarg);
        break;
      case 't': // thread-count
        nworkerthreads = atoi(optarg);
        break;
      case 's': // file-path
        server = optarg;
        break;                                          
      case 'h': // help
        fprintf(stdout, "%s", USAGE);
        exit(0);
        break;       
      default:
        fprintf(stderr, "%s", USAGE);
        exit(1);
    }
  }
  
  qsegment_pointers = (steque_t*)malloc(sizeof(*qsegment_pointers));
  qnames = (steque_t*)malloc(sizeof(*qnames));
  steque_init(qsegment_pointers);
  steque_init(qnames);

  for(i = 1; i <= nsegments; i++)
  {
   // printf("test\n");
    char *name = (char*)malloc(8);
    sprintf(name, "/%d", i);   //crate nameID for creating shared memory
    steque_enqueue(qnames, name);
     

    int shm_fd = shm_open(name, O_CREAT|O_EXCL|O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(blob_t) + segment_size);

    void *ptr = mmap(NULL, sizeof(blob_t) + segment_size, PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if(ptr == MAP_FAILED)
    {
      fprintf(stderr, "Map failed.\n");
      exit(1);
    }
    close(shm_fd);
    
    blob_t *blob = (blob_t*)ptr;

    blob->done_read = 1;  //initially webproxy cannot read data from shared memory
    blob->done_write = 0;  //initially cache can write data to shared memory
    blob->file_len = 1;
    blob->size = segment_size;

    pthread_mutexattr_t attrmutex;
    pthread_mutexattr_init(&attrmutex);
    pthread_mutexattr_setpshared(&attrmutex, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&blob->sh_lock, &attrmutex);

    pthread_condattr_t attrcond1;
    pthread_condattr_init(&attrcond1);
    pthread_condattr_setpshared(&attrcond1, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&blob->sh_read_cv, &attrcond1);
    
    pthread_condattr_t attrcond2;
    pthread_condattr_init(&attrcond2);
    pthread_condattr_setpshared(&attrcond2, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&blob->sh_write_cv, &attrcond2);

    steque_enqueue(qsegment_pointers, blob);    
  }

  /*Initializing server*/
  gfserver_init(&gfs, nworkerthreads);

  /*Setting options*/
  gfserver_setopt(&gfs, GFS_PORT, port);
  gfserver_setopt(&gfs, GFS_MAXNPENDING, 10);
  gfserver_setopt(&gfs, GFS_WORKER_FUNC, handle_with_cache);
  for(i = 0; i < nworkerthreads; i++)
    gfserver_setopt(&gfs, GFS_WORKER_ARG, i, server);

  /*Loops forever*/
  gfserver_serve(&gfs);
}