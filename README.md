# server-client-fileTransfer
This is a Server-Client project where the Server listens for connections to a given port and sends copies of directories asked by clients using sockets,threads and low-level IO.
<br >
<br >
For each new client the server creates a new Communication thread which communicates with the Worker threads using the Producer-Consumer model. The Communication thread(Producer) receives a directory filepath from the client and then finds all the files and folders of the directory recursively and adds each path to the execution queue, where Worker threads(Consumer) take the filepath from the queue and copy/send the data from every file to the client through sockets. 
<br >
<br >
The server is able to serve multiple clients at the same time with the use of mutexes and conditional variables. We assume that the client knows the file structure of the server. The client stops when it receives the contents of the directory it asked for. The directory received is in the "output/" directory which is created on runtime.
## Execution

### Arguments
#### Server
port: Port number that the server listens to.
thread_pool_size: Number of worker threads.
queue_size: Size of the execution queue.
block_size: The size of bytes send through sockets each time.
#### Client
server_ip: Server's IP.
server_port: The port that the server listens to.
directory: The filepath to the directory to be copied and send by the server.

To compile:
```
make
```
To execute Server:
```
./dataServer -p <port> -s <thread_pool_size> -q <queue_size> -b <block_size>
```
To execute Client:
```
./remoteClient -i <server_ip> -p <server_port> -d <directory>
```
To Delete executables / output directory: 
```
make clean
```