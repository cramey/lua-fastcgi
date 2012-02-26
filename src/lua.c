#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <fcgiapp.h>

#include <lua5.1/lua.h>
#include <lua5.1/lauxlib.h>
#include <lua5.1/lualib.h>

#include "lua.h"
#include "lfuncs.h"

#define LF_BUFFERSIZE 4096

typedef struct {
	int fd;
	char buffer[LF_BUFFERSIZE];
	size_t total;
} LF_loaderdata;


#ifdef DEBUG
void LF_printstack(lua_State *l)
{
	int ss = lua_gettop(l);
	printf("Lua Stack:\n");
	for(int i=1; i <= ss; i++){
		printf("\t%d) %s\n", i, lua_typename(l, lua_type(l, i)));
	}
}
#endif


// Gets current thread usage
static int LF_threadusage(struct timeval *tv)
{
	struct rusage usage;
	if(getrusage(RUSAGE_SELF, &usage) == -1){
		return 1;
	}

	// Add user to sys to get actual usage
	tv->tv_usec = usage.ru_utime.tv_usec + usage.ru_stime.tv_usec;
	tv->tv_sec = usage.ru_utime.tv_sec + usage.ru_stime.tv_sec;

	if(tv->tv_usec > 1000000){
		tv->tv_usec -= 1000000;
		tv->tv_sec++;
	}
	return 0;
}


// limits cpu usage
static void LF_limit_hook(lua_State *l, lua_Debug *d)
{
	lua_pushstring(l, "CPU_LIMIT");
	lua_rawget(l, LUA_REGISTRYINDEX);
	struct timeval *limit = lua_touserdata(l, -1);
	lua_pop(l, 1);

	struct timeval tv;
	if(LF_threadusage(&tv)){ luaL_error(l, "CPU usage sample error"); }
	if(timercmp(&tv, limit, >)){ luaL_error(l, "CPU limit exceeded"); }
}


// removes global variables
static void LF_nilglobal(lua_State *l, char *var)
{
	lua_pushnil(l);
	lua_setglobal(l, var);
}


// Limited memory allocator
static void *LF_limit_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
	size_t *limit = ud;

	*limit += osize;

	if(nsize == 0){
		free(ptr);
		return NULL;
	} else {
		if(*limit < nsize){ return NULL; }
		*limit -= nsize;

		return realloc(ptr, nsize);
	}
}


// Allocates and initializes a new set of limits for a lua state
LF_limits *LF_newlimits()
{
	LF_limits *limits = malloc(sizeof(LF_limits));
	if(limits == NULL){ return NULL; }

	memset(limits, 0, sizeof(LF_limits));
	return limits;
}


void LF_setlimits(LF_limits *limits, size_t memory, size_t output, uint32_t cpu_sec, uint32_t cpu_usec)
{
	limits->memory = memory;
	limits->output = output;
	limits->cpu.tv_usec = cpu_usec;
	limits->cpu.tv_sec = cpu_sec;
}



void LF_enablelimits(lua_State *l, LF_limits *limits)
{
	if(limits->cpu.tv_usec > 0 || limits->cpu.tv_sec > 0){
		struct timeval curr;
		if(LF_threadusage(&curr)){
			printf("CPU usage sample error\n"); // FIX ME
		} else {
			timeradd(&limits->cpu, &curr, &limits->cpu);
			lua_sethook(l, &LF_limit_hook, LUA_MASKCOUNT, 1000);
		}

		lua_pushstring(l, "CPU_LIMIT");
		lua_pushlightuserdata(l, &limits->cpu);
		lua_rawset(l, LUA_REGISTRYINDEX);
	}

	if(limits->output){
		lua_pushstring(l, "RESPONSE_LIMIT");
		lua_pushlightuserdata(l, &limits->output);
		lua_rawset(l, LUA_REGISTRYINDEX);
	}

	if(limits->memory){ lua_setallocf(l, &LF_limit_alloc, &limits->memory); }
}


// Initialize a new lua state using specific parameters
lua_State *LF_newstate(int sandbox, char *content_type)
{
	lua_State *l = luaL_newstate();
	if(l == NULL){ return NULL; }

	// Load base
	lua_pushcfunction(l, luaopen_base);
	lua_pushliteral(l, "");
	lua_call(l, 1, 0);

	if(sandbox){
		// Load table
		lua_pushcfunction(l, luaopen_table);
		lua_pushliteral(l, LUA_TABLIBNAME);
		lua_call(l, 1, 0);

		// Load string
		lua_pushcfunction(l, luaopen_string);
		lua_pushliteral(l, LUA_STRLIBNAME);
		lua_call(l, 1, 0);

		// Load math
		lua_pushcfunction(l, luaopen_math);
		lua_pushliteral(l, LUA_MATHLIBNAME);
		lua_call(l, 1, 0);

		// Nil out unsafe functions/objects
		LF_nilglobal(l, "dofile");
		LF_nilglobal(l, "load");
		LF_nilglobal(l, "loadfile");
		LF_nilglobal(l, "xpcall");
		LF_nilglobal(l, "pcall");
		LF_nilglobal(l, "module");
		LF_nilglobal(l, "require");
	}

	// Register the print function
	lua_register(l, "print", &LF_print);
	// Register the write function
	lua_register(l, "write", &LF_write);

	// Setup the "HEADER" value
	lua_newtable(l);

	lua_pushstring(l, "Content-Type");
	lua_pushstring(l, content_type);
	lua_rawset(l, 1);

	lua_setglobal(l, "HEADER");

	return l;
}


