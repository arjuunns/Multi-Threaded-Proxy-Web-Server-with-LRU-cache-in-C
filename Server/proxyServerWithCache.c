#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include "proxy_parse.h"

// ! FOR INDEPTH EXPLANATION CHECKOUT THE EXACLIDRAW LINK 

#define MAX_BYTES 4096
#define MAX_CLIENTS 400
#define MAX_SIZE (200 * (1 << 20))
#define MAX_ELEMENT_SIZE (10 * (1 << 20))

typedef struct cache_element cache_element;

struct cache_element
{
    char *data;
    int len;
    char *url;
    time_t lru_time_track;
    cache_element *next;
};
cache_element *find(char *url);
int add_cache_element(char *data, int size, char *url);
void remove_cache_element();
void *thread_fn(void *socketNew);
int checkHTTPversion(char *msg);
int handle_request(int clientSocket, struct ParsedRequest *request, char *tempReq);
int sendErrorMessage(int socket, int status_code);

pthread_t tid[MAX_CLIENTS];
int port_number = 8080;
int proxy_socketId;

pthread_t tid[MAX_CLIENTS];

sem_t semaphore;
pthread_mutex_t lock;

cache_element *head = NULL;
int cache_size = 0;
int main(int argc, char *argv[])
{
    int client_socketId, client_len;
    printf("Proxy server started\n");
    fflush(stdout);
    struct sockaddr_in server_address, client_address;

    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);

    if (argc == 2)
    {
        port_number = atoi(argv[1]);
    }
    else
    {
        printf("Too few arguments. Usage: ./proxy <port>\n");
        exit(1);
    }
    printf("Proxy server listening on port %d\n", port_number);
    fflush(stdout);

    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_socketId < 0)
    {
        perror("Error in creating socket");
        exit(1);
    }
    int reuse = 1;
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0)
    {
        perror("Error in setting socket options");
        exit(1);
    }

    bzero((char *)&server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port_number);

    if (bind(proxy_socketId, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        perror("Error in binding socket");
        exit(1);
    }
    printf("BINDING ON PORT %d\n", port_number);
    fflush(stdout);

    int listen_status = listen(proxy_socketId, MAX_CLIENTS);
    if (listen_status < 0)
    {
        perror("Error in listening on socket");
        exit(1);
    }

    int iterator = 0;
    int connected_socketId[MAX_CLIENTS];

    while (1)
    {
        client_len = sizeof(client_address);
        bzero((char *)&client_address, client_len);
        client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_address, (socklen_t *)&client_len);
        if (client_socketId < 0)
        {
            perror("Error in accepting client connection");
            exit(1);
        }
        else
            connected_socketId[iterator] = client_socketId;
        struct sockaddr_in *client_ptr = (struct sockaddr_in *)&client_address;
        struct in_addr ip_addr = client_ptr->sin_addr;
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);
        printf("Client connected from %s:%d\n", str, ntohs(client_ptr->sin_port));
        fflush(stdout);
        pthread_create(&tid[iterator], NULL, thread_fn, (void *)&connected_socketId[iterator]);
        iterator++;
    }

    close(proxy_socketId);
    sem_destroy(&semaphore);
    pthread_mutex_destroy(&lock);
    for (int i = 0; i < iterator; i++)
    {
        pthread_join(tid[i], NULL);
    }
    ParsedRequest_destroy(pr);

    return 0;
}

