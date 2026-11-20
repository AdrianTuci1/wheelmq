#ifndef WHEELMQ_SERVER_H
#define WHEELMQ_SERVER_H

#include "hashring.h"
#include "queue.h"
#include "wheel.h"
#include <stdint.h>
#include <pthread.h>

/**
 * ============================================================================
 * ARCHITECTURAL DESIGN DECISIONS & THEORETICAL FOUNDATION: SERVER & PROTOCOL
 * ============================================================================
 *
 * 1. BUFFER SIZING (SERVER_IO_BUF_SIZE = 65536 / 64 KB):
 *    - TCP Window Alignment: 64 KB aligns with the standard unscaled TCP
 *      receive window size (RFC 793 / RFC 1323) across POSIX operating systems.
 *    - Zero Packet Fragmentation: Enables sending large structured JSON payloads,
 *      telemetry bursts, and serialized objects up to 64 KB in a single read()
 *      system call without fragmented buffer reassembly overhead.
 *
 * 2. WIRE PROTOCOL DESIGN (LINE-DELIMITED TEXT / REDIS & BEANSTALKD STYLE):
 *    - Minimalist Ergonomics: Commands use human-readable ASCII text (PUSH, POP,
 *      FAIL, DLQ_LIST, DLQ_REPLAY, STATS, JOIN).
 *    - Observability & Zero-Tooling Debugging: Engineers can interact with,
 *      benchmark, or inspect wheelmq directly using standard tools like 'nc',
 *      'telnet', or 'socat' without requiring complex binary schema compilers
 *      (e.g., Protobuf or Thrift).
 *    - Parsing Performance: Token extraction is implemented using non-allocating
 *      in-place pointer traversal over the contiguous socket buffer.
 *
 * 3. DUAL-MODE IPC (TCP & UNIX DOMAIN SOCKETS):
 *    - TCP Mode: Used for cross-node network clustering and remote producer/worker
 *      communication across datacenters.
 *    - UNIX Domain Socket Mode (AF_UNIX): When running as a local sidecar or on
 *      the same host as workers, UNIX sockets bypass the loopback IP stack,
 *      avoiding TCP checksums, packet headers, and routing tables, delivering
 *      up to 2x higher throughput with lower latency.
 * ============================================================================
 */

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
