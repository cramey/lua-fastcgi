return {
	-- IP/Port to listen on
	-- Default: "127.0.0.1:9222"	
	listen   = "127.0.0.1:9222",

	-- How many connections should be back-logged by each thread
	-- Default: 100
	backlog  = 100,

	-- Number of threads to spin off. Usually one per CPU
	-- (plus hardware threads) is a good idea
	-- Default: 4
	threads  = 1,

	-- Indicates if states should be sandboxed (i.e. Denied access to
	-- file system resources or system-level functions)
	-- Default: true
	sandbox  = true,

	-- Maximum amount of memory a state may consume or 0 for no limit
	-- Default: 65536
	mem_max  = 65536,

	-- Maximum amount of CPU time, in both seconds and picoseconds for each
	-- execution. If cpu_sec and cpu_usec are 0, there is no limit
	-- Default: 5000000
	cpu_usec = 500000,
	-- Default: 0
	cpu_sec  = 0,

	-- Limit page output to x bytes or 0 for unlimited
	-- Default: 65536
	output_max = 65536,

	-- Default content type returned in header
	content_type = "text/html; charset=iso-8859-1"
}
