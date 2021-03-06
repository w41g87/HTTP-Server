#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <iostream>
#include <signal.h>
#include <pthread.h>
#include <dirent.h>
#include <cassert>
#include <queue>
#include <time.h>
#include <dlfcn.h>
#include <link.h>
#include <arpa/inet.h>

const char * usage =
"                                                               \n"
"me-server:                                                \n"
"                                                               \n"
"Simple HTTP server program     \n"
"                                                               \n"
"To use it in one window type:                                  \n"
"                                                               \n"
"   myhttpd [-f|-t|-p] [port]\n"
"                                                               \n"
"Where 1024 < port < 65536.             \n"
"                                                               \n"
"In another window type:                                       \n"
"                                                               \n"
"   telnet <host> <port>                                        \n"
"                                                               \n"
"where <host> is the name of the machine where myhttpd is \n"
"running. <port> is the port number you used when you run myhttpd.\n"
"                                                               \n";

using namespace std;

typedef void (*httprunfunc)(int ssock, const char* querystring);

string credential = "dXNlcjpxd2VydHk=";

int QueueLength = 5;

pthread_t thread[5];

int numThreads = 0;

int serverSocket;

time_t startTime;

double minTime, maxTime;

int numReq, fdLog;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

string minReq, maxReq;

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

enum Request {
  INVALID, 
  GET, 
  POST
};

enum Operation {
  DOC,
  DIRECTORY,
  STAT,
  EXE, 
  SO
};

enum Sort {
  NAME, 
  SIZE, 
  MOD_TIME, 
  CREAT_TIME
};

enum Order {
  ASC, 
  DESC
};

enum FileType {
  UNKNOWN, 
  FOLDER, 
  TEXT, 
  TAR, 
  BINARY, 
  AUDIO, 
  VIDEO, 
  IMAGE
};

int concur = NO_CONCURRENCY;

class Document {
  public:
    Document() {
      _type = UNKNOWN;
    }
    string _name;
    off_t _size;
    int _type;
    string modTime() { return string(ctime(&_mtime)); }
    string creatTime() { return string(ctime(&_ctime)); }
    time_t _mtime;
    time_t _ctime;

};

void pipeHandler(int signum) {
  cout << "SIGPIPE" << endl;
}

void chldHandler(int signum) {
  int e;
  while((e = wait(NULL)) > 0) if (isatty(0)) printf("%d exited\n", e);
}

void intHandler(int signum) {
  cout << "interrupted" << endl;
  switch (concur) {
    case NEW_PROCESS:
      wait(NULL);
      break;
    case POOL_OF_THREADS:
      //pthread_exit(0);
      for (int i = 0; i < 5; i++) pthread_cancel(thread[i]);
      break;
  }
  close(serverSocket);
  close(fdLog);
  exit(0);
}

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

bool is_dir(string path) {
  struct stat st;
  stat(path.c_str(), &st);
  return S_ISDIR(st.st_mode);
}

int requestType (string const &str) {
  int pos = str.find(' ', str.find(' ') + 1);
  if (pos < 0) return INVALID;
  if (!matchStart(str.substr(pos + 1), "HTTP/1.1")) return INVALID;
  if (matchStart(str, string("GET"))) return GET;
  else if (matchStart(str, string("POST"))) return POST;
  else return INVALID;
}

int opType (string str) {
  if (matchStart(str, string("/cgi-bin/"))) {
    if (matchEnd(str, ".so")) return SO;
    else return EXE;
  } else if (!str.compare("/stat")) return STAT;
  else return DOC;
}

