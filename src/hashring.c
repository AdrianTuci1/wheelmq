#include "hashring.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// 64-bit MurmurHash3
uint64_t hashring_hash(const void *key, size_t len, uint64_t seed) {
    const uint8_t *data = (const uint8_t *)key;
    const size_t nblocks = len / 8;
    uint64_t h = seed ^ (len * 0xc6a4a7935bd1e995ULL);

    const uint64_t c1 = 0xc6a4a7935bd1e995ULL;
    const int r1 = 47;

    for (size_t i = 0; i < nblocks; i++) {
        uint64_t k;
        memcpy(&k, data + i * 8, sizeof(uint64_t));

        k *= c1;
        k ^= k >> r1;
        k *= c1;

        h ^= k;
        h *= c1;
    }

    const uint8_t *tail = data + nblocks * 8;
    switch (len & 7) {
        case 7: h ^= (uint64_t)(tail[6]) << 48; // fallthrough
        case 6: h ^= (uint64_t)(tail[5]) << 40; // fallthrough
        case 5: h ^= (uint64_t)(tail[4]) << 32; // fallthrough
        case 4: h ^= (uint64_t)(tail[3]) << 24; // fallthrough
        case 3: h ^= (uint64_t)(tail[2]) << 16; // fallthrough
        case 2: h ^= (uint64_t)(tail[1]) << 8;  // fallthrough
        case 1: h ^= (uint64_t)(tail[0]);
                h *= c1;
    }

    h ^= h >> r1;
    h *= c1;
    h ^= h >> r1;

    return h;
}

static int compare_vnodes(const void *a, const void *b) {
    const vnode_t *va = (const vnode_t *)a;
    const vnode_t *vb = (const vnode_t *)b;
    if (va->hash_point < vb->hash_point) return -1;
    if (va->hash_point > vb->hash_point) return 1;
    return 0;
}

static void rebuild_vnodes(hashring_t *ring) {
    ring->vnode_count = 0;

    char vnode_buffer[256];
    for (uint32_t i = 0; i < ring->node_count; i++) {
        if (!ring->nodes[i].is_alive) {
            continue;
        }

        for (uint32_t v = 0; v < HASHRING_VNODES_PER_NODE; v++) {
            if (ring->vnode_count >= HASHRING_MAX_VNODES) break;

            int len = snprintf(vnode_buffer, sizeof(vnode_buffer), "%s#%u", ring->nodes[i].id, v);
            uint64_t h = hashring_hash(vnode_buffer, (size_t)len, 0x12345678);

            ring->vnodes[ring->vnode_count].hash_point = h;
            ring->vnodes[ring->vnode_count].node_index = i;
            ring->vnode_count++;
        }
    }

    if (ring->vnode_count > 0) {
        qsort(ring->vnodes, ring->vnode_count, sizeof(vnode_t), compare_vnodes);
    }
}

void hashring_init(hashring_t *ring) {
    memset(ring, 0, sizeof(hashring_t));
}

int hashring_add_node(hashring_t *ring, const char *id, const char *addr, uint16_t port) {
    if (ring->node_count >= HASHRING_MAX_NODES) return -1;

    for (uint32_t i = 0; i < ring->node_count; i++) {
        if (strncmp(ring->nodes[i].id, id, HASHRING_NODE_ID_LEN) == 0) {
            // Already exists, reactivate
            ring->nodes[i].is_alive = true;
            strncpy(ring->nodes[i].address, addr, sizeof(ring->nodes[i].address) - 1);
            ring->nodes[i].port = port;
            rebuild_vnodes(ring);
            return (int)i;
        }
    }

    uint32_t idx = ring->node_count++;
    strncpy(ring->nodes[idx].id, id, HASHRING_NODE_ID_LEN - 1);
    ring->nodes[idx].id[HASHRING_NODE_ID_LEN - 1] = '\0';
    strncpy(ring->nodes[idx].address, addr, sizeof(ring->nodes[idx].address) - 1);
    ring->nodes[idx].address[sizeof(ring->nodes[idx].address) - 1] = '\0';
    ring->nodes[idx].port = port;
    ring->nodes[idx].is_alive = true;

    rebuild_vnodes(ring);
    return (int)idx;
}

bool hashring_remove_node(hashring_t *ring, const char *id) {
    for (uint32_t i = 0; i < ring->node_count; i++) {
        if (strncmp(ring->nodes[i].id, id, HASHRING_NODE_ID_LEN) == 0) {
            // Shift remaining nodes
            for (uint32_t j = i; j < ring->node_count - 1; j++) {
                ring->nodes[j] = ring->nodes[j + 1];
            }
            ring->node_count--;
            rebuild_vnodes(ring);
            return true;
        }
    }
    return false;
}

bool hashring_set_node_status(hashring_t *ring, const char *id, bool is_alive) {
    for (uint32_t i = 0; i < ring->node_count; i++) {
        if (strncmp(ring->nodes[i].id, id, HASHRING_NODE_ID_LEN) == 0) {
            if (ring->nodes[i].is_alive != is_alive) {
                ring->nodes[i].is_alive = is_alive;
                rebuild_vnodes(ring);
            }
            return true;
        }
    }
    return false;
}

const cluster_node_t *hashring_lookup(const hashring_t *ring, const char *key, size_t key_len) {
    if (ring->vnode_count == 0) return NULL;

    uint64_t key_hash = hashring_hash(key, key_len, 0x12345678);

    // Binary search for first vnode with hash_point >= key_hash
    uint32_t low = 0;
    uint32_t high = ring->vnode_count;

    while (low < high) {
        uint32_t mid = low + (high - low) / 2;
        if (ring->vnodes[mid].hash_point < key_hash) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    // If beyond the end of the ring, wrap around to first vnode (clock ring)
    if (low == ring->vnode_count) {
        low = 0;
    }

    uint32_t node_idx = ring->vnodes[low].node_index;
    return &ring->nodes[node_idx];
}
