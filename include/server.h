#ifndef WHEELMQ_SERVER_H
#define WHEELMQ_SERVER_H

#include "hashring.h"
#include "queue.h"
#include "wheel.h"
#include <stdint.h>
#include <pthread.h>

#define MAX_QUEUES 256

typedef struct {
    char node_id[HASHRING_NODE_ID_LEN];
    char bind_ip[64];
    uint16_t port;
    char unix_socket_path[128]; // If set, binds to UNIX domain socket

    hashring_t ring;
    timing_wheel_t wheel;

    queue_t queues[MAX_QUEUES];
    size_t queue_count;

    pthread_mutex_t lock;
    pthread_t ticker_thread;
    volatile bool running;

    int server_fd;
    uint64_t next_msg_id;
} wheelmq_server_t;

// Initialize server instance
int server_init(wheelmq_server_t *srv, const char *node_id, const char *bind_ip, uint16_t port, const char *unix_socket_path);

// Run the server event loop (blocks until stopped)
int server_run(wheelmq_server_t *srv);

// Stop the server
void server_stop(wheelmq_server_t *srv);

// Free all resources
void server_destroy(wheelmq_server_t *srv);

#endif // WHEELMQ_SERVER_H
