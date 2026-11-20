#ifndef WHEELMQ_HASHRING_H
#define WHEELMQ_HASHRING_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Maximum number of physical cluster nodes supported.
 */
#define HASHRING_MAX_NODES 64

/**
 * Number of virtual nodes (vnodes) per physical node.
 * Based on the Amazon Dynamo paper: 100-200 vnodes per host ensures
 * a uniform key distribution with < 5% standard deviation across nodes.
 */
#define HASHRING_VNODES_PER_NODE 128

/**
 * Total maximum virtual nodes on the ring.
 */
#define HASHRING_MAX_VNODES (HASHRING_MAX_NODES * HASHRING_VNODES_PER_NODE)

/**
 * Maximum string length for a cluster node identifier.
 */
#define HASHRING_NODE_ID_LEN 64

/**
 * Represents a physical cluster node.
 */
typedef struct {
    char id[HASHRING_NODE_ID_LEN];  /**< Unique identifier of the node (e.g. "node-1") */
    char address[128];              /**< IP address or hostname */
    uint16_t port;                  /**< TCP port number */
    bool is_alive;                  /**< Health flag: true if healthy, false if partitioned/dead */
} cluster_node_t;

/**
 * Represents a virtual node on the 64-bit consistent hash ring.
 */
typedef struct {
    uint64_t hash_point;            /**< Position on the [0, 2^64 - 1] ring */
    uint32_t node_index;            /**< Index pointing to the parent physical node */
} vnode_t;

/**
 * Consistent Hash Ring structure.
 */
typedef struct {
    cluster_node_t nodes[HASHRING_MAX_NODES]; /**< Registered physical nodes */
    uint32_t node_count;                      /**< Total physical nodes registered */

    vnode_t vnodes[HASHRING_MAX_VNODES];      /**< Sorted array of virtual nodes */
    uint32_t vnode_count;                     /**< Total virtual nodes active on the ring */
} hashring_t;

/**
 * Initialize an empty hash ring.
 *
 * @param ring Pointer to the hashring_t structure to initialize.
 */
void hashring_init(hashring_t *ring);

/**
 * Add a physical node to the ring and scatter its virtual nodes across the 64-bit ring.
 *
 * @param ring Pointer to the hashring_t structure.
 * @param id Unique identifier string for the node.
 * @param addr IP address or hostname of the node.
 * @param port TCP port number.
 * @return Physical node index on success, or -1 if the ring is at maximum capacity.
 */
int hashring_add_node(hashring_t *ring, const char *id, const char *addr, uint16_t port);

/**
 * Remove a physical node from the ring and remove all its associated virtual nodes.
 *
 * @param ring Pointer to the hashring_t structure.
 * @param id Unique identifier string of the node to remove.
 * @return true if found and removed, false otherwise.
 */
bool hashring_remove_node(hashring_t *ring, const char *id);

/**
 * Update the health status of a node. When marked dead (false), its vnodes are
 * excluded from routing without removing node registration.
 *
 * @param ring Pointer to the hashring_t structure.
 * @param id Unique identifier string of the node.
 * @param is_alive true for healthy, false for failed/unreachable.
 * @return true if node was found and updated, false otherwise.
 */
bool hashring_set_node_status(hashring_t *ring, const char *id, bool is_alive);

/**
 * Look up which physical node owns a given key using binary search on the sorted ring.
 *
 * @param ring Pointer to the hashring_t structure.
 * @param key The key string (e.g. queue name or job ID).
 * @param key_len Length of the key in bytes.
 * @return Pointer to the owning cluster_node_t, or NULL if no healthy nodes exist.
 */
const cluster_node_t *hashring_lookup(const hashring_t *ring, const char *key, size_t key_len);

/**
 * 64-bit MurmurHash3 algorithm implementation.
 * Provides uniform avalanche characteristics and high hashing speed.
 *
 * @param key Pointer to data to hash.
 * @param len Length of data in bytes.
 * @param seed Randomization seed.
 * @return 64-bit hash integer.
 */
uint64_t hashring_hash(const void *key, size_t len, uint64_t seed);

#endif // WHEELMQ_HASHRING_H