cache_element *find(char *url)
{
    cache_element *current = head;
    pthread_mutex_lock(&lock); 
    while (current != NULL)
    {
        if (strcmp(current->url, url) == 0)
        {
            current->lru_time_track = time(NULL);
            pthread_mutex_unlock(&lock);
            return current;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}

int add_cache_element(char *data, int size, char *url)
{
    int element_size = size + strlen(url) + sizeof(cache_element);
    pthread_mutex_lock(&lock);
    if (element_size > 10 * (1 << 20))
    {
        pthread_mutex_unlock(&lock);
        return 0;
    }
    while (cache_size + element_size > 200 * (1 << 20))
    {
        remove_cache_element();
    }
    cache_element *new_elem = (cache_element *)malloc(sizeof(cache_element));
    new_elem->data = (char *)malloc(size + 1);
    strncpy(new_elem->data, data, size);
    new_elem->data[size] = '\0';
    new_elem->len = size;
    new_elem->url = (char *)malloc(strlen(url) + 1);
    strcpy(new_elem->url, url);
    new_elem->lru_time_track = time(NULL);
    new_elem->next = head;
    head = new_elem;
    cache_size += element_size;
    pthread_mutex_unlock(&lock);
    return 1;
}

void remove_cache_element()
{
    pthread_mutex_lock(&lock);
    if (head == NULL)
    {
        pthread_mutex_unlock(&lock);
        return;
    }
    cache_element *prev = NULL, *curr = head, *lru_prev = NULL, *lru_elem = head;
    while (curr != NULL)
    {
        if (curr->lru_time_track < lru_elem->lru_time_track)
        {
            lru_elem = curr;
            lru_prev = prev;
        }
        prev = curr;
        curr = curr->next;
    }
    if (lru_prev == NULL)
    {
        head = lru_elem->next;
    }
    else
    {
        lru_prev->next = lru_elem->next;
    }
    cache_size -= (lru_elem->len + strlen(lru_elem->url) + sizeof(cache_element));
    free(lru_elem->data);
    free(lru_elem->url);
    free(lru_elem);
    pthread_mutex_unlock(&lock);
}

void *thread_fn(void *socketNew)
{
    sem_wait(&semaphore);
    int p;
    sem_getvalue(&semaphore, &p);
    printf("semaphore value:%d\n", p);
    fflush(stdout);
    int *t = (int *)(socketNew);
    int socket = *t;
    int bytes_send_client, len;

    char *buffer = (char *)calloc(MAX_BYTES, sizeof(char));
    bzero(buffer, MAX_BYTES);
    bytes_send_client = recv(socket, buffer, MAX_BYTES, 0);

    while (bytes_send_client > 0)
    {
        len = strlen(buffer);
        if (strstr(buffer, "\r\n\r\n") == NULL)
        {
            bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
        }
        else
        {
            break;
        }
    }
    char *tempReq = (char *)malloc(strlen(buffer) * sizeof(char) + 1);
    for (int i = 0; i < strlen(buffer); i++)
    {
        tempReq[i] = buffer[i];
    }

    struct cache_element *temp = find(tempReq);

    if (temp != NULL)
    {
        int size = temp->len / sizeof(char);
        int pos = 0;
        char response[MAX_BYTES];
        while (pos < size)
        {
            bzero(response, MAX_BYTES);
            for (int i = 0; i < MAX_BYTES && pos < size; i++)
            {
                response[i] = temp->data[pos];
                pos++;
            }
            send(socket, response, MAX_BYTES, 0);
        }
        printf("Data retrived from the Cache\n\n");
        fflush(stdout);
        printf("%s\n\n", response);
        fflush(stdout);
    }
    else if (bytes_send_client > 0)
    {
        len = strlen(buffer);
        struct ParsedRequest *request = ParsedRequest_create();
        if (ParsedRequest_parse(request, buffer, len) < 0)
        {
            printf("Parsing failed\n");
        }
        else
        {
            bzero(buffer, MAX_BYTES);
            if (!strcmp(request->method, "GET"))
            {
                if (request->host && request->path && (checkHTTPversion(request->version) == 1))
                {
                    bytes_send_client = handle_request(socket, request, tempReq);
                    if (bytes_send_client == -1)
                    {
                        sendErrorMessage(socket, 500);
                    }
                }
                else
                    sendErrorMessage(socket, 500);
            }
            else
            {
                printf("This code doesn't support any method other than GET\n");
            }
        }
        ParsedRequest_destroy(request);
    }
    else if (bytes_send_client < 0)
    {
        perror("Error in receiving from client.\n");
    }
    else if (bytes_send_client == 0)
    {
        printf("Client disconnected!\n");
    }

    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);
    sem_post(&semaphore);
    sem_getvalue(&semaphore, &p);
    printf("Semaphore post value:%d\n", p);
    fflush(stdout);
    free(tempReq);
    return NULL;
}
int sendErrorMessage(int socket, int status_code)
{
    char str[1024];
    char currentTime[50];
    time_t now = time(0);
    struct tm data = *gmtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);
    switch (status_code)
    {
    case 400:
        snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: ProxyServer\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Request</H1>\n</BODY></HTML>", currentTime);
        printf("400 Bad Request\n");
        fflush(stdout);
        send(socket, str, strlen(str), 0);
        break;
    case 403:
        snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: ProxyServer\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
        printf("403 Forbidden\n");
        fflush(stdout);
        send(socket, str, strlen(str), 0);
        break;
    case 404:
        snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: ProxyServer\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
        printf("404 Not Found\n");
        fflush(stdout);
        send(socket, str, strlen(str), 0);
        break;
    case 500:
        snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: ProxyServer\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
        send(socket, str, strlen(str), 0);
        break;
    case 501:
        snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: ProxyServer\r\n\r\n<HTML><HEAD><TITLE>501 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
        printf("501 Not Implemented\n");
        fflush(stdout);
        send(socket, str, strlen(str), 0);
        break;
    case 505:
        snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: ProxyServer\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
        printf("505 HTTP Version Not Supported\n");
        fflush(stdout);
        send(socket, str, strlen(str), 0);
        break;
    default:
        return -1;
    }
    return 1;
}

