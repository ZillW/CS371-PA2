/*
 * CS 371 - PA2 Task 2 (UDP + Sequence Number + ARQ)
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
#include <errno.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define DEFAULT_CLIENT_THREADS 4
#define HEADER_SIZE 8   // 4 bytes for client_id, 4 bytes for sequence number
#define FRAME_SIZE (HEADER_SIZE + MESSAGE_SIZE)

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 100000;


typedef struct {
    int epoll_fd;                     
    int socket_fd;                  
    long long total_rtt;           
    long total_messages;            
    float request_rate;             
    long tx_cnt;                     
    long rx_cnt;                     
    int client_id;                    
    struct sockaddr_in server_addr;  
} client_thread_data_t;

//Fill packet header
void fill_header(char *buf, int client_id, int seq_num) {
    memcpy(buf, &client_id, 4);
    memcpy(buf + 4, &seq_num, 4);
}

//Parse packet header
void parse_header(char *buf, int *client_id, int *seq_num) {
    memcpy(client_id, buf, 4);
    memcpy(seq_num, buf + 4, 4);
}


void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    struct epoll_event event, events[MAX_EVENTS];
    char send_buf[FRAME_SIZE];
    char recv_buf[FRAME_SIZE];
    struct timeval start, end;
    socklen_t addrlen = sizeof(data->server_addr);

    event.data.fd = data->socket_fd;
    event.events = EPOLLIN;
    if (epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->socket_fd, &event) < 0) {
        perror("epoll_ctl failed");
        pthread_exit(NULL);
    }

    for (int seq = 0; seq < num_requests; seq++) {
        fill_header(send_buf, data->client_id, seq);
        memset(send_buf + HEADER_SIZE, 'A', MESSAGE_SIZE);

        int acknowledged = 0;
        int retries = 0;

        while (!acknowledged) {
            gettimeofday(&start, NULL);
            if (sendto(data->socket_fd, send_buf, FRAME_SIZE, 0,
                       (struct sockaddr *)&data->server_addr, addrlen) < 0) {
                perror("sendto failed");
                continue;
            }
            data->tx_cnt++;

            int nfds = epoll_wait(data->epoll_fd, events, MAX_EVENTS, 100); // 100ms timeout
            if (nfds > 0) {
                ssize_t len = recvfrom(data->socket_fd, recv_buf, FRAME_SIZE, 0, NULL, NULL);
                if (len >= HEADER_SIZE) {
                    int cid, sid;
                    parse_header(recv_buf, &cid, &sid);
                    if (cid == data->client_id && sid == seq) {
                        gettimeofday(&end, NULL);
                        long long rtt = (end.tv_sec - start.tv_sec) * 1000000LL +
                                        (end.tv_usec - start.tv_usec);
                        data->total_rtt += rtt;
                        data->total_messages++;
                        data->rx_cnt++;
                        acknowledged = 1;
                        break;
                    }
                }
            }

            retries++;
            if (retries > 10) {
                printf("[Client %d] Packet %d failed after 10 retries.\n", data->client_id, seq);
                break;
            }
        }
    }

    data->request_rate = (float)data->total_messages / (data->total_rtt / 1000000.0);
    printf("[Client %d] Sent: %ld, Received: %ld, Lost: %ld\n",
           data->client_id, data->tx_cnt, data->rx_cnt, data->tx_cnt - data->rx_cnt);
    return NULL;
}


void run_client() {
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];

    for (int i = 0; i < num_client_threads; i++) {
        thread_data[i].socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (thread_data[i].socket_fd < 0) {
            perror("socket failed");
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
        thread_data[i].client_id = i;

        pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
    }

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

    printf("\n=== Summary ===\n");
    printf("Total Sent: %ld, Received: %ld, Lost: %ld\n", total_tx, total_rx, total_tx - total_rx);
    printf("Average RTT: %lld us\n", total_messages > 0 ? total_rtt / total_messages : 0);
    printf("Total Request Rate: %f messages/s\n", total_request_rate);

    //Proper cleanup
    for (int i = 0; i < num_client_threads; i++) {
        close(thread_data[i].socket_fd);
        close(thread_data[i].epoll_fd);
    }
}


void run_server() {
    int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    char buffer[FRAME_SIZE];

    while (1) {
        ssize_t len = recvfrom(server_fd, buffer, FRAME_SIZE, 0,
                               (struct sockaddr *)&client_addr, &client_len);
        if (len >= HEADER_SIZE) {
            sendto(server_fd, buffer, FRAME_SIZE, 0,
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

