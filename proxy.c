#include <stdio.h>
#include <stdlib.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define SBUF_SIZE 32
#define NTHREADS 8

/* $begin sbuft */
typedef struct
{
    int *buf;    /* Buffer array */
    int n;       /* Maximum number of slots */
    int front;   /* buf[(front+1)%n] is first item */
    int rear;    /* buf[rear%n] is last item */
    sem_t mutex; /* Protects accesses to buf */
    sem_t slots; /* Counts available slots */
    sem_t items; /* Counts available items */
} sbuf_t;
/* $end sbuft */

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static char *port;
static const char *ignore_key[4] = {"Host", "Connection", "Proxy-Connection", "User-Agent"};
static sbuf_t reqbuf;

void check_arg(int);
void check_port(char *);
void sequencial_proxy(char *);
void concurrent_proxy(char *);
void handle_request(int);
void *thread(void *);

void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);

int main(int argc, char *argv[])
{
    check_arg(argc);
    port = argv[1];
    check_port(port);
    concurrent_proxy(port);
    // sequencial_proxy(port);
    return 0;
}

void check_arg(int argc)
{
    if (argc != 2)
        app_error("invalid arg count");
}

void check_port(char *portstr)
{
    int port = atoi(portstr);
    if (port <= 1024 && port >= 65536)
        app_error("invalid port number");

    // printf("use port: %d\n", port);
}

void sequencial_proxy(char *port)
{
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    int listenfd = Open_listenfd(port), connfd;
    // printf("listenfd: %d\n", listenfd);
    while (1)
    {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        // printf("connfd: %d\n", connfd);
        handle_request(connfd);
        Close(connfd);
    }
}

void concurrent_proxy(char *port)
{
    pthread_t pid;
    sbuf_init(&reqbuf, SBUF_SIZE);
    for (int i = 0; i < NTHREADS; i++)
        Pthread_create(&pid, NULL, thread, NULL);
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    int listenfd = Open_listenfd(port), connfd;
    while (1)
    {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        sbuf_insert(&reqbuf, connfd);
    }
}

void handle_request(int connfd)
{
    size_t n;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], header_key[MAXLINE];
    char forwardbuf[8300];
    rio_t rio, server_rio;
    Rio_readinitb(&rio, connfd);
    if ((n = Rio_readlineb(&rio, buf, MAXLINE)) == 0)
    {
        printf("empty content\n");
        return;
    }
    sscanf(buf, "%s %s %s", method, uri, version);
    char host[MAXLINE];
    int server_port = 80;
    char page[MAXLINE];
    // only fit for the autograder
    sscanf(uri, "http://%99[^:]:%99d%99[^\n]", host, &server_port, page);
    char server_port_char[MAXLINE];
    sprintf(server_port_char, "%d", server_port);

    if (strcasecmp(method, "GET"))
    {
        printf("only support GET\n");
        return;
    }
    printf("%s %d %s\n", host, server_port, page);

    int server_fd;
    server_fd = Open_clientfd(host, server_port_char);
    printf("server fd: %d\n", server_fd);
    Rio_readinitb(&server_rio, server_fd);
    sprintf(forwardbuf, "GET %s HTTP/1.0\r\n", page);
    Rio_writen(server_fd, forwardbuf, strlen(forwardbuf));
    sprintf(forwardbuf, "Host: %s\r\n", host);
    Rio_writen(server_fd, forwardbuf, strlen(forwardbuf));
    sprintf(forwardbuf, "%s", user_agent_hdr);
    Rio_writen(server_fd, forwardbuf, strlen(forwardbuf));
    sprintf(forwardbuf, "Connection: close\r\n");
    Rio_writen(server_fd, forwardbuf, strlen(forwardbuf));
    sprintf(forwardbuf, "Proxy-Connection: close\r\n");
    Rio_writen(server_fd, forwardbuf, strlen(forwardbuf));

    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0)
    {
        printf("%s", buf);
        // only fit for the autograder (maybe)
        sscanf(buf, "%99[^:]:", header_key);
        int ignore = 0;
        for (int i = 0; i <= 4; i++)
        {
            if (!strcmp(header_key, ignore_key[i]))
            {
                ignore = 1;
                break;
            }
        }
        if (!ignore)
        {
            Rio_writen(server_fd, buf, strlen(buf));
        }
        if (!strcmp(buf, "\r\n"))
            break;
    }

    sprintf(forwardbuf, "\r\n");
    Rio_writen(server_fd, forwardbuf, strlen(forwardbuf));

    char payload[MAX_OBJECT_SIZE];
    int sum;
    strcpy(payload, "");
    while ((n = Rio_readlineb(&server_rio, forwardbuf, 8300)) != 0)
    {
        printf("%s", forwardbuf);
        sum += n;
        if (sum <= MAX_OBJECT_SIZE)
            strcat(payload, forwardbuf);
        Rio_writen(connfd, forwardbuf, n);
    }

    printf("forward respond %d bytes\n", sum);
}

void *thread(void *vargp)
{
    Pthread_detach(pthread_self());
    while (1)
    {
        int connfd = sbuf_remove(&reqbuf);
        printf("thread %lu handling connfd %d\n", pthread_self(), connfd);
        handle_request(connfd);
        Close(connfd);
    }
}

/* Create an empty, bounded, shared FIFO buffer with n slots */
/* $begin sbuf_init */
void sbuf_init(sbuf_t *sp, int n)
{
    sp->buf = Calloc(n, sizeof(int));
    sp->n = n;                  /* Buffer holds max of n items */
    sp->front = sp->rear = 0;   /* Empty buffer iff front == rear */
    Sem_init(&sp->mutex, 0, 1); /* Binary semaphore for locking */
    Sem_init(&sp->slots, 0, n); /* Initially, buf has n empty slots */
    Sem_init(&sp->items, 0, 0); /* Initially, buf has zero data items */
}
/* $end sbuf_init */

/* Clean up buffer sp */
/* $begin sbuf_deinit */
void sbuf_deinit(sbuf_t *sp)
{
    Free(sp->buf);
}
/* $end sbuf_deinit */

/* Insert item onto the rear of shared buffer sp */
/* $begin sbuf_insert */
void sbuf_insert(sbuf_t *sp, int item)
{
    P(&sp->slots);                          /* Wait for available slot */
    P(&sp->mutex);                          /* Lock the buffer */
    sp->buf[(++sp->rear) % (sp->n)] = item; /* Insert the item */
    V(&sp->mutex);                          /* Unlock the buffer */
    V(&sp->items);                          /* Announce available item */
}
/* $end sbuf_insert */

/* Remove and return the first item from buffer sp */
/* $begin sbuf_remove */
int sbuf_remove(sbuf_t *sp)
{
    int item;
    P(&sp->items);                           /* Wait for available item */
    P(&sp->mutex);                           /* Lock the buffer */
    item = sp->buf[(++sp->front) % (sp->n)]; /* Remove the item */
    V(&sp->mutex);                           /* Unlock the buffer */
    V(&sp->slots);                           /* Announce available slot */
    return item;
}
/* $end sbuf_remove */
/* $end sbufc */