// Set GET variables
static void LF_parsequerystring(lua_State *l, char *query_string)
{
	lua_newtable(l);

	int stack = lua_gettop(l);

	char *sptr, *optr, *nptr;
	for(nptr = optr = sptr = &query_string[0]; 1; optr++){
		switch(*optr){
			case '+':
				*nptr++ = ' ';
			break;

			case '=':
				// Push a key, if it's valid and there's not already one
				if(lua_gettop(l) == stack){
					if((nptr-sptr) > 0){ lua_pushlstring(l, sptr, (nptr - sptr)); }
					sptr = nptr;
				} else {
					*nptr++ = '=';
				}
			break;

			case '&':
				// Push key or value if valid
				if((nptr-sptr) > 0){ lua_pushlstring(l, sptr, (nptr - sptr)); }

				// Push value, if there is already a key
				if(lua_gettop(l) == (stack+1)){ lua_pushstring(l, ""); }

				// Set key/value if they exist
				if(lua_gettop(l) == (stack+2)){ lua_rawset(l, stack); }

				sptr = nptr;
			break;

			case '%': {
				// Decode hex percent encoded sets, if valid
				char c1 = *(optr+1), c2 = *(optr+2);
				if(isxdigit(c1) && isxdigit(c2)){
					char digit = 16 * (c1 >= 'A' ? (c1 & 0xdf) - '7' : (c1 - '0'));
					digit += (c2 >= 'A' ? (c2 & 0xdf) - '7' : (c2 - '0'));
					*nptr++ = digit;
					optr += 2;
				} else {
					*nptr++ = '%';
				}
			} break;

			case '\0':
				// Push key or value if valid
				if((nptr-sptr) > 0){ lua_pushlstring(l, sptr, (nptr - sptr)); }

				// Push value, if needed
				if(lua_gettop(l) == (stack+1)){ lua_pushstring(l, ""); }

				// Set key/value if valid
				if(lua_gettop(l) == (stack+2)){ lua_rawset(l, stack); }

				// Finally, set the table
				lua_setglobal(l, "GET");
				return;
			break;

			default:
				*nptr++ = *optr;
			break;
		}
	}
}


// Parses fastcgi request
void LF_parserequest(lua_State *l, FCGX_Request *request, LF_response *response)
{
	lua_newtable(l);
	for(char **p = request->envp; *p; ++p){
		char *vptr = strchr(*p, '=');
		int keylen = (vptr - *p);

		lua_pushlstring(l, *p, keylen); // Push Key
		lua_pushstring(l, (vptr+1)); // Push Value
		lua_rawset(l, 1); // Set key/value into table

		if(keylen == 12 && memcmp(*p, "QUERY_STRING", 12) == 0){
			LF_parsequerystring(l, (vptr+1));
		}
	}
	lua_setglobal(l, "REQUEST");

	response->committed = 0;
	response->out = request->out;
	lua_pushstring(l, "RESPONSE");
	lua_pushlightuserdata(l, response);
	lua_rawset(l, LUA_REGISTRYINDEX);
}


static const char *LF_filereader(lua_State *l, void *data, size_t *size)
{
	LF_loaderdata *ld = data;

	*size = read(ld->fd, ld->buffer, LF_BUFFERSIZE);

	if(ld->total == 0 && *size > 3){
		if(memcmp(ld->buffer, LUA_SIGNATURE, 4) == 0){
			luaL_error(l, "Compiled bytecode not supported.");
		}
	}

	switch(*size){
		case 0: return NULL;
		case -1: luaL_error(l, strerror(errno));
		default:
			ld->total += *size;
			return ld->buffer;
	}
}


// Loads a lua file into a state
int LF_loadfile(lua_State *l)
{
	lua_getglobal(l, "REQUEST");

	int stack = lua_gettop(l);

	lua_pushstring(l, "SCRIPT_FILENAME");
	lua_rawget(l, stack);
	const char *path = lua_tostring(l, stack+1);

	lua_pushstring(l, "SCRIPT_NAME");
	lua_rawget(l, stack);
	const char *name = lua_tostring(l, stack+2);

	LF_loaderdata ld;
	ld.total = 0;
	ld.fd = open(path, O_RDONLY);
	if(ld.fd == -1){
		lua_pushstring(l, strerror(errno));
		return errno;
	}

	// Generate a string with an '=' followed by the script name
	// this ensures lua will generation a reasonable error
	size_t len = strlen(name) + 1;
	char scriptname[len + 1];
	scriptname[0] = '=';
	memcpy(&scriptname[1], name, len);
	
	int r = lua_load(l, &LF_filereader, &ld, scriptname);

	close(ld.fd);
	return (r == 0 ? 0 : ENOMSG);
}


// Closes a state
void LF_closestate(lua_State *l)
{
	lua_close(l);
}