int getFileType(const char * name) {
  string str = string(name);
  if (matchEnd(str, ".txt") || matchEnd(str, ".c") || matchEnd(str, ".cc") ||
    matchEnd(str, ".pl") || matchEnd(str, ".tcl") || matchEnd(str, ".h")) return TEXT;
  else if (matchEnd(str, ".xbm") || matchEnd(str, ".gif") || matchEnd(str, ".jpg") ||
    matchEnd(str, ".jpeg") || matchEnd(str, ".png") || matchEnd(str, ".bmp") ||
    matchEnd(str, ".tiff")) return IMAGE;
  else if (matchEnd(str, ".o") || matchEnd(str, ".so") || matchEnd(str, ".bin")) return BINARY;
  else if (matchEnd(str, ".wav") || matchEnd(str, ".flac") || matchEnd(str, ".mp3") ||
    matchEnd(str, ".m4a") || matchEnd(str, ".ogg") || matchEnd(str, ".wma")) return AUDIO;
  else if (matchEnd(str, ".mp4") || matchEnd(str, ".mpeg") || matchEnd(str, ".avi") ||
    matchEnd(str, ".wmv") || matchEnd(str, ".flv") || matchEnd(str, ".webm")) return VIDEO;
  else if (matchEnd(str, ".tar")) return TAR;
  else return UNKNOWN;
}

string dirToTable(DIR * dir, string dirPath, string linkPath, int sort, int order) {
  cout << "start of table body generation" << endl;

  auto comp = [sort, order](Document a, Document b) {
    switch (sort) {
      case NAME:
        return (order == ASC && a._name.compare(b._name) > 0) || (order == DESC && a._name.compare(b._name) <= 0);
        break;
      case SIZE:
        return (order == ASC && a._size > b._size) || (order == DESC && a._size <= b._size);
        break;
      case MOD_TIME:
        return (order == ASC && a._mtime > b._mtime) || (order == DESC && a._mtime <= b._mtime);
        break;
      case CREAT_TIME:
        return (order == ASC && a._ctime > b._ctime) || (order == DESC && a._ctime <= b._ctime);
        break;
    }
  };
  
  struct dirent * ent;
  priority_queue<Document, vector<Document>, decltype(comp)> q(comp);
  string output = string();

  cout << "start of pq population" << endl;

  while ((ent = readdir(dir)) != NULL) {
    char *name = ent->d_name;
    string filePath = string(dirPath);
    filePath.append("/");
    filePath.append(name);
    unsigned char type = ent->d_type;
    if (strcmp(name, ".") && strcmp(name, "..")) {
        Document doc = Document();
        doc._name = string(name);
        if (type == DT_DIR) doc._type = FOLDER;
        else doc._type = getFileType(name);
        struct stat st;
        stat(filePath.c_str(), &st);
        cout << filePath << endl;
        doc._size = st.st_size;
        doc._mtime = st.st_mtim.tv_sec;
        doc._ctime = st.st_ctim.tv_sec;
        cout << st.st_size << endl;
        q.push(doc);
    } 
  }
  cout << "pq populated" << endl;
  while(!q.empty()) {
    Document doc = q.top();
    output.append("<tr>\n");
      output.append("<td valign=\"top\">\n");
        output.append("<img src=\"/icons/");
        switch (doc._type) {
          case UNKNOWN:
            output.append("unknown.gif\" alt=\"[FILE]\">\n");
            break;
          case FOLDER:
            output.append("menu.gif\" alt=\"[DIR]\">\n");
            break;
          case TEXT:
            output.append("text.gif\" alt=\"[TXT]\">\n");
            break;
          case TAR:
            output.append("tar.gif\" alt=\"[TAR]\">\n");
            break;
          case BINARY:
            output.append("binary.gif\" alt=\"[BIN]\">\n");
            break;
          case AUDIO:
            output.append("sound.gif\" alt=\"[AUDIO]\">\n");
            break;
          case VIDEO:
            output.append("movie.gif\" alt=\"[VIDEO]\">\n");
            break;
          case IMAGE:
            output.append("image.gif\" alt=\"[IMAGE]\">\n");
            break;
        }
      output.append("</td>\n");

      output.append("<td>\n");
        output.append("<a href=\"");
        if (!matchEnd(linkPath, "/")) output.append(linkPath + "/");
        output.append(doc._name);
        if (doc._type == FOLDER) output.append("/");
        output.append("\">" + doc._name);
        if (doc._type == FOLDER) output.append("/");
        output.append("</a>\n");
      output.append("</td>\n");

      output.append("<td align=\"right\">\n");
        output.append(doc._type == FOLDER ? "  - " : to_string(doc._size));
      output.append("B\n</td>\n");

      output.append("<td align=\"right\">\n");
        output.append(doc.modTime());
      output.append("\n</td>\n");

      output.append("<td align=\"right\">\n");
        output.append(doc.creatTime());
      output.append("\n</td>\n");

    output.append("</tr>\n");
    q.pop();
  }

  cout << "end of table body population (still in function)" << endl;
  return output;
}

