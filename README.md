This is a process manager that is intended to isolate different processes from each-oter in order
to safeguard from crashes and stucks from different utils. This manager has also the duty to
schedule said processes: start them at specific times, etc.

This manager will:
	- allways be running (for example as a daemon)
	- have a config file
	- have a ctrl binary
	- start/stop processes (restart them when they crash)
	- (maybe not?) hold shared memory locations
	- (maybe not?) hold db-s
	- hold comm channels
	- hold some mutexes-stuff
	- have a library to connect with?
