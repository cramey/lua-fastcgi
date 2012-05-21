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
	if(!state.committed){ \
			FCGX_FPrintF(request.out, "Status: %d %s\r\n", status_code, http_status_strings[status_code]); \
			FCGX_FPrintF(request.out, "Content-Type: %s\r\n\r\n", config->content_type); \
			state.committed = 1; \
	} \
	FCGX_PutS(error_string, state.response);


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
	LF_state state;
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
		struct timespec rstart, rend;
		clock_gettime(CLOCK_MONOTONIC, &rstart);
		#endif

		LF_parserequest(l, &request, &state);

		#ifdef DEBUG
		clock_gettime(CLOCK_MONOTONIC, &rend);
		// Assumes the request returns in less than a second (which it should)
		printf("Request parsed in %luns\n", (rend.tv_nsec-rstart.tv_nsec));
		#endif

		LF_enablelimits(l, limits);

		switch(LF_loadscript(l)){
			case 0:
				if(lua_pcall(l, 0, 0, 0)){
					if(lua_isstring(l, -1)){
						senderror(500, lua_tostring(l, -1));
					} else {
						senderror(500, "unspecified lua error");
					}
				} else if(!state.committed){
					senderror(200, "");
				}
			break;

			case LF_ERRACCESS: senderror(403, "access denied"); break;
			case LF_ERRMEMORY: senderror(500, "not enough memory"); break;
			case LF_ERRNOTFOUND:
				printf("404\n");
				senderror(404, "no such file or directory");
			break;
			case LF_ERRSYNTAX: senderror(500, lua_tostring(l, -1)); break;
			case LF_ERRBYTECODE: senderror(403, "compiled bytecode not supported"); break;
			case LF_ERRNOPATH: senderror(500, "SCRIPT_FILENAME not provided"); break;
			case LF_ERRNONAME: senderror(500, "SCRIPT_NAME not provided"); break;
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
