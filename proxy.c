/*
 * proxy.c - Web proxy for COMPSCI 512
 *
 */

#include <stdio.h>
#include "csapp.h"
#include <pthread.h>

#define   FILTER_FILE   "proxy.filter"
#define   LOG_FILE      "proxy.log"
#define   DEBUG_FILE	"proxy.debug"


/*============================================================
 * function declarations
 *============================================================*/

int  find_target_address(char * uri,
			 char * target_address,
			 char * path,
			 int  * port);


void  format_log_entry(char * logstring,
		       int sock,
		       char * uri,
		       int size);
		       
void parseAddress(char* url, char* host, char** file, int* serverPort);

void *forwarder(void* args);
void *webTalk(void* args);
void secureTalk(int clientfd, rio_t client, char *inHost, char *version, int serverPort);
void ignore();

int debug;
int proxyPort;
int debugfd;
int logfd;
pthread_mutex_t mutex;

/* main function for the proxy program */

int main(int argc, char *argv[])
{
  //TODO: count, i, hp, haddrp, tid, args
  int count = 0;
  int listenfd, connfd, clientlen, optval, serverPort, i;
  struct sockaddr_in clientaddr;
  struct hostent *hp;
  char *haddrp;
  sigset_t sig_pipe; 
  pthread_t tid;
  int *args;
  
  if (argc < 2) {
    printf("Usage: ./%s port [debug] [webServerPort]\n", argv[0]);
    exit(1);
  }
  if(argc == 4)
    serverPort = atoi(argv[3]);
  else
    serverPort = 80;
  
  Signal(SIGPIPE, ignore);
  
  if(sigemptyset(&sig_pipe) || sigaddset(&sig_pipe, SIGPIPE))
    unix_error("creating sig_pipe set failed");
  if(sigprocmask(SIG_BLOCK, &sig_pipe, NULL) == -1)
    unix_error("sigprocmask failed");
  
  proxyPort = atoi(argv[1]);

  if(argc > 2)
    debug = atoi(argv[2]);
  else
    debug = 0;


  /* start listening on proxy port */

  listenfd = Open_listenfd(proxyPort);

  optval = 1;
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int)); 
  
  if(debug) debugfd = Open(DEBUG_FILE, O_CREAT | O_TRUNC | O_WRONLY, 0666);

  logfd = Open(LOG_FILE, O_CREAT | O_TRUNC | O_WRONLY, 0666);    


  /* if writing to log files, force each thread to grab a lock before writing
     to the files */
  
  pthread_mutex_init(&mutex, NULL);
  
  while(1) {

    printf("New client\n");
    clientlen = sizeof(clientaddr); //struct sockaddr_in

    /* accept a new connection from a client here */

    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    
    /* you have to write the code to process this new client request */

    printf("Connected to client\n");
    serverPort = 80; // Default port

    pthread_t thread;
    char *message = "Thread 1";
    /* create a new thread (or two) to process the new connection */

    args[0] = connfd;
    args[1] = serverPort;

    if (pthread_create(&thread, NULL, webTalk(args), (void*) message) < 0) {
      // printf("Client thread error\n");
    }

    printf("Done Thread\n");
    printf("Done Thread\n");
  }


  printf("Exit while\n");
  if(debug) Close(debugfd);
  Close(logfd);
  pthread_mutex_destroy(&mutex);
  
  return 0;
}

/* a possibly handy function that we provide, fully written */

void parseAddress(char* url, char* host, char** file, int* serverPort)
{
	char *point1;
  char *point2;
  char *saveptr;

	if(strstr(url, "http://"))
		url = &(url[7]);
	*file = strchr(url, '/');
	
	strcpy(host, url);

	/* first time strtok_r is called, returns pointer to host */
	/* strtok_r (and strtok) destroy the string that is tokenized */

	/* get rid of everything after the first / */
	strtok_r(host, "/", &saveptr);

	/* now look to see if we have a colon */
	point1 = strchr(host, ':');
	if(!point1) {
		*serverPort = 80;
		return;
	}
	
	/* we do have a colon, so get the host part out */
	strtok_r(host, ":", &saveptr);

	/* now get the part after the : */
	*serverPort = atoi(strtok_r(NULL, "/",&saveptr));
}


/* this is the function that I spawn as a thread when a new
   connection is accepted */

/* you have to write a lot of it */