string genHtmlFromDir(string realPath, string linkPath) {
  DIR * dir = opendir(realPath.c_str());
  if (!dir) return string();
  string query = string(getenv("QUERY_STRING"));
  int sort, order, pos = 0;

  do {
    query = query.substr(pos);
    if (matchStart(query, "sort=")) {
      query = query.substr(query.find('=') + 1);
      if (matchStart(query, "name")) sort = NAME;
      else if (matchStart(query, "mod-time")) sort = MOD_TIME;
      else if (matchStart(query, "creat-time")) sort = CREAT_TIME;
      else if (matchStart(query, "size")) sort = SIZE;
      else return string();
    } else if (matchStart(query, "order=")) {
      query = query.substr(query.find('=') + 1);
      if (matchStart(query, "asc")) order = ASC;
      else if (matchStart(query, "desc")) order = DESC;
      else return string();
    }
    pos = query.find('&') + 1;
  } while(pos - 1 != string::npos);
  
  cout << "start of HTML generation" << endl;
  string html = string("<!DOCTYPE html>\n");
  html.append("<html>\n");
    html.append("<head>\n");
      html.append("<meta charset=\"UTF-8\">\n");
      html.append("<title>Directory Content of " + realPath.substr(realPath.find('/') + 1) + "</title>\n");
    html.append("</head>\n");

    html.append("<body>\n");
      html.append("<h1>Directory Content of " + linkPath + "</h1>\n");
      html.append("<table>\n");
      html.append("<tbody>\n");

        html.append("<tr>\n");

          html.append("<th valign=\"top\">\n");
          html.append("<img src=\"/icons/blue_ball.gif\" alt=\"[ICO]\">\n");
          html.append("</th>\n");

          html.append("<th>\n");
          if (sort == NAME) {
            if (order == ASC) html.append("<a href=\"?sort=name&order=desc\">Name &#9650;</a>\n");
            else if (order == DESC) html.append("<a href=\"?sort=name&order=asc\">Name &#9660;</a>\n");
          } else html.append("<a href=\"?sort=name&order=asc\">Name</a>\n");
          html.append("</th>\n");

          html.append("<th>\n");
          if (sort == SIZE) {
            if (order == ASC) html.append("<a href=\"?sort=size&order=desc\">Size &#9650;</a>\n");
            else if (order == DESC) html.append("<a href=\"?sort=size&order=asc\">Size &#9660;</a>\n");
          } else html.append("<a href=\"?sort=size&order=asc\">Size</a>\n");
          html.append("</th>\n");

          html.append("<th>\n");
          if (sort == MOD_TIME) {
            if (order == ASC) html.append("<a href=\"?sort=mod-time&order=desc\">Last Modified &#9650;</a>\n");
            else if (order == DESC) html.append("<a href=\"?sort=mod-time&order=asc\">Last Modified &#9660;</a>\n");
          } else html.append("<a href=\"?sort=mod-time&order=asc\">Last Modified</a>\n");
          html.append("</th>\n");

          html.append("<th>\n");
          if (sort == CREAT_TIME) {
            if (order == ASC) html.append("<a href=\"?sort=creat-time&order=desc\">Created &#9650;</a>\n");
            else if (order == DESC) html.append("<a href=\"?sort=creat-time&order=asc\">Created &#9660;</a>\n");
          } else html.append("<a href=\"?sort=creat-time&order=asc\">Created</a>\n");
          html.append("</th>\n");

        html.append("</tr>\n");
cout << "end of table head generation" << endl;
        html.append("<tr>\n");

          html.append("<th colspan=\"5\">\n");
          html.append("<hr>\n");
          html.append("</th>\n");

        html.append("</tr>\n");

        html.append("<tr>\n");

          html.append("<td valign=\"top\">\n");
          html.append("<img src=\"/icons/index.gif\" alt=\"[PARENTDIR]\">\n");
          html.append("</td>\n");

          html.append("<td>\n");
          html.append("<a href=\"" + linkPath.substr(0, linkPath.find_last_of('/', linkPath.size() - 2) + 1) +
            "\">Parent Directory</a>\n");
          html.append("</td>\n");

          html.append("<td align=\"right\">\n");
          html.append("  - ");
          html.append("</td>\n");

          html.append("<td>\n");
          html.append("&nbsp;");
          html.append("</td>\n");

          html.append("<td>\n");
          html.append("&nbsp;");
          html.append("</td>\n");

        html.append("</tr>\n");

        html.append(dirToTable(dir, realPath, linkPath, sort, order));
cout << "end of table body generation" << endl;
        html.append("<tr>\n");

          html.append("<th colspan=\"5\">\n");
          html.append("<hr>\n");
          html.append("</th>\n");

        html.append("</tr>\n");
      
      html.append("</tbody>");
      html.append("</table>\n");
    html.append("</body>\n");

  html.append("</html>\n");
  
  cout << "end of HTML generation" << endl;
  //closedir(dir);
  return html;
}

