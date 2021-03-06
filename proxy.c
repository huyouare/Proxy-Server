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
void *forwarder_post(void* args);
void *forwarder_persistent(void* args);
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
  
  if (argc < 2) {
    printf("Usage: ./%s port [debug] [webServerPort]\n", argv[0]);
    exit(1);
  }
  if(argc == 4)
    serverPort = atoi(argv[3]);
  else
    serverPort = 80;
  
  Signal(SIGPIPE, ignore);
  // signal(SIGPIPE, SIG_ING);
  
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

    int *args = malloc(sizeof(*args));
    args[0] = connfd;
    args[1] = serverPort;

    if (pthread_create(&thread, NULL, &webTalk, args) < 0) {
      printf("webTalk thread error\n");
    }
    // pthread_join(&thread);

    printf("Done Thread\n");
  }

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
  int numBytes1, numBytes2;
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
  free(args);

  char request[MAXLINE];
  char buf[MAXLINE];

  /* Read first line of request */
  Rio_readinitb(&client, clientfd);
  numBytes = Rio_readlineb(&client, request, MAXLINE);

  if (numBytes <= 0) {
    printf("No request received\n");
    return NULL;
  }

  printf("Request: %s", request);

  char *uri;
  char target_address[MAXLINE];
  char path[MAXLINE];
  int *port = &serverPort;
  char request_copy[MAXLINE];

  int *keep_alive = malloc(sizeof(int *));
  *keep_alive = 0;

  /* Create separate variables for URI */
  strcpy(request_copy, request);
  uri = strchr(request, ' ');
  ++uri;
  strtok_r(uri, " ", &saveptr);
  printf("URI %s\n", uri);

  // Parse Address
  find_target_address(uri, target_address, path, port);
  printf("address: %s\n", target_address);
  printf("path: %s\n", path);
  printf("port: %d\n", *port);
  
  // Determine protocol (CONNECT or GET)
  if (strncmp(request, "GET", 3) == 0) {

    // GET: open connection to webserver (try several times, if necessary)
    int i = 0;
    do {
      serverfd = Open_clientfd(target_address, *port);
    } while (serverfd < 0 && i < 10); // Max 10 times

    printf("Connected!\n");

    /* GET: Transfer first header to webserver */
    send(serverfd, request_copy, MAXLINE, 0);
    // send(serverfd, "\n", MAXLINE, 0);
    printf("%s\n", request_copy);

    // GET: Transfer remainder of the request
    while (((Rio_readlineb(&client, buf1, MAXLINE)) > 0) && (buf1[0] != '\r')) {
      // TODO: Connection closed
      // if (strncmp(buf1, "Connection", 10) == 0) {
      //   printf("%s", "Connection: close\n");
      //   // send(serverfd, "Connection: close\n", MAXLINE, 0);
      // } 
      if (strncmp(buf1, "Connection: Keep-Alive", 22) == 0) {
        *keep_alive = 1;
      } else if (strncmp(buf1, "Proxy: Keep-Alive", 17) == 0) {
        *keep_alive = 1;
      }
      printf("%s", buf1);
      send(serverfd, buf1, MAXLINE, 0);
      send(serverfd, "\n", MAXLINE, 0);
    }
    send(serverfd, "\r\n\r\n", MAXLINE, 0);

    // GET: now receive the response
    printf("Begin GET\n");
    int *argsF = malloc(3 * sizeof(argsF));
    argsF[0] = clientfd;
    argsF[1] = serverfd;
    printf("Call forwarder\n");

    pthread_t thread;
    char *message = "Thread 1";

    // Persistent call must keep reading from client
    if (*keep_alive == 1) {
      printf("start keep_alive\n");
      argsF[2] = keep_alive;
      if (pthread_create(&thread, NULL, &forwarder_persistent, argsF) < 0) {
        printf("Fowarder thread error\n");
      }
      printf("Start client read\n");
      while(1) {
        numBytes = Rio_readp(serverfd, buf1, MAXLINE);
        byteCount = Rio_writen(clientfd, buf1, numBytes);
        if (numBytes != byteCount) {
          printf("INCORRECT WRITE");
        }
        if (strncmp(buf1, "\r", 1) == 0) {
          break;
        }
      } 
      printf("Client end read\n");
      *keep_alive = 0;
      shutdown(clientfd, 1);
      Close(clientfd);
    } else {
      forwarder(argsF); // Calls forwarder function
    }

    // if (pthread_create(&thread, NULL, &forwarder, argsF) < 0) {
    //   printf("Fowarder thread error\n");
    // }

    shutdown(serverfd, 1);
    Close(serverfd);
  } 
  else if (strncmp(request, "CONNECT", 7) == 0) {
    // CONNECT: call a different function, securetalk, for HTTPS
    
    /* CONNECT: Transfer first header to webserver */
    send(serverfd, request, MAXLINE, 0);
    send(serverfd, "HTTP/1.0 \n", MAXLINE, 0); // Default to 1.0 for our purposes
    printf("%s HTTP/1.0\n", request);

    if (*port == NULL) {
      *port = 443;
    }
    printf("Caling secureTalk\n");
    char inHost[MAXLINE], version[MAXLINE];
    // Call secure talk to do more work
    secureTalk(clientfd, client, target_address, version, *port);
  } else if (strncmp(request, "POST", 3) == 0) {

    // POST: open connection to webserver (try several times, if necessary)
    int i = 0;
    do {
      serverfd = Open_clientfd(target_address, *port);
    } while (serverfd < 0 && i < 10); // Max 10 times

    printf("Connected!\n");

    /* POST: Transfer first header to webserver */
    send(serverfd, request_copy, MAXLINE, 0);
    send(serverfd, "\n", MAXLINE, 0);
    printf("%s\n", request);

    // POST: Transfer remainder of the request
    while (((Rio_readlineb(&client, buf1, MAXLINE)) > 0) && (buf1[0] != '\r')) {
      printf("%s", buf1);
      send(serverfd, buf1, MAXLINE, 0);
      send(serverfd, "\n", MAXLINE, 0);
    }
    send(serverfd, "\r\n\r\n", MAXLINE, 0);

    // POST: now receive the response
    printf("Begin POST\n");
    int *argsF = malloc(2 * sizeof(argsF));
    argsF[0] = clientfd;
    argsF[1] = serverfd;
    printf("Call forwarder\n");

    pthread_t thread;
    char *message = "Thread 1";
    forwarder_post(argsF);

    // if (pthread_create(&thread, NULL, &forwarder, argsF) < 0) {
    //   printf("Fowarder thread error\n");
    // }

    shutdown(serverfd, 1);
    Close(serverfd);

  } else if (strncmp(request, "PUT", 3) == 0) {
    int i = 0;
    do {
      serverfd = Open_clientfd(target_address, *port);
    } while (serverfd < 0 && i < 10); // Max 10 times

    printf("Connected!\n");

    /* PUT: Transfer first header to webserver */
    send(serverfd, request, MAXLINE, 0);
    send(serverfd, "\n", MAXLINE, 0);
    printf("%s HTTP/1.0\n", request);

    // PUT: Transfer remainder of the request
    do {
      numBytes1 = Rio_readp(clientfd, buf1, MAXLINE);
      numBytes2 = Rio_writen(serverfd, buf1, numBytes1);
      if (numBytes1 != numBytes2) {
        printf("INCORRECT WRITE");
      }
    } while (numBytes1 > 0 || numBytes2 > 0);
    send(serverfd, "\r\n\r\n", MAXLINE, 0);

    // PUT: now receive the response
    printf("Begin PUT\n");
    int *argsF = malloc(2 * sizeof(argsF));
    argsF[0] = clientfd;
    argsF[1] = serverfd;
    printf("Call forwarder\n");

    pthread_t thread;
    char *message = "Thread 1";
    forwarder(argsF);

    shutdown(serverfd, 1);
    Close(serverfd);
  } else if (strncmp(request, "DELETE", 6) == 0) {
    int i = 0;
    do {
      serverfd = Open_clientfd(target_address, *port);
    } while (serverfd < 0 && i < 10); // Max 10 times

    printf("Connected!\n");

    /* DELETE: Transfer first header to webserver */
    send(serverfd, request, MAXLINE, 0);
    send(serverfd, "\n", MAXLINE, 0);
    printf("%s HTTP/1.0\n", request);

    // DELETE: Transfer remainder of the request
    do {
      numBytes1 = Rio_readp(clientfd, buf1, MAXLINE);
      numBytes2 = Rio_writen(serverfd, buf1, numBytes1);
      if (numBytes1 != numBytes2) {
        printf("INCORRECT WRITE");
      }
    } while (numBytes1 > 0 || numBytes2 > 0);
    send(serverfd, "\r\n\r\n", MAXLINE, 0);

    // DELETE: now receive the response
    printf("Begin PUT\n");
    int *argsF = malloc(2 * sizeof(argsF));
    argsF[0] = clientfd;
    argsF[1] = serverfd;
    printf("Call forwarder\n");

    pthread_t thread;
    char *message = "Thread 1";
    forwarder(argsF);

    shutdown(serverfd, 1);
    Close(serverfd);
  } else {
    printf("Request type not yet supported\n");
  }
  return 0;
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
  int i = 0;
  do {
    serverfd = Open_clientfd(inHost, serverPort);
  } while (serverfd < 0 && i < 10); // Max connections
  printf("Connected in secureTalk\n");

  // CONNECT: Transfer remainder of the request
  Rio_readinitb(&client, clientfd);

  /* clientfd is browser */
  /* serverfd is server */
  
  /* let the client know we've connected to the server */
  char msg[MAXLINE];
  strcpy(msg, "HTTP/1.1 200 OK\r\n\r\n");
  Rio_writen(clientfd, msg, strlen(msg));
  /* spawn a thread to pass bytes from origin server through to client */
  printf("Begin CONNECT\n");
  int *argsF = malloc(2 * sizeof(argsF));
  argsF[0] = clientfd;
  argsF[1] = serverfd;
  printf("Call forwarder\n");

  pthread_t thread;

  if (pthread_create(&thread, NULL, &forwarder, argsF) < 0) {
    printf("secureTalk thread error\n");
  }
  /* now pass bytes from client to server */

  do {
    numBytes1 = Rio_readp(clientfd, buf1, MAXLINE);
    numBytes2 = Rio_writen(serverfd, buf1, numBytes1);
    if (numBytes1 != numBytes2) {
      printf("INCORRECT WRITE");
    }
  } while (numBytes1 > 0 || numBytes2 > 0);

  Pthread_join(thread, NULL);
  shutdown(serverfd, 1);
  Close(serverfd);
}