void *webTalk(void* args)
{
  int numBytes, lineNum, serverfd, clientfd, serverPort;
  int tries;
  int byteCount = 0;
  char buf1[MAXLINE], buf2[MAXLINE], buf3[MAXLINE];
  char host[MAXLINE];
  char url[MAXLINE], logString[MAXLINE];
  char *token, *cmd, *version, *file, *saveptr;
  rio_t server, client;
  char slash[10];
  strcpy(slash, "/");
  
  clientfd = ((int*)args)[0];
  serverPort = ((int*)args)[1];
  // free(args);

  char request[MAXLINE];
  char buf[MAXLINE];

  /* Read first line of request */
  Rio_readinitb(&client, clientfd);
  Rio_readlineb(&client, request, MAXLINE);

  printf("Request: %s", request);

  char *uri;
  char target_address[MAXLINE];
  char path[MAXLINE];
  int *port = &serverPort;

  uri = strchr(request, 'h');

  strtok_r(uri, " ", &saveptr);
  printf("URI %s\n", uri);
  
  // Determine protocol (CONNECT or GET)
  if (strncmp(request, "GET", 3) == 0) {
    find_target_address(uri, target_address, path, port);
    printf("address: %s\n", target_address);
    printf("path: %s\n", path);
    printf("port: %d\n", *port);

    // GET: open connection to webserver (try several times, if necessary)

    serverfd = Open_clientfd(target_address, *port);

    printf("Connected!\n");

    // Write(sockfd, request, MAXLINE);
    // int n;
    // while (((n = Rio_readlineb(&client, buf1, MAXLINE)) > 0) && (buf1[0] != '\r')) {
    //   // if (buf1[0] != 'C') {
    //     printf("%s", buf1);
    //     Write(sockfd, buf1, MAXLINE);
    //   // }
    // }
    // printf("Start server read\n");
    // int len = recv(sockfd, buf2, MAXLINE, 0);
    // buf2[len] = '\0';
    // printf("%s\n", buf2);

    // while (((n = Rio_readn(sockfd, buf1, MAXLINE)) > 0) && (buf1[0] != '\r')) {
    //   printf("Server reading...\n");
    //   printf("%s\n", buf1);
    // }

    // int sockfd = Socket(AF_INET, SOCK_STREAM/* use tcp */, 0);
    // struct addrinfo hints;
    // struct addrinfo *res;
    // int status;

    // memset(&hints, 0, sizeof hints);
    // hints.ai_family = AF_UNSPEC;
    // hints.ai_socktype = SOCK_STREAM;
    // hints.ai_flags = AI_PASSIVE;

    // char buffer[10];
    // sprintf(buffer, "%d", *port);

    // if ((status = getaddrinfo(target_address, buffer, &hints, &res)) != 0) {
    //     fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
    //     exit(1);
    // }

    // struct addrinfo *r;
    // r = res;
    // for(r = res; r != NULL; r = r->ai_next) {
    //   void *addr;
    //   char *ipver = "IPV4";
    //   struct sockaddr_in *ip = (struct sockaddr_in *)r->ai_addr;
    //   addr = &(ip->sin_addr);
    //   char ipstr[INET6_ADDRSTRLEN];
      
    //   inet_ntop(r->ai_family, addr, ipstr, sizeof ipstr);
    //   printf("%s: %s\n", ipver, ipstr);

      // int s = socket(r->ai_family, r->ai_socktype, r->ai_protocol);

      // if (Connect(s, ip, sizeof(ip)) < 0) {
      //   printf("Connection fail.\n");
      // } else {

    /* GET: Transfer first header to webserver */

    // TODO: Switch to HTTP/1.0, Connection closed

    send(serverfd, request, MAXLINE, 0);
    send(serverfd, "\r\n", MAXLINE, 0);
    printf("%s", request);

    // char host[MAXLINE];
    // Rio_readlineb(&client, host, MAXLINE);
    // printf("%s", host);
    // send(serverfd, host, MAXLINE, 0);
    // send(serverfd, "\r\n\r\n", MAXLINE, 0);
    // int n;

    // GET: Transfer remainder of the request
    int n;
    while (((n = Rio_readlineb(&client, buf1, MAXLINE)) > 0) && (buf1[0] != '\r')) {
      if (buf1[0] != 'C') {
        printf("%s", buf1);
        send(serverfd, buf1, MAXLINE, 0);
        send(serverfd, "\r\n", MAXLINE, 0);
      }
    }
    send(serverfd, "\r\n\r\n", MAXLINE, 0);

    // GET: now receive the response
    printf("Begin GET\n");
    int argsF[2];
    argsF[0] = clientfd;
    argsF[1] = serverfd;
    printf("Call forwarder\n");
    forwarder(argsF);

  } else {
    // CONNECT: call a different function, securetalk, for HTTPS

  }
  return (void *)args;
}