string getContentByHeader(string str, string header) {
  // cout << (int)str.at(0) << endl;
  // cout << (int)str.at(1) << endl;
  // cout << str.length() << endl;
  // cout << (bool)matchStart(str, string("\r\n")) << endl;
  // sleep(1);
  if (matchStart(str, string("\r\n")) != 0 || str.length() < 2) return string("");
  else if (matchStart(str, header)) {
    int pos = str.find(':') + 2;
    return str.substr(pos, str.find('\r') - pos);
  } else return getContentByHeader(str.substr(str.find('\n') + 1), header);
}

bool verify (string str) {
  string login = getContentByHeader(str, string("Authorization:"));
  // cout << login << endl;
  // cout << login.substr(login.find(' ') + 1) << endl;
  if (!matchStart(login, string("Basic"))) return false;
  if (credential.compare(login.substr(login.find(' ') + 1))) return false;
  cout << "verified" << endl;
  return true;
}

string extractMid(string str) {
  int start = str.find(' ') + 1;
  int end = str.find(' ', start);
  return str.substr(start, end - start);
}

string extractFileName(string str) {
  int pos = str.find('?');
  return pos == -1 ? str : str.substr(0, pos);
}

string getQuery(string str) {
  int pos = str.find('?');
  return pos == -1 ? string() : str.substr(pos + 1);
}

string postQuery(int skt) {
  string query = string("");
  unsigned char newChar;
  int n;

  while((n = read(skt, &newChar, sizeof(newChar) ) ) > 0 ) query += newChar;

  return query;
}

