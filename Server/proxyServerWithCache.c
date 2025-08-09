#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define MAX_CLIENTS 10

typedef struct cache_element cache_element;

struct cache_element // defining our cache element
{
    char *data; // the data it will hold
    int len;
    char *url;
    time_t lru_time_track;
    cache_element *next; // we will creating a linkedlist of cache-elements
};
// declaring the functions , implemented later
cache_element *find(char *url);
int add_cache_element(char *data, int size, char *url);
void remove_cache_element();

int port_number = 8080;
int proxy_socketId;
pthread_t tid[MAX_CLIENTS]; //! storing the ids of maximum concurrent users we will  allow in our server
sem_t semaphore; // declaring semaphore and lock, semaphore to limit maximum concurrent users at a
// time and lock to orevent race conditions while writing in lru cache
pthread_mutex_t lock;

// since our lru cache is a linkedlist, we will define its head =>
cache_element *head;
int cache_size;

int main(int argc, char *argv[]) // argc counts total space separated arguments in cli,
// and argv is the vector that stores the values of these space separated arguments of cli
{
    int client_socketId;
    int client_len;
    struct sockaddr_in server_addr, client_addr; /*
    & sockaddr_in is a c structure that holds an ipv4 + port in the
    & format OS expects, think of it like a small packet that says ""listen on this IP and port"
     */

    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);
    if (argc == 2)
    {
        port_number = atoi(argv[1]);
    }
    else
    {
        printf("Too Few arguments\n");
        exit(1);
    }
    printf("Starting Proxy server at port: %d\n", port_number);
    // socket() tells the OS: “give me a brand-new networking endpoint I can use to talk on the network.” 
    // It returns a small integer called a "file descriptor" (like a handle) that 
    // you use for bind(), listen(), accept(), connect(), read(), write(), close().
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0); // AF_INET means ipv4, sock stream and 0(by deault TCP comm) is for tcp communication

    if (proxy_socketId < 0)
    {
        perror("Failed to create a socket");
        exit(1);
    }
    int reuse = 1;
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0)
    {
        perror("setSocketOpt failed\n");
    }
    bzero((char *)&server_addr, sizeof(server_addr)); // by default inc , a struct stores garbage values, so we clean it 
    //using bzero, i.e. assign it '\0' , its a good practice to do so
    // setting options 
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number); // htons convert port number to a format that is understandable by the OS
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if(bind(proxy_socketId,(struct sockaddr*)&server_addr,sizeof(server_addr))<0){
        perror("Port is not available");
        exit(1);
    }
    printf("Binding on Port %d\n",port_number);
    int listen_status = listen(proxy_socketId,MAX_CLIENTS);
    if(listen_status<0){
        perror("Error in listening\n");
        exit(1);
    }
    int i = 0;
    int Connected_socketId[MAX_CLIENTS];

    while(1){
        bzero((char *)&client_addr,sizeof(client_addr));
        client_len = sizeof(client_addr);
        client_socketId = accept(proxy_socketId,(struct sockaddr *)&client_addr,(socklen_t*)&client_addr);
        if(client_socketId<0){
            printf("Unable to connect");
            exit(1);
        }
        else{
            Connected_socketId[i] = client_socketId;
        }
    }
}   