/* this function handles the two-way encrypted data transferred in
   an HTTPS connection */

void secureTalk(int clientfd, rio_t client, char *inHost, char *version, int serverPort)
{
  int serverfd, numBytes1, numBytes2;
  int tries;
  rio_t server;
  char buf1[MAXLINE], buf2[MAXLINE];
  pthread_t tid;
  int *args;

  if (serverPort == proxyPort)
    serverPort = 443;
  
  /* Open connecton to webserver */


  /* clientfd is browser */
  /* serverfd is server */
  
  
  /* let the client know we've connected to the server */

  /* spawn a thread to pass bytes from origin server through to client */

  /* now pass bytes from client to server */

}

/* this function is for passing bytes from origin server to client */

void *forwarder(void* args)
{
  int numBytes, lineNum, serverfd, clientfd;
  int byteCount = 0;
  char buf[MAXLINE];
  clientfd = ((int*)args)[0];
  serverfd = ((int*)args)[1];
  // free(args);

  printf("Start server read\n");

  



  while(1) {
    
    /* serverfd is for talking to the web server */
    int tmpres;
    memset(buf, 0, sizeof(buf));
    int htmlstart = 0;
    char * htmlcontent;
    htmlstart = 0;
    while((tmpres = recv(serverfd, buf, MAXLINE, 0)) > 0){
      if(htmlstart == 0)
      {
        /* Under certain conditions this will not work.
        * If the \r\n\r\n part is splitted into two messages
        * it will fail to detect the beginning of HTML content
        */
        htmlcontent = strstr(buf, "\r\n\r\n");
        if(htmlcontent != NULL){
          htmlstart = 1;
          htmlcontent += 4;
        }
      }else{
        htmlcontent = buf;
      }
      if(htmlstart){
        Rio_writen(clientfd, htmlcontent, MAXLINE);
      }
    
      memset(buf, 0, tmpres);
    }
    // if(tmpres < 0)
    // {
    //   perror("Error receiving data");
    // }
    /* clientfd is for talking to the browser */
    
  }
}


void ignore()
{
	;
}


/*============================================================
 * url parser:
 *    find_target_address()
 *        Given a url, copy the target web server address to
 *        target_address and the following path to path.
 *        target_address and path have to be allocated before they 
 *        are passed in and should be long enough (use MAXLINE to be 
 *        safe)
 *
 *        Return the port number. 0 is returned if there is
 *        any error in parsing the url.
 *
 *============================================================*/

/*find_target_address - find the host name from the uri */
int  find_target_address(char * uri, char * target_address, char * path,
                         int  * port)
{

  if (strncasecmp(uri, "http://", 7) == 0) {
  	char * hostbegin, * hostend, *pathbegin;
  	int    len;
         
  	/* find the target address */
  	hostbegin = uri+7;
  	hostend = strpbrk(hostbegin, " :/\r\n");
  	if (hostend == NULL){
  	  hostend = hostbegin + strlen(hostbegin);
  	}

  	len = hostend - hostbegin;

  	strncpy(target_address, hostbegin, len);
  	target_address[len] = '\0';

  	/* find the port number */
  	if (*hostend == ':') {
      *port = atoi(hostend+1);
    }

  	/* find the path */

  	pathbegin = strchr(hostbegin, '/');

  	if (pathbegin == NULL) {
  	  path[0] = '\0';
  	  
  	}
  	else {
  	  pathbegin++;	
  	  strcpy(path, pathbegin);
  	}
  	return 0;
  }
  target_address[0] = '\0';
  return -1;
}



/*============================================================
 * log utility
 *    format_log_entry
 *       Copy the formatted log entry to logstring
 *============================================================*/

void format_log_entry(char * logstring, int sock, char * uri, int size)
{
    time_t  now;
    char    buffer[MAXLINE];
    struct  sockaddr_in addr;
    unsigned  long  host;
    unsigned  char a, b, c, d;
    int    len = sizeof(addr);

    now = time(NULL);
    strftime(buffer, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    if (getpeername(sock, (struct sockaddr *) & addr, &len)) {
      /* something went wrong writing log entry */
      printf("getpeername failed\n");
      return;
    }

    host = ntohl(addr.sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;

    sprintf(logstring, "%s: %d.%d.%d.%d %s %d\n", buffer, a,b,c,d, uri, size);
}
