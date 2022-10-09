all: dataServer remoteClient

dataServer: server.cpp
	g++ -o dataServer server.cpp -std=c++11 -g -lpthread

remoteClient: client.cpp
	g++ -o remoteClient client.cpp -std=c++11 -g

clean:
	rm -f dataServer
	rm -f remoteClient
	rm -rf output/