#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <iostream>

enum Error {
  NO_ERR, 
  FILE_NOT_FOUND, 
  INVALID_REQUEST
};

enum Param {
  NO_CONCURRENCY,
  NEW_PROCESS,
  NEW_THREAD,
  POOL_OF_THREADS
};

bool matchEnd (std::string const &str, std::string const &end) {
  if (str.length() >= end.length())
    return (0 == str.compare (str.length() - end.length(), end.length(), end));
  else return false;
}

bool matchStart (std::string const &str, std::string const &start) {
  if (str.length() >= start.length()) 
    return (0 == str.compare (0, start.length(), start));
  else return false;
}

bool validate (std::string const &str) {
  if (!matchStart(str, std::string("GET"))) return false;
  int pos = str.find(' ', str.find(' ') + 1);
  if (pos < 0) return false;
  if (!matchStart(str.substr(pos + 1), "HTTP/1.0")) return false;
  return true;
}

std::string parseInput(int skt) {
  std::string input = std::string();
  unsigned char newChar;
  int n;

  while(!matchEnd(input, std::string("\r\n\r\n")) &&
	  (n = read(skt, &newChar, sizeof(newChar) ) ) > 0 ) input += newChar;

  return input;
}

std::string initOutput(bool error, std::string type) {
  std::string output = std::string();
  output.append("HTTP/1.1 ");
  if (error) output.append("404 File Not Found\r\n");
  else output.append("200 Document follows\r\n");
  output.append("Server: CS 252 lab5\r\nContent-type: ");
  output.append(type);
  output.append("\r\n\r\n");
  if (error) output.append("I cant find it dumb dingus");
  return output;
}

std::string addDoc(std::string output, int fd) {
  unsigned char newChar;
  int n;

  while((n = read(fd, &newChar, sizeof(newChar) ) ) > 0 ) output += newChar;

  return output;
}

void process(int skt) {
  while(1) {
    int err = NO_ERR;
    std::string input = parseInput(skt);
    if (!validate(input))
  }
}

int main(int argc, char * argv[]) {
   // Print usage if not enough arguments
  if ( argc < 2 ) {
    fprintf( stderr, "%s", usage );
    exit( -1 );
  }
  
  // Get the port from the arguments
  int port = atoi( argv[argc - 1] );
  
  // Set the IP address and port for this server
  struct sockaddr_in serverIPAddress; 
  memset( &serverIPAddress, 0, sizeof(serverIPAddress) );
  serverIPAddress.sin_family = AF_INET;
  serverIPAddress.sin_addr.s_addr = INADDR_ANY;
  serverIPAddress.sin_port = htons((u_short) port);

  // Allocate a socket
  int serverSocket =  socket(PF_INET, SOCK_STREAM, 0);
  if ( serverSocket < 0) {
    perror("socket");
    exit( -1 );
  }

  // Set socket options to reuse port. Otherwise we will
  // have to wait about 2 minutes before reusing the sae port number
  int optval = 1; 
  int err = setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, 
		       (char *) &optval, sizeof( int ) );
   
  // Bind the socket to the IP address and port
  int error = bind( serverSocket,
		    (struct sockaddr *)&serverIPAddress,
		    sizeof(serverIPAddress) );
  if ( error ) {
    perror("bind");
    exit( -1 );
  }
}