string parseInput(int skt) {
  string input = string("");
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
  clock_t reqBegin, reqEnd;
  time_t now;

  reqBegin = clock();

  int req = INVALID;
  int err = NO_ERR;
  int fd = -1;
  int pid = -1;
  void * lib;

  string type = string("text/plain");
  string input = parseInput(skt);
  string query;
  string output, mid;

  cout << input << endl;
  // for (int i = 0; i < input.length(); i ++) {
  //   cout << (int)input.at(i) << endl;
  // }
  
  if (!verify(input)) writeOutput(skt, initOutput(UNAUTHORIZED, type));
  else if ((req = requestType(input)) == INVALID) writeOutput(skt, initOutput(INVALID_REQUEST, type));
  else {
    mid = extractMid(input);
    string fileName = extractFileName(mid);
    string realPath = string("http-root-dir");
    
    int op = opType(fileName);

    if (req == GET) query = getQuery(mid);
    else if (req == POST) query = postQuery(skt);

    if (op == DOC) {
      if (!matchStart(fileName, "/icons")) realPath.append("/htdocs");
      realPath.append(fileName.compare("/") ? fileName : "/index.html");
      if (is_dir(realPath)) op = DIRECTORY;
    } else realPath.append(fileName);

    if (matchEnd(realPath, string("/"))) realPath.pop_back();

    pthread_mutex_lock(&mutex);
    writeOutput(fdLog, "File or dir requested: ");
    writeOutput(fdLog, realPath);
    writeOutput(fdLog, "\n");
    pthread_mutex_unlock(&mutex);

    if (realPath.find("..") != string::npos) {
      writeOutput(skt, initOutput(INVALID_REQUEST, type));
    } else if (access(realPath.c_str(), F_OK) && op != STAT) {
      writeOutput(skt, initOutput(FILE_NOT_FOUND, type));
    } else {
      switch (op) {
        case DOC:
          cout << realPath << endl;
          fd = open(realPath.c_str(), O_RDONLY);
          if (matchEnd(realPath, string(".html"))) type = string("text/html");
          else if (matchEnd(realPath, string(".gif"))) type = string("image/gif");
          else if (matchEnd(realPath, string(".svg"))) type = string("image/svg+xml");

          output = initOutput(err, type);
          writeOutput(skt, addDoc(output, fd));
          close(fd);
          break;
        case DIRECTORY:
          if (setenv("QUERY_STRING", query.empty() ? "sort=name&order=asc" : query.c_str(), 1)) perror("setenv");
          type = string("text/html");
          
          if (genHtmlFromDir(realPath, fileName).empty()) err = INVALID_REQUEST;
          output = initOutput(err, type);

          writeOutput(skt, output.append(genHtmlFromDir(realPath, fileName)));
          
          break;
        case EXE:
          cout << "query: " + query << endl;
          if (setenv("QUERY_STRING", query.c_str(), 1)) perror("setenv");
          setenv("REQUEST_METHOD", req == GET ? "GET" : "POST", 1);

          pid = fork();
          if (pid == 0) {
            close(2);
            close(0);
            dup2(skt, 1);
            printf("HTTP/1.1 200 Document follows\r\nServer: CS 252 lab5\r\n");
            execlp(realPath.c_str(), NULL);
          }
          break;
        case SO:
          lib = dlopen( realPath.c_str(), RTLD_LAZY );

          if ( lib == NULL ) {
            fprintf( stderr, "%s not found\n", realPath.c_str());
            perror( "dlopen");
            exit(1);
          }

          // Get function to print hello
          httprunfunc httprun;

          httprun = (httprunfunc) dlsym( lib, "httprun");
          if ( httprun == NULL ) {
            perror( "dlsym: httprun not found:");
            exit(1);
          }
          writeOutput(skt, string("HTTP/1.1 200 Document follows\r\nServer: CS 252 lab5\r\n"));
          // Call the function
          httprun( skt, query.c_str());

          break;
        case STAT:
          output = initOutput(err, type);
          output.append("Name: Philip Jin\r\n");

          time(&now);
          output.append("Server uptime: ");
          output.append(to_string((int)difftime(now, startTime)));
          output.append(" sec\r\nMinimum service time: ");
          output.append(to_string(minTime));
          output.append(" sec\r\nURL request: " + minReq);
          output.append("\r\nMaximum service time: ");
          output.append(to_string(maxTime));
          output.append(" sec\r\nURL request: " + maxReq);
          writeOutput(skt, output);
          break;
      }
    }
  }
  
  cout << output << endl;
  close(skt);
  reqEnd = clock();
  double diff = (double)(reqEnd - reqBegin) / CLOCKS_PER_SEC;
  if (diff > maxTime) {
    maxTime = diff;
    maxReq = string(mid);
  }
  if (diff < minTime || minTime == 0) {
    minTime = diff;
    minReq = string(mid);
  }
}

