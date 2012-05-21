lua-fastcgi
===========

lua-fastcgi is a sandboxed Lua backend for FastCGI. That is, you can write
Lua scripts that serve up web pages. Options exist in lua-fastcgi.lua
to configure a fixed amount of memory, cpu usage, and output bytes
for each request. While sandboxed, lua-fastcgi supports a limited set
of functions. If sandboxing is disabled, lua-fastcgi loads the standard
libraries and users may load modules as needed.


compiling
---------

lua-fastcgi requires libfcgi and liblua to successfully compile.  lua-fastcgi
has been tested under Linux, specifically Ubuntu 10.10. Other versions of
Ubuntu will likely work without any changes. Other flavors of Linux should
work with little to no effort. Other Unix-like operating systems are untested
and unsupported, for now.


running
-------

lua-fastcgi reads lua-fastcgi.lua in the current working directory. If it
fails to read this file, it will assume certain defaults and continue anyway.
Configuration defaults are documented in the included lua-fastcgi.lua file.


lua-fastcgi has been tested with nginx, but will likely work with other
FastCGI compatible web servers with little effort. lua-fastcgi relies only
on SCRIPT_NAME and SCRIPT_FILENAME FastCGI variables passed to it. Lua scripts
can be configured inside of an nginx server directive as follows:

    location ~* \.lua$ {
        include /etc/nginx/fastcgi_params;
        fastcgi_pass 127.0.0.1:9222;
    }
