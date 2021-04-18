#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <iostream>

using namespace std;

string credential = "dXNlcjpxd2VydHk=";;

int QueueLength = 5;

enum Error {
  NO_ERR, 
  FILE_NOT_FOUND, 
  INVALID_REQUEST, 
  UNAUTHORIZED
};

enum Param {
  NO_CONCURRENCY,
  NEW_PROCESS,
  NEW_THREAD,
  POOL_OF_THREADS
};

bool matchEnd (string const &str, string const &end) {
  if (str.length() >= end.length())
    return (0 == str.compare (str.length() - end.length(), end.length(), end));
  else return false;
}

bool matchStart (string const &str, string const &start) {
  if (str.length() >= start.length()) 
    return (0 == str.compare (0, start.length(), start));
  else return false;
}

bool validate (string const &str) {
  if (!matchStart(str, string("GET"))) return false;
  int pos = str.find(' ', str.find(' ') + 1);
  if (pos < 0) return false;
  if (!matchStart(str.substr(pos + 1), "HTTP/1.1")) return false;
  return true;
}

string getContentByHeader(string str, string header) {
  cout << str << endl;
  if (matchStart(str, "\r\n")) return string();
  if (matchStart(str, header)) {
    int pos = str.find(':') + 2;
    return str.substr(pos, str.find('\r') - pos);
  } else return getContentByHeader(str.substr(str.find('\n') + 1), header);
}

bool verify (string str) {
  string login = getContentByHeader(str, string("Authorization:"));
  if (!matchStart(login, string("Basic"))) return false;
  if (!credential.compare(login.substr(login.find(' ') + 1))) return false;
  return true;
}

int openFile(string fileName) {
  string realPath = string("http-root-dir");
  //cout << fileName.length() << endl;
  if (!fileName.compare("/")) realPath.append("/index.html");
  else realPath.append(fileName);
  if (access(realPath.c_str(), F_OK)) return -1;
  else return open(realPath.c_str(), O_RDONLY);
}

string parseFileName(string str) {
  int start = str.find(' ') + 1;
  int end = str.find(' ', start);
  return str.substr(start, end - start);
}

string parseInput(int skt) {
  string input = string();
  unsigned char newChar;
  int n;

  while(!matchEnd(input, string("\r\n\r\n")) &&
	  (n = read(skt, &newChar, sizeof(newChar) ) ) > 0 ) input += newChar;

  return input;
}

void writeOutput(int skt, string str) {
  write(skt, str.c_str(), str.length());
}

string initOutput(int error, string type) {
  string output = string();
  output.append("HTTP/1.1 ");
  switch (error) {
    case FILE_NOT_FOUND:
      output.append("404 File Not Found\r\n");
      break;
    case INVALID_REQUEST:
      output.append("400 Bad Request\r\n");
      break;
    case NO_ERR:
      output.append("200 Document follows\r\n");
      break;
    case UNAUTHORIZED:
      output.append("401 Unauthorized\r\n");
      output.append("WWW-Authenticate: Basic realm=\"myhttpd-cs252\"\r\n");
      break;
  }
  output.append("Server: CS 252 lab5\r\nContent-type: ");
  output.append(type);
  output.append("\r\n\r\n");
  switch (error) {
    case FILE_NOT_FOUND:
      output.append("I cant find it dumb dingus");
      break;
    case INVALID_REQUEST:
      output.append("I don't understand");
      break;
    case UNAUTHORIZED:
      output.append("Invalid username / password");
      break;
  }
  return output;
}

string addDoc(string output, int fd) {
  if (fd < 0) return output;

  unsigned char newChar;
  int n;

  while((n = read(fd, &newChar, sizeof(newChar) ) ) > 0 ) output += newChar;

  return output;
}

void process(int skt) {
  int err = NO_ERR;
  int fd = -1;
  string type = string("text/plain");
  string input = parseInput(skt);

  cout << input << endl;
  if (!verify(getContentByHeader(input, "Authorization:"))) err = UNAUTHORIZED;
  else if (!validate(input)) err = INVALID_REQUEST;
  else {
    string fileName = parseFileName(input);
    if ((fd = openFile(fileName)) < 0) err = FILE_NOT_FOUND;
    else if (matchEnd(fileName, string(".html"))) type = string("text/html");
    else if (matchEnd(fileName, string(".gif"))) type = string("image/gif");
  }
  string output = initOutput(err, type);
  writeOutput(skt, addDoc(output, fd));
}

int main(int argc, char * argv[]) {

  // Print usage if not enough arguments

  // Get the port from the arguments
  int port = argc > 1 && argv[argc - 1] ? atoi( argv[argc - 1] ) : 8006;
  
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

  // Put socket in listening mode and set the 
  // size of the queue of unprocessed connections
  error = listen( serverSocket, QueueLength);
  if ( error ) {
    perror("listen");
    exit( -1 );
  }

  while ( 1 ) {

    // Accept incoming connections
    struct sockaddr_in clientIPAddress;
    int alen = sizeof( clientIPAddress );
    int clientSocket = accept( serverSocket,
			      (struct sockaddr *)&clientIPAddress,
			      (socklen_t*)&alen);

    if ( clientSocket < 0 ) {
      perror( "accept" );
      exit( -1 );
    }
    cout << "accepted" << endl;
    // Process request.
    process( clientSocket );

    // Close socket
    close( clientSocket );
  }
}
