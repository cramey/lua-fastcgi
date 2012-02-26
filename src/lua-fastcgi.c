#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <fcgi_config.h>
#include <fcgiapp.h>

#include <lua5.1/lua.h>
#include <pthread.h>

#include "lua.h"
#include "config.h"
#include "lua-fastcgi.h"


static char *http_status_strings[] = {
	[200] = "OK",
	[403] = "Forbidden",
	[404] = "Not Found",
	[500] = "Internal Server Error"
};


#define senderror(status_code,error_string) \
	if(!response.committed){ \
			FCGX_FPrintF(request.out, "Status: %d %s\r\n", status_code, http_status_strings[status_code]); \
			FCGX_FPrintF(request.out, "Content-Type: %s\r\n\r\n", config->content_type); \
			response.committed = 1; \
	} \
	FCGX_PutS(error_string, response.out);


#ifdef DEBUG
static void printcfg(LF_config *cfg)
{
	printf("Listen: %s\n", cfg->listen);
	printf("Backlog: %d\n", cfg->backlog);
	printf("Threads: %d\n", cfg->threads);
	printf("Sandbox: %d\n", cfg->sandbox);
	printf("Max Memory: %zu\n", cfg->mem_max);
	printf("Max Output: %zu\n", cfg->output_max);
	printf("CPU usec: %lu\n", cfg->cpu_usec);
	printf("CPU sec: %lu\n", cfg->cpu_sec);
	printf("Default Content Type: %s\n", cfg->content_type);
	printf("\n");
}


static void printvars(FCGX_Request *request)
{
	for(int i=0; request->envp[i] != NULL; i++){
		printf("%s\n", request->envp[i]);
	}
	printf("\n");
}
#endif


void *thread_run(void *arg)
{
	LF_params *params = arg;
	LF_config *config = params->config;
	LF_limits *limits = LF_newlimits();
	LF_response response;
	lua_State *l;

	FCGX_Request request;
	FCGX_InitRequest(&request, params->socket, 0);

	for(;;){
		LF_setlimits(
			limits, config->mem_max, config->output_max,
			config->cpu_sec, config->cpu_usec
		);

		l = LF_newstate(config->sandbox, config->content_type);

		if(FCGX_Accept_r(&request)){ 
			printf("FCGX_Accept_r() failure\n");
			LF_closestate(l);
			continue;
		}

		#ifdef DEBUG
		printvars(&request);
		#endif

		LF_parserequest(l, &request, &response);
		LF_enablelimits(l, limits);

		switch(LF_loadfile(l)){
			case 0:
				if(lua_pcall(l, 0, 0, 0)){
					senderror(500, lua_tostring(l, -1));
				} else if(!response.committed){
					senderror(200, "");
				}
			break;

			case EACCES:
				senderror(403, lua_tostring(l, -1));
			break;

			case ENOENT:
				senderror(404, lua_tostring(l, -1));
			break;

			default:
				senderror(500, lua_tostring(l, -1));
			break;
		}

		FCGX_Finish_r(&request);
		LF_closestate(l);
	}
}


int main()
{
	if(FCGX_Init() != 0){
		printf("FCGX_Init() failure\n");
		exit(EXIT_FAILURE);
	}

	LF_config *config = LF_createconfig();
	if(config == NULL){
		printf("LF_createconfig(): memory allocation error\n");
		exit(EXIT_FAILURE);
	}

	if(LF_loadconfig(config, "./lua-fastcgi.lua")){
		printf("Error loading lua-fastcgi.lua\n");
	}

	#ifdef DEBUG
	printcfg(config);
	#endif

	int socket = FCGX_OpenSocket(config->listen, config->backlog);
	if(socket < 0){
		printf("FCGX_OpenSocket() failure: could not open %s\n", config->listen);
		exit(EXIT_FAILURE);
	}

	LF_params *params = malloc(sizeof(LF_params));
	params->socket = socket;
	params->config = config;

	if(config->threads == 1){
		thread_run(params);
	} else {
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_t threads[config->threads];
		for(int i=0; i < config->threads; i++){
			int r = pthread_create(&threads[i], &attr, &thread_run, params);
			if(r){
				printf("Thread creation error: %d\n", r);
				exit(EXIT_FAILURE);
			}
		}

		pthread_join(threads[0], NULL);
	}
	
	return 0;
}
