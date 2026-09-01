# MemSync
When multiple processes on the same machine access a cache through sockets, each request may involve system calls, serialization, protocol handling, and additional data copying. For local processes, some of this overhead can be avoided. This project proposes a small proof of concept key value cache based on POSIX shared memory. 