/* this function is for passing bytes from origin server to client */

void *forwarder(void* args)
{
  int numBytes, lineNum, serverfd, clientfd;
  int byteCount = 0;
  char buf[MAXLINE], buf1[MAXLINE];
  clientfd = ((int*)args)[0];
  serverfd = ((int*)args)[1];
  free(args);

  printf("\nStart server read\n");

  do {
    numBytes = Rio_readp(serverfd, buf1, MAXLINE);
    byteCount = Rio_writen(clientfd, buf1, numBytes);
    if (numBytes != byteCount) {
      printf("INCORRECT WRITE");
    }
  } while (numBytes > 0 || byteCount > 0);

  shutdown(clientfd, 1);
  Close(clientfd);

  return 0;
}

void *forwarder_persistent(void* args)
{
  printf("forwarder_persistent start\n");
  int numBytes, lineNum, serverfd, clientfd;
  int byteCount = 0;
  char buf[MAXLINE], buf1[MAXLINE];
  clientfd = ((int*)args)[0];
  serverfd = ((int*)args)[1];
  printf("Keep-Alive\n");
  int *keep_alive = ((int*)args)[2];
  free(args);

  printf("\nStart server read\n");

  do {
    numBytes = Rio_readp(serverfd, buf1, MAXLINE);
    byteCount = Rio_writen(clientfd, buf1, numBytes);
    if (numBytes != byteCount) {
      printf("INCORRECT WRITE");
    }
  } while (*keep_alive == 1);

  shutdown(clientfd, 1);
  Close(clientfd);

  return 0;
}

void *forwarder_post(void* args)
{
  int numBytes, lineNum, serverfd, clientfd;
  int byteCount = 0;
  char buf[MAXLINE], buf1[MAXLINE];
  clientfd = ((int*)args)[0];
  serverfd = ((int*)args)[1];
  free(args);

  printf("\nStart server read\n");

  do {
    numBytes = Rio_readn(serverfd, buf1, MAXLINE);
    byteCount = Rio_writen(clientfd, buf1, numBytes);
    if (numBytes != byteCount) {
      printf("INCORRECT WRITE");
    }
  } while (numBytes > 0 || byteCount > 0);

  shutdown(clientfd, 1);
  Close(clientfd);

  return 0;
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

  // if (strncasecmp(uri, "http://", 7) == 0 || strncasecmp(uri, "https://", 8) == 0) {
  	char * hostbegin, * hostend, *pathbegin;
  	int    len;
         
  	/* find the target address */
  	if (strncasecmp(uri, "http://", 7) == 0) {
      hostbegin = uri+7;
    } else if (strncasecmp(uri, "https://", 8) == 0) {
      hostbegin = uri+8;
    } else {
      hostbegin = uri;
    }
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
  // }
  // target_address[0] = '\0';
  // return -1;
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
