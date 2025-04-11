/*
 * CS 371 - PA2 Task 1
 * Author: Zheer Wang
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define DEFAULT_CLIENT_THREADS 4

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;

typedef struct {
    int epoll_fd;            
    int socket_fd;           
    long long total_rtt;   
    long total_messages;     
    float request_rate;      
    long tx_cnt;            
    long rx_cnt;            
    struct sockaddr_in server_addr; 
} client_thread_data_t;


void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    struct epoll_event event, events[MAX_EVENTS];
    char send_buf[MESSAGE_SIZE] = "ABCDEFGHIJKMLNOP";
    char recv_buf[MESSAGE_SIZE];
    struct timeval start, end;
    socklen_t addrlen = sizeof(data->server_addr);

    //Register socket with epoll
    event.data.fd = data->socket_fd;
    event.events = EPOLLIN;
    if (epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->socket_fd, &event) < 0) {
        perror("epoll_ctl failed");
        pthread_exit(NULL);
    }

   //Begin request loop
    for (int i = 0; i < num_requests; i++) {
        gettimeofday(&start, NULL);
        if (sendto(data->socket_fd, send_buf, MESSAGE_SIZE, 0,
                   (struct sockaddr *)&data->server_addr, addrlen) < 0) {
            perror("sendto failed");
            continue;
        }
        data->tx_cnt++;

        int nfds = epoll_wait(data->epoll_fd, events, MAX_EVENTS, 100); //timeout of 100ms
        if (nfds > 0) {
            if (recvfrom(data->socket_fd, recv_buf, MESSAGE_SIZE, 0, NULL, NULL) > 0) {
                gettimeofday(&end, NULL);
                data->rx_cnt++;
                long long rtt = (end.tv_sec - start.tv_sec) * 1000000LL +
                                (end.tv_usec - start.tv_usec);
                data->total_rtt += rtt;
                data->total_messages++;
            }
        }
    }

    data->request_rate = (float)data->total_messages / (data->total_rtt / 1000000.0);
    printf("Thread sent: %ld, received: %ld, lost: %ld\n",
           data->tx_cnt, data->rx_cnt, data->tx_cnt - data->rx_cnt);
    return NULL;
}

//Launch all client threads
void run_client() {
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];

    for (int i = 0; i < num_client_threads; i++) {
        thread_data[i].socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (thread_data[i].socket_fd < 0) {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

        thread_data[i].epoll_fd = epoll_create1(0);
        if (thread_data[i].epoll_fd < 0) {
            perror("epoll_create1 failed");
            close(thread_data[i].socket_fd);
            exit(EXIT_FAILURE);
        }

        memset(&thread_data[i].server_addr, 0, sizeof(struct sockaddr_in));
        thread_data[i].server_addr.sin_family = AF_INET;
        thread_data[i].server_addr.sin_port = htons(server_port);
        inet_pton(AF_INET, server_ip, &thread_data[i].server_addr.sin_addr);

        thread_data[i].total_rtt = 0;
        thread_data[i].total_messages = 0;
        thread_data[i].tx_cnt = 0;
        thread_data[i].rx_cnt = 0;

        pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
    }

    //Aggregate results
    long long total_rtt = 0;
    long total_messages = 0;
    float total_request_rate = 0.0;
    long total_tx = 0, total_rx = 0;

    for (int i = 0; i < num_client_threads; i++) {
        pthread_join(threads[i], NULL);
        total_rtt += thread_data[i].total_rtt;
        total_messages += thread_data[i].total_messages;
        total_request_rate += thread_data[i].request_rate;
        total_tx += thread_data[i].tx_cnt;
        total_rx += thread_data[i].rx_cnt;
    }

    printf("Average RTT: %lld us\n", total_messages > 0 ? total_rtt / total_messages : 0);
    printf("Total Request Rate: %f messages/s\n", total_request_rate);
    printf("Total Sent: %ld, Received: %ld, Lost: %ld\n",
           total_tx, total_rx, total_tx - total_rx);

    //close file descriptors
    for (int i = 0; i < num_client_threads; i++) {
        close(thread_data[i].socket_fd);
        close(thread_data[i].epoll_fd);
    }
}


void run_server() {
    int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr, client_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    char buffer[MESSAGE_SIZE];
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        ssize_t len = recvfrom(server_fd, buffer, MESSAGE_SIZE, 0,
                               (struct sockaddr *)&client_addr, &client_len);
        if (len > 0) {
	    usleep(1000);
            sendto(server_fd, buffer, MESSAGE_SIZE, 0,
                   (struct sockaddr *)&client_addr, client_len);
        }
    }

    close(server_fd);
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "server") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);
        run_server();
    } else if (argc > 1 && strcmp(argv[1], "client") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);
        if (argc > 4) num_client_threads = atoi(argv[4]);
        if (argc > 5) num_requests = atoi(argv[5]);
        run_client();
    } else {
        printf("Usage: %s <server|client> [server_ip server_port num_client_threads num_requests]\n", argv[0]);
    }
    return 0;
}