int checkHTTPversion(char *msg)
{
    int version = -1;
    if (strncmp(msg, "HTTP/1.1", 8) == 0)
    {
        version = 1;
    }
    else if (strncmp(msg, "HTTP/1.0", 8) == 0)
    {
        version = 1;
    }
    else
        version = -1;
    return version;
}

int connectRemoteServer(char *host_addr, int port_num)
{
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (remoteSocket < 0)
    {
        printf("Error in Creating Socket.\n");
        return -1;
    }
    struct hostent *host = gethostbyname(host_addr);
    if (host == NULL)
    {
        fprintf(stderr, "No such host exists.\n");
        return -1;
    }
    struct sockaddr_in server_addr;
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);
    bcopy((char *)host->h_addr, (char *)&server_addr.sin_addr.s_addr, host->h_length);
    if (connect(remoteSocket, (struct sockaddr *)&server_addr, (socklen_t)sizeof(server_addr)) < 0)
    {
        fprintf(stderr, "Error in connecting !\n");
        return -1;
    }
    return remoteSocket;
}

int handle_request(int clientSocket, struct ParsedRequest *request, char *tempReq)
{
    char *buf = (char *)malloc(sizeof(char) * MAX_BYTES);
    strcpy(buf, "GET ");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");
    size_t len = strlen(buf);
    if (ParsedHeader_set(request, "Connection", "close") < 0)
    {
        printf("set header key not work\n");
        fflush(stdout);
    }
    if (ParsedHeader_get(request, "Host") == NULL)
    {
        if (ParsedHeader_set(request, "Host", request->host) < 0)
        {
            printf("Set \"Host\" header key not working\n");
            fflush(stdout);
        }
    }
    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0)
    {
        printf("unparse failed\n");
        fflush(stdout);
    }
    int server_port = 80;
    if (request->port != NULL)
        server_port = atoi(request->port);
    int remoteSocketID = connectRemoteServer(request->host, server_port);
    if (remoteSocketID < 0)
        return -1;
    int bytes_send = send(remoteSocketID, buf, strlen(buf), 0);
    bzero(buf, MAX_BYTES);
    bytes_send = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);
    char *temp_buffer = (char *)malloc(sizeof(char) * MAX_BYTES);
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;
    while (bytes_send > 0)
    {
        bytes_send = send(clientSocket, buf, bytes_send, 0);
        for (int i = 0; i < bytes_send / sizeof(char); i++)
        {
            temp_buffer[temp_buffer_index] = buf[i];
            temp_buffer_index++;
        }
        temp_buffer_size += MAX_BYTES;
        temp_buffer = (char *)realloc(temp_buffer, temp_buffer_size);
        if (bytes_send < 0)
        {
            perror("Error in sending data to client socket.\n");
            break;
        }
        bzero(buf, MAX_BYTES);
        bytes_send = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);
    }
    temp_buffer[temp_buffer_index] = '\0';
    free(buf);
    add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
    printf("Done\n");
    fflush(stdout);
    free(temp_buffer);
    close(remoteSocketID);
    return 0;
}
