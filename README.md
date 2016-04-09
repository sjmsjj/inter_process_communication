# Shared memory based inter-process communication

##Design idea
Initial communication between webproxy process and cache process was established through socket to exchange header informatioin, then file data was transfered from cache process to webproxy process through shared memory object

##The initial communication process is describled below:

Webproxy socket send header information [shared memory size + nameID used to create the shared memory object + request file path] to cache --> cache worker thread socket receive and parse the header information from webproxy --> cache check whether the request file exists or not --> if file does not exist, cache sends "error" back to webproxy; if file exists, caches get the file descriptor, sends the file length back to webproxy through socket, map the shared memory object and prepare to write data to the shared memory --> webproxy receives  and parses header information from cache, it it is "error", webproxy sends "FILE NOT EXIST" to the requested client, otherwise, it prepare to read data from the shared memory and send the data to the client.

To transfer data through shared memory, a structure called blot_t is created and it contains the following elements:
*size: size in bytes of shared memory
*done_read: if webproxy has finished reading data from the shared memory,
           set it to be 1, otherwise set it to be 0
*done_write: if cache has finished writing data to the shared memory, set 
            it to be 1, otherwise set it to be 0
*file_len: length of request file
*read_len: bytes length cache read from the file one time (which is set to 
          be always smaller than the segment size), this is also the length of data that cache write to the shared memory one time
*sh_lock: mutex for reading and writing data to the shared memory
*sh_write_cv: conditional variable for writing
*sh_read_cv: conditional variable for reading

##Multhreading design

In webproxy, two steque objects are created: qnames for storing the shared memory’s name ID, qsegment_pointer for storing the pointer to the blob_t structure. When shared memory objects are created, both name ID and blob_t pointer will be added to these queues. Everytime webproxy receives a request from the client, name ID from the qname, and pop blob_t pointer from the qsegment_pointer are poped for use, and then work is done, both name ID and blob_t pointer wil be returned for recycling.

In cache, one steque object is created: queue. Every time the boss thread in cache receive one request from webproxy, it will add the created socket for handling header information to the queue. Then worker thread's work starts with popping out one socket from the queue


