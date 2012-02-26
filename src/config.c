#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <lua5.1/lua.h>
#include <lua5.1/lauxlib.h>
#include <lua5.1/lualib.h>

#include "config.h"


// Checks if a file exists
static int LF_fileexists(char *path)
{
	if(access(path, F_OK) == 0){ return 1; }
	else { return 0; }
}


// Create configuration with default settings
LF_config *LF_createconfig()
{
	LF_config *c = malloc(sizeof(LF_config));
	if(c == NULL){ return NULL; }

	// Default settings
	c->listen = "127.0.0.1:9222";
	c->backlog = 100;
	c->threads = 1;
	c->sandbox = 1;
	c->mem_max = 65536;
	c->output_max = 65536;
	c->cpu_usec = 500000;
	c->cpu_sec  = 0;

	return c;
}


// Load configuration
int LF_loadconfig(LF_config *cfg, char *path)
{
	if(!LF_fileexists(path)){ return 1; }

	lua_State *l = luaL_newstate();
	if(luaL_loadfile(l, path) || lua_pcall(l,0,1,0)){
		printf("%s\n", lua_tostring(l, -1));
		lua_close(l);
		return 1;
	}

	lua_settop(l, 1);

	if(lua_istable(l, 1)){
		lua_pushstring(l, "listen");
		lua_rawget(l, 1);
		if(lua_isstring(l, 2)){
			size_t len = 0;
			const char *str = lua_tolstring(l, 2, &len);

			if(len > 0){
				cfg->listen = malloc(len+1);
				memcpy(cfg->listen, str, len+1);
			}
		}

		lua_settop(l, 1);

		lua_pushstring(l, "backlog");
		lua_rawget(l, 1);
		if(lua_isnumber(l, 2)){ cfg->backlog = lua_tonumber(l, 2); }

		lua_settop(l, 1);

		lua_pushstring(l, "threads");
		lua_rawget(l, 1);
		if(lua_isnumber(l, 2)){ cfg->threads = lua_tonumber(l, 2); }

		lua_settop(l, 1);

		lua_pushstring(l, "sandbox");
		lua_rawget(l, 1);
		if(lua_isboolean(l, 2)){ cfg->sandbox = lua_toboolean(l, 2); }

		lua_settop(l, 1);

		lua_pushstring(l, "mem_max");
		lua_rawget(l, 1);
		if(lua_isnumber(l, 2)){ cfg->mem_max = lua_tonumber(l, 2); }

		lua_settop(l, 1);

		lua_pushstring(l, "cpu_usec");
		lua_rawget(l, 1);
		if(lua_isnumber(l, 2)){ cfg->cpu_usec = lua_tonumber(l, 2); }

		lua_settop(l, 1);

		lua_pushstring(l, "cpu_sec");
		lua_rawget(l, 1);
		if(lua_isnumber(l, 2)){ cfg->cpu_sec = lua_tonumber(l, 2); }

		lua_settop(l, 1);

		lua_pushstring(l, "output_max");
		lua_rawget(l, 1);
		if(lua_isnumber(l, 2)){ cfg->output_max = lua_tonumber(l, 2); }

		lua_settop(l, 1);

		lua_pushstring(l, "content_type");
		lua_rawget(l, 1);
		if(lua_isstring(l, 2)){
			size_t len = 0;
			const char *str = lua_tolstring(l, 2, &len);

			if(len > 0){
				cfg->content_type = malloc(len+1);
				memcpy(cfg->content_type, str, len+1);
			}
		}
	}

	lua_close(l);
	return 0;
}
