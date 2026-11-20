#ifndef WHEELMQ_HASHRING_H
#define WHEELMQ_HASHRING_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define HASHRING_MAX_NODES 64
#define HASHRING_VNODES_PER_NODE 128
#define HASHRING_MAX_VNODES (HASHRING_MAX_NODES * HASHRING_VNODES_PER_NODE)
#define HASHRING_NODE_ID_LEN 64

typedef struct {
    char id[HASHRING_NODE_ID_LEN];
    char address[128];
    uint16_t port;
    bool is_alive;
} cluster_node_t;

typedef struct {
    uint64_t hash_point;
    uint32_t node_index;
} vnode_t;

typedef struct {
    cluster_node_t nodes[HASHRING_MAX_NODES];
    uint32_t node_count;

    vnode_t vnodes[HASHRING_MAX_VNODES];
    uint32_t vnode_count;
} hashring_t;

// Initialize an empty hash ring
void hashring_init(hashring_t *ring);

// Add a physical node to the ring (returns node_index on success, -1 on failure)
int hashring_add_node(hashring_t *ring, const char *id, const char *addr, uint16_t port);

// Remove a physical node by id
bool hashring_remove_node(hashring_t *ring, const char *id);

// Mark a node alive/dead
bool hashring_set_node_status(hashring_t *ring, const char *id, bool is_alive);

// Find which node owns a given key (e.g. queue name or job id)
// Returns pointer to cluster_node_t or NULL if no alive nodes
const cluster_node_t *hashring_lookup(const hashring_t *ring, const char *key, size_t key_len);

// 64-bit MurmurHash3 implementation
uint64_t hashring_hash(const void *key, size_t len, uint64_t seed);

#endif // WHEELMQ_HASHRING_H