void atomic() {
  int error;

  while ( 1 ) {
    // Accept incoming connections
    struct sockaddr_in clientIPAddress;
    int alen = sizeof( clientIPAddress );
    int clientSocket = accept( serverSocket,
			      (struct sockaddr *)&clientIPAddress,
			      (socklen_t*)&alen);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(clientIPAddress.sin_addr), ip, INET_ADDRSTRLEN);
    pthread_mutex_lock(&mutex);
    writeOutput(fdLog, "Host IP Address: ");
    writeOutput(fdLog, string(ip));
    writeOutput(fdLog, "\r\n");
    numReq++;
    pthread_mutex_unlock(&mutex);

    if ( clientSocket < 0 ) {
      perror( "accept" );
      exit( -1 );
    }
    switch (concur) {
      case NO_CONCURRENCY:
      case POOL_OF_THREADS:
        process( clientSocket );
        break;
      case NEW_PROCESS: 
        cout << "fork" << endl;
        error = fork();
        if (!error) {
          process(clientSocket);
          exit(0);
        }
        close( clientSocket );
        break;
      case NEW_THREAD:
        pthread_t thread;
        pthread_attr_t attr;

        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        cout << "thread" << endl;
        pthread_create(&thread, &attr, (void * (*)(void *)) process, (void*)clientSocket);
        break;
    }
  }
}

int main(int argc, char * argv[]) {
  time(&startTime);

  fdLog = open("http-root-dir/htdocs/log", O_RDWR | O_APPEND | O_CREAT);

  int port = 8006;

  if (argc == 2) {
    if (!strcmp(argv[1], "-f")) concur = NEW_PROCESS;
    else if (!strcmp(argv[1], "-t")) concur = NEW_THREAD;
    else if (!strcmp(argv[1], "-p")) concur = POOL_OF_THREADS;
    else if (!strcmp(argv[1], "--help")) {
      cout << usage << endl;
      exit(0);
    } else {
      try {
        port = stoi(string(argv[1]));
        if (port <= 1024 || port >= 65536) {
          cout << usage << endl;
          exit(0);
        }
      } catch (invalid_argument e) {
        cout << usage << endl;
        exit(0);
      }
    }
  } else if (argc == 3) {
    if (!strcmp(argv[1], "-f")) concur = NEW_PROCESS;
    else if (!strcmp(argv[1], "-t")) concur = NEW_THREAD;
    else if (!strcmp(argv[1], "-p")) concur = POOL_OF_THREADS;
    else {
      cout << usage << endl;
      exit(0);
    }

    try {
      port = stoi(string(argv[2]));
      if (port <= 1024 || port >= 65536) {
        cout << usage << endl;
        exit(0);
      }
    } catch (invalid_argument e) {
      cout << usage << endl;
      exit(0);
    }
  } else if (argc > 3) {
    cout << usage << endl;
    exit(0);
  }

  // Print usage if not enough arguments


  // Signal handling for SIGPIPE
  struct sigaction pipe, chld, c;
  pipe.sa_handler = pipeHandler;
  sigemptyset(&pipe.sa_mask);
  pipe.sa_flags = SA_RESTART;
  
  chld.sa_handler = chldHandler;
  sigemptyset(&chld.sa_mask);
  chld.sa_flags = SA_RESTART;

  c.sa_handler = intHandler;
  sigemptyset(&c.sa_mask);
  c.sa_flags = SA_RESTART;

  if(sigaction(SIGPIPE, &pipe, NULL)){
      perror("sigaction");
      exit(2);
  }

  // Signal handling for child exiting

  if(sigaction(SIGCHLD, &chld, NULL)){
      perror("sigaction");
      exit(2);
  }

  if(sigaction(SIGINT, &c, NULL)){
      perror("sigaction");
      exit(2);
  }


  // Get the port from the arguments
  argc > 1 && argv[argc - 1] ? atoi( argv[argc - 1] ) : 8006;
  
  // Set the IP address and port for this server
  struct sockaddr_in serverIPAddress; 
  memset( &serverIPAddress, 0, sizeof(serverIPAddress) );
  serverIPAddress.sin_family = AF_INET;
  serverIPAddress.sin_addr.s_addr = INADDR_ANY;
  serverIPAddress.sin_port = htons((u_short) port);

  // Allocate a socket
  serverSocket =  socket(PF_INET, SOCK_STREAM, 0);
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
  if (concur == POOL_OF_THREADS) {
    for (int i = 0; i < 5; i++) {
      numThreads++;
      pthread_create(&thread[i], NULL, (void * (*)(void *))atomic, NULL);
    }
  }
  atomic();
}
