#ifndef WHEELMQ_SERVER_H
#define WHEELMQ_SERVER_H

#include "hashring.h"
#include "queue.h"
#include "wheel.h"
#include <stdint.h>
#include <pthread.h>

/**
 * Maximum number of independent queues managed by this node instance.
 */
#define MAX_QUEUES 256

/**
 * wheelmq server daemon instance state.
 */
typedef struct {
    char node_id[HASHRING_NODE_ID_LEN];  /**< Unique cluster node identifier */
    char bind_ip[64];                    /**< TCP bind IP address (e.g. 127.0.0.1 or 0.0.0.0) */
    uint16_t port;                       /**< TCP bind port (e.g. 7000) */
    char unix_socket_path[128];          /**< Optional UNIX domain socket path */

    hashring_t ring;                     /**< Consistent Hash Ring for cluster topology */
    timing_wheel_t wheel;                /**< Hierarchical Timing Wheel for delayed tasks */

    queue_t queues[MAX_QUEUES];          /**< Managed queue containers */
    size_t queue_count;                  /**< Active queue count */

    pthread_mutex_t lock;                /**< Re-entrant lock for thread-safe state access */
    pthread_t ticker_thread;             /**< Background thread driving timing wheel ticks */
    volatile bool running;               /**< Daemon lifecycle flag */

    int server_fd;                       /**< Listening socket file descriptor */
    uint64_t next_msg_id;                /**< Monotonically increasing message ID generator */
} wheelmq_server_t;

/**
 * Initialize a wheelmq server instance.
 *
 * @param srv Pointer to wheelmq_server_t structure.
 * @param node_id Unique cluster node identifier.
 * @param bind_ip IP address to listen on (ignored if unix_socket_path is set).
 * @param port TCP port to listen on.
 * @param unix_socket_path Path to UNIX domain socket (or empty string for TCP mode).
 * @return 0 on success, -1 on failure.
 */
int server_init(wheelmq_server_t *srv, const char *node_id, const char *bind_ip, uint16_t port, const char *unix_socket_path);

/**
 * Start the server network event loop and background ticker thread.
 * Blocks until server_stop is called or process receives termination signal.
 *
 * @param srv Pointer to wheelmq_server_t structure.
 * @return 0 on clean shutdown, -1 on socket error.
 */
int server_run(wheelmq_server_t *srv);

/**
 * Gracefully stop the server, shutting down the listening socket and joining ticker thread.
 *
 * @param srv Pointer to wheelmq_server_t structure.
 */
void server_stop(wheelmq_server_t *srv);

/**
 * Clean up all server memory, destroy queues and timing wheels.
 *
 * @param srv Pointer to wheelmq_server_t structure.
 */
void server_destroy(wheelmq_server_t *srv);

#endif // WHEELMQ_SERVER_H
