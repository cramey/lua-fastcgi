#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <fcgiapp.h>

#include <lua5.1/lua.h>
#include <lua5.1/lauxlib.h>
#include <lua5.1/lualib.h>

#include "lua.h"
#include "lfuncs.h"


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

	if(limits->memory){
		lua_pushstring(l, "MEMORY_LIMIT");
		lua_pushlightuserdata(l, &limits->memory);
		lua_rawset(l, LUA_REGISTRYINDEX);

		lua_setallocf(l, &LF_limit_alloc, &limits->memory);
	}
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
		LF_nilglobal(l, "load");
		LF_nilglobal(l, "xpcall");
		LF_nilglobal(l, "pcall");
		LF_nilglobal(l, "module");
		LF_nilglobal(l, "require");

		// Override unsafe functions
		lua_register(l, "loadstring", &LF_loadstring);
		lua_register(l, "loadfile", &LF_loadfile);
		lua_register(l, "dofile", &LF_dofile);
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
static void LF_parsequerystring(lua_State *l, char *query_string, char *table)
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
				lua_setglobal(l, table);
				return;
			break;

			default:
				*nptr++ = *optr;
			break;
		}
	}
}


// Parses fastcgi request
void LF_parserequest(lua_State *l, FCGX_Request *request, LF_state *state)
{
	uintmax_t content_length = 0;
	char *content_type = NULL;

	state->committed = 0;
	state->response = request->out;
	lua_pushstring(l, "STATE");
	lua_pushlightuserdata(l, state);
	lua_rawset(l, LUA_REGISTRYINDEX);

	lua_newtable(l);
	for(char **p = request->envp; *p; ++p){
		char *vptr = strchr(*p, '=');
		int keylen = (vptr - *p);

		lua_pushlstring(l, *p, keylen); // Push Key
		lua_pushstring(l, (vptr+1)); // Push Value
		lua_rawset(l, 1); // Set key/value into table
		
		switch(keylen){
			case 11:
				if(memcmp(*p, "SCRIPT_NAME", 11) == 0){
					lua_pushstring(l, "SCRIPT_NAME");
					lua_pushlightuserdata(l, (vptr+1));
					lua_rawset(l, LUA_REGISTRYINDEX);
				}
			break;

			case 12: 
				if(memcmp(*p, "QUERY_STRING", 12) == 0){
					LF_parsequerystring(l, (vptr+1), "GET");
				} if(memcmp(*p, "CONTENT_TYPE", 12) == 0){
					content_type = (vptr+1);
				}
			break;

			case 13:
				if(memcmp(*p, "DOCUMENT_ROOT", 13) == 0){
					lua_pushstring(l, "DOCUMENT_ROOT");
					lua_pushlightuserdata(l, (vptr+1));
					lua_rawset(l, LUA_REGISTRYINDEX);
				}
			break;

			case 14:
				if(memcmp(*p, "CONTENT_LENGTH", 14) == 0){
					content_length = strtoumax((vptr+1), NULL, 10);
				}
			break;

			case 15:
				if(memcmp(*p, "SCRIPT_FILENAME", 15) == 0){
					lua_pushstring(l, "SCRIPT_FILENAME");
					lua_pushlightuserdata(l, (vptr+1));
					lua_rawset(l, LUA_REGISTRYINDEX);
				}
			break;
		}
	}
	lua_setglobal(l, "REQUEST");

	if(content_length > 0 && content_type != NULL && memcmp(content_type, "application/x-www-form-urlencoded", 33) == 0){
		char *content = lua_newuserdata(l, content_length+1);
		int r = FCGX_GetStr(
			content, (content_length > INT_MAX ? INT_MAX : content_length),
			request->in
		);
		*(content + r) = 0; // Add NUL byte at end for proper string
		LF_parsequerystring(l, content, "POST");
		lua_pop(l, 1);
	}
}


// Load script by name and path
int LF_fileload(lua_State *l, const char *name, char *scriptpath)
{
	char *script = NULL;
	int fd = -1, r = 0;
	struct stat sb;
	
	if(scriptpath == NULL){ return LF_ERRNOPATH; }
	if(name == NULL){ return LF_ERRNONAME; }

	// Generate a string with an '=' followed by the script name
	// this ensures lua will generation a reasonable error
	size_t namelen = strlen(name);
	char scriptname[namelen+2];
	scriptname[0] = '=';
	memcpy(&scriptname[1], name, namelen+1);

	if((fd = open(scriptpath, O_RDONLY)) == -1){ goto errorL; }
	
	if(fstat(fd, &sb) == -1){ goto errorL; }
	
	if((script = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0)) == NULL){
		goto errorL;
	}
	
	if(madvise(script, sb.st_size, MADV_SEQUENTIAL) == -1){ goto errorL; }
	
	if(sb.st_size > 3 && memcmp(script, LUA_SIGNATURE, 4) == 0){
		r = LF_ERRBYTECODE;
	} else {
		switch(luaL_loadbuffer(l, script, sb.st_size, scriptname)){
			case LUA_ERRSYNTAX: r = LF_ERRSYNTAX; break;
			case LUA_ERRMEM: r = LF_ERRMEMORY; break;
		}
	}

	if(script != NULL){ munmap(script, sb.st_size); }
	if(fd != -1){ close(fd); }
	return r;

	errorL:
	if(script != NULL){ munmap(script, sb.st_size); }
	if(fd != -1){ close(fd); }
	switch(errno){
		case EACCES: return r = LF_ERRACCESS;
		case ENOENT: return r = LF_ERRNOTFOUND;
		case ENOMEM: return r = LF_ERRMEMORY;
		default: return r = LF_ERRANY;
	}
	return r;
}


// Loads script specified in registryindex into lua state
int LF_loadscript(lua_State *l)
{
	lua_pushstring(l, "SCRIPT_FILENAME");
	lua_rawget(l, LUA_REGISTRYINDEX);
	char *scriptpath = lua_touserdata(l, 1); 
	lua_pop(l, 1);

	lua_pushstring(l, "SCRIPT_NAME");
	lua_rawget(l, LUA_REGISTRYINDEX);
	char *name = lua_touserdata(l, 1);
	lua_pop(l, 1);

	return LF_fileload(l, name, scriptpath);
}


// Closes a state
void LF_closestate(lua_State *l)
{
	lua_close(l);
}
