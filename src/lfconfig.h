#ifndef LFCONFIG_H
#define LFCONFIG_H

typedef struct {
	char *listen;
	int backlog;
	int threads;

	int sandbox;
	size_t mem_max;
	size_t output_max;
	unsigned long cpu_usec;
	unsigned long cpu_sec;

	char *content_type;
} LF_config;

LF_config *LF_createconfig();
int LF_loadconfig(LF_config *, char *);

#endif
