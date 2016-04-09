#include <stdlib.h>
#include <fcntl.h>
#include <curl/curl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "gfserver.h"

//By Jianming Sang

//Replace with an implementation of handle_with_curl and any other
//functions you may need.



//basic working idea is to down the data from the server and save all the data in a temporary 
//memory block, and then send this block data through gfs_send.

struct memory_hub
{
	char *block;  //for storing all the downloaded data
	size_t size;  //total size of downloaded data
};

//write call back to store received data and file size
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userdata)
{
	size_t real_size = size * nmemb;
	struct memory_hub *mem = (struct memory_hub*)userdata;

	mem->block = realloc(mem->block, mem->size + real_size + 1);
	if(mem->block == NULL)
	{
		fprintf(stderr, "not enough memory in realloc");
		return EXIT_FAILURE;
	}

	memcpy(&(mem->block[mem->size]), contents, real_size);
	mem->size += real_size;
	mem->block[mem->size] = 0;

	return real_size;
}

//clean up everything after job done
static void clean(CURL *curl_handler, struct memory_hub *hub)
{
	curl_easy_cleanup(curl_handler);
	free(hub->block);
	curl_global_cleanup();
}

ssize_t handle_with_curl(gfcontext_t *ctx, char *path, void* arg)
{
	size_t bytes_transferred;
	char url[4096];
	char *server = (char*)arg;
    //construct request url
	strcpy(url, "http://");
	strcat(url, server);
	strcat(url, path);

	struct memory_hub hub;
	hub.block = malloc(1);
	hub.size = 0;
    //initialize curl handler
    curl_global_init(CURL_GLOBAL_ALL);
	CURL *curl_handler = curl_easy_init();

	if(!curl_handler)
	{
		fprintf(stderr, "error in creating curl handler");
		return EXIT_FAILURE;
	}
	else
	{
		CURLcode res;
		curl_easy_setopt(curl_handler, CURLOPT_URL, url);
		curl_easy_setopt(curl_handler, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl_handler, CURLOPT_WRITEDATA, (void*)&hub);
        curl_easy_setopt(curl_handler, CURLOPT_FAILONERROR, 1L);//return error when the file does not exist

		res = curl_easy_perform(curl_handler);
		if(res != CURLE_OK)
		{
			clean(curl_handler, &hub);
			return gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
		}
		else
		{
			gfs_sendheader(ctx, GF_OK, hub.size);
            //gfs_send is supposed to send all the data (it should be implemented to do so)
			bytes_transferred = gfs_send(ctx, hub.block, hub.size);
			if(bytes_transferred != hub.size)//error checking
			{
				clean(curl_handler, &hub);
				fprintf(stderr, "error in gfs_send.");
				return EXIT_FAILURE;
			}
			clean(curl_handler, &hub);
			return bytes_transferred;
		}
    }
}


